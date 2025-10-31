/**
 * @file request_logger_filter.cc
 * @brief Implementation of JSON-RPC request logging filter
 */

#include "mcp/filter/request_logger_filter.h"

#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#define GOPHER_LOG_COMPONENT "filter.request_logger"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace filter {

namespace {

std::string directionToString(
    RequestLoggerFilter::MessageDirection direction) {
  switch (direction) {
    case RequestLoggerFilter::MessageDirection::Incoming:
      return "in";
    case RequestLoggerFilter::MessageDirection::Outgoing:
      return "out";
    case RequestLoggerFilter::MessageDirection::Internal:
      return "internal";
  }
  return "unknown";
}

std::string logLevelToString(RequestLoggerFilter::LogLevel level) {
  switch (level) {
    case RequestLoggerFilter::LogLevel::INFO:
      return "INFO";
    case RequestLoggerFilter::LogLevel::DEBUG:
      return "DEBUG";
    case RequestLoggerFilter::LogLevel::VERBOSE:
      return "VERBOSE";
  }
  return "DEBUG";
}

std::string requestIdToString(const RequestId& id) {
  std::string result;
  mcp::match(
      id, [&result](const std::string& value) { result = value; },
      [&result](int value) { result = std::to_string(value); });
  return result;
}

}  // namespace

RequestLoggerFilter::RequestLoggerFilter(const Config& config)
    : config_(config) {
  // DEBUG TRACE
  std::cout << "\n🏗️  [RequestLogger] CONSTRUCTOR" << std::endl;
  std::cout << "   Log level: " << static_cast<int>(config_.log_level) << std::endl;
  std::cout << "   Log format: " << static_cast<int>(config_.log_format) << std::endl;
  std::cout << "   Output: " << static_cast<int>(config_.output) << std::endl;
  std::cout << "   Include timestamps: " << (config_.include_timestamps ? "true" : "false") << std::endl;
  std::cout << "   Include payload: " << (config_.include_payload ? "true" : "false") << std::endl;

  if (config_.output == Output::FILE) {
    std::cout << "   Opening log file: " << config_.output_path << std::endl;
    file_stream_.emplace(config_.output_path,
                         std::ios::out | std::ios::app | std::ios::binary);
    if (!file_stream_->is_open()) {
      std::cout << "❌ [RequestLogger] Failed to open log file, falling back to stderr" << std::endl;
      GOPHER_LOG(Error,
                 "RequestLoggerFilter failed to open log file '{}', "
                 "falling back to stderr",
                 config_.output_path);
      file_stream_.reset();
      config_.output = Output::STDERR;
    } else {
      std::cout << "✅ [RequestLogger] Log file opened successfully" << std::endl;
    }
  }

  std::cout << "✅ [RequestLogger] Constructor completed" << std::endl;
}

RequestLoggerFilter::~RequestLoggerFilter() {
  // DEBUG TRACE
  std::cout << "\n🔧 [RequestLogger] DESTRUCTOR" << std::endl;

  if (file_stream_ && file_stream_->is_open()) {
    std::cout << "   Flushing and closing log file..." << std::endl;
    file_stream_->flush();
    file_stream_->close();
    std::cout << "   ✅ Log file closed" << std::endl;
  }

  std::cout << "✅ [RequestLogger] Destructor completed" << std::endl;
}

void RequestLoggerFilter::onRequest(const jsonrpc::Request& request) {
  // DEBUG TRACE
  std::cout << "\n🟢 [RequestLogger] onRequest() ENTRY" << std::endl;
  std::cout << "   Method: " << request.method << std::endl;
  std::cout << "   ID: " << requestIdToString(request.id) << std::endl;

  std::ostringstream summary;
  summary << "method=" << request.method;
  summary << " id=" << requestIdToString(request.id);

  json::JsonValue payload = json::to_json(request);
  logJsonRpcMessage("request", MessageDirection::Incoming, summary.str(),
                    payload);

  std::cout << "✅ [RequestLogger] Request logged, propagating to next handler" << std::endl;

  if (next_callbacks_) {
    next_callbacks_->onRequest(request);
  } else {
    std::cout << "⚠️  [RequestLogger] No next handler registered!" << std::endl;
  }
}

void RequestLoggerFilter::onNotification(
    const jsonrpc::Notification& notification) {
  // DEBUG TRACE
  std::cout << "\n🟢 [RequestLogger] onNotification() ENTRY" << std::endl;
  std::cout << "   Method: " << notification.method << std::endl;

  std::ostringstream summary;
  summary << "method=" << notification.method;

  json::JsonValue payload = json::to_json(notification);
  logJsonRpcMessage("notification", MessageDirection::Outgoing, summary.str(),
                    payload);

  if (next_callbacks_) {
    next_callbacks_->onNotification(notification);
  }
}

void RequestLoggerFilter::onResponse(const jsonrpc::Response& response) {
  // DEBUG TRACE
  std::cout << "\n🟢 [RequestLogger] onResponse() ENTRY" << std::endl;
  std::cout << "   ID: " << requestIdToString(response.id) << std::endl;
  std::cout << "   Status: " << (response.error.has_value() ? "ERROR" : "OK") << std::endl;

  std::ostringstream summary;
  summary << "id=" << requestIdToString(response.id);
  summary << " status=" << (response.error.has_value() ? "error" : "ok");

  json::JsonValue payload = json::to_json(response);
  logJsonRpcMessage("response", MessageDirection::Outgoing, summary.str(),
                    payload);

  std::cout << "✅ [RequestLogger] Response logged, propagating to next handler" << std::endl;

  if (next_callbacks_) {
    next_callbacks_->onResponse(response);
  } else {
    std::cout << "⚠️  [RequestLogger] No next handler registered!" << std::endl;
  }
}

void RequestLoggerFilter::onProtocolError(const Error& error) {
  // DEBUG TRACE
  std::cout << "\n🟢 [RequestLogger] onProtocolError() ENTRY" << std::endl;
  std::cout << "   Error code: " << error.code << std::endl;
  std::cout << "   Error message: " << error.message << std::endl;

  std::ostringstream summary;
  summary << "code=" << error.code << " message=" << error.message;

  json::JsonValue payload = json::to_json(error);
  logJsonRpcMessage("protocol_error", MessageDirection::Internal,
                    summary.str(), payload);

  std::cout << "✅ [RequestLogger] Protocol error logged, propagating to next handler" << std::endl;

  if (next_callbacks_) {
    next_callbacks_->onProtocolError(error);
  } else {
    std::cout << "⚠️  [RequestLogger] No next handler registered!" << std::endl;
  }
}

network::FilterStatus RequestLoggerFilter::onData(Buffer& buffer, bool end_stream) {
  // DEBUG TRACE
  std::cout << "\n🟢 [RequestLogger::onData] ENTRY" << std::endl;
  std::cout << "   Buffer length: " << buffer.length() << std::endl;
  std::cout << "   End stream: " << (end_stream ? "true" : "false") << std::endl;

  if (buffer.length() > 0) {
    std::string content = buffer.toString();

    // Print the actual incoming request with clear formatting
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "→ INCOMING REQUEST:" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    if (config_.max_payload_length > 0 && content.size() > config_.max_payload_length) {
      std::cout << content.substr(0, config_.max_payload_length) << "...(truncated)" << std::endl;
    } else {
      std::cout << content << std::endl;
    }
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << std::endl;

    // Also keep the debug trace for compatibility
    std::cout << "   Buffer content (first 200 chars): "
              << content.substr(0, std::min(size_t(200), content.size())) << std::endl;
  }

  std::cout << "✅ [RequestLogger::onData] Returning Continue" << std::endl;
  return network::FilterStatus::Continue;
}

network::FilterStatus RequestLoggerFilter::onWrite(Buffer& buffer, bool end_stream) {
  // DEBUG TRACE
  std::cout << "\n🟢 [RequestLogger::onWrite] ENTRY" << std::endl;
  std::cout << "   Buffer length: " << buffer.length() << std::endl;
  std::cout << "   End stream: " << (end_stream ? "true" : "false") << std::endl;

  if (buffer.length() > 0) {
    std::string content = buffer.toString();

    // Print the actual outgoing response with clear formatting
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "← OUTGOING RESPONSE:" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    if (config_.max_payload_length > 0 && content.size() > config_.max_payload_length) {
      std::cout << content.substr(0, config_.max_payload_length) << "...(truncated)" << std::endl;
    } else {
      std::cout << content << std::endl;
    }
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" << std::endl;

    // Also keep the debug trace for compatibility
    std::cout << "   Buffer content (first 200 chars): "
              << content.substr(0, std::min(size_t(200), content.size())) << std::endl;
  }

  std::cout << "✅ [RequestLogger::onWrite] Returning Continue" << std::endl;
  return network::FilterStatus::Continue;
}

network::FilterStatus RequestLoggerFilter::onNewConnection() {
  // DEBUG TRACE
  std::cout << "\n🟢 [RequestLogger::onNewConnection] ENTRY" << std::endl;

  logTextLine(formatPrefix("connection", MessageDirection::Internal) +
              " opened");

  std::cout << "✅ [RequestLogger::onNewConnection] Connection logged, returning Continue" << std::endl;
  return network::FilterStatus::Continue;
}

void RequestLoggerFilter::setNextCallbacks(
    JsonRpcProtocolFilter::MessageHandler* callbacks) {
  // DEBUG TRACE
  std::cout << "\n🟢 [RequestLogger::setNextCallbacks] ENTRY" << std::endl;
  std::cout << "   Callbacks ptr: " << callbacks << std::endl;

  next_callbacks_ = callbacks;

  std::cout << "✅ [RequestLogger::setNextCallbacks] Next callbacks set" << std::endl;
}

void RequestLoggerFilter::logJsonRpcMessage(
    const std::string& message_type,
    MessageDirection direction,
    const std::string& summary,
    const json::JsonValue& payload) {
  // DEBUG TRACE
  std::cout << "\n🔹 [RequestLogger::logJsonRpcMessage] ENTRY" << std::endl;
  std::cout << "   Message type: " << message_type << std::endl;
  std::cout << "   Direction: " << directionToString(direction) << std::endl;
  std::cout << "   Summary: " << summary << std::endl;

  // INFO level only logs summary lines
  bool include_payload =
      config_.include_payload && config_.log_level != LogLevel::INFO;

  std::cout << "   Include payload: " << (include_payload ? "true" : "false") << std::endl;

  std::string prefix = formatPrefix(message_type, direction);
  std::ostringstream line_builder;
  line_builder << prefix << ' ' << summary;

  if (include_payload) {
    const bool pretty = config_.log_format == LogFormat::PRETTY;
    std::string rendered =
        truncatePayload(payload.toString(pretty && config_.log_format != LogFormat::JSON));

    const bool payload_truncated =
        config_.max_payload_length != 0 &&
        payload.toString(false).size() > config_.max_payload_length;

    if (config_.log_format == LogFormat::JSON) {
      json::JsonObjectBuilder builder;
      builder.add("level", logLevelToString(config_.log_level));
      builder.add("type", message_type);
      builder.add("direction", directionToString(direction));
      builder.add("summary", summary);
      if (config_.include_timestamps) {
        builder.add("timestamp", makeTimestamp());
      }
      if (payload_truncated) {
        builder.add("payload_truncated", true);
        builder.add("payload", rendered);
      } else {
        builder.add("payload", payload);
      }
      line_builder.str(std::string());  // Replace existing text
      line_builder << builder.build().toString(false);
    } else {
      line_builder << " payload=" << rendered;
    }
  }

  std::cout << "🔹 [RequestLogger::logJsonRpcMessage] Calling logTextLine with formatted message..." << std::endl;
  logTextLine(line_builder.str());
  std::cout << "✅ [RequestLogger::logJsonRpcMessage] Message logged" << std::endl;
}

void RequestLoggerFilter::logTextLine(const std::string& line) {
  // DEBUG TRACE (minimal to avoid spam)
  std::cout << "\n🔹 [RequestLogger::logTextLine] ENTRY" << std::endl;
  std::cout << "   Line length: " << line.length() << std::endl;
  std::cout << "   Output type: " << static_cast<int>(config_.output) << std::endl;

  std::lock_guard<std::mutex> guard(write_mutex_);

  std::ostream* stream = nullptr;
  switch (config_.output) {
    case Output::STDOUT:
      std::cout << "   Using STDOUT" << std::endl;
      stream = &std::cout;
      break;
    case Output::STDERR:
      std::cout << "   Using STDERR" << std::endl;
      stream = &std::cerr;
      break;
    case Output::FILE:
      std::cout << "   Using FILE" << std::endl;
      if (file_stream_) {
        stream = &(*file_stream_);
      } else {
        std::cout << "   File stream not available, falling back to STDERR" << std::endl;
        stream = &std::cerr;
      }
      break;
  }

  if (stream) {
    std::cout << "   Writing to output stream..." << std::endl;
    (*stream) << line << std::endl;
    std::cout << "✅ [RequestLogger::logTextLine] Line written" << std::endl;
  } else {
    std::cout << "❌ [RequestLogger::logTextLine] No output stream available!" << std::endl;
  }
}

std::string RequestLoggerFilter::formatPrefix(
    const std::string& message_type,
    MessageDirection direction) const {
  std::ostringstream oss;
  if (config_.include_timestamps) {
    oss << '[' << makeTimestamp() << "] ";
  }
  oss << '[' << logLevelToString(config_.log_level) << "] ";
  oss << '[' << directionToString(direction) << "] ";
  oss << message_type;
  return oss.str();
}

std::string RequestLoggerFilter::truncatePayload(
    const std::string& payload) const {
  if (config_.max_payload_length == 0 ||
      payload.size() <= config_.max_payload_length) {
    return payload;
  }

  std::string truncated = payload.substr(0, config_.max_payload_length);
  truncated += "...(truncated)";
  return truncated;
}

std::string RequestLoggerFilter::makeTimestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  std::tm tm_snapshot;
#if defined(_WIN32)
  localtime_s(&tm_snapshot, &time_t_now);
#else
  localtime_r(&time_t_now, &tm_snapshot);
#endif

  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm_snapshot);

  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  char result[40];
  std::snprintf(result, sizeof(result), "%s.%03lld",
                buffer, static_cast<long long>(ms.count()));
  return std::string(result);
}

}  // namespace filter
}  // namespace mcp
