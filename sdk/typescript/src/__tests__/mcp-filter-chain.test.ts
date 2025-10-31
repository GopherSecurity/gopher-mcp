/**
 * @file mcp-filter-chain.test.ts
 * @brief Lightweight unit tests for the TypeScript filter chain helpers
 */

import {
  CanonicalConfig,
  ChainExecutionMode,
  ChainState,
  createFilterChainFromConfig,
  createSimpleChain,
  getChainState,
  pauseChain,
  resetChain,
  resumeChain,
  RoutingStrategy,
} from "../mcp-filter-chain";

import { mcpFilterLib } from "../mcp-ffi-bindings";

jest.mock("../mcp-ffi-bindings", () => {
  const mockJsonHandle = { __jsonHandle: true } as const;
  return {
    mcpFilterLib: {
      mcp_json_parse: jest.fn(() => mockJsonHandle),
      mcp_json_free: jest.fn(),
      mcp_chain_create_from_json: jest.fn(() => 101),
      mcp_chain_get_state: jest.fn(() => ChainState.IDLE),
      mcp_chain_pause: jest.fn(() => 0),
      mcp_chain_resume: jest.fn(() => 0),
      mcp_chain_reset: jest.fn(() => 0),
    },
  };
});

type MockedFilterLib = jest.Mocked<typeof mcpFilterLib>;
const mockedLib = mcpFilterLib as MockedFilterLib;

const SAMPLE_CONFIG: CanonicalConfig = {
  listeners: [
    {
      name: "listener_one",
      address: {
        socket_address: {
          address: "127.0.0.1",
          port_value: 8080,
        },
      },
      filter_chains: [
        {
          filters: [
            { name: "http", type: "http.codec" },
            { name: "json", type: "json_rpc.dispatcher" },
          ],
        },
      ],
    },
  ],
};

describe("Filter chain helpers", () => {
  beforeEach(() => {
    jest.clearAllMocks();
  });

  it("creates a chain from canonical config", () => {
    const handle = createFilterChainFromConfig(7, SAMPLE_CONFIG);

    expect(handle).toBe(101);
    expect(mockedLib.mcp_json_parse).toHaveBeenCalledTimes(1);
    expect(mockedLib.mcp_chain_create_from_json).toHaveBeenCalledWith(
      7,
      expect.any(Object)
    );
    expect(mockedLib.mcp_json_free).toHaveBeenCalledTimes(1);
  });

  it("throws for non-canonical configuration", () => {
    expect(() => createFilterChainFromConfig(0, {} as CanonicalConfig)).toThrow(
      /canonical format/
    );
  });

  it("proxies chain state helpers to the FFI bindings", () => {
    const chain = 42;

    expect(getChainState(chain)).toBe(ChainState.IDLE);
    expect(mockedLib.mcp_chain_get_state).toHaveBeenCalledWith(chain);

    expect(pauseChain(chain)).toBe(0);
    expect(mockedLib.mcp_chain_pause).toHaveBeenCalledWith(chain);

    expect(resumeChain(chain)).toBe(0);
    expect(mockedLib.mcp_chain_resume).toHaveBeenCalledWith(chain);

    expect(resetChain(chain)).toBe(0);
    expect(mockedLib.mcp_chain_reset).toHaveBeenCalledWith(chain);
  });

  it("builds a simple chain from filter type names", () => {
    const handle = createSimpleChain(11, ["http.codec", "json_rpc.dispatcher"], "sample");

    expect(handle).toBe(101);
    expect(mockedLib.mcp_json_parse).toHaveBeenCalled();

    const jsonArg = mockedLib.mcp_json_parse.mock.calls[0][0] as string;
    expect(jsonArg).toContain("http.codec");
    expect(jsonArg).toContain("json_rpc.dispatcher");
  });

  it("exposes enum values for routing and execution modes", () => {
    expect(ChainExecutionMode.SEQUENTIAL).toBe(0);
    expect(ChainExecutionMode.PARALLEL).toBe(1);
    expect(RoutingStrategy.ROUND_ROBIN).toBe(0);
    expect(RoutingStrategy.CUSTOM).toBe(99);
  });
});
