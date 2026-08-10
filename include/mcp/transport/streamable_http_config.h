/**
 * Streamable HTTP Transport Configuration
 *
 * Settings for the Streamable HTTP transport, where a single endpoint
 * serves JSON-RPC over POST, optional server-to-client SSE streams, and
 * session lifecycle via headers.
 *
 * The server and client structs are deliberately separate: most server
 * settings (session minting, replay buffers, origin policy, bind policy)
 * have no client-side meaning.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "mcp/protocol/protocol_versions.h"

namespace mcp {
namespace transport {

/**
 * Server-side Streamable HTTP settings.
 */
struct StreamableHttpConfig {
  // Endpoint serving POST, and optionally GET and DELETE.
  std::string mcp_path = "/mcp";

  // Standalone server-to-client SSE stream. False answers GET with 405.
  bool enable_get_stream = true;

  // Session tracking. False is stateless mode: no session id is ever
  // minted and none is ever required.
  bool enable_sessions = true;

  // Client-initiated session termination. False answers DELETE with 405.
  bool allow_client_termination = true;

  // Event ids and replay of missed events after a stream reconnects.
  bool enable_resumability = true;

  // Events retained per stream for replay, and the bound on messages
  // queued while no stream is connected.
  size_t replay_buffer_events = 256;

  // Idle window after which a session is discarded.
  std::chrono::milliseconds session_timeout{300000};

  // How long a disconnected stream's state stays replayable.
  std::chrono::milliseconds closed_stream_retention{60000};

  // Interval between SSE comment keep-alives on an idle stream.
  std::chrono::milliseconds keepalive_interval{30000};

  // Require the authenticated principal to match the one the session was
  // minted for. Holding a session id alone must not authorize a request.
  bool require_principal_match = true;

  // Guards binding a non-loopback address, which exposes the endpoint
  // beyond the local host.
  bool allow_public_bind = false;

  // What to do with bytes that arrive on a connection while a streaming
  // response is still open.
  //
  // DecoderGate suspends HTTP parsing and buffers raw bytes until the
  // stream completes, leaving socket reads and close notifications armed
  // so a peer disconnect is still observed immediately.
  //
  // SingleUseClose closes the connection once the stream ends instead.
  enum class StreamConnPolicy { DecoderGate, SingleUseClose };
  StreamConnPolicy stream_conn_policy = StreamConnPolicy::DecoderGate;

  // Cap on bytes buffered behind the gate. Overflow closes the transport;
  // a mid-stream HTTP error response is not possible at that point.
  size_t gated_input_buffer_bytes = 64 * 1024;

  // HTTP/1.0 has no chunked encoding, so an SSE stream to a 1.0 client
  // cannot be framed. False refuses such requests.
  bool allow_sse_to_http_1_0 = false;

  // Origins permitted to reach the endpoint from a browser. Empty applies
  // the localhost defaults.
  std::vector<std::string> allowed_origins;

  // Concurrent standalone streams one session may hold. Requests beyond
  // this are answered with 429.
  size_t max_get_streams_per_session = 4;

  // Protocol revisions this server can actually serve, newest first. A
  // server must never advertise a revision it cannot serve, so support
  // for a new revision adds its constant here only once the pipeline
  // behind it exists.
  //
  // This list, not the single configured protocol version, decides what
  // an initialize response echoes: a peer asking for a listed revision
  // gets it back, anyone else gets the newest entry. Leave the list empty
  // to negotiate against the configured version alone, and add an older
  // revision here to keep serving peers that still request it.
  std::vector<std::string> protocol_versions = {
      protocol::kProtocolVersion20251125, protocol::kProtocolVersion20250618,
      protocol::kProtocolVersion20250326};

  // Keep serving the older HTTP+SSE transport alongside this one.
  bool legacy_http_sse_enabled = true;
};

/**
 * Client-side Streamable HTTP settings.
 */
struct StreamableHttpClientConfig {
  // Path of the server's endpoint.
  std::string mcp_path = "/mcp";

  // Revisions this client accepts, newest first. The first entry is what
  // it offers when initializing.
  std::vector<std::string> protocol_versions = {
      protocol::kProtocolVersion20251125, protocol::kProtocolVersion20250618,
      protocol::kProtocolVersion20250326};

  // Hold a standalone stream for the server to reach this client on.
  // False leaves the conversation request-and-answer only; nothing the
  // server says unprompted can arrive.
  bool open_server_stream = true;

  // Window between losing a stream and asking for it back, doubling
  // each time up to the second, so a server that has just come back is
  // not met by every client it ever had at once.
  std::chrono::milliseconds stream_reconnect_min{250};
  std::chrono::milliseconds stream_reconnect_max{30000};

  // How many times an answer cut off mid-stream is asked for again
  // before the request it belongs to is failed. A server that cannot
  // finish an answer must not be able to keep one request alive forever.
  size_t resume_attempts = 2;

  // How long a stream may say nothing at all before it is treated as
  // gone. Keep-alive comments count as something. Zero disables it,
  // which is the default because a silent stream is not by itself a
  // broken one.
  std::chrono::milliseconds stream_idle_timeout{0};

  // How long each question in the search for what a server speaks may
  // take. Short, because these are questions rather than conversations:
  // a server that opens a stream and never says where to post has
  // answered, and waiting longer will not change the answer.
  std::chrono::milliseconds fallback_probe_timeout{5000};
};

}  // namespace transport
}  // namespace mcp
