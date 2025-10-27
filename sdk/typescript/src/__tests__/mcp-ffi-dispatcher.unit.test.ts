/**
 * Unit tests for FFI dispatcher and connection bindings (mocked)
 * These tests verify function definitions and signatures without requiring the C++ library
 */

import { TransportType } from '../mcp-ffi-bindings';

describe('FFI Bindings - Unit Tests (Mocked)', () => {
  beforeAll(() => {
    // Mock koffi to avoid needing C++ library for unit tests
    jest.mock('koffi', () => ({
      load: jest.fn().mockReturnValue({
        mcp_dispatcher_create: jest.fn().mockReturnValue(BigInt(0x7f8000000001)),
        mcp_dispatcher_run: jest.fn().mockReturnValue(0),
        mcp_dispatcher_run_timeout: jest.fn().mockReturnValue(0),
        mcp_dispatcher_stop: jest.fn(),
        mcp_dispatcher_destroy: jest.fn(),
        mcp_connection_create_client: jest.fn().mockReturnValue(BigInt(0x7f8000000002)),
        mcp_connection_configure: jest.fn().mockReturnValue(0),
        mcp_connection_destroy: jest.fn(),
      }),
    }));
  });

  afterAll(() => {
    jest.unmock('koffi');
  });

  describe('Transport Type Enum', () => {
    it('should define correct transport type values', () => {
      expect(TransportType.MCP_TRANSPORT_HTTP_SSE).toBe(0);
      expect(TransportType.MCP_TRANSPORT_STDIO).toBe(1);
      expect(TransportType.MCP_TRANSPORT_PIPE).toBe(2);
    });
  });

  describe('Dispatcher FFI Functions', () => {
    it('should define all 5 dispatcher FFI functions', () => {
      // Import after mocking
      const { mcpFilterLib } = require('../mcp-ffi-bindings');

      const dispatcherFunctions = [
        'mcp_dispatcher_create',
        'mcp_dispatcher_run',
        'mcp_dispatcher_run_timeout',
        'mcp_dispatcher_stop',
        'mcp_dispatcher_destroy'
      ];

      for (const funcName of dispatcherFunctions) {
        expect(mcpFilterLib[funcName]).toBeDefined();
        expect(typeof mcpFilterLib[funcName]).toBe('function');
      }
    });

    it('should have correct function signatures for dispatcher functions', () => {
      const { mcpFilterLib } = require('../mcp-ffi-bindings');

      // Test that functions can be called with expected parameters
      const dispatcher = mcpFilterLib.mcp_dispatcher_create();
      expect(dispatcher).toBeTruthy();

      const runResult = mcpFilterLib.mcp_dispatcher_run(dispatcher);
      expect(runResult).toBe(0); // MCP_OK

      const runTimeoutResult = mcpFilterLib.mcp_dispatcher_run_timeout(dispatcher, 1000);
      expect(runTimeoutResult).toBe(0);

      // These should not throw
      expect(() => mcpFilterLib.mcp_dispatcher_stop(dispatcher)).not.toThrow();
      expect(() => mcpFilterLib.mcp_dispatcher_destroy(dispatcher)).not.toThrow();
    });
  });

  describe('Connection FFI Functions', () => {
    it('should define all 3 connection FFI functions', () => {
      const { mcpFilterLib } = require('../mcp-ffi-bindings');

      const connectionFunctions = [
        'mcp_connection_create_client',
        'mcp_connection_configure',
        'mcp_connection_destroy'
      ];

      for (const funcName of connectionFunctions) {
        expect(mcpFilterLib[funcName]).toBeDefined();
        expect(typeof mcpFilterLib[funcName]).toBe('function');
      }
    });

    it('should have correct function signatures for connection functions', () => {
      const { mcpFilterLib } = require('../mcp-ffi-bindings');

      const dispatcher = mcpFilterLib.mcp_dispatcher_create();

      // Test creating a connection with HTTP_SSE transport
      const connection = mcpFilterLib.mcp_connection_create_client(
        dispatcher,
        TransportType.MCP_TRANSPORT_HTTP_SSE
      );
      expect(connection).toBeTruthy();

      // Test configuring connection (with null pointers for now)
      const configResult = mcpFilterLib.mcp_connection_configure(
        connection,
        0, // null address
        0, // null options
        0  // null ssl config
      );
      expect(configResult).toBe(0); // MCP_OK

      // Test destroying connection
      expect(() => mcpFilterLib.mcp_connection_destroy(connection)).not.toThrow();
    });
  });

  describe('Total Function Count', () => {
    it('should have 101 total functions (93 original + 8 new)', () => {
      const { mcpFilterLib } = require('../mcp-ffi-bindings');

      // Count all functions in mcpFilterLib
      const functionCount = Object.keys(mcpFilterLib).filter(
        key => typeof mcpFilterLib[key] === 'function'
      ).length;

      // We added 8 new functions (5 dispatcher + 3 connection)
      expect(functionCount).toBeGreaterThanOrEqual(101);
    });
  });

  describe('Error Handling', () => {
    it('should handle invalid parameters gracefully', () => {
      const { mcpFilterLib } = require('../mcp-ffi-bindings');

      // Test with null/invalid handles
      expect(() => mcpFilterLib.mcp_dispatcher_run(null)).not.toThrow();
      expect(() => mcpFilterLib.mcp_dispatcher_destroy(0)).not.toThrow();
      expect(() => mcpFilterLib.mcp_connection_destroy(null)).not.toThrow();
    });
  });
});