/**
 * @file c_api_example.c
 * @brief Example demonstrating MCP C API usage
 * 
 * This example shows how to use the MCP C API from pure C code.
 * It creates a stdio-based MCP client that can communicate with an MCP server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "mcp/c_api/mcp_c_api.h"

// Global dispatcher for signal handling
static mcp_dispatcher_t g_dispatcher = NULL;

// Signal handler to stop the dispatcher
void signal_handler(int sig) {
    printf("\nReceived signal %d, stopping...\n", sig);
    if (g_dispatcher) {
        mcp_dispatcher_stop(g_dispatcher);
    }
}

// Connection state callback
void on_connection_state_change(
    mcp_connection_t connection,
    mcp_connection_state_t old_state,
    mcp_connection_state_t new_state,
    void* user_data
) {
    const char* state_names[] = {
        "CONNECTING",
        "CONNECTED",
        "DISCONNECTING",
        "DISCONNECTED",
        "ERROR"
    };
    
    printf("Connection state changed: %s -> %s\n",
           state_names[old_state], state_names[new_state]);
    
    // Send initialize request when connected
    if (new_state == MCP_CONNECTION_STATE_CONNECTED) {
        // Create initialize request JSON
        const char* init_request = 
            "{\"jsonrpc\":\"2.0\","
            "\"method\":\"initialize\","
            "\"params\":{"
            "\"protocolVersion\":\"2025-06-18\","
            "\"capabilities\":{},"
            "\"clientInfo\":{"
            "\"name\":\"C Example Client\","
            "\"version\":\"1.0.0\""
            "}},"
            "\"id\":1}";
        
        // Send the request
        mcp_connection_write(
            connection,
            (const uint8_t*)init_request,
            strlen(init_request),
            NULL,
            NULL
        );
        
        printf("Sent initialize request\n");
    }
}

// Data received callback
void on_data_received(
    mcp_connection_t connection,
    const uint8_t* data,
    size_t length,
    void* user_data
) {
    // Print received data (assuming it's text)
    printf("Received %zu bytes: %.*s\n", length, (int)length, data);
    
    // Parse and handle the response
    // In a real implementation, you would parse the JSON-RPC response
    // and handle it appropriately
}

// Error callback
void on_error(
    mcp_result_t error,
    const char* message,
    void* user_data
) {
    fprintf(stderr, "Error %d: %s\n", error, message);
}

// Timer callback example
void on_timer(void* user_data) {
    static int count = 0;
    printf("Timer fired: %d\n", ++count);
    
    // Stop after 5 timer events
    if (count >= 5 && g_dispatcher) {
        printf("Stopping dispatcher from timer\n");
        mcp_dispatcher_stop(g_dispatcher);
    }
}

int main(int argc, char* argv[]) {
    mcp_result_t result;
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("MCP C API Example\n");
    printf("=================\n\n");
    
    // Initialize the library
    result = mcp_init(NULL);
    if (result != MCP_OK) {
        fprintf(stderr, "Failed to initialize MCP library: %d\n", result);
        return 1;
    }
    
    printf("MCP Library version: %s\n", mcp_get_version());
    
    // Create dispatcher
    g_dispatcher = mcp_dispatcher_create();
    if (!g_dispatcher) {
        fprintf(stderr, "Failed to create dispatcher: %s\n", mcp_get_last_error());
        mcp_shutdown();
        return 1;
    }
    
    printf("Created dispatcher\n");
    
    // Create connection
    mcp_connection_t connection = mcp_connection_create_client(
        g_dispatcher,
        MCP_TRANSPORT_STDIO
    );
    if (!connection) {
        fprintf(stderr, "Failed to create connection: %s\n", mcp_get_last_error());
        mcp_dispatcher_destroy(g_dispatcher);
        mcp_shutdown();
        return 1;
    }
    
    printf("Created stdio connection\n");
    
    // Set connection callbacks
    result = mcp_connection_set_callbacks(
        connection,
        on_connection_state_change,
        on_data_received,
        on_error,
        NULL  // user_data
    );
    if (result != MCP_OK) {
        fprintf(stderr, "Failed to set callbacks: %d\n", result);
        mcp_connection_destroy(connection);
        mcp_dispatcher_destroy(g_dispatcher);
        mcp_shutdown();
        return 1;
    }
    
    // Example: Create and enable a timer
    uint64_t timer_id = mcp_dispatcher_create_timer(
        g_dispatcher,
        on_timer,
        NULL  // user_data
    );
    if (timer_id > 0) {
        // Enable timer to fire every 2 seconds
        mcp_dispatcher_enable_timer(g_dispatcher, timer_id, 2000, true);
        printf("Created timer (fires every 2 seconds)\n");
    }
    
    // Connect
    result = mcp_connection_connect(connection);
    if (result != MCP_OK) {
        fprintf(stderr, "Failed to connect: %d\n", result);
        mcp_connection_destroy(connection);
        mcp_dispatcher_destroy(g_dispatcher);
        mcp_shutdown();
        return 1;
    }
    
    printf("Connecting...\n");
    printf("Press Ctrl+C to stop\n\n");
    
    // Run the event loop
    result = mcp_dispatcher_run(g_dispatcher);
    if (result != MCP_OK && result != MCP_ERROR_CANCELLED) {
        fprintf(stderr, "Dispatcher run failed: %d\n", result);
    }
    
    printf("\nShutting down...\n");
    
    // Clean up
    if (timer_id > 0) {
        mcp_dispatcher_destroy_timer(g_dispatcher, timer_id);
    }
    mcp_connection_close(connection, true);
    mcp_connection_destroy(connection);
    mcp_dispatcher_destroy(g_dispatcher);
    mcp_shutdown();
    
    printf("Cleanup complete\n");
    
    return 0;
}

/**
 * Building this example:
 * 
 * gcc -o c_api_example c_api_example.c \
 *     -I../include \
 *     -L../build -lmcp_c \
 *     -Wl,-rpath,../build
 * 
 * Or with CMake, add to CMakeLists.txt:
 * 
 * add_executable(c_api_example examples/c_api_example.c)
 * target_link_libraries(c_api_example mcp_c)
 * 
 * Running:
 * ./c_api_example
 * 
 * To test with an MCP server:
 * 1. Start an MCP server that uses stdio transport
 * 2. Pipe the client and server together:
 *    mkfifo /tmp/mcp_pipe
 *    ./mcp_server < /tmp/mcp_pipe | ./c_api_example > /tmp/mcp_pipe
 */