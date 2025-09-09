package com.gopher.mcp.jna.type.filter;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_filter_server_context_t */
@FieldOrder({"server", "request_filters", "response_filters"})
public class McpFilterServerContext extends Structure {
  public Pointer server; // mcp_server_t
  public long request_filters; // mcp_filter_chain_t (uint64_t)
  public long response_filters; // mcp_filter_chain_t (uint64_t)

  public McpFilterServerContext() {
    super();
  }

  public McpFilterServerContext(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpFilterServerContext implements Structure.ByReference {}

  public static class ByValue extends McpFilterServerContext implements Structure.ByValue {}
}
