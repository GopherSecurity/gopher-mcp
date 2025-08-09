/**
 * @file stdio_transport.h
 * @brief Stdio transport implementation for echo server/client
 * 
 * Thread Safety Design:
 * - Separate read thread for stdin monitoring
 * - Non-blocking I/O with poll() for responsiveness
 * - Atomic flags for state management
 * - Thread-safe send() via stdout with flush
 * 
 * This implementation replaces the original dispatcher-based approach
 * with a simpler thread-based design that doesn't require libevent.
 */

#ifndef MCP_ECHO_STDIO_TRANSPORT_H
#define MCP_ECHO_STDIO_TRANSPORT_H

#include "mcp/echo/echo_basic.h"
#include "mcp/event/libevent_dispatcher.h"
#include <thread>
#include <atomic>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace mcp {
namespace echo {

/**
 * Stdio transport implementation
 * 
 * Uses stdin/stdout for communication, suitable for pipe-based IPC
 * 
 * Key Design Decisions:
 * - Non-blocking stdin to avoid blocking on read
 * - Poll with timeout for responsive shutdown
 * - Separate read thread to avoid blocking main thread
 * - Direct stdout write (no buffering issues)
 * 
 * Original Thread Safety Problem (from dispatcher version):
 * The original implementation required posting to dispatcher thread
 * because libevent's file events must be created on the same thread
 * that runs the event loop. This version avoids that complexity
 * by using a dedicated read thread instead.
 */
class StdioTransport : public EchoTransportBase {
public:
  StdioTransport() 
      : running_(false),
        connected_(false) {}
  
  ~StdioTransport() override {
    stop();
  }
  
  void send(const std::string& data) override {
    if (!running_) {
      return;
    }
    
    // Write to stdout
    // Thread-safe: cout with immediate flush
    // No need for mutex as cout is thread-safe for complete operations
    std::cout << data;
    std::cout.flush();
  }
  
  void setDataCallback(DataCallback callback) override {
    data_callback_ = callback;
  }
  
  void setConnectionCallback(ConnectionCallback callback) override {
    connection_callback_ = callback;
  }
  
  bool start() override {
    if (running_) {
      return true;
    }
    
    // Set stdin to non-blocking mode
    // This prevents read() from blocking indefinitely
    // and allows clean shutdown via poll timeout
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) {
      return false;
    }
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    running_ = true;
    connected_ = true;
    
    // Start read thread
    read_thread_ = std::thread([this]() {
      readLoop();
    });
    
    // Notify connection
    if (connection_callback_) {
      connection_callback_(true);
    }
    
    return true;
  }
  
  void stop() override {
    if (!running_) {
      return;
    }
    
    running_ = false;
    
    // Wait for read thread
    if (read_thread_.joinable()) {
      read_thread_.join();
    }
    
    // Notify disconnection
    if (connected_ && connection_callback_) {
      connection_callback_(false);
    }
    
    connected_ = false;
  }
  
  bool isConnected() const override {
    return connected_;
  }
  
  std::string getTransportType() const override {
    return "stdio";
  }
  
private:
  void readLoop() {
    // Read thread main loop
    // Uses poll() for efficient waiting with timeout
    // Allows responsive shutdown without blocking
    char buffer[8192];
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    
    while (running_) {
      // Poll with 100ms timeout
      // This allows checking running_ flag regularly for clean shutdown
      int ret = poll(&pfd, 1, 100);
      
      if (ret > 0 && (pfd.revents & POLLIN)) {
        ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
        
        if (n > 0) {
          // Data received
          if (data_callback_) {
            data_callback_(std::string(buffer, n));
          }
        } else if (n == 0) {
          // EOF - stdin closed (other end of pipe closed)
          if (connection_callback_) {
            connection_callback_(false);
          }
          connected_ = false;
          break;
        }
        // n < 0 would be EAGAIN/EWOULDBLOCK for non-blocking, ignore
        // This is normal when no data available
      }
    }
  }
  
  std::atomic<bool> running_;
  std::atomic<bool> connected_;
  std::thread read_thread_;
  DataCallback data_callback_;
  ConnectionCallback connection_callback_;
};

/**
 * Factory function for stdio transport
 */
inline EchoTransportBasePtr createStdioTransport() {
  return std::make_unique<StdioTransport>();
}

} // namespace echo
} // namespace mcp

#endif // MCP_ECHO_STDIO_TRANSPORT_H