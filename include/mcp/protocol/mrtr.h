/**
 * Asking a client for something without sending it a request.
 *
 * In every earlier revision a server that needed the client to sample a
 * model, fill in a form or list its roots sent a JSON-RPC request of its
 * own and waited. This one has no such thing: a server never initiates.
 *
 * Instead a handler that cannot finish answers with what it still needs,
 * and the client makes the whole request again with the answers attached.
 * The two rounds are independent requests — different ids, nothing shared
 * between them but what travelled in the body — so nothing has to be
 * remembered between them and no two servers behind a load balancer have
 * to agree about which of them is mid-conversation with whom.
 *
 * What carries across a round is `requestState`: a string the server
 * makes and the client hands back untouched. It is the only thing that
 * spans rounds, and it comes back through the client, so a server that
 * lets it decide anything has to treat it as something an attacker
 * wrote.
 */

#pragma once

#include <map>
#include <string>
#include <vector>

#include "mcp/core/compat.h"
#include "mcp/json/json_bridge.h"
#include "mcp/protocol/modern_era.h"

namespace mcp {
namespace protocol {
namespace modern {

/** One thing a server is asking the client to go and get. */
struct InputRequest {
  /** `sampling/createMessage`, `elicitation/create` or `roots/list`. */
  std::string method;
  /** Whatever that request takes, as the schema for it defines. */
  json::JsonValue params;
};

/**
 * Everything being asked for at once, under names the server chooses.
 *
 * The names are how the answers come back: the client returns a map with
 * the same keys, so a server asking two questions can tell the answers
 * apart without ordering them.
 */
using InputRequests = std::map<std::string, InputRequest>;

/**
 * What a handler answers with when it cannot finish yet.
 *
 * At least one of the two has to be there. Asking for nothing and
 * carrying no state would be a server saying "go away and come back"
 * with no reason and nothing to come back with.
 */
struct NeedsInput {
  InputRequests requests;

  /**
   * Opaque to everyone but the handler that made it. It goes out through
   * the client and comes back through the client, so a handler that lets
   * it decide anything — who is calling, what they may reach — must
   * protect its integrity and reject what fails the check. A handler for
   * which tampering can do nothing worse than fail the request may leave
   * it unprotected.
   *
   * Nothing in this library parses it, and nothing should.
   */
  optional<std::string> request_state;

  bool empty() const { return requests.empty() && !request_state.has_value(); }
};

/**
 * The result that carries a question back, ready to be answered with.
 *
 * `resultType` is `"input_required"` rather than the usual `"complete"`,
 * which is what tells the client this is not the answer.
 */
json::JsonValue renderInputRequired(const NeedsInput& needed);

/**
 * The capabilities a client would need in order to be asked these things
 * and did not declare.
 *
 * A server must never ask for something the caller cannot do — the
 * request would sit unanswerable and the client would have no way to say
 * so. Empty means everything asked for is within what was declared.
 *
 * @param declared The caller's capabilities as its request stated them.
 *                 An absent or unreadable declaration is read as
 *                 declaring nothing, which refuses every request rather
 *                 than waving them all through.
 */
std::vector<std::string> capabilitiesMissingFor(const InputRequests& requests,
                                                const std::string& declared);

/**
 * The capabilities a request declared, out of the metadata it declared
 * them in.
 *
 * The metadata arrives serialized, because nested JSON does on the way
 * in. Empty when the request said nothing — which is a request that
 * cannot be asked for anything, not one that can be asked for
 * everything.
 */
std::string declaredCapabilitiesIn(const std::string& raw_meta);

/**
 * The revision a request declared it is speaking, out of the same
 * metadata.
 *
 * Empty when it declared none, which is every request of every earlier
 * era: those settle a version once at the handshake this one does not
 * have, so a request that says nothing is not of this era.
 */
std::string declaredVersionIn(const std::string& raw_meta);

/** The data a refusal for missing capabilities carries. */
json::JsonValue requiredCapabilitiesData(
    const std::vector<std::string>& missing);

/**
 * What a retry brought back with it.
 *
 * Both are absent on a first attempt, which is the ordinary case: a
 * handler sees them only when it has asked for something.
 */
struct CarriedInput {
  /** The answers, under the names they were asked for. */
  json::JsonValue responses;
  /** The state this handler made last round, byte for byte. */
  optional<std::string> request_state;

  bool empty() const {
    return !request_state.has_value() &&
           (!responses.isObject() || responses.keys().empty());
  }
};

/** Read out of a request's params what a previous round put there. */
CarriedInput carriedInputOf(const json::JsonValue& params);

/**
 * What an answer turned out to be asking for, read from the client side.
 *
 * The same shape `renderInputRequired` writes, coming back the other way.
 */
struct AskedFor {
  /**
   * Whether this is a question rather than an answer.
   *
   * A result that says nothing about its kind came from a server of an
   * older revision and is the answer, since no earlier one could ask.
   */
  bool asked{false};

  InputRequests requests;
  /** Handed back untouched, which is the whole of what it is for. */
  optional<std::string> request_state;
};

/**
 * Read a server's answer as a question, if that is what it is.
 *
 * A result claiming to be a question while asking for nothing and
 * carrying nothing is not one: retrying it would send the identical
 * request a second time and be answered the same way, forever. It is
 * read as malformed rather than obeyed.
 */
AskedFor askedForIn(const json::JsonValue& result);

/**
 * The answers, ready to go back under the names they were asked for.
 *
 * Every name asked about appears, including ones nothing could be found
 * for: a server that asked two questions and gets one key back cannot
 * tell which of the two the client failed to answer.
 */
json::JsonValue renderInputResponses(
    const std::map<std::string, json::JsonValue>& answers);

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
