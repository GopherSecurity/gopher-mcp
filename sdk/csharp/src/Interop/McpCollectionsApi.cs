using System;
using System.Runtime.InteropServices;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpHandles;

namespace GopherMcp.Interop
{
    /// <summary>
    /// P/Invoke declarations for MCP Collections API functions (mcp_c_collections.h)
    /// </summary>
    public static class McpCollectionsApi
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
         * List Functions
         * ============================================================================ */

        /// <summary>
        /// Creates a new list for the specified element type
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpListHandle mcp_list_create(mcp_type_id_t element_type);

        /// <summary>
        /// Creates a new list with initial capacity
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpListHandle mcp_list_create_with_capacity(
            mcp_type_id_t element_type,
            UIntPtr capacity);

        /// <summary>
        /// Frees a list and its resources
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_list_free(McpListHandle list);

        /// <summary>
        /// Appends an item to the list
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_list_append(McpListHandle list, IntPtr item);

        /// <summary>
        /// Inserts an item at the specified index
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_list_insert(
            McpListHandle list,
            UIntPtr index,
            IntPtr item);

        /// <summary>
        /// Gets an item at the specified index
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_list_get(McpListHandle list, UIntPtr index);

        /// <summary>
        /// Sets an item at the specified index
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_list_set(
            McpListHandle list,
            UIntPtr index,
            IntPtr item);

        /// <summary>
        /// Removes an item at the specified index
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_list_remove(McpListHandle list, UIntPtr index);

        /// <summary>
        /// Gets the size of the list
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_list_size(McpListHandle list);

        /// <summary>
        /// Gets the capacity of the list
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_list_capacity(McpListHandle list);

        /// <summary>
        /// Clears all items from the list
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_list_clear(McpListHandle list);

        /// <summary>
        /// Checks if the list is valid
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_list_is_valid(McpListHandle list);

        /// <summary>
        /// Gets the element type of the list
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_type_id_t mcp_list_element_type(McpListHandle list);

        /* ============================================================================
         * List Iterator Functions
         * ============================================================================ */

        /// <summary>
        /// Creates an iterator for the list
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpListIteratorHandle mcp_list_iterator_create(McpListHandle list);

        /// <summary>
        /// Frees a list iterator
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_list_iterator_free(McpListIteratorHandle iter);

        /// <summary>
        /// Checks if the iterator has more elements
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_list_iterator_has_next(McpListIteratorHandle iter);

        /// <summary>
        /// Gets the next element from the iterator
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_list_iterator_next(McpListIteratorHandle iter);

        /// <summary>
        /// Resets the iterator to the beginning
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_list_iterator_reset(McpListIteratorHandle iter);

        /* ============================================================================
         * Map Functions
         * ============================================================================ */

        /// <summary>
        /// Creates a new map for the specified value type
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpMapHandle mcp_map_create(mcp_type_id_t value_type);

        /// <summary>
        /// Creates a new map with initial capacity
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpMapHandle mcp_map_create_with_capacity(
            mcp_type_id_t value_type,
            UIntPtr capacity);

        /// <summary>
        /// Frees a map and its resources
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_map_free(McpMapHandle map);

        /// <summary>
        /// Sets a value for the specified key
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_map_set(
            McpMapHandle map,
            [MarshalAs(UnmanagedType.LPStr)] string key,
            IntPtr value);

        /// <summary>
        /// Gets a value for the specified key
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_map_get(
            McpMapHandle map,
            [MarshalAs(UnmanagedType.LPStr)] string key);

        /// <summary>
        /// Checks if the map contains the specified key
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_map_has(
            McpMapHandle map,
            [MarshalAs(UnmanagedType.LPStr)] string key);

        /// <summary>
        /// Removes a value for the specified key
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_map_remove(
            McpMapHandle map,
            [MarshalAs(UnmanagedType.LPStr)] string key);

        /// <summary>
        /// Gets the size of the map
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_map_size(McpMapHandle map);

        /// <summary>
        /// Clears all items from the map
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_map_clear(McpMapHandle map);

        /// <summary>
        /// Checks if the map is valid
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_map_is_valid(McpMapHandle map);

        /// <summary>
        /// Gets the value type of the map
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_type_id_t mcp_map_value_type(McpMapHandle map);

        /* ============================================================================
         * Map Iterator Functions
         * ============================================================================ */

        /// <summary>
        /// Creates an iterator for the map
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpMapIteratorHandle mcp_map_iterator_create(McpMapHandle map);

        /// <summary>
        /// Frees a map iterator
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_map_iterator_free(McpMapIteratorHandle iter);

        /// <summary>
        /// Checks if the iterator has more elements
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_map_iterator_has_next(McpMapIteratorHandle iter);

        /// <summary>
        /// Gets the next key from the iterator
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_map_iterator_next_key(McpMapIteratorHandle iter);

        /// <summary>
        /// Gets the next value from the iterator
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_map_iterator_next_value(McpMapIteratorHandle iter);

        /// <summary>
        /// Resets the iterator to the beginning
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_map_iterator_reset(McpMapIteratorHandle iter);

        /* ============================================================================
         * JSON Value Functions
         * ============================================================================ */

        /// <summary>
        /// Creates a JSON null value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonHandle mcp_json_create_null();

        /// <summary>
        /// Creates a JSON boolean value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonHandle mcp_json_create_bool(mcp_bool_t value);

        /// <summary>
        /// Creates a JSON number value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonHandle mcp_json_create_number(double value);

        /// <summary>
        /// Creates a JSON string value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonHandle mcp_json_create_string(
            [MarshalAs(UnmanagedType.LPStr)] string value);

        /// <summary>
        /// Creates a JSON array
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonHandle mcp_json_create_array();

        /// <summary>
        /// Creates a JSON object
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonHandle mcp_json_create_object();

        /// <summary>
        /// Frees a JSON value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_json_free(McpJsonHandle json);

        /// <summary>
        /// Gets the type of a JSON value
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_json_type_t mcp_json_get_type(McpJsonHandle json);

        /// <summary>
        /// Gets the boolean value from a JSON boolean
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_json_get_bool(McpJsonHandle json);

        /// <summary>
        /// Gets the number value from a JSON number
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern double mcp_json_get_number(McpJsonHandle json);

        /// <summary>
        /// Gets the string value from a JSON string
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr mcp_json_get_string(McpJsonHandle json);

        /// <summary>
        /// Gets the size of a JSON array
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern UIntPtr mcp_json_array_size(McpJsonHandle json);

        /// <summary>
        /// Gets an element from a JSON array
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonHandle mcp_json_array_get(McpJsonHandle json, UIntPtr index);

        /// <summary>
        /// Appends a value to a JSON array
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_json_array_append(
            McpJsonHandle json,
            McpJsonHandle value);

        /// <summary>
        /// Sets a value in a JSON object
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_json_object_set(
            McpJsonHandle json,
            [MarshalAs(UnmanagedType.LPStr)] string key,
            McpJsonHandle value);

        /// <summary>
        /// Gets a value from a JSON object
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonHandle mcp_json_object_get(
            McpJsonHandle json,
            [MarshalAs(UnmanagedType.LPStr)] string key);

        /// <summary>
        /// Checks if a JSON object has a key
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_bool_t mcp_json_object_has(
            McpJsonHandle json,
            [MarshalAs(UnmanagedType.LPStr)] string key);

        /* ============================================================================
         * Metadata Functions
         * ============================================================================ */

        /// <summary>
        /// Creates a new metadata object
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpMetadataHandle mcp_metadata_create();

        /// <summary>
        /// Frees a metadata object
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void mcp_metadata_free(McpMetadataHandle metadata);

        /// <summary>
        /// Populates metadata from JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern mcp_result_t mcp_metadata_from_json(
            McpMetadataHandle metadata,
            McpJsonHandle json);

        /// <summary>
        /// Converts metadata to JSON
        /// </summary>
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
        public static extern McpJsonHandle mcp_metadata_to_json(McpMetadataHandle metadata);

        /* ============================================================================
         * Helper Methods
         * ============================================================================ */

        /// <summary>
        /// Helper to convert key pointer to managed string
        /// </summary>
        public static string GetMapIteratorKey(McpMapIteratorHandle iter)
        {
            var ptr = mcp_map_iterator_next_key(iter);
            return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
        }

        /// <summary>
        /// Helper to convert JSON string pointer to managed string
        /// </summary>
        public static string GetJsonString(McpJsonHandle json)
        {
            var ptr = mcp_json_get_string(json);
            return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
        }
    }
}