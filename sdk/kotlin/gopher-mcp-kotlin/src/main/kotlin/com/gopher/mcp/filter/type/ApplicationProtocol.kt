package com.gopher.mcp.filter.type

/**
 * Application layer protocol types
 */
enum class ApplicationProtocol(val value: Int) {
    /** Unknown or unspecified protocol */
    UNKNOWN(0),
    
    /** HTTP/1.0 protocol */
    HTTP_10(1),
    
    /** HTTP/1.1 protocol */
    HTTP_11(2),
    
    /** HTTP/2 protocol */
    HTTP_2(3),
    
    /** HTTP/3 protocol (QUIC) */
    HTTP_3(4),
    
    /** WebSocket protocol */
    WEBSOCKET(5),
    
    /** gRPC protocol */
    GRPC(6),
    
    /** GraphQL protocol */
    GRAPHQL(7),
    
    /** JSON-RPC protocol */
    JSON_RPC(8),
    
    /** XML-RPC protocol */
    XML_RPC(9),
    
    /** SOAP protocol */
    SOAP(10),
    
    /** REST API */
    REST(11),
    
    /** MQTT protocol */
    MQTT(12),
    
    /** AMQP protocol */
    AMQP(13),
    
    /** Redis protocol */
    REDIS(14),
    
    /** MySQL protocol */
    MYSQL(15),
    
    /** PostgreSQL protocol */
    POSTGRESQL(16),
    
    /** MongoDB protocol */
    MONGODB(17),
    
    /** Kafka protocol */
    KAFKA(18),
    
    /** Custom protocol */
    CUSTOM(99);
    
    companion object {
        @JvmStatic
        fun fromValue(value: Int): ApplicationProtocol = 
            entries.find { it.value == value } ?: UNKNOWN
    }
}