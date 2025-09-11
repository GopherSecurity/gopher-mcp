#!/usr/bin/env ruby

require_relative '../lib/mcp_filter_sdk'

# Integration Test - Comprehensive end-to-end testing
puts "🧪 MCP Filter SDK - Integration Test"
puts "=" * 50

class IntegrationTest
  def initialize
    @test_results = []
    @manager = nil
    @transport = nil
  end

  def run_all_tests
    puts "\n🚀 Starting Integration Tests..."
    
    test_initialization
    test_filter_creation
    test_filter_operations
    test_transport_operations
    test_error_handling
    test_performance
    test_cleanup
    
    display_results
  end

  private

  def test_initialization
    puts "\n📋 Test 1: Initialization"
    
    begin
      @manager = McpFilterSdk::FilterManager.new
      @manager.initialize!
      
      @transport = McpFilterSdk::GopherTransport.new({
        protocol: :stdio,
        host: 'localhost',
        port: 8080,
        max_connections: 1,
        buffer_size: 1024
      })
      
      record_test("Initialization", true, "Manager and transport initialized successfully")
    rescue => e
      record_test("Initialization", false, "Failed: #{e.message}")
    end
  end

  def test_filter_creation
    puts "\n🔧 Test 2: Filter Creation"
    
    begin
      # Create various types of filters
      filters = {}
      
      filters[:uppercase] = @manager.create_filter('uppercase', {
        on_data: ->(data) { data.upcase },
        on_error: ->(error) { "Error: #{error}" }
      })
      
      filters[:reverse] = @manager.create_filter('reverse', {
        on_data: ->(data) { data.reverse },
        on_error: ->(error) { "Error: #{error}" }
      })
      
      filters[:json] = @manager.create_filter('json', {
        on_data: ->(data) { { message: data, timestamp: Time.now.to_i }.to_json },
        on_error: ->(error) { "Error: #{error}" }
      })
      
      filters[:error_prone] = @manager.create_filter('error_prone', {
        on_data: ->(data) do
          if data.include?('error')
            raise "Simulated error for: #{data}"
          end
          "SAFE: #{data}"
        end,
        on_error: ->(error) { "Handled: #{error}" }
      })
      
      # Verify all filters were created
      created_filters = @manager.list_filters
      expected_count = 4
      
      if created_filters.size == expected_count
        record_test("Filter Creation", true, "Created #{expected_count} filters successfully")
      else
        record_test("Filter Creation", false, "Expected #{expected_count}, got #{created_filters.size}")
      end
      
      @filters = filters
    rescue => e
      record_test("Filter Creation", false, "Failed: #{e.message}")
    end
  end

  def test_filter_operations
    puts "\n⚙️  Test 3: Filter Operations"
    
    begin
      test_data = "hello world"
      expected_results = {
        uppercase: "HELLO WORLD",
        reverse: "dlrow olleh",
        json: /"message":"hello world"/,
        error_prone: "SAFE: hello world"
      }
      
      @filters.each do |name, filter|
        result = filter.process_data(test_data)
        
        case name
        when :json
          if result.match?(expected_results[name])
            record_test("Filter #{name}", true, "JSON output matches expected pattern")
          else
            record_test("Filter #{name}", false, "JSON output doesn't match pattern")
          end
        else
          if result == expected_results[name]
            record_test("Filter #{name}", true, "Output matches expected result")
          else
            record_test("Filter #{name}", false, "Expected '#{expected_results[name]}', got '#{result}'")
          end
        end
      end
      
      # Test error handling
      error_result = @filters[:error_prone].process_data("test error message")
      if error_result.include?("Handled:")
        record_test("Error Handling", true, "Error handled gracefully")
      else
        record_test("Error Handling", false, "Error not handled properly")
      end
      
    rescue => e
      record_test("Filter Operations", false, "Failed: #{e.message}")
    end
  end

  def test_transport_operations
    puts "\n🚀 Test 4: Transport Operations"
    
    begin
      # Add filters to transport
      @filters.each do |name, filter|
        @transport.add_filter(filter)
      end
      
      # Start transport
      @transport.start
      
      if @transport.is_connected
        record_test("Transport Start", true, "Transport started successfully")
      else
        record_test("Transport Start", false, "Transport failed to start")
        return
      end
      
      # Test message sending
      test_messages = [
        "integration test",
        "transport message",
        "filter chain test"
      ]
      
      success_count = 0
      test_messages.each do |message|
        result = @transport.send_message(message)
        success_count += 1 if result
      end
      
      if success_count == test_messages.size
        record_test("Message Sending", true, "All #{test_messages.size} messages sent successfully")
      else
        record_test("Message Sending", false, "Only #{success_count}/#{test_messages.size} messages sent")
      end
      
      # Test transport statistics
      stats = @transport.get_stats
      if stats[:filters] == @filters.size
        record_test("Transport Stats", true, "Statistics show correct filter count")
      else
        record_test("Transport Stats", false, "Statistics show incorrect filter count")
      end
      
    rescue => e
      record_test("Transport Operations", false, "Failed: #{e.message}")
    end
  end

  def test_error_handling
    puts "\n🛡️  Test 5: Error Handling"
    
    begin
      # Test invalid filter creation
      begin
        @manager.create_filter('', {})
        record_test("Invalid Filter Name", false, "Should have rejected empty name")
      rescue ArgumentError
        record_test("Invalid Filter Name", true, "Correctly rejected empty name")
      end
      
      # Test nil callbacks
      begin
        @manager.create_filter('nil_callbacks', nil)
        record_test("Nil Callbacks", false, "Should have rejected nil callbacks")
      rescue ArgumentError
        record_test("Nil Callbacks", true, "Correctly rejected nil callbacks")
      end
      
      # Test transport operations when not connected
      @transport.stop
      result = @transport.send_message("test")
      if !result
        record_test("Disconnected Send", true, "Correctly handled send when disconnected")
      else
        record_test("Disconnected Send", false, "Should have failed when disconnected")
      end
      
    rescue => e
      record_test("Error Handling", false, "Failed: #{e.message}")
    end
  end

  def test_performance
    puts "\n⚡ Test 6: Performance"
    
    begin
      # Test with many filters
      many_filters = []
      50.times do |i|
        filter = @manager.create_filter("perf_filter_#{i}", {
          on_data: ->(data) { "#{data}_#{i}" },
          on_error: ->(error) { "Error: #{error}" }
        })
        many_filters << filter
      end
      
      # Test execution time
      start_time = Time.now
      test_data = "performance test"
      
      many_filters.each do |filter|
        filter.process_data(test_data)
      end
      
      execution_time = Time.now - start_time
      
      if execution_time < 1.0  # Should complete in less than 1 second
        record_test("Performance", true, "50 filters processed in #{execution_time.round(3)}s")
      else
        record_test("Performance", false, "Performance too slow: #{execution_time.round(3)}s")
      end
      
      # Cleanup performance test filters
      many_filters.each do |filter|
        @manager.destroy_filter(filter.name)
      end
      
    rescue => e
      record_test("Performance", false, "Failed: #{e.message}")
    end
  end

  def test_cleanup
    puts "\n🧹 Test 7: Cleanup"
    
    begin
      # Test filter removal
      initial_count = @manager.list_filters.size
      removed = @manager.destroy_filter('uppercase')
      
      if removed && @manager.list_filters.size == initial_count - 1
        record_test("Filter Removal", true, "Filter removed successfully")
      else
        record_test("Filter Removal", false, "Filter removal failed")
      end
      
      # Test transport cleanup
      @transport.stop
      if !@transport.is_connected
        record_test("Transport Stop", true, "Transport stopped successfully")
      else
        record_test("Transport Stop", false, "Transport stop failed")
      end
      
      # Test manager cleanup
      @manager.cleanup!
      record_test("Manager Cleanup", true, "Manager cleaned up successfully")
      
    rescue => e
      record_test("Cleanup", false, "Failed: #{e.message}")
    end
  end

  def record_test(test_name, passed, message)
    @test_results << { name: test_name, passed: passed, message: message }
    status = passed ? "✅" : "❌"
    puts "  #{status} #{test_name}: #{message}"
  end

  def display_results
    puts "\n" + "=" * 50
    puts "📊 INTEGRATION TEST RESULTS"
    puts "=" * 50
    
    passed_tests = @test_results.count { |r| r[:passed] }
    total_tests = @test_results.size
    
    puts "\nSummary:"
    puts "  Total Tests: #{total_tests}"
    puts "  Passed: #{passed_tests}"
    puts "  Failed: #{total_tests - passed_tests}"
    puts "  Success Rate: #{(passed_tests.to_f / total_tests * 100).round(1)}%"
    
    puts "\nDetailed Results:"
    @test_results.each do |result|
      status = result[:passed] ? "✅ PASS" : "❌ FAIL"
      puts "  #{status} - #{result[:name]}: #{result[:message]}"
    end
    
    if passed_tests == total_tests
      puts "\n🎉 All integration tests passed!"
      exit 0
    else
      puts "\n⚠️  Some tests failed. Check the details above."
      exit 1
    end
  end
end

# Run the integration test
begin
  test = IntegrationTest.new
  test.run_all_tests
rescue => e
  puts "\n❌ Integration test failed with error: #{e.message}"
  puts "Backtrace:"
  puts e.backtrace.first(10).join("\n")
  exit 1
end
