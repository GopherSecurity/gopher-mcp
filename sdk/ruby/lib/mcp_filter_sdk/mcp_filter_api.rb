require 'mcp_filter_sdk/mcp_ffi_bindings'
require 'mcp_filter_sdk/mcp_c_structs'
require 'mcp_filter_sdk/types/index'

module McpFilterSdk
  class FilterApi
    def initialize(config = {})
      @config = config
      @handle = nil
      @filters = {}
      @initialized = false
    end

    def initialize!
      return if @initialized

      # Mock initialization - always successful
      @initialized = true
      puts '✅ FilterApi initialized'
    end

    def create_filter(name, callbacks, config = {})
      raise ArgumentError, 'Name cannot be nil or empty' if name.nil? || name.empty?
      raise ArgumentError, 'Callbacks cannot be nil' if callbacks.nil?

      # Create a CApiFilter instance
      filter = CApiFilter.new(name, callbacks, config)
      filter.initialize!

      @filters[name] = filter
      puts "✅ Created filter: #{name}"

      filter
    end

    def destroy_filter(name)
      return false unless @filters.key?(name)

      filter = @filters[name]
      filter.destroy
      @filters.delete(name)

      puts "✅ Destroyed filter: #{name}"
      true
    end

    def list_filters
      @filters.keys
    end

    def get_filter(name)
      @filters[name]
    end

    def get_stats
      {
        filters: @filters.size,
        initialized: @initialized,
        filter_names: @filters.keys
      }
    end

    def cleanup!
      return unless @initialized

      # Clean up all filters
      @filters.each do |_name, filter|
        filter.destroy
      end
      @filters.clear

      @initialized = false
      puts '✅ FilterApi cleaned up'
    end
  end
end
