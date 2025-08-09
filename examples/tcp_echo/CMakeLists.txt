# TCP Echo Examples

# TCP Echo Server (Basic)
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

# TCP Echo Client (Basic)  
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