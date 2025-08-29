# CMakeLists.txt Symbol Export Fix for FFI Usage

## Problem Description

The shared libraries (`libgopher_mcp_c.dylib`, `libgopher-mcp.dylib`) were not properly built for FFI usage. They were missing dynamic symbol tables, which prevented the TypeScript SDK from loading them via `koffi` (FFI library).

## Root Cause

The CMake configuration was using `-fvisibility=hidden` which hides all symbols by default, and the export mechanism wasn't working correctly on macOS, resulting in libraries that couldn't be loaded via FFI.

## Changes Made

### 1. Added Configuration Option

```cmake
# Configuration option for symbol export
option(EXPORT_ALL_SYMBOLS "Export all symbols for FFI usage" ON)
```

### 2. Updated Library Properties

```cmake
# Ensure symbols are exported for FFI usage
if(EXPORT_ALL_SYMBOLS)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # On macOS, ensure symbols are properly exported for dynamic loading
        set_target_properties(gopher_mcp_c PROPERTIES
            C_VISIBILITY_PRESET default
            CXX_VISIBILITY_PRESET default
        )
        message(STATUS "Symbol export enabled for FFI usage on macOS")
    endif()
else()
    message(STATUS "Symbol export disabled - library may not work with FFI")
endif()
```

### 3. Fixed macOS Symbol Export

```cmake
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # macOS uses different linker flags
    set_target_properties(gopher_mcp_c PROPERTIES
        LINK_FLAGS "-Wl,-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols"
    )
    # Generate comprehensive symbols list for macOS
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols" "# Export all MCP API symbols\n")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols" "mcp_*\n")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols" "_mcp_*\n")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/gopher_mcp_c.symbols" "MCP_*\n")
```

### 4. Conditional Visibility Settings

```cmake
# Unix/Linux/macOS: Use visibility attributes
if(EXPORT_ALL_SYMBOLS)
    target_compile_options(gopher_mcp_c PRIVATE -fvisibility=default)
    message(STATUS "Using default visibility for symbol export")
else()
    target_compile_options(gopher_mcp_c PRIVATE -fvisibility=hidden)
    message(STATUS "Using hidden visibility (may not work with FFI)")
endif()
```

### 5. Added FFI Support Status

```cmake
# ============================================================================
# FFI Support Configuration
# ============================================================================
message(STATUS "  FFI Support:")
message(STATUS "    Symbol Export: ${EXPORT_ALL_SYMBOLS}")
if(EXPORT_ALL_SYMBOLS)
    message(STATUS "    ✓ Library will be built with symbols exported for FFI usage")
    message(STATUS "    ✓ TypeScript SDK should be able to load the shared library")
else()
    message(STATUS "    ✗ Library will be built with hidden symbols (may not work with FFI)")
    message(STATUS "    ✗ TypeScript SDK may fail to load the shared library")
endif()
```

## How to Use

### Build with Symbol Export (Recommended for FFI)

```bash
cd /Users/divyanshingle/Project/modelcontextprovider/gopher/gopher-mcp
mkdir -p build && cd build
cmake .. -DEXPORT_ALL_SYMBOLS=ON
make -j$(nproc)
sudo make install
```

### Build without Symbol Export (Not recommended for FFI)

```bash
cmake .. -DEXPORT_ALL_SYMBOLS=OFF
```

## Expected Results

With `EXPORT_ALL_SYMBOLS=ON`:

- ✅ Shared libraries will have proper dynamic symbol tables
- ✅ TypeScript SDK will be able to load libraries via FFI
- ✅ All `mcp_*` functions will be accessible
- ✅ FFI bindings will work correctly

With `EXPORT_ALL_SYMBOLS=OFF`:

- ❌ Shared libraries will have hidden symbols
- ❌ TypeScript SDK will fail to load libraries
- ❌ FFI bindings will not work

## Verification

After building, verify the symbols are exported:

```bash
nm -D /usr/local/lib/libgopher_mcp_c.0.1.0.dylib | grep mcp_init
```

Should show:

```
0000000000001234 T _mcp_init
```

Instead of:

```
nm: error: File format has no dynamic symbol table
```

## Notes

- This fix only affects the C API library build
- No changes to the core C++ codebase functionality
- Maintains backward compatibility
- Provides clear feedback during build process
- Allows developers to choose between FFI compatibility and symbol hiding
