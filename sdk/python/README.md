# Python MCP SDK with CApiFilter Integration

This Python SDK provides comprehensive integration with the MCP (Model Context Protocol) C++ library, including advanced CApiFilter functionality that allows Python callbacks to execute within the C++ filter chain.

## Features

- **CApiFilter Integration**: Execute Python callbacks in the C++ filter chain
- **Comprehensive Filter Support**: All 15 available C++ filter types
- **Zero-Copy Buffer Operations**: Efficient memory management
- **Real-time Message Processing**: Process JSON-RPC messages through filter pipelines
- **Cross-Platform Support**: Works on macOS, Linux, and Windows
- **Comprehensive Testing**: Full test coverage with mock and integration tests

## Installation

### Prerequisites

- Python 3.8 or higher
- MCP C++ library built and installed
- Platform-specific dependencies (see below)

### Platform-Specific Setup

#### macOS
```bash
# Install via Homebrew (if available)
brew install gopher-mcp

# Or build from source
cd /path/to/gopher-mcp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

#### Linux
```bash
# Install system dependencies
sudo apt-get update
sudo apt-get install build-essential cmake

# Build and install
cd /path/to/gopher-mcp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

#### Windows
```cmd
# Install Visual Studio Build Tools
# Download and install from: https://visualstudio.microsoft.com/downloads/

# Build using CMake
cd C:\path\to\gopher-mcp
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release
cmake --install . --config Release
```

### Python Package Installation

```bash
# Install the Python SDK
pip install -e .

# Or install in development mode
pip install -e .[dev]
```

## Quick Start

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

### Run All Tests
```bash
python -m pytest tests/
```

### Run Specific Test Suites
```bash
# CApiFilter tests
python -m pytest tests/test_capifilter.py -v

# Buffer operations tests
python -m pytest tests/test_buffer_operations.py -v

# End-to-end integration tests
python -m pytest tests/test_end_to_end.py -v
```

### Run Examples
```bash
# Calculator client
python mcp_example/src/mcp_calculator_client.py

# Calculator server
python mcp_example/src/mcp_calculator_server.py
```

## Troubleshooting

### Common Issues

#### Library Not Found
```
RuntimeError: Could not find MCP library for darwin/x86_64
```

**Solution**: Set the `MCP_LIBRARY_PATH` environment variable:
```bash
export MCP_LIBRARY_PATH="/path/to/your/libgopher_mcp_c.dylib"
```

#### Callback Registration Failed
```
ValueError: Invalid signature for on_data callback
```

**Solution**: Ensure your callback has the correct signature:
```python
def data_callback(buf, end_stream, user_data):  # 3 parameters
    return 0
```

#### Buffer Operations Failed
```
ValueError: Invalid buffer handle: 0
```

**Solution**: Ensure you're using a valid buffer handle from the C++ library.

### Debug Mode

Enable debug logging to see detailed CApiFilter execution:

```python
import logging
logging.basicConfig(level=logging.DEBUG)

# Your code here
```

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