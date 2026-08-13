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

#include <map>
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

// The other end of the same shape. What a handler asked for has to come
// back off the wire as what it asked for, or the client answers the
// wrong questions.
TEST(Mrtr, AQuestionIsReadBackAsTheQuestionItWas) {
  NeedsInput needed;
  InputRequest who;
  who.method = kMethodElicitation;
  who.params = json::JsonValue::object();
  who.params.set("message", json::JsonValue("Who are you?"));
  needed.requests["who"] = who;
  needed.request_state = mcp::make_optional(std::string("round-1"));

  const auto asked = askedForIn(renderInputRequired(needed));
  ASSERT_TRUE(asked.asked);
  ASSERT_EQ(asked.requests.size(), 1u);
  ASSERT_EQ(asked.requests.count("who"), 1u)
      << "the name a question was asked under did not survive, so its "
         "answer could not be matched to it";
  EXPECT_EQ(asked.requests.at("who").method, kMethodElicitation);
  EXPECT_EQ(asked.requests.at("who").params["message"].getString(),
            "Who are you?");
  ASSERT_TRUE(asked.request_state.has_value());
  EXPECT_EQ(asked.request_state.value(), "round-1");
}

// An answer is an answer. A server of an older revision says nothing
// about what kind of result it is sending, and reading silence as a
// question would make every one of its answers a retry.
TEST(Mrtr, AnAnswerIsNotMistakenForAQuestion) {
  json::JsonValue complete = json::JsonValue::object();
  complete.set(kResultTypeField, json::JsonValue(kResultTypeComplete));
  EXPECT_FALSE(askedForIn(complete).asked);

  json::JsonValue silent = json::JsonValue::object();
  silent.set("tools", json::JsonValue::array());
  EXPECT_FALSE(askedForIn(silent).asked);

  EXPECT_FALSE(askedForIn(json::JsonValue("not even an object")).asked);
}

// A question that asks for nothing and carries nothing would be answered
// by sending the identical request again — and answered the same way,
// without end.
TEST(Mrtr, AQuestionThatAsksNothingIsNotOne) {
  json::JsonValue empty = json::JsonValue::object();
  empty.set(kResultTypeField, json::JsonValue(kResultTypeInputRequired));
  EXPECT_FALSE(askedForIn(empty).asked);

  json::JsonValue unanswerable = json::JsonValue::object();
  unanswerable.set(kResultTypeField, json::JsonValue(kResultTypeInputRequired));
  json::JsonValue requests = json::JsonValue::object();
  requests.set("who", json::JsonValue::object());  // no method to ask of
  unanswerable.set(kInputRequestsField, requests);
  EXPECT_FALSE(askedForIn(unanswerable).asked)
      << "a question naming nothing to ask was taken as answerable";
}

// The state is carried, not read. A server that encoded JSON into it gets
// the same bytes back rather than something that meant the same.
TEST(Mrtr, TheStateIsCarriedAcrossUntouched) {
  const std::string awkward = "{\"round\":2,\"who\":\"unverified\"}";
  json::JsonValue answer = json::JsonValue::object();
  answer.set(kResultTypeField, json::JsonValue(kResultTypeInputRequired));
  answer.set(kRequestStateField, json::JsonValue(awkward));

  const auto asked = askedForIn(answer);
  ASSERT_TRUE(asked.asked);
  ASSERT_TRUE(asked.request_state.has_value());
  EXPECT_EQ(asked.request_state.value(), awkward);
}

// Every name asked about comes back, including the ones nothing was
// found for: a server that asked two questions and gets one key back
// cannot tell which of the two went unanswered.
TEST(Mrtr, TheAnswersComeBackUnderTheNamesTheyWereAskedFor) {
  std::map<std::string, json::JsonValue> answers;
  answers["who"] = json::JsonValue::object();
  answers["who"].set("action", json::JsonValue("accept"));
  answers["where"] = json::JsonValue();  // nothing could be found

  const auto rendered = renderInputResponses(answers);
  ASSERT_TRUE(rendered.isObject());
  EXPECT_TRUE(rendered.contains("who"));
  EXPECT_TRUE(rendered.contains("where"))
      << "a question the client could not answer vanished, leaving the "
         "server unable to tell which one it was";
  EXPECT_EQ(rendered["who"]["action"].getString(), "accept");
}

}  // namespace
}  // namespace modern
}  // namespace protocol
}  // namespace mcp
