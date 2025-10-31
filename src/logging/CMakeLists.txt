# Logging library CMake configuration
cmake_minimum_required(VERSION 3.14)

find_package(fmt REQUIRED CONFIG)

# Collect logging sources
set(MCP_LOGGING_SOURCES
  logger.cc
  logger_registry.cc
  log_sink.cc
  log_formatter.cc
)

# Create static library
add_library(gopher-mcp-logging-static STATIC ${MCP_LOGGING_SOURCES})
set_target_properties(gopher-mcp-logging-static PROPERTIES
  OUTPUT_NAME gopher-mcp-logging
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(gopher-mcp-logging-static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# Link with fmt - it should handle its own include directories
target_link_libraries(gopher-mcp-logging-static PUBLIC
  fmt::fmt
  Threads::Threads
)

target_compile_features(gopher-mcp-logging-static PUBLIC cxx_std_17)

target_compile_definitions(gopher-mcp-logging-static PUBLIC
  $<$<CONFIG:Debug>:MCP_DEBUG_LOGGING>
)

# Create shared library
if(BUILD_SHARED_LIBS)
  add_library(gopher-mcp-logging SHARED ${MCP_LOGGING_SOURCES})
  
  target_include_directories(gopher-mcp-logging PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
  
  target_link_libraries(gopher-mcp-logging PUBLIC
    fmt::fmt
    Threads::Threads
  )
  
  target_compile_features(gopher-mcp-logging PUBLIC cxx_std_17)
  
  target_compile_definitions(gopher-mcp-logging PUBLIC
    $<$<CONFIG:Debug>:MCP_DEBUG_LOGGING>
  )
else()
  add_library(gopher-mcp-logging ALIAS gopher-mcp-logging-static)
endif()

# Install rules
if(GOPHER_MCP_INSTALL AND NOT GOPHER_MCP_IS_SUBMODULE)
  install(TARGETS gopher-mcp-logging-static
    EXPORT gopher-mcp-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
  
  if(BUILD_SHARED_LIBS)
    install(TARGETS gopher-mcp-logging
      EXPORT gopher-mcp-targets
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib
    )
  endif()
  
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/mcp/logging
    DESTINATION include/mcp
    FILES_MATCHING PATTERN "*.h"
  )
endif()
