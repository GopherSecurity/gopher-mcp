#!/usr/bin/env ruby

require_relative '../lib/mcp_calculator_client'

# Example usage of the calculator client
puts "🧮 MCP Calculator Client Example"
puts "================================="

# Create and run the client
client = McpCalculatorClient.new

begin
  # Connect to server
  client.connect
  
  # Run example calculations
  client.run_examples
  
rescue => e
  puts "❌ Error: #{e.message}"
  puts e.backtrace.join("\n")
ensure
  # Always disconnect
  client.disconnect
end

puts "✅ Example completed"
