/**
 * Unit tests for the request-id map key.
 *
 * The property that matters is that the string id "5" and the number 5 stay
 * distinct. JSON-RPC treats them as different requests, so a key that
 * conflates them lets one request's response resolve another's — a
 * correlation bug that only shows up when a peer happens to use string ids.
 */

#include <map>
#include <string>

#include <gtest/gtest.h>

#include "mcp/core/request_id_key.h"

namespace mcp {
namespace {

RequestId stringId(const std::string& value) { return RequestId(value); }

RequestId numberId(int64_t value) { return RequestId(value); }

TEST(RequestIdKeyTest, StringAndNumberWithTheSameTextAreDistinct) {
  const auto text = requestIdKey(stringId("5"));
  const auto number = requestIdKey(numberId(5));

  EXPECT_NE(text, number);
  // Whichever way they order, they must order consistently and not compare
  // equal in both directions.
  EXPECT_NE(text < number, number < text);
}

TEST(RequestIdKeyTest, EqualIdsProduceEqualKeys) {
  EXPECT_EQ(requestIdKey(stringId("abc")), requestIdKey(stringId("abc")));
  EXPECT_EQ(requestIdKey(numberId(42)), requestIdKey(numberId(42)));
}

TEST(RequestIdKeyTest, DifferentIdsOfTheSameKindDiffer) {
  EXPECT_NE(requestIdKey(stringId("abc")), requestIdKey(stringId("abd")));
  EXPECT_NE(requestIdKey(numberId(42)), requestIdKey(numberId(43)));
}

TEST(RequestIdKeyTest, OrderingIsStrictAndTotal) {
  const RequestIdKey keys[] = {
      requestIdKey(numberId(-1)),  requestIdKey(numberId(0)),
      requestIdKey(numberId(7)),   requestIdKey(stringId("")),
      requestIdKey(stringId("a")), requestIdKey(stringId("b")),
  };

  for (const auto& a : keys) {
    EXPECT_FALSE(a < a) << "a key must not order before itself";
    for (const auto& b : keys) {
      if (a == b) {
        EXPECT_FALSE(a < b);
        EXPECT_FALSE(b < a);
      } else {
        // Exactly one direction holds.
        EXPECT_NE(a < b, b < a);
      }
    }
  }
}

TEST(RequestIdKeyTest, NumbersOrderNumericallyNotLexicographically) {
  // Stringifying would put "10" before "9"; a correlation map that walked
  // ids in order would then walk them in the wrong order.
  EXPECT_TRUE(requestIdKey(numberId(9)) < requestIdKey(numberId(10)));
}

TEST(RequestIdKeyTest, KeysWorkInAMap) {
  std::map<RequestIdKey, std::string> pending;
  pending[requestIdKey(numberId(5))] = "number";
  pending[requestIdKey(stringId("5"))] = "string";

  ASSERT_EQ(pending.size(), 2u);
  EXPECT_EQ(pending[requestIdKey(numberId(5))], "number");
  EXPECT_EQ(pending[requestIdKey(stringId("5"))], "string");

  pending.erase(requestIdKey(numberId(5)));
  EXPECT_EQ(pending.size(), 1u);
  EXPECT_EQ(pending.count(requestIdKey(stringId("5"))), 1u);
}

TEST(RequestIdKeyTest, StringFormIsForLogsOnly) {
  EXPECT_EQ(requestIdKeyToString(requestIdKey(stringId("abc"))), "abc");
  EXPECT_EQ(requestIdKeyToString(requestIdKey(numberId(-3))), "-3");
  // Note the two forms collide, which is exactly why the key itself is
  // tagged rather than stringified.
  EXPECT_EQ(requestIdKeyToString(requestIdKey(stringId("5"))),
            requestIdKeyToString(requestIdKey(numberId(5))));
}

}  // namespace
}  // namespace mcp
