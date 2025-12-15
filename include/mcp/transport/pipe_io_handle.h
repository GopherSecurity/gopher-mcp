#pragma once

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "mcp/network/io_socket_handle_impl.h"

namespace mcp {
namespace transport {

/**
 * PipeIoHandle - Simplified IoHandle for pipes with separate read/write FDs
 *
 * This class extends IoSocketHandleImpl and overrides just the methods needed
 * to handle separate read and write file descriptors for pipes.
 */
class PipeIoHandle : public network::IoSocketHandleImpl {
 public:
  PipeIoHandle(int read_fd, int write_fd)
      : IoSocketHandleImpl(read_fd),  // Use read_fd as the primary FD
        write_fd_(write_fd) {}

  ~PipeIoHandle() override {
    // Close write FD if still open
    if (write_fd_ >= 0) {
#ifdef _WIN32
      closesocket(write_fd_);
#else
      ::close(write_fd_);
#endif
      write_fd_ = -1;
    }
  }

  // Override writev to use the write FD instead of the read FD
  network::IoCallResult writev(const ConstRawSlice* slices,
                               size_t num_slices) override {
    if (write_fd_ < 0) {
      return network::IoCallResult::error(EBADF);
    }

    if (num_slices == 0) {
      return network::IoCallResult::success(0);
    }

    // Writing to pipes: handle one slice at a time for simplicity
    size_t total_written = 0;
    for (size_t i = 0; i < num_slices; ++i) {
#ifdef _WIN32
      int result = send(write_fd_, static_cast<const char*>(slices[i].mem_),
                        static_cast<int>(slices[i].len_), 0);
#else
      ssize_t result = ::write(write_fd_, slices[i].mem_, slices[i].len_);
#endif
      if (result >= 0) {
        total_written += result;
        if (static_cast<size_t>(result) < slices[i].len_) {
          // Partial write, stop here
          break;
        }
      } else {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (total_written > 0) {
          return network::IoCallResult::success(total_written);
        }
        if (err == WSAEWOULDBLOCK) {
          return network::IoCallResult::error(EAGAIN);
        }
        return network::IoCallResult::error(err);
#else
        int err = errno;
        if (total_written > 0) {
          // Return what we've written so far
          return network::IoCallResult::success(total_written);
        }
        if (err == EAGAIN || err == EWOULDBLOCK) {
          // Would block - this is normal for non-blocking pipes
          // The caller will check wouldBlock() on the result
          return network::IoCallResult::error(err);
        }
        return network::IoCallResult::error(err);
#endif
      }
    }
    return network::IoCallResult::success(total_written);
  }

  // Override close to close both FDs
  network::IoVoidResult close() override {
    // Close write FD first
    if (write_fd_ >= 0) {
#ifdef _WIN32
      closesocket(write_fd_);
#else
      ::close(write_fd_);
#endif
      write_fd_ = -1;
    }
    // Then close read FD via parent class
    return IoSocketHandleImpl::close();
  }

 private:
  int write_fd_;  // Separate FD for writing
};

}  // namespace transport
}  // namespace mcp