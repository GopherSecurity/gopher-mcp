package com.gopher.mcp.filter.type

import com.gopher.mcp.jna.McpFilterLibrary

/**
 * Transport protocols for Layer 4 (Transport layer).
 * Specifies the transport protocol being used.
 */
enum class TransportProtocol(private val value: Int) {

    /** Transmission Control Protocol. Reliable, connection-oriented protocol. */
    TCP(McpFilterLibrary.MCP_TRANSPORT_PROTOCOL_TCP),

    /** User Datagram Protocol. Unreliable, connectionless protocol. */
    UDP(McpFilterLibrary.MCP_TRANSPORT_PROTOCOL_UDP),

    /** QUIC (Quick UDP Internet Connections). Modern transport protocol built on UDP. */
    QUIC(McpFilterLibrary.MCP_TRANSPORT_PROTOCOL_QUIC),

    /** Stream Control Transmission Protocol. Message-oriented protocol with multi-streaming. */
    SCTP(McpFilterLibrary.MCP_TRANSPORT_PROTOCOL_SCTP);

    /**
     * Get the integer value for JNA calls
     *
     * @return The numeric value of this transport protocol
     */
    fun getValue(): Int = value

    /**
     * Check if this is a reliable protocol
     *
     * @return true if the protocol guarantees delivery
     */
    fun isReliable(): Boolean = this == TCP || this == SCTP

    /**
     * Check if this is a connection-oriented protocol
     *
     * @return true if the protocol establishes connections
     */
    fun isConnectionOriented(): Boolean = this == TCP || this == QUIC || this == SCTP

    /**
     * Check if this protocol supports streaming
     *
     * @return true if the protocol supports stream-based data transfer
     */
    fun supportsStreaming(): Boolean = this == TCP || this == SCTP

    companion object {
        /**
         * Convert from integer value to enum
         *
         * @param value The integer value from native code
         * @return The corresponding TransportProtocol enum value
         * @throws IllegalArgumentException if value is not valid
         */
        @JvmStatic
        fun fromValue(value: Int): TransportProtocol {
            return values().find { it.value == value }
                ?: throw IllegalArgumentException("Invalid TransportProtocol value: $value")
        }
    }
}