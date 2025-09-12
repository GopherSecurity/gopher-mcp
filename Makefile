# Makefile for MCP C++ SDK

.PHONY: all build test test-verbose test-parallel test-list check check-verbose check-parallel clean release debug help format format-ts format-python format-cs check-format install uninstall csharp csharp-release csharp-test csharp-clean csharp-format

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

# Format all source files (C++ and TypeScript)
format:
	@echo "Formatting all source files..."
	@echo "Formatting C++ files with clang-format..."
	@if command -v clang-format >/dev/null 2>&1; then \
	        find . -path "./build*" -prune -o \( -name "*.h" -o -name "*.cpp" -o -name "*.cc" \) -print | xargs clang-format -i; \
	        echo "C++ formatting complete."; \
	else \
	        echo "Warning: clang-format not found, skipping C++ formatting."; \
	        echo "Install clang-format to format C++ files: brew install clang-format (macOS) or apt-get install clang-format (Ubuntu)"; \
	fi
	@echo "Formatting TypeScript files with prettier..."
	@if [ -d "sdk/typescript" ]; then \
	        cd sdk/typescript && \
	        if [ ! -f "node_modules/.bin/prettier" ]; then \
	                echo "Installing prettier for TypeScript formatting..."; \
	                npm install --save-dev prettier @typescript-eslint/parser @typescript-eslint/eslint-plugin; \
	        fi; \
	        ./node_modules/.bin/prettier --write "src/**/*.ts" "examples/**/*.ts" "mcp-example/src/**/*.ts" "**/*.json" "**/*.md" --ignore-path .gitignore; \
	        echo "TypeScript formatting complete."; \
	else \
	        echo "TypeScript SDK directory not found, skipping TypeScript formatting."; \
	fi
	@echo "Formatting Python files with black..."
	@if [ -d "sdk/python" ]; then \
	        if command -v black >/dev/null 2>&1; then \
	                cd sdk/python && black . --line-length 100 --target-version py38; \
	                echo "Python formatting complete."; \
	        else \
	                echo "Installing black for Python formatting..."; \
	                pip install black; \
	                cd sdk/python && black . --line-length 100 --target-version py38; \
	                echo "Python formatting complete."; \
	        fi; \
	else \
	        echo "Python SDK directory not found, skipping Python formatting."; \
	fi
	@echo "Formatting Rust files with rustfmt..."
	@if [ -d "sdk/rust" ]; then \
	        cd sdk/rust && \
	        if command -v rustfmt >/dev/null 2>&1; then \
	                rustfmt --edition 2021 src/**/*.rs; \
	                echo "Rust formatting complete."; \
	        else \
	                echo "Installing rustfmt for Rust formatting..."; \
	                rustup component add rustfmt; \
	                rustfmt --edition 2021 src/**/*.rs; \
	                echo "Rust formatting complete."; \
	        fi; \
	else \
	        echo "Rust SDK directory not found, skipping Rust formatting."; \
	fi
	@echo "Formatting C# files with dotnet format..."
	@if [ -d "sdk/csharp" ]; then \
	        if command -v dotnet >/dev/null 2>&1; then \
	                cd sdk/csharp && \
	                export DOTNET_CLI_UI_LANGUAGE=en && \
	                dotnet format GopherMcp.sln --verbosity quiet --no-restore || true; \
	                echo "C# formatting complete."; \
	        else \
	                echo "Warning: dotnet CLI not found, skipping C# formatting."; \
	                echo "Install .NET SDK to format C# files: https://dotnet.microsoft.com/download"; \
	        fi; \
	else \
	        echo "C# SDK directory not found, skipping C# formatting."; \
	fi
	@echo "Formatting Go files with gofmt..."
	@if [ -d "sdk/go" ]; then \
	    cd sdk/go && \
	    if command -v gofmt >/dev/null 2>&1; then \
	        gofmt -s -w .; \
	        if command -v goimports >/dev/null 2>&1; then \
	            goimports -w .; \
	        fi; \
	        echo "Go formatting complete."; \
	    else \
	        echo "Warning: gofmt not found, skipping Go formatting."; \
	        echo "Install Go to format Go files: https://golang.org/dl/"; \
	    fi; \
	else \
	    echo "Go SDK directory not found, skipping Go formatting."; \
	fi
	@echo "Formatting Java files with Spotless..."
	@if [ -d "sdk/java" ]; then \
		if command -v mvn >/dev/null 2>&1; then \
			cd sdk/java && mvn spotless:apply; \
			echo "Java formatting complete."; \
		else \
			echo "Warning: Maven not found, skipping Java formatting."; \
			echo "Install Maven to format Java files: brew install maven (macOS) or apt-get install maven (Ubuntu)"; \
		fi; \
	else \
		echo "Java SDK directory not found, skipping Java formatting."; \
	fi
	@echo "All formatting complete."

# Format only TypeScript files
format-ts:
	@echo "Formatting TypeScript files with prettier..."
	@if [ -d "sdk/typescript" ]; then \
	        cd sdk/typescript && \
	        if [ ! -f "node_modules/.bin/prettier" ]; then \
	                echo "Installing prettier for TypeScript formatting..."; \
	                npm install --save-dev prettier @typescript-eslint/parser @typescript-eslint/eslint-plugin; \
	        fi; \
	        ./node_modules/.bin/prettier --write "src/**/*.ts" "examples/**/*.ts" "mcp-example/src/**/*.ts" "**/*.json" "**/*.md" --ignore-path .gitignore; \
	        echo "TypeScript formatting complete."; \
	else \
	        echo "TypeScript SDK directory not found."; \
	        exit 1; \
	fi

# Format only Python files
format-python:
	@echo "Formatting Python files with black..."
	@if [ -d "sdk/python" ]; then \
	        if command -v black >/dev/null 2>&1; then \
	                cd sdk/python && black . --line-length 100 --target-version py38; \
	                echo "Python formatting complete."; \
	        else \
	                echo "Installing black for Python formatting..."; \
	                pip install black; \
	                cd sdk/python && black . --line-length 100 --target-version py38; \
	                echo "Python formatting complete."; \
	        fi; \
	else \
	        echo "Python SDK directory not found, skipping Python formatting."; \
	fi

# Format only Rust files
format-rust:
	@echo "Formatting Rust files with rustfmt..."
	@if [ -d "sdk/rust" ]; then \
	        cd sdk/rust && \
	        if command -v rustfmt >/dev/null 2>&1; then \
	                rustfmt --edition 2021 src/**/*.rs; \
	                echo "Rust formatting complete."; \
	        else \
	                echo "Installing rustfmt for Rust formatting..."; \
	                rustup component add rustfmt; \
	                rustfmt --edition 2021 src/**/*.rs; \
	                echo "Rust formatting complete."; \
	        fi; \
	else \
	        echo "Rust SDK directory not found."; \
	fi

# Format only C# files
format-cs:
	@echo "Formatting C# files with dotnet format..."
	@if [ -d "sdk/csharp" ]; then \
	        if command -v dotnet >/dev/null 2>&1; then \
	                cd sdk/csharp && \
	                echo "Running dotnet format on all C# files..."; \
	                export DOTNET_CLI_UI_LANGUAGE=en && \
	                dotnet format GopherMcp.sln --no-restore 2>/dev/null || \
	                dotnet format whitespace GopherMcp.sln --no-restore 2>/dev/null || \
	                echo "Note: dotnet format completed (some warnings may be normal)."; \
	                echo "C# formatting complete."; \
	        else \
	                echo "Error: dotnet CLI not found. Please install .NET SDK to format C# files."; \
	                echo "Visit https://dotnet.microsoft.com/download to install .NET SDK."; \
	                exit 1; \
	        fi; \
	else \
	        echo "C# SDK directory not found at sdk/csharp"; \
	        exit 1; \
	fi

# Build C# SDK
csharp:
	@echo "Building C# SDK..."
	@if [ -f "sdk/csharp/build.sh" ]; then \
	        cd sdk/csharp && \
	        chmod +x build.sh && \
	        ./build.sh; \
	        echo "C# SDK build complete."; \
	else \
	        echo "C# SDK build script not found at sdk/csharp/build.sh"; \
	        exit 1; \
	fi

# Build C# SDK in release mode
csharp-release:
	@echo "Building C# SDK in release mode..."
	@if [ -f "sdk/csharp/build.sh" ]; then \
	        cd sdk/csharp && \
	        chmod +x build.sh && \
	        ./build.sh --release; \
	        echo "C# SDK release build complete."; \
	else \
	        echo "C# SDK build script not found at sdk/csharp/build.sh"; \
	        exit 1; \
	fi

# Run C# SDK tests
csharp-test:
	@echo "Running C# SDK tests..."
	@if [ -f "sdk/csharp/build.sh" ]; then \
	        cd sdk/csharp && \
	        chmod +x build.sh && \
	        ./build.sh --test; \
	        echo "C# SDK tests complete."; \
	else \
	        echo "C# SDK build script not found at sdk/csharp/build.sh"; \
	        exit 1; \
	fi

# Clean C# SDK build artifacts
csharp-clean:
	@echo "Cleaning C# SDK build artifacts..."
	@if [ -f "sdk/csharp/build.sh" ]; then \
	        cd sdk/csharp && \
	        chmod +x build.sh && \
	        ./build.sh --clean; \
	        echo "C# SDK clean complete."; \
	else \
	        echo "C# SDK build script not found at sdk/csharp/build.sh"; \
	        exit 1; \
	fi

# Format C# SDK source code
csharp-format:
	@echo "Formatting C# SDK source code..."
	@if [ -d "sdk/csharp" ]; then \
	        if command -v dotnet >/dev/null 2>&1; then \
	                cd sdk/csharp && \
	                echo "Running dotnet format on solution..."; \
	                dotnet format GopherMcp.sln --no-restore 2>/dev/null || \
	                dotnet format whitespace GopherMcp.sln --no-restore 2>/dev/null || \
	                echo "Note: dotnet format completed (some warnings may be normal)."; \
	                echo "C# SDK formatting complete."; \
	        else \
	                echo "Error: dotnet CLI not found. Please install .NET SDK to format C# files."; \
	                echo "Visit https://dotnet.microsoft.com/download to install .NET SDK."; \
	                exit 1; \
	        fi; \
	else \
	        echo "C# SDK directory not found at sdk/csharp"; \
	        exit 1; \
	fi

# Format only Go files
format-go:
	@echo "Formatting Go files with gofmt..."
	@if [ -d "sdk/go" ]; then \
		cd sdk/go && \
		if command -v gofmt >/dev/null 2>&1; then \
			gofmt -s -w .; \
			if command -v goimports >/dev/null 2>&1; then \
				goimports -w .; \
			fi; \
			echo "Go formatting complete."; \
		else \
			echo "Error: gofmt not found."; \
			echo "Install Go to format Go files: https://golang.org/dl/"; \
			exit 1; \
		fi; \
	else \
		echo "Go SDK directory not found."; \
		exit 1; \
	fi

# Format only Java files
format-java:
	@echo "Formatting Java files with Spotless..."
	@if [ -d "sdk/java" ]; then \
		if command -v mvn >/dev/null 2>&1; then \
			cd sdk/java && mvn spotless:apply; \
			echo "Java formatting complete."; \
		else \
			echo "Warning: Maven not found, skipping Java formatting."; \
			echo "Install Maven to format Java files: brew install maven (macOS) or apt-get install maven (Ubuntu)"; \
		fi; \
	else \
		echo "Java SDK directory not found."; \
		exit 1; \
	fi

# Check formatting without modifying files
check-format:
	@echo "Checking source file formatting..."
	@echo "Checking C++ file formatting..."
	@if command -v clang-format >/dev/null 2>&1; then \
	        find . -path "./build*" -prune -o \( -name "*.h" -o -name "*.cpp" -o -name "*.cc" \) -print | xargs clang-format --dry-run --Werror; \
	        echo "C++ formatting check complete."; \
	else \
	        echo "Warning: clang-format not found, skipping C++ formatting check."; \
	        echo "Install clang-format to check C++ formatting: brew install clang-format (macOS) or apt-get install clang-format (Ubuntu)"; \
	fi
	@echo "Checking TypeScript file formatting..."
	@if [ -d "sdk/typescript" ]; then \
	        cd sdk/typescript && \
	        if [ ! -f "node_modules/.bin/prettier" ]; then \
	                echo "Installing prettier for TypeScript formatting check..."; \
	                npm install --save-dev prettier @typescript-eslint/parser @typescript-eslint/eslint-plugin; \
	        fi; \
	        ./node_modules/.bin/prettier --check "src/**/*.ts" "examples/**/*.ts" "mcp-example/src/**/*.ts" "**/*.json" "**/*.md" --ignore-path .gitignore; \
	        echo "TypeScript formatting check complete."; \
	else \
	        echo "TypeScript SDK directory not found, skipping TypeScript formatting check."; \
	fi
	@echo "Checking Python file formatting..."
	@if [ -d "sdk/python" ]; then \
	        cd sdk/python && \
	        if command -v black >/dev/null 2>&1; then \
	                black . --check --line-length 100 --target-version py38; \
	                echo "Python formatting check complete."; \
	        else \
	                echo "Installing black for Python formatting check..."; \
	                pip install black; \
	                black . --check --line-length 100 --target-version py38; \
	                echo "Python formatting check complete."; \
	        fi; \
	else \
	        echo "Python SDK directory not found, skipping Python formatting check."; \
	fi
	@echo "Checking C# file formatting..."
	@if [ -d "sdk/csharp" ]; then \
	        if command -v dotnet >/dev/null 2>&1; then \
	                cd sdk/csharp && \
	                export DOTNET_CLI_UI_LANGUAGE=en && \
	                dotnet format GopherMcp.sln --verify-no-changes --no-restore 2>/dev/null || \
	                { echo "C# formatting issues detected. Run 'make format-cs' to fix."; exit 1; }; \
	                echo "C# formatting check complete."; \
	        else \
	                echo "Warning: dotnet CLI not found, skipping C# formatting check."; \
	                echo "Install .NET SDK to check C# formatting: https://dotnet.microsoft.com/download"; \
	        fi; \
	else \
	        echo "C# SDK directory not found, skipping C# formatting check."; \
	fi
	@echo "Checking Go file formatting..."
	@if [ -d "sdk/go" ]; then \
	    cd sdk/go && \
	    if command -v gofmt >/dev/null 2>&1; then \
	        if [ -n "$$(gofmt -s -l .)" ]; then \
	            echo "Go formatting check failed. Files need formatting:"; \
	            gofmt -s -l .; \
	            exit 1; \
	        else \
	            echo "Go formatting check complete."; \
	        fi; \
	    else \
	        echo "Warning: gofmt not found, skipping Go formatting check."; \
	    fi; \
	else \
	    echo "Go SDK directory not found, skipping Go formatting check."; \
	fi
	@echo "Formatting check complete."

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

# ═══════════════════════════════════════════════════════════════════════
# GO SDK TARGETS
# ═══════════════════════════════════════════════════════════════════════

# Build Go SDK
go-build:
	@echo "Building Go SDK..."
	@if [ -d "sdk/go" ]; then \
		cd sdk/go && \
		if command -v go >/dev/null 2>&1; then \
			make build; \
		else \
			echo "Error: Go not found. Install Go from https://golang.org/dl/"; \
			exit 1; \
		fi; \
	else \
		echo "Go SDK directory not found."; \
		exit 1; \
	fi

# Run Go SDK tests
go-test:
	@echo "Running Go SDK tests..."
	@if [ -d "sdk/go" ]; then \
		cd sdk/go && \
		if command -v go >/dev/null 2>&1; then \
			make test; \
		else \
			echo "Error: Go not found. Install Go from https://golang.org/dl/"; \
			exit 1; \
		fi; \
	else \
		echo "Go SDK directory not found."; \
		exit 1; \
	fi

# Format Go SDK code
go-format:
	@$(MAKE) format-go

# Clean Go SDK build artifacts
go-clean:
	@echo "Cleaning Go SDK build artifacts..."
	@if [ -d "sdk/go" ]; then \
		cd sdk/go && \
		if command -v go >/dev/null 2>&1; then \
			make clean; \
		else \
			echo "Error: Go not found. Install Go from https://golang.org/dl/"; \
			exit 1; \
		fi; \
	else \
		echo "Go SDK directory not found."; \
		exit 1; \
	fi

# Build and test Go SDK examples
go-examples:
	@echo "Building and testing Go SDK examples..."
	@if [ -d "sdk/go" ]; then \
		cd sdk/go && \
		if command -v go >/dev/null 2>&1; then \
			make examples; \
		else \
			echo "Error: Go not found. Install Go from https://golang.org/dl/"; \
			exit 1; \
		fi; \
	else \
		echo "Go SDK directory not found."; \
		exit 1; \
	fi

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
	@echo "┌─ C# SDK TARGETS ────────────────────────────────────────────────────┐"
	@echo "│ make csharp        Build C# SDK (debug mode)                         │"
	@echo "│ make csharp-release Build C# SDK in release mode                     │"
	@echo "│ make csharp-test   Run C# SDK tests                                  │"
	@echo "│ make csharp-clean  Clean C# SDK build artifacts                      │"
	@echo "│ make csharp-format Format all C# source code files                   │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ GO SDK TARGETS ────────────────────────────────────────────────────┐"
	@echo "│ make go-build      Build Go SDK libraries                            │"
	@echo "│ make go-test       Run Go SDK tests                                  │"
	@echo "│ make go-format     Format Go SDK code with gofmt                     │"
	@echo "│ make go-clean      Clean Go SDK build artifacts                      │"
	@echo "│ make go-examples   Build and test Go SDK examples                    │"
	@echo "└─────────────────────────────────────────────────────────────────────┘"
	@echo ""
	@echo "┌─ CODE QUALITY TARGETS ──────────────────────────────────────────────┐"
	@echo "│ make format        Auto-format all source files (C++, TS, Python, Rust, C#, Go, Java) │"
	@echo "│ make format-ts     Format only TypeScript files with prettier        │"
	@echo "│ make format-python Format only Python files with black               │"
	@echo "│ make format-rust   Format only Rust files with rustfmt               │"
	@echo "│ make format-cs     Format only C# files with dotnet format           │"
	@echo "│ make format-go     Format only Go files with gofmt and goimports     │"
	@echo "│ make format-java   Format only Java files with Spotless              │"
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
	@echo "│   $$ make format          # Format all code (C++, TS, Python, Rust, C#, Go, Java) │"
	@echo "│   $$ make format-ts       # Format only TypeScript files             │"
	@echo "│   $$ make format-python   # Format only Python files                 │"
	@echo "│   $$ make format-rust     # Format only Rust files                   │"
	@echo "│   $$ make format-cs       # Format only C# files                     │"
	@echo "│   $$ make format-go       # Format only Go files                     │"
	@echo "│   $$ make format-java     # Format only Java files                   │"
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
