/**
 * @file test_exchange_registry.cc
 * @brief Tests for per-connection exchange bookkeeping
 *
 * The registry answers two questions a connection has to answer about
 * itself: whether a response is currently streaming, and what becomes of
 * work still in progress when the connection dies. Both matter because
 * HTTP/1.1 delivers responses in request order, so a connection that is
 * mid-stream cannot answer anything else yet.
 */

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/transport/exchange_registry.h"

namespace mcp {
namespace transport {
namespace {

class ExchangeRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    factory_ = event::createLibeventDispatcherFactory();
    dispatcher_ = factory_->createDispatcher("registry_test");
    dispatcher_->run(event::RunType::NonBlock);
    registry_.reset(new ExchangeRegistry(*dispatcher_));
    store_.reset(new RetainedExchangeStore(*dispatcher_));
  }

  void TearDown() override {
    store_.reset();
    registry_.reset();
    dispatcher_.reset();
    factory_.reset();
  }

  RequestExchangePtr makeExchange(int64_t id) {
    std::unique_ptr<RetainedExchangeSink> sink(new RetainedExchangeSink());
    return RequestExchange::create(*dispatcher_, std::move(sink),
                                   optional<RequestId>(RequestId(id)));
  }

  // Drive the dispatcher on this thread so timers actually fire, without
  // handing the exchanges to a second thread they do not belong to.
  void runDispatcherFor(std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  RequestExchangePtr makeAnonymousExchange() {
    std::unique_ptr<RetainedExchangeSink> sink(new RetainedExchangeSink());
    return RequestExchange::create(*dispatcher_, std::move(sink), nullopt);
  }

  event::DispatcherFactoryPtr factory_;
  event::DispatcherPtr dispatcher_;
  std::unique_ptr<ExchangeRegistry> registry_;
  std::unique_ptr<RetainedExchangeStore> store_;
};

TEST_F(ExchangeRegistryTest, TracksAndFindsByRequestId) {
  auto first = makeExchange(1);
  auto second = makeExchange(2);
  registry_->add(first);
  registry_->add(second);

  EXPECT_EQ(registry_->size(), 2u);
  EXPECT_EQ(registry_->find(requestIdKey(RequestId(int64_t(2)))), second);
  EXPECT_EQ(registry_->find(requestIdKey(RequestId(int64_t(3)))), nullptr);
}

TEST_F(ExchangeRegistryTest, StringAndNumericIdsDoNotCollide) {
  // The whole reason the key is tagged rather than stringified.
  std::unique_ptr<RetainedExchangeSink> sink(new RetainedExchangeSink());
  auto text =
      RequestExchange::create(*dispatcher_, std::move(sink),
                              optional<RequestId>(RequestId(std::string("5"))));
  auto number = makeExchange(5);
  registry_->add(text);
  registry_->add(number);

  EXPECT_EQ(registry_->find(requestIdKey(RequestId(std::string("5")))), text);
  EXPECT_EQ(registry_->find(requestIdKey(RequestId(int64_t(5)))), number);
}

TEST_F(ExchangeRegistryTest, AnExchangeWithNoRequestIdIsNeverFound) {
  auto anonymous = makeAnonymousExchange();
  registry_->add(anonymous);

  EXPECT_EQ(registry_->size(), 1u);
  EXPECT_EQ(registry_->find(requestIdKey(RequestId(int64_t(1)))), nullptr);
}

TEST_F(ExchangeRegistryTest, RemoveAndReapDropExchanges) {
  auto first = makeExchange(1);
  auto second = makeExchange(2);
  registry_->add(first);
  registry_->add(second);

  registry_->remove(first);
  EXPECT_EQ(registry_->size(), 1u);
  // Removing something already gone is not an error.
  registry_->remove(first);
  EXPECT_EQ(registry_->size(), 1u);

  second->complete();
  registry_->reapCompleted();
  EXPECT_EQ(registry_->size(), 0u);
}

TEST_F(ExchangeRegistryTest, ReportsWhetherAResponseIsStreaming) {
  // What a connection consults before deciding whether an incoming request
  // can be answered now or has to wait its turn.
  auto unary = makeExchange(1);
  registry_->add(unary);
  EXPECT_FALSE(registry_->hasActiveStream());

  auto streaming = makeExchange(2);
  registry_->add(streaming);
  ASSERT_TRUE(streaming->beginStream());
  EXPECT_TRUE(registry_->hasActiveStream());

  streaming->complete();
  EXPECT_FALSE(registry_->hasActiveStream());
}

TEST_F(ExchangeRegistryTest, ConnectionDeathCancelsOrdinaryExchanges) {
  auto exchange = makeExchange(1);
  registry_->add(exchange);
  bool cancelled = false;
  exchange->cancellation().addObserver([&cancelled]() { cancelled = true; });

  auto survivors = registry_->onConnectionGone();

  EXPECT_TRUE(survivors.empty());
  EXPECT_TRUE(cancelled);
  EXPECT_EQ(registry_->size(), 0u);
}

TEST_F(ExchangeRegistryTest, RetainedExchangesAreHandedBackForSomeoneToHold) {
  auto ordinary = makeExchange(1);
  auto retained = makeExchange(2);
  retained->setRetainOnDisconnect(true);
  ASSERT_TRUE(retained->beginStream());

  registry_->add(ordinary);
  registry_->add(retained);

  auto survivors = registry_->onConnectionGone();

  ASSERT_EQ(survivors.size(), 1u);
  EXPECT_EQ(survivors[0], retained);
  EXPECT_TRUE(retained->detached());
  // The registry is emptied either way; it is about to be destroyed with
  // the connection.
  EXPECT_EQ(registry_->size(), 0u);
}

TEST_F(ExchangeRegistryTest, ASurvivorStaysUsableThroughTheStore) {
  // The registry dies with its connection, so a survivor has to be held by
  // something longer-lived or it is released at the moment it is rescued.
  auto retained = makeExchange(7);
  retained->setRetainOnDisconnect(true);
  ASSERT_TRUE(retained->beginStream());
  registry_->add(retained);

  auto survivors = registry_->onConnectionGone();
  ASSERT_EQ(survivors.size(), 1u);
  for (const auto& survivor : survivors) {
    store_->retain(survivor);
  }

  // Drop every other reference; the store is the only owner now.
  std::weak_ptr<RequestExchange> observer = retained;
  retained.reset();
  survivors.clear();

  ASSERT_FALSE(observer.expired());
  auto found = store_->find(requestIdKey(RequestId(int64_t(7))));
  ASSERT_TRUE(found);
  EXPECT_TRUE(found->writeEvent("message", "produced after the client left"));
  EXPECT_EQ(found->retainedEvents().size(), 1u);

  found.reset();
  store_->release(store_->find(requestIdKey(RequestId(int64_t(7)))));
  EXPECT_EQ(store_->size(), 0u);
  EXPECT_TRUE(observer.expired());
}

// ── Retention ──────────────────────────────────────────────────────────────

TEST_F(ExchangeRegistryTest, AnUnfinishedExchangeIsHeldIndefinitely) {
  // The window is for a client that might still come back, so it does not
  // start until there is a final answer to come back for.
  store_->setRetention(std::chrono::milliseconds(0));

  auto retained = makeExchange(1);
  retained->setRetainOnDisconnect(true);
  ASSERT_TRUE(retained->beginStream());
  ASSERT_TRUE(retained->onConnectionGone());
  store_->retain(retained);

  runDispatcherFor(std::chrono::milliseconds(50));
  EXPECT_EQ(store_->size(), 1u) << "work still in progress must not be reaped";
}

TEST_F(ExchangeRegistryTest, AFinishedExchangeIsReleasedAfterItsWindow) {
  store_->setRetention(std::chrono::milliseconds(10));

  auto retained = makeExchange(1);
  retained->setRetainOnDisconnect(true);
  ASSERT_TRUE(retained->beginStream());
  ASSERT_TRUE(retained->onConnectionGone());
  store_->retain(retained);

  std::weak_ptr<RequestExchange> observer = retained;
  retained.reset();
  ASSERT_EQ(store_->size(), 1u);

  // Finishing starts the clock.
  store_->find(requestIdKey(RequestId(int64_t(1))))->complete();
  EXPECT_EQ(store_->size(), 1u) << "the window has not elapsed yet";

  runDispatcherFor(std::chrono::milliseconds(200));

  EXPECT_EQ(store_->size(), 0u);
  EXPECT_TRUE(observer.expired())
      << "the exchange should be gone once nobody came back for it";
}

TEST_F(ExchangeRegistryTest, AnAlreadyFinishedExchangeStartsItsWindowAtOnce) {
  store_->setRetention(std::chrono::milliseconds(10));

  auto retained = makeExchange(1);
  retained->setRetainOnDisconnect(true);
  ASSERT_TRUE(retained->beginStream());
  ASSERT_TRUE(retained->onConnectionGone());
  retained->complete();
  store_->retain(retained);
  retained.reset();

  runDispatcherFor(std::chrono::milliseconds(200));
  EXPECT_EQ(store_->size(), 0u);
}

TEST_F(ExchangeRegistryTest, RetentionSurvivesTheStoreBeingTornDownFirst) {
  // A pending release must not fire against a store that is going away.
  store_->setRetention(std::chrono::milliseconds(10));

  auto retained = makeExchange(1);
  retained->setRetainOnDisconnect(true);
  ASSERT_TRUE(retained->beginStream());
  ASSERT_TRUE(retained->onConnectionGone());
  store_->retain(retained);
  retained->complete();

  store_.reset();
  runDispatcherFor(std::chrono::milliseconds(100));
  // Reaching here without a crash is the assertion.
  SUCCEED();
}

TEST_F(ExchangeRegistryTest, ClearingTheStoreReleasesEverything) {
  auto retained = makeExchange(1);
  retained->setRetainOnDisconnect(true);
  store_->retain(retained);

  std::weak_ptr<RequestExchange> observer = retained;
  retained.reset();
  ASSERT_FALSE(observer.expired());

  store_->clear();
  EXPECT_EQ(store_->size(), 0u);
  EXPECT_TRUE(observer.expired());
}

}  // namespace
}  // namespace transport
}  // namespace mcp
