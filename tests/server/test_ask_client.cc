// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Questions this server asks a client while answering it.
 *
 * A server that needs the client to sample a model, or to be asked
 * anything else on the way to a result, sends a request of its own down
 * the stream the answer will arrive on. The client replies on whatever
 * connection it likes, and nothing but the JSON-RPC id connects the reply
 * to the question — so what these tests are about is that the id is
 * registered before the question goes out, that exactly one thing is told
 * the outcome, and that a client which never answers cannot hold the
 * question open forever.
 *
 * The stream is a stand-in rather than a real one: what is under test is
 * the bookkeeping around the question, not the framing of it.
 */

#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/event/event_loop.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

using namespace std::chrono_literals;

/** A stream that keeps what was written instead of writing it. */
class RecordingStream : public ResponseStream {
 public:
  VoidResult sendNotification(const jsonrpc::Notification&) override {
    return makeVoidSuccess();
  }

  VoidResult sendRequest(const jsonrpc::Request& request) override {
    if (refuse_) {
      return makeVoidError(
          Error(jsonrpc::INTERNAL_ERROR, "nowhere to ask the question"));
    }
    asked.push_back(request);
    return makeVoidSuccess();
  }

  VoidResult sendResponse(const jsonrpc::Response& response) override {
    answered.push_back(response);
    return makeVoidSuccess();
  }

  bool alive() const override { return true; }

  void refuseQuestions() { refuse_ = true; }

  std::vector<jsonrpc::Request> asked;
  std::vector<jsonrpc::Response> answered;

 private:
  bool refuse_{false};
};

/** Widens what a test needs: the inbound answer path and the waiters. */
class AskingServer : public McpServer {
 public:
  explicit AskingServer(const McpServerConfig& config) : McpServer(config) {}
  using McpServer::clientRequests;
  using McpServer::onRequestWithContext;
  using McpServer::onResponse;
  using McpServer::onResponseWithContext;

  event::Dispatcher* dispatcher() { return main_dispatcher_; }
};

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "ask-client-test";
  config.server_version = "0.0.1";
  return config;
}

jsonrpc::Request sampling(int64_t id) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(id);
  request.method = "sampling/createMessage";
  return request;
}

class CallerContext : public NullMessageDispatchContext {
 public:
  const std::string& principal() const override { return who; }
  const std::string& transportSessionId() const override { return transport; }
  network::Connection* originConnection() const override { return connection; }
  ResponseStreamPtr beginResponseStream() override { return stream; }
  VoidResult sendResponse(const jsonrpc::Response&) override {
    return makeVoidSuccess();
  }

  std::string who;
  std::string transport;
  network::Connection* connection = nullptr;
  ResponseStreamPtr stream = std::make_shared<RecordingStream>();
};

TEST(AskClient, AnUnauthenticatedAnswerMustComeFromTheOriginalConnection) {
  AskingServer server(testConfig());
  CallerContext original, other;
  original.connection = reinterpret_cast<network::Connection*>(0x2101);
  other.connection = reinterpret_cast<network::Connection*>(0x2102);
  int delivered = 0;
  server.registerAsyncRequestHandler(
      "test/ask", [&](const jsonrpc::Request&, SessionContext&,
                      const ResponseStreamPtr& answer) {
        EXPECT_TRUE(holds_alternative<std::nullptr_t>(server.askClient(
            answer, sampling(71),
            [&](const jsonrpc::Response&) { ++delivered; }, 0ms)));
      });
  auto request = sampling(1);
  request.method = "test/ask";
  server.onRequestWithContext(request, original);
  ASSERT_EQ(server.clientRequests().pending(), 1u);
  const auto response = jsonrpc::Response::success(
      make_request_id(71), jsonrpc::ResponseResult(Metadata()));

  server.onResponseWithContext(response, other);
  EXPECT_EQ(delivered, 0);
  EXPECT_EQ(server.clientRequests().pending(), 1u);
  server.onResponseWithContext(response, original);
  EXPECT_EQ(delivered, 1);
  EXPECT_EQ(server.clientRequests().pending(), 0u);
}

TEST(AskClient, AnUnauthenticatedTransportSessionCanAnswerOnANewConnection) {
  AskingServer server(testConfig());
  CallerContext original, returning, other;
  original.transport = returning.transport = "original-session";
  other.transport = "other-session";
  original.connection = reinterpret_cast<network::Connection*>(0x2201);
  returning.connection = reinterpret_cast<network::Connection*>(0x2202);
  other.connection = reinterpret_cast<network::Connection*>(0x2203);
  int delivered = 0;
  server.registerAsyncRequestHandler(
      "test/ask", [&](const jsonrpc::Request&, SessionContext&,
                      const ResponseStreamPtr& answer) {
        EXPECT_TRUE(holds_alternative<std::nullptr_t>(server.askClient(
            answer, sampling(72),
            [&](const jsonrpc::Response&) { ++delivered; }, 0ms)));
      });
  auto request = sampling(1);
  request.method = "test/ask";
  server.onRequestWithContext(request, original);
  ASSERT_EQ(server.clientRequests().pending(), 1u);
  const auto response = jsonrpc::Response::success(
      make_request_id(72), jsonrpc::ResponseResult(Metadata()));

  server.onResponseWithContext(response, other);
  EXPECT_EQ(delivered, 0);
  EXPECT_EQ(server.clientRequests().pending(), 1u);
  server.onResponseWithContext(response, returning);
  EXPECT_EQ(delivered, 1);
  EXPECT_EQ(server.clientRequests().pending(), 0u);
}

TEST(AskClient, TheAnswerReachesWhoeverAsked) {
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();

  std::vector<jsonrpc::Response> heard;
  auto sent = server.askClient(
      stream, sampling(7),
      [&heard](const jsonrpc::Response& response) {
        heard.push_back(response);
      },
      0ms);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(sent));

  ASSERT_EQ(stream->asked.size(), 1u) << "the question was never sent";
  EXPECT_EQ(stream->asked[0].method, "sampling/createMessage");
  EXPECT_EQ(server.clientRequests().pending(), 1u);

  // The client answers, on whatever connection it chose.
  auto answer = jsonrpc::Response::success(make_request_id(7),
                                           jsonrpc::ResponseResult(Metadata()));
  server.onResponse(answer);

  ASSERT_EQ(heard.size(), 1u) << "the answer reached nobody";
  EXPECT_TRUE(heard[0].result.has_value());
  EXPECT_EQ(server.clientRequests().pending(), 0u)
      << "an answered question is still being waited for";
}

// An answer to something nobody asked stays unmatched, which is the
// existing behaviour and the reason registering happens first.
TEST(AskClient, AnAnswerToNothingIsCounted) {
  AskingServer server(testConfig());

  server.onResponse(jsonrpc::Response::success(
      make_request_id(99), jsonrpc::ResponseResult(Metadata())));

  EXPECT_EQ(server.getServerStats().responses_unmatched.load(), 1u);
}

TEST(AskClient, AQuestionThatCouldNotBeSentLeavesNobodyWaiting) {
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();
  stream->refuseQuestions();

  bool told = false;
  auto sent = server.askClient(
      stream, sampling(7), [&told](const jsonrpc::Response&) { told = true; },
      0ms);

  ASSERT_FALSE(holds_alternative<std::nullptr_t>(sent))
      << "a question that was not sent was reported as sent";
  EXPECT_FALSE(told) << "the caller was told an outcome for a question that "
                        "never went out";
  EXPECT_EQ(server.clientRequests().pending(), 0u)
      << "a question that never went out is still being waited for";
}

TEST(AskClient, ADuplicateIsRejectedBeforeSendingAnotherQuestion) {
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();
  int first = 0, second = 0;
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(server.askClient(
      stream, sampling(73), [&](const jsonrpc::Response&) { ++first; }, 0ms)));
  const auto duplicate = server.askClient(
      stream, sampling(73), [&](const jsonrpc::Response&) { ++second; }, 0ms);
  ASSERT_TRUE(holds_alternative<Error>(duplicate));
  EXPECT_EQ(get<Error>(duplicate).code, jsonrpc::INVALID_REQUEST);
  EXPECT_EQ(stream->asked.size(), 1u);
  EXPECT_EQ(server.clientRequests().pending(), 1u);
  server.onResponse(jsonrpc::Response::success(
      make_request_id(73), jsonrpc::ResponseResult(Metadata())));
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 0);
}

TEST(AskClient, ARejectedDuplicateDoesNotReplaceTheOriginalDeadline) {
  AskingServer server(testConfig());
  ASSERT_TRUE(server.initialize());
  server.dispatcher()->run(event::RunType::NonBlock);
  auto stream = std::make_shared<RecordingStream>();
  int first = 0, second = 0;
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(server.askClient(
      stream, sampling(74),
      [&](const jsonrpc::Response& response) {
        ++first;
        EXPECT_TRUE(response.error.has_value());
        server.dispatcher()->exit();
      },
      30ms)));
  const auto duplicate = server.askClient(
      stream, sampling(74), [&](const jsonrpc::Response&) { ++second; }, 2s);
  ASSERT_TRUE(holds_alternative<Error>(duplicate));
  auto watchdog =
      server.dispatcher()->createTimer([&] { server.dispatcher()->exit(); });
  watchdog->enableTimer(500ms);
  server.dispatcher()->run(event::RunType::RunUntilExit);
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 0);
  EXPECT_EQ(stream->asked.size(), 1u);
  EXPECT_EQ(server.clientRequests().pending(), 0u);
  EXPECT_EQ(server.getServerStats().client_requests_timed_out.load(), 1u);
}

TEST(AskClient, SendRequestRejectsADuplicateWithoutConsumingTheOriginalWaiter) {
  AskingServer server(testConfig());
  ASSERT_TRUE(server.initialize());
  server.dispatcher()->run(event::RunType::NonBlock);
  CallerContext caller;
  caller.who = "alice";
  std::string session_id;
  int delivered = 0;
  server.registerAsyncRequestHandler(
      "test/ask", [&](const jsonrpc::Request&, SessionContext& session,
                      const ResponseStreamPtr& answer) {
        session_id = session.getId();
        EXPECT_TRUE(holds_alternative<std::nullptr_t>(server.askClient(
            answer, sampling(75),
            [&](const jsonrpc::Response&) { ++delivered; }, 0ms)));
      });
  auto request = sampling(1);
  request.method = "test/ask";
  server.onRequestWithContext(request, caller);
  ASSERT_FALSE(session_id.empty());
  auto duplicate = server.sendRequest(session_id, sampling(75), 1s);
  ASSERT_EQ(duplicate.wait_for(0ms), std::future_status::ready);
  const auto response = duplicate.get();
  ASSERT_TRUE(response.error.has_value());
  EXPECT_EQ(response.error->code, jsonrpc::INVALID_REQUEST);
  EXPECT_EQ(server.clientRequests().pending(), 1u);
  server.onResponseWithContext(
      jsonrpc::Response::success(make_request_id(75),
                                 jsonrpc::ResponseResult(Metadata())),
      caller);
  EXPECT_EQ(delivered, 1);
  EXPECT_EQ(server.clientRequests().pending(), 0u);
}

TEST(AskClient, AQuestionNeedsSomewhereToGoAndSomebodyToHearTheAnswer) {
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();

  EXPECT_FALSE(holds_alternative<std::nullptr_t>(server.askClient(
      nullptr, sampling(1), [](const jsonrpc::Response&) {}, 0ms)));
  EXPECT_FALSE(holds_alternative<std::nullptr_t>(
      server.askClient(stream, sampling(2), nullptr, 0ms)));
  EXPECT_EQ(server.clientRequests().pending(), 0u);
}

// A client that takes a question and never answers must not be able to
// hold the request open. The deadline needs a dispatcher to fire on, so
// this one runs a real event loop.
TEST(AskClient, AClientThatNeverAnswersRunsOutOfTime) {
  AskingServer server(testConfig());
  ASSERT_TRUE(server.initialize());
  ASSERT_NE(server.dispatcher(), nullptr);

  std::thread loop(
      [&server]() { server.dispatcher()->run(event::RunType::RunUntilExit); });

  auto stream = std::make_shared<RecordingStream>();
  std::promise<jsonrpc::Response> outcome;
  auto reached = outcome.get_future();

  server.dispatcher()->post([&server, stream, &outcome]() {
    auto sent = server.askClient(
        stream, sampling(11),
        [&outcome](const jsonrpc::Response& response) {
          outcome.set_value(response);
        },
        60ms);
    EXPECT_TRUE(holds_alternative<std::nullptr_t>(sent));
  });

  ASSERT_EQ(reached.wait_for(5s), std::future_status::ready)
      << "the question was never settled";
  const auto response = reached.get();
  ASSERT_TRUE(response.error.has_value())
      << "a question nobody answered was reported as answered";
  EXPECT_NE(response.error->message.find("in time"), std::string::npos)
      << response.error->message;
  EXPECT_EQ(server.getServerStats().client_requests_timed_out.load(), 1u);

  // And an answer arriving afterwards finds nobody, rather than telling
  // the same asker a second time.
  std::promise<void> checked;
  server.dispatcher()->post([&server, &checked]() {
    server.onResponse(jsonrpc::Response::success(
        make_request_id(11), jsonrpc::ResponseResult(Metadata())));
    checked.set_value();
  });
  checked.get_future().wait_for(5s);
  EXPECT_EQ(server.getServerStats().responses_unmatched.load(), 1u);

  server.dispatcher()->exit();
  loop.join();
}

TEST(AskClient, OffThreadSendRequestDeadlineDoesNotNeedDispatcherToRun) {
  AskingServer server(testConfig());
  ASSERT_TRUE(server.initialize());
  ASSERT_NE(server.dispatcher(), nullptr);

  auto future = server.sendRequest("missing-session", sampling(12), 60ms);
  ASSERT_EQ(future.wait_for(5s), std::future_status::ready)
      << "sendRequest future waited on a dispatcher post that never ran";

  jsonrpc::Response response;
  ASSERT_NO_THROW(response = future.get())
      << "sendRequest exposed a broken promise instead of an error response";
  ASSERT_TRUE(response.error.has_value());
  EXPECT_NE(response.error->message.find("in time"), std::string::npos)
      << response.error->message;
}

TEST(AskClient, OffThreadSendRequestDoesNotUseDestroyedServer) {
  auto server = std::make_unique<AskingServer>(testConfig());
  ASSERT_TRUE(server->initialize());
  ASSERT_NE(server->dispatcher(), nullptr);

  auto future = server->sendRequest("missing-session", sampling(13), 5s);
  server.reset();

  ASSERT_EQ(future.wait_for(5s), std::future_status::ready)
      << "sendRequest future was abandoned when the server was destroyed";

  jsonrpc::Response response;
  ASSERT_NO_THROW(response = future.get())
      << "sendRequest exposed a broken promise after server destruction";
  ASSERT_TRUE(response.error.has_value());
  EXPECT_NE(response.error->message.find("stopped"), std::string::npos)
      << response.error->message;
}

TEST(AskClient, RelatedStatelessRequestKeepsTheOriginalVerifiedPrincipal) {
  class Caller : public NullMessageDispatchContext {
   public:
    explicit Caller(std::string who) : principal_(std::move(who)) {}
    const std::string& principal() const override { return principal_; }

   private:
    std::string principal_;
  };
  AskingServer server(testConfig());
  auto stream = std::make_shared<RecordingStream>();
  ClientResponseOwner owner;
  owner.bound = true;
  owner.principal = "realm|alice";
  stream->setRequestOwner(owner);
  int delivered = 0;
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(server.askClient(
      stream, sampling(51), [&](const jsonrpc::Response&) { ++delivered; },
      0ms)));
  const auto response = jsonrpc::Response::success(
      make_request_id(51), jsonrpc::ResponseResult(Metadata()));
  Caller bob("realm|bob"), alice("realm|alice");
  server.onResponseWithContext(response, bob);
  server.onResponse(response);
  EXPECT_EQ(0, delivered);
  EXPECT_EQ(1u, server.clientRequests().pending());
  server.onResponseWithContext(response, alice);
  EXPECT_EQ(1, delivered);
  EXPECT_EQ(0u, server.clientRequests().pending());
}

}  // namespace
}  // namespace server
}  // namespace mcp
