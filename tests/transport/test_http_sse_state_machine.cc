/**
 * @file test_http_sse_state_machine.cc
 * @brief Comprehensive unit tests for HTTP SSE state machine
 *
 * Tests cover:
 * - State transitions for client and server modes
 * - Entry/exit actions
 * - Stream management
 * - Reconnection logic
 * - Error handling
 * - Thread safety through dispatcher
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <chrono>
#include <future>
#include <thread>
#include <atomic>

#include "mcp/transport/http_sse_state_machine.h"
#include "mcp/event/event_loop.h"

using namespace mcp;
using namespace mcp::transport;
using namespace std::chrono_literals;
using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;

// Minimal mock dispatcher for testing - only implement what's actually used
class MockDispatcher : public event::Dispatcher {
public:
  MockDispatcher() : name_("test") {}
  
  // Execute callbacks immediately for testing
  void post(std::function<void()> callback) override {
    if (callback) callback();
  }
  
  // Return null timer for simplicity
  event::TimerPtr createTimer(event::TimerCb) override {
    return nullptr;
  }
  
  // Required pure virtual methods - minimal implementation
  const std::string& name() const override { return name_; }
  event::TimeSource& timeSource() override { 
    static event::RealTimeSource ts;
    return ts;
  }
  void exit() override {}
  event::SignalEventPtr listenForSignal(int, event::SignalCb) override { return nullptr; }
  event::FileEventPtr createFileEvent(network::Socket::OsFdType, event::FileReadyCb, event::FileTriggerType, uint32_t) override { return nullptr; }
  void run(event::RunType) override {}
  void pushTrackedObject(event::TrackedObject*) override {}
  void popTrackedObject(event::TrackedObject*) override {}
  bool isThreadSafe() const override { return false; }
  void initializeStats(event::DispatcherStats&) override {}
  void clearDeferredDeleteList() override {}
  void updateApproximateMonotonicTime() override {}
  event::MonotonicTime approximateMonotonicTime() const override { 
    return event::MonotonicTime(); 
  }
  bool isWorkerThread() { return true; }
  event::SchedulerPtr createScheduler(event::SchedulerCb) { return nullptr; }
  void deferredDelete(event::DeferredDeletablePtr) override {}
  const event::ScopeTrackedObject* trackedObject() const { return nullptr; }
  
private:
  std::string name_;
};

class HttpSseStateMachineTest : public ::testing::Test {
protected:
  void SetUp() override {
    dispatcher_ = std::make_unique<MockDispatcher>();
  }

  void TearDown() override {
    client_machine_.reset();
    server_machine_.reset();
    dispatcher_.reset();
  }

  std::unique_ptr<HttpSseStateMachine> createClientMachine() {
    return std::make_unique<HttpSseStateMachine>(
        HttpSseMode::Client, *dispatcher_);
  }

  std::unique_ptr<HttpSseStateMachine> createServerMachine() {
    return std::make_unique<HttpSseStateMachine>(
        HttpSseMode::Server, *dispatcher_);
  }

  std::unique_ptr<MockDispatcher> dispatcher_;
  std::unique_ptr<HttpSseStateMachine> client_machine_;
  std::unique_ptr<HttpSseStateMachine> server_machine_;
};

// ===== Basic State Tests =====

TEST_F(HttpSseStateMachineTest, InitialState) {
  client_machine_ = createClientMachine();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Uninitialized);
  EXPECT_FALSE(client_machine_->isConnected());
  EXPECT_FALSE(client_machine_->isSseActive());
  EXPECT_FALSE(client_machine_->isTerminalState());
}

TEST_F(HttpSseStateMachineTest, StateNameMapping) {
  // Test all state names are properly mapped
  EXPECT_EQ(HttpSseStateMachine::getStateName(HttpSseState::Uninitialized), 
            "Uninitialized");
  EXPECT_EQ(HttpSseStateMachine::getStateName(HttpSseState::TcpConnecting), 
            "TcpConnecting");
  EXPECT_EQ(HttpSseStateMachine::getStateName(HttpSseState::TcpConnected), 
            "TcpConnected");
  EXPECT_EQ(HttpSseStateMachine::getStateName(HttpSseState::HttpRequestSending), 
            "HttpRequestSending");
  EXPECT_EQ(HttpSseStateMachine::getStateName(HttpSseState::HttpResponseHeadersReceiving), 
            "HttpResponseHeadersReceiving");
  EXPECT_EQ(HttpSseStateMachine::getStateName(HttpSseState::SseStreamActive), 
            "SseStreamActive");
  EXPECT_EQ(HttpSseStateMachine::getStateName(HttpSseState::ReconnectWaiting), 
            "ReconnectWaiting");
  EXPECT_EQ(HttpSseStateMachine::getStateName(HttpSseState::ShutdownCompleted), 
            "ShutdownCompleted");
  EXPECT_EQ(HttpSseStateMachine::getStateName(HttpSseState::Error), 
            "Error");
}

// ===== Client State Transition Tests =====

TEST_F(HttpSseStateMachineTest, ClientBasicConnectionFlow) {
  client_machine_ = createClientMachine();
  
  // Track state changes
  std::vector<std::pair<HttpSseState, HttpSseState>> state_changes;
  client_machine_->addStateChangeListener(
      [&](HttpSseState old_state, HttpSseState new_state) {
        state_changes.push_back({old_state, new_state});
      });

  // Uninitialized -> Initialized
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::Uninitialized, HttpSseState::Initialized));
  client_machine_->transition(HttpSseState::Initialized);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Initialized);

  // Initialized -> TcpConnecting
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::Initialized, HttpSseState::TcpConnecting));
  client_machine_->transition(HttpSseState::TcpConnecting);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::TcpConnecting);

  // TcpConnecting -> TcpConnected
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::TcpConnecting, HttpSseState::TcpConnected));
  client_machine_->transition(HttpSseState::TcpConnected);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::TcpConnected);

  // TcpConnected -> HttpRequestPreparing
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::TcpConnected, HttpSseState::HttpRequestPreparing));
  client_machine_->transition(HttpSseState::HttpRequestPreparing);
  dispatcher_->run();
  
  // Verify state changes were recorded
  EXPECT_GE(state_changes.size(), 4);
  EXPECT_EQ(state_changes[0].first, HttpSseState::Uninitialized);
  EXPECT_EQ(state_changes[0].second, HttpSseState::Initialized);
}

TEST_F(HttpSseStateMachineTest, ClientHttpRequestFlow) {
  client_machine_ = createClientMachine();
  
  // Setup initial connection
  client_machine_->transition(HttpSseState::Initialized);
  client_machine_->transition(HttpSseState::TcpConnecting);
  client_machine_->transition(HttpSseState::TcpConnected);
  dispatcher_->run();
  
  // HTTP request flow
  client_machine_->transition(HttpSseState::HttpRequestPreparing);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::HttpRequestPreparing);
  
  client_machine_->transition(HttpSseState::HttpRequestSending);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::HttpRequestSending);
  
  client_machine_->transition(HttpSseState::HttpRequestSent);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::HttpRequestSent);
  
  client_machine_->transition(HttpSseState::HttpResponseWaiting);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::HttpResponseWaiting);
}

TEST_F(HttpSseStateMachineTest, ClientSseEstablishment) {
  client_machine_ = createClientMachine();
  
  // Fast-forward to HTTP response state
  client_machine_->forceTransition(HttpSseState::HttpResponseWaiting);
  dispatcher_->run();
  
  // SSE negotiation flow
  client_machine_->transition(HttpSseState::HttpResponseHeadersReceiving);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), 
            HttpSseState::HttpResponseHeadersReceiving);
  
  client_machine_->transition(HttpSseState::SseNegotiating);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::SseNegotiating);
  
  client_machine_->transition(HttpSseState::SseStreamActive);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::SseStreamActive);
  EXPECT_TRUE(client_machine_->isSseActive());
  EXPECT_TRUE(client_machine_->isConnected());
}

TEST_F(HttpSseStateMachineTest, ClientInvalidTransitions) {
  client_machine_ = createClientMachine();
  
  // Cannot go directly from Uninitialized to Connected
  EXPECT_FALSE(client_machine_->canTransition(
      HttpSseState::Uninitialized, HttpSseState::SseStreamActive));
  
  // Cannot go from TcpConnecting directly to SseStreamActive
  client_machine_->transition(HttpSseState::Initialized);
  client_machine_->transition(HttpSseState::TcpConnecting);
  dispatcher_->run();
  EXPECT_FALSE(client_machine_->canTransition(
      HttpSseState::TcpConnecting, HttpSseState::SseStreamActive));
  
  // Cannot go backwards in connection flow
  client_machine_->transition(HttpSseState::TcpConnected);
  dispatcher_->run();
  EXPECT_FALSE(client_machine_->canTransition(
      HttpSseState::TcpConnected, HttpSseState::TcpConnecting));
}

// ===== Server State Transition Tests =====

TEST_F(HttpSseStateMachineTest, ServerBasicFlow) {
  server_machine_ = createServerMachine();
  
  // Server: Uninitialized -> Initialized -> ServerListening
  server_machine_->transition(HttpSseState::Initialized);
  dispatcher_->run();
  EXPECT_EQ(server_machine_->getCurrentState(), HttpSseState::Initialized);
  
  EXPECT_TRUE(server_machine_->canTransition(
      HttpSseState::Initialized, HttpSseState::ServerListening));
  server_machine_->transition(HttpSseState::ServerListening);
  dispatcher_->run();
  EXPECT_EQ(server_machine_->getCurrentState(), HttpSseState::ServerListening);
  
  // Accept connection
  EXPECT_TRUE(server_machine_->canTransition(
      HttpSseState::ServerListening, HttpSseState::ServerConnectionAccepted));
  server_machine_->transition(HttpSseState::ServerConnectionAccepted);
  dispatcher_->run();
  EXPECT_EQ(server_machine_->getCurrentState(), 
            HttpSseState::ServerConnectionAccepted);
}

TEST_F(HttpSseStateMachineTest, ServerRequestHandling) {
  server_machine_ = createServerMachine();
  
  // Setup server in accepted state
  server_machine_->forceTransition(HttpSseState::ServerConnectionAccepted);
  dispatcher_->run();
  
  // Receive request
  EXPECT_TRUE(server_machine_->canTransition(
      HttpSseState::ServerConnectionAccepted, 
      HttpSseState::ServerRequestReceiving));
  server_machine_->transition(HttpSseState::ServerRequestReceiving);
  dispatcher_->run();
  
  // Send response
  EXPECT_TRUE(server_machine_->canTransition(
      HttpSseState::ServerRequestReceiving, 
      HttpSseState::ServerResponseSending));
  server_machine_->transition(HttpSseState::ServerResponseSending);
  dispatcher_->run();
  
  // Push SSE events
  EXPECT_TRUE(server_machine_->canTransition(
      HttpSseState::ServerResponseSending, 
      HttpSseState::ServerSsePushing));
  server_machine_->transition(HttpSseState::ServerSsePushing);
  dispatcher_->run();
  EXPECT_EQ(server_machine_->getCurrentState(), HttpSseState::ServerSsePushing);
}

// ===== Stream Management Tests =====

TEST_F(HttpSseStateMachineTest, StreamCreation) {
  client_machine_ = createClientMachine();
  
  auto* stream = client_machine_->createStream("test-stream-1");
  ASSERT_NE(stream, nullptr);
  EXPECT_EQ(stream->stream_id, "test-stream-1");
  EXPECT_EQ(stream->bytes_sent, 0);
  EXPECT_EQ(stream->bytes_received, 0);
  EXPECT_FALSE(stream->headers_complete);
  EXPECT_FALSE(stream->zombie_stream);
  
  // Should be able to retrieve the same stream
  auto* same_stream = client_machine_->getStream("test-stream-1");
  EXPECT_EQ(stream, same_stream);
  
  // Non-existent stream returns nullptr
  auto* no_stream = client_machine_->getStream("non-existent");
  EXPECT_EQ(no_stream, nullptr);
}

TEST_F(HttpSseStateMachineTest, StreamLifecycle) {
  client_machine_ = createClientMachine();
  
  auto* stream = client_machine_->createStream("lifecycle-stream");
  ASSERT_NE(stream, nullptr);
  
  // Simulate request sent
  client_machine_->onHttpRequestSent("lifecycle-stream");
  dispatcher_->run();
  EXPECT_TRUE(stream->end_stream_sent);
  
  // Simulate response received
  client_machine_->onHttpResponseReceived("lifecycle-stream", 200);
  dispatcher_->run();
  EXPECT_TRUE(stream->headers_complete);
  
  // Simulate SSE events
  client_machine_->onSseEventReceived("lifecycle-stream", "test event 1");
  client_machine_->onSseEventReceived("lifecycle-stream", "test event 2");
  dispatcher_->run();
  EXPECT_EQ(stream->events_received, 2);
}

TEST_F(HttpSseStateMachineTest, StreamReset) {
  client_machine_ = createClientMachine();
  
  auto* stream = client_machine_->createStream("reset-stream");
  ASSERT_NE(stream, nullptr);
  
  // Reset stream
  client_machine_->resetStream("reset-stream", 
                                StreamResetReason::ConnectionFailure);
  dispatcher_->run();
  EXPECT_TRUE(stream->reset_called);
  
  // Stream should still exist but be marked for cleanup
  auto* reset_stream = client_machine_->getStream("reset-stream");
  EXPECT_NE(reset_stream, nullptr);
  EXPECT_TRUE(reset_stream->reset_called);
}

TEST_F(HttpSseStateMachineTest, ZombieStreamManagement) {
  client_machine_ = createClientMachine();
  
  // Create multiple streams
  client_machine_->createStream("stream-1");
  client_machine_->createStream("stream-2");
  client_machine_->createStream("stream-3");
  
  // Mark some as zombies
  client_machine_->markStreamAsZombie("stream-1");
  client_machine_->markStreamAsZombie("stream-3");
  dispatcher_->run();
  
  auto* zombie1 = client_machine_->getStream("stream-1");
  auto* active2 = client_machine_->getStream("stream-2");
  auto* zombie3 = client_machine_->getStream("stream-3");
  
  EXPECT_TRUE(zombie1->zombie_stream);
  EXPECT_FALSE(active2->zombie_stream);
  EXPECT_TRUE(zombie3->zombie_stream);
  
  // Cleanup zombies
  client_machine_->cleanupZombieStreams();
  dispatcher_->run();
  
  // Zombie streams should be gone
  EXPECT_EQ(client_machine_->getStream("stream-1"), nullptr);
  EXPECT_NE(client_machine_->getStream("stream-2"), nullptr);
  EXPECT_EQ(client_machine_->getStream("stream-3"), nullptr);
}

TEST_F(HttpSseStateMachineTest, StreamStatistics) {
  client_machine_ = createClientMachine();
  
  // Create and update streams
  auto* stream1 = client_machine_->createStream("stats-1");
  auto* stream2 = client_machine_->createStream("stats-2");
  
  stream1->bytes_sent = 1000;
  stream1->bytes_received = 2000;
  stream1->events_received = 5;
  
  stream2->bytes_sent = 500;
  stream2->bytes_received = 1500;
  stream2->events_received = 3;
  
  client_machine_->markStreamAsZombie("stats-2");
  
  auto stats = client_machine_->getStreamStats();
  EXPECT_EQ(stats.active_streams, 1);  // Only stats-1 is active
  EXPECT_EQ(stats.zombie_streams, 1);  // stats-2 is zombie
  EXPECT_EQ(stats.total_bytes_sent, 1500);
  EXPECT_EQ(stats.total_bytes_received, 3500);
  EXPECT_EQ(stats.total_events_received, 8);
}

// ===== Entry/Exit Action Tests =====

TEST_F(HttpSseStateMachineTest, EntryExitActions) {
  client_machine_ = createClientMachine();
  
  bool entry_called = false;
  bool exit_called = false;
  bool entry_done_called = false;
  bool exit_done_called = false;
  
  // Set entry action for TcpConnecting
  client_machine_->setEntryAction(HttpSseState::TcpConnecting,
      [&](HttpSseState state, std::function<void()> done) {
        EXPECT_EQ(state, HttpSseState::TcpConnecting);
        entry_called = true;
        // Simulate async operation
        dispatcher_->post([done, &entry_done_called]() {
          entry_done_called = true;
          done();
        });
      });
  
  // Set exit action for TcpConnecting
  client_machine_->setExitAction(HttpSseState::TcpConnecting,
      [&](HttpSseState state, std::function<void()> done) {
        EXPECT_EQ(state, HttpSseState::TcpConnecting);
        exit_called = true;
        // Simulate async operation
        dispatcher_->post([done, &exit_done_called]() {
          exit_done_called = true;
          done();
        });
      });
  
  // Transition to TcpConnecting (should trigger entry)
  client_machine_->transition(HttpSseState::Initialized);
  client_machine_->transition(HttpSseState::TcpConnecting);
  dispatcher_->run();
  
  EXPECT_TRUE(entry_called);
  EXPECT_TRUE(entry_done_called);
  EXPECT_FALSE(exit_called);
  
  // Transition away from TcpConnecting (should trigger exit)
  client_machine_->transition(HttpSseState::TcpConnected);
  dispatcher_->run();
  
  EXPECT_TRUE(exit_called);
  EXPECT_TRUE(exit_done_called);
}

TEST_F(HttpSseStateMachineTest, AsyncTransitionCompletion) {
  client_machine_ = createClientMachine();
  
  bool callback_called = false;
  bool callback_success = false;
  std::string callback_error;
  
  // Transition with completion callback
  client_machine_->transition(HttpSseState::Initialized,
      [&](bool success, const std::string& error) {
        callback_called = true;
        callback_success = success;
        callback_error = error;
      });
  
  dispatcher_->run();
  
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(callback_success);
  EXPECT_TRUE(callback_error.empty());
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Initialized);
}

TEST_F(HttpSseStateMachineTest, FailedTransitionCallback) {
  client_machine_ = createClientMachine();
  
  bool callback_called = false;
  bool callback_success = false;
  std::string callback_error;
  
  // Attempt invalid transition
  client_machine_->transition(HttpSseState::SseStreamActive,
      [&](bool success, const std::string& error) {
        callback_called = true;
        callback_success = success;
        callback_error = error;
      });
  
  dispatcher_->run();
  
  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(callback_success);
  EXPECT_FALSE(callback_error.empty());
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Uninitialized);
}

// ===== Reconnection Tests =====

TEST_F(HttpSseStateMachineTest, ReconnectionScheduling) {
  client_machine_ = createClientMachine();
  
  // Set reconnection strategy (exponential backoff)
  client_machine_->setReconnectStrategy(
      [](uint32_t attempt) -> std::chrono::milliseconds {
        return std::chrono::milliseconds(100 * (1 << attempt));  // 100, 200, 400...
      });
  
  // Simulate connection failure
  client_machine_->forceTransition(HttpSseState::Error);
  
  // Schedule reconnection
  client_machine_->scheduleReconnect();
  dispatcher_->run();
  
  EXPECT_TRUE(client_machine_->isReconnecting());
  EXPECT_EQ(client_machine_->getReconnectAttempt(), 1);
}

TEST_F(HttpSseStateMachineTest, ReconnectionCancellation) {
  client_machine_ = createClientMachine();
  
  // Schedule reconnection
  client_machine_->scheduleReconnect();
  dispatcher_->run();
  EXPECT_TRUE(client_machine_->isReconnecting());
  
  // Cancel reconnection
  client_machine_->cancelReconnect();
  dispatcher_->run();
  
  // Should no longer be in reconnecting state
  EXPECT_FALSE(client_machine_->isReconnecting());
  EXPECT_EQ(client_machine_->getReconnectAttempt(), 0);
}

TEST_F(HttpSseStateMachineTest, ReconnectionFlow) {
  client_machine_ = createClientMachine();
  
  std::vector<HttpSseState> state_sequence;
  client_machine_->addStateChangeListener(
      [&](HttpSseState old_state, HttpSseState new_state) {
        state_sequence.push_back(new_state);
      });
  
  // Set up machine in connected state
  client_machine_->forceTransition(HttpSseState::SseStreamActive);
  dispatcher_->run();
  
  // Simulate connection failure
  client_machine_->transition(HttpSseState::Error);
  dispatcher_->run();
  
  // Attempt reconnection
  client_machine_->transition(HttpSseState::ReconnectScheduled);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::ReconnectScheduled);
  
  client_machine_->transition(HttpSseState::ReconnectWaiting);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::ReconnectWaiting);
  
  client_machine_->transition(HttpSseState::ReconnectAttempting);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::ReconnectAttempting);
  
  // Back to connecting
  client_machine_->transition(HttpSseState::TcpConnecting);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::TcpConnecting);
}

// ===== Shutdown Tests =====

TEST_F(HttpSseStateMachineTest, GracefulShutdown) {
  client_machine_ = createClientMachine();
  
  // Set up connected state
  client_machine_->forceTransition(HttpSseState::SseStreamActive);
  dispatcher_->run();
  
  // Initiate shutdown
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::SseStreamActive, HttpSseState::ShutdownInitiated));
  client_machine_->transition(HttpSseState::ShutdownInitiated);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::ShutdownInitiated);
  
  // Drain data
  client_machine_->transition(HttpSseState::ShutdownDraining);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::ShutdownDraining);
  
  // Complete shutdown
  client_machine_->transition(HttpSseState::ShutdownCompleted);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::ShutdownCompleted);
  
  // Finally close
  client_machine_->transition(HttpSseState::Closed);
  dispatcher_->run();
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Closed);
  EXPECT_TRUE(client_machine_->isTerminalState());
}

TEST_F(HttpSseStateMachineTest, ForceShutdown) {
  client_machine_ = createClientMachine();
  
  // Set up connected state
  client_machine_->forceTransition(HttpSseState::SseStreamActive);
  dispatcher_->run();
  
  // Force immediate close
  client_machine_->forceTransition(HttpSseState::Closed);
  dispatcher_->run();
  
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Closed);
  EXPECT_TRUE(client_machine_->isTerminalState());
}

// ===== State Timeout Tests =====

TEST_F(HttpSseStateMachineTest, StateTimeout) {
  client_machine_ = createClientMachine();
  
  bool timeout_triggered = false;
  client_machine_->addStateChangeListener(
      [&](HttpSseState old_state, HttpSseState new_state) {
        if (new_state == HttpSseState::Error) {
          timeout_triggered = true;
        }
      });
  
  // Set timeout for TcpConnecting state
  client_machine_->transition(HttpSseState::Initialized);
  client_machine_->transition(HttpSseState::TcpConnecting);
  client_machine_->setStateTimeout(100ms, HttpSseState::Error);
  
  // Wait for timeout
  std::this_thread::sleep_for(150ms);
  dispatcher_->run();
  
  EXPECT_TRUE(timeout_triggered);
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Error);
}

TEST_F(HttpSseStateMachineTest, StateTimeoutCancellation) {
  client_machine_ = createClientMachine();
  
  bool timeout_triggered = false;
  client_machine_->addStateChangeListener(
      [&](HttpSseState old_state, HttpSseState new_state) {
        if (new_state == HttpSseState::Error) {
          timeout_triggered = true;
        }
      });
  
  // Set timeout
  client_machine_->transition(HttpSseState::Initialized);
  client_machine_->transition(HttpSseState::TcpConnecting);
  client_machine_->setStateTimeout(100ms, HttpSseState::Error);
  
  // Cancel timeout before it triggers
  client_machine_->cancelStateTimeout();
  
  // Wait longer than timeout
  std::this_thread::sleep_for(150ms);
  dispatcher_->run();
  
  EXPECT_FALSE(timeout_triggered);
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::TcpConnecting);
}

// ===== Custom Validator Tests =====

TEST_F(HttpSseStateMachineTest, CustomTransitionValidator) {
  client_machine_ = createClientMachine();
  
  bool validator_called = false;
  
  // Add custom validator that blocks specific transition
  client_machine_->addTransitionValidator(
      [&](HttpSseState from, HttpSseState to) -> bool {
        validator_called = true;
        // Block transition from Initialized to TcpConnecting
        if (from == HttpSseState::Initialized && 
            to == HttpSseState::TcpConnecting) {
          return false;
        }
        return true;
      });
  
  client_machine_->transition(HttpSseState::Initialized);
  dispatcher_->run();
  
  // Normally valid transition should now be blocked
  EXPECT_FALSE(client_machine_->canTransition(
      HttpSseState::Initialized, HttpSseState::TcpConnecting));
  
  // Attempt transition
  bool transition_failed = false;
  client_machine_->transition(HttpSseState::TcpConnecting,
      [&](bool success, const std::string& error) {
        transition_failed = !success;
      });
  dispatcher_->run();
  
  EXPECT_TRUE(validator_called);
  EXPECT_TRUE(transition_failed);
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Initialized);
}

// ===== State History Tests =====

TEST_F(HttpSseStateMachineTest, StateHistory) {
  client_machine_ = createClientMachine();
  
  // Perform several transitions
  client_machine_->transition(HttpSseState::Initialized);
  client_machine_->transition(HttpSseState::TcpConnecting);
  client_machine_->transition(HttpSseState::TcpConnected);
  client_machine_->transition(HttpSseState::HttpRequestPreparing);
  dispatcher_->run();
  
  auto history = client_machine_->getStateHistory(10);
  EXPECT_GE(history.size(), 4);
  
  // History should be in chronological order
  EXPECT_EQ(history[0].first, HttpSseState::Uninitialized);  // Initial state
  EXPECT_EQ(history[1].first, HttpSseState::Initialized);
  EXPECT_EQ(history[2].first, HttpSseState::TcpConnecting);
  EXPECT_EQ(history[3].first, HttpSseState::TcpConnected);
  
  // Timestamps should be increasing
  for (size_t i = 1; i < history.size(); ++i) {
    EXPECT_GT(history[i].second, history[i-1].second);
  }
}

TEST_F(HttpSseStateMachineTest, TimeInCurrentState) {
  client_machine_ = createClientMachine();
  
  client_machine_->transition(HttpSseState::Initialized);
  dispatcher_->run();
  
  auto time_before = client_machine_->getTimeInCurrentState();
  
  // Wait a bit
  std::this_thread::sleep_for(100ms);
  
  auto time_after = client_machine_->getTimeInCurrentState();
  EXPECT_GT(time_after.count(), time_before.count());
  EXPECT_GE(time_after.count(), 100);
}

// ===== Degraded Mode Tests =====

TEST_F(HttpSseStateMachineTest, HttpOnlyMode) {
  client_machine_ = createClientMachine();
  
  // Transition to HTTP-only mode (SSE unavailable)
  client_machine_->forceTransition(HttpSseState::HttpOnlyMode);
  dispatcher_->run();
  
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::HttpOnlyMode);
  EXPECT_TRUE(client_machine_->isConnected());
  EXPECT_FALSE(client_machine_->isSseActive());
  
  // Can still perform HTTP operations
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::HttpOnlyMode, HttpSseState::HttpRequestPreparing));
}

TEST_F(HttpSseStateMachineTest, PartialDataReceived) {
  client_machine_ = createClientMachine();
  
  // Simulate partial data scenario
  client_machine_->forceTransition(HttpSseState::PartialDataReceived);
  dispatcher_->run();
  
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::PartialDataReceived);
  
  // Should be able to recover
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::PartialDataReceived, HttpSseState::SseEventBuffering));
}

// ===== SSE Event States Tests =====

TEST_F(HttpSseStateMachineTest, SseEventFlow) {
  client_machine_ = createClientMachine();
  
  // Set up SSE stream
  client_machine_->forceTransition(HttpSseState::SseStreamActive);
  dispatcher_->run();
  
  // Receive partial event
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::SseStreamActive, HttpSseState::SseEventBuffering));
  client_machine_->transition(HttpSseState::SseEventBuffering);
  dispatcher_->run();
  
  // Complete event received
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::SseEventBuffering, HttpSseState::SseEventReceived));
  client_machine_->transition(HttpSseState::SseEventReceived);
  dispatcher_->run();
  
  // Back to active for next event
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::SseEventReceived, HttpSseState::SseStreamActive));
  client_machine_->transition(HttpSseState::SseStreamActive);
  dispatcher_->run();
  
  EXPECT_TRUE(client_machine_->isSseActive());
}

TEST_F(HttpSseStateMachineTest, SseKeepAlive) {
  client_machine_ = createClientMachine();
  
  // Set up SSE stream
  client_machine_->forceTransition(HttpSseState::SseStreamActive);
  dispatcher_->run();
  
  // Receive keep-alive
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::SseStreamActive, HttpSseState::SseKeepAliveReceiving));
  client_machine_->transition(HttpSseState::SseKeepAliveReceiving);
  dispatcher_->run();
  
  // Back to active after keep-alive
  EXPECT_TRUE(client_machine_->canTransition(
      HttpSseState::SseKeepAliveReceiving, HttpSseState::SseStreamActive));
  client_machine_->transition(HttpSseState::SseStreamActive);
  dispatcher_->run();
  
  EXPECT_TRUE(client_machine_->isSseActive());
}

// ===== Factory Tests =====

TEST_F(HttpSseStateMachineTest, FactoryClientCreation) {
  auto machine = HttpSseStateMachineFactory::createClientStateMachine(*dispatcher_);
  ASSERT_NE(machine, nullptr);
  EXPECT_EQ(machine->getMode(), HttpSseMode::Client);
  EXPECT_EQ(machine->getCurrentState(), HttpSseState::Uninitialized);
}

TEST_F(HttpSseStateMachineTest, FactoryServerCreation) {
  auto machine = HttpSseStateMachineFactory::createServerStateMachine(*dispatcher_);
  ASSERT_NE(machine, nullptr);
  EXPECT_EQ(machine->getMode(), HttpSseMode::Server);
  EXPECT_EQ(machine->getCurrentState(), HttpSseState::Uninitialized);
}

TEST_F(HttpSseStateMachineTest, FactoryCustomConfig) {
  HttpSseStateMachineFactory::Config config;
  config.mode = HttpSseMode::Client;
  config.default_timeout = 5000ms;
  config.reconnect_initial_delay = 500ms;
  config.reconnect_max_delay = 10000ms;
  config.max_reconnect_attempts = 5;
  config.enable_zombie_cleanup = true;
  config.zombie_cleanup_interval = 2000ms;
  
  auto machine = HttpSseStateMachineFactory::createStateMachine(config, *dispatcher_);
  ASSERT_NE(machine, nullptr);
  EXPECT_EQ(machine->getMode(), HttpSseMode::Client);
}

// ===== Pattern Helper Tests =====

TEST_F(HttpSseStateMachineTest, StatePatterns) {
  // Test connected states
  EXPECT_TRUE(HttpSseStatePatterns::isConnectedState(HttpSseState::SseStreamActive));
  EXPECT_TRUE(HttpSseStatePatterns::isConnectedState(HttpSseState::HttpOnlyMode));
  EXPECT_FALSE(HttpSseStatePatterns::isConnectedState(HttpSseState::TcpConnecting));
  EXPECT_FALSE(HttpSseStatePatterns::isConnectedState(HttpSseState::Closed));
  
  // Test HTTP request states
  EXPECT_TRUE(HttpSseStatePatterns::isHttpRequestState(HttpSseState::HttpRequestPreparing));
  EXPECT_TRUE(HttpSseStatePatterns::isHttpRequestState(HttpSseState::HttpRequestSending));
  EXPECT_TRUE(HttpSseStatePatterns::isHttpRequestState(HttpSseState::HttpRequestSent));
  EXPECT_FALSE(HttpSseStatePatterns::isHttpRequestState(HttpSseState::HttpResponseWaiting));
  
  // Test HTTP response states
  EXPECT_TRUE(HttpSseStatePatterns::isHttpResponseState(HttpSseState::HttpResponseWaiting));
  EXPECT_TRUE(HttpSseStatePatterns::isHttpResponseState(HttpSseState::HttpResponseHeadersReceiving));
  EXPECT_TRUE(HttpSseStatePatterns::isHttpResponseState(HttpSseState::HttpResponseBodyReceiving));
  EXPECT_FALSE(HttpSseStatePatterns::isHttpResponseState(HttpSseState::HttpRequestSending));
  
  // Test SSE stream states
  EXPECT_TRUE(HttpSseStatePatterns::isSseStreamState(HttpSseState::SseStreamActive));
  EXPECT_TRUE(HttpSseStatePatterns::isSseStreamState(HttpSseState::SseEventBuffering));
  EXPECT_TRUE(HttpSseStatePatterns::isSseStreamState(HttpSseState::SseEventReceived));
  EXPECT_FALSE(HttpSseStatePatterns::isSseStreamState(HttpSseState::HttpOnlyMode));
  
  // Test send/receive capability
  EXPECT_TRUE(HttpSseStatePatterns::canSendData(HttpSseState::SseStreamActive));
  EXPECT_TRUE(HttpSseStatePatterns::canSendData(HttpSseState::HttpRequestSending));
  EXPECT_FALSE(HttpSseStatePatterns::canSendData(HttpSseState::Closed));
  
  EXPECT_TRUE(HttpSseStatePatterns::canReceiveData(HttpSseState::SseStreamActive));
  EXPECT_TRUE(HttpSseStatePatterns::canReceiveData(HttpSseState::HttpResponseBodyReceiving));
  EXPECT_FALSE(HttpSseStatePatterns::canReceiveData(HttpSseState::Closed));
  
  // Test error states
  EXPECT_TRUE(HttpSseStatePatterns::isErrorState(HttpSseState::Error));
  EXPECT_FALSE(HttpSseStatePatterns::isErrorState(HttpSseState::SseStreamActive));
  
  // Test reconnection capability
  EXPECT_TRUE(HttpSseStatePatterns::canReconnect(HttpSseState::Error));
  EXPECT_TRUE(HttpSseStatePatterns::canReconnect(HttpSseState::Closed));
  EXPECT_FALSE(HttpSseStatePatterns::canReconnect(HttpSseState::SseStreamActive));
}

TEST_F(HttpSseStateMachineTest, StatePatternFlowPrediction) {
  // Test HTTP request flow prediction
  auto next = HttpSseStatePatterns::getNextHttpRequestState(
      HttpSseState::HttpRequestPreparing);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), HttpSseState::HttpRequestSending);
  
  next = HttpSseStatePatterns::getNextHttpRequestState(
      HttpSseState::HttpRequestSending);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), HttpSseState::HttpRequestSent);
  
  next = HttpSseStatePatterns::getNextHttpRequestState(
      HttpSseState::HttpRequestSent);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), HttpSseState::HttpResponseWaiting);
  
  // Test HTTP response flow prediction
  next = HttpSseStatePatterns::getNextHttpResponseState(
      HttpSseState::HttpResponseWaiting);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), HttpSseState::HttpResponseHeadersReceiving);
  
  next = HttpSseStatePatterns::getNextHttpResponseState(
      HttpSseState::HttpResponseHeadersReceiving);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next.value(), HttpSseState::HttpResponseBodyReceiving);
}

// ===== Transition Coordinator Tests =====

TEST_F(HttpSseStateMachineTest, TransitionCoordinatorConnection) {
  client_machine_ = createClientMachine();
  HttpSseTransitionCoordinator coordinator(*client_machine_);
  
  bool connection_complete = false;
  bool connection_success = false;
  
  std::unordered_map<std::string, std::string> headers = {
      {"Accept", "text/event-stream"},
      {"Cache-Control", "no-cache"}
  };
  
  coordinator.executeConnection("http://example.com/events", headers,
      [&](bool success) {
        connection_complete = true;
        connection_success = success;
      });
  
  dispatcher_->run();
  
  EXPECT_TRUE(connection_complete);
  // Connection should fail since we don't have actual network
  EXPECT_FALSE(connection_success);
}

TEST_F(HttpSseStateMachineTest, TransitionCoordinatorShutdown) {
  client_machine_ = createClientMachine();
  HttpSseTransitionCoordinator coordinator(*client_machine_);
  
  // Set up connected state
  client_machine_->forceTransition(HttpSseState::SseStreamActive);
  dispatcher_->run();
  
  bool shutdown_complete = false;
  bool shutdown_success = false;
  
  coordinator.executeShutdown([&](bool success) {
    shutdown_complete = true;
    shutdown_success = success;
  });
  
  dispatcher_->run();
  
  EXPECT_TRUE(shutdown_complete);
  EXPECT_TRUE(shutdown_success);
  EXPECT_TRUE(client_machine_->isTerminalState());
}

// ===== Thread Safety Tests =====

TEST_F(HttpSseStateMachineTest, ScheduledTransition) {
  client_machine_ = createClientMachine();
  
  bool transition_complete = false;
  
  client_machine_->addStateChangeListener(
      [&](HttpSseState old_state, HttpSseState new_state) {
        if (new_state == HttpSseState::Initialized) {
          transition_complete = true;
        }
      });
  
  // Schedule transition for next event loop iteration
  client_machine_->scheduleTransition(HttpSseState::Initialized);
  
  // State should not change immediately
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Uninitialized);
  EXPECT_FALSE(transition_complete);
  
  // Run dispatcher to process scheduled transition
  dispatcher_->run();
  
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Initialized);
  EXPECT_TRUE(transition_complete);
}

TEST_F(HttpSseStateMachineTest, TransitionReentrancyProtection) {
  client_machine_ = createClientMachine();
  
  bool nested_transition_attempted = false;
  bool nested_transition_succeeded = false;
  
  // Set entry action that attempts another transition
  client_machine_->setEntryAction(HttpSseState::Initialized,
      [&](HttpSseState state, std::function<void()> done) {
        nested_transition_attempted = true;
        // Attempt transition while already in transition
        client_machine_->transition(HttpSseState::TcpConnecting,
            [&](bool success, const std::string& error) {
              nested_transition_succeeded = success;
            });
        done();
      });
  
  client_machine_->transition(HttpSseState::Initialized);
  dispatcher_->run();
  
  EXPECT_TRUE(nested_transition_attempted);
  // Nested transition should be blocked or scheduled
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::Initialized);
}

// ===== Edge Case Tests =====

TEST_F(HttpSseStateMachineTest, MultipleListeners) {
  client_machine_ = createClientMachine();
  
  int listener1_count = 0;
  int listener2_count = 0;
  int listener3_count = 0;
  
  auto id1 = client_machine_->addStateChangeListener(
      [&](HttpSseState old_state, HttpSseState new_state) {
        listener1_count++;
      });
  
  auto id2 = client_machine_->addStateChangeListener(
      [&](HttpSseState old_state, HttpSseState new_state) {
        listener2_count++;
      });
  
  auto id3 = client_machine_->addStateChangeListener(
      [&](HttpSseState old_state, HttpSseState new_state) {
        listener3_count++;
      });
  
  // Perform transition
  client_machine_->transition(HttpSseState::Initialized);
  dispatcher_->run();
  
  EXPECT_EQ(listener1_count, 1);
  EXPECT_EQ(listener2_count, 1);
  EXPECT_EQ(listener3_count, 1);
  
  // Remove middle listener
  client_machine_->removeStateChangeListener(id2);
  
  // Another transition
  client_machine_->transition(HttpSseState::TcpConnecting);
  dispatcher_->run();
  
  EXPECT_EQ(listener1_count, 2);
  EXPECT_EQ(listener2_count, 1);  // Should not increase
  EXPECT_EQ(listener3_count, 2);
}

TEST_F(HttpSseStateMachineTest, LargeNumberOfStreams) {
  client_machine_ = createClientMachine();
  
  const int num_streams = 1000;
  
  // Create many streams
  for (int i = 0; i < num_streams; ++i) {
    auto stream_id = "stream-" + std::to_string(i);
    auto* stream = client_machine_->createStream(stream_id);
    ASSERT_NE(stream, nullptr);
    
    // Simulate some activity
    stream->bytes_sent = i * 100;
    stream->bytes_received = i * 200;
    stream->events_received = i;
    
    // Mark every third stream as zombie
    if (i % 3 == 0) {
      client_machine_->markStreamAsZombie(stream_id);
    }
  }
  
  auto stats = client_machine_->getStreamStats();
  EXPECT_EQ(stats.active_streams, num_streams - (num_streams / 3) - 1);
  EXPECT_EQ(stats.zombie_streams, (num_streams / 3) + 1);
  
  // Cleanup zombies
  client_machine_->cleanupZombieStreams();
  dispatcher_->run();
  
  stats = client_machine_->getStreamStats();
  EXPECT_EQ(stats.zombie_streams, 0);
}

TEST_F(HttpSseStateMachineTest, RapidStateTransitions) {
  client_machine_ = createClientMachine();
  
  // Perform many rapid transitions
  std::vector<HttpSseState> transition_sequence = {
      HttpSseState::Initialized,
      HttpSseState::TcpConnecting,
      HttpSseState::TcpConnected,
      HttpSseState::HttpRequestPreparing,
      HttpSseState::HttpRequestSending,
      HttpSseState::HttpRequestSent,
      HttpSseState::HttpResponseWaiting,
      HttpSseState::HttpResponseHeadersReceiving,
      HttpSseState::SseNegotiating,
      HttpSseState::SseStreamActive
  };
  
  for (auto state : transition_sequence) {
    client_machine_->transition(state);
  }
  
  dispatcher_->run();
  
  // Should end up in the last state
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::SseStreamActive);
  
  // History should contain all transitions
  auto history = client_machine_->getStateHistory(20);
  EXPECT_GE(history.size(), transition_sequence.size());
}

// ===== Performance Tests =====

TEST_F(HttpSseStateMachineTest, PerformanceStateTransitions) {
  client_machine_ = createClientMachine();
  
  const int num_transitions = 10000;
  auto start = std::chrono::steady_clock::now();
  
  for (int i = 0; i < num_transitions; ++i) {
    // Alternate between states
    if (i % 2 == 0) {
      client_machine_->forceTransition(HttpSseState::Initialized);
    } else {
      client_machine_->forceTransition(HttpSseState::TcpConnecting);
    }
  }
  
  dispatcher_->run();
  
  auto end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  
  // Should complete quickly (< 1 second for 10k transitions)
  EXPECT_LT(duration.count(), 1000);
  
  // Verify final state
  EXPECT_EQ(client_machine_->getCurrentState(), HttpSseState::TcpConnecting);
}

TEST_F(HttpSseStateMachineTest, PerformanceStreamOperations) {
  client_machine_ = createClientMachine();
  
  const int num_operations = 5000;
  auto start = std::chrono::steady_clock::now();
  
  // Create, update, and cleanup streams
  for (int i = 0; i < num_operations; ++i) {
    auto stream_id = "perf-stream-" + std::to_string(i);
    auto* stream = client_machine_->createStream(stream_id);
    
    // Simulate events
    client_machine_->onSseEventReceived(stream_id, "test event");
    
    // Mark as zombie and cleanup periodically
    if (i % 100 == 0) {
      client_machine_->markStreamAsZombie(stream_id);
      if (i % 500 == 0) {
        client_machine_->cleanupZombieStreams();
      }
    }
  }
  
  dispatcher_->run();
  
  auto end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  
  // Should complete quickly (< 2 seconds for 5k operations)
  EXPECT_LT(duration.count(), 2000);
}