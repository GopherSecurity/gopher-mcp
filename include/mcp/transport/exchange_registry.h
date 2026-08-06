#ifndef MCP_TRANSPORT_EXCHANGE_REGISTRY_H
#define MCP_TRANSPORT_EXCHANGE_REGISTRY_H

#include <map>
#include <vector>

#include "mcp/core/request_id_key.h"
#include "mcp/event/event_loop.h"
#include "mcp/transport/request_exchange.h"

namespace mcp {
namespace transport {

/**
 * The exchanges a single connection currently has in flight.
 *
 * The registry holds them strongly. That is not the long-term arrangement —
 * eventually a stream's owner holds it — but today a handler answers within
 * the dispatch that called it, so the connection is the only thing around
 * long enough to hold a reference at all.
 *
 * What the registry is really for is the two questions the connection has
 * to answer about itself: whether a response is currently streaming (and so
 * whether an incoming request has to wait its turn), and what should happen
 * to work still in progress when the connection dies.
 *
 * Dispatcher-thread only, like everything it holds.
 */
class ExchangeRegistry {
 public:
  explicit ExchangeRegistry(event::Dispatcher& dispatcher);
  ~ExchangeRegistry();

  /** Take charge of an exchange for as long as it is unfinished. */
  void add(const RequestExchangePtr& exchange);

  /** Drop an exchange that has finished. Safe for one already gone. */
  void remove(const RequestExchangePtr& exchange);

  /** Forget every exchange whose work is done. */
  void reapCompleted();

  /** An exchange by the request id it is answering, if it is still here. */
  RequestExchangePtr find(const RequestIdKey& key) const;

  /** Whether a response is currently streaming on this connection. */
  bool hasActiveStream() const;

  size_t size() const { return exchanges_.size(); }

  /**
   * The connection has gone. Every exchange is told; those that asked to
   * survive it are handed back for someone longer-lived to hold, and the
   * rest are cancelled and dropped.
   *
   * @return The exchanges that survived and now need an owner.
   */
  std::vector<RequestExchangePtr> onConnectionGone();

 private:
  void assertOnDispatcher() const;

  event::Dispatcher& dispatcher_;
  std::vector<RequestExchangePtr> exchanges_;
};

/**
 * Holds exchanges that outlived the connection they were born on.
 *
 * It has to live above the connection, because the per-connection registry
 * is destroyed at exactly the moment it would otherwise take ownership.
 * Whoever owns the listener owns this.
 */
class RetainedExchangeStore {
 public:
  explicit RetainedExchangeStore(event::Dispatcher& dispatcher);
  ~RetainedExchangeStore();

  void retain(const RequestExchangePtr& exchange);

  /** Give up an exchange nobody came back for. */
  void release(const RequestExchangePtr& exchange);

  RequestExchangePtr find(const RequestIdKey& key) const;

  size_t size() const { return exchanges_.size(); }

  /** Drop everything. Called when the owner is shutting down. */
  void clear();

 private:
  void assertOnDispatcher() const;

  event::Dispatcher& dispatcher_;
  std::vector<RequestExchangePtr> exchanges_;
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_EXCHANGE_REGISTRY_H
