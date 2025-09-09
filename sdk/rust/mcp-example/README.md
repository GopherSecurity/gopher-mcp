# MCP Example - Rust

This directory contains example implementations using the MCP (Model Context Protocol) SDK for Rust with the MCP Filter SDK integration.

## Overview

The MCP Example demonstrates how to:

- Create MCP servers and clients using the original MCP SDK
- Integrate with the MCP Filter SDK for advanced filtering capabilities
- Use GopherTransport for network communication
- Implement real MCP protocol communication

## Examples

### 1. Calculator Server (`mcp-calculator-server`)

A complete MCP server that provides calculator functionality with **CApiFilter integration**:

```bash
cargo run --bin mcp-calculator-server
```

**Features:**

- ✅ **CApiFilter Integration**: Real C++ filter callbacks for data processing
- ✅ **Real MCP Protocol**: Full JSON-RPC implementation
- ✅ **GopherTransport**: TCP server with connection management
- ✅ **Filter Management**: Integrated filter system with callbacks
- ✅ **Error Handling**: Comprehensive error handling and validation
- ✅ **C++ Library Loading**: Loads `libgopher_mcp_c.0.1.0.dylib`

**Supported Operations:**

- `add` - Addition
- `subtract` - Subtraction
- `multiply` - Multiplication
- `divide` - Division
- `power` - Exponentiation
- `sqrt` - Square root
- `factorial` - Factorial

**Filter Callbacks:**

- `on_data` - Process incoming data
- `on_write` - Handle outgoing data
- `on_new_connection` - Handle new client connections
- `on_error` - Error handling
- `on_high_watermark` / `on_low_watermark` - Buffer management

### 2. Calculator Client (`mcp-calculator-client`)

A complete MCP client that connects to the calculator server with **CApiFilter integration**:

```bash
cargo run --bin mcp-calculator-client
```

**Features:**

- ✅ **CApiFilter Integration**: Client-side filter processing
- ✅ **GopherTransport**: TCP client with connection management
- ✅ **Filter Management**: Client-side filter system
- ✅ **Example Calculations**: Demonstrates all calculator operations
- ✅ **Error Handling**: Proper error handling and graceful disconnection
- ✅ **C++ Library Loading**: Loads the same C++ library as server

**Filter Callbacks:**

- `on_data` - Process server responses
- `on_write` - Handle outgoing requests
- `on_new_connection` - Connection establishment
- `on_error` - Error handling
- `on_high_watermark` / `on_low_watermark` - Buffer management

### 3. Filter Demo (`filter-demo`)

Demonstrates the MCP Filter SDK capabilities:

```bash
cargo run --bin filter-demo
```

**Features:**

- ✅ **Filter Creation**: Create and manage custom filters
- ✅ **Filter Chains**: Advanced filter chain operations
- ✅ **Buffer Operations**: Zero-copy buffer management
- ✅ **Transport Integration**: GopherTransport with filters
- ✅ **C++ Library Integration**: Real C++ library loading
- ✅ **Comprehensive Demo**: Shows all SDK capabilities

## Dependencies

This example uses:

- **MCP SDK**: `mcp = "0.1.0"` - Original MCP SDK for Rust
- **MCP Filter SDK**: `mcp-filter-sdk = { path = "../" }` - Local filter SDK
- **Tokio**: Async runtime
- **Serde**: Serialization
- **Tracing**: Logging

## Running the Examples

1. **Start the Calculator Server:**

   ```bash
   cargo run --bin mcp-calculator-server
   ```

2. **Run the Calculator Client (in another terminal):**

   ```bash
   cargo run --bin mcp-calculator-client
   ```

3. **Run the Filter Demo:**
   ```bash
   cargo run --bin filter-demo
   ```

## Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   MCP Client    │◄──►│  GopherTransport │◄──►│   MCP Server    │
└─────────────────┘    └──────────────────┘    └─────────────────┘
         │                       │                       │
         │                       ▼                       │
         │              ┌──────────────────┐             │
         │              │  MCP Filter SDK  │             │
         │              │  (C++ Integration)│            │
         │              └──────────────────┘             │
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  MCP Protocol   │    │   Filter Chain   │    │  MCP Protocol   │
│   (JSON-RPC)    │    │   Management     │    │   (JSON-RPC)    │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

## Key Features

### MCP Protocol Integration

- ✅ **Full MCP Implementation**: Complete server/client with JSON-RPC
- ✅ **Tool Registration**: Calculator operations as MCP tools
- ✅ **Error Handling**: Comprehensive error types and propagation
- ✅ **Message Validation**: Proper JSON-RPC message validation

### CApiFilter Integration

- ✅ **Real C++ Library**: Loads `libgopher_mcp_c.0.1.0.dylib`
- ✅ **Filter Callbacks**: Complete callback system for data processing
- ✅ **Filter Management**: Integrated filter configuration and management
- ✅ **Buffer Operations**: Zero-copy buffer operations with C++ integration
- ✅ **Connection Handling**: Filter callbacks for connection events

### Transport Layer

- ✅ **GopherTransport**: Full TCP/UDP/Stdio support
- ✅ **Connection Management**: Multi-client connection handling
- ✅ **Message Routing**: Integrated with filter system
- ✅ **Error Recovery**: Robust error handling and recovery

### Testing & Validation

- ✅ **47 Unit Tests**: Comprehensive test coverage
- ✅ **Integration Tests**: End-to-end testing
- ✅ **Example Validation**: Working calculator client/server
- ✅ **Filter Verification**: CApiFilter integration verified

## Development

To add new examples:

1. Create a new binary in `src/`
2. Add it to `Cargo.toml`:
   ```toml
   [[bin]]
   name = "your-example"
   path = "src/your-example.rs"
   ```
3. Implement the example using MCP SDK and Filter SDK
4. Add documentation

## Testing

### Unit Tests

Run all unit tests in the SDK:

```bash
cd ../  # Go to the main SDK directory
cargo test
```

Run specific test categories:

```bash
# Test CApiFilter integration
cargo test capifilter

# Test filter API
cargo test filter_api

# Test buffer operations
cargo test buffer

# Test GopherTransport
cargo test gopher_transport
```

### Integration Tests

Run integration tests:

```bash
cd ../  # Go to the main SDK directory
cargo test --test integration
```

### Example Testing

Test the calculator examples:

1. **Start the server:**

   ```bash
   cargo run --bin mcp-calculator-server
   ```

2. **In another terminal, run the client:**

   ```bash
   cargo run --bin mcp-calculator-client
   ```

3. **Test the filter demo:**
   ```bash
   cargo run --bin filter-demo
   ```

### Test Coverage

The test suite includes:

- ✅ **47 Individual Component Tests**: Unit tests for each SDK component
- ✅ **CApiFilter Tests**: Filter creation, callbacks, and C++ integration
- ✅ **Filter API Tests**: Filter management and configuration
- ✅ **Buffer Tests**: Zero-copy buffer operations
- ✅ **Transport Tests**: GopherTransport functionality
- ✅ **Integration Tests**: End-to-end testing
- ✅ **Example Tests**: Calculator client/server examples

### Test Results

All tests should pass with the following expected output:

```
running 47 tests
test capifilter::tests::test_capifilter_creation ... ok
test capifilter::tests::test_capifilter_callbacks ... ok
test filter_api::tests::test_filter_manager_creation ... ok
test buffer::tests::test_buffer_operations ... ok
test transport::tests::test_gopher_transport ... ok
...
test result: ok. 47 passed; 0 failed; 0 ignored
```

### Debug Testing

Enable debug logging for detailed test output:

```bash
RUST_LOG=debug cargo test
```

### Performance Testing

Run performance benchmarks:

```bash
cargo test --release
```

## CApiFilter Integration Verification

### How to Verify CApiFilter is Working

1. **Check Library Loading:**
   When running examples, you should see:

   ```
   INFO mcp_filter_sdk::ffi::library_loader: Loading library from: ../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib
   ```

2. **Check Filter Processing:**
   Look for filter processing logs:

   ```
   INFO mcp_filter_sdk::transport::gopher: ✅ Message processed through filters: add (id: 1)
   ```

3. **Check Connection Handling:**
   Server should log new connections:

   ```
   INFO mcp_filter_sdk::transport::gopher: 🔗 New connection from 127.0.0.1:58067
   INFO mcp_filter_sdk::transport::gopher: ✅ Connection stored, total connections: 1
   ```

4. **Verify Filter Callbacks:**
   The filter callbacks are registered and ready to process data:
   - `on_data` - Processes incoming data
   - `on_write` - Handles outgoing data
   - `on_new_connection` - Manages new connections
   - `on_error` - Handles errors
   - `on_high_watermark` / `on_low_watermark` - Buffer management

### Expected Behavior

- ✅ **Server starts** and listens on port 8080
- ✅ **Client connects** successfully to server
- ✅ **Messages are processed** through the filter system
- ✅ **C++ library is loaded** and accessible
- ✅ **Filter callbacks are registered** and ready
- ✅ **Connection management** works properly

### Troubleshooting CApiFilter Issues

If CApiFilter integration is not working:

1. **Check C++ Library Path:**

   ```bash
   ls -la ../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib
   ```

2. **Verify Library Loading:**
   Look for the library loading message in logs

3. **Check Filter Configuration:**
   Ensure `filter_config` is not `None` in examples

4. **Debug Mode:**
   ```bash
   RUST_LOG=debug cargo run --bin mcp-calculator-client
   ```

## Troubleshooting

### Common Issues

1. **Connection Refused**: Make sure the server is running before starting the client
2. **Filter SDK Errors**: Ensure the C++ library is properly built and available
3. **Transport Errors**: Check network configuration and port availability

### Debug Mode

Enable debug logging:

```bash
RUST_LOG=debug cargo run --bin your-example
```

## Contributing

When contributing to the examples:

1. Follow the existing code style
2. Add proper error handling
3. Include documentation
4. Test thoroughly
5. Update this README if needed
