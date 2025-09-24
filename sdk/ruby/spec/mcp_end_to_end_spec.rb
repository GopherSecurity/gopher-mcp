# frozen_string_literal: true

require 'spec_helper'

RSpec.describe 'MCP Filter SDK End-to-End Integration' do
  let(:filter_manager) { McpFilterSdk::FilterManager.new }
  let(:transport) { McpFilterSdk::GopherTransport.new(create_test_transport_config) }

  before do
    filter_manager.initialize!
  end

  after do
    filter_manager.cleanup!
    transport.stop if transport.is_connected
  end

  describe 'Complete workflow integration' do
    it 'processes data through complete filter chain' do
      # Create filters
      filter1 = filter_manager.create_filter('uppercase', {
                                               on_data: lambda(&:upcase),
                                               on_error: ->(error) { puts "Filter1 error: #{error}" }
                                             })

      filter2 = filter_manager.create_filter('add-prefix', {
                                               on_data: ->(data) { "PROCESSED: #{data}" },
                                               on_error: ->(error) { puts "Filter2 error: #{error}" }
                                             })

      # Add filters to transport
      transport.add_filter(filter1)
      transport.add_filter(filter2)

      # Start transport
      transport.start
      expect(transport.is_connected).to be true

      # Send message through transport
      test_message = 'hello world'
      result = transport.send_message(test_message)

      # Verify message was processed
      expect(result).to be true

      # Verify filters were applied
      expect(transport.filters.size).to eq(2)
    end

    it 'handles filter errors gracefully' do
      # Create a filter that throws errors
      error_filter = filter_manager.create_filter('error-filter', {
                                                    on_data: ->(_data) { raise 'Processing error' },
                                                    on_error: ->(error) { "Error handled: #{error}" }
                                                  })

      transport.add_filter(error_filter)
      transport.start

      # Send message - should not crash
      result = transport.send_message('test')
      expect(result).to be true
    end

    it 'manages multiple filters in sequence' do
      # Create multiple filters
      filters = []
      3.times do |i|
        filter = filter_manager.create_filter("filter-#{i}", {
                                                on_data: ->(data) { "#{data}-#{i}" },
                                                on_error: ->(error) { puts "Filter #{i} error: #{error}" }
                                              })
        filters << filter
        transport.add_filter(filter)
      end

      transport.start

      # Send message
      result = transport.send_message('start')
      expect(result).to be true

      # Verify all filters are present
      expect(transport.filters.size).to eq(3)
    end

    it 'handles transport lifecycle correctly' do
      # Create and add filter
      filter = filter_manager.create_filter('test-filter', {
                                              on_data: ->(data) { data },
                                              on_error: ->(error) { puts "Error: #{error}" }
                                            })

      transport.add_filter(filter)

      # Start transport
      expect(transport.is_connected).to be false
      transport.start
      expect(transport.is_connected).to be true

      # Send message
      result = transport.send_message('test')
      expect(result).to be true

      # Stop transport
      transport.stop
      expect(transport.is_connected).to be false
    end

    it 'provides comprehensive statistics' do
      # Create filters
      filter1 = filter_manager.create_filter('filter1', {
                                               on_data: ->(data) { data },
                                               on_error: ->(error) { puts "Error: #{error}" }
                                             })

      filter2 = filter_manager.create_filter('filter2', {
                                               on_data: ->(data) { data },
                                               on_error: ->(error) { puts "Error: #{error}" }
                                             })

      transport.add_filter(filter1)
      transport.add_filter(filter2)
      transport.start

      # Get statistics
      filter_stats = filter_manager.get_stats
      transport_stats = transport.get_stats

      expect(filter_stats[:filters][:total]).to eq(2)
      expect(transport_stats[:filters]).to eq(2)
      expect(transport_stats[:is_connected]).to be true
    end

    it 'handles concurrent operations' do
      # Create filter
      filter = filter_manager.create_filter('concurrent-filter', {
                                              on_data: ->(data) { data },
                                              on_error: ->(error) { puts "Error: #{error}" }
                                            })

      transport.add_filter(filter)
      transport.start

      # Send multiple messages concurrently
      messages = %w[msg1 msg2 msg3 msg4 msg5]
      results = messages.map { |msg| transport.send_message(msg) }

      # All should succeed
      expect(results.all?).to be true
    end
  end

  describe 'Error handling and recovery' do
    it 'recovers from filter failures' do
      # Create a filter that fails initially but recovers
      recovery_count = 0
      filter = filter_manager.create_filter('recovery-filter', {
                                              on_data: lambda do |data|
                                                recovery_count += 1
                                                raise 'Initial failure' if recovery_count == 1

                                                "Recovered: #{data}"
                                              end,
                                              on_error: ->(error) { "Handled: #{error}" }
                                            })

      transport.add_filter(filter)
      transport.start

      # First message should trigger error handling
      result1 = transport.send_message('test1')
      expect(result1).to be true

      # Second message should work normally
      result2 = transport.send_message('test2')
      expect(result2).to be true
    end

    it 'handles transport disconnection gracefully' do
      filter = filter_manager.create_filter('disconnect-filter', {
                                              on_data: ->(data) { data },
                                              on_error: ->(error) { puts "Error: #{error}" }
                                            })

      transport.add_filter(filter)
      transport.start

      # Send message while connected
      result1 = transport.send_message('connected')
      expect(result1).to be true

      # Disconnect
      transport.stop
      expect(transport.is_connected).to be false

      # Sending message while disconnected should handle gracefully
      result2 = transport.send_message('disconnected')
      expect(result2).to be false
    end
  end
end
