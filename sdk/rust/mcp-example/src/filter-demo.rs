/**
 * @file filter-demo.rs
 * @brief Filter demonstration using MCP Filter SDK
 *
 * This example demonstrates how to use the MCP Filter SDK to create
 * and manage filters with real C++ integration.
 *
 */

use mcp_filter_sdk::{
    EnhancedLibraryLoader, FilterManager, FilterManagerConfig, FilterType, FilterConfig,
    FilterCallbacks, FilterStatus, BuiltinFilterType, AdvancedChainManager,
    ConditionOperator, GopherTransport, GopherTransportConfig, ProtocolType
};
use serde_json::json;
use std::time::Duration;
use std::sync::Arc;
use tracing::{info, warn, error};

/// Filter demonstration implementation
pub struct FilterDemo {
    filter_manager: FilterManager,
    chain_manager: AdvancedChainManager,
    transport: GopherTransport,
}

impl FilterDemo {
    /// Create a new filter demo
    pub fn new() -> Result<Self, Box<dyn std::error::Error>> {
        // Initialize the enhanced library loader
        let library = Arc::new(EnhancedLibraryLoader::new()?);
        
        // Create filter manager
        let config = FilterManagerConfig {
            max_filters: 100,
            debug: true,
            metrics: true,
        };
        let filter_manager = FilterManager::with_config(config.clone())?;
        
        // Create chain manager
        let chain_manager = AdvancedChainManager::new(library);
        
        // Create transport
        let transport_config = GopherTransportConfig {
            name: "filter-demo".to_string(),
            version: "1.0.0".to_string(),
            protocol: ProtocolType::Stdio,
            host: None,
            port: None,
            connect_timeout: Some(Duration::from_millis(5000)),
            send_timeout: Some(Duration::from_millis(2000)),
            receive_timeout: Some(Duration::from_millis(5000)),
            max_connections: Some(5),
            buffer_size: Some(4096),
            filter_config: Some(config),
        };
        let transport = GopherTransport::new(transport_config);

        Ok(Self {
            filter_manager,
            chain_manager,
            transport,
        })
    }

    /// Run the filter demonstration
    pub async fn run(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("🔧 MCP Filter SDK Demonstration");
        info!("===============================");

        // Initialize the transport
        self.transport.start().await?;
        info!("✅ Transport initialized");

        // Demonstrate filter creation
        self.demonstrate_filter_creation().await?;

        // Demonstrate filter chains
        self.demonstrate_filter_chains().await?;

        // Demonstrate buffer operations
        self.demonstrate_buffer_operations().await?;

        // Demonstrate transport integration
        self.demonstrate_transport_integration().await?;

        info!("🎉 Filter demonstration completed successfully");
        Ok(())
    }

    /// Demonstrate filter creation
    async fn demonstrate_filter_creation(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("📝 Demonstrating filter creation...");

        // Create a custom filter with callbacks
        let callbacks = FilterCallbacks {
            on_data: Some(Box::new(|data, end_stream| {
                info!("📥 Received data: {} bytes, end_stream: {}", data.len(), end_stream);
                FilterStatus::Continue
            })),
            on_write: Some(Box::new(|data, end_stream| {
                info!("📤 Writing data: {} bytes, end_stream: {}", data.len(), end_stream);
                FilterStatus::Continue
            })),
            on_new_connection: Some(Box::new(|state| {
                info!("🔗 New connection: state={}", state);
            })),
            on_error: Some(Box::new(|error_code, error_msg| {
                warn!("❌ Filter error: {} - {}", error_code, error_msg);
            })),
            on_high_watermark: Some(Box::new(|| {
                info!("🌊 High watermark reached");
            })),
            on_low_watermark: Some(Box::new(|| {
                info!("🌊 Low watermark reached");
            })),
            user_data: None,
        };

        info!("✅ Filter callbacks configured");

        Ok(())
    }

    /// Demonstrate filter chains
    async fn demonstrate_filter_chains(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("🔗 Demonstrating filter chains...");

        // Create a simple sequential chain
        let filters = vec![1, 2, 3];
        let chain = self.chain_manager.create_simple_chain(filters, "demo-chain")?;
        info!("✅ Created simple chain: {}", chain.config.name);

        // Create a parallel chain
        let parallel_chain = self.chain_manager.create_parallel_chain(
            vec![1, 2, 3, 4],
            2,
            "parallel-demo-chain",
        )?;
        info!("✅ Created parallel chain: {}", parallel_chain.config.name);

        // Create a conditional chain
        let conditions = vec![(
            "request.method == 'POST'".to_string(),
            ConditionOperator::Equals,
            serde_json::Value::String("POST".to_string()),
            Some(1),
        )];
        let conditional_chain = self.chain_manager.create_conditional_chain(
            conditions,
            "conditional-demo-chain",
        )?;
        info!("✅ Created conditional chain: {}", conditional_chain.config.name);

        Ok(())
    }

    /// Demonstrate buffer operations
    async fn demonstrate_buffer_operations(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("💾 Demonstrating buffer operations...");

        // Note: Buffer operations would need to be implemented in FilterManager
        // For now, we'll just demonstrate the concept
        info!("✅ Buffer operations concept demonstrated");

        Ok(())
    }

    /// Demonstrate transport integration
    async fn demonstrate_transport_integration(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("🚀 Demonstrating transport integration...");

        // Send a test message through the transport
        let test_message = json!({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "test",
            "params": {
                "message": "Hello from filter demo!"
            }
        });

        self.transport.send(test_message).await?;
        info!("✅ Sent test message through transport");

        // Get transport statistics
        let stats = self.transport.get_stats();
        info!("📊 Transport stats: {:?}", stats);

        Ok(())
    }

    /// Cleanup resources
    pub async fn cleanup(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("🧹 Cleaning up resources...");
        
        self.transport.close().await?;
        info!("✅ Transport closed");
        
        info!("✅ Cleanup completed");
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing
    tracing_subscriber::fmt()
        .with_env_filter("info")
        .init();

    let mut demo = FilterDemo::new()?;
    
    // Handle shutdown signals
    let shutdown = async {
        tokio::signal::ctrl_c()
            .await
            .expect("Failed to listen for ctrl+c");
    };

    tokio::select! {
        result = demo.run() => {
            match result {
                Ok(_) => info!("Demo completed successfully"),
                Err(e) => error!("Demo failed: {}", e),
            }
        }
        _ = shutdown => {
            info!("Shutdown signal received");
        }
    }

    // Cleanup
    demo.cleanup().await?;

    Ok(())
}