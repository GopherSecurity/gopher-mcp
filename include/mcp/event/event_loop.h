#ifndef MCP_EVENT_EVENT_LOOP_H
#define MCP_EVENT_EVENT_LOOP_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "mcp/core/compat.h"

namespace mcp {
namespace event {

// Forward declaration
class WatermarkFactory {
 public:
  virtual ~WatermarkFactory() = default;
};

// Forward declarations
class Dispatcher;
class FileEvent;
class Timer;
class SignalEvent;
class SchedulableCallback;
class DeferredDeletable;
class Scheduler;
class ScaledRangeTimerManager;
class WatchDog;

// Type aliases
using DispatcherPtr = std::unique_ptr<Dispatcher>;
using FileEventPtr = std::unique_ptr<FileEvent>;
using TimerPtr = std::unique_ptr<Timer>;
using SignalEventPtr = std::unique_ptr<SignalEvent>;
using SchedulableCallbackPtr = std::unique_ptr<SchedulableCallback>;
using DeferredDeletablePtr = std::unique_ptr<DeferredDeletable>;
using WatchDogSharedPtr = std::shared_ptr<WatchDog>;

// Callback types
using PostCb = std::function<void()>;
using FileReadyCb = std::function<void(uint32_t events)>;
using TimerCb = std::function<void()>;
using SignalCb = std::function<void()>;

// File event types (matches epoll/kqueue semantics)
enum class FileReadyType : uint32_t {
  Read = 0x01,
  Write = 0x02,
  Closed = 0x04,
  Error = 0x08
};

// Combine FileReadyType flags
inline FileReadyType operator|(FileReadyType a, FileReadyType b) {
  return static_cast<FileReadyType>(static_cast<uint32_t>(a) |
                                    static_cast<uint32_t>(b));
}

inline uint32_t operator&(FileReadyType a, uint32_t b) {
  return static_cast<uint32_t>(a) & b;
}

// File trigger types
enum class FileTriggerType {
  // Level-triggered events - continuously fire while condition is met
  // Used on all platforms for DNS and TCP listeners
  Level,
  
  // Edge-triggered events - fire only on state transitions  
  // Default on Linux/macOS for better performance (EPOLLET/EV_CLEAR)
  Edge,
  
  // Synthetic edge events managed by the framework
  // Based on level events that are immediately disabled when triggered
  // Consumer must re-enable the event when the socket operation would block
  // Main application: Windows which lacks native edge-triggered events
  // Can only be used where PlatformDefaultTriggerType is EmulatedEdge
  EmulatedEdge
};

// Determine platform-preferred event type
constexpr FileTriggerType determinePlatformPreferredEventType() {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(FORCE_LEVEL_EVENTS)
  // Windows doesn't support native edge triggers, use emulated
  // FORCE_LEVEL_EVENTS allows testing Windows behavior on POSIX
  return FileTriggerType::EmulatedEdge;
#else
  // Linux/macOS/BSD support native edge triggering
  return FileTriggerType::Edge;
#endif
}

// Platform default trigger type
static constexpr FileTriggerType PlatformDefaultTriggerType = determinePlatformPreferredEventType();

// Run types for dispatcher
enum class RunType {
  Block,        // Run until exit() is called
  NonBlock,     // Run one iteration
  RunUntilExit  // Run until exit() is called, blocking for events
};

// Timer types for scaled timers
enum class ScaledTimerType {
  // Timer that scales with CPU/network load
  HttpConnectionManagerDecodeDuration,
  HttpConnectionManagerNewStream,
  OverloadActionState,
  // Add more as needed
};

// Minimum timer values for scaled timers
struct ScaledTimerMinimum {
  std::chrono::milliseconds min_value;
  std::chrono::milliseconds max_value{std::chrono::milliseconds::max()};
};

/**
 * @brief Interface for objects that should be deleted on a deferred basis.
 */
class DeferredDeletable {
 public:
  virtual ~DeferredDeletable() = default;
};

/**
 * @brief Abstract interface for file events
 *
 * File events monitor file descriptors for read/write/error conditions.
 * Supports edge-triggered, level-triggered, and emulated edge modes.
 */
class FileEvent {
 public:
  virtual ~FileEvent() = default;

  /**
   * Activate the file event explicitly. Used for edge-triggered events
   * that need manual activation.
   */
  virtual void activate(uint32_t events) = 0;

  /**
   * Enable the file event with a new set of event types to monitor.
   */
  virtual void setEnabled(uint32_t events) = 0;
  
  /**
   * Unregister event if using emulated edge triggering.
   * Called when socket operation would block to disable the event.
   */
  virtual void unregisterEventIfEmulatedEdge(uint32_t event) = 0;
  
  /**
   * Re-register event if using emulated edge triggering.
   * Called to re-enable the event after socket becomes ready again.
   */
  virtual void registerEventIfEmulatedEdge(uint32_t event) = 0;
};

/**
 * @brief Abstract interface for timers
 *
 * Timers execute callbacks after a specified duration.
 */
class Timer {
 public:
  virtual ~Timer() = default;

  /**
   * Disable the timer. No-op if already disabled.
   */
  virtual void disableTimer() = 0;

  /**
   * Enable the timer to fire once after the given duration.
   */
  virtual void enableTimer(std::chrono::milliseconds duration) = 0;

  /**
   * Enable the timer to fire repeatedly at the given interval.
   */
  virtual void enableHRTimer(std::chrono::microseconds duration) = 0;

  /**
   * Return whether the timer is currently enabled.
   */
  virtual bool enabled() = 0;
};

/**
 * @brief Abstract interface for schedulable callbacks
 *
 * Callbacks that can be scheduled to run in the current or next iteration.
 */
class SchedulableCallback {
 public:
  virtual ~SchedulableCallback() = default;

  /**
   * Schedule the callback to run in the current event loop iteration.
   * Note: if called from outside the event loop thread, the callback
   * will run in the next iteration.
   */
  virtual void scheduleCallbackCurrentIteration() = 0;

  /**
   * Schedule the callback to run in the next event loop iteration.
   */
  virtual void scheduleCallbackNextIteration() = 0;

  /**
   * Cancel a pending callback.
   */
  virtual void cancel() = 0;

  /**
   * Return whether the callback is currently enabled.
   */
  virtual bool enabled() = 0;
};

/**
 * @brief Abstract interface for signal events
 */
class SignalEvent {
 public:
  virtual ~SignalEvent() = default;
};

/**
 * @brief ScopeTracker interface for debugging and tracking
 */
class ScopeTrackedObject {
 public:
  virtual ~ScopeTrackedObject() = default;
  virtual void dumpState(std::ostream& os, int indent_level = 0) const = 0;
};

/**
 * @brief Base dispatcher interface
 *
 * Provides minimal interface for posting callbacks and thread safety checks.
 */
class DispatcherBase {
 public:
  virtual ~DispatcherBase() = default;

  /**
   * Post a callback to be executed in the dispatcher thread.
   * Thread-safe: can be called from any thread.
   */
  virtual void post(PostCb callback) = 0;

  /**
   * Check if the current thread is the dispatcher thread.
   */
  virtual bool isThreadSafe() const = 0;
};

/**
 * @brief Stats for dispatcher performance monitoring
 */
struct DispatcherStats {
  // Histogram of event loop iteration durations
  std::string loop_duration_us;
  // Histogram of poll delays
  std::string poll_delay_us;
};

/**
 * @brief Main event dispatcher interface
 *
 * This is the core event loop abstraction:
 * - Single-threaded event loop per dispatcher
 * - Thread-safe posting from other threads
 * - Deferred deletion for safe object lifecycle
 * - Integrated file events, timers, and signals
 * - Support for scaled timers that adapt to load
 * - Watchdog integration for monitoring
 *
 * Each worker thread has its own dispatcher instance.
 */
class Dispatcher : public DispatcherBase {
 public:
  virtual ~Dispatcher() = default;

  /**
   * Return the name of this dispatcher (e.g., "worker_0", "main_thread").
   */
  virtual const std::string& name() = 0;

  /**
   * Register a watchdog for monitoring this dispatcher.
   * The dispatcher will touch the watchdog periodically.
   */
  virtual void registerWatchdog(
      const WatchDogSharedPtr& watchdog,
      std::chrono::milliseconds min_touch_interval) = 0;

  /**
   * Create a file event that monitors a file descriptor.
   */
  virtual FileEventPtr createFileEvent(int fd,
                                       FileReadyCb cb,
                                       FileTriggerType trigger,
                                       uint32_t events) = 0;

  /**
   * Create a timer.
   */
  virtual TimerPtr createTimer(TimerCb cb) = 0;

  /**
   * Create a scaled timer that adapts its duration based on load.
   */
  virtual TimerPtr createScaledTimer(ScaledTimerType timer_type,
                                     TimerCb cb) = 0;
  virtual TimerPtr createScaledTimer(ScaledTimerMinimum minimum,
                                     TimerCb cb) = 0;

  /**
   * Create a schedulable callback.
   */
  virtual SchedulableCallbackPtr createSchedulableCallback(
      std::function<void()> cb) = 0;

  /**
   * Submit an item for deferred deletion.
   * The item will be deleted in a future event loop iteration.
   */
  virtual void deferredDelete(DeferredDeletablePtr&& to_delete) = 0;

  /**
   * Exit the event loop.
   */
  virtual void exit() = 0;

  /**
   * Listen for a signal. Only one dispatcher per process should listen for
   * signals.
   */
  virtual SignalEventPtr listenForSignal(int signal_num, SignalCb cb) = 0;

  /**
   * Run the event loop.
   */
  virtual void run(RunType type) = 0;

  /**
   * Get the buffer factory for this dispatcher.
   */
  virtual WatermarkFactory& getWatermarkFactory() = 0;

  /**
   * Push a tracked object for debugging.
   */
  virtual void pushTrackedObject(const ScopeTrackedObject* object) = 0;

  /**
   * Pop a tracked object.
   */
  virtual void popTrackedObject(const ScopeTrackedObject* expected_object) = 0;

  /**
   * Return approximate monotonic time without a system call.
   */
  virtual std::chrono::steady_clock::time_point approximateMonotonicTime()
      const = 0;

  /**
   * Update the approximate monotonic time.
   */
  virtual void updateApproximateMonotonicTime() = 0;

  /**
   * Clear the deferred deletion queue.
   */
  virtual void clearDeferredDeleteList() = 0;

  /**
   * Initialize stats for monitoring.
   */
  virtual void initializeStats(DispatcherStats& stats) = 0;

  /**
   * Shutdown the dispatcher.
   */
  virtual void shutdown() = 0;
};

/**
 * @brief Scheduler interface for timer management
 */
class Scheduler {
 public:
  virtual ~Scheduler() = default;

  /**
   * Create a timer with the scheduler.
   */
  virtual TimerPtr createTimer(const TimerCb& cb, Dispatcher& dispatcher) = 0;
};

/**
 * @brief Watchdog interface for monitoring dispatchers
 */
class WatchDog {
 public:
  virtual ~WatchDog() = default;

  /**
   * Touch the watchdog to indicate the dispatcher is responsive.
   */
  virtual void touch() = 0;

  /**
   * Get the thread ID being watched.
   */
  virtual std::thread::id threadId() const = 0;

  /**
   * Get the current monotonic time.
   */
  virtual std::chrono::steady_clock::time_point lastTouchTime() const = 0;
};

/**
 * @brief Factory for creating dispatchers
 *
 * Different backends (libevent, epoll, kqueue, IOCP, ASIO) can provide
 * their own factory implementations.
 */
class DispatcherFactory {
 public:
  virtual ~DispatcherFactory() = default;

  /**
   * Create a new dispatcher instance.
   *
   * @param name Name for the dispatcher (e.g., "worker_0")
   * @return Unique pointer to the dispatcher
   */
  virtual DispatcherPtr createDispatcher(const std::string& name) = 0;

  /**
   * Get the name of this factory's backend.
   *
   * @return Backend name (e.g., "libevent", "epoll", "kqueue", "iocp", "asio")
   */
  virtual const std::string& backendName() const = 0;
};

using DispatcherFactoryPtr = std::unique_ptr<DispatcherFactory>;

/**
 * @brief Get the default dispatcher factory for the current platform
 *
 * This will return:
 * - epoll on Linux
 * - kqueue on macOS/BSD
 * - IOCP on Windows
 */
DispatcherFactoryPtr createPlatformDefaultDispatcherFactory();

/**
 * @brief Create a libevent-based dispatcher factory
 *
 * Libevent provides a portable event notification library that works
 * across different platforms.
 */
DispatcherFactoryPtr createLibeventDispatcherFactory();

/**
 * @brief Create an ASIO-based dispatcher factory
 *
 * ASIO provides a cross-platform C++ library for network programming.
 */
DispatcherFactoryPtr createAsioDispatcherFactory();

/**
 * @brief Worker thread abstraction
 *
 * Each worker thread runs its own dispatcher and handles connections
 * independently. This design allows for efficient multi-core scaling.
 */
class Worker {
 public:
  virtual ~Worker() = default;

  /**
   * Start the worker thread.
   *
   * @param guard_dog Optional watchdog for monitoring
   */
  virtual void start(WatchDogSharedPtr guard_dog = nullptr) = 0;

  /**
   * Stop the worker thread.
   *
   * This will cause the dispatcher to exit and the thread to join.
   */
  virtual void stop() = 0;

  /**
   * Get the dispatcher for this worker.
   */
  virtual Dispatcher& dispatcher() = 0;
};

using WorkerPtr = std::unique_ptr<Worker>;

/**
 * @brief Worker factory for creating worker threads
 */
class WorkerFactory {
 public:
  virtual ~WorkerFactory() = default;

  /**
   * Create a new worker instance.
   *
   * @param index Worker index (e.g., 0, 1, 2...)
   * @param dispatcher_factory Factory for creating the dispatcher
   * @return Unique pointer to the worker
   */
  virtual WorkerPtr createWorker(uint32_t index,
                                 DispatcherFactory& dispatcher_factory) = 0;
};

using WorkerFactoryPtr = std::unique_ptr<WorkerFactory>;

/**
 * @brief Create the default worker factory
 */
WorkerFactoryPtr createDefaultWorkerFactory();

/**
 * @brief Thread pool for running multiple workers
 *
 * This manages a pool of worker threads, each with its own dispatcher.
 * Work is distributed across workers using various strategies (round-robin,
 * least-connections, etc.).
 */
class ThreadPool {
 public:
  virtual ~ThreadPool() = default;

  /**
   * Initialize the thread pool with a given number of workers.
   *
   * @param num_threads Number of worker threads to create
   * @param dispatcher_factory Factory for creating dispatchers
   * @param worker_factory Factory for creating workers
   */
  virtual void initialize(size_t num_threads,
                          DispatcherFactory& dispatcher_factory,
                          WorkerFactory& worker_factory) = 0;

  /**
   * Start all worker threads.
   */
  virtual void start() = 0;

  /**
   * Stop all worker threads.
   */
  virtual void stop() = 0;

  /**
   * Get a worker by index.
   *
   * @param index Worker index
   * @return Reference to the worker
   */
  virtual Worker& getWorker(size_t index) = 0;

  /**
   * Get the next worker using the configured strategy.
   *
   * @return Reference to the next worker
   */
  virtual Worker& nextWorker() = 0;

  /**
   * Get the number of workers.
   */
  virtual size_t size() const = 0;
};

using ThreadPoolPtr = std::unique_ptr<ThreadPool>;

/**
 * @brief Create a thread pool instance
 */
ThreadPoolPtr createThreadPool();

/**
 * @brief Create a watchdog instance for monitoring a thread
 *
 * @param thread_id The thread ID to monitor
 * @return Shared pointer to the watchdog
 */
WatchDogSharedPtr createWatchDog(std::thread::id thread_id);

}  // namespace event
}  // namespace mcp

#endif  // MCP_EVENT_EVENT_LOOP_H