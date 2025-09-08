//! # CApiFilter Demo
//!
//! This example demonstrates how to use CApiFilter with custom Rust callbacks
//! for processing MCP messages through the filter chain.

use mcp_filter_sdk::{
    create_custom_filter, create_builtin_filter_with_callbacks, FilterCallbacks, FilterConfig,
    FilterStatus, FilterType, BuiltinFilterType, LibraryLoader,
};
use serde_json::{json, Value};
use std::sync::Arc;
use tracing::{info, warn, error};

/// Custom user data for our filter
#[derive(Debug, Clone)]
struct FilterContext {
    filter_name: String,
    message_count: u64,
    last_message: Option<String>,
}

/// Main demo function
pub async fn run_capifilter_demo() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize logging
    tracing_subscriber::fmt::init();

    println!("🔧 CApiFilter Demo with Rust SDK");
    println!("=================================");

    // Create library loader
    let library = Arc::new(LibraryLoader::new()?);

    // Create custom filter with callbacks
    let custom_filter = create_custom_filter(
        library.clone(),
        create_custom_callbacks(),
        Some(Box::new(FilterContext {
            filter_name: "CustomFilter".to_string(),
            message_count: 0,
            last_message: None,
        })),
    )?;

    println!("✅ Created custom CApiFilter with handle: {}", custom_filter.handle());

    // Create built-in filter with callbacks
    let builtin_filter = create_builtin_filter_with_callbacks(
        library.clone(),
        FilterType::Builtin(BuiltinFilterType::RateLimit),
        FilterConfig::new("BuiltinFilter", "1.0.0", true, 100),
        create_builtin_callbacks(),
        Some(Box::new(FilterContext {
            filter_name: "BuiltinFilter".to_string(),
            message_count: 0,
            last_message: None,
        })),
    )?;

    println!("✅ Created built-in CApiFilter with handle: {}", builtin_filter.handle());

    // Simulate processing messages
    let messages = vec![
        json!({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "test",
            "params": {"data": "Hello from CApiFilter!"}
        }),
        json!({
            "jsonrpc": "2.0",
            "id": 2,
            "method": "calculate",
            "params": {"operation": "add", "a": 5, "b": 3}
        }),
        json!({
            "jsonrpc": "2.0",
            "id": 3,
            "method": "error_test",
            "params": {"should_fail": true}
        }),
    ];

    for (i, message) in messages.iter().enumerate() {
        println!("\n📤 Processing message {}: {}", i + 1, message);
        
        // Simulate filter processing
        simulate_filter_processing(&custom_filter, message).await?;
        simulate_filter_processing(&builtin_filter, message).await?;
    }

    println!("\n🎉 CApiFilter demo completed successfully!");
    Ok(())
}

/// Create custom callbacks for demonstration
fn create_custom_callbacks() -> FilterCallbacks {
    FilterCallbacks {
        on_data: Some(Box::new(|data, end_stream| {
            info!("🔍 Custom filter on_data: {} bytes, end_stream: {}", data.len(), end_stream);
            
            // Log the data (in a real implementation, this would be the actual message data)
            if let Ok(message) = std::str::from_utf8(data) {
                info!("📝 Custom filter processing message: {}", message);
            }
            
            // Always continue processing
            FilterStatus::Continue
        })),
        
        on_write: Some(Box::new(|data, end_stream| {
            info!("✍️ Custom filter on_write: {} bytes, end_stream: {}", data.len(), end_stream);
            
            // In a real implementation, this would modify the response data
            FilterStatus::Continue
        })),
        
        on_new_connection: Some(Box::new(|state| {
            info!("🔗 Custom filter on_new_connection: state = {}", state);
        })),
        
        on_high_watermark: Some(Box::new(|| {
            warn!("⚠️ Custom filter high watermark reached");
        })),
        
        on_low_watermark: Some(Box::new(|| {
            info!("✅ Custom filter low watermark reached");
        })),
        
        on_error: Some(Box::new(|error_code, message| {
            error!("❌ Custom filter error: {} - {}", error_code, message);
        })),
        
        user_data: None,
    }
}

/// Create built-in filter callbacks
fn create_builtin_callbacks() -> FilterCallbacks {
    FilterCallbacks {
        on_data: Some(Box::new(|data, end_stream| {
            info!("🏗️ Built-in filter on_data: {} bytes, end_stream: {}", data.len(), end_stream);
            
            // Simulate some processing
            if data.len() > 1000 {
                warn!("📊 Large message detected: {} bytes", data.len());
            }
            
            FilterStatus::Continue
        })),
        
        on_write: Some(Box::new(|data, end_stream| {
            info!("✍️ Built-in filter on_write: {} bytes, end_stream: {}", data.len(), end_stream);
            FilterStatus::Continue
        })),
        
        on_new_connection: Some(Box::new(|state| {
            info!("🔗 Built-in filter on_new_connection: state = {}", state);
        })),
        
        on_high_watermark: Some(Box::new(|| {
            warn!("⚠️ Built-in filter high watermark reached");
        })),
        
        on_low_watermark: Some(Box::new(|| {
            info!("✅ Built-in filter low watermark reached");
        })),
        
        on_error: Some(Box::new(|error_code, message| {
            error!("❌ Built-in filter error: {} - {}", error_code, message);
        })),
        
        user_data: None,
    }
}

/// Simulate filter processing (placeholder for actual C++ integration)
async fn simulate_filter_processing(
    filter: &mcp_filter_sdk::CApiFilter,
    message: &Value,
) -> Result<(), Box<dyn std::error::Error>> {
    // In a real implementation, this would:
    // 1. Convert the JSON message to bytes
    // 2. Pass the bytes to the C++ filter chain
    // 3. The C++ filter chain would call our Rust callbacks
    // 4. We would process the result and return it
    
    let message_bytes = serde_json::to_vec(message)?;
    
    // Simulate calling the data callback
    if let Some(ref callback) = filter.callbacks().on_data {
        let result = callback(&message_bytes, false);
        info!("🔄 Filter {} processed message, result: {:?}", 
              filter.handle(), result);
    }
    
    // Simulate calling the write callback
    if let Some(ref callback) = filter.callbacks().on_write {
        let result = callback(&message_bytes, false);
        info!("✍️ Filter {} wrote response, result: {:?}", 
              filter.handle(), result);
    }
    
    // Simulate connection event
    if let Some(ref callback) = filter.callbacks().on_new_connection {
        callback(1); // Connected state
    }
    
    // Simulate watermark events
    if message_bytes.len() > 500 {
        if let Some(ref callback) = filter.callbacks().on_high_watermark {
            callback();
        }
    } else {
        if let Some(ref callback) = filter.callbacks().on_low_watermark {
            callback();
        }
    }
    
    // Simulate error for error_test method
    if message.get("method").and_then(|m| m.as_str()) == Some("error_test") {
        if let Some(ref callback) = filter.callbacks().on_error {
            callback(-1000, "Simulated error for testing".to_string());
        }
    }
    
    Ok(())
}

/// Main function for running the demo
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    run_capifilter_demo().await
}
