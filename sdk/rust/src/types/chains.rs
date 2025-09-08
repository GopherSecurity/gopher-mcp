//! # Chain Type Definitions
//!
//! This module defines types related to filter chains and their configuration.

use crate::types::filters::{FilterConfig, FilterType};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Filter chain execution modes
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum ChainExecutionMode {
    /// Execute filters sequentially
    Sequential,
    /// Execute filters in parallel
    Parallel,
    /// Execute filters conditionally based on results
    Conditional,
    /// Pipeline mode with buffering
    Pipeline,
}

/// Filter chain routing strategies
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum RoutingStrategy {
    /// Route to first matching filter
    FirstMatch,
    /// Route to all matching filters
    AllMatching,
    /// Route based on weights
    Weighted,
    /// Round-robin distribution
    RoundRobin,
    /// Route to least loaded filter
    LeastLoaded,
    /// Custom routing strategy
    Custom,
}

/// Filter chain configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ChainConfig {
    /// Chain name
    pub name: String,
    /// Execution mode
    pub execution_mode: ChainExecutionMode,
    /// Routing strategy
    pub routing_strategy: RoutingStrategy,
    /// Maximum number of filters in the chain
    pub max_filters: usize,
    /// Maximum parallel execution count
    pub max_parallel: i32,
    /// Buffer size for pipeline mode
    pub buffer_size: i32,
    /// Timeout in milliseconds
    pub timeout_ms: i32,
    /// Stop on error
    pub stop_on_error: bool,
    /// Chain-specific parameters
    pub parameters: HashMap<String, serde_json::Value>,
}

impl Default for ChainConfig {
    fn default() -> Self {
        Self {
            name: "default_chain".to_string(),
            execution_mode: ChainExecutionMode::Sequential,
            routing_strategy: RoutingStrategy::FirstMatch,
            max_filters: 100,
            max_parallel: 1,
            buffer_size: 8192,
            timeout_ms: 5000,
            stop_on_error: true,
            parameters: HashMap::new(),
        }
    }
}

/// Filter node in a chain
#[derive(Debug, Clone)]
pub struct FilterNode {
    /// Unique node ID
    pub id: String,
    /// Filter type
    pub filter_type: FilterType,
    /// Filter configuration
    pub config: FilterConfig,
    /// Next node in the chain (for sequential execution)
    pub next: Option<Box<FilterNode>>,
    /// Weight for weighted routing
    pub weight: f64,
    /// Condition for conditional execution
    pub condition: Option<String>,
}

/// Filter condition for conditional execution
#[derive(Debug, Clone)]
pub struct FilterCondition {
    /// Field to check
    pub field: String,
    /// Operator to use
    pub operator: i32,
    /// Value to compare against
    pub value: serde_json::Value,
    /// Chain ID to route to
    pub chain_id: Option<i32>,
}

impl FilterNode {
    /// Create a new filter node
    pub fn new(id: String, filter_type: FilterType, config: FilterConfig) -> Self {
        Self {
            id,
            filter_type,
            config,
            next: None,
            weight: 1.0,
            condition: None,
        }
    }

    /// Set the next node in the chain
    pub fn set_next(&mut self, next: FilterNode) {
        self.next = Some(Box::new(next));
    }

    /// Set the weight for weighted routing
    pub fn set_weight(&mut self, weight: f64) {
        self.weight = weight;
    }

    /// Set the condition for conditional execution
    pub fn set_condition(&mut self, condition: String) {
        self.condition = Some(condition);
    }
}

/// Filter chain builder
#[derive(Debug, Default)]
pub struct ChainBuilder {
    nodes: Vec<FilterNode>,
    config: ChainConfig,
}

impl ChainBuilder {
    /// Create a new chain builder
    pub fn new(config: ChainConfig) -> Self {
        Self {
            nodes: Vec::new(),
            config,
        }
    }

    /// Add a filter node to the chain
    pub fn add_filter(&mut self, node: FilterNode) -> &mut Self {
        self.nodes.push(node);
        self
    }

    /// Build the filter chain
    pub fn build(self) -> Result<FilterChain, ChainError> {
        if self.nodes.is_empty() {
            return Err(ChainError::EmptyChain);
        }

        if self.nodes.len() > self.config.max_filters {
            return Err(ChainError::TooManyFilters {
                count: self.nodes.len(),
                max: self.config.max_filters,
            });
        }

        Ok(FilterChain {
            config: self.config,
            nodes: self.nodes,
            conditions: Vec::new(),
        })
    }
}

/// A filter chain
#[derive(Debug, Clone)]
pub struct FilterChain {
    /// Chain configuration
    pub config: ChainConfig,
    /// Filter nodes in the chain
    pub nodes: Vec<FilterNode>,
    /// Filter conditions for conditional execution
    pub conditions: Vec<FilterCondition>,
}

impl FilterChain {
    /// Create a new filter chain
    pub fn new(config: ChainConfig) -> Self {
        Self {
            config,
            nodes: Vec::new(),
            conditions: Vec::new(),
        }
    }

    /// Add a filter node to the chain
    pub fn add_node(&mut self, node: FilterNode) -> Result<(), ChainError> {
        if self.nodes.len() >= self.config.max_filters {
            return Err(ChainError::TooManyFilters {
                count: self.nodes.len() + 1,
                max: self.config.max_filters,
            });
        }

        self.nodes.push(node);
        Ok(())
    }

    /// Get the number of nodes in the chain
    pub fn len(&self) -> usize {
        self.nodes.len()
    }

    /// Check if the chain is empty
    pub fn is_empty(&self) -> bool {
        self.nodes.is_empty()
    }

    /// Get a node by ID
    pub fn get_node(&self, id: &str) -> Option<&FilterNode> {
        self.nodes.iter().find(|node| node.id == id)
    }

    /// Get a mutable reference to a node by ID
    pub fn get_node_mut(&mut self, id: &str) -> Option<&mut FilterNode> {
        self.nodes.iter_mut().find(|node| node.id == id)
    }
}

/// Chain error type
#[derive(Debug, thiserror::Error)]
pub enum ChainError {
    #[error("Chain is empty")]
    EmptyChain,

    #[error("Too many filters: {count} > {max}")]
    TooManyFilters { count: usize, max: usize },

    #[error("Node not found: {id}")]
    NodeNotFound { id: String },

    #[error("Invalid chain configuration: {reason}")]
    InvalidConfig { reason: String },

    #[error("Chain execution failed: {reason}")]
    ExecutionFailed { reason: String },
}

/// Chain result type
pub type ChainResult<T> = Result<T, ChainError>;
