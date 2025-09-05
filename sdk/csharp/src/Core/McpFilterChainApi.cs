using System;
using System.Runtime.InteropServices;

namespace GopherMcp.Core
{
    /// <summary>
    /// P/Invoke bindings for MCP C Filter Chain API functions
    /// </summary>
    public static class McpFilterChainApi
    {
        private const string LibraryName = "gopher_mcp_c";
        
        // ============================================================================
        // Basic Chain Builder
        // ============================================================================
        
        /// <summary>
        /// Create filter chain builder
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_filter_chain_builder_create(ulong dispatcher);
        
        /// <summary>
        /// Add filter to chain builder
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_filter_chain_add_filter(
            IntPtr builder,
            ulong filter,
            McpFilterPosition position,
            ulong referenceFilter);
        
        /// <summary>
        /// Build filter chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_filter_chain_build(IntPtr builder);
        
        /// <summary>
        /// Destroy filter chain builder
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_chain_builder_destroy(IntPtr builder);
        
        /// <summary>
        /// Retain filter chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_chain_retain(ulong chain);
        
        /// <summary>
        /// Release filter chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_filter_chain_release(ulong chain);
        
        // ============================================================================
        // Advanced Chain Builder
        // ============================================================================
        
        /// <summary>
        /// Create chain builder with configuration
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_chain_builder_create_ex(
            ulong dispatcher,
            ref McpChainConfig config);
        
        /// <summary>
        /// Add filter node to chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_builder_add_node(
            IntPtr builder,
            ref McpFilterNode node);
        
        /// <summary>
        /// Add conditional filter
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_builder_add_conditional(
            IntPtr builder,
            ref McpFilterCondition condition,
            ulong filter);
        
        /// <summary>
        /// Add parallel filter group
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_builder_add_parallel_group(
            IntPtr builder,
            [In] ulong[] filters,
            UIntPtr count);
        
        /// <summary>
        /// Set custom routing function
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_builder_set_router(
            IntPtr builder,
            McpRoutingFunction router,
            IntPtr userData);
        
        /// <summary>
        /// Set chain execution mode
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_builder_set_execution_mode(
            IntPtr builder,
            ChainExecutionMode mode);
        
        // ============================================================================
        // Chain Management
        // ============================================================================
        
        /// <summary>
        /// Get chain state
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ChainState mcp_chain_get_state(ulong chain);
        
        /// <summary>
        /// Pause chain execution
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_pause(ulong chain);
        
        /// <summary>
        /// Resume chain execution
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_resume(ulong chain);
        
        /// <summary>
        /// Reset chain to initial state
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_reset(ulong chain);
        
        /// <summary>
        /// Enable/disable filter in chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_set_filter_enabled(
            ulong chain,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string filterName,
            byte enabled);
        
        /// <summary>
        /// Get chain statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_get_stats(
            ulong chain,
            out McpChainStats stats);
        
        /// <summary>
        /// Set chain event callback
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_set_event_callback(
            ulong chain,
            McpChainEventCallback callback,
            IntPtr userData);
        
        // ============================================================================
        // Chain Processing
        // ============================================================================
        
        /// <summary>
        /// Process buffer through chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_process(
            ulong chain,
            ulong buffer,
            byte endStream);
        
        /// <summary>
        /// Process buffer asynchronously through chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_process_async(
            ulong chain,
            ulong buffer,
            byte endStream,
            McpFilterCompletionCallback callback,
            IntPtr userData);
        
        // ============================================================================
        // Dynamic Chain Composition
        // ============================================================================
        
        /// <summary>
        /// Create dynamic chain from JSON configuration
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_chain_create_from_json(
            ulong dispatcher,
            IntPtr jsonConfig);
        
        /// <summary>
        /// Export chain configuration to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_chain_export_to_json(ulong chain);
        
        /// <summary>
        /// Clone a filter chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_chain_clone(ulong chain);
        
        /// <summary>
        /// Merge two chains
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_chain_merge(
            ulong chain1,
            ulong chain2,
            ChainExecutionMode mode);
        
        // ============================================================================
        // Chain Optimization
        // ============================================================================
        
        /// <summary>
        /// Optimize chain by removing redundant filters
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_optimize(ulong chain);
        
        /// <summary>
        /// Reorder filters for optimal performance
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_reorder_filters(ulong chain);
        
        /// <summary>
        /// Profile chain performance
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_profile(
            ulong chain,
            ulong testBuffer,
            UIntPtr iterations,
            out IntPtr report);
        
        // ============================================================================
        // Chain Debugging
        // ============================================================================
        
        /// <summary>
        /// Enable chain tracing
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_set_trace_level(
            ulong chain,
            uint traceLevel);
        
        /// <summary>
        /// Dump chain structure
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_chain_dump(
            ulong chain,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string format);
        
        /// <summary>
        /// Validate chain configuration
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_validate(
            ulong chain,
            out IntPtr errors);
        
        // ============================================================================
        // Chain Router
        // ============================================================================
        
        /// <summary>
        /// Create chain router
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_chain_router_create(ref McpRouterConfig config);
        
        /// <summary>
        /// Add route to router
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_router_add_route(
            IntPtr router,
            McpFilterMatchCallback condition,
            ulong chain);
        
        /// <summary>
        /// Route buffer through appropriate chain
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_chain_router_route(
            IntPtr router,
            ulong buffer,
            ref McpProtocolMetadata metadata);
        
        /// <summary>
        /// Destroy chain router
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_chain_router_destroy(IntPtr router);
        
        // ============================================================================
        // Chain Pool
        // ============================================================================
        
        /// <summary>
        /// Create chain pool for load balancing
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_chain_pool_create(
            ulong baseChain,
            UIntPtr poolSize,
            RoutingStrategy strategy);
        
        /// <summary>
        /// Get next chain from pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern ulong mcp_chain_pool_get_next(IntPtr pool);
        
        /// <summary>
        /// Return chain to pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_chain_pool_return(
            IntPtr pool,
            ulong chain);
        
        /// <summary>
        /// Get pool statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int mcp_chain_pool_get_stats(
            IntPtr pool,
            out UIntPtr active,
            out UIntPtr idle,
            out ulong totalProcessed);
        
        /// <summary>
        /// Destroy chain pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_chain_pool_destroy(IntPtr pool);
    }
    
    // ============================================================================
    // Enumerations
    // ============================================================================
    
    /// <summary>
    /// Chain execution mode
    /// </summary>
    public enum ChainExecutionMode
    {
        Sequential = 0,
        Parallel = 1,
        Conditional = 2,
        Pipeline = 3
    }
    
    /// <summary>
    /// Chain routing strategy
    /// </summary>
    public enum RoutingStrategy
    {
        RoundRobin = 0,
        LeastLoaded = 1,
        HashBased = 2,
        Priority = 3,
        Custom = 99
    }
    
    /// <summary>
    /// Filter match condition
    /// </summary>
    public enum MatchCondition
    {
        All = 0,
        Any = 1,
        None = 2,
        Custom = 99
    }
    
    /// <summary>
    /// Chain state
    /// </summary>
    public enum ChainState
    {
        Idle = 0,
        Processing = 1,
        Paused = 2,
        Error = 3,
        Completed = 4
    }
    
    // ============================================================================
    // Structures
    // ============================================================================
    
    /// <summary>
    /// Filter node in chain
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpFilterNode
    {
        public ulong Filter;
        
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string Name;
        
        public uint Priority;
        
        public byte Enabled;
        
        public byte BypassOnError;
        
        public IntPtr Config;  // mcp_json_value_t
    }
    
    /// <summary>
    /// Chain configuration
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpChainConfig
    {
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string Name;
        
        public ChainExecutionMode Mode;
        
        public RoutingStrategy Routing;
        
        public uint MaxParallel;
        
        public uint BufferSize;
        
        public uint TimeoutMs;
        
        public byte StopOnError;
    }
    
    /// <summary>
    /// Filter condition for conditional execution
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpFilterCondition
    {
        public MatchCondition MatchType;
        
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string Field;
        
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string Value;
        
        public ulong TargetFilter;
    }
    
    /// <summary>
    /// Chain statistics
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpChainStats
    {
        public ulong TotalProcessed;
        public ulong TotalErrors;
        public ulong TotalBypassed;
        public double AvgLatencyMs;
        public double MaxLatencyMs;
        public double ThroughputMbps;
        public uint ActiveFilters;
    }
    
    /// <summary>
    /// Router configuration
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct McpRouterConfig
    {
        public RoutingStrategy Strategy;
        
        public uint HashSeed;
        
        public ulong RouteTable;  // mcp_map_t
        
        public IntPtr CustomRouterData;
    }
    
    // ============================================================================
    // Callback Delegates
    // ============================================================================
    
    /// <summary>
    /// Custom routing function
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate ulong McpRoutingFunction(
        ulong buffer,
        IntPtr nodes,
        UIntPtr nodeCount,
        IntPtr userData);
    
    /// <summary>
    /// Chain event callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpChainEventCallback(
        ulong chain,
        ChainState oldState,
        ChainState newState,
        IntPtr userData);
    
    /// <summary>
    /// Filter match function
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate byte McpFilterMatchCallback(
        ulong buffer,
        ref McpProtocolMetadata metadata,
        IntPtr userData);
    
    /// <summary>
    /// Filter completion callback
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void McpFilterCompletionCallback(
        int result,
        IntPtr userData);
}