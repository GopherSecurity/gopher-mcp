package com.gopher.mcp.jna.type.filter.chain;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_filter_condition_t */
@FieldOrder({"match_type", "field", "value", "target_filter"})
public class McpFilterCondition extends Structure {
  public int match_type; // mcp_match_condition_t
  public String field;
  public String value;
  public Pointer target_filter; // mcp_filter_t

  public McpFilterCondition() {
    super();
  }

  public McpFilterCondition(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpFilterCondition implements Structure.ByReference {}

  public static class ByValue extends McpFilterCondition implements Structure.ByValue {}
}
