# frozen_string_literal: true

require 'mcp_filter_sdk/mcp_ffi_bindings'
require 'mcp_filter_sdk/mcp_c_structs'
require 'mcp_filter_sdk/types/index'

module McpFilterSdk
  class CApiFilter
    include FfiBindings

    attr_reader :name, :callbacks, :handle, :status

    def initialize(name, callbacks, options = {})
      raise ArgumentError, 'Name cannot be nil or empty' if name.nil? || name.empty?
      raise ArgumentError, 'Callbacks cannot be nil' if callbacks.nil?

      @name = name
      @callbacks = callbacks
      @options = options
      @handle = nil
      @status = Types::FilterStatus::PENDING
      @initialized = false
    end

    def initialize!
      return if @initialized

      # Create filter handle (mock implementation)
      @handle = "filter_#{@name}_#{object_id}"
      @status = Types::FilterStatus::PROCESSING
      @initialized = true

      puts "✅ CApiFilter '#{@name}' initialized"
    end

    def process_data(data)
      return nil unless @initialized

      begin
        @status = Types::FilterStatus::PROCESSING

        # Process data through callbacks
        if @callbacks[:on_data]
          result = @callbacks[:on_data].call(data)
          @status = Types::FilterStatus::COMPLETED
          result
        else
          @status = Types::FilterStatus::COMPLETED
          data
        end
      rescue StandardError => e
        @status = Types::FilterStatus::ERROR

        return @callbacks[:on_error].call(e.message) if @callbacks[:on_error]

        raise FilterError.new(-1, "Filter processing error: #{e.message}")
      end
    end

    def destroy
      return unless @initialized

      @status = Types::FilterStatus::DISABLED
      @handle = nil
      @initialized = false

      puts "✅ CApiFilter '#{@name}' destroyed"
    end

    def enabled?
      @status != Types::FilterStatus::DISABLED
    end

    def disabled?
      @status == Types::FilterStatus::DISABLED
    end

    def error?
      @status == Types::FilterStatus::ERROR
    end

    def processing?
      @status == Types::FilterStatus::PROCESSING
    end

    def completed?
      @status == Types::FilterStatus::COMPLETED
    end

    def get_stats
      {
        name: @name,
        status: @status,
        initialized: @initialized,
        enabled: enabled?,
        callbacks: @callbacks.keys
      }
    end
  end
end
