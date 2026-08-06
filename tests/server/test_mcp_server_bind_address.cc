/**
 * @file test_mcp_server_bind_address.cc
 * @brief Which addresses a server agrees to listen on
 *
 * listen() only records what it was asked for; the bind happens later on
 * the dispatcher thread. That makes listen() the last place a refusal can
 * be reported to the caller, so it is where the decision has to be made —
 * and these tests make the decision without ever starting a listener.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/server/mcp_server.h"

namespace mcp {
namespace server {
namespace {

McpServerConfig configFor(bool allow_public_bind) {
  McpServerConfig config;
  config.server_name = "bind-address-test";
  config.streamable_http.allow_public_bind = allow_public_bind;
  return config;
}

bool accepted(const VoidResult& result) {
  return holds_alternative<std::nullptr_t>(result);
}

std::string reason(const VoidResult& result) {
  return accepted(result) ? std::string() : get<Error>(result).message;
}

class McpServerBindAddressTest : public ::testing::Test {
 protected:
  void TearDown() override {
    if (server_) {
      server_->shutdown();
    }
    server_.reset();
  }

  VoidResult tryListen(const std::string& address, bool allow_public_bind) {
    server_ = createMcpServer(configFor(allow_public_bind));
    return server_->listen(address);
  }

  std::unique_ptr<McpServer> server_;
};

TEST_F(McpServerBindAddressTest, LoopbackIsServedWithoutBeingAskedTwice) {
  EXPECT_TRUE(accepted(tryListen("http://127.0.0.1:0", false)));
}

TEST_F(McpServerBindAddressTest, LocalhostMeansLoopback) {
  EXPECT_TRUE(accepted(tryListen("http://localhost:0", false)));
}

TEST_F(McpServerBindAddressTest, AnAddressWithNoHostIsLoopback) {
  // Not "every interface", which is what leaving the host out used to
  // mean: an address nobody specified is the least reachable one.
  EXPECT_TRUE(accepted(tryListen("http://", false)));
}

TEST_F(McpServerBindAddressTest, EveryInterfaceNeedsSayingSo) {
  const VoidResult refused = tryListen("http://0.0.0.0:0", false);

  ASSERT_FALSE(accepted(refused))
      << "binding every interface puts the endpoint on the network";
  EXPECT_NE(reason(refused).find("allow_public_bind"), std::string::npos)
      << "the refusal must name what would allow it: " << reason(refused);
  EXPECT_NE(reason(refused).find("0.0.0.0"), std::string::npos)
      << "and which address it refused: " << reason(refused);
}

TEST_F(McpServerBindAddressTest, EveryInterfaceIsServedOnceAskedFor) {
  EXPECT_TRUE(accepted(tryListen("http://0.0.0.0:0", true)));
}

TEST_F(McpServerBindAddressTest, ARoutableAddressNeedsSayingSoToo) {
  // Not just the any-address: a specific interface reachable from the
  // network is equally a decision to expose the server.
  const VoidResult refused = tryListen("http://192.0.2.10:0", false);

  ASSERT_FALSE(accepted(refused));
  EXPECT_NE(reason(refused).find("allow_public_bind"), std::string::npos)
      << reason(refused);
}

TEST_F(McpServerBindAddressTest, SomethingThatIsNotAnAddressIsRefused) {
  const VoidResult refused = tryListen("http://not-a-host:0", false);

  ASSERT_FALSE(accepted(refused))
      << "a name this server cannot resolve to an address it can bind is "
         "worse ignored than refused";
  EXPECT_NE(reason(refused).find("not-a-host"), std::string::npos)
      << reason(refused);
}

TEST_F(McpServerBindAddressTest, StdioHasNoAddressToCheck) {
  EXPECT_TRUE(accepted(tryListen("stdio://", false)));
}

}  // namespace
}  // namespace server
}  // namespace mcp
