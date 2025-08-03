# MCP C++ SDK Event Loop Design

## Overview

The MCP C++ SDK event loop system is designed following Envoy Proxy's proven architecture, optimized for high-performance network IO operations. This design provides:

- **High Performance**: Efficient handling of thousands of concurrent connections
- **Scalability**: Multi-threaded worker pool with independent event loops
- **Flexibility**: Backend-agnostic design supporting multiple event systems
- **Safety**: Thread-safe operations with deferred deletion
- **Monitoring**: Built-in performance metrics and watchdog support

## Architecture

### Core Components

1. **Dispatcher**: The heart of the event system
   - Single-threaded event loop per dispatcher
   - Handles file events, timers, and signals
   - Thread-safe posting from other threads
   - Deferred deletion for safe object lifecycle

2. **Worker Threads**: Scalable multi-core utilization
   - Each worker owns a dispatcher
   - Independent event processing
   - Load balancing across workers

3. **Event Backends**: Platform-optimized implementations
   - **Linux**: epoll (edge-triggered by default)
   - **macOS/BSD**: kqueue
   - **Windows**: IOCP
   - **Cross-platform**: libevent, ASIO

### Key Features

#### 1. File Events
```cpp
// Monitor socket for read/write events
auto file_event = dispatcher->createFileEvent(
    socket_fd,
    [](uint32_t events) {
        if (events & static_cast<uint32_t>(FileReadyType::Read)) {
            // Handle incoming data
        }
        if (events & static_cast<uint32_t>(FileReadyType::Write)) {
            // Handle write ready
        }
    },
    FileTriggerType::Edge,  // Edge-triggered for efficiency
    static_cast<uint32_t>(FileReadyType::Read | FileReadyType::Write)
);
```

#### 2. Timers
```cpp
// Standard timer
auto timer = dispatcher->createTimer([]() {
    // Timer callback
});
timer->enableTimer(std::chrono::milliseconds(100));

// Scaled timer that adapts to load
auto scaled_timer = dispatcher->createScaledTimer(
    ScaledTimerType::OverloadActionState,
    []() { /* callback */ }
);
```

#### 3. Thread-Safe Posting
```cpp
// Post work from any thread to the dispatcher
dispatcher->post([data = std::move(data)]() {
    // This runs in the dispatcher thread
    processData(data);
});
```

#### 4. Deferred Deletion
```cpp
// Safe object deletion
class Connection : public DeferredDeletable {
    // ...
};

// Delete in next event loop iteration
dispatcher->deferredDelete(std::move(connection));
```

#### 5. Schedulable Callbacks
```cpp
// Schedule callback for current or next iteration
auto callback = dispatcher->createSchedulableCallback([]() {
    // Callback logic
});

// Run in current iteration if called from dispatcher thread
callback->scheduleCallbackCurrentIteration();

// Always run in next iteration
callback->scheduleCallbackNextIteration();
```

## Thread Model

### Worker Pool Architecture
```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Worker 0      │     │   Worker 1      │     │   Worker N      │
│ ┌─────────────┐ │     │ ┌─────────────┐ │     │ ┌─────────────┐ │
│ │ Dispatcher  │ │     │ │ Dispatcher  │ │     │ │ Dispatcher  │ │
│ │             │ │     │ │             │ │     │ │             │ │
│ │ - epoll/    │ │     │ │ - epoll/    │ │     │ │ - epoll/    │ │
│ │   kqueue    │ │     │ │   kqueue    │ │     │ │   kqueue    │ │
│ │ - Timers    │ │     │ │ - Timers    │ │     │ │ - Timers    │ │
│ │ - Post Q    │ │     │ │ - Post Q    │ │     │ │ - Post Q    │ │
│ └─────────────┘ │     │ └─────────────┘ │     │ └─────────────┘ │
└─────────────────┘     └─────────────────┘     └─────────────────┘
         ↑                       ↑                       ↑
         └───────────────────────┴───────────────────────┘
                    Cross-thread posting
```

### Thread Safety
- Each dispatcher runs in a single thread
- `post()` is thread-safe for cross-thread communication
- No shared mutable state between workers
- Lock-free queues for posted callbacks

## Performance Optimizations

### 1. Edge-Triggered IO (Linux)
- Reduces syscall overhead
- Efficient for high-throughput scenarios
- Automatic fallback to level-triggered when needed

### 2. Approximate Monotonic Time
- Cached time value updated once per iteration
- Reduces `clock_gettime` syscalls
- Used for non-critical timing

### 3. Batched Operations
- Posted callbacks processed in batches
- Deferred deletions handled together
- Reduced context switching

### 4. Zero-Copy Buffer Management
- Integration with MCP buffer system
- Watermark-based flow control
- Efficient memory management

## Backend Abstraction

The event system supports multiple backends through a factory pattern:

```cpp
// Use platform default (recommended)
auto factory = createPlatformDefaultDispatcherFactory();

// Or choose specific backend
auto factory = createLibeventDispatcherFactory();
// auto factory = createAsioDispatcherFactory();

// Create dispatcher
auto dispatcher = factory->createDispatcher("worker_0");
```

### Backend Comparison

| Backend  | Platform      | Pros                      | Cons                    |
|----------|---------------|---------------------------|-------------------------|
| epoll    | Linux         | Highest performance       | Linux-only              |
| kqueue   | macOS/BSD     | Native performance        | BSD-only                |
| IOCP     | Windows       | Windows native            | Different semantics     |
| libevent | Cross-platform| Portable, mature          | Small overhead          |
| ASIO     | Cross-platform| C++ native, header-only   | Template heavy          |

## Usage Example

```cpp
#include "mcp/event/event_loop.h"

using namespace mcp::event;

// Create thread pool
auto thread_pool = createThreadPool();
auto dispatcher_factory = createPlatformDefaultDispatcherFactory();
auto worker_factory = createDefaultWorkerFactory();

// Initialize with 4 workers
thread_pool->initialize(4, *dispatcher_factory, *worker_factory);
thread_pool->start();

// Get a worker's dispatcher
auto& worker = thread_pool->nextWorker();
auto& dispatcher = worker.dispatcher();

// Create MCP server connection
auto server_fd = createServerSocket(8080);
auto accept_event = dispatcher.createFileEvent(
    server_fd,
    [&](uint32_t events) {
        if (events & static_cast<uint32_t>(FileReadyType::Read)) {
            auto client_fd = accept(server_fd, nullptr, nullptr);
            handleNewConnection(client_fd, dispatcher);
        }
    },
    FileTriggerType::Level,
    static_cast<uint32_t>(FileReadyType::Read)
);

// Run event loop
dispatcher.run(RunType::Block);
```

## Integration with MCP Protocol

The event system is designed specifically for MCP's needs:

1. **JSON-RPC Message Handling**: Efficient async processing
2. **Progress Notifications**: Timer-based progress updates
3. **Cancellation Support**: Schedulable callbacks for cleanup
4. **Multi-Connection Support**: Worker pool for scaling

## Monitoring and Debugging

### Performance Metrics
```cpp
DispatcherStats stats;
dispatcher.initializeStats(stats);
// Access stats.loop_duration_us, stats.poll_delay_us
```

### Watchdog Integration
```cpp
auto watchdog = std::make_shared<WatchDog>();
dispatcher.registerWatchdog(watchdog, std::chrono::seconds(5));
```

### Tracked Objects
```cpp
class TrackedOperation : public ScopeTrackedObject {
    void dumpState(std::ostream& os, int indent) const override {
        os << std::string(indent, ' ') << "Operation: " << name_ << "\n";
    }
};

dispatcher.pushTrackedObject(&tracked_op);
// ... operation ...
dispatcher.popTrackedObject(&tracked_op);
```

## Best Practices

1. **Use Edge-Triggered IO** on Linux for best performance
2. **Distribute connections** across workers evenly
3. **Batch operations** when possible using post()
4. **Use deferred deletion** for connection cleanup
5. **Monitor dispatcher health** with watchdogs
6. **Profile with stats** to identify bottlenecks

## Future Enhancements

1. **io_uring backend** for Linux 5.1+
2. **Work stealing** between workers
3. **CPU affinity** options
4. **Dynamic worker scaling**
5. **Built-in connection pooling**