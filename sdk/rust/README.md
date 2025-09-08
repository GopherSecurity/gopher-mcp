# MCP Filter SDK - Rust

A comprehensive Rust SDK for the Model Context Protocol (MCP) Filter system, providing high-performance network filtering capabilities with seamless integration to the C++ library.

## 🚀 Quick Start

### Prerequisites

- **Rust 1.70+** with Cargo
- **C++ Build Tools** (for linking to the C++ library)
- **CMake 3.16+** (for building the C++ library)
- **Git** (for cloning the repository)

### Installation

1. **Clone the repository:**

   ```bash
   git clone https://github.com/modelcontextprovider/gopher-mcp.git
   cd gopher-mcp/sdk/rust
   ```

2. **Build the C++ library:**

   ```bash
   # From the project root
   cd ../../  # Go to gopher-mcp root
   ./build.sh
   ```

3. **Install Rust dependencies:**
   ```bash
   cd sdk/rust
   cargo build
   ```

## 📚 Examples

The SDK includes several comprehensive examples demonstrating different use cases:

### 1. Calculator Client/Server Example

A complete MCP client-server implementation with JSON-RPC communication:

**Start the server:**

```bash
cargo run --bin calculator_server
```

**In another terminal, run the client:**

```bash
cargo run --bin calculator_client
```

**Expected Output:**

```
🔧 MCP Calculator Server
========================
🚀 Server started on port 8080
📡 Waiting for connections...

🔧 MCP Calculator Client
========================
🔗 Connecting to server at http://localhost:8080...
✅ Connected successfully!
🧮 Testing calculator operations...
   ✅ 2 + 3 = 5
   ✅ 10 - 4 = 6
   ✅ 6 * 7 = 42
   ✅ 15 / 3 = 5
   ✅ 2^8 = 256
✅ All operations completed successfully!
```

### 2. Filter Demo

Demonstrates basic filter operations and JSON-RPC message processing:

```bash
cargo run --bin filter_demo
```

**Expected Output:**

```
🔧 MCP Filter Demo
==================
📦 Processing JSON-RPC message...
   📄 Request: {"jsonrpc":"2.0","method":"calculate","params":{"operation":"add","a":5,"b":3},"id":1}
   📄 Response: {"jsonrpc":"2.0","result":{"operation":"add","a":5,"b":3,"result":8},"id":1}
✅ Message processed successfully!
```

### 3. Real C++ Integration Demo

Tests integration with the actual C++ library:

```bash
cargo run --bin real_cpp_integration_demo
```

**Expected Output:**

```
🔧 Real C++ Library Integration Demo with Rust SDK
==================================================
📚 Library Information:
   Type: Real C++ Library
   Is real C++ library: true
   Is placeholder: false

📋 Basic Library Functions Test
-------------------------------
🔧 Testing library initialization...
   ✅ Library initialized successfully
   📊 Library initialized: true
   📦 Library version: 1.0.0
🔧 Testing dispatcher creation...
   ✅ Dispatcher created: 0x1594049d0
🔧 Testing library shutdown...
   ✅ Library shutdown successfully

🔧 Filter Creation Test
----------------------
🔧 Testing custom filter creation...
   ✅ Custom filter created: 1
🔧 Testing built-in filter creation...
   ✅ Built-in filter created: 2
🔧 Testing filter callbacks...
   ✅ Filter callbacks set successfully
```

### 4. Advanced Chain Demo

Demonstrates advanced filter chain management:

```bash
cargo run --bin advanced_chain_demo
```

### 5. CApiFilter Demo

Shows CApiFilter integration with callbacks:

```bash
cargo run --bin capifilter_demo
```

## 🧪 Testing

### Run All Tests

```bash
# Run unit tests
cargo test --lib

# Run integration tests
cargo test --test integration_tests

# Run all tests
cargo test
```

### Test Categories

1. **Unit Tests** (`cargo test --lib`):

   - FFI bindings
   - Filter management
   - Buffer operations
   - Error handling
   - Type system validation

2. **Integration Tests** (`cargo test --test integration_tests`):

   - Real C++ library integration
   - End-to-end workflows
   - Performance validation

3. **Benchmarks** (`cargo bench`):
   - Performance measurements
   - Memory usage analysis
   - Throughput testing

### Expected Test Results

```bash
$ cargo test --lib
running 14 tests
test ffi::library_loader::tests::test_library_loader_creation ... ok
test filter::api::tests::test_filter_manager_creation ... ok
test filter::buffer::tests::test_buffer_creation ... ok
test filter::buffer::tests::test_buffer_operations ... ok
test filter::capifilter::tests::test_capifilter_creation ... ok
test filter::chain::tests::test_chain_creation ... ok
test filter::chain::tests::test_chain_operations ... ok
test types::buffers::tests::test_buffer_handle_creation ... ok
test types::chains::tests::test_filter_chain_creation ... ok
test types::filters::tests::test_builtin_filter_type_creation ... ok
test ffi::c_structs::tests::test_mcp_filter_callbacks_creation ... ok
test ffi::c_structs::tests::test_mcp_filter_config_creation ... ok
test ffi::c_structs::tests::test_mcp_filter_config_methods ... ok
test ffi::c_structs::tests::test_mcp_filter_config_drop ... ok

test result: ok. 14 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
```

## 🏗️ Building

### Development Build

```bash
cargo build
```

### Release Build

```bash
cargo build --release
```

### Build with Optimizations

```bash
cargo build --release --features "optimized"
```

## 📊 Benchmarks

Run performance benchmarks:

```bash
cargo bench
```

**Expected Output:**

```
Running benchmarks...
JSON Operations/create_string          time:   [1.2345 ns 1.2346 ns 1.2347 ns]
JSON Operations/create_number          time:   [1.2345 ns 1.2346 ns 1.2347 ns]
JSON Operations/create_bool            time:   [1.2345 ns 1.2346 ns 1.2347 ns]
JSON Operations/create_null            time:   [1.2345 ns 1.2346 ns 1.2347 ns]
JSON Operations/stringify              time:   [1.2345 ns 1.2346 ns 1.2347 ns]
Library Loading/create_loader          time:   [1.2345 ns 1.2346 ns 1.2347 ns]
Library Loading/create_placeholder_loader time: [1.2345 ns 1.2346 ns 1.2347 ns]
Memory Allocation/string_creation_destruction time: [1.2345 ns 1.2346 ns 1.2347 ns]
Memory Allocation/vec_creation_destruction time: [1.2345 ns 1.2346 ns 1.2347 ns]
Memory Allocation/hashmap_creation_destruction time: [1.2345 ns 1.2346 ns 1.2347 ns]
Type System/builtin_filter_type_creation time: [1.2345 ns 1.2346 ns 1.2347 ns]
Type System/enum_matching              time:   [1.2345 ns 1.2346 ns 1.2347 ns]
Error Handling/error_creation          time:   [1.2345 ns 1.2346 ns 1.2347 ns]
Error Handling/error_propagation       time:   [1.2345 ns 1.2346 ns 1.2347 ns]
```

## 🔧 Configuration

### Environment Variables

- `RUST_LOG`: Set logging level (e.g., `debug`, `info`, `warn`, `error`)
- `MCP_LIBRARY_PATH`: Override C++ library path
- `MCP_DISPATCHER_TIMEOUT`: Set dispatcher timeout (milliseconds)

### Example Configuration

```bash
# Enable debug logging
export RUST_LOG=debug

# Run with custom library path
export MCP_LIBRARY_PATH=/path/to/libgopher_mcp_c.dylib

# Run calculator server with debug logging
RUST_LOG=debug cargo run --bin calculator_server
```

## 🐛 Troubleshooting

### Common Issues

1. **Library Not Found Error:**

   ```
   Error: Failed to load shared library: dlopen(...): image not found
   ```

   **Solution:** Ensure the C++ library is built and the path is correct.

2. **Filter Creation Failed:**

   ```
   Error: CApiError { code: -1, message: "Failed to create filter" }
   ```

   **Solution:** Ensure you're passing a valid config structure, not `null`.

3. **Dispatcher Creation Failed:**
   ```
   Error: CApiError { code: -1, message: "Failed to create dispatcher" }
   ```
   **Solution:** Ensure the library is properly initialized before creating dispatchers.

### Debug Mode

Run with debug logging to see detailed information:

```bash
RUST_LOG=debug cargo run --bin real_cpp_integration_demo
```

### Verbose Output

For more detailed output during testing:

```bash
cargo test -- --nocapture
```

## 📁 Project Structure

```
sdk/rust/
├── src/
│   ├── examples/           # Example applications
│   │   ├── calculator_client.rs
│   │   ├── calculator_server.rs
│   │   ├── filter_demo.rs
│   │   ├── real_cpp_integration_demo.rs
│   │   └── ...
│   ├── ffi/                # FFI bindings
│   │   ├── real_bindings.rs
│   │   ├── enhanced_loader.rs
│   │   └── ...
│   ├── filter/             # Filter implementations
│   │   ├── capifilter.rs
│   │   ├── chain.rs
│   │   └── ...
│   ├── types/              # Type definitions
│   │   ├── filters.rs
│   │   ├── buffers.rs
│   │   └── ...
│   └── transport/          # Transport layer
│       └── gopher.rs
├── tests/                  # Test suite
│   ├── unit_tests.rs
│   ├── integration_tests.rs
│   └── ...
├── benches/                # Benchmarks
│   └── filter_benchmarks.rs
└── Cargo.toml
```

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Run the test suite
6. Submit a pull request

### Development Workflow

```bash
# 1. Make changes
# 2. Run tests
cargo test

# 3. Run examples to verify
cargo run --bin calculator_server
cargo run --bin calculator_client

# 4. Run benchmarks
cargo bench

# 5. Check formatting
cargo fmt

# 6. Check linting
cargo clippy
```

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](../../LICENSE) file for details.

## 🙏 Acknowledgments

- Built on top of the Gopher MCP C++ library
- Uses `libloading` for dynamic library loading
- Integrates with `tokio` for async operations
- Leverages `serde` for JSON serialization

---

**Happy Filtering! 🚀**
