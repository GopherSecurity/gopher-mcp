/**
 * @file basic-usage.ts
 * @brief Basic usage example for MCP Filter SDK
 *
 * This example demonstrates how to use the canonical filter configuration:
 * - Creating filter chains with canonical config
 * - Managing buffers
 * - Using the existing C++ RAII system
 */

import {
  BufferOwnership,
  createBufferFromString,
  createBufferOwned,
  readStringFromBuffer,
  FilterChain,
  createRealDispatcher,
  destroyDispatcher,
} from "../src";
import type { CanonicalConfig } from "../src";

/**
 * Example: Create a simple HTTP processing pipeline using canonical configuration
 */
async function createHttpPipeline() {
  console.log("🔧 Creating HTTP processing pipeline...");

  try {
    // Create dispatcher
    const dispatcher = createRealDispatcher();

    // Create filter chain using canonical configuration
    const config: CanonicalConfig = {
      listeners: [
        {
          name: "http_listener",
          address: {
            socket_address: {
              address: "127.0.0.1",
              port_value: 8080
            }
          },
          filter_chains: [
            {
              filters: [
                { name: "auth", type: "auth" },
                { name: "rate_limiter", type: "rate_limiter" },
                { name: "access_log", type: "access_log" }
              ]
            }
          ]
        }
      ]
    };

    const filterChain = new FilterChain(dispatcher, config);
    console.log(`✅ Created filter chain with 3 filters`);

    return { filterChain, config, dispatcher };
  } catch (error) {
    console.error("❌ Failed to create HTTP pipeline:", error);
    throw error;
  }
}

/**
 * Example: Create a multi-filter pipeline with metrics
 */
async function createMonitoringPipeline() {
  console.log("🔧 Creating monitoring pipeline...");

  try {
    const config: CanonicalConfig = {
      listeners: [
        {
          name: "monitoring_listener",
          address: {
            socket_address: {
              address: "127.0.0.1",
              port_value: 9090
            }
          },
          filter_chains: [
            {
              filters: [
                { name: "metrics", type: "metrics" },
                { name: "tracing", type: "tracing" },
                { name: "access_log", type: "access_log" }
              ]
            }
          ]
        }
      ]
    };

    const dispatcher = createRealDispatcher();
    const filterChain = new FilterChain(dispatcher, config);
    console.log(`✅ Created monitoring pipeline`);

    return { filterChain, config, dispatcher };
  } catch (error) {
    console.error("❌ Failed to create monitoring pipeline:", error);
    throw error;
  }
}

/**
 * Example: Buffer management with zero-copy operations
 */
async function demonstrateBufferOperations() {
  console.log("🔧 Demonstrating buffer operations...");

  try {
    // Create a buffer from string data
    const buffer = createBufferFromString("Hello, MCP Filter SDK!", BufferOwnership.SHARED);
    console.log(`✅ Created buffer`);

    // Read the string back from the buffer
    const content = readStringFromBuffer(buffer);
    console.log(`📖 Buffer content: "${content}"`);

    // Create a buffer with specific capacity
    const largeBuffer = createBufferOwned(1024, BufferOwnership.EXCLUSIVE);
    console.log(`✅ Created large buffer with 1024 bytes capacity`);

    return { buffer, largeBuffer, content };
  } catch (error) {
    console.error("❌ Failed to demonstrate buffer operations:", error);
    throw error;
  }
}

/**
 * Example: Advanced chain with protocol stack
 */
async function demonstrateProtocolStack() {
  console.log("🔧 Demonstrating protocol stack...");

  try {
    const config: CanonicalConfig = {
      listeners: [
        {
          name: "protocol_listener",
          address: {
            socket_address: {
              address: "127.0.0.1",
              port_value: 8443
            }
          },
          filter_chains: [
            {
              filters: [
                { name: "tcp_proxy", type: "tcp_proxy" },
                { name: "tls", type: "tls" },
                { name: "http_codec", type: "http.codec" },
                { name: "json_rpc", type: "json_rpc.dispatcher" }
              ]
            }
          ]
        }
      ]
    };

    const dispatcher = createRealDispatcher();
    const filterChain = new FilterChain(dispatcher, config);
    console.log(`✅ Created protocol stack with 4 layers`);

    return { filterChain, config, dispatcher };
  } catch (error) {
    console.error("❌ Failed to demonstrate protocol stack:", error);
    throw error;
  }
}

/**
 * Main example function
 */
async function main() {
  console.log("🚀 MCP Filter SDK - Basic Usage Example\n");

  const dispatchers: any[] = [];

  try {
    // Demonstrate different pipeline types
    const httpPipeline = await createHttpPipeline();
    dispatchers.push(httpPipeline.dispatcher);
    console.log("\n---\n");

    const monitoringPipeline = await createMonitoringPipeline();
    dispatchers.push(monitoringPipeline.dispatcher);
    console.log("\n---\n");

    const bufferOps = await demonstrateBufferOperations();
    console.log("\n---\n");

    const protocolStack = await demonstrateProtocolStack();
    dispatchers.push(protocolStack.dispatcher);
    console.log("\n---\n");

    console.log("🎉 All examples completed successfully!");
    console.log("\n📊 Summary:");
    console.log(`- HTTP Pipeline: created with canonical config`);
    console.log(`- Monitoring Pipeline: created with canonical config`);
    console.log(`- Buffer Operations: ${bufferOps.content}`);
    console.log(`- Protocol Stack: created with canonical config`);

    // Cleanup
    console.log("\n🧹 Cleaning up...");
    for (const dispatcher of dispatchers) {
      destroyDispatcher(dispatcher);
    }
    console.log("✅ Cleanup complete");
  } catch (error) {
    console.error("💥 Example failed:", error);
    // Cleanup on error
    for (const dispatcher of dispatchers) {
      try {
        destroyDispatcher(dispatcher);
      } catch (e) {
        // Ignore cleanup errors
      }
    }
    process.exit(1);
  }
}

// Run the example if this file is executed directly
if (require.main === module) {
  main().catch(console.error);
}

export {
  createHttpPipeline,
  createMonitoringPipeline,
  demonstrateProtocolStack,
  demonstrateBufferOperations,
};
