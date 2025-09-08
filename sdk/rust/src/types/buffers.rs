//! # Buffer Type Definitions
//!
//! This module defines types related to buffer operations and management.

use serde::{Deserialize, Serialize};
use std::sync::{Arc, Mutex};

/// Buffer handle type
pub type BufferHandle = usize;

/// Buffer manager for handling multiple buffers
#[derive(Debug)]
pub struct BufferManager {
    /// Next available handle
    next_handle: BufferHandle,
    /// Active buffers
    buffers: Arc<Mutex<HashMap<BufferHandle, Buffer>>>,
}

impl BufferManager {
    /// Create a new buffer manager
    pub fn new() -> Self {
        Self {
            next_handle: 1,
            buffers: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// Create a new buffer
    pub fn create_buffer(&mut self, size: usize) -> BufferHandle {
        let handle = self.next_handle;
        self.next_handle += 1;

        let buffer = Buffer::new(size);
        self.buffers.lock().unwrap().insert(handle, buffer);
        handle
    }

    /// Get a buffer by handle
    pub fn get_buffer(&self, handle: BufferHandle) -> Option<Buffer> {
        self.buffers.lock().unwrap().get(&handle).cloned()
    }

    /// Update buffer content (safer than returning mutable reference)
    pub fn update_buffer(&mut self, handle: BufferHandle, data: Vec<u8>) -> bool {
        if let Some(buffer) = self.buffers.lock().unwrap().get_mut(&handle) {
            buffer.data = data;
            true
        } else {
            false
        }
    }

    /// Destroy a buffer
    pub fn destroy_buffer(&mut self, handle: BufferHandle) -> bool {
        self.buffers.lock().unwrap().remove(&handle).is_some()
    }

    /// Get buffer content
    pub fn get_content(&self, handle: BufferHandle) -> Option<Vec<u8>> {
        self.buffers
            .lock()
            .unwrap()
            .get(&handle)
            .map(|b| b.data.clone())
    }

    /// Set buffer content
    pub fn set_content(&mut self, handle: BufferHandle, data: Vec<u8>) -> bool {
        if let Some(buffer) = self.buffers.lock().unwrap().get_mut(&handle) {
            buffer.data = data;
            true
        } else {
            false
        }
    }

    /// Get buffer size
    pub fn get_size(&self, handle: BufferHandle) -> Option<usize> {
        self.buffers
            .lock()
            .unwrap()
            .get(&handle)
            .map(|b| b.data.len())
    }

    /// Check if a buffer exists
    pub fn has_buffer(&self, handle: BufferHandle) -> bool {
        self.buffers.lock().unwrap().contains_key(&handle)
    }
}

impl Default for BufferManager {
    fn default() -> Self {
        Self::new()
    }
}

/// A buffer for storing data
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Buffer {
    /// Buffer data
    pub data: Vec<u8>,
    /// Buffer capacity
    pub capacity: usize,
    /// Buffer position for reading
    pub position: usize,
}

impl Buffer {
    /// Create a new buffer with the specified capacity
    pub fn new(capacity: usize) -> Self {
        Self {
            data: Vec::with_capacity(capacity),
            capacity,
            position: 0,
        }
    }

    /// Create a new buffer with initial data
    pub fn with_data(data: Vec<u8>) -> Self {
        let capacity = data.capacity();
        Self {
            data,
            capacity,
            position: 0,
        }
    }

    /// Get the current data
    pub fn data(&self) -> &[u8] {
        &self.data
    }

    /// Get mutable reference to the data
    pub fn data_mut(&mut self) -> &mut Vec<u8> {
        &mut self.data
    }

    /// Get the buffer size
    pub fn len(&self) -> usize {
        self.data.len()
    }

    /// Check if the buffer is empty
    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }

    /// Get the remaining capacity
    pub fn remaining_capacity(&self) -> usize {
        self.capacity.saturating_sub(self.data.len())
    }

    /// Append data to the buffer
    pub fn append(&mut self, data: &[u8]) -> Result<(), BufferError> {
        if self.data.len() + data.len() > self.capacity {
            return Err(BufferError::InsufficientCapacity {
                required: self.data.len() + data.len(),
                available: self.capacity,
            });
        }

        self.data.extend_from_slice(data);
        Ok(())
    }

    /// Read data from the buffer
    pub fn read(&mut self, size: usize) -> Vec<u8> {
        let end = (self.position + size).min(self.data.len());
        let result = self.data[self.position..end].to_vec();
        self.position = end;
        result
    }

    /// Write data to the buffer
    pub fn write(&mut self, data: &[u8]) -> Result<(), BufferError> {
        if self.data.len() + data.len() > self.capacity {
            return Err(BufferError::InsufficientCapacity {
                required: self.data.len() + data.len(),
                available: self.capacity,
            });
        }

        self.data.extend_from_slice(data);
        Ok(())
    }

    /// Clear the buffer
    pub fn clear(&mut self) {
        self.data.clear();
        self.position = 0;
    }

    /// Reset the position to the beginning
    pub fn reset_position(&mut self) {
        self.position = 0;
    }

    /// Get the current position
    pub fn position(&self) -> usize {
        self.position
    }

    /// Set the position
    pub fn set_position(&mut self, position: usize) -> Result<(), BufferError> {
        if position > self.data.len() {
            return Err(BufferError::InvalidPosition {
                position,
                max: self.data.len(),
            });
        }

        self.position = position;
        Ok(())
    }
}

/// Buffer error type
#[derive(Debug, thiserror::Error)]
pub enum BufferError {
    #[error("Buffer not found: {handle}")]
    NotFound { handle: BufferHandle },

    #[error("Insufficient capacity: required {required}, available {available}")]
    InsufficientCapacity { required: usize, available: usize },

    #[error("Invalid position: {position} > {max}")]
    InvalidPosition { position: usize, max: usize },

    #[error("Buffer operation failed: {operation}")]
    OperationFailed { operation: String },

    #[error("Buffer is locked: {handle}")]
    Locked { handle: BufferHandle },
}

/// Buffer result type
pub type BufferResult<T> = Result<T, BufferError>;

use std::collections::HashMap;
