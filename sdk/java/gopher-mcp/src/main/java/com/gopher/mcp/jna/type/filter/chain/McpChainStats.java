package com.gopher.mcp.jna.type.filter.chain;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/** JNA structure mapping for mcp_chain_stats_t */
@FieldOrder({
  "total_processed",
  "total_errors",
  "total_bypassed",
  "avg_latency_ms",
  "max_latency_ms",
  "throughput_mbps",
  "active_filters"
})
public class McpChainStats extends Structure {
  public long total_processed; // uint64_t
  public long total_errors; // uint64_t
  public long total_bypassed; // uint64_t
  public double avg_latency_ms;
  public double max_latency_ms;
  public double throughput_mbps;
  public int active_filters; // uint32_t

  public McpChainStats() {
    super();
  }

  public McpChainStats(Pointer p) {
    super(p);
    read();
  }

  public static class ByReference extends McpChainStats implements Structure.ByReference {}

  public static class ByValue extends McpChainStats implements Structure.ByValue {}
}
