package com.gopher.mcp.jna.type.filter;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_filter_stats_t */
@FieldOrder({
  "bytes_processed",
  "packets_processed",
  "errors",
  "processing_time_us",
  "throughput_mbps"
})
public class McpFilterStats extends Structure {
  public long bytes_processed;
  public long packets_processed;
  public long errors;
  public long processing_time_us;
  public double throughput_mbps;

  public McpFilterStats() {
    super();
  }

  public McpFilterStats(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpFilterStats implements Structure.ByReference {}

  public static class ByValue extends McpFilterStats implements Structure.ByValue {}
}
