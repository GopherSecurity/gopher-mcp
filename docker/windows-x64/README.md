# Windows x64 Build Files

This directory contains source files and utilities for building libgopher_mcp_auth on Windows x86_64.

## Files

- `verify_auth.c` - Verification tool that loads the DLL and checks all functions
- `mcp_auth_stub.cc` - Stub implementation for testing (doesn't require OpenSSL/CURL)
- `CMakeLists.txt` - CMake configuration for stub build
- `CMakeLists-real.txt` - CMake configuration for real OpenSSL/CURL build
- `README.md` - This file

## Building

To build the library and verification tool, run from the project root:

```bash
./docker/build-windows-x64.sh
```

## Current Implementation

The current build uses a **stub implementation** (`mcp_auth_stub.cc`) that provides all the required exported functions but doesn't actually implement cryptography or networking. This is suitable for:
- Testing the build system
- Verifying DLL loading and function exports
- Development and prototyping

## Using Real OpenSSL and CURL

To build with real OpenSSL and CURL support:

### Option 1: Official Windows Binaries

1. **Download OpenSSL for Windows**:
   - From Shining Light Productions: https://slproweb.com/products/Win32OpenSSL.html
   - Download Win64 OpenSSL (not Light version)
   - Example: `Win64OpenSSL-3_0_12.exe`

2. **Download CURL for Windows**:
   - Official builds: https://curl.se/windows/
   - Download the MinGW version for compatibility
   - Example: `curl-win64-mingw.zip`

3. **Update Dockerfile**:
   - Add commands to download and extract these libraries
   - Update paths in `CMakeLists-real.txt`
   - Switch to use `CMakeLists-real.txt` instead of stub

### Option 2: vcpkg (Microsoft Package Manager)

```powershell
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat

# Install libraries
.\vcpkg\vcpkg install openssl:x64-windows
.\vcpkg\vcpkg install curl:x64-windows
```

### Option 3: Build from Source

For maximum compatibility, build OpenSSL and CURL from source using MinGW-w64.

## Output

The build process creates the following files in `build-output/windows-x64/`:

- `gopher_mcp_auth.dll` - The authentication library (18KB stub, ~500KB with real libs)
- `gopher_mcp_auth.lib` - Import library for linking
- `verify_auth.exe` - Verification tool

## Compatibility

- Target: Windows 7+ (Windows 7, 8, 8.1, 10, 11)
- Architecture: x86_64 / AMD64
- Runtime: MinGW-w64 (statically linked, no external dependencies)

## Cross-Compilation

The build uses Docker with MinGW-w64 for cross-compilation from Linux/macOS to Windows.

## Verification

The verification tool (`verify_auth.exe`):
- Loads the library using LoadLibrary
- Checks for all required exported functions
- Uses Windows-specific APIs for module information
- Tests multiple DLL naming conventions
- Checks for common Windows dependencies
- Works with both .dll naming conventions (with and without "lib" prefix)

## Production Deployment

For production use, you should:
1. Use the real OpenSSL and CURL libraries (not the stub)
2. Sign the DLL with a code signing certificate
3. Include required runtime DLLs (if dynamically linked)
4. Test on target Windows versions

## Troubleshooting

If the DLL fails to load:
1. Check for missing dependencies using Dependency Walker
2. Ensure Visual C++ Redistributables are installed (if needed)
3. Verify the architecture matches (x64 DLL on x64 Windows)