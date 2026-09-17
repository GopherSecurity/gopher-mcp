#!/usr/bin/env ruby
# frozen_string_literal: true

# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0

require_relative '../lib/mcp_filter_sdk'

# Filter Manager Demo - Advanced usage example
puts '🔧 MCP Filter SDK - Filter Manager Demo'
puts '=' * 50

begin
  # Initialize the filter manager
  puts "\n📋 Initializing Filter Manager..."
  manager = McpFilterSdk::FilterManager.new
  manager.initialize!
  puts '✅ Filter Manager initialized successfully'

  # Create a variety of filters with different behaviors
  puts "\n🔧 Creating specialized filters..."

  # Data validation filter
  validation_filter = manager.create_filter('validation', {
                                              on_data: lambda do |data|
                                                raise 'Invalid data: cannot be nil or empty' if data.nil? || data.empty?

                                                "VALID: #{data}"
                                              end,
                                              on_error: ->(error) { "Validation error: #{error}" }
                                            })
  puts '✅ Created validation filter'

  # Data transformation filter
  transform_filter = manager.create_filter('transform', {
                                             on_data: lambda do |data|
                                               # Convert to JSON-like format
                                               {
                                                 original: data,
                                                 length: data.length,
                                                 timestamp: Time.now.to_i,
                                                 processed: true
                                               }.to_json
                                             end,
                                             on_error: ->(error) { "Transform error: #{error}" }
                                           })
  puts '✅ Created transform filter'

  # Logging filter
  log_entries = []
  logging_filter = manager.create_filter('logging', {
                                           on_data: lambda do |data|
                                             log_entry = {
                                               timestamp: Time.now.strftime('%Y-%m-%dT%H:%M:%S.%6N%z'),
                                               data: data,
                                               level: 'INFO'
                                             }
                                             log_entries << log_entry
                                             puts "📝 Log: #{log_entry[:timestamp]} - #{data}"
                                             data
                                           end,
                                           on_error: ->(error) { puts "❌ Log error: #{error}" }
                                         })
  puts '✅ Created logging filter'

  # Rate limiting filter
  request_count = 0
  rate_limit_filter = manager.create_filter('rate-limit', {
                                              on_data: lambda do |data|
                                                request_count += 1
                                                if request_count > 5
                                                  raise "Rate limit exceeded: #{request_count} requests"
                                                end

                                                "RATE_LIMITED: #{data} (#{request_count}/5)"
                                              end,
                                              on_error: ->(error) { "Rate limit error: #{error}" }
                                            })
  puts '✅ Created rate limiting filter'

  # Error handling filter
  error_handling_filter = manager.create_filter('error-handler', {
                                                  on_data: lambda do |data|
                                                    # Simulate occasional errors
                                                    raise 'Simulated processing error' if rand < 0.3

                                                    "ERROR_SAFE: #{data}"
                                                  end,
                                                  on_error: lambda do |error|
                                                    puts "🛡️  Error handled gracefully: #{error}"
                                                    "ERROR_RECOVERED: #{error}"
                                                  end
                                                })
  puts '✅ Created error handling filter'

  # Display filter information
  puts "\n📝 Filter Manager Status:"
  filter_stats = manager.get_stats
  puts "  - Total filters: #{filter_stats[:filters]}"
  puts "  - Filter names: #{filter_stats[:filters][:list].join(', ')}"

  # Test individual filters
  puts "\n🧪 Testing individual filters..."
  test_data = 'test message'

  puts "\n  Testing validation filter:"
  begin
    result = validation_filter.process_data(test_data)
    puts "    Input: '#{test_data}'"
    puts "    Output: '#{result}'"
  rescue StandardError => e
    puts "    Error: #{e.message}"
  end

  puts "\n  Testing transform filter:"
  begin
    result = transform_filter.process_data(test_data)
    puts "    Input: '#{test_data}'"
    puts "    Output: #{result}"
  rescue StandardError => e
    puts "    Error: #{e.message}"
  end

  # Test filter chain execution
  puts "\n🔗 Testing filter chain execution..."

  # Create a simple chain
  chain = McpFilterSdk::FilterChain.new
  chain.initialize!

  chain.add_filter(validation_filter)
  chain.add_filter(logging_filter)
  chain.add_filter(transform_filter)
  chain.add_filter(error_handling_filter)

  puts "  Chain created with #{chain.size} filters"

  # Execute chain with test data
  test_messages = [
    'hello world',
    'filter chain test',
    'error simulation',
    'final message'
  ]

  test_messages.each_with_index do |message, index|
    puts "\n  Chain execution #{index + 1}:"
    puts "    Input: '#{message}'"

    begin
      result = chain.execute(message)
      puts "    Output: #{result}"
    rescue StandardError => e
      puts "    Chain error: #{e.message}"
    end
  end

  # Test rate limiting
  puts "\n⏱️  Testing rate limiting..."
  7.times do |i|
    result = rate_limit_filter.process_data("request #{i + 1}")
    puts "  Request #{i + 1}: #{result}"
  rescue StandardError => e
    puts "  Request #{i + 1}: #{e.message}"
  end

  # Display log entries
  puts "\n📋 Log Entries (#{log_entries.size}):"
  log_entries.each_with_index do |entry, index|
    puts "  #{index + 1}. #{entry[:timestamp]} - #{entry[:data]}"
  end

  # Test filter removal
  puts "\n🗑️  Testing filter removal..."
  puts "  Filters before removal: #{manager.list_filters.size}"

  removed = manager.destroy_filter('validation')
  puts "  Removed validation filter: #{removed}"
  puts "  Filters after removal: #{manager.list_filters.size}"

  # Final statistics
  puts "\n📊 Final Statistics:"
  final_stats = manager.get_stats
  puts "  - Active filters: #{final_stats[:filters]}"
  puts "  - Filter names: #{final_stats[:filters][:list].join(', ')}"
  puts "  - Chain size: #{chain.size}"
  puts "  - Log entries: #{log_entries.size}"
  puts "  - Total requests: #{request_count}"

  # Cleanup
  puts "\n🧹 Cleaning up..."
  chain.cleanup!
  manager.cleanup!
  puts '✅ Cleanup completed'

  puts "\n🎉 Filter Manager demo completed successfully!"
rescue StandardError => e
  puts "\n❌ Error occurred: #{e.message}"
  puts 'Backtrace:'
  puts e.backtrace.first(5).join("\n")
  exit 1
end
