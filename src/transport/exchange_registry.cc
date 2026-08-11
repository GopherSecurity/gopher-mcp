/**
 * Per-connection bookkeeping for in-flight exchanges. See the header.
 */

#include "mcp/transport/exchange_registry.h"

#include <algorithm>
#include <cassert>

#include "mcp/logging/log_macros.h"

namespace mcp {
namespace transport {

namespace {

/**
 * Carries the last reference to an exchange into the dispatcher's deferred
 * delete queue, so it is destroyed after the current callback has unwound
 * rather than inside it.
 */
struct ExchangeHolder : public event::DeferredDeletable {
  explicit ExchangeHolder(RequestExchangePtr held)
      : exchange(std::move(held)) {}
  RequestExchangePtr exchange;
};

// Whether an exchange is answering the given request id.
bool answers(const RequestExchangePtr& exchange, const RequestIdKey& key) {
  if (!exchange || !exchange->requestId().has_value()) {
    return false;
  }
  return requestIdKey(exchange->requestId().value()) == key;
}

}  // namespace

// ===== ExchangeRegistry =====

ExchangeRegistry::ExchangeRegistry(event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

ExchangeRegistry::~ExchangeRegistry() = default;

void ExchangeRegistry::assertOnDispatcher() const {
  assert(dispatcher_.isThreadSafe() &&
         "ExchangeRegistry used off its dispatcher thread");
}

void ExchangeRegistry::add(const RequestExchangePtr& exchange) {
  assertOnDispatcher();
  if (!exchange) {
    return;
  }
  exchanges_.push_back(exchange);
}

void ExchangeRegistry::remove(const RequestExchangePtr& exchange) {
  assertOnDispatcher();
  exchanges_.erase(std::remove(exchanges_.begin(), exchanges_.end(), exchange),
                   exchanges_.end());
}

void ExchangeRegistry::reapCompleted() {
  assertOnDispatcher();
  exchanges_.erase(std::remove_if(exchanges_.begin(), exchanges_.end(),
                                  [](const RequestExchangePtr& exchange) {
                                    return !exchange ||
                                           exchange->mode() ==
                                               RequestExchange::Mode::Complete;
                                  }),
                   exchanges_.end());
}

RequestExchangePtr ExchangeRegistry::find(const RequestIdKey& key) const {
  assertOnDispatcher();
  for (const auto& exchange : exchanges_) {
    if (answers(exchange, key)) {
      return exchange;
    }
  }
  return nullptr;
}

bool ExchangeRegistry::hasActiveStream() const {
  assertOnDispatcher();
  for (const auto& exchange : exchanges_) {
    if (exchange && exchange->mode() == RequestExchange::Mode::Stream) {
      return true;
    }
  }
  return false;
}

void ExchangeRegistry::setWriteInProgress(bool in_progress) {
  assertOnDispatcher();
  for (const auto& exchange : exchanges_) {
    if (exchange) {
      exchange->setWriteInProgress(in_progress);
    }
  }
}

std::vector<RequestExchangePtr> ExchangeRegistry::onConnectionGone() {
  assertOnDispatcher();

  std::vector<RequestExchangePtr> survivors;
  // Move the list out first: telling an exchange its connection is gone can
  // run observers, and those must not see a half-emptied registry.
  std::vector<RequestExchangePtr> exchanges;
  exchanges.swap(exchanges_);

  for (auto& exchange : exchanges) {
    if (!exchange) {
      continue;
    }
    if (exchange->onConnectionGone()) {
      survivors.push_back(exchange);
    }
  }

  if (!survivors.empty()) {
    GOPHER_LOG_DEBUG("{} exchange(s) outlived their connection",
                     survivors.size());
  }
  return survivors;
}

// ===== RetainedExchangeStore =====

RetainedExchangeStore::RetainedExchangeStore(event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

RetainedExchangeStore::~RetainedExchangeStore() {
  running_ = false;
  if (expiry_timer_) {
    // Disable before anything else goes away, so a pending fire cannot run
    // against a half-destroyed store.
    expiry_timer_->disableTimer();
  }
}

void RetainedExchangeStore::assertOnDispatcher() const {
  assert(dispatcher_.isThreadSafe() &&
         "RetainedExchangeStore used off its dispatcher thread");
}

void RetainedExchangeStore::retain(const RequestExchangePtr& exchange) {
  assertOnDispatcher();
  if (!exchange) {
    return;
  }
  exchanges_.push_back(exchange);

  if (exchange->mode() == RequestExchange::Mode::Complete) {
    // Already finished when it got here; the clock starts now.
    scheduleRelease(exchange);
    return;
  }

  // Otherwise wait until it stops producing. Weak, so the exchange holding
  // this callback does not hold itself alive through it.
  std::weak_ptr<RequestExchange> weak = exchange;
  exchange->setCompletionObserver([this, weak]() {
    auto finished = weak.lock();
    if (finished) {
      scheduleRelease(finished);
    }
  });
}

void RetainedExchangeStore::release(const RequestExchangePtr& exchange) {
  assertOnDispatcher();
  exchanges_.erase(std::remove(exchanges_.begin(), exchanges_.end(), exchange),
                   exchanges_.end());
}

RequestExchangePtr RetainedExchangeStore::find(const RequestIdKey& key) const {
  assertOnDispatcher();
  for (const auto& exchange : exchanges_) {
    if (answers(exchange, key)) {
      return exchange;
    }
  }
  return nullptr;
}

void RetainedExchangeStore::scheduleRelease(
    const RequestExchangePtr& exchange) {
  assertOnDispatcher();
  if (!exchange || !running_) {
    return;
  }

  const auto deadline = std::chrono::steady_clock::now() + retention_;
  expiring_.emplace_back(exchange, deadline);

  if (!expiry_timer_) {
    expiry_timer_ = dispatcher_.createTimer([this]() { releaseExpired(); });
  }
  // One timer re-armed as needed rather than one per exchange: a retained
  // exchange is a rare thing, and a timer each would be a lot of machinery
  // for something that only has to be roughly on time.
  armExpiryTimer();
}

void RetainedExchangeStore::armExpiryTimer() {
  assertOnDispatcher();
  if (!expiry_timer_ || expiring_.empty()) {
    return;
  }

  // The soonest of them, not a fresh full window. Arming for the full
  // retention on every arrival is what let a steady trickle of them push
  // the timer out ahead of the oldest for as long as the trickle lasted,
  // so nothing was ever released and "how long this is kept" meant
  // nothing.
  auto soonest = expiring_.front().second;
  for (const auto& entry : expiring_) {
    if (entry.second < soonest) {
      soonest = entry.second;
    }
  }

  const auto now = std::chrono::steady_clock::now();
  const auto wait = soonest <= now
                        ? std::chrono::milliseconds(0)
                        : std::chrono::duration_cast<std::chrono::milliseconds>(
                              soonest - now);
  expiry_timer_->enableTimer(wait);
}

void RetainedExchangeStore::releaseExpired() {
  assertOnDispatcher();
  if (!running_) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();

  // Collect what has run out first, and let go of it only after the list is
  // consistent: releasing an exchange runs its destructor, which is not
  // something to do while walking the container it lives in.
  std::vector<RequestExchangePtr> expired;
  auto it = expiring_.begin();
  while (it != expiring_.end()) {
    if (it->second <= now) {
      expired.push_back(it->first);
      it = expiring_.erase(it);
    } else {
      ++it;
    }
  }

  for (const auto& exchange : expired) {
    GOPHER_LOG_DEBUG("Releasing a retained exchange nobody came back for");
    release(exchange);
  }
  // Hand the last references to the dispatcher rather than dropping them
  // here: this runs inside a timer callback, and destroying an object from
  // inside the callback that is still executing on its behalf is how
  // use-after-free happens.
  for (auto& exchange : expired) {
    dispatcher_.deferredDelete(
        event::DeferredDeletablePtr(new ExchangeHolder(std::move(exchange))));
  }
  expired.clear();

  // Something may still be waiting; come back when the nearest is due.
  armExpiryTimer();
}

void RetainedExchangeStore::clear() {
  assertOnDispatcher();
  expiring_.clear();
  if (expiry_timer_) {
    expiry_timer_->disableTimer();
  }
  exchanges_.clear();
}

}  // namespace transport
}  // namespace mcp
