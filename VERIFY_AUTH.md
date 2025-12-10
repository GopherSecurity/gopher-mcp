# libgopher_mcp_auth Library Verification

This document explains how to verify the standalone authentication library on different platforms.

## Overview

The `verify_auth` application is a simple C++ program that:
- Loads the library dynamically
- Verifies all exported functions are accessible
- Creates and destroys an authentication client
- Tests basic operations
- Reports success or failure

## Platform-Specific Instructions

### macOS (x86_64)

1. **Build the library:**
   ```bash
   ./docker/build-mac-x64.sh
   ```

2. **Build and run verification:**
   ```bash
   cd build-output/mac-x64
   ./build_verify.sh
   ./verify_auth
   ```

3. **Expected output:**
   ```
   ✓ Library loaded successfully
   ✓ All functions loaded
   ✓ Client created successfully
   ✓ All tests passed successfully!
   ```

### Linux (x86_64)

1. **Build the library:**
   ```bash
   ./docker/build-linux-x64.sh
   ```

2. **Build and run verification (using Docker):**
   ```bash
   cd build-output/linux-x64
   docker run --rm -v "$(pwd)":/app -w /app ubuntu:20.04 bash -c "
       apt-get update && apt-get install -y g++ libcurl4 libssl1.1
       g++ -std=c++11 -o verify_auth verify_auth.cc -ldl
       ./verify_auth
   "
   ```

3. **Or build locally (if on Linux):**
   ```bash
   cd build-output/linux-x64
   ./build_verify.sh
   ./verify_auth
   ```

### Windows (x64)

1. **Build the library (cross-compilation):**
   ```bash
   # Requires MinGW-w64 installed
   ./docker/build-windows-x64.sh
   ```

2. **On Windows, build and run verification:**
   ```batch
   cd build-output\windows-x64
   build_verify.bat
   verify_auth.exe
   ```

## Library Sizes

| Platform      | Library Size | File Extension |
|--------------|-------------|----------------|
| macOS x64    | ~161 KB     | .dylib        |
| Linux x64    | ~180 KB     | .so           |
| Windows x64  | ~200 KB*    | .dll          |

*Estimated size for Windows build

## Exported Functions

The library exports the following C API functions:

- `mcp_auth_client_create` - Create authentication client
- `mcp_auth_client_destroy` - Destroy authentication client  
- `mcp_auth_get_last_error` - Get global last error
- `mcp_auth_client_get_last_error` - Get client-specific error
- `mcp_auth_clear_crypto_cache` - Clear cryptographic cache
- `mcp_auth_validate_token` - Validate JWT tokens
- `mcp_auth_set_option` - Set client options

## Troubleshooting

### Library not found
- **macOS:** Set `DYLD_LIBRARY_PATH=.`
- **Linux:** Set `LD_LIBRARY_PATH=.`
- **Windows:** Ensure DLL is in same directory as EXE

### Missing dependencies
- **macOS:** Install OpenSSL via Homebrew
- **Linux:** Install libcurl4 and libssl1.1
- **Windows:** Visual C++ Redistributables may be required

### Compilation errors
- Ensure C++11 compatible compiler is available
- Check that development headers are installed

## Verification Source

The verification application source (`verify_auth.cc`) is located in the project root and is copied to each platform's build output directory during the build process.

## Requirements

### Build Requirements
- CMake 3.10+
- C++11 compatible compiler
- OpenSSL development files
- libcurl development files

### Runtime Requirements
- OpenSSL 1.1+ or 3.x
- libcurl 
- C++ standard library

## Cross-Compilation

The project supports cross-compilation:
- **Linux:** Uses Docker with Ubuntu 20.04 base image
- **Windows:** Uses MinGW-w64 toolchain (experimental)
- **macOS:** Native compilation only

## Distribution

Pre-built libraries can be distributed as:
- Individual platform archives (.tar.gz)
- Universal package with all platforms
- Integration into larger SDK distributions

Each distribution should include:
- The library file (.so/.dylib/.dll)
- Required headers (if using C++ API)
- This verification tool
- License information