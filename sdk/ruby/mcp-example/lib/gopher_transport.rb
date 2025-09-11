require 'mcp_filter_sdk'

# This file demonstrates how to use GopherTransport directly
# It's a wrapper around the main GopherTransport class with additional utilities

module McpExample
  class GopherTransportWrapper < McpFilterSdk::GopherTransport
    def initialize(config)
      super(config)
      @message_count = 0
      @start_time = Time.now
    end

    def send_message_with_stats(message)
      @message_count += 1
      puts "📤 Sending message ##{@message_count}: #{message[:method]}"

      start_time = Time.now
      result = send_message(message)
      end_time = Time.now

      puts "⏱️ Message sent in #{(end_time - start_time) * 1000}ms"
      result
    end

    def get_enhanced_stats
      base_stats = get_stats
      base_stats.merge({
                         message_count: @message_count,
                         uptime: Time.now - @start_time,
                         messages_per_second: @message_count / (Time.now - @start_time)
                       })
    end

    def broadcast_message(message)
      if @config.protocol == :tcp && @config.host.nil?
        # Server mode - broadcast to all connections
        @connections.each do |connection_id, client|
          client.puts(JSON.generate(message))
          puts "📤 Broadcasted to #{connection_id}"
        rescue StandardError => e
          puts "❌ Error broadcasting to #{connection_id}: #{e.message}"
          @connections.delete(connection_id)
        end
      else
        # Client mode - send normally
        send_message(message)
      end
    end

    def add_logging_filter
      logging_callbacks = {
        on_data: lambda { |data|
          puts "📝 [LOG] Data: #{data}"
          data
        },
        on_write: lambda { |data|
          puts "📝 [LOG] Write: #{data}"
          data
        },
        on_error: lambda { |error|
          puts "📝 [LOG] Error: #{error}"
          nil
        }
      }

      logging_filter = LoggingFilter.new(logging_callbacks)
      add_filter(logging_filter)
      puts '✅ Added logging filter'
    end

    def add_metrics_filter
      metrics_callbacks = {
        on_data: lambda { |data|
          @message_count += 1
          puts "📊 [METRICS] Processed message ##{@message_count}"
          data
        }
      }

      metrics_filter = MetricsFilter.new(metrics_callbacks)
      add_filter(metrics_filter)
      puts '✅ Added metrics filter'
    end
  end

  # Simple logging filter
  class LoggingFilter
    attr_reader :name, :callbacks

    def initialize(callbacks)
      @name = 'logging-filter'
      @callbacks = callbacks
    end

    def process_data(data)
      @callbacks[:on_data]&.call(data)
    end
  end

  # Simple metrics filter
  class MetricsFilter
    attr_reader :name, :callbacks

    def initialize(callbacks)
      @name = 'metrics-filter'
      @callbacks = callbacks
    end

    def process_data(data)
      @callbacks[:on_data]&.call(data)
    end
  end
end

# Example usage
if __FILE__ == $0
  puts '🚀 GopherTransport Wrapper Example'
  puts '=================================='

  # Create transport with wrapper
  config = {
    protocol: :stdio,
    host: nil,
    port: nil,
    max_connections: 1,
    buffer_size: 1024
  }

  transport = McpExample::GopherTransportWrapper.new(config)

  # Add filters
  transport.add_logging_filter
  transport.add_metrics_filter

  # Start transport
  transport.start

  # Send test messages
  test_messages = [
    { id: 1, method: 'test', params: { message: 'Hello World' } },
    { id: 2, method: 'ping', params: {} },
    { id: 3, method: 'echo', params: { text: 'Echo test' } }
  ]

  test_messages.each do |message|
    transport.send_message_with_stats(message)
    sleep(0.1) # Small delay between messages
  end

  # Show enhanced stats
  stats = transport.get_enhanced_stats
  puts "📊 Enhanced Stats: #{JSON.generate(stats)}"

  # Cleanup
  transport.stop
  puts '✅ Example completed'
end
