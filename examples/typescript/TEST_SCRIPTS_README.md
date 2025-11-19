# MCP Calculator Test Scripts

This directory contains comprehensive test scripts for the MCP Calculator Hybrid example that combines the official MCP SDK with Gopher-MCP C++ filters.

## 📁 Test Scripts Overview

### 1. `test-server.sh` - Server Test & Launch Script
Starts and tests the MCP Calculator Server with health checks and endpoint validation.

**Features:**
- Checks prerequisites (C++ library, TypeScript dependencies)
- Starts server with configurable host/port
- Tests health endpoint
- Tests MCP endpoints (tools/list, calculations, memory)
- Shows real-time logs
- Graceful cleanup on exit

**Usage:**
```bash
# Start with defaults (localhost:8080, stateless mode)
./test-server.sh

# Custom configuration
PORT=9090 HOST=0.0.0.0 MODE=stateful ./test-server.sh
```

### 2. `test-client.sh` - Client Test Script
Tests the MCP Calculator Client with automated and interactive modes.

**Features:**
- Verifies server availability
- Automated test mode (default)
- Interactive mode for manual testing
- Batch operations testing
- Performance benchmarking

**Usage:**
```bash
# Run automated tests (default)
./test-client.sh

# Interactive mode
INTERACTIVE=true ./test-client.sh

# Custom server URL
SERVER_URL=http://localhost:9090/mcp ./test-client.sh
```

### 3. `test-integration.sh` - Full Integration Test Suite
Comprehensive integration testing across all components.

**Features:**
- 5 test phases: Build, SDK, Server, Client, Performance
- Detailed test reporting
- Pass/fail tracking
- Performance metrics
- Concurrent request testing

**Usage:**
```bash
# Run full integration test
./test-integration.sh

# Verbose mode (keeps logs)
VERBOSE=true ./test-integration.sh

# Custom port
PORT=9090 ./test-integration.sh
```

### 4. `test-health.sh` - Health Monitoring Script
Continuous health monitoring and single-check modes.

**Features:**
- Real-time health monitoring
- Configurable check intervals
- Alert thresholds for failures
- Detailed status checks
- Statistics tracking
- Single-check mode

**Usage:**
```bash
# Continuous monitoring (default)
./test-health.sh

# Single health check
./test-health.sh --once

# Custom interval (10 seconds)
./test-health.sh --interval 10

# Limited checks
./test-health.sh --max 100

# Custom server
./test-health.sh --url http://localhost:9090
```

### 5. `test-all.sh` - Complete Test Suite Runner
Orchestrates all test suites with different modes.

**Features:**
- Three modes: quick, full, custom
- Parallel test execution option
- Comprehensive reporting
- Success rate calculation
- Test result aggregation

**Usage:**
```bash
# Quick tests (prerequisites + SDK)
TEST_MODE=quick ./test-all.sh

# Full test suite (default)
./test-all.sh

# Custom test selection
TEST_MODE=custom RUN_SDK=true RUN_INTEGRATION=true ./test-all.sh

# Verbose output
VERBOSE=true ./test-all.sh
```

## 🚀 Quick Start

### Prerequisites
1. Build the C++ library (optional, but recommended):
   ```bash
   cd ../..
   make build
   ```

2. Install TypeScript dependencies:
   ```bash
   cd sdk/typescript
   npm install
   ```

### Basic Testing Workflow

1. **Start the server:**
   ```bash
   ./test-server.sh
   ```

2. **In another terminal, run client tests:**
   ```bash
   ./test-client.sh
   ```

3. **Monitor server health:**
   ```bash
   ./test-health.sh
   ```

### Automated Testing

Run the complete test suite:
```bash
./test-all.sh
```

Run integration tests only:
```bash
./test-integration.sh
```

## 🔧 Environment Variables

### Server Configuration
- `PORT` - Server port (default: 8080)
- `HOST` - Server host (default: 127.0.0.1)
- `MODE` - Server mode: stateless or stateful (default: stateless)

### Test Configuration
- `VERBOSE` - Enable detailed logging (true/false)
- `TEST_MODE` - Test suite mode (quick/full/custom)
- `INTERACTIVE` - Client interactive mode (true/false)
- `SERVER_URL` - Override server URL for client tests

## 📊 Test Coverage

### Components Tested
- ✅ C++ Library loading and FFI
- ✅ TypeScript SDK functionality
- ✅ HTTP server endpoints
- ✅ MCP protocol (tools/list, tools/call)
- ✅ Calculator operations (add, multiply, sqrt, etc.)
- ✅ Memory operations (store, recall, clear)
- ✅ History tracking
- ✅ Filter chain (rate limiting, metrics, logging, circuit breaker)
- ✅ Performance (throughput, latency)
- ✅ Concurrent request handling

### Test Types
- Unit tests (SDK integration)
- Integration tests (server-client)
- End-to-end tests (full workflow)
- Performance tests (benchmarking)
- Health monitoring (continuous)

## 🛠️ Troubleshooting

### Common Issues

1. **Server fails to start**
   - Check if C++ library is built: `ls ../../build/src/c_api/`
   - Ensure port is not in use: `lsof -i :8080`
   - Check logs: `/tmp/mcp-server-*.log`

2. **Client connection failed**
   - Verify server is running: `curl http://localhost:8080/health`
   - Check server URL in client script
   - Ensure network connectivity

3. **Missing dependencies**
   - Install TypeScript dependencies: `cd ../../sdk/typescript && npm install`
   - Build C++ library: `cd ../.. && make build`

4. **Permission denied**
   - Make scripts executable: `chmod +x test*.sh`

## 📈 Performance Benchmarks

Expected performance on typical hardware:
- **Latency**: <50ms per request
- **Throughput**: >100 req/s
- **Concurrent requests**: 5+ simultaneous
- **Memory usage**: <100MB

## 🔍 Debugging

Enable verbose mode for detailed output:
```bash
VERBOSE=true ./test-integration.sh
```

Check server logs:
```bash
tail -f /tmp/mcp-server-*.log
```

Monitor in real-time:
```bash
./test-health.sh
```

## 📝 Notes

- The calculator server runs in **stateless mode** by default (JSON responses, SSE disabled)
- Use `MODE=stateful` to enable SSE streaming with session management
- All scripts include cleanup handlers to stop servers on exit
- Test reports are saved to `/tmp/mcp-test-report-*.txt`
- Logs are preserved in verbose mode for debugging

## 🤝 Contributing

When adding new test cases:
1. Update the relevant test script
2. Add to the integration test suite
3. Document in this README
4. Ensure cleanup on exit

## 📄 License

MIT