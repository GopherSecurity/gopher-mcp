#include <fcntl.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "mcp/buffer.h"
#include "mcp/transport/stdio_transport_socket.h"

namespace mcp {
namespace transport {
namespace {

class StdioTransportSocketTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create pipe for testing
    int pipe_fds[2];
    ASSERT_EQ(0, pipe(pipe_fds));

    read_fd_ = pipe_fds[0];
    write_fd_ = pipe_fds[1];

    // Make non-blocking
    fcntl(read_fd_, F_SETFL, O_NONBLOCK);
    fcntl(write_fd_, F_SETFL, O_NONBLOCK);

    // Create config using pipe
    config_.stdin_fd = read_fd_;
    config_.stdout_fd = write_fd_;
    config_.non_blocking = true;
  }

  void TearDown() override {
    if (read_fd_ != -1)
      close(read_fd_);
    if (write_fd_ != -1)
      close(write_fd_);
  }

  void writeToStdin(const std::string& data) {
    ::write(write_fd_, data.c_str(), data.size());
  }

  std::string readFromStdout() {
    char buffer[1024];
    ssize_t n = ::read(read_fd_, buffer, sizeof(buffer));
    if (n > 0) {
      return std::string(buffer, n);
    }
    return "";
  }

  StdioTransportSocketConfig config_;
  int read_fd_ = -1;
  int write_fd_ = -1;
};

// StdioTransportSocketFactory tests

class StdioTransportSocketFactoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_.stdin_fd = 0;
    config_.stdout_fd = 1;
    config_.non_blocking = true;

    factory_ = std::make_unique<StdioTransportSocketFactory>(config_);
  }

  StdioTransportSocketConfig config_;
  std::unique_ptr<StdioTransportSocketFactory> factory_;
};

TEST_F(StdioTransportSocketFactoryTest, BasicProperties) {
  EXPECT_FALSE(factory_->implementsSecureTransport());
  EXPECT_EQ("stdio", factory_->name());
}

TEST_F(StdioTransportSocketFactoryTest, CreateTransportSocket) {
  // Create client transport socket
  auto socket = factory_->createTransportSocket(nullptr);
  ASSERT_NE(nullptr, socket);
  EXPECT_EQ("stdio", socket->protocol());

  // Create server transport socket
  socket = factory_->createTransportSocket();
  ASSERT_NE(nullptr, socket);
  EXPECT_EQ("stdio", socket->protocol());
}

TEST_F(StdioTransportSocketFactoryTest, HashKey) {
  std::vector<uint8_t> key;
  factory_->hashKey(key, nullptr);

  // Should contain factory name and config
  EXPECT_GT(key.size(), 0);

  // Verify factory name is in key
  std::string key_str(key.begin(), key.end());
  EXPECT_NE(std::string::npos, key_str.find("stdio"));
}

TEST_F(StdioTransportSocketFactoryTest, FactoryFunction) {
  // Test factory function
  auto factory = createStdioTransportSocketFactory();
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ("stdio", factory->name());

  // With custom config
  StdioTransportSocketConfig custom_config;
  custom_config.stdin_fd = 3;
  custom_config.stdout_fd = 4;

  factory = createStdioTransportSocketFactory(custom_config);
  ASSERT_NE(nullptr, factory);
}

// Integration test with real stdio
TEST(StdioTransportSocketIntegrationTest, DISABLED_RealStdio) {
  // This test is disabled by default as it would interfere with test output
  // Enable manually for debugging

  StdioTransportSocketConfig config;
  config.stdin_fd = STDIN_FILENO;
  config.stdout_fd = STDOUT_FILENO;
  config.non_blocking = false;

  StdioTransportSocket transport(config);
}

}  // namespace
}  // namespace transport
}  // namespace mcp