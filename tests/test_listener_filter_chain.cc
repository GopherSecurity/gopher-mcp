#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "mcp/network/listener.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/socket_interface_impl.h"
#include "mcp/network/address_impl.h"
#include "mcp/event/libevent_dispatcher.h"

using namespace mcp::network;
using namespace mcp::event;
using namespace std::chrono_literals;

// Mock filter that tracks its execution
class MockListenerFilter : public ListenerFilter {
public:
    MockListenerFilter(const std::string& name, 
                      ListenerFilterStatus status = ListenerFilterStatus::Continue,
                      bool async = false)
        : name_(name), return_status_(status), async_(async) {}
    
    ListenerFilterStatus onAccept(ListenerFilterCallbacks& cb) override {
        call_count_++;
        last_callbacks_ = &cb;
        
        if (async_) {
            // Simulate async processing
            std::thread([this, &cb]() {
                std::this_thread::sleep_for(10ms);
                cb.continueFilterChain(true);
            }).detach();
            return ListenerFilterStatus::StopIteration;
        }
        
        return return_status_;
    }
    
    void onDestroy() override {
        destroyed_ = true;
    }
    
    std::string name_;
    ListenerFilterStatus return_status_;
    bool async_;
    std::atomic<int> call_count_{0};
    std::atomic<bool> destroyed_{false};
    ListenerFilterCallbacks* last_callbacks_{nullptr};
};

// Mock callbacks to track connection events
class MockListenerCallbacks : public ListenerCallbacks {
public:
    void onAccept(ConnectionSocketPtr&& socket) override {
        accept_count_++;
        last_accepted_socket_ = socket.get();
    }
    
    void onNewConnection(ConnectionPtr&& connection) override {
        new_connection_count_++;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connections_.push_back(std::move(connection));
        }
        cv_.notify_one();
    }
    
    void waitForConnections(int count, std::chrono::milliseconds timeout = 1000ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, timeout, [this, count]() {
            return connections_.size() >= static_cast<size_t>(count);
        });
    }
    
    std::atomic<int> accept_count_{0};
    std::atomic<int> new_connection_count_{0};
    ConnectionSocket* last_accepted_socket_{nullptr};
    std::vector<ConnectionPtr> connections_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

class ListenerFilterChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        factory_ = createLibeventDispatcherFactory();
        dispatcher_ = factory_->createDispatcher("test");
        socket_interface_ = std::make_unique<SocketInterfaceImpl>();
        callbacks_ = std::make_unique<MockListenerCallbacks>();
    }
    
    void TearDown() override {
        if (listener_) {
            listener_->disable();
        }
        listener_.reset();
        callbacks_.reset();
        socket_interface_.reset();
        dispatcher_.reset();
        factory_.reset();
    }
    
    // Create a listener with filters
    void createListener(std::vector<ListenerFilterPtr> filters) {
        ListenerConfig config;
        config.name = "test_listener";
        config.address = Address::parseInternetAddress("127.0.0.1:0");
        config.bind_to_port = true;
        config.listener_filters = std::move(filters);
        
        listener_ = std::make_unique<ActiveListener>(
            *dispatcher_, *socket_interface_, *callbacks_, std::move(config));
        
        auto result = listener_->listen();
        ASSERT_TRUE(mcp::holds_alternative<Success>(result));
    }
    
    // Helper to create a connection socket
    ConnectionSocketPtr createTestSocket() {
        auto local = Address::parseInternetAddress("127.0.0.1:8080");
        auto remote = Address::parseInternetAddress("127.0.0.1:9090");
        
        IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(5); // Dummy fd
        return std::make_unique<ConnectionSocketImpl>(
            std::move(io_handle), local, remote);
    }
    
    DispatcherFactoryPtr factory_;
    DispatcherPtr dispatcher_;
    std::unique_ptr<SocketInterface> socket_interface_;
    std::unique_ptr<MockListenerCallbacks> callbacks_;
    std::unique_ptr<ActiveListener> listener_;
};

// Test empty filter chain - connection should pass through
TEST_F(ListenerFilterChainTest, EmptyFilterChain) {
    createListener({});
    
    auto socket = createTestSocket();
    listener_->onAccept(std::move(socket));
    
    // Should immediately accept
    EXPECT_EQ(1, callbacks_->accept_count_.load());
}

// Test single filter that continues
TEST_F(ListenerFilterChainTest, SingleFilterContinue) {
    std::vector<ListenerFilterPtr> filters;
    auto filter = std::make_unique<MockListenerFilter>("filter1");
    auto* filter_ptr = filter.get();
    filters.push_back(std::move(filter));
    
    createListener(std::move(filters));
    
    auto socket = createTestSocket();
    listener_->onAccept(std::move(socket));
    
    // Filter should be called
    EXPECT_EQ(1, filter_ptr->call_count_.load());
    // Connection should be accepted
    EXPECT_EQ(1, callbacks_->accept_count_.load());
}

// Test multiple filters in chain
TEST_F(ListenerFilterChainTest, MultipleFiltersChain) {
    std::vector<ListenerFilterPtr> filters;
    std::vector<MockListenerFilter*> filter_ptrs;
    
    for (int i = 0; i < 3; ++i) {
        auto filter = std::make_unique<MockListenerFilter>("filter" + std::to_string(i));
        filter_ptrs.push_back(filter.get());
        filters.push_back(std::move(filter));
    }
    
    createListener(std::move(filters));
    
    auto socket = createTestSocket();
    listener_->onAccept(std::move(socket));
    
    // All filters should be called
    for (auto* filter : filter_ptrs) {
        EXPECT_EQ(1, filter->call_count_.load());
    }
    
    // Connection should be accepted
    EXPECT_EQ(1, callbacks_->accept_count_.load());
}

// Test filter that stops iteration (async)
TEST_F(ListenerFilterChainTest, FilterStopsIteration) {
    std::vector<ListenerFilterPtr> filters;
    
    // First filter is async
    auto filter1 = std::make_unique<MockListenerFilter>(
        "async_filter", ListenerFilterStatus::StopIteration, true);
    auto* filter1_ptr = filter1.get();
    filters.push_back(std::move(filter1));
    
    // Second filter should still be called after async completes
    auto filter2 = std::make_unique<MockListenerFilter>("filter2");
    auto* filter2_ptr = filter2.get();
    filters.push_back(std::move(filter2));
    
    createListener(std::move(filters));
    
    auto thread = std::thread([this]() {
        dispatcher_->run(RunType::RunUntilExit);
    });
    
    auto socket = createTestSocket();
    listener_->onAccept(std::move(socket));
    
    // Wait for async processing
    std::this_thread::sleep_for(50ms);
    
    // Both filters should be called
    EXPECT_EQ(1, filter1_ptr->call_count_.load());
    EXPECT_EQ(1, filter2_ptr->call_count_.load());
    
    dispatcher_->exit();
    thread.join();
}

// Test filter rejecting connection
TEST_F(ListenerFilterChainTest, FilterRejectsConnection) {
    std::vector<ListenerFilterPtr> filters;
    
    // Filter that will reject
    class RejectingFilter : public ListenerFilter {
    public:
        ListenerFilterStatus onAccept(ListenerFilterCallbacks& cb) override {
            cb.continueFilterChain(false); // Reject
            return ListenerFilterStatus::StopIteration;
        }
    };
    
    filters.push_back(std::make_unique<RejectingFilter>());
    
    // This filter should not be called
    auto filter2 = std::make_unique<MockListenerFilter>("filter2");
    auto* filter2_ptr = filter2.get();
    filters.push_back(std::move(filter2));
    
    createListener(std::move(filters));
    
    auto thread = std::thread([this]() {
        dispatcher_->run(RunType::RunUntilExit);
    });
    
    auto socket = createTestSocket();
    listener_->onAccept(std::move(socket));
    
    std::this_thread::sleep_for(50ms);
    
    // Second filter should not be called
    EXPECT_EQ(0, filter2_ptr->call_count_.load());
    // No connection should be created
    EXPECT_EQ(0, callbacks_->accept_count_.load());
    
    dispatcher_->exit();
    thread.join();
}

// Test multiple connections through filter chain
TEST_F(ListenerFilterChainTest, MultipleConcurrentConnections) {
    std::vector<ListenerFilterPtr> filters;
    
    auto filter = std::make_unique<MockListenerFilter>("concurrent_filter");
    auto* filter_ptr = filter.get();
    filters.push_back(std::move(filter));
    
    createListener(std::move(filters));
    
    const int num_connections = 10;
    
    for (int i = 0; i < num_connections; ++i) {
        auto socket = createTestSocket();
        listener_->onAccept(std::move(socket));
    }
    
    // All connections should be processed
    EXPECT_EQ(num_connections, filter_ptr->call_count_.load());
    EXPECT_EQ(num_connections, callbacks_->accept_count_.load());
}

// Test filter accessing socket information
TEST_F(ListenerFilterChainTest, FilterAccessesSocket) {
    std::vector<ListenerFilterPtr> filters;
    
    class SocketInspectingFilter : public ListenerFilter {
    public:
        ListenerFilterStatus onAccept(ListenerFilterCallbacks& cb) override {
            // Access socket info
            auto& socket = cb.socket();
            socket_accessed_ = true;
            
            // Access dispatcher
            auto& dispatcher = cb.dispatcher();
            dispatcher_accessed_ = (&dispatcher != nullptr);
            
            return ListenerFilterStatus::Continue;
        }
        
        std::atomic<bool> socket_accessed_{false};
        std::atomic<bool> dispatcher_accessed_{false};
    };
    
    auto filter = std::make_unique<SocketInspectingFilter>();
    auto* filter_ptr = filter.get();
    filters.push_back(std::move(filter));
    
    createListener(std::move(filters));
    
    auto socket = createTestSocket();
    listener_->onAccept(std::move(socket));
    
    // Filter should have accessed socket and dispatcher
    EXPECT_TRUE(filter_ptr->socket_accessed_.load());
    EXPECT_TRUE(filter_ptr->dispatcher_accessed_.load());
}

// Test filter chain ordering
TEST_F(ListenerFilterChainTest, FilterChainOrdering) {
    std::vector<ListenerFilterPtr> filters;
    std::atomic<int> call_order{0};
    std::vector<int> filter_orders(3);
    
    for (int i = 0; i < 3; ++i) {
        class OrderedFilter : public ListenerFilter {
        public:
            OrderedFilter(std::atomic<int>& order, int& my_order) 
                : order_(order), my_order_(my_order) {}
            
            ListenerFilterStatus onAccept(ListenerFilterCallbacks& cb) override {
                my_order_ = ++order_;
                return ListenerFilterStatus::Continue;
            }
            
        private:
            std::atomic<int>& order_;
            int& my_order_;
        };
        
        filters.push_back(std::make_unique<OrderedFilter>(call_order, filter_orders[i]));
    }
    
    createListener(std::move(filters));
    
    auto socket = createTestSocket();
    listener_->onAccept(std::move(socket));
    
    // Filters should be called in order
    EXPECT_EQ(1, filter_orders[0]);
    EXPECT_EQ(2, filter_orders[1]);
    EXPECT_EQ(3, filter_orders[2]);
}

// Test filter cleanup on listener destruction
TEST_F(ListenerFilterChainTest, FilterCleanupOnDestruction) {
    std::vector<ListenerFilterPtr> filters;
    
    auto filter = std::make_unique<MockListenerFilter>("cleanup_filter");
    auto* filter_ptr = filter.get();
    filters.push_back(std::move(filter));
    
    createListener(std::move(filters));
    
    // Process a connection
    auto socket = createTestSocket();
    listener_->onAccept(std::move(socket));
    
    EXPECT_EQ(1, filter_ptr->call_count_.load());
    
    // Destroy listener - filters should be cleaned up
    listener_.reset();
    
    // Filter should be destroyed
    EXPECT_TRUE(filter_ptr->destroyed_.load());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}