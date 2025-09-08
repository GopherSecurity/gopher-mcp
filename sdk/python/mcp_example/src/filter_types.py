"""
Local type definitions for MCP example.

This module provides local type definitions for the MCP example project,
resolving import issues and providing type compatibility.
"""

import json
from typing import Any, Dict, List, Optional, Union
from dataclasses import dataclass, field
from enum import Enum


# ============================================================================
# JSON-RPC Message Types
# ============================================================================

@dataclass
class JSONRPCMessage:
    """JSON-RPC message structure."""
    jsonrpc: str = "2.0"
    id: Optional[Union[str, int]] = None
    method: Optional[str] = None
    params: Optional[Union[Dict[str, Any], List[Any]]] = None
    result: Optional[Any] = None
    error: Optional[Dict[str, Any]] = None
    
    def is_request(self) -> bool:
        """Check if message is a request."""
        return self.method is not None and self.id is not None
    
    def is_response(self) -> bool:
        """Check if message is a response."""
        return self.result is not None or self.error is not None
    
    def is_notification(self) -> bool:
        """Check if message is a notification."""
        return self.method is not None and self.id is None


# ============================================================================
# Filter Configuration Types
# ============================================================================

class FallbackBehavior(Enum):
    """Fallback behavior for filter failures."""
    REJECT = "reject"
    PASSTHROUGH = "passthrough"
    DEFAULT = "default"


@dataclass
class ErrorHandlingConfig:
    """Error handling configuration."""
    stop_on_error: bool = False
    retry_attempts: int = 0
    fallback_behavior: FallbackBehavior = FallbackBehavior.PASSTHROUGH


# Network Filters
@dataclass
class TcpProxyConfig:
    """TCP proxy filter configuration."""
    enabled: bool = False
    upstream_host: Optional[str] = None
    upstream_port: Optional[int] = None
    timeout_ms: int = 30000
    keep_alive: bool = True


@dataclass
class UdpProxyConfig:
    """UDP proxy filter configuration."""
    enabled: bool = False
    upstream_host: Optional[str] = None
    upstream_port: Optional[int] = None
    timeout_ms: int = 5000
    max_packet_size: int = 65536


@dataclass
class NetworkFilterConfig:
    """Network filter configuration."""
    tcp_proxy: Optional[TcpProxyConfig] = None
    udp_proxy: Optional[UdpProxyConfig] = None


# HTTP Filters
@dataclass
class HttpCodecConfig:
    """HTTP codec filter configuration."""
    enabled: bool = False
    version: str = "1.1"
    max_header_size: int = 8192
    max_body_size: int = 1048576
    chunked_encoding: bool = True


@dataclass
class HttpRouterConfig:
    """HTTP router filter configuration."""
    enabled: bool = False
    routes: List[Dict[str, Any]] = field(default_factory=list)
    default_route: Optional[str] = None
    strip_prefix: bool = False


@dataclass
class HttpCompressionConfig:
    """HTTP compression filter configuration."""
    enabled: bool = False
    algorithms: List[str] = field(default_factory=lambda: ["gzip", "deflate"])
    min_size: int = 1024
    level: int = 6


@dataclass
class HttpFilterConfig:
    """HTTP filter configuration."""
    codec: Optional[HttpCodecConfig] = None
    router: Optional[HttpRouterConfig] = None
    compression: Optional[HttpCompressionConfig] = None


# Security Filters
@dataclass
class TlsTerminationConfig:
    """TLS termination filter configuration."""
    enabled: bool = False
    cert_file: Optional[str] = None
    key_file: Optional[str] = None
    ca_file: Optional[str] = None
    verify_client: bool = False
    protocols: List[str] = field(default_factory=lambda: ["TLSv1.2", "TLSv1.3"])


@dataclass
class AuthenticationConfig:
    """Authentication filter configuration."""
    enabled: bool = False
    method: str = "jwt"  # "jwt", "api-key", "basic", "oauth2"
    secret: Optional[str] = None
    key: Optional[str] = None
    issuer: Optional[str] = None
    audience: Optional[str] = None
    algorithms: List[str] = field(default_factory=lambda: ["HS256", "RS256"])


@dataclass
class AuthorizationConfig:
    """Authorization filter configuration."""
    enabled: bool = False
    policy: str = "allow"  # "allow", "deny"
    rules: List[Dict[str, Any]] = field(default_factory=list)
    default_action: str = "deny"


@dataclass
class SecurityFilterConfig:
    """Security filter configuration."""
    tls_termination: Optional[TlsTerminationConfig] = None
    authentication: Optional[AuthenticationConfig] = None
    authorization: Optional[AuthorizationConfig] = None


# Observability Filters
@dataclass
class AccessLogConfig:
    """Access log filter configuration."""
    enabled: bool = False
    format: str = "json"  # "json", "text", "apache", "nginx"
    include_headers: bool = True
    include_body: bool = False
    max_body_size: int = 4096


@dataclass
class MetricsConfig:
    """Metrics filter configuration."""
    enabled: bool = False
    namespace: str = "mcp_filter"
    labels: Dict[str, str] = field(default_factory=dict)
    histogram_buckets: List[float] = field(default_factory=lambda: [0.1, 0.5, 1.0, 2.5, 5.0, 10.0])


@dataclass
class TracingConfig:
    """Tracing filter configuration."""
    enabled: bool = False
    service_name: str = "mcp_filter"
    sampler_type: str = "const"  # "const", "probabilistic", "ratelimiting"
    sampler_param: float = 1.0
    headers: List[str] = field(default_factory=lambda: ["x-trace-id", "x-span-id"])


@dataclass
class ObservabilityFilterConfig:
    """Observability filter configuration."""
    access_log: Optional[AccessLogConfig] = None
    metrics: Optional[MetricsConfig] = None
    tracing: Optional[TracingConfig] = None


# Traffic Management Filters
@dataclass
class RateLimitConfig:
    """Rate limit filter configuration."""
    enabled: bool = False
    requests_per_minute: int = 1000
    burst_size: int = 100
    key_extractor: str = "ip"  # "ip", "user", "custom"
    custom_key_header: Optional[str] = None


@dataclass
class CircuitBreakerConfig:
    """Circuit breaker filter configuration."""
    enabled: bool = False
    failure_threshold: int = 5
    recovery_timeout: int = 60000
    half_open_max_calls: int = 3
    slow_call_threshold: int = 5000


@dataclass
class RetryConfig:
    """Retry filter configuration."""
    enabled: bool = False
    max_attempts: int = 3
    initial_delay: int = 1000
    max_delay: int = 10000
    backoff_multiplier: float = 2.0
    retryable_status_codes: List[int] = field(default_factory=lambda: [500, 502, 503, 504])


@dataclass
class LoadBalancerConfig:
    """Load balancer filter configuration."""
    enabled: bool = False
    strategy: str = "round_robin"  # "round_robin", "least_connections", "weighted"
    upstreams: List[Dict[str, Any]] = field(default_factory=list)
    health_check_interval: int = 30000
    health_check_timeout: int = 5000


@dataclass
class TrafficManagementFilterConfig:
    """Traffic management filter configuration."""
    rate_limit: Optional[RateLimitConfig] = None
    circuit_breaker: Optional[CircuitBreakerConfig] = None
    retry: Optional[RetryConfig] = None
    load_balancer: Optional[LoadBalancerConfig] = None


# Custom Filters
@dataclass
class CustomFilterConfig:
    """Custom filter configuration."""
    name: str
    type: str
    settings: Dict[str, Any] = field(default_factory=dict)
    enabled: bool = True


# Main Configuration
@dataclass
class FilterManagerConfig:
    """Main filter manager configuration."""
    # Filter categories
    network: Optional[NetworkFilterConfig] = None
    http: Optional[HttpFilterConfig] = None
    security: Optional[SecurityFilterConfig] = None
    observability: Optional[ObservabilityFilterConfig] = None
    traffic_management: Optional[TrafficManagementFilterConfig] = None
    custom_filters: List[CustomFilterConfig] = field(default_factory=list)
    
    # Legacy configuration (for backward compatibility)
    auth: Optional[Dict[str, Any]] = None
    rate_limit: Optional[Dict[str, Any]] = None
    logging: bool = False
    metrics: bool = False
    error_handling: Optional[ErrorHandlingConfig] = None


# ============================================================================
# Transport Configuration
# ============================================================================

@dataclass
class GopherTransportConfig:
    """Gopher transport configuration."""
    name: str
    protocol: str = "stdio"  # "tcp", "udp", "stdio"
    host: Optional[str] = None
    port: Optional[int] = None
    timeout: int = 30000
    max_connections: int = 100
    buffer_size: int = 4096
    
    # Filter configuration
    filters: Optional[FilterManagerConfig] = None
    
    # CApiFilter integration
    custom_callbacks: Optional[Dict[str, Any]] = None
    
    # Session management
    session_timeout: int = 3600000  # 1 hour
    max_sessions: int = 1000
    
    # Security
    tls_enabled: bool = False
    tls_cert_file: Optional[str] = None
    tls_key_file: Optional[str] = None
    tls_ca_file: Optional[str] = None
    
    # Performance
    keep_alive: bool = True
    keep_alive_interval: int = 30000
    max_idle_time: int = 300000


# ============================================================================
# Session Management
# ============================================================================

@dataclass
class Session:
    """Session information."""
    id: str
    created_at: float
    last_activity: float
    client_info: Optional[Dict[str, Any]] = None
    metadata: Dict[str, Any] = field(default_factory=dict)
    
    def is_expired(self, timeout: int) -> bool:
        """Check if session is expired."""
        import time
        return (time.time() - self.last_activity) > (timeout / 1000)
    
    def update_activity(self) -> None:
        """Update last activity timestamp."""
        import time
        self.last_activity = time.time()


# ============================================================================
# Transport Events
# ============================================================================

@dataclass
class TransportEvent:
    """Transport event structure."""
    type: str
    timestamp: float
    session_id: Optional[str] = None
    data: Optional[Dict[str, Any]] = None
    
    @classmethod
    def connection_established(cls, session_id: str) -> 'TransportEvent':
        """Create connection established event."""
        import time
        return cls(
            type="connection_established",
            timestamp=time.time(),
            session_id=session_id
        )
    
    @classmethod
    def connection_closed(cls, session_id: str) -> 'TransportEvent':
        """Create connection closed event."""
        import time
        return cls(
            type="connection_closed",
            timestamp=time.time(),
            session_id=session_id
        )
    
    @classmethod
    def message_received(cls, session_id: str, message: JSONRPCMessage) -> 'TransportEvent':
        """Create message received event."""
        import time
        return cls(
            type="message_received",
            timestamp=time.time(),
            session_id=session_id,
            data={"message": message}
        )
    
    @classmethod
    def message_sent(cls, session_id: str, message: JSONRPCMessage) -> 'TransportEvent':
        """Create message sent event."""
        import time
        return cls(
            type="message_sent",
            timestamp=time.time(),
            session_id=session_id,
            data={"message": message}
        )
    
    @classmethod
    def error_occurred(cls, session_id: Optional[str], error: Exception) -> 'TransportEvent':
        """Create error occurred event."""
        import time
        return cls(
            type="error_occurred",
            timestamp=time.time(),
            session_id=session_id,
            data={"error": str(error), "error_type": type(error).__name__}
        )
