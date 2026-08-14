/**
 * Subscriptions, and what goes down each one.
 *
 * The unit here is the subscription rather than the client, and almost
 * every rule follows from that. One client may hold several at once, so
 * every message carries the id of the stream it is going down; a change
 * matching two of them goes to both, tagged differently, and never twice
 * down one. A server sends nothing that was not asked for, so a filter
 * asking for nothing hears nothing rather than everything.
 *
 * And an ending is said rather than merely done: the response the listen
 * request never got is what distinguishes a subscription that ended from
 * a connection that dropped.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/protocol_versions.h"
#include "mcp/server/listen_registry.h"
#include "mcp/server/mcp_server.h"

namespace mcp {
namespace server {
namespace {

namespace modern = protocol::modern;

/** One subscription's stream, keeping everything written to it. */
class StreamSpy : public ResponseStream {
 public:
  VoidResult sendNotification(
      const jsonrpc::Notification& notification) override {
    if (!alive_) {
      return makeVoidError(Error(jsonrpc::INTERNAL_ERROR, "gone"));
    }
    notifications.push_back(json::to_json(notification));
    return makeVoidSuccess();
  }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    responses.push_back(json::to_json(response));
    return makeVoidSuccess();
  }
  bool alive() const override { return alive_; }
  bool onCancelled(std::function<void()> observer) override {
    cancellation = std::move(observer);
    return true;
  }

  void die() { alive_ = false; }

  /** What a client closing this stream does. */
  void clientWentAway() {
    alive_ = false;
    if (cancellation) {
      auto observer = cancellation;
      cancellation = nullptr;
      observer();
    }
  }

  std::function<void()> cancellation;

  /** The methods delivered here, in order. */
  std::vector<std::string> methods() const {
    std::vector<std::string> named;
    for (const auto& notification : notifications) {
      named.push_back(notification["method"].getString());
    }
    return named;
  }

  /** The subscription a delivered message says it belongs to. */
  int64_t subscriptionOf(size_t which) const {
    return notifications[which]["params"]["_meta"][modern::kMetaSubscriptionId]
        .getInt64();
  }

  std::vector<json::JsonValue> notifications;
  std::vector<json::JsonValue> responses;

 private:
  bool alive_{true};
};

NotificationFilter filterFrom(const std::string& params_json) {
  return NotificationFilter::parse(json::JsonValue::parse(params_json));
}

json::JsonValue updateOf(const std::string& uri) {
  json::JsonValue params = json::JsonValue::object();
  params.set("uri", json::JsonValue(uri));
  return params;
}

TEST(ListenRegistry, ASubscriptionIsAcknowledgedBeforeAnythingElse) {
  ListenRegistry registry;
  auto stream = std::make_shared<StreamSpy>();

  ASSERT_TRUE(registry.open("caller-a", make_request_id(1), stream,
                            filterFrom(R"({"notifications":{
                                "toolsListChanged":true}})")));

  ASSERT_EQ(stream->notifications.size(), 1u)
      << "a stream carried nothing to say it had been taken on";
  EXPECT_EQ(stream->methods()[0],
            modern::kNotificationSubscriptionsAcknowledged);
  EXPECT_EQ(stream->subscriptionOf(0), 1)
      << "the acknowledgement did not say which subscription it opened";

  // And it says what will actually be delivered, so a client is not left
  // waiting for something that was never coming. It arrives as the
  // nested object it is: what travels through the flat map stringified
  // is rebuilt on the way out.
  const auto& echoed = stream->notifications[0]["params"][modern::kFilterField];
  ASSERT_TRUE(echoed.isObject()) << "the filter did not survive as an object";
  EXPECT_TRUE(echoed.contains(modern::kFilterToolsListChanged));
  EXPECT_FALSE(echoed.contains(modern::kFilterPromptsListChanged));
}

TEST(ListenRegistry, NothingArrivesThatWasNotAskedFor) {
  ListenRegistry registry;
  auto stream = std::make_shared<StreamSpy>();
  registry.open("caller-a", make_request_id(1), stream,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));

  EXPECT_EQ(registry.publish(modern::kNotificationToolsListChanged,
                             json::JsonValue::object()),
            1u);
  EXPECT_EQ(registry.publish(modern::kNotificationPromptsListChanged,
                             json::JsonValue::object()),
            0u)
      << "a notification nobody asked for was delivered";

  // Request-scoped notices belong to the request they relate to and
  // never to a subscription.
  EXPECT_EQ(
      registry.publish("notifications/progress", json::JsonValue::object()),
      0u);

  const auto delivered = stream->methods();
  ASSERT_EQ(delivered.size(), 2u) << "acknowledgement plus the one wanted";
  EXPECT_EQ(delivered[1], modern::kNotificationToolsListChanged);
}

// A subscription names the resources it cares about. One that asked
// about a file has not asked about every file.
TEST(ListenRegistry, AResourceUpdateGoesOnlyToWhoAskedForThatResource) {
  ListenRegistry registry;
  auto stream = std::make_shared<StreamSpy>();
  registry.open("caller-a", make_request_id(1), stream,
                filterFrom(R"({"notifications":{
                    "resourceSubscriptions":["file:///a"]}})"));

  EXPECT_EQ(registry.publish(modern::kNotificationResourcesUpdated,
                             updateOf("file:///a"), "file:///a"),
            1u);
  EXPECT_EQ(registry.publish(modern::kNotificationResourcesUpdated,
                             updateOf("file:///b"), "file:///b"),
            0u)
      << "an update arrived for a resource nobody subscribed to";

  ASSERT_EQ(stream->notifications.size(), 2u);
  EXPECT_EQ(stream->notifications[1]["params"]["uri"].getString(), "file:///a")
      << "what the notification was about did not survive";
}

// The rule that follows from the unit being the subscription: a change
// matching two of one client's subscriptions goes to both, tagged with
// each one's own id — and never twice down one stream.
TEST(ListenRegistry, OneChangeReachesEveryMatchingSubscriptionOnce) {
  ListenRegistry registry;
  auto both = std::make_shared<StreamSpy>();
  auto tools_only = std::make_shared<StreamSpy>();

  registry.open("caller-a", make_request_id(1), both,
                filterFrom(R"({"notifications":{"toolsListChanged":true,
                    "promptsListChanged":true}})"));
  registry.open("caller-a", make_request_id(2), tools_only,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));

  EXPECT_EQ(registry.publish(modern::kNotificationToolsListChanged,
                             json::JsonValue::object()),
            2u);
  EXPECT_EQ(registry.publish(modern::kNotificationPromptsListChanged,
                             json::JsonValue::object()),
            1u);

  // The first stream heard both changes; the second heard only the one
  // it asked for.
  ASSERT_EQ(both->notifications.size(), 3u);
  ASSERT_EQ(tools_only->notifications.size(), 2u);

  // And each stream's messages carry that stream's own id, never the
  // other's.
  for (size_t i = 0; i < both->notifications.size(); ++i) {
    EXPECT_EQ(both->subscriptionOf(i), 1) << "message " << i;
  }
  for (size_t i = 0; i < tools_only->notifications.size(); ++i) {
    EXPECT_EQ(tools_only->subscriptionOf(i), 2) << "message " << i;
  }
}

// An ending is said, not merely done: this is what tells a client the
// subscription finished rather than the connection dropping.
TEST(ListenRegistry, AnEndingIsSaidOnTheStreamThatIsEnding) {
  ListenRegistry registry;
  auto ending = std::make_shared<StreamSpy>();
  auto carrying_on = std::make_shared<StreamSpy>();

  registry.open("caller-a", make_request_id(1), ending,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));
  registry.open("caller-a", make_request_id(2), carrying_on,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));

  ASSERT_TRUE(registry.close("caller-a", make_request_id(1)));
  EXPECT_EQ(registry.size(), 1u);

  ASSERT_EQ(ending->responses.size(), 1u)
      << "a subscription ended without saying so";
  const auto& answer = ending->responses[0]["result"];
  EXPECT_EQ(answer[modern::kResultTypeField].getString(),
            modern::kResultTypeComplete);
  EXPECT_EQ(answer["_meta"][modern::kMetaSubscriptionId].getInt64(), 1)
      << "the ending did not say which subscription it ended";

  EXPECT_TRUE(carrying_on->responses.empty())
      << "ending one subscription ended another";

  // And the one still open keeps receiving.
  EXPECT_EQ(registry.publish(modern::kNotificationToolsListChanged,
                             json::JsonValue::object()),
            1u);
  EXPECT_EQ(carrying_on->notifications.size(), 2u);
}

// A subscription's id is one its own client chose, so two clients each
// numbering their requests from one is the ordinary case rather than a
// collision. Refusing the second would mean whichever client subscribed
// first could stop any other from subscribing at all.
TEST(ListenRegistry, TwoClientsMayUseTheSameIdForDifferentSubscriptions) {
  ListenRegistry registry;
  auto first = std::make_shared<StreamSpy>();
  auto second = std::make_shared<StreamSpy>();

  ASSERT_TRUE(registry.open(
      "caller-a", make_request_id(1), first,
      filterFrom(R"({"notifications":{"toolsListChanged":true}})")));
  ASSERT_TRUE(registry.open(
      "caller-b", make_request_id(1), second,
      filterFrom(R"({"notifications":{"toolsListChanged":true}})")))
      << "one client's choice of id stopped another from subscribing";
  EXPECT_EQ(registry.size(), 2u);

  // Both are real subscriptions, and each carries the id its own client
  // chose — which is the same number, meaning something different to
  // each of them.
  EXPECT_EQ(registry.publish(modern::kNotificationToolsListChanged,
                             json::JsonValue::object()),
            2u);
  EXPECT_EQ(first->subscriptionOf(1), 1);
  EXPECT_EQ(second->subscriptionOf(1), 1);

  // And ending one ends that one only, though they are named alike.
  ASSERT_TRUE(registry.close("caller-a", make_request_id(1)));
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_TRUE(second->responses.empty())
      << "ending one client's subscription ended another's";
}

TEST(ListenRegistry, OneClientCannotUseOneIdTwice) {
  ListenRegistry registry;
  auto first = std::make_shared<StreamSpy>();
  auto second = std::make_shared<StreamSpy>();

  ASSERT_TRUE(registry.open("caller-a", make_request_id(1), first,
                            NotificationFilter()));
  EXPECT_FALSE(registry.open("caller-a", make_request_id(1), second,
                             NotificationFilter()))
      << "a second subscription took a name already answered to";
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_TRUE(second->notifications.empty());
}

TEST(ListenRegistry, ASubscriptionWhoseStreamHasGoneIsDropped) {
  ListenRegistry registry;
  auto living = std::make_shared<StreamSpy>();
  auto dead = std::make_shared<StreamSpy>();

  registry.open("caller-a", make_request_id(1), living,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));
  registry.open("caller-a", make_request_id(2), dead,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));
  dead->die();

  EXPECT_EQ(registry.forgetDead(), 1u);
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_EQ(registry.publish(modern::kNotificationToolsListChanged,
                             json::JsonValue::object()),
            1u);
}

// A filter that asks for nothing hears nothing. Reading it as asking for
// everything would send a client the one thing a server must never send:
// something it did not ask for.
TEST(ListenRegistry, AskingForNothingHearsNothing) {
  ListenRegistry registry;
  auto stream = std::make_shared<StreamSpy>();
  registry.open("caller-a", make_request_id(1), stream, filterFrom(R"({})"));

  EXPECT_EQ(registry.publish(modern::kNotificationToolsListChanged,
                             json::JsonValue::object()),
            0u);
  EXPECT_EQ(registry.publish(modern::kNotificationResourcesUpdated,
                             updateOf("file:///a"), "file:///a"),
            0u);
  EXPECT_EQ(stream->notifications.size(), 1u)
      << "only the acknowledgement should have arrived";
}

// There is no message for ending a subscription: a client ends one by
// stopping reading it. So the close has to be what does it — and it has
// to end that subscription only, since one client may hold several on the
// same connection and closing all of them is not what closing one means.
TEST(ListenRegistry, AClientThatStopsReadingHasEndedThatSubscription) {
  ListenRegistry registry;
  auto abandoned = std::make_shared<StreamSpy>();
  auto kept = std::make_shared<StreamSpy>();

  registry.open("caller-a", make_request_id(1), abandoned,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));
  registry.open("caller-a", make_request_id(2), kept,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));
  ASSERT_EQ(registry.size(), 2u);

  abandoned->clientWentAway();

  EXPECT_EQ(registry.size(), 1u)
      << "a subscription nobody is reading is still being held";
  EXPECT_EQ(registry.publish(modern::kNotificationToolsListChanged,
                             json::JsonValue::object()),
            1u)
      << "a change was still being written to a stream nobody reads";
  EXPECT_EQ(kept->notifications.size(), 2u)
      << "one subscription ending ended another";

  // Nothing was said about the ending, because there was nobody left to
  // say it to: the response that marks a graceful close would have gone
  // down a stream that is no longer read.
  EXPECT_TRUE(abandoned->responses.empty());
}

// The method has to be reachable, or the whole of the above is
// machinery nobody can get to. A modern request for it must not be a
// 404, and a server has to answer it rather than hand it to a handler
// that does not exist.
TEST(ListenRegistry, TheMethodIsOneThisServerHas) {
  McpServerConfig config;
  config.server_name = "listen-reachability-test";
  config.server_version = "0.0.1";
  McpServer server(config);

  EXPECT_TRUE(server.knowsMethod(modern::kMethodSubscriptionsListen))
      << "a request to listen would be answered 404";
  EXPECT_TRUE(server.knowsMethod("tools/call"));
  EXPECT_FALSE(server.knowsMethod("tools/invent"));
}

/** A request declaring the era it belongs to, the way one does. */
jsonrpc::Request listenRequest(bool modern_era) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = modern::kMethodSubscriptionsListen;
  if (modern_era) {
    Metadata params;
    params["_meta"] =
        MetadataValue(std::string("{\"") + modern::kMetaProtocolVersion +
                      "\":\"" + protocol::kProtocolVersion20260728 + "\"}");
    request.params = mcp::make_optional(params);
  }
  return request;
}

// And it is answered by a handler that answers later — a subscription's
// response arrives when it ends, which for most of them is never.
TEST(ListenRegistry, ListeningIsAnsweredOnAStream) {
  McpServerConfig config;
  config.server_name = "listen-dispatch-test";
  config.server_version = "0.0.1";
  McpServer server(config);

  EXPECT_EQ(server.streamingFor(listenRequest(/*modern_era=*/true)),
            StreamingMode::Required)
      << "a subscription that is not a stream is a request that never "
         "answers";
}

// This method belongs to one era. A server serving both must not let it
// leak into the other — and least of all leak how it would be answered,
// which is what a refusal for not accepting a stream would tell a caller
// about a method it cannot call.
TEST(ListenRegistry, ACallerOfAnOlderEraHasNoSuchMethod) {
  McpServerConfig config;
  config.server_name = "listen-era-test";
  config.server_version = "0.0.1";
  McpServer server(config);

  EXPECT_EQ(server.streamingFor(listenRequest(/*modern_era=*/false)),
            StreamingMode::None)
      << "a classic caller was told how a method it does not have would "
         "be answered";
  EXPECT_TRUE(server.isModernRequest(listenRequest(true)));
  EXPECT_FALSE(server.isModernRequest(listenRequest(false)));
}

/** A dispatch context that keeps the one answer sent through it. */
class AnswerSpy : public MessageDispatchContext {
 public:
  network::Connection* originConnection() const override { return nullptr; }
  const std::string& transportSessionId() const override {
    static const std::string none;
    return none;
  }
  VoidResult sendResponse(const jsonrpc::Response& response) override {
    answered.push_back(response);
    return makeVoidSuccess();
  }

  std::vector<jsonrpc::Response> answered;
};

// And the answer it gets has to be that one, not a description of how a
// method it cannot call would have been answered. This is the whole of
// what "not found" means here: found-and-refused says the method exists.
TEST(ListenRegistry, AnOlderCallerIsToldTheMethodIsNotFound) {
  McpServerConfig config;
  config.server_name = "listen-era-answer-test";
  config.server_version = "0.0.1";
  // Dispatch is the server's own doorway rather than a public one, so
  // this asks the way the transport does.
  class Doorway : public McpServer {
   public:
    explicit Doorway(const McpServerConfig& config) : McpServer(config) {}
    using McpServer::onRequestWithContext;
  };
  Doorway server(config);

  AnswerSpy context;
  server.onRequestWithContext(listenRequest(/*modern_era=*/false), context);

  ASSERT_EQ(context.answered.size(), 1u) << "a classic caller got no answer";
  ASSERT_TRUE(context.answered[0].error.has_value());
  EXPECT_EQ(context.answered[0].error->code, jsonrpc::METHOD_NOT_FOUND)
      << "a classic caller was answered for a handler it cannot reach, "
         "which says the method exists: "
      << context.answered[0].error->message;
}

}  // namespace
}  // namespace server
}  // namespace mcp
