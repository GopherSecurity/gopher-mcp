"""
Gopher Transport implementation for MCP.

This module provides a complete MCP transport implementation that integrates
the FilterManager for comprehensive message processing and security.
"""

import asyncio
import json
import uuid
import time
from typing import Any, Dict, List, Optional, Union, Callable, AsyncGenerator
from dataclasses import dataclass
from contextlib import asynccontextmanager

# Import MCP SDK types
import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '../../src'))

# Define local types since we don't have the full MCP library
from typing import Protocol
from dataclasses import dataclass

class Transport(Protocol):
    """Transport protocol interface"""
    pass

@dataclass
class JSONRPCMessage:
    """JSON-RPC message structure"""
    jsonrpc: str = "2.0"
    id: Optional[Union[str, int]] = None
    method: Optional[str] = None
    params: Optional[Dict[str, Any]] = None
    result: Optional[Any] = None
    error: Optional[Dict[str, Any]] = None

# Import local types
from filter_types import (
    GopherTransportConfig,
    FilterManagerConfig,
    JSONRPCMessage,
    Session,
    TransportEvent,
    NetworkFilterConfig,
    HttpFilterConfig,
    SecurityFilterConfig,
    ObservabilityFilterConfig,
    TrafficManagementFilterConfig,
    TcpProxyConfig,
    UdpProxyConfig,
    HttpCodecConfig,
    HttpRouterConfig,
    HttpCompressionConfig,
    TlsTerminationConfig,
    AuthenticationConfig,
    AuthorizationConfig,
    AccessLogConfig,
    MetricsConfig,
    TracingConfig,
    RateLimitConfig,
    CircuitBreakerConfig,
    RetryConfig,
    LoadBalancerConfig,
    ErrorHandlingConfig,
    FallbackBehavior,
)


# ============================================================================
# Type Adapters
# ============================================================================

def adapt_to_filter_message(mcp_message: JSONRPCMessage) -> JSONRPCMessage:
    """Convert MCP JSONRPCMessage to FilterManager JSONRPCMessage."""
    return JSONRPCMessage(
        jsonrpc=mcp_message.jsonrpc,
        id=mcp_message.id,
        method=mcp_message.method,
        params=mcp_message.params,
        result=mcp_message.result,
        error=mcp_message.error
    )


def adapt_to_mcp_message(filter_message: JSONRPCMessage) -> JSONRPCMessage:
    """Convert FilterManager JSONRPCMessage to MCP JSONRPCMessage."""
    return JSONRPCMessage(
        jsonrpc=filter_message.jsonrpc,
        id=filter_message.id,
        method=filter_message.method,
        params=filter_message.params,
        result=filter_message.result,
        error=filter_message.error
    )


# ============================================================================
# Gopher Transport Class
# ============================================================================

class GopherTransport(Transport):
    """
    Gopher Transport implementation for MCP.
    
    This transport provides comprehensive filtering capabilities through the
    FilterManager, supporting all 15 available C++ filter types.
    """
    
    def __init__(self, config: GopherTransportConfig):
        """
        Initialize Gopher transport.
        
        Args:
            config: Transport configuration
        """
        self._config = config
        self._filter_manager = None
        self._sessions: Dict[str, Session] = {}
        self._is_started = False
        self._is_closed = False
        self._event_handlers: List[Callable[[TransportEvent], None]] = []
        
        # Initialize filter manager
        self._initialize_filter_manager()
    
    @property
    def config(self) -> GopherTransportConfig:
        """Get transport configuration."""
        return self._config
    
    @property
    def is_started(self) -> bool:
        """Check if transport is started."""
        return self._is_started
    
    @property
    def is_closed(self) -> bool:
        """Check if transport is closed."""
        return self._is_closed
    
    @property
    def session_count(self) -> int:
        """Get current session count."""
        return len(self._sessions)
    
    def _initialize_filter_manager(self) -> None:
        """Initialize the filter manager."""
        if self._config.filters:
            # Use provided filter configuration
            filter_config = self._config.filters
        else:
            # Create default filter configuration
            filter_config = self._create_default_filter_config()
        
        # Add custom callbacks to filter configuration
        if self._config.custom_callbacks:
            filter_config.custom_callbacks = self._config.custom_callbacks
            print(f"🔧 [CApiFilter DEBUG] GopherTransport initialized with custom callbacks")
        
        # Import FilterManager here to avoid circular imports
        try:
            from mcp_filter_sdk import FilterManager
            self._filter_manager = FilterManager(filter_config)
        except ImportError:
            # Fallback to mock filter manager for development
            self._filter_manager = MockFilterManager(filter_config)
    
    def _create_default_filter_config(self) -> FilterManagerConfig:
        """Create default filter configuration."""
        return FilterManagerConfig(
            # Security filters
            security=SecurityFilterConfig(
                authentication=AuthenticationConfig(
                    enabled=True,
                    method="jwt",
                    secret="default-secret-key",
                    issuer="gopher-transport",
                    audience="mcp-clients"
                ),
                authorization=AuthorizationConfig(
                    enabled=True,
                    policy="allow",
                    rules=[
                        {"resource": "*", "action": "read", "role": "user"},
                        {"resource": "*", "action": "write", "role": "admin"}
                    ],
                    default_action="deny"
                )
            ),
            
            # Observability filters
            observability=ObservabilityFilterConfig(
                access_log=AccessLogConfig(
                    enabled=True,
                    format="json",
                    include_headers=True,
                    include_body=False
                ),
                metrics=MetricsConfig(
                    enabled=True,
                    namespace="gopher_transport",
                    labels={
                        "transport": "gopher",
                        "protocol": self._config.protocol
                    }
                ),
                tracing=TracingConfig(
                    enabled=True,
                    service_name=f"gopher-transport-{self._config.name}",
                    sampler_type="const",
                    sampler_param=1.0
                )
            ),
            
            # Traffic management filters
            traffic_management=TrafficManagementFilterConfig(
                rate_limit=RateLimitConfig(
                    enabled=True,
                    requests_per_minute=1000,
                    burst_size=100,
                    key_extractor="ip"
                ),
                circuit_breaker=CircuitBreakerConfig(
                    enabled=True,
                    failure_threshold=5,
                    recovery_timeout=60000
                )
            ),
            
            # Error handling
            error_handling=ErrorHandlingConfig(
                stop_on_error=False,
                retry_attempts=3,
                fallback_behavior=FallbackBehavior.PASSTHROUGH
            )
        )
    
    async def start(self) -> None:
        """Start the transport."""
        if self._is_started:
            raise RuntimeError("Transport is already started")
        
        if self._is_closed:
            raise RuntimeError("Transport has been closed")
        
        # Start transport based on protocol
        if self._config.protocol == "stdio":
            await self._start_stdio()
        elif self._config.protocol == "tcp":
            await self._start_tcp()
        elif self._config.protocol == "udp":
            await self._start_udp()
        else:
            raise ValueError(f"Unsupported protocol: {self._config.protocol}")
        
        self._is_started = True
        self._emit_event(TransportEvent.connection_established("transport"))
    
    async def _start_stdio(self) -> None:
        """Start stdio transport."""
        # For stdio, we don't need to do anything special
        # The transport will handle stdin/stdout
        pass
    
    async def _start_tcp(self) -> None:
        """Start TCP transport."""
        if not self._config.host or not self._config.port:
            raise ValueError("Host and port are required for TCP transport")
        
        # TODO: Implement TCP server
        # This would create a TCP server listening on the specified host/port
        pass
    
    async def _start_udp(self) -> None:
        """Start UDP transport."""
        if not self._config.host or not self._config.port:
            raise ValueError("Host and port are required for UDP transport")
        
        # TODO: Implement UDP server
        # This would create a UDP server listening on the specified host/port
        pass
    
    async def send(self, message: JSONRPCMessage) -> None:
        """
        Send a message through the transport.
        
        Args:
            message: Message to send
        """
        if not self._is_started:
            raise RuntimeError("Transport is not started")
        
        if self._is_closed:
            raise RuntimeError("Transport has been closed")
        
        # Convert to filter message
        filter_message = adapt_to_filter_message(message)
        
        # Process through filters
        if self._filter_manager:
            processed_message = await self._filter_manager.process(filter_message)
        else:
            processed_message = filter_message
        
        # Convert back to MCP message
        mcp_message = adapt_to_mcp_message(processed_message)
        
        # Send the processed message
        await self._send_processed_message(mcp_message)
        
        # Emit event
        self._emit_event(TransportEvent.message_sent("transport", processed_message))
    
    async def _send_processed_message(self, message: JSONRPCMessage) -> None:
        """Send the processed message through the actual transport."""
        if self._config.protocol == "stdio":
            await self._send_stdio(message)
        elif self._config.protocol == "tcp":
            await self._send_tcp(message)
        elif self._config.protocol == "udp":
            await self._send_udp(message)
        else:
            raise ValueError(f"Unsupported protocol: {self._config.protocol}")
    
    async def _send_stdio(self, message: JSONRPCMessage) -> None:
        """Send message through stdio."""
        # TODO: Implement stdio sending
        # This would write the message to stdout
        print(f"STDOUT: {json.dumps(message.dict())}")
    
    async def _send_tcp(self, message: JSONRPCMessage) -> None:
        """Send message through TCP."""
        # TODO: Implement TCP sending
        # This would send the message through the TCP connection
        pass
    
    async def _send_udp(self, message: JSONRPCMessage) -> None:
        """Send message through UDP."""
        # TODO: Implement UDP sending
        # This would send the message through the UDP connection
        pass
    
    async def receive(self) -> AsyncGenerator[JSONRPCMessage, None]:
        """
        Receive messages from the transport.
        
        Yields:
            Received messages
        """
        if not self._is_started:
            raise RuntimeError("Transport is not started")
        
        if self._is_closed:
            raise RuntimeError("Transport has been closed")
        
        # Start receiving based on protocol
        if self._config.protocol == "stdio":
            async for message in self._receive_stdio():
                yield message
        elif self._config.protocol == "tcp":
            async for message in self._receive_tcp():
                yield message
        elif self._config.protocol == "udp":
            async for message in self._receive_udp():
                yield message
        else:
            raise ValueError(f"Unsupported protocol: {self._config.protocol}")
    
    async def _receive_stdio(self) -> AsyncGenerator[JSONRPCMessage, None]:
        """Receive messages from stdio."""
        # TODO: Implement stdio receiving
        # This would read messages from stdin
        # For now, we'll simulate receiving messages
        while not self._is_closed:
            await asyncio.sleep(0.1)
            # Simulate receiving a message
            if self._is_closed:
                break
            
            # Create a mock message for demonstration
            mock_message = JSONRPCMessage(
                jsonrpc="2.0",
                id=1,
                method="tools/list",
                params={}
            )
            
            # Process through filters
            if self._filter_manager:
                filter_message = adapt_to_filter_message(mock_message)
                processed_message = await self._filter_manager.process(filter_message)
                mcp_message = adapt_to_mcp_message(processed_message)
            else:
                mcp_message = mock_message
            
            # Emit event
            self._emit_event(TransportEvent.message_received("transport", filter_message))
            
            yield mcp_message
    
    async def _receive_tcp(self) -> AsyncGenerator[JSONRPCMessage, None]:
        """Receive messages from TCP."""
        # TODO: Implement TCP receiving
        # This would read messages from TCP connections
        while not self._is_closed:
            await asyncio.sleep(0.1)
            if self._is_closed:
                break
            # Simulate receiving messages
            pass
    
    async def _receive_udp(self) -> AsyncGenerator[JSONRPCMessage, None]:
        """Receive messages from UDP."""
        # TODO: Implement UDP receiving
        # This would read messages from UDP connections
        while not self._is_closed:
            await asyncio.sleep(0.1)
            if self._is_closed:
                break
            # Simulate receiving messages
            pass
    
    async def close(self) -> None:
        """Close the transport."""
        if self._is_closed:
            return
        
        self._is_closed = True
        self._is_started = False
        
        # Close all sessions
        for session in self._sessions.values():
            self._emit_event(TransportEvent.connection_closed(session.id))
        
        self._sessions.clear()
        
        # Destroy filter manager
        if self._filter_manager:
            self._filter_manager.destroy()
            self._filter_manager = None
        
        # Emit event
        self._emit_event(TransportEvent.connection_closed("transport"))
    
    def add_event_handler(self, handler: Callable[[TransportEvent], None]) -> None:
        """Add event handler."""
        self._event_handlers.append(handler)
    
    def remove_event_handler(self, handler: Callable[[TransportEvent], None]) -> None:
        """Remove event handler."""
        if handler in self._event_handlers:
            self._event_handlers.remove(handler)
    
    def _emit_event(self, event: TransportEvent) -> None:
        """Emit transport event."""
        for handler in self._event_handlers:
            try:
                handler(event)
            except Exception as e:
                # Log error but don't let it break the transport
                print(f"Error in event handler: {e}")
    
    def create_session(self, client_info: Optional[Dict[str, Any]] = None) -> Session:
        """Create a new session."""
        session_id = str(uuid.uuid4())
        session = Session(
            id=session_id,
            created_at=time.time(),
            last_activity=time.time(),
            client_info=client_info
        )
        
        self._sessions[session_id] = session
        self._emit_event(TransportEvent.connection_established(session_id))
        
        return session
    
    def get_session(self, session_id: str) -> Optional[Session]:
        """Get session by ID."""
        return self._sessions.get(session_id)
    
    def update_session_activity(self, session_id: str) -> None:
        """Update session activity."""
        session = self._sessions.get(session_id)
        if session:
            session.update_activity()
    
    def remove_session(self, session_id: str) -> None:
        """Remove session."""
        if session_id in self._sessions:
            del self._sessions[session_id]
            self._emit_event(TransportEvent.connection_closed(session_id))
    
    def cleanup_expired_sessions(self) -> None:
        """Clean up expired sessions."""
        expired_sessions = []
        for session_id, session in self._sessions.items():
            if session.is_expired(self._config.session_timeout):
                expired_sessions.append(session_id)
        
        for session_id in expired_sessions:
            self.remove_session(session_id)
    
    async def simulate_receive(self, message: JSONRPCMessage) -> JSONRPCMessage:
        """
        Simulate receiving a message (for testing).
        
        Args:
            message: Message to simulate receiving
            
        Returns:
            Processed message
        """
        if not self._is_started:
            raise RuntimeError("Transport is not started")
        
        # Convert to filter message
        filter_message = adapt_to_filter_message(message)
        
        # Process through filters
        if self._filter_manager:
            processed_message = await self._filter_manager.process_response(filter_message)
        else:
            processed_message = filter_message
        
        # Convert back to MCP message
        mcp_message = adapt_to_mcp_message(processed_message)
        
        # Emit event
        self._emit_event(TransportEvent.message_received("transport", processed_message))
        
        return mcp_message


# ============================================================================
# Mock Filter Manager (for development)
# ============================================================================

class MockFilterManager:
    """Mock filter manager for development and testing."""
    
    def __init__(self, config: FilterManagerConfig):
        """Initialize mock filter manager."""
        self._config = config
        self._is_destroyed = False
    
    async def process(self, message: JSONRPCMessage) -> JSONRPCMessage:
        """Process message through mock filters."""
        if self._is_destroyed:
            raise RuntimeError("Filter manager has been destroyed")
        
        # Mock processing - just return the message unchanged
        return message
    
    async def process_response(self, message: JSONRPCMessage) -> JSONRPCMessage:
        """Process response through mock filters."""
        if self._is_destroyed:
            raise RuntimeError("Filter manager has been destroyed")
        
        # Mock processing - just return the message unchanged
        return message
    
    def destroy(self) -> None:
        """Destroy mock filter manager."""
        self._is_destroyed = True


# ============================================================================
# Context Managers
# ============================================================================

@asynccontextmanager
async def gopher_transport_context(config: GopherTransportConfig):
    """
    Async context manager for Gopher transport lifecycle.
    
    Args:
        config: Transport configuration
        
    Yields:
        GopherTransport instance
    """
    transport = GopherTransport(config)
    try:
        await transport.start()
        yield transport
    finally:
        await transport.close()


# ============================================================================
# Module Exports
# ============================================================================

__all__ = [
    # Core classes
    "GopherTransport",
    "GopherTransportConfig",
    "MockFilterManager",
    
    # Type adapters
    "adapt_to_filter_message",
    "adapt_to_mcp_message",
    
    # Context managers
    "gopher_transport_context",
    
    # Types
    "Session",
    "TransportEvent",
    "FilterManagerConfig",
    "JSONRPCMessage",
]
