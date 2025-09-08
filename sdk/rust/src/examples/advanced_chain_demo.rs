//! # Advanced Chain Demo
//!
//! This example demonstrates advanced filter chain management including
//! sequential, parallel, conditional, and pipeline execution modes.

use mcp_filter_sdk::{
    AdvancedChainManager, ConditionOperator, EnhancedLibraryLoader,
};
use serde_json::json;
use std::sync::Arc;
use tracing::{info, warn, error};

/// Main demo function
pub async fn run_advanced_chain_demo() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize logging
    tracing_subscriber::fmt::init();

    println!("🔧 Advanced Chain Management Demo with Rust SDK");
    println!("================================================");

    // Create library loader
    let library = Arc::new(EnhancedLibraryLoader::new()?);

    // Create advanced chain manager
    let mut manager = AdvancedChainManager::new(library);

    // Demonstrate different chain types
    demonstrate_sequential_chain(&manager).await?;
    demonstrate_parallel_chain(&manager).await?;
    demonstrate_conditional_chain(&manager).await?;
    demonstrate_pipeline_chain(&manager).await?;

    // Demonstrate chain execution
    demonstrate_chain_execution(&mut manager).await?;

    println!("\n🎉 Advanced chain demo completed successfully!");
    Ok(())
}

/// Demonstrate sequential chain creation and execution
async fn demonstrate_sequential_chain(
    manager: &AdvancedChainManager,
) -> Result<(), Box<dyn std::error::Error>> {
    println!("\n📋 Sequential Chain Demo");
    println!("------------------------");

    // Create a sequential chain with multiple filters
    let filters = vec![1, 2, 3, 4, 5];
    let chain = manager
        .create_simple_chain(filters, "sequential-demo")
        .unwrap();

    println!("✅ Created sequential chain: {}", chain.config.name);
    println!("   - Execution mode: {:?}", chain.config.execution_mode);
    println!("   - Number of filters: {}", chain.nodes.len());
    println!("   - Max parallel: {}", chain.config.max_parallel);

    Ok(())
}

/// Demonstrate parallel chain creation and execution
async fn demonstrate_parallel_chain(
    manager: &AdvancedChainManager,
) -> Result<(), Box<dyn std::error::Error>> {
    println!("\n⚡ Parallel Chain Demo");
    println!("----------------------");

    // Create a parallel chain with multiple filters
    let filters = vec![10, 20, 30, 40];
    let max_parallel = 2;
    let chain = manager
        .create_parallel_chain(filters, max_parallel, "parallel-demo")
        .unwrap();

    println!("✅ Created parallel chain: {}", chain.config.name);
    println!("   - Execution mode: {:?}", chain.config.execution_mode);
    println!("   - Number of filters: {}", chain.nodes.len());
    println!("   - Max parallel: {}", chain.config.max_parallel);

    Ok(())
}

/// Demonstrate conditional chain creation and execution
async fn demonstrate_conditional_chain(
    manager: &AdvancedChainManager,
) -> Result<(), Box<dyn std::error::Error>> {
    println!("\n🔀 Conditional Chain Demo");
    println!("-------------------------");

    // Create conditional rules
    let conditions = vec![
        (
            "method".to_string(),
            ConditionOperator::Equals,
            json!("GET"),
            Some(1),
        ),
        (
            "path".to_string(),
            ConditionOperator::StartsWith,
            json!("/api"),
            Some(2),
        ),
        (
            "content_type".to_string(),
            ConditionOperator::Contains,
            json!("application/json"),
            Some(3),
        ),
        (
            "user_agent".to_string(),
            ConditionOperator::Regex,
            json!("Mozilla/.*"),
            Some(4),
        ),
    ];

    let chain = manager
        .create_conditional_chain(conditions, "conditional-demo")
        .unwrap();

    println!("✅ Created conditional chain: {}", chain.config.name);
    println!("   - Execution mode: {:?}", chain.config.execution_mode);
    println!("   - Number of conditions: {}", chain.conditions.len());
    println!("   - Routing strategy: {:?}", chain.config.routing_strategy);

    // Show the conditions
    for (i, condition) in chain.conditions.iter().enumerate() {
        println!("   - Condition {}: {} {:?} {:?} -> chain {}",
            i + 1,
            condition.field,
            condition.operator,
            condition.value,
            condition.chain_id.unwrap_or(0)
        );
    }

    Ok(())
}

/// Demonstrate pipeline chain creation and execution
async fn demonstrate_pipeline_chain(
    manager: &AdvancedChainManager,
) -> Result<(), Box<dyn std::error::Error>> {
    println!("\n🔧 Pipeline Chain Demo");
    println!("----------------------");

    // Create a pipeline chain with buffering
    let filters = vec![100, 200, 300, 400, 500];
    let buffer_size = 16384; // 16KB buffer
    let chain = manager
        .create_pipeline_chain(filters, buffer_size, "pipeline-demo")
        .unwrap();

    println!("✅ Created pipeline chain: {}", chain.config.name);
    println!("   - Execution mode: {:?}", chain.config.execution_mode);
    println!("   - Number of filters: {}", chain.nodes.len());
    println!("   - Buffer size: {} bytes", chain.config.buffer_size);
    println!("   - Timeout: {} ms", chain.config.timeout_ms);

    Ok(())
}

/// Demonstrate chain execution with different data types
async fn demonstrate_chain_execution(
    manager: &mut AdvancedChainManager,
) -> Result<(), Box<dyn std::error::Error>> {
    println!("\n🚀 Chain Execution Demo");
    println!("-----------------------");

    // Create and add a test chain
    let filters = vec![1, 2, 3];
    let chain = manager
        .create_simple_chain(filters, "execution-test")
        .unwrap();
    manager.add_chain(chain);

    // Test data samples
    let large_data = vec![0xAA; 1000];
    let test_cases = vec![
        ("Simple text", b"hello world".as_slice()),
        ("JSON data", br#"{"message": "test", "value": 42}"#.as_slice()),
        ("Binary data", &[0x00, 0x01, 0x02, 0x03, 0x04]),
        ("Empty data", b"".as_slice()),
        ("Large data", &large_data),
    ];

    for (description, data) in test_cases {
        println!("\n📤 Testing: {}", description);
        println!("   Input size: {} bytes", data.len());

        match manager.execute_chain("execution-test", data).await {
            Ok(result) => {
                println!("   ✅ Execution successful");
                println!("   Output size: {} bytes", result.len());
                if result != data {
                    println!("   🔄 Data was modified by filters");
                } else {
                    println!("   ➡️ Data passed through unchanged");
                }
            }
            Err(e) => {
                println!("   ❌ Execution failed: {}", e);
            }
        }
    }

    // Demonstrate chain management
    println!("\n📊 Chain Management");
    println!("------------------");
    println!("Total chains: {}", manager.chain_count());

    if let Some(chain) = manager.get_chain("execution-test") {
        println!("Found chain: {}", chain.config.name);
        println!("  - Filters: {}", chain.nodes.len());
        println!("  - Mode: {:?}", chain.config.execution_mode);
    }

    // Test chain removal
    let removed = manager.remove_chain("execution-test");
    println!("Chain removed: {}", removed);
    println!("Remaining chains: {}", manager.chain_count());

    Ok(())
}

/// Demonstrate performance characteristics
async fn demonstrate_performance(
    manager: &mut AdvancedChainManager,
) -> Result<(), Box<dyn std::error::Error>> {
    println!("\n⚡ Performance Demo");
    println!("------------------");

    // Create a performance test chain
    let filters = vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    let chain = manager
        .create_simple_chain(filters, "performance-test")
        .unwrap();
    manager.add_chain(chain);

    // Test with different data sizes
    let data_sizes = vec![100, 1000, 10000, 100000];
    let test_data = vec![0x42; 100000]; // 100KB of test data

    for size in data_sizes {
        let data = &test_data[..size];
        let start = std::time::Instant::now();

        match manager.execute_chain("performance-test", data).await {
            Ok(_) => {
                let duration = start.elapsed();
                let throughput = (size as f64) / duration.as_secs_f64() / 1024.0; // KB/s
                println!("Size: {} bytes, Time: {:?}, Throughput: {:.2} KB/s",
                    size, duration, throughput);
            }
            Err(e) => {
                println!("Size: {} bytes, Error: {}", size, e);
            }
        }
    }

    Ok(())
}

/// Main function for running the demo
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    run_advanced_chain_demo().await
}
