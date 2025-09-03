"""
Buffer-specific type definitions.

This module provides type definitions for buffer operations and management.
"""

from .buffer_types import (
    BufferFragment,
    BufferReservation,
    BufferStats,
    DrainTracker,
    BufferPoolConfig,
    BufferOwnership,
    BufferFlags,
)

__all__ = [
    "BufferFragment",
    "BufferReservation",
    "BufferStats",
    "DrainTracker",
    "BufferPoolConfig",
    "BufferOwnership",
    "BufferFlags",
]
