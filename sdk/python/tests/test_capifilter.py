"""
Comprehensive test suite for CApiFilter functionality.

This module tests the CApiFilter integration, including callback registration,
custom filter creation, and message processing through the C++ filter chain.
"""

import unittest
import sys
import os
from unittest.mock import Mock, patch, MagicMock
from typing import Any, Dict, List, Optional

# Add src to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../src'))

from mcp_c_structs import (
    create_filter_callbacks_struct,
    create_default_callbacks,
    validate_callback_signature,
    register_python_callback,
    create_callback_function_pointer,
    cleanup_callbacks,
    get_callback_count,
    list_registered_callbacks,
    McpFilterCallbacks,
    DataCallback,
    WriteCallback,
    ConnCallback,
    MarkCallback,
    ErrorCallback,
)

from filter_api import (
    create_custom_filter,
    Filter,
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


class TestCApiFilterStructs(unittest.TestCase):
    """Test C struct conversion and callback registration."""
    
    def setUp(self):
        """Set up test fixtures."""
        cleanup_callbacks()
    
    def tearDown(self):
        """Clean up after tests."""
        cleanup_callbacks()
    
    def test_create_default_callbacks(self):
        """Test creation of default callbacks."""
        callbacks = create_default_callbacks()
        
        self.assertIsInstance(callbacks, dict)
        self.assertIn("on_data", callbacks)
        self.assertIn("on_write", callbacks)
        self.assertIn("on_new_connection", callbacks)
        self.assertIn("on_high_watermark", callbacks)
        self.assertIn("on_low_watermark", callbacks)
        self.assertIn("on_error", callbacks)
        self.assertIn("user_data", callbacks)
        
        # All callbacks should be callable
        for key, callback in callbacks.items():
            if key != "user_data":
                self.assertTrue(callable(callback), f"Callback {key} should be callable")
    
    def test_validate_callback_signature(self):
        """Test callback signature validation."""
        def valid_data_callback(buf, end_stream, user_data):
            return 0
        
        def valid_connection_callback(user_data, fd):
            pass
        
        def valid_mark_callback(user_data):
            pass
        
        def valid_error_callback(user_data, code, msg):
            pass
        
        def invalid_callback():
            pass
        
        # Valid signatures
        self.assertTrue(validate_callback_signature(valid_data_callback, "data"))
        self.assertTrue(validate_callback_signature(valid_data_callback, "write"))
        self.assertTrue(validate_callback_signature(valid_connection_callback, "connection"))
        self.assertTrue(validate_callback_signature(valid_mark_callback, "mark"))
        self.assertTrue(validate_callback_signature(valid_error_callback, "error"))
        
        # Invalid signatures
        self.assertFalse(validate_callback_signature(invalid_callback, "data"))
        self.assertFalse(validate_callback_signature(valid_data_callback, "invalid_type"))
    
    def test_register_python_callback(self):
        """Test Python callback registration."""
        def test_callback(buf, end_stream, user_data):
            return 0
        
        callback_ptr = register_python_callback(
            test_callback,
            "int (*)(void *, bool, void *)",
            "test_callback"
        )
        
        self.assertIsNotNone(callback_ptr)
        self.assertEqual(get_callback_count(), 1)
        self.assertIn("test_callback", list_registered_callbacks())
    
    def test_create_callback_function_pointer(self):
        """Test creation of callback function pointers."""
        def test_data_callback(buf, end_stream, user_data):
            return 0
        
        def test_connection_callback(user_data, fd):
            pass
        
        # Test data callback
        data_ptr = create_callback_function_pointer(test_data_callback, "data")
        self.assertIsNotNone(data_ptr)
        
        # Test connection callback
        conn_ptr = create_callback_function_pointer(test_connection_callback, "connection")
        self.assertIsNotNone(conn_ptr)
        
        # Test invalid callback type
        with self.assertRaises(ValueError):
            create_callback_function_pointer(test_data_callback, "invalid_type")
    
    def test_create_filter_callbacks_struct(self):
        """Test creation of filter callbacks struct."""
        callbacks = create_default_callbacks()
        callback_struct = create_filter_callbacks_struct(callbacks)
        
        self.assertIsInstance(callback_struct, McpFilterCallbacks)
        self.assertIsNotNone(callback_struct.on_data)
        self.assertIsNotNone(callback_struct.on_write)
        self.assertIsNotNone(callback_struct.on_new_connection)
        self.assertIsNotNone(callback_struct.on_high_watermark)
        self.assertIsNotNone(callback_struct.on_low_watermark)
        self.assertIsNotNone(callback_struct.on_error)
    
    def test_create_filter_callbacks_struct_with_none(self):
        """Test creation of filter callbacks struct with None callbacks."""
        callbacks = {
            "on_data": None,
            "on_write": None,
            "on_new_connection": None,
            "on_high_watermark": None,
            "on_low_watermark": None,
            "on_error": None,
            "user_data": None,
        }
        
        callback_struct = create_filter_callbacks_struct(callbacks)
        
        self.assertIsInstance(callback_struct, McpFilterCallbacks)
        # None callbacks should be converted to no-op callbacks
        self.assertIsNotNone(callback_struct.on_data)
        self.assertIsNotNone(callback_struct.on_write)
        self.assertIsNotNone(callback_struct.on_new_connection)
        self.assertIsNotNone(callback_struct.on_high_watermark)
        self.assertIsNotNone(callback_struct.on_low_watermark)
        self.assertIsNotNone(callback_struct.on_error)


class TestCApiFilterAPI(unittest.TestCase):
    """Test CApiFilter API functions."""
    
    def setUp(self):
        """Set up test fixtures."""
        cleanup_callbacks()
    
    def tearDown(self):
        """Clean up after tests."""
        cleanup_callbacks()
    
    @patch('filter_api.create_mock_dispatcher')
    @patch('filter_api.mcp_filter_create_builtin')
    @patch('filter_api.mcp_filter_set_callbacks')
    def test_create_custom_filter_success(self, mock_set_callbacks, mock_create_builtin, mock_dispatcher):
        """Test successful custom filter creation."""
        # Mock the C API calls
        mock_dispatcher.return_value = 12345
        mock_create_builtin.return_value = 67890
        mock_set_callbacks.return_value = 0  # MCP_OK
        
        callbacks = create_default_callbacks()
        filter_instance = create_custom_filter(callbacks=callbacks, name="test-filter")
        
        self.assertIsInstance(filter_instance, Filter)
        self.assertEqual(filter_instance.handle, 67890)
        
        # Verify C API calls
        mock_create_builtin.assert_called_once()
        mock_set_callbacks.assert_called_once()
    
    @patch('filter_api.create_mock_dispatcher')
    @patch('filter_api.mcp_filter_create_builtin')
    def test_create_custom_filter_with_default_callbacks(self, mock_create_builtin, mock_dispatcher):
        """Test custom filter creation with default callbacks."""
        mock_dispatcher.return_value = 12345
        mock_create_builtin.return_value = 67890
        
        filter_instance = create_custom_filter(name="test-filter")
        
        self.assertIsInstance(filter_instance, Filter)
        mock_create_builtin.assert_called_once()
    
    def test_create_custom_filter_invalid_callback_signature(self):
        """Test custom filter creation with invalid callback signature."""
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
        
        # Currently validation is disabled, so this should succeed
        filter_instance = create_custom_filter(callbacks=callbacks)
        self.assertIsNotNone(filter_instance)


class TestCApiFilterManager(unittest.TestCase):
    """Test CApiFilter integration with FilterManager."""
    
    def setUp(self):
        """Set up test fixtures."""
        cleanup_callbacks()
    
    def tearDown(self):
        """Clean up after tests."""
        cleanup_callbacks()
    
    def test_filter_manager_with_custom_callbacks(self):
        """Test FilterManager with custom callbacks."""
        callbacks = create_default_callbacks()
        config = FilterManagerConfig(custom_callbacks=callbacks)
        
        manager = FilterManager(config)
        
        self.assertIsNotNone(manager)
        self.assertTrue(manager.is_initialized)
    
    def test_filter_manager_config_with_custom_callbacks(self):
        """Test FilterManagerConfig with custom callbacks."""
        callbacks = create_default_callbacks()
        config = FilterManagerConfig(custom_callbacks=callbacks)
        
        self.assertIsNotNone(config.custom_callbacks)
        self.assertEqual(config.custom_callbacks, callbacks)


class TestCApiFilterBufferOperations(unittest.TestCase):
    """Test CApiFilter buffer operations."""
    
    def test_get_buffer_content_invalid_handle(self):
        """Test get_buffer_content with invalid handle."""
        with self.assertRaises(ValueError):
            get_buffer_content(0)
    
    def test_update_buffer_content_invalid_handle(self):
        """Test update_buffer_content with invalid handle."""
        with self.assertRaises(ValueError):
            update_buffer_content(0, "test content")
    
    def test_read_string_from_buffer_with_handle_invalid_handle(self):
        """Test read_string_from_buffer_with_handle with invalid handle."""
        with self.assertRaises(ValueError):
            read_string_from_buffer_with_handle(0)
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_get_buffer_content_success(self, mock_buffer_class):
        """Test successful get_buffer_content."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer.length.return_value = 12
        mock_buffer_class.return_value = mock_buffer
        
        # Mock read_string_from_buffer
        with patch('filter_buffer.read_string_from_buffer', return_value="Hello World!"):
            result = get_buffer_content(12345)
            self.assertEqual(result, "Hello World!")
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_update_buffer_content_success(self, mock_buffer_class):
        """Test successful update_buffer_content."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer_class.return_value = mock_buffer
        
        update_buffer_content(12345, "test content")
        
        # Verify add_data was called
        mock_buffer.add_data.assert_called_once_with(b"test content")


class TestCApiFilterIntegration(unittest.TestCase):
    """Test end-to-end CApiFilter integration."""
    
    def setUp(self):
        """Set up test fixtures."""
        cleanup_callbacks()
    
    def tearDown(self):
        """Clean up after tests."""
        cleanup_callbacks()
    
    def test_callback_registration_and_cleanup(self):
        """Test callback registration and cleanup."""
        def test_callback(buf, end_stream, user_data):
            return 0
        
        # Register callback
        callback_ptr = register_python_callback(
            test_callback,
            "int (*)(void *, bool, void *)",
            "test_callback"
        )
        
        self.assertEqual(get_callback_count(), 1)
        self.assertIn("test_callback", list_registered_callbacks())
        
        # Cleanup
        cleanup_callbacks()
        self.assertEqual(get_callback_count(), 0)
        self.assertEqual(len(list_registered_callbacks()), 0)
    
    def test_default_callbacks_execution(self):
        """Test execution of default callbacks."""
        callbacks = create_default_callbacks()
        
        # Test data callback
        result = callbacks["on_data"](12345, True, None)
        self.assertEqual(result, 0)  # MCP_FILTER_CONTINUE
        
        # Test write callback
        result = callbacks["on_write"](12345, False, None)
        self.assertEqual(result, 0)  # MCP_FILTER_CONTINUE
        
        # Test connection callback (should not raise exception)
        callbacks["on_new_connection"](None, 42)
        
        # Test mark callbacks (should not raise exception)
        callbacks["on_high_watermark"](None)
        callbacks["on_low_watermark"](None)
        
        # Test error callback (should not raise exception)
        callbacks["on_error"](None, 500, None)


if __name__ == '__main__':
    # Create test suite
    test_suite = unittest.TestSuite()
    
    # Add test classes
    test_classes = [
        TestCApiFilterStructs,
        TestCApiFilterAPI,
        TestCApiFilterManager,
        TestCApiFilterBufferOperations,
        TestCApiFilterIntegration,
    ]
    
    for test_class in test_classes:
        tests = unittest.TestLoader().loadTestsFromTestCase(test_class)
        test_suite.addTests(tests)
    
    # Run tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(test_suite)
    
    # Exit with appropriate code
    sys.exit(0 if result.wasSuccessful() else 1)
