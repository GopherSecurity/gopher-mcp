/**
 * @file mcp_c_api_connection.cc
 * @brief Connection management C API implementation
 * Uses RAII for memory safety.
 */

#include "mcp/c_api/mcp_c_api.h"
#include "mcp/c_api/mcp_c_bridge.h"
#include "mcp/c_api/mcp_raii.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/server_listener_impl.h"
#include "mcp/transport/http_sse_transport_socket.h"
#include "mcp/transport/ssl_transport_socket.h"
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/transport/tcp_transport_socket_state_machine.h"

using namespace mcp::c_api;

extern "C" {

/* ============================================================================
 * Connection Management
 * ============================================================================
 */

mcp_connection_t mcp_connection_create_client_ex(
    mcp_dispatcher_t dispatcher,
    const mcp_transport_config_t* transport_config) MCP_NOEXCEPT {
  CHECK_HANDLE_VALID_NULL(dispatcher);

  TRY_WITH_RAII_NULL({
    auto dispatcher_impl =
        reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
    auto conn_impl = std::make_unique<mcp::c_api::mcp_connection_impl>(dispatcher_impl);

    // Use default configuration if not provided
    mcp_transport_config_t default_config = {};
    if (!transport_config) {
      // TODO: Get default configuration from MCP capabilities
      // Note: These defaults should come from MCP protocol negotiation
      default_config.type = MCP_TRANSPORT_HTTP_SSE;
      default_config.connect_timeout_ms = 30000;
      default_config.idle_timeout_ms = 0;
      default_config.enable_keepalive = true;
      transport_config = &default_config;
    }

    // Create transport socket based on configuration
    std::unique_ptr<mcp::network::TransportSocket> transport_socket;

    switch (transport_config->type) {
      case MCP_TRANSPORT_HTTP_SSE: {
        // TODO: Full implementation of HTTP+SSE configuration
        // Note: Use configuration from transport_config
        mcp::transport::HttpSseTransportSocketConfig http_config;
        if (transport_config->config.http_sse.http_headers) {
          // TODO: Parse and set HTTP headers
        }
        if (transport_config->config.http_sse.retry_delay_ms > 0) {
          // TODO: Set retry delay
        }
        transport_socket =
            std::make_unique<mcp::transport::HttpSseTransportSocket>(
                http_config, *dispatcher_impl->dispatcher,
                nullptr  // no filter manager for now
            );
        break;
      }

      case MCP_TRANSPORT_STDIO: {
        // TODO: Full implementation of stdio configuration
        // Note: Use configuration from transport_config
        mcp::transport::StdioTransportSocketConfig stdio_config;
        if (transport_config->config.stdio.stdin_fd >= 0) {
          // TODO: Set custom stdin fd
        }
        if (transport_config->config.stdio.stdout_fd >= 0) {
          // TODO: Set custom stdout fd
        }
        transport_socket =
            std::make_unique<mcp::transport::StdioTransportSocket>(
                stdio_config);
        break;
      }

      case MCP_TRANSPORT_PIPE: {
        // TODO: Implement PIPE transport socket wrapper
        // Note: PIPE transport needs proper platform-specific handling
        transport_socket = nullptr;
        break;
      }

      default:
        ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, "Unsupported transport type");
        return nullptr;
    }

    // Create socket and connection
    // TODO: Create proper socket based on transport type
    // Note: SocketImpl is abstract, need concrete implementation
    // For now, connection will create its own socket
    mcp::network::SocketSharedPtr socket = nullptr;

    // TODO: Create connection with proper socket
    // Note: Need to create socket based on transport type
    // For now, pass nullptr and let connection handle it
    conn_impl->connection = std::make_shared<mcp::network::ConnectionImpl>(
        *dispatcher_impl->dispatcher,
        nullptr,  // Socket will be created on connect
        std::move(transport_socket),
        false  // Not connected yet
    );

    return reinterpret_cast<mcp_connection_t>(conn_impl.release());
  });
}

mcp_connection_t mcp_connection_create_client(mcp_dispatcher_t dispatcher,
                                              mcp_transport_type_t transport) {
  // Legacy function - create with default configuration
  mcp_transport_config_t config = {};
  config.type = transport;
  config.connect_timeout_ms = 30000;
  config.idle_timeout_ms = 0;
  config.enable_keepalive = true;

  // Set transport-specific defaults
  switch (transport) {
    case MCP_TRANSPORT_HTTP_SSE:
      config.config.http_sse.retry_delay_ms = 1000;
      config.config.http_sse.max_retries = 3;
      break;
    case MCP_TRANSPORT_STDIO:
      config.config.stdio.stdin_fd = -1;   // Use default
      config.config.stdio.stdout_fd = -1;  // Use default
      config.config.stdio.stderr_fd = -1;  // Use default
      break;
    case MCP_TRANSPORT_PIPE:
      // Pipe transport defaults
      break;
    default:
      break;
  }

  return mcp_connection_create_client_ex(dispatcher, &config);
}

mcp_result_t mcp_connection_configure(mcp_connection_t connection,
                                      const mcp_address_t* address,
                                      const mcp_socket_options_t* options,
                                      const mcp_ssl_config_t* ssl_config) {
  CHECK_HANDLE_VALID(connection);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);

    // Configure address if provided
    if (address) {
      auto cpp_address = to_cpp_address_safe(address);
      if (!cpp_address) {
        ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, "Invalid address");
        return MCP_ERROR_INVALID_ARGUMENT;
      }
      // Store address for connection
      impl->remote_address = cpp_address;
    }

    // Configure socket options if provided
    if (options) {
      // Apply socket options through connection
      // This would need extension of the ConnectionImpl API
      // For now, we store them for later use
    }

    // Configure SSL if provided
    if (ssl_config) {
      // Configure SSL through transport socket
      // This would need access to the SSL transport socket
    }

    return MCP_OK;
  });
}

mcp_result_t mcp_connection_set_callbacks(
    mcp_connection_t connection,
    mcp_connection_state_callback_t state_cb,
    mcp_data_callback_t data_cb,
    mcp_error_callback_t error_cb,
    void* user_data) {
  CHECK_HANDLE_VALID(connection);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);

    // Store callbacks
    impl->state_callback = state_cb;
    impl->data_callback = data_cb;
    impl->error_callback = error_cb;
    impl->callback_user_data = user_data;

    // Create and set the callback bridge
    impl->callback_bridge = std::make_unique<mcp::c_api::mcp_connection_impl::CallbackBridge>(impl);
    impl->connection->addConnectionCallbacks(*impl->callback_bridge);

    return MCP_OK;
  });
}

mcp_result_t mcp_connection_set_watermarks(
    mcp_connection_t connection, const mcp_watermark_config_t* config) {
  CHECK_HANDLE_VALID(connection);

  if (!config) {
    ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, "Invalid watermark config");
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);

    impl->connection->setBufferLimits(config->high_watermark);
    // Note: Low watermark would need additional API in ConnectionImpl

    return MCP_OK;
  });
}

mcp_result_t mcp_connection_connect(mcp_connection_t connection) {
  CHECK_HANDLE_VALID(connection);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);

    // Ensure we're in dispatcher thread
    if (!mcp_dispatcher_is_thread(
            reinterpret_cast<mcp_dispatcher_t>(impl->dispatcher))) {
      // Post to dispatcher thread
      impl->dispatcher->dispatcher->post([impl]() {
        // Cast to ClientConnection and connect
        auto client_conn =
            std::dynamic_pointer_cast<mcp::network::ClientConnection>(
                impl->connection);
        if (client_conn) {
          client_conn->connect();
        }
      });
    } else {
      // Cast to ClientConnection and connect
      auto client_conn =
          std::dynamic_pointer_cast<mcp::network::ClientConnection>(
              impl->connection);
      if (client_conn) {
        client_conn->connect();
      }
    }

    impl->current_state = MCP_CONNECTION_STATE_CONNECTING;
    return MCP_OK;
  });
}

mcp_result_t mcp_connection_write(mcp_connection_t connection,
                                  const uint8_t* data,
                                  size_t length,
                                  mcp_write_callback_t callback,
                                  void* user_data) {
  CHECK_HANDLE_VALID(connection);

  if (!data || length == 0) {
    ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, "Invalid data");
    return MCP_ERROR_INVALID_ARGUMENT;
  }

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);

    // TODO: Create buffer properly
    // Note: Buffer is abstract, need BufferImpl or OwnedImpl
    // For now, use a simpler approach
    mcp::Buffer* buffer = nullptr;  // Will need proper buffer implementation

    // Track bytes written
    impl->bytes_written += length;

    // TODO: Implement proper async write with buffer
    // Note: Need to create BufferImpl and handle async dispatch
    // For now, simplified implementation without actual write

    // Call write callback if provided
    if (callback) {
      callback(reinterpret_cast<mcp_connection_t>(impl), MCP_OK, length,
               user_data);
    }

    return MCP_OK;
  });
}

mcp_result_t mcp_connection_close(mcp_connection_t connection, mcp_bool_t flush) MCP_NOEXCEPT {
  CHECK_HANDLE_VALID(connection);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);

    impl->current_state = MCP_CONNECTION_STATE_CLOSING;

    if (!mcp_dispatcher_is_thread(
            reinterpret_cast<mcp_dispatcher_t>(impl->dispatcher))) {
      // Post to dispatcher thread
      impl->dispatcher->dispatcher->post([impl, flush]() {
        if (flush) {
          impl->connection->close(
              mcp::network::ConnectionCloseType::FlushWrite);
        } else {
          impl->connection->close(mcp::network::ConnectionCloseType::NoFlush);
        }
      });
    } else {
      if (flush) {
        impl->connection->close(mcp::network::ConnectionCloseType::FlushWrite);
      } else {
        impl->connection->close(mcp::network::ConnectionCloseType::NoFlush);
      }
    }

    return MCP_OK;
  });
}

mcp_connection_state_t mcp_connection_get_state(mcp_connection_t connection) {
  if (!connection) {
    return MCP_CONNECTION_STATE_ERROR;
  }

  auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);
  return impl->current_state;
}

mcp_result_t mcp_connection_get_stats(mcp_connection_t connection,
                                      uint64_t* bytes_read,
                                      uint64_t* bytes_written) {
  CHECK_HANDLE_VALID(connection);

  auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);

  if (bytes_read) {
    *bytes_read = impl->bytes_read;
  }
  if (bytes_written) {
    *bytes_written = impl->bytes_written;
  }

  return MCP_OK;
}

void mcp_connection_destroy(mcp_connection_t connection) MCP_NOEXCEPT {
  if (!connection)
    return;

  auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);

  // Close connection if still open
  if (impl->current_state == MCP_CONNECTION_STATE_CONNECTED ||
      impl->current_state == MCP_CONNECTION_STATE_CONNECTING) {
    mcp_connection_close(connection, false);
  }

  // Release the handle
  impl->Release();
}

/* ============================================================================
 * Server & Listener
 * ============================================================================
 */

mcp_listener_t mcp_listener_create(mcp_dispatcher_t dispatcher,
                                   mcp_transport_type_t transport) {
  CHECK_HANDLE_VALID_NULL(dispatcher);

  TRY_WITH_RAII_NULL({
    auto dispatcher_impl =
        reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
    auto listener_impl = std::make_unique<mcp::c_api::mcp_listener_impl>(dispatcher_impl);

    // Create listener based on transport type
    switch (transport) {
      case MCP_TRANSPORT_HTTP_SSE: {
        // TODO: Create HTTP/SSE listener with proper socket
        // Note: Socket is abstract, need platform-specific implementation
        // The actual listener and socket should be created when configured with
        // address
        listener_impl->listener = nullptr;  // Will be created in configure
        break;
      }

      case MCP_TRANSPORT_STDIO:
      case MCP_TRANSPORT_PIPE: {
        // For stdio/pipe, we don't actually listen
        // The connection is established directly
        break;
      }

      default:
        ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, "Unsupported transport type for listener");
        return nullptr;
    }

    return reinterpret_cast<mcp_listener_t>(listener_impl.release());
  });
}

mcp_result_t mcp_listener_configure(mcp_listener_t listener,
                                    const mcp_address_t* address,
                                    const mcp_socket_options_t* options,
                                    const mcp_ssl_config_t* ssl_config) {
  CHECK_HANDLE_VALID(listener);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_listener_impl*>(listener);

    if (!impl->listener) {
      // Stdio/pipe listener - no configuration needed
      return MCP_OK;
    }

    // Configure bind address
    if (address) {
      auto cpp_address = to_cpp_address_safe(address);
      if (!cpp_address) {
        ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, "Invalid address");
        return MCP_ERROR_INVALID_ARGUMENT;
      }
      // Store address for later binding
      // The actual listener would be configured with this address
    }

    // Configure socket options if provided
    if (options) {
      // Apply socket options
      // This would need extension of the Listener API
    }

    // Store SSL config for accepted connections
    if (ssl_config) {
      // This would be used when accepting connections
    }

    return MCP_OK;
  });
}

mcp_result_t mcp_listener_set_accept_callback(mcp_listener_t listener,
                                              mcp_accept_callback_t callback,
                                              void* user_data) {
  CHECK_HANDLE_VALID(listener);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_listener_impl*>(listener);

    impl->accept_callback = callback;
    impl->callback_user_data = user_data;

    if (impl->listener) {
      // TODO: Set accept callback on the listener
      // Note: ListenerCallbacks interface needs proper implementation
      // The callback should:
      // 1. Accept new connections
      // 2. Create ConnectionImpl with accepted socket
      // 3. Configure transport based on server config
      // 4. Call user's accept callback

      // For now, store callbacks for later use
      // impl->listener->setListenerCallbacks(...);
    }

    return MCP_OK;
  });
}

mcp_result_t mcp_listener_start(mcp_listener_t listener, int backlog) {
  CHECK_HANDLE_VALID(listener);

  TRY_WITH_RAII({
    auto impl = reinterpret_cast<mcp::c_api::mcp_listener_impl*>(listener);

    if (!impl->listener) {
      // Stdio/pipe - no actual listening
      return MCP_OK;
    }

    // Start listening - simplified for C API
    if (impl->listener) {
      auto result = impl->listener->listen();
      // TODO: Handle VoidResult properly
      // Note: VoidResult is variant<nullptr_t, Error>
      // Check if result holds an error
      if (mcp::holds_alternative<mcp::Error>(result)) {
        auto& error = mcp::get<mcp::Error>(result);
        ErrorManager::SetError(MCP_ERROR_INVALID_ARGUMENT, error.message);
        return MCP_ERROR_UNKNOWN;
      }
    }

    return MCP_OK;
  });
}

void mcp_listener_stop(mcp_listener_t listener) MCP_NOEXCEPT {
  if (!listener)
    return;

  auto impl = reinterpret_cast<mcp::c_api::mcp_listener_impl*>(listener);

  if (impl->listener) {
    impl->listener->disable();
  }
}

void mcp_listener_destroy(mcp_listener_t listener) MCP_NOEXCEPT {
  if (!listener)
    return;

  auto impl = reinterpret_cast<mcp::c_api::mcp_listener_impl*>(listener);

  // Stop listening
  mcp_listener_stop(listener);

  // Release the handle
  impl->Release();
}

}  // extern "C"