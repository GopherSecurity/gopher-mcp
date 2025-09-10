module McpFilterSdk
  module Types
    # Filter Status Enum
    class FilterStatus
      PENDING = :pending
      PROCESSING = :processing
      COMPLETED = :completed
      ERROR = :error
      DISABLED = :disabled
    end

    # Protocol Type Enum
    class ProtocolType
      TCP = :tcp
      UDP = :udp
      STDIO = :stdio
      HTTP = :http
      HTTPS = :https
    end

    # Filter Type Enum
    class FilterType
      DATA = :data
      CONNECTION = :connection
      ERROR = :error
      WATERMARK = :watermark
      AUTHENTICATION = :authentication
      AUTHORIZATION = :authorization
      LOGGING = :logging
      METRICS = :metrics
    end

    # Chain Execution Mode Enum
    class ChainExecutionMode
      SEQUENTIAL = :sequential
      PARALLEL = :parallel
      CONDITIONAL = :conditional
      PIPELINE = :pipeline
    end

    # Buffer Type Enum
    class BufferType
      RING = :ring
      LINEAR = :linear
      CIRCULAR = :circular
      DYNAMIC = :dynamic
    end

    # Connection State Enum
    class ConnectionState
      DISCONNECTED = :disconnected
      CONNECTING = :connecting
      CONNECTED = :connected
      DISCONNECTING = :disconnecting
      ERROR = :error
    end

    # Error Code Enum
    class ErrorCode
      SUCCESS = 0
      GENERAL_ERROR = -1
      INVALID_PARAMETER = -2
      MEMORY_ERROR = -3
      LIBRARY_LOAD_ERROR = -4
      FILTER_CREATION_ERROR = -5
      BUFFER_OPERATION_ERROR = -6
      TRANSPORT_ERROR = -7
      CONNECTION_ERROR = -8
      CHAIN_EXECUTION_ERROR = -9
    end

    # Filter Priority Levels
    class FilterPriority
      LOWEST = 0
      LOW = 25
      NORMAL = 50
      HIGH = 75
      HIGHEST = 100
    end

    # Watermark Levels
    class WatermarkLevel
      LOW = 0.25
      MEDIUM = 0.50
      HIGH = 0.75
      CRITICAL = 0.90
    end

    # Transport Configuration
    class TransportConfig
      attr_accessor :name, :version, :protocol, :host, :port
      attr_accessor :connect_timeout, :send_timeout, :receive_timeout
      attr_accessor :max_connections, :buffer_size, :filter_config

      def initialize(options = {})
        @name = options[:name] || 'mcp-transport'
        @version = options[:version] || '1.0.0'
        @protocol = options[:protocol] || ProtocolType::TCP
        @host = options[:host] || 'localhost'
        @port = options[:port] || 8080
        @connect_timeout = options[:connect_timeout] || 30000
        @send_timeout = options[:send_timeout] || 5000
        @receive_timeout = options[:receive_timeout] || 5000
        @max_connections = options[:max_connections] || 10
        @buffer_size = options[:buffer_size] || 8192
        @filter_config = options[:filter_config] || {}
      end

      def to_h
        {
          name: @name,
          version: @version,
          protocol: @protocol,
          host: @host,
          port: @port,
          connect_timeout: @connect_timeout,
          send_timeout: @send_timeout,
          receive_timeout: @receive_timeout,
          max_connections: @max_connections,
          buffer_size: @buffer_size,
          filter_config: @filter_config
        }
      end
    end

    # Filter Configuration
    class FilterConfig
      attr_accessor :name, :type, :priority, :enabled, :config_data

      def initialize(options = {})
        @name = options[:name] || 'mcp-filter'
        @type = options[:type] || FilterType::DATA
        @priority = options[:priority] || FilterPriority::NORMAL
        @enabled = options[:enabled] != false
        @config_data = options[:config_data] || {}
      end

      def to_h
        {
          name: @name,
          type: @type,
          priority: @priority,
          enabled: @enabled,
          config_data: @config_data
        }
      end
    end

    # Chain Configuration
    class ChainConfig
      attr_accessor :name, :execution_mode, :max_filters, :timeout, :enabled

      def initialize(options = {})
        @name = options[:name] || 'mcp-chain'
        @execution_mode = options[:execution_mode] || ChainExecutionMode::SEQUENTIAL
        @max_filters = options[:max_filters] || 100
        @timeout = options[:timeout] || 30000
        @enabled = options[:enabled] != false
      end

      def to_h
        {
          name: @name,
          execution_mode: @execution_mode,
          max_filters: @max_filters,
          timeout: @timeout,
          enabled: @enabled
        }
      end
    end

    # Connection Information
    class ConnectionInfo
      attr_accessor :id, :host, :port, :protocol, :connected, :created_at

      def initialize(options = {})
        @id = options[:id] || SecureRandom.uuid
        @host = options[:host] || 'localhost'
        @port = options[:port] || 8080
        @protocol = options[:protocol] || ProtocolType::TCP
        @connected = options[:connected] || false
        @created_at = options[:created_at] || Time.now
      end

      def to_h
        {
          id: @id,
          host: @host,
          port: @port,
          protocol: @protocol,
          connected: @connected,
          created_at: @created_at
        }
      end
    end

    # Error Information
    class ErrorInfo
      attr_accessor :code, :message, :source, :timestamp

      def initialize(options = {})
        @code = options[:code] || ErrorCode::GENERAL_ERROR
        @message = options[:message] || 'Unknown error'
        @source = options[:source] || 'unknown'
        @timestamp = options[:timestamp] || Time.now
      end

      def to_h
        {
          code: @code,
          message: @message,
          source: @source,
          timestamp: @timestamp
        }
      end
    end
  end
end
