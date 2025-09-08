#!/bin/bash

# MCP Filter Rust SDK - Example Runner
# This script provides easy commands to run different examples

set -e  # Exit on any error

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

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_usage() {
    echo "🔧 MCP Filter Rust SDK - Example Runner"
    echo "======================================="
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  server      Start the calculator server"
    echo "  client      Run the calculator client"
    echo "  demo        Run the filter demo"
    echo "  integration Run the real C++ integration demo"
    echo "  chain       Run the advanced chain demo"
    echo "  capifilter  Run the CApiFilter demo"
    echo "  all         Run all examples in sequence"
    echo "  help        Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 server          # Start server in background"
    echo "  $0 client          # Run client (connects to server)"
    echo "  $0 demo            # Run basic filter demo"
    echo "  $0 integration     # Test C++ library integration"
    echo "  $0 all             # Run all examples"
    echo ""
}

# Check if we're in the right directory
if [ ! -f "Cargo.toml" ]; then
    print_error "Please run this script from the sdk/rust directory"
    exit 1
fi

# Function to run calculator server
run_server() {
    print_status "Starting calculator server..."
    print_status "Server will run on http://localhost:8080"
    print_status "Press Ctrl+C to stop the server"
    echo ""
    cargo run --bin calculator_server
}

# Function to run calculator client
run_client() {
    print_status "Running calculator client..."
    print_status "Make sure the server is running first!"
    echo ""
    cargo run --bin calculator_client
}

# Function to run filter demo
run_demo() {
    print_status "Running filter demo..."
    echo ""
    cargo run --bin filter_demo
}

# Function to run integration demo
run_integration() {
    print_status "Running real C++ integration demo..."
    echo ""
    cargo run --bin real_cpp_integration_demo
}

# Function to run chain demo
run_chain() {
    print_status "Running advanced chain demo..."
    echo ""
    cargo run --bin advanced_chain_demo
}

# Function to run capifilter demo
run_capifilter() {
    print_status "Running CApiFilter demo..."
    echo ""
    cargo run --bin capifilter_demo
}

# Function to run all examples
run_all() {
    print_status "Running all examples..."
    echo ""
    
    # 1. Filter demo
    print_status "1. Running filter demo..."
    cargo run --bin filter_demo
    echo ""
    
    # 2. Integration demo
    print_status "2. Running real C++ integration demo..."
    cargo run --bin real_cpp_integration_demo
    echo ""
    
    # 3. Chain demo
    print_status "3. Running advanced chain demo..."
    cargo run --bin advanced_chain_demo
    echo ""
    
    # 4. CApiFilter demo
    print_status "4. Running CApiFilter demo..."
    cargo run --bin capifilter_demo
    echo ""
    
    # 5. Calculator server/client
    print_status "5. Running calculator server/client..."
    print_status "Starting server in background..."
    cargo run --bin calculator_server &
    SERVER_PID=$!
    
    # Wait for server to start
    sleep 3
    
    print_status "Running client..."
    cargo run --bin calculator_client
    
    # Kill the server
    kill $SERVER_PID 2>/dev/null || true
    
    print_success "All examples completed!"
}

# Main script logic
case "${1:-help}" in
    server)
        run_server
        ;;
    client)
        run_client
        ;;
    demo)
        run_demo
        ;;
    integration)
        run_integration
        ;;
    chain)
        run_chain
        ;;
    capifilter)
        run_capifilter
        ;;
    all)
        run_all
        ;;
    help|--help|-h)
        print_usage
        ;;
    *)
        print_error "Unknown command: $1"
        echo ""
        print_usage
        exit 1
        ;;
esac
