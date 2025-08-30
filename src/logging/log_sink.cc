#include "mcp/logging/log_sink.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace mcp {
namespace logging {

// LogSink base implementation is in the header file

// NullSink implementation is inline in the header

// StdioSink implementation is inline in the header

// ExternalSink implementation is inline in the header

// RotatingFileSink implementation
RotatingFileSink::RotatingFileSink(const Config& config)
  : config_(config),
    current_size_(0),
    file_index_(0) {
  openFile();
}

RotatingFileSink::~RotatingFileSink() {
  flush();
  closeFile();
}

void RotatingFileSink::log(const LogMessage& msg) {
  std::string formatted = formatter_->format(msg);
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (!file_.is_open()) {
    openFile();
  }
  
  // Check if rotation is needed
  if (config_.max_file_size > 0 && 
      current_size_ + formatted.size() > config_.max_file_size) {
    rotate();
  }
  
  // Check time-based rotation
  if (config_.rotation_period != std::chrono::seconds::zero()) {
    auto now = std::chrono::system_clock::now();
    if (now - last_rotation_ > config_.rotation_period) {
      rotate();
    }
  }
  
  // Write to file
  file_ << formatted;
  if (formatted.empty() || formatted.back() != '\n') {
    file_ << '\n';
  }
  
  current_size_ += formatted.size() + 1;
  
  // Auto-flush based on policy
  if (config_.auto_flush) {
    file_.flush();
  }
}

void RotatingFileSink::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_.is_open()) {
    file_.flush();
  }
}

SinkType RotatingFileSink::type() const {
  return SinkType::File;
}

bool RotatingFileSink::supportsRotation() const {
  return true;
}

void RotatingFileSink::openFile() {
  closeFile();
  
  file_.open(config_.base_filename, std::ios::app);
  if (file_.is_open()) {
    // Get current file size
    file_.seekp(0, std::ios::end);
    current_size_ = file_.tellp();
    last_rotation_ = std::chrono::system_clock::now();
  }
}

void RotatingFileSink::closeFile() {
  if (file_.is_open()) {
    file_.close();
  }
}

void RotatingFileSink::rotate() {
  closeFile();
  
  // Rename existing files
  namespace fs = std::filesystem;
  
  // Delete oldest file if at max
  if (config_.max_files > 0) {
    std::string oldest = config_.base_filename + "." + 
                        std::to_string(config_.max_files);
    if (fs::exists(oldest)) {
      fs::remove(oldest);
    }
    
    // Shift existing backup files
    for (int i = config_.max_files - 1; i > 0; --i) {
      std::string old_name = config_.base_filename + "." + std::to_string(i);
      std::string new_name = config_.base_filename + "." + std::to_string(i + 1);
      
      if (fs::exists(old_name)) {
        fs::rename(old_name, new_name);
      }
    }
  }
  
  // Rename current file to .1
  if (fs::exists(config_.base_filename)) {
    std::string backup = config_.base_filename + ".1";
    fs::rename(config_.base_filename, backup);
  }
  
  // Open new file
  openFile();
}

// SinkFactory implementation
std::unique_ptr<LogSink> SinkFactory::createFileSink(const std::string& filename) {
  RotatingFileSink::Config config;
  config.base_filename = filename;
  config.max_file_size = 10 * 1024 * 1024; // 10MB default
  config.max_files = 5;
  config.auto_flush = false;
  
  return std::make_unique<RotatingFileSink>(config);
}

std::unique_ptr<LogSink> SinkFactory::createStdioSink(bool use_stderr) {
  return std::make_unique<StdioSink>(
    use_stderr ? StdioSink::Stderr : StdioSink::Stdout);
}

std::unique_ptr<LogSink> SinkFactory::createNullSink() {
  return std::make_unique<NullSink>();
}

std::unique_ptr<LogSink> SinkFactory::createExternalSink(
    std::function<void(LogLevel, const std::string&, const std::string&)> callback) {
  return std::make_unique<ExternalSink>(callback);
}

} // namespace logging
} // namespace mcp