# Gopher-MCP Logging Framework Design

## Executive Summary

This document presents a comprehensive logging framework design for Gopher-MCP that combines enterprise-grade logging patterns with MCP specification compliance, cross-language support through FFI, and high-performance optimizations for critical data paths.

## Architecture Overview

```
┌────────────────────────────────────────────────────────────────┐
│                    Application Layer                            │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ MCP Server/Client │ User Application │ Language Bindings │  │
│  └──────────────────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────────────────┤
│                 Component Logger Layer                          │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ ServerLogger │ ClientLogger │ FilterLogger │ NetworkLogger│  │
│  └──────────────────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────────────────┤
│                    Logging API Layer                            │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ LoggerRegistry │ LogContext │ LogMacros │ RAII Guards    │  │
│  └──────────────────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────────────────┤
│                    FFI Bridge Layer                             │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ C API │ RAII Wrappers │ External Sink │ Callback Bridge │  │
│  └──────────────────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────────────────┤
│                    Logger Core Layer                            │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Logger │ DelegatingLogSink │ FineGrainLogger │ LogLevel  │  │
│  └──────────────────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────────────────┤
│                   Context Propagation Layer                     │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ StreamInfo │ FilterContext │ DispatcherContext │ TraceID │  │
│  └──────────────────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────────────────┤
│                      Sink Layer                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ FileSink │ StdioSink │ NullSink │ ExternalSink │ MCPSink │  │
│  └──────────────────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────────────────┤
│                   Performance Optimization Layer                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ AsyncLogger │ BloomFilter │ LockFree │ CompileTimeFilter │  │
│  └──────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────┘
```

## Core Design Principles

### 1. Zero-Configuration by Default
The framework works out-of-the-box without any configuration:
- Default stderr sink with sensible formatting
- Info log level by default
- Automatic component detection from context
- Smart defaults for all settings
- Progressive enhancement when configuration is added

### 2. MCP Specification Compliance
The framework strictly adheres to MCP logging levels as defined in RFC-5424:
- **debug**: Detailed information for debugging
- **info**: Informational messages
- **notice**: Normal but significant conditions
- **warning**: Warning conditions
- **error**: Error conditions
- **critical**: Critical conditions
- **alert**: Action must be taken immediately
- **emergency**: System is unusable

### 3. Dispatcher-Aware Design
All logging operations are designed with the dispatcher threading model in mind:
- Thread-local logger instances per dispatcher
- Lock-free logging in critical paths
- Automatic thread ID and dispatcher context capture
- Async sink flushing to avoid blocking I/O

### 4. Zero-Cost Abstractions
Following best practices for performance-critical systems:
- Compile-time log level filtering
- Constexpr evaluation for disabled log statements
- Template-based formatting to avoid runtime overhead
- Optional no-op logger for ultra-high performance paths

### 5. RAII Pattern for Resource Management
All logging resources follow RAII principles:
- Automatic cleanup of loggers and sinks
- Safe FFI boundary with guard objects
- Exception-safe resource management
- Deterministic destruction in dispatcher context

### 6. Component-Level Logging
Hierarchical component-based logging architecture:
- Each layer has dedicated component loggers
- Fine-grain control per component
- Inheritance of log levels from parent components
- Dynamic runtime reconfiguration per component

### 7. Rich Metadata for Analysis
Comprehensive metadata captured for each log entry:
- Component hierarchy and layer information
- Source location (file, line, function)
- Process and thread identifiers
- Connection and request correlation IDs
- Performance metrics (latency, throughput)
- Custom key-value pairs for context

## Component Design

### 1. Component-Based Logger System

```cpp
namespace mcp::logging {

// Component identifiers for hierarchical logging
enum class Component {
  Root,
  Server,
  Client,
  Network,
  Filter,
  Transport,
  Protocol,
  Event,
  // Add more components as needed
};

// Component logger with automatic hierarchy
class ComponentLogger {
public:
  ComponentLogger(Component component, const std::string& name)
    : component_(component), name_(name) {
    logger_ = LoggerRegistry::instance().getOrCreateLogger(
      getComponentPath(component, name));
  }
  
  // Component-aware logging
  template<typename... Args>
  void log(LogLevel level, const char* fmt, Args&&... args) {
    if (shouldLog(level)) {
      logger_->logWithComponent(level, component_, fmt, 
                               std::forward<Args>(args)...);
    }
  }
  
  // Set component-specific level
  void setLevel(LogLevel level) {
    logger_->setLevel(level);
    LoggerRegistry::instance().setComponentLevel(
      getComponentPath(component_, name_), level);
  }
  
private:
  Component component_;
  std::string name_;
  LoggerPtr logger_;
  
  static std::string getComponentPath(Component comp, const std::string& name);
};

class LoggerRegistry {
public:
  // Singleton instance with zero-configuration defaults
  static LoggerRegistry& instance() {
    static LoggerRegistry registry;
    return registry;
  }
  
  // Register component logger
  void registerComponentLogger(Component component, 
                              const std::string& name, 
                              LoggerPtr logger);
  
  // Get or create logger with hierarchy support
  LoggerPtr getOrCreateLogger(const std::string& name);
  
  // Get default logger (zero-configuration)
  LoggerPtr getDefaultLogger() {
    if (!default_logger_) {
      initializeDefaults();
    }
    return default_logger_;
  }
  
  // Set global log level
  void setGlobalLevel(LogLevel level);
  
  // Component-level control
  void setComponentLevel(Component component, LogLevel level);
  void setComponentLevel(const std::string& path, LogLevel level);
  
  // MCP-compliant level management
  void handleSetLevelRequest(const SetLevelRequest& request);
  
  // Bloom filter for quick log filtering
  bool shouldLog(const std::string& logger_name, LogLevel level) const {
    // Quick check with bloom filter first
    if (bloom_filter_.mayContain(logger_name)) {
      return checkActualLevel(logger_name, level);
    }
    return level >= global_level_;
  }
  
private:
  LoggerRegistry() {
    initializeDefaults();
  }
  
  void initializeDefaults() {
    // Zero-configuration defaults
    default_logger_ = std::make_shared<Logger>("default", LogMode::Sync);
    default_logger_->setSink(std::make_unique<StdioSink>(StdioSink::Stderr));
    default_logger_->setLevel(LogLevel::Info);
    
    // Set default formatter with component info
    auto formatter = std::make_unique<DefaultFormatter>();
    default_logger_->setFormatter(std::move(formatter));
  }
  
  // Hierarchical logger map (using std::unordered_map)
  std::unordered_map<std::string, LoggerPtr> loggers_;
  
  // Component-specific loggers
  std::unordered_map<Component, 
                     std::unordered_map<std::string, LoggerPtr>> component_loggers_;
  
  // Thread-local logger cache for dispatcher
  static thread_local std::unordered_map<std::string, LoggerPtr> tls_cache_;
  
  // Bloom filter for performance
  BloomFilter<std::string> bloom_filter_;
  
  // Fine-grain log patterns (glob support)
  std::vector<LogPattern> patterns_;
  
  LogLevel global_level_{LogLevel::Info};
  LoggerPtr default_logger_;
};

}
```

### 2. Logger Core with Sync/Async Support

```cpp
namespace mcp::logging {

// Log levels matching MCP specification
enum class LogLevel : uint8_t {
  Debug = 0,
  Info = 1,
  Notice = 2,
  Warning = 3,
  Error = 4,
  Critical = 5,
  Alert = 6,
  Emergency = 7,
  Off = 8
};

// Logging mode selection
enum class LogMode {
  Sync,    // Direct logging (for debugging)
  Async,   // Buffered async logging (for production)
  NoOp     // No operation (for critical paths)
};

class Logger : public std::enable_shared_from_this<Logger> {
public:
  explicit Logger(const std::string& name, LogMode mode = LogMode::Async)
    : name_(name), mode_(mode) {
    if (mode == LogMode::Async) {
      async_processor_ = std::make_unique<AsyncLogProcessor>();
    }
  }
  
  // Core logging methods with zero-cost when disabled
  template<typename... Args>
  void debug(const char* fmt, Args&&... args) {
    if (shouldLog(LogLevel::Debug)) {
      logImpl(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
  }
  
  // Similar for info, notice, warning, error, critical, alert, emergency
  
  // Context-aware logging
  template<typename... Args>
  void logWithContext(LogLevel level, const LogContext& ctx, 
                     const char* fmt, Args&&... args);
  
  // Component-aware logging
  template<typename... Args>
  void logWithComponent(LogLevel level, Component component,
                       const char* fmt, Args&&... args);
  
  // Set logger-specific configuration
  void setLevel(LogLevel level);
  void setSink(LogSinkPtr sink);
  void setFormatter(FormatterPtr formatter);
  void setMode(LogMode mode);
  
  // Performance optimization with bloom filter hint
  bool shouldLog(LogLevel level) const {
    // Fast path: check bloom filter first
    if (bloom_filter_hint_ && !bloom_filter_hint_->mayContain(name_)) {
      return false;
    }
    return level >= effective_level_.load(std::memory_order_relaxed);
  }
  
protected:
  template<typename... Args>
  void logImpl(LogLevel level, const char* fmt, Args&&... args) {
    switch (mode_) {
      case LogMode::Sync:
        logSync(level, fmt, std::forward<Args>(args)...);
        break;
      case LogMode::Async:
        logAsync(level, fmt, std::forward<Args>(args)...);
        break;
      case LogMode::NoOp:
        // Do nothing - compile-time optimization
        break;
    }
  }
  
private:
  std::atomic<LogLevel> effective_level_{LogLevel::Info};
  LogSinkPtr sink_;
  FormatterPtr formatter_;
  std::string name_;
  LogMode mode_;
  std::unique_ptr<AsyncLogProcessor> async_processor_;
  const BloomFilter<std::string>* bloom_filter_hint_{nullptr};
  
  template<typename... Args>
  void logSync(LogLevel level, const char* fmt, Args&&... args);
  
  template<typename... Args>
  void logAsync(LogLevel level, const char* fmt, Args&&... args);
};

}
```

### 3. Log Context with Rich Metadata

```cpp
namespace mcp::logging {

// Enhanced log message with comprehensive metadata
struct LogMessage {
  LogLevel level;
  std::string message;
  std::chrono::system_clock::time_point timestamp;
  
  // Component information
  Component component{Component::Root};
  std::string component_name;
  std::string layer_name;
  
  // Source location
  const char* file{nullptr};
  int line{0};
  const char* function{nullptr};
  
  // Process and thread info
  pid_t process_id{0};
  std::thread::id thread_id;
  uint32_t dispatcher_id{0};
  
  // Correlation IDs for tracing
  std::string trace_id;
  std::string span_id;
  std::string parent_span_id;
  std::string request_id;
  std::string connection_id;
  std::string stream_id;
  std::string session_id;
  
  // Performance metrics
  std::chrono::nanoseconds latency{0};
  size_t bytes_processed{0};
  size_t messages_processed{0};
  
  // MCP-specific fields
  std::string tool_name;
  std::string resource_uri;
  std::string method_name;
  
  // Custom metadata (using std::map)
  std::map<std::string, std::string> metadata;
  
  // Logger information
  std::string logger_name;
  
  // Formatted message cache
  mutable std::string formatted_message;
};

// Context that flows through filter chain
class LogContext {
public:
  // Core context fields
  std::string trace_id;
  std::string span_id;
  std::string connection_id;
  std::string stream_id;
  std::string request_id;
  
  // Component and layer info
  Component component{Component::Root};
  std::string component_name;
  std::string layer_name;
  
  // MCP-specific fields
  std::string session_id;
  std::string tool_name;
  std::string resource_uri;
  
  // Performance tracking
  std::chrono::steady_clock::time_point start_time;
  std::chrono::nanoseconds accumulated_latency{0};
  
  // Custom metadata (using std::map)
  std::map<std::string, std::string> metadata;
  
  // Thread and timing info
  std::thread::id thread_id;
  std::chrono::system_clock::time_point timestamp;
  uint32_t dispatcher_id{0};
  
  // Source location
  void setLocation(const char* file, int line, const char* func) {
    source_file = file;
    source_line = line;
    source_function = func;
  }
  
  // Merge contexts as they flow through filters
  void merge(const LogContext& other);
  
  // Extract from stream info
  static LogContext fromStreamInfo(const StreamInfo& info);
  
  // Convert to LogMessage
  LogMessage toLogMessage(LogLevel level, const std::string& msg) const;
  
private:
  const char* source_file{nullptr};
  int source_line{0};
  const char* source_function{nullptr};
};

// Filter chain integration
class LogContextFilter : public network::Filter {
public:
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    // Propagate log context
    current_context_.merge(upstream_context_);
    
    // Log with context
    GOPHER_LOG_WITH_CONTEXT(debug, current_context_, 
                           "Processing data: {} bytes", data.length());
    
    return network::FilterStatus::Continue;
  }
  
private:
  LogContext current_context_;
  LogContext upstream_context_;
};

}
```

### 4. Log Sinks with RAII Management

```cpp
namespace mcp::logging {

// Base sink interface with RAII
class LogSink {
public:
  virtual ~LogSink() = default;
  
  // Core logging
  virtual void log(const LogMessage& msg) = 0;
  virtual void flush() = 0;
  
  // Sink capabilities
  virtual bool supportsAsync() const { return false; }
  virtual bool supportsRotation() const { return false; }
  virtual SinkType type() const = 0;
};

// RAII sink wrapper for automatic resource management
template<typename SinkT>
class SinkGuard {
public:
  explicit SinkGuard(std::unique_ptr<SinkT> sink)
    : sink_(std::move(sink)) {}
  
  ~SinkGuard() {
    if (sink_) {
      sink_->flush();
    }
  }
  
  SinkT* operator->() { return sink_.get(); }
  const SinkT* operator->() const { return sink_.get(); }
  
  std::unique_ptr<SinkT> release() { return std::move(sink_); }
  
private:
  std::unique_ptr<SinkT> sink_;
};

// File sink with rotation
class RotatingFileSink : public LogSink {
public:
  struct Config {
    std::string base_filename;
    size_t max_file_size = 100 * 1024 * 1024; // 100MB
    size_t max_files = 10;
    std::chrono::hours rotation_interval{24};
    bool compress_rotated = true;
  };
  
  explicit RotatingFileSink(const Config& config);
  ~RotatingFileSink() override { flush(); }
  
  void log(const LogMessage& msg) override;
  void flush() override;
  SinkType type() const override { return SinkType::File; }
  
  // Rotation management
  void checkRotation();
  void rotate();
  void cleanOldFiles();
  
private:
  Config config_;
  std::unique_ptr<std::ofstream> current_file_;
  std::chrono::system_clock::time_point last_rotation_;
  std::mutex rotation_mutex_;
};

// Stdio sink (stdout/stderr)
class StdioSink : public LogSink {
public:
  enum Target { Stdout, Stderr };
  
  explicit StdioSink(Target target = Stderr)
    : target_(target) {}
  
  void log(const LogMessage& msg) override {
    auto& stream = (target_ == Stdout) ? std::cout : std::cerr;
    stream << formatter_->format(msg) << std::endl;
  }
  
  void flush() override {
    auto& stream = (target_ == Stdout) ? std::cout : std::cerr;
    stream.flush();
  }
  
  SinkType type() const override { return SinkType::Stdio; }
  
private:
  Target target_;
  std::unique_ptr<Formatter> formatter_;
};

// MCP protocol sink
class MCPProtocolSink : public LogSink {
public:
  explicit MCPProtocolSink(std::shared_ptr<MCPTransport> transport)
    : transport_(transport) {}
  
  void log(const LogMessage& msg) override {
    // Convert to MCP notification
    LoggingMessageNotification notification;
    notification.method = "notifications/message";
    notification.params.level = toMCPLevel(msg.level);
    notification.params.logger = msg.logger_name;
    notification.params.data = msg.formatted_message;
    
    // Send through MCP transport
    transport_->send(notification);
  }
  
  void flush() override { transport_->flush(); }
  SinkType type() const override { return SinkType::MCP; }
  
private:
  std::shared_ptr<MCPTransport> transport_;
};

// External sink for FFI integration
class ExternalSink : public LogSink {
public:
  using LogCallback = std::function<void(LogLevel, const std::string&, 
                                        const std::string&)>;
  
  explicit ExternalSink(LogCallback callback)
    : callback_(callback) {}
  
  void log(const LogMessage& msg) override {
    if (callback_) {
      callback_(msg.level, msg.logger_name, msg.formatted_message);
    }
  }
  
  void flush() override {} // External system handles flushing
  SinkType type() const override { return SinkType::External; }
  
private:
  LogCallback callback_;
};

// High-performance null sink
class NullSink : public LogSink {
public:
  void log(const LogMessage&) override {} // No-op
  void flush() override {}
  SinkType type() const override { return SinkType::Null; }
};

// Sink factory for easy creation
class SinkFactory {
public:
  static std::unique_ptr<LogSink> createFileSink(const std::string& filename) {
    RotatingFileSink::Config config;
    config.base_filename = filename;
    return std::make_unique<RotatingFileSink>(config);
  }
  
  static std::unique_ptr<LogSink> createStdioSink(bool use_stderr = true) {
    return std::make_unique<StdioSink>(
      use_stderr ? StdioSink::Stderr : StdioSink::Stdout);
  }
  
  static std::unique_ptr<LogSink> createNullSink() {
    return std::make_unique<NullSink>();
  }
  
  static std::unique_ptr<LogSink> createExternalSink(
      ExternalSink::LogCallback callback) {
    return std::make_unique<ExternalSink>(callback);
  }
};

}
```

### 5. FFI C Binding with RAII Guards

```cpp
// C API for language bindings with RAII safety
extern "C" {

// Opaque handle types
typedef struct mcp_logger_impl* mcp_logger_t;
typedef struct mcp_log_context_impl* mcp_log_context_t;
typedef struct mcp_log_sink_impl* mcp_log_sink_t;
typedef struct mcp_log_guard_impl* mcp_log_guard_t;

// Log levels matching MCP spec
typedef enum {
  MCP_LOG_DEBUG = 0,
  MCP_LOG_INFO = 1,
  MCP_LOG_NOTICE = 2,
  MCP_LOG_WARNING = 3,
  MCP_LOG_ERROR = 4,
  MCP_LOG_CRITICAL = 5,
  MCP_LOG_ALERT = 6,
  MCP_LOG_EMERGENCY = 7,
  MCP_LOG_OFF = 8
} mcp_log_level_t;

// Logging modes
typedef enum {
  MCP_LOG_MODE_SYNC = 0,
  MCP_LOG_MODE_ASYNC = 1,
  MCP_LOG_MODE_NOOP = 2
} mcp_log_mode_t;

// Sink types
typedef enum {
  MCP_SINK_FILE = 0,
  MCP_SINK_STDIO = 1,
  MCP_SINK_NULL = 2,
  MCP_SINK_EXTERNAL = 3,
  MCP_SINK_MCP = 4
} mcp_sink_type_t;

// RAII guard for automatic resource cleanup
typedef struct {
  mcp_logger_t logger;
  void (*cleanup)(mcp_logger_t);
} mcp_logger_guard_t;

// Logger creation with RAII guard
mcp_logger_guard_t mcp_logger_create_guarded(const char* name, 
                                            mcp_log_mode_t mode);
mcp_logger_t mcp_logger_create(const char* name, mcp_log_mode_t mode);
void mcp_logger_destroy(mcp_logger_t logger);

// Component logger creation
mcp_logger_t mcp_logger_create_component(const char* component, 
                                        const char* name);

// Core logging functions
void mcp_log(mcp_logger_t logger, mcp_log_level_t level, 
            const char* message);
void mcp_log_formatted(mcp_logger_t logger, mcp_log_level_t level,
                      const char* format, ...);
void mcp_log_with_context(mcp_logger_t logger, mcp_log_level_t level,
                         mcp_log_context_t context, const char* message);

// Context management with RAII
mcp_log_context_t mcp_log_context_create();
void mcp_log_context_destroy(mcp_log_context_t context);
void mcp_log_context_set_field(mcp_log_context_t context, 
                              const char* key, const char* value);
void mcp_log_context_set_dispatcher(mcp_log_context_t context,
                                   uint32_t dispatcher_id);
void mcp_log_context_merge(mcp_log_context_t dest, 
                          mcp_log_context_t src);

// Sink configuration with RAII
mcp_log_sink_t mcp_log_sink_create_file(const char* filename,
                                       size_t max_size,
                                       uint32_t max_files);
mcp_log_sink_t mcp_log_sink_create_stdio(int use_stderr);
mcp_log_sink_t mcp_log_sink_create_null();
mcp_log_sink_t mcp_log_sink_create_external(
    void (*callback)(mcp_log_level_t level, 
                    const char* logger_name,
                    const char* message, 
                    const char* metadata_json,
                    void* user_data), 
    void* user_data);
void mcp_log_sink_destroy(mcp_log_sink_t sink);
void mcp_log_sink_flush(mcp_log_sink_t sink);

// Logger configuration
void mcp_logger_set_level(mcp_logger_t logger, mcp_log_level_t level);
void mcp_logger_set_sink(mcp_logger_t logger, mcp_log_sink_t sink);
void mcp_logger_set_mode(mcp_logger_t logger, mcp_log_mode_t mode);
int mcp_logger_should_log(mcp_logger_t logger, mcp_log_level_t level);

// Global configuration
void mcp_logging_set_global_level(mcp_log_level_t level);
void mcp_logging_set_component_level(const char* component, 
                                    mcp_log_level_t level);
void mcp_logging_set_pattern(const char* pattern, mcp_log_level_t level);
void mcp_logging_set_bloom_filter(int enabled, size_t size);

// External logger integration
typedef struct {
  void (*log_callback)(mcp_log_level_t level, 
                      const char* component,
                      const char* message, 
                      const char* metadata_json, 
                      void* user_data);
  void (*flush_callback)(void* user_data);
  void* user_data;
} mcp_external_logger_t;

// Register external logger from language binding
void mcp_logging_register_external(const char* name, 
                                  mcp_external_logger_t* external);
void mcp_logging_unregister_external(const char* name);

// Dispatcher integration
void mcp_logging_init_dispatcher(uint32_t dispatcher_id);
void mcp_logging_cleanup_dispatcher(uint32_t dispatcher_id);

// Statistics and monitoring
typedef struct {
  uint64_t messages_logged;
  uint64_t messages_dropped;
  uint64_t bytes_written;
  uint32_t active_loggers;
  uint32_t active_sinks;
} mcp_log_stats_t;

void mcp_logging_get_stats(mcp_log_stats_t* stats);
void mcp_logging_reset_stats();

}
```

### 6. Performance Optimizations with Bloom Filter

```cpp
namespace mcp::logging {

// Bloom filter for fast log filtering
template<typename T>
class BloomFilter {
public:
  explicit BloomFilter(size_t size = 1024, size_t num_hashes = 3)
    : bits_(size), num_hashes_(num_hashes) {}
  
  void add(const T& item) {
    for (size_t i = 0; i < num_hashes_; ++i) {
      size_t hash = hash_function(item, i) % bits_.size();
      bits_[hash] = true;
    }
  }
  
  bool mayContain(const T& item) const {
    for (size_t i = 0; i < num_hashes_; ++i) {
      size_t hash = hash_function(item, i) % bits_.size();
      if (!bits_[hash]) {
        return false; // Definitely not in set
      }
    }
    return true; // Maybe in set
  }
  
  void clear() {
    std::fill(bits_.begin(), bits_.end(), false);
  }
  
private:
  std::vector<bool> bits_;
  size_t num_hashes_;
  
  size_t hash_function(const T& item, size_t seed) const {
    return std::hash<T>{}(item) ^ (seed * 0x9e3779b9);
  }
};

// Async logger with ring buffer and bloom filter
class AsyncLogger : public Logger {
public:
  AsyncLogger(size_t buffer_size = 8192, bool use_bloom_filter = true) 
    : Logger("async", LogMode::Async),
      ring_buffer_(buffer_size),
      use_bloom_filter_(use_bloom_filter) {
    if (use_bloom_filter_) {
      bloom_filter_ = std::make_unique<BloomFilter<std::string>>(4096, 3);
    }
    worker_thread_ = std::thread(&AsyncLogger::processLogs, this);
  }
  
  ~AsyncLogger() {
    shutdown_.store(true, std::memory_order_release);
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
  }
  
  // Override shouldLog with bloom filter optimization
  bool shouldLog(LogLevel level) const override {
    // Fast path: bloom filter check
    if (use_bloom_filter_ && bloom_filter_) {
      if (!bloom_filter_->mayContain(name_)) {
        return false;
      }
    }
    return Logger::shouldLog(level);
  }
  
protected:
  void logImpl(LogLevel level, const std::string& msg) override {
    LogMessage log_msg{level, msg, std::chrono::system_clock::now()};
    
    // Lock-free enqueue
    if (!ring_buffer_.try_push(std::move(log_msg))) {
      // Buffer full, handle overflow
      handleOverflow(log_msg);
    }
  }
  
private:
  // Lock-free ring buffer
  folly::ProducerConsumerQueue<LogMessage> ring_buffer_;
  std::thread worker_thread_;
  std::atomic<bool> shutdown_{false};
  bool use_bloom_filter_;
  std::unique_ptr<BloomFilter<std::string>> bloom_filter_;
  
  void processLogs() {
    LogMessage msg;
    while (!shutdown_.load(std::memory_order_relaxed)) {
      if (ring_buffer_.try_pop(msg)) {
        sink_->log(msg);
      } else {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }
  }
  
  void handleOverflow(const LogMessage& msg) {
    // Strategy: drop oldest or write to emergency sink
    overflow_counter_.fetch_add(1, std::memory_order_relaxed);
  }
  
  std::atomic<uint64_t> overflow_counter_{0};
};

// Compile-time filtering with zero overhead
template<LogLevel MinLevel>
class StaticFilteredLogger {
public:
  explicit StaticFilteredLogger(std::shared_ptr<Logger> logger)
    : logger_(logger) {}
  
  template<typename... Args>
  void log(LogLevel level, const char* fmt, Args&&... args) {
    if constexpr (level >= MinLevel) {
      logger_->log(level, fmt, std::forward<Args>(args)...);
    }
    // Else: compiled out entirely - zero overhead
  }
  
  // Component-specific compile-time filtering
  template<Component C, typename... Args>
  void logComponent(LogLevel level, const char* fmt, Args&&... args) {
    if constexpr (shouldLogComponent(C, level)) {
      logger_->logWithComponent(level, C, fmt, std::forward<Args>(args)...);
    }
  }
  
private:
  std::shared_ptr<Logger> logger_;
  
  // Compile-time component filtering
  template<Component C>
  static constexpr bool shouldLogComponent(Component comp, LogLevel level) {
    // Can be specialized per component at compile time
    if constexpr (C == Component::Network) {
      return level >= LogLevel::Warning;
    } else if constexpr (C == Component::Filter) {
      return level >= LogLevel::Debug;
    }
    return level >= LogLevel::Info;
  }
};

// No-op logger for critical paths
class NoOpLogger : public Logger {
public:
  NoOpLogger() : Logger("noop", LogMode::NoOp) {}
  
  bool shouldLog(LogLevel) const override { return false; }
  
  template<typename... Args>
  void log(LogLevel, const char*, Args&&...) {
    // Completely optimized out
  }
};

}
```

### 7. Logging Macros

```cpp
// Zero-configuration logging - works without any setup
#define LOG(level, ...)                                            \
  do {                                                             \
    auto logger = ::mcp::logging::LoggerRegistry::instance()       \
                    .getDefaultLogger();                           \
    if (logger->shouldLog(::mcp::logging::LogLevel::level)) {      \
      ::mcp::logging::LogContext ctx;                              \
      ctx.setLocation(__FILE__, __LINE__, __FUNCTION__);           \
      logger->logWithContext(::mcp::logging::LogLevel::level,      \
                           ctx, __VA_ARGS__);                      \
    }                                                              \
  } while (0)

// Quick logging macros
#define LOG_DEBUG(...) LOG(Debug, __VA_ARGS__)
#define LOG_INFO(...) LOG(Info, __VA_ARGS__)
#define LOG_WARNING(...) LOG(Warning, __VA_ARGS__)
#define LOG_ERROR(...) LOG(Error, __VA_ARGS__)

// Efficient logging macros with compile-time optimization
#ifdef GOPHER_LOG_DISABLE
  #define GOPHER_LOG(level, ...) ((void)0)
#else
  #define GOPHER_LOG(level, ...)                                    \
    do {                                                            \
      if (::mcp::logging::LoggerRegistry::instance()               \
          .shouldLog(#level, ::mcp::logging::LogLevel::level)) {   \
        ::mcp::logging::LoggerRegistry::instance()                 \
          .getOrCreateLogger(GOPHER_LOG_COMPONENT)                 \
          ->log(::mcp::logging::LogLevel::level,                   \
               GOPHER_LOG_LOCATION, __VA_ARGS__);                  \
      }                                                             \
    } while (0)
#endif

// Source location helper
#define GOPHER_LOG_LOCATION __FILE__, __LINE__, __FUNCTION__

// Context-aware logging
#define GOPHER_LOG_WITH_CONTEXT(level, context, ...)               \
  GOPHER_LOG_IMPL_WITH_CONTEXT(::mcp::logging::LogLevel::level,    \
                              context, __VA_ARGS__)

// Component-specific logging
#define GOPHER_LOG_COMPONENT_DEBUG(component, ...)                 \
  GOPHER_LOG_COMPONENT(component, Debug, __VA_ARGS__)

// Structured logging
#define GOPHER_LOG_STRUCTURED(level, ...)                          \
  ::mcp::logging::StructuredLogger()                               \
    .setLevel(::mcp::logging::LogLevel::level)                     \
    .setLocation(GOPHER_LOG_LOCATION)                              \
    __VA_ARGS__                                                    \
    .log()

// Usage example:
// GOPHER_LOG_STRUCTURED(Info,
//   .field("request_id", request_id)
//   .field("method", "tools/call")
//   .field("duration_ms", duration.count())
// );
```

### 8. Configuration and Management

```yaml
# logging.yaml - Runtime configuration
logging:
  # Global log level
  global_level: info
  
  # Component-specific levels
  components:
    mcp.server: debug
    mcp.client: info
    network.connection: warning
    filter.http_codec: debug
    
  # Fine-grain patterns
  patterns:
    - pattern: "filter/*.cc"
      level: debug
    - pattern: "network/connection_*.cc"
      level: trace
      
  # Sink configuration
  sinks:
    - type: rotating_file
      config:
        base_filename: /var/log/gopher-mcp/app.log
        max_file_size: 104857600  # 100MB
        max_files: 10
        rotation_interval: 24h
        compress_rotated: true
        
    - type: console
      config:
        colored: true
        format: "[%Y-%m-%d %H:%M:%S.%e][%t][%l][%n] %v"
        
    - type: mcp_protocol
      config:
        transport: stdio
        buffer_size: 8192
        
  # Performance settings
  performance:
    async_logging: true
    ring_buffer_size: 16384
    flush_interval: 1000ms
    
  # Format patterns
  format:
    default: "[%Y-%m-%d %H:%M:%S.%e][%t][%l][%n] %v"
    json: '{"timestamp":"%Y-%m-%d %H:%M:%S.%e","thread":"%t","level":"%l","logger":"%n","message":"%v"}'
```

### 9. Integration with Dispatcher Model and Layered Architecture

```cpp
namespace mcp::logging {

// Thread-local logger per dispatcher with context
class DispatcherLogger {
public:
  static DispatcherLogger& instance() {
    static thread_local DispatcherLogger instance;
    return instance;
  }
  
  void initialize(uint32_t dispatcher_id, Component component = Component::Root) {
    dispatcher_id_ = dispatcher_id;
    component_ = component;
    logger_ = LoggerRegistry::instance().getOrCreateLogger(
      fmt::format("dispatcher.{}.{}", dispatcher_id, toString(component)));
    
    // Set thread-local context
    context_.dispatcher_id = dispatcher_id;
    context_.thread_id = std::this_thread::get_id();
  }
  
  // Log with automatic dispatcher context
  template<typename... Args>
  void log(LogLevel level, const char* fmt, Args&&... args) {
    if (logger_->shouldLog(level)) {
      context_.timestamp = std::chrono::system_clock::now();
      logger_->logWithContext(level, context_, fmt, 
                             std::forward<Args>(args)...);
    }
  }
  
  // Set current layer context
  void enterLayer(const std::string& layer_name) {
    context_.metadata["layer"] = layer_name;
  }
  
  void exitLayer() {
    context_.metadata.erase("layer");
  }
  
  // Set connection/stream context
  void setConnectionContext(const std::string& conn_id) {
    context_.connection_id = conn_id;
  }
  
  void setStreamContext(const std::string& stream_id) {
    context_.stream_id = stream_id;
  }
  
  LogContext& context() { return context_; }
  
private:
  uint32_t dispatcher_id_{0};
  Component component_{Component::Root};
  std::shared_ptr<Logger> logger_;
  LogContext context_;
};

// Layer-aware logging for integration with architecture
class LayerLogger {
public:
  LayerLogger(const std::string& layer_name, Component component)
    : layer_name_(layer_name), component_(component) {
    logger_ = std::make_shared<ComponentLogger>(component, layer_name);
  }
  
  // RAII layer context
  class LayerScope {
  public:
    LayerScope(const std::string& layer) {
      DispatcherLogger::instance().enterLayer(layer);
    }
    ~LayerScope() {
      DispatcherLogger::instance().exitLayer();
    }
  };
  
  template<typename... Args>
  void log(LogLevel level, const char* fmt, Args&&... args) {
    LayerScope scope(layer_name_);
    logger_->log(level, fmt, std::forward<Args>(args)...);
  }
  
private:
  std::string layer_name_;
  Component component_;
  std::shared_ptr<ComponentLogger> logger_;
};

// Integration with filter chain
class LoggingFilter : public network::Filter {
public:
  LoggingFilter(const std::string& name)
    : filter_name_(name),
      logger_(Component::Filter, name) {}
  
  network::FilterStatus onData(Buffer& data, bool end_stream) override {
    // Automatic context propagation
    auto& dispatcher_logger = DispatcherLogger::instance();
    
    logger_.log(LogLevel::Debug, "Processing {} bytes in filter {}", 
               data.length(), filter_name_);
    
    // Continue chain with context
    return network::FilterStatus::Continue;
  }
  
private:
  std::string filter_name_;
  ComponentLogger logger_;
};

// Integration macros
#define DISPATCHER_LOG(level, ...)                                 \
  ::mcp::logging::DispatcherLogger::instance()                     \
    .log(::mcp::logging::LogLevel::level, __VA_ARGS__)

#define LAYER_LOG(layer, level, ...)                              \
  ::mcp::logging::LayerLogger(layer, ::mcp::logging::Component::Root) \
    .log(::mcp::logging::LogLevel::level, __VA_ARGS__)

#define COMPONENT_LOG(component, level, ...)                      \
  ::mcp::logging::ComponentLogger(::mcp::logging::Component::component, \
                                 #component)                       \
    .log(::mcp::logging::LogLevel::level, __VA_ARGS__)

}
```

### 10. Testing and Validation

```cpp
namespace mcp::logging::test {

// Test sink for capturing logs
class TestLogSink : public LogSink {
public:
  void log(const LogMessage& msg) override {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back(msg);
  }
  
  std::vector<LogMessage> getMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
  }
  
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.clear();
  }
  
private:
  mutable std::mutex mutex_;
  std::vector<LogMessage> messages_;
};

// Test utilities
#define EXPECT_LOG_CONTAINS(sink, level, substring)                \
  EXPECT_TRUE(::mcp::logging::test::containsLog(                   \
    sink, ::mcp::logging::LogLevel::level, substring))

#define EXPECT_NO_LOGS(sink, level)                                \
  EXPECT_TRUE(::mcp::logging::test::hasNoLogs(                     \
    sink, ::mcp::logging::LogLevel::level))

}
```

## Key Features Summary

1. **MCP Compliance**: Full support for MCP logging levels and notifications
2. **Multiple Sink Types**: File, stdio, null, external, and MCP protocol sinks
3. **Component-Level Logging**: Hierarchical component-based logging with fine-grain control
4. **Sync/Async Modes**: Easy switching between synchronous and asynchronous logging
5. **RAII Pattern**: Automatic resource management for both C++ and FFI boundaries
6. **FFI-Friendly**: Complete C API with RAII guards for safe cross-language integration
7. **Dispatcher Context**: Thread-local loggers with automatic dispatcher context capture
8. **Layered Architecture Integration**: Seamless integration with gopher-mcp layers
9. **Bloom Filter Optimization**: Fast path filtering for high-performance scenarios
10. **Performance Options**: Compile-time filtering, no-op mode, lock-free async logging
11. **Context Propagation**: Automatic flow through filter chains with metadata
12. **File Rotation**: Time and size-based log rotation with compression

## Migration Path

1. Replace all `std::cerr` and `std::cout` with appropriate log levels
2. Add component names to existing logging locations
3. Integrate with filter chain for context propagation
4. Configure sinks based on deployment environment
5. Set up log rotation and retention policies
6. Enable async logging for performance-critical paths

## Performance Considerations

- **Hot Path**: Use `NullSink` or compile-time disabled logging with no-op mode
- **Normal Path**: Async logging with ring buffer and bloom filter optimization
- **Debug Path**: Synchronous logging with full context and component details
- **Production**: File rotation with compression, external sink for aggregation
- **Critical Data Path**: Complete no-op with zero overhead through compile-time elimination

## Usage Examples

### Zero-Configuration Usage
```cpp
// Works immediately without any setup
LOG_INFO("Application started");
LOG_DEBUG("Processing request: {}", request_id);
LOG_ERROR("Failed to connect: {}", error_msg);

// The framework automatically:
// - Creates a default logger
// - Outputs to stderr
// - Uses Info level by default
// - Includes timestamp, thread ID, and source location
// - No configuration file needed!
```

### Component-Level Logging
```cpp
// Create component logger
ComponentLogger server_logger(Component::Server, "mcp_server");
server_logger.setLevel(LogLevel::Debug);
server_logger.log(LogLevel::Info, "Server started on port {}", port);

// Use layer logging
LayerLogger network_layer("NetworkLayer", Component::Network);
network_layer.log(LogLevel::Debug, "Connection established: {}", conn_id);
```

### Sink Configuration
```cpp
// File sink with rotation
auto file_sink = SinkFactory::createFileSink("/var/log/app.log");

// Stdio sink
auto console_sink = SinkFactory::createStdioSink(true); // stderr

// Null sink for performance
auto null_sink = SinkFactory::createNullSink();

// External sink for language bindings
auto external_sink = SinkFactory::createExternalSink(
  [](LogLevel level, const std::string& logger, const std::string& msg) {
    // Forward to Python/Go/Rust logging system
  });
```

### FFI Integration
```c
// C API usage
mcp_logger_t logger = mcp_logger_create("my_app", MCP_LOG_MODE_ASYNC);
mcp_log_sink_t sink = mcp_log_sink_create_stdio(1); // stderr
mcp_logger_set_sink(logger, sink);

mcp_log(logger, MCP_LOG_INFO, "Application started");

// With RAII guard
mcp_logger_guard_t guard = mcp_logger_create_guarded("guarded", MCP_LOG_MODE_SYNC);
// Automatic cleanup when guard goes out of scope
```

### Dispatcher Integration
```cpp
// Initialize dispatcher logger
DispatcherLogger::instance().initialize(dispatcher_id, Component::Server);

// Use in dispatcher context
DISPATCHER_LOG(Debug, "Processing request in dispatcher {}", dispatcher_id);

// Set connection context
DispatcherLogger::instance().setConnectionContext(conn_id);
```

### Rich Metadata Logging
```cpp
// Log with comprehensive metadata
LogContext ctx;
ctx.component = Component::Filter;
ctx.component_name = "http_codec";
ctx.layer_name = "FilterChain";
ctx.trace_id = "abc123";
ctx.request_id = "req456";
ctx.metadata["client_ip"] = "192.168.1.1";
ctx.metadata["protocol"] = "HTTP/2";

auto start = std::chrono::steady_clock::now();
// ... process request ...
auto end = std::chrono::steady_clock::now();
ctx.accumulated_latency = end - start;

logger->logWithContext(LogLevel::Info, ctx, 
                      "Request processed: {} bytes in {} ms", 
                      bytes, duration_ms);

// Output includes all metadata for analysis:
// [2024-01-01 12:00:00.123][tid:1234][INFO][Filter:http_codec][FilterChain]
// [trace:abc123][req:req456][lat:15ms][client_ip:192.168.1.1][protocol:HTTP/2]
// Request processed: 1024 bytes in 15 ms
```

### Default Formatter Output
```cpp
// With zero configuration, logs are formatted with useful defaults:
LOG_INFO("Server started");
// Output: [2024-01-01 12:00:00.123][12345][1234][INFO][default] Server started (main.cpp:42:main)

// With component context:
COMPONENT_LOG(Network, Info, "Connection from {}", client_addr);
// Output: [2024-01-01 12:00:00.123][12345][1234][INFO][Network] Connection from 192.168.1.1 (network.cpp:100:onConnect)

// Process ID, Thread ID, and source location are always included
```

This design provides a production-ready, high-performance logging framework that:
- Works immediately with zero configuration
- Uses standard C++ containers
- Captures rich metadata for comprehensive log analysis
- Seamlessly integrates with Gopher-MCP's layered architecture
- Maintains flexibility for all use cases from development to production