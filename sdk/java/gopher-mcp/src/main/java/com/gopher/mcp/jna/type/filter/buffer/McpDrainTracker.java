package com.gopher.mcp.jna.type.filter.buffer;

import com.sun.jna.Callback;
import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;

/**
 * JNA structure for mcp_drain_tracker_t from mcp_c_filter_buffer.h.
 * Drain tracker for monitoring buffer consumption.
 */
@FieldOrder({"callback", "user_data"})
public class McpDrainTracker extends Structure {

    /**
     * Drain tracker callback interface.
     * Called when bytes are drained from the buffer.
     */
    public interface MCP_DRAIN_TRACKER_CB extends Callback {
        void invoke(long bytes_drained, Pointer user_data);
    }

    /** Callback function */
    public MCP_DRAIN_TRACKER_CB callback;

    /** User data for callback */
    public Pointer user_data;

    public static class ByReference extends McpDrainTracker implements Structure.ByReference {}
    public static class ByValue extends McpDrainTracker implements Structure.ByValue {}
}
