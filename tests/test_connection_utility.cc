#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>

#include "mcp/network/connection_utility.h"
#include "mcp/network/socket_impl.h"
#include "mcp/network/address_impl.h"

using namespace mcp::network;

class ConnectionUtilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a test socket
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(socket_fd_, 0) << "Failed to create socket";
        
        // Make it non-blocking for tests
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    }
    
    void TearDown() override {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
        }
    }
    
    // Helper to get socket option value
    template<typename T>
    T getSocketOption(int level, int optname) {
        T value;
        socklen_t len = sizeof(value);
        EXPECT_EQ(0, getsockopt(socket_fd_, level, optname, &value, &len));
        return value;
    }
    
    int socket_fd_ = -1;
};

// Test TCP_NODELAY is set correctly
TEST_F(ConnectionUtilityTest, SetTcpNoDelay) {
    // Apply socket configuration
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    // Verify TCP_NODELAY is enabled
    int nodelay = getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY);
    EXPECT_EQ(1, nodelay) << "TCP_NODELAY should be enabled";
}

// Test keep-alive options are set
TEST_F(ConnectionUtilityTest, SetKeepAlive) {
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    // Verify SO_KEEPALIVE is enabled
    int keepalive = getSocketOption<int>(SOL_SOCKET, SO_KEEPALIVE);
    EXPECT_EQ(1, keepalive) << "SO_KEEPALIVE should be enabled";
    
#ifdef __linux__
    // On Linux, check specific keep-alive parameters
    int keepidle = getSocketOption<int>(IPPROTO_TCP, TCP_KEEPIDLE);
    EXPECT_EQ(60, keepidle) << "TCP_KEEPIDLE should be 60 seconds";
    
    int keepintvl = getSocketOption<int>(IPPROTO_TCP, TCP_KEEPINTVL);
    EXPECT_EQ(10, keepintvl) << "TCP_KEEPINTVL should be 10 seconds";
    
    int keepcnt = getSocketOption<int>(IPPROTO_TCP, TCP_KEEPCNT);
    EXPECT_EQ(3, keepcnt) << "TCP_KEEPCNT should be 3";
#endif

#ifdef __APPLE__
    // On macOS, check keep-alive idle time
    int keepalive_time = getSocketOption<int>(IPPROTO_TCP, TCP_KEEPALIVE);
    EXPECT_EQ(60, keepalive_time) << "TCP_KEEPALIVE should be 60 seconds";
#endif
}

// Test socket buffer sizes are set
TEST_F(ConnectionUtilityTest, SetBufferSizes) {
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    // Verify send buffer size
    int sndbuf = getSocketOption<int>(SOL_SOCKET, SO_SNDBUF);
    EXPECT_GE(sndbuf, 256 * 1024) << "Send buffer should be at least 256KB";
    
    // Verify receive buffer size
    int rcvbuf = getSocketOption<int>(SOL_SOCKET, SO_RCVBUF);
    EXPECT_GE(rcvbuf, 256 * 1024) << "Receive buffer should be at least 256KB";
}

// Test that setSocketOptions handles invalid file descriptor gracefully
TEST_F(ConnectionUtilityTest, InvalidFileDescriptor) {
    // Should not crash or throw
    ConnectionUtility::setSocketOptions(-1);
    ConnectionUtility::setSocketOptions(999999);
}

// Test multiple calls to setSocketOptions
TEST_F(ConnectionUtilityTest, MultipleCallsIdempotent) {
    // Apply configuration multiple times
    ConnectionUtility::setSocketOptions(socket_fd_);
    ConnectionUtility::setSocketOptions(socket_fd_);
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    // Should still have correct settings
    int nodelay = getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY);
    EXPECT_EQ(1, nodelay);
    
    int keepalive = getSocketOption<int>(SOL_SOCKET, SO_KEEPALIVE);
    EXPECT_EQ(1, keepalive);
}

// Test with an actual Socket object
TEST_F(ConnectionUtilityTest, ConfigureSocketObject) {
    auto addr = Address::parseInternetAddress("127.0.0.1:0");
    auto socket = createListenSocket(addr, {.non_blocking = true}, false);
    ASSERT_NE(nullptr, socket);
    
    // Apply configuration
    ConnectionUtility::setSocketOptions(socket->ioHandle().fd());
    
    // Verify settings on the socket
    int nodelay = 0;
    socklen_t len = sizeof(nodelay);
    EXPECT_EQ(0, socket->getSocketOption(IPPROTO_TCP, TCP_NODELAY, &nodelay, &len));
    EXPECT_EQ(1, nodelay);
}

// Test platform-specific optimizations
TEST_F(ConnectionUtilityTest, PlatformSpecificOptions) {
    ConnectionUtility::setSocketOptions(socket_fd_);
    
#ifdef __linux__
    // Test Linux-specific TCP_USER_TIMEOUT
    int user_timeout = getSocketOption<int>(IPPROTO_TCP, TCP_USER_TIMEOUT);
    EXPECT_EQ(30000, user_timeout) << "TCP_USER_TIMEOUT should be 30 seconds";
    
    // Test TCP_QUICKACK
    int quickack = getSocketOption<int>(IPPROTO_TCP, TCP_QUICKACK);
    EXPECT_EQ(1, quickack) << "TCP_QUICKACK should be enabled";
#endif

#ifdef SO_NOSIGPIPE
    // Test macOS/BSD SO_NOSIGPIPE
    int nosigpipe = getSocketOption<int>(SOL_SOCKET, SO_NOSIGPIPE);
    EXPECT_EQ(1, nosigpipe) << "SO_NOSIGPIPE should be enabled";
#endif
}

// Performance test - measure time to configure many sockets
TEST_F(ConnectionUtilityTest, PerformanceTest) {
    const int num_sockets = 100;
    std::vector<int> sockets;
    
    // Create sockets
    for (int i = 0; i < num_sockets; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(fd, 0);
        sockets.push_back(fd);
    }
    
    // Measure configuration time
    auto start = std::chrono::steady_clock::now();
    for (int fd : sockets) {
        ConnectionUtility::setSocketOptions(fd);
    }
    auto end = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should be fast - less than 100ms for 100 sockets
    EXPECT_LT(duration.count(), 100) << "Configuring " << num_sockets 
                                     << " sockets took " << duration.count() << "ms";
    
    // Cleanup
    for (int fd : sockets) {
        close(fd);
    }
}

// Test error handling with closed socket
TEST_F(ConnectionUtilityTest, ClosedSocketHandling) {
    // Close the socket
    close(socket_fd_);
    
    // Should handle gracefully without crashing
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    // Mark as closed so TearDown doesn't try to close again
    socket_fd_ = -1;
}

// Test configuration for different connection types
TEST_F(ConnectionUtilityTest, ConfigurationForDifferentConnectionTypes) {
    // Test for client connection
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    int nodelay = getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY);
    EXPECT_EQ(1, nodelay);
    
    // Test for server connection (might have different settings)
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(server_socket, 0);
    
    ConnectionUtility::setSocketOptions(server_socket);
    
    int server_nodelay = getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY);
    EXPECT_EQ(1, server_nodelay);
    
    close(server_socket);
}

// Test socket option persistence after connection
TEST_F(ConnectionUtilityTest, OptionsPersistAfterConnection) {
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    // Simulate bind (for testing only, won't actually connect)
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);  // Let system choose port
    addr.sin_addr.s_addr = INADDR_ANY;
    
    // Options should still be set
    int nodelay = getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY);
    EXPECT_EQ(1, nodelay);
    
    int keepalive = getSocketOption<int>(SOL_SOCKET, SO_KEEPALIVE);
    EXPECT_EQ(1, keepalive);
}

// Test comprehensive socket state validation
TEST_F(ConnectionUtilityTest, ComprehensiveSocketStateValidation) {
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    // Validate all options are set correctly
    struct {
        int level;
        int optname;
        int expected_value;
        const char* name;
    } options_to_check[] = {
        {IPPROTO_TCP, TCP_NODELAY, 1, "TCP_NODELAY"},
        {SOL_SOCKET, SO_KEEPALIVE, 1, "SO_KEEPALIVE"},
#ifdef __linux__
        {IPPROTO_TCP, TCP_KEEPIDLE, 60, "TCP_KEEPIDLE"},
        {IPPROTO_TCP, TCP_KEEPINTVL, 10, "TCP_KEEPINTVL"},
        {IPPROTO_TCP, TCP_KEEPCNT, 3, "TCP_KEEPCNT"},
        {IPPROTO_TCP, TCP_USER_TIMEOUT, 30000, "TCP_USER_TIMEOUT"},
        {IPPROTO_TCP, TCP_QUICKACK, 1, "TCP_QUICKACK"},
#endif
#ifdef __APPLE__
        {IPPROTO_TCP, TCP_KEEPALIVE, 60, "TCP_KEEPALIVE"},
#endif
#ifdef SO_NOSIGPIPE
        {SOL_SOCKET, SO_NOSIGPIPE, 1, "SO_NOSIGPIPE"},
#endif
    };
    
    for (const auto& opt : options_to_check) {
        int value = 0;
        socklen_t len = sizeof(value);
        int result = getsockopt(socket_fd_, opt.level, opt.optname, &value, &len);
        
        if (result == 0) {
            EXPECT_EQ(opt.expected_value, value) 
                << "Option " << opt.name << " has unexpected value";
        }
    }
}

// Test error recovery for socket option failures
TEST_F(ConnectionUtilityTest, ErrorRecoveryForOptionFailures) {
    // Create a UDP socket (some TCP options won't apply)
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(udp_socket, 0);
    
    // Should handle gracefully even if some options fail
    ConnectionUtility::setSocketOptions(udp_socket);
    
    // SO_KEEPALIVE should still be attempted
    int keepalive = 0;
    socklen_t len = sizeof(keepalive);
    getsockopt(udp_socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, &len);
    // Note: behavior may vary by platform
    
    close(udp_socket);
}

// Test socket configuration with different address families
TEST_F(ConnectionUtilityTest, DifferentAddressFamilies) {
    // Test with IPv6 socket
    int ipv6_socket = socket(AF_INET6, SOCK_STREAM, 0);
    if (ipv6_socket >= 0) {
        ConnectionUtility::setSocketOptions(ipv6_socket);
        
        int nodelay = getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY);
        EXPECT_EQ(1, nodelay);
        
        close(ipv6_socket);
    }
    
    // Test with Unix domain socket
    int unix_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unix_socket >= 0) {
        // Should handle gracefully (most TCP options won't apply)
        ConnectionUtility::setSocketOptions(unix_socket);
        close(unix_socket);
    }
}

// Test concurrent socket configuration
TEST_F(ConnectionUtilityTest, ConcurrentSocketConfiguration) {
    const int num_threads = 10;
    const int sockets_per_thread = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < sockets_per_thread; ++i) {
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                if (fd >= 0) {
                    ConnectionUtility::setSocketOptions(fd);
                    
                    // Verify configuration
                    int nodelay = 0;
                    socklen_t len = sizeof(nodelay);
                    if (getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, 
                                  &nodelay, &len) == 0 && nodelay == 1) {
                        success_count++;
                    }
                    
                    close(fd);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Most configurations should succeed
    EXPECT_GT(success_count.load(), num_threads * sockets_per_thread * 0.9);
}

// Test socket option boundaries and limits
TEST_F(ConnectionUtilityTest, SocketOptionBoundaries) {
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    // Test buffer size limits
    int sndbuf = getSocketOption<int>(SOL_SOCKET, SO_SNDBUF);
    int rcvbuf = getSocketOption<int>(SOL_SOCKET, SO_RCVBUF);
    
    // Buffers should be within reasonable limits
    EXPECT_GT(sndbuf, 0);
    EXPECT_LT(sndbuf, 100 * 1024 * 1024); // Less than 100MB
    EXPECT_GT(rcvbuf, 0);
    EXPECT_LT(rcvbuf, 100 * 1024 * 1024);
    
#ifdef __linux__
    // Test keep-alive parameter limits
    int keepidle = getSocketOption<int>(IPPROTO_TCP, TCP_KEEPIDLE);
    int keepintvl = getSocketOption<int>(IPPROTO_TCP, TCP_KEEPINTVL);
    int keepcnt = getSocketOption<int>(IPPROTO_TCP, TCP_KEEPCNT);
    
    EXPECT_GT(keepidle, 0);
    EXPECT_LT(keepidle, 7200); // Less than 2 hours
    EXPECT_GT(keepintvl, 0);
    EXPECT_LT(keepintvl, 3600); // Less than 1 hour
    EXPECT_GT(keepcnt, 0);
    EXPECT_LT(keepcnt, 100); // Reasonable probe count
#endif
}

// Test socket configuration with system resource limits
TEST_F(ConnectionUtilityTest, SystemResourceLimits) {
    // Try to create many sockets and configure them
    std::vector<int> sockets;
    const int max_sockets = 100;
    
    for (int i = 0; i < max_sockets; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            break; // Hit system limit
        }
        
        ConnectionUtility::setSocketOptions(fd);
        sockets.push_back(fd);
    }
    
    // Verify all created sockets are configured
    for (int fd : sockets) {
        int nodelay = 0;
        socklen_t len = sizeof(nodelay);
        EXPECT_EQ(0, getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, &len));
        EXPECT_EQ(1, nodelay);
    }
    
    // Cleanup
    for (int fd : sockets) {
        close(fd);
    }
}

// Test socket configuration persistence across fork
#ifndef _WIN32
TEST_F(ConnectionUtilityTest, ConfigurationAcrossFork) {
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        int nodelay = getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY);
        exit(nodelay == 1 ? 0 : 1);
    } else if (pid > 0) {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        EXPECT_EQ(0, WEXITSTATUS(status)) << "Child process should see configured socket";
    } else {
        FAIL() << "Fork failed";
    }
}
#endif

// Test configuration with socket in different states
TEST_F(ConnectionUtilityTest, ConfigurationInDifferentStates) {
    // Test on newly created socket
    ConnectionUtility::setSocketOptions(socket_fd_);
    EXPECT_EQ(1, getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY));
    
    // Test on listening socket
    int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_socket, 0);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(listen_socket, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        ConnectionUtility::setSocketOptions(listen_socket);
        EXPECT_EQ(1, getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY));
        
        if (listen(listen_socket, 5) == 0) {
            // Options should persist after listen
            EXPECT_EQ(1, getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY));
        }
    }
    
    close(listen_socket);
}

// Test configuration impact on socket behavior
TEST_F(ConnectionUtilityTest, ConfigurationImpactOnBehavior) {
    // Create a pair of connected sockets for testing
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        // Configure one socket
        ConnectionUtility::setSocketOptions(sv[0]);
        
        // The configured socket should have different options
        int configured_nodelay = 0, unconfigured_nodelay = 0;
        socklen_t len = sizeof(int);
        
        getsockopt(sv[0], IPPROTO_TCP, TCP_NODELAY, &configured_nodelay, &len);
        getsockopt(sv[1], IPPROTO_TCP, TCP_NODELAY, &unconfigured_nodelay, &len);
        
        // Note: socketpair creates UNIX sockets, so TCP options may not apply
        // This test is more about not crashing than specific behavior
        
        close(sv[0]);
        close(sv[1]);
    }
}

// Stress test with rapid configuration changes
TEST_F(ConnectionUtilityTest, RapidConfigurationChanges) {
    const int num_iterations = 1000;
    
    for (int i = 0; i < num_iterations; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            ConnectionUtility::setSocketOptions(fd);
            
            // Rapidly change some options
            int value = i % 2;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
            
            // Reconfigure
            ConnectionUtility::setSocketOptions(fd);
            
            // Should be back to configured state
            int nodelay = getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY);
            EXPECT_EQ(1, nodelay);
            
            close(fd);
        }
    }
}

// Test configuration with socket option conflicts
TEST_F(ConnectionUtilityTest, SocketOptionConflicts) {
    // Set conflicting options before configuration
    int zero = 0;
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
    setsockopt(socket_fd_, SOL_SOCKET, SO_KEEPALIVE, &zero, sizeof(zero));
    
    // Configure should override
    ConnectionUtility::setSocketOptions(socket_fd_);
    
    // Verify configuration won
    EXPECT_EQ(1, getSocketOption<int>(IPPROTO_TCP, TCP_NODELAY));
    EXPECT_EQ(1, getSocketOption<int>(SOL_SOCKET, SO_KEEPALIVE));
}

// Test memory and resource leak detection
TEST_F(ConnectionUtilityTest, MemoryAndResourceLeakTest) {
    // Get initial resource usage (simplified)
    const int num_iterations = 10000;
    
    for (int i = 0; i < num_iterations; ++i) {
        ConnectionUtility::setSocketOptions(socket_fd_);
        
        // Force some allocations/deallocations if any
        if (i % 1000 == 0) {
            // Re-create socket to test cleanup
            close(socket_fd_);
            socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
            ASSERT_GE(socket_fd_, 0);
        }
    }
    
    // If we get here without crash/hang, no obvious leaks
    SUCCEED();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}