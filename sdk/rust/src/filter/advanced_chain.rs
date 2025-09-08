//! # Advanced Filter Chain Management
//!
//! This module provides advanced filter chain management including
//! sequential, parallel, conditional, and pipeline execution modes.

use crate::types::chains::{
    ChainConfig, ChainExecutionMode, ChainResult, FilterChain, FilterCondition, FilterNode,
    RoutingStrategy,
};
use crate::ffi::error::{FilterError, FilterResult};
use crate::ffi::enhanced_loader::EnhancedLibraryLoader;
use std::sync::Arc;

/// Advanced filter chain builder
#[derive(Debug)]
pub struct AdvancedChainBuilder {
    library: Arc<EnhancedLibraryLoader>,
    config: ChainConfig,
    filters: Vec<FilterNode>,
    conditions: Vec<FilterCondition>,
}

/// Filter node for advanced chains
#[derive(Debug, Clone)]
pub struct AdvancedFilterNode {
    pub filter_id: usize,
    pub name: String,
    pub priority: i32,
    pub enabled: bool,
    pub bypass_on_error: bool,
    pub config: Option<serde_json::Value>,
}

/// Condition operators for conditional execution
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ConditionOperator {
    Equals,
    NotEquals,
    GreaterThan,
    LessThan,
    Contains,
    StartsWith,
    EndsWith,
    Regex,
}

impl From<ConditionOperator> for i32 {
    fn from(op: ConditionOperator) -> Self {
        match op {
            ConditionOperator::Equals => 0,
            ConditionOperator::NotEquals => 1,
            ConditionOperator::GreaterThan => 2,
            ConditionOperator::LessThan => 3,
            ConditionOperator::Contains => 4,
            ConditionOperator::StartsWith => 5,
            ConditionOperator::EndsWith => 6,
            ConditionOperator::Regex => 7,
        }
    }
}

impl AdvancedChainBuilder {
    /// Create a new advanced chain builder
    pub fn new(library: Arc<EnhancedLibraryLoader>, config: ChainConfig) -> Self {
        Self {
            library,
            config,
            filters: Vec::new(),
            conditions: Vec::new(),
        }
    }

    /// Add a filter to the chain
    pub fn add_filter(&mut self, filter: AdvancedFilterNode) -> FilterResult<()> {
        if self.filters.len() >= self.config.max_filters {
            return Err(FilterError::Internal(format!(
                "Too many filters: {} >= {}",
                self.filters.len(), self.config.max_filters
            )));
        }

        self.filters.push(FilterNode {
            id: filter.name.clone(),
            filter_type: crate::types::filters::FilterType::Custom(filter.name.clone()),
            config: crate::types::filters::FilterConfig::new(
                &filter.name,
                "1.0.0",
                filter.enabled,
                filter.priority,
            ),
            next: None,
            weight: 1.0,
            condition: None,
        });

        Ok(())
    }

    /// Add a condition to the chain
    pub fn add_condition(
        &mut self,
        field: &str,
        operator: ConditionOperator,
        value: serde_json::Value,
        chain_id: Option<usize>,
    ) -> FilterResult<()> {
        self.conditions.push(FilterCondition {
            field: field.to_string(),
            operator: operator.into(),
            value,
            chain_id: chain_id.map(|id| id as i32),
        });

        Ok(())
    }

    /// Build the filter chain
    pub fn build(self) -> FilterResult<FilterChain> {
        // In a real implementation, this would call the C++ library
        // For now, create a chain with the configured settings
        let chain = FilterChain {
            config: self.config,
            nodes: self.filters,
            conditions: self.conditions,
        };

        Ok(chain)
    }
}

/// Advanced filter chain manager
#[derive(Debug)]
pub struct AdvancedChainManager {
    chains: Vec<FilterChain>,
    library: Arc<EnhancedLibraryLoader>,
}

impl AdvancedChainManager {
    /// Create a new advanced chain manager
    pub fn new(library: Arc<EnhancedLibraryLoader>) -> Self {
        Self {
            chains: Vec::new(),
            library,
        }
    }

    /// Create a simple sequential chain
    pub fn create_simple_chain(
        &self,
        filters: Vec<usize>,
        name: &str,
    ) -> FilterResult<FilterChain> {
        let config = ChainConfig {
            name: name.to_string(),
            execution_mode: ChainExecutionMode::Sequential,
            routing_strategy: RoutingStrategy::RoundRobin,
            max_filters: 100,
            max_parallel: 1,
            buffer_size: 8192,
            timeout_ms: 5000,
            stop_on_error: false,
            parameters: std::collections::HashMap::new(),
        };

        let mut builder = AdvancedChainBuilder::new(self.library.clone(), config);

        // Add all filters in sequence
        for (i, filter_id) in filters.iter().enumerate() {
            builder.add_filter(AdvancedFilterNode {
                filter_id: *filter_id,
                name: format!("filter-{}", i),
                priority: i as i32,
                enabled: true,
                bypass_on_error: false,
                config: None,
            })?;
        }

        builder.build()
    }

    /// Create a parallel processing chain
    pub fn create_parallel_chain(
        &self,
        filters: Vec<usize>,
        max_parallel: usize,
        name: &str,
    ) -> FilterResult<FilterChain> {
        let config = ChainConfig {
            name: name.to_string(),
            execution_mode: ChainExecutionMode::Parallel,
            routing_strategy: RoutingStrategy::RoundRobin,
            max_filters: 100,
            max_parallel: max_parallel as i32,
            buffer_size: 8192,
            timeout_ms: 5000,
            stop_on_error: false,
            parameters: std::collections::HashMap::new(),
        };

        let mut builder = AdvancedChainBuilder::new(self.library.clone(), config);

        // Add parallel filter group
        for (i, filter_id) in filters.iter().enumerate() {
            builder.add_filter(AdvancedFilterNode {
                filter_id: *filter_id,
                name: format!("parallel-filter-{}", i),
                priority: 0, // Same priority for parallel execution
                enabled: true,
                bypass_on_error: false,
                config: None,
            })?;
        }

        builder.build()
    }

    /// Create a conditional chain
    pub fn create_conditional_chain(
        &self,
        conditions: Vec<(String, ConditionOperator, serde_json::Value, Option<usize>)>,
        name: &str,
    ) -> FilterResult<FilterChain> {
        let config = ChainConfig {
            name: name.to_string(),
            execution_mode: ChainExecutionMode::Conditional,
            routing_strategy: RoutingStrategy::Custom,
            max_filters: 100,
            max_parallel: 1,
            buffer_size: 8192,
            timeout_ms: 5000,
            stop_on_error: false,
            parameters: std::collections::HashMap::new(),
        };

        let mut builder = AdvancedChainBuilder::new(self.library.clone(), config);

        // Add conditional filters
        for (field, operator, value, chain_id) in conditions {
            builder.add_condition(&field, operator, value, chain_id)?;
        }

        builder.build()
    }

    /// Create a pipeline chain
    pub fn create_pipeline_chain(
        &self,
        filters: Vec<usize>,
        buffer_size: usize,
        name: &str,
    ) -> FilterResult<FilterChain> {
        let config = ChainConfig {
            name: name.to_string(),
            execution_mode: ChainExecutionMode::Pipeline,
            routing_strategy: RoutingStrategy::FirstMatch,
            max_filters: 100,
            max_parallel: 1,
            buffer_size: buffer_size as i32,
            timeout_ms: 5000,
            stop_on_error: false,
            parameters: std::collections::HashMap::new(),
        };

        let mut builder = AdvancedChainBuilder::new(self.library.clone(), config);

        // Add filters in pipeline order
        for (i, filter_id) in filters.iter().enumerate() {
            builder.add_filter(AdvancedFilterNode {
                filter_id: *filter_id,
                name: format!("pipeline-filter-{}", i),
                priority: i as i32,
                enabled: true,
                bypass_on_error: false,
                config: None,
            })?;
        }

        builder.build()
    }

    /// Add a chain to the manager
    pub fn add_chain(&mut self, chain: FilterChain) {
        self.chains.push(chain);
    }

    /// Get a chain by name
    pub fn get_chain(&self, name: &str) -> Option<&FilterChain> {
        self.chains.iter().find(|chain| chain.config.name == name)
    }

    /// Get a mutable reference to a chain by name
    pub fn get_chain_mut(&mut self, name: &str) -> Option<&mut FilterChain> {
        self.chains.iter_mut().find(|chain| chain.config.name == name)
    }

    /// Remove a chain by name
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

    /// Get all chains
    pub fn get_chains(&self) -> &[FilterChain] {
        &self.chains
    }

    /// Get the number of chains
    pub fn chain_count(&self) -> usize {
        self.chains.len()
    }

    /// Execute a chain by name
    pub async fn execute_chain(
        &self,
        name: &str,
        data: &[u8],
    ) -> FilterResult<Vec<u8>> {
        let chain = self
            .get_chain(name)
            .ok_or_else(|| FilterError::NotFound {
                resource: format!("Chain '{}' not found", name),
            })?;

        // In a real implementation, this would execute the chain through the C++ library
        // For now, simulate execution based on the chain configuration
        match chain.config.execution_mode {
            ChainExecutionMode::Sequential => self.execute_sequential(chain, data).await,
            ChainExecutionMode::Parallel => self.execute_parallel(chain, data).await,
            ChainExecutionMode::Conditional => self.execute_conditional(chain, data).await,
            ChainExecutionMode::Pipeline => self.execute_pipeline(chain, data).await,
        }
    }

    /// Execute a sequential chain
    async fn execute_sequential(
        &self,
        chain: &FilterChain,
        data: &[u8],
    ) -> FilterResult<Vec<u8>> {
        let mut result = data.to_vec();

        for node in &chain.nodes {
            if !node.config.enabled {
                continue;
            }

            // Simulate filter processing
            result = self.process_filter_node(node, &result).await?;
        }

        Ok(result)
    }

    /// Execute a parallel chain
    async fn execute_parallel(
        &self,
        chain: &FilterChain,
        data: &[u8],
    ) -> FilterResult<Vec<u8>> {
        let mut handles = Vec::new();

        for node in &chain.nodes {
            if !node.config.enabled {
                continue;
            }

            let data_clone = data.to_vec();
            let node_clone = node.clone();
            let handle = tokio::spawn(async move {
                Self::process_filter_node_static(&node_clone, &data_clone).await
            });
            handles.push(handle);
        }

        // Wait for all parallel tasks to complete
        let mut results = Vec::new();
        for handle in handles {
            let result = handle.await.map_err(|e| FilterError::Internal(e.to_string()))?;
            results.push(result?);
        }

        // Combine results (in a real implementation, this would be more sophisticated)
        Ok(results.into_iter().flatten().collect())
    }

    /// Execute a conditional chain
    async fn execute_conditional(
        &self,
        chain: &FilterChain,
        data: &[u8],
    ) -> FilterResult<Vec<u8>> {
        // In a real implementation, this would evaluate conditions
        // For now, just execute the first enabled node
        for node in &chain.nodes {
            if node.config.enabled {
                return self.process_filter_node(node, data).await;
            }
        }

        Ok(data.to_vec())
    }

    /// Execute a pipeline chain
    async fn execute_pipeline(
        &self,
        chain: &FilterChain,
        data: &[u8],
    ) -> FilterResult<Vec<u8>> {
        let mut result = data.to_vec();

        for node in &chain.nodes {
            if !node.config.enabled {
                continue;
            }

            // Process with buffering (simplified)
            result = self.process_filter_node(node, &result).await?;
        }

        Ok(result)
    }

    /// Process a single filter node
    async fn process_filter_node(
        &self,
        node: &FilterNode,
        data: &[u8],
    ) -> FilterResult<Vec<u8>> {
        Self::process_filter_node_static(node, data).await
    }

    /// Static method for processing a filter node (used in parallel execution)
    async fn process_filter_node_static(
        node: &FilterNode,
        data: &[u8],
    ) -> FilterResult<Vec<u8>> {
        // In a real implementation, this would call the actual filter
        // For now, simulate processing
        let mut result = data.to_vec();
        
        // Add a simple transformation based on the filter name
        if node.id.contains("uppercase") {
            result = result.iter().map(|&b| b.to_ascii_uppercase()).collect();
        } else if node.id.contains("lowercase") {
            result = result.iter().map(|&b| b.to_ascii_lowercase()).collect();
        }

        Ok(result)
    }
}

impl Default for AdvancedChainManager {
    fn default() -> Self {
        Self {
            chains: Vec::new(),
            library: Arc::new(EnhancedLibraryLoader::new().unwrap()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ffi::enhanced_loader::EnhancedLibraryLoader;

    #[tokio::test]
    async fn test_simple_chain_creation() {
        let library = Arc::new(EnhancedLibraryLoader::new().unwrap());
        let manager = AdvancedChainManager::new(library);

        let filters = vec![1, 2, 3];
        let chain = manager.create_simple_chain(filters, "test-chain").unwrap();

        assert_eq!(chain.config.name, "test-chain");
        assert_eq!(chain.config.execution_mode, ChainExecutionMode::Sequential);
        assert_eq!(chain.nodes.len(), 3);
    }

    #[tokio::test]
    async fn test_parallel_chain_creation() {
        let library = Arc::new(EnhancedLibraryLoader::new().unwrap());
        let manager = AdvancedChainManager::new(library);

        let filters = vec![1, 2, 3, 4];
        let chain = manager
            .create_parallel_chain(filters, 2, "parallel-chain")
            .unwrap();

        assert_eq!(chain.config.name, "parallel-chain");
        assert_eq!(chain.config.execution_mode, ChainExecutionMode::Parallel);
        assert_eq!(chain.config.max_parallel, 2);
        assert_eq!(chain.nodes.len(), 4);
    }

    #[tokio::test]
    async fn test_conditional_chain_creation() {
        let library = Arc::new(EnhancedLibraryLoader::new().unwrap());
        let manager = AdvancedChainManager::new(library);

        let conditions = vec![
            (
                "method".to_string(),
                ConditionOperator::Equals,
                serde_json::json!("GET"),
                Some(1),
            ),
            (
                "path".to_string(),
                ConditionOperator::StartsWith,
                serde_json::json!("/api"),
                Some(2),
            ),
        ];

        let chain = manager
            .create_conditional_chain(conditions, "conditional-chain")
            .unwrap();

        assert_eq!(chain.config.name, "conditional-chain");
        assert_eq!(chain.config.execution_mode, ChainExecutionMode::Conditional);
        assert_eq!(chain.conditions.len(), 2);
    }

    #[tokio::test]
    async fn test_pipeline_chain_creation() {
        let library = Arc::new(EnhancedLibraryLoader::new().unwrap());
        let manager = AdvancedChainManager::new(library);

        let filters = vec![1, 2, 3];
        let chain = manager
            .create_pipeline_chain(filters, 4096, "pipeline-chain")
            .unwrap();

        assert_eq!(chain.config.name, "pipeline-chain");
        assert_eq!(chain.config.execution_mode, ChainExecutionMode::Pipeline);
        assert_eq!(chain.config.buffer_size, 4096);
        assert_eq!(chain.nodes.len(), 3);
    }

    #[tokio::test]
    async fn test_chain_execution() {
        let library = Arc::new(EnhancedLibraryLoader::new().unwrap());
        let mut manager = AdvancedChainManager::new(library);

        let filters = vec![1, 2, 3];
        let chain = manager.create_simple_chain(filters, "test-chain").unwrap();
        manager.add_chain(chain);

        let data = b"hello world";
        let result = manager.execute_chain("test-chain", data).await.unwrap();

        assert_eq!(result, data);
    }
}
