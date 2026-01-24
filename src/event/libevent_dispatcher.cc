#include "mcp/event/libevent_dispatcher.h"

#include <cassert>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "mcp/logging/log_macros.h"

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#endif

#include <event2/event.h>
#include <event2/thread.h>
#include <event2/util.h>

namespace mcp {
namespace event {

namespace {

// Convert our event types to libevent flags
short toLibeventEvents(uint32_t events, FileTriggerType trigger) {
  short result = 0;
  if (events & static_cast<uint32_t>(FileReadyType::Read)) {
    result |= EV_READ;
  }
  if (events & static_cast<uint32_t>(FileReadyType::Write)) {
    result |= EV_WRITE;
  }

  // Add trigger type specific flags
  if (trigger == FileTriggerType::Edge) {
#ifdef EV_ET
    result |= EV_ET;  // Edge-triggered (Linux epoll)
    result |= EV_PERSIST;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    // macOS/BSD: EV_CLEAR provides edge-triggered behavior for kqueue
    // EV_CLEAR: Clear the event state after reporting (edge-triggered)
    // Don't use EV_PERSIST with EV_CLEAR
    result |= EV_CLEAR;
#else
    result |= EV_PERSIST;
#endif
  } else if (trigger == FileTriggerType::EmulatedEdge) {
    // EmulatedEdge: do not use EV_PERSIST, re-add event after each trigger
    // This simulates edge-triggered behavior on platforms without native
    // support
  } else {
    // Level-triggered: use EV_PERSIST for continuous monitoring
    result |= EV_PERSIST;
  }

  return result;
}

// Convert libevent flags to our event types
uint32_t fromLibeventEvents(short events) {
  uint32_t result = 0;
  if (events & EV_READ) {
    result |= static_cast<uint32_t>(FileReadyType::Read);
  }
  if (events & EV_WRITE) {
    result |= static_cast<uint32_t>(FileReadyType::Write);
  }
  if (events & EV_TIMEOUT) {
    result |= static_cast<uint32_t>(FileReadyType::Error);
  }
  return result;
}

// Lazy initialization for libevent threading support
// Uses std::call_once to ensure thread-safe one-time initialization
// This avoids blocking in static initialization which was causing hangs
void ensureLibeventThreadingInitialized() {
  static std::once_flag init_flag;
  std::call_once(init_flag, []() {
#ifdef _WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif
    // Enable debug mode in debug builds
#ifndef NDEBUG
    event_enable_debug_mode();
#endif
  });
}

}  // namespace

// LibeventDispatcher implementation
LibeventDispatcher::LibeventDispatcher(const std::string& name) : name_(name) {
  // Initialize libevent threading support on first use (lazy initialization)
  ensureLibeventThreadingInitialized();

  // Initialize shared validity flag - timers use this to safely check if
  // the dispatcher is still valid without accessing dispatcher members
  dispatcher_valid_ = std::make_shared<std::atomic<bool>>(true);

  // Don't set thread_id_ here - it should only be set when run() is called
  initializeLibevent();
  updateApproximateMonotonicTime();
}

LibeventDispatcher::~LibeventDispatcher() {
  shutdown();

  if (wakeup_event_) {
    event_free(wakeup_event_);
  }

  if (wakeup_fd_[0] >= 0) {
#ifdef _WIN32
    evutil_closesocket(wakeup_fd_[0]);
#else
    close(wakeup_fd_[0]);
#endif
  }
  if (wakeup_fd_[1] >= 0) {
#ifdef _WIN32
    evutil_closesocket(wakeup_fd_[1]);
#else
    close(wakeup_fd_[1]);
#endif
  }

  if (base_) {
    event_base_free(base_);
  }
}

void LibeventDispatcher::initializeLibevent() {
  // Create event base with more efficient backend if available
  struct event_config* config = event_config_new();
  if (config) {
#ifdef __linux__
    // Prefer epoll on Linux
    event_config_avoid_method(config, "select");
    event_config_avoid_method(config, "poll");
#endif
    event_config_set_flag(config, EVENT_BASE_FLAG_PRECISE_TIMER);
    base_ = event_base_new_with_config(config);
    event_config_free(config);
  } else {
    base_ = event_base_new();
  }

  if (!base_) {
    throw std::runtime_error("Failed to create event base");
  }

  // Log which backend libevent is using
  const char* method = event_base_get_method(base_);
  GOPHER_LOG_DEBUG("Created event base using backend: {}",
                   method ? method : "unknown");

  // Create pipe for waking up the event loop
  // TODO We can keep evutil_socketpair as platformm independent code
  // and remove pipe later
#ifdef _WIN32
  // On Windows, use evutil_socketpair which emulates Unix socketpair
  // evutil_socketpair expects evutil_socket_t* (intptr_t*), cast from our
  // SOCKET type
  evutil_socket_t temp_fds[2];
  if (evutil_socketpair(AF_INET, SOCK_STREAM, 0, temp_fds) != 0) {
    throw std::runtime_error("Failed to create wakeup pipe");
  }
  wakeup_fd_[0] = static_cast<libevent_socket_t>(temp_fds[0]);
  wakeup_fd_[1] = static_cast<libevent_socket_t>(temp_fds[1]);
#else
  if (pipe(wakeup_fd_) != 0) {
    throw std::runtime_error("Failed to create wakeup pipe");
  }
#endif

  // Make pipe non-blocking
  evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(wakeup_fd_[0]));
  evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(wakeup_fd_[1]));

  // Cast callback to match libevent's expected signature (evutil_socket_t is
  // intptr_t)
  wakeup_event_ = event_new(base_, static_cast<evutil_socket_t>(wakeup_fd_[0]),
                            EV_READ | EV_PERSIST,
                            reinterpret_cast<event_callback_fn>(
                                &LibeventDispatcher::postWakeupCallback),
                            this);

  if (!wakeup_event_) {
    throw std::runtime_error("Failed to create wakeup event");
  }

  event_add(wakeup_event_, nullptr);

  // Create buffer factory
  buffer_factory_ = std::make_unique<WatermarkFactory>();

  // Create deferred delete callback
  deferred_delete_cb_ = std::make_unique<SchedulableCallbackImpl>(
      *this, [this]() { runDeferredDeletes(); });
}

void LibeventDispatcher::post(PostCb callback) {
  bool need_wakeup = false;
  {
    std::lock_guard<std::mutex> lock(post_mutex_);
    need_wakeup = post_callbacks_.empty();
    post_callbacks_.push(std::move(callback));
  }

  GOPHER_LOG_TRACE("post(): need_wakeup={} isThreadSafe={}", need_wakeup,
                   isThreadSafe());

  if (need_wakeup) {
    // Wake up the event loop - the queue was empty, so the event loop
    // may be blocked waiting for events. We need to wake it up even if
    // we're on the dispatcher thread, because event_base_loop() in Block
    // mode won't return to check post_callbacks_ until an event fires.
    GOPHER_LOG_TRACE("post(): waking up event loop");
    char byte = 1;
#ifdef _WIN32
    int rc = send(wakeup_fd_[1], &byte, 1, 0);
#else
    ssize_t rc = write(wakeup_fd_[1], &byte, 1);
#endif
    (void)rc;  // Ignore EAGAIN
  }
}

bool LibeventDispatcher::isThreadSafe() const {
  // If thread_id_ is not set (run() hasn't been called yet),
  // we're not in the dispatcher thread yet.
  // Return false to indicate we need synchronization (e.g., in post())
  if (thread_id_ == std::thread::id()) {
    return false;
  }
  return std::this_thread::get_id() == thread_id_;
}

void LibeventDispatcher::registerWatchdog(
    const WatchDogSharedPtr& watchdog,
    std::chrono::milliseconds min_touch_interval) {
  assert(thread_id_ == std::thread::id() || isThreadSafe());

  watchdog_registration_ = std::make_unique<WatchdogRegistration>();
  watchdog_registration_->watchdog = watchdog;
  watchdog_registration_->interval = min_touch_interval;

  // Create timer to touch watchdog periodically
  watchdog_registration_->timer = std::make_unique<TimerImpl>(
      *this,
      [this]() {
        touchWatchdog();
        watchdog_registration_->timer->enableTimer(
            watchdog_registration_->interval);
      },
      dispatcher_valid_);

  // Start the timer
  watchdog_registration_->timer->enableTimer(min_touch_interval);

  // Touch immediately
  touchWatchdog();
}

FileEventPtr LibeventDispatcher::createFileEvent(os_fd_t fd,
                                                 FileReadyCb cb,
                                                 FileTriggerType trigger,
                                                 uint32_t events) {
  assert(thread_id_ == std::thread::id() || isThreadSafe());

  const char* trigger_str = (trigger == FileTriggerType::Level) ? "Level"
                            : (trigger == FileTriggerType::Edge)
                                ? "Edge"
                                : "EmulatedEdge";
  GOPHER_LOG_TRACE("createFileEvent: fd={} events={} trigger={}", fd, events,
                   trigger_str);

  return std::make_unique<FileEventImpl>(*this, fd, std::move(cb), trigger,
                                         events);
}

TimerPtr LibeventDispatcher::createTimer(TimerCb cb) {
  assert(thread_id_ == std::thread::id() || isThreadSafe());
  return std::make_unique<TimerImpl>(*this, std::move(cb), dispatcher_valid_);
}

TimerPtr LibeventDispatcher::createScaledTimer(ScaledTimerType /*timer_type*/,
                                               TimerCb cb) {
  // For now, just create a regular timer
  // TODO: Implement scaled timer logic based on load
  return createTimer(std::move(cb));
}

TimerPtr LibeventDispatcher::createScaledTimer(ScaledTimerMinimum /*minimum*/,
                                               TimerCb cb) {
  // For now, just create a regular timer
  // TODO: Implement scaled timer logic based on load
  return createTimer(std::move(cb));
}

SchedulableCallbackPtr LibeventDispatcher::createSchedulableCallback(
    std::function<void()> cb) {
  assert(thread_id_ == std::thread::id() || isThreadSafe());
  return std::make_unique<SchedulableCallbackImpl>(*this, std::move(cb));
}

void LibeventDispatcher::deferredDelete(DeferredDeletablePtr&& to_delete) {
  assert(thread_id_ == std::thread::id() || isThreadSafe());
  deferred_delete_list_.push_back(std::move(to_delete));

  // Schedule deferred delete callback if not already scheduled
  if (!deferred_delete_cb_->enabled()) {
    deferred_delete_cb_->scheduleCallbackCurrentIteration();
  }
}

void LibeventDispatcher::exit() {
  exit_requested_ = true;

  if (!isThreadSafe()) {
    // Wake up the event loop and break it
    // In Block mode, the loop won't exit until event_base_loopbreak is called
    post([this]() { event_base_loopbreak(base_); });
  } else {
    event_base_loopbreak(base_);
  }
}

SignalEventPtr LibeventDispatcher::listenForSignal(int signal_num,
                                                   SignalCb cb) {
  assert(thread_id_ == std::thread::id() || isThreadSafe());
  return std::make_unique<SignalEventImpl>(*this, signal_num, std::move(cb));
}

void LibeventDispatcher::run(RunType type) {
  // Reset exit flag to allow dispatcher reuse
  exit_requested_ = false;
  thread_id_ = std::this_thread::get_id();

  const char* type_str = (type == RunType::Block)          ? "Block"
                         : (type == RunType::NonBlock)     ? "NonBlock"
                         : (type == RunType::RunUntilExit) ? "RunUntilExit"
                                                           : "Unknown";
  GOPHER_LOG_TRACE("run(): starting, type={}", type_str);

  // Run any pending post callbacks before starting
  runPostCallbacks();

  int flags = 0;
  switch (type) {
    case RunType::Block:
      // Run until no more events
      break;
    case RunType::NonBlock:
      flags = EVLOOP_NONBLOCK;
      break;
    case RunType::RunUntilExit:
      while (!exit_requested_) {
        updateApproximateMonotonicTime();
        event_base_loop(base_, EVLOOP_ONCE);
        runPostCallbacks();
      }
      GOPHER_LOG_TRACE("run(): RunUntilExit loop ended");
      return;
  }

  updateApproximateMonotonicTime();
  int result = event_base_loop(base_, flags);

  GOPHER_LOG_TRACE("run(): event_base_loop returned {} (type={})", result,
                   type_str);
  if (result == 1) {
    GOPHER_LOG_TRACE("run(): event_base_loop returned 1 (no events)");
  } else if (result == -1) {
    GOPHER_LOG_ERROR("run(): event_base_loop returned -1");
  }

  runPostCallbacks();
}

WatermarkFactory& LibeventDispatcher::getWatermarkFactory() {
  return *buffer_factory_;
}

void LibeventDispatcher::pushTrackedObject(const ScopeTrackedObject* object) {
  assert(isThreadSafe());
  tracked_objects_.push_back(object);
}

void LibeventDispatcher::popTrackedObject(
    const ScopeTrackedObject* expected_object) {
  assert(isThreadSafe());
  assert(!tracked_objects_.empty());
  assert(tracked_objects_.back() == expected_object);
  tracked_objects_.pop_back();
}

std::chrono::steady_clock::time_point
LibeventDispatcher::approximateMonotonicTime() const {
  return approximate_monotonic_time_;
}

void LibeventDispatcher::updateApproximateMonotonicTime() {
  approximate_monotonic_time_ = std::chrono::steady_clock::now();
}

void LibeventDispatcher::clearDeferredDeleteList() {
  assert(isThreadSafe());
  deferred_delete_list_.clear();
}

void LibeventDispatcher::initializeStats(DispatcherStats& stats) {
  stats_ = &stats;
}

void LibeventDispatcher::shutdown() {
  // IMPORTANT: Always clear callbacks even when called from another thread.
  // When the dispatcher is being destroyed (e.g., after
  // dispatcher_thread_.join()), isThreadSafe() returns false but we still need
  // to clear pending callbacks BEFORE the event_base is freed. Otherwise, the
  // callback destructors (e.g., FileEvent destructor calling event_del) will
  // access freed memory.

  // CRITICAL FIX: Set exit_requested_ to prevent callbacks (like touchWatchdog)
  // from accessing resources that are about to be destroyed. This must be set
  // before clearing any resources.
  exit_requested_.store(true, std::memory_order_release);

  // CRITICAL FIX: Mark dispatcher as invalid so timer callbacks can safely
  // check this flag without accessing potentially destroyed dispatcher members.
  // This shared_ptr is held by all timers created by this dispatcher.
  if (dispatcher_valid_) {
    dispatcher_valid_->store(false, std::memory_order_release);
  }

  // Clear all pending work - must happen before event_base_free
  deferred_delete_list_.clear();

  // Clear post callbacks - must happen before event_base_free
  {
    std::lock_guard<std::mutex> lock(post_mutex_);
    std::queue<PostCb> empty;
    post_callbacks_.swap(empty);
  }

  // Stop watchdog (only if on same thread to avoid thread safety issues)
  if (isThreadSafe()) {
    watchdog_registration_.reset();
  }
}

void LibeventDispatcher::postWakeupCallback(libevent_socket_t fd,
                                            short /*events*/,
                                            void* arg) {
  auto* dispatcher = static_cast<LibeventDispatcher*>(arg);

  GOPHER_LOG_TRACE("postWakeupCallback: entering");

  // Drain the pipe
  char buffer[256];
#ifdef _WIN32
  while (recv(fd, buffer, sizeof(buffer), 0) > 0) {
#else
  while (read(fd, buffer, sizeof(buffer)) > 0) {
#endif
    // Continue draining
  }

  dispatcher->runPostCallbacks();

  GOPHER_LOG_TRACE(
      "postWakeupCallback: returning, active events={} added={}",
      event_base_get_num_events(dispatcher->base_, EVENT_BASE_COUNT_ACTIVE),
      event_base_get_num_events(dispatcher->base_, EVENT_BASE_COUNT_ADDED));
}

void LibeventDispatcher::runPostCallbacks() {
  std::queue<PostCb> callbacks;
  {
    std::lock_guard<std::mutex> lock(post_mutex_);
    callbacks.swap(post_callbacks_);
  }

  if (!callbacks.empty()) {
    GOPHER_LOG_TRACE("runPostCallbacks(): running {} callbacks",
                     callbacks.size());
  }

  while (!callbacks.empty()) {
    callbacks.front()();
    callbacks.pop();

    // Touch watchdog periodically
    if (watchdog_registration_ && callbacks.size() % 100 == 0) {
      touchWatchdog();
    }
  }
}

void LibeventDispatcher::runDeferredDeletes() {
  assert(isThreadSafe());

  // Move list to avoid issues if callbacks add more deferred deletes
  std::vector<DeferredDeletablePtr> to_delete;
  to_delete.swap(deferred_delete_list_);

  // Objects are deleted when vector goes out of scope
}

void LibeventDispatcher::touchWatchdog() {
  // CRITICAL FIX: Guard against accessing watchdog_registration_ during or
  // after shutdown. When the dispatcher is being destroyed,
  // watchdog_registration_ may have been reset or is in the process of being
  // destroyed. Timer callbacks that fire during shutdown may call
  // touchWatchdog() after the registration is destroyed, causing a
  // use-after-free crash.
  if (exit_requested_.load(std::memory_order_acquire)) {
    return;
  }
  if (watchdog_registration_ && watchdog_registration_->watchdog) {
    watchdog_registration_->watchdog->touch();
  }
}

// FileEventImpl implementation
LibeventDispatcher::FileEventImpl::FileEventImpl(LibeventDispatcher& dispatcher,
                                                 os_fd_t fd,
                                                 FileReadyCb cb,
                                                 FileTriggerType trigger,
                                                 uint32_t events)
    : dispatcher_(dispatcher),
      fd_(fd),
      cb_(std::move(cb)),
      trigger_(trigger),
      enabled_events_(0) {
#ifdef _WIN32
  // Windows only supports level-triggered mode via select()
  // PlatformDefaultTriggerType should be Level on Windows
  if (trigger != FileTriggerType::Level) {
    throw std::runtime_error(
        "Windows only supports FileTriggerType::Level. "
        "Use PlatformDefaultTriggerType for cross-platform code.");
  }
  // On Windows, don't create the event here - create it in assignEvents()
  // with the proper flags from the start. This ensures the socket is properly
  // registered with select() when the event is created.
  event_ = nullptr;
#else
  // Validate EmulatedEdge usage
  if constexpr (PlatformDefaultTriggerType != FileTriggerType::EmulatedEdge) {
    if (trigger_ == FileTriggerType::EmulatedEdge) {
      throw std::runtime_error(
          "Cannot use EmulatedEdge events on platforms where they are not the "
          "default");
    }
  }

  event_ = event_new(dispatcher_.base(), fd_, 0, &FileEventImpl::eventCallback,
                     this);
  if (!event_) {
    throw std::runtime_error("Failed to create file event");
  }

  if (trigger_ == FileTriggerType::EmulatedEdge) {
    // Create activation callback for emulated edge support
    activation_cb_ = std::make_unique<SchedulableCallbackImpl>(
        dispatcher_, [this]() { mergeInjectedEventsAndRunCb(0); });
  }
#endif

  setEnabled(events);
}

LibeventDispatcher::FileEventImpl::~FileEventImpl() {
  if (event_) {
    if (event_added_) {
      event_del(event_);
    }
    event_free(event_);
  }
}

void LibeventDispatcher::FileEventImpl::activate(uint32_t events) {
  if (trigger_ == FileTriggerType::EmulatedEdge && activation_cb_) {
    // For emulated edge, schedule callback with injected events
    injected_activation_events_ |= events;
    if (injected_activation_events_ != 0) {
      activation_cb_->scheduleCallbackCurrentIteration();
    }
  } else if (event_) {
    // For Edge and Level triggers, use direct activation
    short libevent_events = toLibeventEvents(events, trigger_);
    if (libevent_events != 0) {
      event_active(event_, libevent_events, 0);
    }
  }
}

void LibeventDispatcher::FileEventImpl::setEnabled(uint32_t events) {
  GOPHER_LOG_TRACE("setEnabled: fd={} events={} (prev={})", fd_, events,
                   enabled_events_);
  // For edge-triggered, always update even if mask unchanged
  // This forces re-computation of readable/writable state
  if (trigger_ != FileTriggerType::Edge && enabled_events_ == events) {
    GOPHER_LOG_TRACE("setEnabled: skipping (no change)");
    return;
  }
  enabled_events_ = events;
  updateEvents(events);
}

void LibeventDispatcher::FileEventImpl::updateEvents(uint32_t events) {
#ifdef _WIN32
  // On Windows, assignEvents() creates/recreates the event entirely.
  // event_ may be nullptr initially (deferred creation from constructor).
  bool had_events = event_added_;
  event_added_ = false;

  if (events != 0) {
    assignEvents(events);
    event_added_ = true;
  } else if (had_events && event_) {
    // No new events requested, just delete and free the old event
    event_del(event_);
    event_free(event_);
    event_ = nullptr;
  }
#else
  if (event_) {
    // Only call event_del if the event was previously added to the event base
    // This prevents the "event has no event_base set" warning
    if (event_added_) {
      event_del(event_);
      event_added_ = false;
    }

    if (events != 0) {
      assignEvents(events);
      event_added_ = true;
    }
  }
#endif
}

void LibeventDispatcher::FileEventImpl::assignEvents(uint32_t events) {
  short libevent_events = toLibeventEvents(events, trigger_);

#ifdef _WIN32
  // On Windows, we create events fresh each time with the proper flags.
  // This is because Windows select() requires the socket to be properly
  // registered when the event is created, not reassigned later.
  if (event_) {
    event_del(event_);
    event_free(event_);
  }

  // Create new event with proper callback and arg
  event_ = event_new(dispatcher_.base(), fd_, libevent_events,
                     &FileEventImpl::eventCallback, this);

  if (!event_) {
    GOPHER_LOG_ERROR("event_new() returned NULL for fd={}", fd_);
    return;
  }

  int add_result = event_add(event_, nullptr);
  if (add_result != 0) {
    GOPHER_LOG_ERROR("event_add() failed for fd={}", fd_);
  }
#else
  event_assign(event_, dispatcher_.base(), fd_, libevent_events,
               &FileEventImpl::eventCallback, this);
  int add_result = event_add(event_, nullptr);
  GOPHER_LOG_TRACE("assignEvents: fd={} libevent_events=0x{:x} event_add={}",
                   fd_, libevent_events, add_result);
#endif
}

void LibeventDispatcher::FileEventImpl::eventCallback(libevent_socket_t fd,
                                                      short events,
                                                      void* arg) {
  auto* file_event = static_cast<FileEventImpl*>(arg);

  GOPHER_LOG_TRACE("eventCallback: fd={} events=0x{:x} enabled={}", fd, events,
                   file_event->enabled_events_);

  // Update approximate time before callback
  file_event->dispatcher_.updateApproximateMonotonicTime();

  // Save dispatcher reference before callback - callback may schedule deferred
  // delete
  auto& dispatcher = file_event->dispatcher_;

  uint32_t ready_events = fromLibeventEvents(events);

  if (file_event->trigger_ == FileTriggerType::EmulatedEdge) {
    // For emulated edge, merge with any injected events
    file_event->mergeInjectedEventsAndRunCb(ready_events);

    // Re-enable the event for next trigger (emulating edge behavior)
    if (file_event->enabled_events_ != 0) {
      file_event->assignEvents(file_event->enabled_events_);
    }
  } else if (ready_events != 0) {
    // CRITICAL FIX: Save values before callback since callback may
    // disable/delete this object This happens when connection closes during
    // event handling (e.g., connection refused) The connection code uses
    // deferred delete, but we still need to check enabled_events_
#if defined(__APPLE__) || defined(__FreeBSD__)
    auto trigger = file_event->trigger_;
#endif

    file_event->cb_(ready_events);

    // After callback, check if event was disabled (enabled_events_ == 0)
    // If disabled, don't try to re-add the event
#if defined(__APPLE__) || defined(__FreeBSD__)
    // On macOS/BSD with EV_CLEAR, we need to re-add the event after it fires
    // This is necessary for edge-triggered behavior with kqueue
    // Only do this if the event is still enabled
    if (trigger == FileTriggerType::Edge && file_event->enabled_events_ != 0) {
      file_event->assignEvents(file_event->enabled_events_);
    }
#endif
  }

  // Touch watchdog after callback
  dispatcher.touchWatchdog();
  (void)fd;  // Suppress unused parameter warning
}

void LibeventDispatcher::FileEventImpl::mergeInjectedEventsAndRunCb(
    uint32_t events) {
  // Merge real events with any injected activation events
  uint32_t merged_events = events | injected_activation_events_;
  injected_activation_events_ = 0;

  if (merged_events != 0) {
    cb_(merged_events);
  }
}

void LibeventDispatcher::FileEventImpl::unregisterEventIfEmulatedEdge(
    uint32_t event) {
  if (trigger_ != FileTriggerType::EmulatedEdge) {
    return;
  }

  // Disable the specific event type
  enabled_events_ &= ~event;
  updateEvents(enabled_events_);
}

void LibeventDispatcher::FileEventImpl::registerEventIfEmulatedEdge(
    uint32_t event) {
  if (trigger_ != FileTriggerType::EmulatedEdge) {
    return;
  }

  // Re-enable the specific event type
  enabled_events_ |= event;
  updateEvents(enabled_events_);
}

// TimerImpl implementation
LibeventDispatcher::TimerImpl::TimerImpl(
    LibeventDispatcher& dispatcher,
    TimerCb cb,
    std::shared_ptr<std::atomic<bool>> dispatcher_valid)
    : dispatcher_(dispatcher),
      cb_(std::move(cb)),
      enabled_(false),
      dispatcher_valid_(std::move(dispatcher_valid)) {
  event_ = evtimer_new(
      dispatcher_.base(),
      reinterpret_cast<event_callback_fn>(&TimerImpl::timerCallback), this);
  if (!event_) {
    throw std::runtime_error("Failed to create timer");
  }
}

LibeventDispatcher::TimerImpl::~TimerImpl() {
  if (event_) {
    event_del(event_);
    event_free(event_);
  }
}

void LibeventDispatcher::TimerImpl::disableTimer() {
  if (enabled_ && event_) {
    event_del(event_);
    enabled_ = false;
  }
}

void LibeventDispatcher::TimerImpl::enableTimer(
    std::chrono::milliseconds duration) {
  struct timeval tv;
  tv.tv_sec = duration.count() / 1000;
  tv.tv_usec = (duration.count() % 1000) * 1000;

  event_add(event_, &tv);
  enabled_ = true;
}

void LibeventDispatcher::TimerImpl::enableHRTimer(
    std::chrono::microseconds duration) {
  struct timeval tv;
  tv.tv_sec = duration.count() / 1000000;
  tv.tv_usec = duration.count() % 1000000;

  event_add(event_, &tv);
  enabled_ = true;
}

bool LibeventDispatcher::TimerImpl::enabled() { return enabled_; }

void LibeventDispatcher::TimerImpl::timerCallback(libevent_socket_t /*fd*/,
                                                  short /*events*/,
                                                  void* arg) {
  auto* timer = static_cast<TimerImpl*>(arg);

  timer->enabled_ = false;

  // CRITICAL FIX: Check dispatcher validity using shared flag BEFORE accessing
  // any dispatcher members. The shared_ptr allows us to check validity without
  // dereferencing the potentially dangling dispatcher_ reference.
  if (!timer->dispatcher_valid_ ||
      !timer->dispatcher_valid_->load(std::memory_order_acquire)) {
    // Dispatcher is destroyed or shutting down - do not access it
    return;
  }

  // Update approximate time before callback
  timer->dispatcher_.updateApproximateMonotonicTime();

  // Run the user callback. This callback might cause the dispatcher to be
  // destroyed (e.g., if it completes a future that unblocks cleanup code).
  // After cb_() returns, we MUST NOT access dispatcher_ as it may be freed.
  timer->cb_();

  // CRITICAL FIX: Do NOT access dispatcher_ after the callback.
  // The callback may have triggered destruction of the dispatcher.
  // Watchdog touching is a non-essential optimization that can be skipped
  // to avoid use-after-free crashes.
  // timer->dispatcher_.touchWatchdog();  // Removed - unsafe after cb_()
}

// SchedulableCallbackImpl implementation
LibeventDispatcher::SchedulableCallbackImpl::SchedulableCallbackImpl(
    LibeventDispatcher& dispatcher, std::function<void()> cb)
    : dispatcher_(dispatcher), cb_(std::move(cb)), scheduled_(false) {
  // Use a timer with 0 delay for scheduling
  timer_ = std::make_unique<TimerImpl>(
      dispatcher_,
      [this]() {
        scheduled_ = false;
        cb_();
      },
      dispatcher_.dispatcher_valid_);
}

LibeventDispatcher::SchedulableCallbackImpl::~SchedulableCallbackImpl() {
  cancel();
}

void LibeventDispatcher::SchedulableCallbackImpl::
    scheduleCallbackCurrentIteration() {
  if (!scheduled_) {
    if (dispatcher_.isThreadSafe()) {
      // We're in the dispatcher thread, run immediately
      scheduled_ = false;
      cb_();
    } else {
      // Post to dispatcher thread
      dispatcher_.post([this]() {
        if (scheduled_) {
          scheduled_ = false;
          cb_();
        }
      });
      scheduled_ = true;
    }
  }
}

void LibeventDispatcher::SchedulableCallbackImpl::
    scheduleCallbackNextIteration() {
  if (!scheduled_) {
    timer_->enableTimer(std::chrono::milliseconds(0));
    scheduled_ = true;
  }
}

void LibeventDispatcher::SchedulableCallbackImpl::cancel() {
  if (scheduled_) {
    timer_->disableTimer();
    scheduled_ = false;
  }
}

bool LibeventDispatcher::SchedulableCallbackImpl::enabled() {
  return scheduled_;
}

// SignalEventImpl implementation
LibeventDispatcher::SignalEventImpl::SignalEventImpl(
    LibeventDispatcher& dispatcher, int signal_num, SignalCb cb)
    : dispatcher_(dispatcher), signal_num_(signal_num), cb_(std::move(cb)) {
  event_ = evsignal_new(
      dispatcher_.base(), signal_num_,
      reinterpret_cast<event_callback_fn>(&SignalEventImpl::signalCallback),
      this);
  if (!event_) {
    throw std::runtime_error("Failed to create signal event");
  }

  event_add(event_, nullptr);
}

LibeventDispatcher::SignalEventImpl::~SignalEventImpl() {
  if (event_) {
    event_del(event_);
    event_free(event_);
  }
}

void LibeventDispatcher::SignalEventImpl::signalCallback(
    libevent_socket_t /*fd*/, short /*events*/, void* arg) {
  auto* signal_event = static_cast<SignalEventImpl*>(arg);

  // Update approximate time before callback
  signal_event->dispatcher_.updateApproximateMonotonicTime();

  signal_event->cb_();

  // Touch watchdog after callback
  signal_event->dispatcher_.touchWatchdog();
}

// LibeventDispatcherFactory implementation
const std::string LibeventDispatcherFactory::backend_name_ = "libevent";

DispatcherPtr LibeventDispatcherFactory::createDispatcher(
    const std::string& name) {
  return std::make_unique<LibeventDispatcher>(name);
}

const std::string& LibeventDispatcherFactory::backendName() const {
  return backend_name_;
}

// Factory function implementations
DispatcherFactoryPtr createLibeventDispatcherFactory() {
  return std::make_unique<LibeventDispatcherFactory>();
}

DispatcherFactoryPtr createPlatformDefaultDispatcherFactory() {
  // For now, always use libevent
  // TODO: Add native epoll/kqueue/iocp implementations
  return createLibeventDispatcherFactory();
}

}  // namespace event
}  // namespace mcp
