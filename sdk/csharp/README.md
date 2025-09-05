# MCP Filter C# SDK Design Document

## 1. Architecture Overview

### Layered Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                      │
│         (MCP Client/Server, User Applications)           │
├─────────────────────────────────────────────────────────┤
│                   High-Level API Layer                    │
│   (FilterManager, GopherTransport, MCP Integration)      │
├─────────────────────────────────────────────────────────┤
│                   Managed Wrapper Layer                   │
│    (Type-safe C# wrappers, async/await, IDisposable)    │
├─────────────────────────────────────────────────────────┤
│                    P/Invoke Layer                        │
│        (Direct FFI bindings to C API functions)          │
├─────────────────────────────────────────────────────────┤
│                    Native C++ Layer                      │
│            (libgopher_mcp_c shared library)              │
└─────────────────────────────────────────────────────────┘
```

## 2. Project Structure

```
sdk/csharp/
├── src/
│   ├── GopherMcp.csproj                    # Main project file
│   │
│   ├── Core/                               # Core P/Invoke bindings
│   │   ├── McpTypesApi.cs                  # Type manipulation functions
│   │   ├── McpFilterApi.cs                 # Filter lifecycle & chain management
│   │   ├── McpFilterChainApi.cs            # Advanced chain composition
│   │   ├── McpFilterBufferApi.cs           # Buffer operations
│   │   ├── NativeLibrary.cs                # Library loading & management
│   │   └── SafeHandles.cs                  # Safe handle implementations
│   │
│   ├── Types/                              # Type definitions
│   │   ├── McpTypes.cs                     # Core MCP types & enums
│   │   ├── FilterTypes.cs                  # Filter-specific types
│   │   ├── ChainTypes.cs                   # Chain composition types
│   │   ├── BufferTypes.cs                  # Buffer operation types
│   │   └── Exceptions.cs                   # Custom exceptions
│   │
│   ├── Filters/                            # Managed filter wrappers
│   │   ├── Filter.cs                       # Base filter class
│   │   ├── FilterChain.cs                  # Chain management
│   │   ├── FilterBuffer.cs                 # Buffer operations
│   │   ├── FilterConfig.cs                 # Configuration classes
│   │   └── BuiltinFilters/                 # Built-in filter implementations
│   │       ├── TcpProxyFilter.cs
│   │       ├── HttpCodecFilter.cs
│   │       ├── AuthenticationFilter.cs
│   │       ├── RateLimitFilter.cs
│   │       └── ... (all 15 filter types)
│   │
│   ├── Manager/                            # High-level management
│   │   ├── FilterManager.cs                # Message processing pipeline
│   │   ├── FilterManagerConfig.cs          # Configuration
│   │   ├── ChainBuilder.cs                 # Fluent chain builder
│   │   └── MessageProcessor.cs             # JSON-RPC processing
│   │
│   ├── Transport/                          # Transport implementations
│   │   ├── ITransport.cs                   # Transport interface
│   │   ├── GopherTransport.cs              # Main transport implementation
│   │   ├── TcpTransport.cs                 # TCP-specific transport
│   │   ├── UdpTransport.cs                 # UDP-specific transport
│   │   ├── StdioTransport.cs               # Stdio transport
│   │   └── TransportConfig.cs              # Transport configuration
│   │
│   ├── Integration/                        # MCP integration
│   │   ├── McpClient.cs                    # MCP client wrapper
│   │   ├── McpServer.cs                    # MCP server wrapper
│   │   ├── JsonRpcMessage.cs               # JSON-RPC message types
│   │   └── McpExtensions.cs                # Extension methods
│   │
│   └── Utils/                              # Utilities
│       ├── MemoryManager.cs                # Memory management utilities
│       ├── CallbackManager.cs              # Callback lifecycle management
│       ├── PlatformDetection.cs            # Platform & architecture detection
│       └── JsonSerializer.cs               # JSON serialization helpers
│
├── tests/
│   ├── GopherMcp.Tests.csproj
│   ├── Unit/
│   │   ├── FilterTests.cs
│   │   ├── ChainTests.cs
│   │   ├── BufferTests.cs
│   │   └── ManagerTests.cs
│   ├── Integration/
│   │   ├── TransportTests.cs
│   │   ├── McpIntegrationTests.cs
│   │   └── EndToEndTests.cs
│   └── Fixtures/
│       └── TestFixtures.cs
│
├── examples/
│   ├── BasicUsage/
│   │   ├── BasicUsage.csproj
│   │   └── Program.cs
│   ├── McpCalculatorServer/
│   │   ├── McpCalculatorServer.csproj
│   │   ├── Program.cs
│   │   └── CalculatorTools.cs
│   ├── McpCalculatorClient/
│   │   ├── McpCalculatorClient.csproj
│   │   └── Program.cs
│   └── AdvancedFiltering/
│       ├── AdvancedFiltering.csproj
│       └── Program.cs
│
├── docs/
│   ├── API.md
│   ├── GettingStarted.md
│   ├── FilterGuide.md
│   └── Examples.md
│
└── build/
    ├── build.sh
    ├── build.ps1
    └── native/
        └── (platform-specific libraries)
```

## 3. Core Components Design

### 3.1 P/Invoke Layer

```csharp
namespace GopherMcp.Core
{
    public static class McpFilterApi
    {
        private const string LibraryName = "gopher_mcp_c";

        // Filter lifecycle
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpFilterHandle mcp_filter_create(
            IntPtr managerId,
            IntPtr config);

        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_destroy(McpFilterHandle filter);

        // Chain management
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpChainHandle mcp_chain_create(
            IntPtr managerId,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    }
}
```

### 3.2 Safe Handle Implementation

```csharp
namespace GopherMcp.Core
{
    public class McpFilterHandle : SafeHandle
    {
        public McpFilterHandle() : base(IntPtr.Zero, true) { }

        public override bool IsInvalid => handle == IntPtr.Zero;

        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                McpFilterApi.mcp_filter_destroy(this);
                return true;
            }
            return false;
        }
    }

    public class McpChainHandle : SafeHandle { /* Similar implementation */ }
    public class McpBufferHandle : SafeHandle { /* Similar implementation */ }
}
```

### 3.3 Managed Filter Wrapper

```csharp
namespace GopherMcp.Filters
{
    public abstract class Filter : IDisposable
    {
        protected McpFilterHandle handle;
        private readonly FilterConfig config;
        private bool disposed;

        protected Filter(FilterConfig config)
        {
            this.config = config ?? throw new ArgumentNullException(nameof(config));
            this.handle = CreateNativeFilter(config);
        }

        // Core operations
        public virtual async Task<FilterResult> ProcessAsync(
            FilterBuffer input, 
            CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            return await Task.Run(() => ProcessInternal(input), cancellationToken);
        }

        // Lifecycle events
        public event EventHandler<FilterEventArgs> OnInitialize;
        public event EventHandler<FilterEventArgs> OnDestroy;
        public event EventHandler<FilterDataEventArgs> OnData;
        public event EventHandler<FilterErrorEventArgs> OnError;

        // Statistics
        public FilterStatistics GetStatistics()
        {
            ThrowIfDisposed();
            return GetStatisticsInternal();
        }

        // Configuration
        public void UpdateConfig(FilterConfig config)
        {
            ThrowIfDisposed();
            UpdateConfigInternal(config);
        }

        // Resource management
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!disposed)
            {
                if (disposing)
                {
                    // Dispose managed resources
                }
                handle?.Dispose();
                disposed = true;
            }
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
                throw new ObjectDisposedException(GetType().Name);
        }

        // Abstract methods for derived classes
        protected abstract McpFilterHandle CreateNativeFilter(FilterConfig config);
        protected abstract FilterResult ProcessInternal(FilterBuffer input);
        protected abstract FilterStatistics GetStatisticsInternal();
        protected abstract void UpdateConfigInternal(FilterConfig config);
    }
}
```

### 3.4 Filter Manager

```csharp
namespace GopherMcp.Manager
{
    public class FilterManager : IDisposable
    {
        private readonly McpManagerHandle handle;
        private readonly FilterManagerConfig config;
        private readonly List<FilterChain> chains;
        private readonly ConcurrentDictionary<Guid, Filter> filters;

        public FilterManager(FilterManagerConfig config = null)
        {
            this.config = config ?? FilterManagerConfig.Default;
            this.handle = McpFilterApi.mcp_filter_manager_create(IntPtr.Zero);
            this.chains = new List<FilterChain>();
            this.filters = new ConcurrentDictionary<Guid, Filter>();
            
            InitializeDefaultFilters();
        }

        // Message processing
        public async Task<JsonRpcMessage> ProcessAsync(
            JsonRpcMessage message,
            ProcessingContext context = null,
            CancellationToken cancellationToken = default)
        {
            var buffer = SerializeMessage(message);
            var result = await ProcessBufferAsync(buffer, context, cancellationToken);
            return DeserializeMessage(result);
        }

        // Chain management
        public FilterChain CreateChain(string name, ChainConfig config = null)
        {
            var chain = new FilterChain(handle, name, config);
            chains.Add(chain);
            return chain;
        }

        // Filter registration
        public void RegisterFilter(string name, Filter filter)
        {
            var id = Guid.NewGuid();
            if (filters.TryAdd(id, filter))
            {
                filter.Id = id;
                filter.Manager = this;
            }
        }

        // Fluent builder interface
        public ChainBuilder BuildChain(string name)
        {
            return new ChainBuilder(this, name);
        }

        // Statistics and monitoring
        public ManagerStatistics GetStatistics()
        {
            return new ManagerStatistics
            {
                TotalFilters = filters.Count,
                TotalChains = chains.Count,
                ProcessedMessages = GetProcessedMessageCount(),
                AverageLatency = GetAverageLatency()
            };
        }

        // Configuration updates
        public void UpdateConfiguration(FilterManagerConfig config)
        {
            this.config = config;
            ApplyConfiguration();
        }

        private void InitializeDefaultFilters()
        {
            if (config.Security?.Authentication?.Enabled == true)
            {
                RegisterFilter("auth", new AuthenticationFilter(
                    config.Security.Authentication));
            }

            if (config.Observability?.Metrics?.Enabled == true)
            {
                RegisterFilter("metrics", new MetricsFilter(
                    config.Observability.Metrics));
            }

            if (config.TrafficManagement?.RateLimit?.Enabled == true)
            {
                RegisterFilter("ratelimit", new RateLimitFilter(
                    config.TrafficManagement.RateLimit));
            }
        }
    }
}
```

### 3.5 Transport Implementation

```csharp
namespace GopherMcp.Transport
{
    public interface ITransport : IDisposable
    {
        Task StartAsync(CancellationToken cancellationToken = default);
        Task StopAsync(CancellationToken cancellationToken = default);
        Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken = default);
        Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken = default);
        
        event EventHandler<MessageReceivedEventArgs> MessageReceived;
        event EventHandler<ErrorEventArgs> Error;
        event EventHandler Connected;
        event EventHandler Disconnected;
    }

    public class GopherTransport : ITransport
    {
        private readonly TransportConfig config;
        private readonly FilterManager filterManager;
        private readonly ITransportProtocol protocol;
        private CancellationTokenSource cancellationTokenSource;
        private Task receiveTask;

        public GopherTransport(TransportConfig config)
        {
            this.config = config ?? throw new ArgumentNullException(nameof(config));
            
            // Initialize filter manager with transport config
            this.filterManager = new FilterManager(config.Filters);
            
            // Select protocol implementation
            this.protocol = config.Protocol switch
            {
                TransportProtocol.Tcp => new TcpTransportProtocol(config),
                TransportProtocol.Udp => new UdpTransportProtocol(config),
                TransportProtocol.Stdio => new StdioTransportProtocol(config),
                _ => throw new NotSupportedException($"Protocol {config.Protocol} not supported")
            };
        }

        public async Task StartAsync(CancellationToken cancellationToken = default)
        {
            await protocol.ConnectAsync(cancellationToken);
            
            cancellationTokenSource = new CancellationTokenSource();
            receiveTask = Task.Run(() => ReceiveLoop(cancellationTokenSource.Token));
            
            Connected?.Invoke(this, EventArgs.Empty);
        }

        public async Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken = default)
        {
            // Process through filter manager
            var processed = await filterManager.ProcessAsync(message, 
                new ProcessingContext { Direction = MessageDirection.Outbound },
                cancellationToken);
            
            // Send via protocol
            await protocol.SendAsync(processed, cancellationToken);
        }

        private async Task ReceiveLoop(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                try
                {
                    var message = await protocol.ReceiveAsync(cancellationToken);
                    
                    // Process through filter manager
                    var processed = await filterManager.ProcessAsync(message,
                        new ProcessingContext { Direction = MessageDirection.Inbound },
                        cancellationToken);
                    
                    MessageReceived?.Invoke(this, new MessageReceivedEventArgs(processed));
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Error?.Invoke(this, new ErrorEventArgs(ex));
                }
            }
        }

        public event EventHandler<MessageReceivedEventArgs> MessageReceived;
        public event EventHandler<ErrorEventArgs> Error;
        public event EventHandler Connected;
        public event EventHandler Disconnected;
    }
}
```

### 3.6 Fluent Chain Builder

```csharp
namespace GopherMcp.Manager
{
    public class ChainBuilder
    {
        private readonly FilterManager manager;
        private readonly string chainName;
        private readonly List<FilterDescriptor> filters;
        private ChainExecutionMode executionMode;

        internal ChainBuilder(FilterManager manager, string chainName)
        {
            this.manager = manager;
            this.chainName = chainName;
            this.filters = new List<FilterDescriptor>();
            this.executionMode = ChainExecutionMode.Sequential;
        }

        // Filter addition methods
        public ChainBuilder AddFilter<T>(Action<T> configure = null) 
            where T : Filter, new()
        {
            var filter = new T();
            configure?.Invoke(filter);
            filters.Add(new FilterDescriptor { Filter = filter });
            return this;
        }

        public ChainBuilder AddTcpProxy(Action<TcpProxyConfig> configure)
        {
            var config = new TcpProxyConfig();
            configure?.Invoke(config);
            filters.Add(new FilterDescriptor 
            { 
                Filter = new TcpProxyFilter(config) 
            });
            return this;
        }

        public ChainBuilder AddAuthentication(AuthenticationMethod method, string secret)
        {
            filters.Add(new FilterDescriptor
            {
                Filter = new AuthenticationFilter(new AuthenticationConfig
                {
                    Method = method,
                    Secret = secret
                })
            });
            return this;
        }

        public ChainBuilder AddRateLimit(int requestsPerMinute, int burstSize = 10)
        {
            filters.Add(new FilterDescriptor
            {
                Filter = new RateLimitFilter(new RateLimitConfig
                {
                    RequestsPerMinute = requestsPerMinute,
                    BurstSize = burstSize
                })
            });
            return this;
        }

        // Chain configuration methods
        public ChainBuilder Sequential()
        {
            executionMode = ChainExecutionMode.Sequential;
            return this;
        }

        public ChainBuilder Parallel(int maxConcurrency = 4)
        {
            executionMode = ChainExecutionMode.Parallel;
            return this;
        }

        public ChainBuilder Conditional(Func<ProcessingContext, bool> condition)
        {
            executionMode = ChainExecutionMode.Conditional;
            return this;
        }

        // Build the chain
        public FilterChain Build()
        {
            var chain = manager.CreateChain(chainName, new ChainConfig
            {
                ExecutionMode = executionMode,
                Filters = filters
            });

            foreach (var descriptor in filters)
            {
                chain.AddFilter(descriptor.Filter);
            }

            return chain;
        }
    }
}
```

## 4. Configuration System

```csharp
namespace GopherMcp.Manager
{
    public class FilterManagerConfig
    {
        public SecurityConfig Security { get; set; }
        public ObservabilityConfig Observability { get; set; }
        public TrafficManagementConfig TrafficManagement { get; set; }
        public HttpConfig Http { get; set; }
        public NetworkConfig Network { get; set; }
        public ErrorHandlingConfig ErrorHandling { get; set; }

        public static FilterManagerConfig Default => new()
        {
            Security = new SecurityConfig
            {
                Authentication = new AuthenticationConfig
                {
                    Enabled = false,
                    Method = AuthenticationMethod.None
                }
            },
            Observability = new ObservabilityConfig
            {
                Metrics = new MetricsConfig { Enabled = true },
                AccessLog = new AccessLogConfig { Enabled = true }
            },
            TrafficManagement = new TrafficManagementConfig
            {
                RateLimit = new RateLimitConfig
                {
                    Enabled = true,
                    RequestsPerMinute = 1000
                }
            }
        };
    }

    public class SecurityConfig
    {
        public AuthenticationConfig Authentication { get; set; }
        public AuthorizationConfig Authorization { get; set; }
        public TlsConfig Tls { get; set; }
    }

    public class AuthenticationConfig
    {
        public bool Enabled { get; set; }
        public AuthenticationMethod Method { get; set; }
        public string Secret { get; set; }
        public string Issuer { get; set; }
        public string Audience { get; set; }
        public TimeSpan TokenExpiration { get; set; }
    }

    public enum AuthenticationMethod
    {
        None,
        Jwt,
        ApiKey,
        OAuth2,
        Basic,
        Certificate
    }
}
```

## 5. Usage Examples

### 5.1 Basic Filter Usage

```csharp
using GopherMcp.Manager;
using GopherMcp.Filters;

// Create filter manager with configuration
var config = new FilterManagerConfig
{
    Security = new SecurityConfig
    {
        Authentication = new AuthenticationConfig
        {
            Enabled = true,
            Method = AuthenticationMethod.Jwt,
            Secret = "my-secret-key"
        }
    },
    TrafficManagement = new TrafficManagementConfig
    {
        RateLimit = new RateLimitConfig
        {
            Enabled = true,
            RequestsPerMinute = 100
        }
    }
};

using var manager = new FilterManager(config);

// Process a message
var message = new JsonRpcMessage
{
    JsonRpc = "2.0",
    Id = "1",
    Method = "tools/list",
    Params = new { }
};

var result = await manager.ProcessAsync(message);
```

### 5.2 Fluent Chain Building

```csharp
using var manager = new FilterManager();

var chain = manager.BuildChain("api-pipeline")
    .AddAuthentication(AuthenticationMethod.Jwt, "secret")
    .AddRateLimit(1000, burstSize: 50)
    .AddFilter<MetricsFilter>(f => f.Labels.Add("service", "api"))
    .AddFilter<AccessLogFilter>()
    .Sequential()
    .Build();

await chain.ProcessAsync(message);
```

### 5.3 MCP Integration

```csharp
using GopherMcp.Transport;
using GopherMcp.Integration;

// Create transport with filters
var transport = new GopherTransport(new TransportConfig
{
    Protocol = TransportProtocol.Tcp,
    Host = "localhost",
    Port = 8080,
    Filters = new FilterManagerConfig
    {
        Security = new SecurityConfig
        {
            Authentication = new AuthenticationConfig
            {
                Enabled = true,
                Method = AuthenticationMethod.Jwt,
                Secret = "server-secret"
            }
        }
    }
});

// Create MCP server
var server = new McpServer("calculator-server", "1.0.0");
server.AddTool(new CalculatorTool());

// Start server with transport
await transport.StartAsync();
await server.ListenAsync(transport);
```

### 5.4 Custom Filter Implementation

```csharp
public class CustomValidationFilter : Filter
{
    private readonly ValidationConfig config;

    public CustomValidationFilter(ValidationConfig config) 
        : base(config)
    {
        this.config = config;
    }

    protected override async Task<FilterResult> ProcessAsync(
        FilterBuffer input,
        CancellationToken cancellationToken)
    {
        var message = DeserializeMessage(input);
        
        // Custom validation logic
        if (!IsValid(message))
        {
            return FilterResult.Error(
                FilterError.InvalidInput,
                "Validation failed");
        }

        // Continue processing
        return FilterResult.Continue(input);
    }

    private bool IsValid(JsonRpcMessage message)
    {
        // Custom validation logic
        return message.Method != null && 
               message.Params != null;
    }
}
```

## 6. Key Design Principles

1. **Resource Safety**: All native resources wrapped in SafeHandle
2. **Async-First**: All I/O operations are async
3. **Fluent API**: Builder pattern for intuitive configuration
4. **Type Safety**: Strong typing throughout the API
5. **Extensibility**: Easy to add custom filters and transports
6. **Performance**: Zero-copy operations where possible
7. **Error Handling**: Comprehensive exception hierarchy
8. **Testability**: Interfaces and dependency injection
9. **Documentation**: XML comments on all public APIs
10. **Cross-Platform**: Support for Windows, Linux, macOS

## 7. Performance Considerations

- **Memory Pooling**: Reuse buffers to reduce GC pressure
- **Async I/O**: Non-blocking operations for scalability
- **Native Interop**: Minimize marshaling overhead
- **Caching**: Cache frequently used configurations
- **Lazy Initialization**: Defer resource creation until needed
- **Batch Processing**: Process multiple messages in one call

## 8. Error Handling Strategy

```csharp
namespace GopherMcp.Types
{
    public class McpException : Exception
    {
        public FilterError ErrorCode { get; }
        public McpException(FilterError errorCode, string message) 
            : base(message) 
        {
            ErrorCode = errorCode;
        }
    }

    public class FilterException : McpException { }
    public class ChainException : McpException { }
    public class TransportException : McpException { }
    public class ConfigurationException : McpException { }
}
```

## 9. Testing Strategy

- **Unit Tests**: Test each component in isolation
- **Integration Tests**: Test filter chains and transport
- **Performance Tests**: Benchmark critical paths
- **Memory Tests**: Check for leaks and proper disposal
- **Cross-Platform Tests**: Test on all supported platforms

This design provides a comprehensive, production-ready C# SDK for the MCP Filter system with strong typing, resource safety, and excellent developer experience.