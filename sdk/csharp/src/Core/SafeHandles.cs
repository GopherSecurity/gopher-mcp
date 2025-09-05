using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace GopherMcp.Core
{
    /// <summary>
    /// SafeHandle for MCP Filter handle
    /// </summary>
    public sealed class McpFilterHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Initializes a new instance of the McpFilterHandle class
        /// </summary>
        public McpFilterHandle() : base(true)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the McpFilterHandle class with a pre-existing handle
        /// </summary>
        public McpFilterHandle(IntPtr preexistingHandle, bool ownsHandle = true) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }
        
        /// <summary>
        /// Create from ulong handle value
        /// </summary>
        public static McpFilterHandle FromULong(ulong handle)
        {
            return new McpFilterHandle(new IntPtr((long)handle), true);
        }
        
        /// <summary>
        /// Get handle value as ulong
        /// </summary>
        public ulong ToULong()
        {
            return (ulong)handle.ToInt64();
        }
        
        /// <summary>
        /// Release the filter handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void mcp_filter_release(ulong filter);
        
        /// <summary>
        /// When overridden in a derived class, executes the code required to free the handle
        /// </summary>
        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                mcp_filter_release(ToULong());
            }
            return true;
        }
    }
    
    /// <summary>
    /// SafeHandle for MCP Filter Chain handle
    /// </summary>
    public sealed class McpChainHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Initializes a new instance of the McpChainHandle class
        /// </summary>
        public McpChainHandle() : base(true)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the McpChainHandle class with a pre-existing handle
        /// </summary>
        public McpChainHandle(IntPtr preexistingHandle, bool ownsHandle = true) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }
        
        /// <summary>
        /// Create from ulong handle value
        /// </summary>
        public static McpChainHandle FromULong(ulong handle)
        {
            return new McpChainHandle(new IntPtr((long)handle), true);
        }
        
        /// <summary>
        /// Get handle value as ulong
        /// </summary>
        public ulong ToULong()
        {
            return (ulong)handle.ToInt64();
        }
        
        /// <summary>
        /// Release the filter chain handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void mcp_filter_chain_release(ulong chain);
        
        /// <summary>
        /// When overridden in a derived class, executes the code required to free the handle
        /// </summary>
        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                mcp_filter_chain_release(ToULong());
            }
            return true;
        }
    }
    
    /// <summary>
    /// SafeHandle for MCP Buffer handle
    /// </summary>
    public sealed class McpBufferHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Initializes a new instance of the McpBufferHandle class
        /// </summary>
        public McpBufferHandle() : base(true)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the McpBufferHandle class with a pre-existing handle
        /// </summary>
        public McpBufferHandle(IntPtr preexistingHandle, bool ownsHandle = true) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }
        
        /// <summary>
        /// Create from ulong handle value
        /// </summary>
        public static McpBufferHandle FromULong(ulong handle)
        {
            return new McpBufferHandle(new IntPtr((long)handle), true);
        }
        
        /// <summary>
        /// Get handle value as ulong
        /// </summary>
        public ulong ToULong()
        {
            return (ulong)handle.ToInt64();
        }
        
        /// <summary>
        /// Release the buffer handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void mcp_filter_buffer_release(ulong buffer);
        
        /// <summary>
        /// When overridden in a derived class, executes the code required to free the handle
        /// </summary>
        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                mcp_filter_buffer_release(ToULong());
            }
            return true;
        }
    }
    
    /// <summary>
    /// SafeHandle for MCP Filter Manager handle
    /// </summary>
    public sealed class McpManagerHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Initializes a new instance of the McpManagerHandle class
        /// </summary>
        public McpManagerHandle() : base(true)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the McpManagerHandle class with a pre-existing handle
        /// </summary>
        public McpManagerHandle(IntPtr preexistingHandle, bool ownsHandle = true) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }
        
        /// <summary>
        /// Create from ulong handle value
        /// </summary>
        public static McpManagerHandle FromULong(ulong handle)
        {
            return new McpManagerHandle(new IntPtr((long)handle), true);
        }
        
        /// <summary>
        /// Get handle value as ulong
        /// </summary>
        public ulong ToULong()
        {
            return (ulong)handle.ToInt64();
        }
        
        /// <summary>
        /// Release the filter manager handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void mcp_filter_manager_release(ulong manager);
        
        /// <summary>
        /// When overridden in a derived class, executes the code required to free the handle
        /// </summary>
        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                mcp_filter_manager_release(ToULong());
            }
            return true;
        }
    }
    
    /// <summary>
    /// SafeHandle for MCP Dispatcher handle
    /// </summary>
    public sealed class McpDispatcherHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Initializes a new instance of the McpDispatcherHandle class
        /// </summary>
        public McpDispatcherHandle() : base(true)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the McpDispatcherHandle class with a pre-existing handle
        /// </summary>
        public McpDispatcherHandle(IntPtr preexistingHandle, bool ownsHandle = true) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }
        
        /// <summary>
        /// Create from ulong handle value
        /// </summary>
        public static McpDispatcherHandle FromULong(ulong handle)
        {
            return new McpDispatcherHandle(new IntPtr((long)handle), true);
        }
        
        /// <summary>
        /// Get handle value as ulong
        /// </summary>
        public ulong ToULong()
        {
            return (ulong)handle.ToInt64();
        }
        
        /// <summary>
        /// Release the dispatcher handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void mcp_dispatcher_release(ulong dispatcher);
        
        /// <summary>
        /// When overridden in a derived class, executes the code required to free the handle
        /// </summary>
        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                mcp_dispatcher_release(ToULong());
            }
            return true;
        }
    }
    
    /// <summary>
    /// SafeHandle for MCP Buffer Pool handle
    /// </summary>
    public sealed class McpBufferPoolHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Initializes a new instance of the McpBufferPoolHandle class
        /// </summary>
        public McpBufferPoolHandle() : base(true)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the McpBufferPoolHandle class with a pre-existing handle
        /// </summary>
        public McpBufferPoolHandle(IntPtr preexistingHandle, bool ownsHandle = true) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }
        
        /// <summary>
        /// Destroy the buffer pool handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void mcp_buffer_pool_destroy(IntPtr pool);
        
        /// <summary>
        /// When overridden in a derived class, executes the code required to free the handle
        /// </summary>
        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                mcp_buffer_pool_destroy(handle);
            }
            return true;
        }
    }
    
    /// <summary>
    /// SafeHandle for MCP Chain Router handle
    /// </summary>
    public sealed class McpChainRouterHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Initializes a new instance of the McpChainRouterHandle class
        /// </summary>
        public McpChainRouterHandle() : base(true)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the McpChainRouterHandle class with a pre-existing handle
        /// </summary>
        public McpChainRouterHandle(IntPtr preexistingHandle, bool ownsHandle = true) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }
        
        /// <summary>
        /// Destroy the chain router handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void mcp_chain_router_destroy(IntPtr router);
        
        /// <summary>
        /// When overridden in a derived class, executes the code required to free the handle
        /// </summary>
        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                mcp_chain_router_destroy(handle);
            }
            return true;
        }
    }
    
    /// <summary>
    /// SafeHandle for MCP Chain Pool handle
    /// </summary>
    public sealed class McpChainPoolHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Initializes a new instance of the McpChainPoolHandle class
        /// </summary>
        public McpChainPoolHandle() : base(true)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the McpChainPoolHandle class with a pre-existing handle
        /// </summary>
        public McpChainPoolHandle(IntPtr preexistingHandle, bool ownsHandle = true) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }
        
        /// <summary>
        /// Destroy the chain pool handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void mcp_chain_pool_destroy(IntPtr pool);
        
        /// <summary>
        /// When overridden in a derived class, executes the code required to free the handle
        /// </summary>
        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                mcp_chain_pool_destroy(handle);
            }
            return true;
        }
    }
    
    /// <summary>
    /// SafeHandle for MCP Chain Builder handle
    /// </summary>
    public sealed class McpChainBuilderHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        private const string LibraryName = "gopher_mcp_c";
        
        /// <summary>
        /// Initializes a new instance of the McpChainBuilderHandle class
        /// </summary>
        public McpChainBuilderHandle() : base(true)
        {
        }
        
        /// <summary>
        /// Initializes a new instance of the McpChainBuilderHandle class with a pre-existing handle
        /// </summary>
        public McpChainBuilderHandle(IntPtr preexistingHandle, bool ownsHandle = true) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }
        
        /// <summary>
        /// Destroy the filter chain builder handle
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void mcp_filter_chain_builder_destroy(IntPtr builder);
        
        /// <summary>
        /// When overridden in a derived class, executes the code required to free the handle
        /// </summary>
        protected override bool ReleaseHandle()
        {
            if (!IsInvalid)
            {
                mcp_filter_chain_builder_destroy(handle);
            }
            return true;
        }
    }
}