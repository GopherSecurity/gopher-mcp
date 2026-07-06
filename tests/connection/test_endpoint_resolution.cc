/**
 * Unit tests for SSE callback endpoint resolution.
 *
 * The server's "endpoint" SSE event announces where the client must POST
 * its JSON-RPC requests. Unless an external URL is configured the server
 * emits a relative form ("callback/{id}"), but the client's POST path
 * requires an absolute URL — before resolution existed, every relative
 * endpoint was rejected as invalid, initialize never reached the server,
 * and no HTTP+SSE session could be established against this SDK's own
 * server.
 *
 * Rules under test: relative endpoints resolve against the configured
 * server address (same scheme and host:port as the SSE stream itself);
 * absolute endpoints pass through untouched; no base means no resolution.
 */

#include <gtest/gtest.h>

#include "mcp/mcp_connection_manager.h"

using mcp::McpConnectionManager;

namespace {

TEST(EndpointResolutionTest, RelativeEndpointResolvesAgainstServerAddress) {
  EXPECT_EQ(McpConnectionManager::resolveEndpointUrl(
                "callback/client_1", "127.0.0.1:8080", /*use_ssl=*/false),
            "http://127.0.0.1:8080/callback/client_1");
}

TEST(EndpointResolutionTest, AbsolutePathEndpointResolves) {
  EXPECT_EQ(McpConnectionManager::resolveEndpointUrl(
                "/callback/client_1", "127.0.0.1:8080", /*use_ssl=*/false),
            "http://127.0.0.1:8080/callback/client_1");
}

TEST(EndpointResolutionTest, SslBaseYieldsHttps) {
  EXPECT_EQ(McpConnectionManager::resolveEndpointUrl(
                "callback/client_1", "example.com:443", /*use_ssl=*/true),
            "https://example.com:443/callback/client_1");
}

// Reverse-proxy deployments configure external_url and the server then
// announces an absolute callback URL — it must pass through unchanged,
// even if it points at a different host than the SSE stream.
TEST(EndpointResolutionTest, AbsoluteEndpointPassesThrough) {
  const std::string absolute = "https://proxy.example.com/mcp/callback/c1";
  EXPECT_EQ(McpConnectionManager::resolveEndpointUrl(absolute, "127.0.0.1:8080",
                                                     /*use_ssl=*/false),
            absolute);
}

// Without a configured server address there is nothing to resolve
// against; the endpoint is passed through and the POST path reports the
// invalid URL, which is more diagnosable than fabricating a base.
TEST(EndpointResolutionTest, NoBasePassesThrough) {
  EXPECT_EQ(McpConnectionManager::resolveEndpointUrl("callback/client_1", "",
                                                     /*use_ssl=*/false),
            "callback/client_1");
}

TEST(EndpointResolutionTest, EmptyEndpointYieldsBaseRoot) {
  EXPECT_EQ(McpConnectionManager::resolveEndpointUrl("", "127.0.0.1:8080",
                                                     /*use_ssl=*/false),
            "http://127.0.0.1:8080/");
}

}  // namespace
