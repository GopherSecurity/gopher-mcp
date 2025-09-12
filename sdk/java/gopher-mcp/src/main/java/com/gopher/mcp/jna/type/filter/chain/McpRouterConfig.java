package com.gopher.mcp.jna.type.filter.chain;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_router_config_t */
@FieldOrder({"strategy", "hash_seed", "route_table", "custom_router_data"})
public class McpRouterConfig extends Structure {
  public int strategy; // mcp_routing_strategy_t
  public int hash_seed; // uint32_t
  public Pointer route_table; // mcp_map_t
  public Pointer custom_router_data;

  public McpRouterConfig() {
    super();
  }

  public McpRouterConfig(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpRouterConfig implements Structure.ByReference {}

  public static class ByValue extends McpRouterConfig implements Structure.ByValue {}
}
