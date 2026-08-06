/**
 * HTTP response serialization with explicit framing.
 *
 * See the header for the contract. The one invariant worth restating: every
 * response this writer produces carries exactly one framing decision, either
 * Content-Length or Transfer-Encoding or an explicit close, so a recipient
 * always knows where the body ends.
 */

#include "mcp/http/response_writer.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "mcp/http/http_parser.h"
#include "mcp/http/sse_formatter.h"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace http {

namespace {

const char kCrLf[] = "\r\n";

std::string lowercase(const std::string& value) {
  std::string result = value;
  std::transform(result.begin(), result.end(), result.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return result;
}

/**
 * Headers the writer derives from the response itself. A caller-supplied
 * value for any of these would contradict the framing actually emitted, so
 * they are dropped rather than duplicated.
 */
bool isFramingHeader(const std::string& name) {
  const std::string lower = lowercase(name);
  return lower == "content-length" || lower == "transfer-encoding" ||
         lower == "connection";
}

/** Headers that describe the stream itself and are not the caller's to set. */
bool isStreamHeader(const std::string& name) {
  const std::string lower = lowercase(name);
  return lower == "content-type" || lower == "cache-control" ||
         lower == "x-accel-buffering";
}

std::string toHex(size_t value) {
  std::ostringstream hex;
  hex << std::hex << value;
  return hex.str();
}

}  // namespace

ResponseWriter::ResponseWriter() : ResponseWriter(Options()) {}

ResponseWriter::ResponseWriter(Options options)
    : options_(options), framing_(options.framing) {}

void ResponseWriter::appendStatusLine(int status_code) {
  std::ostringstream line;
  line << (options_.http_1_1 ? "HTTP/1.1 " : "HTTP/1.0 ") << status_code << " "
       << httpStatusCodeToString(static_cast<HttpStatusCode>(status_code))
       << kCrLf;
  const std::string bytes = line.str();
  out_.add(bytes.c_str(), bytes.length());
}

void ResponseWriter::appendHeader(const std::string& name,
                                  const std::string& value) {
  out_.add(name.c_str(), name.length());
  out_.add(": ", 2);
  out_.add(value.c_str(), value.length());
  out_.add(kCrLf, 2);
}

void ResponseWriter::appendCallerHeaders(const HeaderList& headers) {
  for (const auto& header : headers) {
    if (isFramingHeader(header.first)) {
      GOPHER_LOG_DEBUG("ResponseWriter: ignoring caller-supplied {} header",
                       header.first);
      continue;
    }
    if (mode_ == Mode::Sse && isStreamHeader(header.first)) {
      GOPHER_LOG_DEBUG("ResponseWriter: ignoring caller-supplied {} on stream",
                       header.first);
      continue;
    }
    appendHeader(header.first, header.second);
  }
}

bool ResponseWriter::startUnary(int status_code,
                                const HeaderList& headers,
                                const std::string& body) {
  if (mode_ != Mode::Idle) {
    GOPHER_LOG_ERROR(
        "ResponseWriter: startUnary on a writer that already started a "
        "response");
    return false;
  }

  mode_ = Mode::Unary;
  appendStatusLine(status_code);
  appendCallerHeaders(headers);
  appendHeader("Content-Length", std::to_string(body.length()));
  appendHeader("Connection", options_.keep_alive ? "keep-alive" : "close");
  out_.add(kCrLf, 2);
  if (!body.empty()) {
    out_.add(body.c_str(), body.length());
  }

  close_after_finish_ = !options_.keep_alive;
  mode_ = Mode::Finished;
  return true;
}

ResponseWriter::SseStart ResponseWriter::startSse(int status_code,
                                                  const HeaderList& headers) {
  if (mode_ != Mode::Idle) {
    GOPHER_LOG_ERROR(
        "ResponseWriter: startSse on a writer that already started a "
        "response");
    return SseStart::Rejected;
  }

  // Chunked encoding does not exist before HTTP/1.1. A 1.0 client can only
  // be streamed to by running the body to connection close, and only when
  // the deployment has said that is acceptable.
  if (!options_.http_1_1) {
    if (!options_.allow_sse_to_http_1_0) {
      GOPHER_LOG_DEBUG(
          "ResponseWriter: refusing an event stream to an HTTP/1.0 client");
      options_.keep_alive = false;
      startUnary(static_cast<int>(HttpStatusCode::NotAcceptable),
                 HeaderList{{"Content-Type", "application/json"}},
                 "{\"error\":\"event_stream_requires_http_1_1\"}");
      return SseStart::RefusedUnary;
    }
    framing_ = Framing::CloseDelimited;
  }

  mode_ = Mode::Sse;
  appendStatusLine(status_code);
  appendCallerHeaders(headers);
  appendHeader("Content-Type", "text/event-stream");
  appendHeader("Cache-Control", "no-cache");
  // Intermediaries that buffer a response would hold events until the stream
  // ended, which for a live stream means indefinitely.
  appendHeader("X-Accel-Buffering", "no");

  if (framing_ == Framing::Chunked) {
    appendHeader("Transfer-Encoding", "chunked");
    appendHeader("Connection", options_.keep_alive ? "keep-alive" : "close");
    close_after_finish_ = !options_.keep_alive;
  } else {
    // Without a framing header the body is delimited by the close itself,
    // so the connection cannot be reused and must say so.
    appendHeader("Connection", "close");
    close_after_finish_ = true;
  }
  out_.add(kCrLf, 2);

  if (observer_) {
    observer_->onSseStreamStarted();
  }
  return SseStart::Streaming;
}

void ResponseWriter::appendChunk(const Buffer& payload) {
  const size_t size = payload.length();
  if (size == 0) {
    // A zero-length chunk is the terminator; emitting one here would end the
    // body mid-stream.
    return;
  }
  const std::string size_line = toHex(size) + kCrLf;
  out_.add(size_line.c_str(), size_line.length());
  out_.add(payload);
  out_.add(kCrLf, 2);
}

bool ResponseWriter::appendSsePayload(const Buffer& payload) {
  if (mode_ != Mode::Sse) {
    GOPHER_LOG_ERROR("ResponseWriter: event written outside an open stream");
    return false;
  }
  if (framing_ == Framing::Chunked) {
    appendChunk(payload);
  } else {
    out_.add(payload);
  }
  return true;
}

bool ResponseWriter::writeEvent(const std::string& event,
                                const std::string& data,
                                const optional<std::string>& id) {
  OwnedBuffer payload;
  formatSseEvent(payload, event, data, id);
  return appendSsePayload(payload);
}

bool ResponseWriter::writeComment(const std::string& comment) {
  OwnedBuffer payload;
  formatSseComment(payload, comment);
  return appendSsePayload(payload);
}

bool ResponseWriter::finish() {
  if (mode_ == Mode::Finished) {
    return true;
  }
  if (mode_ != Mode::Sse) {
    GOPHER_LOG_ERROR("ResponseWriter: finish before a response was started");
    return false;
  }

  if (framing_ == Framing::Chunked) {
    out_.add("0\r\n\r\n", 5);
  }
  mode_ = Mode::Finished;

  if (observer_) {
    observer_->onSseStreamFinished(close_after_finish_);
  }
  return true;
}

size_t ResponseWriter::drainTo(Buffer& destination) {
  const size_t moved = out_.length();
  if (moved > 0) {
    out_.move(destination);
  }
  return moved;
}

}  // namespace http
}  // namespace mcp
