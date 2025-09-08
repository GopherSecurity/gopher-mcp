//! # Example Applications
//!
//! This module contains example applications demonstrating the MCP Filter SDK usage.

pub mod advanced_chain_demo;
pub mod calculator_client;
pub mod calculator_server;
pub mod capifilter_demo;
pub mod filter_demo;
pub mod real_cpp_integration_demo;

// Re-export main examples
pub use calculator_client::*;
pub use calculator_server::*;
pub use filter_demo::*;
