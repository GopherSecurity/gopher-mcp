/**
 * Integration tests for FFI dispatcher and connection bindings
 * These tests require the C++ library to be built and available
 */

import { existsSync } from 'fs';
import { TransportType } from '../mcp-ffi-bindings';

describe('FFI Bindings - Integration Tests (Real Library)', () => {
  let libraryAvailable = false;
  let lib: any = null;  // Declare lib at describe scope

  beforeAll(() => {
    try {
      // Attempt to load real library
      const koffi = require('koffi');
      const libPath = process.env['MCP_LIB_PATH'] ||
        '../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib';

      // Check multiple possible paths
      const searchPaths = [
        libPath,
        './build/libgopher_mcp_c.so',
        './build/libgopher_mcp_c.dylib',
        './build/gopher_mcp_c.dll',
        '../../build/src/c_api/libgopher_mcp_c.so',
        '../../build/src/c_api/libgopher_mcp_c.dylib',
        '../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib',
        '../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib',
      ];

      let foundPath = '';
      for (const path of searchPaths) {
        if (existsSync(path)) {
          foundPath = path;
          break;
        }
      }

      if (foundPath) {
        lib = koffi.load(foundPath);  // Assign to outer scope variable

        // Bind the dispatcher functions
        lib.mcp_dispatcher_create = lib.func('mcp_dispatcher_create', 'pointer', []);
        lib.mcp_dispatcher_run = lib.func('mcp_dispatcher_run', 'int', ['pointer']);
        lib.mcp_dispatcher_run_timeout = lib.func('mcp_dispatcher_run_timeout', 'int', ['pointer', 'int']);
        lib.mcp_dispatcher_stop = lib.func('mcp_dispatcher_stop', 'void', ['pointer']);
        lib.mcp_dispatcher_destroy = lib.func('mcp_dispatcher_destroy', 'void', ['pointer']);

        // Bind the connection functions
        lib.mcp_connection_create_client = lib.func('mcp_connection_create_client', 'pointer', ['pointer', 'int']);
        lib.mcp_connection_configure = lib.func('mcp_connection_configure', 'int', ['pointer', 'pointer', 'pointer', 'pointer']);
        lib.mcp_connection_destroy = lib.func('mcp_connection_destroy', 'void', ['pointer']);

        libraryAvailable = true;
        console.log(`✅ Native library loaded successfully from: ${foundPath}`);
      } else {
        console.log('⚠️  Native library not found - skipping integration tests');
        console.log('   Searched paths:', searchPaths);
      }
    } catch (error: any) {
      console.log(`⚠️  Cannot load native library: ${error.message}`);
      console.log('   Build with "make build" to enable integration tests');
    }
  });

  // Use conditional test/test.skip pattern instead of non-existent it.skipIf
  const conditionalTest = libraryAvailable ? test : test.skip;

  conditionalTest('should load native library symbols', () => {
    // Test that all 8 new functions are available
    const functions = [
      'mcp_dispatcher_create',
      'mcp_dispatcher_run',
      'mcp_dispatcher_run_timeout',
      'mcp_dispatcher_stop',
      'mcp_dispatcher_destroy',
      'mcp_connection_create_client',
      'mcp_connection_configure',
      'mcp_connection_destroy'
    ];

    for (const fn of functions) {
      expect(lib[fn]).toBeDefined();
      expect(typeof lib[fn]).toBe('function');
    }
  });

  conditionalTest('should create and destroy dispatcher', () => {
    const dispatcher = lib.mcp_dispatcher_create();
    expect(dispatcher).toBeTruthy();
    expect(dispatcher).not.toBe(0);

    // Log the handle value for debugging
    console.log(`Created dispatcher handle: 0x${dispatcher.toString(16)}`);

    lib.mcp_dispatcher_destroy(dispatcher);
    // Should not crash
  });

  conditionalTest('should create and destroy connection', () => {
    // First create a dispatcher
    const dispatcher = lib.mcp_dispatcher_create();
    expect(dispatcher).toBeTruthy();

    // Create a client connection with HTTP_SSE transport
    const connection = lib.mcp_connection_create_client(
      dispatcher,
      TransportType.MCP_TRANSPORT_HTTP_SSE
    );
    expect(connection).toBeTruthy();
    expect(connection).not.toBe(0);

    console.log(`Created connection handle: 0x${connection.toString(16)}`);

    // Clean up
    lib.mcp_connection_destroy(connection);
    lib.mcp_dispatcher_destroy(dispatcher);
  });

  conditionalTest('should configure connection', () => {
    const dispatcher = lib.mcp_dispatcher_create();
    const connection = lib.mcp_connection_create_client(
      dispatcher,
      TransportType.MCP_TRANSPORT_HTTP_SSE
    );

    // Configure with null parameters for now
    const result = lib.mcp_connection_configure(connection, null, null, null);
    expect(result).toBe(0); // MCP_OK

    lib.mcp_connection_destroy(connection);
    lib.mcp_dispatcher_destroy(dispatcher);
  });

  conditionalTest('should run dispatcher with timeout', () => {
    const dispatcher = lib.mcp_dispatcher_create();

    // Run with 10ms timeout (should return immediately)
    const result = lib.mcp_dispatcher_run_timeout(dispatcher, 10);

    // Result should be 0 (MCP_OK) or timeout code
    expect(result).toBeGreaterThanOrEqual(0);

    lib.mcp_dispatcher_destroy(dispatcher);
  });

  conditionalTest('should handle multiple dispatcher instances', () => {
    const dispatcher1 = lib.mcp_dispatcher_create();
    const dispatcher2 = lib.mcp_dispatcher_create();

    expect(dispatcher1).toBeTruthy();
    expect(dispatcher2).toBeTruthy();
    expect(dispatcher1).not.toBe(dispatcher2);

    lib.mcp_dispatcher_destroy(dispatcher1);
    lib.mcp_dispatcher_destroy(dispatcher2);
  });

  conditionalTest('should create connections with different transport types', () => {
    const dispatcher = lib.mcp_dispatcher_create();

    // Test creating connections with different transport types
    const httpConnection = lib.mcp_connection_create_client(
      dispatcher,
      TransportType.MCP_TRANSPORT_HTTP_SSE
    );
    expect(httpConnection).toBeTruthy();

    const stdioConnection = lib.mcp_connection_create_client(
      dispatcher,
      TransportType.MCP_TRANSPORT_STDIO
    );
    expect(stdioConnection).toBeTruthy();

    const pipeConnection = lib.mcp_connection_create_client(
      dispatcher,
      TransportType.MCP_TRANSPORT_PIPE
    );
    expect(pipeConnection).toBeTruthy();

    // Clean up all connections
    lib.mcp_connection_destroy(httpConnection);
    lib.mcp_connection_destroy(stdioConnection);
    lib.mcp_connection_destroy(pipeConnection);
    lib.mcp_dispatcher_destroy(dispatcher);
  });

  test('should gracefully skip when library is missing', () => {
    if (libraryAvailable) {
      console.log('Library available - test not applicable');
      return;
    }

    // Ensure the system doesn't crash when library is missing
    expect(() => {
      require('../mcp-ffi-bindings');
    }).not.toThrow(); // Should handle missing library gracefully
  });

  conditionalTest('should verify library exports all expected functions', () => {
    // Try to import the actual module
    const { mcpFilterLib } = require('../mcp-ffi-bindings');

    // Verify the new functions exist
    expect(mcpFilterLib.mcp_dispatcher_create).toBeDefined();
    expect(mcpFilterLib.mcp_dispatcher_run).toBeDefined();
    expect(mcpFilterLib.mcp_dispatcher_run_timeout).toBeDefined();
    expect(mcpFilterLib.mcp_dispatcher_stop).toBeDefined();
    expect(mcpFilterLib.mcp_dispatcher_destroy).toBeDefined();
    expect(mcpFilterLib.mcp_connection_create_client).toBeDefined();
    expect(mcpFilterLib.mcp_connection_configure).toBeDefined();
    expect(mcpFilterLib.mcp_connection_destroy).toBeDefined();
  });
});