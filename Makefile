# Makefile for MCP C++ SDK

.PHONY: all build test clean release debug help

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

# Run tests (assumes already built)
test:
	@cd build && ./tests/test_variant

# Clean build
clean:
	@./build.sh --clean --no-tests

# Clean and rebuild
rebuild: clean all

# Verbose build
verbose:
	@./build.sh --verbose

# Help
help:
	@echo "MCP C++ SDK Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  make          - Build and run tests (debug mode)"
	@echo "  make debug    - Build in debug mode with tests"
	@echo "  make release  - Build in release mode with tests"
	@echo "  make build    - Build only, no tests"
	@echo "  make test     - Run tests (requires prior build)"
	@echo "  make clean    - Clean build directory"
	@echo "  make rebuild  - Clean and rebuild everything"
	@echo "  make verbose  - Build with verbose output"
	@echo "  make help     - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build and test in debug mode"
	@echo "  make release           # Build and test in release mode"
	@echo "  make clean && make     # Clean rebuild"