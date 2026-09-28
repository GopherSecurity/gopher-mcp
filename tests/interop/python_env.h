// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Running the Python side of the interop suites.
 *
 * The official Python SDK is the only released SDK that speaks 2026-07-28,
 * so it is what this project's modern-era behaviour is checked against. It
 * runs from a virtualenv `make test-interop` creates, pinned to one version;
 * these find it, say plainly when it is not there, and start a script in it
 * as a process.
 */

#pragma once

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "child_process.h"

namespace mcp {
namespace test {

/** The virtualenv's interpreter: from the environment, else the build. */
inline std::string pythonInterpreter() {
  const char* from_env = std::getenv("GOPHER_INTEROP_PYTHON");
  if (from_env != nullptr && *from_env != '\0') {
    return from_env;
  }
#ifdef GOPHER_INTEROP_PYTHON
  return GOPHER_INTEROP_PYTHON;
#else
  return "tests/interop/.venv-py/bin/python";
#endif
}

inline bool pythonFileExists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

/**
 * True when there is an interpreter with the pinned SDK installed, and the
 * script to run in it.
 *
 * Asked by importing the SDK rather than by looking for files: an install
 * that was interrupted leaves the files and not a working package, and a
 * test that then fails looks like an interop failure when it is not one.
 */
inline bool pythonAvailable(const std::string& script, std::string& why_not) {
  const std::string python = pythonInterpreter();
  if (!pythonFileExists(python)) {
    why_not = "the Python interop environment is not at " + python +
              "; run `make test-interop` to create it";
    return false;
  }
  if (!pythonFileExists(script)) {
    why_not = "the Python interop script is not at " + script;
    return false;
  }
  Child probe;
  if (!probe.start(".", {python, "-c", "import mcp, mcp_types"},
                   /*capture=*/true)) {
    why_not = "the Python interpreter at " + python + " could not be run";
    return false;
  }
  const Child::Ending ending = probe.wait(std::chrono::seconds(30));
  if (!ending.exitedWith(0)) {
    why_not = "the Python SDK is not installed in " + python + " (" +
              ending.describe() + "): " + probe.output();
    return false;
  }
  return true;
}

/** A Python script serving MCP on a port of its own, as a process. */
class PythonServer {
 public:
  /**
   * Start `script` with `--port N` and the flags, and wait until something
   * is accepting on the port.
   */
  ::testing::AssertionResult start(const std::string& dir,
                                   const std::string& script,
                                   const std::vector<std::string>& flags = {}) {
    std::string why_not;
    port_ = pickFreePort(&why_not);
    if (port_ == 0) {
      return ::testing::AssertionFailure()
             << "no port for the Python server: " << why_not;
    }

    std::vector<std::string> argv{pythonInterpreter(), script, "--port",
                                  std::to_string(port_)};
    for (const auto& flag : flags) {
      argv.push_back(flag);
    }

    if (!process_.start(dir, argv, /*capture=*/true)) {
      return ::testing::AssertionFailure()
             << "the Python server could not be started in " << dir;
    }
    // Longer than Node's: the interpreter imports the whole SDK before it
    // opens the port.
    if (waitUntilAccepting(port_, std::chrono::seconds(30))) {
      return ::testing::AssertionSuccess();
    }

    const Child::Ending ending = process_.wait(std::chrono::seconds(1));
    const std::string said = process_.output();
    return ::testing::AssertionFailure()
           << "the Python server did not accept on port " << port_
           << " within 30s; it " << ending.describe()
           << (said.empty() ? std::string(" and wrote nothing")
                            : std::string(" and wrote:\n") + said);
  }

  void stop() { process_.stop(); }

  /** What the server has written so far, for a failure message. */
  std::string output() { return process_.output(); }

  uint16_t port() const { return port_; }
  std::string url() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
  }

 private:
  Child process_;
  uint16_t port_{0};
};

}  // namespace test
}  // namespace mcp
