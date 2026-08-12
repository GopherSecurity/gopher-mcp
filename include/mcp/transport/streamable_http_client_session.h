/**
 * @file streamable_http_client_session.h
 * @brief What a Streamable HTTP client holds between requests
 *
 * A session belongs to the conversation, not to the socket it was
 * started on: the client reconnects without initializing again, and the
 * per-connection filter that reads the session id off a response dies
 * with its connection. So this is held above both, and every layer that
 * needs it holds the same one.
 *
 * Two things live here, and they are the same thing seen from either
 * end of a request:
 *
 *   - what to say. The session id and the negotiated revision are held
 *     only between an initialize response and the 404 that says the
 *     server has forgotten them, and they are put on a request exactly
 *     when they are held. That is why the initialize request carries
 *     neither without anybody testing for it: the first one is sent
 *     before there is anything to hold, and the one that follows a 404
 *     is sent after it has been let go.
 *
 *   - who said it. An HTTP status is not a JSON-RPC message and carries
 *     no id, so a 404 cannot name the request it answered. What names it
 *     is the order: one entry goes in as each message is written and one
 *     comes out as each response completes.
 *
 * Dispatcher-confined, like the rest of the transport state.
 */

#ifndef MCP_TRANSPORT_STREAMABLE_HTTP_CLIENT_SESSION_H
#define MCP_TRANSPORT_STREAMABLE_HTTP_CLIENT_SESSION_H

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "mcp/protocol/designated_params.h"
#include "mcp/protocol/header_sentinel.h"
#include "mcp/types.h"

namespace mcp {
namespace transport {

/** Header names, spelled once so both ends of a request agree. */
constexpr const char* kSessionIdHeader = "Mcp-Session-Id";
constexpr const char* kProtocolVersionHeader = "MCP-Protocol-Version";

class StreamableHttpClientSession {
 public:
  // ===== What is held =====

  /** The id the server minted, or empty when it keeps no sessions. */
  const std::string& id() const { return session_id_; }

  /** The revision the initialize response settled on. */
  const std::string& protocolVersion() const { return protocol_version_; }

  /**
   * True once the server has named a session. A stateless server never
   * does, which is what stops the client claiming one it was not given —
   * and is why this, not the handshake, is what a 404 is read against.
   */
  bool hasId() const { return !session_id_.empty(); }

  /**
   * True once an initialize has been answered, session id or not. What a
   * reconnecting connection is seeded from: the handshake is the thing
   * that does not happen twice, whether or not it produced an id.
   */
  bool established() const { return established_; }

  /**
   * The id read off a response. Only the initialize response carries
   * one, so this is set once per session and by the only layer that
   * sees the header.
   */
  void setId(const std::string& session_id) { session_id_ = session_id; }

  /**
   * The revision the initialize response settled on, which is in its
   * body rather than its headers and so arrives from a layer that
   * parses one. Marks the handshake answered.
   */
  void setProtocolVersion(const std::string& protocol_version) {
    protocol_version_ = protocol_version;
    established_ = true;
  }

  /**
   * Let the session go. Called on the 404 that says the server has no
   * record of it, so that the initialize sent next carries nothing from
   * the session that is gone. The handshake stays answered — what is
   * being started is a new session, not a first one.
   */
  void forget() {
    session_id_.clear();
    protocol_version_.clear();
  }

  /**
   * Put on a request what this session is holding, and nothing when it
   * holds nothing.
   */
  void decorate(std::map<std::string, std::string>& headers) const {
    if (!session_id_.empty()) {
      headers[kSessionIdHeader] = session_id_;
    }
    if (!protocol_version_.empty()) {
      headers[kProtocolVersionHeader] = protocol_version_;
    }
  }

  /**
   * The same, plus what the newest revision mirrors out of this message.
   *
   * That era carries the method, and for three of them the name of what
   * the request is about, in headers beside the body — so that something
   * between the two ends can route on them without parsing. A value that
   * cannot travel as itself is encoded, and a server compares the decoded
   * header against the body, so the two agree by construction.
   *
   * Does nothing for an older revision, which mirrors nothing.
   */
  void decorate(std::map<std::string, std::string>& headers,
                const json::JsonValue& message) const;

  /**
   * Remember what a tool asks to have carried in headers.
   *
   * Learned from a listing rather than configured: which arguments a
   * tool mirrors is the server's decision, and a client that guessed
   * would send headers the server never expects. A tool whose
   * designations cannot be resolved is remembered as designating
   * nothing, since it is one this client will not be calling.
   */
  void rememberDesignations(
      const std::string& tool,
      const std::vector<protocol::modern::DesignatedParam>& params) {
    designations_[tool] = params;
  }

  /** What a tool was last seen to designate. */
  const std::vector<protocol::modern::DesignatedParam>* designationsFor(
      const std::string& tool) const {
    auto it = designations_.find(tool);
    return it == designations_.end() ? nullptr : &it->second;
  }

  // ===== Who said it =====

  /**
   * Note a message going out. Recorded where the write is ordered
   * rather than where it is asked for, because that is the order the
   * answers come back in. A notification has no id and still draws a
   * response, so it takes a place too — one that is empty.
   */
  void recordSent(const optional<RequestId>& request_id) {
    in_flight_.push_back(request_id);
  }

  /**
   * Take the request the response now completing was answering. Empty
   * when that message had no id, or when nothing is on record — a
   * response nobody asked for, which is the server's business and not
   * something to attribute to whoever asked last.
   */
  optional<RequestId> takeAnswered() {
    if (in_flight_.empty()) {
      return optional<RequestId>();
    }
    optional<RequestId> answered = in_flight_.front();
    in_flight_.pop_front();
    return answered;
  }

  /**
   * The request the response now arriving is answering, without giving
   * up its place. For an answer that never finished: the queue was
   * never popped for it, so it is still at the front, and it is still
   * outstanding.
   */
  optional<RequestId> peekAnswered() const {
    return in_flight_.empty() ? optional<RequestId>() : in_flight_.front();
  }

  /**
   * Forget what is outstanding. A connection that goes takes its
   * unanswered requests with it — the retry and deadline machinery owns
   * them from there — and an entry left behind would name the wrong
   * request for the first response on the next connection.
   */
  void forgetInFlight() { in_flight_.clear(); }

  size_t inFlight() const { return in_flight_.size(); }

 private:
  std::string session_id_;
  // What each tool asks to have mirrored, as its listing said. Kept per
  // session because a server may change its tools and a client is
  // expected to re-read them rather than remember forever.
  std::map<std::string, std::vector<protocol::modern::DesignatedParam>>
      designations_;
  std::string protocol_version_;
  bool established_{false};
  std::deque<optional<RequestId>> in_flight_;
};

using StreamableHttpClientSessionPtr =
    std::shared_ptr<StreamableHttpClientSession>;

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_STREAMABLE_HTTP_CLIENT_SESSION_H
