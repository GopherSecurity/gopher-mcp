require 'socket'
require 'json'
require 'securerandom'
require 'mcp_filter_sdk/types'

module McpFilterSdk
  class GopherTransport
    include Types

    attr_reader :config, :connections, :filters, :session_id, :is_connected, :is_destroyed

    def initialize(config)
      @config = config.is_a?(Hash) ? TransportConfig.new(config) : config
      @connections = {}
      @filters = []
      @session_id = "gopher-#{Time.now.to_i}-#{SecureRandom.random_number(2**64)}"
      @is_connected = false
      @is_destroyed = false
      @tcp_listener = nil
      @tcp_server_thread = nil
    end

    def start
      puts "🚀 Starting GopherTransport (#{@config.protocol.to_s.capitalize})"
      puts "📋 Session ID: #{@session_id}"

      case @config.protocol
      when :tcp
        start_tcp_transport
      when :udp
        start_udp_transport
      when :stdio
        start_stdio_transport
      else
        raise TransportError.new(-1, "Unsupported protocol: #{@config.protocol}")
      end

      @is_connected = true
      puts "✅ GopherTransport started successfully"
    end

    def stop
      puts "🔌 Closing GopherTransport connection"
      
      @tcp_server_thread&.kill
      @tcp_listener&.close
      @connections.each_value(&:close)
      @connections.clear
      
      @is_connected = false
      @is_destroyed = true
      
      puts "✅ GopherTransport closed successfully"
    end

    def send_message(message)
      puts "📤 Sending message: #{message[:method]} (id: #{message[:id]})"
      
      # Process through filters
      processed_message = process_through_filters(message)
      puts "✅ Message processed through filters: #{processed_message[:method]} (id: #{processed_message[:id]})"
      
      # Convert to JSON
      json_message = JSON.generate(processed_message)
      puts "✅ Message ready for transport: #{json_message}"
      
      # Send based on protocol
      case @config.protocol
      when :tcp
        send_tcp_message(json_message)
      when :udp
        send_udp_message(json_message)
      when :stdio
        send_stdio_message(json_message)
      end
      
      puts "✅ Sent message through transport"
    end

    def add_filter(filter)
      @filters << filter
      puts "✅ Added filter: #{filter.name}"
    end

    def get_stats
      {
        config: @config.to_h,
        connections: @connections.size,
        is_connected: @is_connected,
        is_destroyed: @is_destroyed,
        session_id: @session_id
      }
    end

    private

    def start_tcp_transport
      host = @config.host || '127.0.0.1'
      port = @config.port || 8080
      
      puts "📡 Starting TCP transport on #{host}:#{port}"

      if @config.host.nil?
        # Server mode - listen for connections
        @tcp_listener = TCPServer.new(host, port)
        puts "🚀 TCP server listening on port #{port}"
        puts "🔄 Starting TCP server loop to accept connections..."
        
        @tcp_server_thread = Thread.new do
          loop do
            begin
              client = @tcp_listener.accept
              client_info = client.peeraddr
              connection_id = "#{client_info[3]}:#{client_info[1]}"
              
              puts "🔗 New connection from #{connection_id}"
              @connections[connection_id] = client
              puts "✅ Connection stored, total connections: #{@connections.size}"
              
              # Handle client in a separate thread
              Thread.new(client, connection_id) do |client_socket, conn_id|
                handle_tcp_client(client_socket, conn_id)
              end
            rescue => e
              puts "❌ Error accepting connection: #{e.message}"
            end
          end
        end
      else
        # Client mode - connect to server
        client = TCPSocket.new(host, port)
        connection_id = "#{host}:#{port}"
        @connections[connection_id] = client
        puts "🔗 Connected to TCP server"
      end
    end

    def start_udp_transport
      host = @config.host || '127.0.0.1'
      port = @config.port || 8080
      
      puts "📡 Starting UDP transport on #{host}:#{port}"
      
      if @config.host.nil?
        # Server mode
        @udp_socket = UDPSocket.new
        @udp_socket.bind(host, port)
        puts "🚀 UDP server listening on port #{port}"
      else
        # Client mode
        @udp_socket = UDPSocket.new
        @udp_socket.connect(host, port)
        puts "🔗 Connected to UDP server"
      end
    end

    def start_stdio_transport
      puts "📡 Starting stdio transport"
      puts "✅ Stdio transport ready for input"
    end

    def send_tcp_message(message)
      if @config.host.nil?
        # Server mode - broadcast to all clients
        @connections.each do |connection_id, client|
          begin
            client.puts(message)
            puts "📤 Sent to client #{connection_id}"
          rescue => e
            puts "❌ Error sending to client #{connection_id}: #{e.message}"
            @connections.delete(connection_id)
          end
        end
      else
        # Client mode - send to server
        client = @connections.values.first
        client&.puts(message)
      end
    end

    def send_udp_message(message)
      @udp_socket&.send(message, 0)
    end

    def send_stdio_message(message)
      puts message
    end

    def handle_tcp_client(client_socket, connection_id)
      begin
        while line = client_socket.gets
          puts "📥 Received from #{connection_id}: #{line.chomp}"
          # Process received message
        end
      rescue => e
        puts "❌ Error handling client #{connection_id}: #{e.message}"
      ensure
        client_socket.close
        @connections.delete(connection_id)
        puts "🔌 Client #{connection_id} disconnected"
      end
    end

    def process_through_filters(message)
      processed_message = message.dup
      
      @filters.each do |filter|
        begin
          if filter.respond_to?(:process_data)
            result = filter.process_data(processed_message)
            processed_message = result if result
          end
        rescue => e
          puts "❌ Error in filter #{filter.name}: #{e.message}"
        end
      end
      
      processed_message
    end
  end
end
