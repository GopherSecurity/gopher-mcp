/**
 * @file echo_stdio_transport_advanced.h
 * @brief Stdio transport implementation for echo client/server
 */

#pragma once

#include "mcp/echo/echo_transport_advanced.h"
#include <thread>
#include <atomic>
#include <mutex>

namespace mcp {
namespace echo {

/**
 * @brief Stdio transport implementation
 * 
 * Uses stdin/stdout for bidirectional communication.
 * Suitable for command-line echo applications and testing.
 */
class StdioEchoTransport : public EchoTransport {
public:
  StdioEchoTransport();
  ~StdioEchoTransport() override;
  
  // EchoTransport interface
  variant<Success, Error> initialize() override;
  variant<Success, Error> connect(const std::string& endpoint) override;
  variant<Success, Error> listen(const std::string& endpoint) override;
  variant<Success, Error> send(const std::string& data) override;
  void close() override;
  Status getStatus() const override;
  void setCallbacks(const Callbacks& callbacks) override;
  bool isBidirectional() const override { return true; }
  std::string getTransportType() const override { return "stdio"; }
  
private:
  void readThread();
  void setNonBlocking(int fd);
  
  Callbacks callbacks_;
  std::atomic<Status> status_{Status::Disconnected};
  std::atomic<bool> running_{false};
  std::thread read_thread_;
  mutable std::mutex write_mutex_;
  
  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  bool initialized_ = false;
};

} // namespace echo
} // namespace mcp