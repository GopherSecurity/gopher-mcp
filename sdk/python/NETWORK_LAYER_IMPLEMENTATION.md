# Network Layer Implementation Guide

This document provides a comprehensive guide for implementing the actual network layer for the GopherTransport in the Python SDK.

## Overview

The current `GopherTransport` implementation provides a foundation for integrating with the filter system, but it doesn't include actual network communication. This guide explains how to extend it to support real network protocols.

## Architecture

### Current Implementation

The current `GopherTransport` provides:

- Filter integration through `FilterManager`
- Session management
- Event handling
- Message processing simulation

### Target Implementation

The enhanced network layer should support:

- **TCP/UDP Communication**: Low-level network protocols
- **WebSocket Support**: Real-time bidirectional communication
- **HTTP/HTTPS**: RESTful API communication
- **Connection Management**: Pooling, keep-alive, and health checks
- **Message Serialization**: Efficient data encoding/decoding
- **Error Handling**: Network-specific error recovery

## Implementation Strategy

### 1. Protocol Abstraction

Create a protocol abstraction layer that supports multiple transport protocols:

```python
from abc import ABC, abstractmethod
from typing import Protocol, Dict, Any, Optional
import asyncio

class NetworkProtocol(ABC):
    """Abstract base class for network protocols."""

    @abstractmethod
    async def connect(self, host: str, port: int, **kwargs) -> None:
        """Connect to the remote endpoint."""
        pass

    @abstractmethod
    async def disconnect(self) -> None:
        """Disconnect from the remote endpoint."""
        pass

    @abstractmethod
    async def send(self, data: bytes) -> None:
        """Send data to the remote endpoint."""
        pass

    @abstractmethod
    async def receive(self) -> bytes:
        """Receive data from the remote endpoint."""
        pass

    @abstractmethod
    def is_connected(self) -> bool:
        """Check if the connection is active."""
        pass

class TcpProtocol(NetworkProtocol):
    """TCP protocol implementation."""
    pass

class UdpProtocol(NetworkProtocol):
    """UDP protocol implementation."""
    pass

class WebSocketProtocol(NetworkProtocol):
    """WebSocket protocol implementation."""
    pass

class HttpProtocol(NetworkProtocol):
    """HTTP protocol implementation."""
    pass
```

### 2. Connection Management

Implement connection pooling and management:

```python
import asyncio
from typing import Dict, List, Optional
from dataclasses import dataclass
from enum import Enum

class ConnectionState(Enum):
    IDLE = "idle"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    DISCONNECTING = "disconnecting"
    DISCONNECTED = "disconnected"
    ERROR = "error"

@dataclass
class Connection:
    """Represents a network connection."""
    id: str
    protocol: NetworkProtocol
    state: ConnectionState
    created_at: float
    last_used: float
    metadata: Dict[str, Any]

class ConnectionPool:
    """Manages a pool of network connections."""

    def __init__(self, max_connections: int = 100, idle_timeout: float = 300.0):
        self.max_connections = max_connections
        self.idle_timeout = idle_timeout
        self.connections: Dict[str, Connection] = {}
        self.available_connections: List[str] = []
        self._lock = asyncio.Lock()

    async def get_connection(self, endpoint: str) -> Optional[Connection]:
        """Get an available connection or create a new one."""
        async with self._lock:
            # Try to find an available connection
            for conn_id in self.available_connections:
                conn = self.connections[conn_id]
                if conn.state == ConnectionState.CONNECTED:
                    conn.last_used = asyncio.get_event_loop().time()
                    self.available_connections.remove(conn_id)
                    return conn

            # Create new connection if under limit
            if len(self.connections) < self.max_connections:
                return await self._create_connection(endpoint)

            return None

    async def return_connection(self, connection: Connection) -> None:
        """Return a connection to the pool."""
        async with self._lock:
            if connection.state == ConnectionState.CONNECTED:
                connection.last_used = asyncio.get_event_loop().time()
                self.available_connections.append(connection.id)

    async def _create_connection(self, endpoint: str) -> Connection:
        """Create a new connection."""
        # Implementation depends on the specific protocol
        pass

    async def cleanup_idle_connections(self) -> None:
        """Remove idle connections."""
        current_time = asyncio.get_event_loop().time()
        async with self._lock:
            idle_connections = [
                conn_id for conn_id, conn in self.connections.items()
                if current_time - conn.last_used > self.idle_timeout
            ]

            for conn_id in idle_connections:
                await self._close_connection(conn_id)

    async def _close_connection(self, conn_id: str) -> None:
        """Close a specific connection."""
        if conn_id in self.connections:
            conn = self.connections[conn_id]
            await conn.protocol.disconnect()
            del self.connections[conn_id]
            if conn_id in self.available_connections:
                self.available_connections.remove(conn_id)
```

### 3. Message Serialization

Implement efficient message serialization:

```python
import json
import msgpack
import pickle
from typing import Any, Dict, Union
from enum import Enum

class SerializationFormat(Enum):
    JSON = "json"
    MSGPACK = "msgpack"
    PICKLE = "pickle"
    PROTOBUF = "protobuf"

class MessageSerializer:
    """Handles message serialization and deserialization."""

    def __init__(self, format: SerializationFormat = SerializationFormat.JSON):
        self.format = format

    def serialize(self, message: Dict[str, Any]) -> bytes:
        """Serialize a message to bytes."""
        if self.format == SerializationFormat.JSON:
            return json.dumps(message).encode('utf-8')
        elif self.format == SerializationFormat.MSGPACK:
            return msgpack.packb(message)
        elif self.format == SerializationFormat.PICKLE:
            return pickle.dumps(message)
        else:
            raise ValueError(f"Unsupported serialization format: {self.format}")

    def deserialize(self, data: bytes) -> Dict[str, Any]:
        """Deserialize bytes to a message."""
        if self.format == SerializationFormat.JSON:
            return json.loads(data.decode('utf-8'))
        elif self.format == SerializationFormat.MSGPACK:
            return msgpack.unpackb(data)
        elif self.format == SerializationFormat.PICKLE:
            return pickle.loads(data)
        else:
            raise ValueError(f"Unsupported serialization format: {self.format}")
```

### 4. Protocol Implementations

#### TCP Protocol

```python
import asyncio
import socket
from typing import Optional

class TcpProtocol(NetworkProtocol):
    """TCP protocol implementation."""

    def __init__(self, host: str, port: int, timeout: float = 30.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self._connected = False

    async def connect(self, host: str, port: int, **kwargs) -> None:
        """Connect to the TCP endpoint."""
        try:
            self.reader, self.writer = await asyncio.wait_for(
                asyncio.open_connection(host, port),
                timeout=self.timeout
            )
            self._connected = True
        except asyncio.TimeoutError:
            raise ConnectionError(f"Connection timeout to {host}:{port}")
        except Exception as e:
            raise ConnectionError(f"Failed to connect to {host}:{port}: {e}")

    async def disconnect(self) -> None:
        """Disconnect from the TCP endpoint."""
        if self.writer:
            self.writer.close()
            await self.writer.wait_closed()
        self._connected = False

    async def send(self, data: bytes) -> None:
        """Send data over TCP."""
        if not self._connected or not self.writer:
            raise ConnectionError("Not connected")

        try:
            self.writer.write(data)
            await self.writer.drain()
        except Exception as e:
            self._connected = False
            raise ConnectionError(f"Failed to send data: {e}")

    async def receive(self) -> bytes:
        """Receive data from TCP."""
        if not self._connected or not self.reader:
            raise ConnectionError("Not connected")

        try:
            data = await asyncio.wait_for(
                self.reader.read(4096),
                timeout=self.timeout
            )
            if not data:
                self._connected = False
                raise ConnectionError("Connection closed by remote")
            return data
        except asyncio.TimeoutError:
            raise ConnectionError("Receive timeout")
        except Exception as e:
            self._connected = False
            raise ConnectionError(f"Failed to receive data: {e}")

    def is_connected(self) -> bool:
        """Check if the connection is active."""
        return self._connected
```

#### WebSocket Protocol

```python
import asyncio
import websockets
from typing import Optional

class WebSocketProtocol(NetworkProtocol):
    """WebSocket protocol implementation."""

    def __init__(self, uri: str, timeout: float = 30.0):
        self.uri = uri
        self.timeout = timeout
        self.websocket: Optional[websockets.WebSocketServerProtocol] = None
        self._connected = False

    async def connect(self, host: str, port: int, **kwargs) -> None:
        """Connect to the WebSocket endpoint."""
        try:
            self.websocket = await asyncio.wait_for(
                websockets.connect(self.uri),
                timeout=self.timeout
            )
            self._connected = True
        except asyncio.TimeoutError:
            raise ConnectionError(f"WebSocket connection timeout to {self.uri}")
        except Exception as e:
            raise ConnectionError(f"Failed to connect to {self.uri}: {e}")

    async def disconnect(self) -> None:
        """Disconnect from the WebSocket endpoint."""
        if self.websocket:
            await self.websocket.close()
        self._connected = False

    async def send(self, data: bytes) -> None:
        """Send data over WebSocket."""
        if not self._connected or not self.websocket:
            raise ConnectionError("Not connected")

        try:
            await self.websocket.send(data)
        except Exception as e:
            self._connected = False
            raise ConnectionError(f"Failed to send data: {e}")

    async def receive(self) -> bytes:
        """Receive data from WebSocket."""
        if not self._connected or not self.websocket:
            raise ConnectionError("Not connected")

        try:
            data = await asyncio.wait_for(
                self.websocket.recv(),
                timeout=self.timeout
            )
            if isinstance(data, str):
                data = data.encode('utf-8')
            return data
        except asyncio.TimeoutError:
            raise ConnectionError("Receive timeout")
        except Exception as e:
            self._connected = False
            raise ConnectionError(f"Failed to receive data: {e}")

    def is_connected(self) -> bool:
        """Check if the connection is active."""
        return self._connected
```

### 5. Enhanced GopherTransport

Extend the current `GopherTransport` with network capabilities:

```python
import asyncio
from typing import Dict, Any, Optional, List
from .filter_manager import FilterManager, FilterManagerConfig
from .network_protocols import NetworkProtocol, ConnectionPool, MessageSerializer

class EnhancedGopherTransport:
    """Enhanced GopherTransport with network capabilities."""

    def __init__(self, config: GopherTransportConfig):
        self.config = config
        self.filter_manager = FilterManager(config.filters)
        self.connection_pool = ConnectionPool(
            max_connections=config.max_connections,
            idle_timeout=config.session_timeout / 1000.0
        )
        self.serializer = MessageSerializer(
            SerializationFormat(config.serialization_format)
        )
        self.protocols: Dict[str, NetworkProtocol] = {}
        self.sessions: Dict[str, Dict[str, Any]] = {}
        self.event_handlers: List[callable] = []
        self._running = False
        self._cleanup_task: Optional[asyncio.Task] = None

    async def start(self) -> None:
        """Start the transport with network capabilities."""
        if self._running:
            return

        self._running = True

        # Start filter manager
        await self.filter_manager.start()

        # Start connection cleanup task
        self._cleanup_task = asyncio.create_task(self._cleanup_connections())

        # Initialize network protocols
        await self._initialize_protocols()

        print(f"✅ Enhanced transport started with {len(self.protocols)} protocols")

    async def close(self) -> None:
        """Close the transport and cleanup resources."""
        if not self._running:
            return

        self._running = False

        # Cancel cleanup task
        if self._cleanup_task:
            self._cleanup_task.cancel()
            try:
                await self._cleanup_task
            except asyncio.CancelledError:
                pass

        # Close all connections
        await self.connection_pool.cleanup_idle_connections()

        # Close all protocols
        for protocol in self.protocols.values():
            await protocol.disconnect()

        # Destroy filter manager
        self.filter_manager.destroy()

        print("✅ Enhanced transport closed")

    async def send(self, message: JSONRPCMessage, endpoint: str = None) -> None:
        """Send a message over the network."""
        if not self._running:
            raise RuntimeError("Transport not started")

        # Process message through filters
        processed_message = self.filter_manager.process(message)

        # Serialize message
        serialized_data = self.serializer.serialize(processed_message.dict())

        # Get connection
        connection = await self.connection_pool.get_connection(endpoint or "default")
        if not connection:
            raise ConnectionError("No available connections")

        try:
            # Send data
            await connection.protocol.send(serialized_data)

            # Return connection to pool
            await self.connection_pool.return_connection(connection)

            # Emit event
            self._emit_event("message_sent", {
                "message": processed_message,
                "endpoint": endpoint,
                "connection_id": connection.id
            })

        except Exception as e:
            # Return connection to pool on error
            await self.connection_pool.return_connection(connection)
            raise

    async def receive(self, endpoint: str = None) -> JSONRPCMessage:
        """Receive a message from the network."""
        if not self._running:
            raise RuntimeError("Transport not started")

        # Get connection
        connection = await self.connection_pool.get_connection(endpoint or "default")
        if not connection:
            raise ConnectionError("No available connections")

        try:
            # Receive data
            serialized_data = await connection.protocol.receive()

            # Deserialize message
            message_dict = self.serializer.deserialize(serialized_data)
            message = JSONRPCMessage(**message_dict)

            # Process message through filters
            processed_message = self.filter_manager.process_response(message)

            # Return connection to pool
            await self.connection_pool.return_connection(connection)

            # Emit event
            self._emit_event("message_received", {
                "message": processed_message,
                "endpoint": endpoint,
                "connection_id": connection.id
            })

            return processed_message

        except Exception as e:
            # Return connection to pool on error
            await self.connection_pool.return_connection(connection)
            raise

    async def _initialize_protocols(self) -> None:
        """Initialize network protocols based on configuration."""
        if self.config.protocol == "tcp":
            protocol = TcpProtocol(
                self.config.host,
                self.config.port,
                self.config.timeout / 1000.0
            )
            await protocol.connect(self.config.host, self.config.port)
            self.protocols["tcp"] = protocol

        elif self.config.protocol == "websocket":
            protocol = WebSocketProtocol(
                f"ws://{self.config.host}:{self.config.port}",
                self.config.timeout / 1000.0
            )
            await protocol.connect(self.config.host, self.config.port)
            self.protocols["websocket"] = protocol

        elif self.config.protocol == "http":
            protocol = HttpProtocol(
                self.config.host,
                self.config.port,
                self.config.timeout / 1000.0
            )
            await protocol.connect(self.config.host, self.config.port)
            self.protocols["http"] = protocol

    async def _cleanup_connections(self) -> None:
        """Periodically cleanup idle connections."""
        while self._running:
            try:
                await asyncio.sleep(60)  # Cleanup every minute
                await self.connection_pool.cleanup_idle_connections()
            except asyncio.CancelledError:
                break
            except Exception as e:
                print(f"Error during connection cleanup: {e}")

    def _emit_event(self, event_type: str, data: Dict[str, Any]) -> None:
        """Emit an event to all registered handlers."""
        for handler in self.event_handlers:
            try:
                handler(event_type, data)
            except Exception as e:
                print(f"Error in event handler: {e}")
```

## Configuration

### Network Configuration

```python
@dataclass
class NetworkConfig:
    """Network-specific configuration."""
    protocol: str = "tcp"  # tcp, udp, websocket, http
    host: str = "localhost"
    port: int = 8080
    timeout: int = 30000  # milliseconds
    max_connections: int = 100
    idle_timeout: int = 300000  # milliseconds
    keep_alive: bool = True
    keep_alive_interval: int = 30000  # milliseconds
    max_idle_time: int = 300000  # milliseconds
    serialization_format: str = "json"  # json, msgpack, pickle
    compression: bool = False
    encryption: bool = False
    ssl_context: Optional[ssl.SSLContext] = None
    custom_headers: Dict[str, str] = field(default_factory=dict)
    retry_attempts: int = 3
    retry_delay: int = 1000  # milliseconds
    health_check_interval: int = 30000  # milliseconds
    health_check_timeout: int = 5000  # milliseconds
```

### Enhanced Transport Configuration

```python
@dataclass
class EnhancedGopherTransportConfig(GopherTransportConfig):
    """Enhanced transport configuration with network capabilities."""
    network: NetworkConfig = field(default_factory=NetworkConfig)
    protocols: List[str] = field(default_factory=lambda: ["tcp", "websocket"])
    load_balancing: bool = False
    failover: bool = False
    circuit_breaker: bool = False
    rate_limiting: bool = False
    monitoring: bool = True
    metrics_endpoint: Optional[str] = None
    tracing_endpoint: Optional[str] = None
```

## Usage Examples

### Basic TCP Transport

```python
from gopher_mcp import EnhancedGopherTransport, EnhancedGopherTransportConfig, NetworkConfig

# Configure network
network_config = NetworkConfig(
    protocol="tcp",
    host="localhost",
    port=8080,
    timeout=30000,
    max_connections=50,
    keep_alive=True
)

# Configure transport
transport_config = EnhancedGopherTransportConfig(
    name="tcp_transport",
    network=network_config,
    filters=FilterManagerConfig(
        name="tcp_filters",
        enabled=True,
        network=NetworkFilterConfig(
            tcp_proxy=TcpProxyConfig(
                enabled=True,
                upstream_host="backend.example.com",
                upstream_port=8080
            )
        )
    )
)

# Create and use transport
transport = EnhancedGopherTransport(transport_config)
await transport.start()

# Send message
message = JSONRPCMessage(
    jsonrpc="2.0",
    id=1,
    method="tools/list",
    params={}
)

await transport.send(message, "backend.example.com:8080")

# Receive response
response = await transport.receive("backend.example.com:8080")
print(f"Received: {response}")

await transport.close()
```

### WebSocket Transport

```python
# Configure WebSocket
network_config = NetworkConfig(
    protocol="websocket",
    host="localhost",
    port=8080,
    timeout=30000,
    max_connections=100,
    keep_alive=True,
    serialization_format="msgpack"
)

# Configure transport
transport_config = EnhancedGopherTransportConfig(
    name="websocket_transport",
    network=network_config,
    filters=FilterManagerConfig(
        name="websocket_filters",
        enabled=True,
        http=HttpFilterConfig(
            codec=HttpCodecConfig(
                enabled=True,
                version="1.1"
            )
        )
    )
)

# Create and use transport
transport = EnhancedGopherTransport(transport_config)
await transport.start()

# WebSocket communication
message = JSONRPCMessage(
    jsonrpc="2.0",
    id=1,
    method="tools/call",
    params={"name": "calculator", "arguments": {"operation": "add", "a": 5, "b": 3}}
)

await transport.send(message, "ws://localhost:8080")
response = await transport.receive("ws://localhost:8080")
print(f"WebSocket response: {response}")

await transport.close()
```

## Performance Considerations

### Connection Pooling

- **Pool Size**: Configure based on expected concurrent connections
- **Idle Timeout**: Balance between resource usage and connection reuse
- **Health Checks**: Regular health checks to maintain connection quality

### Serialization

- **Format Selection**: Choose based on performance vs. compatibility needs
- **Compression**: Enable for large messages or slow networks
- **Caching**: Cache serialized data for frequently sent messages

### Error Handling

- **Retry Logic**: Implement exponential backoff for transient failures
- **Circuit Breaker**: Prevent cascading failures
- **Graceful Degradation**: Fallback mechanisms for network issues

## Security Considerations

### TLS/SSL

- **Certificate Validation**: Proper certificate chain validation
- **Cipher Suites**: Use strong encryption algorithms
- **Protocol Versions**: Support only secure protocol versions

### Authentication

- **API Keys**: Secure API key management
- **JWT Tokens**: Proper token validation and refresh
- **Mutual TLS**: Client certificate authentication

### Network Security

- **Firewall Rules**: Proper network access controls
- **Rate Limiting**: Prevent abuse and DoS attacks
- **IP Whitelisting**: Restrict access to known IPs

## Monitoring and Observability

### Metrics

- **Connection Metrics**: Active connections, connection pool usage
- **Message Metrics**: Messages sent/received, processing time
- **Error Metrics**: Connection errors, message failures

### Logging

- **Structured Logging**: JSON-formatted logs for easy parsing
- **Log Levels**: Appropriate log levels for different scenarios
- **Sensitive Data**: Avoid logging sensitive information

### Tracing

- **Distributed Tracing**: Track requests across service boundaries
- **Span Attributes**: Include relevant metadata in traces
- **Sampling**: Configure appropriate sampling rates

## Testing

### Unit Tests

- **Protocol Tests**: Test individual protocol implementations
- **Connection Tests**: Test connection management and pooling
- **Serialization Tests**: Test message serialization/deserialization

### Integration Tests

- **End-to-End Tests**: Test complete message flow
- **Network Tests**: Test with real network conditions
- **Load Tests**: Test under high load conditions

### Mock Testing

- **Network Mocks**: Mock network conditions and failures
- **Protocol Mocks**: Mock protocol implementations for testing
- **Connection Mocks**: Mock connection behavior for testing

## Deployment

### Containerization

- **Docker**: Containerize the application for easy deployment
- **Health Checks**: Implement proper health check endpoints
- **Resource Limits**: Set appropriate resource limits

### Orchestration

- **Kubernetes**: Deploy using Kubernetes for scalability
- **Service Mesh**: Use service mesh for advanced networking
- **Load Balancing**: Implement proper load balancing strategies

### Configuration Management

- **Environment Variables**: Use environment variables for configuration
- **Config Maps**: Use Kubernetes ConfigMaps for configuration
- **Secrets**: Use Kubernetes Secrets for sensitive data

## Conclusion

This guide provides a comprehensive approach to implementing the network layer for the GopherTransport. The implementation should be done incrementally, starting with basic TCP support and gradually adding more advanced features like WebSocket, HTTP, and advanced connection management.

The key principles are:

1. **Modularity**: Separate concerns into distinct components
2. **Performance**: Optimize for high throughput and low latency
3. **Reliability**: Implement proper error handling and recovery
4. **Security**: Ensure secure communication and data handling
5. **Observability**: Provide comprehensive monitoring and logging

By following this guide, you can create a robust, scalable, and maintainable network layer that integrates seamlessly with the existing filter system.
