#include <atomic>
#include <cassert>

#include "mcp/event/event_loop.h"

namespace mcp {
namespace event {

/**
 * @brief Default implementation of Worker
 */
class WorkerImpl : public Worker {
 public:
  WorkerImpl(uint32_t index, DispatcherPtr dispatcher)
      : dispatcher_(std::move(dispatcher)), running_(false) {
    name_ = "worker_" + std::to_string(index);
  }

  ~WorkerImpl() override { stop(); }

  void start(WatchDogSharedPtr guard_dog) override {
    if (running_.exchange(true)) {
      return;  // Already running
    }

    guard_dog_ = guard_dog;

    thread_ = std::make_unique<std::thread>([this]() { threadRoutine(); });
  }

  void stop() override {
    if (!running_.exchange(false)) {
      return;  // Already stopped
    }

    // Request dispatcher to exit
    dispatcher_->exit();

    // Wait for thread to finish
    if (thread_ && thread_->joinable()) {
      thread_->join();
    }

    thread_.reset();
    guard_dog_.reset();
  }

  Dispatcher& dispatcher() override { return *dispatcher_; }

 private:
  void threadRoutine() {
    // Set thread name for debugging
#ifdef __linux__
    pthread_setname_np(pthread_self(), name_.c_str());
#elif defined(__APPLE__)
    pthread_setname_np(name_.c_str());
#endif

    // Register watchdog if provided
    if (guard_dog_) {
      dispatcher_->registerWatchdog(guard_dog_, std::chrono::seconds(5));
    }

    // Run the event loop
    dispatcher_->run(RunType::RunUntilExit);

    // Cleanup
    dispatcher_->shutdown();
  }

  std::string name_;
  DispatcherPtr dispatcher_;
  std::atomic<bool> running_;
  WatchDogSharedPtr guard_dog_;
  std::unique_ptr<std::thread> thread_;
};

/**
 * @brief Default implementation of WorkerFactory
 */
class DefaultWorkerFactory : public WorkerFactory {
 public:
  WorkerPtr createWorker(uint32_t index,
                         DispatcherFactory& dispatcher_factory) override {
    auto dispatcher =
        dispatcher_factory.createDispatcher("worker_" + std::to_string(index));
    return std::make_unique<WorkerImpl>(index, std::move(dispatcher));
  }
};

WorkerFactoryPtr createDefaultWorkerFactory() {
  return std::make_unique<DefaultWorkerFactory>();
}

/**
 * @brief Default implementation of ThreadPool
 */
class ThreadPoolImpl : public ThreadPool {
 public:
  ThreadPoolImpl() : next_worker_index_(0) {}

  ~ThreadPoolImpl() override { stop(); }

  void initialize(size_t num_threads,
                  DispatcherFactory& dispatcher_factory,
                  WorkerFactory& worker_factory) override {
    assert(workers_.empty());  // Can only initialize once

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
      workers_.push_back(worker_factory.createWorker(i, dispatcher_factory));
    }
  }

  void start() override {
    for (auto& worker : workers_) {
      worker->start();
    }
  }

  void stop() override {
    for (auto& worker : workers_) {
      worker->stop();
    }
  }

  Worker& getWorker(size_t index) override {
    assert(index < workers_.size());
    return *workers_[index];
  }

  Worker& nextWorker() override {
    assert(!workers_.empty());

    // Round-robin selection
    size_t index = next_worker_index_.fetch_add(1) % workers_.size();
    return *workers_[index];
  }

  size_t size() const override { return workers_.size(); }

 private:
  std::vector<WorkerPtr> workers_;
  std::atomic<size_t> next_worker_index_;
};

ThreadPoolPtr createThreadPool() { return std::make_unique<ThreadPoolImpl>(); }

/**
 * @brief Simple WatchDog implementation
 */
class WatchDogImpl : public WatchDog {
 public:
  WatchDogImpl(std::thread::id thread_id)
      : thread_id_(thread_id),
        last_touch_time_(std::chrono::steady_clock::now()) {}

  void touch() override { last_touch_time_ = std::chrono::steady_clock::now(); }

  std::thread::id threadId() const override { return thread_id_; }

  std::chrono::steady_clock::time_point lastTouchTime() const override {
    return last_touch_time_.load();
  }

 private:
  std::thread::id thread_id_;
  std::atomic<std::chrono::steady_clock::time_point> last_touch_time_;
};

WatchDogSharedPtr createWatchDog(std::thread::id thread_id) {
  return std::make_shared<WatchDogImpl>(thread_id);
}

}  // namespace event
}  // namespace mcp