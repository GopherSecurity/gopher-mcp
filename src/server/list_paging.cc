// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "mcp/server/list_paging.h"

#include <array>
#include <stdexcept>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace mcp {
namespace server {
namespace paging {

namespace {

constexpr const char* kCursorVersion = "c2";
// Half of an HMAC-SHA256, which is still far beyond guessing.
constexpr size_t kTagBytes = 16;

/** Made once per process, never written down anywhere. */
const std::array<unsigned char, 32>& cursorKey() {
  static const std::array<unsigned char, 32> key = []() {
    std::array<unsigned char, 32> made{};
    if (RAND_bytes(made.data(), static_cast<int>(made.size())) != 1) {
      throw std::runtime_error("could not make a key for list cursors");
    }
    return made;
  }();
  return key;
}

std::string toHex(const unsigned char* bytes, size_t length) {
  static const char* digits = "0123456789abcdef";
  std::string hex;
  hex.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    hex += digits[bytes[i] >> 4];
    hex += digits[bytes[i] & 0x0f];
  }
  return hex;
}

bool fromHex(const std::string& hex, std::string* bytes) {
  if (hex.size() % 2 != 0) {
    return false;
  }
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    return -1;
  };
  std::string out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int high = nibble(hex[i]);
    const int low = nibble(hex[i + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    out += static_cast<char>((high << 4) | low);
  }
  *bytes = out;
  return true;
}

/** The tag over everything a cursor says, before the tag itself. */
std::string tagOf(const std::string& body) {
  const auto& key = cursorKey();
  unsigned char mac[EVP_MAX_MD_SIZE];
  unsigned int mac_length = 0;
  if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
           reinterpret_cast<const unsigned char*>(body.data()), body.size(),
           mac, &mac_length) == nullptr ||
      mac_length < kTagBytes) {
    throw std::runtime_error("could not sign a list cursor");
  }
  return toHex(mac, kTagBytes);
}

}  // namespace

std::string makeCursor(const std::string& list, const std::string& last_key) {
  const std::string body =
      std::string(kCursorVersion) + "." + list + "." +
      toHex(reinterpret_cast<const unsigned char*>(last_key.data()),
            last_key.size());
  return body + "." + tagOf(body);
}

bool readCursor(const std::string& cursor,
                const std::string& list,
                std::string* last_key) {
  const std::string prefix = std::string(kCursorVersion) + "." + list + ".";
  if (cursor.size() < prefix.size() + 1 + kTagBytes * 2 ||
      cursor.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }
  const size_t dot = cursor.rfind('.');
  if (dot == std::string::npos || dot < prefix.size() - 1 ||
      cursor.size() - dot - 1 != kTagBytes * 2) {
    return false;
  }
  const std::string body = cursor.substr(0, dot);
  const std::string tag = cursor.substr(dot + 1);
  const std::string expected = tagOf(body);
  if (CRYPTO_memcmp(tag.data(), expected.data(), expected.size()) != 0) {
    return false;
  }
  return fromHex(cursor.substr(prefix.size(), dot - prefix.size()), last_key);
}

}  // namespace paging
}  // namespace server
}  // namespace mcp
