# Test executables
# Core tests

# Include filter tests subdirectory
add_subdirectory(filter)
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
add_executable(test_http_sse_transport_socket transport/test_http_sse_transport_socket.cc)
add_executable(test_http_sse_transport_socket_simple transport/test_http_sse_transport_socket_simple.cc)
add_executable(test_stdio_pipe_bridge transport/test_stdio_pipe_bridge.cc)

# SSL/TLS transport tests
add_executable(test_ssl_context transport/test_ssl_context.cc)
add_executable(test_ssl_transport_socket transport/test_ssl_transport_socket.cc)
add_executable(test_ssl_state_machine transport/test_ssl_state_machine.cc)
add_executable(test_https_sse_factory transport/test_https_sse_factory.cc)
add_executable(test_ssl_integration transport/test_ssl_integration.cc)

# TCP transport tests
add_executable(test_tcp_transport_socket transport/test_tcp_transport_socket.cc)

# Full stack transport tests
add_executable(test_full_stack_transport transport/test_full_stack_transport.cc)

# Application base refactored test
add_executable(test_application_base_refactored test_application_base_refactored.cc)

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

# C API tests
if(BUILD_C_API)
    # Tests using new opaque handle API
    add_executable(test_c_api_types_simple c_api/test_c_api_types_simple.cc)
    add_executable(test_mcp_c_raii c_api/test_mcp_c_raii.cc)
    add_executable(test_c_types_impl c_api/test_c_types_impl.cc)
    add_executable(test_c_memory_impl c_api/test_c_memory_impl.cc)
    add_executable(test_c_collections_impl c_api/test_c_collections_impl.cc)
    add_executable(test_c_api_json_simple c_api/test_c_api_json_simple.cc)
    add_executable(test_c_api_json c_api/test_c_api_json.cc)
    add_executable(test_mcp_c_filter_api c_api/test_mcp_c_filter_api.cc)
    add_executable(test_mcp_c_filter_chain c_api/test_mcp_c_filter_chain.cc)
    add_executable(test_mcp_c_filter_buffer c_api/test_mcp_c_filter_buffer.cc)
    add_executable(test_mcp_c_logging_api c_api/test_mcp_c_logging_api.cc)
    
    # Old tests removed - they used obsolete struct-based API:
    # - test_c_api_basic.cc
    # - test_c_api_types.cc  
    # - test_c_api_builders.cc
    # - test_c_api_type_helpers.cc
    # - test_c_api_protocol_types.cc
    # - test_c_api_memory.cc
    # - test_ffi_improvements.cc (used archived headers)
endif()

# Network tests for recent fixes
add_executable(test_connection_pool_callbacks network/test_connection_pool_callbacks.cc)
add_executable(test_connection_utility network/test_connection_utility.cc)
add_executable(test_listener_filter_chain network/test_listener_filter_chain.cc)
add_executable(test_listener_filter_chain_simple network/test_listener_filter_chain_simple.cc)
add_executable(test_thread_safety_extended network/test_thread_safety_extended.cc)
add_executable(test_connection_state_machine network/test_connection_state_machine.cc)
add_executable(test_filter_chain_state_machine network/test_filter_chain_state_machine.cc)

# Real IO integration tests (using real sockets instead of mocks)
add_executable(test_event_loop_real_io event/test_event_loop_real_io.cc)
add_executable(test_connection_manager_real_io network/test_connection_manager_real_io.cc)
add_executable(test_event_handling integration/test_event_handling.cc)

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
    gopher-mcp
    gtest 
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_mcp_types
    gopher-mcp
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
    gopher-mcp
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_mcp_serialization
    gopher-mcp
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_mcp_serialization_extensive
    gopher-mcp
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_template_serialization
    gopher-mcp
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_event_loop
    gopher-mcp-event
    gopher-mcp
    gtest 
    gtest_main
    Threads::Threads
)

target_link_libraries(test_short_json_api
    gopher-mcp
    gtest 
    gtest_main
    Threads::Threads
)

# Network test libraries
target_link_libraries(test_io_socket_handle
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_address
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_socket
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_socket_interface
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_socket_option
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_transport_socket
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_filter
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_impl
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_listener_impl
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_tcp_listener
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_manager_impl
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# Stream info test libraries
target_link_libraries(test_stream_info_impl
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
)

# Transport test libraries
target_link_libraries(test_stdio_transport_socket
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
)


target_link_libraries(test_http_sse_transport_socket
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_http_sse_transport_socket
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_http_sse_transport_socket_simple
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# SSL/TLS transport test libraries
target_link_libraries(test_ssl_context
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_ssl_transport_socket
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_ssl_state_machine
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_https_sse_factory
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_ssl_integration
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_tcp_transport_socket
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_full_stack_transport
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(test_application_base_refactored
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# Transport socket state machine test libraries
target_link_libraries(test_transport_socket_state_machine
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# HTTP parser test libraries
target_link_libraries(test_http_parser
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_llhttp_parser
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_nghttp2_parser
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_sse_parser
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_stdio_echo_server
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_stdio_echo_client
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_stdio_pipe_bridge
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Advanced example test libraries
target_link_libraries(test_stdio_echo_client_advanced
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_stdio_echo_server_advanced
    gopher-mcp
    gopher-mcp-event
    gtest
    gmock
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# TCP echo test libraries
target_link_libraries(test_tcp_echo_client_basic
    gopher-mcp
    gopher-mcp-event
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_tcp_echo_server_basic
    gopher-mcp
    gopher-mcp-event
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# MCP test libraries
target_link_libraries(test_mcp_connection_manager
    gopher-mcp
    gopher-mcp-event
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_builders
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
)

# Link libraries for C API tests
if(BUILD_C_API)
    # Test for new opaque handle C API types
    target_link_libraries(test_c_api_types_simple
        gopher-mcp
        gopher_mcp_c
        gtest
        gtest_main
        Threads::Threads
    )
    
    # RAII utilities test (comprehensive with all edge cases)
    # Disable atexit leak checker for tests since some tests intentionally leave resources tracked
    target_compile_definitions(test_mcp_c_raii PRIVATE MCP_RAII_DISABLE_ATEXIT)
    
    # New comprehensive implementation tests
    target_link_libraries(test_c_types_impl
        gopher-mcp
        gopher_mcp_c
        gtest
        gtest_main
        Threads::Threads
    )
    
    target_link_libraries(test_c_memory_impl
        gopher-mcp
        gopher_mcp_c
        gtest
        gtest_main
        Threads::Threads
    )
    
    target_link_libraries(test_c_collections_impl
        gopher-mcp
        gopher_mcp_c
        gtest
        gtest_main
        Threads::Threads
    )
    
    target_link_libraries(test_c_api_json_simple
        gopher-mcp
        gopher_mcp_c
        gtest
        gtest_main
        Threads::Threads
    )
    
    target_link_libraries(test_c_api_json
        gopher-mcp
        gopher_mcp_c
        gtest
        gtest_main
        Threads::Threads
    )
    
    target_link_libraries(test_mcp_c_filter_api
        gopher_mcp_c_static
        gopher-mcp-static
        gopher-mcp-event-static
        ${LIBEVENT_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        gtest
        gmock
        gtest_main
        Threads::Threads
    )
    if(NGHTTP2_FOUND)
        target_link_directories(test_mcp_c_filter_api PRIVATE ${NGHTTP2_LIBRARY_DIRS})
        target_link_libraries(test_mcp_c_filter_api ${NGHTTP2_LIBRARIES})
    endif()
    if(LLHTTP_FOUND)
        target_link_libraries(test_mcp_c_filter_api llhttp)
    endif()
    
    target_link_libraries(test_mcp_c_filter_chain
        gopher_mcp_c_static
        gopher-mcp-static
        gopher-mcp-event-static
        ${LIBEVENT_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        gtest
        gmock
        gtest_main
        Threads::Threads
    )
    if(NGHTTP2_FOUND)
        target_link_directories(test_mcp_c_filter_chain PRIVATE ${NGHTTP2_LIBRARY_DIRS})
        target_link_libraries(test_mcp_c_filter_chain ${NGHTTP2_LIBRARIES})
    endif()
    if(LLHTTP_FOUND)
        target_link_libraries(test_mcp_c_filter_chain llhttp)
    endif()
    
    target_link_libraries(test_mcp_c_filter_buffer
        gopher_mcp_c_static
        gopher-mcp-static
        gopher-mcp-event-static
        ${LIBEVENT_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        gtest
        gmock
        gtest_main
        Threads::Threads
    )
    if(NGHTTP2_FOUND)
        target_link_directories(test_mcp_c_filter_buffer PRIVATE ${NGHTTP2_LIBRARY_DIRS})
        target_link_libraries(test_mcp_c_filter_buffer ${NGHTTP2_LIBRARIES})
    endif()
    if(LLHTTP_FOUND)
        target_link_libraries(test_mcp_c_filter_buffer llhttp)
    endif()
    
    target_link_libraries(test_mcp_c_logging_api
        gopher_mcp_c_static
        gopher-mcp-static
        gopher-mcp-event-static
        gopher-mcp-logging-static
        ${LIBEVENT_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        gtest
        gtest_main
        Threads::Threads
    )
    if(NGHTTP2_FOUND)
        target_link_libraries(test_mcp_c_logging_api ${NGHTTP2_LIBRARIES})
    endif()
    if(LLHTTP_FOUND)
        target_link_libraries(test_mcp_c_logging_api llhttp)
    endif()
    
    target_link_libraries(test_mcp_c_raii
        gopher_mcp_c_static  # Use static library for access to internal symbols
        gopher-mcp-static
        gopher-mcp-event-static
        ${LIBEVENT_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        gtest
        gtest_main
        Threads::Threads
    )
    if(NGHTTP2_FOUND)
        target_link_directories(test_mcp_c_raii PRIVATE ${NGHTTP2_LIBRARY_DIRS})
        target_link_libraries(test_mcp_c_raii ${NGHTTP2_LIBRARIES})
    endif()
    if(LLHTTP_FOUND)
        target_link_libraries(test_mcp_c_raii llhttp)
    endif()
endif()

# Link libraries for new tests
target_link_libraries(test_connection_pool_callbacks
    gopher-mcp
    gopher-mcp-event
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_utility
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_listener_filter_chain
    gopher-mcp
    gopher-mcp-event
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_listener_filter_chain_simple
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_thread_safety_extended
    gopher-mcp-event
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_state_machine
    gopher-mcp-event
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_filter_chain_state_machine
    gopher-mcp-event
    gopher-mcp
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# Link libraries for real IO tests
target_link_libraries(test_event_loop_real_io
    gopher-mcp-event
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_connection_manager_real_io
    gopher-mcp
    gopher-mcp-event
    gtest
    gtest_main
    Threads::Threads
)

target_link_libraries(test_event_handling
    gopher-mcp
    gopher-mcp-event
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
add_test(NAME HttpSseTransportSocketTest COMMAND test_http_sse_transport_socket)
add_test(NAME HttpSseTransportSocketSimpleTest COMMAND test_http_sse_transport_socket_simple)
add_test(NAME StdioPipeBridgeTest COMMAND test_stdio_pipe_bridge)

# SSL/TLS transport tests
add_test(NAME SslContextTest COMMAND test_ssl_context)
add_test(NAME SslTransportSocketTest COMMAND test_ssl_transport_socket)
add_test(NAME SslStateMachineTest COMMAND test_ssl_state_machine)
add_test(NAME HttpsSseFactoryTest COMMAND test_https_sse_factory)
add_test(NAME SslIntegrationTest COMMAND test_ssl_integration)

# HTTP SSE state machine test

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

# TCP transport socket test
add_test(NAME TcpTransportSocketTest COMMAND test_tcp_transport_socket)

# Full stack transport test
add_test(NAME FullStackTransportTest COMMAND test_full_stack_transport)


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
add_test(NAME FilterChainStateMachineTest COMMAND test_filter_chain_state_machine)

# Real IO integration tests
add_test(NAME EventLoopRealIoTest COMMAND test_event_loop_real_io)
add_test(NAME ConnectionManagerRealIoTest COMMAND test_connection_manager_real_io)
add_test(NAME EventHandlingTest COMMAND test_event_handling)

# Filter state machine tests
add_executable(test_http_codec_state_machine filter/test_http_codec_state_machine.cc)
target_link_libraries(test_http_codec_state_machine 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME HttpCodecStateMachineTest COMMAND test_http_codec_state_machine)

add_executable(test_http_codec_filter_simple filter/test_http_codec_filter_simple.cc)
target_link_libraries(test_http_codec_filter_simple 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME HttpCodecFilterSimpleTest COMMAND test_http_codec_filter_simple)

# Comprehensive test with real I/O
add_executable(test_http_codec_filter filter/test_http_codec_filter.cc)
target_link_libraries(test_http_codec_filter 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME HttpCodecFilterTest COMMAND test_http_codec_filter)

# HTTP Codec Filter Integration Test - removed (file was abandoned per user request)

# HTTP Routing Filter Test - Simple version
add_executable(test_http_routing_filter_simple filter/test_http_routing_filter_simple.cc)
target_link_libraries(test_http_routing_filter_simple 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME HttpRoutingFilterSimpleTest COMMAND test_http_routing_filter_simple)

add_executable(test_sse_codec_state_machine filter/test_sse_codec_state_machine.cc)
target_link_libraries(test_sse_codec_state_machine 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME SseCodecStateMachineTest COMMAND test_sse_codec_state_machine)

add_executable(test_sse_codec_filter filter/test_sse_codec_filter_simple.cc)
target_link_libraries(test_sse_codec_filter 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME SseCodecFilterTest COMMAND test_sse_codec_filter)

# Comprehensive SSE tests disabled due to compilation issues with abstract classes
# add_executable(test_sse_codec_filter_comprehensive filter/test_sse_codec_filter.cc)
# target_link_libraries(test_sse_codec_filter_comprehensive 
#     gopher-mcp
#     gopher-mcp-event
#     gtest 
#     gtest_main 
#     gmock
#     Threads::Threads
# )
# add_test(NAME SseCodecFilterComprehensiveTest COMMAND test_sse_codec_filter_comprehensive)

# MCP JSON-RPC Filter Tests
add_executable(test_json_rpc_protocol_filter filter/test_json_rpc_protocol_filter.cc)
target_link_libraries(test_json_rpc_protocol_filter 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME JsonRpcProtocolFilterTest COMMAND test_json_rpc_protocol_filter)

# MCP Protocol State Machine Tests
add_executable(test_mcp_protocol_state_machine protocol/test_mcp_protocol_state_machine.cc)
target_link_libraries(test_mcp_protocol_state_machine 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME McpProtocolStateMachineTest COMMAND test_mcp_protocol_state_machine)

# Stdio Filter Chain Factory Tests
add_executable(test_stdio_filter_chain_factory filter/test_stdio_filter_chain_factory.cc)
target_link_libraries(test_stdio_filter_chain_factory 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME StdioFilterChainFactoryTest COMMAND test_stdio_filter_chain_factory)

# JSON-RPC Filter Factory Tests
add_executable(test_json_rpc_filter_factory filter/test_json_rpc_filter_factory.cc)
target_link_libraries(test_json_rpc_filter_factory 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME JsonRpcFilterFactoryTest COMMAND test_json_rpc_filter_factory)

# HTTP SSE Filter Chain Factory Tests
add_executable(test_http_sse_filter_chain_factory filter/test_http_sse_filter_chain_factory.cc)
target_link_libraries(test_http_sse_filter_chain_factory 
    gopher-mcp
    gopher-mcp-event
    gtest 
    gtest_main 
    gmock
    Threads::Threads
)
add_test(NAME HttpSseFilterChainFactoryTest COMMAND test_http_sse_filter_chain_factory)

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
            test_stdio_transport_socket test_http_sse_transport_socket
            test_gopher_mcp_connection_manager test_builders
            test_event_loop_real_io test_connection_manager_real_io
            test_http_codec_state_machine test_sse_codec_state_machine
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests..."
)

# ============================================================================
# Logging Framework Tests
# ============================================================================

# Configuration tests
add_executable(test_config_merge config/test_config_merge.cc)
add_executable(test_parse_error config/test_parse_error.cc)
add_executable(test_server_types config/test_server_types.cc)
add_executable(test_types config/test_types.cc)
add_executable(test_units config/test_units.cc)
add_executable(test_validation_policy config/test_validation_policy.cc)
add_executable(test_file_config_source config/test_file_config_source.cc)
add_executable(test_file_config_source_enhanced config/test_file_config_source_enhanced.cc)
add_executable(test_file_source_interface config/test_file_source_interface.cc)
add_executable(test_file_source_yaml_env_includes config/test_file_source_yaml_env_includes.cc)
add_executable(test_merge_and_validate config/test_merge_and_validate.cc)
add_executable(test_search_precedence config/test_search_precedence.cc)
add_executable(test_units config/test_units.cc)
add_executable(test_merge_semantics config/test_merge_semantics.cc)

# Logging tests
add_executable(test_bloom_filter logging/test_bloom_filter.cc)
add_executable(test_log_level logging/test_log_level.cc)
add_executable(test_log_sink logging/test_log_sink.cc)
add_executable(test_logger logging/test_logger.cc)
add_executable(test_logger_registry logging/test_logger_registry.cc)

# Link configuration test libraries
target_link_libraries(test_config_merge
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_parse_error
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_server_types
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_types
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_units
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_validation_policy
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_file_config_source
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_file_config_source_enhanced
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_file_source_interface
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_file_source_yaml_env_includes
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_merge_and_validate
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_search_precedence
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_units
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_merge_semantics
    gopher-mcp
    gtest
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# Link logging test libraries
target_link_libraries(test_bloom_filter
    gopher-mcp-logging-static
    fmt::fmt
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_log_level
    gopher-mcp-logging-static
    fmt::fmt
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_log_sink
    gopher-mcp-logging-static
    fmt::fmt
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_logger
    gopher-mcp-logging-static
    fmt::fmt
    gtest
    gmock
    gtest_main
    Threads::Threads
)

target_link_libraries(test_logger_registry
    gopher-mcp-logging-static
    fmt::fmt
    gtest
    gmock
    gtest_main
    Threads::Threads
)

# Add configuration tests to CTest
add_test(NAME ConfigMergeTest COMMAND test_config_merge)
add_test(NAME ParseErrorTest COMMAND test_parse_error)
add_test(NAME ServerTypesTest COMMAND test_server_types)
add_test(NAME TypesTest COMMAND test_types)
add_test(NAME UnitsTest COMMAND test_units)
add_test(NAME ValidationPolicyTest COMMAND test_validation_policy)
add_test(NAME FileConfigSourceTest COMMAND test_file_config_source)
add_test(NAME FileConfigSourceEnhancedTest COMMAND test_file_config_source_enhanced)
add_test(NAME FileSourceInterfaceTest COMMAND test_file_source_interface)
add_test(NAME FileSourceYamlEnvIncludesTest COMMAND test_file_source_yaml_env_includes)
add_test(NAME MergeAndValidateTest COMMAND test_merge_and_validate)
add_test(NAME SearchPrecedenceTest COMMAND test_search_precedence)
add_test(NAME UnitsTest COMMAND test_units)
add_test(NAME MergeSemanticsTest COMMAND test_merge_semantics)

# Add logging tests to CTest
add_test(NAME test_bloom_filter COMMAND test_bloom_filter)
add_test(NAME test_log_level COMMAND test_log_level)
add_test(NAME test_log_sink COMMAND test_log_sink)
add_test(NAME test_logger COMMAND test_logger)
add_test(NAME test_logger_registry COMMAND test_logger_registry)

