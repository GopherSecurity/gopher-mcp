/**
 * @file test_sse_codec_state_machine.cc
 * @brief Comprehensive tests for SSE codec state machine
 */

#include <chrono>
#include <sstream>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/event/event_loop.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/sse_codec_state_machine.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;
using ::testing::_;

class SseCodecStateMachineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create dispatcher using factory
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");

    // Run once to set thread_id_ and make isThreadSafe() return true
    // This allows timer creation to work properly
    dispatcher_->run(event::RunType::NonBlock);

    config_ = SseCodecStateMachineConfig{};
    config_.keep_alive_interval = 100ms;
    config_.event_timeout = 200ms;
    config_.enable_keep_alive = true;
    config_.max_event_size = 1024;

    state_machine_ =
        std::make_unique<SseCodecStateMachine>(*dispatcher_, config_);
  }

  void TearDown() override {
    state_machine_.reset();
    dispatcher_.reset();
  }

  // Helper to run dispatcher for a duration
  void runFor(std::chrono::milliseconds duration) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(1ms);
    }
  }

  // Helper to verify state
  void expectState(SseCodecState expected) {
    EXPECT_EQ(state_machine_->currentState(), expected);
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  SseCodecStateMachineConfig config_;
  std::unique_ptr<SseCodecStateMachine> state_machine_;

  // Track callbacks
  bool keep_alive_called_ = false;
  std::string last_error_;
  std::vector<SseCodecStateTransitionContext> state_changes_;
};

// ===== Basic State Transition Tests =====

TEST_F(SseCodecStateMachineTest, InitialState) {
  expectState(SseCodecState::Idle);
  EXPECT_TRUE(state_machine_->canStartStream());
  EXPECT_FALSE(state_machine_->isStreaming());
  EXPECT_FALSE(state_machine_->canSendEvent());
  EXPECT_FALSE(state_machine_->hasError());
}

TEST_F(SseCodecStateMachineTest, StartStream) {
  expectState(SseCodecState::Idle);

  auto result = state_machine_->handleEvent(SseCodecEvent::StartStream);
  EXPECT_TRUE(result.success);

  runFor(10ms);
  expectState(SseCodecState::StreamActive);
  EXPECT_TRUE(state_machine_->isStreaming());
  EXPECT_TRUE(state_machine_->canSendEvent());
}

TEST_F(SseCodecStateMachineTest, SendEvent) {
  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  expectState(SseCodecState::StreamActive);

  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);
  expectState(SseCodecState::SendingEvent);
  EXPECT_TRUE(state_machine_->isStreaming());
  EXPECT_FALSE(state_machine_->canSendEvent());
}

TEST_F(SseCodecStateMachineTest, EventSent) {
  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);
  expectState(SseCodecState::SendingEvent);

  state_machine_->handleEvent(SseCodecEvent::EventSent);
  runFor(10ms);
  expectState(SseCodecState::StreamActive);
  EXPECT_TRUE(state_machine_->canSendEvent());

  // Check event counter
  EXPECT_EQ(state_machine_->getEventsSent(), 1);
}

TEST_F(SseCodecStateMachineTest, CloseStream) {
  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  expectState(SseCodecState::StreamActive);

  state_machine_->handleEvent(SseCodecEvent::CloseStream);
  runFor(10ms);
  expectState(SseCodecState::Closed);
  EXPECT_FALSE(state_machine_->isStreaming());
}

TEST_F(SseCodecStateMachineTest, StreamError) {
  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);

  state_machine_->handleEvent(SseCodecEvent::StreamError);
  runFor(10ms);
  expectState(SseCodecState::Error);
  EXPECT_TRUE(state_machine_->hasError());
  EXPECT_FALSE(state_machine_->isStreaming());
}

TEST_F(SseCodecStateMachineTest, ResetAfterError) {
  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::StreamError);
  runFor(10ms);
  expectState(SseCodecState::Error);

  state_machine_->handleEvent(SseCodecEvent::Reset);
  runFor(10ms);
  expectState(SseCodecState::Idle);
  EXPECT_TRUE(state_machine_->canStartStream());
}

TEST_F(SseCodecStateMachineTest, ResetAfterClose) {
  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::CloseStream);
  runFor(10ms);
  expectState(SseCodecState::Closed);

  state_machine_->handleEvent(SseCodecEvent::Reset);
  runFor(10ms);
  expectState(SseCodecState::Idle);
}

// ===== Keep-Alive Tests =====

TEST_F(SseCodecStateMachineTest, KeepAliveTimer) {
  bool keep_alive_triggered = false;
  config_.keep_alive_callback = [&keep_alive_triggered]() {
    keep_alive_triggered = true;
  };
  state_machine_ =
      std::make_unique<SseCodecStateMachine>(*dispatcher_, config_);

  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  expectState(SseCodecState::StreamActive);

  // Wait for keep-alive timer
  runFor(config_.keep_alive_interval + 50ms);

  EXPECT_TRUE(keep_alive_triggered);
  // Should still be in StreamActive state
  expectState(SseCodecState::StreamActive);
}

TEST_F(SseCodecStateMachineTest, KeepAliveStopsWhenSendingEvent) {
  int keep_alive_count = 0;
  config_.keep_alive_callback = [&keep_alive_count]() { keep_alive_count++; };
  state_machine_ =
      std::make_unique<SseCodecStateMachine>(*dispatcher_, config_);

  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);

  // Wait for first keep-alive
  runFor(config_.keep_alive_interval + 10ms);
  EXPECT_EQ(keep_alive_count, 1);

  // Start sending event
  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);

  // Wait another keep-alive interval - should not trigger
  runFor(config_.keep_alive_interval + 10ms);
  EXPECT_EQ(keep_alive_count, 1);  // Should not increase

  // Complete event send
  state_machine_->handleEvent(SseCodecEvent::EventSent);
  runFor(10ms);

  // Now keep-alive should resume
  runFor(config_.keep_alive_interval + 10ms);
  EXPECT_EQ(keep_alive_count, 2);
}

TEST_F(SseCodecStateMachineTest, ManualKeepAlive) {
  bool keep_alive_triggered = false;
  config_.keep_alive_callback = [&keep_alive_triggered]() {
    keep_alive_triggered = true;
  };
  state_machine_ =
      std::make_unique<SseCodecStateMachine>(*dispatcher_, config_);

  state_machine_->startStream();
  runFor(10ms);

  state_machine_->sendKeepAlive();
  runFor(10ms);

  EXPECT_TRUE(keep_alive_triggered);
}

TEST_F(SseCodecStateMachineTest, StartStopKeepAliveTimer) {
  state_machine_->startStream();
  runFor(10ms);

  state_machine_->startKeepAliveTimer();
  runFor(10ms);

  state_machine_->stopKeepAliveTimer();

  // Wait for would-be keep-alive interval
  runFor(config_.keep_alive_interval + 50ms);

  // Keep-alive should not have triggered
  expectState(SseCodecState::StreamActive);
}

// ===== Event Timeout Tests =====

TEST_F(SseCodecStateMachineTest, EventTimeout) {
  bool error_called = false;
  config_.error_callback = [&error_called](const std::string& error) {
    error_called = true;
    EXPECT_TRUE(error.find("event send timeout") != std::string::npos);
  };
  state_machine_ =
      std::make_unique<SseCodecStateMachine>(*dispatcher_, config_);

  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);
  expectState(SseCodecState::SendingEvent);

  // Wait for event timeout
  runFor(config_.event_timeout + 50ms);

  EXPECT_TRUE(error_called);
  expectState(SseCodecState::Error);
}

TEST_F(SseCodecStateMachineTest, NoTimeoutWhenEventSent) {
  bool error_called = false;
  config_.error_callback = [&error_called](const std::string&) {
    error_called = true;
  };
  state_machine_ =
      std::make_unique<SseCodecStateMachine>(*dispatcher_, config_);

  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);

  // Send event before timeout
  runFor(config_.event_timeout / 2);
  state_machine_->handleEvent(SseCodecEvent::EventSent);
  runFor(10ms);

  // Wait rest of timeout period
  runFor(config_.event_timeout);

  EXPECT_FALSE(error_called);
  expectState(SseCodecState::StreamActive);
}

// ===== State Change Listener Tests =====

TEST_F(SseCodecStateMachineTest, StateChangeListeners) {
  std::vector<SseCodecStateTransitionContext> transitions;

  state_machine_->addStateChangeListener(
      [&transitions](const SseCodecStateTransitionContext& ctx) {
        transitions.push_back(ctx);
      });

  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::EventSent);
  runFor(10ms);

  ASSERT_GE(transitions.size(), 3);
  EXPECT_EQ(transitions[0].from_state, SseCodecState::Idle);
  EXPECT_EQ(transitions[0].to_state, SseCodecState::StreamActive);
  EXPECT_EQ(transitions[1].from_state, SseCodecState::StreamActive);
  EXPECT_EQ(transitions[1].to_state, SseCodecState::SendingEvent);
  EXPECT_EQ(transitions[2].from_state, SseCodecState::SendingEvent);
  EXPECT_EQ(transitions[2].to_state, SseCodecState::StreamActive);
}

TEST_F(SseCodecStateMachineTest, StateChangeCallback) {
  bool callback_invoked = false;
  config_.state_change_callback =
      [&callback_invoked](const SseCodecStateTransitionContext& ctx) {
        callback_invoked = true;
        EXPECT_FALSE(ctx.reason.empty());
      };
  state_machine_ =
      std::make_unique<SseCodecStateMachine>(*dispatcher_, config_);

  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);

  EXPECT_TRUE(callback_invoked);
}

// ===== Entry/Exit Action Tests =====

TEST_F(SseCodecStateMachineTest, EntryExitActions) {
  bool entry_called = false;
  bool exit_called = false;

  state_machine_->setEntryAction(
      SseCodecState::StreamActive,
      [&entry_called](SseCodecState state, std::function<void()> done) {
        entry_called = true;
        done();
      });

  state_machine_->setExitAction(
      SseCodecState::Idle,
      [&exit_called](SseCodecState state, std::function<void()> done) {
        exit_called = true;
        done();
      });

  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);

  EXPECT_TRUE(entry_called);
  EXPECT_TRUE(exit_called);
}

// ===== Async Callback Tests =====

TEST_F(SseCodecStateMachineTest, AsyncStartStream) {
  bool callback_invoked = false;
  bool success = false;

  state_machine_->startStream([&callback_invoked, &success](bool result) {
    callback_invoked = true;
    success = result;
  });

  runFor(10ms);

  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(success);
  expectState(SseCodecState::StreamActive);
}

TEST_F(SseCodecStateMachineTest, AsyncCloseStream) {
  state_machine_->startStream();
  runFor(10ms);

  bool callback_invoked = false;
  bool success = false;

  state_machine_->closeStream([&callback_invoked, &success](bool result) {
    callback_invoked = true;
    success = result;
  });

  runFor(10ms);

  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(success);
  expectState(SseCodecState::Closed);
}

TEST_F(SseCodecStateMachineTest, AsyncSendKeepAlive) {
  state_machine_->startStream();
  runFor(10ms);

  bool callback_invoked = false;
  bool success = false;

  state_machine_->sendKeepAlive([&callback_invoked, &success](bool result) {
    callback_invoked = true;
    success = result;
  });

  runFor(10ms);

  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(success);
}

TEST_F(SseCodecStateMachineTest, AsyncCallbackOnInvalidOperation) {
  // Try to close stream when not streaming
  bool callback_invoked = false;
  bool success = false;

  state_machine_->closeStream([&callback_invoked, &success](bool result) {
    callback_invoked = true;
    success = result;
  });

  runFor(10ms);

  EXPECT_TRUE(callback_invoked);
  EXPECT_FALSE(success);  // Should fail
}

// ===== Transition Validation Tests =====

TEST_F(SseCodecStateMachineTest, ValidTransitions) {
  EXPECT_TRUE(state_machine_->isTransitionValid(SseCodecState::Idle,
                                                SseCodecState::StreamActive,
                                                SseCodecEvent::StartStream));

  EXPECT_TRUE(state_machine_->isTransitionValid(SseCodecState::StreamActive,
                                                SseCodecState::SendingEvent,
                                                SseCodecEvent::SendEvent));

  EXPECT_TRUE(state_machine_->isTransitionValid(SseCodecState::SendingEvent,
                                                SseCodecState::StreamActive,
                                                SseCodecEvent::EventSent));
}

TEST_F(SseCodecStateMachineTest, InvalidTransitions) {
  EXPECT_FALSE(state_machine_->isTransitionValid(SseCodecState::Idle,
                                                 SseCodecState::SendingEvent,
                                                 SseCodecEvent::SendEvent));

  EXPECT_FALSE(state_machine_->isTransitionValid(SseCodecState::Closed,
                                                 SseCodecState::StreamActive,
                                                 SseCodecEvent::StartStream));
}

TEST_F(SseCodecStateMachineTest, CustomValidator) {
  bool validator_called = false;

  state_machine_->addTransitionValidator(
      [&validator_called](SseCodecState from, SseCodecState to) {
        validator_called = true;
        // Block transition to Error state
        return to != SseCodecState::Error;
      });

  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);

  EXPECT_TRUE(validator_called);

  // Try to transition to error - should be blocked
  state_machine_->handleEvent(SseCodecEvent::StreamError);
  runFor(10ms);

  // Should still be in StreamActive due to validator
  expectState(SseCodecState::StreamActive);
}

// ===== Force Transition Tests =====

TEST_F(SseCodecStateMachineTest, ForceTransition) {
  expectState(SseCodecState::Idle);

  state_machine_->forceTransition(SseCodecState::StreamActive, "forced test");

  expectState(SseCodecState::StreamActive);

  // Verify state history contains forced transition
  auto history = state_machine_->getStateHistory();
  ASSERT_FALSE(history.empty());
  EXPECT_TRUE(history.back().reason.find("forced") != std::string::npos);
}

// ===== Schedule Transition Tests =====

TEST_F(SseCodecStateMachineTest, ScheduledTransition) {
  expectState(SseCodecState::Idle);

  state_machine_->scheduleTransition(SseCodecState::StreamActive,
                                     SseCodecEvent::StartStream,
                                     "scheduled test");

  // Should not transition immediately
  expectState(SseCodecState::Idle);

  // Run dispatcher to process scheduled transition
  runFor(10ms);

  expectState(SseCodecState::StreamActive);
}

// ===== SSE Event Formatting Tests =====

TEST_F(SseCodecStateMachineTest, FormatSimpleEvent) {
  SseEventData event;
  event.data = "Hello, World!";

  std::string formatted = SseCodecStateMachine::formatEvent(event);

  EXPECT_EQ(formatted, "data: Hello, World!\n\n");
}

TEST_F(SseCodecStateMachineTest, FormatEventWithId) {
  SseEventData event;
  event.id = "123";
  event.data = "Test data";

  std::string formatted = SseCodecStateMachine::formatEvent(event);

  EXPECT_TRUE(formatted.find("id: 123\n") != std::string::npos);
  EXPECT_TRUE(formatted.find("data: Test data\n") != std::string::npos);
  EXPECT_TRUE(formatted.size() >= 2 &&
              formatted.substr(formatted.size() - 2) == "\n\n");
}

TEST_F(SseCodecStateMachineTest, FormatEventWithType) {
  SseEventData event;
  event.event = "message";
  event.data = "Test data";

  std::string formatted = SseCodecStateMachine::formatEvent(event);

  EXPECT_TRUE(formatted.find("event: message\n") != std::string::npos);
  EXPECT_TRUE(formatted.find("data: Test data\n") != std::string::npos);
}

TEST_F(SseCodecStateMachineTest, FormatEventWithRetry) {
  SseEventData event;
  event.retry = 5000;
  event.data = "Test data";

  std::string formatted = SseCodecStateMachine::formatEvent(event);

  EXPECT_TRUE(formatted.find("retry: 5000\n") != std::string::npos);
  EXPECT_TRUE(formatted.find("data: Test data\n") != std::string::npos);
}

TEST_F(SseCodecStateMachineTest, FormatEventWithMultilineData) {
  SseEventData event;
  event.data = "Line 1\nLine 2\nLine 3";

  std::string formatted = SseCodecStateMachine::formatEvent(event);

  EXPECT_TRUE(formatted.find("data: Line 1\n") != std::string::npos);
  EXPECT_TRUE(formatted.find("data: Line 2\n") != std::string::npos);
  EXPECT_TRUE(formatted.find("data: Line 3\n") != std::string::npos);
  EXPECT_TRUE(formatted.size() >= 2 &&
              formatted.substr(formatted.size() - 2) == "\n\n");
}

TEST_F(SseCodecStateMachineTest, FormatCompleteEvent) {
  SseEventData event;
  event.id = "456";
  event.event = "update";
  event.retry = 3000;
  event.data = "Complete event data";

  std::string formatted = SseCodecStateMachine::formatEvent(event);

  // Verify order and presence of all fields
  std::istringstream stream(formatted);
  std::string line;

  std::getline(stream, line);
  EXPECT_EQ(line, "id: 456");

  std::getline(stream, line);
  EXPECT_EQ(line, "event: update");

  std::getline(stream, line);
  EXPECT_EQ(line, "retry: 3000");

  std::getline(stream, line);
  EXPECT_EQ(line, "data: Complete event data");

  std::getline(stream, line);
  EXPECT_EQ(line, "");  // Blank line at end
}

// ===== Metrics Tests =====

TEST_F(SseCodecStateMachineTest, TransitionMetrics) {
  EXPECT_EQ(state_machine_->getTotalTransitions(), 0);
  EXPECT_EQ(state_machine_->getEventsSent(), 0);

  // Send multiple events
  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);

  for (int i = 0; i < 5; ++i) {
    state_machine_->handleEvent(SseCodecEvent::SendEvent);
    runFor(10ms);
    state_machine_->handleEvent(SseCodecEvent::EventSent);
    runFor(10ms);
  }

  EXPECT_GT(state_machine_->getTotalTransitions(), 0);
  EXPECT_EQ(state_machine_->getEventsSent(), 5);
}

TEST_F(SseCodecStateMachineTest, TimeInState) {
  auto initial_time = state_machine_->getTimeInCurrentState();
  EXPECT_GE(initial_time.count(), 0);

  std::this_thread::sleep_for(50ms);

  auto later_time = state_machine_->getTimeInCurrentState();
  EXPECT_GT(later_time, initial_time);
  EXPECT_GE(later_time.count(), 50);
}

// ===== State History Tests =====

TEST_F(SseCodecStateMachineTest, StateHistory) {
  // Perform several transitions
  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::EventSent);
  runFor(10ms);
  state_machine_->handleEvent(SseCodecEvent::CloseStream);
  runFor(10ms);

  auto history = state_machine_->getStateHistory();
  EXPECT_GE(history.size(), 4);

  // Verify history is in chronological order
  for (size_t i = 1; i < history.size(); ++i) {
    EXPECT_GE(history[i].timestamp, history[i - 1].timestamp);
  }
}

TEST_F(SseCodecStateMachineTest, StateHistoryLimit) {
  // Perform many transitions to exceed history limit
  for (int i = 0; i < 150; ++i) {
    state_machine_->handleEvent(SseCodecEvent::StartStream);
    runFor(1ms);
    state_machine_->handleEvent(SseCodecEvent::CloseStream);
    runFor(1ms);
    state_machine_->handleEvent(SseCodecEvent::Reset);
    runFor(1ms);
  }

  auto history = state_machine_->getStateHistory();
  // Should be limited to kMaxHistorySize (100)
  EXPECT_LE(history.size(), 100);
}

// ===== State Name Tests =====

TEST_F(SseCodecStateMachineTest, StateNames) {
  EXPECT_EQ(SseCodecStateMachine::getStateName(SseCodecState::Idle), "Idle");
  EXPECT_EQ(SseCodecStateMachine::getStateName(SseCodecState::StreamActive),
            "StreamActive");
  EXPECT_EQ(SseCodecStateMachine::getStateName(SseCodecState::SendingEvent),
            "SendingEvent");
  EXPECT_EQ(SseCodecStateMachine::getStateName(SseCodecState::Closed),
            "Closed");
  EXPECT_EQ(SseCodecStateMachine::getStateName(SseCodecState::Error), "Error");
}

TEST_F(SseCodecStateMachineTest, EventNames) {
  EXPECT_EQ(SseCodecStateMachine::getEventName(SseCodecEvent::StartStream),
            "StartStream");
  EXPECT_EQ(SseCodecStateMachine::getEventName(SseCodecEvent::SendEvent),
            "SendEvent");
  EXPECT_EQ(SseCodecStateMachine::getEventName(SseCodecEvent::EventSent),
            "EventSent");
  EXPECT_EQ(SseCodecStateMachine::getEventName(SseCodecEvent::KeepAliveTimer),
            "KeepAliveTimer");
  EXPECT_EQ(SseCodecStateMachine::getEventName(SseCodecEvent::StreamError),
            "StreamError");
  EXPECT_EQ(SseCodecStateMachine::getEventName(SseCodecEvent::CloseStream),
            "CloseStream");
  EXPECT_EQ(SseCodecStateMachine::getEventName(SseCodecEvent::Reset), "Reset");
}

// ===== Multiple Event Cycles Test =====

TEST_F(SseCodecStateMachineTest, MultipleEventCycles) {
  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);

  for (int i = 0; i < 10; ++i) {
    // Send event
    state_machine_->handleEvent(SseCodecEvent::SendEvent);
    runFor(10ms);
    expectState(SseCodecState::SendingEvent);

    // Complete event
    state_machine_->handleEvent(SseCodecEvent::EventSent);
    runFor(10ms);
    expectState(SseCodecState::StreamActive);
  }

  EXPECT_EQ(state_machine_->getEventsSent(), 10);
}

// ===== Stream Lifecycle Test =====

TEST_F(SseCodecStateMachineTest, CompleteStreamLifecycle) {
  // Start stream
  state_machine_->startStream();
  runFor(10ms);
  expectState(SseCodecState::StreamActive);

  // Send some events
  for (int i = 0; i < 3; ++i) {
    state_machine_->handleEvent(SseCodecEvent::SendEvent);
    runFor(10ms);
    state_machine_->handleEvent(SseCodecEvent::EventSent);
    runFor(10ms);
  }

  // Close stream
  state_machine_->closeStream();
  runFor(10ms);
  expectState(SseCodecState::Closed);

  // Reset
  state_machine_->handleEvent(SseCodecEvent::Reset);
  runFor(10ms);
  expectState(SseCodecState::Idle);

  // Can start new stream
  state_machine_->startStream();
  runFor(10ms);
  expectState(SseCodecState::StreamActive);
}

// ===== Clear Listeners Test =====

TEST_F(SseCodecStateMachineTest, ClearStateChangeListeners) {
  int call_count = 0;

  state_machine_->addStateChangeListener(
      [&call_count](const SseCodecStateTransitionContext&) { call_count++; });

  state_machine_->handleEvent(SseCodecEvent::StartStream);
  runFor(10ms);
  EXPECT_EQ(call_count, 1);

  state_machine_->clearStateChangeListeners();

  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);
  EXPECT_EQ(call_count, 1);  // Should not increase
}

// ===== Error Recovery Test =====

TEST_F(SseCodecStateMachineTest, ErrorRecovery) {
  state_machine_->startStream();
  runFor(10ms);

  // Cause error
  state_machine_->handleEvent(SseCodecEvent::StreamError);
  runFor(10ms);
  expectState(SseCodecState::Error);

  // Reset to recover
  state_machine_->handleEvent(SseCodecEvent::Reset);
  runFor(10ms);
  expectState(SseCodecState::Idle);

  // Can start new stream after recovery
  state_machine_->startStream();
  runFor(10ms);
  expectState(SseCodecState::StreamActive);
  EXPECT_TRUE(state_machine_->isStreaming());
}

// ===== Concurrent Operations Test =====

TEST_F(SseCodecStateMachineTest, PreventConcurrentSends) {
  state_machine_->startStream();
  runFor(10ms);

  // Start sending event
  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);
  expectState(SseCodecState::SendingEvent);

  // Try to send another event while still sending
  auto result = state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);

  // Should still be in SendingEvent state
  expectState(SseCodecState::SendingEvent);

  // Complete first event
  state_machine_->handleEvent(SseCodecEvent::EventSent);
  runFor(10ms);
  expectState(SseCodecState::StreamActive);

  // Now can send another
  state_machine_->handleEvent(SseCodecEvent::SendEvent);
  runFor(10ms);
  expectState(SseCodecState::SendingEvent);
}

}  // namespace
}  // namespace filter
}  // namespace mcp