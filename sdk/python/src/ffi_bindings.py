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
                "build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
                "build/src/c_api/libgopher_mcp_c.dylib",
                "build/lib/libgopher_mcp_c.dylib",
                "/usr/local/lib/libgopher_mcp_c.dylib",
                "/opt/homebrew/lib/libgopher_mcp_c.dylib",
            ],
        },
        "arm64": {
            "name": "libgopher_mcp_c.dylib",
            "search_paths": [
                "build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
                "build/src/c_api/libgopher_mcp_c.dylib",
                "build/lib/libgopher_mcp_c.dylib",
                "/usr/local/lib/libgopher_mcp_c.dylib",
                "/opt/homebrew/lib/libgopher_mcp_c.dylib",
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
        
        # Buffer operations
        ("mcp_filter_buffer_create", c_uint64, [c_void_p, c_size_t, c_uint32]),
        ("mcp_filter_buffer_release", None, [c_uint64]),
        ("mcp_filter_buffer_length", c_size_t, [c_uint64]),
        ("mcp_buffer_peek", c_int, [c_uint64, c_size_t, c_void_p, c_size_t]),
        ("mcp_filter_get_buffer_slices", c_int, [c_uint64, c_void_p, c_void_p]),
        ("mcp_filter_reserve_buffer", c_int, [c_uint64, c_size_t, c_void_p]),
        ("mcp_filter_commit_buffer", c_int, [c_uint64, c_size_t]),
        # Note: Many buffer functions are defined in headers but not implemented in the C++ library
        # Only the functions actually implemented in mcp_c_filter_api.cc are included here
        # Buffer pool operations
        ("mcp_buffer_pool_create", c_void_p, [c_size_t, c_size_t]),
        ("mcp_buffer_pool_acquire", c_uint64, [c_void_p]),
        ("mcp_buffer_pool_release", None, [c_void_p, c_uint64]),
        ("mcp_buffer_pool_destroy", None, [c_void_p]),
        # Filter manager operations
        ("mcp_filter_manager_create", c_uint64, [c_uint64, c_uint64]),
        ("mcp_filter_manager_add_filter", c_int, [c_uint64, c_uint64]),
        ("mcp_filter_manager_add_chain", c_int, [c_uint64, c_uint64]),
        ("mcp_filter_manager_initialize", c_int, [c_uint64]),
        ("mcp_filter_manager_release", None, [c_uint64]),
        # Filter statistics
        ("mcp_filter_get_stats", c_int, [c_uint64, c_void_p]),
        ("mcp_filter_reset_stats", c_int, [c_uint64]),
        # Filter resource guard
        ("mcp_filter_guard_create", c_void_p, [c_uint64]),
        ("mcp_filter_guard_add_filter", c_int, [c_void_p, c_uint64]),
        ("mcp_filter_guard_release", None, [c_void_p]),
        # Thread-safe operations
        ("mcp_filter_post_data", c_int, [c_uint64, c_void_p, c_size_t, c_void_p, c_void_p]),
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

# Export the library and key functions
__all__ = [
    "mcp_filter_lib",
    "get_library_path",
    "get_library_name",
    "create_mock_dispatcher",
    "create_mock_connection", 
    "create_mock_memory_pool",
    "check_result",
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
] + [func_name for func_name, _, _ in function_list if func_name in globals()]
