#pragma once

#include "mcp/logging/logger_registry.h"

// Zero-configuration logging - works without any setup
#define LOG(level, ...)                                                \
  do {                                                                 \
    auto logger =                                                      \
        ::mcp::logging::LoggerRegistry::instance().getDefaultLogger(); \
    if (logger->shouldLog(::mcp::logging::LogLevel::level)) {          \
      ::mcp::logging::LogContext ctx;                                  \
      ctx.setLocation(__FILE__, __LINE__, __FUNCTION__);               \
      logger->logWithContext(::mcp::logging::LogLevel::level, ctx,     \
                             __VA_ARGS__);                             \
    }                                                                  \
  } while (0)

// Quick logging macros
#define LOG_DEBUG(...) LOG(Debug, __VA_ARGS__)
#define LOG_INFO(...) LOG(Info, __VA_ARGS__)
#define LOG_NOTICE(...) LOG(Notice, __VA_ARGS__)
#define LOG_WARNING(...) LOG(Warning, __VA_ARGS__)
#define LOG_ERROR(...) LOG(Error, __VA_ARGS__)
#define LOG_CRITICAL(...) LOG(Critical, __VA_ARGS__)
#define LOG_ALERT(...) LOG(Alert, __VA_ARGS__)
#define LOG_EMERGENCY(...) LOG(Emergency, __VA_ARGS__)

// Efficient logging macros with compile-time optimization
#ifdef GOPHER_LOG_DISABLE
#define GOPHER_LOG(level, ...) ((void)0)
#else
#define GOPHER_LOG(level, ...)                                        \
  do {                                                                \
    if (::mcp::logging::LoggerRegistry::instance().shouldLog(         \
            GOPHER_LOG_COMPONENT, ::mcp::logging::LogLevel::level)) { \
      ::mcp::logging::LoggerRegistry::instance()                      \
          .getOrCreateLogger(GOPHER_LOG_COMPONENT)                    \
          ->log(::mcp::logging::LogLevel::level, __FILE__, __LINE__,  \
                __FUNCTION__, __VA_ARGS__);                           \
    }                                                                 \
  } while (0)
#endif

// Component must be defined before using GOPHER_LOG
#ifndef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "default"
#endif

// Source location helper
#define GOPHER_LOG_LOCATION __FILE__, __LINE__, __FUNCTION__

// Context-aware logging
#define GOPHER_LOG_WITH_CONTEXT(level, context, ...)                   \
  do {                                                                 \
    auto logger =                                                      \
        ::mcp::logging::LoggerRegistry::instance().getOrCreateLogger(  \
            GOPHER_LOG_COMPONENT);                                     \
    if (logger->shouldLog(::mcp::logging::LogLevel::level)) {          \
      logger->logWithContext(::mcp::logging::LogLevel::level, context, \
                             __VA_ARGS__);                             \
    }                                                                  \
  } while (0)

// Component-specific logging
#define GOPHER_LOG_COMPONENT_DEBUG(component, ...) \
  GOPHER_LOG_COMPONENT_LOG(component, Debug, __VA_ARGS__)

#define GOPHER_LOG_COMPONENT_LOG(component, level, ...)       \
  do {                                                        \
    ::mcp::logging::ComponentLogger logger(                   \
        ::mcp::logging::Component::component, #component);    \
    logger.log(::mcp::logging::LogLevel::level, __VA_ARGS__); \
  } while (0)

// Dispatcher logging
#define DISPATCHER_LOG(level, ...)                  \
  ::mcp::logging::DispatcherLogger::instance().log( \
      ::mcp::logging::LogLevel::level, __VA_ARGS__)

// Layer logging
#define LAYER_LOG(layer, level, ...)                                  \
  ::mcp::logging::LayerLogger(layer, ::mcp::logging::Component::Root) \
      .log(::mcp::logging::LogLevel::level, __VA_ARGS__)

// Component logging
#define COMPONENT_LOG(component, level, ...)                            \
  ::mcp::logging::ComponentLogger(::mcp::logging::Component::component, \
                                  #component)                           \
      .log(::mcp::logging::LogLevel::level, __VA_ARGS__)