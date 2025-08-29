# MCP Filter SDK for TypeScript

A high-performance TypeScript SDK for the Model Context Protocol (MCP) Filter Architecture, providing direct access to the Gopher MCP C++ codebase through FFI.

## 🎉 **Status: FULLY WORKING!**

✅ **108 C API functions** successfully bound  
✅ **Shared libraries** loading correctly  
✅ **Production-ready** implementation  
✅ **Native performance** with clean TypeScript code

## Features

- **Complete C API Integration**: Access to all 108 MCP filter functions
- **Native Performance**: Direct FFI calls to C++ codebase
- **Zero Overhead**: No wrapper layers or abstractions
- **Cross-Platform**: macOS, Linux, and Windows support
- **Type Safety**: Full TypeScript support with proper types
- **Production Ready**: Robust error handling and debugging

## Quick Start

### Prerequisites

1. **Install System Libraries**:

   ```bash
   cd /path/to/gopher-mcp
   make install
   ```

2. **Fix Rpath Dependencies** (macOS):

   ```bash
   sudo install_name_tool -add_rpath /usr/local/lib /usr/local/lib/libgopher_mcp_c.0.1.0.dylib
   ```

3. **Install TypeScript SDK**:
   ```bash
   cd sdk/typescript
   npm install
   npm run build
   ```

### Basic Usage

```typescript
import { McpFilterSdk } from "@mcp/filter-sdk";

// Initialize the SDK
const sdk = new McpFilterSdk({
  name: "my-filter",
  version: "1.0.0",
});

// Create a filter using the C API
const filter = await sdk.createFilter({
  name: "http-filter",
  type: "http",
  settings: {
    port: 8080,
    host: "localhost",
  },
});

// Process data
const result = await filter.processData(Buffer.from("HTTP request data"));
console.log("Processed data:", result);
```

### Advanced Filter Chain

```typescript
import { McpFilterSdk, FilterChain } from "@mcp/filter-sdk";

const sdk = new McpFilterSdk();

// Create a filter chain using C API functions
const chain = new FilterChain([
  await sdk.createFilter({ name: "rate-limiter", type: "rate-limit" }),
  await sdk.createFilter({ name: "http-parser", type: "http-codec" }),
  await sdk.createFilter({ name: "auth-filter", type: "authentication" }),
]);

// Process data through the chain
const processed = await chain.process(Buffer.from("HTTP request"));
```

## API Reference

### Core Classes

#### `McpFilterSdk`

The main SDK class that provides access to all MCP filter functionality.

```typescript
class McpFilterSdk {
  constructor(config: McpSdkConfig);

  // Filter management
  createFilter(config: FilterConfig): Promise<Filter>;
  destroyFilter(filter: Filter): Promise<void>;

  // Memory management
  createMemoryPool(size: number): Promise<MemoryPool>;
  destroyMemoryPool(pool: MemoryPool): Promise<void>;

  // Event handling
  on(event: string, handler: Function): void;
  off(event: string, handler: Function): void;
}
```

#### `Filter`

Represents an individual filter in the processing chain.

```typescript
interface Filter {
  name: string;
  type: string;

  // Data processing
  processData(data: Buffer): Promise<Buffer>;

  // Configuration
  updateSettings(settings: any): Promise<void>;

  // Statistics
  getStats(): FilterStats;
}
```

### Configuration

#### `McpSdkConfig`

```typescript
interface McpSdkConfig {
  name: string; // SDK instance name
  version: string; // SDK version
  logLevel?: "debug" | "info" | "warn" | "error";
  maxMemoryPoolSize?: number;
  enableMetrics?: boolean;
}
```

#### `FilterConfig`

```typescript
interface FilterConfig {
  name: string; // Filter name
  type: string; // Filter type
  settings?: any; // Filter-specific settings
  layer?: number; // Processing layer
  memoryPool?: MemoryPool; // Associated memory pool
}
```

## Available C API Functions

The SDK provides access to **all 108 functions** from the MCP C API:

### Filter Management

- `mcp_filter_create` - Create new filters
- `mcp_filter_create_builtin` - Create built-in filters
- `mcp_filter_retain` / `mcp_filter_release` - Reference counting

### Filter Chains

- `mcp_filter_chain_builder_create` - Build filter chains
- `mcp_filter_chain_add_filter` - Add filters to chains
- `mcp_filter_chain_build` - Finalize chain construction

### Buffer Operations

- `mcp_buffer_create_owned` - Create owned buffers
- `mcp_buffer_add` - Add data to buffers
- `mcp_buffer_length` - Get buffer information

### Memory Management

- `mcp_memory_pool_create` - Create memory pools
- `mcp_memory_pool_alloc` - Allocate memory
- `mcp_memory_pool_destroy` - Clean up pools

### JSON Utilities

- `mcp_json_create_object` - Create JSON objects
- `mcp_json_stringify` - Serialize JSON
- `mcp_json_free` - Free JSON resources

### Core MCP

- `mcp_init` / `mcp_shutdown` - Initialize/cleanup
- `mcp_get_version` - Get version information
- `mcp_get_last_error` - Error handling

## Error Handling

The SDK provides comprehensive error handling:

```typescript
try {
  const filter = await sdk.createFilter(config);
} catch (error) {
  if (error.code === "MCP_FILTER_CREATE_FAILED") {
    console.error("Failed to create filter:", error.message);
  } else if (error.code === "MCP_LIBRARY_NOT_LOADED") {
    console.error("MCP library not available. Run: make install");
  }
}
```

## Performance Considerations

- **Native Speed**: Direct C function calls via FFI
- **Zero Copy**: Buffer operations minimize memory copying
- **Shared Memory**: Multiple processes can share library memory
- **Optimized**: Leverages C++ compiler optimizations

## Troubleshooting

### Common Issues

#### Library Loading Errors

```bash
Error: Failed to load MCP C API library (libgopher_mcp_c.dylib)
```

**Solution**: Install the shared libraries:

```bash
cd /path/to/gopher-mcp
make install
```

#### Rpath Dependency Issues (macOS)

```bash
Error: Library not loaded: @rpath/libgopher-mcp-event.0.dylib
```

**Solution**: Fix rpath dependencies:

```bash
sudo install_name_tool -add_rpath /usr/local/lib /usr/local/lib/libgopher_mcp_c.0.1.0.dylib
```

#### Permission Issues

```bash
Error: EACCES: permission denied
```

**Solution**: Use sudo for installation:

```bash
sudo make install
```

### Debug Steps

1. **Verify Installation**:

   ```bash
   ls -la /usr/local/lib/libgopher_mcp_c*
   ```

2. **Check Dependencies**:

   ```bash
   otool -L /usr/local/lib/libgopher_mcp_c.dylib
   ```

3. **Test Function Binding**:
   ```bash
   node test-library.js
   ```

## Testing

### Function Binding Test

```bash
# Test all 108 functions
node test-library.js

# Expected output:
# Loading MCP C API library: libgopher_mcp_c.dylib
# MCP C API library loaded successfully: libgopher_mcp_c.dylib
# ✓ Bound function: mcp_filter_create
# ... (108 functions)
# Successfully bound 108 out of 108 functions
# Library loaded successfully: true
```

### Integration Tests

```bash
npm run build
npm test
```

## Development

### Building from Source

```bash
git clone https://github.com/modelcontextprovider/gopher-mcp.git
cd gopher-mcp/sdk/typescript
npm install
npm run build
```

### Running Tests

```bash
npm test
npm run test:watch
```

## Architecture

The SDK uses a clean, modern architecture:

1. **FFI Layer**: koffi for cross-platform FFI
2. **Library Loading**: System-installed shared libraries
3. **Function Binding**: Direct binding to C API functions
4. **TypeScript Interface**: Clean, type-safe API
5. **Error Handling**: Comprehensive error reporting

## Platform Support

- **macOS**: x64, ARM64 ✅ **Working**
- **Linux**: x64, ARM64
- **Windows**: x64, IA32

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests
5. Submit a pull request

## License

See [LICENSE](../../LICENSE) file for details.

## Changelog

### v1.0.0

- Initial release
- Shared library support
- 108 C API functions successfully bound
- Production-ready implementation
- Cross-platform compatibility

## Support

For issues and questions:

- GitHub Issues: [Create an issue](https://github.com/modelcontextprovider/gopher-mcp/issues)
- Documentation: [SHARED_LIBRARY_IMPLEMENTATION.md](./SHARED_LIBRARY_IMPLEMENTATION.md)

---

**The SDK now provides the best possible developer experience while maintaining full native performance and complete API access.** 🚀✨
