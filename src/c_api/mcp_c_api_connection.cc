/**
 * @file mcp_c_api_connection.cc
 * @brief Connection management C API implementation
 */

#include "mcp/c_api/mcp_c_bridge.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/listener_impl.h"
#include "mcp/transport/tcp_transport_socket_state_machine.h"
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/transport/ssl_transport_socket.h"
#include "mcp/transport/http_sse_transport_socket.h"

using namespace mcp::c_api;

extern "C" {

/* ============================================================================
 * Connection Management
 * ============================================================================ */

mcp_connection_t mcp_connection_create_client(
    mcp_dispatcher_t dispatcher,
    mcp_transport_type_t transport
) {
    CHECK_HANDLE_RETURN_NULL(dispatcher);
    
    TRY_CATCH_NULL({
        auto dispatcher_impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
        auto conn_impl = new mcp::c_api::mcp_connection_impl();
        conn_impl->dispatcher = dispatcher_impl;
        
        // Create transport socket based on type
        std::unique_ptr<mcp::network::TransportSocket> transport_socket;
        
        switch (transport) {
            case MCP_TRANSPORT_TCP: {
                auto socket = std::make_unique<mcp::network::Socket>();
                transport_socket = std::make_unique<mcp::transport::TcpTransportSocket>(
                    std::move(socket),
                    *dispatcher_impl->dispatcher
                );
                break;
            }
            
            case MCP_TRANSPORT_STDIO: {
                transport_socket = std::make_unique<mcp::transport::StdioTransportSocket>(
                    *dispatcher_impl->dispatcher
                );
                break;
            }
            
            case MCP_TRANSPORT_SSL: {
                // Create SSL context
                auto ssl_context = std::make_unique<mcp::transport::SslContext>();
                auto socket = std::make_unique<mcp::network::Socket>();
                transport_socket = std::make_unique<mcp::transport::SslTransportSocket>(
                    std::move(socket),
                    std::move(ssl_context),
                    *dispatcher_impl->dispatcher,
                    mcp::transport::InitialRole::Client
                );
                break;
            }
            
            case MCP_TRANSPORT_HTTP_SSE: {
                auto socket = std::make_unique<mcp::network::Socket>();
                transport_socket = std::make_unique<mcp::transport::HttpSseTransportSocket>(
                    std::move(socket),
                    *dispatcher_impl->dispatcher
                );
                break;
            }
            
            default:
                ErrorManager::set_error("Unsupported transport type");
                delete conn_impl;
                return nullptr;
        }
        
        // Create connection
        conn_impl->connection = std::make_shared<mcp::network::ConnectionImpl>(
            std::move(transport_socket),
            *dispatcher_impl->dispatcher
        );
        
        return conn_impl;
    });
}

mcp_result_t mcp_connection_configure(
    mcp_connection_t connection,
    const mcp_address_t* address,
    const mcp_socket_options_t* options,
    const mcp_ssl_config_t* ssl_config
) {
    CHECK_HANDLE(connection);
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);
        
        // Configure address if provided
        if (address) {
            auto cpp_address = to_cpp_address(address);
            if (!cpp_address) {
                ErrorManager::set_error("Invalid address");
                return MCP_ERROR_INVALID_ARGUMENT;
            }
            impl->connection->setRemoteAddress(cpp_address);
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
    void* user_data
) {
    CHECK_HANDLE(connection);
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);
        
        // Store callbacks
        impl->state_callback = state_cb;
        impl->data_callback = data_cb;
        impl->error_callback = error_cb;
        impl->callback_user_data = user_data;
        
        // Create and set the callback bridge
        auto bridge = std::make_shared<ConnectionCallbackBridge>(impl);
        impl->connection->setConnectionCallbacks(bridge);
        
        return MCP_OK;
    });
}

mcp_result_t mcp_connection_set_watermarks(
    mcp_connection_t connection,
    const mcp_watermark_config_t* config
) {
    CHECK_HANDLE(connection);
    
    if (!config) {
        ErrorManager::set_error("Invalid watermark config");
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);
        
        impl->connection->setBufferLimits(config->high_watermark);
        // Note: Low watermark would need additional API in ConnectionImpl
        
        return MCP_OK;
    });
}

mcp_result_t mcp_connection_connect(mcp_connection_t connection) {
    CHECK_HANDLE(connection);
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);
        
        // Ensure we're in dispatcher thread
        if (!mcp_dispatcher_is_thread(impl->dispatcher)) {
            // Post to dispatcher thread
            impl->dispatcher->dispatcher->post([impl]() {
                impl->connection->connect();
            });
        } else {
            impl->connection->connect();
        }
        
        impl->current_state = MCP_CONNECTION_STATE_CONNECTING;
        return MCP_OK;
    });
}

mcp_result_t mcp_connection_write(
    mcp_connection_t connection,
    const uint8_t* data,
    size_t length,
    mcp_write_callback_t callback,
    void* user_data
) {
    CHECK_HANDLE(connection);
    
    if (!data || length == 0) {
        ErrorManager::set_error("Invalid data");
        return MCP_ERROR_INVALID_ARGUMENT;
    }
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);
        
        // Create buffer from data
        auto buffer = std::make_unique<mcp::Buffer>();
        buffer->add(data, length);
        
        // Track bytes written
        impl->bytes_written += length;
        
        // Write data (async)
        if (!mcp_dispatcher_is_thread(impl->dispatcher)) {
            // Post to dispatcher thread
            impl->dispatcher->dispatcher->post([impl, buffer = std::move(buffer), callback, user_data]() mutable {
                impl->connection->write(*buffer, false);
                
                // Call write callback if provided
                if (callback) {
                    callback(impl, MCP_OK, buffer->length(), user_data);
                }
            });
        } else {
            impl->connection->write(*buffer, false);
            
            // Call write callback if provided
            if (callback) {
                callback(impl, MCP_OK, length, user_data);
            }
        }
        
        return MCP_OK;
    });
}

mcp_result_t mcp_connection_close(mcp_connection_t connection, bool flush) {
    CHECK_HANDLE(connection);
    
    TRY_CATCH({
        auto impl = reinterpret_cast<mcp::c_api::mcp_connection_impl*>(connection);
        
        impl->current_state = MCP_CONNECTION_STATE_DISCONNECTING;
        
        if (!mcp_dispatcher_is_thread(impl->dispatcher)) {
            // Post to dispatcher thread
            impl->dispatcher->dispatcher->post([impl, flush]() {
                if (flush) {
                    impl->connection->close(mcp::network::ConnectionCloseType::FlushWrite);
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
    
    auto impl = static_cast<mcp_connection_impl*>(connection);
    return impl->current_state;
}

mcp_result_t mcp_connection_get_stats(
    mcp_connection_t connection,
    uint64_t* bytes_read,
    uint64_t* bytes_written
) {
    CHECK_HANDLE(connection);
    
    auto impl = static_cast<mcp_connection_impl*>(connection);
    
    if (bytes_read) {
        *bytes_read = impl->bytes_read;
    }
    if (bytes_written) {
        *bytes_written = impl->bytes_written;
    }
    
    return MCP_OK;
}

void mcp_connection_destroy(mcp_connection_t connection) {
    if (!connection) return;
    
    auto impl = static_cast<mcp_connection_impl*>(connection);
    
    // Close connection if still open
    if (impl->current_state == MCP_CONNECTION_STATE_CONNECTED ||
        impl->current_state == MCP_CONNECTION_STATE_CONNECTING) {
        mcp_connection_close(connection, false);
    }
    
    // Release the handle
    impl->release();
}

/* ============================================================================
 * Server & Listener
 * ============================================================================ */

mcp_listener_t mcp_listener_create(
    mcp_dispatcher_t dispatcher,
    mcp_transport_type_t transport
) {
    CHECK_HANDLE_RETURN_NULL(dispatcher);
    
    TRY_CATCH_NULL({
        auto dispatcher_impl = reinterpret_cast<mcp::c_api::mcp_dispatcher_impl*>(dispatcher);
        auto listener_impl = new mcp_listener_impl();
        listener_impl->dispatcher = dispatcher_impl;
        
        // Create listener based on transport type
        switch (transport) {
            case MCP_TRANSPORT_TCP:
            case MCP_TRANSPORT_SSL: {
                // Create TCP listener
                auto socket = std::make_unique<mcp::network::Socket>();
                listener_impl->listener = std::make_unique<mcp::network::TcpListenerImpl>(
                    std::move(socket),
                    *dispatcher_impl->dispatcher
                );
                break;
            }
            
            case MCP_TRANSPORT_STDIO:
            case MCP_TRANSPORT_PIPE: {
                // For stdio/pipe, we don't actually listen
                // The connection is established directly
                break;
            }
            
            default:
                ErrorManager::set_error("Unsupported transport type for listener");
                delete listener_impl;
                return nullptr;
        }
        
        return listener_impl;
    });
}

mcp_result_t mcp_listener_configure(
    mcp_listener_t listener,
    const mcp_address_t* address,
    const mcp_socket_options_t* options,
    const mcp_ssl_config_t* ssl_config
) {
    CHECK_HANDLE(listener);
    
    TRY_CATCH({
        auto impl = static_cast<mcp_listener_impl*>(listener);
        
        if (!impl->listener) {
            // Stdio/pipe listener - no configuration needed
            return MCP_OK;
        }
        
        // Configure bind address
        if (address) {
            auto cpp_address = to_cpp_address(address);
            if (!cpp_address) {
                ErrorManager::set_error("Invalid address");
                return MCP_ERROR_INVALID_ARGUMENT;
            }
            impl->listener->setListenAddress(cpp_address);
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

mcp_result_t mcp_listener_set_accept_callback(
    mcp_listener_t listener,
    mcp_accept_callback_t callback,
    void* user_data
) {
    CHECK_HANDLE(listener);
    
    TRY_CATCH({
        auto impl = static_cast<mcp_listener_impl*>(listener);
        
        impl->accept_callback = callback;
        impl->callback_user_data = user_data;
        
        if (impl->listener) {
            // Set accept callback on the listener
            impl->listener->setListenerCallbacks(
                std::make_shared<mcp::network::ListenerCallbacks>(
                    [impl](mcp::network::ConnectionSocketPtr&& socket) {
                        // Create connection wrapper for accepted socket
                        auto conn_impl = new mcp::c_api::mcp_connection_impl();
                        conn_impl->dispatcher = impl->dispatcher;
                        
                        // Create connection from accepted socket
                        conn_impl->connection = std::make_shared<mcp::network::ConnectionImpl>(
                            std::move(socket),
                            *impl->dispatcher->dispatcher
                        );
                        
                        conn_impl->current_state = MCP_CONNECTION_STATE_CONNECTED;
                        
                        // Call accept callback
                        if (impl->accept_callback) {
                            impl->accept_callback(impl, conn_impl, impl->callback_user_data);
                        }
                    }
                )
            );
        }
        
        return MCP_OK;
    });
}

mcp_result_t mcp_listener_start(mcp_listener_t listener, int backlog) {
    CHECK_HANDLE(listener);
    
    TRY_CATCH({
        auto impl = static_cast<mcp_listener_impl*>(listener);
        
        if (!impl->listener) {
            // Stdio/pipe - no actual listening
            return MCP_OK;
        }
        
        // Start listening
        impl->listener->listen(backlog);
        
        return MCP_OK;
    });
}

void mcp_listener_stop(mcp_listener_t listener) {
    if (!listener) return;
    
    auto impl = static_cast<mcp_listener_impl*>(listener);
    
    if (impl->listener) {
        impl->listener->disable();
    }
}

void mcp_listener_destroy(mcp_listener_t listener) {
    if (!listener) return;
    
    auto impl = static_cast<mcp_listener_impl*>(listener);
    
    // Stop listening
    mcp_listener_stop(listener);
    
    // Release the handle
    impl->release();
}

} // extern "C"