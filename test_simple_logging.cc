#include <iostream>
#include "mcp/c_api/mcp_c_logging_api.h"

int main() {
    std::cout << "Starting test...\n";
    
    // Simple string view
    mcp_string_view_t name;
    name.data = "test";
    name.length = 4;
    
    std::cout << "Creating logger...\n";
    mcp_logger_handle_t handle;
    auto result = mcp_logger_get_or_create(name, &handle);
    
    std::cout << "Result: " << result << ", Handle: " << handle << "\n";
    
    if (result == MCP_LOG_OK) {
        std::cout << "Logger created successfully\n";
        mcp_logger_release(handle);
        std::cout << "Logger released\n";
    } else {
        std::cout << "Failed to create logger\n";
    }
    
    return 0;
}