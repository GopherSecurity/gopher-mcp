require 'mcp_filter_sdk/types'

module McpFilterSdk
  class FilterChain
    include Types

    attr_reader :name, :execution_mode, :filters, :config

    def initialize(name, config = {})
      @name = name
      @config = config.is_a?(Hash) ? ChainConfig.new(config) : config
      @execution_mode = @config.execution_mode
      @filters = []
      @enabled = @config.enabled
    end

    def add_filter(filter)
      raise FilterError.new(-1, "Chain is disabled") unless @enabled
      raise FilterError.new(-1, "Maximum filters reached") if @filters.size >= @config.max_filters

      @filters << filter
      puts "✅ Added filter #{filter.name} to chain #{@name}"
    end

    def remove_filter(filter)
      @filters.delete(filter)
      puts "✅ Removed filter #{filter.name} from chain #{@name}"
    end

    def execute(data)
      return data unless @enabled
      return data if @filters.empty?

      puts "🔗 Executing chain #{@name} with #{@filters.size} filters"

      case @execution_mode
      when :sequential
        execute_sequential(data)
      when :parallel
        execute_parallel(data)
      when :conditional
        execute_conditional(data)
      when :pipeline
        execute_pipeline(data)
      else
        execute_sequential(data)
      end
    end

    def enable
      @enabled = true
      puts "✅ Chain #{@name} enabled"
    end

    def disable
      @enabled = false
      puts "❌ Chain #{@name} disabled"
    end

    def enabled?
      @enabled
    end

    def size
      @filters.size
    end

    def empty?
      @filters.empty?
    end

    def clear
      @filters.clear
      puts "🧹 Cleared all filters from chain #{@name}"
    end

    private

    def execute_sequential(data)
      result = data
      
      @filters.each_with_index do |filter, index|
        begin
          puts "  #{index + 1}. Processing with filter: #{filter.name}"
          result = filter.process_data(result) if filter.respond_to?(:process_data)
        rescue => e
          puts "❌ Error in filter #{filter.name}: #{e.message}"
          raise FilterError.new(-1, "Chain execution failed at filter #{filter.name}: #{e.message}")
        end
      end
      
      result
    end

    def execute_parallel(data)
      threads = @filters.map do |filter|
        Thread.new do
          begin
            puts "  Processing with filter: #{filter.name} (parallel)"
            filter.process_data(data) if filter.respond_to?(:process_data)
          rescue => e
            puts "❌ Error in filter #{filter.name}: #{e.message}"
            nil
          end
        end
      end
      
      results = threads.map(&:value).compact
      results.any? ? results.first : data
    end

    def execute_conditional(data)
      result = data
      
      @filters.each_with_index do |filter, index|
        begin
          # Check if filter should be executed based on some condition
          if should_execute_filter?(filter, result)
            puts "  #{index + 1}. Processing with filter: #{filter.name} (conditional)"
            result = filter.process_data(result) if filter.respond_to?(:process_data)
          else
            puts "  #{index + 1}. Skipping filter: #{filter.name} (condition not met)"
          end
        rescue => e
          puts "❌ Error in filter #{filter.name}: #{e.message}"
          raise FilterError.new(-1, "Chain execution failed at filter #{filter.name}: #{e.message}")
        end
      end
      
      result
    end

    def execute_pipeline(data)
      result = data
      
      @filters.each_with_index do |filter, index|
        begin
          puts "  #{index + 1}. Processing with filter: #{filter.name} (pipeline)"
          result = filter.process_data(result) if filter.respond_to?(:process_data)
          
          # Pipeline mode: each filter gets the output of the previous filter
          # This is similar to sequential but with explicit pipeline semantics
        rescue => e
          puts "❌ Error in filter #{filter.name}: #{e.message}"
          raise FilterError.new(-1, "Pipeline execution failed at filter #{filter.name}: #{e.message}")
        end
      end
      
      result
    end

    def should_execute_filter?(filter, data)
      # Default condition: always execute
      # This can be overridden by specific filter implementations
      true
    end
  end
end
