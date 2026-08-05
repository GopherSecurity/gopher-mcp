/**
 * MCP Protocol Version Constants
 *
 * Named constants for every MCP protocol revision this SDK knows about, plus
 * the helpers used to decide which revision a peer speaks.
 *
 * Two invariants hold throughout:
 * - Version strings are ISO dates, so lexicographic comparison orders them
 *   chronologically. That is what makes versionAtLeast() correct.
 * - A supported-version list is always ordered newest-first. Helpers that
 *   pick a "latest" version take the front element and rely on this.
 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace mcp {
namespace protocol {

constexpr const char* kProtocolVersion20241105 = "2024-11-05";
constexpr const char* kProtocolVersion20250326 = "2025-03-26";
constexpr const char* kProtocolVersion20250618 = "2025-06-18";
constexpr const char* kProtocolVersion20251125 = "2025-11-25";
constexpr const char* kProtocolVersion20260728 = "2026-07-28";

// Version a client offers by default, and the answer a server falls back to
// when it has no supported-version list configured at all.
constexpr const char* kDefaultProtocolVersion = kProtocolVersion20250618;

// Version an HTTP server assumes for a request that carries no protocol
// version header. The header only became mandatory after this revision, so
// its absence identifies a peer speaking this one.
constexpr const char* kLegacyAssumedVersion = kProtocolVersion20250326;

// True when the exact version string appears in the supported list.
inline bool isSupportedVersion(const std::string& version,
                               const std::vector<std::string>& supported) {
  return std::find(supported.begin(), supported.end(), version) !=
         supported.end();
}

// Newest version in the list. Lists are ordered newest-first, so this is the
// front element; an empty list yields the default version.
inline std::string latestSupportedVersion(
    const std::vector<std::string>& supported) {
  return supported.empty() ? std::string(kDefaultProtocolVersion)
                           : supported.front();
}

// Server-side negotiation: echo back the version the peer asked for when we
// support it, otherwise answer with the newest version we do support. An
// empty or unrecognized request therefore yields the newest version, which
// is what a peer needs in order to decide whether it can continue.
inline std::string negotiateProtocolVersion(
    const std::string& requested, const std::vector<std::string>& supported) {
  if (isSupportedVersion(requested, supported)) {
    return requested;
  }
  return latestSupportedVersion(supported);
}

// True when version is min_version or newer. Valid because the version
// strings are ISO dates.
inline bool versionAtLeast(const std::string& version,
                           const std::string& min_version) {
  return version >= min_version;
}

}  // namespace protocol
}  // namespace mcp
