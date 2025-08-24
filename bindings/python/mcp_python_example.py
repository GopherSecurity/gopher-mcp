"""
Python bindings example for Gopher MCP library

This demonstrates how to create Python bindings using the C API.
The same pattern can be applied to other languages like:
- TypeScript (via N-API)
- Go (via CGO)
- Rust (via FFI)
- Ruby (via FFI)
- C# (via P/Invoke)
- Swift (via bridging header)
- Kotlin (via JNI)
"""

import ctypes
from ctypes import (
    c_void_p, c_char_p, c_int, c_uint32, c_uint64, c_bool,
    c_size_t, c_double, POINTER, Structure, Union, CFUNCTYPE,
    byref, cast, create_string_buffer
)
from enum import IntEnum
from typing import Optional, Callable, Any, Dict, List
import json

# Load the MCP C library
mcp_lib = ctypes.CDLL("libmcp_c.so")  # or .dll on Windows, .dylib on macOS

# ============================================================================
# Type Definitions (matching mcp_c_types.h)
# ============================================================================

class MCPResult(IntEnum):
    """MCP result codes"""
    OK = 0
    ERROR = -1
    ERROR_INVALID_ARGUMENT = -2
    ERROR_OUT_OF_MEMORY = -3
    ERROR_NOT_CONNECTED = -4
    ERROR_TIMEOUT = -5
    ERROR_CANCELLED = -6
    ERROR_NOT_FOUND = -7
    ERROR_ALREADY_EXISTS = -8
    ERROR_PERMISSION_DENIED = -9
    ERROR_RESOURCE_EXHAUSTED = -10
    ERROR_INVALID_STATE = -11
    ERROR_PROTOCOL = -12
    ERROR_NOT_IMPLEMENTED = -13
    ERROR_IO = -14
    ERROR_SSL = -15

class MCPString(Structure):
    """C string structure"""
    _fields_ = [
        ("data", c_char_p),
        ("length", c_size_t)
    ]
    
    @classmethod
    def from_python(cls, s: str):
        """Create MCPString from Python string"""
        encoded = s.encode('utf-8')
        return cls(encoded, len(encoded))
    
    def to_python(self) -> str:
        """Convert to Python string"""
        if self.data:
            return self.data[:self.length].decode('utf-8')
        return ""

class MCPConnectionState(IntEnum):
    """Connection states"""
    CONNECTING = 0
    CONNECTED = 1
    DISCONNECTING = 2
    DISCONNECTED = 3
    ERROR = 4

class MCPTransportType(IntEnum):
    """Transport types"""
    TCP = 0
    SSL = 1
    HTTP_SSE = 2
    STDIO = 3
    PIPE = 4

# Opaque handle types
MCPDispatcher = c_void_p
MCPConnection = c_void_p
MCPClient = c_void_p
MCPServer = c_void_p
MCPJsonValue = c_void_p

# Callback types
ConnectionStateCallback = CFUNCTYPE(
    None,  # return type
    MCPConnection,  # connection
    c_int,  # old_state
    c_int,  # new_state
    c_void_p  # user_data
)

DataCallback = CFUNCTYPE(
    None,
    MCPConnection,
    POINTER(c_uint8),  # data
    c_size_t,  # length
    c_void_p  # user_data
)

ErrorCallback = CFUNCTYPE(
    None,
    c_int,  # error code
    c_char_p,  # message
    c_void_p  # user_data
)

# ============================================================================
# Function Declarations
# ============================================================================

# Library initialization
mcp_lib.mcp_init.argtypes = [c_void_p]  # allocator (NULL for default)
mcp_lib.mcp_init.restype = c_int

mcp_lib.mcp_shutdown.argtypes = []
mcp_lib.mcp_shutdown.restype = None

mcp_lib.mcp_get_version.argtypes = []
mcp_lib.mcp_get_version.restype = c_char_p

mcp_lib.mcp_get_last_error.argtypes = []
mcp_lib.mcp_get_last_error.restype = c_char_p

# Dispatcher functions
mcp_lib.mcp_dispatcher_create.argtypes = []
mcp_lib.mcp_dispatcher_create.restype = MCPDispatcher

mcp_lib.mcp_dispatcher_run.argtypes = [MCPDispatcher]
mcp_lib.mcp_dispatcher_run.restype = c_int

mcp_lib.mcp_dispatcher_stop.argtypes = [MCPDispatcher]
mcp_lib.mcp_dispatcher_stop.restype = None

mcp_lib.mcp_dispatcher_destroy.argtypes = [MCPDispatcher]
mcp_lib.mcp_dispatcher_destroy.restype = None

# Connection functions
mcp_lib.mcp_connection_create_client.argtypes = [MCPDispatcher, c_int]
mcp_lib.mcp_connection_create_client.restype = MCPConnection

mcp_lib.mcp_connection_set_callbacks.argtypes = [
    MCPConnection,
    ConnectionStateCallback,
    DataCallback,
    ErrorCallback,
    c_void_p
]
mcp_lib.mcp_connection_set_callbacks.restype = c_int

mcp_lib.mcp_connection_connect.argtypes = [MCPConnection]
mcp_lib.mcp_connection_connect.restype = c_int

mcp_lib.mcp_connection_write.argtypes = [
    MCPConnection,
    POINTER(c_uint8),
    c_size_t,
    c_void_p,  # write callback
    c_void_p   # user data
]
mcp_lib.mcp_connection_write.restype = c_int

mcp_lib.mcp_connection_close.argtypes = [MCPConnection, c_bool]
mcp_lib.mcp_connection_close.restype = c_int

mcp_lib.mcp_connection_destroy.argtypes = [MCPConnection]
mcp_lib.mcp_connection_destroy.restype = None

# JSON functions
mcp_lib.mcp_json_parse.argtypes = [MCPString]
mcp_lib.mcp_json_parse.restype = MCPJsonValue

mcp_lib.mcp_json_release.argtypes = [MCPJsonValue]
mcp_lib.mcp_json_release.restype = None

# Utility functions
mcp_lib.mcp_string_from_cstr.argtypes = [c_char_p]
mcp_lib.mcp_string_from_cstr.restype = MCPString

# ============================================================================
# Python Wrapper Classes
# ============================================================================

class MCPLibrary:
    """Main MCP library wrapper"""
    
    def __init__(self):
        """Initialize the MCP library"""
        result = mcp_lib.mcp_init(None)  # Use default allocator
        if result != MCPResult.OK:
            raise RuntimeError(f"Failed to initialize MCP library: {result}")
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        mcp_lib.mcp_shutdown()
    
    @property
    def version(self) -> str:
        """Get library version"""
        return mcp_lib.mcp_get_version().decode('utf-8')
    
    @property
    def last_error(self) -> str:
        """Get last error message"""
        error = mcp_lib.mcp_get_last_error()
        return error.decode('utf-8') if error else ""

class Dispatcher:
    """Event dispatcher wrapper"""
    
    def __init__(self):
        """Create a new dispatcher"""
        self._handle = mcp_lib.mcp_dispatcher_create()
        if not self._handle:
            raise RuntimeError("Failed to create dispatcher")
        self._running = False
    
    def run(self):
        """Run the dispatcher (blocks)"""
        self._running = True
        result = mcp_lib.mcp_dispatcher_run(self._handle)
        self._running = False
        if result != MCPResult.OK:
            raise RuntimeError(f"Dispatcher run failed: {result}")
    
    def stop(self):
        """Stop the dispatcher"""
        if self._running:
            mcp_lib.mcp_dispatcher_stop(self._handle)
            self._running = False
    
    def __del__(self):
        """Clean up dispatcher"""
        if hasattr(self, '_handle') and self._handle:
            self.stop()
            mcp_lib.mcp_dispatcher_destroy(self._handle)

class Connection:
    """Network connection wrapper"""
    
    def __init__(self, dispatcher: Dispatcher, transport: MCPTransportType):
        """Create a new connection"""
        self._dispatcher = dispatcher
        self._handle = mcp_lib.mcp_connection_create_client(
            dispatcher._handle,
            transport.value
        )
        if not self._handle:
            raise RuntimeError("Failed to create connection")
        
        # Store callbacks to prevent GC
        self._state_cb = None
        self._data_cb = None
        self._error_cb = None
        self._user_callbacks = {}
    
    def set_callbacks(self,
                      on_state: Optional[Callable] = None,
                      on_data: Optional[Callable] = None,
                      on_error: Optional[Callable] = None):
        """Set connection callbacks"""
        
        # Create C callback wrappers
        if on_state:
            def state_wrapper(conn, old_state, new_state, user_data):
                on_state(MCPConnectionState(old_state), MCPConnectionState(new_state))
            self._state_cb = ConnectionStateCallback(state_wrapper)
        
        if on_data:
            def data_wrapper(conn, data_ptr, length, user_data):
                # Convert C data to Python bytes
                data = bytes(data_ptr[:length])
                on_data(data)
            self._data_cb = DataCallback(data_wrapper)
        
        if on_error:
            def error_wrapper(error_code, message, user_data):
                on_error(MCPResult(error_code), message.decode('utf-8'))
            self._error_cb = ErrorCallback(error_wrapper)
        
        # Set callbacks in C library
        result = mcp_lib.mcp_connection_set_callbacks(
            self._handle,
            self._state_cb or cast(None, ConnectionStateCallback),
            self._data_cb or cast(None, DataCallback),
            self._error_cb or cast(None, ErrorCallback),
            None  # user_data
        )
        
        if result != MCPResult.OK:
            raise RuntimeError(f"Failed to set callbacks: {result}")
    
    def connect(self):
        """Connect (async)"""
        result = mcp_lib.mcp_connection_connect(self._handle)
        if result != MCPResult.OK:
            raise RuntimeError(f"Failed to connect: {result}")
    
    def write(self, data: bytes):
        """Write data (async)"""
        data_array = (c_uint8 * len(data)).from_buffer_copy(data)
        result = mcp_lib.mcp_connection_write(
            self._handle,
            data_array,
            len(data),
            None,  # write callback
            None   # user data
        )
        if result != MCPResult.OK:
            raise RuntimeError(f"Failed to write: {result}")
    
    def close(self, flush: bool = True):
        """Close connection"""
        result = mcp_lib.mcp_connection_close(self._handle, flush)
        if result != MCPResult.OK:
            raise RuntimeError(f"Failed to close: {result}")
    
    def __del__(self):
        """Clean up connection"""
        if hasattr(self, '_handle') and self._handle:
            mcp_lib.mcp_connection_destroy(self._handle)

# ============================================================================
# Example Usage
# ============================================================================

def example_stdio_client():
    """Example: Create a stdio MCP client"""
    
    with MCPLibrary() as lib:
        print(f"MCP Library version: {lib.version}")
        
        # Create dispatcher
        dispatcher = Dispatcher()
        
        # Create stdio connection
        connection = Connection(dispatcher, MCPTransportType.STDIO)
        
        # Set up callbacks
        def on_state_change(old_state, new_state):
            print(f"Connection state: {old_state} -> {new_state}")
        
        def on_data_received(data):
            print(f"Received: {data.decode('utf-8')}")
        
        def on_error(error_code, message):
            print(f"Error {error_code}: {message}")
        
        connection.set_callbacks(
            on_state=on_state_change,
            on_data=on_data_received,
            on_error=on_error
        )
        
        # Connect
        connection.connect()
        
        # Send initialize request
        initialize_request = {
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {
                    "name": "Python MCP Client",
                    "version": "0.1.0"
                }
            },
            "id": 1
        }
        
        connection.write(json.dumps(initialize_request).encode('utf-8'))
        
        # Run event loop
        try:
            dispatcher.run()
        except KeyboardInterrupt:
            print("Shutting down...")
            dispatcher.stop()

if __name__ == "__main__":
    example_stdio_client()