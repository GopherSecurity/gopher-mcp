/**
 * @file mcp-end-to-end.test.ts
 * @brief End-to-end style tests for the FilterManager using mocked FFI bindings
 */

import { FilterManager, JSONRPCMessage } from "../mcp-filter-manager";
import * as FilterApi from "../mcp-filter-api";
import * as BufferModule from "../mcp-filter-buffer";
import * as FilterChain from "../mcp-filter-chain";

jest.mock("../mcp-ffi-bindings", () => ({
  mcpFilterLib: {},
}));

jest.mock("../mcp-filter-api", () => {
  const FilterStatus = {
    CONTINUE: 0,
    STOP_ITERATION: 1,
    ERROR: -1,
  } as const;

  return {
    FilterStatus,
    createFilterManager: jest.fn(() => 9001),
    initializeFilterManager: jest.fn(() => FilterStatus.CONTINUE),
    addChainToManager: jest.fn(),
    postDataToFilter: jest.fn(() => FilterStatus.CONTINUE),
    releaseFilterChain: jest.fn(),
    releaseFilterManager: jest.fn(),
  };
});

jest.mock("../mcp-filter-buffer", () => {
  const encode = (value: unknown) => new TextEncoder().encode(JSON.stringify(value));
  const defaultBytes = encode({ jsonrpc: "2.0", result: "ok" });

  return {
    BufferOwnership: {
      NONE: 0,
      SHARED: 1,
      EXCLUSIVE: 2,
      EXTERNAL: 3,
    },
    createBufferPoolSimple: jest.fn(() => 6001),
    destroyBufferPool: jest.fn(),
    createBuffer: jest.fn(() => 7001),
    writeBufferData: jest.fn(),
    getBufferLength: jest.fn(() => defaultBytes.length),
    readBufferData: jest.fn(() => defaultBytes),
    releaseBuffer: jest.fn(),
  };
});

jest.mock("../mcp-filter-chain", () => ({
  createFilterChainFromConfig: jest.fn(() => 8001),
  ChainExecutionMode: { SEQUENTIAL: 0, PARALLEL: 1 },
  RoutingStrategy: { ROUND_ROBIN: 0, LEAST_LOADED: 1 },
}));

const filterApiMocks = FilterApi as jest.Mocked<typeof FilterApi>;
const bufferMocks = BufferModule as jest.Mocked<typeof BufferModule>;
const filterChainMocks = FilterChain as jest.Mocked<typeof FilterChain>;

const encodeMessage = (payload: JSONRPCMessage) =>
  new TextEncoder().encode(JSON.stringify(payload));

describe("FilterManager end-to-end behaviour", () => {
  let manager: FilterManager;

  beforeEach(() => {
    jest.clearAllMocks();

    // Default buffer mocks should return a successful JSON-RPC message
    const successPayload: JSONRPCMessage = {
      jsonrpc: "2.0",
      result: "Message processed",
    };
    const successBytes = encodeMessage(successPayload);

    bufferMocks.getBufferLength.mockImplementation(() => successBytes.length);
    bufferMocks.readBufferData.mockImplementation(() => successBytes);
    filterApiMocks.postDataToFilter.mockImplementation(
      () => FilterApi.FilterStatus.CONTINUE
    );

    manager = new FilterManager({
      mcp: { jsonRpcProtocol: true, sseCodec: true },
      observability: { metrics: true },
    });
  });

  afterEach(() => {
    if (manager) {
      manager.destroy();
    }
  });

  it("processes a single message through the mocked pipeline", async () => {
    const request: JSONRPCMessage = {
      jsonrpc: "2.0",
      id: 1,
      method: "tools/call",
      params: { name: "demo" },
    };

    const response = await manager.processMessage(request);

    expect(response.jsonrpc).toBe("2.0");
    expect(response.result).toBe("Message processed");

    expect(bufferMocks.createBuffer).toHaveBeenCalledTimes(1);
    expect(filterApiMocks.postDataToFilter).toHaveBeenCalledTimes(1);
  });

  it("produces per-message results when running a batch", async () => {
    const messages: JSONRPCMessage[] = [
      { jsonrpc: "2.0", id: 1, method: "one" },
      { jsonrpc: "2.0", id: 2, method: "two" },
    ];

    // Simulate a failure for the second message
    filterApiMocks.postDataToFilter
      .mockImplementationOnce(() => FilterApi.FilterStatus.CONTINUE)
      .mockImplementationOnce(() => FilterApi.FilterStatus.STOP_ITERATION);

    const results = await manager.processBatch(messages);

    expect(results).toHaveLength(2);
    expect(results[0]?.result).toBe("Message processed");
    expect(results[1]?.error).toBeDefined();
    expect(results[1]?.id).toBe(2);
  });

  it("rebuilds the filter chain when the configuration changes", async () => {
    await manager.updateConfig({
      http: { codec: true },
      security: { authentication: true },
    });

    expect(filterApiMocks.releaseFilterChain).toHaveBeenCalledTimes(1);
    expect(filterChainMocks.createFilterChainFromConfig).toHaveBeenCalledTimes(2);
    // Second call should use the updated configuration
    const lastCall =
      filterChainMocks.createFilterChainFromConfig.mock.calls[
        filterChainMocks.createFilterChainFromConfig.mock.calls.length - 1
      ];

    expect(lastCall).toBeDefined();
    expect(lastCall![1]).toMatchObject({
      listeners: expect.any(Array),
    });
  });

  it("throws when processing after destruction", async () => {
    manager.destroy();

    await expect(
      manager.processMessage({ jsonrpc: "2.0", method: "test" })
    ).rejects.toThrow(/destroyed/);
  });
});
