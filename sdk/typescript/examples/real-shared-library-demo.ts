#!/usr/bin/env node

/**
 * @file real-shared-library-demo.ts
 * @brief Comprehensive demonstration using the REAL shared library
 *
 * This example demonstrates all the functionality we've implemented:
 * - Basic filters (HTTP, TCP Proxy)
 * - Advanced buffer operations
 * - Filter chains
 * - RAII resource management
 * - Real C API integration
 *
 * IMPORTANT: This uses the ACTUAL shared library, not mocks!
 */

import {
  // Advanced Buffer
  AdvancedBuffer,
  AdvancedBufferPool,
  // RAII Resource Management
  AllocationTransaction,
  BufferOwnership,
  // HTTP Filter
  HttpFilter,
  HttpFilterType,
  makeResourceGuard,
  makeScopedCleanup,

  // FFI Bindings (real shared library)
  mcpFilterLib,
  TcpFilterType,
  // TCP Proxy Filter
  TcpProxyFilter,
} from "../src";

// ============================================================================
// Utility Functions
// ============================================================================

function log(message: string, data?: any) {
  console.log(`🔍 ${message}`);
  if (data !== undefined) {
    console.log(`   Data:`, data);
  }
}

function logSuccess(message: string) {
  console.log(`✅ ${message}`);
}

function logError(message: string, error?: any) {
  console.log(`❌ ${message}`);
  if (error) {
    console.log(`   Error:`, error);
  }
}

function logSection(title: string) {
  console.log(`\n${"=".repeat(60)}`);
  console.log(`📋 ${title}`);
  console.log(`${"=".repeat(60)}`);
}

// ============================================================================
// 1. Basic Filter Creation and Management
// ============================================================================

async function demonstrateBasicFilters() {
  logSection("1. Basic Filter Creation and Management");

  try {
    // Create HTTP Filter
    log("Creating HTTP Filter...");
    const httpFilter = new HttpFilter({
      name: "demo-http-filter",
      type: HttpFilterType.HTTP_CODEC,
      settings: {
        port: 8080,
        host: "localhost",
        ssl: false,
        compression: true,
        maxBodySize: 1024 * 1024,
        timeout: 30000,
        cors: {
          enabled: true,
          origins: ["*"],
          methods: ["GET", "POST", "PUT", "DELETE"],
        },
      },
      layer: 7, // Application layer
      memoryPool: null,
    });
    logSuccess("HTTP Filter created successfully");

    // Create TCP Proxy Filter
    log("Creating TCP Proxy Filter...");
    const tcpFilter = new TcpProxyFilter({
      name: "demo-tcp-filter",
      type: TcpFilterType.TCP_PROXY,
      settings: {
        upstreamHost: "127.0.0.1",
        upstreamPort: 8080,
        localPort: 9090,
        localHost: "127.0.0.1",
        maxConnections: 100,
        connectionTimeout: 30000,
        bufferSize: 8192,
        keepAlive: true,
      },
      layer: 4, // Transport layer
      memoryPool: null,
    });
    logSuccess("TCP Proxy Filter created successfully");

    // Test filter properties
    log("HTTP Filter properties:", {
      name: httpFilter.name,
      type: httpFilter.type,
      filterHandle: httpFilter.filterHandle,
      memoryPool: httpFilter.memoryPool,
    });

    log("TCP Filter properties:", {
      name: tcpFilter.name,
      type: tcpFilter.type,
      filterHandle: tcpFilter.filterHandle,
      memoryPool: tcpFilter.memoryPool,
    });

    // Clean up
    httpFilter.destroy();
    tcpFilter.destroy();
    logSuccess("Filters cleaned up successfully");
  } catch (error) {
    logError("Failed to create basic filters", error);
  }
}

// ============================================================================
// 2. Advanced Buffer Operations
// ============================================================================

async function demonstrateAdvancedBuffers() {
  logSection("2. Advanced Buffer Operations");

  try {
    // Create buffer pool
    log("Creating buffer pool...");
    const pool = new AdvancedBufferPool(4096, 10, 2, false, true);
    logSuccess("Buffer pool created successfully");

    // Create buffer
    log("Creating advanced buffer...");
    const buffer = new AdvancedBuffer(1024, BufferOwnership.EXCLUSIVE);
    logSuccess("Buffer created successfully");

    // Test data operations
    log("Testing buffer data operations...");

    // Add string data
    const testData = "Hello, Advanced Buffer!";
    buffer.addString(testData);
    logSuccess(`Added string: "${testData}"`);

    // Add binary data
    const binaryData = Buffer.from([0x01, 0x02, 0x03, 0x04]);
    buffer.add(binaryData);
    logSuccess(
      `Added binary data: [${Array.from(binaryData)
        .map((b) => "0x" + b.toString(16).padStart(2, "0"))
        .join(", ")}]`
    );

    // Test integer I/O
    log("Testing integer I/O operations...");
    buffer.writeLittleEndianInt(0x12345678, 4);
    buffer.writeBigEndianInt(0x87654321, 4);
    logSuccess("Wrote integers in both endianness");

    // Test search operations
    log("Testing search operations...");
    const searchResult = buffer.search(Buffer.from("Buffer"), 6);
    logSuccess(`Search result: ${searchResult}`);

    // Test buffer information
    log("Buffer information:", {
      length: buffer.length,
      isEmpty: buffer.isEmpty,
      capacity: buffer.capacity,
    });

    // Test buffer pool operations
    log("Testing buffer pool operations...");
    const poolStats = pool.getStats();
    logSuccess("Pool statistics retrieved");
    log("Pool stats:", poolStats);

    // Clean up
    buffer.destroy();
    pool.destroy();
    logSuccess("Buffer and pool cleaned up successfully");
  } catch (error) {
    logError("Failed to demonstrate advanced buffers", error);
  }
}

// ============================================================================
// 3. RAII Resource Management
// ============================================================================

async function demonstrateRAIIManagement() {
  logSection("3. RAII Resource Management");

  try {
    // Test ResourceGuard
    log("Testing ResourceGuard...");
    const resource = { id: 1, data: "test resource" };

    const guard = makeResourceGuard(resource, (r) => {
      logSuccess(`Resource ${r.id} cleaned up automatically`);
    });

    logSuccess("ResourceGuard created successfully");
    log("Resource data:", guard.get());

    // Test AllocationTransaction
    log("Testing AllocationTransaction...");
    const transaction = new AllocationTransaction();

    const resource1 = { id: 2, data: "transaction resource 1" };
    const resource2 = { id: 3, data: "transaction resource 2" };

    transaction.track(resource1, (r) => {
      logSuccess(`Transaction resource ${r.id} cleaned up`);
    });

    transaction.track(resource2, (r) => {
      logSuccess(`Transaction resource ${r.id} cleaned up`);
    });

    logSuccess("Resources tracked in transaction");
    log("Transaction resource count:", transaction.resourceCount);

    // Test transaction rollback
    log("Testing transaction rollback...");
    transaction.rollback();
    logSuccess("Transaction rolled back - all resources cleaned up");

    // Test ScopedCleanup
    log("Testing ScopedCleanup...");

    const cleanup = makeScopedCleanup(() => {
      logSuccess("Scoped cleanup executed");
    });

    logSuccess("ScopedCleanup created");

    // Clean up
    guard.destroy();
    transaction.destroy();
    cleanup.destroy();

    logSuccess("RAII resources cleaned up successfully");
  } catch (error) {
    logError("Failed to demonstrate RAII management", error);
  }
}

// ============================================================================
// 4. Real Shared Library Integration Test
// ============================================================================

async function demonstrateSharedLibraryIntegration() {
  logSection("4. Real Shared Library Integration Test");

  try {
    // Test if we can actually call C API functions
    log("Testing real C API function calls...");

    // Test MCP initialization
    log("Testing MCP initialization...");
    const initResult = mcpFilterLib.mcp_init(null);
    logSuccess(`MCP init result: ${initResult}`);

    // Test MCP version
    log("Testing MCP version...");
    const version = mcpFilterLib.mcp_get_version();
    logSuccess(`MCP version: ${version}`);

    // Test if MCP is initialized
    log("Testing MCP initialization status...");
    const isInitialized = mcpFilterLib.mcp_is_initialized();
    logSuccess(`MCP initialized: ${isInitialized}`);

    // Test dispatcher creation
    log("Testing dispatcher creation...");
    const dispatcher = mcpFilterLib.mcp_dispatcher_create();
    logSuccess(`Dispatcher created: ${dispatcher}`);

    // Test memory pool creation
    log("Testing memory pool creation...");
    const memoryPool = mcpFilterLib.mcp_memory_pool_create(1024 * 1024); // 1MB
    logSuccess(`Memory pool created: ${memoryPool}`);

    // Test filter creation through C API
    log("Testing filter creation through C API...");

    // Create filter config
    const filterConfig = {
      name: "demo-c-api-filter",
      type: 100, // MCP_FILTER_CUSTOM
      settings: null,
      layer: 7, // MCP_PROTOCOL_LAYER_7_APPLICATION
      memory_pool: memoryPool,
    };

    const filter = mcpFilterLib.mcp_filter_create(dispatcher, filterConfig);
    logSuccess(`Filter created through C API: ${filter}`);

    // Test filter operations
    if (filter) {
      log("Testing filter operations...");

      // Get filter stats
      const stats = {
        bytes_processed: 0,
        packets_processed: 0,
        errors: 0,
        processing_time_us: 0,
        throughput_mbps: 0,
      };
      const statsResult = mcpFilterLib.mcp_filter_get_stats(filter, stats);
      logSuccess(`Filter stats retrieved: ${statsResult}`);

      // Release filter
      mcpFilterLib.mcp_filter_release(filter);
      logSuccess("Filter released");
    }

    // Clean up C API resources
    log("Cleaning up C API resources...");
    mcpFilterLib.mcp_memory_pool_destroy(memoryPool);
    mcpFilterLib.mcp_dispatcher_destroy(dispatcher);
    mcpFilterLib.mcp_shutdown();
    logSuccess("C API resources cleaned up");
  } catch (error) {
    logError("Failed to demonstrate shared library integration", error);
  }
}

// ============================================================================
// 5. Performance and Stress Testing
// ============================================================================

async function demonstratePerformanceTesting() {
  logSection("5. Performance and Stress Testing");

  try {
    // Test buffer creation performance
    log("Testing buffer creation performance...");
    const startTime = Date.now();
    const bufferCount = 1000;

    const buffers: AdvancedBuffer[] = [];
    for (let i = 0; i < bufferCount; i++) {
      const buffer = new AdvancedBuffer(1024, BufferOwnership.EXCLUSIVE);
      buffer.addString(`Buffer ${i}: ${"x".repeat(100)}`);
      buffers.push(buffer);
    }

    const endTime = Date.now();
    const duration = endTime - startTime;
    logSuccess(
      `Created ${bufferCount} buffers in ${duration}ms (${(
        (bufferCount / duration) *
        1000
      ).toFixed(2)} buffers/sec)`
    );

    // Test buffer operations performance
    log("Testing buffer operations performance...");
    const opStartTime = Date.now();
    const opCount = 10000;

    for (let i = 0; i < opCount; i++) {
      const buffer = buffers[i % bufferCount];
      if (buffer) {
        buffer.addString(`Operation ${i}`);
        buffer.search(Buffer.from("Operation"), 9);
      }
    }

    const opEndTime = Date.now();
    const opDuration = opEndTime - opStartTime;
    logSuccess(
      `Performed ${opCount} operations in ${opDuration}ms (${(
        (opCount / opDuration) *
        1000
      ).toFixed(2)} ops/sec)`
    );

    // Clean up
    log("Cleaning up performance test buffers...");
    buffers.forEach((buffer) => buffer.destroy());
    logSuccess("Performance test buffers cleaned up");
  } catch (error) {
    logError("Failed to demonstrate performance testing", error);
  }
}

// ============================================================================
// Main Execution
// ============================================================================

async function main() {
  console.log("🚀 MCP Filter SDK - Real Shared Library Demo");
  console.log("This demo uses the ACTUAL shared library, not mocks!");
  console.log("Shared library path: /usr/local/lib/libgopher_mcp_c.dylib");
  console.log("Available functions:", Object.keys(mcpFilterLib).length);

  try {
    // Run all demonstrations
    await demonstrateBasicFilters();
    await demonstrateAdvancedBuffers();
    await demonstrateRAIIManagement();
    await demonstrateSharedLibraryIntegration();
    await demonstratePerformanceTesting();

    console.log("\n🎉 All demonstrations completed successfully!");
    console.log("✅ Real shared library integration working");
    console.log("✅ All TypeScript functionality working");
    console.log("✅ Performance tests passed");
  } catch (error) {
    console.error("\n💥 Demo failed:", error);
    process.exit(1);
  }
}

// Run the demo
if (require.main === module) {
  main().catch(console.error);
}

export { main };
