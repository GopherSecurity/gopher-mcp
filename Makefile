# Makefile for MCP C++ SDK

.PHONY: all build test test-verbose test-parallel test-list check check-verbose check-parallel clean release debug help format check-format

# Default target
all: build test

# Build in debug mode
debug:
	@./build.sh

# Build in release mode
release:
	@./build.sh --release

# Build without running tests
build:
	@./build.sh --no-tests

# Run tests with minimal output (assumes already built)
test:
	@echo "Running all tests..."
	@cd build && ctest --output-on-failure

# Run tests with verbose output
test-verbose:
	@echo "Running all tests (verbose)..."
	@cd build && ctest -V

# Run tests in parallel
test-parallel:
	@echo "Running all tests in parallel..."
	@cd build && ctest -j8 --output-on-failure

# Alias targets for consistency with CMake
check: test
check-verbose: test-verbose  
check-parallel: test-parallel

# List all available tests
test-list:
	@echo "Available test cases:"
	@cd build && for test in tests/test_*; do \
		if [ -x "$$test" ]; then \
			echo ""; \
			echo "=== $$(basename $$test) ==="; \
			./$$test --gtest_list_tests | sed 's/^/  /'; \
		fi; \
	done

# Clean build
clean:
	@./build.sh --clean --no-tests

# Clean and rebuild
rebuild: clean all

# Verbose build
verbose:
	@./build.sh --verbose

# Format all source files
format:
	@echo "Formatting all source files with clang-format..."
	@find . -path "./build*" -prune -o \( -name "*.h" -o -name "*.cpp" -o -name "*.cc" \) -print | xargs clang-format -i
	@echo "Formatting complete."

# Check formatting without modifying files
check-format:
	@echo "Checking source file formatting..."
	@find . -path "./build*" -prune -o \( -name "*.h" -o -name "*.cpp" -o -name "*.cc" \) -print | xargs clang-format --dry-run --Werror

# Help
help:
	@echo "MCP C++ SDK Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  make               - Build and run tests (debug mode)"
	@echo "  make debug         - Build in debug mode with tests"
	@echo "  make release       - Build in release mode with tests"
	@echo "  make build         - Build only, no tests"
	@echo ""
	@echo "Test targets:"
	@echo "  make test          - Run tests with minimal output (recommended)"
	@echo "  make test-verbose  - Run tests with detailed output"
	@echo "  make test-parallel - Run tests in parallel"
	@echo "  make test-list     - List all available test cases"
	@echo "  make check         - Alias for 'make test'"
	@echo "  make check-verbose - Alias for 'make test-verbose'"
	@echo "  make check-parallel - Alias for 'make test-parallel'"
	@echo ""
	@echo "Other targets:"
	@echo "  make clean         - Clean build directory"
	@echo "  make rebuild       - Clean and rebuild everything"
	@echo "  make verbose       - Build with verbose output"
	@echo "  make format        - Format all source files with clang-format"
	@echo "  make check-format  - Check formatting without modifying files"
	@echo "  make help          - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build and test in debug mode"
	@echo "  make release           # Build and test in release mode"
	@echo "  make clean && make     # Clean rebuild"
	@echo "  make test              # Run tests (minimal output)"
	@echo "  make test-verbose      # Run tests (detailed output)"