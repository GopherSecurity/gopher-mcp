#ifndef MCP_HTTP_RESPONSE_WRITER_H
#define MCP_HTTP_RESPONSE_WRITER_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/core/compat.h"

namespace mcp {
namespace http {

/**
 * Serializes one HTTP response, either as a single framed body or as a live
 * Server-Sent Events stream.
 *
 * The writer decides framing explicitly rather than inferring it from the
 * payload: a unary response carries Content-Length, and a stream carries
 * Transfer-Encoding with each event in its own chunk. A response with
 * neither is unparseable on a persistent connection, because the recipient
 * has no way to know where the body ends.
 *
 * It produces bytes and nothing else. There is no connection reference and
 * it never writes: callers drain the accumulated bytes and put them wherever
 * they belong. That matters because two of its callers run inside a write
 * filter, where writing to the connection would re-enter the write path.
 *
 * One writer serializes one exchange. It is not thread-safe and expects to
 * live on the connection's dispatcher thread.
 */
class ResponseWriter {
 public:
  /**
   * How a streaming body is delimited.
   *
   * Chunked keeps the connection reusable and is the only correct choice for
   * HTTP/1.1. CloseDelimited runs the body to end-of-connection and is what
   * remains when chunked encoding is unavailable, as on HTTP/1.0.
   */
  enum class Framing { Chunked, CloseDelimited };

  /** What the writer has committed to so far. */
  enum class Mode { Idle, Unary, Sse, Finished };

  /** Outcome of asking for a stream. */
  enum class SseStart {
    Streaming,     // stream open, events may be written
    RefusedUnary,  // client cannot take a stream; a complete response was
                   // written in its place and the exchange is over
    Rejected       // programming error: this writer already started a response
  };

  struct Options {
    /** False when answering an HTTP/1.0 request. Chunk syntax is never sent
     *  to such a client, which would not understand it. */
    bool http_1_1 = true;
    /** Whether the connection may be reused after this exchange. */
    bool keep_alive = true;
    /** Permit streaming to an HTTP/1.0 client with a close-delimited body
     *  instead of refusing outright. */
    bool allow_sse_to_http_1_0 = false;
    /** Preferred stream framing; downgraded automatically when the request
     *  version cannot support it. */
    Framing framing = Framing::Chunked;
  };

  /**
   * Notified when a stream opens and closes, so the owner can apply its
   * connection policy — holding back request processing while a response is
   * in flight, and releasing it afterwards.
   */
  class Observer {
   public:
    virtual ~Observer() = default;
    virtual void onSseStreamStarted() = 0;
    /**
     * @param close_connection True when the exchange leaves the connection
     *                         unusable and the owner should close it. The
     *                         writer never closes anything itself.
     */
    virtual void onSseStreamFinished(bool close_connection) = 0;
  };

  /** Response headers in emission order. */
  using HeaderList = std::vector<std::pair<std::string, std::string>>;

  // Two constructors rather than a defaulted argument: Options is a nested
  // type, and its member initializers are not available while this class is
  // still being defined.
  ResponseWriter();
  explicit ResponseWriter(Options options);

  void setObserver(Observer* observer) { observer_ = observer; }

  /**
   * Write a complete response with a Content-Length body. Ends the exchange.
   * @return False if this writer already started a response.
   */
  bool startUnary(int status_code,
                  const HeaderList& headers,
                  const std::string& body = std::string());

  /**
   * Open a Server-Sent Events stream: status line, SSE headers and the
   * chosen framing header. Never emits Content-Length.
   *
   * If the client is HTTP/1.0 and close-delimited streaming is not allowed,
   * this writes a complete 406 instead and reports RefusedUnary — the caller
   * has an answered exchange, not a stream.
   */
  SseStart startSse(int status_code, const HeaderList& headers);

  /** Append one event to an open stream, framed as a single chunk. */
  bool writeEvent(const std::string& event,
                  const std::string& data,
                  const optional<std::string>& id = nullopt);

  /** Append a comment to an open stream — keeps idle connections alive. */
  bool writeComment(const std::string& comment);

  /**
   * End the exchange, emitting the terminating chunk when chunked. Releases
   * the connection for reuse; it does not close the socket. Idempotent.
   */
  bool finish();

  Mode mode() const { return mode_; }

  /** True when the exchange leaves the connection unusable. */
  bool closeAfterFinish() const { return close_after_finish_; }

  /** Bytes serialized but not yet taken by the caller. */
  size_t pendingBytes() const { return out_.length(); }

  /** Hand the serialized bytes to the caller, emptying the writer. */
  size_t drainTo(Buffer& destination);

 private:
  void appendStatusLine(int status_code);
  /** Append caller headers, dropping any that would fight the writer's own
   *  framing decisions. */
  void appendCallerHeaders(const HeaderList& headers);
  void appendHeader(const std::string& name, const std::string& value);
  /** Wrap already-formatted payload bytes in exactly one HTTP chunk. */
  void appendChunk(const Buffer& payload);
  bool appendSsePayload(const Buffer& payload);

  Options options_;
  Observer* observer_{nullptr};
  Mode mode_{Mode::Idle};
  Framing framing_{Framing::Chunked};
  bool close_after_finish_{false};
  OwnedBuffer out_;
};

}  // namespace http
}  // namespace mcp

#endif  // MCP_HTTP_RESPONSE_WRITER_H
