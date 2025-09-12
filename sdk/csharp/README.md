# GopherMcp C# Filter SDK

A high-performance, extensible filter chain library for C#/.NET applications, designed for request/response processing pipelines with built-in filters for common patterns like rate limiting, retries, circuit breaking, and more.

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Built-in Filters](#built-in-filters)
- [Building from Source](#building-from-source)
- [Testing](#testing)
- [Examples](#examples)
- [API Reference](#api-reference)
- [Performance Considerations](#performance-considerations)
- [Contributing](#contributing)
- [License](#license)
- [Support](#support)

## Overview

The GopherMcp Filter SDK provides a flexible filter chain architecture for processing data through a series of transformations and validations. It's particularly useful for:

- API request/response processing
- Message queue processing
- Data transformation pipelines
- Protocol implementation (like MCP - Model Context Protocol)
- Network traffic filtering and monitoring

### Key Features

- **Composable Filter Chains**: Build complex processing pipelines from simple, reusable filters
- **Async/Await Support**: Fully asynchronous processing for high-performance scenarios
- **Built-in Filters**: Production-ready filters for common patterns
- **Extensible**: Easy to create custom filters
- **Thread-Safe**: Designed for concurrent processing
- **Metrics & Monitoring**: Built-in statistics and performance tracking
- **Buffer Management**: Efficient memory management with pooled buffers

## Architecture

The GopherMcp Filter SDK is designed with a layered architecture that promotes separation of concerns, extensibility, and high performance. The architecture follows SOLID principles and employs several design patterns to ensure maintainability and scalability.

### Architecture Layers

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Application Layer                           │
│  (Your Application, MCP Servers/Clients, API Services, etc.)       │
└────────────────────────────┬────────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────────┐
│                      Filter Chain Layer                             │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │   FilterChain Orchestrator                                   │  │
│  │   • Sequential/Parallel Execution                            │  │
│  │   • Error Handling & Recovery                                │  │
│  │   • Statistics & Monitoring                                  │  │
│  └──────────────────────────────────────────────────────────────┘  │
└────────────────────────────┬────────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────────┐
│                        Filter Layer                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │RateLimit │→ │  Retry   │→ │ Circuit  │→ │ Metrics  │→ ...     │
│  │ Filter   │  │  Filter  │  │ Breaker  │  │  Filter  │          │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘          │
│                                                                     │
│  Each filter can:                                                  │
│  • Transform data (compression, encryption)                        │
│  • Validate/Authenticate (auth, schema validation)                 │
│  • Control flow (rate limiting, circuit breaking)                  │
│  • Monitor/Log (metrics, access logs)                             │
└────────────────────────────┬────────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────────┐
│                     Core Abstractions Layer                         │
│  ┌────────────┐  ┌─────────────────┐  ┌──────────────────┐       │
│  │   Filter   │  │ProcessingContext│  │  FilterResult    │       │
│  │(Base Class)│  │  (Metadata)     │  │   (Outcomes)     │       │
│  └────────────┘  └─────────────────┘  └──────────────────┘       │
└────────────────────────────┬────────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────────┐
│                    Infrastructure Layer                             │
│  ┌────────────┐  ┌─────────────┐  ┌────────────┐  ┌────────────┐ │
│  │   Buffer   │  │   Logger    │  │  Metrics   │  │  Config    │ │
│  │   Pool     │  │ Abstraction │  │ Collector  │  │  Manager   │ │
│  └────────────┘  └─────────────┘  └────────────┘  └────────────┘ │
└──────────────────────────────────────────────────────────────────────┘
```

### Core Components

#### 1. **Filter (Base Abstraction)**

The `Filter` class is the fundamental building block of the SDK. It provides a consistent interface for all data processing operations while allowing for diverse implementations. Each filter operates as an independent, self-contained unit that can be composed into complex processing pipelines.

**Key Responsibilities:**
- **Data Processing**: Transform, validate, or analyze incoming byte arrays
- **Context Enrichment**: Add metadata and state information to the processing context
- **Flow Control**: Decide whether to continue, block, or redirect the processing flow
- **Resource Management**: Handle initialization, cleanup, and disposal of resources

```csharp
public abstract class Filter
{
    protected FilterConfig Config { get; }
    protected Filter? Next { get; set; }  // Next filter in chain
    
    public abstract Task<FilterResult> ProcessAsync(byte[] data, ProcessingContext context);
    public abstract Task InitializeAsync();
    public abstract void Dispose();
    
    // Lifecycle hooks
    protected virtual Task OnInitializeAsync() => Task.CompletedTask;
    protected virtual void OnDispose() { }
}
```

**Design Considerations:**
- Filters are stateless for thread-safety and reusability
- Async-first design for non-blocking I/O operations
- Explicit lifecycle management for resource optimization

#### 2. **FilterChain (Orchestrator)**

The `FilterChain` class orchestrates the execution of multiple filters, managing their lifecycle and coordinating data flow between them. It implements sophisticated execution strategies and provides comprehensive monitoring capabilities.

**Key Responsibilities:**
- **Filter Management**: Add, remove, and organize filters in the processing pipeline
- **Execution Strategy**: Support sequential, parallel, and conditional execution modes
- **Error Handling**: Implement retry logic, fallback mechanisms, and error recovery
- **Performance Monitoring**: Track metrics, latencies, and throughput
- **Resource Coordination**: Manage shared resources across filters

```csharp
public class FilterChain
{
    private readonly List<Filter> _filters;
    private readonly ChainConfig _config;
    private readonly IMetricsCollector _metrics;
    
    public void AddFilter(Filter filter);
    public void RemoveFilter(string name);
    public async Task<FilterResult> ProcessAsync(byte[] data, ProcessingContext context);
    public ChainStatistics GetStatistics();
    
    // Advanced features
    public void SetExecutionMode(ChainExecutionMode mode);
    public void EnableCircuitBreaker(CircuitBreakerConfig config);
    public IDisposable Subscribe(IObserver<FilterEvent> observer);
}
```

**Execution Modes:**
- **Sequential**: Filters execute one after another in order
- **Parallel**: Independent filters execute concurrently
- **Conditional**: Filters execute based on runtime conditions
- **Pipeline**: Streaming execution with backpressure support

#### 3. **ProcessingContext (Metadata Carrier)**

The `ProcessingContext` serves as a thread-safe container for metadata and state that flows through the filter chain. It enables filters to communicate and share information without tight coupling.

**Key Features:**
- **Property Bag**: Store arbitrary key-value pairs for filter communication
- **Correlation Tracking**: Maintain request correlation IDs for distributed tracing
- **Performance Metrics**: Track timing information for each filter
- **Error Context**: Accumulate error information and recovery attempts
- **Cancellation Support**: Propagate cancellation tokens through the chain

```csharp
public class ProcessingContext
{
    private readonly ConcurrentDictionary<string, object> _properties;
    private readonly Stopwatch _stopwatch;
    
    public string CorrelationId { get; }
    public CancellationToken CancellationToken { get; }
    public TimeSpan ElapsedTime => _stopwatch.Elapsed;
    
    // Property management
    public void SetProperty(string key, object value);
    public T GetProperty<T>(string key);
    public bool TryGetProperty<T>(string key, out T value);
    
    // Metrics tracking
    public void RecordFilterExecutionTime(string filterName, TimeSpan duration);
    public IReadOnlyDictionary<string, TimeSpan> GetFilterTimings();
}
```

#### 4. **FilterResult (Operation Outcome)**

The `FilterResult` class encapsulates the outcome of a filter operation, providing a rich representation of success, failure, or intermediate states. It supports detailed error information and data transformation results.

**Result Types:**
- **Success**: Operation completed successfully with optional transformed data
- **Error**: Operation failed with detailed error information
- **Blocked**: Operation was intentionally blocked (e.g., rate limited)
- **Skipped**: Filter was bypassed based on conditions
- **Partial**: Operation partially succeeded with warnings

```csharp
public class FilterResult
{
    public FilterResultType Type { get; }
    public bool IsSuccess => Type == FilterResultType.Success;
    public bool IsError => Type == FilterResultType.Error;
    public string? ErrorMessage { get; }
    public Exception? Exception { get; }
    public byte[]? Data { get; }
    public Dictionary<string, object> Metadata { get; }
    
    // Factory methods
    public static FilterResult Success(byte[] data);
    public static FilterResult Error(string message, Exception? ex = null);
    public static FilterResult Blocked(string reason);
    public static FilterResult Partial(byte[] data, string[] warnings);
}
```

### Data Flow

```
  Input Data
      │
      ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Filter 1   │────▶│  Filter 2   │────▶│  Filter 3   │
│             │     │             │     │             │
│ • Validate  │     │ • Transform │     │ • Compress  │
└─────────────┘     └─────────────┘     └─────────────┘
      │                   │                   │
      ▼                   ▼                   ▼
   Context            Context             Context
   Updates            Updates             Updates
      │                   │                   │
      └───────────────────┴───────────────────┘
                          │
                          ▼
                    Final Result
```

### Design Patterns

#### **Chain of Responsibility Pattern**
The core pattern that allows filters to process requests in sequence, where each filter decides whether to handle, modify, or pass the request to the next filter in the chain. This pattern provides flexibility in composing processing pipelines and allows for dynamic chain modification.

#### **Strategy Pattern**
Different filter implementations represent different processing strategies (e.g., different compression algorithms, authentication methods). The pattern allows for runtime selection and swapping of algorithms without changing the client code.

#### **Builder Pattern**
Fluent configuration APIs for constructing filter chains and configuring individual filters. This pattern improves code readability and ensures proper initialization sequences.

```csharp
var chain = new FilterChainBuilder()
    .WithName("ApiPipeline")
    .AddRateLimit(60, TimeSpan.FromMinutes(1))
    .AddRetry(3, TimeSpan.FromSeconds(1))
    .AddCompression(CompressionLevel.Optimal)
    .Build();
```

#### **Object Pool Pattern**
Efficient management of frequently allocated objects like buffers and contexts. The pattern reduces garbage collection pressure and improves performance in high-throughput scenarios.

#### **Observer Pattern**
Event-driven notifications for filter lifecycle events, metrics updates, and error conditions. This pattern enables loose coupling between the filter chain and monitoring/logging systems.

#### **Decorator Pattern**
Filters can wrap and extend the functionality of other filters without modifying their structure. This pattern is used for adding cross-cutting concerns like logging and metrics collection.

### Thread Safety and Concurrency

The SDK is designed for concurrent execution with the following guarantees:

- **Immutable Configuration**: Filter configurations are immutable after initialization
- **Thread-Safe Context**: ProcessingContext uses concurrent collections for property storage
- **Stateless Filters**: Filters maintain no mutable state between invocations
- **Async/Await**: Proper use of async/await patterns for scalable I/O operations
- **Cancellation Support**: Cooperative cancellation through CancellationToken propagation

### Performance Optimizations

- **Buffer Pooling**: Reuse of byte arrays through ArrayPool<byte> to minimize allocations
- **Lazy Initialization**: Filters initialize resources only when first used
- **Short-Circuit Evaluation**: Failed filters immediately return without invoking subsequent filters
- **Metrics Caching**: Aggregate metrics are calculated periodically, not on every request
- **Lock-Free Operations**: Use of lock-free data structures where possible

## Installation

### Via NuGet Package (Future)

```bash
dotnet add package GopherMcp
```

### Via Project Reference

```xml
<ProjectReference Include="path/to/GopherMcp.csproj" />
```

## Quick Start

### Basic Filter Chain

```csharp
using GopherMcp.Filters;
using GopherMcp.Filters.BuiltinFilters;
using GopherMcp.Types;

// Create and configure a filter chain
var filterChain = new FilterChain(new ChainConfig 
{ 
    Name = "MyFilterChain",
    ExecutionMode = ChainExecutionMode.Sequential 
});

// Add built-in filters
filterChain.AddFilter(new RateLimitFilter(new RateLimitConfig
{
    Name = "ApiRateLimit",
    Enabled = true,
    RequestsPerMinute = 100,
    WindowSizeSeconds = 60
}));

filterChain.AddFilter(new RetryFilter(new RetryConfig
{
    Name = "ApiRetry",
    Enabled = true,
    MaxAttempts = 3,
    InitialDelay = TimeSpan.FromSeconds(1)
}));

// Initialize the chain
await filterChain.InitializeAsync();

// Process data
var data = Encoding.UTF8.GetBytes("Hello, World!");
var context = new ProcessingContext();
var result = await filterChain.ProcessAsync(data, context);

if (result.IsSuccess)
{
    Console.WriteLine("Processing succeeded");
}
```

## Built-in Filters

### 1. **RateLimitFilter**
Controls the rate of requests to prevent overload.

```csharp
var config = new RateLimitConfig
{
    Name = "RateLimit",
    Enabled = true,
    RequestsPerMinute = 60,
    WindowSizeSeconds = 60,
    BurstSize = 10
};
```

### 2. **RetryFilter**
Automatically retries failed operations with exponential backoff.

```csharp
var config = new RetryConfig
{
    Name = "Retry",
    Enabled = true,
    MaxAttempts = 3,
    InitialDelay = TimeSpan.FromMilliseconds(100),
    MaxDelay = TimeSpan.FromSeconds(30),
    BackoffMultiplier = 2.0
};
```

### 3. **CircuitBreakerFilter**
Prevents cascading failures by temporarily blocking requests after failures.

```csharp
var config = new CircuitBreakerConfig
{
    Name = "CircuitBreaker",
    Enabled = true,
    FailureThreshold = 5,
    TimeoutDuration = TimeSpan.FromSeconds(30),
    HalfOpenRequests = 3
};
```

### 4. **MetricsFilter**
Collects performance metrics and statistics.

```csharp
var config = new MetricsConfig
{
    Name = "Metrics",
    Enabled = true,
    EnableHistograms = true,
    HistogramBuckets = new[] { 0.1, 0.5, 1.0, 5.0, 10.0 }
};
```

### 5. **AccessLogFilter**
Logs all requests for auditing and debugging.

```csharp
var config = new AccessLogConfig
{
    Name = "AccessLog",
    Enabled = true,
    LogLevel = LogLevel.Information,
    IncludeHeaders = true,
    IncludeBody = false
};
```

### 6. **AuthenticationFilter**
Validates authentication tokens and credentials.

```csharp
var config = new AuthenticationConfig
{
    Name = "Auth",
    Enabled = true,
    Type = AuthType.Bearer,
    ValidationEndpoint = "https://auth.example.com/validate"
};
```

### 7. **CompressionFilter**
Compresses/decompresses data for efficient transmission.

```csharp
var config = new CompressionConfig
{
    Name = "Compression",
    Enabled = true,
    Algorithm = CompressionAlgorithm.Gzip,
    Level = CompressionLevel.Optimal
};
```

## Building from Source

### Prerequisites

- **.NET 8.0 SDK** or later ([Download](https://dotnet.microsoft.com/download))
- **Git** for cloning the repository
- **Optional IDE/Editor**:
  - Visual Studio 2022 (Windows/Mac)
  - VS Code with C# extension
  - JetBrains Rider
  - Visual Studio for Mac

### Quick Build

The easiest way to build the SDK is using the provided build script:

```bash
# Clone the repository
git clone https://github.com/yourusername/mcp-cpp-sdk.git
cd mcp-cpp-sdk/sdk/csharp

# Make build script executable (Unix/Linux/Mac)
chmod +x build.sh

# Build with default settings (Debug mode)
./build.sh

# Build in Release mode
./build.sh Release

# Build and run tests
./build.sh Release normal --test

# Build and create NuGet package
./build.sh Release normal --pack
```

### Build Script Options

The `build.sh` script provides comprehensive build automation:

```bash
Usage: ./build.sh [configuration] [verbosity] [options]

Configuration:
  Debug     Build with debug configuration (default)
  Release   Build with release configuration

Verbosity:
  quiet      Quiet output
  minimal    Minimal output  
  normal     Normal output (default)
  detailed   Detailed output
  diagnostic Diagnostic output

Options:
  --test, -t   Run tests after building
  --pack, -p   Create NuGet package
  --docs, -d   Generate documentation
  --help, -h   Show help message

Examples:
  ./build.sh                    # Build with Debug configuration
  ./build.sh Release            # Build with Release configuration
  ./build.sh Release normal -t  # Build and run tests
  ./build.sh Release minimal -p # Build and create package
```

### Manual Build Commands

If you prefer to build manually or on Windows without bash:

```bash
# Restore dependencies
dotnet restore

# Build the library
dotnet build src/GopherMcp.csproj -c Release

# Build examples
dotnet build examples/BasicUsage/BasicUsage.csproj -c Release
dotnet build examples/McpOfficial/Server/SimpleServer.csproj -c Release
dotnet build examples/McpOfficial/Client/SimpleClient.csproj -c Release

# Build tests
dotnet build tests/GopherMcp.Tests/GopherMcp.Tests.csproj -c Release

# Run tests
dotnet test tests/GopherMcp.Tests/GopherMcp.Tests.csproj -c Release

# Create NuGet package
dotnet pack src/GopherMcp.csproj -c Release -o ./nupkg
```

### Windows PowerShell Build

For Windows users, a PowerShell equivalent:

```powershell
# Set build configuration
$config = "Release"

# Clean previous builds
dotnet clean --configuration $config

# Restore packages
dotnet restore

# Build library
dotnet build src/GopherMcp.csproj --configuration $config

# Build and run tests
dotnet test tests/GopherMcp.Tests/GopherMcp.Tests.csproj --configuration $config

# Create package
dotnet pack src/GopherMcp.csproj --configuration $config --output ./nupkg
```

### Project Structure

```
sdk/csharp/
├── build.sh                       # Build automation script
├── README.md                      # This documentation
├── GopherMcp.sln                 # Solution file (if exists)
│
├── src/                          # Source code
│   ├── GopherMcp.csproj         # Main project file
│   ├── Filters/                  # Core filter implementations
│   │   ├── Filter.cs            # Base filter class
│   │   ├── FilterChain.cs       # Chain orchestration
│   │   ├── FilterBuffer.cs      # Buffer management
│   │   └── BuiltinFilters/      # Built-in filter implementations
│   │       ├── RateLimitFilter.cs
│   │       ├── RetryFilter.cs
│   │       ├── CircuitBreakerFilter.cs
│   │       ├── MetricsFilter.cs
│   │       ├── AccessLogFilter.cs
│   │       ├── AuthenticationFilter.cs
│   │       └── CompressionFilter.cs
│   ├── Types/                    # Common types and configs
│   │   ├── FilterConfig.cs
│   │   ├── FilterResult.cs
│   │   └── ProcessingContext.cs
│   ├── Utils/                    # Utility classes
│   │   ├── BufferPool.cs
│   │   ├── Metrics.cs
│   │   └── Logger.cs
│   ├── Transport/                # Transport abstractions
│   │   ├── ITransport.cs
│   │   └── GopherTransport.cs
│   └── Integration/              # Integration helpers
│       └── JsonRpcMessage.cs
│
├── tests/                        # Test projects
│   ├── Unit/                     # Unit tests
│   │   ├── FilterTests.cs
│   │   ├── ChainTests.cs
│   │   └── BufferTests.cs
│   ├── Integration/              # Integration tests
│   │   ├── EndToEndTests.cs
│   │   └── McpIntegrationTests.cs
│   ├── Performance/              # Performance benchmarks
│   │   └── BenchmarkTests.cs
│   └── Fixtures/                 # Test fixtures and helpers
│       └── TestFixtures.cs
│
├── examples/                     # Example applications
│   ├── BasicUsage/              # Simple filter chain example
│   ├── McpOfficial/             # Official MCP SDK integration
│   │   ├── Server/              # MCP server with filters
│   │   ├── Client/              # MCP client with filters
│   │   └── test-filters.sh     # Test script
│   └── AdvancedFiltering/       # Complex filtering scenarios
│
├── docs/                         # Documentation
│   ├── api/                     # API documentation
│   ├── guides/                  # User guides
│   └── docfx.json              # DocFX configuration
│
└── nupkg/                        # NuGet package output
```

### Build Output

After a successful build, you'll find the artifacts in these locations:

- **Library**: `src/bin/{Configuration}/net8.0/GopherMcp.dll`
- **Examples**: `examples/*/bin/{Configuration}/net8.0/`
- **Tests**: `tests/GopherMcp.Tests/bin/{Configuration}/net8.0/`
- **NuGet Package**: `./nupkg/GopherMcp.{version}.nupkg`
- **Documentation**: `./docs/_site/` (if generated)

## Testing

### Automated Test Execution

The build script provides comprehensive test execution with detailed reporting:

```bash
# Run tests with the build script
./build.sh Release normal --test
```

This will:
- Build all test projects
- Execute all tests with detailed console output
- Generate multiple test report formats (TRX, HTML)
- Collect code coverage data
- Display a comprehensive test summary with visual indicators
- Show failed test details if any tests fail

### Test Output Example

```
╔════════════════════════════════════════════════════════════════════════════╗
║                             TEST EXECUTION                                 ║
╚════════════════════════════════════════════════════════════════════════════╝

Running tests...

╔════════════════════════════════════════════════════════════════════════════╗
║                             TEST SUMMARY                                   ║
╚════════════════════════════════════════════════════════════════════════════╝

Test Results:
┌─────────────────────────────────────────────────────────┐
│ Metric              │ Count      │ Status              │
├─────────────────────────────────────────────────────────┤
│ Total Tests         │ 127        │ Executed            │
│ ✅ Passed           │ 125        │ SUCCESS             │
│ ❌ Failed           │ 0          │ FAILURE             │
│ ⏭️  Skipped         │ 2          │ SKIPPED             │
└─────────────────────────────────────────────────────────┘

Execution Time: 14 seconds
Pass Rate: 98.4%
[██████████████████████████████████████████████░░]

╔════════════════════════════════════════════════════════════════════════════╗
║                            TEST ARTIFACTS                                  ║
╚════════════════════════════════════════════════════════════════════════════╝

  📄 TRX Report: ./test-results/test_results.trx
  🌐 HTML Report: ./test-results/test_results.html
     Open in browser: open ./test-results/test_results.html
  📊 Code Coverage: ./test-results/coverage.cobertura.xml
     Coverage: 87.3%
  🔍 Diagnostic Log: ./test-results/diagnostics.log
```

### Manual Test Commands

```bash
# Run all tests
dotnet test

# Run with detailed output
dotnet test --logger "console;verbosity=detailed"

# Run specific test project
dotnet test tests/Unit/FilterTests.cs

# Run tests matching a pattern
dotnet test --filter "FullyQualifiedName~RateLimit"

# Run tests by category
dotnet test --filter "Category=Unit"
dotnet test --filter "Category=Integration"
dotnet test --filter "Category=Performance"

# Run with code coverage
dotnet test --collect:"XPlat Code Coverage" \
            --results-directory ./test-results

# Run with multiple loggers
dotnet test --logger "console;verbosity=normal" \
            --logger "trx;LogFileName=results.trx" \
            --logger "html;LogFileName=results.html"

# Run with blame to identify hanging tests
dotnet test --blame --blame-hang-timeout 30s
```

### Test Categories

Tests are organized into categories for targeted execution:

#### **Unit Tests** (`Category=Unit`)
Test individual components in isolation:
- Filter implementations
- Chain operations
- Buffer management
- Configuration validation
- Result handling

#### **Integration Tests** (`Category=Integration`)
Test component interactions:
- Filter chain execution
- End-to-end scenarios
- MCP SDK integration
- Transport layer tests
- Error propagation

#### **Performance Tests** (`Category=Performance`)
Benchmark and profile:
- Throughput measurements
- Memory allocation tracking
- Latency analysis
- Concurrent execution
- Buffer pool efficiency

#### **Smoke Tests** (`Category=Smoke`)
Quick validation tests:
- Basic functionality
- Configuration loading
- Filter initialization
- Chain assembly

### Writing Tests

#### Unit Test Example

```csharp
using Xunit;
using FluentAssertions;
using GopherMcp.Filters.BuiltinFilters;

[Trait("Category", "Unit")]
public class RateLimitFilterTests
{
    [Fact]
    public async Task RateLimitFilter_ShouldThrottleRequests_WhenLimitExceeded()
    {
        // Arrange
        var filter = new RateLimitFilter(new RateLimitConfig
        {
            Name = "TestRateLimit",
            Enabled = true,
            RequestsPerMinute = 60,
            WindowSizeSeconds = 60
        });
        
        await filter.InitializeAsync();
        var context = new ProcessingContext();
        var data = new byte[] { 1, 2, 3 };
        
        // Act - Send requests up to the limit
        for (int i = 0; i < 60; i++)
        {
            var result = await filter.ProcessAsync(data, context);
            result.IsSuccess.Should().BeTrue($"Request {i + 1} should succeed");
        }
        
        // Act - Send one more request (should be rate limited)
        var throttledResult = await filter.ProcessAsync(data, context);
        
        // Assert
        throttledResult.IsError.Should().BeTrue();
        throttledResult.ErrorMessage.Should().Contain("rate limit exceeded");
    }
    
    [Theory]
    [InlineData(10, 1)]
    [InlineData(60, 60)]
    [InlineData(120, 60)]
    public void RateLimitFilter_ShouldCalculateCorrectWindow(
        int requestsPerMinute, 
        int expectedWindowSeconds)
    {
        // Arrange & Act
        var config = new RateLimitConfig
        {
            RequestsPerMinute = requestsPerMinute,
            WindowSizeSeconds = expectedWindowSeconds
        };
        
        // Assert
        config.WindowSizeSeconds.Should().Be(expectedWindowSeconds);
    }
}
```

#### Integration Test Example

```csharp
[Trait("Category", "Integration")]
public class FilterChainIntegrationTests
{
    [Fact]
    public async Task FilterChain_ShouldProcessThroughMultipleFilters()
    {
        // Arrange
        var chain = new FilterChain(new ChainConfig 
        { 
            Name = "TestChain",
            ExecutionMode = ChainExecutionMode.Sequential 
        });
        
        chain.AddFilter(new RateLimitFilter(new RateLimitConfig
        {
            Name = "RateLimit",
            RequestsPerMinute = 100
        }));
        
        chain.AddFilter(new MetricsFilter(new MetricsConfig
        {
            Name = "Metrics",
            EnableHistograms = true
        }));
        
        await chain.InitializeAsync();
        
        // Act
        var data = Encoding.UTF8.GetBytes("test data");
        var context = new ProcessingContext();
        var result = await chain.ProcessAsync(data, context);
        
        // Assert
        result.IsSuccess.Should().BeTrue();
        chain.GetStatistics().TotalPacketsProcessed.Should().Be(1);
        chain.GetStatistics().TotalErrors.Should().Be(0);
    }
}
```

### Test Reports

The test execution generates multiple report formats:

1. **Console Output**: Real-time test execution feedback
2. **TRX Report**: Visual Studio compatible test results
3. **HTML Report**: Browser-viewable test report with charts
4. **Code Coverage**: XML and HTML coverage reports
5. **Diagnostic Logs**: Detailed execution logs for troubleshooting

### Continuous Integration

For CI/CD pipelines, use these commands:

```yaml
# GitHub Actions example
- name: Test
  run: |
    dotnet test \
      --configuration Release \
      --no-build \
      --verbosity normal \
      --logger "trx;LogFileName=test-results.trx" \
      --logger "GitHubActions" \
      --collect:"XPlat Code Coverage" \
      --results-directory ./TestResults \
      /p:CollectCoverage=true \
      /p:CoverletOutputFormat=opencover

# Azure DevOps example  
- task: DotNetCoreCLI@2
  displayName: 'Run Tests'
  inputs:
    command: 'test'
    projects: '**/tests/**/*.csproj'
    arguments: '--configuration Release --collect:"XPlat Code Coverage"'
    publishTestResults: true
```

### Performance Testing

Run performance benchmarks:

```bash
# Run BenchmarkDotNet tests
dotnet run -c Release --project tests/Performance/GopherMcp.Benchmarks.csproj

# Profile memory allocations
dotnet run -c Release --project tests/Performance/GopherMcp.Benchmarks.csproj -- --memory

# Generate performance reports
dotnet run -c Release --project tests/Performance/GopherMcp.Benchmarks.csproj -- --exporters html json
```

## Examples

### MCP Integration Example

The `examples/McpOfficial` directory contains a complete example of integrating GopherMcp filters with the official MCP C# SDK:

```bash
# Run the server
cd examples/McpOfficial/Server
dotnet run

# In another terminal, run the client
cd examples/McpOfficial/Client
dotnet run

# Or use the test script
cd examples/McpOfficial
./test-filters.sh
```

### Custom Filter Example

```csharp
public class LoggingFilter : Filter
{
    private readonly ILogger _logger;
    
    public LoggingFilter(LoggingConfig config, ILogger logger) 
        : base(config)
    {
        _logger = logger;
    }
    
    public override async Task<FilterResult> ProcessAsync(
        byte[] data, 
        ProcessingContext context)
    {
        // Log request
        _logger.LogInformation($"Processing {data.Length} bytes");
        
        // Pass to next filter
        var result = await Next.ProcessAsync(data, context);
        
        // Log response
        _logger.LogInformation($"Result: {result.IsSuccess}");
        
        return result;
    }
    
    protected override Task OnInitializeAsync()
    {
        _logger.LogInformation($"Filter {Name} initialized");
        return Task.CompletedTask;
    }
}
```

## API Reference

### Core Classes

#### FilterChain
- `AddFilter(Filter filter)` - Add a filter to the chain
- `RemoveFilter(string name)` - Remove a filter by name
- `ProcessAsync(byte[] data, ProcessingContext context)` - Process data through the chain
- `GetStatistics()` - Get chain performance statistics
- `GetFilters()` - Get list of filters in the chain
- `InitializeAsync()` - Initialize all filters in the chain

#### ProcessingContext
- `SetProperty(string key, object value)` - Set a context property
- `GetProperty<T>(string key)` - Get a typed property
- `TryGetProperty<T>(string key, out T value)` - Try to get a property
- `ContainsProperty(string key)` - Check if property exists
- `RemoveProperty(string key)` - Remove a property

#### FilterResult
- `Success(byte[] data)` - Create a success result
- `Error(string message)` - Create an error result
- `Blocked(string reason)` - Create a blocked result

### Configuration Classes

All filter configurations inherit from `FilterConfig`:

```csharp
public class FilterConfig
{
    public string Name { get; set; }
    public bool Enabled { get; set; }
    public int Priority { get; set; }
    public Dictionary<string, object> Metadata { get; set; }
}
```

## Performance Considerations

1. **Buffer Pooling**: Use `FilterBuffer` for efficient memory management
2. **Async Processing**: All filters support async operations for non-blocking I/O
3. **Statistics**: Monitor performance with built-in metrics
4. **Batch Processing**: Process multiple items together when possible
5. **Circuit Breaking**: Prevent cascade failures with circuit breaker pattern

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](../../CONTRIBUTING.md) for guidelines.

## License

This project is licensed under the MIT License - see the [LICENSE](../../LICENSE) file for details.

## Support

For issues, questions, or contributions, please visit the [GitHub repository](https://github.com/yourusername/mcp-cpp-sdk).