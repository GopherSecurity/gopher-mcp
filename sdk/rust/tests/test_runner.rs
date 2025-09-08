//! # Test Runner
//!
//! Comprehensive test runner for the MCP Filter SDK.
//! This module provides utilities for running different types of tests.

use crate::test_config::{TestConfig, utils};
use mcp_filter_sdk::{
    EnhancedLibraryLoader, FilterCallbacks, FilterStatus, BuiltinFilterType,
    AdvancedChainManager, ConditionOperator,
};
use std::time::Duration;
use tracing::{info, warn, error};

/// Test suite runner
pub struct TestRunner {
    config: TestConfig,
}

impl TestRunner {
    /// Create a new test runner
    pub fn new(config: TestConfig) -> Self {
        Self { config }
    }
    
    /// Run all tests
    pub async fn run_all_tests(&self) -> Result<TestResults, Box<dyn std::error::Error>> {
        let mut results = TestResults::new();
        
        info!("🧪 Starting comprehensive test suite");
        
        // Run unit tests
        self.run_unit_tests(&mut results).await?;
        
        // Run integration tests if enabled
        if self.config.run_integration_tests {
            self.run_integration_tests(&mut results).await?;
        }
        
        // Run performance tests if enabled
        if self.config.run_performance_tests {
            self.run_performance_tests(&mut results).await?;
        }
        
        // Run stress tests
        self.run_stress_tests(&mut results).await?;
        
        // Run error handling tests
        self.run_error_handling_tests(&mut results).await?;
        
        info!("🎉 Test suite completed");
        results.print_summary();
        
        Ok(results)
    }
    
    /// Run unit tests
    async fn run_unit_tests(&self, results: &mut TestResults) -> Result<(), Box<dyn std::error::Error>> {
        info!("📋 Running unit tests");
        
        let tests: Vec<(&str, fn() -> Result<(), Box<dyn std::error::Error>>)> = vec![
            ("filter_callbacks", self.test_filter_callbacks),
            ("filter_status", self.test_filter_status),
            ("builtin_filter_types", self.test_builtin_filter_types),
            ("chain_execution_modes", self.test_chain_execution_modes),
            ("condition_operators", self.test_condition_operators),
            ("error_types", self.test_error_types),
        ];
        
        for (name, test_fn) in tests {
            let (result, duration) = utils::measure_time(|| test_fn());
            results.add_test(name, result.is_ok(), duration, result.err().map(|e| e.to_string()));
        }
        
        Ok(())
    }
    
    /// Run integration tests
    async fn run_integration_tests(&self, results: &mut TestResults) -> Result<(), Box<dyn std::error::Error>> {
        info!("🔗 Running integration tests");
        
        let tests: Vec<(&str, fn() -> std::pin::Pin<Box<dyn std::future::Future<Output = Result<(), Box<dyn std::error::Error>>>>>)> = vec![
            ("mcp_initialization", Box::pin(self.test_mcp_initialization())),
            ("dispatcher_management", Box::pin(self.test_dispatcher_management())),
            ("filter_creation", Box::pin(self.test_filter_creation())),
            ("buffer_operations", Box::pin(self.test_buffer_operations())),
            ("filter_chain_operations", Box::pin(self.test_filter_chain_operations())),
            ("advanced_chain_management", Box::pin(self.test_advanced_chain_management())),
            ("json_operations", Box::pin(self.test_json_operations())),
        ];
        
        for (name, test_fn) in tests {
            let (result, duration) = utils::measure_time(|| async {
                utils::run_with_timeout(test_fn, Duration::from_secs(30)).await
            });
            let result = result.await;
            results.add_test(name, result.is_ok(), duration, result.err().map(|e| e.to_string()));
        }
        
        Ok(())
    }
    
    /// Run performance tests
    async fn run_performance_tests(&self, results: &mut TestResults) -> Result<(), Box<dyn std::error::Error>> {
        info!("⚡ Running performance tests");
        
        let tests = vec![
            ("filter_creation_performance", self.test_filter_creation_performance().await),
            ("buffer_operations_performance", self.test_buffer_operations_performance().await),
            ("chain_execution_performance", self.test_chain_execution_performance().await),
            ("memory_allocation_performance", self.test_memory_allocation_performance().await),
            ("concurrent_operations_performance", self.test_concurrent_operations_performance().await),
        ];
        
        for (name, test_fn) in tests {
            let (result, duration) = utils::measure_time(|| async {
                utils::run_with_timeout(test_fn, Duration::from_secs(60)).await
            });
            let result = result.await;
            results.add_test(name, result.is_ok(), duration, result.err().map(|e| e.to_string()));
        }
        
        Ok(())
    }
    
    /// Run stress tests
    async fn run_stress_tests(&self, results: &mut TestResults) -> Result<(), Box<dyn std::error::Error>> {
        info!("💪 Running stress tests");
        
        let tests = vec![
            ("high_volume_filter_creation", self.test_high_volume_filter_creation().await),
            ("high_volume_buffer_operations", self.test_high_volume_buffer_operations().await),
            ("long_running_chain_execution", self.test_long_running_chain_execution().await),
            ("memory_pressure_test", self.test_memory_pressure().await),
        ];
        
        for (name, test_fn) in tests {
            let (result, duration) = utils::measure_time(|| async {
                utils::run_with_timeout(test_fn, Duration::from_secs(120)).await
            });
            let result = result.await;
            results.add_test(name, result.is_ok(), duration, result.err().map(|e| e.to_string()));
        }
        
        Ok(())
    }
    
    /// Run error handling tests
    async fn run_error_handling_tests(&self, results: &mut TestResults) -> Result<(), Box<dyn std::error::Error>> {
        info!("🚨 Running error handling tests");
        
        let tests = vec![
            ("invalid_parameters", self.test_invalid_parameters().await),
            ("resource_exhaustion", self.test_resource_exhaustion().await),
            ("concurrent_access_errors", self.test_concurrent_access_errors().await),
            ("timeout_handling", self.test_timeout_handling().await),
        ];
        
        for (name, test_fn) in tests {
            let (result, duration) = utils::measure_time(|| async {
                utils::run_with_timeout(test_fn, Duration::from_secs(30)).await
            });
            let result = result.await;
            results.add_test(name, result.is_ok(), duration, result.err().map(|e| e.to_string()));
        }
        
        Ok(())
    }
    
    // Unit test implementations
    fn test_filter_callbacks(&self) -> Result<(), Box<dyn std::error::Error>> {
        let callbacks = FilterCallbacks {
            on_data: Some(Box::new(|data, end_stream| {
                assert!(!data.is_empty());
                FilterStatus::Continue
            })),
            on_write: Some(Box::new(|data, end_stream| {
                assert!(!data.is_empty());
                FilterStatus::Continue
            })),
            on_new_connection: Some(Box::new(|state| {
                assert!(state >= 0);
            })),
            on_high_watermark: Some(Box::new(|| {})),
            on_low_watermark: Some(Box::new(|| {})),
            on_error: Some(Box::new(|error_code, message| {
                assert!(error_code >= 0);
                assert!(!message.is_empty());
            })),
            user_data: Some(Box::new("test_data".to_string())),
        };
        
        // Test callbacks
        if let Some(ref callback) = callbacks.on_data {
            let result = callback(b"test data", false);
            assert_eq!(result, FilterStatus::Continue);
        }
        
        Ok(())
    }
    
    fn test_filter_status(&self) -> Result<(), Box<dyn std::error::Error>> {
        assert_eq!(FilterStatus::Continue as i32, 0);
        assert_eq!(FilterStatus::Stop as i32, 1);
        assert_eq!(FilterStatus::StopAndBuffer as i32, 2);
        Ok(())
    }
    
    fn test_builtin_filter_types(&self) -> Result<(), Box<dyn std::error::Error>> {
        assert_eq!(BuiltinFilterType::Tcp as i32, 0);
        assert_eq!(BuiltinFilterType::RateLimit as i32, 11);
        assert_eq!(BuiltinFilterType::LoadBalancer as i32, 14);
        Ok(())
    }
    
    fn test_chain_execution_modes(&self) -> Result<(), Box<dyn std::error::Error>> {
        use mcp_filter_sdk::types::chains::ChainExecutionMode;
        assert_eq!(ChainExecutionMode::Sequential as i32, 0);
        assert_eq!(ChainExecutionMode::Parallel as i32, 1);
        assert_eq!(ChainExecutionMode::Conditional as i32, 2);
        assert_eq!(ChainExecutionMode::Pipeline as i32, 3);
        Ok(())
    }
    
    fn test_condition_operators(&self) -> Result<(), Box<dyn std::error::Error>> {
        assert_eq!(ConditionOperator::Equals as i32, 0);
        assert_eq!(ConditionOperator::NotEquals as i32, 1);
        assert_eq!(ConditionOperator::GreaterThan as i32, 2);
        assert_eq!(ConditionOperator::LessThan as i32, 3);
        assert_eq!(ConditionOperator::Contains as i32, 4);
        assert_eq!(ConditionOperator::StartsWith as i32, 5);
        assert_eq!(ConditionOperator::EndsWith as i32, 6);
        assert_eq!(ConditionOperator::Regex as i32, 7);
        Ok(())
    }
    
    fn test_error_types(&self) -> Result<(), Box<dyn std::error::Error>> {
        use mcp_filter_sdk::ffi::error::FilterError;
        
        let not_found = FilterError::NotFound {
            resource: "test_resource".to_string(),
        };
        assert!(format!("{:?}", not_found).contains("NotFound"));
        
        let internal = FilterError::Internal("test_error".to_string());
        assert!(format!("{:?}", internal).contains("Internal"));
        
        Ok(())
    }
    
    // Integration test implementations
    async fn test_mcp_initialization(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        let is_initialized = loader.mcp_is_initialized()?;
        assert!(is_initialized);
        let version = loader.mcp_get_version()?;
        assert!(!version.is_empty());
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_dispatcher_management(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        assert!(!dispatcher.is_null());
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_filter_creation(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        
        let custom_filter = loader.mcp_filter_create(dispatcher, std::ptr::null())?;
        assert!(custom_filter > 0);
        
        let builtin_filter = loader.mcp_filter_create_builtin(
            dispatcher,
            BuiltinFilterType::RateLimit as i32,
            std::ptr::null()
        )?;
        assert!(builtin_filter > 0);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_buffer_operations(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        
        let buffer = loader.mcp_buffer_create(1024)?;
        assert!(buffer > 0);
        
        let test_data = b"Hello, MCP Filter SDK!";
        loader.mcp_buffer_set_data(buffer, test_data)?;
        
        let (retrieved_data, size) = loader.mcp_buffer_get_data(buffer)?;
        assert_eq!(size, test_data.len());
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_filter_chain_operations(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        
        let builder = loader.mcp_filter_chain_builder_create(dispatcher)?;
        assert!(!builder.is_null());
        
        let filter1 = loader.mcp_filter_create(dispatcher, std::ptr::null())?;
        let filter2 = loader.mcp_filter_create_builtin(dispatcher, BuiltinFilterType::Metrics as i32, std::ptr::null())?;
        
        loader.mcp_filter_chain_add_filter(builder, filter1, 0, 0)?;
        loader.mcp_filter_chain_add_filter(builder, filter2, 1, filter1)?;
        
        let chain = loader.mcp_filter_chain_build(builder)?;
        assert!(chain > 0);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_advanced_chain_management(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        let manager = AdvancedChainManager::new(Arc::new(loader));
        
        let filters = vec![1, 2, 3, 4, 5];
        let sequential_chain = manager.create_simple_chain(filters.clone(), "sequential-test")?;
        assert_eq!(sequential_chain.config.name, "sequential-test");
        assert_eq!(sequential_chain.nodes.len(), 5);
        
        let parallel_chain = manager.create_parallel_chain(filters.clone(), 2, "parallel-test")?;
        assert_eq!(parallel_chain.config.name, "parallel-test");
        assert_eq!(parallel_chain.config.max_parallel, 2);
        
        let conditions = vec![
            ("method".to_string(), ConditionOperator::Equals, serde_json::json!("GET"), Some(1)),
        ];
        let conditional_chain = manager.create_conditional_chain(conditions, "conditional-test")?;
        assert_eq!(conditional_chain.config.name, "conditional-test");
        assert_eq!(conditional_chain.conditions.len(), 1);
        
        Ok(())
    }
    
    async fn test_json_operations(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        
        let json_string = loader.mcp_json_create_string("test_value")?;
        assert!(!json_string.is_null());
        
        let json_number = loader.mcp_json_create_number(42.0)?;
        assert!(!json_number.is_null());
        
        let json_bool = loader.mcp_json_create_bool(true)?;
        assert!(!json_bool.is_null());
        
        let json_null = loader.mcp_json_create_null()?;
        assert!(!json_null.is_null());
        
        let stringified = loader.mcp_json_stringify(json_string)?;
        assert!(!stringified.is_empty());
        
        loader.mcp_json_free(json_string);
        loader.mcp_json_free(json_number);
        loader.mcp_json_free(json_bool);
        loader.mcp_json_free(json_null);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    // Performance test implementations
    async fn test_filter_creation_performance(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        
        let start = std::time::Instant::now();
        let mut filters = Vec::new();
        
        for _ in 0..1000 {
            let filter = loader.mcp_filter_create(dispatcher, std::ptr::null())?;
            filters.push(filter);
        }
        
        let duration = start.elapsed();
        info!("Created {} filters in {:?}", filters.len(), duration);
        
        assert!(duration.as_secs() < 10, "Filter creation took too long: {:?}", duration);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_buffer_operations_performance(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        
        let start = std::time::Instant::now();
        let mut buffers = Vec::new();
        
        for _ in 0..1000 {
            let buffer = loader.mcp_buffer_create(1024)?;
            buffers.push(buffer);
        }
        
        let duration = start.elapsed();
        info!("Created {} buffers in {:?}", buffers.len(), duration);
        
        assert!(duration.as_secs() < 5, "Buffer creation took too long: {:?}", duration);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_chain_execution_performance(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        let manager = AdvancedChainManager::new(Arc::new(loader));
        
        let filters = vec![1, 2, 3, 4, 5];
        let chain = manager.create_simple_chain(filters, "performance-test")?;
        
        let test_data = utils::generate_test_data(1024);
        
        let start = std::time::Instant::now();
        
        for _ in 0..100 {
            let result = manager.execute_chain(&chain, &test_data).await?;
            assert_eq!(result.len(), test_data.len());
        }
        
        let duration = start.elapsed();
        info!("Executed chain 100 times in {:?}", duration);
        
        assert!(duration.as_secs() < 30, "Chain execution took too long: {:?}", duration);
        
        Ok(())
    }
    
    async fn test_memory_allocation_performance(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        
        let start = std::time::Instant::now();
        
        for _ in 0..100 {
            let mut filters = Vec::new();
            let mut buffers = Vec::new();
            
            for _ in 0..10 {
                let filter = loader.mcp_filter_create(dispatcher, std::ptr::null())?;
                let buffer = loader.mcp_buffer_create(1024)?;
                filters.push(filter);
                buffers.push(buffer);
            }
            
            // Filters and buffers are automatically cleaned up when dropped
        }
        
        let duration = start.elapsed();
        info!("Memory allocation test completed in {:?}", duration);
        
        assert!(duration.as_secs() < 20, "Memory allocation took too long: {:?}", duration);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_concurrent_operations_performance(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = Arc::new(EnhancedLibraryLoader::new()?);
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        
        let start = std::time::Instant::now();
        
        let mut handles = Vec::new();
        
        for _ in 0..10 {
            let loader_clone = loader.clone();
            let dispatcher_clone = dispatcher;
            let handle = tokio::spawn(async move {
                let mut filters = Vec::new();
                for _ in 0..100 {
                    let filter = loader_clone.mcp_filter_create(dispatcher_clone, std::ptr::null()).unwrap();
                    filters.push(filter);
                }
                filters
            });
            handles.push(handle);
        }
        
        let mut total_filters = 0;
        for handle in handles {
            let filters = handle.await?;
            total_filters += filters.len();
        }
        
        let duration = start.elapsed();
        info!("Created {} filters concurrently in {:?}", total_filters, duration);
        
        assert!(duration.as_secs() < 15, "Concurrent operations took too long: {:?}", duration);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    // Stress test implementations
    async fn test_high_volume_filter_creation(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        
        let mut filters = Vec::new();
        
        for i in 0..10000 {
            let filter = loader.mcp_filter_create(dispatcher, std::ptr::null())?;
            filters.push(filter);
            
            if i % 1000 == 0 {
                info!("Created {} filters", i);
            }
        }
        
        info!("Successfully created {} filters", filters.len());
        assert_eq!(filters.len(), 10000);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_high_volume_buffer_operations(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        
        let mut buffers = Vec::new();
        let test_data = utils::generate_test_data(1024);
        
        for i in 0..10000 {
            let buffer = loader.mcp_buffer_create(1024)?;
            loader.mcp_buffer_set_data(buffer, &test_data)?;
            buffers.push(buffer);
            
            if i % 1000 == 0 {
                info!("Created {} buffers", i);
            }
        }
        
        info!("Successfully created {} buffers", buffers.len());
        assert_eq!(buffers.len(), 10000);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_long_running_chain_execution(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        let manager = AdvancedChainManager::new(Arc::new(loader));
        
        let filters = vec![1, 2, 3, 4, 5];
        let chain = manager.create_simple_chain(filters, "long-running-test")?;
        let test_data = utils::generate_test_data(1024);
        
        let start = std::time::Instant::now();
        
        for i in 0..10000 {
            let result = manager.execute_chain(&chain, &test_data).await?;
            assert_eq!(result.len(), test_data.len());
            
            if i % 1000 == 0 {
                info!("Executed chain {} times", i);
            }
        }
        
        let duration = start.elapsed();
        info!("Executed chain 10000 times in {:?}", duration);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_memory_pressure(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        
        let mut filters = Vec::new();
        let mut buffers = Vec::new();
        
        // Create many resources to test memory pressure
        for i in 0..5000 {
            let filter = loader.mcp_filter_create(dispatcher, std::ptr::null())?;
            let buffer = loader.mcp_buffer_create(4096)?;
            filters.push(filter);
            buffers.push(buffer);
            
            if i % 500 == 0 {
                info!("Created {} filters and {} buffers", filters.len(), buffers.len());
            }
        }
        
        info!("Memory pressure test completed with {} filters and {} buffers", 
              filters.len(), buffers.len());
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    // Error handling test implementations
    async fn test_invalid_parameters(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        
        // Test with uninitialized library
        let result = loader.mcp_dispatcher_create();
        assert!(result.is_err(), "Should fail with uninitialized library");
        
        loader.mcp_init(None)?;
        
        // Test with null dispatcher
        let result = loader.mcp_filter_create(std::ptr::null(), std::ptr::null());
        assert!(result.is_err(), "Should fail with null dispatcher");
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_resource_exhaustion(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        
        // Test creating many filters to see if we hit resource limits
        let mut filters = Vec::new();
        let mut error_count = 0;
        
        for i in 0..100000 {
            match loader.mcp_filter_create(dispatcher, std::ptr::null()) {
                Ok(filter) => filters.push(filter),
                Err(_) => {
                    error_count += 1;
                    if error_count > 100 {
                        break; // Stop if we get too many errors
                    }
                }
            }
            
            if i % 10000 == 0 {
                info!("Created {} filters, {} errors", filters.len(), error_count);
            }
        }
        
        info!("Resource exhaustion test: {} filters created, {} errors", 
              filters.len(), error_count);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_concurrent_access_errors(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = Arc::new(EnhancedLibraryLoader::new()?);
        loader.mcp_init(None)?;
        let dispatcher = loader.mcp_dispatcher_create()?;
        
        let mut handles = Vec::new();
        
        // Spawn many concurrent operations
        for i in 0..100 {
            let loader_clone = loader.clone();
            let dispatcher_clone = dispatcher;
            let handle = tokio::spawn(async move {
                let mut filters = Vec::new();
                for _ in 0..100 {
                    match loader_clone.mcp_filter_create(dispatcher_clone, std::ptr::null()) {
                        Ok(filter) => filters.push(filter),
                        Err(e) => {
                            // Expected some errors due to concurrent access
                            eprintln!("Concurrent access error: {}", e);
                        }
                    }
                }
                filters
            });
            handles.push(handle);
        }
        
        let mut total_filters = 0;
        let mut error_count = 0;
        
        for handle in handles {
            match handle.await {
                Ok(filters) => total_filters += filters.len(),
                Err(e) => {
                    error_count += 1;
                    eprintln!("Task error: {}", e);
                }
            }
        }
        
        info!("Concurrent access test: {} filters created, {} task errors", 
              total_filters, error_count);
        
        loader.mcp_shutdown()?;
        Ok(())
    }
    
    async fn test_timeout_handling(&self) -> Result<(), Box<dyn std::error::Error>> {
        let loader = EnhancedLibraryLoader::new()?;
        loader.mcp_init(None)?;
        
        // Test timeout with a long-running operation
        let result = utils::run_with_timeout(
            async {
                // Simulate a long-running operation
                tokio::time::sleep(Duration::from_secs(10)).await;
                Ok::<(), Box<dyn std::error::Error>>(())
            },
            Duration::from_secs(1)
        ).await;
        
        assert!(result.is_err(), "Should timeout");
        
        loader.mcp_shutdown()?;
        Ok(())
    }
}

/// Test results structure
#[derive(Debug, Default)]
pub struct TestResults {
    tests: Vec<TestResult>,
}

#[derive(Debug)]
struct TestResult {
    name: String,
    success: bool,
    duration: Duration,
    error: Option<String>,
}

impl TestResults {
    fn new() -> Self {
        Self { tests: Vec::new() }
    }
    
    fn add_test(&mut self, name: &str, success: bool, duration: Duration, error: Option<String>) {
        self.tests.push(TestResult {
            name: name.to_string(),
            success,
            duration,
            error,
        });
    }
    
    fn print_summary(&self) {
        let total = self.tests.len();
        let passed = self.tests.iter().filter(|t| t.success).count();
        let failed = total - passed;
        let success_rate = if total > 0 { (passed as f64 / total as f64) * 100.0 } else { 0.0 };
        
        println!("\n📊 Test Summary");
        println!("===============");
        println!("Total tests: {}", total);
        println!("Passed: {}", passed);
        println!("Failed: {}", failed);
        println!("Success rate: {:.1}%", success_rate);
        
        if failed > 0 {
            println!("\n❌ Failed Tests:");
            for test in &self.tests {
                if !test.success {
                    println!("  - {} ({:?})", test.name, test.duration);
                    if let Some(error) = &test.error {
                        println!("    Error: {}", error);
                    }
                }
            }
        }
        
        println!("\n✅ Passed Tests:");
        for test in &self.tests {
            if test.success {
                println!("  - {} ({:?})", test.name, test.duration);
            }
        }
    }
}

/// Main test runner function
pub async fn run_comprehensive_tests() -> Result<(), Box<dyn std::error::Error>> {
    let config = TestConfig::new();
    config.setup()?;
    
    let runner = TestRunner::new(config);
    let results = runner.run_all_tests().await?;
    
    if results.tests.iter().any(|t| !t.success) {
        std::process::exit(1);
    }
    
    Ok(())
}
