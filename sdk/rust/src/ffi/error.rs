//! # Error Types for FFI Operations
//!
//! This module defines comprehensive error types for all FFI operations,
//! providing clear error context and chaining.

use thiserror::Error;

/// Main error type for FFI operations
#[derive(Error, Debug)]
pub enum FilterError {
    #[error("Library loading failed: {0}")]
    LibraryLoad(#[from] libloading::Error),

    #[error("Function not found: {function_name}")]
    FunctionNotFound { function_name: String },

    #[error("Invalid function signature: {function_name}")]
    InvalidSignature { function_name: String },

    #[error("C API error: {code} - {message}")]
    CApiError { code: i32, message: String },

    #[error("Buffer operation failed: {0}")]
    Buffer(#[from] BufferError),

    #[error("Transport error: {0}")]
    Transport(#[from] TransportError),

    #[error("Serialization error: {0}")]
    Serialization(#[from] serde_json::Error),

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("UTF-8 conversion error: {0}")]
    Utf8(#[from] std::str::Utf8Error),

    #[error("Invalid parameter: {parameter} - {reason}")]
    InvalidParameter { parameter: String, reason: String },

    #[error("Operation not supported: {operation}")]
    NotSupported { operation: String },

    #[error("Resource not found: {resource}")]
    NotFound { resource: String },

    #[error("Permission denied: {resource}")]
    PermissionDenied { resource: String },

    #[error("Timeout: {operation} took too long")]
    Timeout { operation: String },

    #[error("Out of memory")]
    OutOfMemory,

    #[error("Internal error: {0}")]
    Internal(String),
}

/// Buffer-specific errors
#[derive(Error, Debug)]
pub enum BufferError {
    #[error("Invalid buffer handle: {handle}")]
    InvalidHandle { handle: usize },

    #[error("Buffer too small: required {required}, available {available}")]
    TooSmall { required: usize, available: usize },

    #[error("Buffer operation failed: {operation}")]
    OperationFailed { operation: String },

    #[error("Buffer not found: {handle}")]
    NotFound { handle: usize },

    #[error("Buffer already exists: {handle}")]
    AlreadyExists { handle: usize },

    #[error("Buffer locked: {handle}")]
    Locked { handle: usize },
}

/// Transport-specific errors
#[derive(Error, Debug)]
pub enum TransportError {
    #[error("Connection failed: {reason}")]
    ConnectionFailed { reason: String },

    #[error("Connection lost: {reason}")]
    ConnectionLost { reason: String },

    #[error("Protocol error: {message}")]
    ProtocolError { message: String },

    #[error("Message too large: {size} bytes")]
    MessageTooLarge { size: usize },

    #[error("Invalid message format: {reason}")]
    InvalidMessage { reason: String },

    #[error("Send failed: {reason}")]
    SendFailed { reason: String },

    #[error("Receive failed: {reason}")]
    ReceiveFailed { reason: String },
}

/// Result type for FFI operations
pub type FilterResult<T> = Result<T, FilterError>;

/// Result type for buffer operations
pub type BufferResult<T> = Result<T, BufferError>;

/// Result type for transport operations
pub type TransportResult<T> = Result<T, TransportError>;

impl From<FilterError> for std::io::Error {
    fn from(err: FilterError) -> Self {
        match err {
            FilterError::Io(io_err) => io_err,
            _ => std::io::Error::new(std::io::ErrorKind::Other, err),
        }
    }
}
