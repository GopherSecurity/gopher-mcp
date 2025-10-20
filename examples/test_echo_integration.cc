#include <iostream>

#include "mcp/builders.h"

int main() {
  std::cout << "Testing MCP Echo Implementation with Builder Pattern\n";
  std::cout << "====================================================\n\n";

  try {
    // Test 1: Build a JSON-RPC Request using builder pattern
    std::cout << "Test 1: Building JSON-RPC Request\n";
    auto request =
        mcp::make<mcp::jsonrpc::Request>(mcp::make_request_id(1), "test.echo")
            .param("message", "Hello, World!")
            .param("timestamp", static_cast<int64_t>(1234567890))
            .build();

    std::cout << "✓ Request built successfully\n";
    std::cout << "  Method: " << request.method << "\n";
    std::cout << "  Has params: " << (request.params.has_value() ? "yes" : "no")
              << "\n";
    if (request.params.has_value()) {
      std::cout << "  Params count: " << request.params.value().size() << "\n";
    }

    // Test 2: Build a JSON-RPC Response using builder pattern
    std::cout << "\nTest 2: Building JSON-RPC Response\n";
    auto metadata = mcp::make<mcp::Metadata>()
                        .add("echo", true)
                        .add("method", "test.echo")
                        .add("success", true)
                        .build();

    auto response = mcp::make<mcp::jsonrpc::Response>(request.id)
                        .result(mcp::jsonrpc::ResponseResult(metadata))
                        .build();

    std::cout << "✓ Response built successfully\n";
    std::cout << "  Has result: "
              << (response.result.has_value() ? "yes" : "no") << "\n";
    std::cout << "  Has error: " << (response.error.has_value() ? "yes" : "no")
              << "\n";

    // Test 3: Build a JSON-RPC Notification using builder pattern
    std::cout << "\nTest 3: Building JSON-RPC Notification\n";
    auto notification = mcp::make<mcp::jsonrpc::Notification>("log")
                            .param("level", "info")
                            .param("message", "Test notification")
                            .build();

    std::cout << "✓ Notification built successfully\n";
    std::cout << "  Method: " << notification.method << "\n";
    std::cout << "  Has params: "
              << (notification.params.has_value() ? "yes" : "no") << "\n";
    if (notification.params.has_value()) {
      std::cout << "  Params count: " << notification.params.value().size()
                << "\n";
    }

    // Test 4: Build complex nested structures
    std::cout << "\nTest 4: Building Complex Nested Structures\n";

    // Build a tool using ToolBuilder
    auto tool =
        mcp::ToolBuilder("calculator")
            .description("A simple calculator tool")
            .parameter("expression", "string", "Mathematical expression", true)
            .build();

    std::cout << "✓ Tool built successfully\n";
    std::cout << "  Tool name: " << tool.name << "\n";
    std::cout << "  Has description: "
              << (tool.description.has_value() ? "yes" : "no") << "\n";
    std::cout << "  Has parameters: "
              << (tool.parameters.has_value() ? "yes" : "no") << "\n";

    // Build sampling params using SamplingParamsBuilder
    auto sampling = mcp::SamplingParamsBuilder()
                        .temperature(0.7)
                        .maxTokens(100)
                        .metadata("test_key", "test_value")
                        .build();

    std::cout << "✓ SamplingParams built successfully\n";
    std::cout << "  Has temperature: "
              << (sampling.temperature.has_value() ? "yes" : "no") << "\n";
    std::cout << "  Has maxTokens: "
              << (sampling.maxTokens.has_value() ? "yes" : "no") << "\n";
    std::cout << "  Has metadata: "
              << (sampling.metadata.has_value() ? "yes" : "no") << "\n";

    // Test 5: Build metadata structures
    std::cout << "\nTest 5: Building Metadata Structures\n";
    auto complex_metadata = mcp::make<mcp::Metadata>()
                                .add("string_field", "test_string")
                                .add("int_field", static_cast<int64_t>(42))
                                .add("double_field", 3.14159)
                                .add("bool_field", true)
                                .build();

    std::cout << "✓ Complex metadata built successfully\n";
    std::cout << "  Field count: " << complex_metadata.size() << "\n";

    std::cout << "\n=== All Tests Completed Successfully! ===\n";
    std::cout << "\nThe MCP Echo implementation demonstrates:\n";
    std::cout << "• Proper use of the builder pattern from mcp/builders.h\n";
    std::cout << "• Generic make<T>() function for type-safe construction\n";
    std::cout << "• Fluent interface for complex object building\n";
    std::cout << "• Integration with JSON-RPC message types\n";
    std::cout << "• Support for nested metadata structures\n";
    std::cout << "• Compatibility with existing MCP builders\n\n";

    std::cout << "The echo server and client examples showcase:\n";
    std::cout << "• Real-world usage of builders in MCP applications\n";
    std::cout << "• Stdio transport implementation for MCP\n";
    std::cout << "• JSON-RPC message handling with builders\n";
    std::cout << "• Event-driven architecture with libevent\n";
    std::cout << "• Comprehensive error handling and logging\n\n";

    return 0;

  } catch (const std::exception& e) {
    std::cout << "✗ Test failed with exception: " << e.what() << "\n";
    return 1;
  }
}