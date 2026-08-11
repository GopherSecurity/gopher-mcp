#ifndef MCP_TRANSPORT_REQUEST_EXCHANGE_H
#define MCP_TRANSPORT_REQUEST_EXCHANGE_H

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/core/compat.h"
#include "mcp/core/result.h"
#include "mcp/event/event_loop.h"
#include "mcp/http/response_writer.h"
#include "mcp/types.h"

namespace mcp {

namespace network {
class Connection;
}

namespace transport {

/**
 * Where an exchange's bytes go.
 *
 * Separated from the exchange so the destination can change underneath it.
 * A response that is still being produced when its connection dies has to
 * keep going somewhere, and the exchange should not have to know whether it
 * is still talking to a socket.
 */
class ExchangeSink {
 public:
  virtual ~ExchangeSink() = default;

  /** Emit bytes. False when the destination is gone and they were dropped. */
  virtual bool write(Buffer& data) = 0;

  /** Whether anything written now would actually reach a peer. */
  virtual bool alive() const = 0;

  /**
   * Tell the sink that its destination is currently mid-write, so writing
   * again right now would corrupt what is already going out. Only sinks
   * backed by something non-re-entrant care.
   */
  virtual void setWriteInProgress(bool in_progress) { (void)in_progress; }
};

using ExchangeSinkPtr = std::unique_ptr<ExchangeSink>;

/**
 * One event held for a client that may come back for it.
 *
 * Kept before framing rather than after: a resuming client asks for
 * everything after some event id, which means the events have to be
 * individually addressable. Framed bytes are one opaque run and cannot be
 * replayed selectively.
 */
struct RetainedEvent {
  std::string id;
  std::string event;
  std::string data;
};

/**
 * How much a server is holding for clients that might come back.
 *
 * Shared by every resumable stream so the total can be answered without
 * visiting each one, which is not something a single thread may do: the
 * streams belong to whichever dispatcher accepted them. Atomic for the
 * same reason.
 */
struct ReplayAccounting {
  std::atomic<size_t> events{0};
  std::atomic<size_t> bytes{0};
};

using ReplayAccountingPtr = std::shared_ptr<ReplayAccounting>;

/**
 * A sink that drops what it is given, keeping only the bytes.
 *
 * Used once a stream is detached from its connection — what is worth
 * keeping is kept by the exchange, in the form a resuming client can be
 * given — and by tests that want to observe an exchange without standing
 * up a socket.
 */
class RetainedExchangeSink : public ExchangeSink {
 public:
  RetainedExchangeSink() = default;

  bool write(Buffer& data) override;
  bool alive() const override { return true; }

  /** Bytes handed to write(), which is how unary responses are observed. */
  const std::string& bytes() const { return bytes_; }

 private:
  std::string bytes_;
};

/** A sink that writes to a live connection. */
class ConnectionExchangeSink : public ExchangeSink {
 public:
  ConnectionExchangeSink(network::Connection* connection)
      : connection_(connection) {}

  bool write(Buffer& data) override;
  bool alive() const override;

  network::Connection* connection() const { return connection_; }

  /**
   * Refuse to write while the connection is inside its own write() call.
   *
   * Connection writes are not re-entrant: the connection holds a pointer to
   * the buffer being written for the duration, and a nested write clobbers
   * it. The owner sets this while it is on such a stack.
   */
  void setWriteInProgress(bool in_progress) override {
    write_in_progress_ = in_progress;
  }

  void detach() { connection_ = nullptr; }

 private:
  network::Connection* connection_;
  bool write_in_progress_{false};
};

/**
 * Tells interested parties, once, that the work behind a request is no
 * longer wanted.
 *
 * Fires when the peer goes away, so a handler that is still producing can
 * stop. Observers registered after the fact fire immediately, because a
 * cancellation that a late observer never hears about is worse than one
 * delivered out of order.
 */
class CancellationToken {
 public:
  using Observer = std::function<void()>;

  bool cancelled() const { return cancelled_; }

  void addObserver(Observer observer);

  /** Idempotent. Each observer runs exactly once. */
  void cancel();

 private:
  bool cancelled_{false};
  std::vector<Observer> observers_;
};

/**
 * What a request told us about the peer that sent it.
 *
 * Carried per request rather than per session because a protocol revision
 * without a handshake has no session-establishing moment to record it at —
 * every request states its own terms.
 */
struct ExchangeClientContext {
  /** Protocol revision in force for this request. */
  std::string protocol_version;
  /**
   * The request's params._meta, still in its serialized form. Nested JSON
   * arrives stringified, so it is carried as it came and parsed by whoever
   * actually needs a field out of it.
   */
  optional<std::string> raw_meta;

  /**
   * What the request said it would accept. Recorded rather than acted on:
   * a response that streams and a response that does not are framed
   * differently and irreversibly, so whoever chooses between them needs to
   * know what the peer can read before the first byte goes out.
   *
   * Both default to true, which is what a request with no Accept header
   * means — anything.
   */
  bool accepts_json{true};
  bool accepts_sse{true};

  /**
   * Whether the request said anything at all about what it accepts.
   *
   * Kept apart from the two above because silence and consent are not the
   * same answer, and which of them a missing header is depends on what is
   * being asked. A request that leaves the framing of an ordinary answer
   * to the server has said nothing and meant anything. A request for a
   * stream has to name the one thing a stream is made of, so silence
   * there is not a request for one.
   */
  bool stated_accept{false};

  /**
   * Who the request is from, as resolved when it arrived.
   *
   * Recorded here rather than looked up later because a session is bound
   * to the caller who created it, and by the time that check matters the
   * request's headers are long gone. Empty when nothing resolved one.
   */
  std::string principal;
};

/**
 * The runtime for one inbound request, from dispatch to final byte.
 *
 * A dispatch context is deliberately stack-scoped — it dies when the
 * callback returns, which is what makes a stale reply path unrepresentable.
 * That leaves nowhere to hang anything that outlives the callback: a
 * response still being streamed, a cancellation the peer has not sent yet,
 * a header decided after the handler ran. This is that place.
 *
 * Reference-counted and explicitly allowed to outlive the dispatch that
 * created it. Lives on its connection's dispatcher thread; every method
 * asserts as much, and other threads reach it through dispatcher.post().
 */
class RequestExchange : public std::enable_shared_from_this<RequestExchange> {
 public:
  /** What the exchange has committed to. */
  enum class Mode {
    Open,     // nothing decided yet
    Json,     // answered with a single response
    Stream,   // streaming events
    Complete  // finished; nothing further may be written
  };

  /**
   * Where the request is in its own lifetime.
   *
   * Separate from Mode, which says how the response is framed. This says
   * what the request is doing, and it belongs here rather than on the
   * connection because a connection serves many requests in sequence and
   * its own mode is fixed for its whole life — there is nowhere on it to
   * record something that differs from one keep-alive request to the next.
   */
  enum class Phase {
    ReceivingBody,    // the request is still arriving
    Dispatching,      // handed to the application, no answer yet
    RespondingJson,   // answered with a single JSON-RPC response
    Responding202,    // accepted; there is nothing to answer with
    RespondingError,  // answered with a transport-level error

    // A streamed answer, which unlike the others is not over the moment it
    // begins. Open is still producing; Draining has emitted the response
    // and is finishing the framing; Closed has nothing left to send but is
    // not yet released.
    RespondingSseOpen,
    RespondingSseDraining,
    RespondingSseClosed,

    Done  // nothing further will happen
  };

  /**
   * @param dispatcher The thread this exchange belongs to.
   * @param sink       Where its bytes go, initially.
   * @param id         The inbound request id, when there is one. A stream
   *                   opened without a request (a client asking to be
   *                   pushed to) has none, and inventing one would put a
   *                   phantom entry in any correlation map.
   */
  static std::shared_ptr<RequestExchange> create(event::Dispatcher& dispatcher,
                                                 ExchangeSinkPtr sink,
                                                 const optional<RequestId>& id);

  ~RequestExchange();

  /** Events dropped from the replay buffer because it was full. */
  size_t droppedEvents() const { return dropped_events_; }

  const optional<RequestId>& requestId() const { return request_id_; }
  Mode mode() const { return mode_; }
  Phase phase() const { return phase_; }

  /**
   * Record where the request has got to. Refused once the exchange is Done,
   * so a late callback cannot reopen something that is over.
   */
  bool setPhase(Phase phase);

  /**
   * Name the request this exchange is answering.
   *
   * Separate from construction because an exchange is made when the request
   * headers arrive, and the id is not known until the body has been parsed.
   * Settable once, and not after the answer has begun: the id is what a
   * correlation lookup finds the exchange by, and moving it would strand
   * anything already holding the old one.
   */
  bool setRequestId(const RequestId& id);

  /**
   * Whether the response should say the client is speaking HTTP/1.1 and may
   * reuse the connection. Captured when the exchange is made rather than
   * read when it answers: by then the connection may be handling a
   * different request, or none.
   */
  void setResponseOptions(bool http_1_1, bool keep_alive);

  /**
   * Set the status a JSON response will carry. Only meaningful before the
   * first byte; false afterwards.
   */
  bool setStatus(int status_code);

  /**
   * Add a response header. Only meaningful before the first byte, which is
   * what makes it possible to attach something the handler decided — a
   * session id, say — after dispatch has run.
   */
  bool setResponseHeader(const std::string& name, const std::string& value);

  /**
   * Take a response header back. Only meaningful before the first byte —
   * once a header has gone out it has been said.
   *
   * @return True when a header of that name was removed.
   */
  bool removeResponseHeader(const std::string& name);

  /**
   * Headers to include whenever this exchange frames its own response.
   *
   * Separate from setResponseHeader because these are not a reason to
   * frame one: a plain answer still goes out through the codec downstream,
   * exactly as it always has. They are what a response has to carry when
   * that codec is bypassed and would otherwise say nothing — who is
   * allowed to read it, and what it is.
   *
   * Snapshotted per request rather than resolved when the response is
   * written, since a streamed answer is framed long after the request that
   * settled what it may say.
   */
  void setFramedHeaders(const http::ResponseWriter::HeaderList& headers) {
    framed_headers_ = headers;
  }

  /**
   * Answer with a single JSON-RPC response and finish.
   *
   * A plain 200 with no added headers is written as bare JSON and framed by
   * the HTTP codec downstream, exactly as an ordinary response always has
   * been. Anything else has to be framed here, because that codec emits a
   * fixed status and header set.
   */
  VoidResult respondJson(const jsonrpc::Response& response);

  /**
   * Answer with a body this exchange frames itself, and finish.
   *
   * For answers that are not a JSON-RPC response: an empty 202, or an error
   * whose id is null. Neither can go through respondJson — a
   * jsonrpc::Response must carry an id, and there is no null id to give it —
   * and neither can go through the downstream codec, which emits one fixed
   * status and header set.
   *
   * @param content_type Sent as Content-Type; omitted when empty.
   * @param body         The body, which may be empty.
   */
  VoidResult respondUnary(const std::string& content_type,
                          const std::string& body);

  /** Begin streaming. Mutually exclusive with respondJson. */
  bool beginStream();

  /**
   * Make this stream resumable: its events carry ids on the wire, and are
   * kept, bounded, for a client that comes back asking for what it missed.
   *
   * One call rather than two switches, because an id is a promise and this
   * is what makes it keepable: a client that reads an id may return with
   * it, so ids go out exactly where there is a buffer behind them and a
   * stream identity to find that buffer by.
   *
   * @param stream_id  Prefixes every id this stream mints, which is what
   *                   makes those ids unique beyond this one stream.
   * @param accounting Where the cost of what is being kept is reported.
   */
  void makeResumable(const std::string& stream_id,
                     const ReplayAccountingPtr& accounting);

  /** The stream id this exchange's events are minted under, if any. */
  const std::string& streamId() const { return stream_id_; }

  /**
   * Give up what was kept for a returning client.
   *
   * Called when nobody may ask for it any more. Explicit rather than left
   * to the last reference going away, so the moment the memory is released
   * is a moment that can be pointed at.
   */
  void releaseReplay();

  /**
   * Told about each event as it is written, with the id it went out under.
   *
   * What a stream that is being followed reports through: a client that
   * reconnected elsewhere is owed not only what it missed but whatever
   * comes next.
   */
  void setEventObserver(std::function<void(const RetainedEvent&)> observer) {
    event_observer_ = std::move(observer);
  }

  /**
   * Told when this exchange's stream opens and closes.
   *
   * The connection needs to know: while a response streams, HTTP/1.1 gives
   * it no way to answer anything else, so it has to stop turning arriving
   * bytes into requests until the stream ends.
   */
  void setStreamObserver(http::ResponseWriter::Observer* observer) {
    stream_observer_ = observer;
  }

  /**
   * Append one event to an open stream.
   *
   * @param id An id from somewhere else — a replayed event, or one
   *           forwarded from a stream this one is carrying on for. It goes
   *           out as given and does not advance this stream's own
   *           sequence, because it belongs to another stream's cursor.
   */
  bool writeEvent(const std::string& event,
                  const std::string& data,
                  const optional<std::string>& id = nullopt);

  /**
   * Append a comment to an open stream.
   *
   * A stream that says nothing for minutes is indistinguishable from a
   * dead one to anything sitting between the two ends, so an idle one says
   * something meaningless on purpose. Comments are not events: nothing
   * retains them and no client sees them as data.
   */
  bool writeComment(const std::string& comment);

  /** Finish the exchange. Idempotent. */
  bool complete();

  /**
   * Told once, when the exchange finishes producing. Whoever is holding a
   * detached exchange uses this to start counting down to letting it go.
   */
  void setCompletionObserver(std::function<void()> observer) {
    completion_observer_ = std::move(observer);
  }

  CancellationToken& cancellation() { return cancellation_; }
  ExchangeClientContext& clientContext() { return client_context_; }
  const ExchangeClientContext& clientContext() const { return client_context_; }

  /**
   * Whether this exchange should carry on after its connection dies.
   *
   * A completed or single-response exchange has nothing left to do and is
   * released. A stream whose result a client is expected to come back for
   * is not: it keeps producing into a retained buffer so a reconnecting
   * client can pick up where it left off.
   */
  void setRetainOnDisconnect(bool retain) { retain_on_disconnect_ = retain; }
  bool retainOnDisconnect() const { return retain_on_disconnect_; }

  /**
   * How many events to keep for a client that may come back. Bounded
   * because a producer that never stops would otherwise grow without limit
   * behind a client that never returns.
   */
  void setRetainedEventLimit(size_t events) { retained_event_limit_ = events; }

  /** True once the connection is gone and the exchange kept going. */
  bool detached() const { return detached_; }

  /**
   * The connection this exchange was born on has gone away. Either detach
   * and keep producing, or give up and cancel.
   * @return True when the exchange survived and still needs an owner.
   */
  bool onConnectionGone();

  /** Events held for a client that comes back for them. */
  const std::deque<RetainedEvent>& retainedEvents() const { return replay_; }

  /** The sink, for tests and for the owner that swapped it in. */
  ExchangeSink& sink() { return *sink_; }

  /** Pass a mid-write warning down to wherever this exchange writes. */
  void setWriteInProgress(bool in_progress) {
    sink_->setWriteInProgress(in_progress);
  }

 private:
  RequestExchange(event::Dispatcher& dispatcher,
                  ExchangeSinkPtr sink,
                  const optional<RequestId>& id);

  void assertOnDispatcher() const;
  bool writeBytes(const std::string& bytes);
  /** Whether events here are worth keeping, and why. */
  bool resumable() const { return !stream_id_.empty(); }
  /** Keep one event, dropping the oldest when the bound is reached. */
  void retainEvent(const RetainedEvent& event);
  /** True when the response cannot be expressed by the downstream codec. */
  bool needsOwnFraming() const;
  /**
   * The headers of a self-framed response: what the caller set, with the
   * framed-only ones behind them and a content type if none was named.
   */
  http::ResponseWriter::HeaderList framedHeaders(
      const std::string& content_type) const;

  event::Dispatcher& dispatcher_;
  ExchangeSinkPtr sink_;
  optional<RequestId> request_id_;

  Mode mode_{Mode::Open};
  Phase phase_{Phase::ReceivingBody};
  bool first_byte_written_{false};
  bool retain_on_disconnect_{false};
  bool detached_{false};

  int status_code_{200};
  http::ResponseWriter::HeaderList headers_;
  http::ResponseWriter::HeaderList framed_headers_;
  http::ResponseWriter::Options writer_options_;
  std::unique_ptr<http::ResponseWriter> stream_writer_;

  CancellationToken cancellation_;
  ExchangeClientContext client_context_;
  std::function<void()> completion_observer_;

  // What a returning client would be given, oldest first. Filled while
  // this stream is resumable and while nobody is connected to it — the two
  // cases where an event may still be asked for after it was written.
  std::deque<RetainedEvent> replay_;
  size_t retained_event_limit_{256};
  size_t dropped_events_{0};
  ReplayAccountingPtr accounting_;

  std::string stream_id_;
  size_t next_sequence_{1};
  std::function<void(const RetainedEvent&)> event_observer_;
  http::ResponseWriter::Observer* stream_observer_{nullptr};
};

using RequestExchangePtr = std::shared_ptr<RequestExchange>;

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_REQUEST_EXCHANGE_H
