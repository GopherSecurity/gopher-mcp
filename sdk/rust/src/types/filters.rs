//! # Filter Type Definitions
//!
//! This module defines types related to filters and their configuration.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Filter types supported by the SDK
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum FilterType {
    /// Built-in filter types
    Builtin(BuiltinFilterType),
    /// Custom filter with callbacks
    Custom(String),
}

/// Built-in filter types
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum BuiltinFilterType {
    /// Authentication filter
    Authentication,
    /// Authorization filter
    Authorization,
    /// Rate limiting filter
    RateLimit,
    /// Circuit breaker filter
    CircuitBreaker,
    /// Retry filter
    Retry,
    /// Logging filter
    Logging,
    /// Metrics filter
    Metrics,
    /// Tracing filter
    Tracing,
    /// Compression filter
    Compression,
    /// Encryption filter
    Encryption,
}

/// Filter configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FilterConfig {
    /// Filter name
    pub name: String,
    /// Filter version
    pub version: String,
    /// Whether the filter is enabled
    pub enabled: bool,
    /// Filter priority (higher numbers = higher priority)
    pub priority: i32,
    /// Filter-specific parameters
    pub parameters: HashMap<String, serde_json::Value>,
}

impl FilterConfig {
    /// Create a new filter configuration
    pub fn new(name: &str, version: &str, enabled: bool, priority: i32) -> Self {
        Self {
            name: name.to_string(),
            version: version.to_string(),
            enabled,
            priority,
            parameters: HashMap::new(),
        }
    }
}

impl Default for FilterConfig {
    fn default() -> Self {
        Self {
            name: "default_filter".to_string(),
            version: "1.0.0".to_string(),
            enabled: true,
            priority: 0,
            parameters: HashMap::new(),
        }
    }
}

/// Filter callbacks for custom filters
pub struct FilterCallbacks {
    /// Callback for data processing
    pub on_data: Option<Box<dyn Fn(&[u8], bool) -> FilterStatus + Send + Sync>>,
    /// Callback for data writing
    pub on_write: Option<Box<dyn Fn(&[u8], bool) -> FilterStatus + Send + Sync>>,
    /// Callback for new connections
    pub on_new_connection: Option<Box<dyn Fn(i32) + Send + Sync>>,
    /// Callback for high watermark
    pub on_high_watermark: Option<Box<dyn Fn() + Send + Sync>>,
    /// Callback for low watermark
    pub on_low_watermark: Option<Box<dyn Fn() + Send + Sync>>,
    /// Callback for errors
    pub on_error: Option<Box<dyn Fn(i32, String) + Send + Sync>>,
    /// User data pointer
    pub user_data: Option<Box<dyn std::any::Any + Send + Sync>>,
}

/// Filter status result
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum FilterStatus {
    /// Continue processing
    Continue,
    /// Stop processing
    Stop,
    /// Stop and buffer data
    StopAndBuffer,
}

impl From<FilterStatus> for i32 {
    fn from(status: FilterStatus) -> Self {
        match status {
            FilterStatus::Continue => 0,
            FilterStatus::Stop => 1,
            FilterStatus::StopAndBuffer => 2,
        }
    }
}

impl From<i32> for FilterStatus {
    fn from(value: i32) -> Self {
        match value {
            0 => FilterStatus::Continue,
            1 => FilterStatus::Stop,
            2 => FilterStatus::StopAndBuffer,
            _ => FilterStatus::Continue, // Default to continue
        }
    }
}

/// Filter result type
pub type FilterResult<T> = Result<T, FilterError>;

/// Filter error type
#[derive(Debug, thiserror::Error)]
pub enum FilterError {
    #[error("Filter not found: {name}")]
    NotFound { name: String },

    #[error("Filter already exists: {name}")]
    AlreadyExists { name: String },

    #[error("Invalid filter configuration: {reason}")]
    InvalidConfig { reason: String },

    #[error("Filter execution failed: {reason}")]
    ExecutionFailed { reason: String },

    #[error("Callback error: {reason}")]
    CallbackError { reason: String },
}
