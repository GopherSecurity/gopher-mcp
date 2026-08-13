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

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/modern_era.h"
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
  void die() { alive_ = false; }

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

  ASSERT_TRUE(
      registry.open(make_request_id(1), stream, filterFrom(R"({"notifications":{
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
  registry.open(make_request_id(1), stream,
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
  registry.open(make_request_id(1), stream, filterFrom(R"({"notifications":{
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

  registry.open(make_request_id(1), both,
                filterFrom(R"({"notifications":{"toolsListChanged":true,
                    "promptsListChanged":true}})"));
  registry.open(make_request_id(2), tools_only,
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

  registry.open(make_request_id(1), ending,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));
  registry.open(make_request_id(2), carrying_on,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));

  ASSERT_TRUE(registry.close(make_request_id(1)));
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

TEST(ListenRegistry, TwoSubscriptionsCannotAnswerToOneName) {
  ListenRegistry registry;
  auto first = std::make_shared<StreamSpy>();
  auto second = std::make_shared<StreamSpy>();

  ASSERT_TRUE(registry.open(make_request_id(1), first, NotificationFilter()));
  EXPECT_FALSE(registry.open(make_request_id(1), second, NotificationFilter()))
      << "a second subscription took a name already answered to";
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_TRUE(second->notifications.empty());
}

TEST(ListenRegistry, ASubscriptionWhoseStreamHasGoneIsDropped) {
  ListenRegistry registry;
  auto living = std::make_shared<StreamSpy>();
  auto dead = std::make_shared<StreamSpy>();

  registry.open(make_request_id(1), living,
                filterFrom(R"({"notifications":{"toolsListChanged":true}})"));
  registry.open(make_request_id(2), dead,
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
  registry.open(make_request_id(1), stream, filterFrom(R"({})"));

  EXPECT_EQ(registry.publish(modern::kNotificationToolsListChanged,
                             json::JsonValue::object()),
            0u);
  EXPECT_EQ(registry.publish(modern::kNotificationResourcesUpdated,
                             updateOf("file:///a"), "file:///a"),
            0u);
  EXPECT_EQ(stream->notifications.size(), 1u)
      << "only the acknowledgement should have arrived";
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

// And it is answered by a handler that answers later — a subscription's
// response arrives when it ends, which for most of them is never.
TEST(ListenRegistry, ListeningIsAnsweredOnAStream) {
  McpServerConfig config;
  config.server_name = "listen-dispatch-test";
  config.server_version = "0.0.1";
  McpServer server(config);

  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = make_request_id(1);
  request.method = modern::kMethodSubscriptionsListen;

  EXPECT_EQ(server.streamingFor(request), StreamingMode::Required)
      << "a subscription that is not a stream is a request that never "
         "answers";
}

}  // namespace
}  // namespace server
}  // namespace mcp
