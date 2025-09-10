require 'mcp_filter_sdk'
require 'json'

class McpCalculatorServer
  def initialize
    puts "🧮 MCP Calculator Server with Ruby SDK"
    puts "========================================="
    
    @transport = create_transport
    @filter = create_calculator_filter
    @transport.add_filter(@filter)
  end

  def start
    puts "🚀 Starting calculator server..."
    @transport.start
    puts "Server completed"
  end

  private

  def create_transport
    config = {
      protocol: :tcp,
      host: nil, # None means server mode
      port: 8080,
      max_connections: 10,
      buffer_size: 8192,
      filter_config: {
        debug: true,
        max_filters: 100,
        metrics: true
      }
    }
    
    McpFilterSdk::GopherTransport.new(config)
  end

  def create_calculator_filter
    callbacks = {
      on_data: method(:handle_calculator_request),
      on_write: method(:handle_write),
      on_new_connection: method(:handle_new_connection),
      on_error: method(:handle_error),
      on_high_watermark: method(:handle_high_watermark),
      on_low_watermark: method(:handle_low_watermark)
    }
    
    filter_config = {
      name: 'calculator-filter',
      type: :data,
      priority: 50,
      enabled: true,
      config_data: {
        operations: ['add', 'subtract', 'multiply', 'divide', 'power', 'sqrt', 'factorial']
      }
    }
    
    # Create a simple filter object that responds to process_data
    CalculatorFilter.new(callbacks, filter_config)
  end

  def handle_calculator_request(data)
    puts "📥 Received calculator request: #{data}"
    
    begin
      message = JSON.parse(data)
      result = process_calculation(message)
      
      response = {
        id: message['id'],
        jsonrpc: '2.0',
        result: result
      }
      
      puts "📤 Sending response: #{response}"
      response
    rescue JSON::ParserError => e
      puts "❌ Invalid JSON: #{e.message}"
      error_response(message['id'], -32700, "Parse error")
    rescue => e
      puts "❌ Calculation error: #{e.message}"
      error_response(message['id'], -32603, "Internal error")
    end
  end

  def handle_write(data)
    puts "📝 Write callback: #{data}"
    data
  end

  def handle_new_connection(connection_info)
    puts "🔗 New connection: #{connection_info}"
    nil
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

  def process_calculation(message)
    method = message['method']
    params = message['params'] || {}
    
    case method
    when 'add'
      params['a'] + params['b']
    when 'subtract'
      params['a'] - params['b']
    when 'multiply'
      params['a'] * params['b']
    when 'divide'
      raise "Division by zero" if params['b'] == 0
      params['a'].to_f / params['b']
    when 'power'
      params['a'] ** params['b']
    when 'sqrt'
      Math.sqrt(params['a'])
    when 'factorial'
      factorial(params['a'])
    else
      raise "Unknown operation: #{method}"
    end
  end

  def factorial(n)
    return 1 if n <= 1
    n * factorial(n - 1)
  end

  def error_response(id, code, message)
    {
      id: id,
      jsonrpc: '2.0',
      error: {
        code: code,
        message: message
      }
    }
  end
end

# Simple filter class for demonstration
class CalculatorFilter
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
  server = McpCalculatorServer.new
  server.start
end
