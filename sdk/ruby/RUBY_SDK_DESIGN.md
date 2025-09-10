# Ruby SDK Design for MCP Filter C API

## Overview

This document outlines the design and architecture for the Ruby SDK that mirrors the TypeScript SDK structure and functionality. The Ruby SDK will provide a native Ruby interface to the MCP Filter C API using FFI bindings, following the same patterns and structure as the TypeScript implementation.

## Directory Structure

```
sdk/ruby/
├── lib/
│   ├── mcp_filter_sdk/
│   │   ├── version.rb
│   │   ├── mcp_c_structs.rb
│   │   ├── mcp_ffi_bindings.rb
│   │   ├── mcp_filter_api.rb
│   │   ├── mcp_filter_buffer.rb
│   │   ├── mcp_filter_chain.rb
│   │   ├── mcp_filter_manager.rb
│   │   ├── gopher_transport.rb
│   │   └── types/
│   │       ├── filters/
│   │       │   └── index.rb
│   │       ├── chains/
│   │       │   └── index.rb
│   │       ├── buffers/
│   │       │   └── index.rb
│   │       ├── mcp_types.rb
│   │       └── index.rb
│   └── mcp_filter_sdk.rb
├── spec/
│   ├── mcp_capifilter_spec.rb
│   ├── mcp_filter_api_spec.rb
│   ├── mcp_filter_buffer_spec.rb
│   ├── mcp_filter_chain_spec.rb
│   ├── mcp_filter_manager_spec.rb
│   ├── mcp_gopher_transport_integration_spec.rb
│   └── mcp_end_to_end_spec.rb
├── mcp-example/
│   ├── lib/
│   │   ├── filter_demo.rb
│   │   ├── gopher_transport.rb
│   │   ├── mcp_calculator_client.rb
│   │   └── mcp_calculator_server.rb
│   ├── spec/
│   │   └── integration_spec.rb
│   ├── Gemfile
│   │   └── Gemfile.lock
│   ├── Rakefile
│   ├── README.md
│   └── examples/
│       ├── calculator_client.rb
│       ├── calculator_server.rb
│       └── filter_demo.rb
├── Gemfile
├── Gemfile.lock
├── Rakefile
├── README.md
├── .gitignore
├── .rspec
├── .rubocop.yml
└── mcp_filter_sdk.gemspec
```

## Core Components

### 1. FFI Bindings (`mcp_ffi_bindings.rb`)

```ruby
require 'ffi'

module McpFilterSdk
  module FfiBindings
    extend FFI::Library

    # Load the C library
    ffi_lib File.join(File.dirname(__FILE__), '../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib')

    # Define C function bindings
    attach_function :mcp_init, [:pointer], :int
    attach_function :mcp_cleanup, [], :void
    attach_function :mcp_filter_create, [:pointer, :pointer, :pointer], :int
    attach_function :mcp_filter_destroy, [:pointer], :void
    # ... more bindings
  end
end
```

### 2. C Structs (`mcp_c_structs.rb`)

```ruby
module McpFilterSdk
  module CStructs
    class McpFilterCallbacks < FFI::Struct
      layout :on_data, :pointer,
             :on_write, :pointer,
             :on_new_connection, :pointer,
             :on_error, :pointer,
             :on_high_watermark, :pointer,
             :on_low_watermark, :pointer,
             :user_data, :pointer
    end

    class McpBuffer < FFI::Struct
      layout :data, :pointer,
             :size, :size_t,
             :capacity, :size_t
    end

    # ... more structs
  end
end
```

### 3. Filter API (`mcp_filter_api.rb`)

```ruby
module McpFilterSdk
  class FilterManager
    include FfiBindings

    def initialize
      @handle = nil
      @callbacks = {}
    end

    def create_filter(name, callbacks, config = {})
      # Implementation using FFI bindings
    end

    def destroy_filter(filter_handle)
      # Implementation
    end

    def process_data(data, filter_handle)
      # Implementation
    end
  end
end
```

### 4. Buffer Operations (`mcp_filter_buffer.rb`)

```ruby
module McpFilterSdk
  class FilterBuffer
    include FfiBindings

    def initialize(capacity = 4096)
      @buffer = create_buffer(capacity)
    end

    def add_data(data)
      # Zero-copy buffer operations
    end

    def get_contiguous_data
      # Get contiguous data from buffer
    end

    def clear
      # Clear buffer
    end

    private

    def create_buffer(capacity)
      # Create buffer using C API
    end
  end
end
```

### 5. Filter Chain (`mcp_filter_chain.rb`)

```ruby
module McpFilterSdk
  class FilterChain
    def initialize(name, execution_mode = :sequential)
      @name = name
      @execution_mode = execution_mode
      @filters = []
    end

    def add_filter(filter)
      @filters << filter
    end

    def execute(data)
      case @execution_mode
      when :sequential
        execute_sequential(data)
      when :parallel
        execute_parallel(data)
      when :conditional
        execute_conditional(data)
      end
    end

    private

    def execute_sequential(data)
      # Sequential execution logic
    end

    def execute_parallel(data)
      # Parallel execution logic
    end

    def execute_conditional(data)
      # Conditional execution logic
    end
  end
end
```

### 6. Gopher Transport (`gopher_transport.rb`)

```ruby
module McpFilterSdk
  class GopherTransport
    PROTOCOL_TYPES = {
      tcp: 'tcp',
      udp: 'udp',
      stdio: 'stdio'
    }.freeze

    def initialize(config)
      @config = config
      @connections = {}
      @filters = []
    end

    def start
      case @config[:protocol]
      when :tcp
        start_tcp_transport
      when :udp
        start_udp_transport
      when :stdio
        start_stdio_transport
      end
    end

    def send_message(message)
      # Process through filters and send
    end

    def add_filter(filter)
      @filters << filter
    end

    private

    def start_tcp_transport
      # TCP server implementation
    end

    def start_udp_transport
      # UDP implementation
    end

    def start_stdio_transport
      # Stdio implementation
    end
  end
end
```

## Type System

### MCP Types (`types/mcp_types.rb`)

```ruby
module McpFilterSdk
  module Types
    class FilterStatus
      PENDING = :pending
      PROCESSING = :processing
      COMPLETED = :completed
      ERROR = :error
    end

    class ProtocolType
      TCP = :tcp
      UDP = :udp
      STDIO = :stdio
    end

    class FilterType
      DATA = :data
      CONNECTION = :connection
      ERROR = :error
      WATERMARK = :watermark
    end

    # ... more types
  end
end
```

## Testing Framework

### RSpec Configuration (`.rspec`)

```
--format documentation
--color
--require spec_helper
```

### Test Structure

```ruby
# spec/mcp_capifilter_spec.rb
require 'spec_helper'

RSpec.describe McpFilterSdk::CApiFilter do
  describe '#initialize' do
    it 'creates a filter with valid callbacks' do
      # Test implementation
    end
  end

  describe '#process_data' do
    it 'processes data through filter chain' do
      # Test implementation
    end
  end
end
```

## Example Implementation

### Calculator Server (`mcp-example/lib/mcp_calculator_server.rb`)

```ruby
require 'mcp_filter_sdk'
require 'json'

class McpCalculatorServer
  def initialize
    @transport = McpFilterSdk::GopherTransport.new({
      protocol: :tcp,
      host: 'localhost',
      port: 8080
    })

    @filter = create_calculator_filter
    @transport.add_filter(@filter)
  end

  def start
    puts "🧮 Starting MCP Calculator Server with Ruby SDK"
    @transport.start

    loop do
      # Handle incoming connections and messages
    end
  end

  private

  def create_calculator_filter
    callbacks = {
      on_data: method(:handle_calculator_request),
      on_error: method(:handle_error),
      on_new_connection: method(:handle_new_connection)
    }

    McpFilterSdk::CApiFilter.new('calculator-filter', callbacks)
  end

  def handle_calculator_request(data)
    # Process calculator operations
  end
end
```

### Calculator Client (`mcp-example/lib/mcp_calculator_client.rb`)

```ruby
require 'mcp_filter_sdk'
require 'json'

class McpCalculatorClient
  def initialize
    @transport = McpFilterSdk::GopherTransport.new({
      protocol: :tcp,
      host: 'localhost',
      port: 8080
    })

    @filter = create_client_filter
    @transport.add_filter(@filter)
  end

  def connect
    @transport.start
  end

  def calculate(operation, a, b)
    message = {
      method: 'calculate',
      params: {
        operation: operation,
        a: a,
        b: b
      }
    }

    @transport.send_message(message)
  end

  private

  def create_client_filter
    callbacks = {
      on_data: method(:handle_response),
      on_error: method(:handle_error)
    }

    McpFilterSdk::CApiFilter.new('client-filter', callbacks)
  end

  def handle_response(data)
    # Handle server response
  end
end
```

## Gemspec Configuration

```ruby
# mcp_filter_sdk.gemspec
Gem::Specification.new do |spec|
  spec.name          = "mcp_filter_sdk"
  spec.version       = McpFilterSdk::VERSION
  spec.authors       = ["MCP Team"]
  spec.email         = ["team@mcp.dev"]

  spec.summary       = "Ruby SDK for MCP Filter C API"
  spec.description   = "Native Ruby interface to MCP Filter C API with FFI bindings"
  spec.homepage      = "https://github.com/modelcontextprovider/gopher-mcp"
  spec.license       = "MIT"

  spec.files         = Dir["lib/**/*.rb"]
  spec.require_paths = ["lib"]

  spec.add_dependency "ffi", "~> 1.15"
  spec.add_dependency "json", "~> 2.6"

  spec.add_development_dependency "rspec", "~> 3.12"
  spec.add_development_dependency "rubocop", "~> 1.50"
  spec.add_development_dependency "rake", "~> 13.0"
end
```

## Build System

### Rakefile

```ruby
require 'rake'
require 'rspec/core/rake_task'

RSpec::Core::RakeTask.new(:spec)

task :default => :spec

desc "Run all tests"
task :test => :spec

desc "Run linter"
task :lint do
  sh "rubocop lib spec"
end

desc "Build gem"
task :build do
  sh "gem build mcp_filter_sdk.gemspec"
end
```

## Integration with C++ Library

### Library Loading

```ruby
module McpFilterSdk
  module LibraryLoader
    def self.load_library
      lib_path = find_library_path
      FFI::Library.new(lib_path)
    end

    private

    def self.find_library_path
      # Search for libgopher_mcp_c.0.1.0.dylib in build directory
      possible_paths = [
        File.join(__dir__, '../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dylib'),
        File.join(__dir__, '../../../../build/src/c_api/libgopher_mcp_c.0.1.0.so'),
        File.join(__dir__, '../../../../build/src/c_api/libgopher_mcp_c.0.1.0.dll')
      ]

      possible_paths.find { |path| File.exist?(path) } || raise "C library not found"
    end
  end
end
```

## Error Handling

```ruby
module McpFilterSdk
  class FilterError < StandardError
    attr_reader :code, :message

    def initialize(code, message)
      @code = code
      @message = message
      super("#{code}: #{message}")
    end
  end

  class LibraryLoadError < FilterError; end
  class FilterCreationError < FilterError; end
  class BufferOperationError < FilterError; end
  class TransportError < FilterError; end
end
```

## Performance Considerations

1. **Zero-copy operations**: Use FFI pointers for direct memory access
2. **Connection pooling**: Reuse connections for better performance
3. **Async processing**: Use Ruby threads for concurrent operations
4. **Memory management**: Proper cleanup of FFI resources

## Testing Strategy

1. **Unit tests**: Individual component testing with RSpec
2. **Integration tests**: End-to-end testing with real C library
3. **Performance tests**: Benchmark critical operations
4. **Memory tests**: Ensure no memory leaks

## Documentation

- **API documentation**: YARD documentation for all public methods
- **Examples**: Comprehensive examples in `mcp-example/`
- **Integration guide**: Step-by-step integration instructions
- **Performance guide**: Optimization recommendations

This design ensures the Ruby SDK maintains feature parity with the TypeScript SDK while leveraging Ruby's strengths in metaprogramming, dynamic typing, and elegant syntax.
