require 'mcp_filter_sdk/ffi_bindings'
require 'mcp_filter_sdk/c_structs'

module McpFilterSdk
  class FilterBuffer
    include FfiBindings

    def initialize(capacity = 4096)
      @capacity = capacity
      @buffer = create_buffer(capacity)
      @size = 0
    end

    def add_data(data)
      data_size = data.bytesize
      return false if @size + data_size > @capacity

      data_ptr = FFI::MemoryPointer.from_string(data)
      result = mcp_buffer_add(@buffer, data_ptr, data_size)
      
      if result == 0
        @size += data_size
        true
      else
        raise BufferOperationError.new(result, "Failed to add data to buffer")
      end
    end

    def get_contiguous_data
      size_ptr = FFI::MemoryPointer.new(:size_t)
      result = mcp_buffer_get_contiguous(@buffer, size_ptr)
      
      if result == 0
        size = size_ptr.read(:size_t)
        data_ptr = get_buffer_data_pointer
        data_ptr.read_string(size) if size > 0
      else
        raise BufferOperationError.new(result, "Failed to get contiguous data from buffer")
      end
    end

    def clear
      result = mcp_buffer_clear(@buffer)
      raise BufferOperationError.new(result, "Failed to clear buffer") if result != 0
      
      @size = 0
      true
    end

    def size
      mcp_buffer_get_size(@buffer)
    end

    def capacity
      mcp_buffer_get_capacity(@buffer)
    end

    def available_space
      capacity - size
    end

    def full?
      size >= capacity
    end

    def empty?
      size == 0
    end

    def destroy
      mcp_buffer_destroy(@buffer) if @buffer
      @buffer = nil
    end

    private

    def create_buffer(capacity)
      buffer_ptr = mcp_buffer_create_owned(capacity)
      raise BufferOperationError.new(-1, "Failed to create buffer") if buffer_ptr.null?
      buffer_ptr
    end

    def get_buffer_data_pointer
      # This would need to be implemented based on the C API structure
      # For now, return a placeholder
      FFI::Pointer.new(0)
    end
  end
end
