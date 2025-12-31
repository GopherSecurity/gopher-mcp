#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

// Platform-specific includes - must come before mcp headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef _SOCKLEN_T_DEFINED
#define _SOCKLEN_T_DEFINED
typedef int socklen_t;
#endif
#else
#include <fcntl.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "mcp/event/event_loop.h"
#include "mcp/event/libevent_dispatcher.h"

using namespace mcp::event;
using namespace std::chrono_literals;

// Platform-specific socket helpers (os_fd_t is defined in event_loop.h)
namespace {
#ifdef _WIN32
constexpr os_fd_t INVALID_SOCKET_FD = INVALID_SOCKET;
inline void close_socket(os_fd_t s) { closesocket(s); }
inline int get_socket_error() { return WSAGetLastError(); }
inline bool is_would_block(int err) { return err == WSAEWOULDBLOCK; }
inline void set_nonblocking(os_fd_t fd) {
  u_long mode = 1;
  ioctlsocket(fd, FIONBIO, &mode);
}
#else
constexpr os_fd_t INVALID_SOCKET_FD = -1;
inline void close_socket(os_fd_t s) { close(s); }
inline int get_socket_error() { return errno; }
inline bool is_would_block(int err) {
  return err == EAGAIN || err == EWOULDBLOCK;
}
inline void set_nonblocking(os_fd_t fd) { fcntl(fd, F_SETFL, O_NONBLOCK); }
#endif
}  // namespace

/**
 * Simple echo server example using the MCP event loop
 */
class EchoServer {
 public:
  EchoServer(Dispatcher& dispatcher, int port)
      : dispatcher_(dispatcher), port_(port) {}

  bool start() {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
      std::cerr << "WSAStartup failed\n";
      return false;
    }
#endif

    // Create server socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == INVALID_SOCKET_FD) {
      std::cerr << "Failed to create socket\n";
      return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
      std::cerr << "Failed to set socket options\n";
      return false;
    }

    // Make non-blocking
    set_nonblocking(server_fd_);

    // Bind
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, reinterpret_cast<::sockaddr*>(&addr), sizeof(addr)) <
        0) {
      std::cerr << "Failed to bind to port " << port_ << "\n";
      return false;
    }

    // Listen
    if (listen(server_fd_, 10) < 0) {
      std::cerr << "Failed to listen\n";
      return false;
    }

    std::cout << "Echo server listening on port " << port_ << "\n";

    // Create file event for accepting connections
    accept_event_ = dispatcher_.createFileEvent(
        server_fd_, [this](uint32_t events) { handleAccept(events); },
        FileTriggerType::Level, static_cast<uint32_t>(FileReadyType::Read));

    return true;
  }

  void stop() {
    accept_event_.reset();
    connections_.clear();
    if (server_fd_ != INVALID_SOCKET_FD) {
      close_socket(server_fd_);
      server_fd_ = INVALID_SOCKET_FD;
    }
#ifdef _WIN32
    WSACleanup();
#endif
  }

 private:
  struct Connection {
    os_fd_t fd;
    FileEventPtr event;
    std::string buffer;
  };

  void handleAccept(uint32_t events) {
    if (!(events & static_cast<uint32_t>(FileReadyType::Read))) {
      return;
    }

    while (true) {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);

      os_fd_t client_fd = accept(
          server_fd_, reinterpret_cast<::sockaddr*>(&client_addr), &client_len);
      if (client_fd == INVALID_SOCKET_FD) {
        if (is_would_block(get_socket_error())) {
          break;  // No more connections
        }
        std::cerr << "Accept failed\n";
        break;
      }

      // Make non-blocking
      set_nonblocking(client_fd);

      std::cout << "New connection from " << inet_ntoa(client_addr.sin_addr)
                << ":" << ntohs(client_addr.sin_port) << "\n";

      // Create connection object
      auto conn = std::make_shared<Connection>();
      conn->fd = client_fd;

      // Create file event for this connection
      conn->event = dispatcher_.createFileEvent(
          client_fd,
          [this, conn](uint32_t events) { handleConnection(conn, events); },
          FileTriggerType::Level,
          static_cast<uint32_t>(FileReadyType::Read | FileReadyType::Write));

      connections_[client_fd] = conn;
    }
  }

  void handleConnection(std::shared_ptr<Connection> conn, uint32_t events) {
    if (events & static_cast<uint32_t>(FileReadyType::Read)) {
      char buffer[1024];
      while (true) {
#ifdef _WIN32
        int n = recv(conn->fd, buffer, sizeof(buffer), 0);
#else
        ssize_t n = read(conn->fd, buffer, sizeof(buffer));
#endif
        if (n > 0) {
          // Echo back
          conn->buffer.append(buffer, n);
        } else if (n == 0) {
          // Connection closed
          std::cout << "Connection closed\n";
          connections_.erase(conn->fd);
          close_socket(conn->fd);
          return;
        } else {
          if (is_would_block(get_socket_error())) {
            break;  // No more data
          }
          // Error
          std::cerr << "Read error\n";
          connections_.erase(conn->fd);
          close_socket(conn->fd);
          return;
        }
      }
    }

    if (events & static_cast<uint32_t>(FileReadyType::Write) &&
        !conn->buffer.empty()) {
      while (!conn->buffer.empty()) {
#ifdef _WIN32
        int n = send(conn->fd, conn->buffer.data(),
                     static_cast<int>(conn->buffer.size()), 0);
#else
        ssize_t n = write(conn->fd, conn->buffer.data(), conn->buffer.size());
#endif
        if (n > 0) {
          conn->buffer.erase(0, n);
        } else {
          if (is_would_block(get_socket_error())) {
            break;  // Can't write more now
          }
          // Error
          std::cerr << "Write error\n";
          connections_.erase(conn->fd);
          close_socket(conn->fd);
          return;
        }
      }

      // Disable write events if buffer is empty
      if (conn->buffer.empty()) {
        conn->event->setEnabled(static_cast<uint32_t>(FileReadyType::Read));
      }
    }
  }

  Dispatcher& dispatcher_;
  int port_;
  os_fd_t server_fd_ = INVALID_SOCKET_FD;
  FileEventPtr accept_event_;
  std::unordered_map<os_fd_t, std::shared_ptr<Connection>> connections_;
};

int main(int argc, char* argv[]) {
  // Parse command line
  int port = 8080;
  int num_workers = 4;

  if (argc > 1) {
    port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    num_workers = std::atoi(argv[2]);
  }

  std::cout << "Starting echo server with " << num_workers << " workers\n";

  // Create event loop components
  auto dispatcher_factory = createLibeventDispatcherFactory();
  auto worker_factory = createDefaultWorkerFactory();
  auto thread_pool = createThreadPool();

  // Initialize thread pool
  thread_pool->initialize(num_workers, *dispatcher_factory, *worker_factory);

  // Create echo servers on each worker
  std::vector<std::unique_ptr<EchoServer>> servers;
  for (size_t i = 0; i < thread_pool->size(); ++i) {
    auto& worker = thread_pool->getWorker(i);
    auto server = std::make_unique<EchoServer>(worker.dispatcher(), port + i);

    // Start server in worker thread
    worker.dispatcher().post([&server]() {
      if (!server->start()) {
        std::cerr << "Failed to start server\n";
      }
    });

    servers.push_back(std::move(server));
  }

  // Start all workers
  thread_pool->start();

  std::cout << "Echo servers running. Press Ctrl+C to stop.\n";
  std::cout << "Try: echo 'Hello World' | nc localhost " << port << "\n";

  // Set up signal handler for graceful shutdown
  signal(SIGINT, [](int) {
    std::cout << "\nShutting down...\n";
    exit(0);
  });

  // Create a timer in the main thread for periodic stats
  auto main_dispatcher = dispatcher_factory->createDispatcher("main");
  auto stats_timer = main_dispatcher->createTimer([&]() {
    static int count = 0;
    std::cout << "Stats update " << ++count << ": All systems running\n";
  });
  stats_timer->enableTimer(10s);

  // Run main event loop
  main_dispatcher->run(RunType::Block);

  // Cleanup
  thread_pool->stop();

  return 0;
}