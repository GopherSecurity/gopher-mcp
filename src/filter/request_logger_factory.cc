/**
 * @file request_logger_factory.cc
 * @brief Factory and registration for request logger filter
 */

#include "mcp/filter/request_logger_filter.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "mcp/filter/filter_context.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/json/json_bridge.h"

#define GOPHER_LOG_COMPONENT "filter.factory.request_logger"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace filter {

namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

RequestLoggerFilter::LogLevel parseLogLevel(const std::string& level_str) {
  const std::string value = toLower(level_str);
  if (value == "info") {
    return RequestLoggerFilter::LogLevel::INFO;
  }
  if (value == "verbose") {
    return RequestLoggerFilter::LogLevel::VERBOSE;
  }
  return RequestLoggerFilter::LogLevel::DEBUG;
}

RequestLoggerFilter::LogFormat parseLogFormat(const std::string& format_str) {
  const std::string value = toLower(format_str);
  if (value == "compact") {
    return RequestLoggerFilter::LogFormat::COMPACT;
  }
  if (value == "json") {
    return RequestLoggerFilter::LogFormat::JSON;
  }
  return RequestLoggerFilter::LogFormat::PRETTY;
}

RequestLoggerFilter::Output parseOutput(const std::string& output_str) {
  const std::string value = toLower(output_str);
  if (value == "stderr") {
    return RequestLoggerFilter::Output::STDERR;
  }
  if (value == "file") {
    return RequestLoggerFilter::Output::FILE;
  }
  return RequestLoggerFilter::Output::STDOUT;
}

}  // namespace

network::FilterSharedPtr createRequestLoggerFilter(
    const FilterCreationContext&,
    const json::JsonValue& config) {
  RequestLoggerFilter::Config filter_config;

  if (config.isObject()) {
    if (config.contains("log_level") && config["log_level"].isString()) {
      filter_config.log_level =
          parseLogLevel(config["log_level"].getString());
    }

    if (config.contains("log_format") && config["log_format"].isString()) {
      filter_config.log_format =
          parseLogFormat(config["log_format"].getString());
    }

    if (config.contains("include_timestamps")) {
      filter_config.include_timestamps =
          config["include_timestamps"].getBool(filter_config.include_timestamps);
    }

    if (config.contains("include_payload")) {
      filter_config.include_payload =
          config["include_payload"].getBool(filter_config.include_payload);
    }

    if (config.contains("max_payload_length") &&
        config["max_payload_length"].isNumber()) {
      auto length = config["max_payload_length"].getInt64(
          static_cast<int64_t>(filter_config.max_payload_length));
      if (length >= 0) {
        filter_config.max_payload_length =
            static_cast<size_t>(length);
      }
    }

    if (config.contains("output") && config["output"].isString()) {
      filter_config.output = parseOutput(config["output"].getString());
    }

    if (config.contains("output_path") && config["output_path"].isString()) {
      filter_config.output_path = config["output_path"].getString();
    }
  }

  auto filter = std::make_shared<RequestLoggerFilter>(filter_config);
  GOPHER_LOG(Info, "Created request_logger filter (format={}, output={})",
             static_cast<int>(filter_config.log_format),
             static_cast<int>(filter_config.output));
  return filter;
}

void registerRequestLoggerFactory() {
  auto& registry = FilterRegistry::instance();

  BasicFilterMetadata metadata;
  metadata.name = "request_logger";
  metadata.version = "1.0.0";
  metadata.description =
      "Logs JSON-RPC requests/responses for debugging and observability";

  json::JsonObjectBuilder defaults;
  defaults.add("log_level", "debug")
      .add("log_format", "pretty")
      .add("include_timestamps", true)
      .add("include_payload", true)
      .add("max_payload_length", static_cast<int>(1000))
      .add("output", "stdout");
  metadata.default_config = defaults.build();

  registry.registerContextFactory("request_logger", createRequestLoggerFilter,
                                  metadata);
}

namespace {
struct RequestLoggerRegistrar {
  RequestLoggerRegistrar() { registerRequestLoggerFactory(); }
};

static RequestLoggerRegistrar request_logger_registrar;
}  // namespace

extern "C" {
void* request_logger_registrar_ref = reinterpret_cast<void*>(0xDEADCAFE);
}

}  // namespace filter
}  // namespace mcp
