# TCP Echo Examples

# TCP Echo Server (Basic Implementation)
add_executable(tcp_echo_server_basic
    tcp_echo_server_basic.cc
)

target_link_libraries(tcp_echo_server_basic
    PRIVATE
    gopher-mcp
    gopher-mcp-event
    Threads::Threads
)

target_include_directories(tcp_echo_server_basic
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# TCP Echo Client (Basic Implementation)
add_executable(tcp_echo_client_basic
    tcp_echo_client_basic.cc
)

target_link_libraries(tcp_echo_client_basic
    PRIVATE
    gopher-mcp
    gopher-mcp-event
    Threads::Threads
)

target_include_directories(tcp_echo_client_basic
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)