# Advanced MCP Echo Examples
cmake_minimum_required(VERSION 3.10)

# Add executables
add_executable(stdio_echo_server_advanced 
    stdio_echo_server_advanced.cc)

add_executable(stdio_echo_client_advanced 
    stdio_echo_client_advanced.cc)

add_executable(stdio_echo_server_basic 
    stdio_echo_server_basic.cc)

add_executable(stdio_echo_client_basic 
    stdio_echo_client_basic.cc)

# Link libraries for server (using new advanced echo library with transport abstraction)
target_link_libraries(stdio_echo_server_advanced
    gopher-mcp-echo-advanced
    gopher-mcp
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Link libraries for client (using new advanced echo library with transport abstraction)
target_link_libraries(stdio_echo_client_advanced
    gopher-mcp-echo-advanced
    gopher-mcp
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Link libraries for basic server (using existing basic echo functionality)
target_link_libraries(stdio_echo_server_basic
    gopher-mcp-event
    gopher-mcp
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Link libraries for basic client (using existing basic echo functionality)
target_link_libraries(stdio_echo_client_basic
    gopher-mcp-event
    gopher-mcp
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# C++ standard is inherited from parent CMakeLists.txt

# Add include directories
target_include_directories(stdio_echo_server_advanced PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/examples/stdio_echo
)

target_include_directories(stdio_echo_client_advanced PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/examples/stdio_echo
)

target_include_directories(stdio_echo_server_basic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/examples/stdio_echo
)

target_include_directories(stdio_echo_client_basic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/examples/stdio_echo
)

# Installation rules
install(TARGETS stdio_echo_server_advanced stdio_echo_client_advanced stdio_echo_server_basic stdio_echo_client_basic
    RUNTIME DESTINATION bin
)

# Add test script
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_advanced_echo.sh
    ${CMAKE_CURRENT_BINARY_DIR}/test_advanced_echo.sh
    COPYONLY
)