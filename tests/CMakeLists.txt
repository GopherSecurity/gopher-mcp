# Test executables
# Core tests
add_executable(test_variant core/test_variant.cc)
add_executable(test_variant_extensive core/test_variant_extensive.cc)
add_executable(test_variant_advanced core/test_variant_advanced.cc)
add_executable(test_optional core/test_optional.cc)
add_executable(test_optional_extensive core/test_optional_extensive.cc)
add_executable(test_optional_advanced core/test_optional_advanced.cc)
add_executable(test_mcp_type_helpers core/test_mcp_type_helpers.cc)
add_executable(test_mcp_types core/test_mcp_types.cc)
add_executable(test_compat core/test_compat.cc)
add_executable(test_buffer core/test_buffer.cc)
# JSON serialization tests
add_executable(test_mcp_serialization json/test_mcp_serialization.cc)
add_executable(test_mcp_serialization_extensive json/test_mcp_serialization_extensive.cc)
add_executable(test_template_serialization json/test_template_serialization.cc)
add_executable(test_short_json_api json/test_short_json_api.cc)
# Event tests
add_executable(test_event_loop event/test_event_loop.cc)

# Network tests
add_executable(test_io_socket_handle network/test_io_socket_handle.cc)
add_executable(test_address network/test_address.cc)
add_executable(test_socket network/test_socket.cc)
add_executable(test_socket_interface network/test_socket_interface.cc)
add_executable(test_socket_option network/test_socket_option.cc)
add_executable(test_transport_socket network/test_transport_socket.cc)
add_executable(test_filter network/test_filter.cc)
add_executable(test_connection_impl network/test_connection_impl.cc)
add_executable(test_listener_impl network/test_listener_impl.cc)
add_executable(test_tcp_listener network/test_tcp_listener.cc)
add_executable(test_connection_manager_impl network/test_connection_manager_impl.cc)

# Stream info tests
add_executable(test_stream_info_impl stream_info/test_stream_info_impl.cc)

# Transport tests
add_executable(test_stdio_transport_socket transport/test_stdio_transport_socket.cc)
add_executable(test_http_transport_socket transport/test_http_transport_socket.cc)
add_executable(test_http_sse_transport_socket transport/test_http_sse_transport_socket.cc)
# Additional HTTP SSE transport test files have been merged into test_http_sse_transport_socket.cc
# to reduce duplication and improve maintainability
add_executable(test_stdio_pipe_bridge transport/test_stdio_pipe_bridge.cc)

# SSL/TLS transport tests
add_executable(test_ssl_context transport/test_ssl_context.cc)
add_executable(test_ssl_transport_socket transport/test_ssl_transport_socket.cc)
add_executable(test_ssl_state_machine transport/test_ssl_state_machine.cc)
add_executable(test_https_sse_factory transport/test_https_sse_factory.cc)
add_executable(test_ssl_integration transport/test_ssl_integration.cc)

# HTTP SSE state machine test
add_executable(test_http_sse_state_machine transport/test_http_sse_state_machine.cc)

# Transport socket state machine test
add_executable(test_transport_socket_state_machine transport/test_transport_socket_state_machine.cc)

# HTTP parser tests
add_executable(test_http_parser http/test_http_parser.cc)
add_executable(test_llhttp_parser http/test_llhttp_parser.cc)
add_executable(test_nghttp2_parser http/test_nghttp2_parser.cc)
add_executable(test_sse_parser http/test_sse_parser.cc)

# Transport layer tests (low-level transport/pipe/socket testing)
add_executable(test_stdio_echo_server transport/test_stdio_echo_server.cc)
add_executable(test_stdio_echo_client transport/test_stdio_echo_client.cc)

# Advanced example tests
add_executable(test_stdio_echo_client_advanced test_stdio_echo_client_advanced.cc)
add_executable(test_stdio_echo_server_advanced test_stdio_echo_server_advanced.cc)

# TCP echo tests
add_executable(test_tcp_echo_client_basic test_tcp_echo_client_basic.cc)
add_executable(test_tcp_echo_server_basic test_tcp_echo_server_basic.cc)

# MCP tests
add_executable(test_mcp_connection_manager network/test_mcp_connection_manager.cc)
add_executable(test_builders core/test_builders.cc)

# Network tests for recent fixes
add_executable(test_connection_pool_callbacks network/test_connection_pool_callbacks.cc)
add_executable(test_connection_utility network/test_connection_utility.cc)
add_executable(test_listener_filter_chain network/test_listener_filter_chain.cc)
add_executable(test_listener_filter_chain_simple network/test_listener_filter_chain_simple.cc)
add_executable(test_thread_safety_extended network/test_thread_safety_extended.cc)
add_executable(test_connection_state_machine network/test_connection_state_machine.cc)

# Real IO integration tests (using real sockets instead of mocks)
add_executable(test_event_loop_real_io event/test_event_loop_real_io.cc)
add_executable(test_connection_manager_real_io network/test_connection_manager_real_io.cc)

# Link libraries
target_link_libraries(test_variant 
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_variant_extensive
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_variant_advanced
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_optional
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_optional_extensive
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_optional_advanced
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_mcp_type_helpers
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_mcp_types
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_compat
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_buffer
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_mcp_serialization
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_mcp_serialization_extensive
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_template_serialization
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_event_loop
    mcp-event
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_short_json_api
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
)

# Network test libraries
target_link_libraries(test_io_socket_handle
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_address
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_socket
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_socket_interface
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_socket_option
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_transport_socket
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_filter
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_impl
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_listener_impl
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_tcp_listener
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_manager_impl
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# Stream info test libraries
target_link_libraries(test_stream_info_impl
    mcp-sdk
    gtest
    gtest_main
    Threads::Threads
)

# Transport test libraries
target_link_libraries(test_stdio_transport_socket
    mcp-sdk
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_http_transport_socket
    mcp-sdk
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_http_sse_transport_socket
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# Link libraries for the merged HTTP SSE transport socket tests are included
# in the main test_http_sse_transport_socket target above

# SSL/TLS transport test libraries
target_link_libraries(test_ssl_context
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_ssl_transport_socket
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_ssl_state_machine
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_https_sse_factory
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_ssl_integration
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

# HTTP SSE state machine test libraries
target_link_libraries(test_http_sse_state_machine
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# Transport socket state machine test libraries
target_link_libraries(test_transport_socket_state_machine
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# HTTP parser test libraries
target_link_libraries(test_http_parser
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_llhttp_parser
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_nghttp2_parser
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_sse_parser
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_stdio_echo_server
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_stdio_echo_client
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_stdio_pipe_bridge
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Advanced example test libraries
target_link_libraries(test_stdio_echo_client_advanced
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_stdio_echo_server_advanced
    mcp-sdk
    mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# TCP echo test libraries
target_link_libraries(test_tcp_echo_client_basic
    mcp-sdk
    mcp-event
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_tcp_echo_server_basic
    mcp-sdk
    mcp-event
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# MCP test libraries
target_link_libraries(test_mcp_connection_manager
    mcp-sdk
    mcp-event
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_builders
    mcp-sdk
    gtest
    gtest_main
    Threads::Threads
)

# Link libraries for new tests
target_link_libraries(test_connection_pool_callbacks
    mcp-sdk
    mcp-event
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_utility
    mcp-sdk
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_listener_filter_chain
    mcp-sdk
    mcp-event
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_listener_filter_chain_simple
    mcp-sdk
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_thread_safety_extended
    mcp-event
    mcp-sdk
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_state_machine
    mcp-event
    mcp-sdk
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# Link libraries for real IO tests
target_link_libraries(test_event_loop_real_io
    mcp-event
    mcp-sdk
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_manager_real_io
    mcp-sdk
    mcp-event
    gtest
    gtest_main
    Threads::Threads
)

# Add tests
add_test(NAME VariantTest COMMAND test_variant)
add_test(NAME VariantExtensiveTest COMMAND test_variant_extensive)
add_test(NAME VariantAdvancedTest COMMAND test_variant_advanced)
add_test(NAME OptionalTest COMMAND test_optional)
add_test(NAME OptionalExtensiveTest COMMAND test_optional_extensive)
add_test(NAME OptionalAdvancedTest COMMAND test_optional_advanced)
add_test(NAME MCPTypeHelpersTest COMMAND test_mcp_type_helpers)
add_test(NAME MCPTypesTest COMMAND test_mcp_types)
add_test(NAME CompatTest COMMAND test_compat)
add_test(NAME BufferTest COMMAND test_buffer)
add_test(NAME McpSerializationTest COMMAND test_mcp_serialization)
add_test(NAME McpSerializationExtensiveTest COMMAND test_mcp_serialization_extensive)
add_test(NAME TemplateSerializationTest COMMAND test_template_serialization)
add_test(NAME EventLoopTest COMMAND test_event_loop)

# Network tests
add_test(NAME IoSocketHandleTest COMMAND test_io_socket_handle)
add_test(NAME AddressTest COMMAND test_address)
add_test(NAME SocketTest COMMAND test_socket)
add_test(NAME SocketInterfaceTest COMMAND test_socket_interface)
add_test(NAME SocketOptionTest COMMAND test_socket_option)
add_test(NAME TransportSocketTest COMMAND test_transport_socket)
add_test(NAME FilterTest COMMAND test_filter)
add_test(NAME ConnectionImplTest COMMAND test_connection_impl)
add_test(NAME ListenerImplTest COMMAND test_listener_impl)
add_test(NAME TcpListenerTest COMMAND test_tcp_listener)
add_test(NAME ConnectionManagerImplTest COMMAND test_connection_manager_impl)

# Stream info tests
add_test(NAME StreamInfoImplTest COMMAND test_stream_info_impl)

# Transport tests
add_test(NAME StdioTransportSocketTest COMMAND test_stdio_transport_socket)
add_test(NAME HttpTransportSocketTest COMMAND test_http_transport_socket)
add_test(NAME HttpSseTransportSocketTest COMMAND test_http_sse_transport_socket)
# HttpSseTransportSocketRealIoTest merged into HttpSseTransportSocketTest to reduce duplication
add_test(NAME StdioPipeBridgeTest COMMAND test_stdio_pipe_bridge)

# SSL/TLS transport tests
add_test(NAME SslContextTest COMMAND test_ssl_context)
add_test(NAME SslTransportSocketTest COMMAND test_ssl_transport_socket)
add_test(NAME SslStateMachineTest COMMAND test_ssl_state_machine)
add_test(NAME HttpsSseFactoryTest COMMAND test_https_sse_factory)
add_test(NAME SslIntegrationTest COMMAND test_ssl_integration)

# HTTP SSE state machine test
add_test(NAME HttpSseStateMachineTest COMMAND test_http_sse_state_machine)

# Transport socket state machine test
add_test(NAME TransportSocketStateMachineTest COMMAND test_transport_socket_state_machine)

# HTTP parser tests
add_test(NAME HttpParserTest COMMAND test_http_parser)
add_test(NAME LLHttpParserTest COMMAND test_llhttp_parser)
add_test(NAME Nghttp2ParserTest COMMAND test_nghttp2_parser)
add_test(NAME SseParserTest COMMAND test_sse_parser)
add_test(NAME StdioEchoServerTest COMMAND test_stdio_echo_server)
add_test(NAME StdioEchoClientTest COMMAND test_stdio_echo_client)

# Advanced example tests
add_test(NAME StdioEchoClientAdvancedTest COMMAND test_stdio_echo_client_advanced)
add_test(NAME StdioEchoServerAdvancedTest COMMAND test_stdio_echo_server_advanced)

# TCP echo tests
add_test(NAME TcpEchoClientBasicTest COMMAND test_tcp_echo_client_basic)
add_test(NAME TcpEchoServerBasicTest COMMAND test_tcp_echo_server_basic)


# MCP tests
add_test(NAME McpConnectionManagerTest COMMAND test_mcp_connection_manager)
add_test(NAME BuildersTest COMMAND test_builders)

# New tests for recent fixes
add_test(NAME ConnectionPoolCallbacksTest COMMAND test_connection_pool_callbacks)
add_test(NAME ConnectionUtilityTest COMMAND test_connection_utility)
add_test(NAME ListenerFilterChainTest COMMAND test_listener_filter_chain)
add_test(NAME ListenerFilterChainSimpleTest COMMAND test_listener_filter_chain_simple)
add_test(NAME ThreadSafetyExtendedTest COMMAND test_thread_safety_extended)
add_test(NAME ConnectionStateMachineTest COMMAND test_connection_state_machine)

# Real IO integration tests
add_test(NAME EventLoopRealIoTest COMMAND test_event_loop_real_io)
add_test(NAME ConnectionManagerRealIoTest COMMAND test_connection_manager_real_io)

# Set test properties
set_tests_properties(VariantTest PROPERTIES
    TIMEOUT 60
)

# Custom target to run tests with output
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
    DEPENDS test_variant test_variant_extensive test_variant_advanced 
            test_optional test_optional_extensive test_optional_advanced
            test_mcp_type_helpers test_mcp_types test_compat
            test_buffer test_template_serialization test_event_loop
            test_io_socket_handle test_address test_socket
            test_socket_interface test_socket_option test_transport_socket
            test_filter test_connection_impl test_listener_impl
            test_connection_manager_impl test_stream_info_impl
            test_stdio_transport_socket test_http_transport_socket test_http_sse_transport_socket
            test_mcp_connection_manager test_builders
            test_event_loop_real_io test_connection_manager_real_io
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests..."
)

