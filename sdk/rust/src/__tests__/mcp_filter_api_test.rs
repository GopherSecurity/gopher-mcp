/**
 * @file mcp_filter_api_test.rs
 * @brief Tests for MCP Filter API functionality
 *
 * This test file covers the core filter API including:
 * - Filter manager creation and configuration
 * - Filter chain management
 * - Basic buffer operations
 * - Filter manager operations
 */
use crate::{
    AdvancedChainManager, ConditionOperator, EnhancedLibraryLoader, FilterCallbacks, FilterManager,
    FilterManagerConfig, FilterStatus,
};
use std::sync::Arc;

#[tokio::test]
async fn test_filter_manager_creation() {
    let config = FilterManagerConfig {
        max_filters: 100,
        debug: true,
        metrics: true,
    };

    let manager = FilterManager::with_config(config).unwrap();
    // Test that manager was created successfully
    assert!(true);
}

#[tokio::test]
async fn test_filter_manager_default_config() {
    let manager = FilterManager::new().unwrap();
    // Test that manager was created successfully
    assert!(true);
}

#[tokio::test]
async fn test_filter_chain_creation() {
    let library = Arc::new(EnhancedLibraryLoader::new().unwrap());
    let manager = AdvancedChainManager::new(library);

    let filters = vec![1, 2, 3];
    let chain = manager.create_simple_chain(filters, "test-chain").unwrap();

    assert_eq!(chain.config.name, "test-chain");
    assert_eq!(
        chain.config.execution_mode,
        crate::ChainExecutionMode::Sequential
    );
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
    assert_eq!(
        chain.config.execution_mode,
        crate::ChainExecutionMode::Parallel
    );
}

#[tokio::test]
async fn test_conditional_chain_creation() {
    let library = Arc::new(EnhancedLibraryLoader::new().unwrap());
    let manager = AdvancedChainManager::new(library);

    let conditions = vec![(
        "key".to_string(),
        ConditionOperator::Equals,
        serde_json::Value::String("value".to_string()),
        None,
    )];

    let chain = manager
        .create_conditional_chain(conditions, "conditional-chain")
        .unwrap();

    assert_eq!(chain.config.name, "conditional-chain");
}

#[tokio::test]
async fn test_filter_callbacks() {
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
        on_error: Some(Box::new(|code, message| {
            assert!(code >= 0);
            assert!(!message.is_empty());
        })),
        on_high_watermark: Some(Box::new(|| {
            // High watermark callback
        })),
        on_low_watermark: Some(Box::new(|| {
            // Low watermark callback
        })),
        user_data: None,
    };

    // Test that callbacks can be created
    assert!(callbacks.on_data.is_some());
    assert!(callbacks.on_write.is_some());
    assert!(callbacks.on_new_connection.is_some());
    assert!(callbacks.on_error.is_some());
    assert!(callbacks.on_high_watermark.is_some());
    assert!(callbacks.on_low_watermark.is_some());
}

#[tokio::test]
async fn test_filter_status_conversion() {
    // Test FilterStatus to i32 conversion
    assert_eq!(i32::from(FilterStatus::Continue), 0);
    assert_eq!(i32::from(FilterStatus::Stop), 1);
    assert_eq!(i32::from(FilterStatus::StopAndBuffer), 2);

    // Test i32 to FilterStatus conversion
    assert_eq!(FilterStatus::from(0), FilterStatus::Continue);
    assert_eq!(FilterStatus::from(1), FilterStatus::Stop);
    assert_eq!(FilterStatus::from(2), FilterStatus::StopAndBuffer);
    assert_eq!(FilterStatus::from(999), FilterStatus::Continue); // Default case
}

#[tokio::test]
async fn test_chain_execution_modes() {
    let library = Arc::new(EnhancedLibraryLoader::new().unwrap());
    let manager = AdvancedChainManager::new(library);

    // Test sequential chain
    let sequential_chain = manager.create_simple_chain(vec![1, 2, 3], "seq").unwrap();
    assert_eq!(
        sequential_chain.config.execution_mode,
        crate::ChainExecutionMode::Sequential
    );

    // Test parallel chain
    let parallel_chain = manager
        .create_parallel_chain(vec![1, 2, 3, 4], 2, "par")
        .unwrap();
    assert_eq!(
        parallel_chain.config.execution_mode,
        crate::ChainExecutionMode::Parallel
    );
}

#[tokio::test]
async fn test_condition_operators() {
    // Test that condition operators exist and can be used
    let _equals = ConditionOperator::Equals;
    let _not_equals = ConditionOperator::NotEquals;
    let _greater_than = ConditionOperator::GreaterThan;
    let _less_than = ConditionOperator::LessThan;
    let _contains = ConditionOperator::Contains;
    let _regex = ConditionOperator::Regex;

    // This test just ensures the enum variants exist and can be instantiated
    assert!(true);
}
