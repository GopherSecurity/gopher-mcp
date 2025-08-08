#include "mcp/transport/stdio_pipe_transport.h"
#include "mcp/buffer.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Testing StdioPipeTransport...\n";
    
    // Create transport config
    mcp::transport::StdioPipeTransportConfig config;
    config.stdin_fd = 0;
    config.stdout_fd = 1;
    config.non_blocking = true;
    
    // Create transport
    auto transport = std::make_unique<mcp::transport::StdioPipeTransport>(config);
    
    // Initialize (creates pipes and starts threads)
    auto result = transport->initialize();
    if (mcp::holds_alternative<mcp::Error>(result)) {
        std::cerr << "Failed to initialize: " << mcp::get<mcp::Error>(result).message << "\n";
        return 1;
    }
    
    std::cout << "Pipe bridge initialized successfully!\n";
    std::cout << "Type a message and press Enter:\n";
    
    // The transport is now bridging stdio to pipes
    // Background threads are running
    
    // Give it time to process
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    std::cout << "Test complete.\n";
    return 0;
}