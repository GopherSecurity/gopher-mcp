#include "mcp/event/libevent_dispatcher.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <unistd.h>

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

// Initialize libevent for thread safety
struct LibeventInitializer {
  LibeventInitializer() {
#ifdef _WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif
    // Enable debug mode in debug builds
#ifndef NDEBUG
    event_enable_debug_mode();
#endif
  }
};

static LibeventInitializer libevent_initializer;

}  // namespace

// LibeventDispatcher implementation
LibeventDispatcher::LibeventDispatcher(const std::string& name) : name_(name) {
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
    close(wakeup_fd_[0]);
  }
  if (wakeup_fd_[1] >= 0) {
    close(wakeup_fd_[1]);
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

  // Create pipe for waking up the event loop
  if (pipe(wakeup_fd_) != 0) {
    throw std::runtime_error("Failed to create wakeup pipe");
  }

  // Make pipe non-blocking
  evutil_make_socket_nonblocking(wakeup_fd_[0]);
  evutil_make_socket_nonblocking(wakeup_fd_[1]);

  // Create wakeup event
  wakeup_event_ = event_new(base_, wakeup_fd_[0], EV_READ | EV_PERSIST,
                            &LibeventDispatcher::postWakeupCallback, this);
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

  if (need_wakeup && !isThreadSafe()) {
    // Wake up the event loop
    char byte = 1;
    ssize_t rc = write(wakeup_fd_[1], &byte, 1);
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
  assert(isThreadSafe());

  watchdog_registration_ = std::make_unique<WatchdogRegistration>();
  watchdog_registration_->watchdog = watchdog;
  watchdog_registration_->interval = min_touch_interval;

  // Create timer to touch watchdog periodically
  watchdog_registration_->timer = std::make_unique<TimerImpl>(*this, [this]() {
    touchWatchdog();
    watchdog_registration_->timer->enableTimer(
        watchdog_registration_->interval);
  });

  // Start the timer
  watchdog_registration_->timer->enableTimer(min_touch_interval);

  // Touch immediately
  touchWatchdog();
}

FileEventPtr LibeventDispatcher::createFileEvent(int fd,
                                                 FileReadyCb cb,
                                                 FileTriggerType trigger,
                                                 uint32_t events) {
  assert(isThreadSafe());
  return std::make_unique<FileEventImpl>(*this, fd, std::move(cb), trigger,
                                         events);
}

TimerPtr LibeventDispatcher::createTimer(TimerCb cb) {
  assert(isThreadSafe());
  return std::make_unique<TimerImpl>(*this, std::move(cb));
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
  assert(isThreadSafe());
  return std::make_unique<SchedulableCallbackImpl>(*this, std::move(cb));
}

void LibeventDispatcher::deferredDelete(DeferredDeletablePtr&& to_delete) {
  assert(isThreadSafe());
  deferred_delete_list_.push_back(std::move(to_delete));

  // Schedule deferred delete callback if not already scheduled
  if (!deferred_delete_cb_->enabled()) {
    deferred_delete_cb_->scheduleCallbackCurrentIteration();
  }
}

void LibeventDispatcher::exit() {
  exit_requested_ = true;

  if (!isThreadSafe()) {
    // Wake up the event loop
    post([]() {});  // Empty callback just to wake up
  } else {
    event_base_loopbreak(base_);
  }
}

SignalEventPtr LibeventDispatcher::listenForSignal(int signal_num,
                                                   SignalCb cb) {
  assert(isThreadSafe());
  return std::make_unique<SignalEventImpl>(*this, signal_num, std::move(cb));
}

void LibeventDispatcher::run(RunType type) {
  thread_id_ = std::this_thread::get_id();

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
      return;
  }

  updateApproximateMonotonicTime();
  event_base_loop(base_, flags);
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
  if (isThreadSafe()) {
    // Clear all pending work
    clearDeferredDeleteList();

    // Clear post callbacks
    {
      std::lock_guard<std::mutex> lock(post_mutex_);
      std::queue<PostCb> empty;
      post_callbacks_.swap(empty);
    }

    // Stop watchdog
    watchdog_registration_.reset();
  }
}

void LibeventDispatcher::postWakeupCallback(int fd,
                                            short /*events*/,
                                            void* arg) {
  auto* dispatcher = static_cast<LibeventDispatcher*>(arg);

  // Drain the pipe
  char buffer[256];
  while (read(fd, buffer, sizeof(buffer)) > 0) {
    // Continue draining
  }

  dispatcher->runPostCallbacks();
}

void LibeventDispatcher::runPostCallbacks() {
  std::queue<PostCb> callbacks;
  {
    std::lock_guard<std::mutex> lock(post_mutex_);
    callbacks.swap(post_callbacks_);
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
  if (watchdog_registration_ && watchdog_registration_->watchdog) {
    watchdog_registration_->watchdog->touch();
  }
}

// FileEventImpl implementation
LibeventDispatcher::FileEventImpl::FileEventImpl(LibeventDispatcher& dispatcher,
                                                 int fd,
                                                 FileReadyCb cb,
                                                 FileTriggerType trigger,
                                                 uint32_t events)
    : dispatcher_(dispatcher),
      fd_(fd),
      cb_(std::move(cb)),
      trigger_(trigger),
      enabled_events_(0) {
#ifdef _WIN32
  // Windows doesn't support edge triggers with libevent
  if (trigger == FileTriggerType::Edge) {
    trigger_ = FileTriggerType::EmulatedEdge;
  }
#endif

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
  // For edge-triggered, always update even if mask unchanged
  // This forces re-computation of readable/writable state
  if (trigger_ != FileTriggerType::Edge && enabled_events_ == events) {
    return;
  }

  enabled_events_ = events;
  updateEvents(events);
}

void LibeventDispatcher::FileEventImpl::updateEvents(uint32_t events) {
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
}

void LibeventDispatcher::FileEventImpl::assignEvents(uint32_t events) {
  short libevent_events = toLibeventEvents(events, trigger_);

  event_assign(event_, dispatcher_.base(), fd_, libevent_events,
               &FileEventImpl::eventCallback, this);
  event_add(event_, nullptr);
}

void LibeventDispatcher::FileEventImpl::eventCallback(int fd,
                                                      short events,
                                                      void* arg) {
  auto* file_event = static_cast<FileEventImpl*>(arg);

  // Update approximate time before callback
  file_event->dispatcher_.updateApproximateMonotonicTime();

  uint32_t ready_events = fromLibeventEvents(events);

  if (file_event->trigger_ == FileTriggerType::EmulatedEdge) {
    // For emulated edge, merge with any injected events
    file_event->mergeInjectedEventsAndRunCb(ready_events);

    // Re-enable the event for next trigger (emulating edge behavior)
    if (file_event->enabled_events_ != 0) {
      file_event->assignEvents(file_event->enabled_events_);
    }
  } else if (ready_events != 0) {
    file_event->cb_(ready_events);

#if defined(__APPLE__) || defined(__FreeBSD__)
    // On macOS/BSD with EV_CLEAR, we need to re-add the event after it fires
    // This is necessary for edge-triggered behavior with kqueue
    if (file_event->trigger_ == FileTriggerType::Edge &&
        file_event->enabled_events_ != 0) {
      file_event->assignEvents(file_event->enabled_events_);
    }
#endif
  }

  // Touch watchdog after callback
  file_event->dispatcher_.touchWatchdog();
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
LibeventDispatcher::TimerImpl::TimerImpl(LibeventDispatcher& dispatcher,
                                         TimerCb cb)
    : dispatcher_(dispatcher), cb_(std::move(cb)), enabled_(false) {
  event_ = evtimer_new(dispatcher_.base(), &TimerImpl::timerCallback, this);
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

void LibeventDispatcher::TimerImpl::timerCallback(int /*fd*/,
                                                  short /*events*/,
                                                  void* arg) {
  auto* timer = static_cast<TimerImpl*>(arg);

  timer->enabled_ = false;

  // Update approximate time before callback
  timer->dispatcher_.updateApproximateMonotonicTime();

  timer->cb_();

  // Touch watchdog after callback
  timer->dispatcher_.touchWatchdog();
}

// SchedulableCallbackImpl implementation
LibeventDispatcher::SchedulableCallbackImpl::SchedulableCallbackImpl(
    LibeventDispatcher& dispatcher, std::function<void()> cb)
    : dispatcher_(dispatcher), cb_(std::move(cb)), scheduled_(false) {
  // Use a timer with 0 delay for scheduling
  timer_ = std::make_unique<TimerImpl>(dispatcher_, [this]() {
    scheduled_ = false;
    cb_();
  });
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
  event_ = evsignal_new(dispatcher_.base(), signal_num_,
                        &SignalEventImpl::signalCallback, this);
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

void LibeventDispatcher::SignalEventImpl::signalCallback(int /*fd*/,
                                                         short /*events*/,
                                                         void* arg) {
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