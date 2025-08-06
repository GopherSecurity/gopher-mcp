# Test executables
add_executable(test_variant test_variant.cc)
add_executable(test_variant_extensive test_variant_extensive.cc)
add_executable(test_variant_advanced test_variant_advanced.cc)
add_executable(test_optional test_optional.cc)
add_executable(test_optional_extensive test_optional_extensive.cc)
add_executable(test_optional_advanced test_optional_advanced.cc)
add_executable(test_mcp_type_helpers test_mcp_type_helpers.cc)
add_executable(test_mcp_types test_mcp_types.cc)
add_executable(test_compat test_compat.cc)
add_executable(test_buffer test_buffer.cc)
add_executable(test_json test_json.cc)
# Temporarily disabled - being refactored
# add_executable(test_mcp_serialization test_mcp_serialization.cc)
# add_executable(test_mcp_serialization_extensive test_mcp_serialization_extensive.cc)
add_executable(test_template_serialization test_template_serialization.cc)
add_executable(test_event_loop test_event_loop.cc)

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
add_executable(test_connection_manager_impl network/test_connection_manager_impl.cc)

# Stream info tests
add_executable(test_stream_info_impl stream_info/test_stream_info_impl.cc)

# Transport tests
add_executable(test_stdio_transport_socket transport/test_stdio_transport_socket.cc)
add_executable(test_http_sse_transport_socket transport/test_http_sse_transport_socket.cc)

# MCP tests
add_executable(test_mcp_connection_manager test_mcp_connection_manager.cc)

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
    gtest 
    gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_link_libraries(test_mcp_types
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

target_link_libraries(test_json
    mcp-sdk
    gtest 
    gtest_main
    Threads::Threads
)

# Temporarily disabled - being refactored
# target_link_libraries(test_mcp_serialization
#     mcp-sdk
#     gtest 
#     gtest_main
#     Threads::Threads
# )
# 
# target_link_libraries(test_mcp_serialization_extensive
#     mcp-sdk
#     gtest 
#     gtest_main
#     Threads::Threads
# )

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

target_link_libraries(test_http_sse_transport_socket
    mcp-sdk
    gtest
    gtest_main
    Threads::Threads
)

# MCP test libraries
target_link_libraries(test_mcp_connection_manager
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
add_test(NAME JsonTest COMMAND test_json)
# Temporarily disabled - being refactored
# add_test(NAME McpSerializationTest COMMAND test_mcp_serialization)
# add_test(NAME McpSerializationExtensiveTest COMMAND test_mcp_serialization_extensive)
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
add_test(NAME ConnectionManagerImplTest COMMAND test_connection_manager_impl)

# Stream info tests
add_test(NAME StreamInfoImplTest COMMAND test_stream_info_impl)

# Transport tests
add_test(NAME StdioTransportSocketTest COMMAND test_stdio_transport_socket)
add_test(NAME HttpSseTransportSocketTest COMMAND test_http_sse_transport_socket)

# MCP tests
add_test(NAME McpConnectionManagerTest COMMAND test_mcp_connection_manager)

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
            test_buffer test_json test_template_serialization test_event_loop
            test_io_socket_handle test_address test_socket
            test_socket_interface test_socket_option test_transport_socket
            test_filter test_connection_impl test_listener_impl
            test_connection_manager_impl test_stream_info_impl
            test_stdio_transport_socket test_http_sse_transport_socket
            test_mcp_connection_manager
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests..."
)

