"""
Test runner for the Python SDK.

This module provides a comprehensive test runner that executes all test cases
and generates detailed reports.
"""

import unittest
import sys
import os
import time
import json
from io import StringIO
from typing import Dict, List, Any

# Add src to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

# Import test modules
from test_filter_api import TestFilterAPI
from test_filter_buffer import TestFilterBuffer
from test_filter_chain import TestFilterChain
from test_filter_manager import TestFilterManager
from test_new_functions import TestNewFunctions


class TestRunner:
    """Comprehensive test runner for the Python SDK."""
    
    def __init__(self):
        """Initialize the test runner."""
        self.test_suites = []
        self.results = {}
        self.start_time = None
        self.end_time = None
    
    def add_test_suite(self, test_class, suite_name: str):
        """Add a test suite to the runner."""
        suite = unittest.TestLoader().loadTestsFromTestCase(test_class)
        self.test_suites.append((suite, suite_name))
    
    def run_all_tests(self) -> Dict[str, Any]:
        """Run all test suites and return comprehensive results."""
        print("=" * 80)
        print("Python SDK Test Runner")
        print("=" * 80)
        
        self.start_time = time.time()
        
        # Add all test suites
        self.add_test_suite(TestFilterAPI, "Filter API Tests")
        self.add_test_suite(TestFilterBuffer, "Filter Buffer Tests")
        self.add_test_suite(TestFilterChain, "Filter Chain Tests")
        self.add_test_suite(TestFilterManager, "Filter Manager Tests")
        self.add_test_suite(TestNewFunctions, "New Functions Tests")
        
        total_tests = 0
        total_failures = 0
        total_errors = 0
        total_skipped = 0
        
        for suite, suite_name in self.test_suites:
            print(f"\n--- Running {suite_name} ---")
            
            # Capture test output
            stream = StringIO()
            runner = unittest.TextTestRunner(stream=stream, verbosity=2)
            result = runner.run(suite)
            
            # Count results
            tests_run = result.testsRun
            failures = len(result.failures)
            errors = len(result.errors)
            skipped = len(result.skipped) if hasattr(result, 'skipped') else 0
            
            total_tests += tests_run
            total_failures += failures
            total_errors += errors
            total_skipped += skipped
            
            # Store results
            self.results[suite_name] = {
                "tests_run": tests_run,
                "failures": failures,
                "errors": errors,
                "skipped": skipped,
                "success_rate": ((tests_run - failures - errors) / tests_run * 100) if tests_run > 0 else 0,
                "output": stream.getvalue()
            }
            
            # Print summary
            print(f"Tests run: {tests_run}")
            print(f"Failures: {failures}")
            print(f"Errors: {errors}")
            print(f"Skipped: {skipped}")
            print(f"Success rate: {self.results[suite_name]['success_rate']:.1f}%")
            
            if failures > 0 or errors > 0:
                print("\n--- Failures and Errors ---")
                for test, traceback in result.failures + result.errors:
                    print(f"FAILED: {test}")
                    print(traceback)
                    print("-" * 40)
        
        self.end_time = time.time()
        duration = self.end_time - self.start_time
        
        # Print overall summary
        print("\n" + "=" * 80)
        print("OVERALL TEST SUMMARY")
        print("=" * 80)
        print(f"Total tests run: {total_tests}")
        print(f"Total failures: {total_failures}")
        print(f"Total errors: {total_errors}")
        print(f"Total skipped: {total_skipped}")
        print(f"Total duration: {duration:.2f} seconds")
        
        overall_success_rate = ((total_tests - total_failures - total_errors) / total_tests * 100) if total_tests > 0 else 0
        print(f"Overall success rate: {overall_success_rate:.1f}%")
        
        # Print per-suite summary
        print("\n--- Per-Suite Summary ---")
        for suite_name, result in self.results.items():
            print(f"{suite_name}: {result['tests_run']} tests, {result['success_rate']:.1f}% success")
        
        # Return comprehensive results
        return {
            "overall": {
                "total_tests": total_tests,
                "total_failures": total_failures,
                "total_errors": total_errors,
                "total_skipped": total_skipped,
                "success_rate": overall_success_rate,
                "duration": duration
            },
            "suites": self.results
        }
    
    def generate_report(self, results: Dict[str, Any], output_file: str = "test_report.json"):
        """Generate a detailed test report."""
        report = {
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "python_version": sys.version,
            "platform": sys.platform,
            "results": results
        }
        
        with open(output_file, 'w') as f:
            json.dump(report, f, indent=2)
        
        print(f"\nDetailed test report saved to: {output_file}")
    
    def run_specific_suite(self, suite_name: str) -> Dict[str, Any]:
        """Run a specific test suite."""
        print(f"Running {suite_name} only...")
        
        # Find the requested suite
        target_suite = None
        for suite, name in self.test_suites:
            if name == suite_name:
                target_suite = suite
                break
        
        if not target_suite:
            print(f"Error: Test suite '{suite_name}' not found")
            return {}
        
        # Run the specific suite
        stream = StringIO()
        runner = unittest.TextTestRunner(stream=stream, verbosity=2)
        result = runner.run(target_suite)
        
        # Count results
        tests_run = result.testsRun
        failures = len(result.failures)
        errors = len(result.errors)
        skipped = len(result.skipped) if hasattr(result, 'skipped') else 0
        
        # Print results
        print(f"Tests run: {tests_run}")
        print(f"Failures: {failures}")
        print(f"Errors: {errors}")
        print(f"Skipped: {skipped}")
        print(f"Success rate: {((tests_run - failures - errors) / tests_run * 100) if tests_run > 0 else 0:.1f}%")
        
        if failures > 0 or errors > 0:
            print("\n--- Failures and Errors ---")
            for test, traceback in result.failures + result.errors:
                print(f"FAILED: {test}")
                print(traceback)
                print("-" * 40)
        
        return {
            "suite_name": suite_name,
            "tests_run": tests_run,
            "failures": failures,
            "errors": errors,
            "skipped": skipped,
            "success_rate": ((tests_run - failures - errors) / tests_run * 100) if tests_run > 0 else 0,
            "output": stream.getvalue()
        }


def main():
    """Main function to run tests."""
    import argparse
    
    parser = argparse.ArgumentParser(description="Run Python SDK tests")
    parser.add_argument("--suite", help="Run specific test suite", 
                       choices=["Filter API Tests", "Filter Buffer Tests", "Filter Chain Tests", "Filter Manager Tests", "New Functions Tests"])
    parser.add_argument("--report", help="Generate detailed report", action="store_true")
    parser.add_argument("--output", help="Output file for report", default="test_report.json")
    
    args = parser.parse_args()
    
    runner = TestRunner()
    
    if args.suite:
        # Run specific suite
        results = runner.run_specific_suite(args.suite)
        if args.report:
            runner.generate_report({"specific_suite": results}, args.output)
    else:
        # Run all tests
        results = runner.run_all_tests()
        if args.report:
            runner.generate_report(results, args.output)
    
    # Exit with appropriate code
    if args.suite:
        if results.get("failures", 0) > 0 or results.get("errors", 0) > 0:
            sys.exit(1)
    else:
        if results["overall"]["total_failures"] > 0 or results["overall"]["total_errors"] > 0:
            sys.exit(1)


if __name__ == "__main__":
    main()
