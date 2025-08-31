# MCP Filter SDK Examples

This directory contains examples that demonstrate the MCP Filter SDK functionality using the **REAL shared library**.

## 🚀 Examples Overview

### 1. `real-shared-library-demo.ts` - Comprehensive Demo
A full-featured demonstration that showcases all SDK capabilities:
- **Basic Filters**: HTTP and TCP Proxy filter creation and management
- **Advanced Buffers**: Buffer operations, pooling, and performance testing
- **Filter Chains**: Chain management, execution modes, and state control
- **RAII Management**: Resource guards, transactions, and cleanup
- **Real C API Integration**: Direct calls to the shared library
- **Performance Testing**: Stress testing with real operations

### 2. `integration-test.ts` - Focused Integration Testing
A focused test suite that verifies the real shared library integration:
- **MCP Core**: Initialization, version, status
- **Resource Management**: Dispatchers, memory pools
- **Filter Operations**: Creation, stats, cleanup
- **Buffer Operations**: Creation, properties, pools
- **JSON Operations**: All JSON creation functions
- **Resource Cleanup**: Proper cleanup of all resources

## 🏃‍♂️ How to Run

### Prerequisites
1. **Shared Library Installed**: `/usr/local/lib/libgopher_mcp_c.dylib`
2. **Project Built**: Run `npm run build` first
3. **Node.js**: Version 16+ recommended

### Running the Comprehensive Demo
```bash
# Build the project first
npm run build

# Run the comprehensive demo
node dist/examples/real-shared-library-demo.js
```

### Running the Integration Tests
```bash
# Build the project first
npm run build

# Run the integration tests
node dist/examples/integration-test.js
```

## 🔍 What These Examples Demonstrate

### ✅ **Real Shared Library Integration**
- **No Mocks**: These examples use the actual C library functions
- **Real FFI Calls**: Direct calls to `libgopher_mcp_c.dylib`
- **Resource Management**: Proper creation and cleanup of C resources
- **Error Handling**: Real error conditions and responses

### ✅ **Complete SDK Functionality**
- **All Filter Types**: HTTP, TCP Proxy, and custom filters
- **Buffer Operations**: Advanced buffer management and pooling
- **Chain Management**: Filter chain creation and execution
- **RAII Patterns**: Automatic resource cleanup
- **Performance Metrics**: Real performance measurements

### ✅ **Production-Ready Code**
- **Resource Cleanup**: Proper cleanup of all resources
- **Error Handling**: Comprehensive error handling and reporting
- **Performance Testing**: Stress testing with real workloads
- **Memory Management**: Proper memory allocation and deallocation

## 🧪 Testing Strategy

### **Unit Tests** (in `src/__tests__/`)
- **Use Mocks**: Fast, isolated testing
- **No Shared Library**: Tests run without external dependencies
- **Fast Execution**: Suitable for CI/CD and development

### **Integration Examples** (in `examples/`)
- **Use Real Library**: Actual shared library calls
- **Real Resources**: Real memory allocation and cleanup
- **End-to-End Testing**: Complete workflow testing
- **Performance Validation**: Real performance characteristics

## 🎯 Key Benefits

1. **Real Integration**: Verify that TypeScript wrappers work with actual C library
2. **Performance Validation**: Measure real performance characteristics
3. **Resource Management**: Test proper cleanup and memory management
4. **Error Handling**: Test real error conditions and edge cases
5. **Documentation**: Live examples of how to use the SDK

## 🔧 Troubleshooting

### **Shared Library Not Found**
```bash
# Check if library exists
ls -la /usr/local/lib/libgopher_mcp_c.dylib

# Check library dependencies
otool -L /usr/local/lib/libgopher_mcp_c.dylib
```

### **Build Errors**
```bash
# Clean and rebuild
npm run clean
npm run build
```

### **Runtime Errors**
- Check that all dependencies are installed
- Verify shared library permissions
- Check Node.js version compatibility

## 📊 Expected Output

### **Comprehensive Demo**
```
🚀 MCP Filter SDK - Real Shared Library Demo
This demo uses the ACTUAL shared library, not mocks!
Shared library path: /usr/local/lib/libgopher_mcp_c.dylib
Available functions: 85

============================================================
📋 1. Basic Filter Creation and Management
============================================================
🔍 Creating HTTP Filter...
✅ HTTP Filter created successfully
...
```

### **Integration Tests**
```
🚀 MCP Filter SDK - Integration Test Suite
Testing REAL shared library functionality
Shared library path: /usr/local/lib/libgopher_mcp_c.dylib
Available functions: 85

🧪 Running test: MCP Initialization
   Testing MCP initialization...
✅ MCP Initialization - PASSED
...
```

## 🎉 Success Criteria

- **All Examples Run**: No crashes or fatal errors
- **Resources Cleaned Up**: No memory leaks or resource leaks
- **Performance Acceptable**: Operations complete in reasonable time
- **Error Handling**: Graceful handling of edge cases
- **Integration Working**: TypeScript wrappers properly call C functions

These examples provide the **definitive proof** that our TypeScript SDK is fully integrated with the real shared library and ready for production use!
