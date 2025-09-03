"""
Chain-specific type definitions.

This module defines data structures and types specific to filter chain operations,
including nodes, configuration, routing, and statistics.
"""

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Callable
from ctypes import c_void_p, c_uint64, c_uint32, c_bool

from ..mcp_types import (
    McpFilter,
    McpFilterChain,
    McpBufferHandle,
    McpJsonValue,
    McpMap,
    ChainExecutionMode,
    RoutingStrategy,
    MatchCondition,
    ChainState,
    RoutingFunction,
    ChainEventCallback,
    FilterMatchCallback,
)


# ============================================================================
# Filter Node
# ============================================================================

@dataclass
class FilterNode:
    """Filter node in chain."""
    filter: McpFilter
    name: Optional[str] = None
    priority: int = 0
    enabled: bool = True
    bypass_on_error: bool = False
    config: Optional[McpJsonValue] = None


# ============================================================================
# Chain Configuration
# ============================================================================

@dataclass
class ChainConfig:
    """Chain configuration structure."""
    name: Optional[str] = None
    mode: int = 0  # ChainExecutionMode
    routing: int = 0  # RoutingStrategy
    max_parallel: int = 1
    buffer_size: int = 4096
    timeout_ms: int = 30000
    stop_on_error: bool = False


# ============================================================================
# Filter Condition
# ============================================================================

@dataclass
class FilterCondition:
    """Filter condition for conditional execution."""
    match_type: int = 0  # MatchCondition
    field: Optional[str] = None
    value: Optional[str] = None
    target_filter: Optional[McpFilter] = None


# ============================================================================
# Chain Statistics
# ============================================================================

@dataclass
class ChainStats:
    """Chain statistics structure."""
    total_processed: int = 0
    total_errors: int = 0
    total_bypassed: int = 0
    avg_latency_ms: float = 0.0
    max_latency_ms: float = 0.0
    throughput_mbps: float = 0.0
    active_filters: int = 0


# ============================================================================
# Router Configuration
# ============================================================================

@dataclass
class RouterConfig:
    """Router configuration structure."""
    strategy: int = 0  # RoutingStrategy
    hash_seed: int = 0
    route_table: Optional[McpMap] = None  # Map of conditions to chains
    custom_router_data: Optional[c_void_p] = None


# ============================================================================
# Chain Pool Configuration
# ============================================================================

@dataclass
class ChainPoolConfig:
    """Chain pool configuration."""
    pool_size: int = 10
    strategy: int = 0  # RoutingStrategy
    health_check_interval: int = 30000  # milliseconds
    max_idle_time: int = 300000  # milliseconds
    auto_scale: bool = False
    min_pool_size: int = 2
    max_pool_size: int = 50


# ============================================================================
# Chain Optimization
# ============================================================================

@dataclass
class ChainOptimizationConfig:
    """Chain optimization configuration."""
    enable_reordering: bool = True
    enable_deduplication: bool = True
    enable_parallelization: bool = True
    max_parallel_filters: int = 4
    optimization_level: int = 2  # 0=none, 1=basic, 2=aggressive


# ============================================================================
# Chain Profiling
# ============================================================================

@dataclass
class ChainProfileConfig:
    """Chain profiling configuration."""
    enable_profiling: bool = False
    sample_rate: float = 0.1  # 10% sampling
    profile_duration: int = 60000  # milliseconds
    output_format: str = "json"  # "json", "csv", "binary"


@dataclass
class ChainProfileResult:
    """Chain profiling result."""
    total_executions: int = 0
    total_time_us: int = 0
    avg_time_us: float = 0.0
    min_time_us: int = 0
    max_time_us: int = 0
    filter_timings: Dict[str, float] = field(default_factory=dict)
    bottlenecks: List[str] = field(default_factory=list)
    recommendations: List[str] = field(default_factory=list)


# ============================================================================
# Chain Debugging
# ============================================================================

@dataclass
class ChainDebugConfig:
    """Chain debugging configuration."""
    trace_level: int = 0  # 0=off, 1=basic, 2=detailed
    log_format: str = "text"  # "text", "json", "binary"
    log_output: str = "console"  # "console", "file", "syslog"
    log_file: Optional[str] = None
    max_log_size: int = 10485760  # 10MB


@dataclass
class ChainValidationResult:
    """Chain validation result."""
    valid: bool = False
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    suggestions: List[str] = field(default_factory=list)


# ============================================================================
# Chain Events
# ============================================================================

@dataclass
class ChainEvent:
    """Chain event structure."""
    event_type: str
    timestamp: int
    chain_id: int
    filter_id: Optional[int] = None
    message: Optional[str] = None
    data: Optional[Dict[str, Any]] = None


# ============================================================================
# Chain Routing
# ============================================================================

@dataclass
class RouteRule:
    """Route rule structure."""
    condition: FilterMatchCallback
    target_chain: McpFilterChain
    priority: int = 0
    enabled: bool = True


@dataclass
class RouteTable:
    """Route table structure."""
    rules: List[RouteRule] = field(default_factory=list)
    default_chain: Optional[McpFilterChain] = None
    fallback_chain: Optional[McpFilterChain] = None


# ============================================================================
# Chain Load Balancing
# ============================================================================

@dataclass
class LoadBalancerConfig:
    """Load balancer configuration."""
    strategy: int = 0  # RoutingStrategy
    health_check_enabled: bool = True
    health_check_interval: int = 30000  # milliseconds
    health_check_timeout: int = 5000  # milliseconds
    max_failures: int = 3
    recovery_time: int = 60000  # milliseconds


@dataclass
class LoadBalancerStats:
    """Load balancer statistics."""
    total_requests: int = 0
    successful_requests: int = 0
    failed_requests: int = 0
    avg_response_time: float = 0.0
    active_connections: int = 0
    healthy_instances: int = 0
    unhealthy_instances: int = 0


# ============================================================================
# Chain Monitoring
# ============================================================================

@dataclass
class ChainMonitorConfig:
    """Chain monitoring configuration."""
    enable_monitoring: bool = True
    metrics_interval: int = 10000  # milliseconds
    alert_thresholds: Dict[str, float] = field(default_factory=dict)
    notification_channels: List[str] = field(default_factory=list)


@dataclass
class ChainAlert:
    """Chain alert structure."""
    alert_type: str
    severity: str  # "low", "medium", "high", "critical"
    message: str
    timestamp: int
    chain_id: int
    filter_id: Optional[int] = None
    metrics: Optional[Dict[str, Any]] = None


# ============================================================================
# Chain Utility Types
# ============================================================================

# Chain handle type
ChainHandle = McpFilterChain

# Filter handle type
FilterHandle = McpFilter

# Chain execution mode type
ChainExecutionModeType = ChainExecutionMode

# Routing strategy type
RoutingStrategyType = RoutingStrategy

# Match condition type
MatchConditionType = MatchCondition

# Chain state type
ChainStateType = ChainState

# Chain config type
ChainConfigType = ChainConfig

# Filter node type
FilterNodeType = FilterNode

# Filter condition type
FilterConditionType = FilterCondition

# Chain stats type
ChainStatsType = ChainStats

# Router config type
RouterConfigType = RouterConfig

# Chain pool config type
ChainPoolConfigType = ChainPoolConfig

# Chain optimization config type
ChainOptimizationConfigType = ChainOptimizationConfig

# Chain profile config type
ChainProfileConfigType = ChainProfileConfig

# Chain profile result type
ChainProfileResultType = ChainProfileResult

# Chain debug config type
ChainDebugConfigType = ChainDebugConfig

# Chain validation result type
ChainValidationResultType = ChainValidationResult

# Chain event type
ChainEventType = ChainEvent

# Route rule type
RouteRuleType = RouteRule

# Route table type
RouteTableType = RouteTable

# Load balancer config type
LoadBalancerConfigType = LoadBalancerConfig

# Load balancer stats type
LoadBalancerStatsType = LoadBalancerStats

# Chain monitor config type
ChainMonitorConfigType = ChainMonitorConfig

# Chain alert type
ChainAlertType = ChainAlert

# Chain callback types
ChainCallback = Callable[[ChainHandle, c_void_p], None]
ChainEventCallbackType = ChainEventCallback
ChainErrorCallback = Callable[[ChainHandle, int, str, c_void_p], None]
ChainStateCallback = Callable[[ChainHandle, ChainState, ChainState, c_void_p], None]
ChainProgressCallback = Callable[[ChainHandle, int, int, c_void_p], None]

# Chain operation callback types
ChainStartCallback = Callable[[ChainHandle, c_void_p], None]
ChainStopCallback = Callable[[ChainHandle, c_void_p], None]
ChainPauseCallback = Callable[[ChainHandle, c_void_p], None]
ChainResumeCallback = Callable[[ChainHandle, c_void_p], None]
ChainResetCallback = Callable[[ChainHandle, c_void_p], None]

# Chain routing callback types
ChainRouteCallback = Callable[[ChainHandle, McpBufferHandle, c_void_p], McpFilterChain]
ChainRouteErrorCallback = Callable[[ChainHandle, int, str, c_void_p], None]

# Chain load balancing callback types
ChainLoadBalanceCallback = Callable[[ChainHandle, c_void_p], McpFilterChain]
ChainHealthCheckCallback = Callable[[ChainHandle, c_void_p], bool]

# Chain monitoring callback types
ChainMetricsCallback = Callable[[ChainHandle, ChainStats, c_void_p], None]
ChainAlertCallback = Callable[[ChainHandle, ChainAlert, c_void_p], None]
ChainThresholdCallback = Callable[[ChainHandle, str, float, float, c_void_p], None]

# Chain optimization callback types
ChainOptimizeCallback = Callable[[ChainHandle, ChainOptimizationConfig, c_void_p], None]
ChainReorderCallback = Callable[[ChainHandle, List[McpFilter], c_void_p], None]

# Chain profiling callback types
ChainProfileCallback = Callable[[ChainHandle, ChainProfileResult, c_void_p], None]
ChainProfileStartCallback = Callable[[ChainHandle, ChainProfileConfig, c_void_p], None]
ChainProfileStopCallback = Callable[[ChainHandle, c_void_p], None]

# Chain debugging callback types
ChainDebugCallback = Callable[[ChainHandle, str, c_void_p], None]
ChainTraceCallback = Callable[[ChainHandle, str, c_void_p], None]
ChainLogCallback = Callable[[ChainHandle, str, c_void_p], None]

# Chain validation callback types
ChainValidateCallback = Callable[[ChainHandle, ChainValidationResult, c_void_p], None]
ChainValidateStartCallback = Callable[[ChainHandle, c_void_p], None]
ChainValidateCompleteCallback = Callable[[ChainHandle, ChainValidationResult, c_void_p], None]
