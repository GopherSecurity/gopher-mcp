# MCP Examples

# MCP Client Example
add_executable(mcp_example_client 
    mcp_example_client.cc
)

target_link_libraries(mcp_example_client 
    mcp-sdk 
    mcp-event
    mcp-echo-advanced  # For advanced features
    ${LIBEVENT_LIBRARIES}
)

# MCP Server Example  
add_executable(mcp_example_server 
    mcp_example_server.cc
)

target_link_libraries(mcp_example_server 
    mcp-sdk
    mcp-event
    mcp-echo-advanced  # For advanced features
    ${LIBEVENT_LIBRARIES}
)

# MCP Server with HTTP Routing Example
add_executable(mcp_server_with_http_routing
    mcp_server_with_http_routing.cc
)

target_link_libraries(mcp_server_with_http_routing
    mcp-sdk
    mcp-event
    ${LIBEVENT_LIBRARIES}
)

# Protocol detection example
add_executable(protocol_detection_example 
    ../protocol_detection_example.cc
)

target_link_libraries(protocol_detection_example 
    mcp-sdk 
    mcp-event
    ${LIBEVENT_LIBRARIES}
)

# MCP Client with Protocol Detection
add_executable(mcp_client_with_protocol_detection
    mcp_client_with_protocol_detection.cc
)

target_link_libraries(mcp_client_with_protocol_detection
    mcp-sdk
    mcp-event
    ${LIBEVENT_LIBRARIES}
)

# Install targets
install(TARGETS mcp_example_client mcp_example_server mcp_server_with_http_routing protocol_detection_example mcp_client_with_protocol_detection
    RUNTIME DESTINATION bin
)