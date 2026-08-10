/**
 * @file test_streamable_http_config.cc
 * @brief Unit tests for streamable HTTP configuration and protocol version
 *        negotiation
 */

#include <gtest/gtest.h>

#include "mcp/client/mcp_client.h"
#include "mcp/protocol/protocol_versions.h"
#include "mcp/server/mcp_server.h"
#include "mcp/transport/streamable_http_config.h"

using namespace mcp::protocol;
using namespace mcp::transport;

class StreamableHttpConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(StreamableHttpConfigTest, ServerDefaults) {
  StreamableHttpConfig config;

  EXPECT_EQ(config.mcp_path, "/mcp");
  EXPECT_TRUE(config.enable_get_stream);
  EXPECT_TRUE(config.enable_sessions);
  EXPECT_TRUE(config.allow_client_termination);
  EXPECT_TRUE(config.enable_resumability);
  EXPECT_EQ(config.replay_buffer_events, 256u);
  EXPECT_EQ(config.session_timeout, std::chrono::milliseconds(300000));
  EXPECT_EQ(config.closed_stream_retention, std::chrono::milliseconds(60000));
  EXPECT_EQ(config.keepalive_interval, std::chrono::milliseconds(30000));
  EXPECT_TRUE(config.require_principal_match);
  EXPECT_FALSE(config.allow_public_bind);
  EXPECT_EQ(config.stream_conn_policy,
            StreamableHttpConfig::StreamConnPolicy::DecoderGate);
  EXPECT_EQ(config.gated_input_buffer_bytes, 64u * 1024u);
  EXPECT_FALSE(config.allow_sse_to_http_1_0);
  EXPECT_TRUE(config.allowed_origins.empty());
  EXPECT_EQ(config.max_get_streams_per_session, 4u);
  EXPECT_TRUE(config.legacy_http_sse_enabled);
}

// Order matters: helpers that pick a latest version take the front entry.
TEST_F(StreamableHttpConfigTest, ServerVersionsAreNewestFirst) {
  StreamableHttpConfig config;

  ASSERT_EQ(config.protocol_versions.size(), 3u);
  EXPECT_EQ(config.protocol_versions[0], "2025-11-25");
  EXPECT_EQ(config.protocol_versions[1], "2025-06-18");
  EXPECT_EQ(config.protocol_versions[2], "2025-03-26");
}

TEST_F(StreamableHttpConfigTest, ClientDefaults) {
  StreamableHttpClientConfig config;

  EXPECT_EQ(config.mcp_path, "/mcp");
  ASSERT_EQ(config.protocol_versions.size(), 3u);
  EXPECT_EQ(config.protocol_versions[0], "2025-11-25");
  EXPECT_EQ(config.protocol_versions[1], "2025-06-18");
  EXPECT_EQ(config.protocol_versions[2], "2025-03-26");

  // A client holds a stream by default: without one, nothing the server
  // says unprompted can reach it.
  EXPECT_TRUE(config.open_server_stream);
  EXPECT_EQ(config.stream_reconnect_min, std::chrono::milliseconds(250));
  EXPECT_EQ(config.stream_reconnect_max, std::chrono::milliseconds(30000));
  EXPECT_LT(config.stream_reconnect_min, config.stream_reconnect_max)
      << "a window that cannot grow is not a backoff";
  EXPECT_EQ(config.resume_attempts, 2u);
  // Silence is not by itself a broken stream, so nothing watches for it
  // until a deployment says how much is too much.
  EXPECT_EQ(config.stream_idle_timeout, std::chrono::milliseconds(0));
}

TEST_F(StreamableHttpConfigTest, ServerAndClientConfigsCarryStreamableHttp) {
  mcp::server::McpServerConfig server_config;
  mcp::client::McpClientConfig client_config;

  EXPECT_EQ(server_config.streamable_http.mcp_path, "/mcp");
  EXPECT_EQ(client_config.streamable_http.mcp_path, "/mcp");
  EXPECT_EQ(server_config.protocol_version, kDefaultProtocolVersion);
  EXPECT_EQ(client_config.protocol_version, kDefaultProtocolVersion);
}

TEST_F(StreamableHttpConfigTest, VersionConstants) {
  EXPECT_STREQ(kProtocolVersion20241105, "2024-11-05");
  EXPECT_STREQ(kProtocolVersion20250326, "2025-03-26");
  EXPECT_STREQ(kProtocolVersion20250618, "2025-06-18");
  EXPECT_STREQ(kProtocolVersion20251125, "2025-11-25");
  EXPECT_STREQ(kProtocolVersion20260728, "2026-07-28");
  EXPECT_STREQ(kDefaultProtocolVersion, "2025-06-18");
  EXPECT_STREQ(kLegacyAssumedVersion, "2025-03-26");
}

TEST_F(StreamableHttpConfigTest, IsSupportedVersion) {
  StreamableHttpConfig config;
  const auto& supported = config.protocol_versions;

  EXPECT_TRUE(isSupportedVersion("2025-11-25", supported));
  EXPECT_TRUE(isSupportedVersion("2025-03-26", supported));
  EXPECT_FALSE(isSupportedVersion("2024-11-05", supported));
  EXPECT_FALSE(isSupportedVersion("2025-06-19", supported));
  EXPECT_FALSE(isSupportedVersion("", supported));
  EXPECT_FALSE(isSupportedVersion("2025-11-25", std::vector<std::string>()));
}

TEST_F(StreamableHttpConfigTest, LatestSupportedVersion) {
  StreamableHttpConfig config;

  EXPECT_EQ(latestSupportedVersion(config.protocol_versions), "2025-11-25");
  EXPECT_EQ(latestSupportedVersion(std::vector<std::string>()),
            kDefaultProtocolVersion);
}

TEST_F(StreamableHttpConfigTest, NegotiationEchoesSupportedRequest) {
  StreamableHttpConfig config;

  EXPECT_EQ(negotiateProtocolVersion("2025-03-26", config.protocol_versions),
            "2025-03-26");
  EXPECT_EQ(negotiateProtocolVersion("2025-06-18", config.protocol_versions),
            "2025-06-18");
}

TEST_F(StreamableHttpConfigTest, NegotiationFallsBackToLatest) {
  StreamableHttpConfig config;

  EXPECT_EQ(negotiateProtocolVersion("2024-11-05", config.protocol_versions),
            "2025-11-25");
  EXPECT_EQ(negotiateProtocolVersion("", config.protocol_versions),
            "2025-11-25");
}

// A server with no supported list speaks exactly one version, whatever the
// peer asks for.
TEST_F(StreamableHttpConfigTest, NegotiationWithEmptySupportedList) {
  std::vector<std::string> supported;

  EXPECT_EQ(negotiateProtocolVersion("2025-11-25", supported),
            kDefaultProtocolVersion);
  EXPECT_EQ(negotiateProtocolVersion("", supported), kDefaultProtocolVersion);

  supported.push_back("2025-06-18");
  EXPECT_EQ(negotiateProtocolVersion("2025-11-25", supported), "2025-06-18");
  EXPECT_EQ(negotiateProtocolVersion("2025-06-18", supported), "2025-06-18");
}

TEST_F(StreamableHttpConfigTest, VersionAtLeast) {
  EXPECT_TRUE(versionAtLeast("2025-06-18", "2025-06-18"));
  EXPECT_TRUE(versionAtLeast("2026-07-28", "2025-11-25"));
  EXPECT_FALSE(versionAtLeast("2024-11-05", "2025-03-26"));
}
