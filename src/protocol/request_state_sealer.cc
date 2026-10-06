// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "mcp/protocol/request_state_sealer.h"

#include <cstdint>
#include <set>
#include <stdexcept>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace mcp {
namespace protocol {
namespace modern {

namespace {

// The first byte of every sealed state: which layout follows.
constexpr unsigned char kVersion = 1;
// Domain separation, so a tag made here means nothing anywhere else.
constexpr const char* kTagLabel = "mcp-request-state";
constexpr size_t kTagBytes = 32;
constexpr size_t kMinSecretBytes = 32;

const char* const kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url(const std::string& bytes) {
  std::string out;
  out.reserve((bytes.size() * 4 + 2) / 3);
  size_t i = 0;
  for (; i + 3 <= bytes.size(); i += 3) {
    const uint32_t n = (static_cast<unsigned char>(bytes[i]) << 16) |
                       (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                       static_cast<unsigned char>(bytes[i + 2]);
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += kAlphabet[(n >> 6) & 63];
    out += kAlphabet[n & 63];
  }
  const size_t rest = bytes.size() - i;
  if (rest == 1) {
    const uint32_t n = static_cast<unsigned char>(bytes[i]) << 16;
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
  } else if (rest == 2) {
    const uint32_t n = (static_cast<unsigned char>(bytes[i]) << 16) |
                       (static_cast<unsigned char>(bytes[i + 1]) << 8);
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += kAlphabet[(n >> 6) & 63];
  }
  return out;
}

bool fromBase64url(const std::string& text, std::string* bytes) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    if (c == '-')
      return 62;
    if (c == '_')
      return 63;
    return -1;
  };
  if (text.size() % 4 == 1) {
    return false;
  }
  std::string out;
  out.reserve(text.size() * 3 / 4);
  uint32_t buffer = 0;
  int bits = 0;
  for (char c : text) {
    const int v = value(c);
    if (v < 0) {
      return false;
    }
    buffer = (buffer << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buffer >> bits) & 0xff);
    }
  }
  // Leftover bits must be zero, so each sealed state has one spelling.
  if (bits > 0 && (buffer & ((1u << bits) - 1)) != 0) {
    return false;
  }
  *bytes = out;
  return true;
}

void appendU32(std::string& out, uint32_t n) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out += static_cast<char>((n >> shift) & 0xff);
  }
}

void appendI64(std::string& out, int64_t n) {
  const uint64_t u = static_cast<uint64_t>(n);
  for (int shift = 56; shift >= 0; shift -= 8) {
    out += static_cast<char>((u >> shift) & 0xff);
  }
}

bool readU32(const std::string& in, size_t* at, uint32_t* n) {
  if (in.size() < *at + 4) {
    return false;
  }
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) {
    v = (v << 8) | static_cast<unsigned char>(in[*at + i]);
  }
  *at += 4;
  *n = v;
  return true;
}

bool readI64(const std::string& in, size_t* at, int64_t* n) {
  if (in.size() < *at + 8) {
    return false;
  }
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v = (v << 8) | static_cast<unsigned char>(in[*at + i]);
  }
  *at += 8;
  *n = static_cast<int64_t>(v);
  return true;
}

/** Length first, so no two different fields can run together. */
void appendField(std::string& out, const std::string& field) {
  appendU32(out, static_cast<uint32_t>(field.size()));
  out += field;
}

/** The params a request and its retry share. */
std::string paramsDigest(const json::JsonValue& params) {
  json::JsonValue salient = json::JsonValue::object();
  if (params.isObject()) {
    for (const auto& key : params.keys()) {
      if (key == "requestState" || key == "inputResponses" || key == "_meta") {
        continue;
      }
      salient.set(key, params[key]);
    }
  }
  // Keys come out sorted, so the same params always digest the same.
  const std::string text = salient.toString();
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(text.data()), text.size(),
         digest);
  return std::string(reinterpret_cast<const char*>(digest), sizeof(digest));
}

bool validKeyId(const std::string& id) {
  if (id.empty() || id.size() > 64) {
    return false;
  }
  for (char c : id) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

}  // namespace

RequestStateSealer::RequestStateSealer(std::vector<Key> keys,
                                       std::chrono::seconds lifetime,
                                       Clock clock)
    : keys_(std::move(keys)), lifetime_(lifetime), clock_(std::move(clock)) {
  if (keys_.empty()) {
    throw std::invalid_argument("a request-state sealer needs a key");
  }
  std::set<std::string> ids;
  for (const auto& key : keys_) {
    if (!validKeyId(key.id)) {
      throw std::invalid_argument("request-state key id '" + key.id +
                                  "' must be 1-64 letters, digits, - or _");
    }
    if (key.secret.size() < kMinSecretBytes) {
      throw std::invalid_argument("request-state key '" + key.id +
                                  "' needs a secret of at least 32 bytes");
    }
    if (!ids.insert(key.id).second) {
      throw std::invalid_argument("two request-state keys share the id '" +
                                  key.id + "'");
    }
  }
  if (lifetime_.count() <= 0) {
    throw std::invalid_argument("a sealed request state needs a lifetime");
  }
  if (!clock_) {
    clock_ = []() { return std::chrono::system_clock::now(); };
  }
}

std::string RequestStateSealer::tagFor(
    const Key& key,
    int64_t expires,
    const std::string& state,
    const RequestStateContext& context) const {
  // Everything the state is bound to, each field length-prefixed. The
  // principal, method and params aren't carried in the sealed value; they
  // are only signed, and supplied again by whoever opens it.
  std::string signed_input;
  appendField(signed_input, kTagLabel);
  signed_input += static_cast<char>(kVersion);
  appendField(signed_input, key.id);
  appendI64(signed_input, expires);
  appendField(signed_input, context.principal);
  appendField(signed_input, context.method);
  appendField(signed_input, paramsDigest(context.params));
  appendField(signed_input, state);

  unsigned char mac[EVP_MAX_MD_SIZE];
  unsigned int mac_length = 0;
  if (HMAC(EVP_sha256(), key.secret.data(), static_cast<int>(key.secret.size()),
           reinterpret_cast<const unsigned char*>(signed_input.data()),
           signed_input.size(), mac, &mac_length) == nullptr ||
      mac_length < kTagBytes) {
    throw std::runtime_error("could not seal a request state");
  }
  return std::string(reinterpret_cast<const char*>(mac), kTagBytes);
}

std::string RequestStateSealer::seal(const std::string& state,
                                     const RequestStateContext& context) const {
  const Key& key = keys_.front();
  const int64_t expires = std::chrono::duration_cast<std::chrono::seconds>(
                              (clock_() + lifetime_).time_since_epoch())
                              .count();

  // version · key id · expiry · state · tag
  std::string sealed;
  sealed += static_cast<char>(kVersion);
  appendField(sealed, key.id);
  appendI64(sealed, expires);
  appendField(sealed, state);
  sealed += tagFor(key, expires, state, context);
  return base64url(sealed);
}

optional<std::string> RequestStateSealer::open(
    const std::string& sealed, const RequestStateContext& context) const {
  try {
    std::string bytes;
    if (!fromBase64url(sealed, &bytes) || bytes.empty() ||
        static_cast<unsigned char>(bytes[0]) != kVersion) {
      return nullopt;
    }
    size_t at = 1;
    uint32_t id_length = 0;
    if (!readU32(bytes, &at, &id_length) || id_length > 64 ||
        bytes.size() < at + id_length) {
      return nullopt;
    }
    const std::string id = bytes.substr(at, id_length);
    at += id_length;
    int64_t expires = 0;
    uint32_t state_length = 0;
    if (!readI64(bytes, &at, &expires) || !readU32(bytes, &at, &state_length) ||
        bytes.size() != at + state_length + kTagBytes) {
      return nullopt;
    }
    const std::string state = bytes.substr(at, state_length);
    const std::string tag = bytes.substr(at + state_length);

    const Key* key = nullptr;
    for (const auto& candidate : keys_) {
      if (candidate.id == id) {
        key = &candidate;
      }
    }
    if (key == nullptr) {
      return nullopt;
    }

    // The tag first, so nothing about the contents is believed before it
    // is known to be ours.
    const std::string expected = tagFor(*key, expires, state, context);
    if (CRYPTO_memcmp(tag.data(), expected.data(), kTagBytes) != 0) {
      return nullopt;
    }
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            clock_().time_since_epoch())
                            .count();
    if (now >= expires) {
      return nullopt;
    }
    return state;
  } catch (const std::exception&) {
    return nullopt;
  }
}

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
