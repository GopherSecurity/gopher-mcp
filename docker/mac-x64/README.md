# macOS x64 Support Files

This directory contains support files for the macOS x64 build.

## Files

- `info.sh` - Display library information
- `build_verify.sh` - Build and run verification
- `verify_auth_c` - C version for macOS 10.14.6 compatibility
- `include/` - Header files (for reference)

## Usage

To get library info:
```bash
./info.sh ../../build-output/mac-x64/libgopher_mcp_auth.0.1.0.dylib
```

To verify the library:
```bash
./build_verify.sh
```

For macOS 10.14.6 compatibility issues, use the C version:
```bash
cd ../../build-output/mac-x64
../../docker/mac-x64/verify_auth_c
```

## Output Directory

The main output files are in `../../build-output/mac-x64/`:
- `libgopher_mcp_auth.0.1.0.dylib` - The authentication library
- `libgopher_mcp_auth.dylib` - Symlink for compatibility
- `verify_auth` - Verification tool (C++ version)

## macOS 10.14.6 Compatibility

If `verify_auth` fails with "dyld: cannot load" error on macOS 10.14.6,
use the C version (`verify_auth_c`) from this support directory instead.
