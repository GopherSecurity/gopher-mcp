package com.gopher.mcp.jna.type.filter.buffer;

import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;
import com.sun.jna.Pointer;

/**
 * JNA structure mapping for mcp_buffer_reservation_t
 */
@FieldOrder({"data", "capacity", "buffer", "reservation_id"})
public class McpBufferReservation extends Structure {
    public Pointer data;
    public long capacity;  // size_t
    public long buffer;    // mcp_buffer_handle_t
    public long reservation_id;  // uint64_t

    public McpBufferReservation() {
        super();
    }

    public McpBufferReservation(Pointer p) {
        super(p);
        read();
    }

    public static class ByReference extends McpBufferReservation implements Structure.ByReference {
    }

    public static class ByValue extends McpBufferReservation implements Structure.ByValue {
    }
}
