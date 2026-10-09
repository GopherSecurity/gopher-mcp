// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * RequestStateSealer: a requestState sealed to a caller, a request and an
 * expiry opens only for that same caller and request, before it expires,
 * under a key that is still configured; and anything else, however it is
 * wrong, simply doesn't open.
 */

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/protocol/request_state_sealer.h"

namespace mcp {
namespace protocol {
namespace modern {
namespace {

using namespace std::chrono_literals;
using Key = RequestStateSealer::Key;

const Key kCurrent{"k2", std::string(32, 'a')};
const Key kPrevious{"k1", std::string(40, 'b')};

/** A clock the test moves by hand. */
struct ManualClock {
  std::chrono::system_clock::time_point now =
      std::chrono::system_clock::time_point(std::chrono::seconds(1800000000));
  RequestStateSealer::Clock fn() {
    return [this]() { return now; };
  }
};

RequestStateContext deployCall(const std::string& principal = "alice") {
  RequestStateContext context;
  context.principal = principal;
  context.method = "tools/call";
  context.params = json::JsonValue::parse(
      R"({"name":"deploy","arguments":{"env":"production"}})");
  return context;
}

TEST(RequestStateSealer, ASealedStateOpensForTheSameCallAndCaller) {
  RequestStateSealer sealer({kCurrent});
  for (const std::string& state :
       {std::string("approved:deploy"), std::string(),
        std::string("\x00\x01\xff binary", 10), std::string(5000, 'x')}) {
    const std::string sealed = sealer.seal(state, deployCall());
    EXPECT_NE(sealed, state);
    const auto opened = sealer.open(sealed, deployCall());
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened.value(), state);
  }
}

// What travels is base64url: safe in a JSON string and in a header.
TEST(RequestStateSealer, ASealedStateIsBase64url) {
  RequestStateSealer sealer({kCurrent});
  const std::string sealed =
      sealer.seal("approved:deploy \"quoted\"\n", deployCall());
  for (char c : sealed) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    EXPECT_TRUE(ok) << "unexpected character '" << c << "' in " << sealed;
  }
}

// Any change at all to what was sealed is caught.
TEST(RequestStateSealer, AnyEditIsDetected) {
  RequestStateSealer sealer({kCurrent});
  const std::string sealed = sealer.seal("approved:deploy", deployCall());
  for (size_t i = 0; i < sealed.size(); ++i) {
    std::string edited = sealed;
    edited[i] = edited[i] == 'A' ? 'B' : 'A';
    EXPECT_FALSE(sealer.open(edited, deployCall()).has_value())
        << "an edit at " << i << " went unnoticed";
  }
}

// Bound to the caller and to the request it was issued for.
TEST(RequestStateSealer, ItOpensForNoOtherCallerOrRequest) {
  RequestStateSealer sealer({kCurrent});
  const std::string sealed = sealer.seal("approved:deploy", deployCall());

  EXPECT_FALSE(sealer.open(sealed, deployCall("mallory")).has_value())
      << "another caller presented it";
  EXPECT_FALSE(sealer.open(sealed, deployCall("")).has_value());

  RequestStateContext other_method = deployCall();
  other_method.method = "prompts/get";
  EXPECT_FALSE(sealer.open(sealed, other_method).has_value());

  RequestStateContext other_params = deployCall();
  other_params.params = json::JsonValue::parse(
      R"({"name":"deploy","arguments":{"env":"staging"}})");
  EXPECT_FALSE(sealer.open(sealed, other_params).has_value())
      << "it was presented on a different call";
}

// What a retry adds to the params doesn't change which request it is.
TEST(RequestStateSealer, TheRetrysOwnFieldsAreNotPartOfTheRequest) {
  RequestStateSealer sealer({kCurrent});
  const std::string sealed = sealer.seal("approved:deploy", deployCall());

  RequestStateContext retry = deployCall();
  retry.params.set("requestState", json::JsonValue(sealed));
  retry.params.set("inputResponses",
                   json::JsonValue::parse(R"({"who":{"action":"accept"}})"));
  retry.params.set("_meta", json::JsonValue::parse(R"({"trace":"t-1"})"));
  const auto opened = sealer.open(sealed, retry);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(opened.value(), "approved:deploy");
}

TEST(RequestStateSealer, ItExpires) {
  ManualClock clock;
  RequestStateSealer sealer({kCurrent}, 60s, clock.fn());
  const std::string sealed = sealer.seal("approved:deploy", deployCall());

  clock.now += 59s;
  EXPECT_TRUE(sealer.open(sealed, deployCall()).has_value());
  clock.now += 1s;
  EXPECT_FALSE(sealer.open(sealed, deployCall()).has_value())
      << "an expired state still opened";
}

// The newest key seals; every configured key verifies until it is removed.
TEST(RequestStateSealer, KeysRotate) {
  RequestStateSealer before({kPrevious});
  const std::string old_state = before.seal("old", deployCall());

  RequestStateSealer rotating({kCurrent, kPrevious});
  const auto opened = rotating.open(old_state, deployCall());
  ASSERT_TRUE(opened.has_value()) << "a state sealed before rotation broke";
  EXPECT_EQ(opened.value(), "old");

  // What it seals now, only the new key opens.
  const std::string new_state = rotating.seal("new", deployCall());
  EXPECT_FALSE(before.open(new_state, deployCall()).has_value());
  RequestStateSealer after({kCurrent});
  EXPECT_TRUE(after.open(new_state, deployCall()).has_value());

  // And once the old key is gone, so are its states.
  EXPECT_FALSE(after.open(old_state, deployCall()).has_value());
}

// The same key id with a different secret is a different key.
TEST(RequestStateSealer, AnotherSecretUnderTheSameIdDoesNotOpenIt) {
  RequestStateSealer sealer({kCurrent});
  RequestStateSealer impostor({Key{kCurrent.id, std::string(32, 'z')}});
  const std::string sealed = impostor.seal("approved:deploy", deployCall());
  EXPECT_FALSE(sealer.open(sealed, deployCall()).has_value());
}

// Nothing malformed throws; it just doesn't open.
TEST(RequestStateSealer, MalformedValuesNeverThrow) {
  RequestStateSealer sealer({kCurrent});
  const std::string sealed = sealer.seal("approved:deploy", deployCall());
  const std::vector<std::string> malformed = {
      "",
      "approved:deploy",
      "!!!!",
      sealed + "=",
      sealed + "A",
      sealed.substr(0, sealed.size() - 1),
      sealed.substr(0, 10),
      "A",
      std::string(4096, 'A'),
  };
  for (const auto& value : malformed) {
    SCOPED_TRACE(value.substr(0, 40));
    optional<std::string> opened;
    EXPECT_NO_THROW(opened = sealer.open(value, deployCall()));
    EXPECT_FALSE(opened.has_value());
  }
}

// A key that could not protect anything is refused when it is configured.
TEST(RequestStateSealer, UnusableKeysAreRefused) {
  EXPECT_THROW(RequestStateSealer({}), std::invalid_argument);
  EXPECT_THROW(RequestStateSealer({Key{"short", std::string(31, 'a')}}),
               std::invalid_argument);
  EXPECT_THROW(RequestStateSealer({Key{"has space", std::string(32, 'a')}}),
               std::invalid_argument);
  EXPECT_THROW(RequestStateSealer({Key{"", std::string(32, 'a')}}),
               std::invalid_argument);
  EXPECT_THROW(
      RequestStateSealer({kCurrent, Key{kCurrent.id, kPrevious.secret}}),
      std::invalid_argument);
  EXPECT_THROW(RequestStateSealer({kCurrent}, 0s), std::invalid_argument);
}

}  // namespace
}  // namespace modern
}  // namespace protocol
}  // namespace mcp
