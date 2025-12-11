# Linux x64 Build Files

This directory contains source files and utilities for building libgopher_mcp_auth on Linux x86_64.

## Files

- `verify_auth.c` - Verification tool that loads the library and checks all functions
- `README.md` - This file

## Building

To build the library and verification tool, run from the project root:

```bash
./docker/build-linux-x64.sh
```

## Output

The build process creates the following files in `build-output/linux-x64/`:

- `libgopher_mcp_auth.so.0.1.0` - The authentication library
- `libgopher_mcp_auth.so.0` - Symlink for compatibility
- `libgopher_mcp_auth.so` - Symlink for development
- `verify_auth` - Verification tool

## Compatibility

- Target: Ubuntu 20.04+ (Focal Fossa and later)
- Architecture: x86_64 / amd64
- GLIBC: 2.31+ (Ubuntu 20.04 baseline)
- OpenSSL: 1.1.1+ or 3.0+
- GCC/G++: 9.3.0+ (C++11 support)

## Notes

The verification tool:
- Loads the library using dlopen with RTLD_LAZY for better compatibility
- Checks for both OpenSSL 1.1 and 3.0 (different Ubuntu versions)
- Uses Linux-specific dlinfo for library information
- Tests multiple library naming conventions (.so, .so.0, .so.0.1.0)