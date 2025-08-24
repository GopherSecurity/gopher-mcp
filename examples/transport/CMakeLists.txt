# Transport examples

# TCP+SSL+HTTP stack example
add_executable(tcp_ssl_http_example tcp_ssl_http_example.cc)

target_link_libraries(tcp_ssl_http_example
    gopher-mcp
    gopher-mcp-event
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Set properties
set_target_properties(tcp_ssl_http_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples/transport
)