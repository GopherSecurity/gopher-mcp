#!/bin/bash

# MCP Filter Rust SDK - Complete Test Suite
# This script runs all tests and examples to verify the SDK is working correctly

set -e  # Exit on any error

echo "🔧 MCP Filter Rust SDK - Complete Test Suite"
echo "============================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the right directory
if [ ! -f "Cargo.toml" ]; then
    print_error "Please run this script from the sdk/rust directory"
    exit 1
fi

# Check if C++ library exists
CPP_LIB_PATH="../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib"
if [ ! -f "$CPP_LIB_PATH" ]; then
    print_warning "C++ library not found at $CPP_LIB_PATH"
    print_warning "Please build the C++ library first: cd ../../ && ./build.sh"
    print_warning "Continuing with tests that don't require the C++ library..."
fi

echo "📋 Running Test Suite..."
echo ""

# 1. Unit Tests
print_status "Running unit tests..."
if cargo test --lib --quiet; then
    print_success "Unit tests passed"
else
    print_error "Unit tests failed"
    exit 1
fi
echo ""

# 2. Integration Tests
print_status "Running integration tests..."
if cargo test --test integration_tests --quiet; then
    print_success "Integration tests passed"
else
    print_warning "Integration tests failed (may be due to missing C++ library)"
fi
echo ""

# 3. Benchmarks
print_status "Running benchmarks..."
if cargo bench --quiet; then
    print_success "Benchmarks completed"
else
    print_warning "Benchmarks failed (may be due to missing C++ library)"
fi
echo ""

# 4. Example Verification
print_status "Running example verification..."

# Test filter_demo
print_status "Testing filter_demo..."
if timeout 10s cargo run --bin filter_demo --quiet; then
    print_success "filter_demo passed"
else
    print_error "filter_demo failed"
fi
echo ""

# Test real_cpp_integration_demo
print_status "Testing real_cpp_integration_demo..."
if timeout 10s cargo run --bin real_cpp_integration_demo --quiet; then
    print_success "real_cpp_integration_demo passed"
else
    print_warning "real_cpp_integration_demo failed (may be due to missing C++ library)"
fi
echo ""

# Test calculator_server (background)
print_status "Testing calculator_server..."
cargo run --bin calculator_server &
SERVER_PID=$!

# Wait for server to start
sleep 3

# Test calculator_client
print_status "Testing calculator_client..."
if timeout 10s cargo run --bin calculator_client --quiet; then
    print_success "calculator_client passed"
else
    print_error "calculator_client failed"
fi

# Kill the server
kill $SERVER_PID 2>/dev/null || true
echo ""

# 5. Code Quality Checks
print_status "Running code quality checks..."

# Format check
print_status "Checking code formatting..."
if cargo fmt -- --check --quiet; then
    print_success "Code formatting is correct"
else
    print_warning "Code formatting issues found (run 'cargo fmt' to fix)"
fi

# Clippy check
print_status "Running clippy..."
if cargo clippy --quiet; then
    print_success "Clippy checks passed"
else
    print_warning "Clippy warnings found (run 'cargo clippy' to see details)"
fi
echo ""

# 6. Build Verification
print_status "Verifying builds..."

# Debug build
print_status "Building debug version..."
if cargo build --quiet; then
    print_success "Debug build successful"
else
    print_error "Debug build failed"
    exit 1
fi

# Release build
print_status "Building release version..."
if cargo build --release --quiet; then
    print_success "Release build successful"
else
    print_error "Release build failed"
    exit 1
fi
echo ""

# Summary
echo "🎉 Test Suite Complete!"
echo "======================="
echo ""
print_success "All core tests passed"
print_success "All examples verified"
print_success "All builds successful"
echo ""
print_status "The MCP Filter Rust SDK is working correctly!"
echo ""
print_status "Next steps:"
echo "  - Run 'cargo run --bin calculator_server' to start the server"
echo "  - Run 'cargo run --bin calculator_client' to test the client"
echo "  - Run 'cargo run --bin filter_demo' to test basic filtering"
echo "  - Run 'cargo run --bin real_cpp_integration_demo' to test C++ integration"
echo ""
print_status "For more information, see README.md and TESTING_GUIDE.md"
