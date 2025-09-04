"""
Simple test runner for Python SDK.

This module runs the simplified test suite for the Python SDK.
"""

import unittest
import sys
import os

# Add src to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

# Import test modules
from test_filter_api import TestFilterAPI
from test_filter_buffer import TestFilterBuffer
from test_filter_chain import TestFilterChain
from test_filter_manager import TestFilterManager
from test_new_functions import TestNewFunctions


def run_tests():
    """Run all tests."""
    # Create test suite
    suite = unittest.TestSuite()
    
    # Add test cases
    suite.addTest(unittest.TestLoader().loadTestsFromTestCase(TestFilterAPI))
    suite.addTest(unittest.TestLoader().loadTestsFromTestCase(TestFilterBuffer))
    suite.addTest(unittest.TestLoader().loadTestsFromTestCase(TestFilterChain))
    suite.addTest(unittest.TestLoader().loadTestsFromTestCase(TestFilterManager))
    suite.addTest(unittest.TestLoader().loadTestsFromTestCase(TestNewFunctions))
    
    # Run tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    
    # Print summary
    print(f"\n{'='*50}")
    print(f"Test Summary:")
    print(f"Tests run: {result.testsRun}")
    print(f"Failures: {len(result.failures)}")
    print(f"Errors: {len(result.errors)}")
    print(f"Success rate: {((result.testsRun - len(result.failures) - len(result.errors)) / result.testsRun * 100):.1f}%")
    print(f"{'='*50}")
    
    return result.wasSuccessful()


if __name__ == '__main__':
    success = run_tests()
    sys.exit(0 if success else 1)
