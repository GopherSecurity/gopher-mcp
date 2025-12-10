# macOS x64 Build Files

This directory contains source files and utilities for building libgopher_mcp_auth on macOS x64.

## Files

- `verify_auth_safe.c` - Safe verification tool that loads the library and checks symbols without calling them
- `verify_auth_full.c` - Full verification tool that tests all functions (may crash if dependencies are missing)
- `verify_auth.c` - Simple verification tool that only checks file existence (fallback)
- `verify_auth.sh` - Shell script verification tool (for environments where compiled binaries fail)
- `README.md` - This file

## Building

To build the library and verification tool, run from the project root:

```bash
./docker/build-mac-x64.sh
```

## Output

The build process creates the following files in `build-output/mac-x64/`:

- `libgopher_mcp_auth.0.1.0.dylib` - The authentication library
- `libgopher_mcp_auth.dylib` - Symlink for compatibility
- `verify_auth` - Verification tool (macOS 10.14.6+ compatible)

## Compatibility

- Target: macOS 10.14+ (Mojave and later)
- Architecture: x86_64

### Verification Tools

The build script will try to build verification tools in this order:

1. **`verify_auth_safe.c`** (Preferred)
   - Loads the library using dlopen
   - Verifies all exported functions exist
   - Checks symbol addresses
   - Tests safe functions only (get_last_error)
   - Checks for common dependencies
   - Won't crash even if dependencies are missing

2. **`verify_auth_full.c`** (Comprehensive but may crash)
   - Loads the library using dlopen
   - Verifies all exported functions exist
   - Tests basic functionality (create/destroy client)
   - Tests error handling functions
   - May segfault if library dependencies are missing

3. **`verify_auth.c`** (Basic fallback)
   - Only checks file existence using stat/access
   - Doesn't load the library
   - Useful for basic verification

4. **`verify_auth.sh`** (Emergency fallback)
   - Shell script for environments with severe restrictions
   - Uses only POSIX shell commands
   - Cannot test library functionality

### Note on macOS 10.14.6 Compatibility

If you encounter "Killed: 9" errors on macOS 10.14.6, this is likely due to security settings on that system rather than the verification app itself. The build script uses `MACOSX_DEPLOYMENT_TARGET=10.14` to ensure compatibility.