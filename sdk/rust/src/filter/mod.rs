//! # Filter Module
//!
//! This module provides the core filter functionality for the MCP Filter SDK.

pub mod advanced_chain;
pub mod api;
pub mod buffer;
pub mod capifilter;
pub mod chain;
pub mod manager;

// Re-export main types
pub use advanced_chain::{
    AdvancedChainBuilder, AdvancedChainManager, AdvancedFilterNode, ConditionOperator,
};
pub use api::{
    create_filter, create_filter_manager, create_filter_manager_with_config,
    create_filter_with_callbacks, Filter, FilterManager, FilterManagerConfig,
};
pub use buffer::BufferOperations;
pub use capifilter::{
    create_builtin_filter_with_callbacks, create_custom_filter, CApiFilter,
};
pub use chain::FilterChainManager;
