// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * The official Python SDK's client against this project's server.
 *
 * The stricter of the two Python directions, as with TypeScript: the
 * official client validates what this server sends against its own reading
 * of the spec, and it is the only released client that speaks 2026-07-28.
 * The driver runs its scenarios once in that revision and once in the
 * earlier ones, and reports each as TAP ("ok N - name", "not ok N - name",
 * "# SKIP why"), which is all this file reads.
 *
 * Kept out of `make test` because it needs Python and a package install.
 * `make test-interop` runs it, and it skips rather than fails where those
 * are not present.
 */

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "python_env.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;
using test::Child;
using test::pickFreePort;
using test::waitUntilAccepting;

std::string driverDir() {
  const char* from_env = std::getenv("GOPHER_INTEROP_PY_CLIENT_DIR");
  if (from_env != nullptr && *from_env != '\0') {
    return from_env;
  }
#ifdef GOPHER_INTEROP_PY_CLIENT_DIR
  return GOPHER_INTEROP_PY_CLIENT_DIR;
#else
  return "tests/interop/official-client-py";
#endif
}

std::string serverBinary() {
  const char* from_env = std::getenv("GOPHER_INTEROP_SERVER_BIN");
  if (from_env != nullptr && *from_env != '\0') {
    return from_env;
  }
#ifdef GOPHER_INTEROP_SERVER_BIN
  return GOPHER_INTEROP_SERVER_BIN;
#else
  return "build/tests/gopher_interop_server";
#endif
}

/** What one run of the driver came to. */
struct DriverRun {
  int status{-1};
  std::string ending{"never ran"};
  std::string output;
};

/**
 * Start this project's interop server with the given flags, and run the
 * Python driver against it in one revision.
 *
 * @param mode        "modern" for 2026-07-28, "legacy" for the earlier ones.
 * @param server_args Flags for the server, also passed to the driver so it
 *                    knows which scenarios apply.
 */
DriverRun driveServer(const std::string& mode,
                      const std::vector<std::string>& server_args = {}) {
  DriverRun run;

  std::string why_not;
  const uint16_t port = pickFreePort(&why_not);
  if (port == 0) {
    run.output = "no free port for the interop run: " + why_not;
    return run;
  }

  std::vector<std::string> server_argv{serverBinary(), "--port",
                                       std::to_string(port)};
  std::vector<std::string> driver_argv{
      test::pythonInterpreter(),
      "client.py",
      "--url",
      "http://127.0.0.1:" + std::to_string(port) + "/mcp",
      "--mode",
      mode};
  for (const auto& arg : server_args) {
    server_argv.push_back(arg);
    driver_argv.push_back(arg);
  }

  Child server;
  if (!server.start(std::string(), server_argv, /*capture=*/true)) {
    run.output = "could not start " + serverBinary();
    return run;
  }
  if (!waitUntilAccepting(port, 10s)) {
    const Child::Ending ending = server.wait(1s);
    run.output = serverBinary() + " never accepted on port " +
                 std::to_string(port) + "; it " + ending.describe() +
                 (server.output().empty() ? " and wrote nothing"
                                          : " and wrote:\n" + server.output());
    return run;
  }

  Child driver;
  if (!driver.start(driverDir(), driver_argv, /*capture=*/true)) {
    run.output = "could not start the Python driver";
    return run;
  }

  const Child::Ending ending = driver.wait(180s);
  run.status = ending.how == Child::Ending::How::Exited ? ending.code : -1;
  run.ending = ending.describe();
  run.output = driver.output();
  return run;
}

/** Fails with the driver's own report rather than a status code. */
void expectClean(const DriverRun& run) {
  EXPECT_EQ(run.status, 0) << "the driver " << run.ending << ":\n"
                           << run.output;
  if (run.status != 0) {
    return;
  }
  // A run that passed nothing at all also exits zero, so the count is
  // checked as well as the status.
  EXPECT_NE(run.output.find("ok 1 -"), std::string::npos)
      << "the driver ran no scenarios:\n"
      << run.output;
  EXPECT_EQ(run.output.find("not ok"), std::string::npos) << run.output;
}

class PythonClientVsServer : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string why_not;
    if (!test::pythonAvailable(driverDir() + "/client.py", why_not)) {
      GTEST_SKIP() << "skipping Python interop: " << why_not;
    }
    if (!test::pythonFileExists(serverBinary())) {
      GTEST_SKIP() << "skipping Python interop: the interop server is not "
                      "built at "
                   << serverBinary();
    }
  }
};

// Everything the newest revision has, against a server keeping sessions.
TEST_F(PythonClientVsServer, EveryScenarioPassesInTheNewestRevision) {
  expectClean(driveServer("modern"));
}

// The same scenarios through the handshake the earlier revisions use.
TEST_F(PythonClientVsServer, EveryScenarioPassesInAnEarlierRevision) {
  expectClean(driveServer("legacy"));
}

// A server keeping no sessions serves both the same.
TEST_F(PythonClientVsServer, AServerKeepingNoSessionsIsStillServed) {
  expectClean(driveServer("modern", {"--stateless"}));
  expectClean(driveServer("legacy", {"--stateless"}));
}

// A server listing a page at a time is read in full by a client following
// its cursors, and the other scenarios still find every tool they need.
TEST_F(PythonClientVsServer, AServerThatPagesIsReadInFull) {
  expectClean(driveServer("modern", {"--page-size", "2"}));
  expectClean(driveServer("legacy", {"--page-size", "2"}));
}

}  // namespace
}  // namespace mcp
