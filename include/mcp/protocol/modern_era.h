/**
 * The names and numbers of the era that has no handshake.
 *
 * From revision 2026-07-28 onward there is no `initialize` and no session:
 * every request declares its own protocol version, client identity and
 * client capabilities in `params._meta`, and the HTTP transport mirrors
 * selected body fields into headers so that a load balancer can route on
 * them without parsing the body. The body stays the source of truth, and a
 * header that disagrees with it is a refusal rather than an override.
 *
 * Everything here is a name or a number the wire uses. It lives in one
 * place because the same strings are read by the transport, written by the
 * client, and asserted by the tests — three chances to spell one of them
 * differently and find out from an interop failure months later.
 */

#pragma once

#include <string>

namespace mcp {
namespace protocol {
namespace modern {

// ===== What a request carries in its body =====

/**
 * The `_meta` keys every modern request carries.
 *
 * The version and the capabilities are required; the client's identity is
 * not. A server must serve a request that never says who is calling —
 * only the version decides whether it can be served at all.
 */
constexpr const char* kMetaProtocolVersion =
    "io.modelcontextprotocol/protocolVersion";
constexpr const char* kMetaClientInfo = "io.modelcontextprotocol/clientInfo";
constexpr const char* kMetaClientCapabilities =
    "io.modelcontextprotocol/clientCapabilities";

/** Where a server names itself in a result, there being no handshake to. */
constexpr const char* kMetaServerInfo = "io.modelcontextprotocol/serverInfo";

// ===== What a request carries in its headers =====

// Sent on every request, and matching the version in the body.
constexpr const char* kProtocolVersionHeader = "MCP-Protocol-Version";

// Sent on every request, and matching the method in the body.
constexpr const char* kMethodHeader = "Mcp-Method";

// Sent on the three methods that name what they are about, and matching
// that name in the body.
constexpr const char* kNameHeader = "Mcp-Name";

// The prefix of a header mirroring a tool argument. What follows it is
// chosen by whoever wrote the tool's schema.
constexpr const char* kParamHeaderPrefix = "Mcp-Param-";

/**
 * The methods that carry `Mcp-Name`, and where each one keeps the name.
 *
 * Two of them call it `name` and one calls it `uri`; the header carries
 * whichever it is, so the difference has to be known here rather than
 * guessed from the body.
 */
constexpr const char* kMethodToolsCall = "tools/call";
constexpr const char* kMethodResourcesRead = "resources/read";
constexpr const char* kMethodPromptsGet = "prompts/get";

/** The method by which a client asks a server what it is. */
constexpr const char* kMethodServerDiscover = "server/discover";

/**
 * Which body field holds the name this method mirrors into `Mcp-Name`.
 * Empty for a method that mirrors none, which is most of them.
 */
inline const char* nameFieldFor(const std::string& method) {
  if (method == kMethodToolsCall || method == kMethodPromptsGet) {
    return "name";
  }
  if (method == kMethodResourcesRead) {
    return "uri";
  }
  return "";
}

/** Whether this method is one of the three that must carry `Mcp-Name`. */
inline bool carriesName(const std::string& method) {
  return *nameFieldFor(method) != '\0';
}

// ===== What a result carries =====

/**
 * Every result says what kind of result it is.
 *
 * A final answer is "complete"; an answer that is really a question back
 * is "input_required" and carries what the server needs. A result with no
 * resultType at all came from a server speaking an older revision and
 * means "complete" — which is what makes it safe for a client to talk to
 * both.
 */
constexpr const char* kResultTypeField = "resultType";
constexpr const char* kResultTypeComplete = "complete";
constexpr const char* kResultTypeInputRequired = "input_required";

// ===== What a refusal says =====

/**
 * The codes this revision allocates for itself.
 *
 * All three are answered with HTTP 400. They are what tells a client that
 * the server it is talking to speaks this era at all: a refusal carrying
 * one of them is a modern server saying no, where a refusal carrying none
 * of them may be anything at all.
 */
constexpr int kHeaderMismatch = -32020;
constexpr int kMissingRequiredClientCapability = -32021;
constexpr int kUnsupportedProtocolVersion = -32022;

/** Unknown method, which this transport answers with HTTP 404. */
constexpr int kMethodNotFound = -32601;

/**
 * The name of the version complaint, for a server that gives its errors
 * names rather than codes. Read as well as the code, because reading both
 * costs nothing and means the classification does not depend on which one
 * a given implementation chose.
 */
constexpr const char* kUnsupportedProtocolVersionName =
    "UnsupportedProtocolVersionError";

/** Keys inside the data of the two refusals that carry any. */
constexpr const char* kSupportedVersionsField = "supported";
constexpr const char* kRequestedVersionField = "requested";
constexpr const char* kRequiredCapabilitiesField = "requiredCapabilities";

/**
 * Whether a request declaring this version is served by the rules of this
 * era rather than the older ones.
 *
 * A string comparison rather than a list membership: the era is decided
 * before anything has looked at what this server actually supports, and a
 * version this server does not serve still has to be refused in the way
 * its own era expects.
 */
inline bool isModernVersion(const std::string& version) {
  // Version strings are ISO dates, so this orders them chronologically.
  return version >= "2026-07-28";
}

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
