/**
 * This project's server, driven by the official TypeScript SDK's client.
 *
 * The other direction from test_client_vs_official_server.cc, and the
 * stricter one: that client validates every message against the schema
 * and is exact about statuses, headers and session semantics. A
 * disagreement it reports is evidence about our server.
 *
 * The scenarios live in the driver rather than here, because they are
 * about what a client can do and the client is TypeScript. What this file
 * does is start a server, run the driver against it, and relay what it
 * said — so a failure is readable without rerunning anything by hand.
 *
 * Kept out of `make test` because it needs Node and a package install.
 * `make test-interop` runs it, and it skips rather than fails where those
 * are not present, so that not having Node is not the same as being
 * broken.
 *
 * Every scenario here is of the older era, and not by choice: the SDK
 * pinned in the driver (1.30.0, the newest published) tops out at
 * 2025-11-25, so there is no implementation of the 2026-07-28 revision on
 * the other side to disagree with us. This server serves that revision by
 * default and the scenarios below go on passing, which is the evidence
 * available today — that turning it on changed nothing for a client of an
 * older era, checked against one nobody here wrote.
 *
 * What would unblock the rest: an SDK release whose SUPPORTED_PROTOCOL_
 * VERSIONS includes 2026-07-28. Until then the two ends of that era are
 * exercised against each other in tests/integration/test_modern_era_*.cc,
 * which is weaker in exactly the way this file exists to fix — both ends
 * share a reading of the specification, and a shared misreading looks
 * like agreement.
 */

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "child_process.h"

namespace mcp {
namespace {

using namespace std::chrono_literals;
using test::Child;
using test::pickFreePort;
using test::waitUntilAccepting;

/** Where the driver lives, relative to the source tree. */
std::string driverDir() {
  const char* from_env = std::getenv("GOPHER_INTEROP_CLIENT_DIR");
  if (from_env != nullptr && *from_env != '\0') {
    return from_env;
  }
#ifdef GOPHER_INTEROP_CLIENT_DIR
  return GOPHER_INTEROP_CLIENT_DIR;
#else
  return "tests/interop/official-client-ts";
#endif
}

/** Where the server this suite drives was built. */
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

bool fileExists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

/** True when there is a Node, an install, and a server to drive. */
bool driverAvailable(std::string& why_not) {
  if (std::system("command -v node > /dev/null 2>&1") != 0) {
    why_not = "node is not installed";
    return false;
  }
  const std::string dir = driverDir();
  if (!fileExists(dir + "/client.ts")) {
    why_not = "the driver is not at " + dir;
    return false;
  }
  if (!fileExists(dir + "/node_modules/.package-lock.json")) {
    why_not =
        "the driver's dependencies are not installed; run `npm ci` in " + dir;
    return false;
  }
  if (!fileExists(serverBinary())) {
    why_not = "the interop server is not at " + serverBinary() +
              "; build the gopher_interop_server target";
    return false;
  }
  return true;
}

/**
 * One run of the driver against a server started with the same flags.
 *
 * The flags are given once and translated for each side, because the two
 * have to agree about what the server is: a driver that thinks sessions
 * are on when they are off would fail scenarios that were never going to
 * apply, and say nothing true.
 */
struct DriverRun {
  int status{-1};
  std::string output;
};

DriverRun driveServer(const std::vector<std::string>& modes) {
  DriverRun run;

  const uint16_t port = pickFreePort();
  if (port == 0) {
    run.output = "no free port";
    return run;
  }

  std::vector<std::string> server_argv{serverBinary(), "--port",
                                       std::to_string(port)};
  std::vector<std::string> driver_argv{
      "node", "client.ts", "--url",
      "http://127.0.0.1:" + std::to_string(port) + "/mcp"};
  for (const auto& mode : modes) {
    server_argv.push_back(mode);
    // --no-resume changes what the server keeps, not what the client may
    // ask for, so the driver is not told about it.
    if (mode != "--no-resume") {
      driver_argv.push_back(mode);
    }
  }

  Child server;
  if (!server.start(std::string(), server_argv)) {
    run.output = "could not start " + serverBinary();
    return run;
  }
  if (!waitUntilAccepting(port, 10s)) {
    run.output = "the server never accepted a connection";
    return run;
  }

  Child driver;
  if (!driver.start(driverDir(), driver_argv, /*capture=*/true)) {
    run.output = "could not start the driver";
    return run;
  }

  run.status = driver.wait(120s);
  run.output = driver.output();
  return run;
}

/** Fails with the driver's own report rather than a status code. */
void expectClean(const DriverRun& run) {
  EXPECT_EQ(run.status, 0) << "the driver reported a failure:\n" << run.output;
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

class OfficialClientVsServer : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string why_not;
    if (!driverAvailable(why_not)) {
      GTEST_SKIP() << why_not;
    }
  }
};

// Everything, against a server keeping sessions and able to replay.
TEST_F(OfficialClientVsServer, EveryScenarioPassesAgainstAWholeServer) {
  const DriverRun run = driveServer({});
  expectClean(run);
}

// A server that keeps nothing between requests is a mode a client has to
// cope with, and one this server has to be honest about: it names no
// session, refuses nothing for want of one, and serves no stream.
TEST_F(OfficialClientVsServer, AServerKeepingNoSessionsIsStillServed) {
  const DriverRun run = driveServer({"--stateless"});
  expectClean(run);
}

// With no standalone stream there is nowhere for anything unprompted to
// arrive, and the server has to say so with a status rather than by
// accepting a GET it will never write to.
TEST_F(OfficialClientVsServer, AServerServingNoStreamRefusesOneProperly) {
  const DriverRun run = driveServer({"--no-get-stream"});
  expectClean(run);
}

// Nothing retained means a client that comes back gets a fresh stream
// rather than what it missed — which the driver's own scenarios cover by
// not depending on replay when it cannot have it.
TEST_F(OfficialClientVsServer, AServerRetainingNothingIsStillServed) {
  const DriverRun run = driveServer({"--no-resume"});
  expectClean(run);
}

}  // namespace
}  // namespace mcp
