# Makefile for MCP C++ SDK

.PHONY: all build test test-verbose test-parallel test-list check check-verbose check-parallel clean release debug help format check-format install uninstall

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

# Install targets
install:
	@if [ ! -d build ]; then \
		echo "Error: build directory not found. Please run 'make build' first."; \
		exit 1; \
	fi
	@echo "Installing gopher-mcp..."
	@cd build && make install

uninstall:
	@if [ ! -d build ]; then \
		echo "Error: build directory not found."; \
		exit 1; \
	fi
	@echo "Uninstalling gopher-mcp..."
	@cd build && make uninstall

# Help
help:
	@echo "╔════════════════════════════════════════════════════════════════════╗"
	@echo "║                     GOPHER MCP C++ SDK BUILD SYSTEM                   ║"
	@echo "╚════════════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "┌─ BUILD TARGETS ─────────────────────────────────────────────────────┐"
	@echo "│ make               Build and run tests (debug mode)                   │"
	@echo "│ make build         Build libraries only, skip tests                   │"
	@echo "│ make debug         Build in debug mode with full tests               │"
	@echo "│ make release       Build optimized release mode with tests           │"
	@echo "│ make verbose       Build with verbose output (shows commands)        │"
	@echo "│ make rebuild       Clean and rebuild everything from scratch         │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ TEST TARGETS ──────────────────────────────────────────────────────┐"
	@echo "│ make test          Run tests with minimal output (recommended)       │"
	@echo "│ make test-verbose  Run tests with detailed output                    │"
	@echo "│ make test-parallel Run tests in parallel (8 threads)                 │"
	@echo "│ make test-list     List all available test cases                     │"
	@echo "│ make check         Alias for 'make test'                             │"
	@echo "│ make check-verbose Alias for 'make test-verbose'                     │"
	@echo "│ make check-parallel Alias for 'make test-parallel'                   │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ INSTALLATION TARGETS ──────────────────────────────────────────────┐"
	@echo "│ make install       Install libraries, headers, and CMake configs     │"
	@echo "│ make uninstall     Remove all installed files                        │"
	@echo "│                                                                       │"
	@echo "│ Installation customization (use with cmake command):                 │"
	@echo "│   CMAKE_INSTALL_PREFIX=/path  Set installation directory             │"
	@echo "│   BUILD_SHARED_LIBS=ON/OFF    Build shared libraries (default: ON)   │"
	@echo "│   BUILD_STATIC_LIBS=ON/OFF    Build static libraries (default: ON)   │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ CODE QUALITY TARGETS ──────────────────────────────────────────────┐"
	@echo "│ make format        Auto-format all source files with clang-format    │"
	@echo "│ make check-format  Check formatting without modifying files          │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ MAINTENANCE TARGETS ───────────────────────────────────────────────┐"
	@echo "│ make clean         Remove build directory and all artifacts          │"
	@echo "│ make help          Show this help message                            │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ COMMON USAGE EXAMPLES ─────────────────────────────────────────────┐"
	@echo "│ Quick build and test:                                                │"
	@echo "│   $$ make                                                             │"
	@echo "│                                                                       │"
	@echo "│ Production build with installation:                                  │"
	@echo "│   $$ make release                                                     │"
	@echo "│   $$ sudo make install                                                │"
	@echo "│                                                                       │"
	@echo "│ Development workflow:                                                │"
	@echo "│   $$ make format          # Format code                              │"
	@echo "│   $$ make build           # Build without tests                      │"
	@echo "│   $$ make test-parallel   # Run tests quickly                        │"
	@echo "│                                                                       │"
	@echo "│ Clean rebuild:                                                       │"
	@echo "│   $$ make clean && make                                              │"
	@echo "│                                                                       │"
	@echo "│ Custom installation:                                                 │"
	@echo "│   $$ cmake -B build -DCMAKE_INSTALL_PREFIX=/opt/gopher-mcp           │"
	@echo "│   $$ make -C build                                                   │"
	@echo "│   $$ make install                                                    │"
	@echo "│                                                                       │"
	@echo "│ Build only static libraries:                                         │"
	@echo "│   $$ cmake -B build -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON  │"
	@echo "│   $$ make -C build                                                   │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ BUILD OPTIONS (configure with cmake) ──────────────────────────────┐"
	@echo "│ • BUILD_SHARED_LIBS     Build shared libraries (.so/.dylib/.dll)     │"
	@echo "│ • BUILD_STATIC_LIBS     Build static libraries (.a/.lib)             │"
	@echo "│ • BUILD_TESTS           Build test executables                       │"
	@echo "│ • BUILD_EXAMPLES        Build example programs                       │"
	@echo "│ • BUILD_C_API           Build C API bindings                         │"
	@echo "│ • MCP_USE_STD_TYPES     Use std::optional/variant if available       │"
	@echo "│ • MCP_USE_LLHTTP        Enable llhttp for HTTP/1.x parsing           │"
	@echo "│ • MCP_USE_NGHTTP2       Enable nghttp2 for HTTP/2 support           │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ INSTALLED COMPONENTS ──────────────────────────────────────────────┐"
	@echo "│ Libraries:                                                           │"
	@echo "│   • libgopher-mcp         Main MCP SDK library                       │"
	@echo "│   • libgopher-mcp-event   Event loop and async I/O                   │"
	@echo "│   • libgopher-mcp-echo-advanced  Advanced echo components            │"
	@echo "│                                                                       │"
	@echo "│ Headers:                                                              │"
	@echo "│   • include/gopher-mcp/mcp/  All public headers                      │"
	@echo "│                                                                       │"
	@echo "│ Integration files:                                                   │"
	@echo "│   • lib/cmake/gopher-mcp/  CMake package config files                │"
	@echo "│   • lib/pkgconfig/*.pc     pkg-config files for Unix systems         │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "For more information, see README.md or visit the project repository."