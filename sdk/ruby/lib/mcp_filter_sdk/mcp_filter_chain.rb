# frozen_string_literal: true

require 'mcp_filter_sdk/mcp_ffi_bindings'
require 'mcp_filter_sdk/mcp_c_structs'
require 'mcp_filter_sdk/types/index'

module McpFilterSdk
  class FilterChain
    attr_reader :filters, :is_initialized

    def initialize
      @filters = []
      @is_initialized = false
    end

    def initialize!
      return if @is_initialized

      @is_initialized = true
      puts '✅ FilterChain initialized'
    end

    def add_filter(filter)
      return false if filter.nil?

      @filters << filter
      puts "✅ Added filter to chain: #{begin
        filter.name
      rescue StandardError
        'unknown'
      end}"
      true
    end

    def remove_filter(filter)
      return false if filter.nil?

      if @filters.include?(filter)
        @filters.delete(filter)
        puts "✅ Removed filter from chain: #{begin
          filter.name
        rescue StandardError
          'unknown'
        end}"
        true
      else
        false
      end
    end

    def execute(data)
      return data unless @is_initialized

      current_data = data

      @filters.each do |filter|
        # Initialize filter if not already initialized
        filter.initialize! if filter.respond_to?(:initialize!) && !filter.instance_variable_get(:@initialized)

        if filter.respond_to?(:process_data)
          current_data = filter.process_data(current_data)
        elsif filter.respond_to?(:callbacks) && filter.callbacks[:on_data]
          current_data = filter.callbacks[:on_data].call(current_data)
        end
      rescue StandardError => e
        if filter.respond_to?(:callbacks) && filter.callbacks[:on_error]
          current_data = filter.callbacks[:on_error].call(e.message)
        else
          puts "Filter error: #{e.message}"
          current_data = "ERROR: #{e.message}"
        end
      end

      current_data
    end

    def get_filters
      @filters.dup
    end

    def size
      @filters.size
    end

    def is_empty?
      @filters.empty?
    end

    def clear
      @filters.clear
      puts '✅ FilterChain cleared'
    end

    def cleanup!
      return unless @is_initialized

      @filters.clear
      @is_initialized = false
      puts '✅ FilterChain cleaned up'
    end
  end
end
