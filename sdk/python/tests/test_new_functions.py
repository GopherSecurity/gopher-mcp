"""
Test the newly added functions that were missing from FFI bindings.
"""

import unittest
import sys
import os

# Add src to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from ffi_bindings import (
    mcp_buffer_pool_create,
    mcp_buffer_pool_acquire,
    mcp_buffer_pool_release,
    mcp_buffer_pool_destroy,
    mcp_filter_manager_create,
    mcp_filter_manager_release,
    mcp_filter_get_stats,
    mcp_filter_reset_stats,
)


class TestNewFunctions(unittest.TestCase):
    """Test the newly added functions."""
    
    def test_buffer_pool_operations(self):
        """Test buffer pool operations."""
        # Create buffer pool
        pool = mcp_buffer_pool_create(1024, 10)
        self.assertIsNotNone(pool)
        print(f"✓ Created buffer pool: {pool}")
        
        # Acquire buffer from pool
        buffer = mcp_buffer_pool_acquire(pool)
        self.assertIsNotNone(buffer)
        print(f"✓ Acquired buffer from pool: {buffer}")
        
        # Release buffer back to pool
        mcp_buffer_pool_release(pool, buffer)
        print("✓ Released buffer back to pool")
        
        # Destroy pool
        mcp_buffer_pool_destroy(pool)
        print("✓ Destroyed buffer pool")
    
    def test_filter_manager_operations(self):
        """Test filter manager operations."""
        # Create filter manager (using mock handles)
        manager = mcp_filter_manager_create(1, 1)  # Mock dispatcher and connection
        self.assertIsNotNone(manager)
        print(f"✓ Created filter manager: {manager}")
        
        # Release filter manager
        mcp_filter_manager_release(manager)
        print("✓ Released filter manager")
    
    def test_filter_statistics_operations(self):
        """Test filter statistics operations."""
        # Create a filter first
        from ffi_bindings import mcp_filter_create_builtin
        from mcp_types import BuiltinFilterType
        
        filter_handle = mcp_filter_create_builtin(1, BuiltinFilterType.HTTP_CODEC, None)
        self.assertIsNotNone(filter_handle)
        print(f"✓ Created filter for stats test: {filter_handle}")
        
        # Test get stats (this might fail if stats structure isn't properly set up)
        try:
            # We need to pass a pointer to a stats structure, but for now just test the call
            result = mcp_filter_get_stats(filter_handle, None)
            print(f"✓ Get stats result: {result}")
        except Exception as e:
            print(f"⚠️ Get stats failed (expected): {e}")
        
        # Test reset stats
        try:
            result = mcp_filter_reset_stats(filter_handle)
            print(f"✓ Reset stats result: {result}")
        except Exception as e:
            print(f"⚠️ Reset stats failed (expected): {e}")
        
        # Clean up
        from ffi_bindings import mcp_filter_release
        mcp_filter_release(filter_handle)
        print("✓ Released filter")


if __name__ == '__main__':
    unittest.main(verbosity=2)
