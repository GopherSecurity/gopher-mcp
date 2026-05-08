/**
 * @file test_server_connection_mode.cc
 * @brief Comprehensive tests for server-side connection mode state machine
 *
 * Tests cover:
 * - Initial state, mode determination (PlainHttp, SseStream, CallbackProxy)
 * - Mode immutability (re-determination rejected)
 * - Close and Error from every mode
 * - HandshakeWriteGuard RAII (set/clear, early return, nested, wrong mode)
 * - SSE headers written (monotonic flag)
 * - State history, callbacks, metrics
 * - Query methods (isSseStream, isCallbackProxy, isPlainHttp)
 * - Utility methods (getModeName, getEventName)
 *
 * All tests use a real libevent dispatcher (no mocks).
 */

#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/event/event_loop.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/server_connection_mode.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

class ServerConnectionModeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    dispatcher_->run(event::RunType::NonBlock);
  }

  void TearDown() override {
    mode_.reset();
    dispatcher_.reset();
  }

  void runFor(std::chrono::milliseconds duration) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(1ms);
    }
  }

  void expectMode(ServerConnMode expected) {
    EXPECT_EQ(mode_->currentMode(), expected);
  }

  void createMode() {
    mode_ = std::make_unique<ServerConnectionMode>(*dispatcher_, config_);
  }

  // Helper: determine mode and verify
  void determineAs(ServerConnEvent event) {
    if (!mode_)
      createMode();
    auto r = mode_->handleEvent(event);
    EXPECT_TRUE(r.success);
    runFor(10ms);
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  ServerConnModeConfig config_;
  std::unique_ptr<ServerConnectionMode> mode_;
};

// ===== Initial State =====

TEST_F(ServerConnectionModeTest, InitialState) {
  createMode();
  expectMode(ServerConnMode::Undetermined);
  EXPECT_FALSE(mode_->isSseStream());
  EXPECT_FALSE(mode_->isCallbackProxy());
  EXPECT_FALSE(mode_->isPlainHttp());
  EXPECT_FALSE(mode_->isWritingHandshake());
  EXPECT_FALSE(mode_->sseHeadersWritten());
  EXPECT_FALSE(mode_->hasError());
}

// ===== Mode Determination =====

TEST_F(ServerConnectionModeTest, DetermineAsPlainHttp) {
  determineAs(ServerConnEvent::PlainHttpDetected);
  expectMode(ServerConnMode::PlainHttp);
  EXPECT_TRUE(mode_->isPlainHttp());
  EXPECT_FALSE(mode_->isSseStream());
  EXPECT_FALSE(mode_->isCallbackProxy());
}

TEST_F(ServerConnectionModeTest, DetermineAsSseStream) {
  determineAs(ServerConnEvent::SseGetDetected);
  expectMode(ServerConnMode::SseStream);
  EXPECT_TRUE(mode_->isSseStream());
  EXPECT_FALSE(mode_->isPlainHttp());
  EXPECT_FALSE(mode_->isCallbackProxy());
}

TEST_F(ServerConnectionModeTest, DetermineAsCallbackProxy) {
  determineAs(ServerConnEvent::CallbackPostDetected);
  expectMode(ServerConnMode::CallbackProxy);
  EXPECT_TRUE(mode_->isCallbackProxy());
  EXPECT_FALSE(mode_->isSseStream());
  EXPECT_FALSE(mode_->isPlainHttp());
}

// ===== Mode Immutability =====

TEST_F(ServerConnectionModeTest, PlainHttpCannotBecomeSseStream) {
  determineAs(ServerConnEvent::PlainHttpDetected);
  auto r = mode_->handleEvent(ServerConnEvent::SseGetDetected);
  EXPECT_FALSE(r.success);
  expectMode(ServerConnMode::PlainHttp);
}

TEST_F(ServerConnectionModeTest, SseStreamCannotBecomePlainHttp) {
  determineAs(ServerConnEvent::SseGetDetected);
  auto r = mode_->handleEvent(ServerConnEvent::PlainHttpDetected);
  EXPECT_FALSE(r.success);
  expectMode(ServerConnMode::SseStream);
}

TEST_F(ServerConnectionModeTest, SseStreamCannotBecomeCallbackProxy) {
  determineAs(ServerConnEvent::SseGetDetected);
  auto r = mode_->handleEvent(ServerConnEvent::CallbackPostDetected);
  EXPECT_FALSE(r.success);
  expectMode(ServerConnMode::SseStream);
}

TEST_F(ServerConnectionModeTest, CallbackProxyCannotBecomeSseStream) {
  determineAs(ServerConnEvent::CallbackPostDetected);
  auto r = mode_->handleEvent(ServerConnEvent::SseGetDetected);
  EXPECT_FALSE(r.success);
  expectMode(ServerConnMode::CallbackProxy);
}

TEST_F(ServerConnectionModeTest, DoubleSseGetDetected_Rejected) {
  determineAs(ServerConnEvent::SseGetDetected);
  auto r = mode_->handleEvent(ServerConnEvent::SseGetDetected);
  EXPECT_FALSE(r.success);
  expectMode(ServerConnMode::SseStream);
}

// ===== Close From Each Mode =====

TEST_F(ServerConnectionModeTest, CloseFromPlainHttp) {
  determineAs(ServerConnEvent::PlainHttpDetected);
  mode_->handleEvent(ServerConnEvent::Close);
  runFor(10ms);
  expectMode(ServerConnMode::Closed);
}

TEST_F(ServerConnectionModeTest, CloseFromSseStream) {
  determineAs(ServerConnEvent::SseGetDetected);
  mode_->handleEvent(ServerConnEvent::Close);
  runFor(10ms);
  expectMode(ServerConnMode::Closed);
}

TEST_F(ServerConnectionModeTest, CloseFromCallbackProxy) {
  determineAs(ServerConnEvent::CallbackPostDetected);
  mode_->handleEvent(ServerConnEvent::Close);
  runFor(10ms);
  expectMode(ServerConnMode::Closed);
}

TEST_F(ServerConnectionModeTest, CloseFromUndetermined) {
  createMode();
  mode_->handleEvent(ServerConnEvent::Close);
  runFor(10ms);
  expectMode(ServerConnMode::Closed);
}

TEST_F(ServerConnectionModeTest, ClosedIsTerminal) {
  determineAs(ServerConnEvent::PlainHttpDetected);
  mode_->handleEvent(ServerConnEvent::Close);
  runFor(10ms);
  auto r = mode_->handleEvent(ServerConnEvent::PlainHttpDetected);
  EXPECT_FALSE(r.success);
  expectMode(ServerConnMode::Closed);
}

// ===== Error From Each Mode =====

TEST_F(ServerConnectionModeTest, ErrorFromPlainHttp) {
  determineAs(ServerConnEvent::PlainHttpDetected);
  mode_->handleEvent(ServerConnEvent::StreamError);
  runFor(10ms);
  expectMode(ServerConnMode::Error);
}

TEST_F(ServerConnectionModeTest, ErrorFromSseStream) {
  determineAs(ServerConnEvent::SseGetDetected);
  mode_->handleEvent(ServerConnEvent::StreamError);
  runFor(10ms);
  expectMode(ServerConnMode::Error);
}

TEST_F(ServerConnectionModeTest, ErrorFromCallbackProxy) {
  determineAs(ServerConnEvent::CallbackPostDetected);
  mode_->handleEvent(ServerConnEvent::StreamError);
  runFor(10ms);
  expectMode(ServerConnMode::Error);
}

TEST_F(ServerConnectionModeTest, ErrorFromUndetermined) {
  createMode();
  mode_->handleEvent(ServerConnEvent::StreamError);
  runFor(10ms);
  expectMode(ServerConnMode::Error);
}

TEST_F(ServerConnectionModeTest, ErrorToClosedIsValid) {
  createMode();
  mode_->handleEvent(ServerConnEvent::StreamError);
  runFor(10ms);
  auto r = mode_->handleEvent(ServerConnEvent::Close);
  EXPECT_TRUE(r.success);
  runFor(10ms);
  expectMode(ServerConnMode::Closed);
}

TEST_F(ServerConnectionModeTest, ErrorToAnythingElseIsInvalid) {
  createMode();
  mode_->handleEvent(ServerConnEvent::StreamError);
  runFor(10ms);
  auto r = mode_->handleEvent(ServerConnEvent::PlainHttpDetected);
  EXPECT_FALSE(r.success);
  expectMode(ServerConnMode::Error);
}

// ===== HandshakeWriteGuard RAII =====

TEST_F(ServerConnectionModeTest, HandshakeGuard_SetsAndClears) {
  determineAs(ServerConnEvent::SseGetDetected);
  EXPECT_FALSE(mode_->isWritingHandshake());
  {
    HandshakeWriteGuard guard(*mode_);
    EXPECT_TRUE(mode_->isWritingHandshake());
  }
  EXPECT_FALSE(mode_->isWritingHandshake());
}

TEST_F(ServerConnectionModeTest, HandshakeGuard_ClearsOnEarlyReturn) {
  determineAs(ServerConnEvent::SseGetDetected);
  auto doWork = [&]() {
    HandshakeWriteGuard guard(*mode_);
    EXPECT_TRUE(mode_->isWritingHandshake());
    return;  // early return, guard destructs
  };
  doWork();
  EXPECT_FALSE(mode_->isWritingHandshake());
}

TEST_F(ServerConnectionModeTest, HandshakeGuard_NestedGuards) {
  determineAs(ServerConnEvent::SseGetDetected);
  {
    HandshakeWriteGuard guard1(*mode_);
    EXPECT_TRUE(mode_->isWritingHandshake());
    {
      // Ref-counted: second guard increments depth
      HandshakeWriteGuard guard2(*mode_);
      EXPECT_TRUE(mode_->isWritingHandshake());
    }
    // Still true — outer guard still alive
    EXPECT_TRUE(mode_->isWritingHandshake());
  }
  EXPECT_FALSE(mode_->isWritingHandshake());
}

TEST_F(ServerConnectionModeTest, HandshakeGuard_WorksInPlainHttpMode) {
  // Handshake guard should still function (no-op semantically) in
  // PlainHttp mode — the guard is a mechanical flag, not mode-gated.
  determineAs(ServerConnEvent::PlainHttpDetected);
  EXPECT_FALSE(mode_->isWritingHandshake());
  {
    HandshakeWriteGuard guard(*mode_);
    EXPECT_TRUE(mode_->isWritingHandshake());
  }
  EXPECT_FALSE(mode_->isWritingHandshake());
}

// ===== SSE Headers Written =====

TEST_F(ServerConnectionModeTest, SseHeadersWritten_InitiallyFalse) {
  determineAs(ServerConnEvent::SseGetDetected);
  EXPECT_FALSE(mode_->sseHeadersWritten());
}

TEST_F(ServerConnectionModeTest, SseHeadersWritten_SetOnce) {
  determineAs(ServerConnEvent::SseGetDetected);
  auto r = mode_->handleEvent(ServerConnEvent::SseHeadersWritten);
  EXPECT_TRUE(r.success);
  EXPECT_TRUE(mode_->sseHeadersWritten());
}

TEST_F(ServerConnectionModeTest, SseHeadersWritten_StaysTrue) {
  determineAs(ServerConnEvent::SseGetDetected);
  mode_->handleEvent(ServerConnEvent::SseHeadersWritten);
  EXPECT_TRUE(mode_->sseHeadersWritten());
  // Still true even after close
  mode_->handleEvent(ServerConnEvent::Close);
  runFor(10ms);
  EXPECT_TRUE(mode_->sseHeadersWritten());
}

TEST_F(ServerConnectionModeTest, SseHeadersWritten_NotApplicableToPlainHttp) {
  determineAs(ServerConnEvent::PlainHttpDetected);
  // SseHeadersWritten event should fail in PlainHttp mode because
  // the switch statement doesn't handle it for this mode.
  auto r = mode_->handleEvent(ServerConnEvent::SseHeadersWritten);
  EXPECT_FALSE(r.success);
  EXPECT_FALSE(mode_->sseHeadersWritten());
}

// ===== State History =====

TEST_F(ServerConnectionModeTest, StateHistoryRecordsAllTransitions) {
  createMode();
  mode_->handleEvent(ServerConnEvent::SseGetDetected);
  runFor(10ms);
  mode_->handleEvent(ServerConnEvent::Close);
  runFor(10ms);

  const auto& history = mode_->getStateHistory();
  ASSERT_GE(history.size(), 2u);
  EXPECT_EQ(history[0].from_mode, ServerConnMode::Undetermined);
  EXPECT_EQ(history[0].to_mode, ServerConnMode::SseStream);
  EXPECT_EQ(history[1].from_mode, ServerConnMode::SseStream);
  EXPECT_EQ(history[1].to_mode, ServerConnMode::Closed);
}

// ===== Callbacks =====

TEST_F(ServerConnectionModeTest,
       StateChangeCallback_InvokedOnModeDetermination) {
  createMode();
  std::vector<ServerConnTransitionContext> transitions;
  mode_->addStateChangeListener([&](const ServerConnTransitionContext& ctx) {
    transitions.push_back(ctx);
  });

  mode_->handleEvent(ServerConnEvent::SseGetDetected);
  runFor(10ms);

  ASSERT_EQ(transitions.size(), 1u);
  EXPECT_EQ(transitions[0].from_mode, ServerConnMode::Undetermined);
  EXPECT_EQ(transitions[0].to_mode, ServerConnMode::SseStream);
}

TEST_F(ServerConnectionModeTest, StateChangeCallback_ConfigCallback) {
  bool invoked = false;
  config_.state_change_callback =
      [&invoked](const ServerConnTransitionContext& ctx) {
        invoked = true;
        EXPECT_FALSE(ctx.reason.empty());
      };
  createMode();
  mode_->handleEvent(ServerConnEvent::PlainHttpDetected);
  runFor(10ms);
  EXPECT_TRUE(invoked);
}

TEST_F(ServerConnectionModeTest, ErrorCallback_InvokedOnError) {
  bool error_called = false;
  std::string error_msg;
  config_.error_callback = [&](const std::string& msg) {
    error_called = true;
    error_msg = msg;
  };
  createMode();
  mode_->handleEvent(ServerConnEvent::StreamError);
  runFor(10ms);
  EXPECT_TRUE(error_called);
  EXPECT_FALSE(error_msg.empty());
}

// ===== Query Methods =====

TEST_F(ServerConnectionModeTest, IsSseStream_OnlyForSseStream) {
  // Undetermined
  createMode();
  EXPECT_FALSE(mode_->isSseStream());

  // PlainHttp
  createMode();
  determineAs(ServerConnEvent::PlainHttpDetected);
  EXPECT_FALSE(mode_->isSseStream());

  // CallbackProxy
  createMode();
  determineAs(ServerConnEvent::CallbackPostDetected);
  EXPECT_FALSE(mode_->isSseStream());

  // SseStream
  createMode();
  determineAs(ServerConnEvent::SseGetDetected);
  EXPECT_TRUE(mode_->isSseStream());

  // Error
  createMode();
  mode_->handleEvent(ServerConnEvent::StreamError);
  runFor(10ms);
  EXPECT_FALSE(mode_->isSseStream());

  // Closed
  createMode();
  mode_->handleEvent(ServerConnEvent::Close);
  runFor(10ms);
  EXPECT_FALSE(mode_->isSseStream());
}

TEST_F(ServerConnectionModeTest, IsCallbackProxy_OnlyForCallbackProxy) {
  createMode();
  EXPECT_FALSE(mode_->isCallbackProxy());

  createMode();
  determineAs(ServerConnEvent::PlainHttpDetected);
  EXPECT_FALSE(mode_->isCallbackProxy());

  createMode();
  determineAs(ServerConnEvent::SseGetDetected);
  EXPECT_FALSE(mode_->isCallbackProxy());

  createMode();
  determineAs(ServerConnEvent::CallbackPostDetected);
  EXPECT_TRUE(mode_->isCallbackProxy());
}

// ===== Utility =====

TEST_F(ServerConnectionModeTest, GetModeName_AllModesNamed) {
  EXPECT_NE(ServerConnectionMode::getModeName(ServerConnMode::Undetermined),
            "Unknown");
  EXPECT_NE(ServerConnectionMode::getModeName(ServerConnMode::PlainHttp),
            "Unknown");
  EXPECT_NE(ServerConnectionMode::getModeName(ServerConnMode::SseStream),
            "Unknown");
  EXPECT_NE(ServerConnectionMode::getModeName(ServerConnMode::CallbackProxy),
            "Unknown");
  EXPECT_NE(ServerConnectionMode::getModeName(ServerConnMode::Error),
            "Unknown");
  EXPECT_NE(ServerConnectionMode::getModeName(ServerConnMode::Closed),
            "Unknown");
}

TEST_F(ServerConnectionModeTest, GetEventName_AllEventsNamed) {
  EXPECT_NE(
      ServerConnectionMode::getEventName(ServerConnEvent::PlainHttpDetected),
      "Unknown");
  EXPECT_NE(ServerConnectionMode::getEventName(ServerConnEvent::SseGetDetected),
            "Unknown");
  EXPECT_NE(
      ServerConnectionMode::getEventName(ServerConnEvent::CallbackPostDetected),
      "Unknown");
  EXPECT_NE(
      ServerConnectionMode::getEventName(ServerConnEvent::SseHeadersWritten),
      "Unknown");
  EXPECT_NE(ServerConnectionMode::getEventName(ServerConnEvent::StreamError),
            "Unknown");
  EXPECT_NE(ServerConnectionMode::getEventName(ServerConnEvent::Close),
            "Unknown");
}

// ===== Metrics =====

TEST_F(ServerConnectionModeTest, TotalTransitions_Counted) {
  createMode();
  EXPECT_EQ(mode_->getTotalTransitions(), 0u);
  mode_->handleEvent(ServerConnEvent::SseGetDetected);
  runFor(10ms);
  mode_->handleEvent(ServerConnEvent::Close);
  runFor(10ms);
  EXPECT_EQ(mode_->getTotalTransitions(), 2u);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
