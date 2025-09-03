"""
MCP Filter SDK - Python

A comprehensive Python SDK for the MCP (Model Context Protocol) Filter C API,
providing advanced filter infrastructure, buffer management, filter chain composition,
and a complete transport layer implementation.

This SDK provides both filter infrastructure and transport layer implementation:

Filter Infrastructure:
- Filter Lifecycle Management: Create, configure, and manage filters
- Filter Chain Composition: Build complex processing pipelines
- Advanced Buffer Operations: Zero-copy operations and memory management
- FilterManager: High-level message processing with comprehensive filter support

Transport Layer:
- GopherTransport: Complete MCP transport implementation
- Protocol Support: TCP, UDP, and stdio protocols
- Enterprise Features: Security, observability, traffic management
- MCP Integration: Seamless integration with MCP client/server
"""

from .filter_api import (
    # Core filter infrastructure
    Filter,
    FilterConfig,
    FilterCallbacks,
    FilterStats,
    ProtocolMetadata,
    BufferSlice,
    
    # Filter lifecycle
    create_filter,
    create_builtin_filter,
    retain_filter,
    release_filter,
    set_filter_callbacks,
    set_protocol_metadata,
    get_protocol_metadata,
    
    # Filter types
    BuiltinFilterType,
    FilterStatus,
    FilterPosition,
    ProtocolLayer,
    FilterError,
    
    # Statistics
    get_filter_stats,
    reset_filter_stats,
)

from .filter_chain import (
    # Advanced chain management
    FilterChain,
    FilterNode,
    ChainConfig,
    FilterCondition,
    ChainStats,
    RouterConfig,
    
    # Chain operations
    create_filter_chain_builder,
    add_filter_to_chain,
    build_filter_chain,
    destroy_filter_chain_builder,
    retain_filter_chain,
    release_filter_chain,
    
    # Chain types
    ChainExecutionMode,
    RoutingStrategy,
    MatchCondition,
    ChainState,
    
    # Utility functions
    create_simple_chain,
    create_parallel_chain,
    create_conditional_chain,
)

from .filter_buffer import (
    # Advanced buffer operations
    AdvancedBuffer,
    BufferFragment,
    BufferReservation,
    BufferStats,
    DrainTracker,
    BufferPoolConfig,
    
    # Buffer creation
    create_buffer_owned,
    create_buffer_view,
    create_buffer_from_fragment,
    clone_buffer,
    create_buffer_cow,
    
    # Buffer operations
    add_data_to_buffer,
    add_string_to_buffer,
    add_buffer_to_buffer,
    add_fragment_to_buffer,
    prepend_data_to_buffer,
    drain_buffer,
    move_buffer_data,
    set_drain_tracker,
    
    # Buffer reservation
    reserve_buffer_space,
    reserve_buffer_iovec,
    commit_reservation,
    cancel_reservation,
    
    # Buffer access
    get_contiguous_buffer,
    linearize_buffer,
    peek_buffer,
    
    # Type-safe I/O
    write_le_int,
    write_be_int,
    read_le_int,
    read_be_int,
    
    # Buffer search
    search_buffer,
    find_byte_in_buffer,
    
    # Buffer information
    get_buffer_length,
    get_buffer_capacity,
    is_buffer_empty,
    get_buffer_stats,
    
    # Buffer watermarks
    set_buffer_watermarks,
    is_above_high_watermark,
    is_below_low_watermark,
    
    # Buffer pooling
    create_buffer_pool,
    create_buffer_pool_ex,
    acquire_buffer_from_pool,
    release_buffer_to_pool,
    get_buffer_pool_stats,
    trim_buffer_pool,
    destroy_buffer_pool,
    
    # Buffer types
    BufferOwnership,
    BufferFlags,
    
    # Utility functions
    create_buffer_from_string,
    read_string_from_buffer,
    create_buffer_from_json,
    read_json_from_buffer,
    create_buffer_slice,
    concatenate_buffers,
    split_buffer,
    compare_buffers,
)

from .filter_manager import (
    # High-level message processing
    FilterManager,
    FilterManagerConfig,
    JSONRPCMessage,
    
    # Filter configuration types
    NetworkFilterConfig,
    HttpFilterConfig,
    SecurityFilterConfig,
    ObservabilityFilterConfig,
    TrafficManagementFilterConfig,
    CustomFilterConfig,
    
    # Network filters
    TcpProxyConfig,
    UdpProxyConfig,
    
    # HTTP filters
    HttpCodecConfig,
    HttpRouterConfig,
    HttpCompressionConfig,
    
    # Security filters
    TlsTerminationConfig,
    AuthenticationConfig,
    AuthorizationConfig,
    
    # Observability filters
    AccessLogConfig,
    MetricsConfig,
    TracingConfig,
    
    # Traffic management filters
    RateLimitConfig,
    CircuitBreakerConfig,
    RetryConfig,
    LoadBalancerConfig,
    
    # Custom filters
    CustomFilterConfig,
    
    # Error handling
    ErrorHandlingConfig,
    FallbackBehavior,
    
    # Processing methods
    process_message,
    process_response,
    process_request_response,
)

from .ffi_bindings import (
    # FFI bindings
    mcp_filter_lib,
    ffi,
    
    # Core functions
    create_filter,
    create_builtin_filter,
    retain_filter,
    release_filter,
    set_filter_callbacks,
    set_protocol_metadata,
    get_protocol_metadata,
    
    # Chain functions
    create_filter_chain_builder,
    add_filter_to_chain,
    build_filter_chain,
    destroy_filter_chain_builder,
    retain_filter_chain,
    release_filter_chain,
    
    # Manager functions
    create_filter_manager,
    add_filter_to_manager,
    add_chain_to_manager,
    initialize_filter_manager,
    release_filter_manager,
    
    # Buffer functions
    create_buffer,
    release_buffer,
    get_buffer_length,
    
    # Statistics functions
    get_filter_stats,
    reset_filter_stats,
    
    # Thread-safe functions
    post_data_to_filter,
    
    # Utility functions
    check_result,
    create_mock_dispatcher,
    create_mock_connection,
    create_mock_memory_pool,
)

# Version information
__version__ = "1.0.0"
__author__ = "MCP Filter SDK Team"
__email__ = "team@mcp-filter-sdk.com"
__description__ = "Python SDK for MCP (Model Context Protocol) Filter C API"

# Package metadata
__all__ = [
    # Core filter infrastructure
    "Filter",
    "FilterConfig",
    "FilterCallbacks",
    "FilterStats",
    "ProtocolMetadata",
    "BufferSlice",
    
    # Filter lifecycle
    "create_filter",
    "create_builtin_filter",
    "retain_filter",
    "release_filter",
    "set_filter_callbacks",
    "set_protocol_metadata",
    "get_protocol_metadata",
    
    # Filter types
    "BuiltinFilterType",
    "FilterStatus",
    "FilterPosition",
    "ProtocolLayer",
    "FilterError",
    
    # Statistics
    "get_filter_stats",
    "reset_filter_stats",
    
    # Advanced chain management
    "FilterChain",
    "FilterNode",
    "ChainConfig",
    "FilterCondition",
    "ChainStats",
    "RouterConfig",
    
    # Chain operations
    "create_filter_chain_builder",
    "add_filter_to_chain",
    "build_filter_chain",
    "destroy_filter_chain_builder",
    "retain_filter_chain",
    "release_filter_chain",
    
    # Chain types
    "ChainExecutionMode",
    "RoutingStrategy",
    "MatchCondition",
    "ChainState",
    
    # Utility functions
    "create_simple_chain",
    "create_parallel_chain",
    "create_conditional_chain",
    
    # Advanced buffer operations
    "AdvancedBuffer",
    "BufferFragment",
    "BufferReservation",
    "BufferStats",
    "DrainTracker",
    "BufferPoolConfig",
    
    # Buffer creation
    "create_buffer_owned",
    "create_buffer_view",
    "create_buffer_from_fragment",
    "clone_buffer",
    "create_buffer_cow",
    
    # Buffer operations
    "add_data_to_buffer",
    "add_string_to_buffer",
    "add_buffer_to_buffer",
    "add_fragment_to_buffer",
    "prepend_data_to_buffer",
    "drain_buffer",
    "move_buffer_data",
    "set_drain_tracker",
    
    # Buffer reservation
    "reserve_buffer_space",
    "reserve_buffer_iovec",
    "commit_reservation",
    "cancel_reservation",
    
    # Buffer access
    "get_contiguous_buffer",
    "linearize_buffer",
    "peek_buffer",
    
    # Type-safe I/O
    "write_le_int",
    "write_be_int",
    "read_le_int",
    "read_be_int",
    
    # Buffer search
    "search_buffer",
    "find_byte_in_buffer",
    
    # Buffer information
    "get_buffer_length",
    "get_buffer_capacity",
    "is_buffer_empty",
    "get_buffer_stats",
    
    # Buffer watermarks
    "set_buffer_watermarks",
    "is_above_high_watermark",
    "is_below_low_watermark",
    
    # Buffer pooling
    "create_buffer_pool",
    "create_buffer_pool_ex",
    "acquire_buffer_from_pool",
    "release_buffer_to_pool",
    "get_buffer_pool_stats",
    "trim_buffer_pool",
    "destroy_buffer_pool",
    
    # Buffer types
    "BufferOwnership",
    "BufferFlags",
    
    # Utility functions
    "create_buffer_from_string",
    "read_string_from_buffer",
    "create_buffer_from_json",
    "read_json_from_buffer",
    "create_buffer_slice",
    "concatenate_buffers",
    "split_buffer",
    "compare_buffers",
    
    # High-level message processing
    "FilterManager",
    "FilterManagerConfig",
    "JSONRPCMessage",
    
    # Filter configuration types
    "NetworkFilterConfig",
    "HttpFilterConfig",
    "SecurityFilterConfig",
    "ObservabilityFilterConfig",
    "TrafficManagementFilterConfig",
    "CustomFilterConfig",
    
    # Network filters
    "TcpProxyConfig",
    "UdpProxyConfig",
    
    # HTTP filters
    "HttpCodecConfig",
    "HttpRouterConfig",
    "HttpCompressionConfig",
    
    # Security filters
    "TlsTerminationConfig",
    "AuthenticationConfig",
    "AuthorizationConfig",
    
    # Observability filters
    "AccessLogConfig",
    "MetricsConfig",
    "TracingConfig",
    
    # Traffic management filters
    "RateLimitConfig",
    "CircuitBreakerConfig",
    "RetryConfig",
    "LoadBalancerConfig",
    
    # Custom filters
    "CustomFilterConfig",
    
    # Error handling
    "ErrorHandlingConfig",
    "FallbackBehavior",
    
    # Processing methods
    "process_message",
    "process_response",
    "process_request_response",
    
    # FFI bindings
    "mcp_filter_lib",
    "ffi",
    
    # Core functions
    "create_filter",
    "create_builtin_filter",
    "retain_filter",
    "release_filter",
    "set_filter_callbacks",
    "set_protocol_metadata",
    "get_protocol_metadata",
    
    # Chain functions
    "create_filter_chain_builder",
    "add_filter_to_chain",
    "build_filter_chain",
    "destroy_filter_chain_builder",
    "retain_filter_chain",
    "release_filter_chain",
    
    # Manager functions
    "create_filter_manager",
    "add_filter_to_manager",
    "add_chain_to_manager",
    "initialize_filter_manager",
    "release_filter_manager",
    
    # Buffer functions
    "create_buffer",
    "release_buffer",
    "get_buffer_length",
    
    # Statistics functions
    "get_filter_stats",
    "reset_filter_stats",
    
    # Thread-safe functions
    "post_data_to_filter",
    
    # Utility functions
    "check_result",
    "create_mock_dispatcher",
    "create_mock_connection",
    "create_mock_memory_pool",
]
