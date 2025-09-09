package com.gopher.mcp.jna.type.filter;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_filter_callbacks_t */
@FieldOrder({"on_data", "on_write", "on_event", "on_metadata", "on_trailers", "user_data"})
public class McpFilterCallbacks extends Structure {
  public Pointer on_data;
  public Pointer on_write;
  public Pointer on_event;
  public Pointer on_metadata;
  public Pointer on_trailers;
  public Pointer user_data;

  public McpFilterCallbacks() {
    super();
  }

  public McpFilterCallbacks(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpFilterCallbacks implements Structure.ByReference {}

  public static class ByValue extends McpFilterCallbacks implements Structure.ByValue {}
}
