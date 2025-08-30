#include "mcp/logging/logger.h"
#include <iostream>
#include <thread>
#include <unistd.h>

namespace mcp {
namespace logging {

// Static members
constexpr size_t Logger::max_queue_size_;

// Logger implementation
Logger::Logger(const std::string& name, LogMode mode)
  : name_(name),
    effective_level_(LogLevel::Info),
    mode_(mode),
    bloom_filter_hint_(nullptr),
    shutdown_(false),
    overflow_counter_(0) {
  
  if (mode_ == LogMode::Async) {
    startAsyncWorker();
  }
}

Logger::~Logger() {
  if (mode_ == LogMode::Async) {
    stopAsyncWorker();
  }
  flush();
}

void Logger::setSink(std::shared_ptr<LogSink> sink) {
  std::lock_guard<std::mutex> lock(sink_mutex_);
  sink_ = sink;
}

void Logger::setLevel(LogLevel level) {
  effective_level_.store(level);
}

LogLevel Logger::getLevel() const {
  return effective_level_.load();
}

void Logger::setMode(LogMode mode) {
  if (mode_ == mode) return;
  
  if (mode_ == LogMode::Async) {
    stopAsyncWorker();
  }
  
  mode_ = mode;
  
  if (mode == LogMode::Async) {
    startAsyncWorker();
  }
}

void Logger::setBloomFilterHint(BloomFilter<std::string>* filter) {
  bloom_filter_hint_ = filter;
}

bool Logger::shouldLog(LogLevel level) const {
  // Quick rejection based on level
  if (level < level_.load()) {
    return false;
  }
  
  // Bloom filter check if available
  if (bloom_filter_hint_ && !bloom_filter_hint_->mayContain(name_)) {
    return false;
  }
  
  return true;
}

void Logger::log(LogLevel level, const char* file, int line, 
                 const char* func, const std::string& msg) {
  if (!shouldLog(level)) return;
  
  LogMessage log_msg;
  log_msg.level = level;
  log_msg.message = msg;
  log_msg.logger_name = name_;
  log_msg.timestamp = std::chrono::system_clock::now();
  log_msg.thread_id = std::this_thread::get_id();
  log_msg.file = file;
  log_msg.line = line;
  log_msg.function = func;
  
  logImpl(std::move(log_msg));
}

void Logger::logWithComponent(LogLevel level, Component comp, 
                              const std::string& msg) {
  if (!shouldLog(level)) return;
  
  LogMessage log_msg;
  log_msg.level = level;
  log_msg.message = msg;
  log_msg.logger_name = name_;
  log_msg.timestamp = std::chrono::system_clock::now();
  log_msg.thread_id = std::this_thread::get_id();
  log_msg.component = comp;
  
  logImpl(std::move(log_msg));
}

void Logger::logWithContext(LogLevel level, const LogContext& ctx, 
                           const std::string& msg) {
  if (!shouldLog(level)) return;
  
  LogMessage log_msg;
  log_msg.level = level;
  log_msg.message = msg;
  log_msg.logger_name = name_;
  log_msg.timestamp = std::chrono::system_clock::now();
  log_msg.thread_id = std::this_thread::get_id();
  
  // Copy context data
  log_msg.trace_id = ctx.trace_id;
  log_msg.request_id = ctx.request_id;
  log_msg.component = ctx.component;
  log_msg.component_name = ctx.component_name;
  log_msg.file = ctx.file;
  log_msg.line = ctx.line;
  log_msg.function = ctx.function;
  log_msg.key_values = ctx.key_values;
  
  logImpl(std::move(log_msg));
}

void Logger::flush() {
  if (mode_ == LogMode::Async) {
    // Wait for async queue to empty
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this] { return message_queue_.empty(); });
  }
  
  std::lock_guard<std::mutex> lock(sink_mutex_);
  if (sink_) {
    sink_->flush();
  }
}

void Logger::logImpl(LogMessage&& msg) {
  switch (mode_) {
    case LogMode::Sync:
      processMessage(msg);
      break;
    
    case LogMode::Async:
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (message_queue_.size() >= max_queue_size_) {
          // Handle overflow - drop oldest
          message_queue_.pop();
          overflow_counter_.fetch_add(1, std::memory_order_relaxed);
        }
        message_queue_.push(std::move(msg));
      }
      queue_cv_.notify_one();
      break;
    
    case LogMode::NoOp:
      // Do nothing
      break;
  }
}

void Logger::processMessage(const LogMessage& msg) {
  std::lock_guard<std::mutex> lock(sink_mutex_);
  if (sink_) {
    sink_->log(msg);
  }
}

void Logger::startAsyncWorker() {
  shutdown_.store(false, std::memory_order_relaxed);
  async_thread_ = std::thread([this]() {
    while (!shutdown_.load(std::memory_order_relaxed)) {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] { 
        return !message_queue_.empty() || shutdown_.load(std::memory_order_relaxed);
      });
      
      // Process all messages in queue
      while (!message_queue_.empty()) {
        auto msg = message_queue_.front();
        message_queue_.pop();
        lock.unlock();
        
        // Log without holding queue lock
        processMessage(msg);
        
        lock.lock();
      }
    }
    
    // Process remaining messages on shutdown
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!message_queue_.empty()) {
      auto msg = message_queue_.front();
      message_queue_.pop();
      processMessage(msg);
    }
  });
}

void Logger::stopAsyncWorker() {
  shutdown_.store(true, std::memory_order_relaxed);
  queue_cv_.notify_all();
  if (async_thread_.joinable()) {
    async_thread_.join();
  }
}

// NoOpLogger implementation
bool NoOpLogger::shouldLog(LogLevel) const {
  return false;
}

void NoOpLogger::log(LogLevel, const char*, int, const char*, const std::string&) {
  // No operation
}

void NoOpLogger::logWithComponent(LogLevel, Component, const std::string&) {
  // No operation
}

void NoOpLogger::logWithContext(LogLevel, const LogContext&, const std::string&) {
  // No operation
}

void NoOpLogger::flush() {
  // No operation
}

// ComponentLogger implementation
ComponentLogger::ComponentLogger(Component component, const std::string& name)
  : component_(component),
    component_name_(name) {
  // Logger will be retrieved from registry when needed
}

void ComponentLogger::log(LogLevel level, const std::string& msg) {
  auto logger = getLogger();
  if (logger) {
    logger->logWithComponent(level, component_, msg);
  }
}

void ComponentLogger::setLevel(LogLevel level) {
  auto logger = getLogger();
  if (logger) {
    logger->setLevel(level);
  }
}

std::shared_ptr<Logger> ComponentLogger::getLogger() {
  // This will be implemented to get from LoggerRegistry
  // For now, return nullptr
  return nullptr;
}

} // namespace logging
} // namespace mcp