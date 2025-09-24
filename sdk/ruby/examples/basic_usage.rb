#!/usr/bin/env ruby
# frozen_string_literal: true

require_relative '../lib/mcp_filter_sdk'

# Basic usage example of the MCP Filter SDK
puts '🚀 MCP Filter SDK - Basic Usage Example'
puts '=' * 50

begin
  # Initialize the filter manager
  puts "\n📋 Initializing Filter Manager..."
  manager = McpFilterSdk::FilterManager.new
  manager.initialize!
  puts '✅ Filter Manager initialized successfully'

  # Create some filters
  puts "\n🔧 Creating filters..."

  # Uppercase filter
  uppercase_filter = manager.create_filter('uppercase', {
                                             on_data: lambda { |data|
                                               if data.is_a?(Hash) && data[:params] && data[:params][:data]
                                                 data[:params][:data] = data[:params][:data].upcase
                                                 data
                                               else
                                                 data.is_a?(String) ? data.upcase : data
                                               end
                                             },
                                             on_error: ->(error) { puts "Uppercase filter error: #{error}" }
                                           })
  puts '✅ Created uppercase filter'

  # Add prefix filter
  prefix_filter = manager.create_filter('add-prefix', {
                                          on_data: lambda { |data|
                                            if data.is_a?(Hash) && data[:params] && data[:params][:data]
                                              data[:params][:data] = "PROCESSED: #{data[:params][:data]}"
                                              data
                                            else
                                              data.is_a?(String) ? "PROCESSED: #{data}" : data
                                            end
                                          },
                                          on_error: ->(error) { puts "Prefix filter error: #{error}" }
                                        })
  puts '✅ Created prefix filter'

  # Reverse filter
  reverse_filter = manager.create_filter('reverse', {
                                           on_data: lambda { |data|
                                             if data.is_a?(Hash) && data[:params] && data[:params][:data]
                                               data[:params][:data] = data[:params][:data].reverse
                                               data
                                             else
                                               data.is_a?(String) ? data.reverse : data
                                             end
                                           },
                                           on_error: ->(error) { puts "Reverse filter error: #{error}" }
                                         })
  puts '✅ Created reverse filter'

  # List all filters
  puts "\n📝 Current filters:"
  manager.list_filters.each do |filter_name|
    puts "  - #{filter_name}"
  end

  # Create a transport
  puts "\n🚀 Creating Gopher Transport..."
  transport_config = {
    protocol: :stdio,
    host: 'localhost',
    port: 8080,
    max_connections: 1,
    buffer_size: 1024
  }

  transport = McpFilterSdk::GopherTransport.new(transport_config)
  puts '✅ Transport created'

  # Add filters to transport
  puts "\n🔗 Adding filters to transport..."
  transport.add_filter(uppercase_filter)
  transport.add_filter(prefix_filter)
  transport.add_filter(reverse_filter)
  puts "✅ Added #{transport.filters.size} filters to transport"

  # Start transport
  puts "\n▶️  Starting transport..."
  transport.start
  puts '✅ Transport started successfully'

  # Send some test messages
  puts "\n📤 Sending test messages..."
  test_messages = [
    { method: 'test', id: 1, params: { data: 'hello world' } },
    { method: 'test', id: 2, params: { data: 'ruby sdk test' } },
    { method: 'test', id: 3, params: { data: 'mcp filter example' } }
  ]

  test_messages.each do |message|
    puts "\n  Sending: '#{message[:params][:data]}'"
    result = transport.send_message(message)
    puts "  Result: #{result ? 'Success' : 'Failed'}"
  end

  # Get statistics
  puts "\n📊 Statistics:"
  filter_stats = manager.get_stats
  transport_stats = transport.get_stats

  puts '  Filter Manager:'
  puts "    - Total filters: #{filter_stats[:filters]}"
  puts "    - Filter names: #{filter_stats[:filters][:list].join(', ')}"

  puts '  Transport:'
  puts "    - Connected: #{transport_stats[:is_connected]}"
  puts "    - Filters: #{transport_stats[:filters]}"
  puts "    - Connections: #{transport_stats[:connections]}"

  # Cleanup
  puts "\n🧹 Cleaning up..."
  transport.stop
  manager.cleanup!
  puts '✅ Cleanup completed'

  puts "\n🎉 Basic usage example completed successfully!"
rescue StandardError => e
  puts "\n❌ Error occurred: #{e.message}"
  puts 'Backtrace:'
  puts e.backtrace.first(5).join("\n")
  exit 1
end
