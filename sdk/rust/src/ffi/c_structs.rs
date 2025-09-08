//! # C Struct Definitions
//!
//! This module defines Rust equivalents of the C structures used by the MCP Filter API.
//! These structures are used for FFI communication with the C++ library.

use serde::{Deserialize, Serialize};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};

/// Filter status codes (mirroring C API)
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum FilterStatus {
    Continue = 0,
    Stop = 1,
    StopAndBuffer = 2,
}

/// Filter callback function types (matching C API signatures)
pub type OnDataCallback =
    extern "C" fn(buffer_handle: u64, end_stream: c_int, user_data: *const c_void) -> c_int;
pub type OnWriteCallback =
    extern "C" fn(buffer_handle: u64, end_stream: c_int, user_data: *const c_void) -> c_int;
pub type OnNewConnectionCallback = extern "C" fn(state: c_int, user_data: *const c_void) -> c_int;
pub type OnHighWatermarkCallback = extern "C" fn(filter: u64, user_data: *const c_void);
pub type OnLowWatermarkCallback = extern "C" fn(filter: u64, user_data: *const c_void);
pub type OnErrorCallback =
    extern "C" fn(filter: u64, error: c_int, message: *const c_char, user_data: *const c_void);

/// Filter callbacks structure (mirroring C API)
#[repr(C)]
#[derive(Debug, Clone)]
pub struct McpFilterCallbacks {
    pub on_data: Option<OnDataCallback>,
    pub on_write: Option<OnWriteCallback>,
    pub on_new_connection: Option<OnNewConnectionCallback>,
    pub on_high_watermark: Option<OnHighWatermarkCallback>,
    pub on_low_watermark: Option<OnLowWatermarkCallback>,
    pub on_error: Option<OnErrorCallback>,
    pub user_data: *const c_void,
}

impl Default for McpFilterCallbacks {
    fn default() -> Self {
        Self {
            on_data: None,
            on_write: None,
            on_new_connection: None,
            on_high_watermark: None,
            on_low_watermark: None,
            on_error: None,
            user_data: std::ptr::null(),
        }
    }
}

/// Filter configuration structure
#[repr(C)]
#[derive(Debug, Clone)]
pub struct McpFilterConfig {
    pub name: *const c_char,
    pub version: *const c_char,
    pub enabled: c_int,
    pub priority: c_int,
}

impl McpFilterConfig {
    pub fn new(name: &str, version: &str, enabled: bool, priority: i32) -> Self {
        Self {
            name: CString::new(name).unwrap().into_raw(),
            version: CString::new(version).unwrap().into_raw(),
            enabled: if enabled { 1 } else { 0 },
            priority,
        }
    }

    pub fn name(&self) -> String {
        unsafe { CStr::from_ptr(self.name).to_string_lossy().to_string() }
    }

    pub fn version(&self) -> String {
        unsafe { CStr::from_ptr(self.version).to_string_lossy().to_string() }
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled != 0
    }
}

impl Drop for McpFilterConfig {
    fn drop(&mut self) {
        unsafe {
            if !self.name.is_null() {
                let _ = CString::from_raw(self.name as *mut c_char);
            }
            if !self.version.is_null() {
                let _ = CString::from_raw(self.version as *mut c_char);
            }
        }
    }
}

/// Filter chain configuration
#[repr(C)]
#[derive(Debug, Clone)]
pub struct McpChainConfig {
    pub name: *const c_char,
    pub execution_mode: c_int,
    pub routing_strategy: c_int,
    pub max_filters: c_int,
}

impl McpChainConfig {
    pub fn new(
        name: &str,
        execution_mode: ChainExecutionMode,
        routing_strategy: RoutingStrategy,
        max_filters: i32,
    ) -> Self {
        Self {
            name: CString::new(name).unwrap().into_raw(),
            execution_mode: execution_mode as c_int,
            routing_strategy: routing_strategy as c_int,
            max_filters,
        }
    }

    pub fn name(&self) -> String {
        unsafe { CStr::from_ptr(self.name).to_string_lossy().to_string() }
    }
}

impl Drop for McpChainConfig {
    fn drop(&mut self) {
        unsafe {
            if !self.name.is_null() {
                let _ = CString::from_raw(self.name as *mut c_char);
            }
        }
    }
}

/// Chain execution modes
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChainExecutionMode {
    Sequential = 0,
    Parallel = 1,
    Conditional = 2,
}

/// Routing strategies
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RoutingStrategy {
    FirstMatch = 0,
    AllMatching = 1,
    Weighted = 2,
}

/// Filter node structure
#[repr(C)]
#[derive(Debug, Clone)]
pub struct McpFilterNode {
    pub filter_id: c_int,
    pub filter_type: c_int,
    pub config: *const McpFilterConfig,
    pub callbacks: *const McpFilterCallbacks,
    pub next: *mut McpFilterNode,
}

impl McpFilterNode {
    pub fn new(
        filter_id: i32,
        filter_type: i32,
        config: *const McpFilterConfig,
        callbacks: *const McpFilterCallbacks,
    ) -> Self {
        Self {
            filter_id,
            filter_type,
            config,
            callbacks,
            next: std::ptr::null_mut(),
        }
    }
}

/// Protocol metadata structure
#[repr(C)]
#[derive(Debug, Clone)]
pub struct McpProtocolMetadata {
    pub protocol: *const c_char,
    pub version: *const c_char,
    pub encoding: *const c_char,
    pub compression: *const c_char,
}

impl McpProtocolMetadata {
    pub fn new(protocol: &str, version: &str, encoding: &str, compression: &str) -> Self {
        Self {
            protocol: CString::new(protocol).unwrap().into_raw(),
            version: CString::new(version).unwrap().into_raw(),
            encoding: CString::new(encoding).unwrap().into_raw(),
            compression: CString::new(compression).unwrap().into_raw(),
        }
    }

    pub fn protocol(&self) -> String {
        unsafe { CStr::from_ptr(self.protocol).to_string_lossy().to_string() }
    }

    pub fn version(&self) -> String {
        unsafe { CStr::from_ptr(self.version).to_string_lossy().to_string() }
    }

    pub fn encoding(&self) -> String {
        unsafe { CStr::from_ptr(self.encoding).to_string_lossy().to_string() }
    }

    pub fn compression(&self) -> String {
        unsafe {
            CStr::from_ptr(self.compression)
                .to_string_lossy()
                .to_string()
        }
    }
}

impl Drop for McpProtocolMetadata {
    fn drop(&mut self) {
        unsafe {
            if !self.protocol.is_null() {
                let _ = CString::from_raw(self.protocol as *mut c_char);
            }
            if !self.version.is_null() {
                let _ = CString::from_raw(self.version as *mut c_char);
            }
            if !self.encoding.is_null() {
                let _ = CString::from_raw(self.encoding as *mut c_char);
            }
            if !self.compression.is_null() {
                let _ = CString::from_raw(self.compression as *mut c_char);
            }
        }
    }
}

/// Buffer handle type
pub type BufferHandle = usize;

/// Filter handle type
pub type FilterHandle = u64;

/// Filter manager handle type
pub type FilterManagerHandle = usize;

/// Chain handle type
pub type ChainHandle = usize;

/// Utility functions for creating C structs from Rust types
pub fn create_filter_config_struct(
    name: &str,
    version: &str,
    enabled: bool,
    priority: i32,
) -> McpFilterConfig {
    McpFilterConfig::new(name, version, enabled, priority)
}

pub fn create_chain_config_struct(
    name: &str,
    execution_mode: ChainExecutionMode,
    routing_strategy: RoutingStrategy,
    max_filters: i32,
) -> McpChainConfig {
    McpChainConfig::new(name, execution_mode, routing_strategy, max_filters)
}

pub fn create_filter_callbacks_struct() -> McpFilterCallbacks {
    McpFilterCallbacks::default()
}

pub fn create_protocol_metadata_struct(
    protocol: &str,
    version: &str,
    encoding: &str,
    compression: &str,
) -> McpProtocolMetadata {
    McpProtocolMetadata::new(protocol, version, encoding, compression)
}

pub fn create_filter_node_struct(
    filter_id: i32,
    filter_type: i32,
    config: *const McpFilterConfig,
    callbacks: *const McpFilterCallbacks,
) -> McpFilterNode {
    McpFilterNode::new(filter_id, filter_type, config, callbacks)
}

/// Free a C struct (for cleanup)
pub unsafe fn free_struct<T>(ptr: *mut T) {
    if !ptr.is_null() {
        let _ = Box::from_raw(ptr);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_filter_config_creation() {
        let config = create_filter_config_struct("test_filter", "1.0.0", true, 100);
        assert_eq!(config.name(), "test_filter");
        assert_eq!(config.version(), "1.0.0");
        assert!(config.is_enabled());
    }

    #[test]
    fn test_chain_config_creation() {
        let config = create_chain_config_struct(
            "test_chain",
            ChainExecutionMode::Sequential,
            RoutingStrategy::FirstMatch,
            10,
        );
        assert_eq!(config.name(), "test_chain");
    }

    #[test]
    fn test_protocol_metadata_creation() {
        let metadata = create_protocol_metadata_struct("http", "1.1", "utf-8", "gzip");
        assert_eq!(metadata.protocol(), "http");
        assert_eq!(metadata.version(), "1.1");
        assert_eq!(metadata.encoding(), "utf-8");
        assert_eq!(metadata.compression(), "gzip");
    }
}
