#!/usr/bin/env ruby
# frozen_string_literal: true

# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0

require_relative '../lib/mcp_calculator_client'

# Example usage of the calculator client
puts '🧮 MCP Calculator Client Example'
puts '================================='

# Create and run the client
client = McpCalculatorClient.new

begin
  # Connect to server
  client.connect

  # Run example calculations
  client.run_examples
rescue StandardError => e
  puts "❌ Error: #{e.message}"
  puts e.backtrace.join("\n")
ensure
  # Always disconnect
  client.disconnect
end

puts '✅ Example completed'
