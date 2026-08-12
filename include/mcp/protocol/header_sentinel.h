/**
 * Carrying an arbitrary value in an HTTP header, and getting it back.
 *
 * The newest revision mirrors body values — a tool name, a resource URI,
 * a designated argument — into headers so that an intermediary can route
 * on them. But a header value is not an arbitrary string: it may hold
 * only visible ASCII, spaces and tabs, and leading or trailing whitespace
 * is not preserved. A tool called "天気" or an argument with a newline in
 * it has to travel some other way.
 *
 * So a value that cannot travel as itself travels as
 * `=?base64?{base64 of its UTF-8}?=`. The markers are lowercase and
 * exact, and a server compares a decoded header against the body rather
 * than the header as it arrived.
 *
 * The subtle rule, and the one an implementation working from intuition
 * gets wrong: a plain-ASCII value that *itself* looks like the sentinel
 * must also be encoded. Otherwise `=?base64?literal?=` in the body would
 * arrive as a header the other end would decode, and the two would
 * disagree about a value neither of them changed.
 */

#pragma once

#include <string>

#include "mcp/json/json_bridge.h"

namespace mcp {
namespace protocol {
namespace modern {

/** The markers that say a header value is not what it appears to be. */
constexpr const char* kSentinelPrefix = "=?base64?";
constexpr const char* kSentinelSuffix = "?=";

/**
 * Whether this value can be carried as itself.
 *
 * False for anything outside visible ASCII, anything with leading or
 * trailing whitespace, and anything already wearing the sentinel.
 */
bool isHeaderSafe(const std::string& value);

/** The header form of a value: itself, or the sentinel around its base64. */
std::string encodeHeaderValue(const std::string& value);

/**
 * What a header value means.
 *
 * A value not wearing the sentinel means itself. One wearing it means
 * what its base64 decodes to — and if that is not valid base64 the
 * header is malformed, which is a refusal rather than a literal: reading
 * it as text would let a client send anything at all by wrapping it in
 * markers that decode to nothing.
 *
 * @return False when the sentinel is worn but the contents are not
 *         base64. `value` is untouched then.
 */
bool decodeHeaderValue(const std::string& header, std::string* value);

/**
 * The header text for a scalar taken out of a request body.
 *
 * A string is itself, an integer is decimal, and a boolean is lowercase.
 * Anything else — a float, an object, an array, null — has no header
 * form: the revision permits only these three to be designated, and a
 * value of any other type is a body this header could not have come
 * from.
 *
 * @return False for a value with no header form, leaving `text` alone.
 */
bool headerTextForScalar(const json::JsonValue& value, std::string* text);

/**
 * Whether a header value and a body value are the same value.
 *
 * Not a string comparison. The header is decoded first, and numbers are
 * compared numerically, because a body carrying `42.0` and a header
 * carrying `42` are the same number written twice.
 */
bool headerMatchesValue(const std::string& header,
                        const json::JsonValue& value);

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
