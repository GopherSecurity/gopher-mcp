package com.gopher.mcp.jna.type.filter.buffer;

import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;
import com.sun.jna.Pointer;
import com.sun.jna.Callback;

/**
 * JNA structure mapping for mcp_buffer_fragment_t
 */
@FieldOrder({"data", "size", "release_callback", "user_data"})
public class McpBufferFragment extends Structure {
    public Pointer data;
    public long size;  // size_t
    public ReleaseCallback release_callback;
    public Pointer user_data;

    public interface ReleaseCallback extends Callback {
        void invoke(Pointer data, long size, Pointer user_data);
    }

    public McpBufferFragment() {
        super();
    }

    public McpBufferFragment(Pointer p) {
        super(p);
        read();
    }

    public static class ByReference extends McpBufferFragment implements Structure.ByReference {
    }

    public static class ByValue extends McpBufferFragment implements Structure.ByValue {
    }
}
