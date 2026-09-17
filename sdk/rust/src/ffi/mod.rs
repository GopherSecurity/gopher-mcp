// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

//! # FFI Module
//!
//! This module provides Foreign Function Interface (FFI) bindings for the MCP Filter C API.
//! It handles dynamic library loading, C struct definitions, and function pointer management.

pub mod bindings;
pub mod c_structs;
pub mod enhanced_loader;
pub mod error;
pub mod library_loader;

// Re-export main types
pub use c_structs::*;
pub use error::*;
pub use library_loader::LibraryLoader;

// Re-export bindings for advanced usage
pub use bindings::*;
