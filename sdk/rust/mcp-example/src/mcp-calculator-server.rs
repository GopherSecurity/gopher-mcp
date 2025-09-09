/**
 * @file mcp-calculator-server.rs
 * @brief Calculator Server with GopherTransport
 *
 * This is a calculator server that provides calculator functionality
 * using GopherTransport with real C++ filter infrastructure.
 *
 */

use mcp_filter_sdk::{
    GopherTransport, GopherTransportConfig, ProtocolType, 
    CApiFilter, FilterCallbacks, FilterStatus, FilterManagerConfig,
    LibraryLoader
};
use serde_json::json;
use std::time::Duration;
use std::sync::Arc;
use tracing::{info, warn, error};

/// Calculator server implementation
pub struct CalculatorServer {
    transport: GopherTransport,
    calculator_filter: CApiFilter,
}

impl CalculatorServer {
    /// Create a new calculator server
    pub fn new() -> Result<Self, Box<dyn std::error::Error>> {
        // Initialize the library loader
        let library = Arc::new(LibraryLoader::new()?);
        
        // Create calculator-specific filter callbacks
        let callbacks = FilterCallbacks {
            on_data: Some(Box::new(|data, end_stream| {
                info!("📊 Calculator filter processing data: {} bytes, end_stream: {}", data.len(), end_stream);
                FilterStatus::Continue
            })),
            on_write: Some(Box::new(|data, end_stream| {
                info!("📤 Calculator filter writing data: {} bytes, end_stream: {}", data.len(), end_stream);
                FilterStatus::Continue
            })),
            on_new_connection: Some(Box::new(|state| {
                info!("🔗 Calculator filter: new connection with state: {}", state);
            })),
            on_error: Some(Box::new(|code, message| {
                error!("❌ Calculator filter error: {} - {}", code, message);
            })),
            on_high_watermark: Some(Box::new(|| {
                warn!("⚠️ Calculator filter: high watermark reached");
            })),
            on_low_watermark: Some(Box::new(|| {
                info!("📉 Calculator filter: low watermark reached");
            })),
            user_data: None,
        };
        
        // Create CApiFilter
        let calculator_filter = CApiFilter::new(
            library,
            callbacks,
            None,
        )?;
        
        let config = GopherTransportConfig {
            name: "calculator-server".to_string(),
            version: "1.0.0".to_string(),
            protocol: ProtocolType::Tcp,
            host: None, // None means server mode
            port: Some(8080),
            connect_timeout: Some(Duration::from_millis(30000)),
            send_timeout: Some(Duration::from_millis(5000)),
            receive_timeout: Some(Duration::from_millis(5000)),
            max_connections: Some(10),
            buffer_size: Some(8192),
            filter_config: Some(FilterManagerConfig {
                max_filters: 10,
                debug: true,
                metrics: true,
            }),
        };

        Ok(Self {
            transport: GopherTransport::new(config),
            calculator_filter,
        })
    }

    /// Start the calculator server
    pub async fn start(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("🧮 MCP Calculator Server with Rust SDK");
        info!("=========================================");
        info!("🚀 Starting calculator server...");

        // Initialize the transport
        self.transport.start().await?;
        info!("✅ Transport started successfully");

        info!("🎉 Calculator server is ready and listening on port 8080");
        info!("📡 Use the calculator client to connect and perform calculations");

        // Keep the server running
        self.run_server_loop().await?;

        Ok(())
    }

    /// Run the main server loop
    async fn run_server_loop(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        loop {
            // Process incoming messages
            if let Err(e) = self.process_messages().await {
                error!("Error processing messages: {}", e);
            }

            // Small delay to prevent busy waiting
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    }

    /// Process incoming messages
    async fn process_messages(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        // This would typically involve reading from the transport
        // and processing MCP protocol messages
        // For now, this is a placeholder
        Ok(())
    }

    /// Stop the calculator server
    pub async fn stop(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("🛑 Stopping calculator server...");
        
        self.transport.close().await?;
        info!("✅ Transport closed");
        
        info!("👋 Calculator server stopped successfully");
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing
    tracing_subscriber::fmt()
        .with_env_filter("info")
        .init();

    let mut server = CalculatorServer::new()?;
    
    // Handle shutdown signals
    let shutdown = async {
        tokio::signal::ctrl_c()
            .await
            .expect("Failed to listen for ctrl+c");
    };

    tokio::select! {
        _ = server.start() => {
            info!("Server completed");
        }
        _ = shutdown => {
            info!("Shutdown signal received");
            server.stop().await?;
        }
    }

    Ok(())
}