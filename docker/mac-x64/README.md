# macOS x64 Support Files

This directory contains support files for the macOS x64 build.

## Files

- `info.sh` - Display library information
- `build_verify.sh` - Build and run verification
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

## Output Directory

The main output files are in `../../build-output/mac-x64/`:
- `libgopher_mcp_auth.0.1.0.dylib` - The authentication library
- `verify_auth` - Verification tool
