# Build Instructions for MCP C++ SDK

## Overview
This document describes how to build the MCP C++ SDK and Authentication Library (`libgopher_mcp_auth`) for multiple platforms and architectures using Docker-based cross-compilation and GitHub Actions CI/CD.

## Platform & Architecture Support Matrix

| Platform | x64/x86_64 | ARM64/aarch64 | Build Method |
|----------|------------|---------------|--------------|
| **macOS** | ✅ Intel Macs | ✅ Apple Silicon | Docker + Native tools |
| **Linux** | ✅ x86_64 | ✅ aarch64 | Docker cross-compilation |
| **Windows** | ✅ Windows 10/11 | ✅ Windows on ARM | Docker + MinGW/LLVM |

## Quick Start - Build Everything

### Build All Platforms and Architectures
```bash
# macOS - builds x64 and ARM64
./docker/build-mac-all.sh

# Linux - builds x64 and ARM64
./docker/build-linux-all.sh

# Windows - builds x64 and ARM64
./docker/build-windows-all.sh
```

### GitHub Actions - Automated Builds
```bash
# Trigger workflow manually from GitHub UI
# Go to Actions → Build All Libraries → Run workflow

# Or trigger via GitHub CLI
gh workflow run build-all.yml \
  --field build_type=Release \
  --field version=0.1.0 \
  --field create_release=true
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
```
**Output:** `build-output/mac-arm64/`

### Build All macOS Targets
```bash
./docker/build-mac-all.sh [x64|arm64|all|clean]
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
- Note: Cross-compilation from Intel to ARM64 requires proper toolchain setup
- Recommended: Use GitHub Actions for ARM64 builds (uses native runners)
- Alternative: Build on actual Apple Silicon hardware

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

## GitHub Actions CI/CD

### Workflow Configuration
The project includes a comprehensive GitHub Actions workflow at `.github/workflows/build-all.yml` that automatically builds for all platforms and architectures.

### Supported Build Matrix
```yaml
jobs:
  build-linux:
    strategy:
      matrix:
        arch: [x64, arm64]
    runs-on: ubuntu-latest
    
  build-windows:
    strategy:
      matrix:
        arch: [x64, arm64]
    runs-on: ubuntu-latest  # Cross-compilation
    
  build-macos:
    strategy:
      matrix:
        include:
          - arch: x64
            runner: macos-15-intel
          - arch: arm64
            runner: macos-15  # Apple Silicon runner
    runs-on: ${{ matrix.runner }}
```

### Workflow Features

#### 1. Manual Trigger with Parameters
```yaml
workflow_dispatch:
  inputs:
    build_type:
      description: 'Build type'
      default: 'Release'
      options: [Release, Debug]
    version:
      description: 'Library version'
      default: '0.1.0'
    create_release:
      description: 'Create GitHub Release'
      default: true
```

#### 2. Automatic Builds
- **Push to branch**: Triggers on push to `feature/gopher-auth-build`
- **Pull requests**: Can be configured for PR validation
- **Scheduled**: Can add cron schedule for nightly builds

#### 3. Build Artifacts
- Each platform/architecture combination produces artifacts
- Artifacts are uploaded and retained for 7 days
- Release builds create downloadable archives

#### 4. GitHub Release Creation
- Automatically creates releases with all platform binaries
- Tagged with version and timestamp
- Includes build report with platform details

### Running GitHub Actions

#### Via GitHub UI
1. Go to the repository on GitHub
2. Click on "Actions" tab
3. Select "Build All Libraries" workflow
4. Click "Run workflow"
5. Fill in parameters:
   - Build type: Release or Debug
   - Version: e.g., 0.1.0
   - Create release: true/false
6. Click "Run workflow"

#### Via GitHub CLI
```bash
# Install GitHub CLI if needed
brew install gh  # macOS
# or see https://cli.github.com/

# Authenticate
gh auth login

# Run workflow with default parameters
gh workflow run build-all.yml

# Run with custom parameters
gh workflow run build-all.yml \
  --field build_type=Release \
  --field version=0.2.0 \
  --field create_release=true

# View workflow runs
gh run list --workflow=build-all.yml

# Watch a specific run
gh run watch
```

### Workflow Jobs Detail

#### Linux Builds (x64 & ARM64)
- Uses Ubuntu latest runner
- Docker with QEMU for ARM64 emulation
- Produces `.so` shared libraries
- Includes verification tools

#### Windows Builds (x64 & ARM64)
- Cross-compilation from Linux
- MinGW-w64 for x64
- LLVM-MinGW for ARM64
- Produces `.dll` and `.lib` files
- No external dependencies (uses Windows APIs)

#### macOS Builds (x64 & ARM64)
- **x64**: Runs on `macos-15-intel` runner
- **ARM64**: Runs on `macos-15` (Apple Silicon) runner
- Native compilation for best performance
- Produces `.dylib` libraries
- Includes code signing preparation

### Build Output from GitHub Actions

#### Artifacts Structure
```
artifacts/
├── linux-x64-libs/
│   ├── libgopher_mcp_auth.so.0.1.0
│   └── verify_auth
├── linux-arm64-libs/
│   └── ...
├── windows-x64-libs/
│   ├── gopher_mcp_auth.dll
│   └── gopher_mcp_auth.lib
├── windows-arm64-libs/
│   └── ...
├── macos-x64-libs/
│   ├── libgopher_mcp_auth.0.1.0.dylib
│   └── verify_auth
└── macos-arm64-libs/
    └── ...
```

#### Release Archives
- `libgopher_mcp_auth-linux-x64.tar.gz`
- `libgopher_mcp_auth-linux-arm64.tar.gz`
- `libgopher_mcp_auth-windows-x64.zip`
- `libgopher_mcp_auth-windows-arm64.zip`
- `libgopher_mcp_auth-macos-x64.tar.gz`
- `libgopher_mcp_auth-macos-arm64.tar.gz`

### Parallel Builds
- All platform builds run in parallel
- Typical total workflow time: 10-15 minutes
- Individual job times:
  - Linux: ~3-5 minutes
  - Windows: ~3-5 minutes
  - macOS: ~5-7 minutes

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

### Local Build Times
| Platform | Architecture | Build Time | Notes |
|----------|-------------|------------|-------|
| macOS | x64 | ~1 min | Native compilation |
| macOS | ARM64 | ~1-2 min | Native on Apple Silicon |
| Linux | x64 | ~1 min | Docker native |
| Linux | ARM64 | ~2 min | Docker cross-platform |
| Windows | x64 | ~1 min | MinGW cross-compile |
| Windows | ARM64 | ~3-5 min | LLVM-MinGW download + build |

### GitHub Actions Build Times
| Platform | Architecture | Build Time | Notes |
|----------|-------------|------------|-------|
| Linux | x64 | ~2-3 min | Ubuntu runner |
| Linux | ARM64 | ~3-4 min | QEMU emulation |
| Windows | x64 | ~2-3 min | Cross-compilation |
| Windows | ARM64 | ~4-5 min | LLVM toolchain |
| macOS | x64 | ~3-4 min | Intel runner |
| macOS | ARM64 | ~3-4 min | Apple Silicon runner |
| **Total Workflow** | **All** | **~10-15 min** | **Parallel execution** |

---

## Security Notes

- All builds use official Docker base images
- Windows builds use native Windows APIs (no OpenSSL dependency)
- Static linking available for deployment scenarios
- No runtime dependencies on Windows
- GitHub Actions uses secure runners with isolated environments
- Artifacts are scanned by GitHub's security features
- Release binaries should be signed before distribution

## Monitoring Builds

### GitHub Actions Dashboard
- View all runs: `https://github.com/[owner]/mcp-cpp-sdk/actions`
- Specific workflow: Click on "Build All Libraries"
- Real-time logs: Click on any running job

### Status Badges
Add to README.md:
```markdown
[![Build All Libraries](https://github.com/[owner]/mcp-cpp-sdk/actions/workflows/build-all.yml/badge.svg)](https://github.com/[owner]/mcp-cpp-sdk/actions/workflows/build-all.yml)
```

### Notifications
- Email: Configure in GitHub Settings → Notifications
- Slack/Discord: Use GitHub Actions webhooks
- GitHub Mobile: Get push notifications

## Troubleshooting GitHub Actions

### Common Issues

1. **Runner not available**
   - `macos-15-intel` may have limited availability
   - Fallback: Use `macos-13` or `macos-latest`

2. **Build failures**
   - Check the job logs for specific errors
   - Verify dependencies are correctly specified
   - Ensure scripts have execute permissions

3. **Artifact upload failures**
   - Check artifact size limits (500MB default)
   - Verify file paths are correct
   - Ensure retention days are within limits

4. **Release creation failures**
   - Verify GITHUB_TOKEN has write permissions
   - Check tag naming doesn't conflict
   - Ensure all artifacts exist before release

### Re-running Failed Jobs
```bash
# Re-run all jobs
gh run rerun [run-id]

# Re-run only failed jobs
gh run rerun [run-id] --failed

# View specific job logs
gh run view [run-id] --log
```

## Best Practices

### For Contributors
1. Test locally before pushing
2. Use draft PRs for work in progress
3. Include platform info in commit messages
4. Run linting and formatting locally

### For Maintainers
1. Tag releases semantically (v0.1.0, v0.2.0)
2. Update version in CMakeLists.txt
3. Document breaking changes
4. Sign release binaries when possible
5. Test artifacts before announcing releases

### For CI/CD
1. Use matrix builds for efficiency
2. Cache dependencies where possible
3. Parallelize independent jobs
4. Set appropriate timeout values
5. Use secrets for sensitive data
6. Regular cleanup of old artifacts