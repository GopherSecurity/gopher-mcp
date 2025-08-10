# TCP Echo Examples

# TCP Echo Server (Basic - Raw Sockets)
add_executable(tcp_echo_server_basic 
    tcp_echo_server_basic.cc
)

target_link_libraries(tcp_echo_server_basic
    PRIVATE
    mcp-sdk
    Threads::Threads
)

target_include_directories(tcp_echo_server_basic
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# TCP Echo Client (Basic - Raw Sockets)  
add_executable(tcp_echo_client_basic
    tcp_echo_client_basic.cc
)

target_link_libraries(tcp_echo_client_basic
    PRIVATE
    mcp-sdk
    Threads::Threads
)

target_include_directories(tcp_echo_client_basic
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# TCP Echo Server (Network Abstractions)
add_executable(tcp_echo_server_network
    tcp_echo_server_network.cc
)

target_link_libraries(tcp_echo_server_network
    PRIVATE
    mcp-sdk
    mcp-event
    Threads::Threads
)

target_include_directories(tcp_echo_server_network
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# TCP Echo Client (Network Abstractions)
add_executable(tcp_echo_client_network
    tcp_echo_client_network.cc
)

target_link_libraries(tcp_echo_client_network
    PRIVATE
    mcp-sdk
    mcp-event
    Threads::Threads
)

target_include_directories(tcp_echo_client_network
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)