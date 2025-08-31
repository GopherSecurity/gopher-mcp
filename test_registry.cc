#include <iostream>
#include "mcp/logging/logger_registry.h"

int main() {
    std::cout << "Getting registry instance...\n" << std::flush;
    auto& registry = mcp::logging::LoggerRegistry::instance();
    std::cout << "Got registry instance\n" << std::flush;
    
    std::cout << "Getting logger...\n" << std::flush;
    auto logger = registry.getOrCreateLogger("test");
    std::cout << "Got logger\n" << std::flush;
    
    return 0;
}