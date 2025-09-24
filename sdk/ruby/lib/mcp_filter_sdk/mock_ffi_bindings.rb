# frozen_string_literal: true

require 'ffi'

module McpFilterSdk
  module MockFfiBindings
    extend FFI::Library

    # Mock implementation for testing when C library is not available
    class MockLibrary
      def self.mcp_init(_ptr)
        0 # Success
      end

      def self.mcp_shutdown
        # Mock shutdown
      end

      def self.mcp_get_version
        '0.1.0-mock'
      end

      def self.mcp_is_initialized
        true
      end

      def self.mcp_dispatcher_create
        FFI::Pointer.new(0x12345678) # Mock pointer
      end

      def self.mcp_dispatcher_destroy(ptr)
        # Mock destroy
      end

      def self.mcp_dispatcher_run(ptr)
        # Mock run
      end

      def self.mcp_dispatcher_stop(ptr)
        # Mock stop
      end

      def self.mcp_filter_create(_dispatcher, _config)
        FFI::Pointer.new(0x87654321) # Mock filter pointer
      end

      def self.mcp_filter_create_builtin(_dispatcher, _type, _config)
        FFI::Pointer.new(0x87654322) # Mock builtin filter pointer
      end

      def self.mcp_filter_retain(filter)
        # Mock retain
      end

      def self.mcp_filter_release(filter)
        # Mock release
      end

      def self.mcp_filter_set_callbacks(_filter, _callbacks)
        0 # Success
      end

      def self.mcp_filter_process_data(_filter, _buffer)
        0 # Success
      end

      def self.mcp_filter_process_write(_filter, _buffer)
        0 # Success
      end

      def self.mcp_buffer_create_owned(_size, _ownership)
        FFI::Pointer.new(0x11111111) # Mock buffer pointer
      end

      def self.mcp_buffer_create_view(_data, _length)
        FFI::Pointer.new(0x22222222) # Mock view pointer
      end

      def self.mcp_buffer_destroy(buffer)
        # Mock destroy
      end

      def self.mcp_buffer_add(_buffer, _data, _size)
        0 # Success
      end

      def self.mcp_buffer_get_contiguous(_buffer, _data, _size)
        0 # Success
      end

      def self.mcp_buffer_clear(buffer)
        # Mock clear
      end

      def self.mcp_buffer_get_size(_buffer)
        1024 # Mock size
      end

      def self.mcp_buffer_get_capacity(_buffer)
        1024 # Mock capacity
      end

      def self.mcp_filter_chain_remove_filter(_chain, _filter)
        0 # Success
      end

      def self.mcp_filter_chain_process(_chain, _data, _size)
        0 # Success
      end

      def self.mcp_buffer_create(_size, _buffer)
        0 # Success
      end

      def self.mcp_buffer_destroy(buffer)
        # Mock destroy
      end

      def self.mcp_buffer_add(_buffer, _data, _size)
        0 # Success
      end

      def self.mcp_buffer_get_contiguous(_buffer, _data, _size)
        0 # Success
      end

      def self.mcp_buffer_get_size(_buffer)
        1024 # Mock size
      end

      def self.mcp_transport_create(_config, _transport)
        0 # Success
      end

      def self.mcp_transport_destroy(transport)
        # Mock destroy
      end

      def self.mcp_transport_start(_transport)
        0 # Success
      end

      def self.mcp_transport_stop(_transport)
        0 # Success
      end

      def self.mcp_transport_send(_transport, _data, _size)
        0 # Success
      end

      def self.mcp_transport_receive(_transport, _data, _size)
        0 # Success
      end
    end

    # Use mock library for testing
    def self.load_library
      # Return mock library instead of loading real C library
      MockLibrary
    end

    # Mock function attachments
    def self.attach_function(name, params, return_type)
      # Mock function attachment - do nothing
    end

    # Load the mock library
    @library = load_library

    # Mock function definitions
    def self.mcp_init(ptr)
      @library.mcp_init(ptr)
    end

    def self.mcp_cleanup
      @library.mcp_cleanup
    end

    def self.mcp_get_version
      @library.mcp_get_version
    end

    def self.mcp_filter_manager_create(ptr)
      @library.mcp_filter_manager_create(ptr)
    end

    def self.mcp_filter_manager_destroy(ptr)
      @library.mcp_filter_manager_destroy(ptr)
    end

    def self.mcp_filter_create(manager, callbacks, filter)
      @library.mcp_filter_create(manager, callbacks, filter)
    end

    def self.mcp_filter_destroy(filter)
      @library.mcp_filter_destroy(filter)
    end

    def self.mcp_filter_process(filter, data, size)
      @library.mcp_filter_process(filter, data, size)
    end

    def self.mcp_filter_chain_create(manager, config, chain)
      @library.mcp_filter_chain_create(manager, config, chain)
    end

    def self.mcp_filter_chain_destroy(chain)
      @library.mcp_filter_chain_destroy(chain)
    end

    def self.mcp_filter_chain_add_filter(chain, filter)
      @library.mcp_filter_chain_add_filter(chain, filter)
    end

    def self.mcp_filter_chain_remove_filter(chain, filter)
      @library.mcp_filter_chain_remove_filter(chain, filter)
    end

    def self.mcp_filter_chain_process(chain, data, size)
      @library.mcp_filter_chain_process(chain, data, size)
    end

    def self.mcp_buffer_create(size, buffer)
      @library.mcp_buffer_create(size, buffer)
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

    def self.mcp_buffer_get_size(buffer)
      @library.mcp_buffer_get_size(buffer)
    end

    def self.mcp_transport_create(config, transport)
      @library.mcp_transport_create(config, transport)
    end

    def self.mcp_transport_destroy(transport)
      @library.mcp_transport_destroy(transport)
    end

    def self.mcp_transport_start(transport)
      @library.mcp_transport_start(transport)
    end

    def self.mcp_transport_stop(transport)
      @library.mcp_transport_stop(transport)
    end

    def self.mcp_transport_send(transport, data, size)
      @library.mcp_transport_send(transport, data, size)
    end

    def self.mcp_transport_receive(transport, data, size)
      @library.mcp_transport_receive(transport, data, size)
    end
  end
end
