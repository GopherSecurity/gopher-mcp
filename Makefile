# Makefile for MCP C++ SDK

.PHONY: all build test test-verbose test-parallel test-list check check-verbose check-parallel clean release debug help format check-format install uninstall

# Configuration detection
OS := $(shell uname -s 2>/dev/null || echo Windows_NT)
CONFIG ?= Release

# Determine installation prefix
# Default to system-wide installation unless explicitly overridden
ifeq ($(CMAKE_INSTALL_PREFIX),)
    PREFIX ?= /usr/local
else
    PREFIX := $(CMAKE_INSTALL_PREFIX)
endif

# Check if we need sudo for installation
# We need sudo if the prefix directory exists and is not writable,
# or if it doesn't exist and the parent directory is not writable
define check_need_sudo
	if [ -d "$(PREFIX)" ]; then \
		test -w "$(PREFIX)" && echo no || echo yes; \
	elif [ -d "$$(dirname "$(PREFIX)")" ]; then \
		test -w "$$(dirname "$(PREFIX)")" && echo no || echo yes; \
	else \
		echo yes; \
	fi
endef
NEED_SUDO := $(shell $(check_need_sudo))
ifeq ($(NEED_SUDO),yes)
    SUDO := sudo
else
    SUDO :=
endif

# Default target
all: build test

# Build in debug mode
debug:
	@./build.sh

# Build in release mode
release:
	@./build.sh --release

# Build without running tests (includes C API by default)
build:
	@echo "Building with install prefix: $(PREFIX)"
	@if [ "$(NEED_SUDO)" = "yes" ]; then \
		echo "Note: Installation will require sudo privileges"; \
	fi
	@./build.sh --no-tests --prefix "$(PREFIX)"

# Build with specific configuration
build-with-options:
	@echo "Building with custom options (prefix: $(PREFIX))..."
	@cmake -B build -DCMAKE_INSTALL_PREFIX="$(PREFIX)" $(CMAKE_ARGS)
	@cmake --build build --config $(CONFIG)

# Build only C++ libraries (no C API)
build-cpp-only:
	@echo "Building C++ libraries only (no C API, prefix: $(PREFIX))..."
	@cmake -B build -DBUILD_C_API=OFF -DCMAKE_INSTALL_PREFIX="$(PREFIX)"
	@cmake --build build --config $(CONFIG)

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

# Install all components (C++ SDK and C API if built)
install:
	@if [ ! -d build ]; then \
		echo "Error: build directory not found. Please run 'make build' first."; \
		exit 1; \
	fi
	@echo "Installing gopher-mcp to $(PREFIX)..."
	@if [ "$(NEED_SUDO)" = "yes" ]; then \
		echo "Note: Installation to $(PREFIX) requires administrator privileges."; \
		echo "You will be prompted for your password."; \
		echo ""; \
	fi
	@$(SUDO) mkdir -p "$(PREFIX)" 2>/dev/null || true
	@if [ "$(OS)" = "Windows_NT" ]; then \
		$(SUDO) cmake --install build --prefix "$(PREFIX)" --config $(CONFIG); \
	else \
		$(SUDO) cmake --install build --prefix "$(PREFIX)"; \
	fi
	@echo ""
	@echo "Installation complete at $(PREFIX)"
	@echo "Components installed:"
	@echo "  - C++ SDK libraries and headers"
	@if [ -f "$(PREFIX)/lib/libgopher_mcp_c.so" ] || [ -f "$(PREFIX)/lib/libgopher_mcp_c.dylib" ] || [ -f "$(PREFIX)/lib/libgopher_mcp_c.a" ]; then \
		echo "  - C API library and headers"; \
	fi
	@if [ "$(PREFIX)" != "/usr/local" ] && [ "$(PREFIX)" != "/usr" ]; then \
		echo ""; \
		echo "Note: Custom installation path detected."; \
		echo "You may need to update your environment:"; \
		echo "  export LD_LIBRARY_PATH=$(PREFIX)/lib:\$$LD_LIBRARY_PATH  # Linux"; \
		echo "  export DYLD_LIBRARY_PATH=$(PREFIX)/lib:\$$DYLD_LIBRARY_PATH  # macOS"; \
		echo "  export PKG_CONFIG_PATH=$(PREFIX)/lib/pkgconfig:\$$PKG_CONFIG_PATH"; \
	fi

# Uninstall all components
uninstall:
	@if [ ! -d build ]; then \
		echo "Error: build directory not found."; \
		exit 1; \
	fi
	@echo "Uninstalling gopher-mcp from $(PREFIX)..."
	@if [ "$(NEED_SUDO)" = "yes" ]; then \
		echo "Note: Uninstalling from $(PREFIX) requires administrator privileges."; \
		echo "You will be prompted for your password."; \
		echo ""; \
	fi
	@if [ -f build/install_manifest.txt ]; then \
		if [ "$(OS)" = "Windows_NT" ]; then \
			cd build && $(SUDO) cmake --build . --target uninstall; \
		else \
			cd build && $(SUDO) $(MAKE) uninstall 2>/dev/null || \
			(echo "Running fallback uninstall..."; \
			 while IFS= read -r file; do \
				 if [ -f "$$file" ] || [ -L "$$file" ]; then \
					 $(SUDO) rm -v "$$file"; \
				 fi; \
			 done < build/install_manifest.txt); \
		fi; \
		echo "Uninstall complete."; \
	else \
		echo "Warning: install_manifest.txt not found. Manual removal may be required."; \
		echo "Typical installation locations:"; \
		echo "  - Libraries: $(PREFIX)/lib/libgopher*"; \
		echo "  - Headers: $(PREFIX)/include/gopher-mcp/"; \
		echo "  - CMake: $(PREFIX)/lib/cmake/gopher-mcp/"; \
		echo "  - pkg-config: $(PREFIX)/lib/pkgconfig/gopher-mcp*.pc"; \
	fi

# Configure cmake with custom options
configure:
	@echo "Configuring build with CMake (prefix: $(PREFIX))..."
	@cmake -B build -DCMAKE_INSTALL_PREFIX="$(PREFIX)" $(CMAKE_ARGS)

# Help
help:
	@echo "╔════════════════════════════════════════════════════════════════════╗"
	@echo "║                     GOPHER MCP C++ SDK BUILD SYSTEM                   ║"
	@echo "╚════════════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "┌─ BUILD TARGETS ─────────────────────────────────────────────────────┐"
	@echo "│ make               Build and run tests (debug mode)                   │"
	@echo "│ make build         Build all libraries (C++ SDK and C API)          │"
	@echo "│ make build-cpp-only Build only C++ SDK (exclude C API)               │"
	@echo "│ make build-with-options Build with custom CMAKE_ARGS               │"
	@echo "│ make debug         Build in debug mode with full tests               │"
	@echo "│ make release       Build optimized release mode with tests           │"
	@echo "│ make verbose       Build with verbose output (shows commands)        │"
	@echo "│ make rebuild       Clean and rebuild everything from scratch         │"
	@echo "│ make configure     Configure with custom CMAKE_ARGS                  │"
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
	@echo "│ make install       Install C++ SDK and C API (if built)              │"
	@echo "│ make uninstall     Remove all installed files                        │"
	@echo "│                                                                       │"
	@echo "│ Installation customization (use with configure or CMAKE_ARGS):       │"
	@echo "│   CMAKE_INSTALL_PREFIX=/path  Set installation directory             │"
	@echo "│                               (default: /usr/local)                  │"
	@echo "│   BUILD_C_API=ON/OFF          Build C API (default: ON)              │"
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
	@echo "│ System-wide installation (default):                                  │"
	@echo "│   $$ make build                                                      │"
	@echo "│   $$ make install                   # Will prompt for sudo if needed │"
	@echo "│                                                                       │"
	@echo "│ User-local installation (no sudo):                                   │"
	@echo "│   $$ make build CMAKE_INSTALL_PREFIX=~/.local                        │"
	@echo "│   $$ make install                                                    │"
	@echo "│                                                                       │"
	@echo "│ Custom installation:                                                 │"
	@echo "│   $$ make build CMAKE_INSTALL_PREFIX=/opt/gopher                     │"
	@echo "│   $$ make install                   # Will use sudo if needed        │"
	@echo "│                                                                       │"
	@echo "│ Build without C API:                                                 │"
	@echo "│   $$ make build-cpp-only                                             │"
	@echo "│   $$ sudo make install                                               │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ BUILD OPTIONS (configure with cmake) ──────────────────────────────┐"
	@echo "│ • BUILD_SHARED_LIBS     Build shared libraries (.so/.dylib/.dll)     │"
	@echo "│ • BUILD_STATIC_LIBS     Build static libraries (.a/.lib)             │"
	@echo "│ • BUILD_TESTS           Build test executables                       │"
	@echo "│ • BUILD_EXAMPLES        Build example programs                       │"
	@echo "│ • BUILD_C_API           Build C API for FFI bindings (default: ON)   │"
	@echo "│ • MCP_USE_STD_TYPES     Use std::optional/variant if available       │"
	@echo "│ • MCP_USE_LLHTTP        Enable llhttp for HTTP/1.x parsing           │"
	@echo "│ • MCP_USE_NGHTTP2       Enable nghttp2 for HTTP/2 support           │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ INSTALLED COMPONENTS ──────────────────────────────────────────────┐"
	@echo "│ Libraries:                                                           │"
	@echo "│   • libgopher-mcp         Main MCP SDK library (C++)                 │"
	@echo "│   • libgopher-mcp-event   Event loop and async I/O (C++)             │"
	@echo "│   • libgopher-mcp-echo-advanced  Advanced echo components (C++)      │"
	@echo "│   • libgopher_mcp_c       C API library for FFI bindings             │"
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