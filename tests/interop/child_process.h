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

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <poll.h>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <sys/wait.h>

#include "mcp/network/address.h"
#include "mcp/network/socket_interface.h"

namespace mcp {
namespace test {

/** A loopback port the kernel believes is free. */
inline uint16_t pickFreePort(std::string* why_not = nullptr) {
  auto say = [why_not](const std::string& reason) {
    if (why_not != nullptr) {
      *why_not = reason;
    }
  };

  auto& iface = network::socketInterface();
  auto fd =
      iface.socket(network::SocketType::Stream, network::Address::Type::Ip,
                   network::Address::IpVersion::v4);
  if (!fd.ok()) {
    say("no socket could be made to find a free port with");
    return 0;
  }
  auto handle = iface.ioHandleForFd(*fd, false);

  // Checked, unlike before. A bind or listen that fails leaves the port
  // at zero and the caller with nothing to go on — and on a machine
  // where this is what is wrong, that is the whole of what it needs to
  // be told.
  auto bound =
      handle->bind(network::Address::parseInternetAddress("127.0.0.1", 0));
  if (!bound.ok()) {
    say("binding a loopback port failed (errno " + std::to_string(errno) +
        "); the loopback interface may be down or restricted");
    handle->close();
    return 0;
  }
  auto listening = handle->listen(1);
  if (!listening.ok()) {
    say("listening on a loopback port failed (errno " + std::to_string(errno) +
        ")");
    handle->close();
    return 0;
  }

  auto local = handle->localAddress();
  uint16_t port = 0;
  if (local.ok()) {
    const auto* ip = dynamic_cast<const network::Address::Ip*>(local->get());
    if (ip != nullptr) {
      port = ip->port();
    }
  }
  if (port == 0) {
    say("a loopback port was bound but the kernel did not name it");
  }
  handle->close();
  return port;
}

/** What `node --version` says, as major and minor. Zeroes when unknown. */
inline std::pair<int, int> nodeVersion() {
  FILE* pipe = popen("node --version 2>/dev/null", "r");
  if (pipe == nullptr) {
    return std::make_pair(0, 0);
  }
  char buffer[64] = {0};
  const char* got = fgets(buffer, sizeof(buffer), pipe);
  pclose(pipe);
  if (got == nullptr) {
    return std::make_pair(0, 0);
  }
  std::string text(buffer);
  if (!text.empty() && text[0] == 'v') {
    text.erase(0, 1);
  }
  int major = 0;
  int minor = 0;
  if (std::sscanf(text.c_str(), "%d.%d", &major, &minor) != 2) {
    return std::make_pair(0, 0);
  }
  return std::make_pair(major, minor);
}

/**
 * What this node needs in order to run a TypeScript file directly.
 *
 * Both interop programs are `.ts` run straight by node, which is only
 * something node can do at all from 22.6, and only without being asked
 * from 23.6. In between it has to be asked — and a node that is not
 * asked fails immediately with a syntax error, which from the outside
 * looks exactly like a server that started and never listened.
 *
 * @return False when this node cannot run them however it is asked.
 */
inline bool nodeTypeScriptFlags(std::vector<std::string>* flags,
                                std::string* why_not) {
  const auto version = nodeVersion();
  if (version.first == 0) {
    *why_not = "node is installed but did not say what version it is";
    return false;
  }
  if (version.first < 22 || (version.first == 22 && version.second < 6)) {
    *why_not = "node " + std::to_string(version.first) + "." +
               std::to_string(version.second) +
               " cannot run TypeScript directly; these programs need 22.6 "
               "or newer";
    return false;
  }
  if (version.first < 23 || (version.first == 23 && version.second < 6)) {
    // Able, but only when asked.
    flags->push_back("--experimental-strip-types");
  }
  return true;
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
   * Both at once, and neither before the other. Draining to EOF first
   * looked like the tidy order — read everything, then reap — but EOF on
   * a child's pipe means every write end has closed, which for a child
   * that is still running is a thing that has not happened yet. Waiting
   * for it there is waiting for the child to exit, without a deadline,
   * inside the call whose whole purpose is to have one. A caller asking
   * why a peer that is still running has not started would have hung on
   * the question.
   *
   * So the pipe is read for whatever is there, the child is reaped if it
   * has gone, and the budget is checked — round and round until one of
   * the last two settles it.
   *
   * @return Its exit status, or -1 if it did not finish in time. What it
   *         wrote up to that point is kept either way.
   */
  int wait(std::chrono::milliseconds budget) {
    if (pid_ <= 0) {
      return -1;
    }

    const auto deadline = std::chrono::steady_clock::now() + budget;
    int status = 0;
    for (;;) {
      drainWhatIsThere();

      const pid_t done = waitpid(pid_, &status, WNOHANG);
      if (done == pid_) {
        // Gone, so its pipe is at EOF and the rest of what it wrote is
        // there to be had.
        drainWhatIsThere();
        closeRead();
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      }

      if (std::chrono::steady_clock::now() >= deadline) {
        return -1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  void stop() {
    // Whatever it managed to say before being told to go, since a child
    // that has to be killed is one somebody will want an account of.
    drainWhatIsThere();
    closeRead();
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

 private:
  /** Take what has been written so far, and never wait for more. */
  void drainWhatIsThere() {
    if (read_fd_ < 0) {
      return;
    }
    for (;;) {
      struct pollfd waiting;
      waiting.fd = read_fd_;
      waiting.events = POLLIN;
      waiting.revents = 0;
      // No timeout at all: this asks what is there, never what is
      // coming.
      const int ready = poll(&waiting, 1, 0);
      if (ready <= 0) {
        return;
      }
      char buffer[4096];
      const ssize_t got = read(read_fd_, buffer, sizeof(buffer));
      if (got > 0) {
        output_.append(buffer, static_cast<size_t>(got));
        continue;
      }
      // Zero is EOF and negative is nothing more to be had now; either
      // way there is nothing further to read in this pass.
      return;
    }
  }

  void closeRead() {
    if (read_fd_ >= 0) {
      close(read_fd_);
      read_fd_ = -1;
    }
  }

 public:
  /** Everything it wrote, once wait() has read it. */
  const std::string& output() const { return output_; }

 private:
  pid_t pid_{-1};
  int read_fd_{-1};
  std::string output_;
};

}  // namespace test
}  // namespace mcp
