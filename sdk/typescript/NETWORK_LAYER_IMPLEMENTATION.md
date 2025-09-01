# Network Layer Implementation Guide

This document outlines the strategy and implementation details for adding real network layer functionality to the GopherTransport, replacing the current simulation layer with actual TCP, UDP, and WebSocket implementations.

## 🎯 **Current State**

The GopherTransport currently uses a simulation layer (`simulateSend()`) that:
- ✅ Processes messages through the complete FilterManager pipeline
- ✅ Adds processing metadata (timestamps, session IDs, processing flags)
- ✅ Handles all filter types (security, observability, traffic management)
- ❌ Doesn't actually send data over the network
- ❌ Doesn't implement real protocol-specific communication

## 🏗️ **Network Layer Architecture**

### **1. Protocol-Specific Implementations**

#### **TCP Implementation**
```typescript
// TCP-specific connection management
class TcpConnection {
  private socket: net.Socket;
  private connectionState: 'connecting' | 'connected' | 'disconnected' | 'error';
  
  async connect(host: string, port: number): Promise<void> {
    // Implement TCP connection logic
    // Handle connection events (connect, error, close)
    // Implement message framing (length-prefixed or delimiter-based)
  }
  
  async send(data: Buffer): Promise<void> {
    // Send data over TCP socket
    // Handle partial writes and backpressure
  }
  
  async receive(): Promise<Buffer> {
    // Receive data from TCP socket
    // Handle message framing and reassembly
  }
}
```

#### **UDP Implementation**
```typescript
// UDP-specific connection management
class UdpConnection {
  private socket: dgram.Socket;
  private connectionState: 'bound' | 'connected' | 'disconnected' | 'error';
  
  async bind(host: string, port: number): Promise<void> {
    // Implement UDP binding logic
    // Handle datagram events (message, error, close)
  }
  
  async send(data: Buffer, address: string, port: number): Promise<void> {
    // Send datagram over UDP socket
    // Handle message ordering if needed
  }
  
  async receive(): Promise<{ data: Buffer; address: string; port: number }> {
    // Receive datagram from UDP socket
    // Handle packet fragmentation for large messages
  }
}
```

#### **WebSocket Implementation**
```typescript
// WebSocket-specific connection management
class WebSocketConnection {
  private ws: WebSocket;
  private connectionState: 'connecting' | 'connected' | 'disconnected' | 'error';
  
  async connect(url: string, protocols?: string[]): Promise<void> {
    // Implement WebSocket connection logic
    // Handle WebSocket events (open, message, error, close)
    // Support MCP subprotocol negotiation
  }
  
  async send(data: string | Buffer): Promise<void> {
    // Send data over WebSocket
    // Handle WebSocket-specific framing
  }
  
  async receive(): Promise<string | Buffer> {
    // Receive data from WebSocket
    // Handle different message types (text, binary)
  }
}
```

### **2. Connection Manager**

```typescript
// Abstract connection interface
interface Connection {
  connect(): Promise<void>;
  send(data: Buffer): Promise<void>;
  receive(): Promise<Buffer>;
  close(): Promise<void>;
  isConnected(): boolean;
  on(event: string, listener: Function): void;
}

// Connection factory
class ConnectionFactory {
  static create(config: GopherTransportConfig): Connection {
    switch (config.protocol) {
      case 'tcp':
        return new TcpConnection(config);
      case 'udp':
        return new UdpConnection(config);
      case 'websocket':
        return new WebSocketConnection(config);
      case 'stdio':
        return new StdioConnection(config);
      default:
        throw new Error(`Unsupported protocol: ${config.protocol}`);
    }
  }
}
```

### **3. Message Serialization**

```typescript
// Message serialization/deserialization
class MessageSerializer {
  // JSON-RPC message serialization
  static serialize(message: JSONRPCMessage): Buffer {
    const json = JSON.stringify(message);
    return Buffer.from(json, 'utf8');
  }
  
  static deserialize(data: Buffer): JSONRPCMessage {
    const json = data.toString('utf8');
    return JSON.parse(json);
  }
  
  // Binary protocol support (Protocol Buffers, MessagePack)
  static serializeBinary(message: JSONRPCMessage): Buffer {
    // Implement binary serialization
  }
  
  static deserializeBinary(data: Buffer): JSONRPCMessage {
    // Implement binary deserialization
  }
}

// Message framing for different protocols
class MessageFramer {
  // Length-prefixed framing
  static frameWithLength(data: Buffer): Buffer {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length, 0);
    return Buffer.concat([length, data]);
  }
  
  static unframeWithLength(data: Buffer): { message: Buffer; remaining: Buffer } {
    // Implement length-prefixed unframing
  }
  
  // Delimiter-based framing
  static frameWithDelimiter(data: Buffer, delimiter: string = '\n'): Buffer {
    return Buffer.concat([data, Buffer.from(delimiter, 'utf8')]);
  }
  
  static unframeWithDelimiter(data: Buffer, delimiter: string = '\n'): { message: Buffer; remaining: Buffer } {
    // Implement delimiter-based unframing
  }
}
```

### **4. Transport Layer Implementation**

```typescript
// Replace simulateSend() with real network implementation
class GopherTransport {
  private connection: Connection;
  private serializer: MessageSerializer;
  private framer: MessageFramer;
  
  async send(message: JSONRPCMessage, options?: TransportSendOptions): Promise<void> {
    if (this.isDestroyed) {
      throw new Error("GopherTransport has been destroyed");
    }

    if (!this.isConnected) {
      throw new Error("GopherTransport is not connected");
    }

    try {
      // Process message through FilterManager
      const filterMessage = adaptToFilterMessage(message);
      const processedFilterMessage = await this.filterManager.process(filterMessage);
      const processedMessage = adaptToMCPMessage(processedFilterMessage);
      
      // Serialize message
      const serializedMessage = this.serializer.serialize(processedMessage);
      
      // Frame message for protocol
      const framedMessage = this.framer.frameWithLength(serializedMessage);
      
      // Send over network
      await this.connection.send(framedMessage);
      
    } catch (error) {
      // Handle network errors
      if (this.onerror) {
        this.onerror(new Error(`Failed to send message: ${error}`));
      }
      throw error;
    }
  }
  
  // Add message receiving capability
  private async startReceiving(): Promise<void> {
    while (this.isConnected && !this.isDestroyed) {
      try {
        const framedData = await this.connection.receive();
        const { message: serializedMessage } = this.framer.unframeWithLength(framedData);
        const message = this.serializer.deserialize(serializedMessage);
        
        // Process response through FilterManager
        const filterMessage = adaptToFilterMessage(message);
        const processedFilterMessage = await this.filterManager.processResponse(filterMessage);
        const processedMessage = adaptToMCPMessage(processedFilterMessage);
        
        // Notify message event
        if (this.onmessage) {
          this.onmessage(processedMessage);
        }
      } catch (error) {
        if (this.onerror) {
          this.onerror(new Error(`Failed to receive message: ${error}`));
        }
      }
    }
  }
}
```

## 🔧 **Implementation Phases**

### **Phase 1: Basic Network Layer**
- [ ] Implement basic TCP client/server connections
- [ ] Add message serialization/deserialization
- [ ] Implement simple message framing
- [ ] Replace `simulateSend()` with real network calls
- [ ] Add connection lifecycle management

### **Phase 2: Protocol Support**
- [ ] Implement UDP connection support
- [ ] Add WebSocket connection support
- [ ] Implement stdio connection support
- [ ] Add protocol-specific configuration options
- [ ] Handle protocol-specific error conditions

### **Phase 3: Advanced Features**
- [ ] Add connection pooling and reuse
- [ ] Implement automatic reconnection
- [ ] Add load balancing across multiple endpoints
- [ ] Implement health checking
- [ ] Add connection state monitoring

### **Phase 4: Production Features**
- [ ] Add TLS/SSL support for secure connections
- [ ] Implement advanced error handling and recovery
- [ ] Add performance optimizations
- [ ] Implement connection rate limiting
- [ ] Add comprehensive logging and monitoring

## 📋 **Configuration Extensions**

### **Network-Specific Configuration**
```typescript
export interface GopherTransportConfig {
  // Existing configuration...
  
  // Network-specific settings
  network?: {
    // Connection settings
    connectTimeout?: number;
    sendTimeout?: number;
    receiveTimeout?: number;
    keepAlive?: boolean;
    keepAliveInterval?: number;
    
    // Buffer settings
    sendBufferSize?: number;
    receiveBufferSize?: number;
    maxMessageSize?: number;
    
    // Retry settings
    maxRetries?: number;
    retryDelay?: number;
    backoffMultiplier?: number;
    
    // Security settings
    tls?: {
      enabled: boolean;
      certPath?: string;
      keyPath?: string;
      caPath?: string;
      protocols?: string[];
      ciphers?: string[];
    };
  };
  
  // Protocol-specific settings
  tcp?: {
    noDelay?: boolean;
    keepAlive?: boolean;
    keepAliveInitialDelay?: number;
  };
  
  udp?: {
    maxPacketSize?: number;
    enableFragmentation?: boolean;
    timeout?: number;
  };
  
  websocket?: {
    subprotocols?: string[];
    pingInterval?: number;
    pongTimeout?: number;
    maxPayloadSize?: number;
  };
}
```

## 🧪 **Testing Strategy**

### **Unit Tests**
- Test each protocol implementation independently
- Test message serialization/deserialization
- Test connection lifecycle management
- Test error handling and recovery

### **Integration Tests**
- Test complete client-server communication
- Test FilterManager integration with real network
- Test different protocol combinations
- Test error scenarios and recovery

### **Performance Tests**
- Test throughput with different message sizes
- Test latency under various network conditions
- Test memory usage and resource cleanup
- Test concurrent connection handling

## 🚀 **Migration Path**

### **Step 1: Add Network Layer**
1. Create protocol-specific connection classes
2. Implement message serialization/framing
3. Add connection management
4. Replace `simulateSend()` with real network calls

### **Step 2: Maintain Compatibility**
1. Keep existing FilterManager integration
2. Maintain same configuration interface
3. Preserve error handling behavior
4. Keep resource cleanup mechanisms

### **Step 3: Add Advanced Features**
1. Add connection pooling
2. Implement automatic reconnection
3. Add load balancing
4. Implement health checking

### **Step 4: Production Deployment**
1. Add comprehensive monitoring
2. Implement security features
3. Add performance optimizations
4. Create deployment documentation

## 🔒 **Security Considerations**

### **Network Security**
- TLS/SSL encryption for all network communication
- Certificate validation and management
- Secure key exchange mechanisms
- Network-level access control

### **Message Security**
- Message integrity verification
- Replay attack prevention
- Rate limiting at network level
- Input validation and sanitization

### **Connection Security**
- Secure connection establishment
- Connection state validation
- Secure connection termination
- Connection hijacking prevention

## 📊 **Performance Considerations**

### **Network Performance**
- Connection pooling and reuse
- Message batching and pipelining
- Compression for large messages
- Efficient serialization formats

### **Memory Management**
- Buffer pooling and reuse
- Efficient message framing
- Garbage collection optimization
- Memory leak prevention

### **Scalability**
- Horizontal scaling support
- Load balancing capabilities
- Connection multiplexing
- Resource usage monitoring

## 🎯 **Success Metrics**

### **Functionality**
- ✅ All protocols (TCP, UDP, WebSocket, stdio) working
- ✅ Complete FilterManager integration maintained
- ✅ All existing tests passing
- ✅ No performance regression

### **Performance**
- ✅ Latency < 1ms for local connections
- ✅ Throughput > 10,000 messages/second
- ✅ Memory usage < 100MB for 1000 concurrent connections
- ✅ CPU usage < 10% under normal load

### **Reliability**
- ✅ 99.9% uptime under normal conditions
- ✅ Automatic recovery from network failures
- ✅ Graceful handling of connection errors
- ✅ Proper resource cleanup

This implementation plan provides a comprehensive roadmap for adding real network layer functionality to the GopherTransport while maintaining all existing FilterManager capabilities and ensuring production-ready performance and reliability.
