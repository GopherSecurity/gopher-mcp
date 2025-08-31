#!/usr/bin/env node

/**
 * @file integration-test.ts
 * @brief Integration test using the REAL shared library
 * 
 * This test focuses on testing actual C API calls and verifying
 * that our TypeScript wrappers work correctly with the real library.
 */

import { mcpFilterLib } from "../src";

// ============================================================================
// Test Results Tracking
// ============================================================================

interface TestResult {
  name: string;
  passed: boolean;
  error?: string;
  data?: any;
}

const testResults: TestResult[] = [];

function runTest(name: string, testFn: (...args: any[]) => any, ...args: any[]): TestResult {
  console.log(`🧪 Running test: ${name}`);
  
  try {
    const result = testFn(...args);
    const testResult: TestResult = {
      name,
      passed: true,
      data: result
    };
    
    console.log(`✅ ${name} - PASSED`);
    if (result !== undefined) {
      console.log(`   Result:`, result);
    }
    
    testResults.push(testResult);
    return testResult;
    
  } catch (error) {
    const testResult: TestResult = {
      name,
      passed: false,
      error: error instanceof Error ? error.message : String(error)
    };
    
    console.log(`❌ ${name} - FAILED`);
    console.log(`   Error:`, testResult.error);
    
    testResults.push(testResult);
    return testResult;
  }
}

// ============================================================================
// Integration Tests
// ============================================================================

function testMCPInitialization(): number {
  console.log("   Testing MCP initialization...");
  const result = mcpFilterLib.mcp_init(null);
  
  if (result !== 0) {
    throw new Error(`MCP init failed with result: ${result}`);
  }
  
  return result;
}

function testMCPVersion(): string {
  console.log("   Testing MCP version retrieval...");
  const version = mcpFilterLib.mcp_get_version();
  
  if (!version || typeof version !== 'string') {
    throw new Error(`Invalid version returned: ${version}`);
  }
  
  return version;
}

function testMCPInitializationStatus(): boolean {
  console.log("   Testing MCP initialization status...");
  const isInitialized = mcpFilterLib.mcp_is_initialized();
  
  if (typeof isInitialized !== 'boolean' && typeof isInitialized !== 'number') {
    throw new Error(`Invalid initialization status: ${isInitialized}`);
  }
  
  return Boolean(isInitialized);
}

function testFilterCreation(): any {
  console.log("   Testing filter creation through C API...");
  
  // Create filter using builtin type
  const filter = mcpFilterLib.mcp_filter_create_builtin(
    0, // dispatcher (0 for now)
    0, // MCP_FILTER_TCP_PROXY
    null // config
  );
  
  if (!filter) {
    throw new Error("Filter creation failed");
  }
  
  return filter;
}





function testFilterOperations(filter: any): any {
  console.log("   Testing filter operations...");
  
  // Test getting filter stats
  const stats = {
    bytes_processed: 0,
    packets_processed: 0,
    errors: 0,
    processing_time_us: 0,
    throughput_mbps: 0
  };
  
  const statsResult = mcpFilterLib.mcp_filter_get_stats(filter, stats);
  
  if (statsResult !== 0) {
    throw new Error(`Filter stats retrieval failed with result: ${statsResult}`);
  }
  
  return stats;
}

function testBufferCreation(): any {
  console.log("   Testing buffer creation...");
  const buffer = mcpFilterLib.mcp_buffer_create_owned(1024, 2); // 1KB, exclusive ownership
  
  if (!buffer) {
    throw new Error("Buffer creation failed");
  }
  
  return buffer;
}

function testBufferOperations(buffer: any): any {
  console.log("   Testing buffer operations...");
  
  // Test buffer length
  const length = mcpFilterLib.mcp_buffer_length(buffer);
  
  if (typeof length !== 'number') {
    throw new Error(`Invalid buffer length: ${length}`);
  }
  
  // Test buffer capacity
  const capacity = mcpFilterLib.mcp_buffer_capacity(buffer);
  
  if (typeof capacity !== 'number') {
    throw new Error(`Invalid buffer capacity: ${capacity}`);
  }
  
  return { length, capacity };
}

function testBufferPoolCreation(): any {
  console.log("   Testing buffer pool creation...");
  const pool = mcpFilterLib.mcp_buffer_pool_create(4096, 10); // 4KB buffers, max 10
  
  if (!pool) {
    throw new Error("Buffer pool creation failed");
  }
  
  return pool;
}

function testBufferPoolOperations(pool: any): any {
  console.log("   Testing buffer pool operations...");
  
  // Test acquiring buffer from pool
  const buffer = mcpFilterLib.mcp_buffer_pool_acquire(pool);
  
  if (!buffer) {
    throw new Error("Buffer acquisition from pool failed");
  }
  
  // Test releasing buffer back to pool
  mcpFilterLib.mcp_buffer_pool_release(pool, buffer);
  
  return buffer;
}

function testJSONOperations(): any {
  console.log("   Testing JSON operations...");
  
  // Test JSON creation
  const jsonObj = mcpFilterLib.mcp_json_create_object();
  
  if (!jsonObj) {
    throw new Error("JSON object creation failed");
  }
  
  // Test JSON string creation
  const jsonString = mcpFilterLib.mcp_json_create_string("test");
  
  if (!jsonString) {
    throw new Error("JSON string creation failed");
  }
  
  // Test JSON number creation
  const jsonNumber = mcpFilterLib.mcp_json_create_number(42);
  
  if (!jsonNumber) {
    throw new Error("JSON number creation failed");
  }
  
  // Test JSON boolean creation
  const jsonBool = mcpFilterLib.mcp_json_create_bool(1);
  
  if (!jsonBool) {
    throw new Error("JSON boolean creation failed");
  }
  
  // Test JSON null creation
  const jsonNull = mcpFilterLib.mcp_json_create_null();
  
  if (!jsonNull) {
    throw new Error("JSON null creation failed");
  }
  
  return { jsonObj, jsonString, jsonNumber, jsonBool, jsonNull };
}

// ============================================================================
// Main Test Execution
// ============================================================================

async function runIntegrationTests() {
  console.log("🚀 MCP Filter SDK - Integration Test Suite");
  console.log("Testing REAL shared library functionality");
  console.log("Shared library path: /usr/local/lib/libgopher_mcp_c.dylib");
  console.log("Available functions:", Object.keys(mcpFilterLib).length);
  console.log("");
  
  let filter: any = null;
  let buffer: any = null;
  let pool: any = null;
  let jsonObjects: any = null;
  
  try {
    // Test 1: MCP Initialization
    runTest("MCP Initialization", testMCPInitialization);
    
    // Test 2: MCP Version
    runTest("MCP Version", testMCPVersion);
    
    // Test 3: MCP Initialization Status
    runTest("MCP Initialization Status", testMCPInitializationStatus);
    
    // Test 4: Filter Creation
    filter = runTest("Filter Creation", testFilterCreation).data;
    
    // Test 7: Filter Operations
    runTest("Filter Operations", testFilterOperations, filter);
    
    // Test 8: Buffer Creation
    buffer = runTest("Buffer Creation", testBufferCreation).data;
    
    // Test 9: Buffer Operations
    runTest("Buffer Operations", testBufferOperations, buffer);
    
    // Test 10: Buffer Pool Creation
    pool = runTest("Buffer Pool Creation", testBufferPoolCreation).data;
    
    // Test 11: Buffer Pool Operations
    runTest("Buffer Pool Operations", testBufferPoolOperations, pool);
    
    // Test 12: JSON Operations
    jsonObjects = runTest("JSON Operations", testJSONOperations).data;
    
    console.log("\n" + "=".repeat(60));
    console.log("🧹 Cleaning up resources...");
    
    // Clean up resources
    if (filter) {
      mcpFilterLib.mcp_filter_release(filter);
      console.log("✅ Filter released");
    }
    
    if (buffer) {
      mcpFilterLib.mcp_filter_buffer_release(buffer);
      console.log("✅ Buffer released");
    }
    
    if (pool) {
      mcpFilterLib.mcp_buffer_pool_destroy(pool);
      console.log("✅ Buffer pool destroyed");
    }
    
    if (jsonObjects) {
      // Clean up JSON objects
      Object.values(jsonObjects).forEach((obj: any) => {
        if (obj) mcpFilterLib.mcp_json_free(obj);
      });
      console.log("✅ JSON objects freed");
    }
    
    // Test 13: MCP Shutdown
    runTest("MCP Shutdown", () => {
      mcpFilterLib.mcp_shutdown();
      return "Shutdown successful";
    });
    
  } catch (error) {
    console.error("💥 Integration test failed:", error);
  }
  
  // Print test summary
  console.log("\n" + "=".repeat(60));
  console.log("📊 TEST SUMMARY");
  console.log("=".repeat(60));
  
  const passedTests = testResults.filter(r => r.passed).length;
  const totalTests = testResults.length;
  
  testResults.forEach(result => {
    const status = result.passed ? "✅ PASS" : "❌ FAIL";
    console.log(`${status} ${result.name}`);
    if (!result.passed && result.error) {
      console.log(`    Error: ${result.error}`);
    }
  });
  
  console.log("\n" + "=".repeat(60));
  console.log(`🎯 RESULTS: ${passedTests}/${totalTests} tests passed`);
  
  if (passedTests === totalTests) {
    console.log("🎉 ALL TESTS PASSED! Integration with shared library successful!");
    return 0;
  } else {
    console.log("💥 Some tests failed. Check the errors above.");
    return 1;
  }
}

// Run the integration tests
if (require.main === module) {
  runIntegrationTests()
    .then(exitCode => process.exit(exitCode))
    .catch(error => {
      console.error("💥 Integration test suite failed:", error);
      process.exit(1);
    });
}

export { runIntegrationTests };
