/**
 * @file request_logger_filter.h
 * @brief JSON-RPC request/response logging filter for Scenario 2
 *
 * This filter logs JSON-RPC messages flowing through the MCP filter chain.
 * It is transport agnostic and operates purely at the protocol layer,
 * making it suitable for Scenario 2 where the official SDK owns HTTP/SSE.
 */

#pragma once

#include <chrono>
#include <fstream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>

#include "mcp/json/json_serialization.h"
#include "mcp/network/filter.h"
#include "mcp/types.h"
#include "json_rpc_protocol_filter.h"

namespace mcp {
namespace filter {

/**
 * Filter that logs JSON-RPC requests, responses, and notifications.
 */
class RequestLoggerFilter : public network::NetworkFilterBase,
                            public JsonRpcProtocolFilter::MessageHandler {
 public:
  enum class LogLevel {
    INFO,
    DEBUG,
    VERBOSE
  };

  enum class LogFormat {
    COMPACT,  ///< Single-line output without indentation
    PRETTY,   ///< Pretty-printed JSON with indentation
    JSON      ///< Emit structured JSON log record
  };

  enum class Output {
    STDOUT,
    STDERR,
    FILE
  };

  enum class MessageDirection {
    Incoming,
    Outgoing,
    Internal
  };

  struct Config {
    LogLevel log_level = LogLevel::DEBUG;
    LogFormat log_format = LogFormat::PRETTY;
    Output output = Output::STDOUT;
    bool include_timestamps = true;
    bool include_payload = true;
    size_t max_payload_length = 1000;
    std::string output_path = "request_logger.log";
  };

  explicit RequestLoggerFilter(const Config& config);
  ~RequestLoggerFilter() override;

  // JsonRpcProtocolFilter::MessageHandler overrides
  void onRequest(const jsonrpc::Request& request) override;
  void onNotification(const jsonrpc::Notification& notification) override;
  void onResponse(const jsonrpc::Response& response) override;
  void onProtocolError(const Error& error) override;

  // network::Filter overrides
  network::FilterStatus onData(Buffer& data, bool end_stream) override;
  network::FilterStatus onWrite(Buffer& data, bool end_stream) override;
  network::FilterStatus onNewConnection() override;

  void setNextCallbacks(JsonRpcProtocolFilter::MessageHandler* callbacks);

 private:
  void logJsonRpcMessage(const std::string& message_type,
                         MessageDirection direction,
                         const std::string& summary,
                         const json::JsonValue& payload);
  void logTextLine(const std::string& line);
  std::string formatPrefix(const std::string& message_type,
                           MessageDirection direction) const;
  std::string truncatePayload(const std::string& payload) const;
  std::string makeTimestamp() const;

  Config config_;
  JsonRpcProtocolFilter::MessageHandler* next_callbacks_{nullptr};
  mutable std::mutex write_mutex_;
  std::optional<std::ofstream> file_stream_;
};

}  // namespace filter
}  // namespace mcp
