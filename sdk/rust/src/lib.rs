//! # MCP Filter SDK for Rust
//!
//! A high-performance, memory-safe Rust SDK for the MCP Filter C API.
//! This library provides seamless integration with the C++ filter infrastructure
//! while leveraging Rust's ownership system for memory safety.
//!
//! ## Features
//!
//! - **Memory Safe**: Zero unsafe code where possible, guaranteed by Rust compiler
//! - **High Performance**: Zero-copy operations and efficient async processing
//! - **CApiFilter Integration**: Custom filters with Rust callbacks
//! - **Transport Layer**: GopherTransport for MCP protocol communication
//! - **Cross-Platform**: Works on macOS, Linux, and Windows
//!
//! ## Quick Start
//!
//! ```rust
//! use mcp_filter_sdk::{FilterManager, FilterManagerConfig};
//! use serde_json::json;
//!
//! #[tokio::main]
//! async fn main() -> Result<(), Box<dyn std::error::Error>> {
//!     // Initialize filter manager
//!     let config = FilterManagerConfig::default();
//!     let mut manager = FilterManager::new(config).await?;
//!
//!     // Process a JSON-RPC message
//!     let message = json!({
//!         "jsonrpc": "2.0",
//!         "id": 1,
//!         "method": "test",
//!         "params": {}
//!     });
//!
//!     let result = manager.process(message).await?;
//!     println!("Processed message: {}", result);
//!
//!     Ok(())
//! }
//! ```

// Core modules
pub mod ffi;
pub mod filter;
pub mod transport;
pub mod types;

// Test modules (only included in test configuration)
#[cfg(test)]
pub mod __tests__;

// Re-export main types for convenience
pub use filter::{
    advanced_chain::{AdvancedChainManager, AdvancedChainBuilder, ConditionOperator},
    api::{FilterManager, FilterManagerConfig},
    capifilter::{CApiFilter, create_custom_filter, create_builtin_filter_with_callbacks},
    manager::FilterManager as HighLevelFilterManager,
};

pub use ffi::enhanced_loader::EnhancedLibraryLoader;

pub use transport::gopher::{GopherTransport, GopherTransportConfig, ProtocolType};

pub use types::{
    filters::{FilterType, FilterConfig, FilterCallbacks, FilterStatus, BuiltinFilterType},
    chains::{ChainConfig, FilterChain, ChainBuilder, ChainExecutionMode, RoutingStrategy, FilterNode},
    buffers::{BufferManager, Buffer, BufferHandle},
};

// Re-export error types
pub use crate::ffi::error::{FilterError, BufferError, TransportError};

// Re-export FFI types for advanced usage
pub use ffi::{
    library_loader::LibraryLoader,
    c_structs::{McpFilterCallbacks, McpFilterConfig, McpChainConfig, McpFilterNode, McpProtocolMetadata, create_filter_config_struct, create_chain_config_struct, create_filter_callbacks_struct, create_protocol_metadata_struct, create_filter_node_struct, free_struct},
};

// Version information
pub const VERSION: &str = env!("CARGO_PKG_VERSION");
pub const NAME: &str = env!("CARGO_PKG_NAME");

/// Initialize the SDK with default configuration
///
/// This function should be called once at the start of your application
/// to initialize logging and other global state.
pub fn init() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing subscriber
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    tracing::info!("MCP Filter SDK v{} initialized", VERSION);
    Ok(())
}

/// Initialize the SDK with custom configuration
pub fn init_with_config(config: InitConfig) -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing subscriber with custom config
    tracing_subscriber::fmt()
        .with_env_filter(config.log_filter)
        .init();

    tracing::info!("MCP Filter SDK v{} initialized with custom config", VERSION);
    Ok(())
}

/// SDK initialization configuration
#[derive(Debug, Clone)]
pub struct InitConfig {
    /// Log filter configuration
    pub log_filter: tracing_subscriber::EnvFilter,
}

impl Default for InitConfig {
    fn default() -> Self {
        Self {
            log_filter: tracing_subscriber::EnvFilter::from_default_env(),
        }
    }
}