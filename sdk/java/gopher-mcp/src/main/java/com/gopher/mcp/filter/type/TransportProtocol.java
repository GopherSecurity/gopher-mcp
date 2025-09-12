package com.gopher.mcp.filter.type;

import com.gopher.mcp.jna.McpFilterLibrary;

/**
 * Transport protocols for Layer 4 (Transport layer). Specifies the transport protocol being used.
 */
public enum TransportProtocol {

  /** Transmission Control Protocol. Reliable, connection-oriented protocol. */
  TCP(McpFilterLibrary.MCP_TRANSPORT_PROTOCOL_TCP),

  /** User Datagram Protocol. Unreliable, connectionless protocol. */
  UDP(McpFilterLibrary.MCP_TRANSPORT_PROTOCOL_UDP),

  /** QUIC (Quick UDP Internet Connections). Modern transport protocol built on UDP. */
  QUIC(McpFilterLibrary.MCP_TRANSPORT_PROTOCOL_QUIC),

  /** Stream Control Transmission Protocol. Message-oriented protocol with multi-streaming. */
  SCTP(McpFilterLibrary.MCP_TRANSPORT_PROTOCOL_SCTP);

  private final int value;

  TransportProtocol(int value) {
    this.value = value;
  }

  /**
   * Get the integer value for JNA calls
   *
   * @return The numeric value of this transport protocol
   */
  public int getValue() {
    return value;
  }

  /**
   * Convert from integer value to enum
   *
   * @param value The integer value from native code
   * @return The corresponding TransportProtocol enum value
   * @throws IllegalArgumentException if value is not valid
   */
  public static TransportProtocol fromValue(int value) {
    for (TransportProtocol protocol : values()) {
      if (protocol.value == value) {
        return protocol;
      }
    }
    throw new IllegalArgumentException("Invalid TransportProtocol value: " + value);
  }

  /**
   * Check if this is a reliable protocol
   *
   * @return true if the protocol guarantees delivery
   */
  public boolean isReliable() {
    return this == TCP || this == SCTP;
  }

  /**
   * Check if this is a connection-oriented protocol
   *
   * @return true if the protocol establishes connections
   */
  public boolean isConnectionOriented() {
    return this == TCP || this == QUIC || this == SCTP;
  }

  /**
   * Check if this protocol supports streaming
   *
   * @return true if the protocol supports stream-based data transfer
   */
  public boolean supportsStreaming() {
    return this == TCP || this == SCTP;
  }
}
