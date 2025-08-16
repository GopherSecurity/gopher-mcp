# MCP C++ SDK Language Bindings

This directory contains language bindings for the MCP C++ SDK, built on top of the C API layer.

## Architecture

The binding architecture follows this layered approach:

```
┌─────────────────────────────────────┐
│   Language Bindings (Python, Go,    │
│   TypeScript, Rust, Ruby, etc.)     │
└─────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────┐
│         C API (Stable ABI)          │
│      mcp_c_api.h / libmcp_c.so     │
└─────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────┐
│       MCP C++ SDK Core              │
│    Event-driven, Thread-confined    │
└─────────────────────────────────────┘
```

## Key Design Principles

### 1. Stable ABI
- C API provides a stable Application Binary Interface
- Opaque handles hide implementation details
- Version compatibility across releases
- No C++ exceptions or RTTI across boundaries

### 2. Thread Safety
- All callbacks execute in dispatcher thread
- No manual synchronization required
- Thread-confined execution model preserved

### 3. Memory Management
- Reference counting for handle lifecycle
- RAII patterns in C++ implementation
- Clear ownership semantics
- Custom allocator support

### 4. Type Safety
- Strong typing preserved across language boundaries
- FFI-friendly POD types
- Comprehensive error handling

## Supported Languages

### Python (`python/`)
- Uses `ctypes` for FFI
- Pythonic wrapper classes
- Async/await support possible
- Compatible with Python 3.6+

### TypeScript/Node.js (`typescript/`)
- N-API or node-ffi-napi based
- Promise-based async API
- EventTarget for events
- TypeScript definitions included

### Go (`go/`)
- CGO-based bindings
- Idiomatic Go interfaces
- Channel-based async patterns
- Full type safety

### Rust (`rust/`)
- Safe FFI wrappers
- Zero-cost abstractions
- Async/await integration
- Memory safety guaranteed

### Additional Languages

The C API can be easily wrapped for:
- **C#**: Using P/Invoke or CppSharp
- **Java/Kotlin**: Using JNI or JNA
- **Ruby**: Using FFI gem
- **Swift**: Using bridging headers
- **Lua**: Using Lua C API
- **PHP**: Using FFI extension

## Building the Bindings

### Prerequisites
```bash
# Install MCP C++ SDK with C API
mkdir build && cd build
cmake .. -DBUILD_C_API=ON
make
sudo make install
```

### Python
```bash
cd bindings/python
pip install -r requirements.txt
python setup.py install
```

### TypeScript/Node.js
```bash
cd bindings/typescript
npm install
npm run build
```

### Go
```bash
cd bindings/go
go mod init github.com/your-org/mcp-go
go build ./...
```

### Rust
```bash
cd bindings/rust
cargo build --release
```

## Usage Examples

### Python
```python
from mcp import Library, Dispatcher, TransportType

# Initialize library
lib = Library()
dispatcher = lib.create_dispatcher()

# Create client
client = dispatcher.create_client(TransportType.STDIO)
client.connect()
client.initialize()

# Run event loop
dispatcher.run()
```

### TypeScript
```typescript
import { MCPLibrary, TransportType } from 'mcp-sdk';

async function main() {
    const lib = new MCPLibrary();
    await lib.init();
    
    const dispatcher = lib.createDispatcher();
    const client = dispatcher.createClient({
        transport: TransportType.STDIO,
        clientInfo: {
            name: 'TypeScript Client',
            version: '1.0.0'
        }
    });
    
    await client.connect();
    await client.initialize();
    
    dispatcher.run();
}
```

### Go
```go
package main

import (
    "github.com/your-org/mcp-go"
)

func main() {
    lib, _ := mcp.Init()
    defer lib.Shutdown()
    
    dispatcher := lib.CreateDispatcher()
    client := dispatcher.CreateClient(mcp.ClientConfig{
        Transport: mcp.TransportStdio,
        ClientInfo: mcp.Implementation{
            Name: "Go Client",
            Version: "1.0.0",
        },
    })
    
    client.Connect()
    client.Initialize()
    
    dispatcher.Run()
}
```

### Rust
```rust
use mcp_sdk::{Library, TransportType};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let lib = Library::init()?;
    let dispatcher = lib.create_dispatcher()?;
    
    let client = dispatcher.create_client(
        TransportType::Stdio,
        "Rust Client",
        "1.0.0"
    )?;
    
    client.connect()?;
    client.initialize()?;
    
    dispatcher.run()?;
    Ok(())
}
```

## API Documentation

### Core Functions

#### Library Management
- `mcp_init()` - Initialize the library
- `mcp_shutdown()` - Cleanup resources
- `mcp_get_version()` - Get library version
- `mcp_get_last_error()` - Get error details

#### Event Loop
- `mcp_dispatcher_create()` - Create event dispatcher
- `mcp_dispatcher_run()` - Run event loop (blocks)
- `mcp_dispatcher_stop()` - Stop event loop
- `mcp_dispatcher_post()` - Post callback to dispatcher thread

#### Connection Management
- `mcp_connection_create_client()` - Create client connection
- `mcp_connection_connect()` - Initiate connection
- `mcp_connection_write()` - Send data
- `mcp_connection_close()` - Close connection
- `mcp_connection_set_callbacks()` - Set event callbacks

#### MCP Protocol
- `mcp_client_create()` - Create MCP client
- `mcp_client_initialize()` - Protocol handshake
- `mcp_client_send_request()` - Send request
- `mcp_client_send_notification()` - Send notification
- `mcp_server_create()` - Create MCP server
- `mcp_server_register_tool()` - Register tool
- `mcp_server_send_response()` - Send response

## Performance Considerations

- **Zero-copy where possible**: Buffers are passed by reference
- **Minimal overhead**: Thin wrapper over C++ implementation
- **Event-driven**: No polling or busy-waiting
- **Batch operations**: Multiple operations can be queued

## Thread Safety

All operations are thread-confined to the dispatcher thread:
- Callbacks always execute in dispatcher thread
- No mutexes or locks needed in user code
- Post operations from any thread to dispatcher

## Error Handling

Comprehensive error reporting:
- Result codes for all operations
- Thread-local error messages
- Callback-based error notifications
- Language-specific exception mapping

## Contributing

To add bindings for a new language:

1. Study the C API in `include/mcp/c_api/`
2. Create a new directory under `bindings/`
3. Implement FFI wrappers for your language
4. Add idiomatic abstractions
5. Include examples and tests
6. Update this README

## License

Same as MCP C++ SDK - see LICENSE file in root directory.