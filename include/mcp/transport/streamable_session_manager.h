#ifndef MCP_TRANSPORT_STREAMABLE_SESSION_MANAGER_H
#define MCP_TRANSPORT_STREAMABLE_SESSION_MANAGER_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "mcp/event/event_loop.h"
#include "mcp/transport/request_exchange.h"

namespace mcp {
namespace transport {

/**
 * One event stream belonging to a session.
 *
 * Declared here because the session is what owns it; nothing populates one
 * yet. A stream carries a random id of its own, which prefixes every event
 * id it emits — that is what makes an id unique across the session while
 * still ordering within the stream it came from.
 */
struct StreamCtx {
  std::string id;
  uint64_t next_sequence{1};

  /** What is producing on this stream, when anything is. */
  RequestExchangePtr exchange;
};

/**
 * Everything the server knows about one logical client session.
 *
 * A session is not a connection. It outlives the connection its initialize
 * arrived on, which is the whole reason it exists: a client that reconnects
 * has to be able to say which conversation it is resuming.
 */
struct SessionCtx {
  /** The id handed to the client, and the key everything else is under. */
  std::string id;

  /**
   * Who created it, as resolved when its initialize arrived.
   *
   * A session id alone must never authorize a caller — anyone who obtains
   * one would otherwise inherit the session it names — so the caller a
   * session was minted for is recorded here to be checked against later.
   */
  std::string principal;

  /** The revision agreed at initialize, not the one configured. */
  std::string negotiated_protocol_version;

  /**
   * The streams this session owns.
   *
   * Owning, and deliberately so: a stream is created when it opens and
   * destroyed at retention expiry or session teardown, never by the death
   * of the connection it was running over. That is what leaves something
   * for a reconnecting client to be handed.
   */
  std::vector<std::unique_ptr<StreamCtx>> streams;

  /**
   * Which stream an event id came from, for replaying to a client that
   * asks to resume from one.
   *
   * The pointers are observers into `streams` and own nothing. Erasing a
   * stream must purge its entries here first — afterwards there is no way
   * to tell which entries belonged to it, and a stale one is a dangling
   * pointer a resuming client would follow.
   */
  std::unordered_map<std::string, StreamCtx*> event_index;

  std::chrono::steady_clock::time_point last_activity;
};

/**
 * The sessions a Streamable HTTP server is currently serving.
 *
 * Threading: session affinity with an owner directory. A session's mutable
 * state belongs to the dispatcher that accepted its initialize and is
 * touched only there; find/touch/remove assert as much, matching every
 * other registry here. Affinity alone would not be enough, because a GET,
 * a POST or a DELETE for a session legitimately arrives on some other
 * connection, which may belong to another dispatcher. The way across is
 * withSession(), not a lock around the session state.
 *
 * The directory — id to owning dispatcher — is the one locked structure,
 * and it holds pointers only.
 */
class StreamableSessionManager {
 public:
  /**
   * @param dispatcher The thread this manager was built on, which owns any
   *                   session minted without naming an owner. Sessions
   *                   record their own owner, so this is a default rather
   *                   than a constraint.
   */
  explicit StreamableSessionManager(event::Dispatcher& dispatcher);
  ~StreamableSessionManager();

  StreamableSessionManager(const StreamableSessionManager&) = delete;
  StreamableSessionManager& operator=(const StreamableSessionManager&) = delete;

  /**
   * A fresh session id: 16 bytes from the system CSPRNG, rendered as 32
   * lowercase hex digits.
   *
   * Hex rather than anything denser because the id travels in an HTTP
   * header, which admits only visible ASCII — 0-9 and a-f satisfy that by
   * construction rather than by a validation step someone could skip.
   *
   * Returns empty when the CSPRNG fails. There is deliberately no fallback
   * to a weaker source: an id that is merely hard to guess is worse than
   * no session at all, because it would still be trusted like one.
   */
  static std::string mintId();

  /**
   * Create a session owned by the calling dispatcher.
   *
   * @return The new session, or null if no id could be drawn. The pointer
   *         is stable until the session is removed.
   */
  SessionCtx* createSession(event::Dispatcher& owner,
                            const std::string& principal);

  /** Create a session owned by the dispatcher this manager was built on. */
  SessionCtx* createSession(const std::string& principal) {
    return createSession(dispatcher_, principal);
  }

  /** The session, or null if there is none or this thread does not own it. */
  SessionCtx* find(const std::string& id);

  /** Mark a session as still in use. False when there is no such session. */
  bool touch(const std::string& id);

  /** Tear a session down, telling the removal observer. Idempotent. */
  bool remove(const std::string& id);

  /**
   * Told once for each session that goes away, on the thread that owned
   * it. This is how the layer above releases what it keyed on the session
   * id — an application session, its subscriptions — which would otherwise
   * outlive the transport identity that was supposed to bound it.
   */
  using SessionRemovedCallback = std::function<void(const std::string&)>;
  void setSessionRemovedCallback(SessionRemovedCallback callback) {
    session_removed_callback_ = std::move(callback);
  }

  /** Idle window after which a session is swept. */
  void setTimeout(std::chrono::milliseconds timeout);
  std::chrono::milliseconds timeout() const { return timeout_; }

  /**
   * Visit every session this thread owns that has been idle for longer
   * than `timeout`. Sessions owned by other dispatchers are left to them.
   */
  void forEachExpired(std::chrono::milliseconds timeout,
                      const std::function<void(SessionCtx&)>& fn);

  /** How many sessions exist, across all owners. */
  size_t size() const;

  /** Whether the id names a live session, whoever owns it. */
  bool known(const std::string& id) const;

  using SessionFn = std::function<void(SessionCtx&)>;
  using DoneFn = std::function<void(bool found)>;

  /**
   * Reach a session from a dispatcher that may not own it.
   *
   * `fn` runs on the owning thread, which is the only place the session
   * may be read or changed. `done` runs on the caller's, so whatever asked
   * can carry on where it left off. An unknown id completes not-found
   * immediately, without a thread hop — there is nothing to hop to.
   */
  void withSession(event::Dispatcher& caller,
                   const std::string& id,
                   SessionFn fn,
                   DoneFn done);

 private:
  struct Entry {
    event::Dispatcher* owner{nullptr};
    std::unique_ptr<SessionCtx> ctx;
  };

  /** The entry, or null when it is absent or belongs to another thread. */
  Entry* ownedEntry(const std::string& id);

  /** Drop an entry and tell the observer. Runs on the owning thread. */
  bool removeOwned(const std::string& id);

  /** Arm one dispatcher's sweep, creating its timer the first time. */
  void armSweep(event::Dispatcher& owner);
  void sweepFor(event::Dispatcher& owner);

  event::Dispatcher& dispatcher_;

  // Guards the map structure and the owner pointers, and nothing else. The
  // SessionCtx behind an entry is the owning thread's alone.
  mutable std::mutex directory_mutex_;
  std::unordered_map<std::string, Entry> directory_;

  // One sweep per dispatcher that owns sessions, so a session is only ever
  // examined by the thread entitled to examine it. Created on the owner's
  // thread when it mints its first session.
  std::unordered_map<event::Dispatcher*, event::TimerPtr> sweepers_;

  SessionRemovedCallback session_removed_callback_;
  std::chrono::milliseconds timeout_{300000};
  bool running_{true};
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_STREAMABLE_SESSION_MANAGER_H
