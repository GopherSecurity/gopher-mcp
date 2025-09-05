package com.gopher.mcp.filter.type;

import com.gopher.mcp.jna.McpFilterLibrary;

/**
 * Application protocols for Layer 7 (Application layer).
 * Specifies the application-level protocol being used.
 */
public enum ApplicationProtocol {
    
    /**
     * Hypertext Transfer Protocol.
     * Standard web protocol over TCP.
     */
    HTTP(McpFilterLibrary.MCP_APP_PROTOCOL_HTTP),
    
    /**
     * HTTP Secure.
     * HTTP over TLS/SSL.
     */
    HTTPS(McpFilterLibrary.MCP_APP_PROTOCOL_HTTPS),
    
    /**
     * HTTP/2.
     * Binary framing layer for HTTP.
     */
    HTTP2(McpFilterLibrary.MCP_APP_PROTOCOL_HTTP2),
    
    /**
     * HTTP/3.
     * HTTP over QUIC.
     */
    HTTP3(McpFilterLibrary.MCP_APP_PROTOCOL_HTTP3),
    
    /**
     * gRPC.
     * Remote procedure call framework.
     */
    GRPC(McpFilterLibrary.MCP_APP_PROTOCOL_GRPC),
    
    /**
     * WebSocket.
     * Full-duplex communication protocol.
     */
    WEBSOCKET(McpFilterLibrary.MCP_APP_PROTOCOL_WEBSOCKET),
    
    /**
     * JSON-RPC.
     * Remote procedure call protocol using JSON.
     */
    JSONRPC(McpFilterLibrary.MCP_APP_PROTOCOL_JSONRPC),
    
    /**
     * Custom protocol.
     * User-defined application protocol.
     */
    CUSTOM(McpFilterLibrary.MCP_APP_PROTOCOL_CUSTOM);
    
    private final int value;
    
    ApplicationProtocol(int value) {
        this.value = value;
    }
    
    /**
     * Get the integer value for JNA calls
     * @return The numeric value of this application protocol
     */
    public int getValue() {
        return value;
    }
    
    /**
     * Convert from integer value to enum
     * @param value The integer value from native code
     * @return The corresponding ApplicationProtocol enum value
     * @throws IllegalArgumentException if value is not valid
     */
    public static ApplicationProtocol fromValue(int value) {
        for (ApplicationProtocol protocol : values()) {
            if (protocol.value == value) {
                return protocol;
            }
        }
        throw new IllegalArgumentException("Invalid ApplicationProtocol value: " + value);
    }
    
    /**
     * Check if this is a secure protocol
     * @return true if the protocol uses encryption
     */
    public boolean isSecure() {
        return this == HTTPS || this == HTTP3;
    }
    
    /**
     * Check if this is an HTTP-based protocol
     * @return true if the protocol is based on HTTP
     */
    public boolean isHttpBased() {
        return this == HTTP || this == HTTPS || this == HTTP2 || this == HTTP3 || this == GRPC;
    }
    
    /**
     * Check if this protocol supports bidirectional streaming
     * @return true if the protocol supports full-duplex communication
     */
    public boolean supportsBidirectionalStreaming() {
        return this == HTTP2 || this == HTTP3 || this == GRPC || this == WEBSOCKET;
    }
}