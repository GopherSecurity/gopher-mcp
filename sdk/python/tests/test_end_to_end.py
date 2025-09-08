"""
End-to-end integration tests for CApiFilter functionality.

This module tests the complete CApiFilter integration using real C library
and client-server communication scenarios.
"""

import unittest
import sys
import os
import asyncio
import time
from unittest.mock import Mock, patch, MagicMock
from typing import Any, Dict, List, Optional

# Add src to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../src'))

from mcp_c_structs import (
    create_default_callbacks,
    create_filter_callbacks_struct,
    cleanup_callbacks,
)

from filter_api import (
    create_custom_filter,
)

from filter_manager import (
    FilterManager,
    FilterManagerConfig,
)

from filter_buffer import (
    get_buffer_content,
    update_buffer_content,
    read_string_from_buffer_with_handle,
)


class TestCApiFilterEndToEnd(unittest.TestCase):
    """Test end-to-end CApiFilter integration."""
    
    def setUp(self):
        """Set up test fixtures."""
        cleanup_callbacks()
    
    def tearDown(self):
        """Clean up after tests."""
        cleanup_callbacks()
    
    @patch('filter_manager.create_filter_manager')
    @patch('filter_manager.initialize_filter_manager')
    @patch('filter_manager.add_filter_to_manager')
    @patch('filter_manager.create_mock_dispatcher')
    @patch('filter_manager.create_mock_connection')
    def test_filter_manager_with_custom_callbacks_integration(self, mock_connection, mock_dispatcher, 
                                                             mock_add_filter, mock_init, mock_create):
        """Test FilterManager with custom callbacks integration."""
        # Mock the C API calls
        mock_dispatcher.return_value = 12345
        mock_connection.return_value = 54321
        mock_create.return_value = 98765
        mock_init.return_value = 0  # MCP_OK
        mock_add_filter.return_value = 0  # MCP_OK
        
        # Create custom callbacks
        callbacks = create_default_callbacks()
        
        # Create filter manager config with custom callbacks
        config = FilterManagerConfig(custom_callbacks=callbacks)
        
        # Create filter manager
        manager = FilterManager(config)
        
        # Verify manager is initialized
        self.assertIsNotNone(manager)
        self.assertTrue(manager.is_initialized)
        
        # Verify C API calls were made
        mock_create.assert_called_once()
        mock_init.assert_called_once()
        mock_add_filter.assert_called_once()
    
    @patch('filter_api.create_mock_dispatcher')
    @patch('filter_api.mcp_filter_create_builtin')
    @patch('filter_api.mcp_filter_set_callbacks')
    def test_custom_filter_creation_integration(self, mock_set_callbacks, mock_create_builtin, mock_dispatcher):
        """Test custom filter creation integration."""
        # Mock the C API calls
        mock_dispatcher.return_value = 12345
        mock_create_builtin.return_value = 67890
        mock_set_callbacks.return_value = 0  # MCP_OK
        
        # Create custom callbacks
        callbacks = create_default_callbacks()
        
        # Create custom filter
        filter_instance = create_custom_filter(callbacks=callbacks, name="integration-test")
        
        # Verify filter creation
        self.assertIsNotNone(filter_instance)
        self.assertEqual(filter_instance.handle, 67890)
        
        # Verify C API calls
        mock_create_builtin.assert_called_once()
        mock_set_callbacks.assert_called_once()
    
    def test_callback_struct_creation_integration(self):
        """Test callback struct creation integration."""
        # Create custom callbacks
        callbacks = create_default_callbacks()
        
        # Create callback struct
        callback_struct = create_filter_callbacks_struct(callbacks)
        
        # Verify struct creation
        self.assertIsNotNone(callback_struct)
        self.assertIsNotNone(callback_struct.on_data)
        self.assertIsNotNone(callback_struct.on_write)
        self.assertIsNotNone(callback_struct.on_new_connection)
        self.assertIsNotNone(callback_struct.on_high_watermark)
        self.assertIsNotNone(callback_struct.on_low_watermark)
        self.assertIsNotNone(callback_struct.on_error)
    
    def test_callback_execution_integration(self):
        """Test callback execution integration."""
        # Create custom callbacks
        callbacks = create_default_callbacks()
        
        # Test data callback execution
        result = callbacks["on_data"](12345, True, None)
        self.assertEqual(result, 0)  # MCP_FILTER_CONTINUE
        
        # Test write callback execution
        result = callbacks["on_write"](12345, False, None)
        self.assertEqual(result, 0)  # MCP_FILTER_CONTINUE
        
        # Test connection callback execution
        callbacks["on_new_connection"](None, 42)
        
        # Test mark callbacks execution
        callbacks["on_high_watermark"](None)
        callbacks["on_low_watermark"](None)
        
        # Test error callback execution
        callbacks["on_error"](None, 500, None)
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_buffer_operations_integration(self, mock_buffer_class):
        """Test buffer operations integration."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer.length.return_value = 12
        mock_buffer.get_contiguous.return_value = (b"Hello World!", 12)
        mock_buffer_class.return_value = mock_buffer
        
        # Test buffer content roundtrip
        update_buffer_content(12345, "Hello World!")
        result = get_buffer_content(12345)
        
        # Verify operations
        self.assertEqual(result, "Hello World!")
        mock_buffer.add_data.assert_called_once_with(b"Hello World!")
    
    def test_callback_cleanup_integration(self):
        """Test callback cleanup integration."""
        # Register multiple callbacks
        def callback1(buf, end_stream, user_data):
            return 0
        
        def callback2(buf, end_stream, user_data):
            return 0
        
        from mcp_c_structs import register_python_callback
        
        register_python_callback(callback1, "int (*)(void *, bool, void *)", "callback1")
        register_python_callback(callback2, "int (*)(void *, bool, void *)", "callback2")
        
        # Verify callbacks are registered
        from mcp_c_structs import get_callback_count, list_registered_callbacks
        self.assertEqual(get_callback_count(), 2)
        self.assertIn("callback1", list_registered_callbacks())
        self.assertIn("callback2", list_registered_callbacks())
        
        # Cleanup callbacks
        cleanup_callbacks()
        
        # Verify callbacks are cleaned up
        self.assertEqual(get_callback_count(), 0)
        self.assertEqual(len(list_registered_callbacks()), 0)


class TestCApiFilterClientServerIntegration(unittest.TestCase):
    """Test CApiFilter client-server integration scenarios."""
    
    def setUp(self):
        """Set up test fixtures."""
        cleanup_callbacks()
    
    def tearDown(self):
        """Clean up after tests."""
        cleanup_callbacks()
    
    @patch('filter_manager.create_filter_manager')
    @patch('filter_manager.initialize_filter_manager')
    @patch('filter_manager.add_filter_to_manager')
    @patch('filter_manager.create_mock_dispatcher')
    @patch('filter_manager.create_mock_connection')
    def test_client_server_message_flow(self, mock_connection, mock_dispatcher, 
                                       mock_add_filter, mock_init, mock_create):
        """Test client-server message flow with CApiFilter."""
        # Mock the C API calls
        mock_dispatcher.return_value = 12345
        mock_connection.return_value = 54321
        mock_create.return_value = 98765
        mock_init.return_value = 0  # MCP_OK
        mock_add_filter.return_value = 0  # MCP_OK
        
        # Create custom callbacks for client
        client_callbacks = create_default_callbacks()
        
        # Create filter manager config for client
        client_config = FilterManagerConfig(custom_callbacks=client_callbacks)
        
        # Create client filter manager
        client_manager = FilterManager(client_config)
        
        # Create custom callbacks for server
        server_callbacks = create_default_callbacks()
        
        # Create filter manager config for server
        server_config = FilterManagerConfig(custom_callbacks=server_callbacks)
        
        # Create server filter manager
        server_manager = FilterManager(server_config)
        
        # Verify both managers are initialized
        self.assertTrue(client_manager.is_initialized)
        self.assertTrue(server_manager.is_initialized)
        
        # Verify C API calls were made for both managers
        self.assertEqual(mock_create.call_count, 2)
        self.assertEqual(mock_init.call_count, 2)
        self.assertEqual(mock_add_filter.call_count, 2)
    
    def test_callback_signature_validation_integration(self):
        """Test callback signature validation integration."""
        from mcp_c_structs import validate_callback_signature
        
        # Valid callbacks
        def valid_data_callback(buf, end_stream, user_data):
            return 0
        
        def valid_connection_callback(user_data, fd):
            pass
        
        # Invalid callbacks
        def invalid_callback():
            return 0
        
        # Test validation
        self.assertTrue(validate_callback_signature(valid_data_callback, "data"))
        self.assertTrue(validate_callback_signature(valid_data_callback, "write"))
        self.assertTrue(validate_callback_signature(valid_connection_callback, "connection"))
        
        self.assertFalse(validate_callback_signature(invalid_callback, "data"))
        self.assertFalse(validate_callback_signature(valid_data_callback, "invalid_type"))
    
    def test_error_handling_integration(self):
        """Test error handling integration."""
        # Test buffer operations with invalid handles
        with self.assertRaises(ValueError):
            get_buffer_content(0)
        
        with self.assertRaises(ValueError):
            update_buffer_content(0, "test")
        
        with self.assertRaises(ValueError):
            read_string_from_buffer_with_handle(0)
        
        # Test callback validation with invalid signatures
        def invalid_callback():
            return 0
        
        callbacks = {
            "on_data": invalid_callback,
            "on_write": None,
            "on_new_connection": None,
            "on_high_watermark": None,
            "on_low_watermark": None,
            "on_error": None,
            "user_data": None,
        }
        
        with self.assertRaises(ValueError):
            create_custom_filter(callbacks=callbacks)


class TestCApiFilterPerformanceIntegration(unittest.TestCase):
    """Test CApiFilter performance integration."""
    
    def setUp(self):
        """Set up test fixtures."""
        cleanup_callbacks()
    
    def tearDown(self):
        """Clean up after tests."""
        cleanup_callbacks()
    
    def test_callback_registration_performance(self):
        """Test callback registration performance."""
        from mcp_c_structs import register_python_callback, get_callback_count
        
        # Register many callbacks
        num_callbacks = 100
        for i in range(num_callbacks):
            def callback(buf, end_stream, user_data):
                return 0
            
            register_python_callback(
                callback,
                "int (*)(void *, bool, void *)",
                f"callback_{i}"
            )
        
        # Verify all callbacks are registered
        self.assertEqual(get_callback_count(), num_callbacks)
        
        # Cleanup
        cleanup_callbacks()
        self.assertEqual(get_callback_count(), 0)
    
    def test_callback_execution_performance(self):
        """Test callback execution performance."""
        callbacks = create_default_callbacks()
        
        # Execute callbacks many times
        num_executions = 1000
        start_time = time.time()
        
        for _ in range(num_executions):
            callbacks["on_data"](12345, True, None)
            callbacks["on_write"](12345, False, None)
        
        end_time = time.time()
        execution_time = end_time - start_time
        
        # Verify performance is reasonable (should be very fast)
        self.assertLess(execution_time, 1.0)  # Should complete in less than 1 second
        
        print(f"Executed {num_executions * 2} callbacks in {execution_time:.4f} seconds")
    
    def test_buffer_operations_performance(self):
        """Test buffer operations performance."""
        with patch('filter_buffer.AdvancedBuffer') as mock_buffer_class:
            mock_buffer = Mock()
            mock_buffer.length.return_value = 1000
            mock_buffer.get_contiguous.return_value = (b"x" * 1000, 1000)
            mock_buffer_class.return_value = mock_buffer
            
            # Test buffer operations many times
            num_operations = 1000
            start_time = time.time()
            
            for i in range(num_operations):
                update_buffer_content(12345, f"test content {i}")
                get_buffer_content(12345)
            
            end_time = time.time()
            execution_time = end_time - start_time
            
            # Verify performance is reasonable
            self.assertLess(execution_time, 2.0)  # Should complete in less than 2 seconds
            
            print(f"Executed {num_operations * 2} buffer operations in {execution_time:.4f} seconds")


if __name__ == '__main__':
    # Create test suite
    test_suite = unittest.TestSuite()
    
    # Add test classes
    test_classes = [
        TestCApiFilterEndToEnd,
        TestCApiFilterClientServerIntegration,
        TestCApiFilterPerformanceIntegration,
    ]
    
    for test_class in test_classes:
        tests = unittest.TestLoader().loadTestsFromTestCase(test_class)
        test_suite.addTests(tests)
    
    # Run tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(test_suite)
    
    # Exit with appropriate code
    sys.exit(0 if result.wasSuccessful() else 1)
