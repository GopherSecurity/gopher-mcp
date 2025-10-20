#include "mcp/transport/stdio_pipe_transport.h"

#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <sys/select.h>
#include <unistd.h>
#include <vector>

#include "mcp/network/address.h"
#include "mcp/network/address_impl.h"

namespace mcp {
namespace transport {

// StdioPipeTransport implementation

StdioPipeTransport::StdioPipeTransport(const StdioPipeTransportConfig& config)
    : config_(config), connected_(false) {
  // Constructor
  read_buffer_ = std::make_unique<OwnedBuffer>();
  stdin_to_conn_pipe_[0] = -1;
  stdin_to_conn_pipe_[1] = -1;
  conn_to_stdout_pipe_[0] = -1;
  conn_to_stdout_pipe_[1] = -1;
}

StdioPipeTransport::~StdioPipeTransport() {
  // Signal threads to stop
  running_ = false;

  // Close write end of stdin pipe to signal EOF to the reader thread
  // This will wake up the bridgeStdinToPipe thread if it's blocked on write()
  if (stdin_to_conn_pipe_[1] != -1) {
    ::close(stdin_to_conn_pipe_[1]);
    stdin_to_conn_pipe_[1] = -1;
  }

  // Close read end of stdout pipe to signal EOF to the writer thread
  // This will wake up the bridgePipeToStdout thread if it's blocked on read()
  if (conn_to_stdout_pipe_[0] != -1) {
    ::close(conn_to_stdout_pipe_[0]);
    conn_to_stdout_pipe_[0] = -1;
  }

  // Wait for threads to finish if they're joinable
  // The threads should exit cleanly after we closed the pipes above
  if (stdin_bridge_thread_.joinable()) {
    stdin_bridge_thread_.join();
  }
  if (stdout_bridge_thread_.joinable()) {
    stdout_bridge_thread_.join();
  }

  // Close remaining pipe ends that weren't transferred to ConnectionSocketImpl
  // Only close if fd != -1 (i.e., not transferred via takePipeSocket)
  if (stdin_to_conn_pipe_[0] != -1) {
    ::close(stdin_to_conn_pipe_[0]);
    stdin_to_conn_pipe_[0] = -1;
  }
  if (conn_to_stdout_pipe_[1] != -1) {
    ::close(conn_to_stdout_pipe_[1]);
    conn_to_stdout_pipe_[1] = -1;
  }
}

VoidResult StdioPipeTransport::initialize() {
  // Create pipes
  if (::pipe(stdin_to_conn_pipe_) == -1) {
    failure_reason_ = "Failed to create stdin pipe: ";
    failure_reason_ += strerror(errno);
    Error err;
    err.code = errno;
    err.message = failure_reason_;
    return makeVoidError(err);
  }

  if (::pipe(conn_to_stdout_pipe_) == -1) {
    ::close(stdin_to_conn_pipe_[0]);
    ::close(stdin_to_conn_pipe_[1]);
    failure_reason_ = "Failed to create stdout pipe: ";
    failure_reason_ += strerror(errno);
    Error err;
    err.code = errno;
    err.message = failure_reason_;
    return makeVoidError(err);
  }

  // Set non-blocking mode on the pipes that ConnectionImpl will use
  if (config_.non_blocking) {
    setNonBlocking(stdin_to_conn_pipe_[0]);   // Read end for ConnectionImpl
    setNonBlocking(conn_to_stdout_pipe_[1]);  // Write end for ConnectionImpl
  }

  // Create custom PipeIoHandle that supports separate read/write FDs
  // Flow: stdin_to_conn_pipe_[0] for reading, conn_to_stdout_pipe_[1] for
  // writing
  auto io_handle = std::make_unique<PipeIoHandle>(stdin_to_conn_pipe_[0],
                                                  conn_to_stdout_pipe_[1]);

  // Verify the handle was created successfully
  if (!io_handle || !io_handle->isOpen()) {
    ::close(stdin_to_conn_pipe_[0]);
    ::close(stdin_to_conn_pipe_[1]);
    ::close(conn_to_stdout_pipe_[0]);
    ::close(conn_to_stdout_pipe_[1]);
    failure_reason_ = "Failed to create IO handle for pipes";
    Error err;
    err.code = -1;
    err.message = failure_reason_;
    return makeVoidError(err);
  }

  // Create pipe addresses
  auto local_address =
      std::make_shared<network::Address::PipeInstance>("/tmp/mcp_stdio_in");
  auto remote_address =
      std::make_shared<network::Address::PipeInstance>("/tmp/mcp_stdio_out");

  // Create the connection socket that ConnectionImpl will use
  // IMPORTANT: The io_handle takes ownership of stdin_to_conn_pipe_[0] and
  // conn_to_stdout_pipe_[1] These fds will be managed by ConnectionSocketImpl
  // and closed when it's destroyed We must NOT close these fds in our
  // destructor to avoid double-close errors
  pipe_socket_ = std::make_unique<network::ConnectionSocketImpl>(
      std::move(io_handle), local_address, remote_address);

  // Start bridge threads
  running_ = true;

  // Capture FD values to avoid race conditions
  int stdin_fd = config_.stdin_fd;
  int write_pipe_fd = stdin_to_conn_pipe_[1];
  int read_pipe_fd = conn_to_stdout_pipe_[0];
  int stdout_fd = config_.stdout_fd;

  stdin_bridge_thread_ = std::thread([this, stdin_fd, write_pipe_fd]() {
    bridgeStdinToPipe(stdin_fd, write_pipe_fd, &running_);
  });

  stdout_bridge_thread_ = std::thread([this, read_pipe_fd, stdout_fd]() {
    bridgePipeToStdout(read_pipe_fd, stdout_fd, &running_);
  });

  return makeVoidSuccess();
}

std::unique_ptr<network::ConnectionSocketImpl>
StdioPipeTransport::takePipeSocket() {
  // CRITICAL: File Descriptor Ownership Transfer
  // =============================================
  // This function transfers ownership of specific pipe FDs to ConnectionImpl
  //
  // FD Ownership After This Call:
  // - stdin_to_conn_pipe_[0]: Owned by ConnectionSocketImpl (for reading)
  // - stdin_to_conn_pipe_[1]: Still owned by us (bridge thread writes here)
  // - conn_to_stdout_pipe_[0]: Still owned by us (bridge thread reads here)
  // - conn_to_stdout_pipe_[1]: Owned by ConnectionSocketImpl (for writing)
  //
  // Why mark FDs as -1?
  // - Prevents double-close in destructor
  // - ConnectionSocketImpl's IoHandle will close them
  // - Bridge threads captured FDs by value, so they're unaffected

  if (pipe_socket_) {
    // Mark transferred FDs as invalid to prevent double-close
    stdin_to_conn_pipe_[0] = -1;   // Now owned by ConnectionSocketImpl
    conn_to_stdout_pipe_[1] = -1;  // Now owned by ConnectionSocketImpl
  }
  return std::move(pipe_socket_);
}

void StdioPipeTransport::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

VoidResult StdioPipeTransport::connect(network::Socket& socket) {
  (void)socket;
  // IMPORTANT: Don't set connected_ = true here
  // ==========================================
  // Reason: If we set connected_ here, closeSocket() won't return early
  // when called during initialization, which would set running_ = false
  // and prevent bridge threads from working.
  //
  // The correct flow is:
  // 1. connect() is called (we return success but don't set connected_)
  // 2. onConnected() is called later (sets connected_ = true)
  // 3. This ensures closeSocket() returns early if called before onConnected()
  return makeVoidSuccess();
}

void StdioPipeTransport::closeSocket(network::ConnectionEvent event) {
  if (!connected_) {
    return;
  }

  connected_ = false;
  running_ = false;

  // Close write end of stdin pipe to signal EOF to ConnectionImpl
  if (stdin_to_conn_pipe_[1] != -1) {
    ::close(stdin_to_conn_pipe_[1]);
    stdin_to_conn_pipe_[1] = -1;
  }

  // Notify callbacks
  if (callbacks_) {
    callbacks_->raiseEvent(event);
  }
}

TransportIoResult StdioPipeTransport::doRead(Buffer& buffer) {
  // This is called by ConnectionImpl to read data from the transport
  // We read from the callbacks' io_handle which has the pipe FD

  if (!callbacks_) {
    return TransportIoResult::stop();
  }

  const size_t max_slice_size = 65536;  // 64KB at a time
  RawSlice slice;

  // Reserve space in the buffer
  void* mem = buffer.reserveSingleSlice(max_slice_size, slice);
  if (!mem) {
    return TransportIoResult::stop();
  }

  // Read from the io_handle (which has stdin_to_conn_pipe_[0])
  network::IoHandle& io_handle = callbacks_->ioHandle();
  auto result = io_handle.readv(slice.len_, &slice, 1);

  if (!result.ok()) {
    buffer.commit(slice, 0);

    // Handle would-block (EAGAIN/EWOULDBLOCK)
    // This is NOT an error or EOF - it just means no data is available right
    // now The event loop will trigger another read when data becomes available
    if (result.wouldBlock()) {
      return TransportIoResult::stop();
    }

    // Handle connection reset
    if (result.error_code() == ECONNRESET) {
      return TransportIoResult::close();
    }

    // Other errors
    failure_reason_ =
        "Read error: " + std::string(strerror(result.error_code()));
    Error err;
    err.code = result.error_code();
    err.message = failure_reason_;
    return TransportIoResult::error(err);
  }

  size_t bytes_read = *result;

  // Handle EOF - for pipes, this means the write end was closed
  // IMPORTANT: Only treat 0 bytes as EOF if read() actually returned 0,
  // not if it returned -1 with EAGAIN (which is handled above)
  if (bytes_read == 0) {
    buffer.commit(slice, 0);
    return TransportIoResult::endStream(0);
  }

  // Commit the read data
  buffer.commit(slice, bytes_read);

  // Mark socket as readable if edge-triggered
  callbacks_->setTransportSocketIsReadable();

  return TransportIoResult::success(bytes_read);
}

TransportIoResult StdioPipeTransport::doWrite(Buffer& buffer, bool end_stream) {
  // This is called by ConnectionImpl to write data to the transport
  // Flow: ConnectionImpl -> doWrite() -> io_handle.writev() ->
  // conn_to_stdout_pipe_[1]
  //       -> bridge thread -> stdout_fd

  (void)end_stream;

  if (!callbacks_) {
    return TransportIoResult::stop();
  }

  // Get slices to write
  constexpr size_t max_slices = 16;
  ConstRawSlice slices[max_slices];
  const size_t num_slices = buffer.getRawSlices(slices, max_slices);

  if (num_slices == 0) {
    return TransportIoResult::success(0);
  }

  // Write to the io_handle (which has conn_to_stdout_pipe_[1])
  network::IoHandle& io_handle = callbacks_->ioHandle();
  auto result = io_handle.writev(slices, num_slices);

  if (!result.ok()) {
    // Handle would-block
    if (result.wouldBlock()) {
      return TransportIoResult::stop();
    }

    // Handle broken pipe
    if (result.error_code() == EPIPE) {
      return TransportIoResult::close();
    }

    // Other errors
    failure_reason_ =
        "Write error: " + std::string(strerror(result.error_code()));
    Error err;
    err.code = result.error_code();
    err.message = failure_reason_;
    return TransportIoResult::error(err);
  }

  size_t bytes_written = *result;
  buffer.drain(bytes_written);

  return TransportIoResult::success(bytes_written);
}

void StdioPipeTransport::onConnected() {
  connected_ = true;

  // Signal that there might be data available to read
  if (callbacks_) {
    callbacks_->setTransportSocketIsReadable();
  }
}

void StdioPipeTransport::bridgeStdinToPipe(int stdin_fd,
                                           int write_pipe_fd,
                                           std::atomic<bool>* running) {
  // Bridge Thread: Transfers data from stdin to internal pipe
  // ============================================================
  // Purpose: This thread reads from stdin (which may block) and writes to the
  // pipe
  //          that ConnectionImpl reads from (non-blocking)
  //
  // Why a separate thread?
  // - stdin is often a blocking file descriptor (terminal, file, etc.)
  // - ConnectionImpl needs non-blocking I/O for its event-driven model
  // - The bridge thread handles the blocking read, allowing ConnectionImpl to
  // use events
  //
  // Flow: stdin_fd -> [select with timeout] -> [read] -> buffer -> [write] ->
  // write_pipe_fd -> ConnectionImpl
  //
  // Use select() with timeout to avoid blocking indefinitely and allow
  // periodic checks of the *running flag

  std::vector<char> buffer(config_.buffer_size);

  while (*running) {
    // Use select() with 100ms timeout to wait for data while remaining responsive
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(stdin_fd, &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms timeout

    int select_result = ::select(stdin_fd + 1, &readfds, nullptr, nullptr, &tv);

    if (select_result < 0) {
      // select() error
      int err = errno;
      if (err != EINTR) {
        failure_reason_ = "Error in select(): ";
        failure_reason_ += strerror(err);
        break;
      }
      // EINTR means interrupted by signal, just retry
      continue;
    }

    if (select_result == 0) {
      // Timeout - no data available, loop back to check *running
      continue;
    }

    // Data is available, read it
    ssize_t bytes_read = ::read(stdin_fd, buffer.data(), buffer.size());

    if (bytes_read > 0) {
      // Write all data to the pipe
      size_t total_written = 0;
      while (total_written < static_cast<size_t>(bytes_read) && *running) {
        ssize_t bytes_written =
            ::write(write_pipe_fd, buffer.data() + total_written,
                    bytes_read - total_written);

        if (bytes_written > 0) {
          total_written += bytes_written;
        } else if (bytes_written == -1) {
          int err = errno;
          if (err != EAGAIN && err != EWOULDBLOCK) {
            // Error writing to pipe
            failure_reason_ = "Error writing to stdin pipe: ";
            failure_reason_ += strerror(err);
            *running = false;
            break;
          }
          // Otherwise, retry
          usleep(1000);  // Sleep 1ms
        }
      }
    } else if (bytes_read == 0) {
      // EOF on stdin
      break;
    } else {
      // Error reading from stdin
      int err = errno;
      if (err != EAGAIN && err != EWOULDBLOCK) {
        failure_reason_ = "Error reading from stdin: ";
        failure_reason_ += strerror(err);
        break;
      }
      // Otherwise, retry via select() timeout
    }
  }

  // Close write end of pipe to signal EOF
  if (write_pipe_fd != -1) {
    ::close(write_pipe_fd);
    // Note: Don't update member variable since it may be accessed elsewhere
  }
}

void StdioPipeTransport::bridgePipeToStdout(int read_pipe_fd,
                                            int stdout_fd,
                                            std::atomic<bool>* running) {
  // Bridge Thread: Transfers data from internal pipe to stdout
  // Flow: ConnectionImpl -> conn_to_stdout_pipe_[1] -> conn_to_stdout_pipe_[0]
  // -> stdout_fd
  //
  // Use select() with timeout to wait for data while remaining responsive
  // This allows the thread to exit promptly when *running becomes false

  std::vector<char> buffer(config_.buffer_size);

  while (*running) {
    // Use select() with 100ms timeout to wait for data while remaining responsive
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(read_pipe_fd, &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms timeout

    int select_result = ::select(read_pipe_fd + 1, &readfds, nullptr, nullptr, &tv);

    if (select_result < 0) {
      // select() error
      int err = errno;
      if (err != EINTR) {
        failure_reason_ = "Error in select(): ";
        failure_reason_ += strerror(err);
        break;
      }
      // EINTR means interrupted by signal, just retry
      continue;
    }

    if (select_result == 0) {
      // Timeout - no data available, loop back to check *running
      continue;
    }

    // Data is available, read it
    ssize_t bytes_read = ::read(read_pipe_fd, buffer.data(), buffer.size());

    if (bytes_read > 0) {
      // Write all data to stdout
      size_t total_written = 0;
      while (total_written < static_cast<size_t>(bytes_read) && *running) {
        ssize_t bytes_written =
            ::write(stdout_fd, buffer.data() + total_written,
                    bytes_read - total_written);

        if (bytes_written > 0) {
          total_written += bytes_written;

          // Flush stdout after each write to ensure data is sent immediately
          // This is critical for tests to receive the data
          if (stdout_fd == 1) {
            fflush(stdout);
          }
        } else if (bytes_written == -1) {
          int err = errno;
          if (err != EAGAIN && err != EWOULDBLOCK) {
            // Error writing to stdout
            failure_reason_ = "Error writing to stdout: ";
            failure_reason_ += strerror(err);
            *running = false;
            break;
          }
          // Otherwise, retry
          usleep(1000);  // Sleep 1ms
        }
      }
    } else if (bytes_read == 0) {
      // EOF on pipe (ConnectionImpl closed)
      break;
    } else {
      // Error reading from pipe
      int err = errno;
      if (err != EAGAIN && err != EWOULDBLOCK) {
        failure_reason_ = "Error reading from stdout pipe: ";
        failure_reason_ += strerror(err);
        break;
      }
      // Otherwise, retry via select() timeout
    }
  }

  // Close read end of pipe
  if (read_pipe_fd != -1) {
    ::close(read_pipe_fd);
    // Note: Don't update member variable since it may be accessed elsewhere
  }
}

void StdioPipeTransport::setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    failure_reason_ = "Failed to get file descriptor flags";
    return;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    failure_reason_ = "Failed to set non-blocking mode";
  }
}

// StdioPipeTransportFactory implementation

StdioPipeTransportFactory::StdioPipeTransportFactory(
    const StdioPipeTransportConfig& config)
    : config_(config) {}

network::TransportSocketPtr StdioPipeTransportFactory::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  (void)options;
  return std::make_unique<StdioPipeTransport>(config_);
}

void StdioPipeTransportFactory::hashKey(
    std::vector<uint8_t>& key,
    network::TransportSocketOptionsSharedPtr options) const {
  // Add factory identifier
  const std::string factory_name = "stdio_pipe";
  key.insert(key.end(), factory_name.begin(), factory_name.end());

  // Add config values
  key.push_back(static_cast<uint8_t>(config_.stdin_fd));
  key.push_back(static_cast<uint8_t>(config_.stdout_fd));
  key.push_back(config_.non_blocking ? 1 : 0);

  (void)options;
}

network::TransportSocketPtr StdioPipeTransportFactory::createTransportSocket()
    const {
  return std::make_unique<StdioPipeTransport>(config_);
}

}  // namespace transport
}  // namespace mcp