require 'ffi'
require 'mcp_filter_sdk/c_structs'

module McpFilterSdk
  module FfiBindings
    extend FFI::Library

    # Library loading
    def self.load_library
      lib_path = find_library_path
      ffi_lib lib_path
    end

    # Load the C library
    load_library

    # MCP Core Functions
    attach_function :mcp_init, [:pointer], :int
    attach_function :mcp_cleanup, [], :void
    attach_function :mcp_get_version, [], :string

    # Filter Management Functions
    attach_function :mcp_filter_manager_create, [:pointer], :int
    attach_function :mcp_filter_manager_destroy, [:pointer], :void
    attach_function :mcp_filter_create, [:pointer, :pointer, :pointer], :int
    attach_function :mcp_filter_destroy, [:pointer], :void
    attach_function :mcp_filter_process, [:pointer, :pointer, :size_t], :int
    attach_function :mcp_filter_get_status, [:pointer], :int

    # Buffer Functions
    attach_function :mcp_buffer_create, [:size_t], :pointer
    attach_function :mcp_buffer_create_owned, [:size_t], :pointer
    attach_function :mcp_buffer_destroy, [:pointer], :void
    attach_function :mcp_buffer_add, [:pointer, :pointer, :size_t], :int
    attach_function :mcp_buffer_get_contiguous, [:pointer, :pointer], :int
    attach_function :mcp_buffer_clear, [:pointer], :void
    attach_function :mcp_buffer_get_size, [:pointer], :size_t
    attach_function :mcp_buffer_get_capacity, [:pointer], :size_t

    # Chain Functions
    attach_function :mcp_chain_create, [:pointer], :int
    attach_function :mcp_chain_destroy, [:pointer], :void
    attach_function :mcp_chain_add_filter, [:pointer, :pointer], :int
    attach_function :mcp_chain_remove_filter, [:pointer, :pointer], :int
    attach_function :mcp_chain_execute, [:pointer, :pointer, :size_t], :int

    # Transport Functions
    attach_function :mcp_transport_create, [:pointer], :int
    attach_function :mcp_transport_destroy, [:pointer], :void
    attach_function :mcp_transport_start, [:pointer], :int
    attach_function :mcp_transport_stop, [:pointer], :void
    attach_function :mcp_transport_send, [:pointer, :pointer, :size_t], :int
    attach_function :mcp_transport_receive, [:pointer, :pointer, :size_t], :int

    # Connection Functions
    attach_function :mcp_connection_create, [:pointer], :int
    attach_function :mcp_connection_destroy, [:pointer], :void
    attach_function :mcp_connection_connect, [:pointer], :int
    attach_function :mcp_connection_disconnect, [:pointer], :void
    attach_function :mcp_connection_send, [:pointer, :pointer, :size_t], :int
    attach_function :mcp_connection_receive, [:pointer, :pointer, :size_t], :int

    # Error Functions
    attach_function :mcp_get_last_error, [], :pointer
    attach_function :mcp_error_get_code, [:pointer], :int
    attach_function :mcp_error_get_message, [:pointer], :string

    # Callback Types
    typedef :pointer, :on_data_callback
    typedef :pointer, :on_write_callback
    typedef :pointer, :on_new_connection_callback
    typedef :pointer, :on_error_callback
    typedef :pointer, :on_high_watermark_callback
    typedef :pointer, :on_low_watermark_callback

    private

    def self.find_library_path
      # Search for the C library in the build directory
      possible_paths = [
        File.join(__dir__, '../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib'),
        File.join(__dir__, '../../../../build/src/c_api/libgopher_mcp_c.0.1.0.so'),
        File.join(__dir__, '../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dll'),
        File.join(__dir__, '../../../../build/src/c_api/libgopher_mcp_c.dylib'),
        File.join(__dir__, '../../../../build/src/c_api/libgopher_mcp_c.so'),
        File.join(__dir__, '../../../../build/src/c_api/libgopher_mcp_c.dll')
      ]

      found_path = possible_paths.find { |path| File.exist?(path) }
      
      unless found_path
        raise LibraryLoadError.new(
          -1,
          "C library not found. Searched paths: #{possible_paths.join(', ')}"
        )
      end

      found_path
    end
  end
end
