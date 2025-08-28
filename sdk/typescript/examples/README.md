# MCP Filter SDK Examples

This directory contains examples demonstrating how to use the MCP Filter SDK, including support for static libraries.

## Examples

### 1. Basic FFI Bindings Demo

**File**: `ffi-bindings.ts`

Demonstrates the SDK's automatic library detection and fallback behavior:

```bash
# Run the demo
npx tsx examples/ffi-bindings.ts
```

This example shows:

- SDK initialization and shutdown
- Filter, buffer, and memory pool creation
- Automatic fallback to mock implementation when native library is unavailable
- Resource cleanup and statistics

### 2. Static Library Support

**Files**:

- `wrapper.c` - C wrapper for creating dynamic libraries from static libraries
- `build-wrapper.sh` - Build script for creating wrapper libraries

## Using Static Libraries (.a files)

The SDK now supports static libraries, but requires a dynamic wrapper since FFI libraries cannot directly load static libraries.

### Quick Start

1. **Place your static library** in the `examples/` directory:

   ```bash
   cp /path/to/your/libgopher-mcp.a examples/
   ```

2. **Build the wrapper library**:

   ```bash
   cd examples
   ./build-wrapper.sh
   ```

3. **The SDK will automatically detect and use the wrapper library**

### Manual Build Commands

#### macOS

```bash
gcc -shared -fPIC -o ../build/libgopher-mcp.dylib wrapper.c -L. -l:libgopher-mcp.a -Wl,-undefined,dynamic_lookup
```

#### Linux

```bash
gcc -shared -fPIC -o ../build/libgopher-mcp.so wrapper.c -L. -l:libgopher-mcp.a
```

#### Windows

```bash
cl.exe /LD wrapper.c libgopher-mcp.lib /Fe:../build/gopher-mcp.dll
```

### How It Works

1. **Static Library Detection**: The SDK automatically detects static libraries (`.a`, `.lib`) in the build directory
2. **Wrapper Creation**: The wrapper creates a dynamic library that links to your static library
3. **FFI Loading**: The SDK loads the wrapper dynamic library using koffi
4. **Function Calls**: All function calls go through the wrapper to your static library

### Customizing the Wrapper

The `wrapper.c` file contains forward declarations for all MCP Filter API functions. You may need to:

1. **Update function signatures** to match your actual static library
2. **Add missing functions** that your library provides
3. **Remove unused functions** to reduce wrapper size

### Troubleshooting

#### Build Errors

- **Missing compiler**: Install GCC (Linux/macOS) or Visual Studio (Windows)
- **Library not found**: Ensure your static library is in the examples directory
- **Symbol errors**: Check that function signatures in wrapper.c match your library

#### Runtime Errors

- **Library not loaded**: Check that the wrapper library was built successfully
- **Function not found**: Verify all required functions are exported in wrapper.c
- **Memory errors**: Ensure proper cleanup in your application

## Advanced Usage

### Custom Wrapper Functions

You can add custom functionality to the wrapper:

```c
// Add custom initialization
__attribute__((visibility("default")))
int mcp_custom_init(const char* config) {
    // Your custom initialization logic
    return mcp_init_impl(NULL);
}
```

### Platform-Specific Optimizations

The wrapper can include platform-specific optimizations:

```c
#ifdef __APPLE__
    // macOS-specific code
#elif defined(__linux__)
    // Linux-specific code
#elif defined(_WIN32)
    // Windows-specific code
#endif
```

### Error Handling

Add custom error handling in the wrapper:

```c
__attribute__((visibility("default")))
int mcp_filter_create_safe(int dispatcher, void* config) {
    if (!config) {
        return -1; // Invalid argument
    }

    int result = mcp_filter_create_impl(dispatcher, config);
    if (result < 0) {
        // Log error or handle failure
        fprintf(stderr, "Filter creation failed: %d\n", result);
    }

    return result;
}
```

## Next Steps

1. **Build your static library** using your preferred build system
2. **Customize the wrapper** to match your library's API
3. **Test the wrapper** with the SDK examples
4. **Deploy** the wrapper library with your application

The SDK will automatically use the real library when available, providing full performance benefits while maintaining the development-friendly mock fallback.
