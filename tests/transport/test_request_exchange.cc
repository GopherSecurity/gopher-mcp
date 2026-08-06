/**
 * @file test_request_exchange.cc
 * @brief Tests for the per-request runtime
 *
 * The point of the object is that it outlives the dispatch callback that
 * created it, so most of these tests keep a reference past the point where
 * a dispatch context would already be destroyed and then use it.
 *
 * Everything runs against a retaining sink rather than a socket: the
 * exchange has no connection reference of its own, which is what lets it
 * survive one.
 */

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/transport/request_exchange.h"

namespace mcp {
namespace transport {
namespace {

using namespace std::chrono_literals;

jsonrpc::Response okResponse(int64_t id) {
  jsonrpc::Response response;
  response.jsonrpc = "2.0";
  response.id = RequestId(id);
  response.result = mcp::make_optional(jsonrpc::ResponseResult(Metadata()));
  return response;
}

class RequestExchangeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    factory_ = event::createLibeventDispatcherFactory();
    dispatcher_ = factory_->createDispatcher("exchange_test");
    // Run non-blocking on this thread so this thread IS the dispatcher
    // thread and the affinity assertions are satisfied.
    dispatcher_->run(event::RunType::NonBlock);
  }

  void TearDown() override {
    dispatcher_.reset();
    factory_.reset();
  }

  // Build an exchange over a retaining sink and hand back both.
  RequestExchangePtr makeExchange(RetainedExchangeSink** sink_out = nullptr,
                                  size_t max_events = 256) {
    std::unique_ptr<RetainedExchangeSink> sink(
        new RetainedExchangeSink(max_events));
    if (sink_out != nullptr) {
      *sink_out = sink.get();
    }
    return RequestExchange::create(*dispatcher_, std::move(sink),
                                   optional<RequestId>(RequestId(int64_t(1))));
  }

  event::DispatcherFactoryPtr factory_;
  event::DispatcherPtr dispatcher_;
};

// ── Lifetime ───────────────────────────────────────────────────────────────

TEST_F(RequestExchangeTest, OutlivesTheDispatchCallbackAndStillWrites) {
  RetainedExchangeSink* sink = nullptr;
  RequestExchangePtr kept;

  // Stand in for a dispatch callback: the exchange is created inside it and
  // the only thing that leaves is a reference.
  {
    auto exchange = makeExchange(&sink);
    kept = exchange;
  }

  ASSERT_TRUE(kept);
  EXPECT_EQ(kept->mode(), RequestExchange::Mode::Open);

  auto result = kept->respondJson(okResponse(1));
  EXPECT_FALSE(holds_alternative<Error>(result));
  EXPECT_NE(sink->bytes().find("\"jsonrpc\""), std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest, AStreamWithNoRequestIdIsAllowed) {
  // A stream a client asked to be pushed to has no inbound request behind
  // it; inventing an id would put a phantom entry in any correlation map.
  std::unique_ptr<RetainedExchangeSink> sink(new RetainedExchangeSink());
  auto exchange =
      RequestExchange::create(*dispatcher_, std::move(sink), nullopt);

  EXPECT_FALSE(exchange->requestId().has_value());
  EXPECT_TRUE(exchange->beginStream());
}

// ── Headers and status ─────────────────────────────────────────────────────

TEST_F(RequestExchangeTest, HeadersMayBeSetUntilTheFirstByte) {
  auto exchange = makeExchange();

  EXPECT_TRUE(exchange->setResponseHeader("Mcp-Session-Id", "abc"));
  EXPECT_TRUE(exchange->setStatus(202));

  exchange->respondJson(okResponse(1));

  // After the response has gone out there is nothing left to attach to.
  EXPECT_FALSE(exchange->setResponseHeader("Mcp-Session-Id", "late"));
  EXPECT_FALSE(exchange->setStatus(500));
}

TEST_F(RequestExchangeTest, PlainResponseIsWrittenBareForTheCodecToFrame) {
  // The ordinary case has to stay exactly as it was: an unframed body that
  // the HTTP codec downstream turns into a response.
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));

  EXPECT_EQ(sink->bytes().find("HTTP/1.1"), std::string::npos)
      << "a plain response must not be framed here: " << sink->bytes();
  EXPECT_EQ(sink->bytes().find('{'), 0u) << sink->bytes();
}

TEST_F(RequestExchangeTest, AHeaderForcesTheExchangeToFrameTheResponse) {
  // The codec downstream emits a fixed header set, so a response that needs
  // anything else has to arrive already framed.
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  exchange->setResponseHeader("Mcp-Session-Id", "session-7");
  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));

  EXPECT_EQ(sink->bytes().find("HTTP/1.1 200 OK\r\n"), 0u) << sink->bytes();
  EXPECT_NE(sink->bytes().find("\r\nMcp-Session-Id: session-7\r\n"),
            std::string::npos)
      << sink->bytes();
  EXPECT_NE(sink->bytes().find("\r\nContent-Length: "), std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest, ANonDefaultStatusForcesFramingToo) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  exchange->setStatus(404);
  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));

  EXPECT_EQ(sink->bytes().find("HTTP/1.1 404 Not Found\r\n"), 0u)
      << sink->bytes();
}

// ── Answering once ─────────────────────────────────────────────────────────

TEST_F(RequestExchangeTest, ASecondResponseIsRefused) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));
  const size_t after_first = sink->bytes().size();

  EXPECT_TRUE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));
  EXPECT_EQ(sink->bytes().size(), after_first)
      << "a refused response must not put bytes on the wire";
}

TEST_F(RequestExchangeTest, StreamAndJsonAreMutuallyExclusive) {
  auto exchange = makeExchange();
  ASSERT_TRUE(exchange->beginStream());

  EXPECT_TRUE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));
  EXPECT_FALSE(exchange->beginStream());
}

TEST_F(RequestExchangeTest, EventsAfterCompleteAreRefused) {
  auto exchange = makeExchange();
  ASSERT_TRUE(exchange->beginStream());
  ASSERT_TRUE(exchange->complete());

  EXPECT_FALSE(exchange->writeEvent("", "late"));
}

TEST_F(RequestExchangeTest, CompleteIsIdempotent) {
  auto exchange = makeExchange();
  ASSERT_TRUE(exchange->beginStream());

  EXPECT_TRUE(exchange->complete());
  EXPECT_TRUE(exchange->complete());
  EXPECT_EQ(exchange->mode(), RequestExchange::Mode::Complete);
}

// ── Cancellation ───────────────────────────────────────────────────────────

TEST_F(RequestExchangeTest, CancellationFiresObserversExactlyOnce) {
  auto exchange = makeExchange();
  int fired = 0;
  exchange->cancellation().addObserver([&fired]() { ++fired; });

  EXPECT_FALSE(exchange->cancellation().cancelled());
  exchange->cancellation().cancel();
  exchange->cancellation().cancel();

  EXPECT_TRUE(exchange->cancellation().cancelled());
  EXPECT_EQ(fired, 1);
}

TEST_F(RequestExchangeTest, ObserverAddedAfterCancellationFiresImmediately) {
  // A late observer that never hears about a cancellation is worse than one
  // that hears about it out of order.
  auto exchange = makeExchange();
  exchange->cancellation().cancel();

  int fired = 0;
  exchange->cancellation().addObserver([&fired]() { ++fired; });
  EXPECT_EQ(fired, 1);
}

TEST_F(RequestExchangeTest, EveryObserverRunsEvenWhenOneAddsAnother) {
  auto exchange = makeExchange();
  int first = 0;
  int nested = 0;

  exchange->cancellation().addObserver([&]() {
    ++first;
    exchange->cancellation().addObserver([&nested]() { ++nested; });
  });

  exchange->cancellation().cancel();
  EXPECT_EQ(first, 1);
  EXPECT_EQ(nested, 1);
}

// ── Connection death ───────────────────────────────────────────────────────

TEST_F(RequestExchangeTest, WithoutRetentionConnectionDeathCancelsAndReleases) {
  auto exchange = makeExchange();
  bool cancelled = false;
  exchange->cancellation().addObserver([&cancelled]() { cancelled = true; });

  EXPECT_FALSE(exchange->onConnectionGone())
      << "an exchange with nothing to retain should not ask to be kept";
  EXPECT_TRUE(cancelled);
  EXPECT_FALSE(exchange->detached());
}

TEST_F(RequestExchangeTest, ACompletedExchangeIsNeverRetained) {
  auto exchange = makeExchange();
  exchange->setRetainOnDisconnect(true);
  exchange->respondJson(okResponse(1));

  EXPECT_FALSE(exchange->onConnectionGone());
}

TEST_F(RequestExchangeTest, RetainedExchangeSurvivesAndKeepsProducing) {
  auto exchange = makeExchange();
  exchange->setRetainOnDisconnect(true);
  ASSERT_TRUE(exchange->beginStream());

  bool cancelled = false;
  exchange->cancellation().addObserver([&cancelled]() { cancelled = true; });

  ASSERT_TRUE(exchange->onConnectionGone());
  EXPECT_TRUE(exchange->detached());
  EXPECT_FALSE(cancelled) << "a detached stream has not been cancelled";

  // The work behind the request carries on with nobody listening.
  EXPECT_TRUE(exchange->writeEvent("message", "first"));
  EXPECT_TRUE(exchange->writeEvent("message", "second"));

  const auto& retained = exchange->retainedEvents();
  ASSERT_EQ(retained.size(), 2u);
  EXPECT_EQ(retained[0].data, "first");
  EXPECT_EQ(retained[1].data, "second");
  EXPECT_TRUE(exchange->complete());
}

TEST_F(RequestExchangeTest, RetainedEventsAreIndividuallyAddressable) {
  // A returning client asks for everything after some id, so the events
  // have to carry ids and be kept unframed.
  auto exchange = makeExchange();
  exchange->setRetainOnDisconnect(true);
  ASSERT_TRUE(exchange->beginStream());
  ASSERT_TRUE(exchange->onConnectionGone());

  exchange->writeEvent("message", "a");
  exchange->writeEvent("message", "b");

  const auto& retained = exchange->retainedEvents();
  ASSERT_EQ(retained.size(), 2u);
  EXPECT_FALSE(retained[0].id.empty());
  EXPECT_NE(retained[0].id, retained[1].id);
  // Unframed: no chunk size lines, no SSE syntax.
  EXPECT_EQ(retained[0].data, "a");
}

TEST_F(RequestExchangeTest, ACallerSuppliedEventIdIsKept) {
  auto exchange = makeExchange();
  exchange->setRetainOnDisconnect(true);
  ASSERT_TRUE(exchange->beginStream());
  ASSERT_TRUE(exchange->onConnectionGone());

  exchange->writeEvent("message", "a", optional<std::string>("custom-9"));
  ASSERT_EQ(exchange->retainedEvents().size(), 1u);
  EXPECT_EQ(exchange->retainedEvents()[0].id, "custom-9");
}

TEST_F(RequestExchangeTest, TheRetainedRingIsBoundedAndDropsOldestFirst) {
  // A producer that keeps going behind a client that never returns would
  // otherwise grow without limit.
  auto exchange = makeExchange();
  exchange->setRetainOnDisconnect(true);
  exchange->setRetainedEventLimit(3);
  ASSERT_TRUE(exchange->beginStream());
  ASSERT_TRUE(exchange->onConnectionGone());

  for (int i = 0; i < 5; ++i) {
    exchange->writeEvent("message", std::to_string(i));
  }

  const auto& retained = exchange->retainedEvents();
  ASSERT_EQ(retained.size(), 3u);
  EXPECT_EQ(retained[0].data, "2") << "the oldest events go first";
  EXPECT_EQ(retained[2].data, "4") << "the newest event must be kept";
}

TEST_F(RequestExchangeTest, RetainedSinkDropsOldestWhenFull) {
  RetainedExchangeSink sink(2);
  sink.retain(RetainedEvent{"1", "message", "a"});
  sink.retain(RetainedEvent{"2", "message", "b"});
  sink.retain(RetainedEvent{"3", "message", "c"});

  ASSERT_EQ(sink.events().size(), 2u);
  EXPECT_EQ(sink.events()[0].id, "2");
  EXPECT_EQ(sink.events()[1].id, "3");
  EXPECT_EQ(sink.droppedEvents(), 1u);
}

// ── Thread affinity ────────────────────────────────────────────────────────

TEST_F(RequestExchangeTest, OtherThreadsReachTheExchangeThroughItsDispatcher) {
  // The exchange belongs to one dispatcher thread. Another thread does not
  // touch it directly; it posts, and the work runs where the exchange
  // lives. This is the supported route, and the assertions inside the
  // exchange are what stop anyone taking a different one.
  auto owner_factory = event::createLibeventDispatcherFactory();
  auto owner = owner_factory->createDispatcher("exchange_owner");

  std::atomic<bool> ready{false};
  std::atomic<bool> done{false};
  std::thread::id ran_on;
  RetainedExchangeSink* sink = nullptr;
  RequestExchangePtr exchange;

  std::thread owner_thread([&]() {
    owner->post([&]() {
      std::unique_ptr<RetainedExchangeSink> owned(new RetainedExchangeSink());
      sink = owned.get();
      exchange = RequestExchange::create(
          *owner, std::move(owned), optional<RequestId>(RequestId(int64_t(1))));
      ready = true;
    });
    owner->run(event::RunType::RunUntilExit);
  });

  while (!ready) {
    std::this_thread::sleep_for(1ms);
  }

  // A second dispatcher, standing in for another connection's thread.
  auto other_factory = event::createLibeventDispatcherFactory();
  auto other = other_factory->createDispatcher("other_thread");
  std::thread other_thread([&]() {
    other->post([&]() {
      // Do not touch the exchange from here. Hand the work to its owner.
      owner->post([&]() {
        ran_on = std::this_thread::get_id();
        exchange->respondJson(okResponse(1));
        done = true;
      });
      other->exit();
    });
    other->run(event::RunType::RunUntilExit);
  });

  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!done && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }

  EXPECT_TRUE(done) << "posted work never ran on the owning dispatcher";
  EXPECT_EQ(ran_on, owner_thread.get_id());
  EXPECT_NE(sink->bytes().find("\"jsonrpc\""), std::string::npos);

  owner->post([&]() { exchange.reset(); });
  owner->exit();
  owner_thread.join();
  other_thread.join();
}

}  // namespace
}  // namespace transport
}  // namespace mcp
