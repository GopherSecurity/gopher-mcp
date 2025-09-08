//! # Buffer Operations
//!
//! This module provides buffer management and operations.

use crate::types::buffers::{BufferHandle, BufferManager, BufferResult};

/// Buffer operations manager
#[derive(Debug)]
pub struct BufferOperations {
    manager: BufferManager,
}

impl BufferOperations {
    /// Create a new buffer operations manager
    pub fn new() -> Self {
        Self {
            manager: BufferManager::new(),
        }
    }

    /// Create a new buffer
    pub fn create_buffer(&mut self, size: usize) -> BufferHandle {
        self.manager.create_buffer(size)
    }

    /// Get buffer content
    pub fn get_content(&self, handle: BufferHandle) -> BufferResult<Vec<u8>> {
        self.manager
            .get_content(handle)
            .ok_or_else(|| crate::types::buffers::BufferError::NotFound { handle })
    }

    /// Set buffer content
    pub fn set_content(&mut self, handle: BufferHandle, data: Vec<u8>) -> BufferResult<()> {
        if self.manager.set_content(handle, data) {
            Ok(())
        } else {
            Err(crate::types::buffers::BufferError::NotFound { handle })
        }
    }

    /// Destroy a buffer
    pub fn destroy_buffer(&mut self, handle: BufferHandle) -> bool {
        self.manager.destroy_buffer(handle)
    }
}

impl Default for BufferOperations {
    fn default() -> Self {
        Self::new()
    }
}
