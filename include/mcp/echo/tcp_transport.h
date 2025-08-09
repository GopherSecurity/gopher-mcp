/**
 * @file tcp_transport.h
 * @brief TCP transport implementation for echo server/client
 * 
 * Thread Safety Design:
 * - Separate read thread for socket monitoring
 * - Non-blocking I/O with poll() for responsiveness
 * - Atomic flags for state management
 * - Thread-safe send() via socket with proper error handling
 * 
 * This provides TCP socket-based transport for the basic echo system,
 * enabling client-server communication over network connections.
 */

#ifndef MCP_ECHO_TCP_TRANSPORT_H
#define MCP_ECHO_TCP_TRANSPORT_H

#include <atomic>
#include <thread>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <cstring>

#include "mcp/echo/echo_basic.h"

namespace mcp {
namespace echo {

/**
 * TCP transport implementation for basic echo system
 * 
 * Supports both client and server modes:
 * - Client mode: Connects to specified host:port
 * - Server mode: Binds to port and accepts single connection
 * 
 * Key Design Decisions:
 * - Non-blocking sockets to avoid blocking on I/O
 * - Poll with timeout for responsive shutdown
 * - Separate read thread to avoid blocking main thread
 * - Automatic reconnection support for clients
 * - Proper socket cleanup and error handling
 * 
 * Address Format:
 * - Client: "tcp://host:port" (e.g., "tcp://localhost:8080")
 * - Server: "tcp://:port" or "tcp://0.0.0.0:port" (e.g., "tcp://:8080")
 */
class TCPTransport : public EchoTransportBase {
public:
  enum class Mode {
    Client,
    Server
  };
  
  explicit TCPTransport(Mode mode = Mode::Client)
      : mode_(mode), 
        running_(false), 
        connected_(false),
        socket_fd_(-1),
        server_fd_(-1) {}
  
  ~TCPTransport() override {
    stop();
  }
  
  void send(const std::string& data) override {
    if (!running_ || !connected_ || socket_fd_ == -1) {
      return;
    }
    
    // Send data via TCP socket
    ssize_t sent = ::send(socket_fd_, data.c_str(), data.length(), MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        // Connection error - trigger disconnection
        if (connection_callback_) {
          connection_callback_(false);
        }
        connected_ = false;
      }
    }
  }
  
  void setDataCallback(DataCallback callback) override {
    data_callback_ = callback;
  }
  
  void setConnectionCallback(ConnectionCallback callback) override {
    connection_callback_ = callback;
  }
  
  bool start() override;
  void stop() override;
  bool isConnected() const override { return connected_; }
  std::string getTransportType() const override { 
    return mode_ == Mode::Client ? "tcp-client" : "tcp-server"; 
  }
  
  // TCP-specific methods
  bool connect(const std::string& host, int port);
  bool listen(int port, const std::string& bind_address = "0.0.0.0");
  
private:
  void readLoop();
  bool setNonBlocking(int fd);
  bool parseAddress(const std::string& address, std::string& host, int& port);
  bool createClientSocket(const std::string& host, int port);
  bool createServerSocket(int port, const std::string& bind_address);
  bool acceptConnection();
  void closeSocket();
  
  Mode mode_;
  std::atomic<bool> running_;
  std::atomic<bool> connected_;
  std::thread read_thread_;
  
  int socket_fd_;           // Connected socket for I/O
  int server_fd_;           // Server listening socket (server mode only)
  
  std::string host_;        // Target host (client mode)
  int port_;               // Target/listening port
  std::string bind_address_; // Bind address (server mode)
  
  DataCallback data_callback_;
  ConnectionCallback connection_callback_;
};

/**
 * Factory functions for TCP transports
 */
inline EchoTransportBasePtr createTCPClientTransport() {
  return std::make_unique<TCPTransport>(TCPTransport::Mode::Client);
}

inline EchoTransportBasePtr createTCPServerTransport() {
  return std::make_unique<TCPTransport>(TCPTransport::Mode::Server);
}

} // namespace echo
} // namespace mcp

#endif // MCP_ECHO_TCP_TRANSPORT_H