package com.gopher.mcp.jna.type.filter.buffer;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_buffer_pool_config_t */
@FieldOrder({"buffer_size", "max_buffers", "prealloc_count", "use_thread_local", "zero_on_alloc"})
public class McpBufferPoolConfig extends Structure {
  public long buffer_size; // size_t
  public long max_buffers; // size_t
  public long prealloc_count; // size_t
  public byte use_thread_local; // mcp_bool_t
  public byte zero_on_alloc; // mcp_bool_t

  public McpBufferPoolConfig() {
    super();
  }

  public McpBufferPoolConfig(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpBufferPoolConfig implements Structure.ByReference {}

  public static class ByValue extends McpBufferPoolConfig implements Structure.ByValue {}
}
