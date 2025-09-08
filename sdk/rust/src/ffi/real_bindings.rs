//! # Real C++ Library FFI Bindings
//!
//! This module provides real FFI bindings to the actual C++ library,
//! replacing the placeholder implementations with real function calls.

use libloading::{Library, Symbol};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::sync::Arc;

use crate::ffi::error::{FilterError, FilterResult};
use crate::ffi::c_structs::{
    FilterHandle, FilterManagerHandle, ChainHandle, BufferHandle,
    McpFilterCallbacks, McpFilterConfig, McpChainConfig,
};

/// Real C++ library bindings
#[derive(Debug)]
pub struct RealLibraryBindings {
    library: Arc<Library>,
}

/// Function signatures for C++ library functions
type McpInitFn = unsafe extern "C" fn(*const c_void) -> c_int;
type McpShutdownFn = unsafe extern "C" fn();
type McpIsInitializedFn = unsafe extern "C" fn() -> c_int;
type McpGetVersionFn = unsafe extern "C" fn() -> *const c_char;
type McpGetLastErrorFn = unsafe extern "C" fn() -> *const c_void;

type McpDispatcherCreateFn = unsafe extern "C" fn() -> *const c_void;
type McpDispatcherRunFn = unsafe extern "C" fn(*const c_void) -> c_int;
type McpDispatcherStopFn = unsafe extern "C" fn(*const c_void);
type McpDispatcherDestroyFn = unsafe extern "C" fn(*const c_void);

type McpFilterCreateFn = unsafe extern "C" fn(*const c_void, *const c_void) -> u64;
type McpFilterCreateBuiltinFn = unsafe extern "C" fn(*const c_void, c_int, *const c_void) -> u64;
type McpFilterRetainFn = unsafe extern "C" fn(u64);
type McpFilterReleaseFn = unsafe extern "C" fn(u64);
type McpFilterSetCallbacksFn = unsafe extern "C" fn(u64, *const McpFilterCallbacks) -> c_int;
type McpFilterSetProtocolMetadataFn = unsafe extern "C" fn(u64, *const c_void) -> c_int;
type McpFilterGetProtocolMetadataFn = unsafe extern "C" fn(u64, *mut c_void) -> c_int;

type McpFilterChainBuilderCreateFn = unsafe extern "C" fn(*const c_void) -> *const c_void;
type McpFilterChainAddFilterFn = unsafe extern "C" fn(*const c_void, u64, c_int, u64) -> c_int;
type McpFilterChainBuildFn = unsafe extern "C" fn(*const c_void) -> u64;
type McpFilterChainBuilderDestroyFn = unsafe extern "C" fn(*const c_void);

type McpBufferCreateFn = unsafe extern "C" fn(usize) -> u64;
type McpBufferCreateOwnedFn = unsafe extern "C" fn(usize, c_int) -> u64;
type McpBufferGetDataFn = unsafe extern "C" fn(u64, *mut *const c_void, *mut usize) -> c_int;
type McpBufferGetContiguousFn = unsafe extern "C" fn(u64, usize, usize, *mut *const c_void, *mut usize) -> c_int;
type McpBufferSetDataFn = unsafe extern "C" fn(u64, *const c_void, usize) -> c_int;
type McpBufferAddFn = unsafe extern "C" fn(u64, *const c_void, usize) -> c_int;
type McpBufferGetSizeFn = unsafe extern "C" fn(u64) -> usize;
type McpBufferResizeFn = unsafe extern "C" fn(u64, usize) -> c_int;
type McpBufferDestroyFn = unsafe extern "C" fn(u64);

type McpJsonCreateStringFn = unsafe extern "C" fn(*const c_char) -> *const c_void;
type McpJsonCreateNumberFn = unsafe extern "C" fn(f64) -> *const c_void;
type McpJsonCreateBoolFn = unsafe extern "C" fn(c_int) -> *const c_void;
type McpJsonCreateNullFn = unsafe extern "C" fn() -> *const c_void;
type McpJsonFreeFn = unsafe extern "C" fn(*const c_void);
type McpJsonStringifyFn = unsafe extern "C" fn(*const c_void) -> *const c_char;

impl RealLibraryBindings {
    /// Create new real library bindings
    pub fn new() -> FilterResult<Self> {
        let library = Self::load_library()?;
        Ok(Self {
            library: Arc::new(library),
        })
    }

    /// Load the C++ library
    fn load_library() -> FilterResult<Library> {
        let library_path = Self::find_library_path()?;
        
        unsafe {
            Library::new(&library_path)
                .map_err(|e| FilterError::LibraryLoad(e))
        }
    }

    /// Find the library path based on platform
    fn find_library_path() -> FilterResult<String> {
        // Check environment variable first
        if let Ok(env_path) = std::env::var("MCP_LIBRARY_PATH") {
            if std::path::Path::new(&env_path).exists() {
                return Ok(env_path);
            }
        }

        // Platform-specific search paths
        let search_paths = match (std::env::consts::OS, std::env::consts::ARCH) {
            ("macos", "x86_64") => vec![
                "../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
                "../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
                "/usr/local/lib/libgopher_mcp_c.dylib",
                "/opt/homebrew/lib/libgopher_mcp_c.dylib",
                "/usr/lib/libgopher_mcp_c.dylib",
            ],
            ("macos", "aarch64") => vec![
                "../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
                "../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
                "/usr/local/lib/libgopher_mcp_c.dylib",
                "/opt/homebrew/lib/libgopher_mcp_c.dylib",
                "/usr/lib/libgopher_mcp_c.dylib",
            ],
            ("linux", "x86_64") => vec![
                "../../../build/src/c_api/libgopher_mcp_c.so",
                "../../../../build/src/c_api/libgopher_mcp_c.so",
                "/usr/local/lib/libgopher_mcp_c.so",
                "/usr/lib/libgopher_mcp_c.so",
            ],
            ("windows", "x86_64") => vec![
                "../../../build/src/c_api/gopher_mcp_c.dll",
                "../../../../build/src/c_api/gopher_mcp_c.dll",
                "C:\\gopher-mcp\\bin\\gopher_mcp_c.dll",
            ],
            _ => return Err(FilterError::NotSupported {
                operation: format!("Platform {} {}", std::env::consts::OS, std::env::consts::ARCH),
            }),
        };

        for path in &search_paths {
            if std::path::Path::new(path).exists() {
                return Ok(path.to_string());
            }
        }

        Err(FilterError::NotFound {
            resource: format!("MCP C++ library not found. Searched: {}", search_paths.join(", ")),
        })
    }

    /// Get a function from the library
    unsafe fn get_function<T>(&self, name: &str) -> FilterResult<Symbol<T>> {
        self.library
            .get(name.as_bytes())
            .map_err(|_e| FilterError::FunctionNotFound {
                function_name: name.to_string(),
            })
    }

    // ============================================================================
    // Core Library Functions
    // ============================================================================

    /// Initialize the MCP library
    pub fn mcp_init(&self, allocator: Option<*const c_void>) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpInitFn> = self.get_function("mcp_init")?;
            let result = func(allocator.unwrap_or(std::ptr::null()));
            if result == 0 {
                Ok(())
            } else {
                Err(FilterError::CApiError {
                    code: result,
                    message: "Failed to initialize MCP library".to_string(),
                })
            }
        }
    }

    /// Shutdown the MCP library
    pub fn mcp_shutdown(&self) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpShutdownFn> = self.get_function("mcp_shutdown")?;
            func();
            Ok(())
        }
    }

    /// Check if MCP library is initialized
    pub fn mcp_is_initialized(&self) -> FilterResult<bool> {
        unsafe {
            let func: Symbol<McpIsInitializedFn> = self.get_function("mcp_is_initialized")?;
            Ok(func() != 0)
        }
    }

    /// Get MCP library version
    pub fn mcp_get_version(&self) -> FilterResult<String> {
        unsafe {
            let func: Symbol<McpGetVersionFn> = self.get_function("mcp_get_version")?;
            let version_ptr = func();
            if version_ptr.is_null() {
                return Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to get version".to_string(),
                });
            }
            let version = CStr::from_ptr(version_ptr).to_string_lossy().to_string();
            Ok(version)
        }
    }

    /// Get last error from MCP library
    pub fn mcp_get_last_error(&self) -> FilterResult<*const c_void> {
        unsafe {
            let func: Symbol<McpGetLastErrorFn> = self.get_function("mcp_get_last_error")?;
            Ok(func())
        }
    }

    // ============================================================================
    // Dispatcher Functions
    // ============================================================================

    /// Create a dispatcher
    pub fn mcp_dispatcher_create(&self) -> FilterResult<*const c_void> {
        unsafe {
            let func: Symbol<McpDispatcherCreateFn> = self.get_function("mcp_dispatcher_create")?;
            let dispatcher = func();
            if dispatcher.is_null() {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to create dispatcher".to_string(),
                })
            } else {
                Ok(dispatcher)
            }
        }
    }

    /// Run the dispatcher
    pub fn mcp_dispatcher_run(&self, dispatcher: *const c_void) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpDispatcherRunFn> = self.get_function("mcp_dispatcher_run")?;
            let result = func(dispatcher);
            if result == 0 {
                Ok(())
            } else {
                Err(FilterError::CApiError {
                    code: result,
                    message: "Failed to run dispatcher".to_string(),
                })
            }
        }
    }

    /// Stop the dispatcher
    pub fn mcp_dispatcher_stop(&self, dispatcher: *const c_void) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpDispatcherStopFn> = self.get_function("mcp_dispatcher_stop")?;
            func(dispatcher);
            Ok(())
        }
    }

    /// Destroy the dispatcher
    pub fn mcp_dispatcher_destroy(&self, dispatcher: *const c_void) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpDispatcherDestroyFn> = self.get_function("mcp_dispatcher_destroy")?;
            func(dispatcher);
            Ok(())
        }
    }

    // ============================================================================
    // Filter Functions
    // ============================================================================

    /// Create a filter
    pub fn mcp_filter_create(&self, dispatcher: *const c_void, config: *const c_void) -> FilterResult<FilterHandle> {
        unsafe {
            let func: Symbol<McpFilterCreateFn> = self.get_function("mcp_filter_create")?;
            let handle = func(dispatcher, config);
            if handle == 0 {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to create filter".to_string(),
                })
            } else {
                Ok(handle as FilterHandle)
            }
        }
    }

    /// Create a built-in filter
    pub fn mcp_filter_create_builtin(&self, dispatcher: *const c_void, filter_type: c_int, config: *const c_void) -> FilterResult<FilterHandle> {
        unsafe {
            let func: Symbol<McpFilterCreateBuiltinFn> = self.get_function("mcp_filter_create_builtin")?;
            let handle = func(dispatcher, filter_type, config);
            if handle == 0 {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to create built-in filter".to_string(),
                })
            } else {
                Ok(handle as FilterHandle)
            }
        }
    }

    /// Retain a filter
    pub fn mcp_filter_retain(&self, filter: FilterHandle) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpFilterRetainFn> = self.get_function("mcp_filter_retain")?;
            func(filter as u64);
            Ok(())
        }
    }

    /// Release a filter
    pub fn mcp_filter_release(&self, filter: FilterHandle) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpFilterReleaseFn> = self.get_function("mcp_filter_release")?;
            func(filter as u64);
            Ok(())
        }
    }

    /// Set filter callbacks
    pub fn mcp_filter_set_callbacks(&self, filter: FilterHandle, callbacks: *const McpFilterCallbacks) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpFilterSetCallbacksFn> = self.get_function("mcp_filter_set_callbacks")?;
            let result = func(filter as u64, callbacks);
            if result == 0 {
                Ok(())
            } else {
                Err(FilterError::CApiError {
                    code: result,
                    message: "Failed to set filter callbacks".to_string(),
                })
            }
        }
    }

    /// Set filter protocol metadata
    pub fn mcp_filter_set_protocol_metadata(&self, filter: FilterHandle, metadata: *const c_void) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpFilterSetProtocolMetadataFn> = self.get_function("mcp_filter_set_protocol_metadata")?;
            let result = func(filter as u64, metadata);
            if result == 0 {
                Ok(())
            } else {
                Err(FilterError::CApiError {
                    code: result,
                    message: "Failed to set protocol metadata".to_string(),
                })
            }
        }
    }

    /// Get filter protocol metadata
    pub fn mcp_filter_get_protocol_metadata(&self, filter: FilterHandle, metadata: *mut c_void) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpFilterGetProtocolMetadataFn> = self.get_function("mcp_filter_get_protocol_metadata")?;
            let result = func(filter as u64, metadata);
            if result == 0 {
                Ok(())
            } else {
                Err(FilterError::CApiError {
                    code: result,
                    message: "Failed to get protocol metadata".to_string(),
                })
            }
        }
    }

    // ============================================================================
    // Filter Chain Functions
    // ============================================================================

    /// Create a filter chain builder
    pub fn mcp_filter_chain_builder_create(&self, dispatcher: *const c_void) -> FilterResult<*const c_void> {
        unsafe {
            let func: Symbol<McpFilterChainBuilderCreateFn> = self.get_function("mcp_filter_chain_builder_create")?;
            let builder = func(dispatcher);
            if builder.is_null() {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to create filter chain builder".to_string(),
                })
            } else {
                Ok(builder)
            }
        }
    }

    /// Add a filter to a chain
    pub fn mcp_filter_chain_add_filter(&self, builder: *const c_void, filter: FilterHandle, position: c_int, reference: u64) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpFilterChainAddFilterFn> = self.get_function("mcp_filter_chain_add_filter")?;
            let result = func(builder, filter as u64, position, reference);
            if result == 0 {
                Ok(())
            } else {
                Err(FilterError::CApiError {
                    code: result,
                    message: "Failed to add filter to chain".to_string(),
                })
            }
        }
    }

    /// Build the filter chain
    pub fn mcp_filter_chain_build(&self, builder: *const c_void) -> FilterResult<ChainHandle> {
        unsafe {
            let func: Symbol<McpFilterChainBuildFn> = self.get_function("mcp_filter_chain_build")?;
            let handle = func(builder);
            if handle == 0 {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to build filter chain".to_string(),
                })
            } else {
                Ok(handle as ChainHandle)
            }
        }
    }

    /// Destroy a filter chain builder
    pub fn mcp_filter_chain_builder_destroy(&self, builder: *const c_void) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpFilterChainBuilderDestroyFn> = self.get_function("mcp_filter_chain_builder_destroy")?;
            func(builder);
            Ok(())
        }
    }

    // ============================================================================
    // Buffer Functions
    // ============================================================================

    /// Create a buffer
    pub fn mcp_buffer_create(&self, size: usize) -> FilterResult<BufferHandle> {
        unsafe {
            let func: Symbol<McpBufferCreateOwnedFn> = self.get_function("mcp_buffer_create_owned")?;
            let handle = func(size, 1); // MCP_BUFFER_OWNERSHIP_SHARED = 1
            if handle == 0 {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to create buffer".to_string(),
                })
            } else {
                Ok(handle as BufferHandle)
            }
        }
    }

    /// Get buffer data
    pub fn mcp_buffer_get_data(&self, buffer: BufferHandle) -> FilterResult<(Vec<u8>, usize)> {
        unsafe {
            let func: Symbol<McpBufferGetContiguousFn> = self.get_function("mcp_buffer_get_contiguous")?;
            let mut data_ptr: *const c_void = std::ptr::null();
            let mut actual_length: usize = 0;
            let result = func(buffer as u64, 0, 0, &mut data_ptr, &mut actual_length);
            if result == 0 {
                let data = if data_ptr.is_null() || actual_length == 0 {
                    Vec::new()
                } else {
                    std::slice::from_raw_parts(data_ptr as *const u8, actual_length).to_vec()
                };
                Ok((data, actual_length))
            } else {
                Err(FilterError::CApiError {
                    code: result,
                    message: "Failed to get buffer data".to_string(),
                })
            }
        }
    }

    /// Set buffer data
    pub fn mcp_buffer_set_data(&self, buffer: BufferHandle, data: &[u8]) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpBufferAddFn> = self.get_function("mcp_buffer_add")?;
            let result = func(buffer as u64, data.as_ptr() as *const c_void, data.len());
            if result == 0 {
                Ok(())
            } else {
                Err(FilterError::CApiError {
                    code: result,
                    message: "Failed to add buffer data".to_string(),
                })
            }
        }
    }

    /// Get buffer size
    pub fn mcp_buffer_get_size(&self, buffer: BufferHandle) -> FilterResult<usize> {
        unsafe {
            let func: Symbol<McpBufferGetSizeFn> = self.get_function("mcp_buffer_get_size")?;
            Ok(func(buffer as u64))
        }
    }

    /// Resize buffer
    pub fn mcp_buffer_resize(&self, buffer: BufferHandle, new_size: usize) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpBufferResizeFn> = self.get_function("mcp_buffer_resize")?;
            let result = func(buffer as u64, new_size);
            if result == 0 {
                Ok(())
            } else {
                Err(FilterError::CApiError {
                    code: result,
                    message: "Failed to resize buffer".to_string(),
                })
            }
        }
    }

    /// Destroy a buffer
    pub fn mcp_buffer_destroy(&self, buffer: BufferHandle) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpBufferDestroyFn> = self.get_function("mcp_buffer_destroy")?;
            func(buffer as u64);
            Ok(())
        }
    }

    // ============================================================================
    // JSON Functions
    // ============================================================================

    /// Create JSON string
    pub fn mcp_json_create_string(&self, value: &str) -> FilterResult<*const c_void> {
        unsafe {
            let func: Symbol<McpJsonCreateStringFn> = self.get_function("mcp_json_create_string")?;
            let c_string = CString::new(value).map_err(|e| FilterError::Internal(e.to_string()))?;
            let json_ptr = func(c_string.as_ptr());
            if json_ptr.is_null() {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to create JSON string".to_string(),
                })
            } else {
                Ok(json_ptr)
            }
        }
    }

    /// Create JSON number
    pub fn mcp_json_create_number(&self, value: f64) -> FilterResult<*const c_void> {
        unsafe {
            let func: Symbol<McpJsonCreateNumberFn> = self.get_function("mcp_json_create_number")?;
            let json_ptr = func(value);
            if json_ptr.is_null() {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to create JSON number".to_string(),
                })
            } else {
                Ok(json_ptr)
            }
        }
    }

    /// Create JSON boolean
    pub fn mcp_json_create_bool(&self, value: bool) -> FilterResult<*const c_void> {
        unsafe {
            let func: Symbol<McpJsonCreateBoolFn> = self.get_function("mcp_json_create_bool")?;
            let json_ptr = func(if value { 1 } else { 0 });
            if json_ptr.is_null() {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to create JSON boolean".to_string(),
                })
            } else {
                Ok(json_ptr)
            }
        }
    }

    /// Create JSON null
    pub fn mcp_json_create_null(&self) -> FilterResult<*const c_void> {
        unsafe {
            let func: Symbol<McpJsonCreateNullFn> = self.get_function("mcp_json_create_null")?;
            let json_ptr = func();
            if json_ptr.is_null() {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to create JSON null".to_string(),
                })
            } else {
                Ok(json_ptr)
            }
        }
    }

    /// Free JSON value
    pub fn mcp_json_free(&self, json: *const c_void) -> FilterResult<()> {
        unsafe {
            let func: Symbol<McpJsonFreeFn> = self.get_function("mcp_json_free")?;
            func(json);
            Ok(())
        }
    }

    /// Stringify JSON value
    pub fn mcp_json_stringify(&self, json: *const c_void) -> FilterResult<String> {
        unsafe {
            let func: Symbol<McpJsonStringifyFn> = self.get_function("mcp_json_stringify")?;
            let string_ptr = func(json);
            if string_ptr.is_null() {
                Err(FilterError::CApiError {
                    code: -1,
                    message: "Failed to stringify JSON".to_string(),
                })
            } else {
                let string = CStr::from_ptr(string_ptr).to_string_lossy().to_string();
                Ok(string)
            }
        }
    }
}

impl Clone for RealLibraryBindings {
    fn clone(&self) -> Self {
        Self {
            library: self.library.clone(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_library_loading() {
        // This test will only pass if the library is available
        match RealLibraryBindings::new() {
            Ok(bindings) => {
                println!("✅ Successfully loaded real C++ library");
                // Test basic functions
                match bindings.mcp_is_initialized() {
                    Ok(initialized) => println!("   Library initialized: {}", initialized),
                    Err(e) => println!("   Failed to check initialization: {}", e),
                }
            }
            Err(e) => {
                println!("⚠️ Could not load real C++ library: {}", e);
                println!("   This is expected if the library is not built yet");
            }
        }
    }
}
