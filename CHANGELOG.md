# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

### Changed

### Fixed

## [0.1.15] - 2026-07-26

### Changed

- Reduce Streamable HTTP reconnect readiness waiting so request timeouts keep
  headroom for the actual send.
- Return `404` for unknown HTTP paths instead of leaving clients waiting for a
  protocol-layer response that never arrives.

### Fixed

- Propagate `Mcp-Session-Id` from Streamable HTTP requests as the transport
  session id used by downstream dispatch.
- Remove redundant session-header lookup code after HTTP header names have
  already been normalized.
- Parse complete end-of-stream HTTP JSON-RPC bodies as one JSON document,
  preserving pretty-printed requests while retaining newline-delimited fallback
  behavior.
- Avoid double parsing JSON-RPC HTTP bodies after validation.
- Report one parse error for a malformed pretty-printed HTTP body instead of
  emitting one error per line fragment.

## [0.1.14] - 2026-07-22

### Fixed

- Fix Streamable HTTP tool-call body parsing for pretty-printed JSON, newline-delimited JSON fallback, and malformed body error reporting.
- Propagate `Mcp-Session-Id` from Streamable HTTP requests as the transport session id.
- Bound reconnect readiness polling so it cannot consume the full request timeout.

## [0.1.13] - 2026-07-08

### Added

- Add MCP HTTP header passthrough support (#250).

### Fixed

- Fix SSL transport use-after-free in posted lambdas (#245).
- Null-guard OpenSSL 3.x error-string accessors to prevent crash (#246).
- Fix SSL transport data loss on full network BIO (#247).
- Route MCP responses to the originating connection (#248).
- Improve transport and MCP flow logging (#249).
- Skip TLS peer metadata when verification is disabled (#251).
- Avoid retaining streamed HTTP client bodies (#260).
- Guard oversized HTTP body callbacks (#255).
- Defer HTTP parser callback errors (#256).
- Post HTTP parser error callbacks (#252).
- Defer active connection destruction on close (#257).
- Bind llhttp symbols inside the shared library (#259).
- Honor preferred HTTP transport (#253).

## [0.1.12] - 2026-07-03

### Added

- Add MCP HTTP header passthrough support.
- Add MCP client and HTTP transport invoke logging controlled by `GOPHER_LOG_LEVEL`.

## [0.1.11] - 2026-07-02

### Changed

- Bind llhttp symbols inside the shared library.
- Defer active connection destruction on close.
- Post and defer HTTP parser error callbacks safely.
- Guard oversized HTTP body callbacks.
- Avoid retaining streamed HTTP client bodies.
- Skip TLS peer metadata when verification is disabled.

### Fixed

- Fix SSL transport data loss on full network BIO.
- Fix SSL transport use-after-free in posted lambdas.

## [0.1.10] - 2026-06-30

### Added

- Add MCP HTTP header passthrough support.
- Add MCP client and HTTP transport invoke logging controlled by `GOPHER_LOG_LEVEL`.

## [0.1.9] - 2026-06-30

### Added

- Add MCP client and HTTP transport invoke logging controlled by `GOPHER_LOG_LEVEL`.

### Changed

- Route MCP responses to the originating connection to fix concurrent request hangs.

### Fixed

- Fix SSL transport data loss on full network BIO.
- Fix SSL transport use-after-free in posted lambdas.

## [0.1.8] - 2026-06-24

### Added

- Add MCP client and HTTP transport invoke logging controlled by `GOPHER_LOG_LEVEL`.

### Changed

- Scope MCP flow logging to a dedicated `GOPHER_MCP_LOG_FLOW` switch.
- Demote filter registry initialization and registration logs to debug level.
- Surface SSL errors through warning logs.
- Null-guard OpenSSL 3.x error-string accessors.

### Fixed

- Fix SSL transport data loss on full network BIO.
- Fix SSL transport use-after-free in posted lambdas.

## [0.1.7] - 2026-06-22

### Changed

- Demote filter registry initialization and registration logs to debug level.
- Surface SSL errors through warning logs.
- Null-guard OpenSSL 3.x error-string accessors.

### Fixed

- Fix SSL transport use-after-free in posted lambdas.

## [0.1.6] - 2026-06-11

### Added

- Add request-scoped `_meta` storage to `SessionContext` for tool handlers.
- Add client-side notification handler registration to `McpClient`.
- Add server and client SSE state machines with integration coverage.
- Add runtime formatting support for dynamic format strings.

### Changed

- Populate `ReadResourceResult` contents in `McpClient::readResource`.
- Integrate SSE connection state machines into `HttpSseJsonRpcProtocolFilter`.
- Remove obsolete request-stream and SSE mode state from the HTTP/SSE filter.
- Improve MSVC build configuration.
- Update README architecture documentation.

### Fixed

- Fix `ConnectionPoolImpl` timeout crash from premature write events.

## [0.1.5] - 2026-04-21

### Added

- Add idle-read timeout handling to `ConnectionImpl` and `McpServer`.
- Add SSE server transport support with a per-factory session registry.
- Add configurable SSE/RPC paths and external URL parameters to the filter chain factory.
- Add `HttpAsyncClient` built on `HttpCodecFilter`.
- Add integration and lifecycle tests for HTTP, SSE, and connection cleanup behavior.

### Changed

- Match `/callback/{id}` requests under reverse-proxy path prefixes.
- Rename the default SSE path to `/sse`.
- Surface numeric `:status` pseudo-headers from the HTTP client codec.
- Disable body timeout for client-mode HTTP codec.
- Defer closed connection destruction through dispatcher cleanup.
- Drain active server connections during shutdown.

### Fixed

- Fix background-task timer lifetime in `McpServer`.
- Fix callback scheduling so deferred callbacks run after caller stack unwinds.

## [0.1.4] - 2026-04-08

### Added

- Add resource read handlers to example server registrations.
- Add `ResourceReadHandler` callback support to `ResourceManager`.
- Add tests for `resources/read` responses and resource manager handlers.

### Changed

- Run formatting on resource read implementation files.

### Fixed

- Fix `resources/read` responses to match the MCP schema.

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
