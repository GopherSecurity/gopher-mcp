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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}