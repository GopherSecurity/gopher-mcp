"""
Advanced Filter Chain Management.

This module provides Python wrappers for the MCP Filter Chain C API, including
chain composition, routing, optimization, and performance monitoring.
"""

import json
from typing import Any, Dict, List, Optional, Union, Callable
from dataclasses import dataclass, field
from contextlib import contextmanager

from mcp_types import (
    McpFilter,
    McpFilterChain,
    McpDispatcher,
    McpResult,
    McpBool,
    FilterStatus,
    FilterPosition,
    ChainExecutionMode,
    RoutingStrategy,
    MatchCondition,
    ChainState,
    FilterNode,
    ChainConfig,
    FilterCondition,
    ChainStats,
    RouterConfig,
    MCP_OK,
    MCP_ERROR_INVALID_ARGUMENT,
    MCP_ERROR_NOT_FOUND,
    MCP_ERROR_INVALID_STATE,
    MCP_ERROR_RESOURCE_EXHAUSTED,
    MCP_FILTER_CONTINUE,
    MCP_FILTER_STOP_ITERATION,
    MCP_FILTER_POSITION_FIRST,
    MCP_FILTER_POSITION_LAST,
    MCP_FILTER_POSITION_BEFORE,
    MCP_FILTER_POSITION_AFTER,
    MCP_CHAIN_MODE_SEQUENTIAL,
    MCP_CHAIN_MODE_PARALLEL,
    MCP_CHAIN_MODE_CONDITIONAL,
    MCP_CHAIN_MODE_PIPELINE,
    MCP_ROUTING_ROUND_ROBIN,
    MCP_ROUTING_LEAST_LOADED,
    MCP_ROUTING_HASH_BASED,
    MCP_ROUTING_PRIORITY,
    MCP_ROUTING_CUSTOM,
    MCP_MATCH_ALL,
    MCP_MATCH_ANY,
    MCP_MATCH_NONE,
    MCP_MATCH_CUSTOM,
    MCP_CHAIN_STATE_IDLE,
    MCP_CHAIN_STATE_PROCESSING,
    MCP_CHAIN_STATE_PAUSED,
    MCP_CHAIN_STATE_ERROR,
    MCP_CHAIN_STATE_COMPLETED,
)

from ffi_bindings import (
    mcp_filter_lib,
    mcp_filter_chain_builder_create,
    mcp_filter_chain_add_filter,
    mcp_filter_chain_build,
    mcp_filter_chain_builder_destroy,
    mcp_filter_chain_retain,
    mcp_filter_chain_release,
    check_result,
    create_mock_dispatcher,
)

from filter_api import Filter


# ============================================================================
# Filter Chain Class
# ============================================================================

class FilterChain:
    """
    Python wrapper for MCP Filter Chain.
    
    This class provides a high-level interface to the MCP Filter Chain C API,
    handling resource management and providing Pythonic methods.
    """
    
    def __init__(self, handle: McpFilterChain, dispatcher: Optional[McpDispatcher] = None):
        """
        Initialize filter chain with handle.
        
        Args:
            handle: Filter chain handle from C API
            dispatcher: Optional dispatcher handle
        """
        self._handle = handle
        self._dispatcher = dispatcher
        self._is_destroyed = False
    
    @property
    def handle(self) -> McpFilterChain:
        """Get filter chain handle."""
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        return self._handle
    
    @property
    def dispatcher(self) -> Optional[McpDispatcher]:
        """Get dispatcher handle."""
        return self._dispatcher
    
    def get_state(self) -> ChainState:
        """
        Get chain state.
        
        Returns:
            Current chain state
        """
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain state retrieval
        return ChainState.IDLE
    
    def pause(self) -> None:
        """Pause chain execution."""
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain pause
        pass
    
    def resume(self) -> None:
        """Resume chain execution."""
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain resume
        pass
    
    def reset(self) -> None:
        """Reset chain to initial state."""
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain reset
        pass
    
    def set_filter_enabled(self, filter_name: str, enabled: bool) -> None:
        """
        Enable/disable filter in chain.
        
        Args:
            filter_name: Name of filter to enable/disable
            enabled: Enable flag
        """
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement filter enable/disable
        pass
    
    def get_stats(self) -> ChainStats:
        """
        Get chain statistics.
        
        Returns:
            Chain statistics structure
        """
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain statistics retrieval
        stats = ChainStats()
        return stats
    
    def set_event_callback(self, callback: Callable, user_data: Optional[Any] = None) -> None:
        """
        Set chain event callback.
        
        Args:
            callback: Event callback function
            user_data: Optional user data
        """
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement event callback setting
        pass
    
    def dump(self, format: str = "text") -> str:
        """
        Dump chain structure.
        
        Args:
            format: Output format ("text", "json", "dot")
            
        Returns:
            String representation of chain
        """
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain dumping
        return f"FilterChain(handle={self._handle}, format={format})"
    
    def validate(self) -> List[str]:
        """
        Validate chain configuration.
        
        Returns:
            List of validation errors (empty if valid)
        """
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain validation
        return []
    
    def optimize(self) -> None:
        """Optimize chain by removing redundant filters."""
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain optimization
        pass
    
    def reorder_filters(self) -> None:
        """Reorder filters for optimal performance."""
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement filter reordering
        pass
    
    def profile(self, test_buffer: Optional[Any] = None, iterations: int = 1000) -> Dict[str, Any]:
        """
        Profile chain performance.
        
        Args:
            test_buffer: Test buffer for profiling
            iterations: Number of test iterations
            
        Returns:
            Performance report
        """
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain profiling
        return {
            "total_executions": iterations,
            "avg_latency_us": 0.0,
            "throughput_mbps": 0.0,
            "bottlenecks": [],
            "recommendations": []
        }
    
    def set_trace_level(self, trace_level: int) -> None:
        """
        Enable chain tracing.
        
        Args:
            trace_level: Trace level (0=off, 1=basic, 2=detailed)
        """
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        # TODO: Implement chain tracing
        pass
    
    def retain(self) -> None:
        """Retain filter chain (increment reference count)."""
        if self._is_destroyed:
            raise RuntimeError("Filter chain has been destroyed")
        
        retain_filter_chain(self._handle)
    
    def release(self) -> None:
        """Release filter chain (decrement reference count)."""
        if self._is_destroyed:
            return
        
        release_filter_chain(self._handle)
        self._is_destroyed = True
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.release()
    
    def __del__(self):
        """Destructor."""
        if not self._is_destroyed:
            self.release()


# ============================================================================
# Filter Chain Builder
# ============================================================================

class FilterChainBuilder:
    """
    Filter chain builder for constructing complex filter chains.
    
    This class provides a fluent interface for building filter chains
    with various execution modes and routing strategies.
    """
    
    def __init__(self, config: ChainConfig, dispatcher: Optional[McpDispatcher] = None):
        """
        Initialize filter chain builder.
        
        Args:
            config: Chain configuration
            dispatcher: Optional dispatcher handle
        """
        self._config = config
        self._dispatcher = dispatcher or create_mock_dispatcher()
        self._builder = create_filter_chain_builder(self._dispatcher)
        self._filters: List[Filter] = []
        self._is_built = False
    
    def add_filter(
        self,
        filter_instance: Filter,
        position: FilterPosition = FilterPosition.LAST,
        reference_filter: Optional[Filter] = None
    ) -> 'FilterChainBuilder':
        """
        Add filter to chain.
        
        Args:
            filter_instance: Filter to add
            position: Position in chain
            reference_filter: Reference filter for BEFORE/AFTER positions
            
        Returns:
            Self for method chaining
        """
        if self._is_built:
            raise RuntimeError("Chain has already been built")
        
        ref_handle = reference_filter.handle if reference_filter else 0
        result = add_filter_to_chain(self._builder, filter_instance.handle, position, ref_handle)
        check_result(result)
        
        self._filters.append(filter_instance)
        return self
    
    def add_conditional_filter(
        self,
        condition: FilterCondition,
        filter_instance: Filter
    ) -> 'FilterChainBuilder':
        """
        Add conditional filter.
        
        Args:
            condition: Condition for filter execution
            filter_instance: Filter to execute if condition met
            
        Returns:
            Self for method chaining
        """
        if self._is_built:
            raise RuntimeError("Chain has already been built")
        
        # TODO: Implement conditional filter addition
        self._filters.append(filter_instance)
        return self
    
    def add_parallel_group(self, filters: List[Filter]) -> 'FilterChainBuilder':
        """
        Add parallel filter group.
        
        Args:
            filters: Array of filters to run in parallel
            
        Returns:
            Self for method chaining
        """
        if self._is_built:
            raise RuntimeError("Chain has already been built")
        
        # TODO: Implement parallel group addition
        self._filters.extend(filters)
        return self
    
    def set_router(self, router: Callable, user_data: Optional[Any] = None) -> 'FilterChainBuilder':
        """
        Set custom routing function.
        
        Args:
            router: Custom routing function
            user_data: User data for router
            
        Returns:
            Self for method chaining
        """
        if self._is_built:
            raise RuntimeError("Chain has already been built")
        
        # TODO: Implement custom router setting
        return self
    
    def build(self) -> FilterChain:
        """
        Build filter chain.
        
        Returns:
            Built filter chain
        """
        if self._is_built:
            raise RuntimeError("Chain has already been built")
        
        handle = build_filter_chain(self._builder)
        if handle == 0:
            raise RuntimeError("Failed to build filter chain")
        
        self._is_built = True
        return FilterChain(handle, self._dispatcher)
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        if not self._is_built:
            destroy_filter_chain_builder(self._builder)


# ============================================================================
# Chain Factory Functions
# ============================================================================

def create_simple_chain(
    filters: List[Filter],
    config: Optional[ChainConfig] = None,
    dispatcher: Optional[McpDispatcher] = None
) -> FilterChain:
    """
    Create a simple sequential filter chain.
    
    Args:
        filters: List of filters to add
        config: Optional chain configuration
        dispatcher: Optional dispatcher handle
        
    Returns:
        Built filter chain
    """
    if config is None:
        config = ChainConfig(
            name="simple_chain",
            mode=ChainExecutionMode.SEQUENTIAL,
            routing=RoutingStrategy.ROUND_ROBIN
        )
    
    with FilterChainBuilder(config, dispatcher) as builder:
        for filter_instance in filters:
            builder.add_filter(filter_instance)
        return builder.build()


def create_parallel_chain(
    filter_groups: List[List[Filter]],
    config: Optional[ChainConfig] = None,
    dispatcher: Optional[McpDispatcher] = None
) -> FilterChain:
    """
    Create a parallel filter chain.
    
    Args:
        filter_groups: List of filter groups to run in parallel
        config: Optional chain configuration
        dispatcher: Optional dispatcher handle
        
    Returns:
        Built filter chain
    """
    if config is None:
        config = ChainConfig(
            name="parallel_chain",
            mode=ChainExecutionMode.PARALLEL,
            routing=RoutingStrategy.ROUND_ROBIN,
            max_parallel=len(filter_groups)
        )
    
    with FilterChainBuilder(config, dispatcher) as builder:
        for group in filter_groups:
            builder.add_parallel_group(group)
        return builder.build()


def create_conditional_chain(
    conditions: List[tuple[FilterCondition, Filter]],
    config: Optional[ChainConfig] = None,
    dispatcher: Optional[McpDispatcher] = None
) -> FilterChain:
    """
    Create a conditional filter chain.
    
    Args:
        conditions: List of (condition, filter) tuples
        config: Optional chain configuration
        dispatcher: Optional dispatcher handle
        
    Returns:
        Built filter chain
    """
    if config is None:
        config = ChainConfig(
            name="conditional_chain",
            mode=ChainExecutionMode.CONDITIONAL,
            routing=RoutingStrategy.ROUND_ROBIN
        )
    
    with FilterChainBuilder(config, dispatcher) as builder:
        for condition, filter_instance in conditions:
            builder.add_conditional_filter(condition, filter_instance)
        return builder.build()


def create_chain_from_json(
    json_config: Union[str, Dict[str, Any]],
    dispatcher: Optional[McpDispatcher] = None
) -> FilterChain:
    """
    Create dynamic chain from JSON configuration.
    
    Args:
        json_config: JSON configuration string or dict
        dispatcher: Optional dispatcher handle
        
    Returns:
        Built filter chain
    """
    if isinstance(json_config, str):
        config_dict = json.loads(json_config)
    else:
        config_dict = json_config
    
    # Parse chain configuration
    chain_config = ChainConfig(
        name=config_dict.get("name", "json_chain"),
        mode=ChainExecutionMode(config_dict.get("mode", ChainExecutionMode.SEQUENTIAL)),
        routing=RoutingStrategy(config_dict.get("routing", RoutingStrategy.ROUND_ROBIN)),
        max_parallel=config_dict.get("max_parallel", 1),
        buffer_size=config_dict.get("buffer_size", 4096),
        timeout_ms=config_dict.get("timeout_ms", 30000),
        stop_on_error=config_dict.get("stop_on_error", False)
    )
    
    # TODO: Parse filters from JSON and create chain
    with FilterChainBuilder(chain_config, dispatcher) as builder:
        # Add filters based on JSON configuration
        filters = config_dict.get("filters", [])
        for filter_config in filters:
            # TODO: Create filter from configuration
            pass
        return builder.build()


def export_chain_to_json(chain: FilterChain) -> Dict[str, Any]:
    """
    Export chain configuration to JSON.
    
    Args:
        chain: Filter chain to export
        
    Returns:
        JSON configuration dictionary
    """
    # TODO: Implement chain export to JSON
    return {
        "name": "exported_chain",
        "mode": chain.get_state(),
        "filters": []
    }


def clone_chain(chain: FilterChain) -> FilterChain:
    """
    Clone a filter chain.
    
    Args:
        chain: Source chain
        
    Returns:
        Cloned chain
    """
    # TODO: Implement chain cloning
    return chain


def merge_chains(
    chain1: FilterChain,
    chain2: FilterChain,
    mode: ChainExecutionMode = ChainExecutionMode.SEQUENTIAL
) -> FilterChain:
    """
    Merge two chains.
    
    Args:
        chain1: First chain
        chain2: Second chain
        mode: Merge mode (sequential, parallel)
        
    Returns:
        Merged chain
    """
    # TODO: Implement chain merging
    return chain1


# ============================================================================
# Context Managers
# ============================================================================

@contextmanager
def chain_builder_context(config: ChainConfig, dispatcher: Optional[McpDispatcher] = None):
    """
    Context manager for filter chain builder.
    
    Args:
        config: Chain configuration
        dispatcher: Optional dispatcher handle
        
    Yields:
        FilterChainBuilder instance
    """
    builder = FilterChainBuilder(config, dispatcher)
    try:
        yield builder
    finally:
        if not builder._is_built:
            destroy_filter_chain_builder(builder._builder)


# ============================================================================
# Module Exports
# ============================================================================

__all__ = [
    # Core classes
    "FilterChain",
    "FilterChainBuilder",
    "FilterNode",
    "ChainConfig",
    "FilterCondition",
    "ChainStats",
    "RouterConfig",
    
    # Chain creation
    "create_simple_chain",
    "create_parallel_chain",
    "create_conditional_chain",
    "create_chain_from_json",
    "export_chain_to_json",
    "clone_chain",
    "merge_chains",
    
    # Context managers
    "chain_builder_context",
    
    # Types
    "ChainExecutionMode",
    "RoutingStrategy",
    "MatchCondition",
    "ChainState",
    "FilterPosition",
    "FilterStatus",
]
