package com.gopher.mcp.filter.type

import com.gopher.mcp.jna.McpFilterLibrary

/**
 * Protocol layers based on the OSI model.
 * Specifies which network layer a filter operates at.
 */
enum class ProtocolLayer(private val value: Int) {

    /** Layer 3: Network layer (IP). Handles routing and addressing. */
    NETWORK(McpFilterLibrary.MCP_PROTOCOL_LAYER_3_NETWORK),

    /** Layer 4: Transport layer (TCP/UDP). Handles end-to-end connections and reliability. */
    TRANSPORT(McpFilterLibrary.MCP_PROTOCOL_LAYER_4_TRANSPORT),

    /** Layer 5: Session layer. Handles session establishment and management. */
    SESSION(McpFilterLibrary.MCP_PROTOCOL_LAYER_5_SESSION),

    /** Layer 6: Presentation layer. Handles data formatting and encryption. */
    PRESENTATION(McpFilterLibrary.MCP_PROTOCOL_LAYER_6_PRESENTATION),

    /** Layer 7: Application layer (HTTP/HTTPS). Handles application-specific protocols. */
    APPLICATION(McpFilterLibrary.MCP_PROTOCOL_LAYER_7_APPLICATION);

    /**
     * Get the integer value for JNA calls
     *
     * @return The numeric value of this protocol layer
     */
    fun getValue(): Int = value

    /**
     * Get the OSI layer number
     *
     * @return The OSI model layer number
     */
    fun getLayerNumber(): Int = value

    /**
     * Check if this is a lower layer (Network or Transport)
     *
     * @return true if layer 3 or 4
     */
    fun isLowerLayer(): Boolean = value <= 4

    /**
     * Check if this is an upper layer (Session, Presentation, or Application)
     *
     * @return true if layer 5, 6, or 7
     */
    fun isUpperLayer(): Boolean = value >= 5

    companion object {
        /**
         * Convert from integer value to enum
         *
         * @param value The integer value from native code
         * @return The corresponding ProtocolLayer enum value
         * @throws IllegalArgumentException if value is not valid
         */
        @JvmStatic
        fun fromValue(value: Int): ProtocolLayer {
            return values().find { it.value == value }
                ?: throw IllegalArgumentException("Invalid ProtocolLayer value: $value")
        }
    }
}