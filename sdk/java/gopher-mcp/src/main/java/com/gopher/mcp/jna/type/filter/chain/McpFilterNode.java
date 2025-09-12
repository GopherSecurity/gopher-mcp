package com.gopher.mcp.jna.type.filter.chain;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_filter_node_t */
@FieldOrder({"filter", "name", "priority", "enabled", "bypass_on_error", "config"})
public class McpFilterNode extends Structure {
  public Pointer filter; // mcp_filter_t
  public String name;
  public int priority; // uint32_t
  public byte enabled; // mcp_bool_t
  public byte bypass_on_error; // mcp_bool_t
  public Pointer config; // mcp_json_value_t

  public McpFilterNode() {
    super();
  }

  public McpFilterNode(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpFilterNode implements Structure.ByReference {}

  public static class ByValue extends McpFilterNode implements Structure.ByValue {}
}
