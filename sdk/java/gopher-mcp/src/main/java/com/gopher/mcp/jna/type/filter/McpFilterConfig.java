package com.gopher.mcp.jna.type.filter;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_filter_config_t */
@FieldOrder({"name", "filter_type", "config_json", "layer"})
public class McpFilterConfig extends Structure {
  public String name;
  public int filter_type; // mcp_filter_type_t
  public Pointer config_json; // mcp_json_value_t
  public int layer; // mcp_filter_layer_t

  public McpFilterConfig() {
    super();
  }

  public McpFilterConfig(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpFilterConfig implements Structure.ByReference {}

  public static class ByValue extends McpFilterConfig implements Structure.ByValue {}
}
