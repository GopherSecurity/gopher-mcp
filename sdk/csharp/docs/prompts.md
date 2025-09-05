# MCP Filter C# SDK - Implementation Prompt Templates

This document provides prompt templates for implementing each task in the recommended order. Each prompt is designed to be specific and actionable.

## Phase 1: Core Foundation (Critical Path)

### Project Setup Tasks

#### 1. Create src/GopherMcp.csproj
```
Create the main project file src/GopherMcp.csproj for the MCP Filter C# SDK with:
- Multi-targeting for net6.0, net7.0, net8.0, and netstandard2.1
- Package metadata (ID: GopherMcp, Version: 1.0.0, Authors, Description, License: Apache-2.0)
- Dependencies: System.Text.Json 8.0.5, Microsoft.Extensions.Logging.Abstractions 8.0.2, System.Memory 4.5.5, System.Threading.Channels 8.0.0
- Runtime identifiers for win-x64, win-x86, linux-x64, linux-arm64, osx-x64, osx-arm64
- Native library packaging configuration for each platform
- Enable nullable reference types and unsafe blocks
- Generate documentation file
```

#### 2. Create NuGet.config
```
Create NuGet.config in the root directory with:
- Default NuGet.org package source
- Local package source for testing (./nupkg)
- Package source mappings for security
- Clear existing sources first
```

#### 3. Create global.json
```
Create global.json to pin the .NET SDK version with:
- SDK version 8.0.400 or later
- Roll-forward policy set to latestFeature
- Allow prerelease: false
```

#### 4. Create LICENSE file
```
Create LICENSE file with Apache 2.0 license text for the GopherMcp C# SDK.
Include copyright notice for the MCP Team and current year.
```

#### 5. Create CHANGELOG.md
```
Create CHANGELOG.md following Keep a Changelog format with:
- Initial version 1.0.0
- Sections for Added, Changed, Deprecated, Removed, Fixed, Security
- Initial release notes documenting core features
```

### Core P/Invoke Bindings

#### 6. Create Core/McpFilterApi.cs
```
Create src/Core/McpFilterApi.cs with P/Invoke bindings for filter lifecycle functions from mcp_c_filter_api.h:
- mcp_filter_create, mcp_filter_destroy
- mcp_filter_manager_create, mcp_filter_manager_destroy
- mcp_filter_manager_add_filter, mcp_filter_manager_remove_filter
- mcp_filter_manager_process
- mcp_filter_get_stats, mcp_filter_reset_stats
- Use DllImport with LibraryName constant "gopher_mcp_c"
- CallingConvention.Cdecl for all functions
- Proper marshaling for strings using MarshalAs(UnmanagedType.LPUTF8Str)
```

#### 7. Create Core/McpFilterChainApi.cs
```
Create src/Core/McpFilterChainApi.cs with P/Invoke bindings for chain composition from mcp_c_filter_chain.h:
- mcp_chain_builder_create_ex, mcp_chain_builder_destroy
- mcp_chain_builder_add_filter_node
- mcp_chain_builder_set_execution_mode
- mcp_chain_builder_build
- mcp_chain_process, mcp_chain_destroy
- Include enums for ChainExecutionMode (Sequential, Parallel, Conditional, Pipeline)
- Proper struct marshaling for FilterNode and ChainConfig
```

#### 8. Create Core/McpFilterBufferApi.cs
```
Create src/Core/McpFilterBufferApi.cs with P/Invoke bindings for buffer operations from mcp_c_filter_buffer.h:
- mcp_buffer_pool_create, mcp_buffer_pool_destroy
- mcp_buffer_allocate, mcp_buffer_release
- mcp_buffer_write, mcp_buffer_read
- mcp_buffer_get_size, mcp_buffer_get_data
- Zero-copy operation functions
- Scatter-gather I/O functions
- Include BufferOwnership enum
```

#### 9. Create Core/NativeLibrary.cs
```
Create src/Core/NativeLibrary.cs for cross-platform library loading:
- Static class NativeLibraryLoader
- Platform detection using RuntimeInformation
- Library name resolution (gopher_mcp_c.dll for Windows, libgopher_mcp_c.so for Linux, libgopher_mcp_c.dylib for macOS)
- Search paths for development and installed locations
- Lazy loading with thread-safe singleton pattern
- Fallback to P/Invoke default loading
```

#### 10. Create Core/SafeHandles.cs
```
Create src/Core/SafeHandles.cs with SafeHandle implementations:
- McpFilterHandle : SafeHandle with ReleaseHandle calling mcp_filter_destroy
- McpChainHandle : SafeHandle with ReleaseHandle calling mcp_chain_destroy  
- McpBufferHandle : SafeHandle with ReleaseHandle calling mcp_buffer_release
- McpManagerHandle : SafeHandle with ReleaseHandle calling mcp_filter_manager_destroy
- All inheriting from SafeHandle with ownsHandle=true
- Override IsInvalid property to check for IntPtr.Zero
```

### Type Definitions

#### 11. Create Types/FilterTypes.cs
```
Create src/Types/FilterTypes.cs with filter-specific type definitions:
- FilterStatus enum (Continue=0, StopIteration=1)
- FilterPosition enum (First, Last, Before, After)
- FilterError enum with error codes
- FilterConfig class with properties: Name, Type, Settings, Layer, MemoryPool
- FilterStatistics struct with metrics
- FilterEventArgs and FilterDataEventArgs for events
- FilterResult class with status and data
```

#### 12. Create Types/ChainTypes.cs
```
Create src/Types/ChainTypes.cs with chain composition types:
- ChainExecutionMode enum (Sequential, Parallel, Conditional, Pipeline)
- RoutingStrategy enum
- FilterNode class with filter reference and metadata
- ChainConfig class with execution settings
- ChainStatistics struct
- ProcessingContext class with direction and metadata
```

#### 13. Create Types/BufferTypes.cs
```
Create src/Types/BufferTypes.cs with buffer operation types:
- BufferOwnership enum (Owned, Borrowed, Shared)
- BufferSlice struct with data, length, flags
- BufferPool configuration class
- ScatterGatherEntry struct
- BufferStatistics struct
- Memory layout attributes
```

#### 14. Create Types/Exceptions.cs
```
Create src/Types/Exceptions.cs with custom exception hierarchy:
- Base McpException : Exception with ErrorCode property
- FilterException : McpException for filter-specific errors
- ChainException : McpException for chain processing errors
- TransportException : McpException for transport layer errors
- ConfigurationException : McpException for configuration errors
- Each with appropriate constructors and inner exception support
```

### Utility Components

#### 15. Create Utils/MemoryManager.cs
```
Create src/Utils/MemoryManager.cs for memory management:
- Memory pool management for buffers
- ArrayPool<byte> wrapper for managed memory
- Pinned memory allocation helpers
- GC pressure monitoring
- Memory usage statistics
- Dispose pattern for cleanup
```

#### 16. Create Utils/CallbackManager.cs
```
Create src/Utils/CallbackManager.cs for callback lifecycle:
- Store native callbacks to prevent GC
- Thread-safe callback registration/unregistration
- Weak reference support for managed callbacks
- Callback context preservation
- Exception handling in callback invocation
```

#### 17. Create Utils/PlatformDetection.cs
```
Create src/Utils/PlatformDetection.cs for platform detection:
- Detect OS (Windows, Linux, macOS)
- Detect architecture (x64, x86, ARM64)
- Runtime identifier generation
- Native library path resolution
- Platform-specific constants
- Environment variable checks
```

#### 18. Create Utils/JsonSerializer.cs
```
Create src/Utils/JsonSerializer.cs for JSON operations:
- Wrapper around System.Text.Json
- Configured JsonSerializerOptions for MCP
- Serialize/Deserialize generic methods
- Stream-based operations
- Custom converters for MCP types
- Performance-optimized settings
```

## Phase 2: Filter Infrastructure

### Base Filter Implementation

#### 19. Create Filters/Filter.cs base class
```
Create src/Filters/Filter.cs as abstract base class:
- Protected McpFilterHandle field
- FilterConfig property
- Abstract ProcessAsync method returning Task<FilterResult>
- IDisposable implementation with Dispose pattern
- Events: OnInitialize, OnDestroy, OnData, OnError
- GetStatistics() method
- UpdateConfig() method
- ThrowIfDisposed() helper
- Abstract methods for derived classes to implement
```

#### 20. Implement Filter.ProcessAsync
```
In src/Filters/Filter.cs, implement ProcessAsync method:
- Validate input parameters
- Check disposed state
- Call ProcessInternal on thread pool
- Handle exceptions and convert to FilterResult
- Update statistics
- Raise OnData event
- Support cancellation token
```

#### 21. Implement Filter.Dispose pattern
```
In src/Filters/Filter.cs, implement proper Dispose pattern:
- Dispose(bool disposing) protected virtual method
- Dispose managed resources if disposing=true
- Call native handle disposal
- Set disposed flag
- Suppress finalizer
- Raise OnDestroy event before disposal
```

#### 22. Implement Filter lifecycle events
```
In src/Filters/Filter.cs, implement event system:
- Define event delegates with proper signatures
- Thread-safe event invocation helpers
- Async event support with Task return
- Exception aggregation for multiple handlers
- Event subscription management
```

### Filter Chain Implementation

#### 23. Create Filters/FilterChain.cs
```
Create src/Filters/FilterChain.cs class:
- List<Filter> for filter storage
- ChainConfig for configuration
- McpChainHandle for native handle
- ProcessAsync method for chain execution
- Statistics aggregation from filters
- IDisposable implementation
- Thread-safe filter addition/removal
```

#### 24. Implement FilterChain.AddFilter
```
In FilterChain.cs, implement AddFilter method:
- Validate filter not null
- Check for duplicate filters
- Add to internal list
- Update native chain via P/Invoke
- Configure filter position (Before/After/First/Last)
- Raise chain modified event
```

#### 25. Implement FilterChain.ProcessAsync
```
In FilterChain.cs, implement ProcessAsync:
- Execute filters based on ChainExecutionMode
- Sequential: process in order
- Parallel: use Task.WhenAll
- Conditional: evaluate conditions
- Pipeline: use channels for buffering
- Aggregate results
- Handle filter exceptions
```

### Filter Buffer Implementation

#### 26. Create Filters/FilterBuffer.cs
```
Create src/Filters/FilterBuffer.cs class:
- Wrap native buffer handle
- Provide read/write operations
- Zero-copy slice creation
- Memory span access
- Size and capacity properties
- Ownership tracking
- IDisposable implementation
```

#### 27. Implement FilterBuffer zero-copy operations
```
In FilterBuffer.cs, implement zero-copy operations:
- GetSpan() for direct memory access
- Slice() for creating views
- CopyTo/CopyFrom with Memory<byte>
- Scatter-gather support
- Reference counting for safety
- Pinned memory handling
```

### Filter Configuration

#### 28. Create Filters/FilterConfig.cs
```
Create src/Filters/FilterConfig.cs base configuration:
- Abstract base class for all filter configs
- Common properties: Name, Enabled, Priority
- Validation methods
- JSON serialization support
- Configuration merge capabilities
- Default values
```

## Phase 3: Manager Layer

### FilterManager Implementation

#### 29. Create Manager/FilterManager.cs
```
Create src/Manager/FilterManager.cs class:
- McpManagerHandle for native handle
- FilterManagerConfig for configuration
- Dictionary<Guid, Filter> for filter registry
- List<FilterChain> for chain management
- ProcessAsync method for message processing
- Statistics aggregation
- IDisposable implementation
```

#### 30. Implement FilterManager constructor
```
In FilterManager.cs, implement constructor:
- Accept optional FilterManagerConfig
- Create native manager handle via P/Invoke
- Initialize filter registry
- Initialize chain list
- Call InitializeDefaultFilters
- Set up logging if configured
```

#### 31. Implement FilterManager.ProcessAsync
```
In FilterManager.cs, implement ProcessAsync:
- Serialize JsonRpcMessage to buffer
- Select appropriate filter chain
- Process through chain
- Deserialize result
- Update statistics
- Handle errors with fallback
- Support cancellation
```

#### 32. Implement FilterManager.CreateChain
```
In FilterManager.cs, implement CreateChain:
- Validate chain name unique
- Create new FilterChain instance
- Configure with provided ChainConfig
- Add to internal chain list
- Register with native manager
- Return chain for further configuration
```

#### 33. Implement FilterManager.RegisterFilter
```
In FilterManager.cs, implement RegisterFilter:
- Generate unique ID for filter
- Add to filter registry
- Set filter's manager reference
- Configure filter with defaults
- Initialize filter
- Raise filter registered event
```

#### 34. Implement FilterManager.BuildChain
```
In FilterManager.cs, implement BuildChain:
- Return new ChainBuilder instance
- Pass manager reference
- Set chain name
- Enable fluent configuration
```

#### 35. Implement FilterManager.GetStatistics
```
In FilterManager.cs, implement GetStatistics:
- Aggregate statistics from all filters
- Calculate total processed messages
- Compute average latency
- Get memory usage
- Return ManagerStatistics object
```

#### 36. Implement FilterManager.UpdateConfiguration
```
In FilterManager.cs, implement UpdateConfiguration:
- Validate new configuration
- Apply changes to existing filters
- Add/remove filters as needed
- Update chain configurations
- Trigger reconfiguration events
```

#### 37. Implement FilterManager.InitializeDefaultFilters
```
In FilterManager.cs, implement InitializeDefaultFilters:
- Check config for enabled filters
- Create Authentication filter if enabled
- Create Metrics filter if enabled
- Create RateLimit filter if enabled
- Register each with manager
```

#### 38. Implement FilterManager.Dispose
```
In FilterManager.cs, implement Dispose pattern:
- Dispose all registered filters
- Dispose all chains
- Release native manager handle
- Clear registries
- Suppress finalizer
```

### Configuration Classes

#### 39. Create Manager/FilterManagerConfig.cs
```
Create src/Manager/FilterManagerConfig.cs:
- Root configuration class
- Properties for each config section
- Static Default property with sensible defaults
- Validation method
- JSON serialization attributes
```

#### 40. Create SecurityConfig nested class
```
In FilterManagerConfig.cs, create SecurityConfig:
- AuthenticationConfig property
- AuthorizationConfig property
- TlsConfig property
- Default security settings
```

#### 41. Create ObservabilityConfig nested class
```
In FilterManagerConfig.cs, create ObservabilityConfig:
- AccessLogConfig property
- MetricsConfig property
- TracingConfig property
- Default observability settings
```

#### 42. Create TrafficManagementConfig nested class
```
In FilterManagerConfig.cs, create TrafficManagementConfig:
- RateLimitConfig property
- CircuitBreakerConfig property
- RetryConfig property
- LoadBalancerConfig property
```

#### 43. Create HttpConfig nested class
```
In FilterManagerConfig.cs, create HttpConfig:
- CodecConfig property
- RouterConfig property
- CompressionConfig property
- Default HTTP settings
```

#### 44. Create NetworkConfig nested class
```
In FilterManagerConfig.cs, create NetworkConfig:
- TcpProxyConfig property
- UdpProxyConfig property
- Connection pool settings
```

#### 45. Create ErrorHandlingConfig nested class
```
In FilterManagerConfig.cs, create ErrorHandlingConfig:
- StopOnError property
- RetryAttempts property
- FallbackBehavior enum property
- Timeout settings
```

### Chain Builder Implementation

#### 46. Create Manager/ChainBuilder.cs
```
Create src/Manager/ChainBuilder.cs class:
- FilterManager reference
- Chain name
- List<FilterDescriptor> for filters
- ChainExecutionMode property
- Fluent interface methods
- Build() method to create chain
```

#### 47. Implement ChainBuilder.AddFilter<T>
```
In ChainBuilder.cs, implement generic AddFilter:
- Create new instance of filter type T
- Apply configuration via Action<T>
- Add to filter descriptor list
- Return this for chaining
```

#### 48. Implement ChainBuilder.AddTcpProxy
```
In ChainBuilder.cs, implement AddTcpProxy:
- Accept Action<TcpProxyConfig>
- Create and configure TcpProxyConfig
- Create TcpProxyFilter with config
- Add to filter list
- Return this for chaining
```

#### 49. Implement ChainBuilder.AddAuthentication
```
In ChainBuilder.cs, implement AddAuthentication:
- Accept AuthenticationMethod and secret
- Create AuthenticationConfig
- Create AuthenticationFilter
- Add to filter list
- Return this for chaining
```

#### 50. Implement ChainBuilder.AddRateLimit
```
In ChainBuilder.cs, implement AddRateLimit:
- Accept requests per minute and burst size
- Create RateLimitConfig
- Create RateLimitFilter
- Add to filter list
- Return this for chaining
```

#### 51. Implement ChainBuilder execution mode methods
```
In ChainBuilder.cs, implement:
- Sequential(): Set mode to Sequential
- Parallel(maxConcurrency): Set mode to Parallel with concurrency
- Conditional(predicate): Set mode to Conditional with condition
- Each returning this for chaining
```

#### 52. Implement ChainBuilder.Build
```
In ChainBuilder.cs, implement Build:
- Call manager.CreateChain with name
- Configure chain with execution mode
- Add each filter from descriptor list
- Configure filter order
- Return completed FilterChain
```

#### 53. Create Manager/MessageProcessor.cs
```
Create src/Manager/MessageProcessor.cs:
- Process JSON-RPC messages
- Message validation
- Routing to appropriate chain
- Response generation
- Error message formatting
- Batch message support
```

## Phase 4: Built-in Filters

### Network Filters

#### 54. Create Filters/BuiltinFilters/TcpProxyFilter.cs
```
Create TcpProxyFilter inheriting from Filter:
- TcpProxyConfig with upstream host/port
- TCP connection management
- Data forwarding logic
- Connection pooling
- Health checking
- Override ProcessAsync for TCP proxying
```

#### 55. Create Filters/BuiltinFilters/UdpProxyFilter.cs
```
Create UdpProxyFilter inheriting from Filter:
- UdpProxyConfig with upstream endpoint
- UDP socket management
- Datagram forwarding
- Session tracking
- Override ProcessAsync for UDP proxying
```

### HTTP Filters

#### 56. Create Filters/BuiltinFilters/HttpCodecFilter.cs
```
Create HttpCodecFilter inheriting from Filter:
- HTTP request/response parsing
- Header manipulation
- Body encoding/decoding
- HTTP version handling
- Override ProcessAsync for codec operations
```

#### 57. Create Filters/BuiltinFilters/HttpRouterFilter.cs
```
Create HttpRouterFilter inheriting from Filter:
- Route configuration
- Path matching
- Method-based routing
- Route parameters extraction
- Override ProcessAsync for routing
```

#### 58. Create Filters/BuiltinFilters/HttpCompressionFilter.cs
```
Create HttpCompressionFilter inheriting from Filter:
- Compression algorithms (gzip, deflate, brotli)
- Content-Encoding handling
- Minimum size threshold
- Compression level configuration
- Override ProcessAsync for compression
```

### Security Filters

#### 59. Create Filters/BuiltinFilters/TlsTerminationFilter.cs
```
Create TlsTerminationFilter inheriting from Filter:
- Certificate configuration
- TLS version selection
- Cipher suite configuration
- Client certificate validation
- Override ProcessAsync for TLS termination
```

#### 60. Create Filters/BuiltinFilters/AuthenticationFilter.cs
```
Create AuthenticationFilter inheriting from Filter:
- Support JWT, API Key, OAuth2
- Token validation
- User extraction
- Authentication bypass rules
- Override ProcessAsync for authentication
```

#### 61. Create Filters/BuiltinFilters/AuthorizationFilter.cs
```
Create AuthorizationFilter inheriting from Filter:
- Policy-based authorization
- Role checking
- Resource-based rules
- Custom authorization handlers
- Override ProcessAsync for authorization
```

### Observability Filters

#### 62. Create Filters/BuiltinFilters/AccessLogFilter.cs
```
Create AccessLogFilter inheriting from Filter:
- Log format configuration (JSON, text)
- Field selection
- Output targets (console, file)
- Request/response logging
- Override ProcessAsync for logging
```

#### 63. Create Filters/BuiltinFilters/MetricsFilter.cs
```
Create MetricsFilter inheriting from Filter:
- Metric collection (counters, gauges, histograms)
- Label configuration
- Prometheus format support
- Custom metrics
- Override ProcessAsync for metrics
```

#### 64. Create Filters/BuiltinFilters/TracingFilter.cs
```
Create TracingFilter inheriting from Filter:
- Distributed tracing support
- Span creation and propagation
- Sampling configuration
- OpenTelemetry integration
- Override ProcessAsync for tracing
```

### Traffic Management Filters

#### 65. Create Filters/BuiltinFilters/RateLimitFilter.cs
```
Create RateLimitFilter inheriting from Filter:
- Token bucket algorithm
- Per-client limiting
- Burst handling
- Custom key extraction
- Override ProcessAsync for rate limiting
```

#### 66. Create Filters/BuiltinFilters/CircuitBreakerFilter.cs
```
Create CircuitBreakerFilter inheriting from Filter:
- Failure threshold configuration
- Timeout settings
- Half-open state
- Reset timeout
- Override ProcessAsync for circuit breaking
```

#### 67. Create Filters/BuiltinFilters/RetryFilter.cs
```
Create RetryFilter inheriting from Filter:
- Retry strategies (exponential, linear)
- Max attempts configuration
- Retry conditions
- Backoff settings
- Override ProcessAsync for retry logic
```

#### 68. Create Filters/BuiltinFilters/LoadBalancerFilter.cs
```
Create LoadBalancerFilter inheriting from Filter:
- Balancing algorithms (round-robin, least-conn, random)
- Health checking
- Upstream configuration
- Session affinity
- Override ProcessAsync for load balancing
```

## Phase 5: Transport and Integration

### Transport Interface

#### 69. Create Transport/ITransport.cs
```
Create src/Transport/ITransport.cs interface:
- StartAsync(CancellationToken) method
- StopAsync(CancellationToken) method  
- SendAsync(JsonRpcMessage, CancellationToken) method
- ReceiveAsync(CancellationToken) returning Task<JsonRpcMessage>
- MessageReceived event
- Error event
- Connected/Disconnected events
- IDisposable inheritance
```

### GopherTransport Implementation

#### 70. Create Transport/GopherTransport.cs
```
Create src/Transport/GopherTransport.cs implementing ITransport:
- TransportConfig property
- FilterManager integration
- Protocol selection logic
- Connection state management
- Message queue
- Event handlers
```

#### 71. Implement GopherTransport constructor
```
In GopherTransport.cs, implement constructor:
- Accept TransportConfig parameter
- Initialize FilterManager with config.Filters
- Select protocol implementation
- Initialize message buffers
- Set up cancellation token source
```

#### 72. Implement GopherTransport.StartAsync
```
In GopherTransport.cs, implement StartAsync:
- Connect via selected protocol
- Start receive loop task
- Initialize connection state
- Raise Connected event
- Handle connection errors
```

#### 73. Implement GopherTransport.StopAsync
```
In GopherTransport.cs, implement StopAsync:
- Cancel receive loop
- Close protocol connection
- Flush pending messages
- Raise Disconnected event
- Cleanup resources
```

#### 74. Implement GopherTransport.SendAsync
```
In GopherTransport.cs, implement SendAsync:
- Process message through FilterManager
- Serialize to protocol format
- Send via protocol implementation
- Handle send errors
- Update statistics
```

#### 75. Implement GopherTransport.ReceiveLoop
```
In GopherTransport.cs, implement ReceiveLoop:
- Continuous receive from protocol
- Process through FilterManager
- Raise MessageReceived event
- Handle receive errors
- Support cancellation
```

#### 76. Implement GopherTransport.Dispose
```
In GopherTransport.cs, implement Dispose:
- Stop transport if running
- Dispose FilterManager
- Dispose protocol implementation
- Clear message queues
- Release resources
```

### Protocol-Specific Transports

#### 77. Create Transport/TcpTransport.cs
```
Create TcpTransport.cs:
- TcpClient management
- NetworkStream handling
- Connection retry logic
- Keep-alive configuration
- Message framing
```

#### 78. Create Transport/UdpTransport.cs
```
Create UdpTransport.cs:
- UdpClient management
- Datagram handling
- Message fragmentation
- Reliability layer (optional)
- Multicast support
```

#### 79. Create Transport/StdioTransport.cs
```
Create StdioTransport.cs:
- Console.In/Out handling
- Line-based protocol
- Buffer management
- Non-blocking I/O
- Process communication
```

#### 80. Create Transport/TransportConfig.cs
```
Create TransportConfig.cs:
- Protocol selection enum
- Host and port settings
- Timeout configurations
- FilterManagerConfig property
- Connection options
```

### Integration Components

#### 81. Create Integration/JsonRpcMessage.cs
```
Create JsonRpcMessage.cs:
- Properties: JsonRpc, Id, Method, Params, Result, Error
- Validation methods
- Serialization support
- Builder pattern for construction
- Compatibility with MCP spec
```

#### 82. Create Integration/McpClient.cs
```
Create McpClient.cs wrapper:
- ITransport integration
- Method invocation helpers
- Response handling
- Request ID management
- Tool/Prompt discovery
```

#### 83. Create Integration/McpServer.cs
```
Create McpServer.cs wrapper:
- ITransport integration
- Method registration
- Request dispatching
- Tool/Prompt/Resource providers
- Error handling
```

#### 84. Create Integration/McpExtensions.cs
```
Create McpExtensions.cs:
- Extension methods for ITransport
- JsonRpcMessage helpers
- Config builders
- Async helpers
- Conversion utilities
```

## Phase 6: Examples and Tests

### Example Applications

#### 85. Create examples/BasicUsage/BasicUsage.csproj
```
Create BasicUsage.csproj:
- Reference GopherMcp project
- Target net8.0
- OutputType: Exe
- Enable nullable
```

#### 86. Create examples/BasicUsage/Program.cs
```
Create Program.cs demonstrating:
- FilterManager creation
- Basic filter configuration
- Message processing
- Statistics display
- Error handling
```

#### 87. Create examples/McpCalculatorServer/McpCalculatorServer.csproj
```
Create McpCalculatorServer.csproj:
- Reference GopherMcp project
- Add MCP SDK package reference
- Target net8.0
- OutputType: Exe
```

#### 88. Create examples/McpCalculatorServer/Program.cs
```
Create Program.cs with:
- McpServer initialization
- GopherTransport configuration
- Calculator tool registration
- Request handling
- Graceful shutdown
```

#### 89. Create examples/McpCalculatorServer/CalculatorTools.cs
```
Create CalculatorTools.cs with:
- Add, Subtract, Multiply, Divide methods
- MCP tool attributes
- Parameter validation
- Error handling
```

#### 90. Create examples/McpCalculatorClient/McpCalculatorClient.csproj
```
Create McpCalculatorClient.csproj:
- Reference GopherMcp project
- Add MCP SDK package reference
- Target net8.0
- OutputType: Exe
```

#### 91. Create examples/McpCalculatorClient/Program.cs
```
Create Program.cs with:
- McpClient initialization
- GopherTransport configuration
- Tool discovery
- Calculator operations invocation
- Result display
```

#### 92. Create examples/AdvancedFiltering/AdvancedFiltering.csproj
```
Create AdvancedFiltering.csproj:
- Reference GopherMcp project
- Target net8.0
- OutputType: Exe
```

#### 93. Create examples/AdvancedFiltering/Program.cs
```
Create Program.cs demonstrating:
- Complex filter chains
- Parallel processing
- Conditional filtering
- Custom filters
- Performance monitoring
```

### Test Infrastructure

#### 94. Create tests/Unit/FilterTests.cs
```
Create FilterTests.cs with tests for:
- Filter lifecycle
- ProcessAsync behavior
- Event raising
- Configuration updates
- Disposal
- Mock native handles
```

#### 95. Create tests/Unit/ChainTests.cs
```
Create ChainTests.cs with tests for:
- Chain building
- Filter ordering
- Execution modes
- Error propagation
- Statistics aggregation
```

#### 96. Create tests/Unit/BufferTests.cs
```
Create BufferTests.cs with tests for:
- Buffer allocation
- Read/write operations
- Zero-copy slicing
- Memory management
- Ownership tracking
```

#### 97. Create tests/Unit/ManagerTests.cs
```
Create ManagerTests.cs with tests for:
- Manager initialization
- Filter registration
- Chain creation
- Message processing
- Configuration updates
```

#### 98. Create tests/Integration/TransportTests.cs
```
Create TransportTests.cs with tests for:
- Transport connection
- Message sending/receiving
- Protocol switching
- Error handling
- Reconnection
```

#### 99. Create tests/Integration/McpIntegrationTests.cs
```
Create McpIntegrationTests.cs with tests for:
- Client-server communication
- Tool invocation
- Error propagation
- Timeout handling
- Multiple clients
```

#### 100. Create tests/Integration/EndToEndTests.cs
```
Create EndToEndTests.cs with tests for:
- Complete message flow
- Filter chain processing
- Performance benchmarks
- Memory leak detection
- Stress testing
```

#### 101. Create tests/Fixtures/TestFixtures.cs
```
Create TestFixtures.cs with:
- Mock filter implementations
- Test configurations
- Sample messages
- Helper methods
- Test data generators
```

## Phase 7: Build and Documentation

### Build System

#### 102. Create build/build.ps1
```
Create PowerShell build script with:
- Parameter definitions for configuration
- Native library building
- .NET SDK building
- Test execution
- Package creation
- Platform detection for Windows
```

#### 103. Create build/GopherMcp.targets
```
Create MSBuild targets file with:
- Native library detection target
- Copy native library target
- Clean native libraries target
- Platform-specific conditions
- Warning for missing libraries
```

#### 104. Create Directory.Build.props
```
Create Directory.Build.props with:
- Global build properties
- Code analysis settings
- Deterministic build settings
- Source Link configuration
- Performance optimizations
```

#### 105. Create Dockerfile
```
Create multi-stage Dockerfile with:
- Native library build stage
- .NET SDK build stage
- Runtime stage
- Test execution support
- Package extraction
```

#### 106. Create .github/workflows/build.yml
```
Create GitHub Actions workflow with:
- Matrix build for multiple OS/frameworks
- Native library compilation
- Test execution
- Artifact upload
- NuGet package publishing
```

#### 107. Create setup-dev.sh
```
Create development setup script with:
- Prerequisite checking
- Repository cloning
- Submodule initialization
- .NET tool restoration
- Initial build
```

### Documentation

#### 108. Create docs/API.md
```
Create API documentation with:
- Complete API reference
- Class hierarchies
- Method signatures
- Property descriptions
- Usage examples for each component
```

#### 109. Create docs/GettingStarted.md
```
Create getting started guide with:
- Installation instructions
- First filter creation
- Basic usage examples
- Common scenarios
- Troubleshooting tips
```

#### 110. Create docs/FilterGuide.md
```
Create filter development guide with:
- Custom filter creation
- Filter lifecycle
- Configuration patterns
- Best practices
- Performance optimization
```

#### 111. Create docs/Examples.md
```
Create examples documentation with:
- Example descriptions
- Running instructions
- Code walkthroughs
- Expected outputs
- Modification suggestions
```

## Usage Instructions

Each prompt template is designed to be:
1. **Specific**: Clear about what needs to be created
2. **Complete**: Includes all necessary details
3. **Actionable**: Can be directly used to implement the task
4. **Contextual**: References related components when needed

To use these templates:
1. Copy the prompt for your current task
2. Add any project-specific context if needed
3. Execute the implementation
4. Mark the task as complete in docs/tasks.md
5. Move to the next task in the phase

The prompts follow the recommended implementation order to minimize dependencies and rework.