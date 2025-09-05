# Changelog

All notable changes to the GopherMcp C# SDK will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Core P/Invoke bindings for MCP C API functions
- Type definitions for MCP protocol structures
- SafeHandle implementations for native resource management
- Filter infrastructure with base Filter abstract class
- FilterChain for composing multiple filters
- FilterBuffer for efficient memory operations
- FilterManager for orchestrating filter pipelines
- ChainBuilder fluent API for filter composition
- Built-in filters (15 types):
  - Network: TcpProxy, UdpProxy
  - HTTP: HttpCodec, HttpRouter, HttpCompression
  - Security: TlsTermination, Authentication, Authorization
  - Observability: AccessLog, Metrics, Tracing
  - Traffic Management: RateLimit, CircuitBreaker, Retry, LoadBalancer
- Transport layer abstraction with ITransport interface
- GopherTransport implementation with filter integration
- Protocol-specific transports (TCP, UDP, stdio)
- MCP client and server wrappers for simplified usage
- JSON-RPC message handling
- Memory management utilities
- Platform detection for cross-platform support
- Comprehensive test suite
- Example applications
- Full API documentation

### Changed
- N/A (Initial release)

### Deprecated
- N/A (Initial release)

### Removed
- N/A (Initial release)

### Fixed
- N/A (Initial release)

### Security
- N/A (Initial release)

## [1.0.0] - TBD

### Added
- Initial release of GopherMcp C# SDK
- Complete filter infrastructure for MCP protocol
- High-performance native interop with C++ backend
- Multi-targeting support for .NET 6.0, 7.0, 8.0, and .NET Standard 2.1
- Cross-platform support (Windows, Linux, macOS)
- Comprehensive filter chain composition system
- Zero-copy buffer operations for optimal performance
- Async/await patterns throughout the API
- Built-in filters for common scenarios
- Fluent API for filter chain configuration
- Transport layer abstraction for various protocols
- Integration with Model Context Protocol (MCP)
- Full documentation and examples
- Apache 2.0 license

### Known Issues
- Native library must be built separately from C++ source
- Performance optimizations for buffer operations pending
- Some advanced filter configurations not yet exposed

## Links

- [Repository](https://github.com/gopher-mcp/mcp-cpp-sdk)
- [Documentation](docs/README.md)
- [API Reference](docs/API.md)
- [Getting Started](docs/GettingStarted.md)
- [Filter Guide](docs/FilterGuide.md)
- [Examples](docs/Examples.md)