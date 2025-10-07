# MCP Examples

# MCP Client Example
add_executable(mcp_example_client 
    mcp_example_client.cc
)

target_link_libraries(mcp_example_client 
    gopher-mcp 
    gopher-mcp-event
    gopher-mcp-echo-advanced  # For advanced features
    ${LIBEVENT_LIBRARIES}
)

# MCP Server Example
add_executable(mcp_example_server
    mcp_example_server.cc
)

target_link_libraries(mcp_example_server
    gopher-mcp
    gopher-mcp-event
    gopher-mcp-echo-advanced  # For advanced features
    ${LIBEVENT_LIBRARIES}
)

# Config-driven MCP Server Example
add_executable(mcp_config_example_server
    mcp_config_example_server.cc
)

target_link_libraries(mcp_config_example_server
    gopher-mcp
    gopher-mcp-event
    ${LIBEVENT_LIBRARIES}
)

# Install targets
install(TARGETS mcp_example_client mcp_example_server mcp_config_example_server
    RUNTIME DESTINATION bin
)

# Install example configuration files
install(FILES
    ../configs/mcp_server_example.json
    DESTINATION share/mcp/examples/configs
)