require 'mcp_filter_sdk'
require 'json'

class McpCalculatorClient
  def initialize
    puts "🧮 MCP Calculator Client with Ruby SDK"
    puts "======================================="
    
    @transport = create_transport
    @filter = create_client_filter
    @transport.add_filter(@filter)
    @message_id = 1
  end

  def connect
    puts "🔗 Connecting to calculator server..."
    @transport.start
    puts "✅ Connected to server"
  end

  def disconnect
    puts "🔌 Disconnecting from server..."
    @transport.stop
    puts "✅ Disconnected from server"
  end

  def calculate(operation, a, b = nil)
    message = {
      id: @message_id,
      jsonrpc: '2.0',
      method: operation,
      params: b ? { a: a, b: b } : { a: a }
    }
    
    @message_id += 1
    
    puts "📤 Sending calculation: #{operation}(#{a}#{", #{b}" if b})"
    @transport.send_message(message)
  end

  def run_examples
    puts "\n🧮 Running calculator examples..."
    
    # Basic arithmetic
    calculate('add', 5, 3)
    calculate('subtract', 10, 4)
    calculate('multiply', 6, 7)
    calculate('divide', 15, 3)
    
    # Advanced operations
    calculate('power', 2, 8)
    calculate('sqrt', 64)
    calculate('factorial', 5)
    
    puts "\n✅ All examples completed"
  end

  private

  def create_transport
    config = {
      protocol: :tcp,
      host: 'localhost',
      port: 8080,
      connect_timeout: 30000,
      send_timeout: 5000,
      receive_timeout: 5000,
      max_connections: 1,
      buffer_size: 8192,
      filter_config: {
        debug: true,
        max_filters: 100,
        metrics: true
      }
    }
    
    McpFilterSdk::GopherTransport.new(config)
  end

  def create_client_filter
    callbacks = {
      on_data: method(:handle_response),
      on_write: method(:handle_write),
      on_error: method(:handle_error),
      on_high_watermark: method(:handle_high_watermark),
      on_low_watermark: method(:handle_low_watermark)
    }
    
    filter_config = {
      name: 'client-filter',
      type: :data,
      priority: 50,
      enabled: true,
      config_data: {
        client_mode: true
      }
    }
    
    ClientFilter.new(callbacks, filter_config)
  end

  def handle_response(data)
    puts "📥 Received response: #{data}"
    
    begin
      response = JSON.parse(data)
      
      if response['result']
        puts "✅ Result: #{response['result']}"
      elsif response['error']
        puts "❌ Error: #{response['error']['message']} (code: #{response['error']['code']})"
      end
    rescue JSON::ParserError => e
      puts "❌ Invalid JSON response: #{e.message}"
    end
  end

  def handle_write(data)
    puts "📝 Write callback: #{data}"
    data
  end

  def handle_error(error_info)
    puts "❌ Error: #{error_info}"
    nil
  end

  def handle_high_watermark(buffer_info)
    puts "⚠️ High watermark: #{buffer_info}"
    nil
  end

  def handle_low_watermark(buffer_info)
    puts "✅ Low watermark: #{buffer_info}"
    nil
  end
end

# Simple client filter class for demonstration
class ClientFilter
  attr_reader :name, :callbacks, :config

  def initialize(callbacks, config)
    @name = config[:name]
    @callbacks = callbacks
    @config = config
  end

  def process_data(data)
    @callbacks[:on_data]&.call(data)
  end
end

# Main execution
if __FILE__ == $0
  client = McpCalculatorClient.new
  
  begin
    client.connect
    client.run_examples
  rescue => e
    puts "❌ Error: #{e.message}"
  ensure
    client.disconnect
  end
end
