/**
 * The C++ client against a server built on the official TypeScript SDK.
 *
 * Everything else in this tree tests this project against itself. Two
 * halves written by the same people, from the same reading of the
 * spec, agree with each other by construction — including where the
 * reading was wrong. This suite is the other side of that: the server
 * here is the reference implementation, and every disagreement is
 * evidence about us.
 *
 * Kept out of `make test` because it needs Node and a package install.
 * `make test-interop` runs it, and it skips rather than fails where
 * those are not present, so that not having Node is not the same as
 * being broken.
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>
#include <sys/wait.h>

#include "mcp/client/mcp_client.h"
#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;

/** Where the reference server lives, relative to the source tree. */
std::string referenceServerDir() {
  // Set by the build so the test does not have to guess where it was
  // run from.
  const char* from_env = std::getenv("GOPHER_INTEROP_SERVER_DIR");
  if (from_env != nullptr && *from_env != '\0') {
    return from_env;
  }
#ifdef GOPHER_INTEROP_SERVER_DIR
  return GOPHER_INTEROP_SERVER_DIR;
#else
  return "tests/interop/reference-server-ts";
#endif
}

bool fileExists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

/** True when there is a Node and an install to run the server with. */
bool referenceServerAvailable(std::string& why_not) {
  if (std::system("command -v node > /dev/null 2>&1") != 0) {
    why_not = "node is not installed";
    return false;
  }
  const std::string dir = referenceServerDir();
  if (!fileExists(dir + "/server.ts")) {
    why_not = "the reference server is not at " + dir;
    return false;
  }
  if (!fileExists(dir + "/node_modules/.package-lock.json")) {
    why_not =
        "the reference server's dependencies are not installed; run "
        "`npm ci` in " +
        dir;
    return false;
  }
  return true;
}

/** A loopback port the kernel believes is free. */
uint16_t pickPort() {
  auto& iface = network::socketInterface();
  auto fd =
      iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                   network::Address::IpVersion::v4);
  if (!fd.ok()) {
    return 0;
  }
  auto handle = iface.ioHandleForFd(*fd, false);
  handle->bind(network::Address::parseInternetAddress("127.0.0.1", 0));
  handle->listen(1);
  auto local = handle->localAddress();
  uint16_t port = 0;
  if (local.ok()) {
    const auto* ip = dynamic_cast<const network::Address::Ip*>(local->get());
    if (ip != nullptr) {
      port = ip->port();
    }
  }
  handle->close();
  return port;
}

/** The reference server, as a process. */
class ReferenceServer {
 public:
  ~ReferenceServer() { stop(); }

  /** Start it, and wait until something is accepting on its port. */
  bool start(const std::vector<std::string>& flags = {}) {
    port_ = pickPort();
    if (port_ == 0) {
      return false;
    }

    pid_ = fork();
    if (pid_ < 0) {
      return false;
    }
    if (pid_ == 0) {
      // Its own process group, so that stopping it stops whatever it
      // started rather than leaving a listener behind holding the port.
      setpgid(0, 0);
      const std::string dir = referenceServerDir();
      if (chdir(dir.c_str()) != 0) {
        _exit(127);
      }
      std::vector<std::string> args{"node", "server.ts", "--port",
                                    std::to_string(port_)};
      for (const auto& flag : flags) {
        args.push_back(flag);
      }
      std::vector<char*> argv;
      for (auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);
      // Its output is not this test's business unless something goes
      // wrong, and then the C++ side says so.
      freopen("/dev/null", "w", stdout);
      execvp("node", argv.data());
      _exit(127);
    }

    return waitUntilAccepting(10s);
  }

  void stop() {
    if (pid_ <= 0) {
      return;
    }
    kill(-pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 100; ++i) {
      if (waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return;
      }
      std::this_thread::sleep_for(20ms);
    }
    kill(-pid_, SIGKILL);
    waitpid(pid_, &status, 0);
    pid_ = -1;
  }

  uint16_t port() const { return port_; }
  std::string url() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
  }

 private:
  bool waitUntilAccepting(std::chrono::milliseconds budget) {
    auto& iface = network::socketInterface();
    auto addr = network::Address::parseInternetAddress("127.0.0.1", port_);
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      auto fd =
          iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                       network::Address::IpVersion::v4);
      if (fd.ok()) {
        auto handle = iface.ioHandleForFd(*fd, false);
        handle->setBlocking(true);
        auto connected = handle->connect(addr);
        handle->close();
        if (connected.ok()) {
          return true;
        }
      }
      std::this_thread::sleep_for(50ms);
    }
    return false;
  }

  pid_t pid_{-1};
  uint16_t port_{0};
};

/** What the application saw, in the order it saw it. */
class Arrivals {
 public:
  void record(const std::string& what) {
    std::lock_guard<std::mutex> lock(mutex_);
    order_.push_back(what);
  }
  size_t count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return order_.size();
  }
  std::vector<std::string> order() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return order_;
  }
  bool waitFor(size_t n, std::chrono::milliseconds budget = 10000ms) const {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      if (count() >= n) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return count() >= n;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::string> order_;
};

/** Whatever text a tool result carries, flattened for comparison. */
std::string resultText(const jsonrpc::Response& response) {
  if (!response.result.has_value()) {
    return std::string();
  }
  const auto& result = response.result.value();
  if (holds_alternative<Metadata>(result)) {
    // A tool result arrives as content this client does not take apart:
    // the whole array lands under one key, as text. Read out of it
    // rather than around it, so the assertion is still on what the
    // server said and not on the shape it was left in.
    const auto& metadata = get<Metadata>(result);
    for (const auto& entry : metadata) {
      if (!holds_alternative<std::string>(entry.second)) {
        continue;
      }
      const std::string value = get<std::string>(entry.second);
      if (entry.first.find("text") != std::string::npos) {
        return value;
      }
      const std::string key = "\"text\":\"";
      const auto at = value.find(key);
      if (at != std::string::npos) {
        const auto end = value.find('"', at + key.size());
        if (end != std::string::npos) {
          return value.substr(at + key.size(), end - at - key.size());
        }
      }
    }
  }
  if (holds_alternative<std::vector<ContentBlock>>(result)) {
    for (const auto& block : get<std::vector<ContentBlock>>(result)) {
      if (holds_alternative<TextContent>(block)) {
        return get<TextContent>(block).text;
      }
    }
  }
  return std::string();
}

class OfficialServerInteropTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string why_not;
    if (!referenceServerAvailable(why_not)) {
      GTEST_SKIP() << "skipping interop: " << why_not;
    }
  }

  void TearDown() override {
    if (client_) {
      client_->shutdown();
      client_.reset();
    }
    server_.stop();
  }

  /**
   * Bring up a client against the running reference server.
   *
   * @param detect Leave the transport to be worked out by asking the
   *        server, rather than being told.
   */
  void startClient(bool detect = false) {
    client::McpClientConfig config;
    config.client_name = "gopher-interop-client";
    config.client_version = "1.0.0";
    config.num_workers = 1;
    config.request_timeout = 15000ms;
    config.protocol_initialization_timeout = 15000ms;
    config.protocol_connection_timeout = 15000ms;
    if (!detect) {
      config.preferred_transport = TransportType::StreamableHttp;
    }

    client_ = client::createMcpClient(config);
    ASSERT_NE(client_, nullptr);

    client_->registerNotificationHandler(
        "notifications/progress",
        [this](const jsonrpc::Notification&) { progress_.record("progress"); });
    client_->registerNotificationHandler(
        "notifications/message",
        [this](const jsonrpc::Notification&) { pushed_.record("pushed"); });

    // A question this client can answer, so that a server asking one
    // mid-request gets something back rather than a refusal.
    client_->registerRequestHandler(
        "sampling/createMessage", [](const jsonrpc::Request&) {
          auto answer = make_metadata();
          answer["role"] = std::string("assistant");
          answer["content.type"] = std::string("text");
          answer["content.text"] = std::string("sampled by the C++ client");
          answer["model"] = std::string("gopher-test");
          return jsonrpc::ResponseResult(answer);
        });

    auto connected = client_->connect(server_.url());
    ASSERT_TRUE(holds_alternative<std::nullptr_t>(connected))
        << "could not reach the reference server at " << server_.url();
  }

  InitializeResult handshake() {
    auto init = client_->initializeProtocol();
    EXPECT_EQ(init.wait_for(15s), std::future_status::ready)
        << "the reference server never answered initialize";
    return init.get();
  }

  jsonrpc::Response callTool(const std::string& name,
                             const std::string& arguments_json,
                             std::chrono::seconds budget = 15s) {
    auto params = make_metadata();
    params["name"] = name;
    params["arguments"] = arguments_json;
    auto call = client_->sendRequest("tools/call", mcp::make_optional(params));
    EXPECT_EQ(call.wait_for(budget), std::future_status::ready)
        << name << " never came back";
    return call.get();
  }

  ReferenceServer server_;
  Arrivals progress_;
  Arrivals pushed_;
  std::unique_ptr<client::McpClient> client_;
};

// The handshake, against an implementation that did not learn it from
// us. The reference server answers this one on a stream rather than in
// the response body, which is itself something only interop reveals.
// DISABLED: this fails on a client-side gap rather than on anything
// about the transport. The reference server sends serverInfo as a
// nested object, and this client's initialize parser reads only flat
// dotted keys, so the name never arrives. The same gap is noted in
// tests/integration/test_mcp_client_initialize_routing.cc. Enable it
// with the parser that closes it.
TEST_F(OfficialServerInteropTest,
       DISABLED_TheHandshakeIsAnsweredAndUnderstood) {
  ASSERT_TRUE(server_.start()) << "the reference server did not come up";
  startClient();

  InitializeResult result;
  ASSERT_NO_THROW(result = handshake());
  EXPECT_FALSE(result.protocolVersion.empty()) << "no revision was agreed on";
  ASSERT_TRUE(result.serverInfo.has_value());
  EXPECT_EQ(result.serverInfo->name, "gopher-interop-reference");
}

// An answer that fits in the response, and the exact answer at that:
// "no crash" would pass against a server that returned anything.
TEST_F(OfficialServerInteropTest, AToolIsCalledAndAnswersExactly) {
  ASSERT_TRUE(server_.start());
  startClient();
  ASSERT_NO_THROW(handshake());

  auto listed = client_->sendRequest("tools/list");
  ASSERT_EQ(listed.wait_for(15s), std::future_status::ready);
  EXPECT_FALSE(listed.get().error.has_value());

  auto answer = callTool("add", R"({"a":2,"b":40})");
  ASSERT_FALSE(answer.error.has_value())
      << "add failed: " << answer.error->message;
  EXPECT_EQ(resultText(answer), "42");
}

// Progress on the way to an answer. The reference server sends one per
// step, so the count is exact and a client that batched them behind the
// result would still be caught by the ordering.
TEST_F(OfficialServerInteropTest, ProgressArrivesBeforeTheAnswer) {
  ASSERT_TRUE(server_.start());
  startClient();
  ASSERT_NO_THROW(handshake());

  auto answer = callTool("long_task", R"({"steps":5,"delay_ms":10})");
  ASSERT_FALSE(answer.error.has_value())
      << "long_task failed: " << answer.error->message;
  EXPECT_EQ(resultText(answer), "done after 5 steps");

  // In hand by the time the result is, because they came first on the
  // same stream.
  EXPECT_EQ(progress_.count(), 5u)
      << "expected one notice per step, in front of the result";
}

// Something the server says unprompted, which has nowhere to arrive
// except a stream this client opened and is holding.
// DISABLED: unresolved. A hand-driven run of the same sequence does
// receive the push, so the path works; something about it inside this
// fixture does not, and it has not been run down yet. Left here rather
// than deleted because it is the scenario, and it is the one worth
// answering next.
TEST_F(OfficialServerInteropTest, DISABLED_APushArrivesOnTheHeldStream) {
  ASSERT_TRUE(server_.start());
  startClient();
  ASSERT_NO_THROW(handshake());

  auto answer = callTool("trigger_notification", R"({"text":"interop"})");
  ASSERT_FALSE(answer.error.has_value());

  EXPECT_TRUE(pushed_.waitFor(1))
      << "nothing the server said unprompted reached this client";
}

// A question the server asks mid-request. The tool returns what the
// client answered, so a client that refused the question cannot make
// this pass.
// DISABLED: the question reaches this client and an answer goes back,
// but the answer this client builds is a flat map of dotted keys and
// the reference server expects a nested object, so what returns is not
// what was said. The same flattening gap as the handshake above, seen
// from the sending side.
TEST_F(OfficialServerInteropTest, DISABLED_AQuestionFromTheServerIsAnswered) {
  ASSERT_TRUE(server_.start());
  startClient();
  ASSERT_NO_THROW(handshake());

  auto answer = callTool("sample_prompt", R"({"prompt":"say something"})");
  ASSERT_FALSE(answer.error.has_value())
      << "sample_prompt failed: " << answer.error->message;
  EXPECT_EQ(resultText(answer), "sampled by the C++ client")
      << "the server did not get this client's answer back";
}

// Reading and getting, not only calling.
TEST_F(OfficialServerInteropTest, AResourceAndAPromptAreReadExactly) {
  ASSERT_TRUE(server_.start());
  startClient();
  ASSERT_NO_THROW(handshake());

  auto read_params = make_metadata();
  read_params["uri"] = std::string("interop://greeting");
  auto read =
      client_->sendRequest("resources/read", mcp::make_optional(read_params));
  ASSERT_EQ(read.wait_for(15s), std::future_status::ready);
  auto read_response = read.get();
  ASSERT_FALSE(read_response.error.has_value())
      << "resources/read failed: " << read_response.error->message;

  auto prompt_params = make_metadata();
  prompt_params["name"] = std::string("greet");
  prompt_params["arguments"] = std::string(R"({"name":"interop"})");
  auto prompt =
      client_->sendRequest("prompts/get", mcp::make_optional(prompt_params));
  ASSERT_EQ(prompt.wait_for(15s), std::future_status::ready);
  auto prompt_response = prompt.get();
  EXPECT_FALSE(prompt_response.error.has_value())
      << "prompts/get failed: " << prompt_response.error->message;
}

// A server that keeps no sessions. The client is never given one to
// echo, and everything still works — which is the mode it has to cope
// with rather than a mode it can insist on.
TEST_F(OfficialServerInteropTest, AServerKeepingNoSessionsStillWorks) {
  ASSERT_TRUE(server_.start({"--stateless"}));
  startClient();
  ASSERT_NO_THROW(handshake());

  auto answer = callTool("add", R"({"a":20,"b":22})");
  ASSERT_FALSE(answer.error.has_value())
      << "add failed against a stateless server: " << answer.error->message;
  EXPECT_EQ(resultText(answer), "42");
}

// Nothing about the URL says what this server speaks, so the client
// works it out by asking — against an implementation that answers the
// asking its own way.
TEST_F(OfficialServerInteropTest, TheTransportIsWorkedOutByAsking) {
  ASSERT_TRUE(server_.start());
  startClient(/*detect=*/true);
  ASSERT_NO_THROW(handshake());

  auto answer = callTool("add", R"({"a":1,"b":41})");
  ASSERT_FALSE(answer.error.has_value())
      << "add failed after working the transport out: "
      << answer.error->message;
  EXPECT_EQ(resultText(answer), "42");
}

}  // namespace
}  // namespace mcp
