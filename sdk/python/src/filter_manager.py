"""
High-level Filter Manager for JSON-RPC message processing.

This module provides a comprehensive FilterManager that supports all 15 available
C++ filter types and provides a simple interface for processing JSON-RPC messages
through a configurable filter pipeline.
"""

import json
import asyncio
import uuid
from typing import Any, Dict, List, Optional, Union, Callable
from dataclasses import dataclass, field
from enum import Enum
from contextlib import asynccontextmanager

from mcp_types import (
    McpFilter,
    McpFilterManager,
    McpDispatcher,
    McpConnection,
    McpResult,
    BuiltinFilterType,
    FilterStatus,
    MCP_OK,
    MCP_ERROR_INVALID_ARGUMENT,
    MCP_ERROR_NOT_FOUND,
    MCP_ERROR_INVALID_STATE,
    MCP_ERROR_RESOURCE_EXHAUSTED,
)

from ffi_bindings import (
    mcp_filter_lib,
    mcp_filter_create_builtin,
    mcp_filter_manager_create,
    mcp_filter_manager_add_filter,
    mcp_filter_manager_add_chain,
    mcp_filter_manager_initialize,
    mcp_filter_manager_release,
    check_result,
    create_mock_dispatcher,
    create_mock_connection,
)

from filter_api import (
    Filter,
    FilterConfig,
    create_builtin_filter_from_type,
    create_custom_filter,
    BuiltinFilterType,
)

from mcp_c_structs import (
    create_default_callbacks,
    validate_callback_signature,
)

from filter_chain import (
    FilterChain,
    FilterChainBuilder,
    ChainConfig,
    ChainExecutionMode,
    RoutingStrategy,
)


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
    
    # CApiFilter integration
    custom_callbacks: Optional[Dict[str, Any]] = None
    
    # Legacy configuration (for backward compatibility)
    auth: Optional[Dict[str, Any]] = None
    rate_limit: Optional[Dict[str, Any]] = None
    logging: bool = False
    metrics: bool = False
    error_handling: Optional[ErrorHandlingConfig] = None


# ============================================================================
# Filter Manager Class
# ============================================================================

class FilterManager:
    """
    High-level filter manager for JSON-RPC message processing.
    
    This class provides a comprehensive interface for processing JSON-RPC messages
    through a configurable filter pipeline supporting all 15 available C++ filter types.
    """
    
    def __init__(self, config: FilterManagerConfig):
        """
        Initialize filter manager.
        
        Args:
            config: Filter manager configuration
        """
        self._config = config
        self._manager_handle: Optional[McpFilterManager] = None
        self._dispatcher: Optional[McpDispatcher] = None
        self._connection: Optional[McpConnection] = None
        self._filters: Dict[str, McpFilter] = {}
        self._custom_filters: Dict[str, McpFilter] = {}
        self._is_initialized = False
        self._is_destroyed = False
        
        # Initialize the manager
        self._initialize()
    
    @property
    def config(self) -> FilterManagerConfig:
        """Get filter manager configuration."""
        return self._config
    
    @property
    def is_initialized(self) -> bool:
        """Check if manager is initialized."""
        return self._is_initialized
    
    @property
    def is_destroyed(self) -> bool:
        """Check if manager is destroyed."""
        return self._is_destroyed
    
    def _initialize(self) -> None:
        """Initialize the filter manager."""
        if self._is_destroyed:
            raise RuntimeError("Filter manager has been destroyed")
        
        # Create mock dispatcher and connection for now
        self._dispatcher = create_mock_dispatcher()
        self._connection = create_mock_connection()
        
        # Create filter manager
        self._manager_handle = create_filter_manager(self._connection, self._dispatcher)
        if self._manager_handle == 0:
            raise RuntimeError("Failed to create filter manager")
        
        # Setup filters based on configuration
        self._setup_filters()
        
        # Initialize the manager
        result = initialize_filter_manager(self._manager_handle)
        check_result(result)
        
        self._is_initialized = True
    
    def _setup_filters(self) -> None:
        """Setup filters based on configuration."""
        # Setup network filters
        if self._config.network:
            self._setup_network_filters()
        
        # Setup HTTP filters
        if self._config.http:
            self._setup_http_filters()
        
        # Setup security filters
        if self._config.security:
            self._setup_security_filters()
        
        # Setup observability filters
        if self._config.observability:
            self._setup_observability_filters()
        
        # Setup traffic management filters
        if self._config.traffic_management:
            self._setup_traffic_management_filters()
        
        # Setup custom filters
        if self._config.custom_filters:
            self._setup_custom_filters()
        
        # Setup CApiFilter custom callbacks
        if self._config.custom_callbacks:
            self._setup_custom_callbacks()
        
        # Setup legacy filters for backward compatibility
        self._setup_legacy_filters()
    
    def _setup_network_filters(self) -> None:
        """Setup network filters."""
        network_config = self._config.network
        
        if network_config.tcp_proxy and network_config.tcp_proxy.enabled:
            settings = {
                "upstream_host": network_config.tcp_proxy.upstream_host,
                "upstream_port": network_config.tcp_proxy.upstream_port,
                "timeout_ms": network_config.tcp_proxy.timeout_ms,
                "keep_alive": network_config.tcp_proxy.keep_alive,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.TCP_PROXY, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["tcp_proxy"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if network_config.udp_proxy and network_config.udp_proxy.enabled:
            settings = {
                "upstream_host": network_config.udp_proxy.upstream_host,
                "upstream_port": network_config.udp_proxy.upstream_port,
                "timeout_ms": network_config.udp_proxy.timeout_ms,
                "max_packet_size": network_config.udp_proxy.max_packet_size,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.UDP_PROXY, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["udp_proxy"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
    
    def _setup_http_filters(self) -> None:
        """Setup HTTP filters."""
        http_config = self._config.http
        
        if http_config.codec and http_config.codec.enabled:
            settings = {
                "version": http_config.codec.version,
                "max_header_size": http_config.codec.max_header_size,
                "max_body_size": http_config.codec.max_body_size,
                "chunked_encoding": http_config.codec.chunked_encoding,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.HTTP_CODEC, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["http_codec"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if http_config.router and http_config.router.enabled:
            settings = {
                "routes": http_config.router.routes,
                "default_route": http_config.router.default_route,
                "strip_prefix": http_config.router.strip_prefix,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.HTTP_ROUTER, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["http_router"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if http_config.compression and http_config.compression.enabled:
            settings = {
                "algorithms": http_config.compression.algorithms,
                "min_size": http_config.compression.min_size,
                "level": http_config.compression.level,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.HTTP_COMPRESSION, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["http_compression"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
    
    def _setup_security_filters(self) -> None:
        """Setup security filters."""
        security_config = self._config.security
        
        if security_config.tls_termination and security_config.tls_termination.enabled:
            settings = {
                "cert_file": security_config.tls_termination.cert_file,
                "key_file": security_config.tls_termination.key_file,
                "ca_file": security_config.tls_termination.ca_file,
                "verify_client": security_config.tls_termination.verify_client,
                "protocols": security_config.tls_termination.protocols,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.TLS_TERMINATION, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["tls_termination"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if security_config.authentication and security_config.authentication.enabled:
            settings = {
                "method": security_config.authentication.method,
                "secret": security_config.authentication.secret,
                "key": security_config.authentication.key,
                "issuer": security_config.authentication.issuer,
                "audience": security_config.authentication.audience,
                "algorithms": security_config.authentication.algorithms,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.AUTHENTICATION, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["authentication"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if security_config.authorization and security_config.authorization.enabled:
            settings = {
                "policy": security_config.authorization.policy,
                "rules": security_config.authorization.rules,
                "default_action": security_config.authorization.default_action,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.AUTHORIZATION, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["authorization"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
    
    def _setup_observability_filters(self) -> None:
        """Setup observability filters."""
        observability_config = self._config.observability
        
        if observability_config.access_log and observability_config.access_log.enabled:
            settings = {
                "format": observability_config.access_log.format,
                "include_headers": observability_config.access_log.include_headers,
                "include_body": observability_config.access_log.include_body,
                "max_body_size": observability_config.access_log.max_body_size,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.ACCESS_LOG, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["access_log"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if observability_config.metrics and observability_config.metrics.enabled:
            settings = {
                "namespace": observability_config.metrics.namespace,
                "labels": observability_config.metrics.labels,
                "histogram_buckets": observability_config.metrics.histogram_buckets,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.METRICS, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["metrics"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if observability_config.tracing and observability_config.tracing.enabled:
            settings = {
                "service_name": observability_config.tracing.service_name,
                "sampler_type": observability_config.tracing.sampler_type,
                "sampler_param": observability_config.tracing.sampler_param,
                "headers": observability_config.tracing.headers,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.TRACING, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["tracing"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
    
    def _setup_traffic_management_filters(self) -> None:
        """Setup traffic management filters."""
        traffic_config = self._config.traffic_management
        
        if traffic_config.rate_limit and traffic_config.rate_limit.enabled:
            settings = {
                "requests_per_minute": traffic_config.rate_limit.requests_per_minute,
                "burst_size": traffic_config.rate_limit.burst_size,
                "key_extractor": traffic_config.rate_limit.key_extractor,
                "custom_key_header": traffic_config.rate_limit.custom_key_header,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.RATE_LIMIT, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["rate_limit"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if traffic_config.circuit_breaker and traffic_config.circuit_breaker.enabled:
            settings = {
                "failure_threshold": traffic_config.circuit_breaker.failure_threshold,
                "recovery_timeout": traffic_config.circuit_breaker.recovery_timeout,
                "half_open_max_calls": traffic_config.circuit_breaker.half_open_max_calls,
                "slow_call_threshold": traffic_config.circuit_breaker.slow_call_threshold,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.CIRCUIT_BREAKER, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["circuit_breaker"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if traffic_config.retry and traffic_config.retry.enabled:
            settings = {
                "max_attempts": traffic_config.retry.max_attempts,
                "initial_delay": traffic_config.retry.initial_delay,
                "max_delay": traffic_config.retry.max_delay,
                "backoff_multiplier": traffic_config.retry.backoff_multiplier,
                "retryable_status_codes": traffic_config.retry.retryable_status_codes,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.RETRY, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["retry"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        if traffic_config.load_balancer and traffic_config.load_balancer.enabled:
            settings = {
                "strategy": traffic_config.load_balancer.strategy,
                "upstreams": traffic_config.load_balancer.upstreams,
                "health_check_interval": traffic_config.load_balancer.health_check_interval,
                "health_check_timeout": traffic_config.load_balancer.health_check_timeout,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.LOAD_BALANCER, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["load_balancer"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
    
    def _setup_custom_filters(self) -> None:
        """Setup custom filters."""
        for custom_config in self._config.custom_filters:
            if not custom_config.enabled:
                continue
            
            settings = {
                "name": custom_config.name,
                "type": custom_config.type,
                **custom_config.settings,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.CUSTOM, json.dumps(settings)
            )
            if filter_handle != 0:
                self._custom_filters[custom_config.name] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
    
    def _setup_custom_callbacks(self) -> None:
        """Setup CApiFilter custom callbacks."""
        print(f"🔧 [CApiFilter DEBUG] Setting up custom callbacks for FilterManager")
        
        # Validate callback signatures
        for callback_type, func in self._config.custom_callbacks.items():
            if func is not None and callback_type != "user_data":
                if not validate_callback_signature(func, callback_type):
                    print(f"⚠️ [CApiFilter DEBUG] Warning: Invalid signature for {callback_type} callback")
                    continue
        
        # Create custom filter with callbacks
        try:
            custom_filter = create_custom_filter(
                callbacks=self._config.custom_callbacks,
                dispatcher=self._dispatcher,
                name="filter-manager-custom"
            )
            
            # Add to custom filters
            self._custom_filters["custom_callbacks"] = custom_filter.handle
            add_filter_to_manager(self._manager_handle, custom_filter.handle)
            
            print(f"✅ [CApiFilter DEBUG] Custom callbacks filter added to FilterManager")
            
        except Exception as e:
            print(f"❌ [CApiFilter DEBUG] Failed to setup custom callbacks: {e}")
            # Continue without custom callbacks
    
    def _setup_legacy_filters(self) -> None:
        """Setup legacy filters for backward compatibility."""
        # Legacy authentication
        if self._config.auth:
            settings = {
                "method": self._config.auth.get("method", "jwt"),
                "secret": self._config.auth.get("secret"),
                "key": self._config.auth.get("key"),
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.AUTHENTICATION, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["auth"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        # Legacy rate limiting
        if self._config.rate_limit:
            settings = {
                "requests_per_minute": self._config.rate_limit.get("requests_per_minute", 1000),
                "burst_size": self._config.rate_limit.get("burst_size", 100),
                "key_extractor": self._config.rate_limit.get("key_extractor", "ip"),
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.RATE_LIMIT, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["rate_limit"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        # Legacy logging
        if self._config.logging:
            settings = {
                "format": "json",
                "include_headers": True,
                "include_body": False,
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.ACCESS_LOG, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["logging"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
        
        # Legacy metrics
        if self._config.metrics:
            settings = {
                "namespace": "mcp_filter",
                "labels": {},
            }
            filter_handle = create_builtin_filter(
                self._dispatcher, BuiltinFilterType.METRICS, json.dumps(settings)
            )
            if filter_handle != 0:
                self._filters["metrics"] = filter_handle
                add_filter_to_manager(self._manager_handle, filter_handle)
    
    async def process(self, message: Union[Dict[str, Any], JSONRPCMessage]) -> JSONRPCMessage:
        """
        Process JSON-RPC message through filter pipeline.
        
        Args:
            message: JSON-RPC message to process
            
        Returns:
            Processed message
        """
        if self._is_destroyed:
            raise RuntimeError("Filter manager has been destroyed")
        
        if not self._is_initialized:
            raise RuntimeError("Filter manager is not initialized")
        
        # Convert dict to JSONRPCMessage if needed
        if isinstance(message, dict):
            message = JSONRPCMessage(**message)
        
        # Validate message
        self._validate_message(message)
        
        try:
            # Process through filters
            processed_message = await self._process_through_filters(message)
            return processed_message
        except Exception as e:
            return self._handle_processing_error(message, e)
    
    async def process_response(self, message: Union[Dict[str, Any], JSONRPCMessage]) -> JSONRPCMessage:
        """
        Process JSON-RPC response through filter pipeline.
        
        Args:
            message: JSON-RPC response to process
            
        Returns:
            Processed response
        """
        if self._is_destroyed:
            raise RuntimeError("Filter manager has been destroyed")
        
        if not self._is_initialized:
            raise RuntimeError("Filter manager is not initialized")
        
        # Convert dict to JSONRPCMessage if needed
        if isinstance(message, dict):
            message = JSONRPCMessage(**message)
        
        # Validate message
        self._validate_message(message)
        
        try:
            # Process through response-specific filters
            processed_message = await self._process_through_response_filters(message)
            return processed_message
        except Exception as e:
            return self._handle_processing_error(message, e)
    
    async def process_request_response(
        self,
        request: Union[Dict[str, Any], JSONRPCMessage],
        response: Union[Dict[str, Any], JSONRPCMessage]
    ) -> tuple[JSONRPCMessage, JSONRPCMessage]:
        """
        Process both request and response through filter pipeline.
        
        Args:
            request: JSON-RPC request to process
            response: JSON-RPC response to process
            
        Returns:
            Tuple of (processed_request, processed_response)
        """
        if self._is_destroyed:
            raise RuntimeError("Filter manager has been destroyed")
        
        if not self._is_initialized:
            raise RuntimeError("Filter manager is not initialized")
        
        # Process request and response concurrently
        processed_request, processed_response = await asyncio.gather(
            self.process(request),
            self.process_response(response)
        )
        
        return processed_request, processed_response
    
    async def _process_through_filters(self, message: JSONRPCMessage) -> JSONRPCMessage:
        """Process message through all filters."""
        # Define filter processing order
        filter_chain = [
            "tcp_proxy", "udp_proxy",  # Network filters
            "tls_termination", "authentication", "authorization",  # Security filters
            "http_codec", "http_router", "http_compression",  # HTTP filters
            "rate_limit", "circuit_breaker", "retry", "load_balancer",  # Traffic management
            "access_log", "metrics", "tracing",  # Observability
        ]
        
        # Add custom filters
        filter_chain.extend(self._custom_filters.keys())
        
        # Add legacy filters
        if "auth" in self._filters:
            filter_chain.append("auth")
        if "logging" in self._filters:
            filter_chain.append("logging")
        
        # Process through each filter
        processed_message = message
        for filter_name in filter_chain:
            if filter_name in self._filters:
                processed_message = await self._process_through_filter(processed_message, filter_name)
        
        return processed_message
    
    async def _process_through_response_filters(self, message: JSONRPCMessage) -> JSONRPCMessage:
        """Process response through response-specific filters."""
        # Response filters (typically observability and HTTP compression)
        response_filter_chain = [
            "http_compression", "http_codec",  # HTTP filters
            "access_log", "metrics", "tracing",  # Observability
        ]
        
        # Add custom filters
        response_filter_chain.extend(self._custom_filters.keys())
        
        # Process through each filter
        processed_message = message
        for filter_name in response_filter_chain:
            if filter_name in self._filters:
                processed_message = await self._process_through_filter(processed_message, filter_name)
        
        return processed_message
    
    async def _process_through_filter(self, message: JSONRPCMessage, filter_name: str) -> JSONRPCMessage:
        """Process message through a specific filter."""
        # TODO: Implement actual filter processing
        # For now, just return the message unchanged
        return message
    
    def _validate_message(self, message: JSONRPCMessage) -> None:
        """Validate JSON-RPC message structure."""
        if not message.jsonrpc:
            raise ValueError("Missing jsonrpc field")
        
        if message.jsonrpc != "2.0":
            raise ValueError(f"Unsupported JSON-RPC version: {message.jsonrpc}")
        
        if not message.is_request() and not message.is_response() and not message.is_notification():
            raise ValueError("Invalid JSON-RPC message: must be request, response, or notification")
    
    def _handle_processing_error(self, original_message: JSONRPCMessage, error: Exception) -> JSONRPCMessage:
        """Handle processing errors based on configuration."""
        error_config = self._config.error_handling or ErrorHandlingConfig()
        
        if error_config.fallback_behavior == FallbackBehavior.REJECT:
            # Return error response
            return JSONRPCMessage(
                jsonrpc="2.0",
                id=original_message.id or "unknown",
                error={
                    "code": -32603,
                    "message": "Internal error",
                    "data": str(error)
                }
            )
        elif error_config.fallback_behavior == FallbackBehavior.PASSTHROUGH:
            # Return original message unchanged
            return original_message
        else:  # DEFAULT
            # Return error response with original message ID
            return JSONRPCMessage(
                jsonrpc="2.0",
                id=original_message.id or "unknown",
                error={
                    "code": -32603,
                    "message": "Internal error",
                    "data": str(error)
                }
            )
    
    def get_custom_filters(self) -> List[str]:
        """Get list of custom filter names."""
        return list(self._custom_filters.keys())
    
    def destroy(self) -> None:
        """Destroy filter manager and release all resources."""
        if self._is_destroyed:
            return
        
        # Release all filters
        self._release_all_filters()
        
        # Release filter manager
        if self._manager_handle:
            release_filter_manager(self._manager_handle)
            self._manager_handle = None
        
        self._is_destroyed = True
        self._is_initialized = False
    
    def _release_all_filters(self) -> None:
        """Release all filter handles."""
        # Release standard filters
        for filter_name, filter_handle in self._filters.items():
            # TODO: Implement filter release
            pass
        
        # Release custom filters
        for filter_name, filter_handle in self._custom_filters.items():
            # TODO: Implement filter release
            pass
        
        # Clear filter dictionaries
        self._filters.clear()
        self._custom_filters.clear()
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.destroy()
    
    def __del__(self):
        """Destructor."""
        if not self._is_destroyed:
            self.destroy()


# ============================================================================
# Context Managers
# ============================================================================

@asynccontextmanager
async def filter_manager_context(config: FilterManagerConfig):
    """
    Async context manager for filter manager lifecycle.
    
    Args:
        config: Filter manager configuration
        
    Yields:
        FilterManager instance
    """
    manager = FilterManager(config)
    try:
        yield manager
    finally:
        manager.destroy()


# ============================================================================
# Module Exports
# ============================================================================

__all__ = [
    # Core classes
    "FilterManager",
    "FilterManagerConfig",
    "JSONRPCMessage",
    
    # Filter configuration types
    "NetworkFilterConfig",
    "HttpFilterConfig",
    "SecurityFilterConfig",
    "ObservabilityFilterConfig",
    "TrafficManagementFilterConfig",
    "CustomFilterConfig",
    
    # Network filters
    "TcpProxyConfig",
    "UdpProxyConfig",
    
    # HTTP filters
    "HttpCodecConfig",
    "HttpRouterConfig",
    "HttpCompressionConfig",
    
    # Security filters
    "TlsTerminationConfig",
    "AuthenticationConfig",
    "AuthorizationConfig",
    
    # Observability filters
    "AccessLogConfig",
    "MetricsConfig",
    "TracingConfig",
    
    # Traffic management filters
    "RateLimitConfig",
    "CircuitBreakerConfig",
    "RetryConfig",
    "LoadBalancerConfig",
    
    # Custom filters
    "CustomFilterConfig",
    
    # Error handling
    "ErrorHandlingConfig",
    "FallbackBehavior",
    
    # Context managers
    "filter_manager_context",
]
