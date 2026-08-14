/**
 * Holding a subscription open, from both ends at once.
 *
 * A real client and a real server, in the revision that has no standalone
 * stream and no `resources/subscribe`. A client says what it wants to
 * hear; the answer to that request never arrives until the subscription
 * ends; everything it asked for comes down the stream in between.
 *
 * The unit is the subscription rather than the client, which is what
 * makes the two ends worth testing together: one client may hold several
 * at once, each on a connection of its own, and every message carries the
 * id of the one it belongs to because nothing else tells them apart.
 *
 * And ending one is done rather than said — the client lets go of the
 * connection, and the server finds out by the stream going. That the two
 * halves meet is only observable here.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/client/mcp_client.h"
#include "mcp/json/json_bridge.h"
#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/subscriptions.h"
#include "mcp/server/mcp_server.h"
#include "mcp/types.h"

#include "../client/scripted_http_server.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;
namespace modern = protocol::modern;

uint16_t pickEphemeralPort() {
  auto& iface = network::socketInterface();
  auto fd_result =
      iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                   network::Address::IpVersion::v4);
  if (!fd_result.ok()) {
    throw std::runtime_error("pickEphemeralPort: socket() failed");
  }
  auto handle = iface.ioHandleForFd(*fd_result, /*socket_v6only=*/false);
  handle->setBlocking(false);
  auto bound =
      handle->bind(network::Address::parseInternetAddress("127.0.0.1", 0));
  if (!bound.ok()) {
    throw std::runtime_error("pickEphemeralPort: bind() failed");
  }
  auto local = handle->localAddress();
  if (!local.ok()) {
    throw std::runtime_error("pickEphemeralPort: localAddress() failed");
  }
  const auto* ip = dynamic_cast<const network::Address::Ip*>(local->get());
  if (ip == nullptr) {
    throw std::runtime_error("pickEphemeralPort: not an IP address");
  }
  const uint16_t port = ip->port();
  handle->close();
  return port;
}

bool waitForListenerReady(uint16_t port, std::chrono::milliseconds budget) {
  auto& iface = network::socketInterface();
  auto addr = network::Address::parseInternetAddress("127.0.0.1", port);
  const auto deadline = std::chrono::steady_clock::now() + budget;
  while (std::chrono::steady_clock::now() < deadline) {
    auto fd_result =
        iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                     network::Address::IpVersion::v4);
    if (fd_result.ok()) {
      auto handle = iface.ioHandleForFd(*fd_result, false);
      handle->setBlocking(true);
      auto connected = handle->connect(addr);
      handle->close();
      if (connected.ok()) {
        return true;
      }
    }
    std::this_thread::sleep_for(25ms);
  }
  return false;
}

/** Everything one subscription was told, in order. */
struct Heard {
  std::mutex mutex;
  std::vector<jsonrpc::Notification> messages;

  void take(const jsonrpc::Notification& notification) {
    std::lock_guard<std::mutex> lock(mutex);
    messages.push_back(notification);
  }

  size_t count() {
    std::lock_guard<std::mutex> lock(mutex);
    return messages.size();
  }

  std::vector<std::string> methods() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::string> named;
    for (const auto& one : messages) {
      named.push_back(one.method);
    }
    return named;
  }

  /** The subscription a message says it belongs to. */
  int64_t subscriptionOf(size_t which) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto& params = messages[which].params.value();
    auto meta = params.find("_meta");
    auto parsed = json::JsonValue::parse(get<std::string>(meta->second));
    return parsed[modern::kMetaSubscriptionId].getInt64();
  }
};

class ModernEraSubscriptionsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    port_ = pickEphemeralPort();

    server::McpServerConfig config;
    config.server_name = "subscriptions-test-server";
    config.server_version = "0.0.1";
    config.supported_transports = {TransportType::HttpSse};
    config.num_workers = 1;
    config.capabilities.resources =
        mcp::make_optional(variant<bool, ResourcesCapability>(true));
    config.streamable_http.enable_modern_era = true;

    server_ = server::createMcpServer(config);
    ASSERT_NE(server_, nullptr);

    auto listening =
        server_->listen("http://127.0.0.1:" + std::to_string(port_));
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(listening));
    server_thread_ = std::thread([this]() { server_->run(); });
    ASSERT_TRUE(waitForListenerReady(port_, 5s));

    client::McpClientConfig client_config;
    client_config.client_name = "subscriptions-test-client";
    client_config.client_version = "0.0.1";
    client_config.num_workers = 1;
    client_config.request_timeout = 5000ms;
    client_config.protocol_connection_timeout = 5000ms;
    client_config.streamable_http.enable_modern_era = true;

    client_ = client::createMcpClient(client_config);
    ASSERT_NE(client_, nullptr);
    auto connected =
        client_->connect("http://127.0.0.1:" + std::to_string(port_) + "/mcp");
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected));
  }

  void TearDown() override {
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    if (server_) {
      server_->shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    server_.reset();
  }

  static bool waitUntil(const std::function<bool()>& done,
                        std::chrono::milliseconds budget = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      if (done()) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return done();
  }

  /** What a subscription asking about one resource looks like. */
  static modern::NotificationFilter about(const std::string& uri) {
    modern::NotificationFilter filter;
    filter.resource_uris.push_back(uri);
    return filter;
  }

  uint16_t port_{0};
  std::unique_ptr<server::McpServer> server_;
  std::thread server_thread_;
  std::unique_ptr<client::McpClient> client_;
};

// The whole of one subscription: it is acknowledged before anything else,
// and what it asked about arrives on it.
TEST_F(ModernEraSubscriptionsTest, ASubscriptionIsAcknowledgedThenFed) {
  Heard heard;
  const int64_t id = client_->listen(
      about("file:///watched"),
      [&heard](const jsonrpc::Notification& n) { heard.take(n); });
  ASSERT_NE(id, 0) << "no subscription was opened";

  ASSERT_TRUE(waitUntil([&heard]() { return heard.count() >= 1; }))
      << "nothing arrived to say the subscription had been taken on";
  EXPECT_EQ(heard.methods()[0], modern::kNotificationSubscriptionsAcknowledged);
  EXPECT_EQ(heard.subscriptionOf(0), id)
      << "the acknowledgement did not say which subscription it opened";

  server_->notifyResourceUpdate("file:///watched");
  ASSERT_TRUE(waitUntil([&heard]() { return heard.count() >= 2; }))
      << "an update to a watched resource never arrived";
  EXPECT_EQ(heard.methods()[1], modern::kNotificationResourcesUpdated);
  EXPECT_EQ(heard.subscriptionOf(1), id);
}

// A server sends nothing that was not asked for, and a subscription that
// named one resource has not asked about every resource.
TEST_F(ModernEraSubscriptionsTest, NothingArrivesThatWasNotAskedFor) {
  Heard heard;
  ASSERT_NE(client_->listen(
                about("file:///watched"),
                [&heard](const jsonrpc::Notification& n) { heard.take(n); }),
            0);
  ASSERT_TRUE(waitUntil([&heard]() { return heard.count() >= 1; }));

  server_->notifyResourceUpdate("file:///ignored");
  server_->notifyResourceUpdate("file:///watched");

  ASSERT_TRUE(waitUntil([&heard]() { return heard.count() >= 2; }));
  // Given time for a wrong one to have arrived after the right one.
  std::this_thread::sleep_for(200ms);
  ASSERT_EQ(heard.count(), 2u)
      << "an update arrived for a resource nobody subscribed to";
}

// One client, two subscriptions, each on a connection of its own. Every
// message carries the id of the one it belongs to, because on a transport
// where several share a client there is nothing else to tell them apart
// by — and a request sent meanwhile is not queued behind either.
TEST_F(ModernEraSubscriptionsTest, SeveralAtOnceStayApartAndBlockNothing) {
  Heard first;
  Heard second;

  const int64_t one = client_->listen(
      about("file:///one"),
      [&first](const jsonrpc::Notification& n) { first.take(n); });
  const int64_t two = client_->listen(
      about("file:///two"),
      [&second](const jsonrpc::Notification& n) { second.take(n); });
  ASSERT_NE(one, 0);
  ASSERT_NE(two, 0);
  ASSERT_NE(one, two);
  EXPECT_EQ(client_->subscriptionsHeld(), 2u);

  ASSERT_TRUE(waitUntil([&]() {
    return first.count() >= 1 && second.count() >= 1;
  })) << "one of two subscriptions was never acknowledged";

  server_->notifyResourceUpdate("file:///one");
  server_->notifyResourceUpdate("file:///two");
  ASSERT_TRUE(
      waitUntil([&]() { return first.count() >= 2 && second.count() >= 2; }));

  // Each stream's messages carry that stream's own id, never the other's.
  EXPECT_EQ(first.subscriptionOf(0), one);
  EXPECT_EQ(first.subscriptionOf(1), one);
  EXPECT_EQ(second.subscriptionOf(0), two);
  EXPECT_EQ(second.subscriptionOf(1), two);

  // And with both held open, an ordinary request still gets through —
  // which is the whole reason each has a connection to itself.
  auto pinged = client_->sendRequest("ping");
  ASSERT_EQ(pinged.wait_for(5s), std::future_status::ready)
      << "a request was queued behind a subscription that never ends";
  EXPECT_FALSE(pinged.get().error.has_value());
}

// There is no message that ends a subscription: a client ends one by
// letting go of it, and the server finds out by the stream going. That
// the two halves meet is only visible from both ends at once.
TEST_F(ModernEraSubscriptionsTest, LettingGoEndsThatSubscriptionAlone) {
  Heard going;
  Heard staying;

  const int64_t leaves = client_->listen(
      about("file:///leaves"),
      [&going](const jsonrpc::Notification& n) { going.take(n); });
  const int64_t stays = client_->listen(
      about("file:///stays"),
      [&staying](const jsonrpc::Notification& n) { staying.take(n); });
  ASSERT_NE(leaves, 0);
  ASSERT_NE(stays, 0);
  ASSERT_TRUE(
      waitUntil([&]() { return going.count() >= 1 && staying.count() >= 1; }));

  client_->stopListening(leaves);
  EXPECT_EQ(client_->subscriptionsHeld(), 1u);

  // The server let go of it too, rather than going on writing to a
  // stream nobody reads.
  ASSERT_TRUE(waitUntil([this]() {
    return server_->subscriptions().size() == 1u;
  })) << "the server was still holding a subscription its client had "
         "stopped reading";

  // And the other one is untouched by it.
  const size_t before = going.count();
  server_->notifyResourceUpdate("file:///leaves");
  server_->notifyResourceUpdate("file:///stays");
  ASSERT_TRUE(waitUntil([&]() { return staying.count() >= 2; }))
      << "ending one subscription ended another";
  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(going.count(), before)
      << "a subscription that had been let go was still being fed";
}

// Letting go of a subscription cuts its stream, and a stream being cut
// names the request whose answer was still arriving. Named from what the
// session recorded, that is somebody else's request — which is then
// asked for again though nothing had happened to it.
TEST_F(ModernEraSubscriptionsTest, EndingOneDoesNotSeverAnotherRequest) {
  Heard heard;
  const int64_t id = client_->listen(
      about("file:///watched"),
      [&heard](const jsonrpc::Notification& n) { heard.take(n); });
  ASSERT_NE(id, 0);
  ASSERT_TRUE(waitUntil([&heard]() { return heard.count() >= 1; }));

  auto pinged = client_->sendRequest("ping");
  client_->stopListening(id);

  ASSERT_EQ(pinged.wait_for(5s), std::future_status::ready)
      << "a request was left outstanding by a subscription being let go of";
  EXPECT_FALSE(pinged.get().error.has_value())
      << "a request was treated as cut off because a subscription ended";
}

// A client on its way out has no dispatcher to open one on, and waiting
// for a loop that is being told to stop is a wait with no end. Refused
// outright instead, and nothing is left recorded for it.
TEST_F(ModernEraSubscriptionsTest, ListeningIsRefusedWhileShuttingDown) {
  client_->shutdown();

  Heard heard;
  const int64_t id = client_->listen(
      about("file:///watched"),
      [&heard](const jsonrpc::Notification& n) { heard.take(n); });
  EXPECT_EQ(id, 0) << "a subscription was opened on a client that is going";
  EXPECT_EQ(client_->subscriptionsHeld(), 0u)
      << "a subscription that never opened is being held";

  client_.reset();
}

// A server may end a subscription itself, and its answer is what says
// so. Nothing else would notice: the request completes like any other,
// leaving this client holding a callback nothing will call and a
// connection nobody reads.
TEST_F(ModernEraSubscriptionsTest, AServerEndingOneReleasesItHere) {
  Heard heard;
  const int64_t id = client_->listen(
      about("file:///watched"),
      [&heard](const jsonrpc::Notification& n) { heard.take(n); });
  ASSERT_NE(id, 0);
  ASSERT_TRUE(waitUntil([&heard]() { return heard.count() >= 1; }));
  ASSERT_EQ(client_->subscriptionsHeld(), 1u);

  // The server lets go of every subscription it holds, saying so on each.
  server_->endAllSubscriptions();

  ASSERT_TRUE(waitUntil([this]() { return client_->subscriptionsHeld() == 0; }))
      << "the server ended a subscription and this client went on holding "
         "it";
}

// A request sent alongside a subscription must not be told about the
// subscription's status. They finish in their own time, and attributing
// one connection's answer to another's request retries or fails a
// request nothing happened to.
TEST_F(ModernEraSubscriptionsTest, ARequestIsNotToldAboutASubscription) {
  Heard heard;
  const int64_t id = client_->listen(
      about("file:///watched"),
      [&heard](const jsonrpc::Notification& n) { heard.take(n); });
  ASSERT_NE(id, 0);
  ASSERT_TRUE(waitUntil([&heard]() { return heard.count() >= 1; }));

  // Ended while an ordinary request is outstanding, so the status its
  // connection reports has somewhere wrong to land.
  auto pinged = client_->sendRequest("ping");
  client_->stopListening(id);

  ASSERT_EQ(pinged.wait_for(5s), std::future_status::ready)
      << "a request was left outstanding by a subscription ending";
  EXPECT_FALSE(pinged.get().error.has_value())
      << "a request was failed for something that happened to a "
         "subscription";
}

bool waitUntilTrue(const std::function<bool()>& done,
                   std::chrono::milliseconds budget) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return done();
}

/**
 * The same client, against a peer that refuses one thing and serves
 * another — which a whole server of ours will not do, since it serves
 * both.
 */
class RefusedSubscriptionTest : public ::testing::Test {
 protected:
  void TearDown() override {
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    server_.stop();
  }

  test::ScriptedServer server_;
  std::unique_ptr<client::McpClient> client_;
};

// A status belongs to the request whose connection carried it. Taken in
// turn off what the session recorded going out, a subscription's refusal
// lands on whatever went out first elsewhere — and that request is
// retried or failed for something that never happened to it.
TEST_F(RefusedSubscriptionTest, ARefusedSubscriptionFailsNoOtherRequest) {
  const uint16_t port =
      server_.start([](const test::Seen& seen) -> test::Reply {
        if (seen.rpc_method == modern::kMethodServerDiscover) {
          return test::Reply::write(test::withBody(
              200, "OK", "application/json",
              "{\"jsonrpc\":\"2.0\",\"id\":\"d\",\"result\":{\"resultType\":"
              "\"complete\",\"supportedVersions\":[\"2026-07-28\"],"
              "\"capabilities\":{}}}",
              std::string()));
        }
        if (seen.rpc_method == modern::kMethodSubscriptionsListen) {
          // The refusal carries no id — that is the whole difficulty, and
          // why the status has to be attributed by something other than the
          // body.
          return test::Reply::write(test::withBody(
              404, "Not Found", "application/json",
              "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32601,"
              "\"message\":\"Method not found\"}}",
              std::string()));
        }
        if (seen.rpc_method == "ping") {
          // Left unanswered, and its connection kept: a request that has
          // already been answered is not one another connection's status
          // can be blamed on, so the fault only shows while it is still
          // outstanding.
          test::Reply held = test::Reply::nothing();
          held.keep_open = true;
          return held;
        }
        return test::Reply::write(test::accepted());
      });

  client::McpClientConfig config;
  config.client_name = "refused-subscription-client";
  config.client_version = "0.0.1";
  config.num_workers = 1;
  config.request_timeout = 5000ms;
  config.protocol_connection_timeout = 5000ms;
  config.streamable_http.fallback_probe_timeout = 700ms;
  client_ = client::createMcpClient(config);
  ASSERT_NE(client_, nullptr);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(
      client_->connect("http://127.0.0.1:" + std::to_string(port) + "/mcp")));

  // Outstanding while the subscription is refused, so its refusal has
  // somewhere wrong to land.
  auto pinged = client_->sendRequest("ping");
  modern::NotificationFilter what;
  what.tools_list_changed = true;
  client_->listen(what, [](const jsonrpc::Notification&) {});

  // Nothing is left behind by a subscription that was refused: the
  // request ends, and a subscription's request ending is the whole of
  // what says the subscription is over, however it ended.
  EXPECT_TRUE(waitUntilTrue(
      [this]() { return client_->subscriptionsHeld() == 0; }, 3000ms))
      << "a refused subscription was still being held";

  // Long enough for the refusal to have arrived and been attributed.
  const bool settled = pinged.wait_for(1500ms) == std::future_status::ready;
  std::string how;
  if (settled) {
    const auto answered = pinged.get();
    how = answered.error.has_value() ? answered.error->message
                                     : std::string("without an error");
  }
  EXPECT_FALSE(settled)
      << "a request nobody had answered was completed by a refusal that "
         "belonged to a subscription: "
      << how;
}

// Letting go of a subscription cuts its stream, and a stream cut short
// names the request whose answer was still arriving. Named from what the
// session recorded going out, that is whichever request went out first —
// which is then asked for again though nothing happened to it.
TEST_F(RefusedSubscriptionTest, EndingOneSeversNoOtherRequest) {
  const uint16_t port =
      server_.start([](const test::Seen& seen) -> test::Reply {
        if (seen.rpc_method == modern::kMethodServerDiscover) {
          return test::Reply::write(test::withBody(
              200, "OK", "application/json",
              "{\"jsonrpc\":\"2.0\",\"id\":\"d\",\"result\":{\"resultType\":"
              "\"complete\",\"supportedVersions\":[\"2026-07-28\"],"
              "\"capabilities\":{}}}",
              std::string()));
        }
        if (seen.rpc_method == modern::kMethodSubscriptionsListen) {
          // A stream that stays open, which is what a subscription is.
          return test::Reply::stream(test::streamPrelude());
        }
        if (seen.rpc_method == "ping") {
          // Outstanding, so that severing has somewhere wrong to land.
          test::Reply held = test::Reply::nothing();
          held.keep_open = false;
          return held;
        }
        return test::Reply::write(test::accepted());
      });

  client::McpClientConfig config;
  config.client_name = "severed-subscription-client";
  config.client_version = "0.0.1";
  config.num_workers = 1;
  config.request_timeout = 5000ms;
  config.protocol_connection_timeout = 5000ms;
  config.streamable_http.fallback_probe_timeout = 700ms;
  client_ = client::createMcpClient(config);
  ASSERT_NE(client_, nullptr);
  ASSERT_TRUE(holds_alternative<std::nullptr_t>(
      client_->connect("http://127.0.0.1:" + std::to_string(port) + "/mcp")));

  auto pinged = client_->sendRequest("ping");
  modern::NotificationFilter what;
  what.tools_list_changed = true;
  const int64_t id = client_->listen(what, [](const jsonrpc::Notification&) {});
  ASSERT_NE(id, 0);
  // Not merely asked for: the peer has to have answered, and the answer
  // has to have been read as the stream it is. Closing before that is
  // closing a connection with no interrupted answer on it, which proves
  // nothing.
  ASSERT_TRUE(server_.waitForRpc(modern::kMethodSubscriptionsListen, 1))
      << "the peer never saw the subscription";
  ASSERT_TRUE(server_.waitForRpc("ping", 1)) << "the peer never saw the ping";
  std::this_thread::sleep_for(300ms);

  // A request believed to have been cut off is picked up again, which
  // is a stream asked for on its behalf. So what says this happened is
  // the peer being asked for one.
  const size_t streams_before = server_.countOfMethod("GET");
  client_->stopListening(id);
  std::this_thread::sleep_for(1500ms);

  EXPECT_EQ(server_.countOfMethod("GET"), streams_before)
      << "a request nobody had answered was picked up again because a "
         "subscription's stream ended";
  EXPECT_EQ(pinged.wait_for(0s), std::future_status::timeout)
      << "a request nobody had answered was completed by a subscription "
         "ending";
}

}  // namespace
}  // namespace mcp
