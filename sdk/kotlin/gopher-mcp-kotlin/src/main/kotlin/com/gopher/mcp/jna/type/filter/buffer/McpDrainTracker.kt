package com.gopher.mcp.jna.type.filter.buffer

import com.sun.jna.Callback
import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder

/**
 * JNA structure for mcp_drain_tracker_t from mcp_c_filter_buffer.h.
 * Drain tracker for monitoring buffer consumption.
 */
@FieldOrder("callback", "user_data")
open class McpDrainTracker : Structure() {

    /**
     * Drain tracker callback interface.
     * Called when bytes are drained from the buffer.
     */
    interface MCP_DRAIN_TRACKER_CB : Callback {
        fun invoke(bytes_drained: Long, user_data: Pointer?)
    }

    /** Callback function */
    @JvmField var callback: MCP_DRAIN_TRACKER_CB? = null

    /** User data for callback */
    @JvmField var user_data: Pointer? = null

    class ByReference : McpDrainTracker(), Structure.ByReference
    class ByValue : McpDrainTracker(), Structure.ByValue
}
