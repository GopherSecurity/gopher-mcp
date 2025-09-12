# MCP Example - Ruby

This directory contains comprehensive examples demonstrating the MCP Filter SDK for Ruby.

## Examples

### Calculator Server (`lib/mcp_calculator_server.rb`)

A complete MCP server that provides calculator functionality:

```bash
ruby lib/mcp_calculator_server.rb
```

**Features:**

- Real MCP protocol implementation
- Calculator tool with arithmetic operations
- GopherTransport integration
- Error handling and validation

**Supported Operations:**

- `add` - Addition
- `subtract` - Subtraction
- `multiply` - Multiplication
- `divide` - Division
- `power` - Exponentiation
- `sqrt` - Square root
- `factorial` - Factorial

### Calculator Client (`lib/mcp_calculator_client.rb`)

A complete MCP client that connects to the calculator server:

```bash
ruby lib/mcp_calculator_client.rb
```

**Features:**

- Connects to MCP server via GopherTransport
- Performs example calculations
- Proper error handling
- Graceful connection management

### Filter Demo (`lib/filter_demo.rb`)

Demonstrates filter capabilities and chain execution:

```bash
ruby lib/filter_demo.rb
```

**Features:**

- Filter creation and configuration
- Chain execution modes
- Buffer operations
- Transport integration

## Running Examples

### Prerequisites

1. Install dependencies:

```bash
bundle install
```

2. Build the C library (if not already built):

```bash
cd ../../../
make build
```

### Calculator Server/Client

1. **Start the server:**

```bash
ruby lib/mcp_calculator_server.rb
```

2. **In another terminal, run the client:**

```bash
ruby lib/mcp_calculator_client.rb
```

### Filter Demo

```bash
ruby lib/filter_demo.rb
```

## Testing

Run the integration tests:

```bash
bundle exec rspec spec/
```

## Configuration

Examples can be configured by modifying the configuration hashes in each file:

```ruby
# Transport configuration
config = {
  protocol: :tcp,
  host: 'localhost',
  port: 8080,
  max_connections: 10,
  buffer_size: 8192
}

# Filter configuration
filter_config = {
  name: 'my-filter',
  type: :data,
  priority: 50,
  enabled: true
}
```

## Troubleshooting

### Common Issues

1. **Library not found**: Ensure the C library is built and in the correct location
2. **Connection refused**: Make sure the server is running before starting the client
3. **Permission denied**: Check file permissions and port availability

### Debug Mode

Enable debug logging by setting the `debug` option in filter configuration:

```ruby
filter_config = {
  debug: true,
  # ... other options
}
```

## Architecture

The examples follow the same architecture as the TypeScript SDK:

- **Transport Layer**: GopherTransport for network communication
- **Filter System**: CApiFilter for data processing
- **Buffer Operations**: Zero-copy buffer management
- **Error Handling**: Comprehensive error management
- **Configuration**: Flexible configuration system

## Contributing

When adding new examples:

1. Follow the existing code structure
2. Add comprehensive error handling
3. Include configuration options
4. Add tests for new functionality
5. Update this README

## License

MIT License - see LICENSE file for details.
