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

RetainedExchangeStore::~RetainedExchangeStore() = default;

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

void RetainedExchangeStore::clear() {
  assertOnDispatcher();
  exchanges_.clear();
}

}  // namespace transport
}  // namespace mcp
