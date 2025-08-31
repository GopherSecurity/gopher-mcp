using System;
using System.Runtime.InteropServices;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpHandles;

namespace GopherMcp.Interop
{
    /// <summary>
    /// P/Invoke declarations for MCP Memory API functions (mcp_c_memory.h)
    /// </summary>
    public static class McpMemoryApi
    {
        // Library name - adjust based on platform naming conventions
#if WINDOWS
        private const string LibraryName = "mcp_c.dll";
#elif MACOS
        private const string LibraryName = "libmcp_c.dylib";
#else
        private const string LibraryName = "libmcp_c.so";
#endif

        /* ============================================================================
         * Error Handling Functions
         * ============================================================================ */

        /// <summary>
        /// Get last error information (thread-local)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_get_last_error();

        /// <summary>
        /// Clear last error for current thread
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_clear_last_error();

        /// <summary>
        /// Error handler delegate type
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void mcp_error_handler_t(IntPtr error, IntPtr user_data);

        /// <summary>
        /// Set custom error handler
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_set_error_handler(
            mcp_error_handler_t handler,
            IntPtr user_data);

        /* ============================================================================
         * Memory Pool Management Functions
         * ============================================================================ */

        /// <summary>
        /// Create a memory pool for batch operations
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpMemoryPoolHandle mcp_memory_pool_create(UIntPtr initial_size);

        /// <summary>
        /// Destroy memory pool and free all allocations
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_memory_pool_destroy(McpMemoryPoolHandle pool);

        /// <summary>
        /// Allocate memory from pool
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_memory_pool_alloc(
            McpMemoryPoolHandle pool,
            UIntPtr size);

        /// <summary>
        /// Reset pool (free all allocations but keep pool)
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_memory_pool_reset(McpMemoryPoolHandle pool);

        /// <summary>
        /// Get pool statistics
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_memory_pool_stats(
            McpMemoryPoolHandle pool,
            out UIntPtr used_bytes,
            out UIntPtr total_bytes,
            out UIntPtr allocation_count);

        /* ============================================================================
         * Batch Operations
         * ============================================================================ */

        /// <summary>
        /// Batch operation type enum
        /// </summary>
        public enum mcp_batch_op_type_t : int
        {
            MCP_BATCH_OP_CREATE = 0,
            MCP_BATCH_OP_FREE = 1,
            MCP_BATCH_OP_SET = 2,
            MCP_BATCH_OP_GET = 3
        }

        /// <summary>
        /// Batch operation structure
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct mcp_batch_operation_t
        {
            public mcp_batch_op_type_t type;
            public mcp_type_id_t target_type;
            public IntPtr target;
            public IntPtr param1;
            public IntPtr param2;
            public mcp_result_t result;
        }

        /// <summary>
        /// Execute batch operations atomically
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_batch_execute(
            [In] mcp_batch_operation_t[] operations,
            UIntPtr count);

        /* ============================================================================
         * Resource Tracking Functions (Debug Mode)
         * ============================================================================ */

#if MCP_DEBUG
        /// <summary>
        /// Enable resource tracking for leak detection
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_enable_resource_tracking(mcp_bool_t enable);

        /// <summary>
        /// Get current resource count by type
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_get_resource_count(mcp_type_id_t type);

        /// <summary>
        /// Print resource tracking report
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_print_resource_report();

        /// <summary>
        /// Check for resource leaks
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_check_leaks();
#endif

        /* ============================================================================
         * Memory Utility Functions
         * ============================================================================ */

        /// <summary>
        /// Duplicate a string using MCP allocator
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_strdup([MarshalAs(UnmanagedType.LPStr)] string str);

        /// <summary>
        /// Free a string allocated by MCP
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_string_free(IntPtr str);

        /// <summary>
        /// Allocate memory using MCP allocator
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_malloc(UIntPtr size);

        /// <summary>
        /// Reallocate memory using MCP allocator
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_realloc(IntPtr ptr, UIntPtr new_size);

        /// <summary>
        /// Free memory allocated by MCP
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_free(IntPtr ptr);

        /* ============================================================================
         * Helper Methods
         * ============================================================================ */

        /// <summary>
        /// Helper to get last error info as a managed structure
        /// </summary>
        public static mcp_error_info_t? GetLastError()
        {
            var ptr = mcp_get_last_error();
            if (ptr == IntPtr.Zero)
                return null;
            
            return Marshal.PtrToStructure<mcp_error_info_t>(ptr);
        }

        /// <summary>
        /// Helper to duplicate a string and return managed string
        /// </summary>
        public static string DuplicateString(string str)
        {
            if (str == null)
                return null;
                
            var ptr = mcp_strdup(str);
            if (ptr == IntPtr.Zero)
                return null;
                
            var result = Marshal.PtrToStringAnsi(ptr);
            mcp_string_free(ptr);
            return result;
        }

        /// <summary>
        /// Helper to allocate memory with size
        /// </summary>
        public static IntPtr Allocate(int size)
        {
            if (size <= 0)
                return IntPtr.Zero;
                
            return mcp_malloc(new UIntPtr((uint)size));
        }

        /// <summary>
        /// Helper to allocate memory with size (long)
        /// </summary>
        public static IntPtr Allocate(long size)
        {
            if (size <= 0)
                return IntPtr.Zero;
                
            return mcp_malloc(new UIntPtr((ulong)size));
        }

        /// <summary>
        /// Helper for managed error handler wrapper
        /// </summary>
        public class ErrorHandlerWrapper
        {
            private readonly Action<mcp_error_info_t?, object> _managedHandler;
            private readonly mcp_error_handler_t _nativeHandler;
            private readonly GCHandle _gcHandle;

            public ErrorHandlerWrapper(Action<mcp_error_info_t?, object> handler, object userData)
            {
                _managedHandler = handler;
                _gcHandle = GCHandle.Alloc(userData);
                _nativeHandler = NativeErrorHandler;
            }

            private void NativeErrorHandler(IntPtr error, IntPtr userData)
            {
                mcp_error_info_t? errorInfo = null;
                if (error != IntPtr.Zero)
                {
                    errorInfo = Marshal.PtrToStructure<mcp_error_info_t>(error);
                }

                var managedUserData = _gcHandle.Target;
                _managedHandler?.Invoke(errorInfo, managedUserData);
            }

            public mcp_error_handler_t GetNativeHandler()
            {
                return _nativeHandler;
            }

            public void Dispose()
            {
                if (_gcHandle.IsAllocated)
                {
                    _gcHandle.Free();
                }
            }
        }
    }
}