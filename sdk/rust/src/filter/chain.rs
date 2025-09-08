//! # Filter Chain Management
//!
//! This module provides advanced functionality for managing filter chains including
//! sequential, parallel, conditional, and pipeline execution modes.

use crate::types::chains::{ChainResult, FilterChain, ChainExecutionMode, RoutingStrategy};
use crate::ffi::error::{FilterError, FilterResult};
use crate::ffi::library_loader::LibraryLoader;
use std::sync::Arc;

/// Filter chain manager
#[derive(Debug)]
pub struct FilterChainManager {
    chains: Vec<FilterChain>,
}

impl FilterChainManager {
    /// Create a new filter chain manager
    pub fn new() -> Self {
        Self { chains: Vec::new() }
    }

    /// Add a filter chain
    pub fn add_chain(&mut self, chain: FilterChain) -> ChainResult<()> {
        self.chains.push(chain);
        Ok(())
    }

    /// Get a filter chain by name
    pub fn get_chain(&self, name: &str) -> Option<&FilterChain> {
        self.chains.iter().find(|chain| chain.config.name == name)
    }

    /// Get a mutable reference to a filter chain
    pub fn get_chain_mut(&mut self, name: &str) -> Option<&mut FilterChain> {
        self.chains
            .iter_mut()
            .find(|chain| chain.config.name == name)
    }

    /// Remove a filter chain
    pub fn remove_chain(&mut self, name: &str) -> bool {
        if let Some(pos) = self
            .chains
            .iter()
            .position(|chain| chain.config.name == name)
        {
            self.chains.remove(pos);
            true
        } else {
            false
        }
    }

    /// Get the number of chains
    pub fn chain_count(&self) -> usize {
        self.chains.len()
    }
}

impl Default for FilterChainManager {
    fn default() -> Self {
        Self::new()
    }
}
