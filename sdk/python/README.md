# Python SDK for Gopher MCP

A comprehensive Python SDK for the Gopher Model Context Protocol (MCP) filter system, providing high-performance security, observability, and traffic management capabilities through FFI bindings to the C++ shared library.

## Features

- **Complete Filter Support**: All 15 filter types from the C++ implementation
- **High Performance**: Direct FFI bindings to optimized C++ code
- **Type Safety**: Full type annotations and validation
- **Easy Integration**: Simple API for MCP transport layers
- **Comprehensive Testing**: Extensive test suite with 100+ test cases
- **Production Ready**: Error handling, resource management, and monitoring

## Installation

### Prerequisites

- Python 3.8 or higher
- The C++ shared library (`libgopher_mcp_c.so` on Linux, `libgopher_mcp_c.dylib` on macOS, `libgopher_mcp_c.dll` on Windows)

### Install from Source

```bash
# Clone the repository
git clone <repository-url>
cd gopher-mcp/sdk/python

# Install dependencies
pip install -r requirements.txt

# Install the SDK
pip install -e .
```

### Dependencies

- `ctypes` (built-in) - For FFI bindings
- `typing` (built-in) - For type annotations
- `json` (built-in) - For JSON handling
- `asyncio` (built-in) - For async operations
- `unittest` (built-in) - For testing

## Quick Start

### Basic Usage

```python
from gopher_mcp import FilterManager, FilterManagerConfig, JSONRPCMessage

# Create filter configuration
config = FilterManagerConfig(
    name="my_filter_manager",
    enabled=True,
    security=SecurityFilterConfig(
        authentication=AuthenticationConfig(
            enabled=True,
            method="jwt",
            secret="your-secret-key"
        )
    ),
    observability=ObservabilityFilterConfig(
        access_log=AccessLogConfig(
            enabled=True,
            format="json"
        )
    )
)

# Create and use filter manager
with FilterManager(config) as manager:
    # Process a request
    request = JSONRPCMessage(
        jsonrpc="2.0",
        id=1,
        method="tools/list",
        params={}
    )

    processed_request = manager.process(request)
    print(f"Processed request: {processed_request}")
```

### MCP Transport Integration

```python
from gopher_mcp import GopherTransport, GopherTransportConfig
from mcp import McpClient, StdioClientTransport

# Create transport with filters
transport_config = GopherTransportConfig(
    name="secure_client",
    protocol="stdio",
    filters=FilterManagerConfig(
        name="client_filters",
        enabled=True,
        security=SecurityFilterConfig(
            authentication=AuthenticationConfig(
                enabled=True,
                method="jwt",
                secret="client-secret"
            )
        )
    )
)

transport = GopherTransport(transport_config)
await transport.start()

# Use with MCP client
client = McpClient(
    name="filtered_client",
    version="1.0.0"
)

await client.connect(transport)
```

## Architecture

### Core Components

1. **FFI Bindings** (`ffi_bindings.py`)

   - Direct interface to C++ shared library
   - Type-safe function bindings
   - Error handling and validation

2. **Filter API** (`filter_api.py`)

   - Core filter operations
   - Filter lifecycle management
   - Protocol metadata handling

3. **Filter Buffer** (`filter_buffer.py`)

   - Advanced buffer operations
   - Zero-copy data handling
   - Buffer pooling and management

4. **Filter Chain** (`filter_chain.py`)

   - Chain management and routing
   - Parallel and conditional execution
   - Chain pooling and statistics

5. **Filter Manager** (`filter_manager.py`)
   - High-level filter orchestration
   - JSON-RPC message processing
   - Error handling and fallback

### Filter Types

The SDK supports all 15 filter types from the C++ implementation:

#### Network Filters

- **TCP Proxy**: TCP connection proxying and load balancing
- **UDP Proxy**: UDP packet forwarding and routing

#### HTTP Filters

- **HTTP Codec**: HTTP/1.1 and HTTP/2 protocol handling
- **HTTP Router**: Request routing and load balancing
- **HTTP Compression**: Gzip, deflate, and brotli compression

#### Security Filters

- **TLS Termination**: SSL/TLS termination and certificate management
- **Authentication**: JWT, OAuth, and custom authentication
- **Authorization**: Role-based access control (RBAC)

#### Observability Filters

- **Access Log**: Request/response logging
- **Metrics**: Prometheus-compatible metrics collection
- **Tracing**: Distributed tracing with OpenTelemetry

#### Traffic Management Filters

- **Rate Limiting**: Request rate limiting and throttling
- **Circuit Breaker**: Fault tolerance and failure handling
- **Retry**: Automatic retry with exponential backoff
- **Load Balancer**: Multiple load balancing strategies

## Configuration

### Filter Manager Configuration

```python
from gopher_mcp import (
    FilterManagerConfig,
    NetworkFilterConfig,
    HttpFilterConfig,
    SecurityFilterConfig,
    ObservabilityFilterConfig,
    TrafficManagementFilterConfig
)

config = FilterManagerConfig(
    name="production_filters",
    enabled=True,

    # Network filters
    network=NetworkFilterConfig(
        tcp_proxy=TcpProxyConfig(
            enabled=True,
            upstream_host="backend.example.com",
            upstream_port=8080,
            timeout_ms=30000,
            keep_alive=True
        )
    ),

    # HTTP filters
    http=HttpFilterConfig(
        codec=HttpCodecConfig(
            enabled=True,
            version="1.1",
            max_header_size=8192,
            max_body_size=1048576,
            chunked_encoding=True
        ),
        router=HttpRouterConfig(
            enabled=True,
            routes=[
                {"path": "/api/v1", "target": "backend-v1", "methods": ["GET", "POST"]},
                {"path": "/api/v2", "target": "backend-v2", "methods": ["GET", "POST", "PUT", "DELETE"]}
            ],
            default_route="default-backend"
        )
    ),

    # Security filters
    security=SecurityFilterConfig(
        tls_termination=TlsTerminationConfig(
            enabled=True,
            cert_file="/path/to/cert.pem",
            key_file="/path/to/key.pem",
            protocols=["TLSv1.2", "TLSv1.3"]
        ),
        authentication=AuthenticationConfig(
            enabled=True,
            method="jwt",
            secret="your-secret-key",
            issuer="your-issuer",
            audience="your-audience"
        ),
        authorization=AuthorizationConfig(
            enabled=True,
            policy="allow",
            rules=[
                {"resource": "/api/*", "action": "read", "role": "user"},
                {"resource": "/admin/*", "action": "*", "role": "admin"}
            ]
        )
    ),

    # Observability filters
    observability=ObservabilityFilterConfig(
        access_log=AccessLogConfig(
            enabled=True,
            format="json",
            include_headers=True,
            include_body=False
        ),
        metrics=MetricsConfig(
            enabled=True,
            namespace="mcp_filters",
            labels={"environment": "production"}
        ),
        tracing=TracingConfig(
            enabled=True,
            service_name="mcp-filter-service",
            sampler_type="const",
            sampler_param=1.0
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
            failure_threshold=5,
            recovery_timeout=30000,
            half_open_max_calls=3
        ),
        retry=RetryConfig(
            enabled=True,
            max_attempts=3,
            initial_delay=1000,
            max_delay=10000,
            backoff_multiplier=2.0
        )
    ),

    # Error handling
    error_handling=ErrorHandlingConfig(
        stop_on_error=False,
        retry_attempts=3,
        fallback_behavior=FallbackBehavior.PASSTHROUGH
    )
)
```

## API Reference

### FilterManager

The main class for managing and orchestrating filters.

```python
class FilterManager:
    def __init__(self, config: FilterManagerConfig)
    def process(self, message: JSONRPCMessage) -> JSONRPCMessage
    def process_response(self, message: JSONRPCMessage) -> JSONRPCMessage
    def process_request_response(self, request: JSONRPCMessage, response: JSONRPCMessage) -> JSONRPCMessage
    def get_stats(self) -> Dict[str, Any]
    def destroy(self) -> None
    def __enter__(self) -> 'FilterManager'
    def __exit__(self, exc_type, exc_val, exc_tb) -> None
```

### GopherTransport

Custom transport layer that integrates with the filter system.

```python
class GopherTransport:
    def __init__(self, config: GopherTransportConfig)
    async def start(self) -> None
    async def close(self) -> None
    async def send(self, message: JSONRPCMessage) -> None
    async def receive(self) -> JSONRPCMessage
    def create_session(self, metadata: Dict[str, Any]) -> Session
    def remove_session(self, session_id: str) -> None
    def add_event_handler(self, handler: Callable) -> None
    def remove_event_handler(self, handler: Callable) -> None
```

## Examples

### Basic Filter Usage

See `examples/basic_usage.py` for a simple example of using the FilterManager.

### Comprehensive Demo

See `examples/filter_manager_demo.py` for a comprehensive demonstration of all filter types.

### MCP Integration

See `mcp_example/` directory for complete MCP client and server examples with filter integration.

## Testing

### Running Tests

```bash
# Run all tests
python tests/run_tests.py

# Run specific test suite
python tests/run_tests.py --suite "Filter Manager Tests"

# Generate detailed report
python tests/run_tests.py --report --output test_report.json
```

### Test Coverage

The test suite includes:

- **Filter API Tests**: 50+ test cases for core filter operations
- **Filter Buffer Tests**: 40+ test cases for buffer management
- **Filter Chain Tests**: 30+ test cases for chain operations
- **Filter Manager Tests**: 20+ test cases for high-level functionality

Total: 140+ test cases with comprehensive coverage of all functionality.

## Performance

### Benchmarks

The Python SDK provides excellent performance through:

- **Direct FFI bindings** to optimized C++ code
- **Zero-copy operations** for buffer handling
- **Efficient memory management** with buffer pooling
- **Minimal Python overhead** in the critical path

### Memory Usage

- **Buffer pooling** reduces memory allocations
- **Automatic cleanup** prevents memory leaks
- **Efficient data structures** minimize overhead

## Error Handling

### Error Types

The SDK provides comprehensive error handling:

- **Configuration errors**: Invalid filter configurations
- **Runtime errors**: Filter processing failures
- **Resource errors**: Memory and handle management
- **Network errors**: Connection and communication issues

### Fallback Behavior

Configurable fallback behavior for error scenarios:

- **REJECT**: Reject requests on filter errors
- **PASSTHROUGH**: Allow requests to pass through on errors
- **DEFAULT**: Use default responses on errors

## Contributing

### Development Setup

```bash
# Clone repository
git clone <repository-url>
cd gopher-mcp/sdk/python

# Install development dependencies
pip install -r requirements-dev.txt

# Run tests
python tests/run_tests.py

# Run linting
python -m flake8 src/
python -m mypy src/
```

### Code Style

- Follow PEP 8 style guidelines
- Use type annotations for all functions
- Write comprehensive docstrings
- Include unit tests for all new functionality

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Support

For questions, issues, or contributions:

1. Check the documentation and examples
2. Run the test suite to verify functionality
3. Create an issue with detailed information
4. Submit a pull request for improvements

## Changelog

### Version 1.0.0

- Initial release with complete filter support
- FFI bindings to C++ shared library
- Comprehensive test suite
- MCP transport integration
- Full documentation and examples
