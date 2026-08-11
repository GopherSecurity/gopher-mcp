/**
 * Starting and stopping the other side of an interop test.
 *
 * Both directions need the same few things: a port the kernel agrees is
 * free, a child process in its own process group so stopping it stops
 * whatever it started, and a way to wait for it — either until it is
 * accepting connections, or until it has finished and said what it found.
 *
 * Shared rather than written twice because the parts that are easy to get
 * subtly wrong — the process group, the kill-then-wait, reading a pipe to
 * EOF before reaping — are exactly the parts that would drift apart.
 */

#pragma once

#include <chrono>
#include <cstdio>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <sys/wait.h>

#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"

namespace mcp {
namespace test {

/** A loopback port the kernel believes is free. */
inline uint16_t pickFreePort() {
  auto& iface = network::socketInterface();
  auto fd =
      iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                   network::Address::IpVersion::v4);
  if (!fd.ok()) {
    return 0;
  }
  auto handle = iface.ioHandleForFd(*fd, false);
  handle->bind(network::Address::parseInternetAddress("127.0.0.1", 0));
  handle->listen(1);
  auto local = handle->localAddress();
  uint16_t port = 0;
  if (local.ok()) {
    const auto* ip = dynamic_cast<const network::Address::Ip*>(local->get());
    if (ip != nullptr) {
      port = ip->port();
    }
  }
  handle->close();
  return port;
}

/** True once something is accepting on the port, or the budget is spent. */
inline bool waitUntilAccepting(uint16_t port,
                               std::chrono::milliseconds budget) {
  auto& iface = network::socketInterface();
  auto addr = network::Address::parseInternetAddress("127.0.0.1", port);
  const auto deadline = std::chrono::steady_clock::now() + budget;
  while (std::chrono::steady_clock::now() < deadline) {
    auto fd =
        iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                     network::Address::IpVersion::v4);
    if (fd.ok()) {
      auto handle = iface.ioHandleForFd(*fd, false);
      handle->setBlocking(true);
      auto connected = handle->connect(addr);
      handle->close();
      if (connected.ok()) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

/**
 * A child process.
 *
 * Started in its own process group, so stopping it stops whatever it
 * started rather than leaving a listener behind holding a port.
 */
class Child {
 public:
  ~Child() { stop(); }

  /**
   * @param working_dir Where to run it, or empty to stay put.
   * @param argv        The program and its arguments.
   * @param capture     Keep what it writes, for a child whose output is
   *                    the result. A child that is only a server has its
   *                    output discarded — it is nobody's business unless
   *                    something goes wrong, and then the test says so.
   */
  bool start(const std::string& working_dir,
             const std::vector<std::string>& argv,
             bool capture = false) {
    if (argv.empty()) {
      return false;
    }

    int pipe_fds[2] = {-1, -1};
    if (capture && pipe(pipe_fds) != 0) {
      return false;
    }

    pid_ = fork();
    if (pid_ < 0) {
      if (capture) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
      }
      return false;
    }

    if (pid_ == 0) {
      setpgid(0, 0);
      if (!working_dir.empty() && chdir(working_dir.c_str()) != 0) {
        _exit(127);
      }
      if (capture) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
      } else {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
      }
      std::vector<char*> raw;
      std::vector<std::string> owned = argv;
      for (auto& arg : owned) {
        raw.push_back(const_cast<char*>(arg.c_str()));
      }
      raw.push_back(nullptr);
      execvp(raw[0], raw.data());
      _exit(127);
    }

    if (capture) {
      close(pipe_fds[1]);
      read_fd_ = pipe_fds[0];
    }
    return true;
  }

  /**
   * Wait for it to finish, keeping whatever it wrote.
   *
   * The pipe is drained to EOF first: reaping a child whose output has not
   * been read is how a test ends up reporting a failure with nothing to
   * say about it.
   *
   * @return Its exit status, or -1 if it did not finish in time.
   */
  int wait(std::chrono::milliseconds budget) {
    if (pid_ <= 0) {
      return -1;
    }
    if (read_fd_ >= 0) {
      char buffer[4096];
      ssize_t got = 0;
      while ((got = read(read_fd_, buffer, sizeof(buffer))) > 0) {
        output_.append(buffer, static_cast<size_t>(got));
      }
      close(read_fd_);
      read_fd_ = -1;
    }

    const auto deadline = std::chrono::steady_clock::now() + budget;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      const pid_t done = waitpid(pid_, &status, WNOHANG);
      if (done == pid_) {
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return -1;
  }

  void stop() {
    if (read_fd_ >= 0) {
      close(read_fd_);
      read_fd_ = -1;
    }
    if (pid_ <= 0) {
      return;
    }
    kill(-pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 100; ++i) {
      if (waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    kill(-pid_, SIGKILL);
    waitpid(pid_, &status, 0);
    pid_ = -1;
  }

  bool running() const { return pid_ > 0; }

  /** Everything it wrote, once wait() has read it. */
  const std::string& output() const { return output_; }

 private:
  pid_t pid_{-1};
  int read_fd_{-1};
  std::string output_;
};

}  // namespace test
}  // namespace mcp
