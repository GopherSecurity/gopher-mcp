#ifndef MCP_EVENT_LIBEVENT_DISPATCHER_H
#define MCP_EVENT_LIBEVENT_DISPATCHER_H

#include <atomic>
#include <mutex>
#include <queue>
#include <unordered_map>

#include "mcp/event/event_loop.h"

// Forward declarations for libevent types
struct event_base;
struct event;

namespace mcp {
namespace event {

// Rename to avoid conflict with struct event
using libevent_event = struct event;

/**
 * @brief Libevent-based implementation of the Dispatcher interface
 *
 * This implementation uses libevent for cross-platform event handling.
 * It provides all the features and maintains
 * portability across different operating systems.
 */
class LibeventDispatcher : public Dispatcher {
 public:
  explicit LibeventDispatcher(const std::string& name);
  ~LibeventDispatcher() override;

  // DispatcherBase interface
  void post(PostCb callback) override;
  bool isThreadSafe() const override;

  // Dispatcher interface
  const std::string& name() override { return name_; }

  void registerWatchdog(const WatchDogSharedPtr& watchdog,
                        std::chrono::milliseconds min_touch_interval) override;

  FileEventPtr createFileEvent(int fd,
                               FileReadyCb cb,
                               FileTriggerType trigger,
                               uint32_t events) override;

  TimerPtr createTimer(TimerCb cb) override;

  TimerPtr createScaledTimer(ScaledTimerType timer_type, TimerCb cb) override;
  TimerPtr createScaledTimer(ScaledTimerMinimum minimum, TimerCb cb) override;

  SchedulableCallbackPtr createSchedulableCallback(
      std::function<void()> cb) override;

  void deferredDelete(DeferredDeletablePtr&& to_delete) override;

  void exit() override;

  SignalEventPtr listenForSignal(int signal_num, SignalCb cb) override;

  void run(RunType type) override;

  WatermarkFactory& getWatermarkFactory() override;

  void pushTrackedObject(const ScopeTrackedObject* object) override;
  void popTrackedObject(const ScopeTrackedObject* expected_object) override;

  std::chrono::steady_clock::time_point approximateMonotonicTime()
      const override;
  void updateApproximateMonotonicTime() override;

  void clearDeferredDeleteList() override;

  void initializeStats(DispatcherStats& stats) override;

  void shutdown() override;

  // Get the underlying event_base for advanced usage
  event_base* base() { return base_; }

 private:
  // Libevent file event implementation
  class FileEventImpl : public FileEvent {
   public:
    FileEventImpl(LibeventDispatcher& dispatcher,
                  int fd,
                  FileReadyCb cb,
                  FileTriggerType trigger,
                  uint32_t events);
    ~FileEventImpl() override;

    void activate(uint32_t events) override;
    void setEnabled(uint32_t events) override;

   private:
    static void eventCallback(int fd, short events, void* arg);

    LibeventDispatcher& dispatcher_;
    int fd_;
    FileReadyCb cb_;
    FileTriggerType trigger_;
    libevent_event* event_;
    uint32_t enabled_events_;
  };

  // Libevent timer implementation
  class TimerImpl : public Timer {
   public:
    TimerImpl(LibeventDispatcher& dispatcher, TimerCb cb);
    ~TimerImpl() override;

    void disableTimer() override;
    void enableTimer(std::chrono::milliseconds duration) override;
    void enableHRTimer(std::chrono::microseconds duration) override;
    bool enabled() override;

   private:
    static void timerCallback(int fd, short events, void* arg);

    LibeventDispatcher& dispatcher_;
    TimerCb cb_;
    libevent_event* event_;
    bool enabled_;
  };

  // Schedulable callback implementation
  class SchedulableCallbackImpl : public SchedulableCallback {
   public:
    SchedulableCallbackImpl(LibeventDispatcher& dispatcher,
                            std::function<void()> cb);
    ~SchedulableCallbackImpl() override;

    void scheduleCallbackCurrentIteration() override;
    void scheduleCallbackNextIteration() override;
    void cancel() override;
    bool enabled() override;

   private:
    LibeventDispatcher& dispatcher_;
    std::function<void()> cb_;
    std::unique_ptr<TimerImpl> timer_;
    bool scheduled_;
  };

  // Signal event implementation
  class SignalEventImpl : public SignalEvent {
   public:
    SignalEventImpl(LibeventDispatcher& dispatcher,
                    int signal_num,
                    SignalCb cb);
    ~SignalEventImpl() override;

   private:
    static void signalCallback(int fd, short events, void* arg);

    LibeventDispatcher& dispatcher_;
    int signal_num_;
    SignalCb cb_;
    libevent_event* event_;
  };

  // Watchdog registration
  struct WatchdogRegistration {
    WatchDogSharedPtr watchdog;
    std::chrono::milliseconds interval;
    std::unique_ptr<TimerImpl> timer;
  };

  // Helper methods
  void runPostCallbacks();
  void runDeferredDeletes();
  void touchWatchdog();
  void initializeLibevent();
  static void postWakeupCallback(int fd, short events, void* arg);

  // Member variables
  const std::string name_;
  event_base* base_;
  std::thread::id thread_id_;
  std::atomic<bool> exit_requested_{false};

  // Post callback handling
  std::mutex post_mutex_;
  std::queue<PostCb> post_callbacks_;
  int wakeup_fd_[2];  // Pipe for waking up event loop
  libevent_event* wakeup_event_;

  // Deferred deletion
  std::vector<DeferredDeletablePtr> deferred_delete_list_;
  std::unique_ptr<SchedulableCallbackImpl> deferred_delete_cb_;

  // Tracked objects for debugging
  std::vector<const ScopeTrackedObject*> tracked_objects_;

  // Approximate time
  mutable std::chrono::steady_clock::time_point approximate_monotonic_time_;

  // Watchdog
  std::unique_ptr<WatchdogRegistration> watchdog_registration_;

  // Stats
  DispatcherStats* stats_ = nullptr;

  // Buffer factory
  std::unique_ptr<WatermarkFactory> buffer_factory_;
};

/**
 * @brief Factory for creating libevent-based dispatchers
 */
class LibeventDispatcherFactory : public DispatcherFactory {
 public:
  DispatcherPtr createDispatcher(const std::string& name) override;
  const std::string& backendName() const override;

 private:
  static const std::string backend_name_;
};

}  // namespace event
}  // namespace mcp

#endif  // MCP_EVENT_LIBEVENT_DISPATCHER_H