//! # Calculator Server Example
//!
//! This example demonstrates how to use the MCP Filter SDK to create a calculator server.

use mcp_filter_sdk::{init, GopherTransport, GopherTransportConfig, ProtocolType};
use serde_json::json;
use std::time::Duration;
use tokio::time::sleep;

/// Calculator server implementation
pub struct CalculatorServer {
    transport: GopherTransport,
}

impl CalculatorServer {
    /// Create a new calculator server
    pub fn new() -> Self {
        let config = GopherTransportConfig {
            name: "calculator-server".to_string(),
            version: "1.0.0".to_string(),
            protocol: ProtocolType::Tcp,
            host: Some("localhost".to_string()),
            port: Some(8080),
            connect_timeout: Some(Duration::from_millis(30000)),
            send_timeout: Some(Duration::from_millis(5000)),
            receive_timeout: Some(Duration::from_millis(5000)),
            max_connections: Some(10),
            buffer_size: Some(8192),
            filter_config: None,
        };

        Self {
            transport: GopherTransport::new(config),
        }
    }

    /// Start the calculator server
    pub async fn start(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("🚀 Starting calculator server...");
        self.transport.start().await?;
        println!("✅ Calculator server started on port 8080");
        Ok(())
    }

    /// Stop the calculator server
    pub async fn stop(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("🛑 Stopping calculator server...");
        self.transport.close().await?;
        println!("✅ Calculator server stopped");
        Ok(())
    }

    /// Handle a calculation request
    pub async fn handle_calculation(
        &self,
        operation: &str,
        a: f64,
        b: f64,
    ) -> Result<f64, Box<dyn std::error::Error>> {
        let result = match operation {
            "add" => a + b,
            "subtract" => a - b,
            "multiply" => a * b,
            "divide" => {
                if b == 0.0 {
                    return Err("Division by zero".into());
                }
                a / b
            }
            _ => return Err("Unknown operation".into()),
        };

        Ok(result)
    }
}

/// Run the calculator server example
pub async fn run_example() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize the SDK
    init()?;

    println!("🧮 MCP Calculator Server with Rust SDK");
    println!("=========================================");

    let mut server = CalculatorServer::new();

    // Start server
    server.start().await?;

    // Simulate server running
    println!("📡 Server is running. Press Ctrl+C to stop.");

    // Keep server running
    loop {
        sleep(Duration::from_secs(1)).await;
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    run_example().await
}
