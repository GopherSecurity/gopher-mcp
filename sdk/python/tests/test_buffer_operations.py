"""
Test suite for buffer operations with error handling.

This module tests the buffer operations functions, including error handling
and edge cases for buffer content manipulation.
"""

import unittest
import sys
import os
from unittest.mock import Mock, patch, MagicMock

# Add src to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../src'))

from filter_buffer import (
    get_buffer_content,
    update_buffer_content,
    read_string_from_buffer_with_handle,
    AdvancedBuffer,
)


class TestBufferOperations(unittest.TestCase):
    """Test buffer operations with error handling."""
    
    def test_get_buffer_content_invalid_handle_zero(self):
        """Test get_buffer_content with handle 0 (invalid)."""
        with self.assertRaises(ValueError) as context:
            get_buffer_content(0)
        
        self.assertIn("Invalid buffer handle: 0", str(context.exception))
    
    def test_get_buffer_content_invalid_handle_negative(self):
        """Test get_buffer_content with negative handle (invalid)."""
        with self.assertRaises(ValueError) as context:
            get_buffer_content(-1)
        
        self.assertIn("Invalid buffer handle: -1", str(context.exception))
    
    def test_update_buffer_content_invalid_handle_zero(self):
        """Test update_buffer_content with handle 0 (invalid)."""
        with self.assertRaises(ValueError) as context:
            update_buffer_content(0, "test content")
        
        self.assertIn("Invalid buffer handle: 0", str(context.exception))
    
    def test_update_buffer_content_invalid_handle_negative(self):
        """Test update_buffer_content with negative handle (invalid)."""
        with self.assertRaises(ValueError) as context:
            update_buffer_content(-1, "test content")
        
        self.assertIn("Invalid buffer handle: -1", str(context.exception))
    
    def test_read_string_from_buffer_with_handle_invalid_handle_zero(self):
        """Test read_string_from_buffer_with_handle with handle 0 (invalid)."""
        with self.assertRaises(ValueError) as context:
            read_string_from_buffer_with_handle(0)
        
        self.assertIn("Invalid buffer handle: 0", str(context.exception))
    
    def test_read_string_from_buffer_with_handle_invalid_handle_negative(self):
        """Test read_string_from_buffer_with_handle with negative handle (invalid)."""
        with self.assertRaises(ValueError) as context:
            read_string_from_buffer_with_handle(-1)
        
        self.assertIn("Invalid buffer handle: -1", str(context.exception))
    
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
    def test_get_buffer_content_empty_buffer(self, mock_buffer_class):
        """Test get_buffer_content with empty buffer."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer.length.return_value = 0
        mock_buffer_class.return_value = mock_buffer
        
        # Mock read_string_from_buffer
        with patch('filter_buffer.read_string_from_buffer', return_value=""):
            result = get_buffer_content(12345)
            self.assertEqual(result, "")
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_get_buffer_content_exception(self, mock_buffer_class):
        """Test get_buffer_content with exception."""
        # Mock buffer to raise exception
        mock_buffer_class.side_effect = RuntimeError("Buffer operation failed")
        
        with self.assertRaises(RuntimeError) as context:
            get_buffer_content(12345)
        
        self.assertIn("Failed to get buffer content", str(context.exception))
        self.assertIn("Buffer operation failed", str(context.exception))
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_update_buffer_content_success(self, mock_buffer_class):
        """Test successful update_buffer_content."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer_class.return_value = mock_buffer
        
        update_buffer_content(12345, "test content")
        
        # Verify add_data was called with correct bytes
        mock_buffer.add_data.assert_called_once_with(b"test content")
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_update_buffer_content_empty_string(self, mock_buffer_class):
        """Test update_buffer_content with empty string."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer_class.return_value = mock_buffer
        
        update_buffer_content(12345, "")
        
        # Verify add_data was called with empty bytes
        mock_buffer.add_data.assert_called_once_with(b"")
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_update_buffer_content_unicode_string(self, mock_buffer_class):
        """Test update_buffer_content with unicode string."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer_class.return_value = mock_buffer
        
        unicode_content = "Hello 世界! 🌍"
        update_buffer_content(12345, unicode_content)
        
        # Verify add_data was called with correct UTF-8 bytes
        expected_bytes = unicode_content.encode('utf-8')
        mock_buffer.add_data.assert_called_once_with(expected_bytes)
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_update_buffer_content_exception(self, mock_buffer_class):
        """Test update_buffer_content with exception."""
        # Mock buffer to raise exception
        mock_buffer_class.side_effect = RuntimeError("Buffer operation failed")
        
        with self.assertRaises(RuntimeError) as context:
            update_buffer_content(12345, "test content")
        
        self.assertIn("Failed to update buffer content", str(context.exception))
        self.assertIn("Buffer operation failed", str(context.exception))
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_read_string_from_buffer_with_handle_success(self, mock_buffer_class):
        """Test successful read_string_from_buffer_with_handle."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer.length.return_value = 12
        mock_buffer_class.return_value = mock_buffer
        
        # Mock get_contiguous
        mock_buffer.get_contiguous.return_value = (b"Hello World!", 12)
        
        result = read_string_from_buffer_with_handle(12345)
        self.assertEqual(result, "Hello World!")
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_read_string_from_buffer_with_handle_empty_buffer(self, mock_buffer_class):
        """Test read_string_from_buffer_with_handle with empty buffer."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer.length.return_value = 0
        mock_buffer_class.return_value = mock_buffer
        
        result = read_string_from_buffer_with_handle(12345)
        self.assertEqual(result, "")
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_read_string_from_buffer_with_handle_different_encoding(self, mock_buffer_class):
        """Test read_string_from_buffer_with_handle with different encoding."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer.length.return_value = 6
        mock_buffer_class.return_value = mock_buffer
        
        # Mock get_contiguous with latin-1 encoded data
        mock_buffer.get_contiguous.return_value = (b"Hello\xe9", 6)
        
        result = read_string_from_buffer_with_handle(12345, encoding='latin-1')
        self.assertEqual(result, "Helloé")
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_read_string_from_buffer_with_handle_exception(self, mock_buffer_class):
        """Test read_string_from_buffer_with_handle with exception."""
        # Mock buffer to raise exception
        mock_buffer_class.side_effect = RuntimeError("Buffer operation failed")
        
        with self.assertRaises(RuntimeError) as context:
            read_string_from_buffer_with_handle(12345)
        
        self.assertIn("Failed to read string from buffer", str(context.exception))
        self.assertIn("Buffer operation failed", str(context.exception))
    
    def test_buffer_handle_validation_edge_cases(self):
        """Test buffer handle validation with edge cases."""
        # Test various invalid handles
        invalid_handles = [0, -1, -100, 0xFFFFFFFF + 1]  # 0xFFFFFFFF + 1 is too large for uint64
        
        for handle in invalid_handles:
            with self.subTest(handle=handle):
                # All functions should raise ValueError for invalid handles
                with self.assertRaises(ValueError):
                    get_buffer_content(handle)
                
                with self.assertRaises(ValueError):
                    update_buffer_content(handle, "test")
                
                with self.assertRaises(ValueError):
                    read_string_from_buffer_with_handle(handle)
    
    def test_buffer_content_encoding_edge_cases(self):
        """Test buffer content encoding with edge cases."""
        test_cases = [
            ("", "empty string"),
            ("Hello World!", "ascii string"),
            ("Hello 世界! 🌍", "unicode string"),
            ("\x00\x01\x02", "binary data"),
            ("\n\r\t", "whitespace"),
            ("'\"\\", "special characters"),
        ]
        
        for content, description in test_cases:
            with self.subTest(description=description):
                with patch('filter_buffer.AdvancedBuffer') as mock_buffer_class:
                    mock_buffer = Mock()
                    mock_buffer_class.return_value = mock_buffer
                    
                    update_buffer_content(12345, content)
                    
                    # Verify add_data was called with correct bytes
                    expected_bytes = content.encode('utf-8')
                    mock_buffer.add_data.assert_called_once_with(expected_bytes)


class TestBufferOperationsIntegration(unittest.TestCase):
    """Test buffer operations integration scenarios."""
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_buffer_roundtrip(self, mock_buffer_class):
        """Test buffer content roundtrip (write then read)."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer.length.return_value = 12
        mock_buffer.get_contiguous.return_value = (b"Hello World!", 12)
        mock_buffer_class.return_value = mock_buffer
        
        # Write content
        update_buffer_content(12345, "Hello World!")
        
        # Read content
        result = get_buffer_content(12345)
        
        # Verify operations
        mock_buffer.add_data.assert_called_once_with(b"Hello World!")
        self.assertEqual(result, "Hello World!")
    
    @patch('filter_buffer.AdvancedBuffer')
    def test_buffer_operations_with_different_encodings(self, mock_buffer_class):
        """Test buffer operations with different encodings."""
        # Mock buffer
        mock_buffer = Mock()
        mock_buffer.length.return_value = 6
        mock_buffer.get_contiguous.return_value = (b"Hello\xe9", 6)
        mock_buffer_class.return_value = mock_buffer
        
        # Test with latin-1 encoding
        result = read_string_from_buffer_with_handle(12345, encoding='latin-1')
        self.assertEqual(result, "Helloé")
        
        # Test with utf-8 encoding (should fail gracefully)
        mock_buffer.get_contiguous.return_value = (b"Hello\xe9", 6)
        with self.assertRaises(UnicodeDecodeError):
            read_string_from_buffer_with_handle(12345, encoding='utf-8')


if __name__ == '__main__':
    # Create test suite
    test_suite = unittest.TestSuite()
    
    # Add test classes
    test_classes = [
        TestBufferOperations,
        TestBufferOperationsIntegration,
    ]
    
    for test_class in test_classes:
        tests = unittest.TestLoader().loadTestsFromTestCase(test_class)
        test_suite.addTests(tests)
    
    # Run tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(test_suite)
    
    # Exit with appropriate code
    sys.exit(0 if result.wasSuccessful() else 1)
