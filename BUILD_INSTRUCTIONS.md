# Build Instructions for libgopher_mcp_auth

## Overview
This document describes how to build the MCP Authentication Library for multiple platforms and architectures using Docker-based cross-compilation.

## Platform & Architecture Support Matrix

| Platform | x64/x86_64 | ARM64/aarch64 | Build Method |
|----------|------------|---------------|--------------|
| **macOS** | ✅ Intel Macs | ✅ Apple Silicon | Docker + Native tools |
| **Linux** | ✅ x86_64 | ✅ aarch64 | Docker cross-compilation |
| **Windows** | ✅ Windows 10/11 | ✅ Windows on ARM | Docker + MinGW/LLVM |

## Quick Start - Build Everything

### Build All Platforms and Architectures
```bash
# macOS - builds x64, ARM64, and Universal
./docker/build-mac-all.sh

# Linux - builds x64 and ARM64
./docker/build-linux-all.sh

# Windows - builds x64 and ARM64
./docker/build-windows-all.sh
```

---

## macOS Builds

### 1. Intel Macs (x86_64)
```bash
./docker/build-mac-x64.sh
```
**Output:** `build-output/mac-x64/`
- `libgopher_mcp_auth.0.1.0.dylib`
- `verify_auth` (test executable)

### 2. Apple Silicon Macs (ARM64)
```bash
# Native build on Apple Silicon
./docker/build-mac-arm64.sh

# Cross-compilation on Intel Mac
./docker/build-mac-arm64-cross.sh
```
**Output:** `build-output/mac-arm64/`

### 3. Universal Binary (Intel + Apple Silicon)
```bash
./docker/build-mac-universal.sh
```
**Output:** `build-output/mac-universal/`
- Single binary supporting both architectures

### Build All macOS Targets
```bash
./docker/build-mac-all.sh [x64|arm64|universal|all|clean]
```

---

## Linux Builds

### 1. Linux x64
```bash
./docker/build-linux-x64.sh
```
**Output:** `build-output/linux-x64/`
- `libgopher_mcp_auth.so.0.1.0`
- `verify_auth` (test executable)
- TypeScript SDK files

### 2. Linux ARM64
```bash
./docker/build-linux-arm64.sh
```
**Output:** `build-output/linux-arm64/`
- ARM64 shared library for Raspberry Pi, AWS Graviton, etc.

### Testing Linux Builds
```bash
# On target Linux system
cd build-output/linux-{arch}/
LD_LIBRARY_PATH=. ./verify_auth
```

---

## Windows Builds

### 1. Windows x64 (Standard 64-bit)
```bash
./docker/build-windows-x64.sh
```
**Output:** `build-output/windows-x64/`
- `gopher_mcp_auth.dll`
- `gopher_mcp_auth.lib` (import library)
- `verify_auth.exe`

### 2. Windows ARM64 (Windows on ARM)
```bash
# Full ARM64 build with LLVM-MinGW
./docker/build-windows-arm64.sh

# Quick stub build for testing
./docker/build-windows-arm64.sh --stub
```
**Output:** `build-output/windows-arm64/`
- ARM64 DLL for Surface Pro X, Windows Dev Kit, etc.

### 3. Build All Windows Architectures
```bash
./docker/build-windows-all.sh
```
- Builds both x64 and ARM64 in parallel

### Windows Build Notes
- Uses Windows native APIs (CryptoAPI, WinHTTP) instead of OpenSSL/CURL
- No external DLL dependencies
- ARM64 builds use LLVM-MinGW toolchain

---

## Build Output Structure

### macOS
```
build-output/mac-{x64|arm64|universal}/
├── libgopher_mcp_auth.0.1.0.dylib  # Main library
├── libgopher_mcp_auth.dylib        # Symlink
├── verify_auth                     # Test executable
└── typescript/                     # TypeScript SDK
```

### Linux
```
build-output/linux-{x64|arm64}/
├── libgopher_mcp_auth.so.0.1.0    # Main library
├── libgopher_mcp_auth.so.0        # Symlink
├── libgopher_mcp_auth.so          # Symlink
├── verify_auth                    # Test executable
└── typescript/                    # TypeScript SDK
```

### Windows
```
build-output/windows-{x64|arm64}/
├── gopher_mcp_auth.dll           # Main library
├── gopher_mcp_auth.lib           # Import library
├── verify_auth.exe               # Test executable
└── typescript/                   # TypeScript SDK
```

---

## Architecture Verification

### macOS
```bash
# Check architecture
lipo -info build-output/mac-*/libgopher_mcp_auth.0.1.0.dylib
# Output: x86_64, arm64, or both
```

### Linux
```bash
# Check architecture
file build-output/linux-*/libgopher_mcp_auth.so.0.1.0
# Output: ELF 64-bit LSB shared object, x86-64 or ARM aarch64
```

### Windows
```bash
# Check architecture (on macOS/Linux with 'file' command)
file build-output/windows-*/gopher_mcp_auth.dll
# Output: PE32+ executable, x86-64 or Aarch64
```

---

## Platform-Specific Requirements

### macOS Requirements
- Docker Desktop for Mac
- Xcode Command Line Tools
- OpenSSL (via Homebrew)
  - Intel: `/usr/local/lib`
  - Apple Silicon: `/opt/homebrew/lib`

### Linux Build Requirements
- Docker (with buildx for ARM64 cross-compilation)
- No special host requirements (all handled in Docker)

### Windows Build Requirements
- Docker
- Builds use MinGW-w64 (x64) and LLVM-MinGW (ARM64)
- No Windows machine required (cross-compilation from macOS/Linux)

---

## Cross-Compilation Features

### Linux ARM64 on x64 Host
- Automatic using Docker's `--platform linux/arm64`
- Uses pre-built GCC image for fast builds

### Windows on macOS/Linux
- Full cross-compilation support
- x64: MinGW-w64
- ARM64: LLVM-MinGW toolchain

### macOS ARM64 on Intel
- Uses `build-mac-arm64-cross.sh`
- Downloads and builds OpenSSL for ARM64
- Static linking to avoid dependency issues

---

## Testing Built Libraries

### macOS
```bash
cd build-output/mac-{arch}/
./verify_auth
```

### Linux
```bash
cd build-output/linux-{arch}/
LD_LIBRARY_PATH=. ./verify_auth
```

### Windows (copy to Windows machine)
```cmd
cd build-output\windows-{arch}\
verify_auth.exe
```

---

## Clean Build Directories

### All Platforms
```bash
# Clean all build outputs
rm -rf build-output/

# Platform-specific clean
./docker/build-mac-all.sh clean        # macOS
rm -rf build-output/linux-*            # Linux
rm -rf build-output/windows-*          # Windows
```

---

## Troubleshooting

### Common Issues

1. **Docker not running**
   - Start Docker Desktop (macOS/Windows)
   - Start Docker daemon (Linux): `sudo systemctl start docker`

2. **ARM64 builds slow**
   - First build downloads dependencies
   - Subsequent builds use cached layers

3. **OpenSSL not found (macOS)**
   - Intel: `brew install openssl@1.1`
   - Apple Silicon: `/opt/homebrew/bin/brew install openssl@1.1`

4. **Windows ARM64 toolchain download**
   - First build downloads ~100MB LLVM-MinGW
   - Use `--stub` flag for quick testing

### Architecture Mismatch
- Ensure you're using the correct binary for your system
- x64 binaries won't run on ARM64 (and vice versa)
- Universal binaries (macOS) work on both

---

## CI/CD Recommendations

### Build Matrix
```yaml
# Example GitHub Actions matrix
strategy:
  matrix:
    include:
      - os: macos-latest
        arch: [x64, arm64, universal]
      - os: ubuntu-latest
        arch: [x64, arm64]
      - os: windows-latest
        arch: [x64, arm64]
```

### Parallel Builds
- Use `build-mac-all.sh` for macOS
- Use `build-windows-all.sh` for Windows
- Run Linux builds in parallel

---

## Distribution Guidelines

### macOS
- **Recommended:** Universal binary for maximum compatibility
- Sign and notarize for distribution

### Linux
- Provide separate x64 and ARM64 packages
- Include appropriate `.so` version symlinks

### Windows
- Distribute x64 for standard Windows
- Provide ARM64 for Windows on ARM devices
- Include `.lib` files for development

---

## Performance Notes

| Platform | Architecture | Build Time | Notes |
|----------|-------------|------------|-------|
| macOS | x64 | ~1 min | Native compilation |
| macOS | ARM64 | ~2-3 min | Cross-compile on Intel |
| macOS | Universal | ~3-4 min | Builds both architectures |
| Linux | x64 | ~1 min | Docker native |
| Linux | ARM64 | ~2 min | Docker cross-platform |
| Windows | x64 | ~1 min | MinGW cross-compile |
| Windows | ARM64 | ~3-5 min | LLVM-MinGW download + build |

---

## Security Notes

- All builds use official Docker base images
- Windows builds use native Windows APIs (no OpenSSL dependency)
- Static linking available for deployment scenarios
- No runtime dependencies on Windows