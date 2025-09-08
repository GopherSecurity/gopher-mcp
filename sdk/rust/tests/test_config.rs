//! # Test Configuration
//!
//! Configuration and utilities for running tests across different environments.

use std::env;
use std::path::PathBuf;
use tracing::{info, warn, error};

/// Test environment configuration
#[derive(Debug, Clone)]
pub struct TestConfig {
    /// Whether to use real C++ library in tests
    pub use_real_library: bool,
    /// Whether to run integration tests
    pub run_integration_tests: bool,
    /// Whether to run performance tests
    pub run_performance_tests: bool,
    /// Test data directory
    pub test_data_dir: PathBuf,
    /// Log level for tests
    pub log_level: String,
}

impl Default for TestConfig {
    fn default() -> Self {
        Self {
            use_real_library: env::var("USE_REAL_LIBRARY").is_ok(),
            run_integration_tests: env::var("RUN_INTEGRATION_TESTS").is_ok(),
            run_performance_tests: env::var("RUN_PERFORMANCE_TESTS").is_ok(),
            test_data_dir: PathBuf::from("test_data"),
            log_level: env::var("RUST_LOG").unwrap_or_else(|_| "info".to_string()),
        }
    }
}

impl TestConfig {
    /// Create a new test configuration
    pub fn new() -> Self {
        Self::default()
    }
    
    /// Check if real library is available
    pub fn is_real_library_available(&self) -> bool {
        if !self.use_real_library {
            return false;
        }
        
        // Check if the real C++ library exists
        let search_paths = vec![
            "../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
            "../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib",
            "/usr/local/lib/libgopher_mcp_c.dylib",
            "/opt/homebrew/lib/libgopher_mcp_c.dylib",
            "/usr/lib/libgopher_mcp_c.dylib",
        ];
        
        for path in search_paths {
            if std::path::Path::new(path).exists() {
                return true;
            }
        }
        
        false
    }
    
    /// Setup test environment
    pub fn setup(&self) -> Result<(), Box<dyn std::error::Error>> {
        // Setup logging
        std::env::set_var("RUST_LOG", &self.log_level);
        tracing_subscriber::fmt::init();
        
        // Create test data directory
        if !self.test_data_dir.exists() {
            std::fs::create_dir_all(&self.test_data_dir)?;
        }
        
        info!("Test configuration:");
        info!("  Use real library: {}", self.use_real_library);
        info!("  Run integration tests: {}", self.run_integration_tests);
        info!("  Run performance tests: {}", self.run_performance_tests);
        info!("  Test data directory: {:?}", self.test_data_dir);
        info!("  Log level: {}", self.log_level);
        
        if self.use_real_library && !self.is_real_library_available() {
            warn!("Real library requested but not available, falling back to placeholder");
        }
        
        Ok(())
    }
    
    /// Get test data path
    pub fn test_data_path(&self, filename: &str) -> PathBuf {
        self.test_data_dir.join(filename)
    }
    
    /// Create test data
    pub fn create_test_data(&self) -> Result<(), Box<dyn std::error::Error>> {
        // Create sample test data files
        let test_data = serde_json::json!({
            "test_string": "Hello, MCP Filter SDK!",
            "test_number": 42,
            "test_boolean": true,
            "test_array": [1, 2, 3, 4, 5],
            "test_object": {
                "nested": "value",
                "count": 100
            }
        });
        
        let test_data_path = self.test_data_path("sample.json");
        std::fs::write(&test_data_path, serde_json::to_string_pretty(&test_data)?)?;
        
        // Create binary test data
        let binary_data = vec![0x42; 1024];
        let binary_data_path = self.test_data_path("sample.bin");
        std::fs::write(&binary_data_path, &binary_data)?;
        
        info!("Created test data files:");
        info!("  JSON: {:?}", test_data_path);
        info!("  Binary: {:?}", binary_data_path);
        
        Ok(())
    }
    
    /// Cleanup test data
    pub fn cleanup_test_data(&self) -> Result<(), Box<dyn std::error::Error>> {
        if self.test_data_dir.exists() {
            std::fs::remove_dir_all(&self.test_data_dir)?;
            info!("Cleaned up test data directory: {:?}", self.test_data_dir);
        }
        Ok(())
    }
}

/// Test utilities
pub mod utils {
    use super::*;
    use std::time::Duration;
    
    /// Run a test with timeout
    pub async fn run_with_timeout<F, T>(
        test_fn: F,
        timeout: Duration,
    ) -> Result<T, Box<dyn std::error::Error>>
    where
        F: std::future::Future<Output = Result<T, Box<dyn std::error::Error>>>,
    {
        tokio::time::timeout(timeout, test_fn)
            .await
            .map_err(|_| "Test timeout".to_string())?
    }
    
    /// Generate test data of specified size
    pub fn generate_test_data(size: usize) -> Vec<u8> {
        (0..size).map(|i| (i % 256) as u8).collect()
    }
    
    /// Generate test JSON data
    pub fn generate_test_json(count: usize) -> serde_json::Value {
        serde_json::json!({
            "items": (0..count).map(|i| {
                serde_json::json!({
                    "id": i,
                    "name": format!("item_{}", i),
                    "value": i * 10,
                    "active": i % 2 == 0
                })
            }).collect::<Vec<_>>()
        })
    }
    
    /// Measure execution time
    pub fn measure_time<F, R>(f: F) -> (R, Duration)
    where
        F: FnOnce() -> R,
    {
        let start = std::time::Instant::now();
        let result = f();
        let duration = start.elapsed();
        (result, duration)
    }
    
    /// Assert that a condition holds within a timeout
    pub async fn assert_within_timeout<F>(
        condition: F,
        timeout: Duration,
        message: &str,
    ) -> Result<(), Box<dyn std::error::Error>>
    where
        F: Fn() -> bool,
    {
        let start = std::time::Instant::now();
        while start.elapsed() < timeout {
            if condition() {
                return Ok(());
            }
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
        Err(format!("Condition not met within timeout: {}", message).into())
    }
}

/// Test macros
#[macro_export]
macro_rules! test_with_config {
    ($name:ident, $config:expr, $test_fn:expr) => {
        #[tokio::test]
        async fn $name() {
            let config = $config;
            config.setup().unwrap();
            $test_fn(config).await.unwrap();
        }
    };
}

#[macro_export]
macro_rules! integration_test {
    ($name:ident, $test_fn:expr) => {
        #[tokio::test]
        async fn $name() {
            let config = TestConfig::new();
            if !config.run_integration_tests {
                println!("Skipping integration test: {}", stringify!($name));
                return;
            }
            config.setup().unwrap();
            $test_fn(config).await.unwrap();
        }
    };
}

#[macro_export]
macro_rules! performance_test {
    ($name:ident, $test_fn:expr) => {
        #[tokio::test]
        async fn $name() {
            let config = TestConfig::new();
            if !config.run_performance_tests {
                println!("Skipping performance test: {}", stringify!($name));
                return;
            }
            config.setup().unwrap();
            $test_fn(config).await.unwrap();
        }
    };
}
