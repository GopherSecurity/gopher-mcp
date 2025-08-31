#include <gtest/gtest.h>

#include "mcp/logging/log_level.h"

using namespace mcp::logging;

TEST(LogLevelTest, LogLevelToString) {
  EXPECT_STREQ(logLevelToString(LogLevel::Debug), "DEBUG");
  EXPECT_STREQ(logLevelToString(LogLevel::Info), "INFO");
  EXPECT_STREQ(logLevelToString(LogLevel::Notice), "NOTICE");
  EXPECT_STREQ(logLevelToString(LogLevel::Warning), "WARNING");
  EXPECT_STREQ(logLevelToString(LogLevel::Error), "ERROR");
  EXPECT_STREQ(logLevelToString(LogLevel::Critical), "CRITICAL");
  EXPECT_STREQ(logLevelToString(LogLevel::Alert), "ALERT");
  EXPECT_STREQ(logLevelToString(LogLevel::Emergency), "EMERGENCY");
  EXPECT_STREQ(logLevelToString(LogLevel::Off), "OFF");
}

TEST(LogLevelTest, StringToLogLevel) {
  // Uppercase
  EXPECT_EQ(stringToLogLevel("DEBUG"), LogLevel::Debug);
  EXPECT_EQ(stringToLogLevel("INFO"), LogLevel::Info);
  EXPECT_EQ(stringToLogLevel("NOTICE"), LogLevel::Notice);
  EXPECT_EQ(stringToLogLevel("WARNING"), LogLevel::Warning);
  EXPECT_EQ(stringToLogLevel("ERROR"), LogLevel::Error);
  EXPECT_EQ(stringToLogLevel("CRITICAL"), LogLevel::Critical);
  EXPECT_EQ(stringToLogLevel("ALERT"), LogLevel::Alert);
  EXPECT_EQ(stringToLogLevel("EMERGENCY"), LogLevel::Emergency);
  EXPECT_EQ(stringToLogLevel("OFF"), LogLevel::Off);

  // Lowercase
  EXPECT_EQ(stringToLogLevel("debug"), LogLevel::Debug);
  EXPECT_EQ(stringToLogLevel("info"), LogLevel::Info);
  EXPECT_EQ(stringToLogLevel("notice"), LogLevel::Notice);
  EXPECT_EQ(stringToLogLevel("warning"), LogLevel::Warning);
  EXPECT_EQ(stringToLogLevel("error"), LogLevel::Error);
  EXPECT_EQ(stringToLogLevel("critical"), LogLevel::Critical);
  EXPECT_EQ(stringToLogLevel("alert"), LogLevel::Alert);
  EXPECT_EQ(stringToLogLevel("emergency"), LogLevel::Emergency);
  EXPECT_EQ(stringToLogLevel("off"), LogLevel::Off);

  // Invalid defaults to Info
  EXPECT_EQ(stringToLogLevel("invalid"), LogLevel::Info);
  EXPECT_EQ(stringToLogLevel(""), LogLevel::Info);
}

TEST(LogLevelTest, ComponentToString) {
  EXPECT_STREQ(componentToString(Component::Root), "Root");
  EXPECT_STREQ(componentToString(Component::Server), "Server");
  EXPECT_STREQ(componentToString(Component::Client), "Client");
  EXPECT_STREQ(componentToString(Component::Network), "Network");
  EXPECT_STREQ(componentToString(Component::Filter), "Filter");
  EXPECT_STREQ(componentToString(Component::Transport), "Transport");
  EXPECT_STREQ(componentToString(Component::Protocol), "Protocol");
  EXPECT_STREQ(componentToString(Component::Event), "Event");
  EXPECT_STREQ(componentToString(Component::Http), "Http");
  EXPECT_STREQ(componentToString(Component::Json), "Json");
}

TEST(LogLevelTest, LogLevelOrdering) {
  // Verify the numeric ordering matches severity
  EXPECT_LT(static_cast<int>(LogLevel::Debug),
            static_cast<int>(LogLevel::Info));
  EXPECT_LT(static_cast<int>(LogLevel::Info),
            static_cast<int>(LogLevel::Notice));
  EXPECT_LT(static_cast<int>(LogLevel::Notice),
            static_cast<int>(LogLevel::Warning));
  EXPECT_LT(static_cast<int>(LogLevel::Warning),
            static_cast<int>(LogLevel::Error));
  EXPECT_LT(static_cast<int>(LogLevel::Error),
            static_cast<int>(LogLevel::Critical));
  EXPECT_LT(static_cast<int>(LogLevel::Critical),
            static_cast<int>(LogLevel::Alert));
  EXPECT_LT(static_cast<int>(LogLevel::Alert),
            static_cast<int>(LogLevel::Emergency));
  EXPECT_LT(static_cast<int>(LogLevel::Emergency),
            static_cast<int>(LogLevel::Off));
}

TEST(LogLevelTest, LogModeValues) {
  // Just verify the enum values exist
  LogMode sync = LogMode::Sync;
  LogMode async = LogMode::Async;
  LogMode noop = LogMode::NoOp;

  EXPECT_NE(static_cast<int>(sync), static_cast<int>(async));
  EXPECT_NE(static_cast<int>(sync), static_cast<int>(noop));
  EXPECT_NE(static_cast<int>(async), static_cast<int>(noop));
}

TEST(LogLevelTest, SinkTypeValues) {
  // Verify sink types are distinct
  EXPECT_NE(static_cast<int>(SinkType::File),
            static_cast<int>(SinkType::Stdio));
  EXPECT_NE(static_cast<int>(SinkType::File), static_cast<int>(SinkType::Null));
  EXPECT_NE(static_cast<int>(SinkType::File),
            static_cast<int>(SinkType::External));
  EXPECT_NE(static_cast<int>(SinkType::File), static_cast<int>(SinkType::MCP));
}