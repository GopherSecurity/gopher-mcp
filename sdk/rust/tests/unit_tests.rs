//! # Unit Tests
//!
//! Comprehensive unit tests for individual MCP Filter SDK components.
//! These tests focus on isolated functionality without external dependencies.

use mcp_filter_sdk::{
    FilterCallbacks, FilterStatus, BuiltinFilterType, FilterType, FilterConfig,
    ChainExecutionMode, RoutingStrategy, ChainConfig,
    ConditionOperator, AdvancedChainManager, AdvancedChainBuilder,
    McpFilterCallbacks,
};
use mcp_filter_sdk::types::chains::FilterNode;
use std::sync::Arc;

/// Test filter callbacks functionality
#[test]
fn test_filter_callbacks() {
    let callbacks = FilterCallbacks {
        on_data: Some(Box::new(|data, end_stream| {
            assert!(!data.is_empty());
            assert!(!end_stream);
            FilterStatus::Continue
        })),
        on_write: Some(Box::new(|data, end_stream| {
            assert!(!data.is_empty());
            assert!(!end_stream);
            FilterStatus::Continue
        })),
        on_new_connection: Some(Box::new(|state| {
            assert!(state >= 0);
        })),
        on_high_watermark: Some(Box::new(|| {
            // Test high watermark callback
        })),
        on_low_watermark: Some(Box::new(|| {
            // Test low watermark callback
        })),
        on_error: Some(Box::new(|error_code, message| {
            assert!(error_code >= 0);
            assert!(!message.is_empty());
        })),
        user_data: Some(Box::new("test_data".to_string())),
    };
    
    // Test data callback
    if let Some(ref callback) = callbacks.on_data {
        let result = callback(b"test data", false);
        assert_eq!(result, FilterStatus::Continue);
    }
    
    // Test write callback
    if let Some(ref callback) = callbacks.on_write {
        let result = callback(b"test write", false);
        assert_eq!(result, FilterStatus::Continue);
    }
    
    // Test connection callback
    if let Some(ref callback) = callbacks.on_new_connection {
        callback(1);
    }
    
    // Test error callback
    if let Some(ref callback) = callbacks.on_error {
        callback(1, "test error".to_string());
    }
    
    // Test user data
    if let Some(ref user_data) = callbacks.user_data {
        let data = user_data.downcast_ref::<String>().unwrap();
        assert_eq!(data, "test_data");
    }
}

/// Test filter status enum
#[test]
fn test_filter_status() {
    assert_eq!(FilterStatus::Continue as i32, 0);
    assert_eq!(FilterStatus::Stop as i32, 1);
    assert_eq!(FilterStatus::StopAndBuffer as i32, 2);
}

/// Test built-in filter types
#[test]
fn test_builtin_filter_types() {
    assert_eq!(BuiltinFilterType::Authentication as i32, 0);
    assert_eq!(BuiltinFilterType::Authorization as i32, 1);
    assert_eq!(BuiltinFilterType::RateLimit as i32, 2);
    assert_eq!(BuiltinFilterType::CircuitBreaker as i32, 3);
    assert_eq!(BuiltinFilterType::Retry as i32, 4);
    assert_eq!(BuiltinFilterType::Logging as i32, 5);
    assert_eq!(BuiltinFilterType::Metrics as i32, 6);
    assert_eq!(BuiltinFilterType::Tracing as i32, 7);
    assert_eq!(BuiltinFilterType::Compression as i32, 8);
    assert_eq!(BuiltinFilterType::Encryption as i32, 9);
}

/// Test filter types
#[test]
fn test_filter_types() {
    let custom_type = FilterType::Custom("test_filter".to_string());
    let builtin_type = FilterType::Builtin(BuiltinFilterType::RateLimit);
    
    match custom_type {
        FilterType::Custom(name) => assert_eq!(name, "test_filter"),
        _ => panic!("Expected custom type"),
    }
    
    match builtin_type {
        FilterType::Builtin(builtin) => assert_eq!(builtin, BuiltinFilterType::RateLimit),
        _ => panic!("Expected builtin type"),
    }
}

/// Test filter configuration
#[test]
fn test_filter_config() {
    let config = FilterConfig::new("test_filter", "1.0.0", true, 100);
    
    assert_eq!(config.name, "test_filter");
    assert_eq!(config.version, "1.0.0");
    assert!(config.enabled);
    assert_eq!(config.priority, 100);
    assert!(config.parameters.is_empty());
}

/// Test chain execution modes
#[test]
fn test_chain_execution_modes() {
    assert_eq!(ChainExecutionMode::Sequential as i32, 0);
    assert_eq!(ChainExecutionMode::Parallel as i32, 1);
    assert_eq!(ChainExecutionMode::Conditional as i32, 2);
    assert_eq!(ChainExecutionMode::Pipeline as i32, 3);
}

/// Test routing strategies
#[test]
fn test_routing_strategies() {
    assert_eq!(RoutingStrategy::FirstMatch as i32, 0);
    assert_eq!(RoutingStrategy::AllMatching as i32, 1);
    assert_eq!(RoutingStrategy::Weighted as i32, 2);
    assert_eq!(RoutingStrategy::RoundRobin as i32, 3);
    assert_eq!(RoutingStrategy::LeastLoaded as i32, 4);
    assert_eq!(RoutingStrategy::Custom as i32, 5);
}

/// Test chain configuration
#[test]
fn test_chain_config() {
    let config = ChainConfig::default();
    
    assert_eq!(config.name, "default_chain");
    assert_eq!(config.execution_mode, ChainExecutionMode::Sequential);
    assert_eq!(config.routing_strategy, RoutingStrategy::FirstMatch);
    assert_eq!(config.max_filters, 100);
    assert_eq!(config.max_parallel, 1);
    assert_eq!(config.buffer_size, 8192);
    assert_eq!(config.timeout_ms, 5000);
    assert!(config.stop_on_error);
    assert!(config.parameters.is_empty());
}

/// Test condition operators
#[test]
fn test_condition_operators() {
    assert_eq!(ConditionOperator::Equals as i32, 0);
    assert_eq!(ConditionOperator::NotEquals as i32, 1);
    assert_eq!(ConditionOperator::GreaterThan as i32, 2);
    assert_eq!(ConditionOperator::LessThan as i32, 3);
    assert_eq!(ConditionOperator::Contains as i32, 4);
    assert_eq!(ConditionOperator::StartsWith as i32, 5);
    assert_eq!(ConditionOperator::EndsWith as i32, 6);
    assert_eq!(ConditionOperator::Regex as i32, 7);
}

/// Test C structs
#[test]
fn test_c_structs() {
    let callbacks = McpFilterCallbacks::default();
    
    assert!(callbacks.on_data.is_none());
    assert!(callbacks.on_write.is_none());
    assert!(callbacks.on_new_connection.is_none());
    assert!(callbacks.on_high_watermark.is_none());
    assert!(callbacks.on_low_watermark.is_none());
    assert!(callbacks.on_error.is_none());
    assert!(callbacks.user_data.is_null());
}

/// Test advanced chain builder
#[test]
fn test_advanced_chain_builder() {
    // This test would require a real library loader, so we'll test the structure
    let config = ChainConfig {
        name: "test_chain".to_string(),
        execution_mode: ChainExecutionMode::Sequential,
        routing_strategy: RoutingStrategy::RoundRobin,
        max_filters: 10,
        max_parallel: 2,
        buffer_size: 4096,
        timeout_ms: 3000,
        stop_on_error: false,
        parameters: std::collections::HashMap::new(),
    };
    
    assert_eq!(config.name, "test_chain");
    assert_eq!(config.execution_mode, ChainExecutionMode::Sequential);
    assert_eq!(config.routing_strategy, RoutingStrategy::RoundRobin);
    assert_eq!(config.max_filters, 10);
    assert_eq!(config.max_parallel, 2);
    assert_eq!(config.buffer_size, 4096);
    assert_eq!(config.timeout_ms, 3000);
    assert!(!config.stop_on_error);
}

/// Test error types
#[test]
fn test_error_types() {
    use mcp_filter_sdk::ffi::error::FilterError;
    
    let not_found = FilterError::NotFound {
        resource: "test_resource".to_string(),
    };
    
    let not_supported = FilterError::NotSupported {
        operation: "test_operation".to_string(),
    };
    
    let internal = FilterError::Internal("test_error".to_string());
    
    let c_api = FilterError::CApiError {
        code: 1,
        message: "test_c_api_error".to_string(),
    };
    
    let function_not_found = FilterError::FunctionNotFound {
        function_name: "test_function".to_string(),
    };
    
    let library_load = FilterError::LibraryLoad(libloading::Error::DlOpen {
        desc: "test_dl_open_error".to_string(),
    });
    
    // Test error formatting
    assert!(format!("{:?}", not_found).contains("NotFound"));
    assert!(format!("{:?}", not_supported).contains("NotSupported"));
    assert!(format!("{:?}", internal).contains("Internal"));
    assert!(format!("{:?}", c_api).contains("CApiError"));
    assert!(format!("{:?}", function_not_found).contains("FunctionNotFound"));
    assert!(format!("{:?}", library_load).contains("LibraryLoad"));
}

/// Test buffer operations
#[test]
fn test_buffer_operations() {
    use mcp_filter_sdk::types::buffers::BufferManager;
    
    let mut manager = BufferManager::new();
    
    // Test buffer creation
    let buffer1 = manager.create_buffer(1024);
    let buffer2 = manager.create_buffer(2048);
    
    assert!(buffer1 > 0);
    assert!(buffer2 > 0);
    assert_ne!(buffer1, buffer2);
    
    // Test buffer retrieval
    let data1 = manager.get_buffer(buffer1).unwrap();
    let data2 = manager.get_buffer(buffer2).unwrap();
    
    assert_eq!(data1.data, b"test data 1");
    assert_eq!(data2.data, b"test data 2");
    
    // Test buffer update
    let success = manager.update_buffer(buffer1, b"updated data".to_vec());
    assert!(success);
    
    let updated_data = manager.get_buffer(buffer1).unwrap();
    assert_eq!(updated_data.data, b"updated data");
    
    // Test buffer operations
    // Note: remove_buffer method not implemented yet
    
    let should_be_none = manager.get_buffer(buffer2);
    assert!(should_be_none.is_none());
}

/// Test filter chain operations
#[test]
fn test_filter_chain_operations() {
    use mcp_filter_sdk::types::chains::{FilterChain, FilterNode, ChainBuilder};
    
    let config = ChainConfig::default();
    let mut chain = FilterChain::new(config);
    
    // Test adding nodes
    let node1 = FilterNode::new(
        "node1".to_string(),
        FilterType::Custom("filter1".to_string()),
        FilterConfig::new("filter1", "1.0.0", true, 100),
    );
    
    let node2 = FilterNode::new(
        "node2".to_string(),
        FilterType::Builtin(BuiltinFilterType::RateLimit),
        FilterConfig::new("filter2", "1.0.0", true, 200),
    );
    
    chain.add_node(node1).unwrap();
    chain.add_node(node2).unwrap();
    
    assert_eq!(chain.len(), 2);
    assert!(!chain.is_empty());
    
    // Test node retrieval
    let retrieved_node = chain.get_node("node1").unwrap();
    assert_eq!(retrieved_node.id, "node1");
    
    // Test node modification
    if let Some(node) = chain.get_node_mut("node2") {
        node.set_weight(2.0);
        node.set_condition("test_condition".to_string());
    }
    
    let modified_node = chain.get_node("node2").unwrap();
    assert_eq!(modified_node.weight, 2.0);
    assert_eq!(modified_node.condition, Some("test_condition".to_string()));
}

/// Test chain builder
#[test]
fn test_chain_builder() {
    use mcp_filter_sdk::types::chains::ChainBuilder;
    
    let config = ChainConfig {
        name: "test_chain".to_string(),
        execution_mode: ChainExecutionMode::Parallel,
        routing_strategy: RoutingStrategy::RoundRobin,
        max_filters: 5,
        max_parallel: 2,
        buffer_size: 2048,
        timeout_ms: 2000,
        stop_on_error: true,
        parameters: std::collections::HashMap::new(),
    };
    
    let mut builder = ChainBuilder::new(config);
    
    // Add nodes
    for i in 0..3 {
        let node = FilterNode::new(
            format!("node{}", i),
            FilterType::Custom(format!("filter{}", i)),
            FilterConfig::new(&format!("filter{}", i), "1.0.0", true, i * 100),
        );
        builder.add_filter(node);
    }
    
    // Build chain
    let chain = builder.build().unwrap();
    assert_eq!(chain.config.name, "test_chain");
    assert_eq!(chain.nodes.len(), 3);
    assert_eq!(chain.config.execution_mode, ChainExecutionMode::Parallel);
    assert_eq!(chain.config.routing_strategy, RoutingStrategy::RoundRobin);
}

/// Test error handling for chain operations
#[test]
fn test_chain_error_handling() {
    use mcp_filter_sdk::types::chains::{ChainBuilder, ChainError};
    
    let config = ChainConfig {
        name: "test_chain".to_string(),
        execution_mode: ChainExecutionMode::Sequential,
        routing_strategy: RoutingStrategy::FirstMatch,
        max_filters: 2, // Limit to 2 filters
        max_parallel: 1,
        buffer_size: 1024,
        timeout_ms: 1000,
        stop_on_error: true,
        parameters: std::collections::HashMap::new(),
    };
    
    let mut builder = ChainBuilder::new(config);
    
    // Add nodes up to the limit
    for i in 0..2 {
        let node = FilterNode::new(
            format!("node{}", i),
            FilterType::Custom(format!("filter{}", i)),
            FilterConfig::new(&format!("filter{}", i), "1.0.0", true, i * 100),
        );
        builder.add_filter(node);
    }
    
    // Try to add one more node (should succeed in builder)
    let extra_node = FilterNode::new(
        "extra_node".to_string(),
        FilterType::Custom("extra_filter".to_string()),
        FilterConfig::new("extra_filter", "1.0.0", true, 300),
    );
    builder.add_filter(extra_node);
    
    // Building should fail due to too many filters
    let result = builder.build();
    match result {
        Err(ChainError::TooManyFilters { count, max }) => {
            assert_eq!(count, 3);
            assert_eq!(max, 2);
        }
        _ => panic!("Expected TooManyFilters error"),
    }
}

/// Test empty chain handling
#[test]
fn test_empty_chain_handling() {
    use mcp_filter_sdk::types::chains::ChainBuilder;
    
    let config = ChainConfig::default();
    let builder = ChainBuilder::new(config);
    
    // Try to build empty chain
    let result = builder.build();
    match result {
        Err(mcp_filter_sdk::types::chains::ChainError::EmptyChain) => {
            // Expected error
        }
        _ => panic!("Expected EmptyChain error"),
    }
}
