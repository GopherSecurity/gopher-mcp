"""
FFI bindings for MCP Filter C API.

This module provides Python bindings to the C++ shared library using ctypes,
enabling access to all filter functionality from Python.
"""

import os
import sys
import platform
from typing import Any, Dict, List, Optional, Union, Callable
from ctypes import (
    CDLL, c_void_p, c_char_p, c_uint64, c_int32, c_uint32, c_bool, c_size_t,
    c_int, c_uint, c_ulong, c_long, c_double, c_float, POINTER, Structure,
    c_char, c_ubyte, c_byte, c_short, c_ushort, c_longlong, c_ulonglong
)

# Library configuration for different platforms and architectures
LIBRARY_CONFIG = {
    "darwin": {
        "x86_64": {
            "name": "libgopher_mcp_c.dylib",
            "search_paths": [
                "../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
                "../../build/src/c_api/libgopher_mcp_c.dylib",
                "../../build/lib/libgopher_mcp_c.dylib",
                "/opt/homebrew/lib/libgopher_mcp_c.dylib",
                "/usr/local/lib/libgopher_mcp_c.dylib",
            ],
        },
        "arm64": {
            "name": "libgopher_mcp_c.dylib",
            "search_paths": [
                "../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
                "../../build/src/c_api/libgopher_mcp_c.dylib",
                "../../build/lib/libgopher_mcp_c.dylib",
                "/opt/homebrew/lib/libgopher_mcp_c.dylib",
                "/usr/local/lib/libgopher_mcp_c.dylib",
            ],
        },
    },
    "linux": {
        "x86_64": {
            "name": "libgopher_mcp_c.so",
            "search_paths": [
                "build/src/c_api/libgopher_mcp_c.so",
                "build/lib/libgopher_mcp_c.so",
                "/usr/local/lib/libgopher_mcp_c.so",
                "/usr/lib/x86_64-linux-gnu/libgopher_mcp_c.so",
                "/usr/lib64/libgopher_mcp_c.so",
            ],
        },
        "aarch64": {
            "name": "libgopher_mcp_c.so",
            "search_paths": [
                "build/src/c_api/libgopher_mcp_c.so",
                "build/lib/libgopher_mcp_c.so",
                "/usr/local/lib/libgopher_mcp_c.so",
                "/usr/lib/aarch64-linux-gnu/libgopher_mcp_c.so",
                "/usr/lib64/libgopher_mcp_c.so",
            ],
        },
    },
    "win32": {
        "AMD64": {
            "name": "gopher_mcp_c.dll",
            "search_paths": [
                "build/src/c_api/gopher_mcp_c.dll",
                "build/bin/gopher_mcp_c.dll",
                "C:\\Program Files\\gopher-mcp\\bin\\gopher_mcp_c.dll",
                "C:\\Program Files\\gopher-mcp\\lib\\gopher_mcp_c.dll",
            ],
        },
        "x86": {
            "name": "gopher_mcp_c.dll",
            "search_paths": [
                "build/src/c_api/gopher_mcp_c.dll",
                "build/bin/gopher_mcp_c.dll",
                "C:\\Program Files (x86)\\gopher-mcp\\bin\\gopher_mcp_c.dll",
                "C:\\Program Files (x86)\\gopher-mcp\\lib\\gopher_mcp_c.dll",
            ],
        },
    },
}

def get_library_path() -> str:
    """Get the path to the shared library for the current platform."""
    current_platform = platform.system().lower()
    current_arch = platform.machine().lower()
    
    # Normalize architecture names
    if current_arch in ["x86_64", "amd64"]:
        current_arch = "x86_64"
    elif current_arch in ["aarch64", "arm64"]:
        current_arch = "arm64" if current_platform == "darwin" else "aarch64"
    elif current_arch in ["i386", "i686"]:
        current_arch = "x86"
    
    if current_platform not in LIBRARY_CONFIG:
        raise RuntimeError(f"Unsupported platform: {current_platform}")
    
    if current_arch not in LIBRARY_CONFIG[current_platform]:
        raise RuntimeError(f"Unsupported architecture: {current_arch} on {current_platform}")
    
    # Get search paths for current platform/architecture
    config = LIBRARY_CONFIG[current_platform][current_arch]
    search_paths = config["search_paths"]
    
    # Check MCP_LIBRARY_PATH environment variable first
    env_path = os.environ.get("MCP_LIBRARY_PATH")
    if env_path and os.path.exists(env_path):
        return env_path
    
    # Search through configured paths
    for path in search_paths:
        if os.path.exists(path):
            return path
    
    # If no path found, raise error with helpful message
    available_paths = "\n".join(f"  - {path}" for path in search_paths)
    raise RuntimeError(
        f"Could not find MCP library for {current_platform}/{current_arch}. "
        f"Searched paths:\n{available_paths}\n"
        f"Set MCP_LIBRARY_PATH environment variable to specify custom library path."
    )

def get_all_possible_library_paths() -> List[str]:
    """Get all possible library paths for the current platform."""
    current_platform = platform.system().lower()
    current_arch = platform.machine().lower()
    
    # Normalize architecture names
    if current_arch in ["x86_64", "amd64"]:
        current_arch = "x86_64"
    elif current_arch in ["aarch64", "arm64"]:
        current_arch = "arm64" if current_platform == "darwin" else "aarch64"
    elif current_arch in ["i386", "i686"]:
        current_arch = "x86"
    
    if current_platform not in LIBRARY_CONFIG:
        return []
    
    if current_arch not in LIBRARY_CONFIG[current_platform]:
        return []
    
    return LIBRARY_CONFIG[current_platform][current_arch]["search_paths"]

def check_library_path(path: str) -> bool:
    """Check if a library path exists and is accessible."""
    return os.path.exists(path) and os.access(path, os.R_OK)

def get_library_name() -> str:
    """Get the name of the shared library for the current platform."""
    current_platform = platform.system().lower()
    current_arch = platform.machine().lower()
    
    # Normalize architecture names
    if current_arch in ["x86_64", "amd64"]:
        current_arch = "x86_64"
    elif current_arch in ["aarch64", "arm64"]:
        current_arch = "arm64" if current_platform == "darwin" else "aarch64"
    elif current_arch in ["i386", "i686"]:
        current_arch = "x86"
    
    if current_platform not in LIBRARY_CONFIG:
        raise RuntimeError(f"Unsupported platform: {current_platform}")
    
    if current_arch not in LIBRARY_CONFIG[current_platform]:
        raise RuntimeError(f"Unsupported architecture: {current_arch} on {current_platform}")
    
    return LIBRARY_CONFIG[current_platform][current_arch]["name"]

# MCP Filter Library interface - using real C API functions
mcp_filter_lib = None

try:
    lib_path = get_library_path()
    lib_name = get_library_name()
    
    print(f"Loading MCP C API library: {lib_name}")
    print(f"Library path: {lib_path}")
    
    # Load the shared library
    mcp_filter_lib = CDLL(lib_path)
    print(f"MCP C API library loaded successfully: {lib_name}")
    
    # List of REAL C API functions from mcp_filter_api.h, mcp_filter_buffer.h, mcp_filter_chain.h
    # This matches the TypeScript function list for full compatibility
    function_list = [
        # Core filter functions from mcp_filter_api.h
        ("mcp_filter_create", c_uint64, [c_uint64, c_void_p]),
        ("mcp_filter_create_builtin", c_uint64, [c_uint64, c_int, c_void_p]),
        ("mcp_filter_retain", None, [c_uint64]),
        ("mcp_filter_release", None, [c_uint64]),
        ("mcp_filter_set_callbacks", c_int, [c_uint64, c_void_p]),
        ("mcp_filter_set_protocol_metadata", c_int, [c_uint64, c_void_p]),
        ("mcp_filter_get_protocol_metadata", c_int, [c_uint64, c_void_p]),
        
        # Filter chain functions from mcp_filter_api.h and mcp_filter_chain.h
        ("mcp_filter_chain_builder_create", c_void_p, [c_uint64]),
        ("mcp_chain_builder_create_ex", c_void_p, [c_uint64, c_void_p]),
        ("mcp_chain_builder_add_node", c_int, [c_void_p, c_void_p]),
        ("mcp_filter_chain_add_filter", c_int, [c_void_p, c_uint64, c_int, c_uint64]),
        ("mcp_filter_chain_build", c_uint64, [c_void_p]),
        ("mcp_filter_chain_builder_destroy", None, [c_void_p]),
        ("mcp_filter_chain_retain", None, [c_uint64]),
        ("mcp_filter_chain_release", None, [c_uint64]),
        
        # Missing chain functions from mcp_c_filter_chain.h
        ("mcp_chain_builder_add_conditional", c_int, [c_void_p, c_void_p, c_uint64]),
        ("mcp_chain_builder_add_parallel_group", c_int, [c_void_p, c_void_p, c_size_t]),
        ("mcp_chain_get_state", c_int, [c_uint64]),
        ("mcp_chain_pause", c_int, [c_uint64]),
        ("mcp_chain_resume", c_int, [c_uint64]),
        ("mcp_chain_reset", c_int, [c_uint64]),
        
        # Buffer functions from mcp_filter_buffer.h
        ("mcp_buffer_create_owned", c_uint64, [c_size_t, c_int]),
        ("mcp_buffer_create_view", c_uint64, [c_void_p, c_size_t]),
        ("mcp_buffer_create_from_fragment", c_uint64, [c_void_p]),
        ("mcp_buffer_clone", c_uint64, [c_uint64]),
        ("mcp_buffer_create_cow", c_uint64, [c_uint64]),
        ("mcp_buffer_add", c_int, [c_uint64, c_void_p, c_size_t]),
        ("mcp_buffer_add_string", c_int, [c_uint64, c_char_p]),
        ("mcp_buffer_add_buffer", c_int, [c_uint64, c_uint64]),
        ("mcp_buffer_add_fragment", c_int, [c_uint64, c_void_p]),
        ("mcp_buffer_prepend", c_int, [c_uint64, c_void_p, c_size_t]),
        ("mcp_buffer_drain", c_int, [c_uint64, c_size_t]),
        ("mcp_buffer_move", c_int, [c_uint64, c_uint64, c_size_t]),
        ("mcp_buffer_reserve", c_int, [c_uint64, c_size_t, c_void_p]),
        ("mcp_buffer_commit_reservation", c_int, [c_void_p, c_size_t]),
        ("mcp_buffer_cancel_reservation", c_int, [c_void_p]),
        ("mcp_buffer_get_contiguous", c_int, [c_uint64, c_size_t, c_size_t, c_void_p, POINTER(c_size_t)]),
        ("mcp_buffer_linearize", c_int, [c_uint64, c_size_t, c_void_p]),
        ("mcp_buffer_peek", c_int, [c_uint64, c_size_t, c_void_p, c_size_t]),
        ("mcp_buffer_write_le_int", c_int, [c_uint64, c_uint64, c_size_t]),
        ("mcp_buffer_write_be_int", c_int, [c_uint64, c_uint64, c_size_t]),
        ("mcp_buffer_read_le_int", c_int, [c_uint64, c_size_t, POINTER(c_uint64)]),
        ("mcp_buffer_read_be_int", c_int, [c_uint64, c_size_t, POINTER(c_uint64)]),
        ("mcp_buffer_search", c_int, [c_uint64, c_void_p, c_size_t, c_size_t, POINTER(c_size_t)]),
        ("mcp_buffer_find_byte", c_int, [c_uint64, c_ubyte, POINTER(c_size_t)]),
        ("mcp_buffer_length", c_size_t, [c_uint64]),
        ("mcp_buffer_capacity", c_size_t, [c_uint64]),
        ("mcp_buffer_is_empty", c_int, [c_uint64]),
        ("mcp_buffer_get_stats", c_int, [c_uint64, c_void_p]),
        ("mcp_buffer_set_watermarks", c_int, [c_uint64, c_size_t, c_size_t]),
        ("mcp_buffer_above_high_watermark", c_int, [c_uint64]),
        ("mcp_buffer_below_low_watermark", c_int, [c_uint64]),
        
        # Buffer pool operations
        ("mcp_buffer_pool_create", c_void_p, [c_size_t, c_size_t]),
        ("mcp_buffer_pool_create_ex", c_void_p, [c_void_p]),
        ("mcp_buffer_pool_acquire", c_uint64, [c_void_p]),
        ("mcp_buffer_pool_release", None, [c_void_p, c_uint64]),
        ("mcp_buffer_pool_destroy", None, [c_void_p]),
        ("mcp_buffer_pool_get_stats", c_int, [c_void_p, c_void_p]),
        ("mcp_buffer_pool_trim", c_int, [c_void_p]),
        
        # Filter manager operations
        ("mcp_filter_manager_create", c_uint64, [c_uint64, c_uint64]),
        ("mcp_filter_manager_add_filter", c_int, [c_uint64, c_uint64]),
        ("mcp_filter_manager_add_chain", c_int, [c_uint64, c_uint64]),
        ("mcp_filter_manager_initialize", c_int, [c_uint64]),
        ("mcp_filter_manager_release", None, [c_uint64]),
        
        # Filter buffer operations
        ("mcp_filter_get_buffer_slices", c_int, [c_uint64, c_void_p, c_void_p]),
        ("mcp_filter_reserve_buffer", c_int, [c_uint64, c_size_t, c_void_p]),
        ("mcp_filter_commit_buffer", c_int, [c_uint64, c_size_t]),
        ("mcp_filter_buffer_create", c_uint64, [c_void_p, c_size_t, c_uint32]),
        ("mcp_filter_buffer_release", None, [c_uint64]),
        
        # Client/Server operations
        ("mcp_client_send_filtered", c_int, [c_uint64, c_void_p, c_size_t]),
        ("mcp_server_process_filtered", c_int, [c_uint64, c_void_p, c_size_t]),
        
        # Filter operations
        ("mcp_filter_post_data", c_int, [c_uint64, c_void_p, c_size_t, c_void_p, c_void_p]),
        ("mcp_filter_guard_create", c_void_p, [c_uint64]),
        ("mcp_filter_guard_add_filter", c_int, [c_void_p, c_uint64]),
        ("mcp_filter_guard_release", None, [c_void_p]),
        ("mcp_filter_get_stats", c_int, [c_uint64, c_void_p]),
        ("mcp_filter_reset_stats", c_int, [c_uint64]),
        
        # Memory pool operations
        ("mcp_memory_pool_create", c_void_p, [c_size_t]),
        ("mcp_memory_pool_destroy", None, [c_void_p]),
        ("mcp_memory_pool_alloc", c_void_p, [c_void_p, c_size_t]),
        
        # JSON operations
        ("mcp_json_create_object", c_void_p, []),
        ("mcp_json_create_string", c_void_p, [c_char_p]),
        ("mcp_json_create_number", c_void_p, [c_double]),
        ("mcp_json_create_bool", c_void_p, [c_int]),
        ("mcp_json_create_null", c_void_p, []),
        ("mcp_json_free", None, [c_void_p]),
        ("mcp_json_stringify", c_char_p, [c_void_p]),
        
        # Core MCP operations
        ("mcp_init", c_int, []),
        ("mcp_shutdown", None, []),
        ("mcp_is_initialized", c_int, []),
        ("mcp_get_version", c_char_p, []),
        ("mcp_get_last_error", c_char_p, []),
        ("mcp_clear_last_error", None, []),
    ]
    
    # Bind functions to the library
    available_functions = {}
    bound_count = 0
    
    for func_name, restype, argtypes in function_list:
        try:
            func = getattr(mcp_filter_lib, func_name)
            func.restype = restype
            func.argtypes = argtypes
            available_functions[func_name] = func
            bound_count += 1
        except AttributeError as e:
            print(f"Warning: Function {func_name} not found in library")
            continue
    
    print(f"Successfully bound {bound_count}/{len(function_list)} functions from MCP C API library")
    
    # Make functions available at module level
    globals().update(available_functions)
    
except Exception as e:
    print(f"Error loading MCP C API library: {e}")
    print("Using mock implementation for development")
    mcp_filter_lib = None

# Constants from C API
MCP_OK = 0
MCP_ERROR_INVALID_ARGUMENT = -1
MCP_ERROR_NOT_FOUND = -2
MCP_ERROR_INVALID_STATE = -3
MCP_ERROR_RESOURCE_EXHAUSTED = -4

MCP_TRUE = 1
MCP_FALSE = 0

MCP_FILTER_CONTINUE = 0
MCP_FILTER_STOP_ITERATION = 1

# Filter types
MCP_FILTER_TCP_PROXY = 0
MCP_FILTER_UDP_PROXY = 1
MCP_FILTER_HTTP_CODEC = 10
MCP_FILTER_HTTP_ROUTER = 11
MCP_FILTER_HTTP_COMPRESSION = 12
MCP_FILTER_TLS_TERMINATION = 20
MCP_FILTER_AUTHENTICATION = 21
MCP_FILTER_AUTHORIZATION = 22
MCP_FILTER_ACCESS_LOG = 30
MCP_FILTER_METRICS = 31
MCP_FILTER_TRACING = 32
MCP_FILTER_RATE_LIMIT = 40
MCP_FILTER_CIRCUIT_BREAKER = 41
MCP_FILTER_RETRY = 42
MCP_FILTER_LOAD_BALANCER = 43
MCP_FILTER_CUSTOM = 100

# Mock implementations for testing when library is not available
def create_mock_dispatcher() -> int:
    """Create a mock dispatcher for testing."""
    return 1

def create_mock_connection() -> int:
    """Create a mock connection for testing."""
    return 1

def create_mock_memory_pool() -> int:
    """Create a mock memory pool for testing."""
    return 1

def check_result(result: int) -> bool:
    """Check if a result code indicates success."""
    return result == MCP_OK

# Mock C API functions
def mcp_filter_create(dispatcher: int, config: int) -> int:
    """Mock filter creation."""
    return 1

def mcp_filter_create_builtin(dispatcher: int, filter_type: int, settings: int) -> int:
    """Mock builtin filter creation."""
    return 1

def mcp_filter_retain(filter_handle: int) -> None:
    """Mock filter retain."""
    pass

def mcp_filter_release(filter_handle: int) -> None:
    """Mock filter release."""
    pass

def mcp_filter_set_callbacks(filter_handle: int, callbacks: int) -> int:
    """Mock set callbacks."""
    return MCP_OK

def mcp_filter_set_protocol_metadata(filter_handle: int, metadata: int) -> int:
    """Mock set protocol metadata."""
    return MCP_OK

def mcp_filter_get_protocol_metadata(filter_handle: int, metadata: int) -> int:
    """Mock get protocol metadata."""
    return MCP_OK

def mcp_filter_chain_builder_create(dispatcher: int) -> int:
    """Mock chain builder creation."""
    return 1

def mcp_filter_chain_builder_add_filter(builder: int, filter_handle: int) -> int:
    """Mock add filter to chain."""
    return MCP_OK

def mcp_filter_chain_builder_build(builder: int) -> int:
    """Mock build chain."""
    return 1

def mcp_filter_chain_builder_destroy(builder: int) -> None:
    """Mock destroy chain builder."""
    pass

def mcp_filter_chain_retain(chain_handle: int) -> None:
    """Mock chain retain."""
    pass

def mcp_filter_chain_release(chain_handle: int) -> None:
    """Mock chain release."""
    pass

def mcp_filter_manager_create(connection: int, dispatcher: int) -> int:
    """Mock manager creation."""
    return 1

def mcp_filter_manager_add_filter(manager: int, filter_handle: int) -> int:
    """Mock add filter to manager."""
    return MCP_OK

def mcp_filter_manager_add_chain(manager: int, chain_handle: int) -> int:
    """Mock add chain to manager."""
    return MCP_OK

def mcp_filter_manager_initialize(manager: int) -> int:
    """Mock initialize manager."""
    return MCP_OK

def mcp_filter_manager_release(manager: int) -> None:
    """Mock release manager."""
    pass

def mcp_filter_buffer_create(data: int, length: int) -> int:
    """Mock buffer creation."""
    return 1

def mcp_filter_buffer_release(buffer_handle: int) -> None:
    """Mock buffer release."""
    pass

def mcp_filter_buffer_length(buffer_handle: int) -> int:
    """Mock buffer length."""
    return 0

def mcp_buffer_peek(buffer_handle: int, data: int, length: int) -> int:
    """Mock buffer peek."""
    return 0

def mcp_filter_get_buffer_slices(buffer_handle: int, slices: int, max_slices: int) -> int:
    """Mock get buffer slices."""
    return 0

def mcp_filter_reserve_buffer(buffer_handle: int, length: int) -> int:
    """Mock reserve buffer."""
    return 0

def mcp_filter_commit_buffer(buffer_handle: int, length: int) -> int:
    """Mock commit buffer."""
    return MCP_OK

def mcp_buffer_pool_create(config: int) -> int:
    """Mock buffer pool creation."""
    return 1

def mcp_buffer_pool_acquire(pool: int) -> int:
    """Mock acquire buffer from pool."""
    return 1

def mcp_buffer_pool_release(pool: int, buffer_handle: int) -> None:
    """Mock release buffer to pool."""
    pass

def mcp_buffer_pool_destroy(pool: int) -> None:
    """Mock destroy buffer pool."""
    pass

def mcp_filter_get_stats(filter_handle: int, stats: int) -> int:
    """Mock get filter stats."""
    return MCP_OK

def mcp_filter_reset_stats(filter_handle: int) -> int:
    """Mock reset filter stats."""
    return MCP_OK

def mcp_filter_post_data(filter_handle: int, data: int, length: int, callback: int, user_data: int) -> int:
    """Mock post data to filter."""
    return MCP_OK

def mcp_filter_chain_create(dispatcher: int, config: int) -> int:
    """Mock create filter chain."""
    return 1

def mcp_filter_chain_destroy(chain_handle: int) -> None:
    """Mock destroy filter chain."""
    pass

def mcp_filter_chain_add_filter(chain_handle: int, filter_handle: int, position: int, config: int) -> int:
    """Mock add filter to chain."""
    return MCP_OK

def mcp_filter_chain_remove_filter(chain_handle: int, filter_handle: int) -> int:
    """Mock remove filter from chain."""
    return MCP_OK

def mcp_filter_chain_start(chain_handle: int) -> int:
    """Mock start filter chain."""
    return MCP_OK

def mcp_filter_chain_stop(chain_handle: int) -> int:
    """Mock stop filter chain."""
    return MCP_OK

def mcp_filter_chain_process(chain_handle: int, data: int, length: int) -> int:
    """Mock process data through filter chain."""
    return MCP_OK

def mcp_filter_chain_build(chain_handle: int) -> int:
    """Mock build filter chain."""
    return MCP_OK

def create_filter_manager(connection: int, dispatcher: int) -> int:
    """Mock create filter manager."""
    return 1

def initialize_filter_manager(manager_handle: int) -> int:
    """Mock initialize filter manager."""
    return MCP_OK

def release_filter_manager(manager_handle: int) -> None:
    """Mock release filter manager."""
    pass

# Export the library and key functions
__all__ = [
    "mcp_filter_lib",
    "get_library_path",
    "get_library_name",
    "create_mock_dispatcher",
    "create_mock_connection", 
    "create_mock_memory_pool",
    "check_result",
    # C API Functions
    "mcp_filter_create", "mcp_filter_destroy", "mcp_filter_start", "mcp_filter_stop",
    "mcp_filter_set_callbacks", "mcp_filter_post_data", "mcp_filter_chain_create",
    "mcp_filter_chain_destroy", "mcp_filter_chain_add_filter", "mcp_filter_chain_remove_filter",
    "mcp_filter_chain_start", "mcp_filter_chain_stop", "mcp_filter_chain_process", "mcp_filter_chain_build",
    "mcp_filter_manager_create", "mcp_filter_manager_destroy", "mcp_filter_manager_add_filter",
    "mcp_filter_manager_remove_filter", "mcp_filter_manager_start", "mcp_filter_manager_stop",
    "mcp_filter_manager_process", "mcp_filter_get_buffer_content", "mcp_filter_update_buffer_content",
    "mcp_filter_get_buffer_slices", "mcp_filter_reserve_buffer", "mcp_filter_commit_buffer",
    "mcp_buffer_pool_create", "mcp_buffer_pool_acquire", "mcp_buffer_pool_release",
    "mcp_buffer_pool_destroy", "mcp_filter_get_stats", "mcp_filter_reset_stats",
    "create_filter_manager", "initialize_filter_manager", "release_filter_manager",
    # Constants
    "MCP_OK", "MCP_ERROR_INVALID_ARGUMENT", "MCP_ERROR_NOT_FOUND", 
    "MCP_ERROR_INVALID_STATE", "MCP_ERROR_RESOURCE_EXHAUSTED",
    "MCP_TRUE", "MCP_FALSE", "MCP_FILTER_CONTINUE", "MCP_FILTER_STOP_ITERATION",
    # Filter types
    "MCP_FILTER_TCP_PROXY", "MCP_FILTER_UDP_PROXY", "MCP_FILTER_HTTP_CODEC",
    "MCP_FILTER_HTTP_ROUTER", "MCP_FILTER_HTTP_COMPRESSION", "MCP_FILTER_TLS_TERMINATION",
    "MCP_FILTER_AUTHENTICATION", "MCP_FILTER_AUTHORIZATION", "MCP_FILTER_ACCESS_LOG",
    "MCP_FILTER_METRICS", "MCP_FILTER_TRACING", "MCP_FILTER_RATE_LIMIT",
    "MCP_FILTER_CIRCUIT_BREAKER", "MCP_FILTER_RETRY", "MCP_FILTER_LOAD_BALANCER",
    "MCP_FILTER_CUSTOM",
]
