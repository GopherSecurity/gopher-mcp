/**
 * @file mcp_logging_api.cc
 * @brief Implementation of MCP Logging C API with RAII enforcement
 */

#include "mcp/c_api/mcp_c_logging_api.h"
#include "mcp/c_api/mcp_c_types.h"
#include "mcp/c_api/mcp_c_memory.h"
#include "mcp/c_api/mcp_c_raii.h"
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
    resources_.emplace(handle, std::unique_ptr<void, void(*)(void*)>(
      resource.release(), deleter));
    
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
  
  LoggerHandle(const std::string& n) : name(n) {
    // Avoid calling LoggerRegistry during static initialization
    // Create logger lazily when first used instead
    logger = nullptr;
  }
  
  void ensureLogger() {
    if (!logger) {
      logger = logging::LoggerRegistry::instance().getOrCreateLogger(name);
    }
  }
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

// Internal helper to create logger
static mcp_logger_handle_t create_logger_internal(mcp_string_view_t name) {
  try {
    auto logger_handle = std::make_unique<LoggerHandle>(to_string(name));
    return HandleManager::instance().allocate(std::move(logger_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

mcp_log_result_t mcp_logger_get_or_create(mcp_string_view_t name,
                                          mcp_logger_handle_t* handle) {
  if (!handle) {
    return MCP_LOG_ERROR_NULL_POINTER;
  }
  *handle = create_logger_internal(name);
  return (*handle != MCP_INVALID_HANDLE) ? MCP_LOG_OK : MCP_LOG_ERROR_OUT_OF_MEMORY;
}

mcp_log_result_t mcp_logger_get_default(mcp_logger_handle_t* handle) {
  if (!handle) {
    return MCP_LOG_ERROR_NULL_POINTER;
  }
  static mcp_logger_handle_t default_handle = []() {
    mcp_string_view_t name = {"default", 7};
    return create_logger_internal(name);
  }();
  *handle = default_handle;
  return MCP_LOG_OK;
}

mcp_log_result_t mcp_logger_release(mcp_logger_handle_t handle) {
  if (handle == MCP_INVALID_HANDLE) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  return HandleManager::instance().release(handle) ? MCP_LOG_OK : MCP_LOG_ERROR_NOT_FOUND;
}

mcp_log_result_t mcp_logger_log(mcp_logger_handle_t handle,
                                mcp_log_level_t level,
                                mcp_string_view_t message) {
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  logger_handle->ensureLogger();
  if (!logger_handle->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    // Use the log method with file/line/function parameters
    logger_handle->logger->log(to_cpp_level(level), 
                               __FILE__, __LINE__, __FUNCTION__,
                               "%s", to_string(message).c_str());
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
  if (!logger_handle) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  logger_handle->ensureLogger();
  if (!logger_handle->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logging::LogMessage log_msg;
    log_msg.level = to_cpp_level(message->level);
    log_msg.message = to_string(message->message);
    log_msg.component = static_cast<logging::Component>(message->component);
    log_msg.connection_id = to_string(message->request_id);  // Map request_id to connection_id
    log_msg.request_id = to_string(message->request_id);
    log_msg.trace_id = to_string(message->trace_id);
    log_msg.span_id = to_string(message->span_id);
    // MCP fields are in the C++ structure but not in C API structure
    
    // Key-value pairs not in C API structure
    
    // Use the formatted log method instead
    logger_handle->logger->log(log_msg.level, __FILE__, __LINE__, __FUNCTION__,
                               "%s", log_msg.message.c_str());
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_logger_set_level(mcp_logger_handle_t handle,
                                  mcp_log_level_t level) {
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  logger_handle->ensureLogger();
  if (!logger_handle->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logger_handle->logger->setLevel(to_cpp_level(level));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_logger_get_level(mcp_logger_handle_t handle,
                                      mcp_log_level_t* level) {
  if (!level) {
    return MCP_LOG_ERROR_NULL_POINTER;
  }
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  logger_handle->ensureLogger();
  if (!logger_handle->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  *level = to_c_level(logger_handle->logger->getLevel());
  return MCP_LOG_OK;
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
  
  if (!logger || !sink || !sink->sink) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  logger->ensureLogger();
  if (!logger->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logger->logger->setSink(sink->sink);
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

// Alias for compatibility with header declaration
mcp_log_result_t mcp_logger_set_sink(mcp_logger_handle_t logger_handle,
                                     mcp_sink_handle_t sink_handle) {
  return mcp_logger_add_sink(logger_handle, sink_handle);
}

mcp_log_result_t mcp_logger_remove_sink(mcp_logger_handle_t logger_handle,
                                    mcp_sink_handle_t sink_handle) {
  auto* logger = HandleManager::instance().get<LoggerHandle>(logger_handle);
  auto* sink = HandleManager::instance().get<SinkHandle>(sink_handle);
  
  if (!logger || !sink || !sink->sink) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  logger->ensureLogger();
  if (!logger->logger) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    logger->logger->setSink(nullptr);
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_logger_flush(mcp_logger_handle_t handle) {
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  logger_handle->ensureLogger();
  if (!logger_handle->logger) {
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
mcp_log_result_t mcp_sink_create_file(mcp_string_view_t filename,
                                      size_t max_file_size,
                                      size_t max_files,
                                      mcp_sink_handle_t* handle) {
  if (!handle) {
    return MCP_LOG_ERROR_NULL_POINTER;
  }
  try {
    // Use RotatingFileSink with no rotation as a simple file sink
    logging::RotatingFileSink::Config config;
    config.base_filename = to_string(filename);
    config.max_file_size = max_file_size == 0 ? SIZE_MAX : max_file_size;
    config.max_files = max_files == 0 ? 1 : max_files;
    auto sink = std::make_shared<logging::RotatingFileSink>(config);
    auto sink_handle = std::make_unique<SinkHandle>(std::move(sink));
    *handle = HandleManager::instance().allocate(std::move(sink_handle));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_OUT_OF_MEMORY;
  }
}

static mcp_sink_handle_t create_rotating_file_internal(mcp_string_view_t filename,
                                                       size_t max_size,
                                                       size_t max_files) {
  try {
    logging::RotatingFileSink::Config config;
    config.base_filename = to_string(filename);
    config.max_file_size = max_size;
    config.max_files = max_files;
    auto sink = std::make_shared<logging::RotatingFileSink>(config);
    auto sink_handle = std::make_unique<SinkHandle>(std::move(sink));
    return HandleManager::instance().allocate(std::move(sink_handle));
  } catch (...) {
    return MCP_INVALID_HANDLE;
  }
}

mcp_log_result_t mcp_sink_create_stdio(int use_stderr,
                                       mcp_sink_handle_t* handle) {
  if (!handle) {
    return MCP_LOG_ERROR_NULL_POINTER;
  }
  try {
    auto sink = std::make_shared<logging::StdioSink>(
      use_stderr ? logging::StdioSink::Stderr 
                 : logging::StdioSink::Stdout);
    auto sink_handle = std::make_unique<SinkHandle>(std::move(sink));
    *handle = HandleManager::instance().allocate(std::move(sink_handle));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_OUT_OF_MEMORY;
  }
}

static mcp_sink_handle_t create_null_sink_internal() {
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
  ExternalSinkWrapper(mcp_log_sink_callback_t write_cb,
                     mcp_log_filter_callback_t flush_cb,
                     void* user_data)
    : write_callback_(write_cb),
      flush_callback_(flush_cb),
      user_data_(user_data) {}
  
  void log(const logging::LogMessage& message) override {
    if (!write_callback_) return;
    
    // Convert LogMessage to C struct
    mcp_log_message_t c_msg = {};
    c_msg.level = to_c_level(message.level);
    
    // Note: These string views point to temporary strings, 
    // callback must copy if needed
    auto msg_str = message.message;
    c_msg.message = {msg_str.c_str(), msg_str.length()};
    
    // Component is an enum, set it directly
    c_msg.component = static_cast<mcp_log_component_t>(message.component);
    
    // Set source location
    c_msg.file = message.file;
    c_msg.line = message.line;
    c_msg.function = message.function;
    
    // Format and call the callback with the correct signature
    auto formatted = formatter_->format(message);
    write_callback_(to_c_level(message.level), 
                   message.logger_name.c_str(),
                   formatted.c_str(), 
                   user_data_);
  }
  
  void flush() override {
    // flush_callback is actually a filter callback, not used for flush
  }
  
  logging::SinkType type() const override { return logging::SinkType::External; }
  
private:
  mcp_log_sink_callback_t write_callback_;
  mcp_log_filter_callback_t flush_callback_;
  void* user_data_;
};

mcp_log_result_t mcp_sink_create_external(mcp_log_sink_callback_t callback,
                                          void* user_data,
                                          mcp_sink_handle_t* handle) {
  if (!handle) {
    return MCP_LOG_ERROR_NULL_POINTER;
  }
  if (!callback) {
    return MCP_LOG_ERROR_NULL_POINTER;
  }
  
  try {
    auto sink = std::make_shared<ExternalSinkWrapper>(
      callback, nullptr, user_data);
    auto sink_handle = std::make_unique<SinkHandle>(std::move(sink));
    *handle = HandleManager::instance().allocate(std::move(sink_handle));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_OUT_OF_MEMORY;
  }
}

void mcp_sink_destroy(mcp_sink_handle_t handle) {
  if (handle != MCP_INVALID_HANDLE) {
    HandleManager::instance().release(handle);
  }
}

// Alias for compatibility with header declaration  
mcp_log_result_t mcp_sink_release(mcp_sink_handle_t handle) {
  if (handle != MCP_INVALID_HANDLE) {
    HandleManager::instance().release(handle);
    return MCP_LOG_OK;
  }
  return MCP_LOG_ERROR_INVALID_HANDLE;
}

mcp_log_result_t mcp_sink_set_formatter(mcp_sink_handle_t handle,
                                    mcp_formatter_type_t type) {
  auto* sink_handle = HandleManager::instance().get<SinkHandle>(handle);
  if (!sink_handle || !sink_handle->sink) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  try {
    std::unique_ptr<logging::Formatter> formatter;
    switch (type) {
      case MCP_FORMATTER_DEFAULT:
        formatter = std::make_unique<logging::DefaultFormatter>();
        break;
      case MCP_FORMATTER_JSON:
        formatter = std::make_unique<logging::JsonFormatter>();
        break;
      default:
        return MCP_LOG_ERROR_NULL_POINTER;
    }
    
    sink_handle->sink->setFormatter(std::move(formatter));
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
    // LogSink doesn't have setMinLevel, filtering is done at logger level
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

// Context API Implementation
mcp_log_result_t mcp_context_create(mcp_log_context_handle_t* handle) {
  if (!handle) {
    return MCP_LOG_ERROR_NULL_POINTER;
  }
  try {
    auto context_handle = std::make_unique<ContextHandle>();
    *handle = HandleManager::instance().allocate(std::move(context_handle));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_OUT_OF_MEMORY;
  }
}

mcp_log_context_handle_t mcp_context_create_with_data(mcp_string_view_t correlation_id,
                                                  mcp_string_view_t request_id) {
  try {
    logging::LogContext context;
    context.connection_id = to_string(correlation_id);  // Map correlation_id to connection_id
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
  
  context_handle->context.connection_id = to_string(correlation_id);  // Map correlation_id to connection_id
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
  
  context_handle->context.accumulated_latency += 
    std::chrono::nanoseconds(static_cast<int64_t>(latency_ms * 1000000));
  return MCP_LOG_OK;
}

mcp_log_result_t mcp_context_propagate(mcp_log_context_handle_t from_handle,
                                   mcp_log_context_handle_t to_handle) {
  auto* from_context = HandleManager::instance().get<ContextHandle>(from_handle);
  auto* to_context = HandleManager::instance().get<ContextHandle>(to_handle);
  
  if (!from_context || !to_context) {
    return MCP_LOG_ERROR_INVALID_HANDLE;
  }
  
  to_context->context.merge(from_context->context);
  return MCP_LOG_OK;
}

// Registry API Implementation
mcp_log_result_t mcp_registry_set_default_level(mcp_log_level_t level) {
  try {
    logging::LoggerRegistry::instance().setGlobalLevel(to_cpp_level(level));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_registry_set_default_mode(mcp_log_mode_t mode) {
  try {
    // LoggerRegistry doesn't have setDefaultMode
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_registry_set_pattern_level(mcp_string_view_t pattern,
                                            mcp_log_level_t level) {
  try {
    logging::LoggerRegistry::instance().setPattern(to_string(pattern), to_cpp_level(level));
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

mcp_log_result_t mcp_registry_flush_all() {
  try {
    // LoggerRegistry doesn't have flushAll, need to flush individual loggers
    return MCP_LOG_OK;
  } catch (...) {
    return MCP_LOG_ERROR_IO_ERROR;
  }
}

void mcp_registry_shutdown() {
  try {
    // LoggerRegistry doesn't have shutdown, cleanup happens in destructor
    HandleManager::instance().clear();
  } catch (...) {
    // Ignore exceptions during shutdown
  }
}

// Statistics API Implementation
void mcp_logger_get_stats(mcp_logger_handle_t handle,
                          mcp_logging_stats_t* stats) {
  if (!stats) return;
  
  std::memset(stats, 0, sizeof(*stats));
  
  auto* logger_handle = HandleManager::instance().get<LoggerHandle>(handle);
  if (!logger_handle || !logger_handle->logger) {
    return;
  }
  
  // Logger doesn't have getStats method
  // Fill with dummy data for now
  stats->messages_logged = 0;
  stats->bytes_written = 0;
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