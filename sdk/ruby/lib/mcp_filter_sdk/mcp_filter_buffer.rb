require 'mcp_filter_sdk/mcp_ffi_bindings'
require 'mcp_filter_sdk/mcp_c_structs'

module McpFilterSdk
  class FilterBuffer
    attr_reader :capacity, :size, :handle

    def initialize(capacity = 1024)
      @capacity = capacity
      @size = 0
      @handle = nil
      @data = ''
      @initialized = false
    end

    def initialize!
      return if @initialized

      # Use mock implementation for now due to C++ library stability issues
      @handle = "buffer_#{object_id}"
      @initialized = true
      puts "✅ FilterBuffer initialized with capacity #{@capacity}"
    end

    def add(data)
      return false unless @initialized

      if data.nil?
        return true # Handle nil gracefully
      end

      data_str = data.to_s
      if @size + data_str.length > @capacity
        return false # Buffer would overflow
      end

      @data += data_str
      @size = @data.length
      true
    end

    def add_data(data)
      add(data)
    end

    def get_contiguous
      return nil unless @initialized
      return nil if @data.empty?

      @data
    end

    def get_data
      get_contiguous
    end

    def clear
      return unless @initialized

      @data = ''
      @size = 0
    end

    def get_size
      return 0 unless @initialized

      @size
    end

    def get_capacity
      return @capacity unless @initialized

      @capacity
    end

    # Alias methods for compatibility
    def size
      get_size
    end

    def capacity
      get_capacity
    end

    def is_empty?
      size == 0
    end

    def cleanup!
      return unless @initialized

      @data = ''
      @size = 0
      @handle = nil
      @initialized = false
      puts '✅ FilterBuffer cleaned up'
    end
  end
end
