"""
C struct conversion functions for MCP Filter CApiFilter integration.

This module provides Python wrappers for C struct conversion and callback registration,
enabling CApiFilter integration with Python callbacks similar to the TypeScript SDK.
"""

import ctypes
from typing import Any, Dict, List, Optional, Callable, Union
from ctypes import (
    Structure, c_void_p, c_uint64, c_int32, c_uint32, c_bool, c_char_p,
    c_size_t, c_int, c_uint, c_ulong, c_long, c_double, c_float, POINTER,
    c_char, c_ubyte, c_byte, c_short, c_ushort, c_longlong, c_ulonglong,
    CFUNCTYPE
)

# Global callback store to prevent garbage collection
global_callback_store: Dict[str, Any] = {}


# ============================================================================
# C Function Pointer Types
# ============================================================================

# Data callback: int (*on_data)(void *buf, bool end_stream, void *user_data)
DataCallback = CFUNCTYPE(c_int, c_void_p, c_bool, c_void_p)

# Write callback: int (*on_write)(void *buf, bool end_stream, void *user_data)
WriteCallback = CFUNCTYPE(c_int, c_void_p, c_bool, c_void_p)

# Connection callback: void (*on_new_connection)(void *user_data, int fd)
ConnCallback = CFUNCTYPE(None, c_void_p, c_int)

# Watermark callbacks: void (*on_high_watermark)(void *user_data)
MarkCallback = CFUNCTYPE(None, c_void_p)

# Error callback: void (*on_error)(void *user_data, int code, const char *msg)
ErrorCallback = CFUNCTYPE(None, c_void_p, c_int, c_char_p)


# ============================================================================
# C Struct Definitions
# ============================================================================

class McpFilterCallbacks(Structure):
    """C struct for MCP filter callbacks."""
    _fields_ = [
        ("on_data", DataCallback),
        ("on_write", WriteCallback),
        ("on_new_connection", ConnCallback),
        ("on_high_watermark", MarkCallback),
        ("on_low_watermark", MarkCallback),
        ("on_error", ErrorCallback),
        ("user_data", c_void_p),
    ]


class McpProtocolMetadata(Structure):
    """C struct for MCP protocol metadata."""
    _fields_ = [
        ("layer", c_int32),
        ("data", c_void_p),  # Union of different layer data
    ]


class McpFilterStats(Structure):
    """C struct for MCP filter statistics."""
    _fields_ = [
        ("bytes_processed", c_uint64),
        ("packets_processed", c_uint64),
        ("errors", c_uint32),
        ("processing_time_us", c_uint64),
        ("throughput_mbps", c_double),
    ]


class McpBufferSlice(Structure):
    """C struct for MCP buffer slice."""
    _fields_ = [
        ("data", c_void_p),
        ("length", c_size_t),
        ("offset", c_size_t),
    ]


# ============================================================================
# Callback Registration Functions
# ============================================================================

def register_python_callback(func: Callable, signature: str, callback_name: str = None) -> Any:
    """
    Register a Python function as a C callback.
    
    Args:
        func: Python function to register
        signature: C function signature (e.g., "int (*)(void *, bool, void *)")
        callback_name: Optional name for the callback (for debugging)
        
    Returns:
        C function pointer that can be used in C structs
    """
    if callback_name is None:
        callback_name = f"callback_{id(func)}"
    
    # Map signature strings to CFUNCTYPE types
    signature_map = {
        "int (*)(void *, bool, void *)": DataCallback,
        "int (*)(void *, bool, void *)": WriteCallback,
        "void (*)(void *, int)": ConnCallback,
        "void (*)(void *)": MarkCallback,
        "void (*)(void *, int, char *)": ErrorCallback,
    }
    
    if signature not in signature_map:
        raise ValueError(f"Unsupported callback signature: {signature}")
    
    cfunc_type = signature_map[signature]
    cfunc = cfunc_type(func)
    
    # Store in global registry to prevent garbage collection
    global_callback_store[callback_name] = cfunc
    
    return cfunc


def create_callback_function_pointer(func: Callable, callback_type: str) -> Any:
    """
    Create a C function pointer from a Python function.
    
    Args:
        func: Python function to convert
        callback_type: Type of callback ("data", "write", "connection", "mark", "error")
        
    Returns:
        C function pointer
    """
    callback_type_map = {
        "data": ("int (*)(void *, bool, void *)", "on_data"),
        "write": ("int (*)(void *, bool, void *)", "on_write"),
        "connection": ("void (*)(void *, int)", "on_new_connection"),
        "mark": ("void (*)(void *)", "on_high_watermark"),
        "error": ("void (*)(void *, int, char *)", "on_error"),
    }
    
    if callback_type not in callback_type_map:
        raise ValueError(f"Unsupported callback type: {callback_type}")
    
    signature, name = callback_type_map[callback_type]
    callback_name = f"{name}_{id(func)}"
    
    return register_python_callback(func, signature, callback_name)


def create_default_callbacks() -> Dict[str, Any]:
    """
    Create default callback functions for testing and development.
    
    Returns:
        Dictionary with default callback functions
    """
    def default_data_callback(buf: c_void_p, end_stream: bool, user_data: c_void_p) -> int:
        """Default data callback that logs and returns CONTINUE."""
        print(f"🔍 [CApiFilter DEBUG] onData callback called! Buffer: {buf}, EndStream: {end_stream}")
        return 0  # MCP_FILTER_CONTINUE
    
    def default_write_callback(buf: c_void_p, end_stream: bool, user_data: c_void_p) -> int:
        """Default write callback that logs and returns CONTINUE."""
        print(f"🔍 [CApiFilter DEBUG] onWrite callback called! Buffer: {buf}, EndStream: {end_stream}")
        return 0  # MCP_FILTER_CONTINUE
    
    def default_connection_callback(user_data: c_void_p, fd: int) -> None:
        """Default connection callback that logs."""
        print(f"🔍 [CApiFilter DEBUG] onNewConnection callback called! FD: {fd}")
    
    def default_mark_callback(user_data: c_void_p) -> None:
        """Default watermark callback that logs."""
        print(f"🔍 [CApiFilter DEBUG] onHighWatermark callback called!")
    
    def default_error_callback(user_data: c_void_p, code: int, msg: c_char_p) -> None:
        """Default error callback that logs."""
        message = msg.value.decode('utf-8') if msg else "Unknown error"
        print(f"🔍 [CApiFilter DEBUG] onError callback called! Code: {code}, Message: {message}")
    
    return {
        "on_data": default_data_callback,
        "on_write": default_write_callback,
        "on_new_connection": default_connection_callback,
        "on_high_watermark": default_mark_callback,
        "on_low_watermark": default_mark_callback,
        "on_error": default_error_callback,
        "user_data": None,
    }


def validate_callback_signature(func: Callable, expected_type: str) -> bool:
    """
    Validate that a Python function has the correct signature for a callback type.
    
    Args:
        func: Python function to validate
        expected_type: Expected callback type ("data", "write", "connection", "mark", "error")
        
    Returns:
        True if signature is valid, False otherwise
    """
    import inspect
    
    try:
        sig = inspect.signature(func)
        params = list(sig.parameters.values())
        
        if expected_type == "data" or expected_type == "write":
            # Should have 3 parameters: buf, end_stream, user_data
            return len(params) == 3
        elif expected_type == "connection":
            # Should have 2 parameters: user_data, fd
            return len(params) == 2
        elif expected_type == "mark":
            # Should have 1 parameter: user_data
            return len(params) == 1
        elif expected_type == "error":
            # Should have 3 parameters: user_data, code, msg
            return len(params) == 3
        
        return False
    except Exception:
        return False


# ============================================================================
# C Struct Creation Functions
# ============================================================================

def create_filter_callbacks_struct(callbacks: Dict[str, Any]) -> McpFilterCallbacks:
    """
    Create a C struct for filter callbacks from Python callbacks.
    
    Args:
        callbacks: Dictionary containing callback functions
                  Keys: "on_data", "on_write", "on_new_connection", 
                        "on_high_watermark", "on_low_watermark", "on_error"
        
    Returns:
        McpFilterCallbacks C struct
    """
    # Create the struct
    callback_struct = McpFilterCallbacks()
    
    # Register callbacks and assign to struct fields
    if "on_data" in callbacks and callbacks["on_data"] is not None:
        callback_struct.on_data = create_callback_function_pointer(
            callbacks["on_data"], "data"
        )
    else:
        callback_struct.on_data = None
    
    if "on_write" in callbacks and callbacks["on_write"] is not None:
        callback_struct.on_write = create_callback_function_pointer(
            callbacks["on_write"], "write"
        )
    else:
        callback_struct.on_write = None
    
    if "on_new_connection" in callbacks and callbacks["on_new_connection"] is not None:
        callback_struct.on_new_connection = create_callback_function_pointer(
            callbacks["on_new_connection"], "connection"
        )
    else:
        callback_struct.on_new_connection = None
    
    if "on_high_watermark" in callbacks and callbacks["on_high_watermark"] is not None:
        callback_struct.on_high_watermark = create_callback_function_pointer(
            callbacks["on_high_watermark"], "mark"
        )
    else:
        callback_struct.on_high_watermark = None
    
    if "on_low_watermark" in callbacks and callbacks["on_low_watermark"] is not None:
        callback_struct.on_low_watermark = create_callback_function_pointer(
            callbacks["on_low_watermark"], "mark"
        )
    else:
        callback_struct.on_low_watermark = None
    
    if "on_error" in callbacks and callbacks["on_error"] is not None:
        callback_struct.on_error = create_callback_function_pointer(
            callbacks["on_error"], "error"
        )
    else:
        callback_struct.on_error = None
    
    # Set user data pointer
    callback_struct.user_data = callbacks.get("user_data", None)
    
    return callback_struct


def create_protocol_metadata_struct(metadata: Dict[str, Any]) -> McpProtocolMetadata:
    """
    Create a C struct for protocol metadata.
    
    Args:
        metadata: Dictionary containing protocol metadata
        
    Returns:
        McpProtocolMetadata C struct
    """
    metadata_struct = McpProtocolMetadata()
    metadata_struct.layer = metadata.get("layer", 0)
    metadata_struct.data = metadata.get("data", None)
    
    return metadata_struct


def create_filter_stats_struct(stats: Dict[str, Any]) -> McpFilterStats:
    """
    Create a C struct for filter statistics.
    
    Args:
        stats: Dictionary containing filter statistics
        
    Returns:
        McpFilterStats C struct
    """
    stats_struct = McpFilterStats()
    stats_struct.bytes_processed = stats.get("bytes_processed", 0)
    stats_struct.packets_processed = stats.get("packets_processed", 0)
    stats_struct.errors = stats.get("errors", 0)
    stats_struct.processing_time_us = stats.get("processing_time_us", 0)
    stats_struct.throughput_mbps = stats.get("throughput_mbps", 0.0)
    
    return stats_struct


# ============================================================================
# Utility Functions
# ============================================================================

def cleanup_callbacks() -> None:
    """Clean up all registered callbacks."""
    global global_callback_store
    global_callback_store.clear()


def get_callback_count() -> int:
    """Get the number of registered callbacks."""
    return len(global_callback_store)


def list_registered_callbacks() -> List[str]:
    """Get a list of registered callback names."""
    return list(global_callback_store.keys())


# ============================================================================
# Module Exports
# ============================================================================

__all__ = [
    # C struct types
    "McpFilterCallbacks",
    "McpProtocolMetadata", 
    "McpFilterStats",
    "McpBufferSlice",
    
    # C function pointer types
    "DataCallback",
    "WriteCallback",
    "ConnCallback",
    "MarkCallback",
    "ErrorCallback",
    
    # Callback registration functions
    "register_python_callback",
    "create_callback_function_pointer",
    "create_default_callbacks",
    "validate_callback_signature",
    
    # C struct creation functions
    "create_filter_callbacks_struct",
    "create_protocol_metadata_struct",
    "create_filter_stats_struct",
    
    # Utility functions
    "cleanup_callbacks",
    "get_callback_count",
    "list_registered_callbacks",
]
