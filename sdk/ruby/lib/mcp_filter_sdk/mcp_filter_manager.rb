require 'mcp_filter_sdk/filter_api'
require 'mcp_filter_sdk/filter_chain'
require 'mcp_filter_sdk/types'

module McpFilterSdk
  class FilterManager
    include Types

    attr_reader :filters, :chains, :config

    def initialize(config = {})
      @config = config
      @filters = {}
      @chains = {}
      @initialized = false
    end

    def initialize!
      return if @initialized

      @api = FilterApi.new(@config)
      @api.initialize!
      @initialized = true
      
      puts "✅ FilterManager initialized"
    end

    def create_filter(name, callbacks, config = {})
      initialize! unless @initialized
      
      filter = @api.create_filter(name, callbacks, config)
      @filters[name] = filter
      filter
    end

    def destroy_filter(name)
      filter = @filters[name]
      return false unless filter

      @api.destroy_filter(name)
      @filters.delete(name)
      
      # Remove from all chains
      @chains.each_value do |chain|
        chain.remove_filter(filter)
      end
      
      true
    end

    def create_chain(name, config = {})
      chain = FilterChain.new(name, config)
      @chains[name] = chain
      puts "✅ Created chain: #{name}"
      chain
    end

    def destroy_chain(name)
      chain = @chains[name]
      return false unless chain

      chain.clear
      @chains.delete(name)
      puts "✅ Destroyed chain: #{name}"
      true
    end

    def add_filter_to_chain(chain_name, filter_name)
      chain = @chains[chain_name]
      filter = @filters[filter_name]
      
      raise FilterError.new(-1, "Chain not found: #{chain_name}") unless chain
      raise FilterError.new(-1, "Filter not found: #{filter_name}") unless filter
      
      chain.add_filter(filter)
      true
    end

    def remove_filter_from_chain(chain_name, filter_name)
      chain = @chains[chain_name]
      filter = @filters[filter_name]
      
      raise FilterError.new(-1, "Chain not found: #{chain_name}") unless chain
      raise FilterError.new(-1, "Filter not found: #{filter_name}") unless filter
      
      chain.remove_filter(filter)
      true
    end

    def execute_chain(chain_name, data)
      chain = @chains[chain_name]
      raise FilterError.new(-1, "Chain not found: #{chain_name}") unless chain
      
      chain.execute(data)
    end

    def process_data(data, filter_name = nil, chain_name = nil)
      if chain_name
        execute_chain(chain_name, data)
      elsif filter_name
        @api.process_data(data, filter_name)
      else
        @api.process_data(data)
      end
    end

    def get_filter_status(name)
      @api.get_filter_status(name)
    end

    def get_chain_status(name)
      chain = @chains[name]
      return :not_found unless chain
      
      chain.enabled? ? :enabled : :disabled
    end

    def list_filters
      @filters.keys
    end

    def list_chains
      @chains.keys
    end

    def get_stats
      {
        filters: {
          total: @filters.size,
          list: @filters.keys
        },
        chains: {
          total: @chains.size,
          list: @chains.keys
        },
        initialized: @initialized
      }
    end

    def cleanup!
      @chains.each_value(&:clear)
      @chains.clear
      @api&.cleanup!
      @initialized = false
      puts "✅ FilterManager cleaned up"
    end
  end
end
