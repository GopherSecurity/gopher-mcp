# Windows Cross-Compilation Support

This directory contains files for cross-compiling libgopher-mcp for Windows.

## Build Scripts

### Windows x64 (AMD64)
```bash
./docker-mcp/build-windows-x64.sh
```

Uses MinGW-w64 to cross-compile for 64-bit Windows (x86_64).

### Windows ARM64
```bash
./docker-mcp/build-windows-arm64.sh
```

Uses LLVM-MinGW to cross-compile for Windows on ARM64 devices (Surface Pro X, Windows Dev Kit 2023, etc.).

For a faster stub build (testing only):
```bash
./docker-mcp/build-windows-arm64.sh --stub
```

## Output

Built files are placed in:
- `build-output/windows-x64/` for x64 builds
- `build-output/windows-arm64/` for ARM64 builds

### Output Contents
- `gopher-mcp.dll` - Main MCP library
- `gopher_mcp_c.dll` - C API library (for FFI bindings)
- `*.lib` - Import libraries for linking
- `verify_mcp.exe` - Verification tool
- `include/` - Header files

## Dependencies

The Windows builds require:
- **OpenSSL** - Pre-built or cross-compiled
- **libevent** - Cross-compiled for Windows

These are automatically downloaded and built in the Docker containers.

## Testing on Windows

1. Copy the entire `build-output/windows-x64/` (or `windows-arm64/`) directory to a Windows machine
2. Run `verify_mcp.exe` to test the libraries
3. Use the DLLs in your application

## Notes

- HTTP/2 support (nghttp2) is disabled in Windows builds
- llhttp support is disabled in Windows builds
- The libraries use Windows native threading (Win32 threads)
- SSL/TLS is provided by OpenSSL (included DLLs)

## Troubleshooting

### Missing DLL errors
Ensure all DLLs are in the same directory or in the system PATH.

### Architecture mismatch
Make sure you're using the correct build for your Windows architecture:
- x64 builds only work on 64-bit Windows (x86_64)
- ARM64 builds only work on Windows ARM64 devices

### Dependency checking
Use `dumpbin /dependents gopher-mcp.dll` on Windows to see required dependencies.
