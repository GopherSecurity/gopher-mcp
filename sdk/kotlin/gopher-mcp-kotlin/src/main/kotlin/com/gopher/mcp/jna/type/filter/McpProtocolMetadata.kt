package com.gopher.mcp.jna.type.filter

import com.sun.jna.Pointer
import com.sun.jna.Structure
import com.sun.jna.Structure.FieldOrder
import com.sun.jna.Union

/**
 * JNA structure mapping for mcp_protocol_metadata_t
 */
@FieldOrder("layer", "data")
open class McpProtocolMetadata : Structure {
    @JvmField var layer: Int = 0 // mcp_protocol_layer_t
    @JvmField var data: DataUnion? = null

    open class DataUnion : Union() {
        @JvmField var l3: L3Data = L3Data()
        @JvmField var l4: L4Data = L4Data()
        @JvmField var l5: L5Data = L5Data()
        @JvmField var l7: L7Data = L7Data()

        @FieldOrder("src_ip", "dst_ip", "protocol", "ttl")
        open class L3Data : Structure() {
            @JvmField var src_ip: Int = 0 // uint32_t
            @JvmField var dst_ip: Int = 0 // uint32_t
            @JvmField var protocol: Byte = 0 // uint8_t
            @JvmField var ttl: Byte = 0 // uint8_t
        }

        @FieldOrder("src_port", "dst_port", "protocol", "sequence_num")
        open class L4Data : Structure() {
            @JvmField var src_port: Short = 0 // uint16_t
            @JvmField var dst_port: Short = 0 // uint16_t
            @JvmField var protocol: Int = 0 // mcp_transport_protocol_t
            @JvmField var sequence_num: Int = 0 // uint32_t
        }

        @FieldOrder("is_tls", "alpn", "sni", "session_id")
        open class L5Data : Structure() {
            @JvmField var is_tls: Byte = 0 // mcp_bool_t
            @JvmField var alpn: String? = null // const char*
            @JvmField var sni: String? = null // const char*
            @JvmField var session_id: Int = 0 // uint32_t
        }

        @FieldOrder("protocol", "headers", "method", "path", "status_code")
        open class L7Data : Structure() {
            @JvmField var protocol: Int = 0 // mcp_app_protocol_t
            @JvmField var headers: Pointer? = null // mcp_map_t
            @JvmField var method: String? = null // const char*
            @JvmField var path: String? = null // const char*
            @JvmField var status_code: Int = 0 // uint32_t
        }
    }

    constructor() : super()

    constructor(p: Pointer) : super(p) {
        read()
    }

    class ByReference : McpProtocolMetadata(), Structure.ByReference
    class ByValue : McpProtocolMetadata(), Structure.ByValue
}
