/**
 * @file test_http_codec_state_machine.cc
 * @brief Comprehensive tests for HTTP codec state machine
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <thread>
#include <vector>

#include "mcp/filter/http_codec_state_machine.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/event/event_loop.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::Invoke;

class HttpCodecStateMachineTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create dispatcher using factory
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    
    // Run once to set thread_id_ and make isThreadSafe() return true
    // This allows timer creation to work properly
    dispatcher_->run(event::RunType::NonBlock);
    
    config_ = HttpCodecStateMachineConfig{};
    config_.header_timeout = 100ms;
    config_.body_timeout = 200ms;
    config_.idle_timeout = 300ms;
    config_.enable_keep_alive = true;
    
    state_machine_ = std::make_unique<HttpCodecStateMachine>(*dispatcher_, config_);
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
  void expectState(HttpCodecState expected) {
    EXPECT_EQ(state_machine_->currentState(), expected);
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  HttpCodecStateMachineConfig config_;
  std::unique_ptr<HttpCodecStateMachine> state_machine_;
  
  // Track state changes
  std::vector<HttpCodecStateTransitionContext> state_changes_;
  bool callback_called_ = false;
  bool callback_success_ = false;
};

// ===== Basic State Transition Tests =====

TEST_F(HttpCodecStateMachineTest, InitialState) {
  expectState(HttpCodecState::WaitingForRequest);
  EXPECT_TRUE(state_machine_->canReceiveRequest());
  EXPECT_FALSE(state_machine_->isReceivingRequestBody());
  EXPECT_FALSE(state_machine_->hasError());
}

TEST_F(HttpCodecStateMachineTest, RequestStartTransition) {
  expectState(HttpCodecState::WaitingForRequest);
  
  auto result = state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  EXPECT_TRUE(result.success);
  
  runFor(10ms);
  expectState(HttpCodecState::ReceivingRequestHeaders);
  EXPECT_FALSE(state_machine_->canReceiveRequest());
}

TEST_F(HttpCodecStateMachineTest, HeadersCompleteWithoutBody) {
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  expectState(HttpCodecState::ReceivingRequestHeaders);
  
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  expectState(HttpCodecState::SendingResponse);
}

TEST_F(HttpCodecStateMachineTest, HeadersCompleteWithBody) {
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  
  // Simulate request with body (e.g., Content-Length > 0)
  state_machine_->setExpectRequestBody(true);
  
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  expectState(HttpCodecState::ReceivingRequestBody);
  EXPECT_TRUE(state_machine_->isReceivingRequestBody());
}

TEST_F(HttpCodecStateMachineTest, BodyDataAndComplete) {
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  
  // Simulate request with body
  state_machine_->setExpectRequestBody(true);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  
  // Receive body data (stays in same state)
  state_machine_->handleEvent(HttpCodecEvent::BodyData);
  runFor(10ms);
  expectState(HttpCodecState::ReceivingRequestBody);
  
  // Complete message
  state_machine_->handleEvent(HttpCodecEvent::MessageComplete);
  runFor(10ms);
  expectState(HttpCodecState::SendingResponse);
}

TEST_F(HttpCodecStateMachineTest, ResponseCompleteWithKeepAlive) {
  // Go through full request cycle
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  
  expectState(HttpCodecState::SendingResponse);
  
  // Complete response with keep-alive enabled
  state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
  runFor(10ms);
  expectState(HttpCodecState::WaitingForRequest);
  EXPECT_TRUE(state_machine_->canReceiveRequest());
}

TEST_F(HttpCodecStateMachineTest, ResponseCompleteWithoutKeepAlive) {
  // Disable keep-alive for this test
  config_.enable_keep_alive = false;
  state_machine_ = std::make_unique<HttpCodecStateMachine>(*dispatcher_, config_);
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  
  state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
  runFor(10ms);
  expectState(HttpCodecState::Closed);
}

// ===== Error Handling Tests =====

TEST_F(HttpCodecStateMachineTest, ParseErrorInHeaders) {
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  expectState(HttpCodecState::ReceivingRequestHeaders);
  
  state_machine_->handleEvent(HttpCodecEvent::ParseError);
  runFor(10ms);
  expectState(HttpCodecState::Error);
  EXPECT_TRUE(state_machine_->hasError());
}

TEST_F(HttpCodecStateMachineTest, ParseErrorInBody) {
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  
  // Simulate request with body
  state_machine_->setExpectRequestBody(true);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  expectState(HttpCodecState::ReceivingRequestBody);
  
  state_machine_->handleEvent(HttpCodecEvent::ParseError);
  runFor(10ms);
  expectState(HttpCodecState::Error);
}

TEST_F(HttpCodecStateMachineTest, ResetAfterError) {
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::ParseError);
  runFor(10ms);
  expectState(HttpCodecState::Error);
  
  state_machine_->handleEvent(HttpCodecEvent::Reset);
  runFor(10ms);
  expectState(HttpCodecState::WaitingForRequest);
  EXPECT_FALSE(state_machine_->hasError());
}

TEST_F(HttpCodecStateMachineTest, CloseConnection) {
  expectState(HttpCodecState::WaitingForRequest);
  
  state_machine_->handleEvent(HttpCodecEvent::Close);
  runFor(10ms);
  expectState(HttpCodecState::Closed);
}

TEST_F(HttpCodecStateMachineTest, ResetAfterClose) {
  state_machine_->handleEvent(HttpCodecEvent::Close);
  runFor(10ms);
  expectState(HttpCodecState::Closed);
  
  state_machine_->handleEvent(HttpCodecEvent::Reset);
  runFor(10ms);
  expectState(HttpCodecState::WaitingForRequest);
}

// ===== Timeout Tests =====

TEST_F(HttpCodecStateMachineTest, HeaderTimeout) {
  bool error_called = false;
  config_.error_callback = [&error_called](const std::string& error) {
    error_called = true;
    EXPECT_TRUE(error.find("header timeout") != std::string::npos);
  };
  state_machine_ = std::make_unique<HttpCodecStateMachine>(*dispatcher_, config_);
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  expectState(HttpCodecState::ReceivingRequestHeaders);
  
  // Wait for header timeout
  runFor(config_.header_timeout + 50ms);
  
  EXPECT_TRUE(error_called);
  expectState(HttpCodecState::Error);
}

TEST_F(HttpCodecStateMachineTest, BodyTimeout) {
  bool error_called = false;
  config_.error_callback = [&error_called](const std::string& error) {
    error_called = true;
    EXPECT_TRUE(error.find("body timeout") != std::string::npos);
  };
  state_machine_ = std::make_unique<HttpCodecStateMachine>(*dispatcher_, config_);
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  
  // Simulate request with body
  state_machine_->setExpectRequestBody(true);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  expectState(HttpCodecState::ReceivingRequestBody);
  
  // Wait for body timeout
  runFor(config_.body_timeout + 50ms);
  
  EXPECT_TRUE(error_called);
  expectState(HttpCodecState::Error);
}

TEST_F(HttpCodecStateMachineTest, IdleTimeout) {
  expectState(HttpCodecState::WaitingForRequest);
  
  // Wait for idle timeout
  runFor(config_.idle_timeout + 50ms);
  
  expectState(HttpCodecState::Closed);
}

TEST_F(HttpCodecStateMachineTest, NoTimeoutDuringActiveProcessing) {
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  
  // Send headers complete before timeout
  runFor(config_.header_timeout / 2);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  
  // Should not have timed out
  expectState(HttpCodecState::SendingResponse);
  EXPECT_FALSE(state_machine_->hasError());
}

// ===== State Change Listener Tests =====

TEST_F(HttpCodecStateMachineTest, StateChangeListeners) {
  std::vector<HttpCodecStateTransitionContext> transitions;
  
  state_machine_->addStateChangeListener(
    [&transitions](const HttpCodecStateTransitionContext& ctx) {
      transitions.push_back(ctx);
    });
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
  runFor(10ms);
  
  ASSERT_GE(transitions.size(), 3);
  EXPECT_EQ(transitions[0].from_state, HttpCodecState::WaitingForRequest);
  EXPECT_EQ(transitions[0].to_state, HttpCodecState::ReceivingRequestHeaders);
  EXPECT_EQ(transitions[1].from_state, HttpCodecState::ReceivingRequestHeaders);
  EXPECT_EQ(transitions[1].to_state, HttpCodecState::SendingResponse);
  EXPECT_EQ(transitions[2].from_state, HttpCodecState::SendingResponse);
  EXPECT_EQ(transitions[2].to_state, HttpCodecState::WaitingForRequest);
}

TEST_F(HttpCodecStateMachineTest, StateChangeCallback) {
  bool callback_invoked = false;
  config_.state_change_callback = 
    [&callback_invoked](const HttpCodecStateTransitionContext& ctx) {
      callback_invoked = true;
      EXPECT_FALSE(ctx.reason.empty());
    };
  state_machine_ = std::make_unique<HttpCodecStateMachine>(*dispatcher_, config_);
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  
  EXPECT_TRUE(callback_invoked);
}

// ===== Entry/Exit Action Tests =====

TEST_F(HttpCodecStateMachineTest, EntryExitActions) {
  bool entry_called = false;
  bool exit_called = false;
  
  state_machine_->setEntryAction(HttpCodecState::ReceivingRequestHeaders,
    [&entry_called](HttpCodecState state, std::function<void()> done) {
      entry_called = true;
      done();
    });
  
  state_machine_->setExitAction(HttpCodecState::WaitingForRequest,
    [&exit_called](HttpCodecState state, std::function<void()> done) {
      exit_called = true;
      done();
    });
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  
  EXPECT_TRUE(entry_called);
  EXPECT_TRUE(exit_called);
}

// ===== Async Callback Tests =====

TEST_F(HttpCodecStateMachineTest, AsyncCompletionCallback) {
  bool callback_invoked = false;
  bool success = false;
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart,
    [&callback_invoked, &success](bool result) {
      callback_invoked = true;
      success = result;
    });
  
  runFor(10ms);
  
  EXPECT_TRUE(callback_invoked);
  EXPECT_TRUE(success);
  expectState(HttpCodecState::ReceivingRequestHeaders);
}

TEST_F(HttpCodecStateMachineTest, AsyncCallbackOnInvalidTransition) {
  bool callback_invoked = false;
  bool success = false;
  
  // Try invalid transition from Error state
  state_machine_->forceTransition(HttpCodecState::Error, "test");
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart,
    [&callback_invoked, &success](bool result) {
      callback_invoked = true;
      success = result;
    });
  
  runFor(10ms);
  
  EXPECT_TRUE(callback_invoked);
  EXPECT_FALSE(success);  // Should fail for invalid transition
}

// ===== Transition Validation Tests =====

TEST_F(HttpCodecStateMachineTest, ValidTransitions) {
  EXPECT_TRUE(state_machine_->isTransitionValid(
    HttpCodecState::WaitingForRequest, 
    HttpCodecState::ReceivingRequestHeaders,
    HttpCodecEvent::RequestStart));
  
  EXPECT_TRUE(state_machine_->isTransitionValid(
    HttpCodecState::ReceivingRequestHeaders,
    HttpCodecState::ReceivingRequestBody,
    HttpCodecEvent::HeadersComplete));
  
  EXPECT_TRUE(state_machine_->isTransitionValid(
    HttpCodecState::SendingResponse,
    HttpCodecState::WaitingForRequest,
    HttpCodecEvent::ResponseComplete));
}

TEST_F(HttpCodecStateMachineTest, InvalidTransitions) {
  EXPECT_FALSE(state_machine_->isTransitionValid(
    HttpCodecState::WaitingForRequest,
    HttpCodecState::SendingResponse,
    HttpCodecEvent::ResponseComplete));
  
  EXPECT_FALSE(state_machine_->isTransitionValid(
    HttpCodecState::Closed,
    HttpCodecState::ReceivingRequestHeaders,
    HttpCodecEvent::RequestStart));
}

TEST_F(HttpCodecStateMachineTest, CustomValidator) {
  bool validator_called = false;
  
  state_machine_->addTransitionValidator(
    [&validator_called](HttpCodecState from, HttpCodecState to) {
      validator_called = true;
      // Block transition to Error state
      return to != HttpCodecState::Error;
    });
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  
  EXPECT_TRUE(validator_called);
  
  // Try to transition to error - should be blocked
  state_machine_->handleEvent(HttpCodecEvent::ParseError);
  runFor(10ms);
  
  // Should still be in ReceivingRequestHeaders due to validator
  expectState(HttpCodecState::ReceivingRequestHeaders);
}

// ===== Force Transition Tests =====

TEST_F(HttpCodecStateMachineTest, ForceTransition) {
  expectState(HttpCodecState::WaitingForRequest);
  
  state_machine_->forceTransition(HttpCodecState::SendingResponse, "forced test");
  
  expectState(HttpCodecState::SendingResponse);
  
  // Verify state history contains forced transition
  auto history = state_machine_->getStateHistory();
  ASSERT_FALSE(history.empty());
  EXPECT_TRUE(history.back().reason.find("forced") != std::string::npos);
}

// ===== Schedule Transition Tests =====

TEST_F(HttpCodecStateMachineTest, ScheduledTransition) {
  expectState(HttpCodecState::WaitingForRequest);
  
  state_machine_->scheduleTransition(
    HttpCodecState::ReceivingRequestHeaders,
    HttpCodecEvent::RequestStart,
    "scheduled test");
  
  // Should not transition immediately
  expectState(HttpCodecState::WaitingForRequest);
  
  // Run dispatcher to process scheduled transition
  runFor(10ms);
  
  expectState(HttpCodecState::ReceivingRequestHeaders);
}

// ===== Metrics Tests =====

TEST_F(HttpCodecStateMachineTest, TransitionMetrics) {
  EXPECT_EQ(state_machine_->getTotalTransitions(), 0);
  EXPECT_EQ(state_machine_->getRequestsProcessed(), 0);
  
  // Complete a full request cycle
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
  runFor(10ms);
  
  EXPECT_GT(state_machine_->getTotalTransitions(), 0);
  EXPECT_EQ(state_machine_->getRequestsProcessed(), 1);
}

TEST_F(HttpCodecStateMachineTest, TimeInState) {
  auto initial_time = state_machine_->getTimeInCurrentState();
  EXPECT_GE(initial_time.count(), 0);
  
  std::this_thread::sleep_for(50ms);
  
  auto later_time = state_machine_->getTimeInCurrentState();
  EXPECT_GT(later_time, initial_time);
  EXPECT_GE(later_time.count(), 50);
}

// ===== State History Tests =====

TEST_F(HttpCodecStateMachineTest, StateHistory) {
  // Perform several transitions
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
  runFor(10ms);
  
  auto history = state_machine_->getStateHistory();
  EXPECT_GE(history.size(), 3);
  
  // Verify history is in chronological order
  for (size_t i = 1; i < history.size(); ++i) {
    EXPECT_GE(history[i].timestamp, history[i-1].timestamp);
  }
}

TEST_F(HttpCodecStateMachineTest, StateHistoryLimit) {
  // Perform many transitions to exceed history limit
  for (int i = 0; i < 150; ++i) {
    state_machine_->handleEvent(HttpCodecEvent::RequestStart);
    runFor(1ms);
    state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
    runFor(1ms);
    state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
    runFor(1ms);
  }
  
  auto history = state_machine_->getStateHistory();
  // Should be limited to kMaxHistorySize (100)
  EXPECT_LE(history.size(), 100);
}

// ===== Reset For Next Request Tests =====

TEST_F(HttpCodecStateMachineTest, ResetForNextRequestFromResponse) {
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  expectState(HttpCodecState::SendingResponse);
  
  bool callback_invoked = false;
  state_machine_->resetForNextRequest([&callback_invoked](bool success) {
    callback_invoked = true;
    EXPECT_TRUE(success);
  });
  
  runFor(10ms);
  
  EXPECT_TRUE(callback_invoked);
  expectState(HttpCodecState::WaitingForRequest);
}

TEST_F(HttpCodecStateMachineTest, ResetForNextRequestFromError) {
  state_machine_->forceTransition(HttpCodecState::Error, "test");
  
  state_machine_->resetForNextRequest();
  runFor(10ms);
  
  // Should close when in error state
  expectState(HttpCodecState::Closed);
}

// ===== State Name Tests =====

TEST_F(HttpCodecStateMachineTest, StateNames) {
  EXPECT_EQ(HttpCodecStateMachine::getStateName(HttpCodecState::WaitingForRequest), 
            "WaitingForRequest");
  EXPECT_EQ(HttpCodecStateMachine::getStateName(HttpCodecState::ReceivingRequestHeaders),
            "ReceivingRequestHeaders");
  EXPECT_EQ(HttpCodecStateMachine::getStateName(HttpCodecState::ReceivingRequestBody),
            "ReceivingRequestBody");
  EXPECT_EQ(HttpCodecStateMachine::getStateName(HttpCodecState::SendingResponse),
            "SendingResponse");
  EXPECT_EQ(HttpCodecStateMachine::getStateName(HttpCodecState::Closed),
            "Closed");
  EXPECT_EQ(HttpCodecStateMachine::getStateName(HttpCodecState::Error),
            "Error");
}

TEST_F(HttpCodecStateMachineTest, EventNames) {
  EXPECT_EQ(HttpCodecStateMachine::getEventName(HttpCodecEvent::RequestStart),
            "RequestStart");
  EXPECT_EQ(HttpCodecStateMachine::getEventName(HttpCodecEvent::HeadersComplete),
            "HeadersComplete");
  EXPECT_EQ(HttpCodecStateMachine::getEventName(HttpCodecEvent::MessageComplete),
            "MessageComplete");
  EXPECT_EQ(HttpCodecStateMachine::getEventName(HttpCodecEvent::ResponseComplete),
            "ResponseComplete");
  EXPECT_EQ(HttpCodecStateMachine::getEventName(HttpCodecEvent::ParseError),
            "ParseError");
  EXPECT_EQ(HttpCodecStateMachine::getEventName(HttpCodecEvent::Timeout),
            "Timeout");
}

// ===== Multiple Request Cycles Test =====

TEST_F(HttpCodecStateMachineTest, MultipleRequestCycles) {
  for (int i = 0; i < 5; ++i) {
    // Start request
    state_machine_->handleEvent(HttpCodecEvent::RequestStart);
    runFor(10ms);
    expectState(HttpCodecState::ReceivingRequestHeaders);
    
    // Complete headers
    state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
    runFor(10ms);
    expectState(HttpCodecState::SendingResponse);
    
    // Complete response
    state_machine_->handleEvent(HttpCodecEvent::ResponseComplete);
    runFor(10ms);
    expectState(HttpCodecState::WaitingForRequest);
  }
  
  EXPECT_EQ(state_machine_->getRequestsProcessed(), 5);
}

// ===== Clear Listeners Test =====

TEST_F(HttpCodecStateMachineTest, ClearStateChangeListeners) {
  int call_count = 0;
  
  state_machine_->addStateChangeListener(
    [&call_count](const HttpCodecStateTransitionContext&) {
      call_count++;
    });
  
  state_machine_->handleEvent(HttpCodecEvent::RequestStart);
  runFor(10ms);
  EXPECT_EQ(call_count, 1);
  
  state_machine_->clearStateChangeListeners();
  
  state_machine_->handleEvent(HttpCodecEvent::HeadersComplete);
  runFor(10ms);
  EXPECT_EQ(call_count, 1);  // Should not increase
}

} // namespace
} // namespace filter
} // namespace mcp