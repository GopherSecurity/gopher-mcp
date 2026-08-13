/**
 * Asking a client for something without sending it a request.
 *
 * The whole point of the shape is that the two rounds are independent:
 * nothing is remembered between them, and everything that has to survive
 * travels in the body. So what these tests are about is what goes into
 * that body and what can be read back out of it — and, above all, that a
 * server never asks a caller for something the caller said it cannot do,
 * since such a question would sit unanswerable with no way to say so.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/protocol/mrtr.h"

namespace mcp {
namespace protocol {
namespace modern {
namespace {

InputRequest elicit(const std::string& message) {
  InputRequest request;
  request.method = kMethodElicitation;
  request.params = json::JsonValue::object();
  request.params.set("message", json::JsonValue(message));
  return request;
}

InputRequest sample(const std::string& prompt) {
  InputRequest request;
  request.method = kMethodSampling;
  request.params = json::JsonValue::object();
  request.params.set("systemPrompt", json::JsonValue(prompt));
  return request;
}

TEST(Mrtr, AQuestionSaysItIsNotAnAnswer) {
  NeedsInput needed;
  needed.requests["github_login"] = elicit("Your GitHub username");
  needed.request_state = mcp::make_optional(std::string("opaque-blob"));

  const auto result = renderInputRequired(needed);

  EXPECT_EQ(result[kResultTypeField].getString(), kResultTypeInputRequired)
      << "a question was sent wearing the shape of an answer";
  ASSERT_TRUE(result.contains(kInputRequestsField));
  const auto& asked = result[kInputRequestsField]["github_login"];
  EXPECT_EQ(asked["method"].getString(), kMethodElicitation);
  EXPECT_EQ(asked["params"]["message"].getString(), "Your GitHub username");
  EXPECT_EQ(result[kRequestStateField].getString(), "opaque-blob");
}

// Either half may stand alone: a server may carry state forward without
// asking anything, or ask without needing to remember.
TEST(Mrtr, EitherHalfMayStandAlone) {
  NeedsInput asking;
  asking.requests["who"] = elicit("Who are you?");
  auto result = renderInputRequired(asking);
  EXPECT_TRUE(result.contains(kInputRequestsField));
  EXPECT_FALSE(result.contains(kRequestStateField));

  NeedsInput remembering;
  remembering.request_state = mcp::make_optional(std::string("round-2"));
  result = renderInputRequired(remembering);
  EXPECT_FALSE(result.contains(kInputRequestsField));
  EXPECT_EQ(result[kRequestStateField].getString(), "round-2");

  // And neither is nothing to say, which is not a question at all.
  EXPECT_TRUE(NeedsInput().empty());
  EXPECT_FALSE(asking.empty());
  EXPECT_FALSE(remembering.empty());
}

// A server must never ask for something the caller said it cannot do:
// the question would sit unanswerable, and the client would have no way
// to say so.
TEST(Mrtr, AskingForWhatTheCallerCannotDoIsCaught) {
  InputRequests both;
  both["form"] = elicit("Your name");
  both["guess"] = sample("Be brief");

  EXPECT_TRUE(
      capabilitiesMissingFor(both, R"({"elicitation":{},"sampling":{}})")
          .empty());

  auto missing = capabilitiesMissingFor(both, R"({"elicitation":{}})");
  ASSERT_EQ(missing.size(), 1u);
  EXPECT_EQ(missing[0], "sampling");

  missing = capabilitiesMissingFor(both, "{}");
  ASSERT_EQ(missing.size(), 2u);

  // A caller that declared nothing readable declared nothing. The wrong
  // way to be wrong here is the permissive one.
  EXPECT_EQ(capabilitiesMissingFor(both, "").size(), 2u);
  EXPECT_EQ(capabilitiesMissingFor(both, "not json").size(), 2u);
}

// Two questions needing one capability name it once: the refusal says
// what to declare, not how many times it was wanted.
TEST(Mrtr, ACapabilityIsNamedOnce) {
  InputRequests twice;
  twice["first"] = sample("one");
  twice["second"] = sample("two");

  const auto missing = capabilitiesMissingFor(twice, "{}");
  ASSERT_EQ(missing.size(), 1u);
  EXPECT_EQ(missing[0], "sampling");

  const auto data = requiredCapabilitiesData(missing);
  ASSERT_TRUE(data.contains(kRequiredCapabilitiesField));
  EXPECT_TRUE(data[kRequiredCapabilitiesField].contains("sampling"))
      << "the refusal did not say what the caller would have to declare";
}

// Present is not declared. A client that said it cannot elicit said so
// with the key there, and reading the key alone would ask it anyway.
TEST(Mrtr, SayingNoIsNotSayingYes) {
  InputRequests asking;
  asking["form"] = elicit("Your name");

  EXPECT_TRUE(capabilitiesMissingFor(asking, R"({"elicitation":{}})").empty());
  EXPECT_TRUE(
      capabilitiesMissingFor(asking, R"({"elicitation":true})").empty());

  EXPECT_FALSE(
      capabilitiesMissingFor(asking, R"({"elicitation":false})").empty())
      << "a client that said it cannot do this was going to be asked";
  EXPECT_FALSE(
      capabilitiesMissingFor(asking, R"({"elicitation":null})").empty());
}

// Something this revision does not define has no capability to check it
// against, and a request that cannot be checked cannot be shown to be
// supported. Sending it anyway would ask a client for something it never
// said it can do — which is the one thing this exists to prevent.
TEST(Mrtr, ARequestThatCannotBeCheckedIsNotSent) {
  InputRequests odd;
  InputRequest future;
  future.method = "something/newer";
  future.params = json::JsonValue::object();
  odd["x"] = future;

  const auto missing = capabilitiesMissingFor(odd, R"({"sampling":{}})");
  ASSERT_FALSE(missing.empty())
      << "a request nothing could vouch for was going to be sent";
  EXPECT_NE(missing[0].find("something/newer"), std::string::npos)
      << "the refusal did not say what could not be checked: " << missing[0];
}

// What a retry brought back, read out of the body it travelled in.
TEST(Mrtr, WhatARetryCarriesIsReadBack) {
  const auto params = json::JsonValue::parse(
      R"({"name":"do_it","inputResponses":{"github_login":{"action":"accept"}},
          "requestState":"opaque-blob"})");

  const auto carried = carriedInputOf(params);
  EXPECT_FALSE(carried.empty());
  EXPECT_EQ(carried.responses["github_login"]["action"].getString(), "accept");
  ASSERT_TRUE(carried.request_state.has_value());
  EXPECT_EQ(carried.request_state.value(), "opaque-blob");
}

// A first attempt carries neither, which is the ordinary case — a
// handler sees them only once it has asked for something.
TEST(Mrtr, AFirstAttemptCarriesNeither) {
  const auto carried =
      carriedInputOf(json::JsonValue::parse(R"({"name":"do_it"})"));
  EXPECT_TRUE(carried.empty());
  EXPECT_FALSE(carried.request_state.has_value());
  EXPECT_TRUE(carried.responses.isObject());
}

// The state comes back as bytes. Nothing here parses it, because it
// arrived through the client and a server that let it decide anything
// has to treat it as something an attacker wrote.
TEST(Mrtr, TheStateComesBackUntouched) {
  const std::string awkward = "{\"looks\":\"like json\"} and then some";
  json::JsonValue params = json::JsonValue::object();
  params.set(kRequestStateField, json::JsonValue(awkward));

  const auto carried = carriedInputOf(params);
  ASSERT_TRUE(carried.request_state.has_value());
  EXPECT_EQ(carried.request_state.value(), awkward);
}

// And only the three that can be answered may be answered this way.
TEST(Mrtr, OnlySomeRequestsMayBeAnsweredWithAQuestion) {
  EXPECT_TRUE(mayAskForInput(kMethodToolsCall));
  EXPECT_TRUE(mayAskForInput(kMethodResourcesRead));
  EXPECT_TRUE(mayAskForInput(kMethodPromptsGet));

  EXPECT_FALSE(mayAskForInput("tools/list"));
  EXPECT_FALSE(mayAskForInput(kMethodServerDiscover));
  EXPECT_FALSE(mayAskForInput(kMethodSubscriptionsListen));
}

}  // namespace
}  // namespace modern
}  // namespace protocol
}  // namespace mcp
