/**
 * @file transport_probe.h
 * @brief Working out which protocol era a server at a URL speaks
 *
 * A client pointed at an arbitrary MCP URL has to find out what is
 * there. Guessing from the URL is guessing about a string; this asks
 * the server.
 *
 * The order the question is asked in is not obvious, and it is not
 * "introduce yourself first". The newest revision has no introduction
 * at all, so a modern server asked to initialize refuses — and that
 * refusal looks, to anything not paying attention, exactly like a
 * server that does not serve this endpoint. A client that read it that
 * way would fall through to the oldest transport, fail there too, and
 * report the wrong thing about the wrong attempt.
 *
 * So: ask whether it is modern, then ask it to initialize, and fall
 * back only when that refusal is not a modern one. Telling those apart
 * is what lives here, and it is a question about a body and a status
 * rather than about any era — which is why it can be settled, and
 * tested, without either era's machinery.
 */

#ifndef MCP_CLIENT_TRANSPORT_PROBE_H
#define MCP_CLIENT_TRANSPORT_PROBE_H

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "mcp/event/event_loop.h"
#include "mcp/http/http_async_client.h"
#include "mcp/network/socket_interface.h"

namespace mcp {
namespace client {

/**
 * Refusals that only a server speaking the newest revision produces.
 *
 * Kept beside the one thing that reads them. When the modern era
 * arrives and something else needs them, they can move; putting them
 * where nothing yet looks would be filing them under a guess.
 */
namespace modern_error {

// The request's headers did not say what this server requires.
constexpr int kHeaderMismatch = -32020;

// This server has never heard of the method — which, for `initialize`,
// is a server that does not have one.
constexpr int kMethodNotFound = -32601;

// Named in the error's data rather than given a code of its own.
constexpr const char* kUnsupportedProtocolVersion =
    "UnsupportedProtocolVersionError";

}  // namespace modern_error

/**
 * True when this answer could only have come from a server speaking the
 * newest revision.
 *
 * Deliberately narrow. A body is a modern refusal only if it is a
 * JSON-RPC error object — an `error` that is an object carrying an
 * integer `code` — and only for the two statuses such a refusal comes
 * back with. Anything looser would read this project's own 404 for an
 * unknown path, `{"error":"not_found"}`, as a modern server and refuse
 * to fall back to the transport that would have worked.
 *
 * Pure, and therefore answerable without a server.
 */
bool isModernRefusal(int status_code, const std::string& body);

/**
 * True when this answer is a server introducing itself back.
 *
 * A status is not enough. The older transport answers a POST with 202
 * and nothing else — it has taken the message, and the answer will
 * arrive on the stream it expects the client to be holding. Reading
 * that as an introduction would have a client settle on the wrong
 * transport against a server that was telling it so.
 *
 * So an introduction has been answered when the answer carries a
 * JSON-RPC result, or when it is a stream, which is where the result
 * will be.
 */
bool isInitializeAnswer(int status_code,
                        const std::string& content_type,
                        const std::string& body);

/**
 * What a probe found.
 *
 * NotModern carries what the server said, so the rung after this one
 * does not have to ask again.
 */
struct ProbeResult {
  enum class Verdict {
    // Speaks the newest revision.
    Modern,

    // Answered, and not as a modern server would.
    NotModern,

    // Could not be asked: nothing listening, nothing answering, or the
    // asking ran out of time.
    Unreachable
  };

  Verdict verdict{Verdict::Unreachable};

  // Set for NotModern: the status and body that decided it, so a
  // failure can say what was actually said rather than that something
  // went wrong.
  int status_code{0};
  std::string body;

  // Set for NotModern: what kind of answer it was, since a status
  // alone cannot tell an introduction that was answered from one that
  // was merely accepted.
  std::string content_type;

  // Set for NotModern where the server named a session — the classic
  // rung's introduction is a real one, and the session it is given is
  // the session the connection that follows should use.
  std::string session_id;

  // Set for Unreachable.
  std::string error;

  static ProbeResult modern() {
    ProbeResult result;
    result.verdict = Verdict::Modern;
    return result;
  }
  static ProbeResult notModern(int status_code,
                               std::string body,
                               std::string session_id = std::string(),
                               std::string content_type = std::string()) {
    ProbeResult result;
    result.verdict = Verdict::NotModern;
    result.status_code = status_code;
    result.body = std::move(body);
    result.session_id = std::move(session_id);
    result.content_type = std::move(content_type);
    return result;
  }
  static ProbeResult unreachable(std::string error) {
    ProbeResult result;
    result.verdict = Verdict::Unreachable;
    result.error = std::move(error);
    return result;
  }
};

using ProbeCallback = std::function<void(const ProbeResult&)>;

/**
 * One rung of the ladder.
 *
 * Asynchronous because everything at this layer is: the answer arrives
 * on the dispatcher thread, and a probe that blocked waiting for it
 * would block the thread the answer comes in on.
 */
class TransportProbe {
 public:
  virtual ~TransportProbe() = default;

  /** Ask, and report once. Dispatcher thread. */
  virtual void probe(const std::string& url, ProbeCallback done) = 0;
};

using TransportProbePtr = std::unique_ptr<TransportProbe>;

/**
 * The modern rung, standing empty.
 *
 * A modern probe is a request this client cannot yet build — modern
 * request construction does not exist. Rather than leave the rung out
 * and have the ladder rewritten when it does, the rung is here and
 * answers "not modern" straight away. What replaces it replaces only
 * this.
 */
class NoModernProbe : public TransportProbe {
 public:
  void probe(const std::string& url, ProbeCallback done) override;
};

/**
 * The classic rung: introduce yourself and see what comes back.
 *
 * One POST, on a connection of its own, under a deadline of its own —
 * the HTTP client underneath has none, and a server that accepts a
 * connection and then says nothing would otherwise hold the whole
 * ladder.
 *
 * The introduction is a real one. A server that answers it has been
 * introduced to, and the session it names is the session the connection
 * that follows should use rather than a second one this walks away
 * from.
 */
class ClassicProbe : public TransportProbe {
 public:
  ClassicProbe(event::Dispatcher& dispatcher,
               network::SocketInterface& socket_interface,
               std::string protocol_version,
               std::string client_name,
               std::string client_version,
               std::chrono::milliseconds timeout);
  ~ClassicProbe() override;

  void probe(const std::string& url, ProbeCallback done) override;

 private:
  // Report once, whichever of the answer and the deadline arrives
  // first, and stop the other from reporting after it.
  void settle(const ProbeResult& result);

  // Built per probe rather than once, because what carries the request
  // depends on the URL being probed and the URL is not known until then.
  std::unique_ptr<http::HttpAsyncClient> clientFor(const std::string& url);

  event::Dispatcher& dispatcher_;
  network::SocketInterface& socket_interface_;
  std::chrono::milliseconds timeout_;
  std::string protocol_version_;
  std::string client_name_;
  std::string client_version_;

  std::unique_ptr<http::HttpAsyncClient> http_;
  event::TimerPtr deadline_;
  ProbeCallback done_;
};

}  // namespace client
}  // namespace mcp

#endif  // MCP_CLIENT_TRANSPORT_PROBE_H
