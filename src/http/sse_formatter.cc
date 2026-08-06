/**
 * Server-Sent Events wire formatting.
 *
 * Pure byte production, no I/O. See the header for the contract.
 */

#include "mcp/http/sse_formatter.h"

#include <sstream>

namespace mcp {
namespace http {

void formatSseField(Buffer& out,
                    const std::string& field,
                    const std::string& value) {
  // getline splits on '\n' and yields no lines at all for an empty value,
  // which is exactly the behavior callers rely on: an event with no data
  // emits no data field rather than an empty one.
  std::istringstream stream(value);
  std::string line;
  while (std::getline(stream, line)) {
    out.add(field.c_str(), field.length());
    out.add(": ", 2);
    out.add(line.c_str(), line.length());
    out.add("\n", 1);
  }
}

void formatSseEvent(Buffer& out,
                    const std::string& event,
                    const std::string& data,
                    const optional<std::string>& id) {
  if (!event.empty()) {
    formatSseField(out, "event", event);
  }

  if (id.has_value()) {
    formatSseField(out, "id", id.value());
  }

  formatSseField(out, "data", data);

  // The blank line is what makes the client dispatch the event.
  out.add("\n", 1);
}

void formatSseComment(Buffer& out, const std::string& comment) {
  out.add(": ", 2);
  out.add(comment.c_str(), comment.length());
  out.add("\n\n", 2);
}

void formatSseRetry(Buffer& out, uint32_t retry_ms) {
  const std::string retry = "retry: " + std::to_string(retry_ms) + "\n\n";
  out.add(retry.c_str(), retry.length());
}

}  // namespace http
}  // namespace mcp
