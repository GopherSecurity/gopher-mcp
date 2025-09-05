package com.gopher.mcp.jna.type.filter;

import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;
import com.sun.jna.Pointer;

/**
 * JNA structure mapping for mcp_buffer_slice_t
 */
@FieldOrder({"data", "size", "flags"})
public class McpBufferSlice extends Structure {
    public Pointer data;
    public long size;  // size_t
    public int flags;   // uint32_t

    public McpBufferSlice() {
        super();
    }

    public McpBufferSlice(Pointer p) {
        super(p);
        read();
    }

    public static class ByReference extends McpBufferSlice implements Structure.ByReference {
    }

    public static class ByValue extends McpBufferSlice implements Structure.ByValue {
    }
}
