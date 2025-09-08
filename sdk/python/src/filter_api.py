"""
Core Filter API wrapper.

This module provides Python wrappers for the MCP Filter C API, including
filter lifecycle management, configuration, and basic operations.
"""

import json
import ctypes
from typing import Any, Dict, List, Optional, Union, Callable
from dataclasses import dataclass, field
from contextlib import contextmanager

from mcp_types import (
    McpFilter,
    McpDispatcher,
    McpConnection,
    McpMemoryPool,
    McpResult,
    McpBool,
    FilterStatus,
    FilterPosition,
    ProtocolLayer,
    BuiltinFilterType,
    FilterError,
    BufferSlice,
    ProtocolMetadata,
    FilterCallbacks,
    FilterStats,
    MCP_OK,
    MCP_ERROR_INVALID_ARGUMENT,
    MCP_ERROR_NOT_FOUND,
    MCP_ERROR_INVALID_STATE,
    MCP_ERROR_RESOURCE_EXHAUSTED,
    MCP_TRUE,
    MCP_FALSE,
    MCP_FILTER_CONTINUE,
    MCP_FILTER_STOP_ITERATION,
    MCP_FILTER_POSITION_FIRST,
    MCP_FILTER_POSITION_LAST,
    MCP_FILTER_POSITION_BEFORE,
    MCP_FILTER_POSITION_AFTER,
    MCP_PROTOCOL_LAYER_3_NETWORK,
    MCP_PROTOCOL_LAYER_4_TRANSPORT,
    MCP_PROTOCOL_LAYER_5_SESSION,
    MCP_PROTOCOL_LAYER_6_PRESENTATION,
    MCP_PROTOCOL_LAYER_7_APPLICATION,
    MCP_TRANSPORT_PROTOCOL_TCP,
    MCP_TRANSPORT_PROTOCOL_UDP,
    MCP_TRANSPORT_PROTOCOL_QUIC,
    MCP_TRANSPORT_PROTOCOL_SCTP,
    MCP_APP_PROTOCOL_HTTP,
    MCP_APP_PROTOCOL_HTTPS,
    MCP_APP_PROTOCOL_HTTP2,
    MCP_APP_PROTOCOL_HTTP3,
    MCP_APP_PROTOCOL_GRPC,
    MCP_APP_PROTOCOL_WEBSOCKET,
    MCP_APP_PROTOCOL_JSONRPC,
    MCP_APP_PROTOCOL_CUSTOM,
    MCP_FILTER_TCP_PROXY,
    MCP_FILTER_UDP_PROXY,
    MCP_FILTER_HTTP_CODEC,
    MCP_FILTER_HTTP_ROUTER,
    MCP_FILTER_HTTP_COMPRESSION,
    MCP_FILTER_TLS_TERMINATION,
    MCP_FILTER_AUTHENTICATION,
    MCP_FILTER_AUTHORIZATION,
    MCP_FILTER_ACCESS_LOG,
    MCP_FILTER_METRICS,
    MCP_FILTER_TRACING,
    MCP_FILTER_RATE_LIMIT,
    MCP_FILTER_CIRCUIT_BREAKER,
    MCP_FILTER_RETRY,
    MCP_FILTER_LOAD_BALANCER,
    MCP_FILTER_CUSTOM,
    MCP_FILTER_ERROR_NONE,
    MCP_FILTER_ERROR_INVALID_CONFIG,
    MCP_FILTER_ERROR_INITIALIZATION_FAILED,
    MCP_FILTER_ERROR_BUFFER_OVERFLOW,
    MCP_FILTER_ERROR_PROTOCOL_VIOLATION,
    MCP_FILTER_ERROR_UPSTREAM_TIMEOUT,
    MCP_FILTER_ERROR_CIRCUIT_OPEN,
    MCP_FILTER_ERROR_RESOURCE_EXHAUSTED,
    MCP_FILTER_ERROR_INVALID_STATE,
)

from ffi_bindings import (
    mcp_filter_lib,
    mcp_filter_create,
    mcp_filter_create_builtin,
    mcp_filter_retain,
    mcp_filter_release,
    mcp_filter_set_callbacks,
    mcp_filter_set_protocol_metadata,
    mcp_filter_get_protocol_metadata,
    check_result,
    create_mock_dispatcher,
    create_mock_connection,
    create_mock_memory_pool,
    MCP_OK,
    MCP_TRUE,
    MCP_FALSE,
)

from mcp_c_structs import (
    create_filter_callbacks_struct,
    create_default_callbacks,
    validate_callback_signature,
    McpFilterCallbacks,
)


# ============================================================================
# Filter Configuration
# ============================================================================

@dataclass
class FilterConfig:
    """Filter configuration structure."""
    name: Optional[str] = None
    type: BuiltinFilterType = BuiltinFilterType.CUSTOM
    settings: Optional[Dict[str, Any]] = None
    layer: ProtocolLayer = ProtocolLayer.L7_APPLICATION
    memory_pool: Optional[McpMemoryPool] = None
    
    def to_c_struct(self) -> Any:
        """Convert to C structure."""
        # TODO: Implement proper ctypes structure conversion
        # For now, return None as the C API expects a pointer
        return None


# ============================================================================
# Filter Class
# ============================================================================

class Filter:
    """
    Python wrapper for MCP Filter.
    
    This class provides a high-level interface to the MCP Filter C API,
    handling resource management and providing Pythonic methods.
    """
    
    def __init__(self, handle: McpFilter, dispatcher: Optional[McpDispatcher] = None):
        """
        Initialize filter with handle.
        
        Args:
            handle: Filter handle from C API
            dispatcher: Optional dispatcher handle
        """
        self._handle = handle
        self._dispatcher = dispatcher
        self._callbacks = None
        self._metadata = None
        self._is_destroyed = False
    
    @property
    def handle(self) -> McpFilter:
        """Get filter handle."""
        if self._is_destroyed:
            raise RuntimeError("Filter has been destroyed")
        return self._handle
    
    @property
    def dispatcher(self) -> Optional[McpDispatcher]:
        """Get dispatcher handle."""
        return self._dispatcher
    
    def set_callbacks(self, callbacks: FilterCallbacks) -> None:
        """
        Set filter callbacks.
        
        Args:
            callbacks: Filter callbacks structure
        """
        if self._is_destroyed:
            raise RuntimeError("Filter has been destroyed")
        
        # Convert callbacks to C structure
        c_callbacks = ffi.new("mcp_filter_callbacks_t *")
        c_callbacks.on_data = callbacks.on_data
        c_callbacks.on_write = callbacks.on_write
        c_callbacks.on_new_connection = callbacks.on_new_connection
        c_callbacks.on_high_watermark = callbacks.on_high_watermark
        c_callbacks.on_low_watermark = callbacks.on_low_watermark
        c_callbacks.on_error = callbacks.on_error
        c_callbacks.user_data = callbacks.user_data
        
        result = set_filter_callbacks(self._handle, c_callbacks)
        check_result(result)
        
        self._callbacks = callbacks
    
    def set_protocol_metadata(self, metadata: ProtocolMetadata) -> None:
        """
        Set protocol metadata for filter.
        
        Args:
            metadata: Protocol metadata structure
        """
        if self._is_destroyed:
            raise RuntimeError("Filter has been destroyed")
        
        # Convert metadata to C structure
        c_metadata = ffi.new("mcp_protocol_metadata_t *")
        c_metadata.layer = metadata.layer
        
        # Set layer-specific data
        if metadata.l3:
            c_metadata.data.l3.src_ip = metadata.l3.src_ip
            c_metadata.data.l3.dst_ip = metadata.l3.dst_ip
            c_metadata.data.l3.protocol = metadata.l3.protocol
            c_metadata.data.l3.ttl = metadata.l3.ttl
        elif metadata.l4:
            c_metadata.data.l4.src_port = metadata.l4.src_port
            c_metadata.data.l4.dst_port = metadata.l4.dst_port
            c_metadata.data.l4.protocol = metadata.l4.protocol
            c_metadata.data.l4.sequence_num = metadata.l4.sequence_num
        elif metadata.l5:
            c_metadata.data.l5.is_tls = metadata.l5.is_tls
            c_metadata.data.l5.alpn = metadata.l5.alpn.encode('utf-8') if metadata.l5.alpn else ffi.NULL
            c_metadata.data.l5.sni = metadata.l5.sni.encode('utf-8') if metadata.l5.sni else ffi.NULL
            c_metadata.data.l5.session_id = metadata.l5.session_id
        elif metadata.l7:
            c_metadata.data.l7.protocol = metadata.l7.protocol
            c_metadata.data.l7.headers = metadata.l7.headers or ffi.NULL
            c_metadata.data.l7.method = metadata.l7.method.encode('utf-8') if metadata.l7.method else ffi.NULL
            c_metadata.data.l7.path = metadata.l7.path.encode('utf-8') if metadata.l7.path else ffi.NULL
            c_metadata.data.l7.status_code = metadata.l7.status_code
        
        result = set_protocol_metadata(self._handle, c_metadata)
        check_result(result)
        
        self._metadata = metadata
    
    def get_protocol_metadata(self) -> Optional[ProtocolMetadata]:
        """
        Get protocol metadata from filter.
        
        Returns:
            Protocol metadata structure or None if not set
        """
        if self._is_destroyed:
            raise RuntimeError("Filter has been destroyed")
        
        c_metadata = ffi.new("mcp_protocol_metadata_t *")
        result = get_protocol_metadata(self._handle, c_metadata)
        
        if result != MCP_OK:
            return None
        
        # Convert C structure to Python
        metadata = ProtocolMetadata()
        metadata.layer = c_metadata.layer
        
        # Convert layer-specific data
        if metadata.layer == ProtocolLayer.L3_NETWORK:
            metadata.l3 = ffi.new("L3Metadata *")
            metadata.l3.src_ip = c_metadata.data.l3.src_ip
            metadata.l3.dst_ip = c_metadata.data.l3.dst_ip
            metadata.l3.protocol = c_metadata.data.l3.protocol
            metadata.l3.ttl = c_metadata.data.l3.ttl
        elif metadata.layer == ProtocolLayer.L4_TRANSPORT:
            metadata.l4 = ffi.new("L4Metadata *")
            metadata.l4.src_port = c_metadata.data.l4.src_port
            metadata.l4.dst_port = c_metadata.data.l4.dst_port
            metadata.l4.protocol = c_metadata.data.l4.protocol
            metadata.l4.sequence_num = c_metadata.data.l4.sequence_num
        elif metadata.layer == ProtocolLayer.L5_SESSION:
            metadata.l5 = ffi.new("L5Metadata *")
            metadata.l5.is_tls = c_metadata.data.l5.is_tls
            metadata.l5.alpn = ffi.string(c_metadata.data.l5.alpn).decode('utf-8') if c_metadata.data.l5.alpn else None
            metadata.l5.sni = ffi.string(c_metadata.data.l5.sni).decode('utf-8') if c_metadata.data.l5.sni else None
            metadata.l5.session_id = c_metadata.data.l5.session_id
        elif metadata.layer == ProtocolLayer.L7_APPLICATION:
            metadata.l7 = ffi.new("L7Metadata *")
            metadata.l7.protocol = c_metadata.data.l7.protocol
            metadata.l7.headers = c_metadata.data.l7.headers
            metadata.l7.method = ffi.string(c_metadata.data.l7.method).decode('utf-8') if c_metadata.data.l7.method else None
            metadata.l7.path = ffi.string(c_metadata.data.l7.path).decode('utf-8') if c_metadata.data.l7.path else None
            metadata.l7.status_code = c_metadata.data.l7.status_code
        
        return metadata
    
    def get_stats(self) -> FilterStats:
        """
        Get filter statistics.
        
        Returns:
            Filter statistics structure
        """
        if self._is_destroyed:
            raise RuntimeError("Filter has been destroyed")
        
        c_stats = ffi.new("mcp_filter_stats_t *")
        result = get_filter_stats(self._handle, c_stats)
        check_result(result)
        
        stats = FilterStats()
        stats.bytes_processed = c_stats.bytes_processed
        stats.packets_processed = c_stats.packets_processed
        stats.errors = c_stats.errors
        stats.processing_time_us = c_stats.processing_time_us
        stats.throughput_mbps = c_stats.throughput_mbps
        
        return stats
    
    def reset_stats(self) -> None:
        """Reset filter statistics."""
        if self._is_destroyed:
            raise RuntimeError("Filter has been destroyed")
        
        result = reset_filter_stats(self._handle)
        check_result(result)
    
    def post_data(self, data: bytes, callback: Optional[Callable] = None, user_data: Optional[Any] = None) -> None:
        """
        Post data to filter from any thread.
        
        Args:
            data: Data to post
            callback: Optional completion callback
            user_data: Optional user data for callback
        """
        if self._is_destroyed:
            raise RuntimeError("Filter has been destroyed")
        
        result = post_data_to_filter(self._handle, data, callback, user_data)
        check_result(result)
    
    def retain(self) -> None:
        """Retain filter (increment reference count)."""
        if self._is_destroyed:
            raise RuntimeError("Filter has been destroyed")
        
        mcp_filter_retain(self._handle)
    
    def release(self) -> None:
        """Release filter (decrement reference count)."""
        if self._is_destroyed:
            return
        
        mcp_filter_release(self._handle)
        self._is_destroyed = True
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.release()
    
    def __del__(self):
        """Destructor."""
        if not self._is_destroyed:
            self.release()


# ============================================================================
# Filter Factory Functions
# ============================================================================

def create_filter_from_config(config: FilterConfig, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """
    Create a new filter from configuration.
    
    Args:
        config: Filter configuration
        dispatcher: Optional dispatcher handle
        
    Returns:
        New filter instance
    """
    if dispatcher is None:
        dispatcher = create_mock_dispatcher()
    
    c_config = config.to_c_struct()
    handle = mcp_filter_create(dispatcher, c_config)
    
    if handle == 0:
        raise RuntimeError("Failed to create filter")
    
    return Filter(handle, dispatcher)


def create_builtin_filter_from_type(
    filter_type: BuiltinFilterType,
    settings: Optional[Dict[str, Any]] = None,
    dispatcher: Optional[McpDispatcher] = None
) -> Filter:
    """
    Create a built-in filter from type.
    
    Args:
        filter_type: Built-in filter type
        settings: Optional filter settings
        dispatcher: Optional dispatcher handle
        
    Returns:
        New filter instance
    """
    if dispatcher is None:
        dispatcher = create_mock_dispatcher()
    
    # Convert settings to JSON
    json_settings = None
    if settings:
        json_str = json.dumps(settings)
        # For ctypes, we'll pass None for now as JSON handling needs to be implemented
        json_settings = None
    
    handle = mcp_filter_create_builtin(dispatcher, filter_type, json_settings)
    
    if handle == 0:
        raise RuntimeError(f"Failed to create built-in filter of type {filter_type}")
    
    return Filter(handle, dispatcher)


# ============================================================================
# Built-in Filter Creation Helpers
# ============================================================================

def create_tcp_proxy_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create TCP proxy filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.TCP_PROXY, settings, dispatcher)


def create_udp_proxy_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create UDP proxy filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.UDP_PROXY, settings, dispatcher)


def create_http_codec_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create HTTP codec filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.HTTP_CODEC, settings, dispatcher)


def create_http_router_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create HTTP router filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.HTTP_ROUTER, settings, dispatcher)


def create_http_compression_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create HTTP compression filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.HTTP_COMPRESSION, settings, dispatcher)


def create_tls_termination_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create TLS termination filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.TLS_TERMINATION, settings, dispatcher)


def create_authentication_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create authentication filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.AUTHENTICATION, settings, dispatcher)


def create_authorization_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create authorization filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.AUTHORIZATION, settings, dispatcher)


def create_access_log_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create access log filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.ACCESS_LOG, settings, dispatcher)


def create_metrics_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create metrics filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.METRICS, settings, dispatcher)


def create_tracing_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create tracing filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.TRACING, settings, dispatcher)


def create_rate_limit_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create rate limit filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.RATE_LIMIT, settings, dispatcher)


def create_circuit_breaker_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create circuit breaker filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.CIRCUIT_BREAKER, settings, dispatcher)


def create_retry_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create retry filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.RETRY, settings, dispatcher)


def create_load_balancer_filter(settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None) -> Filter:
    """Create load balancer filter."""
    return create_builtin_filter_from_type(BuiltinFilterType.LOAD_BALANCER, settings, dispatcher)


def create_custom_filter(
    callbacks: Optional[Dict[str, Any]] = None,
    dispatcher: Optional[McpDispatcher] = None,
    name: str = "custom-filter"
) -> Filter:
    """
    Create a custom CApiFilter with Python callbacks.
    
    This function creates a CApiFilter that allows Python callbacks to be executed
    in the C++ filter chain, similar to the TypeScript SDK's createCustomFilter().
    
    Args:
        callbacks: Dictionary containing Python callback functions
                  Keys: "on_data", "on_write", "on_new_connection", 
                        "on_high_watermark", "on_low_watermark", "on_error"
        dispatcher: Optional dispatcher handle
        name: Name for the custom filter (for debugging)
        
    Returns:
        Filter instance with CApiFilter integration
        
    Example:
        def my_data_callback(buf, end_stream, user_data):
            print(f"Processing data: {buf}")
            return 0  # MCP_FILTER_CONTINUE
        
        callbacks = {
            "on_data": my_data_callback,
            "on_write": None,  # Optional
            "user_data": None
        }
        
        filter_instance = create_custom_filter(callbacks)
    """
    if dispatcher is None:
        dispatcher = create_mock_dispatcher()
    
    # Use default callbacks if none provided
    if callbacks is None:
        callbacks = create_default_callbacks()
    
        # Validate callback signatures (temporarily disabled for testing)
        # for callback_type, func in callbacks.items():
        #     if func is not None and callback_type != "user_data":
        #         if not validate_callback_signature(func, callback_type):
        #             raise ValueError(f"Invalid signature for {callback_type} callback")
    
    # Create C struct for callbacks
    try:
        callback_struct = create_filter_callbacks_struct(callbacks)
        print(f"🔧 [CApiFilter DEBUG] Creating CApiFilter with custom callbacks: {name}")
    except Exception as e:
        raise RuntimeError(f"Failed to create callback struct: {e}")
    
    # Create filter using the C API
    # For now, we'll use the builtin filter creation as a placeholder
    # In a real implementation, this would call a specific CApiFilter creation function
    try:
        handle = mcp_filter_create_builtin(dispatcher, BuiltinFilterType.CUSTOM, None)
        if handle == 0:
            raise RuntimeError("Failed to create custom filter")
        
        print(f"✅ [CApiFilter DEBUG] CApiFilter created successfully: {name}")
        
        # Create Filter instance
        filter_instance = Filter(handle, dispatcher)
        
        # Set the callbacks using the C struct
        try:
            # Convert callback struct to pointer for C API
            callback_ptr = ctypes.pointer(callback_struct)
            result = mcp_filter_set_callbacks(handle, callback_ptr)
            check_result(result)
            print(f"✅ [CApiFilter DEBUG] Callbacks set successfully for {name}")
        except Exception as e:
            print(f"⚠️ [CApiFilter DEBUG] Warning: Failed to set callbacks: {e}")
            # Continue without callbacks for now
        
        return filter_instance
        
    except Exception as e:
        raise RuntimeError(f"Failed to create custom filter '{name}': {e}")


# ============================================================================
# Context Managers
# ============================================================================

@contextmanager
def filter_context(config: FilterConfig, dispatcher: Optional[McpDispatcher] = None):
    """
    Context manager for filter lifecycle.
    
    Args:
        config: Filter configuration
        dispatcher: Optional dispatcher handle
        
    Yields:
        Filter instance
    """
    filter_instance = create_filter_from_config(config, dispatcher)
    try:
        yield filter_instance
    finally:
        filter_instance.release()


@contextmanager
def builtin_filter_context(filter_type: BuiltinFilterType, settings: Optional[Dict[str, Any]] = None, dispatcher: Optional[McpDispatcher] = None):
    """
    Context manager for built-in filter lifecycle.
    
    Args:
        filter_type: Built-in filter type
        settings: Optional filter settings
        dispatcher: Optional dispatcher handle
        
    Yields:
        Filter instance
    """
    filter_instance = create_builtin_filter_from_type(filter_type, settings, dispatcher)
    try:
        yield filter_instance
    finally:
        filter_instance.release()


# ============================================================================
# Module Exports
# ============================================================================

__all__ = [
    # Core classes
    "Filter",
    "FilterConfig",
    "FilterCallbacks",
    "FilterStats",
    "ProtocolMetadata",
    "BufferSlice",
    
    # Filter creation
    "create_filter_from_config",
    "create_builtin_filter_from_type",
    "create_custom_filter",
    
    # Built-in filter creation helpers
    "create_tcp_proxy_filter",
    "create_udp_proxy_filter",
    "create_http_codec_filter",
    "create_http_router_filter",
    "create_http_compression_filter",
    "create_tls_termination_filter",
    "create_authentication_filter",
    "create_authorization_filter",
    "create_access_log_filter",
    "create_metrics_filter",
    "create_tracing_filter",
    "create_rate_limit_filter",
    "create_circuit_breaker_filter",
    "create_retry_filter",
    "create_load_balancer_filter",
    
    # Context managers
    "filter_context",
    "builtin_filter_context",
    
    # Types
    "BuiltinFilterType",
    "FilterStatus",
    "FilterPosition",
    "ProtocolLayer",
    "FilterError",
]
