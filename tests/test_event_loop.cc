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

using namespace mcp::event;
using namespace std::chrono_literals;

class EventLoopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    factory_ = createLibeventDispatcherFactory();
    dispatcher_ = factory_->createDispatcher("test");
  }

  void TearDown() override {
    dispatcher_.reset();
    factory_.reset();
  }

  // Helper to run dispatcher in background thread
  std::thread runInBackground(RunType type = RunType::RunUntilExit) {
    return std::thread([this, type]() { dispatcher_->run(type); });
  }

  // Helper to create a pipe for testing
  struct PipeFds {
    int read_fd;
    int write_fd;
  };

  PipeFds createPipe() {
    int fds[2];
    EXPECT_EQ(0, pipe(fds));

    // Make non-blocking
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);

    return {fds[0], fds[1]};
  }

  DispatcherFactoryPtr factory_;
  DispatcherPtr dispatcher_;
};

// Test basic dispatcher creation and properties
TEST_F(EventLoopTest, BasicProperties) {
  EXPECT_EQ("test", dispatcher_->name());
  EXPECT_EQ("libevent", factory_->backendName());
  EXPECT_FALSE(dispatcher_->isThreadSafe()); // Not in dispatcher thread yet
}

// Test post callback functionality
TEST_F(EventLoopTest, PostCallback) {
  std::atomic<bool> called{false};
  std::mutex mutex;
  std::condition_variable cv;

  dispatcher_->post([&]() {
    called = true;
    cv.notify_one();
  });

  auto thread = runInBackground();

  // Wait for callback
  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(cv.wait_for(lock, 1s, [&]() { return called.load(); }));

  dispatcher_->exit();
  thread.join();
}

// Test multiple post callbacks
TEST_F(EventLoopTest, MultiplePostCallbacks) {
  const int num_callbacks = 100;
  std::atomic<int> count{0};
  std::mutex mutex;
  std::condition_variable cv;

  for (int i = 0; i < num_callbacks; ++i) {
    dispatcher_->post([&]() {
      count++;
      if (count == num_callbacks) {
        cv.notify_one();
      }
    });
  }

  auto thread = runInBackground();

  // Wait for all callbacks
  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(cv.wait_for(lock, 2s, [&]() { return count == num_callbacks; }));

  dispatcher_->exit();
  thread.join();
}

// Test cross-thread posting
TEST_F(EventLoopTest, CrossThreadPost) {
  std::atomic<std::thread::id> callback_thread_id;
  std::promise<void> promise;
  auto future = promise.get_future();

  auto thread = runInBackground();

  // Post from main thread
  dispatcher_->post([&]() {
    callback_thread_id = std::this_thread::get_id();
    promise.set_value();
  });

  ASSERT_EQ(std::future_status::ready, future.wait_for(1s));

  // Callback should run in dispatcher thread
  EXPECT_EQ(thread.get_id(), callback_thread_id.load());

  dispatcher_->exit();
  thread.join();
}

// Test file event for reading
TEST_F(EventLoopTest, FileEventRead) {
  auto pipe_fds = createPipe();
  int read_fd = pipe_fds.read_fd;
  int write_fd = pipe_fds.write_fd;

  std::atomic<bool> read_ready{false};
  std::promise<void> promise;
  auto future = promise.get_future();

  auto file_event = dispatcher_->createFileEvent(
      read_fd,
      [&](uint32_t events) {
        if (events & static_cast<uint32_t>(FileReadyType::Read)) {
          bool expected = false;
          if (read_ready.compare_exchange_strong(expected, true)) {
            promise.set_value();
          }
        }
      },
      FileTriggerType::Level, static_cast<uint32_t>(FileReadyType::Read));

  auto thread = runInBackground();

  // Write data to trigger read event
  const char data[] = "test";
  EXPECT_EQ(sizeof(data), write(write_fd, data, sizeof(data)));

  ASSERT_EQ(std::future_status::ready, future.wait_for(1s));
  EXPECT_TRUE(read_ready);

  dispatcher_->exit();
  thread.join();

  close(read_fd);
  close(write_fd);
}

// Test file event for writing
TEST_F(EventLoopTest, FileEventWrite) {
  auto pipe_fds = createPipe();
  int read_fd = pipe_fds.read_fd;
  int write_fd = pipe_fds.write_fd;

  std::atomic<bool> write_ready{false};
  std::promise<void> promise;
  auto future = promise.get_future();

  auto file_event = dispatcher_->createFileEvent(
      write_fd,
      [&](uint32_t events) {
        if (events & static_cast<uint32_t>(FileReadyType::Write)) {
          bool expected = false;
          if (write_ready.compare_exchange_strong(expected, true)) {
            promise.set_value();
          }
        }
      },
      FileTriggerType::Level, static_cast<uint32_t>(FileReadyType::Write));

  auto thread = runInBackground();

  // Pipe should be immediately writable
  ASSERT_EQ(std::future_status::ready, future.wait_for(1s));
  EXPECT_TRUE(write_ready);

  dispatcher_->exit();
  thread.join();

  close(read_fd);
  close(write_fd);
}

// Test timer functionality
TEST_F(EventLoopTest, Timer) {
  std::atomic<bool> timer_fired{false};
  std::promise<void> promise;
  auto future = promise.get_future();

  auto start_time = std::chrono::steady_clock::now();

  auto timer = dispatcher_->createTimer([&]() {
    timer_fired = true;
    promise.set_value();
  });

  timer->enableTimer(100ms);

  auto thread = runInBackground();

  ASSERT_EQ(std::future_status::ready, future.wait_for(500ms));
  EXPECT_TRUE(timer_fired);

  auto elapsed = std::chrono::steady_clock::now() - start_time;
  EXPECT_GE(elapsed, 100ms);
  EXPECT_LE(elapsed, 200ms);  // Some tolerance

  dispatcher_->exit();
  thread.join();
}

// Test high-resolution timer
TEST_F(EventLoopTest, HighResolutionTimer) {
  std::atomic<bool> timer_fired{false};
  std::promise<void> promise;
  auto future = promise.get_future();

  auto timer = dispatcher_->createTimer([&]() {
    timer_fired = true;
    promise.set_value();
  });

  timer->enableHRTimer(10ms);  // 10 milliseconds

  auto thread = runInBackground();

  ASSERT_EQ(std::future_status::ready, future.wait_for(100ms));
  EXPECT_TRUE(timer_fired);

  dispatcher_->exit();
  thread.join();
}

// Test timer cancellation
TEST_F(EventLoopTest, TimerCancel) {
  std::atomic<bool> timer_fired{false};

  auto timer = dispatcher_->createTimer([&]() { timer_fired = true; });

  timer->enableTimer(100ms);
  EXPECT_TRUE(timer->enabled());

  // Cancel before it fires
  timer->disableTimer();
  EXPECT_FALSE(timer->enabled());

  auto thread = runInBackground();

  // Give it time to (not) fire
  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(timer_fired);

  dispatcher_->exit();
  thread.join();
}

// Test schedulable callback
TEST_F(EventLoopTest, SchedulableCallback) {
  std::atomic<int> call_count{0};
  std::promise<void> promise;
  auto future = promise.get_future();

  auto callback = dispatcher_->createSchedulableCallback([&]() {
    if (++call_count == 2) {
      promise.set_value();
    }
  });

  auto thread = runInBackground();

  // Schedule twice
  callback->scheduleCallbackNextIteration();
  std::this_thread::sleep_for(10ms);
  callback->scheduleCallbackNextIteration();

  ASSERT_EQ(std::future_status::ready, future.wait_for(1s));
  EXPECT_EQ(2, call_count.load());

  dispatcher_->exit();
  thread.join();
}

// Test schedulable callback cancellation
TEST_F(EventLoopTest, SchedulableCallbackCancel) {
  std::atomic<bool> called{false};

  auto callback =
      dispatcher_->createSchedulableCallback([&]() { called = true; });

  callback->scheduleCallbackNextIteration();
  EXPECT_TRUE(callback->enabled());

  // Cancel before it runs
  callback->cancel();
  EXPECT_FALSE(callback->enabled());

  auto thread = runInBackground();

  // Give it time to (not) run
  std::this_thread::sleep_for(100ms);
  EXPECT_FALSE(called);

  dispatcher_->exit();
  thread.join();
}

// Test deferred deletion
TEST_F(EventLoopTest, DeferredDelete) {
  struct TestObject : public DeferredDeletable {
    explicit TestObject(std::atomic<bool>& deleted) : deleted_(deleted) {}
    ~TestObject() override { deleted_ = true; }
    std::atomic<bool>& deleted_;
  };

  std::atomic<bool> deleted{false};
  auto object = std::make_unique<TestObject>(deleted);

  auto thread = runInBackground();

  // Schedule deletion
  dispatcher_->deferredDelete(std::move(object));

  // Object should be deleted in next iteration
  std::this_thread::sleep_for(100ms);
  EXPECT_TRUE(deleted);

  dispatcher_->exit();
  thread.join();
}

// Test approximate monotonic time
TEST_F(EventLoopTest, ApproximateMonotonicTime) {
  auto thread = runInBackground();

  // Get initial time
  auto time1 = dispatcher_->approximateMonotonicTime();

  // Wait and update
  std::this_thread::sleep_for(100ms);
  dispatcher_->post(
      [this]() { dispatcher_->updateApproximateMonotonicTime(); });

  std::this_thread::sleep_for(50ms);
  auto time2 = dispatcher_->approximateMonotonicTime();

  // Time should have advanced
  EXPECT_GT(time2, time1);

  dispatcher_->exit();
  thread.join();
}

// Test signal handling (if not on Windows)
#ifndef _WIN32
TEST_F(EventLoopTest, SignalEvent) {
  std::atomic<bool> signal_received{false};
  std::promise<void> promise;
  auto future = promise.get_future();

  auto signal_event = dispatcher_->listenForSignal(SIGUSR1, [&]() {
    signal_received = true;
    promise.set_value();
  });

  auto thread = runInBackground();

  // Send signal to self
  std::this_thread::sleep_for(50ms);  // Let dispatcher start
  kill(getpid(), SIGUSR1);

  ASSERT_EQ(std::future_status::ready, future.wait_for(1s));
  EXPECT_TRUE(signal_received);

  dispatcher_->exit();
  thread.join();
}
#endif

// Test RunType::NonBlock
TEST_F(EventLoopTest, NonBlockingRun) {
  std::atomic<bool> called{false};

  dispatcher_->post([&]() { called = true; });

  // Should process the callback and return immediately
  dispatcher_->run(RunType::NonBlock);

  EXPECT_TRUE(called);
}

// Test RunType::Block
TEST_F(EventLoopTest, BlockingRun) {
  std::atomic<bool> timer_fired{false};

  auto timer = dispatcher_->createTimer([&]() {
    timer_fired = true;
    dispatcher_->exit();
  });

  timer->enableTimer(50ms);

  auto start = std::chrono::steady_clock::now();
  dispatcher_->run(RunType::Block);
  auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_TRUE(timer_fired);
  EXPECT_GE(elapsed, 50ms);
}

// Test watchdog integration
TEST_F(EventLoopTest, Watchdog) {
  class TestWatchdog : public WatchDog {
   public:
    TestWatchdog() : thread_id_(std::this_thread::get_id()) {}

    void touch() override {
      touch_count_++;
      last_touch_time_ = std::chrono::steady_clock::now();
    }

    std::thread::id threadId() const override { return thread_id_; }

    std::chrono::steady_clock::time_point lastTouchTime() const override {
      return last_touch_time_;
    }

    int touchCount() const { return touch_count_; }

   private:
    std::thread::id thread_id_;
    std::atomic<int> touch_count_{0};
    std::atomic<std::chrono::steady_clock::time_point> last_touch_time_;
  };

  auto watchdog = std::make_shared<TestWatchdog>();

  auto thread = runInBackground();

  // Register watchdog
  dispatcher_->post([&]() { dispatcher_->registerWatchdog(watchdog, 50ms); });

  // Let it run for a while
  std::this_thread::sleep_for(250ms);

  // Should have been touched multiple times
  EXPECT_GE(watchdog->touchCount(), 3);

  dispatcher_->exit();
  thread.join();
}

// Test worker functionality
TEST_F(EventLoopTest, Worker) {
  auto worker_factory = createDefaultWorkerFactory();
  auto worker = worker_factory->createWorker(0, *factory_);

  std::atomic<bool> called{false};
  std::promise<void> promise;
  auto future = promise.get_future();

  worker->start();

  worker->dispatcher().post([&]() {
    called = true;
    promise.set_value();
  });

  ASSERT_EQ(std::future_status::ready, future.wait_for(1s));
  EXPECT_TRUE(called);

  worker->stop();
}

// Test thread pool
TEST_F(EventLoopTest, ThreadPool) {
  auto thread_pool = createThreadPool();
  auto worker_factory = createDefaultWorkerFactory();

  const size_t num_workers = 4;
  thread_pool->initialize(num_workers, *factory_, *worker_factory);

  EXPECT_EQ(num_workers, thread_pool->size());

  thread_pool->start();

  // Test round-robin worker selection
  std::set<Dispatcher*> dispatchers;
  for (size_t i = 0; i < num_workers * 2; ++i) {
    auto& worker = thread_pool->nextWorker();
    dispatchers.insert(&worker.dispatcher());
  }

  EXPECT_EQ(num_workers, dispatchers.size());

  // Post work to all workers
  std::atomic<int> total_work{0};
  std::promise<void> promise;
  auto future = promise.get_future();

  const int work_per_worker = 10;
  const int total_expected = num_workers * work_per_worker;

  for (size_t i = 0; i < num_workers; ++i) {
    auto& worker = thread_pool->getWorker(i);
    for (int j = 0; j < work_per_worker; ++j) {
      worker.dispatcher().post([&]() {
        if (++total_work == total_expected) {
          promise.set_value();
        }
      });
    }
  }

  ASSERT_EQ(std::future_status::ready, future.wait_for(2s));
  EXPECT_EQ(total_expected, total_work.load());

  thread_pool->stop();
}

// Test high load scenario
TEST_F(EventLoopTest, HighLoad) {
  const int num_events = 1000;
  std::atomic<int> events_processed{0};
  std::vector<FileEventPtr> file_events;
  std::vector<TimerPtr> timers;
  std::vector<PipeFds> pipes;

  // Create many file events
  for (int i = 0; i < 10; ++i) {
    auto pipe_fds = createPipe();
    int read_fd = pipe_fds.read_fd;
    pipes.push_back(pipe_fds);

    file_events.push_back(dispatcher_->createFileEvent(
        read_fd, [&](uint32_t /*events*/) { events_processed++; },
        FileTriggerType::Level, static_cast<uint32_t>(FileReadyType::Read)));
  }

  // Create many timers
  for (int i = 0; i < 10; ++i) {
    auto timer = dispatcher_->createTimer([&]() { events_processed++; });
    timer->enableTimer(std::chrono::milliseconds(10 + i * 10));
    timers.push_back(std::move(timer));
  }

  // Post many callbacks
  for (int i = 0; i < num_events; ++i) {
    dispatcher_->post([&]() { events_processed++; });
  }

  auto thread = runInBackground();

  // Trigger file events
  for (auto& pipe : pipes) {
    write(pipe.write_fd, "x", 1);
  }

  // Wait for events to be processed
  auto start = std::chrono::steady_clock::now();
  while (events_processed < num_events + 20 &&
         std::chrono::steady_clock::now() - start < 5s) {
    std::this_thread::sleep_for(10ms);
  }

  EXPECT_GE(events_processed.load(), num_events + 20);

  dispatcher_->exit();
  thread.join();

  // Cleanup pipes
  for (auto& pipe : pipes) {
    close(pipe.read_fd);
    close(pipe.write_fd);
  }
}

// Test thread safety of isThreadSafe()
TEST_F(EventLoopTest, ThreadSafetyCheck) {
  std::atomic<bool> is_thread_safe{false};
  std::promise<void> promise;
  auto future = promise.get_future();

  // Create dispatcher in a different thread to test cross-thread behavior
  DispatcherPtr other_dispatcher;
  std::thread create_thread(
      [&]() { other_dispatcher = factory_->createDispatcher("other"); });
  create_thread.join();

  auto thread =
      std::thread([&]() { other_dispatcher->run(RunType::RunUntilExit); });

  // Check from main thread - should be false since dispatcher is in another
  // thread
  EXPECT_FALSE(other_dispatcher->isThreadSafe());

  // Check from dispatcher thread
  other_dispatcher->post([&]() {
    is_thread_safe = other_dispatcher->isThreadSafe();
    promise.set_value();
  });

  ASSERT_EQ(std::future_status::ready, future.wait_for(1s));
  EXPECT_TRUE(is_thread_safe);

  other_dispatcher->exit();
  thread.join();
}

// Test edge-triggered file events (Linux only)
#ifdef __linux__
TEST_F(EventLoopTest, EdgeTriggeredFileEvent) {
  auto pipe_fds = createPipe();
  int read_fd = pipe_fds.read_fd;
  int write_fd = pipe_fds.write_fd;

  std::atomic<int> read_count{0};
  std::promise<void> promise;
  auto future = promise.get_future();

  auto file_event = dispatcher_->createFileEvent(
      read_fd,
      [&](uint32_t events) {
        if (events & static_cast<uint32_t>(FileReadyType::Read)) {
          read_count++;
          if (read_count == 1) {
            // Don't read data, should not trigger again
            promise.set_value();
          }
        }
      },
      FileTriggerType::Edge, static_cast<uint32_t>(FileReadyType::Read));

  auto thread = runInBackground();

  // Write data
  write(write_fd, "test", 4);

  ASSERT_EQ(std::future_status::ready, future.wait_for(1s));

  // Wait to see if it triggers again (it shouldn't)
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(1, read_count.load());

  dispatcher_->exit();
  thread.join();

  close(read_fd);
  close(write_fd);
}
#endif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}