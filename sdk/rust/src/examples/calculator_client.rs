//! # Calculator Client Example
//!
//! This example demonstrates how to use the MCP Filter SDK to create a calculator client.

use mcp_filter_sdk::{init, GopherTransport, GopherTransportConfig, ProtocolType};
use serde_json::json;
use std::time::Duration;
use tokio::time::sleep;

/// Calculator client implementation
pub struct CalculatorClient {
    transport: GopherTransport,
}

impl CalculatorClient {
    /// Create a new calculator client
    pub fn new() -> Self {
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
            filter_config: None,
        };

        Self {
            transport: GopherTransport::new(config),
        }
    }

    /// Connect to the calculator server
    pub async fn connect(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("🔗 Connecting to calculator server...");
        self.transport.start().await?;
        println!("✅ Connected to calculator server");
        Ok(())
    }

    /// Disconnect from the calculator server
    pub async fn disconnect(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("🔌 Disconnecting from calculator server...");
        self.transport.close().await?;
        println!("✅ Disconnected from calculator server");
        Ok(())
    }

    /// Perform a calculation
    pub async fn calculate(
        &self,
        operation: &str,
        a: f64,
        b: f64,
    ) -> Result<f64, Box<dyn std::error::Error>> {
        let message = json!({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "calculate",
            "params": {
                "operation": operation,
                "a": a,
                "b": b
            }
        });

        println!("🧮 Calculating: {} {} {}", a, operation, b);
        self.transport.send(message).await?;

        // Placeholder result
        let result = match operation {
            "add" => a + b,
            "subtract" => a - b,
            "multiply" => a * b,
            "divide" => a / b,
            _ => return Err("Unknown operation".into()),
        };

        println!("✅ Result: {}", result);
        Ok(result)
    }
}

/// Run the calculator client example
pub async fn run_example() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize the SDK
    init()?;

    println!("🧮 MCP Calculator Client with Rust SDK");
    println!("==========================================");

    let mut client = CalculatorClient::new();

    // Connect to server
    client.connect().await?;

    // Perform some calculations
    let operations = vec![
        ("add", 10.0, 5.0),
        ("subtract", 20.0, 8.0),
        ("multiply", 6.0, 7.0),
        ("divide", 100.0, 4.0),
    ];

    for (op, a, b) in operations {
        client.calculate(op, a, b).await?;
        sleep(Duration::from_millis(100)).await;
    }

    // Disconnect
    client.disconnect().await?;

    println!("🎉 Calculator client example completed successfully!");
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    run_example().await
}
