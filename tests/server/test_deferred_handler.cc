/**
 * Handlers that answer after their dispatch has returned.
 *
 * A handler that answers by returning cannot wait for anything: every
 * connection this server accepts is on one dispatcher thread, so a
 * handler waiting on the client would be waiting on the thread that has
 * to accept the client's reply. A deferred handler is the way out — it is
 * handed the stream its answer goes on and told nothing about when to use
 * it.
 *
 * What that costs is that nothing is sent on its behalf, so these tests
 * are largely about silence: that the dispatch produced no answer, that
 * the answer sent later is the only one, and that a handler which throws
 * still leaves the client with something.
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/message_dispatch_context.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

namespace mcp {
namespace server {
namespace {

/** A return path that keeps what was sent, both kinds. */
class CapturingContext : public NullMessageDispatchContext {
 public:
  class Stream : public ResponseStream {
   public:
    VoidResult sendNotification(
        const jsonrpc::Notification& notification) override {
      notifications.push_back(notification);
      return makeVoidSuccess();
    }
    VoidResult sendResponse(const jsonrpc::Response& response) override {
      responses.push_back(response);
      return makeVoidSuccess();
    }
    bool alive() const override { return true; }

    std::vector<jsonrpc::Notification> notifications;
    std::vector<jsonrpc::Response> responses;
  };

  VoidResult sendResponse(const jsonrpc::Response& response) override {
    direct.push_back(response);
    return makeVoidSuccess();
  }

  ResponseStreamPtr beginResponseStream() override {
    if (!stream) {
      stream = std::make_shared<Stream>();
    }
    return stream;
  }

  /** Answers written straight to the request, without a stream. */
  std::vector<jsonrpc::Response> direct;
  std::shared_ptr<Stream> stream;
};

class DeferringServer : public McpServer {
 public:
  explicit DeferringServer(const McpServerConfig& config) : McpServer(config) {}
  using McpServer::onRequestWithContext;
};

McpServerConfig testConfig() {
  McpServerConfig config;
  config.server_name = "deferred-handler-test";
  config.server_version = "0.0.1";
  return config;
}

jsonrpc::Request call(int64_t id, const std::string& method) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(id);
  request.method = method;
  return request;
}

TEST(DeferredHandler, NothingIsSentUntilTheHandlerSaysSo) {
  DeferringServer server(testConfig());
  CapturingContext context;

  ResponseStreamPtr held;
  server.registerAsyncRequestHandler(
      "tools/call",
      [&held](const jsonrpc::Request&, SessionContext&,
              const ResponseStreamPtr& answer) { held = answer; });

  server.onRequestWithContext(call(1, "tools/call"), context);

  EXPECT_TRUE(context.direct.empty())
      << "an answer was sent for a handler that has not answered";
  ASSERT_TRUE(context.stream);
  EXPECT_TRUE(context.stream->responses.empty())
      << "an answer was sent for a handler that has not answered";
  ASSERT_TRUE(held) << "the handler was given nothing to answer on";

  // Long after the dispatch returned.
  held->sendResponse(jsonrpc::Response::success(
      make_request_id(1), jsonrpc::ResponseResult(Metadata())));

  ASSERT_EQ(context.stream->responses.size(), 1u);
  EXPECT_TRUE(context.stream->responses[0].result.has_value());
  EXPECT_TRUE(context.direct.empty())
      << "the answer went out twice, by two routes";
}

// Progress on the way to an answer works the same as it does for a
// handler that answers by returning.
TEST(DeferredHandler, ItCanSayThingsOnTheWay) {
  DeferringServer server(testConfig());
  CapturingContext context;

  server.registerAsyncRequestHandler(
      "tools/call", [](const jsonrpc::Request& request, SessionContext&,
                       const ResponseStreamPtr& answer) {
        jsonrpc::Notification progress;
        progress.jsonrpc = "2.0";
        progress.method = "notifications/progress";
        answer->sendNotification(progress);
        answer->sendResponse(jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(Metadata())));
      });

  server.onRequestWithContext(call(1, "tools/call"), context);

  ASSERT_TRUE(context.stream);
  EXPECT_EQ(context.stream->notifications.size(), 1u);
  EXPECT_EQ(context.stream->responses.size(), 1u);
}

// A handler that throws has still been asked something, and the client
// is owed an answer rather than a silence it has to time out on.
TEST(DeferredHandler, AHandlerThatThrowsStillAnswers) {
  DeferringServer server(testConfig());
  CapturingContext context;

  server.registerAsyncRequestHandler(
      "tools/call",
      [](const jsonrpc::Request&, SessionContext&, const ResponseStreamPtr&) {
        throw std::runtime_error("the tool fell over");
      });

  server.onRequestWithContext(call(1, "tools/call"), context);

  ASSERT_TRUE(context.stream);
  ASSERT_EQ(context.stream->responses.size(), 1u)
      << "a handler that threw left the client waiting";
  ASSERT_TRUE(context.stream->responses[0].error.has_value());
  EXPECT_NE(context.stream->responses[0].error->message.find("fell over"),
            std::string::npos);
}

// Registering a method one way removes the other, so a method cannot be
// answered twice by two handlers that both think they own it.
TEST(DeferredHandler, RegisteringOneWayRemovesTheOther) {
  DeferringServer server(testConfig());
  CapturingContext context;

  server.registerAsyncRequestHandler(
      "tools/call", [](const jsonrpc::Request& request, SessionContext&,
                       const ResponseStreamPtr& answer) {
        answer->sendResponse(jsonrpc::Response::success(
            request.id, jsonrpc::ResponseResult(Metadata())));
      });
  server.registerRequestHandler(
      "tools/call", [](const jsonrpc::Request& request, SessionContext&) {
        return jsonrpc::Response::success(request.id,
                                          jsonrpc::ResponseResult(Metadata()));
      });

  server.onRequestWithContext(call(1, "tools/call"), context);

  EXPECT_EQ(context.direct.size(), 1u)
      << "the handler registered last did not answer";
  EXPECT_FALSE(context.stream)
      << "a handler answering by returning asked for a stream it never needs";
}

}  // namespace
}  // namespace server
}  // namespace mcp
