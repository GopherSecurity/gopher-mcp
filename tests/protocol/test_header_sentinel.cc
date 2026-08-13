/**
 * Values that have to survive a trip through an HTTP header.
 *
 * A header value may hold only visible ASCII, spaces and tabs, and its
 * leading and trailing whitespace is not preserved. Everything else
 * travels as `=?base64?…?=`. Both ends must agree exactly about which
 * values need that and what they decode back to, because a server
 * compares the decoded header against the body and refuses the request
 * when they differ — so a disagreement here is not a corrupted value, it
 * is a request that cannot be made at all.
 *
 * These are pure functions, so the whole table can be asked without a
 * server anywhere.
 */

#include <string>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/protocol/header_sentinel.h"

namespace mcp {
namespace protocol {
namespace modern {
namespace {

/** Every row of the published examples table, in both directions. */
struct Example {
  const char* value;
  const char* header;
  const char* why;
};

const Example kExamples[] = {
    {"us-west1", "us-west1", "plain ASCII travels as itself"},
    {"Hello, \xe4\xb8\x96\xe7\x95\x8c",
     "=?base64?SGVsbG8sIOS4lueVjA==?=", "contains non-ASCII"},
    {" padded ", "=?base64?IHBhZGRlZCA=?=", "leading and trailing spaces"},
    {"line1\nline2", "=?base64?bGluZTEKbGluZTI=?=", "contains a newline"},
    {"=?base64?literal?=", "=?base64?PT9iYXNlNjQ/bGl0ZXJhbD89?=",
     "matches the sentinel pattern"},
};

TEST(HeaderSentinel, EveryPublishedExampleEncodesAsPublished) {
  for (const auto& row : kExamples) {
    EXPECT_EQ(encodeHeaderValue(row.value), row.header) << row.why;
  }
}

TEST(HeaderSentinel, EveryPublishedExampleDecodesBack) {
  for (const auto& row : kExamples) {
    std::string decoded;
    ASSERT_TRUE(decodeHeaderValue(row.header, &decoded)) << row.why;
    EXPECT_EQ(decoded, row.value) << row.why;
  }
}

// The row an implementation working from intuition gets wrong. A value
// that already looks encoded must be encoded, or the other end decodes
// something nobody encoded and the two disagree about a value neither of
// them changed.
TEST(HeaderSentinel, AValueThatLooksEncodedIsEncoded) {
  const std::string literal = "=?base64?literal?=";
  EXPECT_FALSE(isHeaderSafe(literal))
      << "a value wearing the markers was sent as itself";
  EXPECT_NE(encodeHeaderValue(literal), literal);

  std::string round_tripped;
  ASSERT_TRUE(decodeHeaderValue(encodeHeaderValue(literal), &round_tripped));
  EXPECT_EQ(round_tripped, literal);
}

TEST(HeaderSentinel, WhatCanTravelAsItselfDoes) {
  EXPECT_TRUE(isHeaderSafe("get_weather"));
  EXPECT_TRUE(isHeaderSafe("file:///projects/myapp/config.json"));
  EXPECT_TRUE(isHeaderSafe(""));
  EXPECT_TRUE(isHeaderSafe("a b"));

  EXPECT_FALSE(isHeaderSafe(" leading"));
  EXPECT_FALSE(isHeaderSafe("trailing "));
  EXPECT_FALSE(isHeaderSafe("\ttab-led"));
  EXPECT_FALSE(isHeaderSafe(std::string("nul\0inside", 10)));
  EXPECT_FALSE(isHeaderSafe("carriage\rreturn"));
}

// A header wearing the markers with rubbish inside is malformed, not a
// literal. Reading it as text would let a caller send anything at all by
// wrapping it in markers that decode to nothing.
TEST(HeaderSentinel, MarkersAroundRubbishAreRefused) {
  std::string decoded;
  EXPECT_FALSE(decodeHeaderValue("=?base64?not base64!?=", &decoded));
  EXPECT_FALSE(decodeHeaderValue("=?base64?SGVsbG8?=", &decoded))
      << "a length that is not a multiple of four is not base64";
  // The backslash is load-bearing: "??=" is a trigraph, and without it
  // this string is the four characters "=?base64#".
  EXPECT_FALSE(decodeHeaderValue("=?base64?\?=", &decoded))
      << "markers around nothing at all decoded to something";
  EXPECT_FALSE(decodeHeaderValue("=?base64?SGV=bG8=?=", &decoded))
      << "padding in the middle is not padding";
}

// The markers are lowercase and exact. Something that merely resembles
// them is an ordinary value, and decoding it would change it.
TEST(HeaderSentinel, TheMarkersAreExact) {
  std::string decoded;
  ASSERT_TRUE(decodeHeaderValue("=?BASE64?SGVsbG8=?=", &decoded));
  EXPECT_EQ(decoded, "=?BASE64?SGVsbG8=?=")
      << "an uppercase marker was treated as the sentinel";

  ASSERT_TRUE(decodeHeaderValue("=?base64?SGVsbG8=", &decoded));
  EXPECT_EQ(decoded, "=?base64?SGVsbG8=") << "a missing suffix decoded anyway";
}

TEST(HeaderSentinel, AScalarHasOneHeaderForm) {
  std::string text;

  ASSERT_TRUE(headerTextForScalar(json::JsonValue("us-west1"), &text));
  EXPECT_EQ(text, "us-west1");

  ASSERT_TRUE(
      headerTextForScalar(json::JsonValue(static_cast<int64_t>(42)), &text));
  EXPECT_EQ(text, "42");

  ASSERT_TRUE(
      headerTextForScalar(json::JsonValue(static_cast<int64_t>(-7)), &text));
  EXPECT_EQ(text, "-7");

  ASSERT_TRUE(headerTextForScalar(json::JsonValue(true), &text));
  EXPECT_EQ(text, "true") << "a boolean is lowercase";
  ASSERT_TRUE(headerTextForScalar(json::JsonValue(false), &text));
  EXPECT_EQ(text, "false");

  // Only those three may be designated, so anything else is a body this
  // header could not have come from.
  EXPECT_FALSE(headerTextForScalar(json::JsonValue::object(), &text));
  EXPECT_FALSE(headerTextForScalar(json::JsonValue::array(), &text));
  EXPECT_FALSE(headerTextForScalar(json::JsonValue(), &text));
}

// Numbers are compared as numbers. A body carrying 42.0 and a header
// carrying 42 are one number written twice, and comparing their text
// would refuse a request that is perfectly well formed.
TEST(HeaderSentinel, ANumberWrittenTwoWaysIsOneNumber) {
  EXPECT_TRUE(
      headerMatchesValue("42", json::JsonValue(static_cast<int64_t>(42))));
  EXPECT_TRUE(headerMatchesValue("42", json::JsonValue(42.0)));
  EXPECT_TRUE(
      headerMatchesValue("42.0", json::JsonValue(static_cast<int64_t>(42))));
  EXPECT_FALSE(
      headerMatchesValue("43", json::JsonValue(static_cast<int64_t>(42))));
  EXPECT_FALSE(headerMatchesValue("forty-two",
                                  json::JsonValue(static_cast<int64_t>(42))));

  // Two integers a double cannot tell apart are still two integers.
  // Comparing them as one would let a header carry a value the server
  // never read out of the body, which is the split the mirroring exists
  // to prevent.
  const int64_t big = 9007199254740992LL;
  EXPECT_TRUE(headerMatchesValue("9007199254740992", json::JsonValue(big)));
  EXPECT_FALSE(headerMatchesValue("9007199254740993", json::JsonValue(big)))
      << "two different integers were treated as one";
  EXPECT_FALSE(
      headerMatchesValue("9007199254740992", json::JsonValue(big + 1)));

  // An integer written with a fraction that changes nothing is the same
  // integer, and one written with a fraction that changes it is not.
  EXPECT_TRUE(
      headerMatchesValue("42.000", json::JsonValue(static_cast<int64_t>(42))));
  EXPECT_FALSE(
      headerMatchesValue("42.5", json::JsonValue(static_cast<int64_t>(42))));

  // And nothing gets through by being ignored. A header that is a number
  // followed by anything is a header that does not say that number —
  // reading it as one would leave a router acting on a value the server
  // never saw, which is the whole thing this is for.
  EXPECT_FALSE(
      headerMatchesValue("42junk", json::JsonValue(static_cast<int64_t>(42))));
  EXPECT_FALSE(
      headerMatchesValue("42 ", json::JsonValue(static_cast<int64_t>(42))));
  EXPECT_FALSE(
      headerMatchesValue("0x2a", json::JsonValue(static_cast<int64_t>(42))));
  EXPECT_FALSE(
      headerMatchesValue("4e1", json::JsonValue(static_cast<int64_t>(40))))
      << "an exponent is not a form this comparison accepts";

  // Nor by being rounded on the way in: a decimal a double would fold
  // onto the body value is still a different number.
  EXPECT_FALSE(headerMatchesValue("9007199254740993.0", json::JsonValue(big)))
      << "a decimal that a double cannot tell from the body was matched";

  // A float body is held to the same standard.
  EXPECT_TRUE(headerMatchesValue("1.5", json::JsonValue(1.5)));
  EXPECT_FALSE(headerMatchesValue("1.5junk", json::JsonValue(1.5)));
}

TEST(HeaderSentinel, AHeaderIsDecodedBeforeItIsCompared) {
  const json::JsonValue body("Hello, \xe4\xb8\x96\xe7\x95\x8c");
  EXPECT_TRUE(headerMatchesValue("=?base64?SGVsbG8sIOS4lueVjA==?=", body));
  EXPECT_FALSE(headerMatchesValue("Hello, ???", body));

  // And a malformed one matches nothing at all.
  EXPECT_FALSE(headerMatchesValue("=?base64?not base64!?=", body));
}

}  // namespace
}  // namespace modern
}  // namespace protocol
}  // namespace mcp
