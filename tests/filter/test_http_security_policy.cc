/**
 * @file test_http_security_policy.cc
 * @brief Which origins are served, and what CORS headers come back
 *
 * Pure value tests: no socket, no dispatcher, no filter. The question here
 * is only what the policy decides, which is worth pinning literally —
 * a default-allow set that drifts by one entry either locks out every
 * browser client or opens the server to any page a user visits.
 */

#include <gtest/gtest.h>

#include "mcp/filter/http_security_filter.h"
#include "mcp/json/json_bridge.h"

namespace mcp {
namespace filter {
namespace {

RequestSecurity from(const std::string& origin) {
  RequestSecurity security;
  security.origin = origin;
  return security;
}

// ===== The default set =====

TEST(HttpSecurityPolicyTest, LocalPagesAreServedByDefault) {
  HttpSecurityPolicy policy;

  // A locally installed server is reachable from a page on this machine
  // and from nowhere else. Every form of "this machine" counts, because a
  // browser picks between them without asking.
  EXPECT_TRUE(policy.originAllowed("http://localhost"));
  EXPECT_TRUE(policy.originAllowed("http://localhost:3000"));
  EXPECT_TRUE(policy.originAllowed("https://localhost:8443"));
  EXPECT_TRUE(policy.originAllowed("http://127.0.0.1:6274"));
  EXPECT_TRUE(policy.originAllowed("https://127.0.0.1"));
  EXPECT_TRUE(policy.originAllowed("http://[::1]:3000"));
}

TEST(HttpSecurityPolicyTest, TheCaseOfAnOriginDoesNotMatter) {
  HttpSecurityPolicy policy;

  EXPECT_TRUE(policy.originAllowed("HTTP://LocalHost:3000"));
}

TEST(HttpSecurityPolicyTest, PagesFromAnywhereElseAreRefused) {
  HttpSecurityPolicy policy;

  EXPECT_FALSE(policy.originAllowed("http://evil.example"));
  EXPECT_FALSE(policy.originAllowed("https://evil.example:3000"));
  // Names that merely start or end with a local one are different hosts.
  EXPECT_FALSE(policy.originAllowed("http://localhost.evil.example"));
  EXPECT_FALSE(policy.originAllowed("http://notlocalhost"));
  // A private address is not this machine, whatever the network topology.
  EXPECT_FALSE(policy.originAllowed("http://192.168.1.10:3000"));
}

TEST(HttpSecurityPolicyTest, OnlyHttpSchemesReachTheDefaultSet) {
  HttpSecurityPolicy policy;

  EXPECT_FALSE(policy.originAllowed("ftp://localhost"));
  EXPECT_FALSE(policy.originAllowed("file://localhost"));
}

TEST(HttpSecurityPolicyTest, SomethingThatIsNotAnOriginIsRefused) {
  HttpSecurityPolicy policy;

  // A sandboxed frame sends this, and it names no host to trust.
  EXPECT_FALSE(policy.originAllowed("null"));
  EXPECT_FALSE(policy.originAllowed("localhost:3000"));
  // An origin has no path; anything carrying one was not sent by a browser
  // and must not be matched as though it were.
  EXPECT_FALSE(policy.originAllowed("http://localhost/../evil"));
  EXPECT_FALSE(policy.originAllowed("http://evil.example@localhost"));
  EXPECT_FALSE(policy.originAllowed("http://localhost:notaport"));
}

TEST(HttpSecurityPolicyTest, NoOriginIsServed) {
  HttpSecurityPolicy policy;

  // Every non-browser client: it reached the port on its own, so there is
  // no cross-site question to answer.
  EXPECT_TRUE(policy.originAllowed(""));
}

// ===== A configured set =====

TEST(HttpSecurityPolicyTest, AConfiguredListReplacesTheDefaultSet) {
  HttpSecurityPolicy policy;
  policy.setAllowedOrigins({"https://app.example.com"});

  EXPECT_TRUE(policy.originAllowed("https://app.example.com"));
  // Naming an origin narrows the server to it; localhost is no longer
  // implied, or a developer's own machine would stay open in production.
  EXPECT_FALSE(policy.originAllowed("http://localhost:3000"));
  EXPECT_FALSE(policy.originAllowed("https://app.example.com:8443"));
}

TEST(HttpSecurityPolicyTest, AWildcardEntryServesAnyone) {
  HttpSecurityPolicy policy;
  policy.setAllowedOrigins({"*"});

  EXPECT_TRUE(policy.originAllowed("http://evil.example"));
}

TEST(HttpSecurityPolicyTest, AWildcardStillReflectsTheOrigin) {
  HttpSecurityPolicy policy;
  policy.setAllowedOrigins({"*"});

  const auto headers = policy.responseHeaders(from("http://evil.example"));

  // Reflected rather than answered with "*": a wildcard is not usable
  // once credentials are allowed, and the header should not have to be
  // rewritten the day they are.
  EXPECT_EQ(headers.at("Access-Control-Allow-Origin"), "http://evil.example");
}

// ===== Response headers =====

TEST(HttpSecurityPolicyTest, AnAnswerReflectsTheOriginThatAskedForIt) {
  HttpSecurityPolicy policy;

  const auto headers = policy.responseHeaders(from("http://localhost:3000"));

  EXPECT_EQ(headers.at("Access-Control-Allow-Origin"), "http://localhost:3000");
  EXPECT_EQ(headers.at("Vary"), "Origin")
      << "a shared cache must not serve one origin's answer to another";
  EXPECT_EQ(headers.at("Access-Control-Expose-Headers"), "Mcp-Session-Id")
      << "without this a browser cannot read the session it was just given";
}

TEST(HttpSecurityPolicyTest, AnAnswerToNoOriginCarriesNothing) {
  HttpSecurityPolicy policy;

  EXPECT_TRUE(policy.responseHeaders(from("")).empty())
      << "there is no browser to read them and nothing to reflect";
  EXPECT_TRUE(policy.preflightHeaders(from("")).empty());
}

// ===== Preflight =====

TEST(HttpSecurityPolicyTest, PreflightAdvertisesEveryMethodTheEndpointServes) {
  HttpSecurityPolicy policy;

  const auto headers = policy.preflightHeaders(from("http://localhost:3000"));

  // DELETE included: a browser that cannot preflight it cannot end its
  // own session.
  EXPECT_EQ(headers.at("Access-Control-Allow-Methods"),
            "POST, GET, DELETE, OPTIONS");
  EXPECT_EQ(headers.at("Access-Control-Max-Age"), "86400");
}

TEST(HttpSecurityPolicyTest, PreflightAdvertisesEveryHeaderTheTransportSends) {
  HttpSecurityPolicy policy;

  const std::string allowed =
      policy.preflightHeaders(from("http://localhost:3000"))
          .at("Access-Control-Allow-Headers");

  for (const char* name :
       {"Content-Type", "Accept", "Authorization", "Mcp-Session-Id",
        "MCP-Protocol-Version", "Last-Event-ID", "Mcp-Method", "Mcp-Name"}) {
    EXPECT_NE(allowed.find(name), std::string::npos)
        << name << " missing from: " << allowed;
  }
}

TEST(HttpSecurityPolicyTest, PreflightCarriesTheCorsHeadersToo) {
  HttpSecurityPolicy policy;

  const auto headers = policy.preflightHeaders(from("http://localhost:3000"));

  EXPECT_EQ(headers.at("Access-Control-Allow-Origin"), "http://localhost:3000");
  EXPECT_EQ(headers.at("Vary"), "Origin");
}

TEST(HttpSecurityPolicyTest, DesignatedHeadersAreAskedForEveryTime) {
  HttpSecurityPolicy policy;
  std::vector<std::string> registered;
  policy.setExtraAllowedHeaders([&registered]() { return registered; });

  EXPECT_EQ(policy.preflightHeaders(from("http://localhost:3000"))
                .at("Access-Control-Allow-Headers")
                .find("Mcp-Param-Region"),
            std::string::npos);

  // Tools can be registered at any point in a server's life, so the set is
  // read when a preflight is answered rather than captured once.
  registered.push_back("Mcp-Param-Region");

  EXPECT_NE(policy.preflightHeaders(from("http://localhost:3000"))
                .at("Access-Control-Allow-Headers")
                .find("Mcp-Param-Region"),
            std::string::npos);
}

TEST(HttpSecurityPolicyTest, ADesignatedHeaderIsNamedOnlyOnce) {
  HttpSecurityPolicy policy;
  policy.setExtraAllowedHeaders([]() {
    return std::vector<std::string>{"Mcp-Param-Region", "mcp-param-region",
                                    "accept"};
  });

  const std::string allowed =
      policy.preflightHeaders(from("http://localhost:3000"))
          .at("Access-Control-Allow-Headers");

  size_t count = 0;
  for (size_t at = allowed.find("egion"); at != std::string::npos;
       at = allowed.find("egion", at + 1)) {
    ++count;
  }
  EXPECT_EQ(count, 1u) << allowed;

  count = 0;
  for (size_t at = allowed.find("ccept"); at != std::string::npos;
       at = allowed.find("ccept", at + 1)) {
    ++count;
  }
  EXPECT_EQ(count, 1u) << "header names differ only by case: " << allowed;
}

// ===== Designated parameters =====

Tool toolWithSchema(const std::string& name, const std::string& schema) {
  Tool tool(name);
  tool.inputSchema = mcp::make_optional(json::JsonValue::parse(schema));
  return tool;
}

TEST(HttpSecurityPolicyTest, ADesignatedParameterBecomesAHeaderName) {
  const Tool tool = toolWithSchema("search", R"({
    "type": "object",
    "properties": {
      "query": {"type": "string"},
      "region": {"type": "string", "x-mcp-header": true}
    }
  })");

  const auto names = HttpSecurityPolicy::paramHeadersFor(tool);

  ASSERT_EQ(names.size(), 1u);
  EXPECT_EQ(names[0], "Mcp-Param-region");
}

TEST(HttpSecurityPolicyTest, ADesignationMayNameTheHeaderItself) {
  const Tool tool = toolWithSchema("search", R"({
    "properties": {"region": {"x-mcp-header": "Region"}}
  })");

  const auto names = HttpSecurityPolicy::paramHeadersFor(tool);

  ASSERT_EQ(names.size(), 1u);
  EXPECT_EQ(names[0], "Mcp-Param-Region");
}

TEST(HttpSecurityPolicyTest, ADeclinedDesignationIsNotAHeader) {
  const Tool tool = toolWithSchema("search", R"({
    "properties": {"region": {"x-mcp-header": false}}
  })");

  EXPECT_TRUE(HttpSecurityPolicy::paramHeadersFor(tool).empty());
}

TEST(HttpSecurityPolicyTest, ANestedDesignationContributesItsOwnName) {
  const Tool tool = toolWithSchema("search", R"({
    "properties": {
      "filter": {
        "type": "object",
        "properties": {"region": {"x-mcp-header": true}}
      }
    }
  })");

  const auto names = HttpSecurityPolicy::paramHeadersFor(tool);

  // The leaf name, not a path: the names have to be unique across the
  // whole schema anyway, so a path would say nothing extra.
  ASSERT_EQ(names.size(), 1u);
  EXPECT_EQ(names[0], "Mcp-Param-region");
}

TEST(HttpSecurityPolicyTest, AToolWithNoSchemaDesignatesNothing) {
  EXPECT_TRUE(HttpSecurityPolicy::paramHeadersFor(Tool("noop")).empty());

  const Tool empty = toolWithSchema("empty", R"({"type": "object"})");
  EXPECT_TRUE(HttpSecurityPolicy::paramHeadersFor(empty).empty());
}

}  // namespace
}  // namespace filter
}  // namespace mcp
