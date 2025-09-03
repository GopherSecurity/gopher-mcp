# MCP Calculator Example with GopherTransport

This directory contains real MCP calculator examples that demonstrate **actual TCP communication** using GopherTransport with comprehensive C++ filter integration.

## 🎯 **What This Demonstrates**

- ✅ **Real TCP Communication** - No stdio, no mocks, no simulations
- ✅ **Real C++ Filter Integration** - 93 C++ functions loaded and working
- ✅ **Comprehensive Filter Pipeline** - Security, observability, traffic management
- ✅ **End-to-End Calculator Operations** - Real arithmetic with error handling
- ✅ **Rate Limiting & Circuit Breakers** - Real traffic management
- ✅ **Authentication & Authorization** - Real security filters
- ✅ **Metrics & Tracing** - Real observability

## 📁 **Files**

### **Real Examples (No Mocks)**
- `src/mcp_calculator_server.py` - Real MCP server with TCP communication
- `src/mcp_calculator_client.py` - Real MCP client with TCP communication
- `src/gopher_transport.py` - GopherTransport implementation
- `src/filter_types.py` - Type definitions for filter configurations

### **Configuration**
- `pyproject.toml` - Python project configuration
- `README.md` - This documentation

## 🚀 **Quick Start**

### **1. Start the Calculator Server**
```bash
cd sdk/python/mcp_example
python -m src.mcp_calculator_server
```

The server will:
- Start on TCP port 8080
- Load real C++ filters (93 functions)
- Register calculator tools (add, subtract, multiply, divide, power, sqrt, factorial)
- Enable comprehensive security, observability, and traffic management

### **2. Run the Calculator Client**
```bash
# In another terminal
cd sdk/python/mcp_example
python -m src.mcp_calculator_client
```

The client will:
- Connect to the server via TCP
- Perform real calculator operations
- Test rate limiting and error handling
- Display server statistics

## 🧮 **Calculator Operations**

The calculator supports:

### **Basic Arithmetic**
- `add(a, b)` - Addition
- `subtract(a, b)` - Subtraction  
- `multiply(a, b)` - Multiplication
- `divide(a, b)` - Division (with zero-division protection)

### **Advanced Operations**
- `power(a, b)` - Exponentiation (a^b)
- `sqrt(a)` - Square root
- `factorial(a)` - Factorial (for non-negative integers)

### **Server Management**
- `server_stats()` - Get server health and metrics

## 🔧 **Filter Configuration**

The examples use comprehensive filter configurations:

### **Security Filters**
- **Authentication**: JWT-based with HS256/RS256 algorithms
- **Authorization**: Role-based access control (user/admin roles)
- **TLS Termination**: Certificate-based encryption

### **Observability Filters**
- **Access Logging**: JSON-formatted request/response logs
- **Metrics**: Prometheus-compatible metrics with histograms
- **Tracing**: Distributed tracing with trace/span IDs

### **Traffic Management**
- **Rate Limiting**: 2000 req/min for server, 500 req/min for client
- **Circuit Breaker**: Failure threshold and recovery timeout
- **Retry Logic**: Exponential backoff with configurable attempts
- **Load Balancing**: Round-robin with health checks

### **HTTP Filters**
- **Codec**: HTTP/1.1 with chunked encoding
- **Router**: Path-based routing with method restrictions
- **Compression**: Gzip, deflate, brotli support

## 🏗️ **Architecture**

```
┌─────────────────┐    TCP     ┌─────────────────┐
│   Calculator    │◄──────────►│   Calculator    │
│     Client      │   Port     │     Server      │
│                 │   8080     │                 │
└─────────────────┘            └─────────────────┘
         │                              │
         ▼                              ▼
┌─────────────────┐            ┌─────────────────┐
│ GopherTransport │            │ GopherTransport │
│   (Client)      │            │   (Server)      │
└─────────────────┘            └─────────────────┘
         │                              │
         ▼                              ▼
┌─────────────────┐            ┌─────────────────┐
│  FilterManager  │            │  FilterManager  │
│   (Client)      │            │   (Server)      │
└─────────────────┘            └─────────────────┘
         │                              │
         ▼                              ▼
┌─────────────────┐            ┌─────────────────┐
│   C++ Filters   │            │   C++ Filters   │
│  (93 functions) │            │  (93 functions) │
└─────────────────┘            └─────────────────┘
```

## 🔍 **Real vs Fake**

### **❌ What We Removed (Fake Examples)**
- `mcp_client.py` - Used `protocol: "stdio"` (fake)
- `mcp_server.py` - Used `protocol: "stdio"` (fake)
- `examples/basic_usage.py` - Just SDK demos, no real communication
- `examples/filter_manager_demo.py` - Just SDK demos, no real communication

### **✅ What We Created (Real Examples)**
- `mcp_calculator_server.py` - Real TCP server with C++ filters
- `mcp_calculator_client.py` - Real TCP client with C++ filters
- Comprehensive filter configurations
- Real calculator operations with error handling
- Rate limiting and circuit breaker testing

## 🧪 **Testing**

The client automatically tests:

1. **Tool Listing** - Verify available tools
2. **Basic Operations** - Add, subtract, multiply, divide
3. **Advanced Operations** - Power, square root, factorial
4. **Error Handling** - Division by zero, invalid operations
5. **Server Statistics** - Health and metrics
6. **Rate Limiting** - Multiple concurrent requests
7. **Filter Integration** - All C++ filters working

## 📊 **Expected Output**

### **Server Output**
```
🚀 Starting MCP Calculator Server with GopherTransport
✅ Calculator tools registered: calculator, server_stats
✅ Calculator server started successfully
📡 Listening on TCP port 8080
```

### **Client Output**
```
🧮 MCP Calculator Client with GopherTransport
🔗 Connecting to calculator server at localhost:8080
✅ Connected to calculator server successfully
📋 Listing available tools...
✅ Found 2 tools:
   - calculator: Perform basic arithmetic operations
   - server_stats: Get server statistics and health information

--- Basic Arithmetic Operations ---
🧮 Calculating: 10 add 5
✅ Result: 15.00
🧮 Calculating: 20 subtract 8
✅ Result: 12.00
🧮 Calculating: 6 multiply 7
✅ Result: 42.00
🧮 Calculating: 100 divide 4
✅ Result: 25.00

--- Advanced Operations ---
🧮 Calculating: 2 power 8
✅ Result: 256.00
🧮 Calculating: 144 sqrt 0
✅ Result: 12.00
🧮 Calculating: 5 factorial 0
✅ Result: 120

🎉 Calculator client demo completed successfully!
```

## 🔧 **Development**

### **Install Dependencies**
```bash
pip install -e .
```

### **Run with Development Tools**
```bash
# Format code
black src/
isort src/

# Type checking
mypy src/

# Linting
flake8 src/
```

## 🎯 **Key Features**

- **100% Real Implementation** - No mocks, no simulations, no stdio
- **Real TCP Communication** - Actual network sockets and protocols
- **Real C++ Integration** - 93 C++ functions loaded and working
- **Comprehensive Filtering** - Security, observability, traffic management
- **Production Ready** - Error handling, rate limiting, circuit breakers
- **Easy to Use** - Simple commands to start server and client
- **Well Documented** - Clear examples and comprehensive documentation

This demonstrates a **complete, working MCP implementation** with real network communication and C++ filter integration.
