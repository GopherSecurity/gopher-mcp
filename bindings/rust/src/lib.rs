//! Rust bindings for MCP C++ SDK
//!
//! This crate provides safe Rust bindings for the MCP C++ SDK using FFI.
//! The C API provides a stable ABI that Rust can safely call.
//!
//! # Example
//!
//! ```rust,no_run
//! use mcp_sdk::{Library, Dispatcher, TransportType};
//!
//! fn main() -> Result<(), Box<dyn std::error::Error>> {
//!     // Initialize library
//!     let lib = Library::init()?;
//!     
//!     // Create dispatcher
//!     let dispatcher = lib.create_dispatcher()?;
//!     
//!     // Create client
//!     let client = dispatcher.create_client(
//!         TransportType::Stdio,
//!         "Rust MCP Client",
//!         "1.0.0"
//!     )?;
//!     
//!     // Connect and initialize
//!     client.connect()?;
//!     client.initialize()?;
//!     
//!     // Run event loop
//!     dispatcher.run()?;
//!     
//!     Ok(())
//! }
//! ```

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::ptr;
use std::sync::{Arc, Mutex, Once};
use std::collections::HashMap;
use std::pin::Pin;
use std::sync::atomic::{AtomicUsize, Ordering};

// Include the generated bindings
// In a real implementation, you would use bindgen to generate these
// bindgen --header mcp_c_api.h --output src/bindings.rs
// mod bindings;
// use bindings::*;

// For this example, we'll manually define the key types and functions
#[repr(C)]
pub struct mcp_dispatcher_impl;
pub type mcp_dispatcher_t = *mut mcp_dispatcher_impl;

#[repr(C)]
pub struct mcp_connection_impl;
pub type mcp_connection_t = *mut mcp_connection_impl;

#[repr(C)]
pub struct mcp_client_impl;
pub type mcp_client_t = *mut mcp_client_impl;

#[repr(C)]
pub struct mcp_server_impl;
pub type mcp_server_t = *mut mcp_server_impl;

#[repr(C)]
pub struct mcp_json_value_impl;
pub type mcp_json_value_t = *mut mcp_json_value_impl;

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum mcp_result_t {
    MCP_OK = 0,
    MCP_ERROR = -1,
    MCP_ERROR_INVALID_ARGUMENT = -2,
    MCP_ERROR_OUT_OF_MEMORY = -3,
    MCP_ERROR_NOT_CONNECTED = -4,
    MCP_ERROR_TIMEOUT = -5,
    MCP_ERROR_CANCELLED = -6,
    MCP_ERROR_NOT_FOUND = -7,
    MCP_ERROR_ALREADY_EXISTS = -8,
    MCP_ERROR_PERMISSION_DENIED = -9,
    MCP_ERROR_RESOURCE_EXHAUSTED = -10,
    MCP_ERROR_INVALID_STATE = -11,
    MCP_ERROR_PROTOCOL = -12,
    MCP_ERROR_NOT_IMPLEMENTED = -13,
    MCP_ERROR_IO = -14,
    MCP_ERROR_SSL = -15,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum mcp_connection_state_t {
    MCP_CONNECTION_STATE_CONNECTING = 0,
    MCP_CONNECTION_STATE_CONNECTED = 1,
    MCP_CONNECTION_STATE_DISCONNECTING = 2,
    MCP_CONNECTION_STATE_DISCONNECTED = 3,
    MCP_CONNECTION_STATE_ERROR = 4,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum mcp_transport_type_t {
    MCP_TRANSPORT_TCP = 0,
    MCP_TRANSPORT_SSL = 1,
    MCP_TRANSPORT_HTTP_SSE = 2,
    MCP_TRANSPORT_STDIO = 3,
    MCP_TRANSPORT_PIPE = 4,
}

#[repr(C)]
pub struct mcp_string_t {
    pub data: *const c_char,
    pub length: usize,
}

// FFI function declarations
#[link(name = "mcp_c")]
extern "C" {
    fn mcp_init(allocator: *const c_void) -> mcp_result_t;
    fn mcp_shutdown();
    fn mcp_get_version() -> *const c_char;
    fn mcp_get_last_error() -> *const c_char;
    
    fn mcp_dispatcher_create() -> mcp_dispatcher_t;
    fn mcp_dispatcher_run(dispatcher: mcp_dispatcher_t) -> mcp_result_t;
    fn mcp_dispatcher_run_timeout(dispatcher: mcp_dispatcher_t, timeout_ms: u32) -> mcp_result_t;
    fn mcp_dispatcher_stop(dispatcher: mcp_dispatcher_t);
    fn mcp_dispatcher_destroy(dispatcher: mcp_dispatcher_t);
    fn mcp_dispatcher_is_thread(dispatcher: mcp_dispatcher_t) -> bool;
    
    fn mcp_connection_create_client(
        dispatcher: mcp_dispatcher_t,
        transport: mcp_transport_type_t,
    ) -> mcp_connection_t;
    fn mcp_connection_connect(connection: mcp_connection_t) -> mcp_result_t;
    fn mcp_connection_write(
        connection: mcp_connection_t,
        data: *const u8,
        length: usize,
        callback: *const c_void,
        user_data: *mut c_void,
    ) -> mcp_result_t;
    fn mcp_connection_close(connection: mcp_connection_t, flush: bool) -> mcp_result_t;
    fn mcp_connection_get_state(connection: mcp_connection_t) -> mcp_connection_state_t;
    fn mcp_connection_destroy(connection: mcp_connection_t);
}

// Error type
#[derive(Debug)]
pub enum Error {
    InitFailed(String),
    InvalidArgument(String),
    NotConnected,
    Timeout,
    InvalidState(String),
    IoError(String),
    FfiError(mcp_result_t),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::InitFailed(msg) => write!(f, "Initialization failed: {}", msg),
            Error::InvalidArgument(msg) => write!(f, "Invalid argument: {}", msg),
            Error::NotConnected => write!(f, "Not connected"),
            Error::Timeout => write!(f, "Operation timed out"),
            Error::InvalidState(msg) => write!(f, "Invalid state: {}", msg),
            Error::IoError(msg) => write!(f, "I/O error: {}", msg),
            Error::FfiError(code) => write!(f, "FFI error: {:?}", code),
        }
    }
}

impl std::error::Error for Error {}

pub type Result<T> = std::result::Result<T, Error>;

// Convert C result to Rust Result
fn check_result(result: mcp_result_t) -> Result<()> {
    match result {
        mcp_result_t::MCP_OK => Ok(()),
        code => {
            let error_msg = unsafe {
                let ptr = mcp_get_last_error();
                if !ptr.is_null() {
                    CStr::from_ptr(ptr).to_string_lossy().to_string()
                } else {
                    format!("Unknown error: {:?}", code)
                }
            };
            Err(Error::FfiError(code))
        }
    }
}

// Transport type
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransportType {
    Tcp,
    Ssl,
    HttpSse,
    Stdio,
    Pipe,
}

impl Into<mcp_transport_type_t> for TransportType {
    fn into(self) -> mcp_transport_type_t {
        match self {
            TransportType::Tcp => mcp_transport_type_t::MCP_TRANSPORT_TCP,
            TransportType::Ssl => mcp_transport_type_t::MCP_TRANSPORT_SSL,
            TransportType::HttpSse => mcp_transport_type_t::MCP_TRANSPORT_HTTP_SSE,
            TransportType::Stdio => mcp_transport_type_t::MCP_TRANSPORT_STDIO,
            TransportType::Pipe => mcp_transport_type_t::MCP_TRANSPORT_PIPE,
        }
    }
}

// Connection state
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionState {
    Connecting,
    Connected,
    Disconnecting,
    Disconnected,
    Error,
}

impl From<mcp_connection_state_t> for ConnectionState {
    fn from(state: mcp_connection_state_t) -> Self {
        match state {
            mcp_connection_state_t::MCP_CONNECTION_STATE_CONNECTING => ConnectionState::Connecting,
            mcp_connection_state_t::MCP_CONNECTION_STATE_CONNECTED => ConnectionState::Connected,
            mcp_connection_state_t::MCP_CONNECTION_STATE_DISCONNECTING => ConnectionState::Disconnecting,
            mcp_connection_state_t::MCP_CONNECTION_STATE_DISCONNECTED => ConnectionState::Disconnected,
            mcp_connection_state_t::MCP_CONNECTION_STATE_ERROR => ConnectionState::Error,
        }
    }
}

// Library initialization
static INIT: Once = Once::new();
static mut INITIALIZED: bool = false;

/// MCP Library handle
pub struct Library {
    _private: (),
}

impl Library {
    /// Initialize the MCP library
    pub fn init() -> Result<Self> {
        let mut result = mcp_result_t::MCP_OK;
        
        INIT.call_once(|| {
            unsafe {
                result = mcp_init(ptr::null());
                INITIALIZED = result == mcp_result_t::MCP_OK;
            }
        });
        
        if unsafe { INITIALIZED } {
            Ok(Library { _private: () })
        } else {
            Err(Error::InitFailed(
                "Failed to initialize MCP library".to_string()
            ))
        }
    }
    
    /// Get library version
    pub fn version(&self) -> String {
        unsafe {
            let ptr = mcp_get_version();
            if !ptr.is_null() {
                CStr::from_ptr(ptr).to_string_lossy().to_string()
            } else {
                "Unknown".to_string()
            }
        }
    }
    
    /// Create a new dispatcher
    pub fn create_dispatcher(&self) -> Result<Dispatcher> {
        unsafe {
            let handle = mcp_dispatcher_create();
            if handle.is_null() {
                return Err(Error::InitFailed(
                    "Failed to create dispatcher".to_string()
                ));
            }
            
            Ok(Dispatcher {
                handle,
                _marker: std::marker::PhantomData,
            })
        }
    }
}

impl Drop for Library {
    fn drop(&mut self) {
        // Library shutdown is handled globally, not per instance
    }
}

/// Event dispatcher
pub struct Dispatcher {
    handle: mcp_dispatcher_t,
    _marker: std::marker::PhantomData<*const ()>,
}

unsafe impl Send for Dispatcher {}
unsafe impl Sync for Dispatcher {}

impl Dispatcher {
    /// Run the dispatcher (blocks until stopped)
    pub fn run(&self) -> Result<()> {
        unsafe {
            check_result(mcp_dispatcher_run(self.handle))
        }
    }
    
    /// Run the dispatcher with timeout
    pub fn run_timeout(&self, timeout_ms: u32) -> Result<()> {
        unsafe {
            check_result(mcp_dispatcher_run_timeout(self.handle, timeout_ms))
        }
    }
    
    /// Stop the dispatcher
    pub fn stop(&self) {
        unsafe {
            mcp_dispatcher_stop(self.handle);
        }
    }
    
    /// Check if current thread is dispatcher thread
    pub fn is_dispatcher_thread(&self) -> bool {
        unsafe {
            mcp_dispatcher_is_thread(self.handle)
        }
    }
    
    /// Create a new connection
    pub fn create_connection(&self, transport: TransportType) -> Result<Connection> {
        unsafe {
            let handle = mcp_connection_create_client(self.handle, transport.into());
            if handle.is_null() {
                return Err(Error::InitFailed(
                    "Failed to create connection".to_string()
                ));
            }
            
            Ok(Connection {
                handle,
                dispatcher: self.handle,
                callbacks: Arc::new(Mutex::new(None)),
            })
        }
    }
    
    /// Create an MCP client
    pub fn create_client(
        &self,
        transport: TransportType,
        name: &str,
        version: &str,
    ) -> Result<MCPClient> {
        let connection = self.create_connection(transport)?;
        
        Ok(MCPClient {
            connection,
            name: name.to_string(),
            version: version.to_string(),
        })
    }
}

impl Drop for Dispatcher {
    fn drop(&mut self) {
        unsafe {
            mcp_dispatcher_destroy(self.handle);
        }
    }
}

/// Network connection
pub struct Connection {
    handle: mcp_connection_t,
    dispatcher: mcp_dispatcher_t,
    callbacks: Arc<Mutex<Option<ConnectionCallbacks>>>,
}

unsafe impl Send for Connection {}
unsafe impl Sync for Connection {}

/// Connection callbacks
pub struct ConnectionCallbacks {
    pub on_state_change: Option<Box<dyn Fn(ConnectionState, ConnectionState) + Send>>,
    pub on_data: Option<Box<dyn Fn(&[u8]) + Send>>,
    pub on_error: Option<Box<dyn Fn(Error) + Send>>,
}

impl Connection {
    /// Set connection callbacks
    pub fn set_callbacks(&mut self, callbacks: ConnectionCallbacks) {
        *self.callbacks.lock().unwrap() = Some(callbacks);
        
        // In a real implementation, we would register C callbacks here
        // that bridge to the Rust callbacks
    }
    
    /// Connect
    pub fn connect(&self) -> Result<()> {
        unsafe {
            check_result(mcp_connection_connect(self.handle))
        }
    }
    
    /// Write data
    pub fn write(&self, data: &[u8]) -> Result<()> {
        if data.is_empty() {
            return Ok(());
        }
        
        unsafe {
            check_result(mcp_connection_write(
                self.handle,
                data.as_ptr(),
                data.len(),
                ptr::null(),
                ptr::null_mut(),
            ))
        }
    }
    
    /// Close connection
    pub fn close(&self, flush: bool) -> Result<()> {
        unsafe {
            check_result(mcp_connection_close(self.handle, flush))
        }
    }
    
    /// Get current state
    pub fn state(&self) -> ConnectionState {
        unsafe {
            ConnectionState::from(mcp_connection_get_state(self.handle))
        }
    }
}

impl Drop for Connection {
    fn drop(&mut self) {
        unsafe {
            mcp_connection_destroy(self.handle);
        }
    }
}

/// MCP Client
pub struct MCPClient {
    connection: Connection,
    name: String,
    version: String,
}

impl MCPClient {
    /// Connect to server
    pub fn connect(&self) -> Result<()> {
        self.connection.connect()
    }
    
    /// Initialize protocol
    pub fn initialize(&self) -> Result<()> {
        // Create initialize request
        let request = serde_json::json!({
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {
                    "name": self.name,
                    "version": self.version
                }
            },
            "id": 1
        });
        
        let data = serde_json::to_vec(&request).map_err(|e| {
            Error::IoError(format!("Failed to serialize request: {}", e))
        })?;
        
        self.connection.write(&data)
    }
    
    /// Send request
    pub fn send_request(&self, method: &str, params: serde_json::Value) -> Result<()> {
        let request = serde_json::json!({
            "jsonrpc": "2.0",
            "method": method,
            "params": params,
            "id": self.next_request_id()
        });
        
        let data = serde_json::to_vec(&request).map_err(|e| {
            Error::IoError(format!("Failed to serialize request: {}", e))
        })?;
        
        self.connection.write(&data)
    }
    
    fn next_request_id(&self) -> u64 {
        static COUNTER: AtomicUsize = AtomicUsize::new(1);
        COUNTER.fetch_add(1, Ordering::SeqCst) as u64
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_library_init() {
        let lib = Library::init().expect("Failed to init library");
        let version = lib.version();
        assert!(!version.is_empty());
    }
    
    #[test]
    fn test_create_dispatcher() {
        let lib = Library::init().expect("Failed to init library");
        let dispatcher = lib.create_dispatcher().expect("Failed to create dispatcher");
        assert!(!dispatcher.is_dispatcher_thread());
    }
}