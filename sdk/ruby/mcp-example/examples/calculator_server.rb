#!/usr/bin/env ruby
# frozen_string_literal: true

# Copyright 2025 Gopher Security, Inc.
# SPDX-License-Identifier: Apache-2.0

require_relative '../lib/mcp_calculator_server'

# Example usage of the calculator server
puts '🧮 MCP Calculator Server Example'
puts '================================='

# Create and run the server
server = McpCalculatorServer.new

begin
  # Start the server
  server.start
rescue Interrupt
  puts "\n🛑 Server interrupted by user"
rescue StandardError => e
  puts "❌ Error: #{e.message}"
  puts e.backtrace.join("\n")
ensure
  puts '🧹 Cleaning up server resources...'
end

puts '✅ Example completed'
