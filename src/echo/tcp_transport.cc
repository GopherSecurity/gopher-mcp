/**
 * @file tcp_transport.cc
 * @brief Implementation of TCP transport for echo client/server
 */

#include "mcp/echo/tcp_transport.h"
#include <iostream>
#include <sstream>

namespace mcp {
namespace echo {

bool TCPTransport::start() {
  if (running_) {
    return true;
  }
  
  // Parse and validate configuration first
  if (mode_ == Mode::Client && (host_.empty() || port_ <= 0)) {
    std::cerr << "[ERROR] TCP Client: Host and port must be set before starting" << std::endl;
    return false;
  }
  
  if (mode_ == Mode::Server && port_ <= 0) {
    std::cerr << "[ERROR] TCP Server: Port must be set before starting" << std::endl;
    return false;
  }
  
  running_ = true;
  
  // Start read thread which will handle connection setup
  read_thread_ = std::thread([this]() {
    readLoop();
  });
  
  return true;
}

void TCPTransport::stop() {
  if (!running_) {
    return;
  }
  
  running_ = false;
  
  // Close sockets to interrupt poll()
  closeSocket();
  
  // Wait for read thread
  if (read_thread_.joinable()) {
    read_thread_.join();
  }
  
  // Notify disconnection if we were connected
  if (connected_ && connection_callback_) {
    connection_callback_(false);
  }
  
  connected_ = false;
}

bool TCPTransport::connect(const std::string& host, int port) {
  if (mode_ != Mode::Client) {
    std::cerr << "[ERROR] connect() only valid for client mode" << std::endl;
    return false;
  }
  
  host_ = host;
  port_ = port;
  return true;
}

bool TCPTransport::listen(int port, const std::string& bind_address) {
  if (mode_ != Mode::Server) {
    std::cerr << "[ERROR] listen() only valid for server mode" << std::endl;
    return false;
  }
  
  port_ = port;
  bind_address_ = bind_address;
  return true;
}

void TCPTransport::readLoop() {
  const int RECONNECT_DELAY_MS = 5000; // 5 second reconnect delay
  
  while (running_) {
    if (!connected_) {
      // Attempt connection/accept
      bool success = false;
      
      if (mode_ == Mode::Client) {
        success = createClientSocket(host_, port_);
      } else {
        success = createServerSocket(port_, bind_address_) && acceptConnection();
      }
      
      if (success) {
        connected_ = true;
        if (connection_callback_) {
          connection_callback_(true);
        }
        std::cerr << "[INFO] TCP " << (mode_ == Mode::Client ? "Client" : "Server") 
                  << ": Connected" << std::endl;
      } else {
        // Wait before retrying
        std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
        continue;
      }
    }
    
    // Poll for data
    struct pollfd pfd;
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;
    
    int ret = poll(&pfd, 1, 100); // 100ms timeout for responsive shutdown
    
    if (ret > 0 && (pfd.revents & POLLIN)) {
      char buffer[8192];
      ssize_t n = recv(socket_fd_, buffer, sizeof(buffer), 0);
      
      if (n > 0) {
        // Data received
        if (data_callback_) {
          data_callback_(std::string(buffer, n));
        }
      } else if (n == 0) {
        // Connection closed by peer
        std::cerr << "[INFO] TCP: Connection closed by peer" << std::endl;
        if (connection_callback_) {
          connection_callback_(false);
        }
        connected_ = false;
        closeSocket();
      } else {
        // Error occurred
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          std::cerr << "[ERROR] TCP recv error: " << strerror(errno) << std::endl;
          if (connection_callback_) {
            connection_callback_(false);
          }
          connected_ = false;
          closeSocket();
        }
      }
    } else if (ret < 0) {
      // Poll error
      std::cerr << "[ERROR] TCP poll error: " << strerror(errno) << std::endl;
      if (connection_callback_) {
        connection_callback_(false);
      }
      connected_ = false;
      closeSocket();
    }
    
    // Check for socket errors
    if (connected_ && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
      std::cerr << "[INFO] TCP: Socket error detected" << std::endl;
      if (connection_callback_) {
        connection_callback_(false);
      }
      connected_ = false;
      closeSocket();
    }
  }
}

bool TCPTransport::setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    std::cerr << "[ERROR] TCP: Failed to get socket flags: " << strerror(errno) << std::endl;
    return false;
  }
  
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    std::cerr << "[ERROR] TCP: Failed to set non-blocking: " << strerror(errno) << std::endl;
    return false;
  }
  
  return true;
}

bool TCPTransport::createClientSocket(const std::string& host, int port) {
  // Create socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    std::cerr << "[ERROR] TCP Client: Failed to create socket: " << strerror(errno) << std::endl;
    return false;
  }
  
  // Set non-blocking
  if (!setNonBlocking(sock)) {
    close(sock);
    return false;
  }
  
  // Setup server address
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  
  // Convert hostname to IP
  if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
    std::cerr << "[ERROR] TCP Client: Invalid address: " << host << std::endl;
    close(sock);
    return false;
  }
  
  // Connect (non-blocking)
  int result = ::connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
  if (result < 0 && errno != EINPROGRESS) {
    std::cerr << "[ERROR] TCP Client: Connect failed: " << strerror(errno) << std::endl;
    close(sock);
    return false;
  }
  
  // Wait for connection to complete (if EINPROGRESS)
  if (result < 0 && errno == EINPROGRESS) {
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLOUT;
    
    int poll_result = poll(&pfd, 1, 5000); // 5 second timeout
    if (poll_result <= 0) {
      std::cerr << "[ERROR] TCP Client: Connection timeout to " << host << ":" << port << std::endl;
      close(sock);
      return false;
    }
    
    // Check if connection succeeded
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
      std::cerr << "[ERROR] TCP Client: Connection failed to " << host << ":" << port 
                << ": " << strerror(error ? error : errno) << std::endl;
      close(sock);
      return false;
    }
  }
  
  socket_fd_ = sock;
  return true;
}

bool TCPTransport::createServerSocket(int port, const std::string& bind_address) {
  // Create server socket if not already created
  if (server_fd_ == -1) {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
      std::cerr << "[ERROR] TCP Server: Failed to create socket: " << strerror(errno) << std::endl;
      return false;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      std::cerr << "[ERROR] TCP Server: Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
      close(server_fd_);
      server_fd_ = -1;
      return false;
    }
    
    // Set non-blocking
    if (!setNonBlocking(server_fd_)) {
      close(server_fd_);
      server_fd_ = -1;
      return false;
    }
    
    // Bind
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (bind_address == "0.0.0.0" || bind_address.empty()) {
      server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
      if (inet_pton(AF_INET, bind_address.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[ERROR] TCP Server: Invalid bind address: " << bind_address << std::endl;
        close(server_fd_);
        server_fd_ = -1;
        return false;
      }
    }
    
    if (bind(server_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
      std::cerr << "[ERROR] TCP Server: Bind failed on port " << port 
                << ": " << strerror(errno) << std::endl;
      close(server_fd_);
      server_fd_ = -1;
      return false;
    }
    
    // Listen
    if (::listen(server_fd_, 1) < 0) {
      std::cerr << "[ERROR] TCP Server: Listen failed: " << strerror(errno) << std::endl;
      close(server_fd_);
      server_fd_ = -1;
      return false;
    }
    
    std::cerr << "[INFO] TCP Server: Listening on " << bind_address << ":" << port << std::endl;
  }
  
  return true;
}

bool TCPTransport::acceptConnection() {
  if (server_fd_ == -1) {
    return false;
  }
  
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  
  int client_sock = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
  if (client_sock < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      std::cerr << "[ERROR] TCP Server: Accept failed: " << strerror(errno) << std::endl;
    }
    return false;
  }
  
  // Set client socket non-blocking
  if (!setNonBlocking(client_sock)) {
    close(client_sock);
    return false;
  }
  
  // Get client info
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
  std::cerr << "[INFO] TCP Server: Accepted connection from " << client_ip 
            << ":" << ntohs(client_addr.sin_port) << std::endl;
  
  socket_fd_ = client_sock;
  return true;
}

void TCPTransport::closeSocket() {
  if (socket_fd_ != -1) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
  
  if (server_fd_ != -1) {
    close(server_fd_);
    server_fd_ = -1;
  }
}

} // namespace echo
} // namespace mcp