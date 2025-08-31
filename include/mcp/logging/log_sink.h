#pragma once

#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>

#include "mcp/logging/log_formatter.h"
#include "mcp/logging/log_message.h"

namespace mcp {
namespace logging {

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

  // Set formatter
  virtual void setFormatter(std::unique_ptr<Formatter> formatter) {
    formatter_ = std::move(formatter);
  }

 protected:
  std::unique_ptr<Formatter> formatter_{std::make_unique<DefaultFormatter>()};
};

// RAII sink wrapper for automatic resource management
template <typename SinkT>
class SinkGuard {
 public:
  explicit SinkGuard(std::unique_ptr<SinkT> sink) : sink_(std::move(sink)) {}

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

// Stdio sink (stdout/stderr)
class StdioSink : public LogSink {
 public:
  enum Target { Stdout, Stderr };

  explicit StdioSink(Target target = Stderr) : target_(target) {}

  void log(const LogMessage& msg) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& stream = (target_ == Stdout) ? std::cout : std::cerr;
    stream << formatter_->format(msg) << std::endl;
  }

  void flush() override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& stream = (target_ == Stdout) ? std::cout : std::cerr;
    stream.flush();
  }

  SinkType type() const override { return SinkType::Stdio; }

 private:
  Target target_;
  std::mutex mutex_;
};

// File sink with rotation
class RotatingFileSink : public LogSink {
 public:
  struct Config {
    std::string base_filename;
    size_t max_file_size = 100 * 1024 * 1024;  // 100MB
    size_t max_files = 10;
    std::chrono::hours rotation_interval{24};
    bool compress_rotated = false;  // Simplified for now
  };

  explicit RotatingFileSink(const Config& config) : config_(config) {
    openFile();
  }

  ~RotatingFileSink() override {
    flush();
    closeFile();
  }

  void log(const LogMessage& msg) override {
    std::lock_guard<std::mutex> lock(mutex_);
    checkRotation();
    if (current_file_ && current_file_->is_open()) {
      *current_file_ << formatter_->format(msg) << std::endl;
      current_size_ += formatter_->format(msg).size() + 1;
    }
  }

  void flush() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_file_ && current_file_->is_open()) {
      current_file_->flush();
    }
  }

  SinkType type() const override { return SinkType::File; }
  bool supportsRotation() const override { return true; }

 private:
  void openFile() {
    current_file_ =
        std::make_unique<std::ofstream>(config_.base_filename, std::ios::app);
    if (current_file_->is_open()) {
      // Get current file size
      current_file_->seekp(0, std::ios::end);
      current_size_ = current_file_->tellp();
      last_rotation_ = std::chrono::system_clock::now();
    }
  }

  void closeFile() {
    if (current_file_ && current_file_->is_open()) {
      current_file_->close();
    }
    current_file_.reset();
  }

  void checkRotation() {
    // Check size-based rotation
    if (current_size_ >= config_.max_file_size) {
      rotate();
      return;
    }

    // Check time-based rotation
    auto now = std::chrono::system_clock::now();
    if (now - last_rotation_ >= config_.rotation_interval) {
      rotate();
    }
  }

  void rotate() {
    closeFile();

    // Rename existing files
    for (int i = config_.max_files - 1; i > 0; --i) {
      std::string old_name = config_.base_filename + "." + std::to_string(i);
      std::string new_name =
          config_.base_filename + "." + std::to_string(i + 1);
      std::rename(old_name.c_str(), new_name.c_str());
    }

    // Rename current file to .1
    std::string rotated_name = config_.base_filename + ".1";
    std::rename(config_.base_filename.c_str(), rotated_name.c_str());

    // Clean old files
    cleanOldFiles();

    // Open new file
    openFile();
  }

  void cleanOldFiles() {
    std::string oldest =
        config_.base_filename + "." + std::to_string(config_.max_files + 1);
    std::remove(oldest.c_str());
  }

  Config config_;
  std::unique_ptr<std::ofstream> current_file_;
  std::chrono::system_clock::time_point last_rotation_;
  size_t current_size_{0};
  std::mutex mutex_;
};

// High-performance null sink
class NullSink : public LogSink {
 public:
  void log(const LogMessage&) override {}  // No-op
  void flush() override {}
  SinkType type() const override { return SinkType::Null; }
};

// External sink for FFI integration
class ExternalSink : public LogSink {
 public:
  using LogCallback =
      std::function<void(LogLevel, const std::string&, const std::string&)>;

  explicit ExternalSink(LogCallback callback) : callback_(callback) {}

  void log(const LogMessage& msg) override {
    if (callback_) {
      callback_(msg.level, msg.logger_name, formatter_->format(msg));
    }
  }

  void flush() override {}  // External system handles flushing
  SinkType type() const override { return SinkType::External; }

 private:
  LogCallback callback_;
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
    return std::make_unique<StdioSink>(use_stderr ? StdioSink::Stderr
                                                  : StdioSink::Stdout);
  }

  static std::unique_ptr<LogSink> createNullSink() {
    return std::make_unique<NullSink>();
  }

  static std::unique_ptr<LogSink> createExternalSink(
      ExternalSink::LogCallback callback) {
    return std::make_unique<ExternalSink>(callback);
  }
};

}  // namespace logging
}  // namespace mcp