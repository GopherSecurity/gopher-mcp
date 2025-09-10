require 'mcp_filter_sdk/ffi_bindings'
require 'mcp_filter_sdk/c_structs'
require 'mcp_filter_sdk/types'

module McpFilterSdk
  class FilterManager
    include FfiBindings

    def initialize(config = {})
      @config = config
      @handle = nil
      @filters = {}
      @initialized = false
    end

    def initialize!
      return if @initialized

      result = mcp_init(nil)
      raise FilterError.new(result, "Failed to initialize MCP") if result != 0

      @initialized = true
    end

    def create_filter(name, callbacks, config = {})
      initialize! unless @initialized

      filter_config = create_filter_config(name, config)
      c_callbacks = create_c_callbacks(callbacks)

      result = mcp_filter_create(@handle, c_callbacks, filter_config)
      raise FilterCreationError.new(result, "Failed to create filter: #{name}") if result != 0

      filter_handle = get_last_created_filter
      filter = CApiFilter.new(name, callbacks, filter_handle)
      @filters[name] = filter

      filter
    end

    def destroy_filter(name)
      filter = @filters[name]
      return false unless filter

      result = mcp_filter_destroy(filter.handle)
      raise FilterError.new(result, "Failed to destroy filter: #{name}") if result != 0

      @filters.delete(name)
      true
    end

    def process_data(data, filter_name = nil)
      if filter_name
        filter = @filters[filter_name]
        raise FilterError.new(-1, "Filter not found: #{filter_name}") unless filter

        process_with_filter(data, filter)
      else
        process_with_all_filters(data)
      end
    end

    def get_filter_status(name)
      filter = @filters[name]
      return FilterStatus::ERROR unless filter

      status_code = mcp_filter_get_status(filter.handle)
      case status_code
      when 0 then FilterStatus::PENDING
      when 1 then FilterStatus::PROCESSING
      when 2 then FilterStatus::COMPLETED
      when -1 then FilterStatus::ERROR
      else FilterStatus::ERROR
      end
    end

    def list_filters
      @filters.keys
    end

    def cleanup!
      @filters.each_value(&:destroy)
      @filters.clear
      mcp_cleanup if @initialized
      @initialized = false
    end

    private

    def create_filter_config(name, config)
      filter_config = CStructs::McpFilterConfig.new
      filter_config[:name] = name
      filter_config[:type] = config[:type] || FilterType::DATA
      filter_config[:priority] = config[:priority] || FilterPriority::NORMAL
      filter_config[:enabled] = config[:enabled] != false
      filter_config[:config_data] = FFI::MemoryPointer.from_string(config[:config_data].to_json)
      filter_config
    end

    def create_c_callbacks(callbacks)
      c_callbacks = CStructs::McpFilterCallbacks.new
      c_callbacks[:on_data] = create_callback(:on_data, callbacks[:on_data])
      c_callbacks[:on_write] = create_callback(:on_write, callbacks[:on_write])
      c_callbacks[:on_new_connection] = create_callback(:on_new_connection, callbacks[:on_new_connection])
      c_callbacks[:on_error] = create_callback(:on_error, callbacks[:on_error])
      c_callbacks[:on_high_watermark] = create_callback(:on_high_watermark, callbacks[:on_high_watermark])
      c_callbacks[:on_low_watermark] = create_callback(:on_low_watermark, callbacks[:on_low_watermark])
      c_callbacks[:user_data] = nil
      c_callbacks
    end

    def create_callback(type, ruby_callback)
      return nil unless ruby_callback

      # Create a C callback that calls the Ruby method
      FFI::Function.new(:int, [:pointer, :size_t]) do |data_ptr, size|
        begin
          data = data_ptr.read_string(size)
          result = ruby_callback.call(data)
          result ? 1 : 0
        rescue => e
          puts "Error in #{type} callback: #{e.message}"
          0
        end
      end
    end

    def process_with_filter(data, filter)
      data_ptr = FFI::MemoryPointer.from_string(data)
      result = mcp_filter_process(filter.handle, data_ptr, data.bytesize)
      raise FilterError.new(result, "Failed to process data with filter: #{filter.name}") if result != 0
      true
    end

    def process_with_all_filters(data)
      @filters.each_value do |filter|
        process_with_filter(data, filter)
      end
      true
    end

    def get_last_created_filter
      # This would need to be implemented based on the C API
      # For now, return a placeholder
      nil
    end
  end

  class CApiFilter
    attr_reader :name, :handle, :callbacks

    def initialize(name, callbacks, handle = nil)
      @name = name
      @callbacks = callbacks
      @handle = handle
    end

    def process_data(data)
      # Process data through this specific filter
      # Implementation would depend on the C API
      true
    end

    def destroy
      # Clean up filter resources
      @handle = nil
    end
  end
end
