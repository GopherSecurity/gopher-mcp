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
#include <unordered_set>
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

  /**
   * What is producing on this stream, when anything is, and what holds
   * everything it has already sent. The count of events written here is
   * the exchange's too: it writes the bytes, so it is what decides the
   * order they are numbered in.
   */
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

  /**
   * When this stream stops being kept, once nothing more will be written
   * to it. Unset while it is still in use.
   *
   * The window exists because a client whose connection dropped needs a
   * chance to come back and say where it got to; one that never returns
   * must not pin what it was owed forever.
   */
  std::chrono::steady_clock::time_point retire_at{};

  /**
   * Where anything this stream still produces is also sent.
   *
   * Set when a client that lost this stream came back on another one: it
   * is owed not only what it missed but whatever comes next, and what
   * comes next is written here rather than there. Carried by value
   * because the deciding and the writing are not always the same thread.
   */
  RequestExchangePtr follower_exchange;
  event::Dispatcher* follower_dispatcher{nullptr};
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
   * Which stream a given stream id names, for finding the one a resuming
   * client asks to carry on from.
   *
   * Keyed on the stream rather than on each event, so it holds one entry
   * per stream however many events a stream sends — an index that grew
   * with the events would have to be bounded separately from the buffers
   * it points into, and the two bounds would have to be kept in step.
   * Where in the stream the client got to is answered by the stream's own
   * buffer, which is where that answer already lives.
   *
   * The pointers are observers into `streams` and own nothing. Erasing a
   * stream must purge its entry here in the same operation — a stale one
   * is a dangling pointer a resuming client would follow.
   */
  std::unordered_map<std::string, StreamCtx*> stream_index;

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
   * Where ids are drawn from. Defaults to the system CSPRNG.
   *
   * A seam, for a deployment with its own source and for tests that need
   * to see what happens when a draw repeats. Whatever is put here is
   * still expected to be unguessable — there is no fallback to anything
   * weaker, here or anywhere else in this class.
   *
   * @param source Fills the buffer, returning false if it cannot.
   */
  using EntropySource = std::function<bool(unsigned char*, size_t)>;
  void setEntropySource(EntropySource source) { entropy_ = std::move(source); }

  /**
   * A stream id no other stream anywhere in this manager is using.
   *
   * Taken under the directory lock rather than under session affinity, so
   * it is settled on whichever thread is about to write the stream's
   * first event: a stream cannot emit ids it does not yet have, and the
   * record of it on the session may land a moment later.
   *
   * Unique across the manager rather than merely within the session,
   * which is stronger than the protocol asks for and cheaper than asking
   * a session that belongs to another thread what it already holds.
   *
   * @return The id, or empty if none could be drawn. Held until it is
   *         released with the stream it names.
   */
  std::string reserveStreamId();

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
   * Add a stream to a session under an id already reserved for it.
   *
   * @param stream_id  From reserveStreamId(), and already the name the
   *                   exchange is minting event ids under.
   * @param conn       The connection carrying it, for telling a live stream
   *                   from a detached one.
   * @param dispatcher Where that connection lives, which is the only thread
   *                   the stream's exchange may be touched from.
   * @return The stream, owned by the session; null if no id could be drawn.
   */
  StreamCtx* openStream(SessionCtx& session,
                        const std::string& stream_id,
                        StreamCtx::Kind kind,
                        const RequestExchangePtr& exchange,
                        network::Connection* conn,
                        event::Dispatcher& dispatcher);

  /**
   * The same, drawing an id first. For a caller with no reason to have
   * one in hand before the stream exists.
   */
  StreamCtx* openStream(SessionCtx& session,
                        StreamCtx::Kind kind,
                        const RequestExchangePtr& exchange,
                        network::Connection* conn,
                        event::Dispatcher& dispatcher) {
    return openStream(session, reserveStreamId(), kind, exchange, conn,
                      dispatcher);
  }

  /** Take a stream off its session, with everything keyed on it. */
  void closeStream(SessionCtx& session, StreamCtx& stream);

  /** What a client's claim about where it got to turned out to be worth. */
  struct ResumePoint {
    /** Whether this session has the stream the client named. */
    bool found{false};
    StreamCtx::Kind kind{StreamCtx::Kind::Get};
    std::string stream_id;
    /** The event the client says it last saw. */
    std::string cursor;
    /** The source's producer, and the only thread it may be read on. */
    RequestExchangePtr exchange;
    event::Dispatcher* dispatcher{nullptr};
    /** Whether more is still coming, so there is something to follow. */
    bool producing{false};
  };

  /**
   * Place a Last-Event-ID against the streams this session holds.
   *
   * Looked up within one session and no further, which is what makes
   * replaying another client's events unrepresentable here rather than
   * merely forbidden. An id this session does not know is not an error:
   * the client gets a fresh stream, which is all resuming ever promised.
   *
   * Runs on the thread that owns the session.
   */
  static ResumePoint resumeFrom(SessionCtx& session,
                                const std::string& last_event_id);

  /**
   * Take what a client missed, and arrange for what it has not missed yet.
   *
   * Both in one visit, on the thread that owns the source stream: between
   * reading the buffer and starting to follow it there is a gap, and
   * anything written in that gap would be neither replayed nor forwarded.
   *
   * @param cursor    The last event the client saw. Everything after it,
   *                  in order, is what comes back. An id that is not in
   *                  the buffer — evicted, or never from this stream —
   *                  replays nothing.
   * @param follower  Where anything the source still produces is sent, if
   *                  it is still producing. Null to replay only.
   * @return What the client missed, oldest first.
   */
  static std::vector<RetainedEvent> collectAndFollow(
      const RequestExchangePtr& source,
      const std::string& cursor,
      const RequestExchangePtr& follower,
      event::Dispatcher* follower_dispatcher);

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

  /**
   * How long a stream nothing more will be written to is kept.
   *
   * The window exists so a client whose connection dropped has a chance
   * to come back and say where it got to. Zero keeps them for as long as
   * their session lasts, which is a bound but a very loose one.
   */
  void setClosedStreamRetention(std::chrono::milliseconds retention) {
    closed_stream_retention_ = retention;
  }

  /** Where every resumable stream reports what it is holding. */
  const ReplayAccountingPtr& accounting() const { return accounting_; }

  /** Streams held across every session, which is the size of the index. */
  size_t streamCount() const;

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

  /**
   * Start the clock on streams nothing more will be written to, and close
   * the ones whose time is up. Runs on the session's owner thread.
   */
  void retireStreams(SessionCtx& session);

  /** Draw an id of the given length in hex digits, or empty on failure. */
  std::string drawId(size_t hex_digits) const;

  /** Give up a stream id, so it can be drawn again. */
  void releaseStreamId(const std::string& stream_id);

  event::Dispatcher& dispatcher_;

  // Guards the map structure, the owner pointers and the stream ids in
  // use, and nothing else. The SessionCtx behind an entry is the owning
  // thread's alone.
  mutable std::mutex directory_mutex_;
  std::unordered_map<std::string, Entry> directory_;

  // Every stream id currently spoken for, whichever session holds it.
  // Strings only: what they name belongs to one thread each, and this is
  // read by all of them.
  std::unordered_set<std::string> stream_ids_;

  EntropySource entropy_;

  // One sweep per dispatcher that owns sessions, so a session is only ever
  // examined by the thread entitled to examine it. Created on the owner's
  // thread when it mints its first session.
  std::unordered_map<event::Dispatcher*, event::TimerPtr> sweepers_;

  SessionRemovedCallback session_removed_callback_;
  std::chrono::milliseconds timeout_{300000};
  std::chrono::milliseconds closed_stream_retention_{60000};
  size_t pending_limit_{256};
  ReplayAccountingPtr accounting_{std::make_shared<ReplayAccounting>()};
  bool running_{true};
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_STREAMABLE_SESSION_MANAGER_H
