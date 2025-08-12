#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <mutex>
#include <set>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/event/event_loop.h"
#include "mcp/event/libevent_dispatcher.h"
#include "../integration/real_io_test_base.h"

using namespace mcp::event;
using namespace std::chrono_literals;

/**
 * Event loop tests using real IO operations.
 * All operations that require dispatcher thread context are executed
 * within the dispatcher thread using executeInDispatcher().
 */
class EventLoopRealIoTest : public mcp::test::RealIoTestBase {
protected:
  // Additional setup if needed
  void SetUp() override {
    RealIoTestBase::SetUp();
  }
  
  // Helper to store persistent event objects
  std::vector<FileEventPtr> file_events_;
  std::vector<TimerPtr> timers_;
  std::vector<SignalEventPtr> signal_events_;
  std::vector<SchedulableCallbackPtr> schedulable_callbacks_;
  
  void TearDown() override {
    // Clean up event objects within dispatcher thread
    if (dispatcher_) {
      executeInDispatcher([this]() {
        file_events_.clear();
        timers_.clear();
        signal_events_.clear();
        schedulable_callbacks_.clear();
      });
    }
    RealIoTestBase::TearDown();
  }
};

// Test basic dispatcher creation and properties
TEST_F(EventLoopRealIoTest, BasicProperties) {
  EXPECT_EQ("integration_test", dispatcher_->name());
  EXPECT_EQ("libevent", factory_->backendName());
  
  // Check thread safety from within dispatcher thread
  bool is_thread_safe = executeInDispatcher([this]() {
    return dispatcher_->isThreadSafe();
  });
  EXPECT_TRUE(is_thread_safe);
  
  // From test thread, should be false
  EXPECT_FALSE(dispatcher_->isThreadSafe());
}

// Test post callback functionality
TEST_F(EventLoopRealIoTest, PostCallback) {
  std::atomic<bool> called{false};
  std::atomic<int> thread_id{0};
  
  executeInDispatcher([&]() {
    // Post from within dispatcher thread
    dispatcher_->post([&]() {
      called = true;
      thread_id = std::this_thread::get_id() == std::this_thread::get_id() ? 1 : 0;
    });
  });
  
  // Wait for callback
  EXPECT_TRUE(waitFor([&]() { return called.load(); }));
  EXPECT_TRUE(called);
}

// Test multiple post callbacks
TEST_F(EventLoopRealIoTest, MultiplePostCallbacks) {
  const int num_callbacks = 100;
  std::atomic<int> count{0};
  std::vector<int> execution_order;
  std::mutex order_mutex;
  
  executeInDispatcher([&]() {
    for (int i = 0; i < num_callbacks; ++i) {
      dispatcher_->post([&, i]() {
        count++;
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(i);
      });
    }
  });
  
  // Wait for all callbacks
  EXPECT_TRUE(waitFor([&]() { return count.load() == num_callbacks; }, 2s));
  EXPECT_EQ(num_callbacks, count);
  EXPECT_EQ(num_callbacks, execution_order.size());
}

// Test file event for reading (previously DISABLED)
TEST_F(EventLoopRealIoTest, FileEventRead) {
  auto pipe_fds = createPipe();
  int read_fd = pipe_fds.first;
  int write_fd = pipe_fds.second;
  
  std::atomic<bool> read_ready{false};
  std::atomic<int> bytes_read{0};
  
  // Create and store file event within dispatcher thread
  executeInDispatcher([this, read_fd, &read_ready, &bytes_read]() {
    auto file_event = dispatcher_->createFileEvent(
        read_fd,
        [read_fd, &bytes_read, &read_ready](uint32_t events) {
          if (events & static_cast<uint32_t>(FileReadyType::Read)) {
            char buffer[256];
            ssize_t n = ::read(read_fd, buffer, sizeof(buffer));
            if (n > 0) {
              bytes_read = n;
              read_ready = true;
            }
          }
        },
        FileTriggerType::Level, 
        static_cast<uint32_t>(FileReadyType::Read));
    
    // Store the event to keep it alive
    file_events_.push_back(std::move(file_event));
  });
  
  // Give event loop time to register the event
  std::this_thread::sleep_for(10ms);
  
  // Write data to trigger read event
  const char data[] = "test data";
  ssize_t written = write(write_fd, data, sizeof(data));
  EXPECT_EQ(sizeof(data), written);
  
  // Wait for read event
  EXPECT_TRUE(waitFor([&]() { return read_ready.load(); }, 1s));
  EXPECT_TRUE(read_ready);
  EXPECT_EQ(sizeof(data), bytes_read);
}

// Test file event for writing (previously DISABLED)
TEST_F(EventLoopRealIoTest, FileEventWrite) {
  auto pipe_fds = createPipe();
  int read_fd = pipe_fds.first;
  int write_fd = pipe_fds.second;
  
  std::atomic<bool> write_ready{false};
  std::atomic<int> events_received{0};
  
  // Create and store file event within dispatcher thread
  executeInDispatcher([this, write_fd, &write_ready, &events_received]() {
    auto file_event = dispatcher_->createFileEvent(
        write_fd,
        [&write_ready, &events_received](uint32_t events) {
          if (events & static_cast<uint32_t>(FileReadyType::Write)) {
            events_received++;
            write_ready = true;
          }
        },
        FileTriggerType::Level, 
        static_cast<uint32_t>(FileReadyType::Write));
    
    // Store the event to keep it alive
    file_events_.push_back(std::move(file_event));
  });
  
  // Pipe should be immediately writable
  EXPECT_TRUE(waitFor([&]() { return write_ready.load(); }, 500ms));
  EXPECT_TRUE(write_ready);
  EXPECT_GE(events_received, 1);
}

// Test timer functionality (previously DISABLED)
TEST_F(EventLoopRealIoTest, Timer) {
  std::atomic<bool> timer_fired{false};
  std::atomic<int> fire_count{0};
  auto start_time = std::chrono::steady_clock::now();
  
  // Create and store timer within dispatcher thread
  executeInDispatcher([this, &timer_fired, &fire_count]() {
    auto timer = dispatcher_->createTimer([&timer_fired, &fire_count]() {
      fire_count++;
      timer_fired = true;
    });
    
    timer->enableTimer(100ms);
    
    // Store the timer to keep it alive
    timers_.push_back(std::move(timer));
  });
  
  // Wait for timer
  EXPECT_TRUE(waitFor([&]() { return timer_fired.load(); }, 500ms));
  EXPECT_TRUE(timer_fired);
  EXPECT_EQ(1, fire_count);
  
  auto elapsed = std::chrono::steady_clock::now() - start_time;
  EXPECT_GE(elapsed, 100ms);
  EXPECT_LE(elapsed, 300ms);  // Some tolerance
}

// Test high-resolution timer (previously DISABLED)
TEST_F(EventLoopRealIoTest, HighResolutionTimer) {
  std::atomic<bool> timer_fired{false};
  auto start_time = std::chrono::steady_clock::now();
  
  // Create and store timer within dispatcher thread
  executeInDispatcher([this, &timer_fired]() {
    auto timer = dispatcher_->createTimer([&timer_fired]() {
      timer_fired = true;
    });
    
    timer->enableHRTimer(10ms);  // 10 milliseconds
    
    // Store the timer to keep it alive
    timers_.push_back(std::move(timer));
  });
  
  // Wait for timer
  EXPECT_TRUE(waitFor([&]() { return timer_fired.load(); }, 100ms));
  EXPECT_TRUE(timer_fired);
  
  auto elapsed = std::chrono::steady_clock::now() - start_time;
  EXPECT_GE(elapsed, 10ms);
  EXPECT_LE(elapsed, 50ms);  // Some tolerance for high-res timer
}

// Test timer cancellation (previously DISABLED)
TEST_F(EventLoopRealIoTest, TimerCancel) {
  std::atomic<bool> timer_fired{false};
  
  // Create, enable, and cancel timer within dispatcher thread
  executeInDispatcher([this, &timer_fired]() {
    auto timer = dispatcher_->createTimer([&timer_fired]() { 
      timer_fired = true; 
    });
    
    timer->enableTimer(100ms);
    EXPECT_TRUE(timer->enabled());
    
    // Cancel before it fires
    timer->disableTimer();
    EXPECT_FALSE(timer->enabled());
    
    // Store the timer to keep it alive
    timers_.push_back(std::move(timer));
  });
  
  // Give it time to (not) fire
  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(timer_fired);
}

// Test schedulable callback (previously DISABLED)
TEST_F(EventLoopRealIoTest, SchedulableCallback) {
  std::atomic<int> call_count{0};
  
  // Create and store schedulable callback within dispatcher thread
  executeInDispatcher([this, &call_count]() {
    auto callback = dispatcher_->createSchedulableCallback([&call_count]() {
      call_count++;
    });
    
    // Schedule multiple times
    callback->scheduleCallbackNextIteration();
    callback->scheduleCallbackNextIteration();  // Should coalesce
    
    // Store the callback to keep it alive
    schedulable_callbacks_.push_back(std::move(callback));
  });
  
  // Wait for callback
  EXPECT_TRUE(waitFor([&]() { return call_count.load() > 0; }));
  
  // Should only fire once due to coalescing
  std::this_thread::sleep_for(50ms);
  EXPECT_EQ(1, call_count);
  
  // Schedule again
  executeInDispatcher([this]() {
    if (!schedulable_callbacks_.empty()) {
      schedulable_callbacks_[0]->scheduleCallbackNextIteration();
    }
  });
  
  EXPECT_TRUE(waitFor([&]() { return call_count.load() == 2; }));
  EXPECT_EQ(2, call_count);
}

// Test signal handling (previously DISABLED)
TEST_F(EventLoopRealIoTest, SignalEvent) {
  std::atomic<bool> signal_received{false};
  std::atomic<int> signal_count{0};
  
  // Create and store signal event within dispatcher thread
  executeInDispatcher([this, &signal_received, &signal_count]() {
    auto signal_event = dispatcher_->listenForSignal(SIGUSR1, [&signal_received, &signal_count]() {
      signal_count++;
      signal_received = true;
    });
    
    // Store the signal event to keep it alive
    signal_events_.push_back(std::move(signal_event));
  });
  
  // Give event loop time to register the signal
  std::this_thread::sleep_for(10ms);
  
  // Send signal to self
  kill(getpid(), SIGUSR1);
  
  // Wait for signal handler
  EXPECT_TRUE(waitFor([&]() { return signal_received.load(); }));
  EXPECT_TRUE(signal_received);
  EXPECT_EQ(1, signal_count);
}

// Test multiple signals (previously DISABLED)
TEST_F(EventLoopRealIoTest, MultipleSignals) {
  std::atomic<int> usr1_count{0};
  std::atomic<int> usr2_count{0};
  
  // Create and store multiple signal events within dispatcher thread
  executeInDispatcher([this, &usr1_count, &usr2_count]() {
    auto signal1 = dispatcher_->listenForSignal(SIGUSR1, [&usr1_count]() {
      usr1_count++;
    });
    
    auto signal2 = dispatcher_->listenForSignal(SIGUSR2, [&usr2_count]() {
      usr2_count++;
    });
    
    // Store the signal events to keep them alive
    signal_events_.push_back(std::move(signal1));
    signal_events_.push_back(std::move(signal2));
  });
  
  // Give event loop time to register the signals
  std::this_thread::sleep_for(10ms);
  
  // Send both signals
  kill(getpid(), SIGUSR1);
  kill(getpid(), SIGUSR2);
  kill(getpid(), SIGUSR1);  // Send USR1 twice
  
  // Wait for signals
  EXPECT_TRUE(waitFor([&]() { 
    return usr1_count.load() >= 2 && usr2_count.load() >= 1; 
  }));
  
  EXPECT_EQ(2, usr1_count);
  EXPECT_EQ(1, usr2_count);
}

// Test complex integration scenario
TEST_F(EventLoopRealIoTest, ComplexIntegration) {
  // Test multiple async operations together
  std::atomic<int> timer_count{0};
  std::atomic<int> post_count{0};
  std::atomic<int> file_event_count{0};
  
  auto pipe_fds = createPipe();
  int read_fd = pipe_fds.first;
  int write_fd = pipe_fds.second;
  
  // Create all events within dispatcher thread and store them
  executeInDispatcher([this, read_fd, write_fd, &timer_count, &post_count, &file_event_count]() {
    // Create timer that fires 3 times
    // Note: We'll create 3 separate timers to avoid self-reference issues
    for (int i = 0; i < 3; ++i) {
      auto timer = dispatcher_->createTimer([&timer_count, write_fd, i]() {
        timer_count++;
        // Write to pipe on timer
        const char data = 'x';
        ::write(write_fd, &data, 1);
      });
      timer->enableTimer(std::chrono::milliseconds(50 * (i + 1)));
      timers_.push_back(std::move(timer));
    }
    
    // Create file event
    auto file_event = dispatcher_->createFileEvent(
        read_fd,
        [this, read_fd, &file_event_count, &post_count](uint32_t events) {
          if (events & static_cast<uint32_t>(FileReadyType::Read)) {
            char buffer[10];
            ssize_t n = ::read(read_fd, buffer, sizeof(buffer));
            if (n > 0) {
              file_event_count++;
              // Post callback for each byte read
              for (ssize_t i = 0; i < n; ++i) {
                dispatcher_->post([&post_count]() {
                  post_count++;
                });
              }
            }
          }
        },
        FileTriggerType::Level, 
        static_cast<uint32_t>(FileReadyType::Read));
    
    file_events_.push_back(std::move(file_event));
  });
  
  // Wait for multiple timer fires
  EXPECT_TRUE(waitFor([&]() { return timer_count.load() >= 3; }, 1s));
  
  // Verify all components worked
  EXPECT_GE(timer_count, 3);
  EXPECT_GE(file_event_count, 2);  // Should have received file events
  EXPECT_GE(post_count, 2);  // Should have posted callbacks
}

// Test dispatcher exit behavior
TEST_F(EventLoopRealIoTest, DispatcherExit) {
  std::atomic<bool> before_exit{false};
  std::atomic<bool> after_exit{false};
  std::atomic<bool> exit_called{false};
  
  // Create a new dispatcher for this test
  auto test_dispatcher = factory_->createDispatcher("exit_test");
  
  std::thread t([&]() {
    test_dispatcher->post([&]() {
      before_exit = true;
      test_dispatcher->exit();
      exit_called = true;
      // This might or might not execute depending on implementation
      after_exit = true;
    });
    test_dispatcher->run(RunType::RunUntilExit);
  });
  
  t.join();
  
  EXPECT_TRUE(before_exit);
  EXPECT_TRUE(exit_called);
  // after_exit behavior is implementation-dependent
}

// Test RunType::Block behavior
TEST_F(EventLoopRealIoTest, RunTypeBlock) {
  auto test_dispatcher = factory_->createDispatcher("block_test");
  std::atomic<int> iterations{0};
  
  // Post some work
  for (int i = 0; i < 5; ++i) {
    test_dispatcher->post([&]() {
      iterations++;
    });
  }
  
  // Run once, blocking
  test_dispatcher->run(RunType::Block);
  
  // All posted work should be done
  EXPECT_EQ(5, iterations);
}

// Test thread safety with real operations
TEST_F(EventLoopRealIoTest, ThreadSafetyWithRealIO) {
  const int num_threads = 10;
  const int ops_per_thread = 100;
  std::atomic<int> total_ops{0};
  std::vector<std::thread> threads;
  
  // Create pipes for each thread
  std::vector<std::pair<int, int>> pipes;
  for (int i = 0; i < num_threads; ++i) {
    pipes.push_back(createPipe());
  }
  
  // Set up file events within dispatcher and store them
  executeInDispatcher([this, &pipes, &total_ops]() {
    for (auto& pipe_pair : pipes) {
      int read_fd = pipe_pair.first;
      auto file_event = dispatcher_->createFileEvent(
          read_fd,
          [read_fd, &total_ops](uint32_t events) {
            if (events & static_cast<uint32_t>(FileReadyType::Read)) {
              char buffer[256];
              while (::read(read_fd, buffer, sizeof(buffer)) > 0) {
                total_ops++;
              }
            }
          },
          FileTriggerType::Level,
          static_cast<uint32_t>(FileReadyType::Read));
      
      // Store the event to keep it alive
      file_events_.push_back(std::move(file_event));
    }
  });
  
  // Give event loop time to register all events
  std::this_thread::sleep_for(50ms);
  
  // Start threads that write to pipes
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&, i]() {
      auto write_fd = pipes[i].second;
      for (int j = 0; j < ops_per_thread; ++j) {
        char data = 'A' + (j % 26);
        write(write_fd, &data, 1);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    });
  }
  
  // Wait for all threads
  for (auto& t : threads) {
    t.join();
  }
  
  // Wait for all operations to complete
  EXPECT_TRUE(waitFor([&]() { 
    return total_ops.load() >= num_threads * ops_per_thread; 
  }, 5s));
  
  EXPECT_EQ(num_threads * ops_per_thread, total_ops);
}