//! # Performance Benchmarks
//!
//! Comprehensive performance benchmarks for the MCP Filter SDK.
//! These benchmarks measure the performance characteristics of various operations.

use criterion::{black_box, criterion_group, criterion_main, Criterion, BenchmarkId};
use mcp_filter_sdk::{
    EnhancedLibraryLoader, BuiltinFilterType,
};
use std::sync::Arc;

/// Benchmark library loading performance
fn benchmark_library_loading(c: &mut Criterion) {
    let mut group = c.benchmark_group("library_loading");
    
    // Benchmark library loader creation
    group.bench_function("create_loader", |b| {
        b.iter(|| {
            let loader = EnhancedLibraryLoader::new();
            black_box(loader);
        })
    });
    
    // Benchmark placeholder loader creation
    group.bench_function("create_placeholder_loader", |b| {
        b.iter(|| {
            let loader = EnhancedLibraryLoader::new_placeholder();
            black_box(loader);
        })
    });
    
    group.finish();
}

/// Benchmark JSON operations (using placeholder)
fn benchmark_json_operations(c: &mut Criterion) {
    let loader = match EnhancedLibraryLoader::new_placeholder() {
        Ok(loader) => Arc::new(loader),
        Err(_) => return,
    };
    
    let mut group = c.benchmark_group("json_operations");
    
    // Benchmark JSON string creation
    group.bench_function("create_string", |b| {
        b.iter(|| {
            let json = loader.mcp_json_create_string("test_string").unwrap();
            black_box(json);
        })
    });
    
    // Benchmark JSON number creation
    group.bench_function("create_number", |b| {
        b.iter(|| {
            let json = loader.mcp_json_create_number(42.0).unwrap();
            black_box(json);
        })
    });
    
    // Benchmark JSON boolean creation
    group.bench_function("create_bool", |b| {
        b.iter(|| {
            let json = loader.mcp_json_create_bool(true).unwrap();
            black_box(json);
        })
    });
    
    // Benchmark JSON null creation
    group.bench_function("create_null", |b| {
        b.iter(|| {
            let json = loader.mcp_json_create_null().unwrap();
            black_box(json);
        })
    });
    
    // Benchmark JSON stringify
    let json_string = match loader.mcp_json_create_string("test_string") {
        Ok(json) => json,
        Err(_) => {
            group.finish();
            return;
        }
    };
    group.bench_function("stringify", |b| {
        b.iter(|| {
            let stringified = loader.mcp_json_stringify(json_string).unwrap();
            black_box(stringified);
        })
    });
    
    group.finish();
    
    let _ = loader.mcp_json_free(json_string);
}

/// Benchmark error handling performance
fn benchmark_error_handling(c: &mut Criterion) {
    let mut group = c.benchmark_group("error_handling");
    
    // Benchmark error creation and handling
    group.bench_function("error_creation", |b| {
        b.iter(|| {
            let error = mcp_filter_sdk::ffi::error::FilterError::NotFound {
                resource: "test_resource".to_string(),
            };
            black_box(error);
        })
    });
    
    // Benchmark error propagation
    group.bench_function("error_propagation", |b| {
        b.iter(|| {
            let result: Result<i32, mcp_filter_sdk::ffi::error::FilterError> = Err(
                mcp_filter_sdk::ffi::error::FilterError::Internal("test_error".to_string())
            );
            let _ = result.map_err(|e| format!("Error: {:?}", e));
        })
    });
    
    group.finish();
}

/// Benchmark type system performance
fn benchmark_type_system(c: &mut Criterion) {
    let mut group = c.benchmark_group("type_system");
    
    // Benchmark BuiltinFilterType creation
    group.bench_function("builtin_filter_type_creation", |b| {
        b.iter(|| {
            let filter_type = BuiltinFilterType::RateLimit;
            black_box(filter_type);
        })
    });
    
    // Benchmark enum matching
    group.bench_function("enum_matching", |b| {
        b.iter(|| {
            let filter_type = BuiltinFilterType::RateLimit;
            let result = match filter_type {
                BuiltinFilterType::Authentication => "authentication",
                BuiltinFilterType::Authorization => "authorization",
                BuiltinFilterType::RateLimit => "rate_limit",
                BuiltinFilterType::CircuitBreaker => "circuit_breaker",
                BuiltinFilterType::Retry => "retry",
                BuiltinFilterType::Logging => "logging",
                BuiltinFilterType::Metrics => "metrics",
                BuiltinFilterType::Tracing => "tracing",
                BuiltinFilterType::Compression => "compression",
                BuiltinFilterType::Encryption => "encryption",
            };
            black_box(result);
        })
    });
    
    group.finish();
}

/// Benchmark memory allocation patterns
fn benchmark_memory_allocation(c: &mut Criterion) {
    let mut group = c.benchmark_group("memory_allocation");
    
    // Benchmark Vec creation and destruction
    group.bench_function("vec_creation_destruction", |b| {
        b.iter(|| {
            let mut vec = Vec::new();
            for i in 0..1000 {
                vec.push(i);
            }
            black_box(vec);
        })
    });
    
    // Benchmark String creation and destruction
    group.bench_function("string_creation_destruction", |b| {
        b.iter(|| {
            let mut string = String::new();
            for i in 0..100 {
                string.push_str(&format!("item_{}", i));
            }
            black_box(string);
        })
    });
    
    // Benchmark HashMap creation and destruction
    group.bench_function("hashmap_creation_destruction", |b| {
        b.iter(|| {
            use std::collections::HashMap;
            let mut map = HashMap::new();
            for i in 0..100 {
                map.insert(i, format!("value_{}", i));
            }
            black_box(map);
        })
    });
    
    group.finish();
}

criterion_group!(
    benches,
    benchmark_library_loading,
    benchmark_json_operations,
    benchmark_error_handling,
    benchmark_type_system,
    benchmark_memory_allocation
);

criterion_main!(benches);