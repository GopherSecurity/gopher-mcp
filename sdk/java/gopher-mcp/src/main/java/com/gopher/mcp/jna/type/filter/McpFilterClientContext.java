package com.gopher.mcp.jna.type.filter;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_filter_client_context_t */
@FieldOrder({"client", "request_filters", "response_filters"})
public class McpFilterClientContext extends Structure {
  public Pointer client; // mcp_client_t
  public long request_filters; // mcp_filter_chain_t (uint64_t)
  public long response_filters; // mcp_filter_chain_t (uint64_t)

  public McpFilterClientContext() {
    super();
  }

  public McpFilterClientContext(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpFilterClientContext implements Structure.ByReference {}

  public static class ByValue extends McpFilterClientContext implements Structure.ByValue {}
}
