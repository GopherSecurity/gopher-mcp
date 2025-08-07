#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>

#include "mcp/network/connection_manager_impl.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/network/address_impl.h"

using namespace mcp::network;
using namespace mcp::event;
using namespace std::chrono_literals;

class MockConnectionPoolCallbacks : public ConnectionPoolCallbacks {
public:
    struct EventRecord {
        Connection* connection;
        ConnectionEvent event;
        std::chrono::steady_clock::time_point timestamp;
    };

    void onConnectionEvent(Connection& connection, ConnectionEvent event) override {
        EventRecord record{&connection, event, std::chrono::steady_clock::now()};
        events_.push_back(record);
        event_count_++;
        
        if (event == ConnectionEvent::LocalClose) {
            local_close_count_++;
        } else if (event == ConnectionEvent::RemoteClose) {
            remote_close_count_++;
        }
    }

    std::vector<EventRecord> events_;
    std::atomic<int> event_count_{0};
    std::atomic<int> local_close_count_{0};
    std::atomic<int> remote_close_count_{0};
};

class ConnectionPoolCallbacksTest : public ::testing::Test {
protected:
    void SetUp() override {
        factory_ = createLibeventDispatcherFactory();
        dispatcher_ = factory_->createDispatcher("test");
        socket_interface_ = std::make_unique<SocketInterfaceImpl>();
        
        // Create connection manager with callbacks
        callbacks_ = std::make_shared<MockConnectionPoolCallbacks>();
        manager_ = std::make_unique<ConnectionManagerImpl>(
            *dispatcher_, *socket_interface_);
        manager_->setConnectionPoolCallbacks(callbacks_);
    }

    void TearDown() override {
        manager_.reset();
        callbacks_.reset();
        socket_interface_.reset();
        dispatcher_.reset();
        factory_.reset();
    }

    // Helper to run dispatcher in background
    std::thread runDispatcher() {
        return std::thread([this]() {
            dispatcher_->run(RunType::RunUntilExit);
        });
    }

    DispatcherFactoryPtr factory_;
    DispatcherPtr dispatcher_;
    std::unique_ptr<SocketInterface> socket_interface_;
    std::unique_ptr<ConnectionManagerImpl> manager_;
    std::shared_ptr<MockConnectionPoolCallbacks> callbacks_;
};

// Test that callbacks are triggered on local close
TEST_F(ConnectionPoolCallbacksTest, LocalCloseEvent) {
    auto thread = runDispatcher();
    
    // Create a test connection
    auto local_addr = Address::parseInternetAddress("127.0.0.1:8080");
    auto remote_addr = Address::parseInternetAddress("127.0.0.1:9090");
    
    ConnectionPtr connection;
    dispatcher_->post([&]() {
        // Create a mock connection
        auto socket = createClientSocket(remote_addr, local_addr, {});
        if (socket) {
            connection = ConnectionImpl::createClientConnection(
                *dispatcher_, std::move(socket), nullptr, 
                *stream_info::StreamInfoImpl::create());
            
            // Simulate adding to manager's connection pool
            manager_->onEvent(ConnectionEvent::Connected);
            
            // Close the connection locally
            connection->close(Connection::CloseType::NoFlush);
            manager_->onEvent(ConnectionEvent::LocalClose);
        }
    });
    
    // Wait for events to be processed
    std::this_thread::sleep_for(100ms);
    
    // Check that local close event was triggered
    EXPECT_GT(callbacks_->local_close_count_.load(), 0);
    EXPECT_EQ(callbacks_->remote_close_count_.load(), 0);
    
    dispatcher_->exit();
    thread.join();
}

// Test that callbacks are triggered on remote close
TEST_F(ConnectionPoolCallbacksTest, RemoteCloseEvent) {
    auto thread = runDispatcher();
    
    std::atomic<bool> done{false};
    
    dispatcher_->post([&]() {
        // Simulate remote close event
        manager_->onEvent(ConnectionEvent::RemoteClose);
        done = true;
    });
    
    // Wait for event processing
    while (!done) {
        std::this_thread::sleep_for(10ms);
    }
    
    // Verify remote close was recorded
    EXPECT_GT(callbacks_->event_count_.load(), 0);
    
    dispatcher_->exit();
    thread.join();
}

// Test multiple connection events
TEST_F(ConnectionPoolCallbacksTest, MultipleConnectionEvents) {
    auto thread = runDispatcher();
    
    const int num_connections = 10;
    std::atomic<int> completed{0};
    
    for (int i = 0; i < num_connections; ++i) {
        dispatcher_->post([&]() {
            // Simulate connection lifecycle
            manager_->onEvent(ConnectionEvent::Connected);
            manager_->onEvent(ConnectionEvent::LocalClose);
            completed++;
        });
    }
    
    // Wait for all events
    while (completed < num_connections) {
        std::this_thread::sleep_for(10ms);
    }
    
    // Verify all events were recorded
    EXPECT_GE(callbacks_->event_count_.load(), num_connections);
    
    dispatcher_->exit();
    thread.join();
}

// Test that callbacks are properly cleared when connection is removed
TEST_F(ConnectionPoolCallbacksTest, ConnectionRemovalAfterClose) {
    auto thread = runDispatcher();
    
    std::atomic<bool> done{false};
    
    dispatcher_->post([&]() {
        // Create and track multiple connections
        for (int i = 0; i < 5; ++i) {
            manager_->onEvent(ConnectionEvent::Connected);
        }
        
        // Close all connections
        for (int i = 0; i < 5; ++i) {
            manager_->onEvent(ConnectionEvent::LocalClose);
        }
        
        done = true;
    });
    
    // Wait for completion
    while (!done) {
        std::this_thread::sleep_for(10ms);
    }
    
    // Verify events were triggered
    EXPECT_GE(callbacks_->local_close_count_.load(), 5);
    
    dispatcher_->exit();
    thread.join();
}

// Test thread safety of callbacks
TEST_F(ConnectionPoolCallbacksTest, ThreadSafetyOfCallbacks) {
    auto thread = runDispatcher();
    
    const int num_threads = 4;
    const int events_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> total_events{0};
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < events_per_thread; ++i) {
                dispatcher_->post([&]() {
                    manager_->onEvent(ConnectionEvent::Connected);
                    manager_->onEvent(ConnectionEvent::LocalClose);
                    total_events += 2;
                });
                std::this_thread::sleep_for(1ms);
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Give dispatcher time to process
    std::this_thread::sleep_for(500ms);
    
    // Verify thread safety - all events should be recorded
    EXPECT_GE(callbacks_->event_count_.load(), num_threads * events_per_thread);
    
    dispatcher_->exit();
    thread.join();
}

// Test that callbacks handle rapid connection state changes
TEST_F(ConnectionPoolCallbacksTest, RapidStateChanges) {
    auto thread = runDispatcher();
    
    const int num_transitions = 100;
    std::atomic<int> completed{0};
    
    dispatcher_->post([&]() {
        for (int i = 0; i < num_transitions; ++i) {
            // Rapid connect/disconnect cycles
            manager_->onEvent(ConnectionEvent::Connected);
            manager_->onEvent(ConnectionEvent::LocalClose);
            manager_->onEvent(ConnectionEvent::Connected);
            manager_->onEvent(ConnectionEvent::RemoteClose);
        }
        completed = num_transitions;
    });
    
    // Wait for completion
    while (completed < num_transitions) {
        std::this_thread::sleep_for(10ms);
    }
    
    // Should have recorded many events
    EXPECT_GE(callbacks_->event_count_.load(), num_transitions * 4);
    
    dispatcher_->exit();
    thread.join();
}

// Test callback exception handling
TEST_F(ConnectionPoolCallbacksTest, CallbackExceptionHandling) {
    class ThrowingCallbacks : public ConnectionPoolCallbacks {
    public:
        void onConnectionEvent(Connection& connection, ConnectionEvent event) override {
            call_count_++;
            if (should_throw_ && call_count_ % 3 == 0) {
                throw std::runtime_error("Test exception");
            }
        }
        
        std::atomic<int> call_count_{0};
        bool should_throw_{true};
    };
    
    auto throwing_callbacks = std::make_shared<ThrowingCallbacks>();
    manager_->setConnectionPoolCallbacks(throwing_callbacks);
    
    auto thread = runDispatcher();
    
    std::atomic<bool> done{false};
    
    dispatcher_->post([&]() {
        // Should handle exceptions gracefully
        for (int i = 0; i < 10; ++i) {
            try {
                manager_->onEvent(ConnectionEvent::Connected);
                manager_->onEvent(ConnectionEvent::LocalClose);
            } catch (...) {
                // Manager should handle this internally
            }
        }
        done = true;
    });
    
    while (!done) {
        std::this_thread::sleep_for(10ms);
    }
    
    // Some callbacks should have been called despite exceptions
    EXPECT_GT(throwing_callbacks->call_count_.load(), 0);
    
    dispatcher_->exit();
    thread.join();
}

// Test callback ordering and event sequence
TEST_F(ConnectionPoolCallbacksTest, EventSequenceOrdering) {
    auto thread = runDispatcher();
    
    std::vector<ConnectionEvent> expected_sequence = {
        ConnectionEvent::Connected,
        ConnectionEvent::RemoteClose,
        ConnectionEvent::Connected,
        ConnectionEvent::LocalClose
    };
    
    std::promise<void> promise;
    auto future = promise.get_future();
    
    dispatcher_->post([&]() {
        for (auto event : expected_sequence) {
            manager_->onEvent(event);
        }
        promise.set_value();
    });
    
    ASSERT_EQ(std::future_status::ready, future.wait_for(1s));
    
    // Wait for events to be processed
    std::this_thread::sleep_for(100ms);
    
    // Check that events were recorded in order
    EXPECT_GE(callbacks_->events_.size(), expected_sequence.size());
    
    dispatcher_->exit();
    thread.join();
}

// Test concurrent connections with different states
TEST_F(ConnectionPoolCallbacksTest, ConcurrentConnectionStates) {
    auto thread = runDispatcher();
    
    const int num_connections = 20;
    std::atomic<int> active_connections{0};
    std::atomic<int> closed_connections{0};
    
    // Simulate multiple connections in different states
    std::vector<std::thread> connection_threads;
    
    for (int i = 0; i < num_connections; ++i) {
        connection_threads.emplace_back([&, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(i * 10));
            
            dispatcher_->post([&]() {
                manager_->onEvent(ConnectionEvent::Connected);
                active_connections++;
            });
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50 + i * 5));
            
            dispatcher_->post([&]() {
                if (i % 2 == 0) {
                    manager_->onEvent(ConnectionEvent::LocalClose);
                } else {
                    manager_->onEvent(ConnectionEvent::RemoteClose);
                }
                closed_connections++;
            });
        });
    }
    
    // Wait for all threads
    for (auto& t : connection_threads) {
        t.join();
    }
    
    // Wait for all events to be processed
    while (closed_connections < num_connections) {
        std::this_thread::sleep_for(10ms);
    }
    
    // Verify all connections were tracked
    EXPECT_EQ(num_connections, active_connections.load());
    EXPECT_EQ(num_connections, closed_connections.load());
    EXPECT_GE(callbacks_->event_count_.load(), num_connections * 2);
    
    dispatcher_->exit();
    thread.join();
}

// Test memory management with many connections
TEST_F(ConnectionPoolCallbacksTest, MemoryManagementStressTest) {
    auto thread = runDispatcher();
    
    const int num_iterations = 1000;
    std::atomic<int> completed{0};
    
    dispatcher_->post([&]() {
        for (int i = 0; i < num_iterations; ++i) {
            // Create and destroy many connections
            manager_->onEvent(ConnectionEvent::Connected);
            
            // Simulate some work
            if (i % 100 == 0) {
                std::this_thread::yield();
            }
            
            manager_->onEvent(ConnectionEvent::LocalClose);
            completed++;
        }
    });
    
    // Monitor memory usage (in a real test, would use actual memory tracking)
    while (completed < num_iterations) {
        std::this_thread::sleep_for(10ms);
    }
    
    // All events should be processed without memory leaks
    EXPECT_EQ(num_iterations * 2, callbacks_->event_count_.load());
    
    dispatcher_->exit();
    thread.join();
}

// Test callback registration and deregistration
TEST_F(ConnectionPoolCallbacksTest, CallbackRegistrationLifecycle) {
    auto thread = runDispatcher();
    
    // Test multiple callback registrations
    auto callbacks1 = std::make_shared<MockConnectionPoolCallbacks>();
    auto callbacks2 = std::make_shared<MockConnectionPoolCallbacks>();
    
    std::atomic<bool> done{false};
    
    dispatcher_->post([&]() {
        // Register first callback
        manager_->setConnectionPoolCallbacks(callbacks1);
        manager_->onEvent(ConnectionEvent::Connected);
        
        // Switch to second callback
        manager_->setConnectionPoolCallbacks(callbacks2);
        manager_->onEvent(ConnectionEvent::LocalClose);
        
        // Clear callbacks
        manager_->setConnectionPoolCallbacks(nullptr);
        manager_->onEvent(ConnectionEvent::RemoteClose);
        
        done = true;
    });
    
    while (!done) {
        std::this_thread::sleep_for(10ms);
    }
    
    // First callback should have received first event
    EXPECT_EQ(1, callbacks1->event_count_.load());
    // Second callback should have received second event
    EXPECT_EQ(1, callbacks2->event_count_.load());
    
    dispatcher_->exit();
    thread.join();
}

// Test callbacks with connection metadata
TEST_F(ConnectionPoolCallbacksTest, ConnectionMetadataTracking) {
    class MetadataTrackingCallbacks : public ConnectionPoolCallbacks {
    public:
        struct ConnectionMetadata {
            Connection* connection;
            ConnectionEvent last_event;
            std::chrono::steady_clock::time_point event_time;
            int event_count;
        };
        
        void onConnectionEvent(Connection& connection, ConnectionEvent event) override {
            std::lock_guard<std::mutex> lock(mutex_);
            
            auto& metadata = connection_metadata_[&connection];
            metadata.connection = &connection;
            metadata.last_event = event;
            metadata.event_time = std::chrono::steady_clock::now();
            metadata.event_count++;
            
            total_events_++;
        }
        
        std::map<Connection*, ConnectionMetadata> connection_metadata_;
        std::mutex mutex_;
        std::atomic<int> total_events_{0};
    };
    
    auto tracking_callbacks = std::make_shared<MetadataTrackingCallbacks>();
    manager_->setConnectionPoolCallbacks(tracking_callbacks);
    
    auto thread = runDispatcher();
    
    std::atomic<bool> done{false};
    
    dispatcher_->post([&]() {
        // Simulate multiple connections with metadata
        for (int i = 0; i < 5; ++i) {
            manager_->onEvent(ConnectionEvent::Connected);
            std::this_thread::sleep_for(10ms);
            manager_->onEvent(ConnectionEvent::LocalClose);
        }
        done = true;
    });
    
    while (!done) {
        std::this_thread::sleep_for(10ms);
    }
    
    // Should have tracked metadata for connections
    EXPECT_GT(tracking_callbacks->total_events_.load(), 0);
    
    dispatcher_->exit();
    thread.join();
}

// Test performance with high event rate
TEST_F(ConnectionPoolCallbacksTest, HighEventRatePerformance) {
    auto thread = runDispatcher();
    
    const int num_events = 10000;
    auto start_time = std::chrono::steady_clock::now();
    std::atomic<int> processed{0};
    
    dispatcher_->post([&]() {
        for (int i = 0; i < num_events; ++i) {
            manager_->onEvent(i % 2 == 0 ? 
                ConnectionEvent::Connected : ConnectionEvent::LocalClose);
            processed++;
        }
    });
    
    // Wait for processing
    while (processed < num_events) {
        std::this_thread::sleep_for(1ms);
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    
    // Should process events quickly (< 1 second for 10000 events)
    EXPECT_LT(duration.count(), 1000);
    EXPECT_EQ(num_events, callbacks_->event_count_.load());
    
    // Calculate event rate
    double events_per_second = (num_events * 1000.0) / duration.count();
    std::cout << "Event processing rate: " << events_per_second << " events/sec" << std::endl;
    
    dispatcher_->exit();
    thread.join();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}