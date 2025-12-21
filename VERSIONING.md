# Version Configuration Guide

## Overview
The library version for `libgopher_mcp_auth` can be configured in multiple ways depending on your build method.

## Version Format
The project uses semantic versioning: `MAJOR.MINOR.PATCH` (e.g., `0.1.0`)

## Configuration Methods

### 1. Local Builds
Edit the version directly in `src/auth/CMakeLists.txt`:
```cmake
project(gopher-mcp-auth VERSION 0.1.0 LANGUAGES CXX)
```

### 2. GitHub Actions Workflow
When triggering the workflow manually:

1. Go to Actions → Build All Libraries
2. Click "Run workflow"
3. Enter the desired version (e.g., `0.2.0`)
4. Select build type (Release/Debug)
5. Run the workflow

The workflow will automatically:
- Update CMakeLists.txt with the new version
- Build all libraries with the specified version
- Tag artifacts with the version number

### 3. Command Line (Local)
Before building locally, update the version:
```bash
# For Linux/Windows (GNU sed)
sed -i "s/VERSION [0-9.]\+/VERSION 0.2.0/" src/auth/CMakeLists.txt

# For macOS (BSD sed)
sed -i '' "s/VERSION [0-9.]*/VERSION 0.2.0/" src/auth/CMakeLists.txt
```

Then run your build script:
```bash
./docker/build-linux-x64.sh
```

## Version Impact

The version number affects:

### Linux (.so files)
- `libgopher_mcp_auth.so.0.1.0` - Full version in filename
- `libgopher_mcp_auth.so.0` - Major version symlink (SONAME)
- `libgopher_mcp_auth.so` - Development symlink

### macOS (.dylib files)
- `libgopher_mcp_auth.0.1.0.dylib` - Full version in filename
- `libgopher_mcp_auth.0.dylib` - Major version symlink
- `libgopher_mcp_auth.dylib` - Development symlink

### Windows (.dll files)
- `gopher_mcp_auth.dll` - No version in filename (Windows convention)
- Version embedded in DLL resources (viewable in Properties)

## Version Compatibility

### ABI Compatibility Rules
- **PATCH**: Increment for bug fixes (backward compatible)
- **MINOR**: Increment for new features (backward compatible)
- **MAJOR**: Increment for breaking changes (not backward compatible)

### SONAME/Install Name
- Linux: SONAME includes major version (`libgopher_mcp_auth.so.0`)
- macOS: Install name includes major version (`@rpath/libgopher_mcp_auth.0.dylib`)
- Changing MAJOR version requires recompiling dependent applications

## Checking Current Version

### From Built Libraries

Linux:
```bash
# Check SONAME
readelf -d libgopher_mcp_auth.so | grep SONAME

# Check version symbols
strings libgopher_mcp_auth.so | grep -E "^[0-9]+\.[0-9]+\.[0-9]+$"
```

macOS:
```bash
# Check install name
otool -L libgopher_mcp_auth.dylib

# Check version info
otool -l libgopher_mcp_auth.dylib | grep -A2 LC_ID_DYLIB
```

Windows:
```powershell
# PowerShell - check file version
(Get-Item gopher_mcp_auth.dll).VersionInfo.FileVersion
```

### From Source
```bash
grep "VERSION" src/auth/CMakeLists.txt
```

## Release Process

1. Update version in `src/auth/CMakeLists.txt`
2. Commit the change
3. Push to `feature/gopher-auth-build` branch
4. GitHub Actions will automatically build all platforms
5. Download artifacts from Actions run
6. Tag the repository with the version number

## Version History

- **0.1.0** - Initial release with basic MCP authentication support