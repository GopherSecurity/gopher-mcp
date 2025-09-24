# frozen_string_literal: true

require 'ffi'

module McpFilterSdk
  module CStructs
    # MCP Filter Callbacks structure
    class McpFilterCallbacks < FFI::Struct
      layout :on_data, :pointer,
             :on_write, :pointer,
             :on_new_connection, :pointer,
             :on_error, :pointer,
             :on_high_watermark, :pointer,
             :on_low_watermark, :pointer,
             :user_data, :pointer
    end

    # MCP Buffer structure
    class McpBuffer < FFI::Struct
      layout :data, :pointer,
             :size, :size_t,
             :capacity, :size_t,
             :handle, :pointer
    end

    # MCP Filter Handle
    class McpFilterHandle < FFI::Struct
      layout :handle, :pointer,
             :name, :string,
             :type, :int
    end

    # MCP Filter Manager Handle
    class McpFilterManagerHandle < FFI::Struct
      layout :handle, :pointer,
             :max_filters, :int,
             :active_filters, :int
    end

    # MCP Connection Info
    class McpConnectionInfo < FFI::Struct
      layout :id, :string,
             :host, :string,
             :port, :int,
             :protocol, :int,
             :connected, :bool
    end

    # MCP Transport Config
    class McpTransportConfig < FFI::Struct
      layout :name, :string,
             :version, :string,
             :protocol, :int,
             :host, :string,
             :port, :int,
             :connect_timeout, :int,
             :send_timeout, :int,
             :receive_timeout, :int,
             :max_connections, :int,
             :buffer_size, :int
    end

    # MCP Filter Config
    class McpFilterConfig < FFI::Struct
      layout :name, :string,
             :type, :int,
             :priority, :int,
             :enabled, :bool,
             :config_data, :pointer
    end

    # MCP Chain Config
    class McpChainConfig < FFI::Struct
      layout :name, :string,
             :execution_mode, :int,
             :max_filters, :int,
             :timeout, :int,
             :enabled, :bool
    end

    # MCP Error Info
    class McpErrorInfo < FFI::Struct
      layout :code, :int,
             :message, :string,
             :source, :string,
             :timestamp, :long_long
    end
  end
end
