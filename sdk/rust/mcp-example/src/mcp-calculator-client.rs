/**
 * @file mcp-calculator-client.rs
 * @brief Calculator Client with GopherTransport
 *
 * This is a calculator client that connects to the calculator server
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

/// Calculator client implementation
pub struct CalculatorClient {
    transport: GopherTransport,
    client_filter: CApiFilter,
}

impl CalculatorClient {
    /// Create a new calculator client
    pub fn new() -> Result<Self, Box<dyn std::error::Error>> {
        // Initialize the library loader
        let library = Arc::new(LibraryLoader::new()?);
        
        // Create client-specific filter callbacks
        let callbacks = FilterCallbacks {
            on_data: Some(Box::new(|data, end_stream| {
                info!("📊 Client filter processing data: {} bytes, end_stream: {}", data.len(), end_stream);
                FilterStatus::Continue
            })),
            on_write: Some(Box::new(|data, end_stream| {
                info!("📤 Client filter writing data: {} bytes, end_stream: {}", data.len(), end_stream);
                FilterStatus::Continue
            })),
            on_new_connection: Some(Box::new(|state| {
                info!("🔗 Client filter: new connection with state: {}", state);
            })),
            on_error: Some(Box::new(|code, message| {
                error!("❌ Client filter error: {} - {}", code, message);
            })),
            on_high_watermark: Some(Box::new(|| {
                warn!("⚠️ Client filter: high watermark reached");
            })),
            on_low_watermark: Some(Box::new(|| {
                info!("📉 Client filter: low watermark reached");
            })),
            user_data: None,
        };
        
        // Create CApiFilter
        let client_filter = CApiFilter::new(
            library,
            callbacks,
            None,
        )?;
        
        let config = GopherTransportConfig {
            name: "calculator-client".to_string(),
            version: "1.0.0".to_string(),
            protocol: ProtocolType::Tcp,
            host: Some("localhost".to_string()),
            port: Some(8080),
            connect_timeout: Some(Duration::from_millis(30000)),
            send_timeout: Some(Duration::from_millis(5000)),
            receive_timeout: Some(Duration::from_millis(5000)),
            max_connections: Some(1),
            buffer_size: Some(8192),
            filter_config: Some(FilterManagerConfig {
                max_filters: 5,
                debug: true,
                metrics: true,
            }),
        };

        Ok(Self {
            transport: GopherTransport::new(config),
            client_filter,
        })
    }

    /// Connect to the calculator server
    pub async fn connect(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("🚀 Starting Calculator MCP Client with GopherTransport");

        // Initialize the transport
        self.transport.start().await?;
        info!("✅ Transport started successfully");

        Ok(())
    }

    /// Run example calculations
    pub async fn run_examples(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("🧮 Running calculator examples...");

        // Test different types of messages
        let messages = vec![
            json!({
                "jsonrpc": "2.0",
                "id": 1,
                "method": "add",
                "params": {
                    "a": 5,
                    "b": 3
                }
            }),
            json!({
                "jsonrpc": "2.0",
                "id": 2,
                "method": "subtract",
                "params": {
                    "a": 10,
                    "b": 4
                }
            }),
            json!({
                "jsonrpc": "2.0",
                "id": 3,
                "method": "multiply",
                "params": {
                    "a": 6,
                    "b": 7
                }
            }),
        ];

        for message in messages {
            self.transport.send(message).await?;
            info!("✅ Sent calculation request");
        }

        Ok(())
    }

    /// Disconnect from the server
    pub async fn disconnect(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("🔌 Disconnecting from server...");
        
        self.transport.close().await?;
        info!("✅ Transport closed");
        
        info!("👋 Client disconnected successfully");
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize tracing
    tracing_subscriber::fmt()
        .with_env_filter("info")
        .init();

    let mut client = CalculatorClient::new()?;
    
    // Connect to the server
    if let Err(e) = client.connect().await {
        error!("Failed to connect to server: {}", e);
        return Err(e);
    }

    // Run example calculations
    if let Err(e) = client.run_examples().await {
        error!("Failed to run examples: {}", e);
        client.disconnect().await?;
        return Err(e);
    }

    // Disconnect
    client.disconnect().await?;

    Ok(())
}