"""
Core MCP types and enums.

This module defines the fundamental types and enums used throughout the MCP Filter SDK,
mirroring the C++ API definitions.
"""

from enum import IntEnum, IntFlag
from typing import Any, Dict, List, Optional, Union, Callable, Protocol
from dataclasses import dataclass
from ctypes import c_uint64, c_int32, c_uint32, c_uint8, c_bool, c_char_p, c_void_p


# ============================================================================
# Core Handle Types
# ============================================================================

# Opaque handles for FFI safety
McpFilter = c_uint64
McpFilterChain = c_uint64
McpFilterManager = c_uint64
McpBufferHandle = c_uint64
McpDispatcher = c_uint64
McpConnection = c_uint64
McpMemoryPool = c_uint64
McpRequestId = c_uint64

# Basic types
McpBool = c_bool
McpResult = c_int32
McpJsonValue = c_void_p  # JSON value handle
McpMap = c_void_p  # Map handle


# ============================================================================
# Filter Status and Position Enums
# ============================================================================

class FilterStatus(IntEnum):
    """Filter status for processing control."""
    CONTINUE = 0  # Continue filter chain processing
    STOP_ITERATION = 1  # Stop filter chain processing


class FilterPosition(IntEnum):
    """Filter position in chain."""
    FIRST = 0
    LAST = 1
    BEFORE = 2  # Requires reference filter
    AFTER = 3  # Requires reference filter


# ============================================================================
# Protocol Layer Enums
# ============================================================================

class ProtocolLayer(IntEnum):
    """Protocol layers (OSI model)."""
    L3_NETWORK = 3  # IP level
    L4_TRANSPORT = 4  # TCP/UDP
    L5_SESSION = 5  # Session management
    L6_PRESENTATION = 6  # Encoding/Encryption
    L7_APPLICATION = 7  # HTTP/gRPC/WebSocket


class TransportProtocol(IntEnum):
    """Transport protocols for L4."""
    TCP = 0
    UDP = 1
    QUIC = 2
    SCTP = 3


class AppProtocol(IntEnum):
    """Application protocols for L7."""
    HTTP = 0
    HTTPS = 1
    HTTP2 = 2
    HTTP3 = 3
    GRPC = 4
    WEBSOCKET = 5
    JSONRPC = 6
    CUSTOM = 99


# ============================================================================
# Built-in Filter Types
# ============================================================================

class BuiltinFilterType(IntEnum):
    """Built-in filter types."""
    # Network filters
    TCP_PROXY = 0
    UDP_PROXY = 1

    # HTTP filters
    HTTP_CODEC = 10
    HTTP_ROUTER = 11
    HTTP_COMPRESSION = 12

    # Security filters
    TLS_TERMINATION = 20
    AUTHENTICATION = 21
    AUTHORIZATION = 22

    # Observability
    ACCESS_LOG = 30
    METRICS = 31
    TRACING = 32

    # Traffic management
    RATE_LIMIT = 40
    CIRCUIT_BREAKER = 41
    RETRY = 42
    LOAD_BALANCER = 43

    # Custom filter
    CUSTOM = 100


# ============================================================================
# Error Codes
# ============================================================================

class FilterError(IntEnum):
    """Filter error codes."""
    NONE = 0
    INVALID_CONFIG = -1000
    INITIALIZATION_FAILED = -1001
    BUFFER_OVERFLOW = -1002
    PROTOCOL_VIOLATION = -1003
    UPSTREAM_TIMEOUT = -1004
    CIRCUIT_OPEN = -1005
    RESOURCE_EXHAUSTED = -1006
    INVALID_STATE = -1007


# ============================================================================
# Buffer Types and Flags
# ============================================================================

class BufferOwnership(IntEnum):
    """Buffer ownership model."""
    NONE = 0  # No ownership (view only)
    SHARED = 1  # Shared ownership (ref counted)
    EXCLUSIVE = 2  # Exclusive ownership
    EXTERNAL = 3  # External ownership (callback)


class BufferFlags(IntFlag):
    """Buffer flags."""
    READONLY = 0x01
    OWNED = 0x02
    EXTERNAL = 0x04
    ZERO_COPY = 0x08


# ============================================================================
# Chain Execution and Routing
# ============================================================================

class ChainExecutionMode(IntEnum):
    """Chain execution mode."""
    SEQUENTIAL = 0  # Execute filters in order
    PARALLEL = 1  # Execute filters in parallel
    CONDITIONAL = 2  # Execute based on conditions
    PIPELINE = 3  # Pipeline mode with buffering


class RoutingStrategy(IntEnum):
    """Chain routing strategy."""
    ROUND_ROBIN = 0  # Round-robin distribution
    LEAST_LOADED = 1  # Route to least loaded filter
    HASH_BASED = 2  # Hash-based routing
    PRIORITY = 3  # Priority-based routing
    CUSTOM = 99  # Custom routing function


class MatchCondition(IntEnum):
    """Filter match condition."""
    ALL = 0  # Match all conditions
    ANY = 1  # Match any condition
    NONE = 2  # Match no conditions
    CUSTOM = 99  # Custom match function


class ChainState(IntEnum):
    """Chain state."""
    IDLE = 0
    PROCESSING = 1
    PAUSED = 2
    ERROR = 3
    COMPLETED = 4


# ============================================================================
# Connection States
# ============================================================================

class ConnectionState(IntEnum):
    """Connection state."""
    CONNECTING = 0
    CONNECTED = 1
    DISCONNECTING = 2
    DISCONNECTED = 3
    ERROR = 4


# ============================================================================
# Callback Types
# ============================================================================

# Filter data callback
FilterDataCallback = Callable[[McpBufferHandle, McpBool, c_void_p], FilterStatus]

# Filter write callback
FilterWriteCallback = Callable[[McpBufferHandle, McpBool, c_void_p], FilterStatus]

# Connection event callback
FilterEventCallback = Callable[[ConnectionState, c_void_p], FilterStatus]

# Watermark callbacks
FilterWatermarkCallback = Callable[[McpFilter, c_void_p], None]

# Error callback
FilterErrorCallback = Callable[[McpFilter, FilterError, c_char_p, c_void_p], None]

# Completion callback for async operations
FilterCompletionCallback = Callable[[McpResult, c_void_p], None]

# Post completion callback
PostCompletionCallback = Callable[[McpResult, c_void_p], None]

# Request callback for server
FilterRequestCallback = Callable[[McpBufferHandle, McpResult, c_void_p], None]

# Custom routing function
RoutingFunction = Callable[[McpBufferHandle, c_void_p, int, c_void_p], McpFilter]

# Chain event callback
ChainEventCallback = Callable[[McpFilterChain, ChainState, ChainState, c_void_p], None]

# Filter match function
FilterMatchCallback = Callable[[McpBufferHandle, c_void_p, c_void_p], McpBool]

# Drain tracker callback
DrainTrackerCallback = Callable[[int, c_void_p], None]


# ============================================================================
# Constants
# ============================================================================

# MCP API constants
MCP_OK = 0
MCP_ERROR_INVALID_ARGUMENT = -1
MCP_ERROR_NOT_FOUND = -2
MCP_ERROR_INVALID_STATE = -3
MCP_ERROR_RESOURCE_EXHAUSTED = -4

# Boolean constants
MCP_TRUE = True
MCP_FALSE = False

# Connection state constants
MCP_CONNECTION_STATE_CONNECTING = ConnectionState.CONNECTING
MCP_CONNECTION_STATE_CONNECTED = ConnectionState.CONNECTED
MCP_CONNECTION_STATE_DISCONNECTING = ConnectionState.DISCONNECTING
MCP_CONNECTION_STATE_DISCONNECTED = ConnectionState.DISCONNECTED
MCP_CONNECTION_STATE_ERROR = ConnectionState.ERROR

# Filter status constants
MCP_FILTER_CONTINUE = FilterStatus.CONTINUE
MCP_FILTER_STOP_ITERATION = FilterStatus.STOP_ITERATION

# Filter position constants
MCP_FILTER_POSITION_FIRST = FilterPosition.FIRST
MCP_FILTER_POSITION_LAST = FilterPosition.LAST
MCP_FILTER_POSITION_BEFORE = FilterPosition.BEFORE
MCP_FILTER_POSITION_AFTER = FilterPosition.AFTER

# Protocol layer constants
MCP_PROTOCOL_LAYER_3_NETWORK = ProtocolLayer.L3_NETWORK
MCP_PROTOCOL_LAYER_4_TRANSPORT = ProtocolLayer.L4_TRANSPORT
MCP_PROTOCOL_LAYER_5_SESSION = ProtocolLayer.L5_SESSION
MCP_PROTOCOL_LAYER_6_PRESENTATION = ProtocolLayer.L6_PRESENTATION
MCP_PROTOCOL_LAYER_7_APPLICATION = ProtocolLayer.L7_APPLICATION

# Transport protocol constants
MCP_TRANSPORT_PROTOCOL_TCP = TransportProtocol.TCP
MCP_TRANSPORT_PROTOCOL_UDP = TransportProtocol.UDP
MCP_TRANSPORT_PROTOCOL_QUIC = TransportProtocol.QUIC
MCP_TRANSPORT_PROTOCOL_SCTP = TransportProtocol.SCTP

# Application protocol constants
MCP_APP_PROTOCOL_HTTP = AppProtocol.HTTP
MCP_APP_PROTOCOL_HTTPS = AppProtocol.HTTPS
MCP_APP_PROTOCOL_HTTP2 = AppProtocol.HTTP2
MCP_APP_PROTOCOL_HTTP3 = AppProtocol.HTTP3
MCP_APP_PROTOCOL_GRPC = AppProtocol.GRPC
MCP_APP_PROTOCOL_WEBSOCKET = AppProtocol.WEBSOCKET
MCP_APP_PROTOCOL_JSONRPC = AppProtocol.JSONRPC
MCP_APP_PROTOCOL_CUSTOM = AppProtocol.CUSTOM

# Built-in filter type constants
MCP_FILTER_TCP_PROXY = BuiltinFilterType.TCP_PROXY
MCP_FILTER_UDP_PROXY = BuiltinFilterType.UDP_PROXY
MCP_FILTER_HTTP_CODEC = BuiltinFilterType.HTTP_CODEC
MCP_FILTER_HTTP_ROUTER = BuiltinFilterType.HTTP_ROUTER
MCP_FILTER_HTTP_COMPRESSION = BuiltinFilterType.HTTP_COMPRESSION
MCP_FILTER_TLS_TERMINATION = BuiltinFilterType.TLS_TERMINATION
MCP_FILTER_AUTHENTICATION = BuiltinFilterType.AUTHENTICATION
MCP_FILTER_AUTHORIZATION = BuiltinFilterType.AUTHORIZATION
MCP_FILTER_ACCESS_LOG = BuiltinFilterType.ACCESS_LOG
MCP_FILTER_METRICS = BuiltinFilterType.METRICS
MCP_FILTER_TRACING = BuiltinFilterType.TRACING
MCP_FILTER_RATE_LIMIT = BuiltinFilterType.RATE_LIMIT
MCP_FILTER_CIRCUIT_BREAKER = BuiltinFilterType.CIRCUIT_BREAKER
MCP_FILTER_RETRY = BuiltinFilterType.RETRY
MCP_FILTER_LOAD_BALANCER = BuiltinFilterType.LOAD_BALANCER
MCP_FILTER_CUSTOM = BuiltinFilterType.CUSTOM

# Filter error constants
MCP_FILTER_ERROR_NONE = FilterError.NONE
MCP_FILTER_ERROR_INVALID_CONFIG = FilterError.INVALID_CONFIG
MCP_FILTER_ERROR_INITIALIZATION_FAILED = FilterError.INITIALIZATION_FAILED
MCP_FILTER_ERROR_BUFFER_OVERFLOW = FilterError.BUFFER_OVERFLOW
MCP_FILTER_ERROR_PROTOCOL_VIOLATION = FilterError.PROTOCOL_VIOLATION
MCP_FILTER_ERROR_UPSTREAM_TIMEOUT = FilterError.UPSTREAM_TIMEOUT
MCP_FILTER_ERROR_CIRCUIT_OPEN = FilterError.CIRCUIT_OPEN
MCP_FILTER_ERROR_RESOURCE_EXHAUSTED = FilterError.RESOURCE_EXHAUSTED
MCP_FILTER_ERROR_INVALID_STATE = FilterError.INVALID_STATE

# Buffer ownership constants
MCP_BUFFER_OWNERSHIP_NONE = BufferOwnership.NONE
MCP_BUFFER_OWNERSHIP_SHARED = BufferOwnership.SHARED
MCP_BUFFER_OWNERSHIP_EXCLUSIVE = BufferOwnership.EXCLUSIVE
MCP_BUFFER_OWNERSHIP_EXTERNAL = BufferOwnership.EXTERNAL

# Buffer flag constants
MCP_BUFFER_FLAG_READONLY = BufferFlags.READONLY
MCP_BUFFER_FLAG_OWNED = BufferFlags.OWNED
MCP_BUFFER_FLAG_EXTERNAL = BufferFlags.EXTERNAL
MCP_BUFFER_FLAG_ZERO_COPY = BufferFlags.ZERO_COPY

# Chain execution mode constants
MCP_CHAIN_MODE_SEQUENTIAL = ChainExecutionMode.SEQUENTIAL
MCP_CHAIN_MODE_PARALLEL = ChainExecutionMode.PARALLEL
MCP_CHAIN_MODE_CONDITIONAL = ChainExecutionMode.CONDITIONAL
MCP_CHAIN_MODE_PIPELINE = ChainExecutionMode.PIPELINE

# Routing strategy constants
MCP_ROUTING_ROUND_ROBIN = RoutingStrategy.ROUND_ROBIN
MCP_ROUTING_LEAST_LOADED = RoutingStrategy.LEAST_LOADED
MCP_ROUTING_HASH_BASED = RoutingStrategy.HASH_BASED
MCP_ROUTING_PRIORITY = RoutingStrategy.PRIORITY
MCP_ROUTING_CUSTOM = RoutingStrategy.CUSTOM

# Match condition constants
MCP_MATCH_ALL = MatchCondition.ALL
MCP_MATCH_ANY = MatchCondition.ANY
MCP_MATCH_NONE = MatchCondition.NONE
MCP_MATCH_CUSTOM = MatchCondition.CUSTOM

# Chain state constants
MCP_CHAIN_STATE_IDLE = ChainState.IDLE
MCP_CHAIN_STATE_PROCESSING = ChainState.PROCESSING
MCP_CHAIN_STATE_PAUSED = ChainState.PAUSED
MCP_CHAIN_STATE_ERROR = ChainState.ERROR
MCP_CHAIN_STATE_COMPLETED = ChainState.COMPLETED
