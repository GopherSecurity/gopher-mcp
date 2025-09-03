"""
Advanced Buffer Operations.

This module provides Python wrappers for the MCP Filter Buffer C API, including
zero-copy operations, scatter-gather I/O, memory pooling, and type-safe operations.
"""

import json
import struct
from typing import Any, Dict, List, Optional, Union, Callable, Tuple
from dataclasses import dataclass, field
from contextlib import contextmanager

from mcp_types import (
    McpBufferHandle,
    McpResult,
    McpBool,
    BufferOwnership,
    BufferFlags,
    BufferFragment,
    BufferReservation,
    BufferStats,
    DrainTracker,
    BufferPoolConfig,
    MCP_OK,
    MCP_ERROR_INVALID_ARGUMENT,
    MCP_ERROR_NOT_FOUND,
    MCP_ERROR_INVALID_STATE,
    MCP_ERROR_RESOURCE_EXHAUSTED,
    MCP_BUFFER_OWNERSHIP_NONE,
    MCP_BUFFER_OWNERSHIP_SHARED,
    MCP_BUFFER_OWNERSHIP_EXCLUSIVE,
    MCP_BUFFER_OWNERSHIP_EXTERNAL,
    MCP_BUFFER_FLAG_READONLY,
    MCP_BUFFER_FLAG_OWNED,
    MCP_BUFFER_FLAG_EXTERNAL,
    MCP_BUFFER_FLAG_ZERO_COPY,
)

from ffi_bindings import (
    mcp_filter_lib,
    mcp_filter_buffer_create,
    mcp_filter_buffer_release,
    mcp_filter_buffer_length,
    mcp_buffer_peek,
    mcp_filter_get_buffer_slices,
    mcp_filter_reserve_buffer,
    mcp_filter_commit_buffer,
    mcp_buffer_pool_create,
    mcp_buffer_pool_acquire,
    mcp_buffer_pool_release,
    mcp_buffer_pool_destroy,
    check_result,
)


# ============================================================================
# Advanced Buffer Class
# ============================================================================

class AdvancedBuffer:
    """
    Python wrapper for MCP Advanced Buffer.
    
    This class provides a high-level interface to the MCP Buffer C API,
    handling resource management and providing Pythonic methods.
    """
    
    def __init__(self, handle: McpBufferHandle):
        """
        Initialize buffer with handle.
        
        Args:
            handle: Buffer handle from C API
        """
        self._handle = handle
        self._is_destroyed = False
    
    @property
    def handle(self) -> McpBufferHandle:
        """Get buffer handle."""
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        return self._handle
    
    def length(self) -> int:
        """
        Get buffer length.
        
        Returns:
            Buffer length in bytes
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        return mcp_filter_buffer_length(self._handle)
    
    def capacity(self) -> int:
        """
        Get buffer capacity.
        
        Returns:
            Buffer capacity in bytes
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement buffer capacity retrieval
        return 0
    
    def is_empty(self) -> bool:
        """
        Check if buffer is empty.
        
        Returns:
            True if buffer is empty
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        return self.length() == 0
    
    def add_data(self, data: bytes) -> None:
        """
        Add data to buffer.
        
        Args:
            data: Data to add
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement buffer data addition
        pass
    
    def add_string(self, string: str) -> None:
        """
        Add string to buffer.
        
        Args:
            string: String to add
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        self.add_data(string.encode('utf-8'))
    
    def add_buffer(self, source: 'AdvancedBuffer') -> None:
        """
        Add another buffer to buffer.
        
        Args:
            source: Source buffer
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement buffer addition
        pass
    
    def add_fragment(self, fragment: BufferFragment) -> None:
        """
        Add buffer fragment (zero-copy).
        
        Args:
            fragment: Fragment to add
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement fragment addition
        pass
    
    def prepend_data(self, data: bytes) -> None:
        """
        Prepend data to buffer.
        
        Args:
            data: Data to prepend
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement data prepending
        pass
    
    def drain(self, size: int) -> None:
        """
        Drain bytes from front of buffer.
        
        Args:
            size: Number of bytes to drain
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement buffer draining
        pass
    
    def move_data(self, destination: 'AdvancedBuffer', length: int = 0) -> None:
        """
        Move data from one buffer to another.
        
        Args:
            destination: Destination buffer
            length: Bytes to move (0 for all)
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement data movement
        pass
    
    def set_drain_tracker(self, tracker: DrainTracker) -> None:
        """
        Set drain tracker for buffer.
        
        Args:
            tracker: Drain tracker
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement drain tracker setting
        pass
    
    def reserve_space(self, min_size: int) -> BufferReservation:
        """
        Reserve space for writing.
        
        Args:
            min_size: Minimum size to reserve
            
        Returns:
            Buffer reservation
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement space reservation
        reservation = BufferReservation()
        return reservation
    
    def reserve_iovec(self, iovecs: List[Tuple[bytes, int]], iovec_count: int) -> int:
        """
        Reserve for vectored I/O.
        
        Args:
            iovecs: Array of iovec structures
            iovec_count: Number of iovecs
            
        Returns:
            Bytes reserved
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement vectored I/O reservation
        return 0
    
    def commit_reservation(self, reservation: BufferReservation, bytes_written: int) -> None:
        """
        Commit reserved space.
        
        Args:
            reservation: Reservation to commit
            bytes_written: Actual bytes written
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement reservation commit
        pass
    
    def cancel_reservation(self, reservation: BufferReservation) -> None:
        """
        Cancel reservation.
        
        Args:
            reservation: Reservation to cancel
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement reservation cancellation
        pass
    
    def get_contiguous(self, offset: int, length: int) -> Tuple[bytes, int]:
        """
        Get contiguous memory view.
        
        Args:
            offset: Offset in buffer
            length: Requested length
            
        Returns:
            Tuple of (data, actual_length)
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement contiguous memory access
        return b"", 0
    
    def linearize(self, size: int) -> bytes:
        """
        Linearize buffer (ensure contiguous memory).
        
        Args:
            size: Size to linearize
            
        Returns:
            Linearized data
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement buffer linearization
        return b""
    
    def peek(self, offset: int, length: int) -> bytes:
        """
        Peek at buffer data without consuming.
        
        Args:
            offset: Offset to peek at
            length: Length to peek
            
        Returns:
            Peeked data
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement buffer peeking
        return b""
    
    def write_le_int(self, value: int, size: int) -> None:
        """
        Write integer with little-endian byte order.
        
        Args:
            value: Value to write
            size: Size in bytes (1, 2, 4, 8)
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement little-endian integer writing
        pass
    
    def write_be_int(self, value: int, size: int) -> None:
        """
        Write integer with big-endian byte order.
        
        Args:
            value: Value to write
            size: Size in bytes (1, 2, 4, 8)
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement big-endian integer writing
        pass
    
    def read_le_int(self, size: int) -> int:
        """
        Read integer with little-endian byte order.
        
        Args:
            size: Size in bytes (1, 2, 4, 8)
            
        Returns:
            Read value
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement little-endian integer reading
        return 0
    
    def read_be_int(self, size: int) -> int:
        """
        Read integer with big-endian byte order.
        
        Args:
            size: Size in bytes (1, 2, 4, 8)
            
        Returns:
            Read value
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement big-endian integer reading
        return 0
    
    def search(self, pattern: bytes, start_position: int = 0) -> Optional[int]:
        """
        Search for pattern in buffer.
        
        Args:
            pattern: Pattern to search for
            start_position: Start position for search
            
        Returns:
            Position where found, or None if not found
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement pattern search
        return None
    
    def find_byte(self, delimiter: int) -> Optional[int]:
        """
        Find delimiter in buffer.
        
        Args:
            delimiter: Delimiter character
            
        Returns:
            Position where found, or None if not found
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement byte finding
        return None
    
    def get_stats(self) -> BufferStats:
        """
        Get buffer statistics.
        
        Returns:
            Buffer statistics structure
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement statistics retrieval
        stats = BufferStats()
        return stats
    
    def set_watermarks(self, low_watermark: int, high_watermark: int, overflow_watermark: int) -> None:
        """
        Set buffer watermarks for flow control.
        
        Args:
            low_watermark: Low watermark bytes
            high_watermark: High watermark bytes
            overflow_watermark: Overflow watermark bytes
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement watermark setting
        pass
    
    def is_above_high_watermark(self) -> bool:
        """
        Check if buffer is above high watermark.
        
        Returns:
            True if above high watermark
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement high watermark check
        return False
    
    def is_below_low_watermark(self) -> bool:
        """
        Check if buffer is below low watermark.
        
        Returns:
            True if below low watermark
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer has been destroyed")
        
        # TODO: Implement low watermark check
        return False
    
    def release(self) -> None:
        """Release buffer handle."""
        if self._is_destroyed:
            return
        
        mcp_filter_buffer_release(self._handle)
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
    
    def __len__(self) -> int:
        """Get buffer length."""
        return self.length()
    
    def __bool__(self) -> bool:
        """Check if buffer is not empty."""
        return not self.is_empty()


# ============================================================================
# Buffer Pool Class
# ============================================================================

class BufferPool:
    """
    Buffer pool for efficient memory management.
    
    This class provides a pool of pre-allocated buffers for high-performance
    applications that need to minimize allocation overhead.
    """
    
    def __init__(self, config: BufferPoolConfig):
        """
        Initialize buffer pool.
        
        Args:
            config: Buffer pool configuration
        """
        self._config = config
        self._pool_handle = None
        self._is_destroyed = False
        
        # TODO: Create buffer pool from configuration
        pass
    
    @property
    def config(self) -> BufferPoolConfig:
        """Get buffer pool configuration."""
        return self._config
    
    def acquire(self) -> Optional[AdvancedBuffer]:
        """
        Acquire buffer from pool.
        
        Returns:
            Buffer instance or None if pool exhausted
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer pool has been destroyed")
        
        # TODO: Implement buffer acquisition
        return None
    
    def release(self, buffer: AdvancedBuffer) -> None:
        """
        Release buffer back to pool.
        
        Args:
            buffer: Buffer to release
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer pool has been destroyed")
        
        # TODO: Implement buffer release
        pass
    
    def get_stats(self) -> Tuple[int, int, int]:
        """
        Get pool statistics.
        
        Returns:
            Tuple of (free_count, used_count, total_allocated)
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer pool has been destroyed")
        
        # TODO: Implement statistics retrieval
        return 0, 0, 0
    
    def trim(self, target_free: int) -> None:
        """
        Trim pool to reduce memory usage.
        
        Args:
            target_free: Target number of free buffers
        """
        if self._is_destroyed:
            raise RuntimeError("Buffer pool has been destroyed")
        
        # TODO: Implement pool trimming
        pass
    
    def destroy(self) -> None:
        """Destroy buffer pool."""
        if self._is_destroyed:
            return
        
        # TODO: Implement pool destruction
        self._is_destroyed = True
    
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
# Buffer Factory Functions
# ============================================================================

def create_buffer_owned(initial_capacity: int, ownership: BufferOwnership = BufferOwnership.EXCLUSIVE) -> AdvancedBuffer:
    """
    Create a new buffer with ownership.
    
    Args:
        initial_capacity: Initial buffer capacity
        ownership: Ownership model
        
    Returns:
        New buffer instance
    """
    # TODO: Implement owned buffer creation
    handle = mcp_filter_buffer_create(None, initial_capacity, ownership)
    if handle == 0:
        raise RuntimeError("Failed to create owned buffer")
    return AdvancedBuffer(handle)


def create_buffer_view(data: bytes) -> AdvancedBuffer:
    """
    Create a buffer view (zero-copy reference).
    
    Args:
        data: Data to reference
        
    Returns:
        New buffer instance
    """
    # TODO: Implement buffer view creation
    handle = mcp_filter_buffer_create(data, len(data), BufferFlags.READONLY)
    if handle == 0:
        raise RuntimeError("Failed to create buffer view")
    return AdvancedBuffer(handle)


def create_buffer_from_fragment(fragment: BufferFragment) -> AdvancedBuffer:
    """
    Create buffer from external fragment.
    
    Args:
        fragment: External memory fragment
        
    Returns:
        New buffer instance
    """
    # TODO: Implement fragment-based buffer creation
    handle = create_buffer(None, 0, BufferFlags.EXTERNAL)
    if handle == 0:
        raise RuntimeError("Failed to create buffer from fragment")
    return AdvancedBuffer(handle)


def clone_buffer(buffer: AdvancedBuffer) -> AdvancedBuffer:
    """
    Clone a buffer (deep copy).
    
    Args:
        buffer: Source buffer
        
    Returns:
        Cloned buffer instance
    """
    # TODO: Implement buffer cloning
    handle = create_buffer(None, buffer.length(), BufferFlags.OWNED)
    if handle == 0:
        raise RuntimeError("Failed to clone buffer")
    return AdvancedBuffer(handle)


def create_buffer_cow(buffer: AdvancedBuffer) -> AdvancedBuffer:
    """
    Create copy-on-write buffer.
    
    Args:
        buffer: Source buffer
        
    Returns:
        COW buffer instance
    """
    # TODO: Implement copy-on-write buffer creation
    handle = create_buffer(None, buffer.length(), BufferFlags.ZERO_COPY)
    if handle == 0:
        raise RuntimeError("Failed to create COW buffer")
    return AdvancedBuffer(handle)


def create_buffer_pool(buffer_size: int, max_buffers: int) -> BufferPool:
    """
    Create buffer pool.
    
    Args:
        buffer_size: Size of each buffer
        max_buffers: Maximum buffers in pool
        
    Returns:
        New buffer pool instance
    """
    config = BufferPoolConfig(
        buffer_size=buffer_size,
        max_buffers=max_buffers
    )
    return BufferPool(config)


def create_buffer_pool_ex(config: BufferPoolConfig) -> BufferPool:
    """
    Create buffer pool with configuration.
    
    Args:
        config: Pool configuration
        
    Returns:
        New buffer pool instance
    """
    return BufferPool(config)


# ============================================================================
# Utility Functions
# ============================================================================

def create_buffer_from_string(string: str) -> AdvancedBuffer:
    """
    Create buffer from string.
    
    Args:
        string: String to convert
        
    Returns:
        New buffer instance
    """
    data = string.encode('utf-8')
    return create_buffer_view(data)


def read_string_from_buffer(buffer: AdvancedBuffer) -> str:
    """
    Read string from buffer.
    
    Args:
        buffer: Buffer to read from
        
    Returns:
        String content
    """
    data, _ = buffer.get_contiguous(0, buffer.length())
    return data.decode('utf-8')


def create_buffer_from_json(obj: Any) -> AdvancedBuffer:
    """
    Create buffer from JSON object.
    
    Args:
        obj: Object to serialize
        
    Returns:
        New buffer instance
    """
    json_str = json.dumps(obj)
    return create_buffer_from_string(json_str)


def read_json_from_buffer(buffer: AdvancedBuffer) -> Any:
    """
    Read JSON object from buffer.
    
    Args:
        buffer: Buffer to read from
        
    Returns:
        Deserialized object
    """
    json_str = read_string_from_buffer(buffer)
    return json.loads(json_str)


def create_buffer_slice(data: bytes, offset: int = 0, length: Optional[int] = None) -> bytes:
    """
    Create buffer slice.
    
    Args:
        data: Source data
        offset: Offset in data
        length: Length of slice (None for rest)
        
    Returns:
        Buffer slice
    """
    if length is None:
        length = len(data) - offset
    return data[offset:offset + length]


def concatenate_buffers(buffers: List[AdvancedBuffer]) -> AdvancedBuffer:
    """
    Concatenate multiple buffers.
    
    Args:
        buffers: List of buffers to concatenate
        
    Returns:
        Concatenated buffer
    """
    if not buffers:
        return create_buffer_owned(0)
    
    total_length = sum(buffer.length() for buffer in buffers)
    result = create_buffer_owned(total_length)
    
    for buffer in buffers:
        result.add_buffer(buffer)
    
    return result


def split_buffer(buffer: AdvancedBuffer, delimiter: int) -> List[AdvancedBuffer]:
    """
    Split buffer by delimiter.
    
    Args:
        buffer: Buffer to split
        delimiter: Delimiter byte
        
    Returns:
        List of split buffers
    """
    # TODO: Implement buffer splitting
    return [buffer]


def compare_buffers(buffer1: AdvancedBuffer, buffer2: AdvancedBuffer) -> int:
    """
    Compare two buffers.
    
    Args:
        buffer1: First buffer
        buffer2: Second buffer
        
    Returns:
        -1 if buffer1 < buffer2, 0 if equal, 1 if buffer1 > buffer2
    """
    # TODO: Implement buffer comparison
    return 0


# ============================================================================
# Context Managers
# ============================================================================

@contextmanager
def buffer_context(data: Optional[bytes] = None, capacity: int = 0, flags: int = 0):
    """
    Context manager for buffer lifecycle.
    
    Args:
        data: Optional initial data
        capacity: Initial capacity
        flags: Buffer flags
        
    Yields:
        AdvancedBuffer instance
    """
    handle = create_buffer(data, capacity, flags)
    if handle == 0:
        raise RuntimeError("Failed to create buffer")
    
    buffer = AdvancedBuffer(handle)
    try:
        yield buffer
    finally:
        buffer.release()


@contextmanager
def buffer_pool_context(config: BufferPoolConfig):
    """
    Context manager for buffer pool lifecycle.
    
    Args:
        config: Pool configuration
        
    Yields:
        BufferPool instance
    """
    pool = BufferPool(config)
    try:
        yield pool
    finally:
        pool.destroy()


# ============================================================================
# Module Exports
# ============================================================================

__all__ = [
    # Core classes
    "AdvancedBuffer",
    "BufferPool",
    "BufferFragment",
    "BufferReservation",
    "BufferStats",
    "DrainTracker",
    "BufferPoolConfig",
    
    # Buffer creation
    "create_buffer_owned",
    "create_buffer_view",
    "create_buffer_from_fragment",
    "clone_buffer",
    "create_buffer_cow",
    "create_buffer_pool",
    "create_buffer_pool_ex",
    
    # Utility functions
    "create_buffer_from_string",
    "read_string_from_buffer",
    "create_buffer_from_json",
    "read_json_from_buffer",
    "create_buffer_slice",
    "concatenate_buffers",
    "split_buffer",
    "compare_buffers",
    
    # Context managers
    "buffer_context",
    "buffer_pool_context",
    
    # Types
    "BufferOwnership",
    "BufferFlags",
]
