"""
Filter-specific type definitions.

This module defines data structures and types specific to filter operations,
including configuration, metadata, and callback structures.
"""

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Union, Callable
from ctypes import c_void_p, c_char_p, c_uint32, c_uint8, c_uint16, c_uint64

from ..mcp_types import (
    McpFilter,
    McpBufferHandle,
    McpMemoryPool,
    McpJsonValue,
    McpMap,
    ProtocolLayer,
    TransportProtocol,
    AppProtocol,
    FilterStatus,
    FilterDataCallback,
    FilterWriteCallback,
    FilterEventCallback,
    FilterWatermarkCallback,
    FilterErrorCallback,
    FilterCompletionCallback,
    PostCompletionCallback,
    FilterRequestCallback,
)


# ============================================================================
# Filter Configuration
# ============================================================================

@dataclass
class FilterConfig:
    """Filter configuration structure."""
    name: Optional[str] = None
    type: int = 0  # BuiltinFilterType
    settings: Optional[McpJsonValue] = None
    layer: int = 0  # ProtocolLayer
    memory_pool: Optional[McpMemoryPool] = None

    def to_c_struct(self):
        """Convert to C-compatible structure for FFI."""
        return {
            'name': self.name.encode('utf-8') if self.name else None,
            'type': self.type,
            'settings': self.settings,
            'layer': self.layer,
            'memory_pool': self.memory_pool
        }


# ============================================================================
# Buffer Operations
# ============================================================================

@dataclass
class BufferSlice:
    """Buffer slice for zero-copy access."""
    data: Optional[c_void_p] = None  # Direct pointer to buffer memory
    length: int = 0  # Length of this slice
    flags: int = 0  # Buffer flags


# ============================================================================
# Protocol Metadata
# ============================================================================

@dataclass
class L3Metadata:
    """L3 - Network layer metadata."""
    src_ip: int = 0
    dst_ip: int = 0
    protocol: int = 0
    ttl: int = 0


@dataclass
class L4Metadata:
    """L4 - Transport layer metadata."""
    src_port: int = 0
    dst_port: int = 0
    protocol: int = 0  # TransportProtocol
    sequence_num: int = 0


@dataclass
class L5Metadata:
    """L5 - Session layer metadata."""
    is_tls: bool = False
    alpn: Optional[str] = None
    sni: Optional[str] = None
    session_id: int = 0


@dataclass
class L7Metadata:
    """L7 - Application layer metadata."""
    protocol: int = 0  # AppProtocol
    headers: Optional[McpMap] = None
    method: Optional[str] = None
    path: Optional[str] = None
    status_code: int = 0


@dataclass
class ProtocolMetadata:
    """Protocol metadata for different layers."""
    layer: int = 0  # ProtocolLayer
    
    # Union of layer-specific metadata
    l3: Optional[L3Metadata] = None
    l4: Optional[L4Metadata] = None
    l5: Optional[L5Metadata] = None
    l7: Optional[L7Metadata] = None


# ============================================================================
# Filter Callbacks
# ============================================================================

@dataclass
class FilterCallbacks:
    """Filter callbacks structure."""
    # Data callbacks (executed in dispatcher thread)
    on_data: Optional[FilterDataCallback] = None
    on_write: Optional[FilterWriteCallback] = None
    on_new_connection: Optional[FilterEventCallback] = None
    
    # Watermark callbacks
    on_high_watermark: Optional[FilterWatermarkCallback] = None
    on_low_watermark: Optional[FilterWatermarkCallback] = None
    
    # Error handling
    on_error: Optional[FilterErrorCallback] = None
    
    # User data
    user_data: Optional[c_void_p] = None


# ============================================================================
# Filter Statistics
# ============================================================================

@dataclass
class FilterStats:
    """Filter statistics structure."""
    bytes_processed: int = 0
    packets_processed: int = 0
    errors: int = 0
    processing_time_us: int = 0
    throughput_mbps: float = 0.0


# ============================================================================
# Client/Server Integration Types
# ============================================================================

@dataclass
class FilterClientContext:
    """Client context for filtered operations."""
    client: c_void_p  # McpClient
    request_filters: c_uint64  # McpFilterChain
    response_filters: c_uint64  # McpFilterChain


@dataclass
class FilterServerContext:
    """Server context for filtered operations."""
    server: c_void_p  # McpServer
    request_filters: c_uint64  # McpFilterChain
    response_filters: c_uint64  # McpFilterChain


# ============================================================================
# Resource Management
# ============================================================================

@dataclass
class FilterResourceGuard:
    """Filter resource guard for RAII."""
    transaction: Optional[c_void_p] = None
    pool: Optional[McpMemoryPool] = None
    tracked_filters: List[c_uint64] = field(default_factory=list)


# ============================================================================
# Buffer Pool Management
# ============================================================================

@dataclass
class BufferPoolConfig:
    """Buffer pool configuration."""
    buffer_size: int = 4096
    max_buffers: int = 100
    prealloc_count: int = 10
    use_thread_local: bool = False
    zero_on_alloc: bool = True


# ============================================================================
# Utility Types
# ============================================================================

# JSON-RPC message type
JsonRpcMessage = Dict[str, Any]

# Filter configuration type
FilterConfigDict = Dict[str, Any]

# Protocol metadata type
ProtocolMetadataDict = Dict[str, Any]

# Callback user data type
UserData = Optional[c_void_p]

# Filter handle type
FilterHandle = c_uint64

# Chain handle type
ChainHandle = c_uint64

# Manager handle type
ManagerHandle = c_uint64

# Buffer handle type
BufferHandle = c_uint64

# Dispatcher handle type
DispatcherHandle = c_uint64

# Connection handle type
ConnectionHandle = c_uint64

# Memory pool handle type
MemoryPoolHandle = c_uint64

# Request ID type
RequestId = c_uint64

# Result type
Result = int

# Boolean type
Bool = bool

# String type
String = str

# Integer types
UInt8 = int
UInt16 = int
UInt32 = int
UInt64 = int
Int32 = int

# Pointer types
VoidPtr = c_void_p
CharPtr = c_char_p

# JSON value type
JsonValue = McpJsonValue

# Map type
Map = McpMap
