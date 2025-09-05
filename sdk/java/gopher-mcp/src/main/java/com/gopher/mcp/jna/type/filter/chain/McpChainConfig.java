package com.gopher.mcp.jna.type.filter.chain;

import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;
import com.sun.jna.Pointer;

/**
 * JNA structure mapping for mcp_chain_config_t
 */
@FieldOrder({"name", "mode", "routing", "max_parallel", "buffer_size", "timeout_ms", "stop_on_error"})
public class McpChainConfig extends Structure {
    public String name;
    public int mode;           // mcp_chain_execution_mode_t
    public int routing;        // mcp_routing_strategy_t
    public int max_parallel;   // uint32_t
    public int buffer_size;    // uint32_t
    public int timeout_ms;     // uint32_t
    public byte stop_on_error; // mcp_bool_t

    public McpChainConfig() {
        super();
    }

    public McpChainConfig(Pointer p) {
        super(p);
        read();
    }

    public static class ByReference extends McpChainConfig implements Structure.ByReference {
    }

    public static class ByValue extends McpChainConfig implements Structure.ByValue {
    }
}
