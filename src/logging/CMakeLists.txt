# Logging library CMake configuration
cmake_minimum_required(VERSION 3.14)

# Define the logging library
add_library(mcp_logging
  logger.cc
  logger_registry.cc
  log_sink.cc
  log_formatter.cc
)

# Include directories
target_include_directories(mcp_logging PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)

# Link required libraries
target_link_libraries(mcp_logging PUBLIC
  mcp_common
  fmt::fmt
  Threads::Threads
)

# Set compile features
target_compile_features(mcp_logging PUBLIC cxx_std_17)

# Add compile definitions
target_compile_definitions(mcp_logging PUBLIC
  $<$<CONFIG:Debug>:MCP_DEBUG_LOGGING>
)

# Install rules
install(TARGETS mcp_logging
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/mcp/logging
  DESTINATION include/mcp
  FILES_MATCHING PATTERN "*.h"
)