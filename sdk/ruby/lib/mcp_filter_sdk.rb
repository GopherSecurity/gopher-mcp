# frozen_string_literal: true

require 'mcp_filter_sdk/version'
require 'mcp_filter_sdk/mcp_c_structs'
require 'mcp_filter_sdk/mcp_ffi_bindings'
require 'mcp_filter_sdk/mcp_filter_api'
require 'mcp_filter_sdk/mcp_filter_buffer'
require 'mcp_filter_sdk/mcp_filter_chain'
require 'mcp_filter_sdk/mcp_filter_manager'
require 'mcp_filter_sdk/mcp_capifilter'
require 'mcp_filter_sdk/gopher_transport'
require 'mcp_filter_sdk/types/index'

module McpFilterSdk
  # Main entry point for the MCP Filter SDK
  class Error < StandardError; end
  class FilterError < Error; end
  class LibraryLoadError < FilterError; end
  class FilterCreationError < FilterError; end
  class BufferOperationError < FilterError; end
  class TransportError < FilterError; end
end
