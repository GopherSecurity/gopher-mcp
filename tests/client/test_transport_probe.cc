/**
 * Telling a modern server's refusal from every other kind of refusal.
 *
 * This is the one judgement in the detection ladder that decides
 * whether a client stops or falls back, and getting it wrong is
 * expensive in both directions: too eager and a client refuses to talk
 * to a server it could have talked to, too shy and it downgrades a
 * server that was telling it not to. It is a pure function of a status
 * and a body, so it can be asked all of these questions without a
 * server anywhere.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/client/transport_probe.h"

namespace mcp {
namespace client {
namespace {

std::string jsonRpcError(int code, const std::string& message) {
  return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":" +
         std::to_string(code) + ",\"message\":\"" + message + "\"}}";
}

// The codes that mean "you are talking to a newer protocol than you
// think", whichever of them a given server chose.
TEST(ModernRefusalTest, ARecognisedCodeIsAModernRefusal) {
  EXPECT_TRUE(isModernRefusal(
      400, jsonRpcError(modern_error::kHeaderMismatch, "header mismatch")));
  EXPECT_TRUE(isModernRefusal(
      404, jsonRpcError(modern_error::kMethodNotFound, "no such method")));
  EXPECT_TRUE(isModernRefusal(
      400, jsonRpcError(modern_error::kUnsupportedProtocolVersion,
                        "Unsupported protocol version")));
  EXPECT_TRUE(isModernRefusal(
      400, jsonRpcError(modern_error::kMissingRequiredClientCapability,
                        "capability not declared")));
}

// The version complaint as a conformant server actually sends it: a code
// of its own, and a data object naming what it does serve. This was the
// one recorded as having no code — a server sending it would have been
// read as legacy and downgraded.
TEST(ModernRefusalTest, TheVersionComplaintIsRecognisedByItsOwnCode) {
  const std::string body =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32022,\"message\":"
      "\"Unsupported protocol version\",\"data\":{\"supported\":"
      "[\"2026-07-28\"],\"requested\":\"1900-01-01\"}}}";
  EXPECT_TRUE(isModernRefusal(400, body));
  EXPECT_EQ(modern_error::kUnsupportedProtocolVersion, -32022)
      << "the code this ladder watches for is not the one that is sent";
}

// And the complaint that arrives by name rather than by code, in
// whichever of the two places a server puts it.
TEST(ModernRefusalTest, TheVersionComplaintIsRecognisedByName) {
  EXPECT_TRUE(isModernRefusal(
      400, jsonRpcError(-1, "UnsupportedProtocolVersionError: 2025-06-18")));
  EXPECT_TRUE(isModernRefusal(400,
                              "{\"error\":{\"code\":-1,\"data\":{\"type\":"
                              "\"UnsupportedProtocolVersionError\"}}}"));
  EXPECT_TRUE(isModernRefusal(400,
                              "{\"error\":{\"code\":-1,\"data\":"
                              "\"UnsupportedProtocolVersionError\"}}"));
}

// The one that matters most: this project's own answer for a path it
// does not serve. Reading it as a modern server would have a client
// refuse to fall back to the transport that was going to work.
TEST(ModernRefusalTest, AnErrorThatIsNotAnObjectIsNotOne) {
  EXPECT_FALSE(isModernRefusal(404, R"({"error":"not_found"})"));
  EXPECT_FALSE(isModernRefusal(400, R"({"error":["not_found"]})"));
}

// A code this ladder does not recognise says nothing about the era.
TEST(ModernRefusalTest, AnUnrecognisedCodeIsNotOne) {
  EXPECT_FALSE(isModernRefusal(400, jsonRpcError(-32600,
                                                 "Bad Request: session id "
                                                 "is required")));
  EXPECT_FALSE(isModernRefusal(404, jsonRpcError(-32000, "gone")));
}

// Nothing to read is not evidence of anything.
TEST(ModernRefusalTest, NothingToReadIsNotOne) {
  EXPECT_FALSE(isModernRefusal(404, ""));
  EXPECT_FALSE(isModernRefusal(400, "   "));
}

// Neither is something that cannot be read. A plain 404 page is the
// ordinary case here, and it must not throw its way out of the ladder.
TEST(ModernRefusalTest, SomethingUnreadableIsNotOne) {
  EXPECT_FALSE(isModernRefusal(404, "<html><body>Not Found</body></html>"));
  EXPECT_FALSE(isModernRefusal(400, "{\"error\":{\"code\":"));
  EXPECT_FALSE(isModernRefusal(400, "null"));
  EXPECT_FALSE(isModernRefusal(400, "[]"));
}

// The status is half the question. A server that answers a request it
// served with a body that happens to look like this is not refusing
// anything, and stopping the ladder over it would be reading the body
// and ignoring the answer.
TEST(ModernRefusalTest, TheStatusIsPartOfTheQuestion) {
  const std::string modern_body =
      jsonRpcError(modern_error::kHeaderMismatch, "header mismatch");
  EXPECT_FALSE(isModernRefusal(200, modern_body));
  EXPECT_FALSE(isModernRefusal(500, modern_body));
  EXPECT_FALSE(isModernRefusal(403, modern_body));
  // The same body, at the three statuses it would actually arrive with.
  // 405 is one of them: the newest revision serves POST alone, so a
  // client that opened with anything else is told so — and a body naming
  // a modern code alongside it is still a modern server saying no.
  EXPECT_TRUE(isModernRefusal(400, modern_body));
  EXPECT_TRUE(isModernRefusal(404, modern_body));
  EXPECT_TRUE(isModernRefusal(405, modern_body));
}

// But a 405 on its own is not evidence of anything: it is also what an
// ordinary HTTP server says about a method it does not route, and this
// project's own answer for one carries no JSON-RPC error at all.
TEST(ModernRefusalTest, AMethodNotAllowedWithoutAModernBodyIsNotOne) {
  EXPECT_FALSE(isModernRefusal(405, R"({"error":"method_not_allowed"})"));
  EXPECT_FALSE(isModernRefusal(405, ""));
}

// The rung stands empty until there is a request to put on it, and
// standing empty means falling through rather than blocking.
TEST(ModernProbeTest, TheEmptyRungFallsThrough) {
  NoModernProbe probe;
  bool answered = false;
  ProbeResult::Verdict verdict = ProbeResult::Verdict::Modern;
  probe.probe("http://127.0.0.1:1/mcp", [&](const ProbeResult& result) {
    answered = true;
    verdict = result.verdict;
  });
  EXPECT_TRUE(answered) << "a rung that never answers stops the ladder";
  EXPECT_EQ(verdict, ProbeResult::Verdict::NotModern);
}

}  // namespace
}  // namespace client
}  // namespace mcp
