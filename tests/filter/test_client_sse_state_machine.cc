/**
 * @file test_client_sse_state_machine.cc
 * @brief Comprehensive tests for client-side SSE connection state machine
 *
 * Tests cover:
 * - Basic state transitions (happy path, streamable HTTP path)
 * - Invalid transition rejection
 * - Error handling from every negotiating state
 * - Negotiation timeout (fire, cancel, disable, wrong-state)
 * - State history recording and cap
 * - State change callbacks (listener, config, clear)
 * - Error callbacks (error event, timeout)
 * - Completion callbacks (success, failure)
 * - Query methods (isNegotiating, canSendPost, isReady, hasError)
 * - Utility methods (getStateName, getEventName)
 * - Metrics (transition count, time in state)
 * - Reentrancy from callbacks
 * - ForceTransition and ScheduleTransition
 *
 * All tests use a real libevent dispatcher (no mocks), following the
 * pattern from test_sse_codec_state_machine.cc.
 */

#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/event/event_loop.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/filter/client_sse_state_machine.h"

namespace mcp {
namespace filter {
namespace {

using namespace std::chrono_literals;

class ClientSseStateMachineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto factory = event::createLibeventDispatcherFactory();
    dispatcher_ = factory->createDispatcher("test");
    // Run once to initialize the dispatcher's thread ID so that
    // timer creation works and isThreadSafe() returns true.
    dispatcher_->run(event::RunType::NonBlock);

    config_.negotiation_timeout = 200ms;
  }

  void TearDown() override {
    state_machine_.reset();
    dispatcher_.reset();
  }

  // Helper: pump the event loop for the given duration
  void runFor(std::chrono::milliseconds duration) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
      dispatcher_->run(event::RunType::NonBlock);
      std::this_thread::sleep_for(1ms);
    }
  }

  void expectState(ClientSseState expected) {
    EXPECT_EQ(state_machine_->currentState(), expected);
  }

  // Helper: create the state machine with the current config
  void createSseMachine() {
    state_machine_ = std::make_unique<ClientSseStateMachine>(
        *dispatcher_, config_, /*use_sse=*/true);
  }

  void createStreamableHttpMachine() {
    state_machine_ = std::make_unique<ClientSseStateMachine>(
        *dispatcher_, config_, /*use_sse=*/false);
  }

  // Helper: walk the machine to a given state via the happy path
  void walkTo(ClientSseState target) {
    if (!state_machine_) {
      createSseMachine();
    }
    if (target == ClientSseState::Idle) return;

    state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
    runFor(10ms);
    if (target == ClientSseState::WaitingForGetSent) return;

    state_machine_->handleEvent(ClientSseEvent::GetSent);
    runFor(10ms);
    if (target == ClientSseState::WaitingForEndpoint) return;

    state_machine_->handleEvent(ClientSseEvent::EndpointReceived);
    runFor(10ms);
    if (target == ClientSseState::EndpointReceived) return;

    state_machine_->handleEvent(ClientSseEvent::StreamStarted);
    runFor(10ms);
    if (target == ClientSseState::Active) return;

    if (target == ClientSseState::Error) {
      state_machine_->handleEvent(ClientSseEvent::StreamError);
      runFor(10ms);
      return;
    }

    if (target == ClientSseState::Closed) {
      state_machine_->handleEvent(ClientSseEvent::Close);
      runFor(10ms);
      return;
    }
  }

  std::unique_ptr<event::Dispatcher> dispatcher_;
  ClientSseStateMachineConfig config_;
  std::unique_ptr<ClientSseStateMachine> state_machine_;
};

// ===== Basic State Transition Tests =====

TEST_F(ClientSseStateMachineTest, InitialState_SseMode) {
  createSseMachine();
  expectState(ClientSseState::Idle);
  EXPECT_FALSE(state_machine_->isNegotiating());
  EXPECT_FALSE(state_machine_->isReady());
  EXPECT_FALSE(state_machine_->canSendPost());
  EXPECT_FALSE(state_machine_->hasError());
}

TEST_F(ClientSseStateMachineTest, InitialState_StreamableHttpMode) {
  createStreamableHttpMachine();
  expectState(ClientSseState::StreamableHttp);
  EXPECT_FALSE(state_machine_->isNegotiating());
  EXPECT_FALSE(state_machine_->isReady());
  // Streamable HTTP can always POST directly
  EXPECT_TRUE(state_machine_->canSendPost());
  EXPECT_FALSE(state_machine_->hasError());
}

// ===== Happy Path Tests =====

TEST_F(ClientSseStateMachineTest, FullNegotiationHappyPath) {
  createSseMachine();

  auto r1 = state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  EXPECT_TRUE(r1.success);
  runFor(10ms);
  expectState(ClientSseState::WaitingForGetSent);

  auto r2 = state_machine_->handleEvent(ClientSseEvent::GetSent);
  EXPECT_TRUE(r2.success);
  runFor(10ms);
  expectState(ClientSseState::WaitingForEndpoint);

  auto r3 = state_machine_->handleEvent(ClientSseEvent::EndpointReceived);
  EXPECT_TRUE(r3.success);
  runFor(10ms);
  expectState(ClientSseState::EndpointReceived);

  auto r4 = state_machine_->handleEvent(ClientSseEvent::StreamStarted);
  EXPECT_TRUE(r4.success);
  runFor(10ms);
  expectState(ClientSseState::Active);
  EXPECT_TRUE(state_machine_->isReady());
  EXPECT_TRUE(state_machine_->canSendPost());
  EXPECT_FALSE(state_machine_->isNegotiating());

  auto r5 = state_machine_->handleEvent(ClientSseEvent::Close);
  EXPECT_TRUE(r5.success);
  runFor(10ms);
  expectState(ClientSseState::Closed);
}

TEST_F(ClientSseStateMachineTest, StreamableHttpHappyPath) {
  createStreamableHttpMachine();
  expectState(ClientSseState::StreamableHttp);

  auto r = state_machine_->handleEvent(ClientSseEvent::Close);
  EXPECT_TRUE(r.success);
  runFor(10ms);
  expectState(ClientSseState::Closed);
}

TEST_F(ClientSseStateMachineTest, EndpointReceivedToActive) {
  walkTo(ClientSseState::EndpointReceived);
  state_machine_->handleEvent(ClientSseEvent::StreamStarted);
  runFor(10ms);
  expectState(ClientSseState::Active);
  EXPECT_TRUE(state_machine_->isReady());
}

// ===== Invalid Transition Tests =====

TEST_F(ClientSseStateMachineTest, InvalidTransition_IdleToActive) {
  createSseMachine();
  auto r = state_machine_->handleEvent(ClientSseEvent::StreamStarted);
  EXPECT_FALSE(r.success);
  expectState(ClientSseState::Idle);
}

TEST_F(ClientSseStateMachineTest, InvalidTransition_IdleToEndpointReceived) {
  createSseMachine();
  auto r = state_machine_->handleEvent(ClientSseEvent::EndpointReceived);
  EXPECT_FALSE(r.success);
  expectState(ClientSseState::Idle);
}

TEST_F(ClientSseStateMachineTest, InvalidTransition_ActiveToIdle) {
  walkTo(ClientSseState::Active);
  auto r = state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  EXPECT_FALSE(r.success);
  expectState(ClientSseState::Active);
}

TEST_F(ClientSseStateMachineTest, InvalidTransition_ClosedToAny) {
  walkTo(ClientSseState::Closed);
  auto r = state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  EXPECT_FALSE(r.success);
  expectState(ClientSseState::Closed);
}

TEST_F(ClientSseStateMachineTest,
       InvalidTransition_StreamableHttpToWaitingForGetSent) {
  createStreamableHttpMachine();
  auto r = state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  EXPECT_FALSE(r.success);
  expectState(ClientSseState::StreamableHttp);
}

// ===== Error Handling Tests =====

TEST_F(ClientSseStateMachineTest, ErrorFromIdle) {
  createSseMachine();
  state_machine_->handleEvent(ClientSseEvent::StreamError);
  runFor(10ms);
  expectState(ClientSseState::Error);
  EXPECT_FALSE(state_machine_->isReady());
  EXPECT_TRUE(state_machine_->hasError());
}

TEST_F(ClientSseStateMachineTest, ErrorFromWaitingForGetSent) {
  walkTo(ClientSseState::WaitingForGetSent);
  state_machine_->handleEvent(ClientSseEvent::StreamError);
  runFor(10ms);
  expectState(ClientSseState::Error);
}

TEST_F(ClientSseStateMachineTest, ErrorFromWaitingForEndpoint) {
  walkTo(ClientSseState::WaitingForEndpoint);
  state_machine_->handleEvent(ClientSseEvent::StreamError);
  runFor(10ms);
  expectState(ClientSseState::Error);
}

TEST_F(ClientSseStateMachineTest, ErrorFromEndpointReceived) {
  walkTo(ClientSseState::EndpointReceived);
  state_machine_->handleEvent(ClientSseEvent::StreamError);
  runFor(10ms);
  expectState(ClientSseState::Error);
}

TEST_F(ClientSseStateMachineTest, ErrorFromActive) {
  walkTo(ClientSseState::Active);
  state_machine_->handleEvent(ClientSseEvent::StreamError);
  runFor(10ms);
  expectState(ClientSseState::Error);
}

TEST_F(ClientSseStateMachineTest, ErrorFromStreamableHttp) {
  createStreamableHttpMachine();
  state_machine_->handleEvent(ClientSseEvent::StreamError);
  runFor(10ms);
  expectState(ClientSseState::Error);
}

TEST_F(ClientSseStateMachineTest, ErrorToClosedIsValid) {
  walkTo(ClientSseState::Error);
  auto r = state_machine_->handleEvent(ClientSseEvent::Close);
  EXPECT_TRUE(r.success);
  runFor(10ms);
  expectState(ClientSseState::Closed);
}

TEST_F(ClientSseStateMachineTest, ErrorToAnythingElseIsInvalid) {
  walkTo(ClientSseState::Error);
  auto r = state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  EXPECT_FALSE(r.success);
  expectState(ClientSseState::Error);
}

// ===== Timeout Tests =====

TEST_F(ClientSseStateMachineTest,
       NegotiationTimeout_FiresFromWaitingForEndpoint) {
  bool error_called = false;
  config_.error_callback = [&error_called](const std::string& msg) {
    error_called = true;
    EXPECT_FALSE(msg.empty());
  };
  createSseMachine();
  walkTo(ClientSseState::WaitingForEndpoint);

  runFor(config_.negotiation_timeout + 50ms);

  EXPECT_TRUE(error_called);
  expectState(ClientSseState::Error);
}

TEST_F(ClientSseStateMachineTest, NegotiationTimeout_DoesNotFireFromActive) {
  createSseMachine();
  // Walk all the way to Active quickly (before timeout)
  walkTo(ClientSseState::Active);

  // Wait past the timeout duration
  runFor(config_.negotiation_timeout + 50ms);

  // Should still be Active — timer was cancelled when leaving
  // WaitingForEndpoint
  expectState(ClientSseState::Active);
}

TEST_F(ClientSseStateMachineTest,
       NegotiationTimeout_CancelledOnEndpointReceived) {
  createSseMachine();
  walkTo(ClientSseState::WaitingForEndpoint);

  // Wait half the timeout
  runFor(config_.negotiation_timeout / 2);

  // Receive the endpoint (cancels timer)
  state_machine_->handleEvent(ClientSseEvent::EndpointReceived);
  runFor(10ms);
  expectState(ClientSseState::EndpointReceived);

  // Wait past the original timeout — timer should have been cancelled
  runFor(config_.negotiation_timeout);
  EXPECT_NE(state_machine_->currentState(), ClientSseState::Error);
}

TEST_F(ClientSseStateMachineTest, NegotiationTimeout_DisabledWhenZero) {
  config_.negotiation_timeout = 0ms;
  createSseMachine();
  walkTo(ClientSseState::WaitingForEndpoint);

  runFor(500ms);

  // Should still be waiting — no timer armed
  expectState(ClientSseState::WaitingForEndpoint);
}

TEST_F(ClientSseStateMachineTest,
       NegotiationTimeout_DoesNotFireFromWaitingForGetSent) {
  createSseMachine();
  state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  runFor(10ms);
  expectState(ClientSseState::WaitingForGetSent);

  // Wait past timeout — timer only starts on WaitingForEndpoint entry
  runFor(config_.negotiation_timeout + 50ms);

  expectState(ClientSseState::WaitingForGetSent);
}

// ===== State History Tests =====

TEST_F(ClientSseStateMachineTest, StateHistoryRecordsAllTransitions) {
  createSseMachine();
  walkTo(ClientSseState::Closed);

  const auto& history = state_machine_->getStateHistory();
  // Idle -> WaitingForGetSent -> WaitingForEndpoint -> EndpointReceived
  //      -> Active -> Closed = 5 transitions
  ASSERT_GE(history.size(), 5u);

  EXPECT_EQ(history[0].from_state, ClientSseState::Idle);
  EXPECT_EQ(history[0].to_state, ClientSseState::WaitingForGetSent);

  for (const auto& entry : history) {
    EXPECT_FALSE(entry.reason.empty());
    EXPECT_GE(entry.time_in_previous_state.count(), 0);
  }
}

// ===== Callback Tests =====

TEST_F(ClientSseStateMachineTest,
       StateChangeCallback_InvokedOnEveryTransition) {
  createSseMachine();

  std::vector<ClientSseTransitionContext> transitions;
  state_machine_->addStateChangeListener(
      [&transitions](const ClientSseTransitionContext& ctx) {
        transitions.push_back(ctx);
      });

  walkTo(ClientSseState::Active);

  // Idle -> WaitingForGetSent -> WaitingForEndpoint -> EndpointReceived
  //      -> Active = 4 transitions
  ASSERT_EQ(transitions.size(), 4u);
  EXPECT_EQ(transitions[0].from_state, ClientSseState::Idle);
  EXPECT_EQ(transitions[0].to_state, ClientSseState::WaitingForGetSent);
  EXPECT_EQ(transitions[3].from_state, ClientSseState::EndpointReceived);
  EXPECT_EQ(transitions[3].to_state, ClientSseState::Active);
}

TEST_F(ClientSseStateMachineTest, StateChangeCallback_ConfigCallback) {
  bool config_callback_invoked = false;
  config_.state_change_callback =
      [&config_callback_invoked](const ClientSseTransitionContext& ctx) {
        config_callback_invoked = true;
        EXPECT_FALSE(ctx.reason.empty());
      };
  createSseMachine();

  state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  runFor(10ms);

  EXPECT_TRUE(config_callback_invoked);
}

TEST_F(ClientSseStateMachineTest, StateChangeCallback_ClearListeners) {
  createSseMachine();

  int call_count = 0;
  state_machine_->addStateChangeListener(
      [&call_count](const ClientSseTransitionContext&) { call_count++; });

  state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  runFor(10ms);
  EXPECT_EQ(call_count, 1);

  state_machine_->clearStateChangeListeners();

  state_machine_->handleEvent(ClientSseEvent::GetSent);
  runFor(10ms);
  // Should NOT have been called again
  EXPECT_EQ(call_count, 1);
}

TEST_F(ClientSseStateMachineTest, ErrorCallback_InvokedOnError) {
  bool error_called = false;
  std::string error_msg;
  config_.error_callback = [&](const std::string& msg) {
    error_called = true;
    error_msg = msg;
  };
  createSseMachine();

  walkTo(ClientSseState::WaitingForEndpoint);
  state_machine_->handleEvent(ClientSseEvent::StreamError);
  runFor(10ms);

  EXPECT_TRUE(error_called);
  EXPECT_FALSE(error_msg.empty());
}

TEST_F(ClientSseStateMachineTest, ErrorCallback_InvokedOnTimeout) {
  bool error_called = false;
  config_.error_callback = [&error_called](const std::string&) {
    error_called = true;
  };
  createSseMachine();
  walkTo(ClientSseState::WaitingForEndpoint);

  runFor(config_.negotiation_timeout + 50ms);

  EXPECT_TRUE(error_called);
  expectState(ClientSseState::Error);
}

// ===== Completion Callback Tests =====

TEST_F(ClientSseStateMachineTest, CompletionCallback_SuccessfulTransition) {
  createSseMachine();
  bool cb_called = false;
  bool cb_success = false;
  state_machine_->handleEvent(ClientSseEvent::ConnectionReady,
                              [&](bool s) {
                                cb_called = true;
                                cb_success = s;
                              });
  runFor(10ms);
  EXPECT_TRUE(cb_called);
  EXPECT_TRUE(cb_success);
}

TEST_F(ClientSseStateMachineTest, CompletionCallback_FailedTransition) {
  createSseMachine();
  bool cb_called = false;
  bool cb_success = true;
  state_machine_->handleEvent(ClientSseEvent::StreamStarted,
                              [&](bool s) {
                                cb_called = true;
                                cb_success = s;
                              });
  runFor(10ms);
  EXPECT_TRUE(cb_called);
  EXPECT_FALSE(cb_success);
}

// ===== Query Method Tests =====

TEST_F(ClientSseStateMachineTest, IsNegotiating_TrueForIntermediateStates) {
  createSseMachine();

  state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  runFor(10ms);
  EXPECT_TRUE(state_machine_->isNegotiating());  // WaitingForGetSent

  state_machine_->handleEvent(ClientSseEvent::GetSent);
  runFor(10ms);
  EXPECT_TRUE(state_machine_->isNegotiating());  // WaitingForEndpoint

  state_machine_->handleEvent(ClientSseEvent::EndpointReceived);
  runFor(10ms);
  EXPECT_TRUE(state_machine_->isNegotiating());  // EndpointReceived
}

TEST_F(ClientSseStateMachineTest, IsNegotiating_FalseForTerminalAndActive) {
  // Idle
  createSseMachine();
  EXPECT_FALSE(state_machine_->isNegotiating());

  // StreamableHttp
  createStreamableHttpMachine();
  EXPECT_FALSE(state_machine_->isNegotiating());

  // Active
  createSseMachine();
  walkTo(ClientSseState::Active);
  EXPECT_FALSE(state_machine_->isNegotiating());

  // Error
  createSseMachine();
  walkTo(ClientSseState::Error);
  EXPECT_FALSE(state_machine_->isNegotiating());

  // Closed
  createSseMachine();
  walkTo(ClientSseState::Closed);
  EXPECT_FALSE(state_machine_->isNegotiating());
}

TEST_F(ClientSseStateMachineTest, CanSendPost_TrueOnlyWhenReady) {
  // Active
  createSseMachine();
  walkTo(ClientSseState::Active);
  EXPECT_TRUE(state_machine_->canSendPost());

  // StreamableHttp
  createStreamableHttpMachine();
  EXPECT_TRUE(state_machine_->canSendPost());

  // All others should be false
  createSseMachine();
  EXPECT_FALSE(state_machine_->canSendPost());  // Idle

  createSseMachine();
  walkTo(ClientSseState::WaitingForGetSent);
  EXPECT_FALSE(state_machine_->canSendPost());

  createSseMachine();
  walkTo(ClientSseState::WaitingForEndpoint);
  EXPECT_FALSE(state_machine_->canSendPost());

  createSseMachine();
  walkTo(ClientSseState::EndpointReceived);
  EXPECT_FALSE(state_machine_->canSendPost());

  createSseMachine();
  walkTo(ClientSseState::Error);
  EXPECT_FALSE(state_machine_->canSendPost());

  createSseMachine();
  walkTo(ClientSseState::Closed);
  EXPECT_FALSE(state_machine_->canSendPost());
}

// ===== Utility Tests =====

TEST_F(ClientSseStateMachineTest, GetStateName_AllStatesNamed) {
  EXPECT_NE(ClientSseStateMachine::getStateName(ClientSseState::StreamableHttp),
            "Unknown");
  EXPECT_NE(ClientSseStateMachine::getStateName(ClientSseState::Idle),
            "Unknown");
  EXPECT_NE(
      ClientSseStateMachine::getStateName(ClientSseState::WaitingForGetSent),
      "Unknown");
  EXPECT_NE(
      ClientSseStateMachine::getStateName(ClientSseState::WaitingForEndpoint),
      "Unknown");
  EXPECT_NE(
      ClientSseStateMachine::getStateName(ClientSseState::EndpointReceived),
      "Unknown");
  EXPECT_NE(ClientSseStateMachine::getStateName(ClientSseState::Active),
            "Unknown");
  EXPECT_NE(ClientSseStateMachine::getStateName(ClientSseState::Error),
            "Unknown");
  EXPECT_NE(ClientSseStateMachine::getStateName(ClientSseState::Closed),
            "Unknown");
}

TEST_F(ClientSseStateMachineTest, GetEventName_AllEventsNamed) {
  EXPECT_NE(
      ClientSseStateMachine::getEventName(ClientSseEvent::ConnectionReady),
      "Unknown");
  EXPECT_NE(ClientSseStateMachine::getEventName(ClientSseEvent::GetSent),
            "Unknown");
  EXPECT_NE(
      ClientSseStateMachine::getEventName(ClientSseEvent::EndpointReceived),
      "Unknown");
  EXPECT_NE(ClientSseStateMachine::getEventName(ClientSseEvent::StreamStarted),
            "Unknown");
  EXPECT_NE(
      ClientSseStateMachine::getEventName(ClientSseEvent::NegotiationTimeout),
      "Unknown");
  EXPECT_NE(ClientSseStateMachine::getEventName(ClientSseEvent::StreamError),
            "Unknown");
  EXPECT_NE(ClientSseStateMachine::getEventName(ClientSseEvent::Close),
            "Unknown");
}

// ===== Metrics Tests =====

TEST_F(ClientSseStateMachineTest, TotalTransitions_Counted) {
  createSseMachine();
  EXPECT_EQ(state_machine_->getTotalTransitions(), 0u);

  state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  runFor(10ms);
  state_machine_->handleEvent(ClientSseEvent::GetSent);
  runFor(10ms);
  state_machine_->handleEvent(ClientSseEvent::EndpointReceived);
  runFor(10ms);

  EXPECT_EQ(state_machine_->getTotalTransitions(), 3u);
}

TEST_F(ClientSseStateMachineTest, TimeInCurrentState_Increases) {
  createSseMachine();
  walkTo(ClientSseState::Active);

  auto t1 = state_machine_->getTimeInCurrentState();
  runFor(50ms);
  auto t2 = state_machine_->getTimeInCurrentState();

  EXPECT_GT(t2, t1);
}

// ===== Reentrancy Tests =====

TEST_F(ClientSseStateMachineTest, ReentrantEventFromCallback) {
  createSseMachine();

  // Register a listener that fires GetSent when entering WaitingForGetSent.
  // This tests the reentrancy guard: the inner handleEvent should be
  // deferred via scheduleTransition to avoid stack overflow.
  state_machine_->addStateChangeListener(
      [this](const ClientSseTransitionContext& ctx) {
        if (ctx.to_state == ClientSseState::WaitingForGetSent) {
          state_machine_->handleEvent(ClientSseEvent::GetSent);
        }
      });

  state_machine_->handleEvent(ClientSseEvent::ConnectionReady);
  // Let the deferred transition execute
  runFor(50ms);

  // Both transitions should have happened
  expectState(ClientSseState::WaitingForEndpoint);
}

// ===== ForceTransition Tests =====

TEST_F(ClientSseStateMachineTest, ForceTransition_BypassesValidation) {
  createSseMachine();
  // Idle -> Active would normally be invalid
  state_machine_->forceTransition(ClientSseState::Active, "test override");
  expectState(ClientSseState::Active);

  const auto& history = state_machine_->getStateHistory();
  ASSERT_FALSE(history.empty());
  EXPECT_NE(history.back().reason.find("FORCED:"), std::string::npos);
}

// ===== ScheduleTransition Tests =====

TEST_F(ClientSseStateMachineTest, ScheduleTransition_DeferredToNextIteration) {
  createSseMachine();
  state_machine_->scheduleTransition(ClientSseState::WaitingForGetSent,
                                     ClientSseEvent::ConnectionReady,
                                     "deferred test");
  // Should NOT have transitioned yet — posted to next iteration
  expectState(ClientSseState::Idle);

  runFor(10ms);
  // Now it should have transitioned
  expectState(ClientSseState::WaitingForGetSent);
}

}  // namespace
}  // namespace filter
}  // namespace mcp
