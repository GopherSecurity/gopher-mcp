/**
 * @file mcp_logging_api.cc
 * @brief Implementation of MCP Logging C API with RAII enforcement
 */

#include "mcp/c_api/mcp_logging_api.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/c_api/mcp_raii.h"
#include "mcp/logging/logger.h"
#include "mcp/logging/logger_registry.h"
#include "mcp/logging/log_sink.h"
#include "mcp/logging/log_formatter.h"
#include "mcp/logging/log_macros.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace mcp {
namespace c_api {
namespace {

// Thread-safe handle management following mcp_raii.h patterns
class HandleManager {
public:
  static HandleManager& instance() {
    static HandleManager manager;
    return manager;
  }

  template<typename T>
  uint64_t allocate(std::unique_ptr<T> resource) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto handle = next_handle_.fetch_add(1, std::memory_order_relaxed);
    
    // Create a type-erased deleter
    auto deleter = [](void* p) { delete static_cast<T*>(p); };
    resources_[handle] = std::unique_ptr<void, void(*)(void*)>(
      resource.release(), deleter);
    
    return handle;
  }

  template<typename T>
  T* get(uint64_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(handle);
    if (it == resources_.end()) {
      return nullptr;
    }
    return static_cast<T*>(it->second.get());
  }

  bool release(uint64_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    return resources_.erase(handle) > 0;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    resources_.clear();
  }

private:
  HandleManager() : next_handle_(1) {} // 0 is invalid handle
  
  std::mutex mutex_;
  std::atomic<uint64_t> next_handle_;
  std::unordered_map<uint64_t, std::unique_ptr<void, void(*)(void*)>> resources_;
};

// RAII wrapper for logger handle
struct LoggerHandle {
  std::shared_ptr<logging::Logger> logger;
  std::string name;
  
  LoggerHandle(const std::string& n) 
    : name(n), logger(logging::LoggerRegistry::getLogger(n)) {}
};

// RAII wrapper for sink handle
struct SinkHandle {
  std::shared_ptr<logging::LogSink> sink;
  
  explicit SinkHandle(std::shared_ptr<logging::LogSink> s) : sink(std::move(s)) {}
};

// RAII wrapper for context handle
struct ContextHandle {
  logging::LogContext context;
  
  ContextHandle() = default;
  explicit ContextHandle(logging::LogContext ctx) : context(std::move(ctx)) {}
};

// Convert C string view to std::string
std::string to_string(mcp_string_view_t str) {
  if (str.data == nullptr || str.length == 0) {
    return "";
  }
  return std::string(str.data, str.length);
}

// Convert std::string to C string view (caller owns memory)
mcp_string_view_t to_string_view(const std::string& str) {
  mcp_string_view_t view;
  view.length = str.length();
  if (view.length > 0) {
    view.data = static_cast<char*>(std::malloc(view.length + 1));
    if (view.data) {
      std::memcpy(const_cast<char*>(view.data), str.c_str(), view.length + 1);
    }
  } else {
    view.data = nullptr;
  }
  return view;
}

// Convert C log level to C++ log level
logging::LogLevel to_cpp_level(mcp_log_level_t level) {
  return static_cast<logging::LogLevel>(level);
}

// Convert C++ log level to C log level
mcp_log_level_t to_c_level(logging::LogLevel level) {
  return static_cast<mcp_log_level_t>(level);
}

// Convert C log mode to C++ log mode
logging::LogMode to_cpp_mode(mcp_log_mode_t mode) {
  return static_cast<logging::LogMode>(mode);
}

} // namespace

// Logger API Implementation
extern "C" {

mcp_logger_handle_t mcp_logger_create(mcp_string_view_t name) {
  try {
    auto logger_handle = std::make_unique<LoggerHandle>(to_string(name));
    return HandleManager::instance().allocate(std::move(logger_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

mcp_logger_handle_t mcp_logger_get_or_create(mcp_string_view_t name) {
  return mcp_logger_create(name); // LoggerRegistry::getLogger already does get-or-create
}

mcp_logger_handle_t mcp_logger_get_default() {
  static mcp_logger_handle_t default_handle = []() {
    mcp_string_view_t name = {"default", 7};
    return mcp_logger_create(name);
  }();
  return default_handle;
}

void mcp_logger_destroy(mcp_logger_handle_t handle) {
  if (handle != MCP_INVALID_HANDLE) {
    HandleManager::instance().release(handle);
  }
}

mcp_log_result_t mcp_logger_log(mcp_logger_handle_t handle,
                                mcp_log_level_t level,
                                mcp_string_view_t message) {
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle || !logger_handle->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logger_handle->logger->log(to_cpp_level(level), to_string(message));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_logger_log_structured(mcp_logger_handle_t handle,
                                           const mcp_log_message_t* message) {
  if (!message) {
    return MCP_LOG_ERROR_NULL_POINTER;
  }
  
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle || !logger_handle->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logging::LogMessage log_msg;
    log_msg.level = to_cpp_level(message->level);
    log_msg.message = to_string(message->message);
    log_msg.component = to_string(message->component);
    log_msg.correlation_id = to_string(message->correlation_id);
    log_msg.request_id = to_string(message->request_id);
    log_msg.trace_id = to_string(message->trace_id);
    log_msg.span_id = to_string(message->span_id);
    log_msg.mcp_method = to_string(message->mcp_method);
    log_msg.mcp_resource = to_string(message->mcp_resource);
    log_msg.mcp_tool = to_string(message->mcp_tool);
    
    // Convert key-value pairs
    for (size_t i = 0; i < message->key_value_count; ++i) {
      log_msg.key_values[to_string(message->key_value_pairs[i].key)] = 
        to_string(message->key_value_pairs[i].value);
    }
    
    // Logger will add timestamp and thread_id automatically
    logger_handle->logger->log(log_msg);
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_logger_set_level(mcp_logger_handle_t handle,
                                  mcp_log_level_t level) {
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle || !logger_handle->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logger_handle->logger->setLevel(to_cpp_level(level));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_level_t mcp_logger_get_level(mcp_logger_handle_t handle) {
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle || !logger_handle->logger) {
    return MCP_LOG_LEVEL_OFF;
  }
  
  return to_c_level(logger_handle->logger->getLevel());
}

mcp_bool_t mcp_logger_should_log(mcp_logger_handle_t handle,
                                 mcp_log_level_t level) {
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle || !logger_handle->logger) {
    return MCP_FALSE;
  }
  
  return logger_handle->logger->shouldLog(to_cpp_level(level)) ? MCP_TRUE : MCP_FALSE;
}

mcp_log_result_t mcp_logger_add_sink(mcp_logger_handle_t logger_handle,
                                 mcp_sink_handle_t sink_handle) {
  auto* logger = HandleManager::instance().get<LoggerHandle>(logger_handle);
  auto* sink = HandleManager::instance().get<SinkHandle>(sink_handle);
  
  if (!logger || !logger->logger || !sink || !sink->sink) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logger->logger->addSink(sink->sink);
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_logger_remove_sink(mcp_logger_handle_t logger_handle,
                                    mcp_sink_handle_t sink_handle) {
  auto* logger = HandleManager::instance().get<LoggerHandle>(logger_handle);
  auto* sink = HandleManager::instance().get<SinkHandle>(sink_handle);
  
  if (!logger || !logger->logger || !sink || !sink->sink) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logger->logger->removeSink(sink->sink);
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_logger_flush(mcp_logger_handle_t handle) {
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle || !logger_handle->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logger_handle->logger->flush();
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

// Sink API Implementation
mcp_sink_handle_t mcp_sink_create_file(mcp_string_view_t filename) {
  try {
    auto sink = std::make_shared<logging::FileSink>(to_string(filename));
    auto sink_handle = std::make_unique<SinkHandle>(std::move(sink));
    return HandleManager::instance().allocate(std::move(sink_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

mcp_sink_handle_t mcp_sink_create_rotating_file(mcp_string_view_t filename,
                                                size_t max_size,
                                                size_t max_files) {
  try {
    auto sink = std::make_shared<logging::RotatingFileSink>(
      to_string(filename), max_size, max_files);
    auto sink_handle = std::make_unique<SinkHandle>(std::move(sink));
    return HandleManager::instance().allocate(std::move(sink_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

mcp_sink_handle_t mcp_sink_create_stdio(mcp_bool_t use_stderr) {
  try {
    auto sink = std::make_shared<logging::StdioSink>(
      use_stderr == MCP_TRUE ? logging::StdioSink::Type::Stderr 
                              : logging::StdioSink::Type::Stdout);
    auto sink_handle = std::make_unique<SinkHandle>(std::move(sink));
    return HandleManager::instance().allocate(std::move(sink_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

mcp_sink_handle_t mcp_sink_create_null() {
  try {
    auto sink = std::make_shared<logging::NullSink>();
    auto sink_handle = std::make_unique<SinkHandle>(std::move(sink));
    return HandleManager::instance().allocate(std::move(sink_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

// External sink callback wrapper
class ExternalSinkWrapper : public logging::LogSink {
public:
  ExternalSinkWrapper(mcp_sink_write_callback_t write_cb,
                     mcp_sink_flush_callback_t flush_cb,
                     void* user_data)
    : write_callback_(write_cb),
      flush_callback_(flush_cb),
      user_data_(user_data) {}
  
  void write(const logging::LogMessage& message) override {
    if (!write_callback_) return;
    
    // Convert LogMessage to C struct
    mcp_log_message_t c_msg = {};
    c_msg.level = to_c_level(message.level);
    
    // Note: These string views point to temporary strings, 
    // callback must copy if needed
    auto msg_str = message.message;
    c_msg.message = {msg_str.c_str(), msg_str.length()};
    
    auto comp_str = message.component;
    c_msg.component = {comp_str.c_str(), comp_str.length()};
    
    c_msg.timestamp_ms = message.timestamp_ms;
    c_msg.thread_id = message.thread_id;
    
    write_callback_(&c_msg, user_data_);
  }
  
  void flush() override {
    if (flush_callback_) {
      flush_callback_(user_data_);
    }
  }
  
private:
  mcp_sink_write_callback_t write_callback_;
  mcp_sink_flush_callback_t flush_callback_;
  void* user_data_;
};

mcp_sink_handle_t mcp_sink_create_external(mcp_sink_write_callback_t write_callback,
                                           mcp_sink_flush_callback_t flush_callback,
                                           void* user_data) {
  if (!write_callback) {
    return MCP_INVALID_HANDLE;
  }
  
  try {
    auto sink = std::make_shared<ExternalSinkWrapper>(
      write_callback, flush_callback, user_data);
    auto sink_handle = std::make_unique<SinkHandle>(std::move(sink));
    return HandleManager::instance().allocate(std::move(sink_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

void mcp_sink_destroy(mcp_sink_handle_t handle) {
  if (handle != MCP_INVALID_HANDLE) {
    HandleManager::instance().release(handle);
  }
}

mcp_log_result_t mcp_sink_set_formatter(mcp_sink_handle_t handle,
                                    mcp_formatter_type_t type) {
  auto* sink_handle = HandleManager::instance().get<SinkHandle>(handle);
  if (!sink_handle || !sink_handle->sink) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    std::shared_ptr<logging::LogFormatter> formatter;
    switch (type) {
      case MCP_FORMATTER_DEFAULT:
        formatter = std::make_shared<logging::DefaultFormatter>();
        break;
      case MCP_FORMATTER_JSON:
        formatter = std::make_shared<logging::JsonFormatter>();
        break;
      default:
        return MCP_LOG_ERROR_NULL_POINTER;
    }
    
    sink_handle->sink->setFormatter(formatter);
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_sink_set_level_filter(mcp_sink_handle_t handle,
                                       mcp_log_level_t min_level) {
  auto* sink_handle = HandleManager::instance().get<SinkHandle>(handle);
  if (!sink_handle || !sink_handle->sink) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    sink_handle->sink->setMinLevel(to_cpp_level(min_level));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

// Context API Implementation
mcp_log_context_handle_t mcp_context_create() {
  try {
    auto context_handle = std::make_unique<ContextHandle>();
    return HandleManager::instance().allocate(std::move(context_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

mcp_log_context_handle_t mcp_context_create_with_data(mcp_string_view_t correlation_id,
                                                  mcp_string_view_t request_id) {
  try {
    logging::LogContext context;
    context.correlation_id = to_string(correlation_id);
    context.request_id = to_string(request_id);
    
    auto context_handle = std::make_unique<ContextHandle>(std::move(context));
    return HandleManager::instance().allocate(std::move(context_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

void mcp_context_destroy(mcp_log_context_handle_t handle) {
  if (handle != MCP_INVALID_HANDLE) {
    HandleManager::instance().release(handle);
  }
}

mcp_log_result_t mcp_context_set_correlation_id(mcp_log_context_handle_t handle,
                                            mcp_string_view_t correlation_id) {
  auto* context_handle = HandleManager::instance().get<ContextHandle>(handle);
  if (!context_handle) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  context_handle->context.correlation_id = to_string(correlation_id);
  return MCP_LOG_OK;
}

mcp_log_result_t mcp_context_set_request_id(mcp_log_context_handle_t handle,
                                        mcp_string_view_t request_id) {
  auto* context_handle = HandleManager::instance().get<ContextHandle>(handle);
  if (!context_handle) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  context_handle->context.request_id = to_string(request_id);
  return MCP_LOG_OK;
}

mcp_log_result_t mcp_context_add_latency(mcp_log_context_handle_t handle,
                                     double latency_ms) {
  auto* context_handle = HandleManager::instance().get<ContextHandle>(handle);
  if (!context_handle) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  context_handle->context.addLatency(
    std::chrono::nanoseconds(static_cast<int64_t>(latency_ms * 1000000)));
  return MCP_LOG_OK;
}

mcp_log_result_t mcp_context_propagate(mcp_log_context_handle_t from_handle,
                                   mcp_log_context_handle_t to_handle) {
  auto* from_context = HandleManager::instance().get<ContextHandle>(from_handle);
  auto* to_context = HandleManager::instance().get<ContextHandle>(to_handle);
  
  if (!from_context || !to_context) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  from_context->context.propagateTo(to_context->context);
  return MCP_LOG_OK;
}

// Registry API Implementation
mcp_log_result_t mcp_registry_set_default_level(mcp_log_level_t level) {
  try {
    logging::LoggerRegistry::setDefaultLevel(to_cpp_level(level));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_registry_set_default_mode(mcp_log_mode_t mode) {
  try {
    logging::LoggerRegistry::setDefaultMode(to_cpp_mode(mode));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_registry_set_pattern_level(mcp_string_view_t pattern,
                                            mcp_log_level_t level) {
  try {
    logging::LoggerRegistry::setLoggerLevel(to_string(pattern), to_cpp_level(level));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_registry_flush_all() {
  try {
    logging::LoggerRegistry::flushAll();
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

void mcp_registry_shutdown() {
  try {
    logging::LoggerRegistry::shutdown();
    HandleManager::instance().clear();
  } catch (...) {
    // Ignore exceptions during shutdown
  }
}

// Statistics API Implementation
void mcp_logger_get_stats(mcp_logger_handle_t handle,
                          mcp_logger_stats_t* stats) {
  if (!stats) return;
  
  std::memset(stats, 0, sizeof(*stats));
  
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle || !logger_handle->logger) {
    return;
  }
  
  auto logger_stats = logger_handle->logger->getStats();
  stats->messages_logged = logger_stats.messages_logged;
  stats->messages_dropped = logger_stats.messages_dropped;
  stats->bytes_written = logger_stats.bytes_written;
  stats->flush_count = logger_stats.flush_count;
  stats->error_count = logger_stats.error_count;
}

// Utility functions
void mcp_free_string_view(mcp_string_view_t* str) {
  if (str && str->data) {
    std::free(const_cast<char*>(str->data));
    str->data = nullptr;
    str->length = 0;
  }
}

const char* mcp_log_level_to_string(mcp_log_level_t level) {
  switch (level) {
    case MCP_LOG_LEVEL_DEBUG: return "DEBUG";
    case MCP_LOG_LEVEL_INFO: return "INFO";
    case MCP_LOG_LEVEL_NOTICE: return "NOTICE";
    case MCP_LOG_LEVEL_WARNING: return "WARNING";
    case MCP_LOG_LEVEL_ERROR: return "ERROR";
    case MCP_LOG_LEVEL_CRITICAL: return "CRITICAL";
    case MCP_LOG_LEVEL_ALERT: return "ALERT";
    case MCP_LOG_LEVEL_EMERGENCY: return "EMERGENCY";
    case MCP_LOG_LEVEL_OFF: return "OFF";
    default: return "UNKNOWN";
  }
}

mcp_log_level_t mcp_log_level_from_string(mcp_string_view_t str) {
  std::string level_str = to_string(str);
  
  // Convert to uppercase for comparison
  for (auto& c : level_str) {
    c = std::toupper(c);
  }
  
  if (level_str == "DEBUG") return MCP_LOG_LEVEL_DEBUG;
  if (level_str == "INFO") return MCP_LOG_LEVEL_INFO;
  if (level_str == "NOTICE") return MCP_LOG_LEVEL_NOTICE;
  if (level_str == "WARNING" || level_str == "WARN") return MCP_LOG_LEVEL_WARNING;
  if (level_str == "ERROR") return MCP_LOG_LEVEL_ERROR;
  if (level_str == "CRITICAL" || level_str == "CRIT") return MCP_LOG_LEVEL_CRITICAL;
  if (level_str == "ALERT") return MCP_LOG_LEVEL_ALERT;
  if (level_str == "EMERGENCY" || level_str == "EMERG") return MCP_LOG_LEVEL_EMERGENCY;
  if (level_str == "OFF") return MCP_LOG_LEVEL_OFF;
  
  return MCP_LOG_LEVEL_INFO; // Default
}

} // extern "C"

} // namespace c_api
} // namespace mcp