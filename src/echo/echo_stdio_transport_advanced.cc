/**
 * @file echo_stdio_transport_advanced.cc
 * @brief Stdio transport implementation for echo client/server
 */

#include "mcp/echo/echo_stdio_transport_advanced.h"

#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace mcp {
namespace echo {

StdioEchoTransport::StdioEchoTransport() {
  stdin_fd_ = STDIN_FILENO;
  stdout_fd_ = STDOUT_FILENO;
}

StdioEchoTransport::~StdioEchoTransport() { close(); }

variant<Success, Error> StdioEchoTransport::initialize() {
  if (initialized_) {
    return Error(-1, "Transport already initialized");
  }

  // Set stdin to non-blocking mode for reading
  setNonBlocking(stdin_fd_);

  initialized_ = true;
  return Success{};
}

variant<Success, Error> StdioEchoTransport::connect(
    const std::string& endpoint) {
  if (!initialized_) {
    return Error(-1, "Transport not initialized");
  }

  if (running_) {
    return Error(-1, "Already connected");
  }

  running_ = true;
  status_ = Status::Connected;

  // Start read thread
  read_thread_ = std::thread(&StdioEchoTransport::readThread, this);

  // Notify connection
  if (callbacks_.onStatusChange) {
    callbacks_.onStatusChange(Status::Connected);
  }

  return Success{};
}

variant<Success, Error> StdioEchoTransport::listen(
    const std::string& endpoint) {
  // For stdio, listen is the same as connect
  return connect(endpoint);
}

variant<Success, Error> StdioEchoTransport::send(const std::string& data) {
  if (!running_) {
    return Error(-1, "Not connected");
  }

  std::lock_guard<std::mutex> lock(write_mutex_);

  size_t total_written = 0;
  while (total_written < data.length()) {
    ssize_t written = write(stdout_fd_, data.c_str() + total_written,
                            data.length() - total_written);

    if (written < 0) {
      if (errno == EINTR) {
        continue;  // Interrupted, retry
      }
      return Error(errno, std::string("Write failed: ") + strerror(errno));
    }

    total_written += written;
  }

  // Flush stdout to ensure immediate delivery
  if (fflush(stdout) != 0) {
    return Error(errno, std::string("Flush failed: ") + strerror(errno));
  }

  return Success{};
}

void StdioEchoTransport::close() {
  if (!running_) {
    return;
  }

  running_ = false;

  if (read_thread_.joinable()) {
    read_thread_.join();
  }

  status_ = Status::Disconnected;

  // Notify disconnection
  if (callbacks_.onStatusChange) {
    callbacks_.onStatusChange(Status::Disconnected);
  }
}

EchoTransportAdvanced::Status StdioEchoTransport::getStatus() const {
  return status_;
}

void StdioEchoTransport::setCallbacks(const Callbacks& callbacks) {
  callbacks_ = callbacks;
}

void StdioEchoTransport::readThread() {
  char buffer[4096];
  std::string partial_data;

  while (running_) {
    ssize_t bytes_read = read(stdin_fd_, buffer, sizeof(buffer));

    if (bytes_read > 0) {
      std::string data(buffer, bytes_read);

      // Notify data received
      if (callbacks_.onDataReceived) {
        callbacks_.onDataReceived(data);
      }
    } else if (bytes_read == 0) {
      // EOF reached
      break;
    } else {
      // Error or would block
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Non-blocking read, no data available
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      } else if (errno == EINTR) {
        // Interrupted, retry
        continue;
      } else {
        // Real error
        if (callbacks_.onError) {
          callbacks_.onError(
              Error(errno, std::string("Read failed: ") + strerror(errno)));
        }
        break;
      }
    }
  }

  // Connection closed
  status_ = Status::Disconnected;
  if (callbacks_.onStatusChange) {
    callbacks_.onStatusChange(Status::Disconnected);
  }
}

void StdioEchoTransport::setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags != -1) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

}  // namespace echo
}  // namespace mcp