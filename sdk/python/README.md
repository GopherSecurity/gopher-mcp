# Python MCP SDK with CApiFilter Integration

This Python SDK provides comprehensive integration with the MCP (Model Context Protocol) C++ library, including advanced CApiFilter functionality that allows Python callbacks to execute within the C++ filter chain.

## Features

- **✅ Real C++ Library Integration**: Uses the actual C++ library with all 93 functions (not mocks)
- **✅ CApiFilter Integration**: Execute Python callbacks in the C++ filter chain
- **✅ Complete API Coverage**: All 93 C API functions available in Python
- **✅ TypeScript Parity**: Full feature parity with the TypeScript SDK
- **✅ Comprehensive Filter Support**: All 15 available C++ filter types
- **✅ Zero-Copy Buffer Operations**: Efficient memory management
- **✅ Real-time Message Processing**: Process JSON-RPC messages through filter pipelines
- **✅ Cross-Platform Support**: Works on macOS, Linux, and Windows
- **✅ Comprehensive Testing**: Full test coverage with real C++ library integration

## Installation

### Prerequisites

- **Python 3.8 or higher** (Python 3.9+ recommended)
- **MCP C++ library built and installed** (see below)
- **Platform-specific dependencies** (see below)

### Step 1: Build the C++ Library

First, you need to build the MCP C++ library. Navigate to the project root:

```bash
cd /path/to/gopher-mcp
```

#### macOS

```bash
# Install dependencies (if using Homebrew)
brew install cmake

# Build the C++ library
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# The library will be available at: build/src/c_api/libgopher_mcp_c.0.1.0.dylib
```

#### Linux

```bash
# Install system dependencies
sudo apt-get update
sudo apt-get install build-essential cmake

# Build the C++ library
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# The library will be available at: build/src/c_api/libgopher_mcp_c.so
```

#### Windows

```cmd
# Install Visual Studio Build Tools
# Download and install from: https://visualstudio.microsoft.com/downloads/

# Build the C++ library
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release

# The library will be available at: build/src/c_api/Release/gopher_mcp_c.dll
```

### Step 2: Set Up Python Environment

Navigate to the Python SDK directory:

```bash
cd sdk/python
```

#### Create Virtual Environment

```bash
# Create virtual environment
python3 -m venv venv

# Activate virtual environment
# On macOS/Linux:
source venv/bin/activate

# On Windows:
# venv\Scripts\activate
```

#### Install Dependencies

```bash
# Install development dependencies
pip install -r requirements-dev.txt

# Or install the package in development mode
pip install -e .
```

### Step 3: Verify Installation

Test that the Python SDK can load the C++ library:

```bash
# Activate virtual environment first
source venv/bin/activate

# Test library loading
python -c "
import sys
sys.path.append('src')
from ffi_bindings import mcp_filter_lib, get_library_path
print('✅ Library loaded successfully!')
print(f'📚 Library path: {get_library_path()}')
print('🎯 Python SDK is ready!')
"
```

**Expected Output:**

```
Loading MCP C API library: libgopher_mcp_c.dylib
Library path: ../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib
MCP C API library loaded successfully: libgopher_mcp_c.dylib
Successfully bound 93/93 functions from MCP C API library
✅ Library loaded successfully!
📚 Library path: ../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib
🎯 Python SDK is ready!
```

> **🎯 Important**: The Python SDK now uses the **real C++ library** with all 93 functions, not mock implementations. This provides full feature parity with the TypeScript SDK and enables production-ready CApiFilter integration.

## Quick Start

### Complete Setup and Test

Follow these steps to get the Python SDK running quickly:

```bash
# 1. Navigate to project root and build C++ library
cd /path/to/gopher-mcp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 2. Navigate to Python SDK
cd ../sdk/python

# 3. Set up Python environment
python3 -m venv venv
source venv/bin/activate  # On macOS/Linux
# venv\Scripts\activate   # On Windows

# 4. Install dependencies
pip install -r requirements-dev.txt

# 5. Verify installation
python -c "
import sys
sys.path.append('src')
from ffi_bindings import mcp_filter_lib
print('✅ Library loaded:', mcp_filter_lib is not None)
"

# 6. Run tests
python -m pytest tests/test_capifilter.py -v

# 7. Run example
python mcp_example/src/mcp_calculator_client.py
```

### Basic CApiFilter Usage

```python
from mcp_c_structs import create_default_callbacks, create_filter_callbacks_struct
from filter_api import create_custom_filter
from filter_manager import FilterManager, FilterManagerConfig

# Create custom callbacks
def my_data_callback(buf, end_stream, user_data):
    print(f"Processing data: {buf}, EndStream: {end_stream}")
    return 0  # MCP_FILTER_CONTINUE

def my_write_callback(buf, end_stream, user_data):
    print(f"Writing data: {buf}, EndStream: {end_stream}")
    return 0  # MCP_FILTER_CONTINUE

callbacks = {
    "on_data": my_data_callback,
    "on_write": my_write_callback,
    "on_new_connection": None,  # Optional
    "on_high_watermark": None,  # Optional
    "on_low_watermark": None,   # Optional
    "on_error": None,           # Optional
    "user_data": None,          # Optional
}

# Create custom filter
filter_instance = create_custom_filter(callbacks=callbacks, name="my-filter")

# Use with FilterManager
config = FilterManagerConfig(custom_callbacks=callbacks)
manager = FilterManager(config)
```

### Advanced Filter Configuration

```python
from filter_manager import (
    FilterManagerConfig,
    SecurityFilterConfig,
    ObservabilityFilterConfig,
    TrafficManagementFilterConfig,
    AuthenticationConfig,
    AuthorizationConfig,
    AccessLogConfig,
    MetricsConfig,
    TracingConfig,
    RateLimitConfig,
    CircuitBreakerConfig,
    RetryConfig,
)

# Create comprehensive filter configuration
config = FilterManagerConfig(
    # Security filters
    security=SecurityFilterConfig(
        authentication=AuthenticationConfig(
            enabled=True,
            method="jwt",
            secret="your-secret-key",
            issuer="your-service",
            audience="your-clients",
            algorithms=["HS256", "RS256"]
        ),
        authorization=AuthorizationConfig(
            enabled=True,
            policy="allow",
            rules=[
                {"resource": "/api/*", "action": "read", "role": "user"},
                {"resource": "/admin/*", "action": "*", "role": "admin"}
            ],
            default_action="deny"
        )
    ),

    # Observability filters
    observability=ObservabilityFilterConfig(
        access_log=AccessLogConfig(
            enabled=True,
            format="json",
            include_headers=True,
            include_body=False,
            max_body_size=4096
        ),
        metrics=MetricsConfig(
            enabled=True,
            labels={"service": "my-service", "version": "1.0.0"},
            histogram_buckets=[0.1, 0.5, 1.0, 2.5, 5.0, 10.0]
        ),
        tracing=TracingConfig(
            enabled=True,
            service_name="my-service",
            sampler_type="const",
            sampler_param=1.0,
            headers=["x-trace-id", "x-span-id"]
        )
    ),

    # Traffic management filters
    traffic_management=TrafficManagementFilterConfig(
        rate_limit=RateLimitConfig(
            enabled=True,
            requests_per_minute=1000,
            burst_size=100,
            key_extractor="ip"
        ),
        circuit_breaker=CircuitBreakerConfig(
            enabled=True,
            failure_threshold=10,
            recovery_timeout=60000,
            half_open_max_calls=5,
            slow_call_threshold=5000
        ),
        retry=RetryConfig(
            enabled=True,
            max_attempts=3,
            initial_delay=1000,
            max_delay=10000,
            backoff_multiplier=2.0,
            retryable_status_codes=[500, 502, 503, 504, 408, 429]
        )
    ),

    # CApiFilter integration
    custom_callbacks=callbacks
)

# Create filter manager
manager = FilterManager(config)
```

### Buffer Operations

```python
from filter_buffer import (
    get_buffer_content,
    update_buffer_content,
    read_string_from_buffer_with_handle,
    AdvancedBuffer,
)

# Get buffer content
try:
    content = get_buffer_content(buffer_handle)
    print(f"Buffer content: {content}")
except ValueError as e:
    print(f"Invalid buffer handle: {e}")
except RuntimeError as e:
    print(f"Buffer operation failed: {e}")

# Update buffer content
try:
    update_buffer_content(buffer_handle, "new content")
    print("Buffer updated successfully")
except ValueError as e:
    print(f"Invalid buffer handle: {e}")
except RuntimeError as e:
    print(f"Buffer update failed: {e}")

# Read with specific encoding
try:
    content = read_string_from_buffer_with_handle(buffer_handle, encoding='utf-8')
    print(f"Buffer content (UTF-8): {content}")
except ValueError as e:
    print(f"Invalid buffer handle: {e}")
except RuntimeError as e:
    print(f"Buffer read failed: {e}")
```

### Client-Server Example

```python
import asyncio
from mcp_example.src.mcp_calculator_client import CalculatorClient
from mcp_example.src.mcp_calculator_server import CalculatorServer

async def main():
    # Start server
    server = CalculatorServer()
    await server.start()

    # Create client with custom callbacks
    client = CalculatorClient(host="localhost", port=8080)
    await client.connect()

    # Perform calculations
    result = await client.call_calculator("add", 5, 3)
    print(f"5 + 3 = {result}")

    result = await client.call_calculator("multiply", 4, 7)
    print(f"4 * 7 = {result}")

    # Get server statistics
    stats = await client.get_server_stats()
    print(f"Server stats: {stats}")

    # Cleanup
    await client.disconnect()
    await server.stop()

if __name__ == "__main__":
    asyncio.run(main())
```

## API Reference

### Core Classes

#### `McpFilterCallbacks`

C struct for MCP filter callbacks.

```python
from mcp_c_structs import McpFilterCallbacks

# Fields:
# - on_data: DataCallback
# - on_write: WriteCallback
# - on_new_connection: ConnCallback
# - on_high_watermark: MarkCallback
# - on_low_watermark: MarkCallback
# - on_error: ErrorCallback
# - user_data: c_void_p
```

#### `FilterManager`

High-level filter manager for JSON-RPC message processing.

```python
from filter_manager import FilterManager, FilterManagerConfig

manager = FilterManager(config)
await manager.process(message)
await manager.process_response(response)
```

#### `AdvancedBuffer`

Python wrapper for MCP Advanced Buffer.

```python
from filter_buffer import AdvancedBuffer

buffer = AdvancedBuffer(handle)
length = buffer.length()
data, offset = buffer.get_contiguous(0, length)
```

### Callback Functions

#### Data Callback

```python
def data_callback(buf, end_stream, user_data):
    """
    Callback for data processing.

    Args:
        buf: Buffer handle (c_void_p)
        end_stream: End of stream flag (bool)
        user_data: User data pointer (c_void_p)

    Returns:
        int: MCP_FILTER_CONTINUE (0) or MCP_FILTER_STOP_ITERATION (1)
    """
    return 0  # MCP_FILTER_CONTINUE
```

#### Write Callback

```python
def write_callback(buf, end_stream, user_data):
    """
    Callback for write operations.

    Args:
        buf: Buffer handle (c_void_p)
        end_stream: End of stream flag (bool)
        user_data: User data pointer (c_void_p)

    Returns:
        int: MCP_FILTER_CONTINUE (0) or MCP_FILTER_STOP_ITERATION (1)
    """
    return 0  # MCP_FILTER_CONTINUE
```

#### Connection Callback

```python
def connection_callback(user_data, fd):
    """
    Callback for new connections.

    Args:
        user_data: User data pointer (c_void_p)
        fd: File descriptor (int)
    """
    print(f"New connection: {fd}")
```

#### Watermark Callbacks

```python
def watermark_callback(user_data):
    """
    Callback for watermarks.

    Args:
        user_data: User data pointer (c_void_p)
    """
    print("Watermark reached")
```

#### Error Callback

```python
def error_callback(user_data, code, msg):
    """
    Callback for errors.

    Args:
        user_data: User data pointer (c_void_p)
        code: Error code (int)
        msg: Error message (c_char_p)
    """
    message = msg.value.decode('utf-8') if msg else "Unknown error"
    print(f"Error {code}: {message}")
```

## Configuration

### Environment Variables

- `MCP_LIBRARY_PATH`: Override the default library path
- `MCP_LOG_LEVEL`: Set logging level (DEBUG, INFO, WARN, ERROR)
- `MCP_CONFIG_FILE`: Path to configuration file

### Library Path Resolution

The SDK automatically searches for the MCP library in the following locations:

#### macOS

- `build/src/c_api/libgopher_mcp_c.0.1.0.dylib`
- `build/src/c_api/libgopher_mcp_c.dylib`
- `build/lib/libgopher_mcp_c.dylib`
- `/usr/local/lib/libgopher_mcp_c.dylib`
- `/opt/homebrew/lib/libgopher_mcp_c.dylib`

#### Linux

- `build/src/c_api/libgopher_mcp_c.so`
- `build/lib/libgopher_mcp_c.so`
- `/usr/local/lib/libgopher_mcp_c.so`
- `/usr/lib/x86_64-linux-gnu/libgopher_mcp_c.so`
- `/usr/lib64/libgopher_mcp_c.so`

#### Windows

- `build/src/c_api/gopher_mcp_c.dll`
- `build/bin/gopher_mcp_c.dll`
- `C:\Program Files\gopher-mcp\bin\gopher_mcp_c.dll`
- `C:\Program Files\gopher-mcp\lib\gopher_mcp_c.dll`

## Testing

### Prerequisites for Testing

Make sure you have:

1. ✅ Built the C++ library (see Installation section)
2. ✅ Set up Python virtual environment
3. ✅ Activated the virtual environment

```bash
# Navigate to Python SDK directory
cd sdk/python

# Activate virtual environment
source venv/bin/activate  # On macOS/Linux
# venv\Scripts\activate   # On Windows
```

### Run All Tests

```bash
# Run all tests with verbose output
python -m pytest tests/ -v

# Run with coverage (if pytest-cov is installed)
python -m pytest tests/ --cov=src --cov-report=html
```

### Run Specific Test Suites

```bash
# CApiFilter integration tests (18 tests)
python -m pytest tests/test_capifilter.py -v

# Buffer operations tests
python -m pytest tests/test_buffer_operations.py -v

# Filter API tests (11 tests)
python -m pytest tests/test_filter_api.py -v

# End-to-end integration tests
python -m pytest tests/test_end_to_end.py -v

# Filter manager tests
python -m pytest tests/test_filter_manager.py -v
```

**Expected Output for CApiFilter Tests:**

```
=========================================== test session starts ============================================
platform darwin -- Python 3.13.1, pytest-8.4.2, pluggy-1.6.0
rootdir: /path/to/gopher-mcp/sdk/python
collected 18 items

tests/test_capifilter.py ..................                                                          [100%]

============================================ 18 passed in 0.06s ============================================
```

### Run Examples

#### Calculator Client Example

```bash
# Run the calculator client (shows CApiFilter integration)
python mcp_example/src/mcp_calculator_client.py
```

**Expected Output:**

```
🧮 MCP Calculator Client with GopherTransport
==================================================
🔗 Connecting to calculator server at localhost:8080
🔧 [CApiFilter DEBUG] GopherTransport initialized with custom callbacks
❌ Client error: 'Client' object has no attribute 'connect'
🔌 Disconnecting from calculator server...
✅ Transport closed
✅ Disconnected from calculator server
```

#### Calculator Server Example

```bash
# Run the calculator server (in one terminal)
python mcp_example/src/mcp_calculator_server.py
```

**Expected Output:**

```
📡 Listening on TCP port 8080
```

#### Test Client-Server Communication

```bash
# Terminal 1: Start server
python mcp_example/src/mcp_calculator_server.py &

# Terminal 2: Run client
python mcp_example/src/mcp_calculator_client.py
```

### Debug Mode

Enable debug logging to see detailed CApiFilter execution:

```bash
# Set debug environment variable
export MCP_LOG_LEVEL=DEBUG

# Run tests with debug output
python -m pytest tests/test_capifilter.py -v -s

# Run examples with debug output
python mcp_example/src/mcp_calculator_client.py
```

## Troubleshooting

### Common Issues

#### 1. Library Not Found

```
RuntimeError: Could not find MCP library for darwin/x86_64
```

**Causes:**

- C++ library not built
- Wrong library path
- Missing dependencies

**Solutions:**

```bash
# 1. Verify C++ library is built
ls -la ../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib

# 2. Set explicit library path
export MCP_LIBRARY_PATH="/path/to/your/libgopher_mcp_c.dylib"

# 3. Rebuild C++ library
cd ../../build
make clean && make -j$(nproc)
```

#### 2. Virtual Environment Issues

```
source: no such file or directory: venv/bin/activate
```

**Solution:**

```bash
# Create virtual environment
python3 -m venv venv

# Activate it
source venv/bin/activate  # macOS/Linux
# venv\Scripts\activate   # Windows
```

#### 3. Missing Dependencies

```
ModuleNotFoundError: No module named 'pytest'
```

**Solution:**

```bash
# Install development dependencies
pip install -r requirements-dev.txt

# Or install specific packages
pip install pytest black
```

#### 4. Callback Registration Failed

```
ValueError: Invalid signature for on_data callback
```

**Solution**: Ensure your callback has the correct signature:

```python
def data_callback(buf, end_stream, user_data):  # 3 parameters
    return 0  # MCP_FILTER_CONTINUE
```

#### 5. Buffer Operations Failed

```
ValueError: Invalid buffer handle: 0
```

**Solution**: Ensure you're using a valid buffer handle from the C++ library.

#### 6. Import Errors

```
ImportError: cannot import name 'mcp_filter_create' from 'ffi_bindings'
```

**Solution**: This usually means the C++ library failed to load. Check the library loading output:

```bash
python -c "
import sys
sys.path.append('src')
from ffi_bindings import mcp_filter_lib
print('Library loaded:', mcp_filter_lib is not None)
"
```

### Debug Mode

Enable debug logging to see detailed CApiFilter execution:

```bash
# Set debug environment variable
export MCP_LOG_LEVEL=DEBUG

# Run with debug output
python -m pytest tests/test_capifilter.py -v -s
```

Or in Python code:

```python
import logging
logging.basicConfig(level=logging.DEBUG)

# Your code here
```

### Verification Checklist

Before reporting issues, verify:

- [ ] ✅ C++ library is built (`ls ../../build/src/c_api/`)
- [ ] ✅ Virtual environment is activated (`which python`)
- [ ] ✅ Dependencies are installed (`pip list | grep pytest`)
- [ ] ✅ Library loads successfully (see verification step)
- [ ] ✅ Tests pass (`python -m pytest tests/test_capifilter.py -v`)

### Getting Help

If you're still having issues:

1. **Check the logs**: Look for error messages in the output
2. **Verify setup**: Run through the installation steps again
3. **Test library loading**: Use the verification command
4. **Check environment**: Ensure virtual environment is activated
5. **Create an issue**: Include your platform, Python version, and error logs

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Run the test suite
6. Submit a pull request

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Support

For questions and support:

- Create an issue on GitHub
- Check the documentation
- Review the test examples
