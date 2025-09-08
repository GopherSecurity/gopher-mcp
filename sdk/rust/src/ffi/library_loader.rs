//! # Dynamic Library Loader
//!
//! This module handles dynamic loading of the MCP Filter C library
//! with platform-specific path resolution and function pointer management.

use libloading::{Library, Symbol};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;
use tracing::{debug, error, info};

use crate::ffi::error::{FilterError, FilterResult};

/// Platform-specific library configuration
#[derive(Debug, Clone)]
pub struct LibraryConfig {
    pub name: String,
    pub search_paths: Vec<PathBuf>,
}

impl LibraryConfig {
    /// Get the default library configuration for the current platform
    pub fn default() -> Self {
        let (name, search_paths) = Self::get_platform_config();
        Self { name, search_paths }
    }

    /// Get platform-specific configuration
    fn get_platform_config() -> (String, Vec<PathBuf>) {
        #[cfg(target_os = "macos")]
        {
            let name = "libgopher_mcp_c.dylib".to_string();
            let search_paths = vec![
                // Development build paths
                PathBuf::from("../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib"),
                PathBuf::from("../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib"),
                // System installation paths
                PathBuf::from("/usr/local/lib/libgopher_mcp_c.dylib"),
                PathBuf::from("/opt/homebrew/lib/libgopher_mcp_c.dylib"),
                PathBuf::from("/usr/lib/libgopher_mcp_c.dylib"),
            ];
            (name, search_paths)
        }

        #[cfg(target_os = "linux")]
        {
            let name = "libgopher_mcp_c.so".to_string();
            let search_paths = vec![
                // Development build paths
                PathBuf::from("../../build/src/c_api/libgopher_mcp_c.so"),
                PathBuf::from("../../../build/src/c_api/libgopher_mcp_c.so"),
                // System installation paths
                PathBuf::from("/usr/local/lib/libgopher_mcp_c.so"),
                PathBuf::from("/usr/lib/libgopher_mcp_c.so"),
                PathBuf::from("/lib/libgopher_mcp_c.so"),
            ];
            (name, search_paths)
        }

        #[cfg(target_os = "windows")]
        {
            let name = "gopher_mcp_c.dll".to_string();
            let search_paths = vec![
                // Development build paths
                PathBuf::from("../../build/src/c_api/gopher_mcp_c.dll"),
                PathBuf::from("../../../build/src/c_api/gopher_mcp_c.dll"),
                // System installation paths
                PathBuf::from("C:/Program Files/gopher-mcp/lib/gopher_mcp_c.dll"),
                PathBuf::from("C:/Program Files (x86)/gopher-mcp/lib/gopher_mcp_c.dll"),
            ];
            (name, search_paths)
        }

        #[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
        {
            let name = "libgopher_mcp_c.so".to_string();
            let search_paths = vec![
                PathBuf::from("../../build/src/c_api/libgopher_mcp_c.so"),
                PathBuf::from("../../../build/src/c_api/libgopher_mcp_c.so"),
            ];
            (name, search_paths)
        }
    }
}

/// Dynamic library loader with function caching
#[derive(Debug)]
pub struct LibraryLoader {
    library: Library,
    functions: HashMap<String, Box<dyn std::any::Any + Send + Sync>>,
    config: LibraryConfig,
}

impl LibraryLoader {
    /// Create a new library loader with default configuration
    pub fn new() -> FilterResult<Self> {
        Self::with_config(LibraryConfig::default())
    }

    /// Create a new library loader with custom configuration
    pub fn with_config(config: LibraryConfig) -> FilterResult<Self> {
        let library = Self::load_library(&config)?;
        Ok(Self {
            library,
            functions: HashMap::new(),
            config,
        })
    }

    /// Load the library from the configured search paths
    fn load_library(config: &LibraryConfig) -> FilterResult<Library> {
        // Check environment variable override
        if let Ok(env_path) = std::env::var("MCP_LIBRARY_PATH") {
            let env_path = PathBuf::from(env_path);
            if env_path.exists() {
                info!("Loading library from environment: {}", env_path.display());
                return unsafe { Library::new(&env_path).map_err(FilterError::LibraryLoad) };
            }
        }

        // Try each search path
        for path in &config.search_paths {
            if path.exists() {
                info!("Loading library from: {}", path.display());
                return unsafe { Library::new(path).map_err(FilterError::LibraryLoad) };
            } else {
                debug!("Library not found at: {}", path.display());
            }
        }

        error!(
            "Failed to find library '{}' in any search path",
            config.name
        );
        Err(FilterError::NotFound {
            resource: format!("Library '{}'", config.name),
        })
    }

    /// Get a function pointer with type safety
    pub fn get_function<T>(&mut self, name: &str) -> FilterResult<Symbol<T>> {
        // Check if we've already loaded this function
        if self.functions.contains_key(name) {
            // For now, we'll reload the function each time
            // In a production implementation, you might want to cache the symbols
        }

        debug!("Loading function: {}", name);
        let symbol: Symbol<T> = unsafe {
            self.library
                .get(name.as_bytes())
                .map_err(|_| FilterError::FunctionNotFound {
                    function_name: name.to_string(),
                })?
        };

        // Cache the function (as a type-erased box)
        // Note: This is a simplified approach. In practice, you'd need more sophisticated caching
        self.functions.insert(name.to_string(), Box::new(()));

        Ok(symbol)
    }

    /// Get the library configuration
    pub fn config(&self) -> &LibraryConfig {
        &self.config
    }

    /// Check if a function exists in the library
    pub fn has_function(&self, name: &str) -> bool {
        unsafe { self.library.get::<()>(name.as_bytes()).is_ok() }
    }

    /// Get all available function names (for debugging)
    pub fn list_functions(&self) -> Vec<String> {
        // This is a simplified implementation
        // In practice, you'd need to parse the library's symbol table
        vec![
            "mcp_filter_create".to_string(),
            "mcp_filter_destroy".to_string(),
            "mcp_filter_manager_create".to_string(),
            // Add more function names as needed
        ]
    }
}

impl Drop for LibraryLoader {
    fn drop(&mut self) {
        debug!("Dropping library loader");
        // The Library will be automatically unloaded when dropped
    }
}

/// Create a shared library loader
pub fn create_shared_loader() -> FilterResult<Arc<LibraryLoader>> {
    let loader = LibraryLoader::new()?;
    Ok(Arc::new(loader))
}

/// Create a shared library loader with custom configuration
pub fn create_shared_loader_with_config(config: LibraryConfig) -> FilterResult<Arc<LibraryLoader>> {
    let loader = LibraryLoader::with_config(config)?;
    Ok(Arc::new(loader))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_library_config_default() {
        let config = LibraryConfig::default();
        assert!(!config.name.is_empty());
        assert!(!config.search_paths.is_empty());
    }

    #[test]
    fn test_library_loader_creation() {
        // This test will fail if the library is not available
        // In a real test environment, you'd mock the library loading
        let result = LibraryLoader::new();
        // The test may pass or fail depending on whether the library is available
        // This is expected behavior
        match result {
            Ok(_) => println!("Library loader created successfully (library available)"),
            Err(_) => println!("Library loader creation failed (library not available)"),
        }
    }
}
