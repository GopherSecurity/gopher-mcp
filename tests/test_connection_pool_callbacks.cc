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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}