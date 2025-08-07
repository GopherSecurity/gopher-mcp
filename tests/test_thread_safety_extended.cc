#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include <set>
#include <map>

#include "mcp/event/libevent_dispatcher.h"

using namespace mcp::event;
using namespace std::chrono_literals;

class ExtendedThreadSafetyTest : public ::testing::Test {
protected:
    void SetUp() override {
        factory_ = createLibeventDispatcherFactory();
    }
    
    void TearDown() override {
        dispatchers_.clear();
        factory_.reset();
    }
    
    // Create multiple dispatchers
    void createDispatchers(int count) {
        for (int i = 0; i < count; ++i) {
            dispatchers_.push_back(
                factory_->createDispatcher("dispatcher_" + std::to_string(i)));
        }
    }
    
    DispatcherFactoryPtr factory_;
    std::vector<DispatcherPtr> dispatchers_;
};

// Test thread_id is only set when run() is called
TEST_F(ExtendedThreadSafetyTest, ThreadIdSetOnlyInRun) {
    auto dispatcher = factory_->createDispatcher("test");
    
    // Before run(), isThreadSafe() should be false from any thread
    EXPECT_FALSE(dispatcher->isThreadSafe());
    
    std::atomic<bool> thread_safe_in_run{false};
    std::atomic<bool> run_started{false};
    
    std::thread t([&]() {
        // Still false before run()
        EXPECT_FALSE(dispatcher->isThreadSafe());
        
        // Now run the dispatcher
        run_started = true;
        dispatcher->post([&]() {
            // Inside the dispatcher thread, should be true
            thread_safe_in_run = dispatcher->isThreadSafe();
            dispatcher->exit();
        });
        dispatcher->run(RunType::RunUntilExit);
    });
    
    // Wait for run to start
    while (!run_started) {
        std::this_thread::sleep_for(1ms);
    }
    
    // From main thread, should still be false
    EXPECT_FALSE(dispatcher->isThreadSafe());
    
    t.join();
    
    // Verify it was true inside the dispatcher thread
    EXPECT_TRUE(thread_safe_in_run);
}

// Test multiple dispatchers in different threads
TEST_F(ExtendedThreadSafetyTest, MultipleDispatchersThreadSafety) {
    const int num_dispatchers = 5;
    createDispatchers(num_dispatchers);
    
    std::vector<std::thread> threads;
    std::map<int, std::thread::id> dispatcher_thread_ids;
    std::mutex mutex;
    std::atomic<int> ready_count{0};
    
    for (int i = 0; i < num_dispatchers; ++i) {
        threads.emplace_back([this, i, &dispatcher_thread_ids, &mutex, &ready_count]() {
            auto& dispatcher = dispatchers_[i];
            
            // Each dispatcher should report false before its own run()
            EXPECT_FALSE(dispatcher->isThreadSafe());
            
            dispatcher->post([&dispatcher, i, &dispatcher_thread_ids, &mutex, &ready_count]() {
                // Inside each dispatcher's thread
                EXPECT_TRUE(dispatcher->isThreadSafe());
                
                // Record thread ID
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    dispatcher_thread_ids[i] = std::this_thread::get_id();
                }
                
                ready_count++;
            });
            
            dispatcher->run(RunType::Block);
            dispatcher->exit();
        });
    }
    
    // Wait for all dispatchers to record their thread IDs
    while (ready_count < num_dispatchers) {
        std::this_thread::sleep_for(10ms);
    }
    
    // All thread IDs should be unique
    std::set<std::thread::id> unique_ids;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& pair : dispatcher_thread_ids) {
            unique_ids.insert(pair.second);
        }
    }
    EXPECT_EQ(num_dispatchers, static_cast<int>(unique_ids.size()));
    
    // Exit all dispatchers
    for (auto& dispatcher : dispatchers_) {
        dispatcher->exit();
    }
    
    // Join all threads
    for (auto& t : threads) {
        t.join();
    }
}

// Test cross-thread posting with thread safety checks
TEST_F(ExtendedThreadSafetyTest, CrossThreadPostingWithSafetyChecks) {
    auto dispatcher = factory_->createDispatcher("test");
    
    std::atomic<int> callback_count{0};
    std::atomic<int> safe_count{0};
    std::atomic<int> unsafe_count{0};
    const int num_posts = 100;
    
    std::thread dispatcher_thread([&]() {
        dispatcher->run(RunType::RunUntilExit);
    });
    
    // Give dispatcher time to start
    std::this_thread::sleep_for(10ms);
    
    // Post from multiple threads
    std::vector<std::thread> posting_threads;
    for (int t = 0; t < 4; ++t) {
        posting_threads.emplace_back([&]() {
            for (int i = 0; i < num_posts / 4; ++i) {
                // Check thread safety before posting (should be false)
                if (!dispatcher->isThreadSafe()) {
                    unsafe_count++;
                }
                
                dispatcher->post([&]() {
                    // Inside callback, should be thread safe
                    if (dispatcher->isThreadSafe()) {
                        safe_count++;
                    }
                    callback_count++;
                });
                
                std::this_thread::sleep_for(1ms);
            }
        });
    }
    
    // Wait for all posts to complete
    for (auto& t : posting_threads) {
        t.join();
    }
    
    // Wait for callbacks to execute
    while (callback_count < num_posts) {
        std::this_thread::sleep_for(10ms);
    }
    
    // All callbacks should report thread safe
    EXPECT_EQ(num_posts, safe_count.load());
    // All external checks should report not thread safe
    EXPECT_EQ(num_posts, unsafe_count.load());
    
    dispatcher->exit();
    dispatcher_thread.join();
}

// Test thread safety with rapid start/stop cycles
TEST_F(ExtendedThreadSafetyTest, RapidStartStopCycles) {
    auto dispatcher = factory_->createDispatcher("test");
    const int num_cycles = 10;
    
    for (int cycle = 0; cycle < num_cycles; ++cycle) {
        std::atomic<bool> verified{false};
        
        std::thread t([&]() {
            dispatcher->post([&]() {
                // Should be thread safe inside dispatcher
                EXPECT_TRUE(dispatcher->isThreadSafe());
                verified = true;
            });
            dispatcher->run(RunType::NonBlock);
        });
        
        t.join();
        
        // Should not be thread safe from main thread
        EXPECT_FALSE(dispatcher->isThreadSafe());
        EXPECT_TRUE(verified);
    }
}

// Test concurrent isThreadSafe() calls
TEST_F(ExtendedThreadSafetyTest, ConcurrentThreadSafetyChecks) {
    auto dispatcher = factory_->createDispatcher("test");
    
    std::atomic<bool> stop{false};
    std::atomic<int> check_count{0};
    
    std::thread dispatcher_thread([&]() {
        dispatcher->run(RunType::RunUntilExit);
    });
    
    // Multiple threads checking thread safety concurrently
    std::vector<std::thread> checker_threads;
    for (int i = 0; i < 8; ++i) {
        checker_threads.emplace_back([&]() {
            while (!stop) {
                // These checks should all return false (not in dispatcher thread)
                EXPECT_FALSE(dispatcher->isThreadSafe());
                check_count++;
                std::this_thread::sleep_for(100us);
            }
        });
    }
    
    // Post callbacks that check from inside
    for (int i = 0; i < 100; ++i) {
        dispatcher->post([&]() {
            // These should all return true (in dispatcher thread)
            EXPECT_TRUE(dispatcher->isThreadSafe());
            check_count++;
        });
    }
    
    // Let it run for a bit
    std::this_thread::sleep_for(100ms);
    
    stop = true;
    for (auto& t : checker_threads) {
        t.join();
    }
    
    // Should have done many checks
    EXPECT_GT(check_count.load(), 100);
    
    dispatcher->exit();
    dispatcher_thread.join();
}

// Test thread safety with nested callbacks
TEST_F(ExtendedThreadSafetyTest, NestedCallbackThreadSafety) {
    auto dispatcher = factory_->createDispatcher("test");
    
    std::atomic<int> depth_count{0};
    const int max_depth = 5;
    
    std::thread t([&]() {
        dispatcher->run(RunType::RunUntilExit);
    });
    
    std::function<void(int)> nested_post = [&](int depth) {
        if (depth >= max_depth) {
            depth_count++;
            return;
        }
        
        dispatcher->post([&, depth]() {
            // Should always be thread safe in callbacks
            EXPECT_TRUE(dispatcher->isThreadSafe());
            
            // Post another callback
            nested_post(depth + 1);
        });
    };
    
    // Start the nested posting
    nested_post(0);
    
    // Wait for completion
    while (depth_count < 1) {
        std::this_thread::sleep_for(10ms);
    }
    
    dispatcher->exit();
    t.join();
}

// Test thread ID consistency across multiple operations
TEST_F(ExtendedThreadSafetyTest, ThreadIdConsistency) {
    auto dispatcher = factory_->createDispatcher("test");
    
    std::thread::id recorded_id;
    std::atomic<bool> id_recorded{false};
    std::vector<std::thread::id> callback_ids;
    std::mutex mutex;
    
    std::thread t([&]() {
        recorded_id = std::this_thread::get_id();
        id_recorded = true;
        dispatcher->run(RunType::RunUntilExit);
    });
    
    // Wait for thread to start
    while (!id_recorded) {
        std::this_thread::sleep_for(1ms);
    }
    
    // Post multiple callbacks and verify they all run in the same thread
    for (int i = 0; i < 20; ++i) {
        dispatcher->post([&]() {
            std::lock_guard<std::mutex> lock(mutex);
            callback_ids.push_back(std::this_thread::get_id());
            EXPECT_TRUE(dispatcher->isThreadSafe());
        });
    }
    
    // Give callbacks time to execute
    std::this_thread::sleep_for(100ms);
    
    // All callbacks should have run in the same thread
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& id : callback_ids) {
            EXPECT_EQ(recorded_id, id);
        }
    }
    
    dispatcher->exit();
    t.join();
}

// Stress test with many threads and operations
TEST_F(ExtendedThreadSafetyTest, StressTestThreadSafety) {
    const int num_dispatchers = 3;
    const int num_threads_per_dispatcher = 4;
    const int operations_per_thread = 50;
    
    createDispatchers(num_dispatchers);
    
    std::vector<std::thread> dispatcher_threads;
    std::atomic<int> total_operations{0};
    std::atomic<int> safe_operations{0};
    
    // Start dispatcher threads
    for (int i = 0; i < num_dispatchers; ++i) {
        dispatcher_threads.emplace_back([this, i]() {
            dispatchers_[i]->run(RunType::RunUntilExit);
        });
    }
    
    // Start worker threads for each dispatcher
    std::vector<std::thread> worker_threads;
    for (int d = 0; d < num_dispatchers; ++d) {
        for (int t = 0; t < num_threads_per_dispatcher; ++t) {
            worker_threads.emplace_back([this, d, &total_operations, &safe_operations]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(1, 10);
                
                for (int op = 0; op < operations_per_thread; ++op) {
                    // Random sleep
                    std::this_thread::sleep_for(std::chrono::microseconds(dis(gen)));
                    
                    // Should not be thread safe from worker thread
                    EXPECT_FALSE(dispatchers_[d]->isThreadSafe());
                    
                    dispatchers_[d]->post([this, d, &total_operations, &safe_operations]() {
                        // Should be thread safe in callback
                        if (dispatchers_[d]->isThreadSafe()) {
                            safe_operations++;
                        }
                        total_operations++;
                    });
                }
            });
        }
    }
    
    // Wait for all workers to complete
    for (auto& t : worker_threads) {
        t.join();
    }
    
    // Wait for all operations to complete
    const int expected_operations = num_dispatchers * num_threads_per_dispatcher * operations_per_thread;
    while (total_operations < expected_operations) {
        std::this_thread::sleep_for(10ms);
    }
    
    // All operations in callbacks should report thread safe
    EXPECT_EQ(expected_operations, safe_operations.load());
    
    // Stop all dispatchers
    for (auto& dispatcher : dispatchers_) {
        dispatcher->exit();
    }
    
    for (auto& t : dispatcher_threads) {
        t.join();
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}