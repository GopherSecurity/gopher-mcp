// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

//! # Filter Demo Example
//!
//! This example demonstrates various filter operations using the MCP Filter SDK.

use mcp_filter_sdk::{init, FilterManager, FilterManagerConfig};
use serde_json::json;

/// Run the filter demo example
pub async fn run_example() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize the SDK
    init()?;

    println!("🔧 MCP Filter Demo with Rust SDK");
    println!("=================================");

    // Create filter manager
    let config = FilterManagerConfig::new()
        .with_max_filters(10)
        .with_debug(true)
        .with_metrics(true);

    let manager = FilterManager::new()?;

    // Create a test message
    let message = json!({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "test",
        "params": {
            "data": "Hello, MCP Filter SDK!"
        }
    });

    println!("📤 Processing message: {}", message);

    // Process the message (convert to bytes for now)
    let message_bytes = serde_json::to_vec(&message)?;
    let result = manager.process(&message_bytes).await?;

    println!("📥 Processed result: {:?}", result);
    println!("🎉 Filter demo completed successfully!");

    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    run_example().await
}
