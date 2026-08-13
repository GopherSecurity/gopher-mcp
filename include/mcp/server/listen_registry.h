/**
 * Long-lived notification streams, one per subscription.
 *
 * The newest revision has no standalone stream a client holds open and
 * no `resources/subscribe`. Instead a client sends `subscriptions/listen`
 * saying what it wants to hear, and the answer to that request never
 * arrives: the response stream stays open and everything it asked for
 * comes down it.
 *
 * **The unit is the subscription, not the client.** One client may hold
 * several at once, each with its own filter and its own id — that id
 * being the JSON-RPC id of the request that opened it. Every message on
 * a stream carries it, because on a transport where subscriptions share
 * a channel there is nothing else to tell them apart by. A change
 * matching two of a client's subscriptions goes to both, tagged with
 * each one's own id, and never twice down the same stream.
 *
 * Dispatcher-confined, like the streams it holds.
 */

#pragma once

#include <map>
#include <string>
#include <vector>

#include "mcp/core/request_id_key.h"
#include "mcp/json/json_bridge.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/types.h"

namespace mcp {
namespace server {

/**
 * What one subscription asked to hear.
 *
 * A server must send nothing a client did not ask for, so a filter that
 * asks for nothing hears nothing — never everything.
 */
struct NotificationFilter {
  bool tools_list_changed{false};
  bool prompts_list_changed{false};
  bool resources_list_changed{false};
  /** The resources whose updates this subscription wants, by URI. */
  std::vector<std::string> resource_uris;

  /** Read out of a listen request's params. */
  static NotificationFilter parse(const json::JsonValue& params);

  /**
   * What the acknowledgement echoes: the subset actually honoured, so a
   * client can see what it will not be getting rather than wait for
   * something that was never coming.
   */
  json::JsonValue render() const;

  /**
   * Whether this subscription asked for this notification. The URI
   * matters only for a resource update — a subscription names the
   * resources it cares about rather than asking for all of them.
   */
  bool wants(const std::string& method, const std::string& uri) const;

  bool empty() const {
    return !tools_list_changed && !prompts_list_changed &&
           !resources_list_changed && resource_uris.empty();
  }
};

/** Every subscription this server is holding open. */
class ListenRegistry {
 public:
  /**
   * Take one on, and say so on its own stream before anything else goes
   * down it.
   *
   * @return False when there is nothing to hold — no stream, or an id
   *         already subscribed, which would leave two subscriptions
   *         answering to one name.
   */
  bool open(const RequestId& id,
            const ResponseStreamPtr& stream,
            const NotificationFilter& filter);

  /**
   * Deliver a change to every subscription that asked for it.
   *
   * @param uri Only meaningful for a resource update.
   * @return How many subscriptions it went to. Zero is nobody listening
   *         rather than anything wrong.
   */
  size_t publish(const std::string& method,
                 const json::JsonValue& params,
                 const std::string& uri = std::string());

  /**
   * End one on the server's own initiative, gracefully.
   *
   * The stream gets the response its listen request never had, which is
   * how a client tells an ending from a connection that dropped. An
   * abrupt close stays legal; this is the polite path.
   *
   * @return False when no such subscription is held.
   */
  bool close(const RequestId& id);

  /** The same for all of them, as a server going away does. */
  void closeAll();

  /** Forget one whose stream has gone, without trying to answer it. */
  bool forget(const RequestId& id);

  /** Drop every subscription whose stream can no longer be reached. */
  size_t forgetDead();

  size_t size() const { return subscriptions_.size(); }

 private:
  struct Subscription {
    RequestId id;
    ResponseStreamPtr stream;
    NotificationFilter filter;
  };

  std::map<RequestIdKey, Subscription> subscriptions_;
};

}  // namespace server
}  // namespace mcp
