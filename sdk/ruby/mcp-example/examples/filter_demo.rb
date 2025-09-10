#!/usr/bin/env ruby

require_relative '../lib/filter_demo'

# Example usage of the filter demo
puts "🔧 MCP Filter Demo Example"
puts "=========================="

# Create and run the demo
demo = FilterDemo.new

begin
  # Run the demonstration
  demo.run
  
rescue => e
  puts "❌ Error: #{e.message}"
  puts e.backtrace.join("\n")
end

puts "✅ Example completed"
