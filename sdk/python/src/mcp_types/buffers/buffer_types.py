"""
Buffer-specific type definitions.

This module defines data structures and types specific to buffer operations,
including fragments, reservations, statistics, and pool management.
"""

from dataclasses import dataclass, field
from typing import Any, Callable, Optional, List
from ctypes import c_void_p, c_uint64, c_uint32, c_size_t

from ..mcp_types import (
    McpBufferHandle,
    BufferOwnership,
    BufferFlags,
    DrainTrackerCallback,
)


# ============================================================================
# Buffer Fragment
# ============================================================================

@dataclass
class BufferFragment:
    """Buffer fragment for external memory."""
    data: Optional[c_void_p] = None
    size: int = 0
    release_callback: Optional[Callable[[c_void_p, int, c_void_p], None]] = None
    user_data: Optional[c_void_p] = None


# ============================================================================
# Buffer Reservation
# ============================================================================

@dataclass
class BufferReservation:
    """Buffer reservation for writing."""
    data: Optional[c_void_p] = None
    capacity: int = 0
    buffer: Optional[McpBufferHandle] = None
    reservation_id: int = 0


# ============================================================================
# Buffer Statistics
# ============================================================================

@dataclass
class BufferStats:
    """Buffer statistics structure."""
    total_bytes: int = 0
    used_bytes: int = 0
    slice_count: int = 0
    fragment_count: int = 0
    read_operations: int = 0
    write_operations: int = 0


# ============================================================================
# Drain Tracker
# ============================================================================

@dataclass
class DrainTracker:
    """Drain tracker for monitoring buffer consumption."""
    callback: Optional[DrainTrackerCallback] = None
    user_data: Optional[c_void_p] = None


# ============================================================================
# Buffer Pool Configuration
# ============================================================================

@dataclass
class BufferPoolConfig:
    """Buffer pool configuration."""
    buffer_size: int = 4096  # Size of each buffer
    max_buffers: int = 100  # Maximum buffers in pool
    prealloc_count: int = 10  # Number to preallocate
    use_thread_local: bool = False  # Use thread-local caching
    zero_on_alloc: bool = True  # Zero memory on allocation


# ============================================================================
# Buffer Watermarks
# ============================================================================

@dataclass
class BufferWatermarks:
    """Buffer watermark configuration."""
    low_watermark: int = 1024  # Low watermark bytes
    high_watermark: int = 8192  # High watermark bytes
    overflow_watermark: int = 16384  # Overflow watermark bytes


# ============================================================================
# Buffer Operations
# ============================================================================

@dataclass
class BufferOperation:
    """Buffer operation result."""
    success: bool = False
    bytes_processed: int = 0
    error_code: int = 0
    error_message: Optional[str] = None


# ============================================================================
# Buffer I/O Types
# ============================================================================

@dataclass
class BufferIOConfig:
    """Buffer I/O configuration."""
    endianness: str = "little"  # "little" or "big"
    signed: bool = False  # Signed or unsigned integers
    size: int = 4  # Size in bytes (1, 2, 4, 8)


# ============================================================================
# Buffer Search Types
# ============================================================================

@dataclass
class SearchResult:
    """Buffer search result."""
    found: bool = False
    position: int = 0
    length: int = 0


@dataclass
class SearchConfig:
    """Buffer search configuration."""
    case_sensitive: bool = True
    start_position: int = 0
    max_results: int = 1


# ============================================================================
# Buffer Utility Types
# ============================================================================

# Buffer data type
BufferData = bytes

# Buffer slice type
BufferSlice = bytes

# Buffer position type
BufferPosition = int

# Buffer length type
BufferLength = int

# Buffer capacity type
BufferCapacity = int

# Buffer offset type
BufferOffset = int

# Buffer size type
BufferSize = int

# Buffer count type
BufferCount = int

# Buffer index type
BufferIndex = int

# Buffer handle type
BufferHandle = McpBufferHandle

# Buffer ownership type
BufferOwnershipType = BufferOwnership

# Buffer flags type
BufferFlagsType = BufferFlags

# Buffer fragment type
BufferFragmentType = BufferFragment

# Buffer reservation type
BufferReservationType = BufferReservation

# Buffer stats type
BufferStatsType = BufferStats

# Buffer pool config type
BufferPoolConfigType = BufferPoolConfig

# Buffer watermarks type
BufferWatermarksType = BufferWatermarks

# Buffer operation type
BufferOperationType = BufferOperation

# Buffer I/O config type
BufferIOConfigType = BufferIOConfig

# Search result type
SearchResultType = SearchResult

# Search config type
SearchConfigType = SearchConfig

# Drain tracker type
DrainTrackerType = DrainTracker

# Buffer callback types
BufferCallback = Callable[[BufferHandle, c_void_p], None]
BufferDataCallback = Callable[[BufferHandle, BufferData, c_void_p], None]
BufferErrorCallback = Callable[[BufferHandle, int, str, c_void_p], None]
BufferProgressCallback = Callable[[BufferHandle, int, int, c_void_p], None]

# Buffer operation callback types
BufferReadCallback = Callable[[BufferHandle, BufferData, c_void_p], None]
BufferWriteCallback = Callable[[BufferHandle, int, c_void_p], None]
BufferResizeCallback = Callable[[BufferHandle, int, c_void_p], None]
BufferClearCallback = Callable[[BufferHandle, c_void_p], None]

# Buffer pool callback types
BufferPoolAcquireCallback = Callable[[BufferHandle, c_void_p], None]
BufferPoolReleaseCallback = Callable[[BufferHandle, c_void_p], None]
BufferPoolExhaustedCallback = Callable[[c_void_p], None]

# Buffer fragment callback types
BufferFragmentAllocCallback = Callable[[int, c_void_p], BufferFragment]
BufferFragmentReleaseCallback = Callable[[BufferFragment, c_void_p], None]

# Buffer search callback types
BufferSearchCallback = Callable[[BufferHandle, bytes, int, c_void_p], SearchResult]
BufferMatchCallback = Callable[[BufferHandle, bytes, int, c_void_p], bool]

# Buffer validation callback types
BufferValidateCallback = Callable[[BufferHandle, c_void_p], bool]
BufferSanitizeCallback = Callable[[BufferHandle, c_void_p], None]

# Buffer transformation callback types
BufferTransformCallback = Callable[[BufferHandle, BufferData, c_void_p], BufferData]
BufferFilterCallback = Callable[[BufferHandle, BufferData, c_void_p], bool]

# Buffer serialization callback types
BufferSerializeCallback = Callable[[BufferHandle, Any, c_void_p], BufferData]
BufferDeserializeCallback = Callable[[BufferHandle, BufferData, c_void_p], Any]

# Buffer compression callback types
BufferCompressCallback = Callable[[BufferHandle, BufferData, c_void_p], BufferData]
BufferDecompressCallback = Callable[[BufferHandle, BufferData, c_void_p], BufferData]

# Buffer encryption callback types
BufferEncryptCallback = Callable[[BufferHandle, BufferData, c_void_p], BufferData]
BufferDecryptCallback = Callable[[BufferHandle, BufferData, c_void_p], BufferData]

# Buffer hash callback types
BufferHashCallback = Callable[[BufferHandle, BufferData, c_void_p], str]
BufferVerifyCallback = Callable[[BufferHandle, BufferData, str, c_void_p], bool]
