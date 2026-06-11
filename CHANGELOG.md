# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

### Changed

### Fixed


## [0.1.6] - 2026-06-11

### Added

- Add a request-scoped _meta carrier to SessionContext (#236) - set/getRequestMeta hold the in-flight request's params._meta as its   stringified-JSON form, so a tool handler can read out-of-band metadata   (e.g. correlation ids) without the dispatch forking - The value is per-request but stored on the per-session SessionContext; this is   safe because a session handles one request at a time on the dispatcher thread   and handleCallTool sets it fresh before each dispatch, so it always reflects the   current request
- Add client-side notification handler registration to McpClient (#237)
- Add integration tests for ServerConnectionMode wiring in filter (#226)
- Add comprehensive unit tests for ServerConnectionMode (#226)
- Add ServerConnectionMode for server-side connection lifecycle (#226)
- Add integration tests for ClientSseStateMachine wiring in filter (#226)
- Add comprehensive unit tests for ClientSseStateMachine (#226)
- Add ClientSseStateMachine for client-side SSE negotiation lifecycle (#226)
- Add fmt::runtime calls to all runtime format strings

### Changed

- Release 0.1.5
- Release 0.1.4
- Enhance dump-version.sh with GitHub release check and auto-generated changelog
- Release 0.1.3
- Format code (#236)
- Surface request params._meta to server-side tool handlers (#236) - handleCallTool stashes params._meta onto the session before dispatch, beside the   existing arguments extraction; cleared when absent so a prior request's _meta   never aliases this one - The tool handler already receives the session, so no handler signature changes
- Populate ReadResourceResult contents in McpClient::readResource (#238)
- Format code (#226)
- Remove dead RequestStream code from HttpSseJsonRpcProtocolFilter (#226)
- Wire state change logging for both state machines (#226)
- Remove is_sse_mode_ and unify SSE detection through state machines (#226)
- Integrate ServerConnectionMode into HttpSseJsonRpcProtocolFilter (#226)
- Wire SSE negotiation timeout to error propagation and message drain (#226)
- Replace client-side boolean flags with ClientSseStateMachine queries (#226)
- Wire ClientSseStateMachine into HttpSseJsonRpcProtocolFilter as shadow (#226)
- build: Add MSVC 26 support and improve build configuration
- Restyle README architecture overview to nested-box layout and align right edges (#225)
- Unstaged changes: CMakeLists.txt

### Fixed

- Fix ConnectionPoolImpl timeout SEGFAULT from premature write event (#226)

## [0.1.5] - 2026-04-21

### Added

- Add idle-read timeout to ConnectionImpl (#224)
- Add real-IO SSE server transport handshake test (#216)
- Add unit tests for SseSessionRegistry  (#215)
- Implement SSE server transport with per-factory session registry (#215)
- Add SSE/RPC path and external_url params to filter chain factory (#215)
- Add integration tests for HttpAsyncClient (#213)
- Add HttpAsyncClient built on HttpCodecFilter (#213)
- Add unit tests for crash-fix contracts (#212)

### Changed

- Release 0.1.4
- Enhance dump-version.sh with GitHub release check and auto-generated changelog
- Release 0.1.3
- Cover server idle-read timeout end-to-end (#224)
- Switch idle-read close to NoFlush so LocalClose actually propagates (#224)
- Arm idle-read timeout on every accepted McpServer connection (#224)
- Cover abortive TCP close in McpServer connection-lifecycle test (#223)
- Cover ConnectionPoolImpl timeout timer against stack-capture UAF (#222)
- Stop capturing stack-local PendingConnection in pool timeout timer (#222)
- Run deferred close through dispatcher post instead of a stack-local timer (#221)
- Drop write-only num_connections_ in favor of public stat (#220)
- Drop vestigial ConnectionCallbacks inheritance from McpServer (#220)
- Cover connections_active/total across three concurrent accepts (#219)
- Count TCP server connections in the public stats (#219)
- Drop leak-on-teardown workaround from initialize-routing test (#218)
- Cover McpServer connection-lifecycle cleanup and shutdown-drain (#218)
- Drain active_connections_ during McpServer::shutdown on the dispatcher (#218)
- Cover dispatcher-thread commit inside McpClient::initializeProtocol (#217)
- Cover POST /callback routing back through the SSE stream (#216)
- Extract SseSessionRegistry into its own translation unit  (#215)
- make format (#215)
- Match POST /callback/{id} under reverse-proxy path prefixes (#215)
- Wire McpServerConfig endpoint paths into HttpSseFilterChainFactory (#215)
- Rename default SSE path to /sse and add external_url config (#215)
- Surface numeric :status pseudo-header from HTTP client codec (#213)
- Test that client-mode HTTP codec actually disables body_timeout (#212)
- Cover lifecycle-adapter self+peer deferred-delete pattern (#212)
- Drive ConnectionManager event tests through the dispatcher thread (#212)
- Disable body timeout for client-mode HTTP codec (#212)
- Route initializeProtocol state commit back to the dispatcher thread (#212)
- Bind server connection callbacks per connection via adapter (#212)
- Defer closed-connection destruction via Dispatcher::deferredDelete (#212)
- Unstaged changes: CMakeLists.txt

### Fixed

- Fix background-task timer lifetime in McpServer (#212)
- Fix scheduleCallbackCurrentIteration to defer past caller's stack frame (#212)

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
