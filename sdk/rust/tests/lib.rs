//! # MCP Filter SDK Tests
//!
//! Comprehensive test suite for the MCP Filter SDK.
//! This module provides unit tests, integration tests, and performance benchmarks.

pub mod test_config;
pub mod test_runner;
pub mod unit_tests;
pub mod integration_tests;

use test_config::TestConfig;
use test_runner::run_comprehensive_tests;

/// Run all tests
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    run_comprehensive_tests().await
}
