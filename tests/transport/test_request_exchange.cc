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
  RequestExchangePtr makeExchange(RetainedExchangeSink** sink_out = nullptr) {
    std::unique_ptr<RetainedExchangeSink> sink(new RetainedExchangeSink());
    if (sink_out != nullptr) {
      *sink_out = sink.get();
    }
    return RequestExchange::create(*dispatcher_, std::move(sink),
                                   optional<RequestId>(RequestId(int64_t(1))));
  }

  // An exchange made before the request id is known, as one is when the
  // headers arrive ahead of the body.
  RequestExchangePtr makeAnonymousExchange(
      RetainedExchangeSink** sink_out = nullptr) {
    std::unique_ptr<RetainedExchangeSink> sink(new RetainedExchangeSink());
    if (sink_out != nullptr) {
      *sink_out = sink.get();
    }
    return RequestExchange::create(*dispatcher_, std::move(sink), nullopt);
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

TEST_F(RequestExchangeTest, AHeaderCanBeTakenBackBeforeItIsSaid) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  exchange->setResponseHeader("Mcp-Session-Id", "session-7");
  EXPECT_TRUE(exchange->removeResponseHeader("Mcp-Session-Id"));
  EXPECT_FALSE(exchange->removeResponseHeader("Mcp-Session-Id"));

  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));

  // Nothing was ever attached, so the answer goes back to the plain form
  // the codec downstream frames.
  EXPECT_EQ(sink->bytes().find("Mcp-Session-Id"), std::string::npos)
      << sink->bytes();
  EXPECT_EQ(sink->bytes().find("HTTP/1.1"), std::string::npos) << sink->bytes();

  // Once it has gone out it has been said.
  EXPECT_FALSE(exchange->removeResponseHeader("Content-Type"));
}

TEST_F(RequestExchangeTest, ASelfFramedResponseSaysWhatItIs) {
  // Nothing downstream supplies a content type on this path, and a client
  // handed a body with no type has to guess what it just read.
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  exchange->setResponseHeader("Mcp-Session-Id", "session-7");
  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));

  EXPECT_NE(sink->bytes().find("\r\nContent-Type: application/json\r\n"),
            std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest, FramedHeadersRideAlongWithoutForcingFraming) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  // Saying who may read an answer is not a reason to frame one here: the
  // codec downstream carries these on the path it frames.
  exchange->setFramedHeaders(
      {{"Access-Control-Allow-Origin", "http://localhost:3000"}});
  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));

  EXPECT_EQ(sink->bytes().find("HTTP/1.1"), std::string::npos) << sink->bytes();
  EXPECT_EQ(sink->bytes().find("Access-Control"), std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest, FramedHeadersAppearOnceFramingIsHere) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  exchange->setFramedHeaders(
      {{"Access-Control-Allow-Origin", "http://localhost:3000"},
       {"Access-Control-Expose-Headers", "Mcp-Session-Id"}});
  exchange->setResponseHeader("Mcp-Session-Id", "session-7");
  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));

  EXPECT_NE(sink->bytes().find(
                "\r\nAccess-Control-Allow-Origin: http://localhost:3000"
                "\r\n"),
            std::string::npos)
      << sink->bytes();
  EXPECT_NE(sink->bytes().find(
                "\r\nAccess-Control-Expose-Headers: Mcp-Session-Id\r\n"),
            std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest,
       WhatThisRequestDecidedBeatsWhatEveryRequestCarries) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  exchange->setFramedHeaders({{"Content-Type", "text/plain"}});
  exchange->setResponseHeader("Content-Type", "application/problem+json");
  exchange->setStatus(403);
  ASSERT_FALSE(holds_alternative<Error>(
      exchange->respondUnary("application/json", "{}")));

  EXPECT_NE(
      sink->bytes().find("\r\nContent-Type: application/problem+json\r\n"),
      std::string::npos)
      << sink->bytes();
  EXPECT_EQ(sink->bytes().find("text/plain"), std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest, AnAcceptedRequestStillSaysWhoMayReadIt) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  exchange->setFramedHeaders(
      {{"Access-Control-Allow-Origin", "http://localhost:3000"}});
  exchange->setStatus(202);
  ASSERT_FALSE(holds_alternative<Error>(exchange->respondUnary("", "")));

  EXPECT_EQ(sink->bytes().find("HTTP/1.1 202 Accepted\r\n"), 0u)
      << sink->bytes();
  EXPECT_NE(sink->bytes().find("Access-Control-Allow-Origin: "
                               "http://localhost:3000"),
            std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest, AStreamSaysWhoMayReadItToo) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  exchange->setFramedHeaders(
      {{"Access-Control-Allow-Origin", "http://localhost:3000"}});
  ASSERT_TRUE(exchange->beginStream());

  EXPECT_NE(sink->bytes().find("Access-Control-Allow-Origin: "
                               "http://localhost:3000"),
            std::string::npos)
      << sink->bytes();
  // The writer names the content type of a stream itself; nothing here
  // should have talked over it.
  EXPECT_NE(sink->bytes().find("Content-Type: text/event-stream"),
            std::string::npos)
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

// ── Request identity and phase ─────────────────────────────────────────────

TEST_F(RequestExchangeTest, TheRequestIdArrivesAfterTheExchangeDoes) {
  // An exchange is made when the request headers arrive; the id is not
  // known until the body has been parsed.
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeAnonymousExchange(&sink);
  EXPECT_FALSE(exchange->requestId().has_value());

  EXPECT_TRUE(exchange->setRequestId(RequestId(int64_t(7))));
  ASSERT_TRUE(exchange->requestId().has_value());
  EXPECT_EQ(get<int64_t>(exchange->requestId().value()), 7);

  EXPECT_FALSE(exchange->setRequestId(RequestId(int64_t(8))))
      << "moving the id would strand anything already holding the old one";
}

TEST_F(RequestExchangeTest, TheRequestIdCannotChangeOnceAnsweringHasBegun) {
  auto exchange = makeAnonymousExchange();
  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));

  EXPECT_FALSE(exchange->setRequestId(RequestId(int64_t(7))));
}

TEST_F(RequestExchangeTest, PhaseFollowsTheRequestThroughItsLifetime) {
  auto exchange = makeExchange();
  EXPECT_EQ(exchange->phase(), RequestExchange::Phase::ReceivingBody);

  EXPECT_TRUE(exchange->setPhase(RequestExchange::Phase::Dispatching));
  EXPECT_EQ(exchange->phase(), RequestExchange::Phase::Dispatching);

  ASSERT_FALSE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));
  EXPECT_EQ(exchange->phase(), RequestExchange::Phase::RespondingJson);

  ASSERT_TRUE(exchange->complete());
  EXPECT_EQ(exchange->phase(), RequestExchange::Phase::Done);
}

TEST_F(RequestExchangeTest, PhaseFollowsAStreamedAnswerToo) {
  auto exchange = makeExchange();
  ASSERT_TRUE(exchange->beginStream());

  // A streamed answer is the one kind that is not over the moment it
  // begins, so it needs somewhere to say how far through it is.
  EXPECT_TRUE(exchange->setPhase(RequestExchange::Phase::RespondingSseOpen));
  EXPECT_EQ(exchange->phase(), RequestExchange::Phase::RespondingSseOpen);

  EXPECT_TRUE(
      exchange->setPhase(RequestExchange::Phase::RespondingSseDraining));
  EXPECT_TRUE(exchange->setPhase(RequestExchange::Phase::RespondingSseClosed));

  ASSERT_TRUE(exchange->complete());
  EXPECT_EQ(exchange->phase(), RequestExchange::Phase::Done);
}

TEST_F(RequestExchangeTest, AStreamWithNoNameMakesNoPromise) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);
  ASSERT_TRUE(exchange->beginStream());

  ASSERT_TRUE(exchange->writeEvent("message", "hello"));

  // An id is a promise a client may come back and hold us to. A stream
  // with no identity has nothing to be found by, so it neither makes the
  // promise nor keeps anything against it.
  EXPECT_EQ(sink->bytes().find("id:"), std::string::npos) << sink->bytes();
  EXPECT_NE(sink->bytes().find("data: hello"), std::string::npos)
      << sink->bytes();
  EXPECT_TRUE(exchange->retainedEvents().empty());
}

TEST_F(RequestExchangeTest, AResumableStreamSaysWhereEachEventSits) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);
  exchange->makeResumable("ab12cd34", nullptr);
  ASSERT_TRUE(exchange->beginStream());

  ASSERT_TRUE(exchange->writeEvent("message", "hello"));
  ASSERT_TRUE(exchange->writeEvent("message", "again"));

  // The stream's own name, then where in it: the first half is what keeps
  // the id from meaning something else on another stream of the same
  // session, the second is the cursor a client comes back with.
  EXPECT_NE(sink->bytes().find("id: ab12cd34:1"), std::string::npos)
      << sink->bytes();
  EXPECT_NE(sink->bytes().find("id: ab12cd34:2"), std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest, AnEventFromSomewhereElseKeepsItsOwnId) {
  // A replayed or forwarded event carries the id of the stream that minted
  // it, and leaves this stream's sequence where it was: two streams
  // counting the same event would each claim it as theirs.
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);
  exchange->makeResumable("ffff0000", nullptr);
  ASSERT_TRUE(exchange->beginStream());

  ASSERT_TRUE(exchange->writeEvent("message", "borrowed",
                                   optional<std::string>("aaaa1111:7")));
  ASSERT_TRUE(exchange->writeEvent("message", "mine"));

  EXPECT_NE(sink->bytes().find("id: aaaa1111:7"), std::string::npos)
      << sink->bytes();
  EXPECT_NE(sink->bytes().find("id: ffff0000:1"), std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest, AResumableStreamKeepsWhatItSentWhileConnected) {
  // The events a client half-received are exactly the ones it will ask
  // for, so keeping them only once nobody is listening is too late.
  auto exchange = makeExchange();
  exchange->makeResumable("00ff00ff", nullptr);
  ASSERT_TRUE(exchange->beginStream());

  exchange->writeEvent("message", "a");
  exchange->writeEvent("message", "b");

  const auto& kept = exchange->retainedEvents();
  ASSERT_EQ(kept.size(), 2u);
  EXPECT_EQ(kept[0].id, "00ff00ff:1");
  EXPECT_EQ(kept[1].data, "b");
}

TEST_F(RequestExchangeTest, WhatAStreamIsHoldingIsCountedAndGivenBack) {
  auto accounting = std::make_shared<ReplayAccounting>();
  auto exchange = makeExchange();
  exchange->makeResumable("12341234", accounting);
  ASSERT_TRUE(exchange->beginStream());

  exchange->writeEvent("message", "a");
  exchange->writeEvent("message", "b");
  EXPECT_EQ(accounting->events.load(), 2u);
  EXPECT_GT(accounting->bytes.load(), 0u);

  // Explicit rather than left to the last reference going away, so the
  // moment the memory is released is a moment that can be pointed at.
  exchange->releaseReplay();
  EXPECT_EQ(accounting->events.load(), 0u);
  EXPECT_EQ(accounting->bytes.load(), 0u);
}

TEST_F(RequestExchangeTest, WhatIsCountedFallsAwayWithTheExchange) {
  auto accounting = std::make_shared<ReplayAccounting>();
  {
    auto exchange = makeExchange();
    exchange->makeResumable("56785678", accounting);
    ASSERT_TRUE(exchange->beginStream());
    exchange->writeEvent("message", "a");
    ASSERT_EQ(accounting->events.load(), 1u);
  }
  EXPECT_EQ(accounting->events.load(), 0u)
      << "an exchange nobody released still stops being held";
}

TEST_F(RequestExchangeTest, EachEventIsReportedAsItIsWritten) {
  // What a stream being followed reports through.
  auto exchange = makeExchange();
  exchange->makeResumable("9999aaaa", nullptr);
  ASSERT_TRUE(exchange->beginStream());

  std::vector<RetainedEvent> seen;
  exchange->setEventObserver(
      [&seen](const RetainedEvent& event) { seen.push_back(event); });

  exchange->writeEvent("message", "one");
  exchange->writeEvent("message", "two");

  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0].id, "9999aaaa:1");
  EXPECT_EQ(seen[1].data, "two");
}

TEST_F(RequestExchangeTest, AnEventIsAddressableWhetherOrNotItSaysSo) {
  auto exchange = makeExchange();
  exchange->setRetainOnDisconnect(true);
  ASSERT_TRUE(exchange->beginStream());
  ASSERT_TRUE(exchange->onConnectionGone());

  exchange->writeEvent("message", "a");
  exchange->writeEvent("message", "b");

  // Withholding the id from the wire does not mean forgetting it: the
  // retained copy is what a returning client is replayed from.
  const auto& retained = exchange->retainedEvents();
  ASSERT_EQ(retained.size(), 2u);
  EXPECT_FALSE(retained[0].id.empty());
  EXPECT_NE(retained[0].id, retained[1].id);
}

TEST_F(RequestExchangeTest, AFinishedRequestCannotBeReopened) {
  auto exchange = makeExchange();
  ASSERT_TRUE(exchange->complete());

  EXPECT_FALSE(exchange->setPhase(RequestExchange::Phase::Dispatching));
  EXPECT_EQ(exchange->phase(), RequestExchange::Phase::Done);
}

TEST_F(RequestExchangeTest, WithoutAnAcceptHeaderThePeerTakesAnything) {
  auto exchange = makeExchange();
  EXPECT_TRUE(exchange->clientContext().accepts_json);
  EXPECT_TRUE(exchange->clientContext().accepts_sse);
}

// ── Answers that are not a JSON-RPC response ───────────────────────────────

TEST_F(RequestExchangeTest, AnAcceptedRequestIsAnsweredWithNoBodyAtAll) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  ASSERT_TRUE(exchange->setStatus(202));
  ASSERT_FALSE(holds_alternative<Error>(exchange->respondUnary("", "")));

  EXPECT_EQ(sink->bytes().find("HTTP/1.1 202 Accepted\r\n"), 0u)
      << sink->bytes();
  EXPECT_NE(sink->bytes().find("\r\nContent-Length: 0\r\n"), std::string::npos)
      << sink->bytes();
  EXPECT_EQ(sink->bytes().substr(sink->bytes().size() - 4), "\r\n\r\n")
      << "nothing may follow the headers: " << sink->bytes();
}

TEST_F(RequestExchangeTest, AnErrorWithoutAnIdIsFramedHere) {
  // A jsonrpc::Response cannot express this: its id is a RequestId, and
  // there is no null alternative to put in one.
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  const std::string body =
      "{\"jsonrpc\":\"2.0\",\"id\":null,"
      "\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}";
  ASSERT_TRUE(exchange->setStatus(400));
  ASSERT_FALSE(holds_alternative<Error>(
      exchange->respondUnary("application/json", body)));

  EXPECT_EQ(sink->bytes().find("HTTP/1.1 400 Bad Request\r\n"), 0u)
      << sink->bytes();
  EXPECT_NE(sink->bytes().find("\r\nContent-Type: application/json\r\n"),
            std::string::npos)
      << sink->bytes();
  EXPECT_NE(sink->bytes().find("\r\n\r\n" + body), std::string::npos)
      << sink->bytes();
}

TEST_F(RequestExchangeTest, ARawAnswerIsStillOnlyAllowedOnce) {
  RetainedExchangeSink* sink = nullptr;
  auto exchange = makeExchange(&sink);

  ASSERT_FALSE(holds_alternative<Error>(exchange->respondUnary("", "")));
  const size_t after_first = sink->bytes().size();

  EXPECT_TRUE(holds_alternative<Error>(exchange->respondUnary("", "")));
  EXPECT_TRUE(holds_alternative<Error>(exchange->respondJson(okResponse(1))));
  EXPECT_EQ(sink->bytes().size(), after_first);
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
