package com.gopher.mcp.jna.type.filter.buffer;

import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;
import com.sun.jna.Pointer;

/**
 * JNA structure mapping for mcp_buffer_stats_t
 */
@FieldOrder({"total_bytes", "used_bytes", "slice_count", "fragment_count", "read_operations", "write_operations"})
public class McpBufferStats extends Structure {
    public long total_bytes;      // size_t
    public long used_bytes;       // size_t
    public long slice_count;      // size_t
    public long fragment_count;   // size_t
    public long read_operations;  // uint64_t
    public long write_operations; // uint64_t

    public McpBufferStats() {
        super();
    }

    public McpBufferStats(Pointer p) {
        super(p);
        read();
    }

    public static class ByReference extends McpBufferStats implements Structure.ByReference {
    }

    public static class ByValue extends McpBufferStats implements Structure.ByValue {
    }
}
