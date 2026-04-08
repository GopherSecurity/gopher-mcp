# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

### Changed

### Fixed


## [0.1.4] - 2026-04-08

### Added

- Add unit tests for resources/read response and ResourceManager handlers (#206)
- Add read handlers to example server resource registrations (#206)
- Add ResourceReadHandler callback to ResourceManager (#206)

### Changed

- Enhance dump-version.sh with GitHub release check and auto-generated changelog
- Release 0.1.3
- Run clang-format on resource read implementation files (#206)
- Unstaged changes: CMakeLists.txt

### Fixed

- Fix resources/read response to match MCP schema (#206)

## [0.1.3] - 2026-04-08

### Added

### Changed

### Fixed

## [0.1.1] - 2026-03-03

### Added

### Changed

### Fixed

## [0.1.0] - 2025-12-15

### Added
- Full MCP 2025-06-18 specification implementation
- JSON-RPC 2.0 protocol support
- Transport layers: stdio, HTTP+SSE, HTTPS+SSE, TCP
- Filter chain architecture with HTTP codec, SSE codec, routing
- Connection pooling and management
- C API bindings for FFI (Python, TypeScript, Go, Rust, Java, C#, Ruby)
- Cross-platform support: Linux, macOS, Windows (x64 and ARM64)
- libevent-based event loop integration
- SSL/TLS transport support
- Comprehensive logging framework
