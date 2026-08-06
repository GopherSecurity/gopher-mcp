#ifndef MCP_HTTP_SSE_FORMATTER_H
#define MCP_HTTP_SSE_FORMATTER_H

#include <cstdint>
#include <string>

#include "mcp/buffer.h"
#include "mcp/core/compat.h"

namespace mcp {
namespace http {

/**
 * Server-Sent Events wire formatting.
 *
 * These are pure functions: they append bytes to the caller's buffer and do
 * nothing else. No connection, no socket, no state. Whoever wants the bytes
 * on a wire is responsible for framing and writing them, which is what lets
 * the same formatter serve a raw SSE stream and an HTTP chunked response.
 *
 * The field syntax follows the SSE specification: one `name: value` line per
 * line of the value, terminated by a blank line at the end of an event.
 */

/**
 * Append one SSE field, splitting a multi-line value into one line per
 * physical line as the specification requires.
 *
 * Two edge cases are deliberate and depended upon by callers:
 * an empty value emits nothing at all, and a trailing newline in the value
 * does not produce an extra empty line.
 */
void formatSseField(Buffer& out,
                    const std::string& field,
                    const std::string& value);

/**
 * Append a complete SSE event: optional `event`, optional `id`, the `data`
 * payload, and the blank line that dispatches the event.
 *
 * An empty event name omits the `event` field, which is how an anonymous
 * message event is written.
 */
void formatSseEvent(Buffer& out,
                    const std::string& event,
                    const std::string& data,
                    const optional<std::string>& id = nullopt);

/**
 * Append an SSE comment (`: text`). Comments carry no data and exist to keep
 * an idle stream and any intermediary alive.
 */
void formatSseComment(Buffer& out, const std::string& comment);

/**
 * Append a `retry` directive telling the client how long to wait before
 * reconnecting a dropped stream.
 */
void formatSseRetry(Buffer& out, uint32_t retry_ms);

}  // namespace http
}  // namespace mcp

#endif  // MCP_HTTP_SSE_FORMATTER_H
