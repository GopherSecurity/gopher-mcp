# Rust SDK Testing Guide

This guide provides comprehensive instructions for testing the MCP Filter Rust SDK, including unit tests, integration tests, benchmarks, and example verification.

## 🧪 Test Categories

### 1. Unit Tests

Test individual components in isolation.

### 2. Integration Tests

Test interactions between components and with the C++ library.

### 3. Benchmarks

Performance and memory usage testing.

### 4. Example Verification

End-to-end testing of example applications.

## 🚀 Quick Test Commands

```bash
# Run all tests
cargo test

# Run only unit tests
cargo test --lib

# Run only integration tests
cargo test --test integration_tests

# Run benchmarks
cargo bench

# Run examples
cargo run --bin calculator_server
cargo run --bin calculator_client
cargo run --bin filter_demo
cargo run --bin real_cpp_integration_demo
```

## 📋 Detailed Testing Instructions

### Unit Tests

**Purpose:** Test individual functions and structs in isolation.

**Run Command:**

```bash
cargo test --lib
```

**Expected Output:**

```
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

**What's Tested:**

- FFI library loading
- Filter manager creation
- Buffer operations
- CApiFilter creation
- Chain management
- Type system validation
- C struct operations

### Integration Tests

**Purpose:** Test interactions between components and with the real C++ library.

**Run Command:**

```bash
cargo test --test integration_tests
```

**Expected Output:**

```
running 5 tests
test test_real_cpp_library_loading ... ok
test test_filter_creation_with_real_library ... ok
test test_buffer_operations_with_real_library ... ok
test test_advanced_chain_management ... ok
test test_performance_validation ... ok

test result: ok. 5 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
```

**What's Tested:**

- Real C++ library loading
- Filter creation with actual C++ functions
- Buffer operations with C++ library
- Advanced chain management
- Performance validation

### Benchmarks

**Purpose:** Measure performance and memory usage.

**Run Command:**

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

**What's Measured:**

- JSON operation performance
- Library loading time
- Memory allocation patterns
- Type system operations
- Error handling overhead

## 🎯 Example Verification

### 1. Calculator Client/Server

**Purpose:** Test complete MCP client-server communication.

**Steps:**

1. **Start the server:**

   ```bash
   cargo run --bin calculator_server
   ```

2. **In another terminal, run the client:**
   ```bash
   cargo run --bin calculator_client
   ```

**Expected Server Output:**

```
🔧 MCP Calculator Server
========================
🚀 Server started on port 8080
📡 Waiting for connections...
📨 Received request: {"jsonrpc":"2.0","method":"calculate","params":{"operation":"add","a":2,"b":3},"id":1}
📤 Sending response: {"jsonrpc":"2.0","result":{"operation":"add","a":2,"b":3,"result":5},"id":1}
```

**Expected Client Output:**

```
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

**Purpose:** Test basic filter operations and JSON-RPC processing.

**Run Command:**

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

**Purpose:** Test integration with the actual C++ library.

**Run Command:**

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

**Purpose:** Test advanced filter chain management.

**Run Command:**

```bash
cargo run --bin advanced_chain_demo
```

**Expected Output:**

```
🔧 Advanced Filter Chain Demo
=============================
🔗 Creating filter chain...
   ✅ Chain created successfully
🔧 Testing sequential execution...
   ✅ Sequential execution completed
🔧 Testing parallel execution...
   ✅ Parallel execution completed
🔧 Testing conditional execution...
   ✅ Conditional execution completed
✅ All chain operations completed successfully!
```

### 5. CApiFilter Demo

**Purpose:** Test CApiFilter integration with callbacks.

**Run Command:**

```bash
cargo run --bin capifilter_demo
```

**Expected Output:**

```
🔧 CApiFilter Demo
==================
🔧 Creating CApiFilter...
   ✅ CApiFilter created successfully
🔧 Testing callbacks...
   ✅ Data callback triggered
   ✅ Write callback triggered
   ✅ Error callback triggered
✅ All CApiFilter operations completed successfully!
```

## 🔍 Debug Testing

### Enable Debug Logging

```bash
RUST_LOG=debug cargo run --bin real_cpp_integration_demo
```

### Verbose Test Output

```bash
cargo test -- --nocapture
```

### Test Specific Module

```bash
cargo test --lib filter::api
cargo test --lib ffi::library_loader
```

### Test with Specific Pattern

```bash
cargo test --lib test_filter_creation
cargo test --lib test_buffer
```

## 🐛 Troubleshooting Tests

### Common Test Failures

1. **Library Loading Failure:**

   ```
   Error: Failed to load shared library
   ```

   **Solution:** Ensure C++ library is built and path is correct.

2. **Filter Creation Failure:**

   ```
   Error: CApiError { code: -1, message: "Failed to create filter" }
   ```

   **Solution:** Check that valid config is being passed.

3. **Dispatcher Creation Failure:**
   ```
   Error: CApiError { code: -1, message: "Failed to create dispatcher" }
   ```
   **Solution:** Ensure library is initialized before creating dispatchers.

### Debug Commands

```bash
# Run with debug logging
RUST_LOG=debug cargo test

# Run specific test with output
cargo test --lib test_filter_creation -- --nocapture

# Run integration tests with debug
RUST_LOG=debug cargo test --test integration_tests

# Check library loading
cargo run --bin real_cpp_integration_demo
```

## 📊 Performance Testing

### Memory Usage

```bash
# Run with memory profiling
cargo test --release -- --nocapture

# Check for memory leaks
valgrind --tool=memcheck cargo test
```

### Throughput Testing

```bash
# Run benchmarks
cargo bench

# Run specific benchmark
cargo bench --bench filter_benchmarks
```

## ✅ Test Checklist

Before submitting code, ensure:

- [ ] All unit tests pass (`cargo test --lib`)
- [ ] All integration tests pass (`cargo test --test integration_tests`)
- [ ] All examples run successfully
- [ ] Benchmarks complete without errors
- [ ] No memory leaks detected
- [ ] Code is properly formatted (`cargo fmt`)
- [ ] No clippy warnings (`cargo clippy`)

## 🚀 Continuous Integration

The test suite is designed to run in CI/CD environments:

```bash
# Full test suite
cargo test --all-features

# Release build test
cargo test --release

# Check formatting
cargo fmt -- --check

# Check linting
cargo clippy -- -D warnings
```

---

**Happy Testing! 🧪**
