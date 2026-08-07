#ifndef MCP_TRANSPORT_STREAMABLE_SESSION_MANAGER_H
#define MCP_TRANSPORT_STREAMABLE_SESSION_MANAGER_H

#include <chrono>
#include <cstdint>
#include <deque>
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
 * The session owns it: created when the stream opens, destroyed at session
 * teardown or retention expiry, and never by the death of the connection it
 * was running over — that only detaches it. A stream carries a random id of
 * its own, which prefixes every event id it emits, and that is what makes an
 * id unique across the session while still ordering within its own stream.
 */
struct StreamCtx {
  /**
   * What the stream is for.
   *
   * Get is the standalone stream a client opens and leaves open, and is
   * where anything the server says on its own initiative goes.
   * PostResponse answers one request and ends with it, so nothing
   * unsolicited may be routed onto it.
   */
  enum class Kind { Get, PostResponse };

  std::string id;
  Kind kind{Kind::Get};
  uint64_t next_sequence{1};

  /** What is producing on this stream, when anything is. */
  RequestExchangePtr exchange;

  /**
   * The connection carrying it, and the thread that connection belongs to.
   *
   * Neither is owned. The connection is nulled when the client goes away,
   * which is how a detached stream is told apart from a live one. The
   * dispatcher outlives every connection on it and is what a write from the
   * session's own thread has to be posted to, since the exchange may only
   * be touched where its connection lives.
   */
  network::Connection* conn{nullptr};
  event::Dispatcher* dispatcher{nullptr};

  /** Whether the stream has begun and not yet finished. */
  bool open() const {
    return exchange && exchange->mode() == RequestExchange::Mode::Stream;
  }

  /**
   * Whether anything written here would reach a client now. An open
   * stream whose client has gone is not live but is still very much
   * there: what is written to it is kept for a client that comes back.
   */
  bool live() const { return conn != nullptr && open(); }
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

  /**
   * Messages the server had to say while no stream was connected to say
   * them on, oldest first.
   *
   * Bounded, and the oldest goes when it is full: a client that never
   * comes back must not be able to grow the server without limit. This is
   * delivery rather than replay — what is handed to the next stream that
   * opens is taken out of here as it goes.
   */
  std::deque<std::string> pending;
  size_t pending_dropped{0};

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
   * Add a stream to a session, with an id no other stream there is using.
   *
   * @param conn       The connection carrying it, for telling a live stream
   *                   from a detached one.
   * @param dispatcher Where that connection lives, which is the only thread
   *                   the stream's exchange may be touched from.
   * @return The stream, owned by the session; null if no id could be drawn.
   */
  StreamCtx* openStream(SessionCtx& session,
                        StreamCtx::Kind kind,
                        const RequestExchangePtr& exchange,
                        network::Connection* conn,
                        event::Dispatcher& dispatcher);

  /**
   * How many streams of a kind the session is holding.
   *
   * @param connected_only True counts only those that could reach a client
   *                       now. False counts detached ones too, which is
   *                       what a bound on memory has to do — a detached
   *                       stream is still holding everything written to it.
   */
  static size_t countStreams(const SessionCtx& session,
                             StreamCtx::Kind kind,
                             bool connected_only);

  /**
   * Where a message the server said on its own initiative goes: the most
   * recently opened stream that could still carry it.
   *
   * Deterministic on purpose. The protocol allows a client to hold several
   * streams and forbids sending the same thing on more than one, so
   * something has to pick, and "the newest" is what a client that has just
   * reconnected expects. Null when none could carry anything.
   */
  static StreamCtx* currentStream(SessionCtx& session);

  /**
   * Send a message that answers no request.
   *
   * @return True when it went out; false when it was queued for the next
   *         stream to open instead.
   */
  bool routeUnsolicited(SessionCtx& session, const std::string& payload);

  /**
   * How many messages one session may hold for a client that is not
   * connected. Beyond it the oldest is dropped.
   */
  void setPendingLimit(size_t messages) { pending_limit_ = messages; }

  /**
   * The client on this connection has gone. Its streams are detached, not
   * removed: the work behind one carries on, and a client that comes back
   * is owed whatever it missed.
   */
  static void detachConnection(SessionCtx& session, network::Connection* conn);

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

  /**
   * Whether the given dispatcher is the one entitled to read this session.
   *
   * Deliberately without the affinity assertion the accessors carry: asking
   * whether you may look is exactly what a thread that may not is supposed
   * to do first.
   */
  bool ownedBy(const std::string& id, event::Dispatcher& dispatcher) const;

  /**
   * Compare two secrets without letting the time taken say how far they
   * matched. Used for the session id and the principal bound to it.
   *
   * What this does not cover: finding the session is a hash lookup, and a
   * hash lookup is not constant time. That is deliberate and it is fine —
   * the lookup happens once per request against a table the caller cannot
   * choose the shape of, whereas a comparison can be probed repeatedly with
   * input the caller picks, one character at a time.
   */
  static bool secureEquals(const std::string& left, const std::string& right);

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

  /** Write one message on a stream, wherever that stream's thread is. */
  static void writeToStream(StreamCtx& stream, const std::string& payload);

  /** Hand a newly opened stream whatever was waiting for one. */
  static void flushPending(SessionCtx& session, StreamCtx& stream);

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
  size_t pending_limit_{256};
  bool running_{true};
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_STREAMABLE_SESSION_MANAGER_H
