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
#include <memory>
#include <string>
#include <vector>

#include "mcp/core/request_id_key.h"
#include "mcp/json/json_bridge.h"
#include "mcp/message_dispatch_context.h"
#include "mcp/protocol/subscriptions.h"
#include "mcp/types.h"

namespace mcp {
namespace server {

/**
 * The same filter both ends speak, named where this side reads it.
 *
 * Parsing and rendering it are two ends of one wire shape, so they live
 * with the rest of the vocabulary rather than with either end.
 */
using NotificationFilter = protocol::modern::NotificationFilter;

/**
 * What tells one subscription from another, server-wide.
 *
 * The id a subscription answers to is the JSON-RPC id of the request
 * that opened it, and that id is only ever unique to the client that
 * chose it — two clients each numbering their requests from one is the
 * ordinary case, not a collision. So what a server holds them under has
 * to carry who they belong to as well; only the id travels on the wire,
 * where the client it went to is the context.
 */
struct SubscriptionKey {
  std::string caller;
  RequestIdKey id;

  bool operator<(const SubscriptionKey& other) const {
    if (caller != other.caller) {
      return caller < other.caller;
    }
    return id < other.id;
  }
};

/** Every subscription this server is holding open. */
class ListenRegistry {
 public:
  /**
   * Take one on, and say so on its own stream before anything else goes
   * down it.
   *
   * @param caller Who it belongs to. Two clients using the same request
   *        id are two subscriptions, not one taken twice.
   * @return False when there is nothing to hold — no stream, or an id
   *         this caller has already subscribed under, which would leave
   *         two of its subscriptions answering to one name.
   */
  bool open(const std::string& caller,
            const RequestId& id,
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
  bool close(const std::string& caller, const RequestId& id);

  /** The same for all of them, as a server going away does. */
  void closeAll();

  /** Forget one whose stream has gone, without trying to answer it. */
  bool forget(const std::string& caller, const RequestId& id);

  /** Drop every subscription whose stream can no longer be reached. */
  size_t forgetDead();

  size_t size() const { return subscriptions_.size(); }

 private:
  struct Subscription {
    std::string caller;
    RequestId id;
    ResponseStreamPtr stream;
    NotificationFilter filter;
  };

  std::map<SubscriptionKey, Subscription> subscriptions_;

  /**
   * Proof this registry is still here, for the cancellations it asked to
   * be told about. A subscription's stream is held by machinery that
   * outlives the server holding this, and a cancellation arriving after
   * would otherwise be delivered into freed memory. Compared, never
   * followed.
   */
  std::shared_ptr<int> alive_{new int(0)};
};

}  // namespace server
}  // namespace mcp
