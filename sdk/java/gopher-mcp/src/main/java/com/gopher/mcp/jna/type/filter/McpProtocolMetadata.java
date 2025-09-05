package com.gopher.mcp.jna.type.filter;

import com.sun.jna.Structure;
import com.sun.jna.Structure.FieldOrder;
import com.sun.jna.Union;
import com.sun.jna.Pointer;

/**
 * JNA structure mapping for mcp_protocol_metadata_t
 */
@FieldOrder({"layer", "data"})
public class McpProtocolMetadata extends Structure {
    public int layer;  // mcp_protocol_layer_t
    public DataUnion data;

    public static class DataUnion extends Union {
        public L3Data l3;
        public L4Data l4;
        public L5Data l5;
        public L7Data l7;

        @FieldOrder({"src_ip", "dst_ip", "protocol", "ttl"})
        public static class L3Data extends Structure {
            public int src_ip;     // uint32_t
            public int dst_ip;     // uint32_t
            public byte protocol;  // uint8_t
            public byte ttl;       // uint8_t
        }

        @FieldOrder({"src_port", "dst_port", "protocol", "sequence_num"})
        public static class L4Data extends Structure {
            public short src_port;    // uint16_t
            public short dst_port;    // uint16_t
            public int protocol;      // mcp_transport_protocol_t
            public int sequence_num;  // uint32_t
        }

        @FieldOrder({"is_tls", "alpn", "sni", "session_id"})
        public static class L5Data extends Structure {
            public byte is_tls;       // mcp_bool_t
            public String alpn;       // const char*
            public String sni;        // const char*
            public int session_id;    // uint32_t
        }

        @FieldOrder({"protocol", "headers", "method", "path", "status_code"})
        public static class L7Data extends Structure {
            public int protocol;        // mcp_app_protocol_t
            public Pointer headers;     // mcp_map_t
            public String method;       // const char*
            public String path;         // const char*
            public int status_code;     // uint32_t
        }
    }

    public McpProtocolMetadata() {
        super();
    }

    public McpProtocolMetadata(Pointer p) {
        super(p);
        read();
    }

    public static class ByReference extends McpProtocolMetadata implements Structure.ByReference {
    }

    public static class ByValue extends McpProtocolMetadata implements Structure.ByValue {
    }
}
