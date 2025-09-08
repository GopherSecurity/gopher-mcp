//! # Integration Tests
//!
//! Comprehensive integration tests for the MCP Filter SDK.
//! These tests verify the complete functionality with real C++ library integration.

use mcp_filter_sdk::{
    EnhancedLibraryLoader, FilterManager, FilterManagerConfig,
    create_custom_filter, FilterCallbacks, FilterStatus, BuiltinFilterType,
    AdvancedChainManager, ConditionOperator, McpFilterCallbacks,
};
use std::sync::Arc;
use tracing::{info, warn, error};

/// Test result structure
#[derive(Debug)]
struct TestResult {
    name: String,
    success: bool,
    duration: std::time::Duration,
    error: Option<String>,
    data: Option<String>,
}

/// Run a test and capture results
fn run_test<F, R>(name: &str, test_fn: F) -> TestResult 
where
    F: FnOnce() -> Result<R, Box<dyn std::error::Error>>,
    R: std::fmt::Debug,
{
    let start = std::time::Instant::now();
    
    match test_fn() {
        Ok(result) => {
            let duration = start.elapsed();
            TestResult {
                name: name.to_string(),
                success: true,
                duration,
                error: None,
                data: Some(format!("{:?}", result)),
            }
        }
        Err(e) => {
            let duration = start.elapsed();
            TestResult {
                name: name.to_string(),
                success: false,
                duration,
                error: Some(e.to_string()),
                data: None,
            }
        }
    }
}

/// Test MCP library initialization
fn test_mcp_initialization() -> Result<String, Box<dyn std::error::Error>> {
    let loader = EnhancedLibraryLoader::new()?;
    
    // Test initialization
    loader.mcp_init(None)?;
    let is_initialized = loader.mcp_is_initialized()?;
    assert!(is_initialized, "Library should be initialized");
    
    // Test version
    let version = loader.mcp_get_version()?;
    assert!(!version.is_empty(), "Version should not be empty");
    
    // Test shutdown
    loader.mcp_shutdown()?;
    
    Ok(format!("Library type: {}, Version: {}", loader.get_library_info(), version))
}

/// Test dispatcher creation and management
fn test_dispatcher_management() -> Result<String, Box<dyn std::error::Error>> {
    let loader = EnhancedLibraryLoader::new()?;
    loader.mcp_init(None)?;
    
    // Create dispatcher
    let dispatcher = loader.mcp_dispatcher_create()?;
    assert!(!dispatcher.is_null(), "Dispatcher should not be null");
    
    // Test dispatcher operations
    let dispatcher_info = format!("Dispatcher created: {:p}", dispatcher);
    
    // Cleanup
    loader.mcp_shutdown()?;
    
    Ok(dispatcher_info)
}

/// Test filter creation and management
fn test_filter_creation() -> Result<String, Box<dyn std::error::Error>> {
    let loader = EnhancedLibraryLoader::new()?;
    loader.mcp_init(None)?;
    let dispatcher = loader.mcp_dispatcher_create()?;
    
    // Test custom filter creation
    let custom_filter = loader.mcp_filter_create(dispatcher, std::ptr::null())?;
    assert!(custom_filter > 0, "Custom filter should have valid handle");
    
    // Test built-in filter creation
    let builtin_filter = loader.mcp_filter_create_builtin(
        dispatcher,
        BuiltinFilterType::RateLimit as i32,
        std::ptr::null()
    )?;
    assert!(builtin_filter > 0, "Built-in filter should have valid handle");
    
    // Test filter callbacks
    let callbacks = McpFilterCallbacks::default();
    loader.mcp_filter_set_callbacks(custom_filter, &callbacks)?;
    
    loader.mcp_shutdown()?;
    
    Ok(format!("Custom filter: {}, Built-in filter: {}", custom_filter, builtin_filter))
}

/// Test buffer operations
fn test_buffer_operations() -> Result<String, Box<dyn std::error::Error>> {
    let loader = EnhancedLibraryLoader::new()?;
    loader.mcp_init(None)?;
    
    // Test buffer creation
    let buffer_size = 1024;
    let buffer = loader.mcp_buffer_create(buffer_size)?;
    assert!(buffer > 0, "Buffer should have valid handle");
    
    // Test buffer data operations
    let test_data = b"Hello, MCP Filter SDK!";
    loader.mcp_buffer_set_data(buffer, test_data)?;
    
    // Test buffer data retrieval
    let (retrieved_data, size) = loader.mcp_buffer_get_data(buffer)?;
    assert_eq!(size, test_data.len(), "Retrieved data size should match");
    
    // Test buffer size
    let buffer_size_actual = loader.mcp_buffer_get_size(buffer)?;
    assert!(buffer_size_actual >= test_data.len(), "Buffer size should be sufficient");
    
    loader.mcp_shutdown()?;
    
    Ok(format!("Buffer: {}, Data size: {}, Retrieved: {} bytes", 
        buffer, size, retrieved_data.len()))
}

/// Test filter chain operations
fn test_filter_chain_operations() -> Result<String, Box<dyn std::error::Error>> {
    let loader = EnhancedLibraryLoader::new()?;
    loader.mcp_init(None)?;
    let dispatcher = loader.mcp_dispatcher_create()?;
    
    // Create chain builder
    let builder = loader.mcp_filter_chain_builder_create(dispatcher)?;
    assert!(!builder.is_null(), "Chain builder should not be null");
    
    // Create filters for chain
    let filter1 = loader.mcp_filter_create(dispatcher, std::ptr::null())?;
    let filter2 = loader.mcp_filter_create_builtin(dispatcher, BuiltinFilterType::Metrics as i32, std::ptr::null())?;
    
    // Add filters to chain
    loader.mcp_filter_chain_add_filter(builder, filter1, 0, 0)?;
    loader.mcp_filter_chain_add_filter(builder, filter2, 1, filter1)?;
    
    // Build chain
    let chain = loader.mcp_filter_chain_build(builder)?;
    assert!(chain > 0, "Chain should have valid handle");
    
    loader.mcp_shutdown()?;
    
    Ok(format!("Chain: {}, Filters: 2", chain))
}

/// Test advanced chain management
fn test_advanced_chain_management() -> Result<String, Box<dyn std::error::Error>> {
    let loader = EnhancedLibraryLoader::new()?;
    let manager = AdvancedChainManager::new(Arc::new(loader));
    
    // Test sequential chain creation
    let filters = vec![1, 2, 3, 4, 5];
    let sequential_chain = manager.create_simple_chain(filters.clone(), "sequential-test")?;
    assert_eq!(sequential_chain.config.name, "sequential-test");
    assert_eq!(sequential_chain.nodes.len(), 5);
    
    // Test parallel chain creation
    let parallel_chain = manager.create_parallel_chain(filters.clone(), 2, "parallel-test")?;
    assert_eq!(parallel_chain.config.name, "parallel-test");
    assert_eq!(parallel_chain.config.max_parallel, 2);
    
    // Test conditional chain creation
    let conditions = vec![
        ("method".to_string(), ConditionOperator::Equals, serde_json::json!("GET"), Some(1)),
        ("path".to_string(), ConditionOperator::StartsWith, serde_json::json!("/api"), Some(2)),
    ];
    let conditional_chain = manager.create_conditional_chain(conditions, "conditional-test")?;
    assert_eq!(conditional_chain.config.name, "conditional-test");
    assert_eq!(conditional_chain.conditions.len(), 2);
    
    Ok(format!("Sequential: {}, Parallel: {}, Conditional: {}", 
        sequential_chain.nodes.len(), 
        parallel_chain.nodes.len(), 
        conditional_chain.conditions.len()))
}

/// Test JSON operations
fn test_json_operations() -> Result<String, Box<dyn std::error::Error>> {
    let loader = EnhancedLibraryLoader::new()?;
    loader.mcp_init(None)?;
    
    // Test JSON string creation
    let json_string = loader.mcp_json_create_string("test_value")?;
    assert!(!json_string.is_null(), "JSON string should not be null");
    
    // Test JSON number creation
    let json_number = loader.mcp_json_create_number(42.0)?;
    assert!(!json_number.is_null(), "JSON number should not be null");
    
    // Test JSON boolean creation
    let json_bool = loader.mcp_json_create_bool(true)?;
    assert!(!json_bool.is_null(), "JSON boolean should not be null");
    
    // Test JSON null creation
    let json_null = loader.mcp_json_create_null()?;
    assert!(!json_null.is_null(), "JSON null should not be null");
    
    // Test JSON stringify
    let stringified = loader.mcp_json_stringify(json_string)?;
    assert!(!stringified.is_empty(), "Stringified JSON should not be empty");
    
    // Cleanup
    loader.mcp_json_free(json_string);
    loader.mcp_json_free(json_number);
    loader.mcp_json_free(json_bool);
    loader.mcp_json_free(json_null);
    
    loader.mcp_shutdown()?;
    
    Ok(format!("JSON operations completed, stringified: {}", stringified))
}

/// Test error handling
fn test_error_handling() -> Result<String, Box<dyn std::error::Error>> {
    let loader = EnhancedLibraryLoader::new()?;
    
    // Test with uninitialized library
    let result = loader.mcp_dispatcher_create();
    // This should fail gracefully
    match result {
        Ok(_) => warn!("Dispatcher creation succeeded unexpectedly"),
        Err(e) => info!("Expected error: {}", e),
    }
    
    // Test with invalid parameters
    loader.mcp_init(None)?;
    let dispatcher = loader.mcp_dispatcher_create()?;
    
    // Test invalid filter creation
    let result = loader.mcp_filter_create(std::ptr::null(), std::ptr::null());
    match result {
        Ok(_) => warn!("Filter creation succeeded unexpectedly"),
        Err(e) => info!("Expected error: {}", e),
    }
    
    loader.mcp_shutdown()?;
    
    Ok("Error handling tests completed".to_string())
}

/// Test performance characteristics
fn test_performance() -> Result<String, Box<dyn std::error::Error>> {
    let loader = EnhancedLibraryLoader::new()?;
    loader.mcp_init(None)?;
    let dispatcher = loader.mcp_dispatcher_create()?;
    
    // Test filter creation performance
    let start = std::time::Instant::now();
    let mut filters = Vec::new();
    
    for i in 0..100 {
        let filter = loader.mcp_filter_create(dispatcher, std::ptr::null())?;
        filters.push(filter);
    }
    
    let filter_creation_time = start.elapsed();
    
    // Test buffer operations performance
    let start = std::time::Instant::now();
    let mut buffers = Vec::new();
    
    for i in 0..100 {
        let buffer = loader.mcp_buffer_create(1024)?;
        buffers.push(buffer);
    }
    
    let buffer_creation_time = start.elapsed();
    
    // Test data operations performance
    let start = std::time::Instant::now();
    let test_data = vec![0x42; 1024];
    
    for buffer in &buffers {
        loader.mcp_buffer_set_data(*buffer, &test_data)?;
        let (retrieved_data, _) = loader.mcp_buffer_get_data(*buffer)?;
        assert_eq!(retrieved_data.len(), test_data.len());
    }
    
    let data_operations_time = start.elapsed();
    
    loader.mcp_shutdown()?;
    
    Ok(format!(
        "Performance: {} filters in {:?}, {} buffers in {:?}, data ops in {:?}",
        filters.len(), filter_creation_time,
        buffers.len(), buffer_creation_time,
        data_operations_time
    ))
}

/// Run all integration tests
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize logging
    tracing_subscriber::fmt::init();
    
    println!("🧪 MCP Filter SDK - Integration Tests");
    println!("====================================");
    
    let tests: Vec<(&str, fn() -> Result<String, Box<dyn std::error::Error>>)> = vec![
        ("MCP Initialization", test_mcp_initialization),
        ("Dispatcher Management", test_dispatcher_management),
        ("Filter Creation", test_filter_creation),
        ("Buffer Operations", test_buffer_operations),
        ("Filter Chain Operations", test_filter_chain_operations),
        ("Advanced Chain Management", test_advanced_chain_management),
        ("JSON Operations", test_json_operations),
        ("Error Handling", test_error_handling),
        ("Performance", test_performance),
    ];
    
    let mut results = Vec::new();
    let mut passed = 0;
    let mut failed = 0;
    
    for (name, test_fn) in &tests {
        println!("\n🔧 Running test: {}", name);
        let result = run_test(name, test_fn);
        
        if result.success {
            println!("   ✅ PASSED in {:?}", result.duration);
            if let Some(data) = &result.data {
                println!("   📊 {}", data);
            }
            passed += 1;
        } else {
            println!("   ❌ FAILED in {:?}", result.duration);
            if let Some(error) = &result.error {
                println!("   🚨 Error: {}", error);
            }
            failed += 1;
        }
        
        results.push(result);
    }
    
    // Summary
    println!("\n📊 Test Summary");
    println!("===============");
    println!("Total tests: {}", tests.len());
    println!("Passed: {}", passed);
    println!("Failed: {}", failed);
    println!("Success rate: {:.1}%", (passed as f64 / tests.len() as f64) * 100.0);
    
    // Detailed results
    println!("\n📋 Detailed Results");
    println!("===================");
    for result in &results {
        let status = if result.success { "✅" } else { "❌" };
        println!("{} {} - {:?}", status, result.name, result.duration);
    }
    
    if failed > 0 {
        println!("\n🚨 Some tests failed. Check the output above for details.");
        std::process::exit(1);
    } else {
        println!("\n🎉 All tests passed successfully!");
    }
    
    Ok(())
}
