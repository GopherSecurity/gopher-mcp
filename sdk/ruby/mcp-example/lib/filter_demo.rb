require 'mcp_filter_sdk'
require 'json'

class FilterDemo
  def initialize
    puts '🔧 MCP Filter SDK Demonstration'
    puts '==============================='

    @transport = create_transport
    @filter_manager = create_filter_manager
    @chains = create_filter_chains
  end

  def run
    puts '🚀 Starting filter demonstration...'

    # Initialize transport
    initialize_transport

    # Demonstrate filter creation
    demonstrate_filter_creation

    # Demonstrate filter chains
    demonstrate_filter_chains

    # Demonstrate buffer operations
    demonstrate_buffer_operations

    # Demonstrate transport integration
    demonstrate_transport_integration

    puts '🎉 Filter demonstration completed successfully'
    puts 'Demo completed successfully'

    # Cleanup
    cleanup
  end

  private

  def create_transport
    config = {
      protocol: :stdio,
      host: nil,
      port: nil,
      max_connections: 5,
      buffer_size: 4096,
      filter_config: {
        debug: true,
        max_filters: 100,
        metrics: true
      }
    }

    McpFilterSdk::GopherTransport.new(config)
  end

  def create_filter_manager
    manager = McpFilterSdk::FilterManager.new
    manager.initialize!
    manager
  end

  def create_filter_chains
    chains = {}

    # Simple chain
    chains[:simple] = @filter_manager.create_chain('demo-chain', {
                                                     execution_mode: :sequential,
                                                     max_filters: 10,
                                                     timeout: 5000,
                                                     enabled: true
                                                   })

    # Parallel chain
    chains[:parallel] = @filter_manager.create_chain('parallel-demo-chain', {
                                                       execution_mode: :parallel,
                                                       max_filters: 10,
                                                       timeout: 5000,
                                                       enabled: true
                                                     })

    # Conditional chain
    chains[:conditional] = @filter_manager.create_chain('conditional-demo-chain', {
                                                          execution_mode: :conditional,
                                                          max_filters: 10,
                                                          timeout: 5000,
                                                          enabled: true
                                                        })

    chains
  end

  def initialize_transport
    puts '🚀 Initializing transport...'
    @transport.start
    puts '✅ Transport initialized'
  end

  def demonstrate_filter_creation
    puts '📝 Demonstrating filter creation...'

    # Create a data processing filter
    create_data_filter
    @filter_manager.add_filter_to_chain('demo-chain', 'data-filter')

    # Create an error handling filter
    create_error_filter
    @filter_manager.add_filter_to_chain('demo-chain', 'error-filter')

    puts '✅ Filter callbacks configured'
  end

  def demonstrate_filter_chains
    puts '🔗 Demonstrating filter chains...'

    # Add filters to chains
    @chains.each do |name, chain|
      puts "✅ Created #{name} chain: #{chain.name}"
    end
  end

  def demonstrate_buffer_operations
    puts '💾 Demonstrating buffer operations...'

    buffer = McpFilterSdk::FilterBuffer.new(1024)

    # Add data to buffer
    test_data = 'Hello, MCP Filter SDK!'
    buffer.add_data(test_data)

    # Get data from buffer
    buffer.get_contiguous_data
    puts '✅ Buffer operations concept demonstrated'
  end

  def demonstrate_transport_integration
    puts '🚀 Demonstrating transport integration...'

    # Create a test message
    message = {
      id: 1,
      jsonrpc: '2.0',
      method: 'test',
      params: {
        message: 'Hello from filter demo!'
      }
    }

    # Send message through transport
    @transport.send_message(message)
    puts '✅ Sent test message through transport'

    # Show transport stats
    stats = @transport.get_stats
    puts "📊 Transport stats: #{JSON.generate(stats)}"
  end

  def create_data_filter
    callbacks = {
      on_data: lambda { |data|
        puts "📥 Processing data: #{data}"
        "processed: #{data}"
      },
      on_error: lambda { |error|
        puts "❌ Data filter error: #{error}"
        nil
      }
    }

    @filter_manager.create_filter('data-filter', callbacks, {
                                    type: :data,
                                    priority: 50,
                                    enabled: true
                                  })
  end

  def create_error_filter
    callbacks = {
      on_data: lambda { |data|
        puts "🔍 Error filter checking: #{data}"
        data
      },
      on_error: lambda { |error|
        puts "🛡️ Error filter caught: #{error}"
        "error_handled: #{error}"
      }
    }

    @filter_manager.create_filter('error-filter', callbacks, {
                                    type: :error,
                                    priority: 75,
                                    enabled: true
                                  })
  end

  def cleanup
    puts '🧹 Cleaning up resources...'
    @transport.stop
    puts '✅ Transport closed'
    @filter_manager.cleanup!
    puts '✅ Cleanup completed'
  end
end

# Main execution
if __FILE__ == $0
  demo = FilterDemo.new
  demo.run
end
