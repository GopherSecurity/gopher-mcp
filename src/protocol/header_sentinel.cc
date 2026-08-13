/**
 * Header values that survive the trip. See the header for the rules.
 */

#include "mcp/protocol/header_sentinel.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace mcp {
namespace protocol {
namespace modern {

namespace {

const char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** The value of one base64 character, or -1 for anything that is not one. */
int sixBits(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return -1;
}

std::string toBase64(const std::string& bytes) {
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i + 2 < bytes.size()) {
    const uint32_t triple = (static_cast<unsigned char>(bytes[i]) << 16) |
                            (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                            static_cast<unsigned char>(bytes[i + 2]);
    out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
    out.push_back(kAlphabet[(triple >> 6) & 0x3f]);
    out.push_back(kAlphabet[triple & 0x3f]);
    i += 3;
  }

  const size_t left = bytes.size() - i;
  if (left == 1) {
    const uint32_t triple = static_cast<unsigned char>(bytes[i]) << 16;
    out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
    out.append("==");
  } else if (left == 2) {
    const uint32_t triple = (static_cast<unsigned char>(bytes[i]) << 16) |
                            (static_cast<unsigned char>(bytes[i + 1]) << 8);
    out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
    out.push_back(kAlphabet[(triple >> 6) & 0x3f]);
    out.push_back('=');
  }
  return out;
}

/**
 * Strict on purpose. Anything that is not exactly base64 — a stray
 * character, a length that is not a multiple of four, padding in the
 * middle — is a malformed header rather than something to salvage.
 */
bool fromBase64(const std::string& text, std::string* bytes) {
  if (text.empty() || (text.size() % 4) != 0) {
    return false;
  }

  size_t padding = 0;
  if (text[text.size() - 1] == '=') {
    ++padding;
    if (text.size() >= 2 && text[text.size() - 2] == '=') {
      ++padding;
    }
  }

  std::string out;
  out.reserve((text.size() / 4) * 3);

  for (size_t i = 0; i < text.size(); i += 4) {
    uint32_t quad = 0;
    for (size_t j = 0; j < 4; ++j) {
      const char c = text[i + j];
      if (c == '=') {
        // Only the last group may be padded, and only at its end.
        const bool last_group = i + 4 >= text.size();
        const bool tail = j >= 4 - padding;
        if (!last_group || !tail) {
          return false;
        }
        quad <<= 6;
        continue;
      }
      const int value = sixBits(c);
      if (value < 0) {
        return false;
      }
      quad = (quad << 6) | static_cast<uint32_t>(value);
    }

    out.push_back(static_cast<char>((quad >> 16) & 0xff));
    if (padding < 2 || i + 4 < text.size()) {
      out.push_back(static_cast<char>((quad >> 8) & 0xff));
    }
    if (padding < 1 || i + 4 < text.size()) {
      out.push_back(static_cast<char>(quad & 0xff));
    }
  }

  *bytes = out;
  return true;
}

/**
 * The exact integer a header names, if it names one.
 *
 * Accepts "42", "-7", and an integer written with a fractional part that
 * is all zeros — "42.0" is the same integer as 42, and a body carrying
 * one may be mirrored as the other. Rejects everything else: trailing
 * text, a non-zero fraction, an exponent, or a magnitude that will not
 * fit. Nothing here goes through a double, because the whole point is to
 * tell apart values a double cannot.
 */
bool exactIntegerFrom(const std::string& text, int64_t* out) {
  if (text.empty()) {
    return false;
  }

  size_t at = 0;
  if (text[at] == '+' || text[at] == '-') {
    ++at;
  }
  const size_t digits_begin = at;
  while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
    ++at;
  }
  if (at == digits_begin) {
    return false;
  }
  const std::string whole = text.substr(0, at);

  if (at < text.size()) {
    // Only a fractional part, and only one that changes nothing.
    if (text[at] != '.') {
      return false;
    }
    ++at;
    for (; at < text.size(); ++at) {
      if (text[at] != '0') {
        return false;
      }
    }
  }

  try {
    size_t consumed = 0;
    const long long parsed = std::stoll(whole, &consumed);
    if (consumed != whole.size()) {
      return false;
    }
    *out = static_cast<int64_t>(parsed);
    return true;
  } catch (const std::exception&) {
    // Out of range, which is not this integer whatever else it is.
    return false;
  }
}

bool wearsSentinel(const std::string& value) {
  const std::string prefix(kSentinelPrefix);
  const std::string suffix(kSentinelSuffix);
  return value.size() >= prefix.size() + suffix.size() &&
         value.compare(0, prefix.size(), prefix) == 0 &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

}  // namespace

bool isHeaderSafe(const std::string& value) {
  if (value.empty()) {
    return true;
  }
  // Whitespace at either end is not carried: a header value is trimmed on
  // its way through, so a value that ends in a space would arrive as a
  // different value and be refused for not matching its own body.
  if (value.front() == ' ' || value.front() == '\t' || value.back() == ' ' ||
      value.back() == '\t') {
    return false;
  }
  for (const char c : value) {
    const unsigned char byte = static_cast<unsigned char>(c);
    // Visible ASCII, space and horizontal tab, and nothing else.
    if (byte != 0x09 && (byte < 0x20 || byte > 0x7e)) {
      return false;
    }
  }
  // A value that already looks encoded has to be encoded, or the other
  // end would decode a value nobody encoded.
  return !wearsSentinel(value);
}

std::string encodeHeaderValue(const std::string& value) {
  if (isHeaderSafe(value)) {
    return value;
  }
  return std::string(kSentinelPrefix) + toBase64(value) + kSentinelSuffix;
}

bool decodeHeaderValue(const std::string& header, std::string* value) {
  if (value == nullptr) {
    return false;
  }
  if (!wearsSentinel(header)) {
    *value = header;
    return true;
  }

  const size_t prefix = std::string(kSentinelPrefix).size();
  const size_t suffix = std::string(kSentinelSuffix).size();
  const std::string encoded =
      header.substr(prefix, header.size() - prefix - suffix);

  std::string decoded;
  if (!fromBase64(encoded, &decoded)) {
    return false;
  }
  *value = decoded;
  return true;
}

bool headerTextForScalar(const json::JsonValue& value, std::string* text) {
  if (text == nullptr) {
    return false;
  }
  if (value.isString()) {
    *text = value.getString();
    return true;
  }
  if (value.isBoolean()) {
    *text = value.getBool() ? "true" : "false";
    return true;
  }
  if (value.isInteger()) {
    // 64 bits: a header carrying a large integer must carry the one the
    // body holds, and getInt would truncate it to 32.
    *text = std::to_string(value.getInt64());
    return true;
  }
  return false;
}

bool headerMatchesValue(const std::string& header,
                        const json::JsonValue& value) {
  std::string decoded;
  if (!decodeHeaderValue(header, &decoded)) {
    return false;
  }

  // A number written two ways is one number. A body carrying 42.0 and a
  // header carrying 42 agree, and comparing their text would say they do
  // not.
  if (value.isInteger()) {
    // Read exactly, and never through a double: two integers a double
    // cannot tell apart are still two integers, and treating them as one
    // would let a header say something the server never read. Anything
    // that is not exactly this integer — trailing text, a fraction that
    // changes it, a magnitude that will not fit — is not a match.
    int64_t from_header = 0;
    if (!exactIntegerFrom(decoded, &from_header)) {
      return false;
    }
    // getInt64 rather than getInt, which is 32 bits wide and would
    // truncate the very values this comparison exists to tell apart.
    return from_header == value.getInt64();
  }
  if (value.isFloat()) {
    try {
      size_t consumed = 0;
      const double from_header = std::stod(decoded, &consumed);
      // All of it, or none of it: "1.5junk" is not 1.5 with something
      // ignored, it is a header that does not say 1.5.
      return consumed == decoded.size() && from_header == value.getFloat();
    } catch (const std::exception&) {
      return false;
    }
  }

  std::string expected;
  if (!headerTextForScalar(value, &expected)) {
    return false;
  }
  return decoded == expected;
}

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
