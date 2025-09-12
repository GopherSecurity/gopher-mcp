require 'ffi'
require 'mcp_filter_sdk/mcp_c_structs'

module McpFilterSdk
  module FfiBindings
    extend FFI::Library

    # Library loading
    def self.load_library
      lib_path = find_library_path
      puts "Attempting to load library: #{lib_path}"
      ffi_lib lib_path
      puts 'Successfully loaded real C++ library'
    rescue LoadError => e
      puts "Failed to load real library: #{e.message}"
      # Fall back to mock implementation if real library is not available
      require 'mcp_filter_sdk/mock_ffi_bindings'
      McpFilterSdk::MockFfiBindings
    end

    def self.find_library_path
      # Try to find the C library in common locations
      possible_paths = [
        # Project build directory (most likely location)
        File.expand_path('../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib', __dir__),
        File.expand_path('../../../../build/src/c_api/libgopher_mcp_c.dylib', __dir__),
        File.expand_path('../../../../build/libgopher_mcp_c.0.1.0.dylib', __dir__),
        File.expand_path('../../../../build/libgopher_mcp_c.dylib', __dir__),
        # Relative to current directory
        File.join(File.dirname(__FILE__), '../../../build/libgopher_mcp_c.so'),
        File.join(File.dirname(__FILE__), '../../../build/libgopher_mcp_c.dylib'),
        # System installation paths (lower priority due to dependency issues)
        '/usr/local/lib/libgopher_mcp_c.so',
        '/usr/lib/libgopher_mcp_c.so',
        '/opt/homebrew/lib/libgopher_mcp_c.dylib',
        '/usr/local/lib/libgopher_mcp_c.dylib',
        # Windows paths
        'libgopher_mcp_c.dll'
      ]

      possible_paths.each do |path|
        if File.exist?(path)
          puts "Found library at: #{path}"
          return path
        end
      end

      # Fallback to library name for system search
      'gopher_mcp_c'
    end

    # Load the C library or mock
    @library = load_library

    # If we got a mock library, use its methods directly
    if @library.is_a?(Class) && @library.name == 'McpFilterSdk::MockFfiBindings'
      # Use mock methods directly - Updated to match real API
      def self.mcp_init(ptr)
        @library.mcp_init(ptr)
      end

      def self.mcp_shutdown
        @library.mcp_shutdown
      end

      def self.mcp_get_version
        @library.mcp_get_version
      end

      def self.mcp_is_initialized
        @library.mcp_is_initialized
      end

      def self.mcp_dispatcher_create
        @library.mcp_dispatcher_create
      end

      def self.mcp_dispatcher_destroy(ptr)
        @library.mcp_dispatcher_destroy(ptr)
      end

      def self.mcp_dispatcher_run(ptr)
        @library.mcp_dispatcher_run(ptr)
      end

      def self.mcp_dispatcher_stop(ptr)
        @library.mcp_dispatcher_stop(ptr)
      end

      def self.mcp_filter_create(dispatcher, config)
        @library.mcp_filter_create(dispatcher, config)
      end

      def self.mcp_filter_create_builtin(dispatcher, type, config)
        @library.mcp_filter_create_builtin(dispatcher, type, config)
      end

      def self.mcp_filter_retain(filter)
        @library.mcp_filter_retain(filter)
      end

      def self.mcp_filter_release(filter)
        @library.mcp_filter_release(filter)
      end

      def self.mcp_filter_set_callbacks(filter, callbacks)
        @library.mcp_filter_set_callbacks(filter, callbacks)
      end

      def self.mcp_filter_process_data(filter, buffer)
        @library.mcp_filter_process_data(filter, buffer)
      end

      def self.mcp_filter_process_write(filter, buffer)
        @library.mcp_filter_process_write(filter, buffer)
      end

      def self.mcp_buffer_create_owned(size, ownership)
        @library.mcp_buffer_create_owned(size, ownership)
      end

      def self.mcp_buffer_create_view(data, length)
        @library.mcp_buffer_create_view(data, length)
      end

      def self.mcp_buffer_destroy(buffer)
        @library.mcp_buffer_destroy(buffer)
      end

      def self.mcp_buffer_add(buffer, data, size)
        @library.mcp_buffer_add(buffer, data, size)
      end

      def self.mcp_buffer_get_contiguous(buffer, data, size)
        @library.mcp_buffer_get_contiguous(buffer, data, size)
      end

      def self.mcp_buffer_clear(buffer)
        @library.mcp_buffer_clear(buffer)
      end

      def self.mcp_buffer_get_size(buffer)
        @library.mcp_buffer_get_size(buffer)
      end

      def self.mcp_buffer_get_capacity(buffer)
        @library.mcp_buffer_get_capacity(buffer)
      end
    else
      # Use real FFI bindings - Minimal set of functions that actually exist
      # MCP Core Functions
      attach_function :mcp_init, [:pointer], :int
      attach_function :mcp_shutdown, [], :void
      attach_function :mcp_get_version, [], :string

      # Dispatcher Functions
      attach_function :mcp_dispatcher_create, [], :pointer
      attach_function :mcp_dispatcher_destroy, [:pointer], :void
      attach_function :mcp_dispatcher_run, [:pointer], :void
      attach_function :mcp_dispatcher_stop, [:pointer], :void

      # Buffer Functions
      attach_function :mcp_buffer_create, [:size_t], :pointer
      attach_function :mcp_buffer_create_owned, %i[size_t int], :pointer
      attach_function :mcp_buffer_create_view, %i[pointer size_t], :pointer
      attach_function :mcp_buffer_free, [:pointer], :void
      attach_function :mcp_buffer_length, [:pointer], :size_t
      attach_function :mcp_buffer_capacity, [:pointer], :size_t

      # Filter Functions
      attach_function :mcp_filter_create, %i[pointer pointer], :pointer
      attach_function :mcp_filter_create_builtin, %i[pointer int pointer], :pointer
      attach_function :mcp_filter_retain, [:pointer], :void
      attach_function :mcp_filter_release, [:pointer], :void
      attach_function :mcp_filter_set_callbacks, %i[pointer pointer], :int

      # Filter Chain Functions
      attach_function :mcp_filter_chain_builder_create, [:pointer], :pointer
      attach_function :mcp_filter_chain_add_filter, %i[pointer pointer], :int
      attach_function :mcp_filter_chain_build, [:pointer], :pointer
    end
  end
end
