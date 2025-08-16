/**
 * @file mcp_c_bridge.h
 * @brief Internal C++ to C bridge implementation
 * 
 * This header provides the internal bridge between C++ and C APIs.
 * It contains the implementation details for converting between C and C++ types,
 * managing callbacks, and handling memory lifecycle.
 * 
 * This file is NOT part of the public API and should only be included
 * by the implementation files.
 */

#ifndef MCP_C_BRIDGE_H
#define MCP_C_BRIDGE_H

#include "mcp_c_types.h"
#include "mcp_c_api.h"

// C++ headers
#include "mcp/types.h"
#include "mcp/buffer.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/connection.h"
#include "mcp/network/listener.h"
#include "mcp/network/address_impl.h"
#include "mcp/client/mcp_client.h"
#include "mcp/server/mcp_server.h"
#include "mcp/json/json_bridge.h"

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <thread>

namespace mcp {
namespace c_api {

/* ============================================================================
 * Handle Implementations
 * ============================================================================ */

/**
 * Base class for all handle implementations
 * Provides reference counting and thread-safe destruction
 */
class HandleBase {
public:
    HandleBase() : ref_count_(1) {}
    virtual ~HandleBase() = default;
    
    void add_ref() { ref_count_++; }
    void release() {
        if (--ref_count_ == 0) {
            delete this;
        }
    }
    
private:
    std::atomic<int> ref_count_;
};

/**
 * Dispatcher implementation
 */
struct mcp_dispatcher_impl : public HandleBase {
    std::unique_ptr<mcp::event::Dispatcher> dispatcher;
    std::thread::id dispatcher_thread_id;
    bool running = false;
    
    // Timer management
    struct TimerInfo {
        std::unique_ptr<mcp::event::Timer> timer;
        mcp_timer_callback_t callback;
        void* user_data;
    };
    std::map<uint64_t, TimerInfo> timers;
    uint64_t next_timer_id = 1;
};

/**
 * Connection implementation
 */
struct mcp_connection_impl : public HandleBase {
    std::shared_ptr<mcp::network::Connection> connection;
    mcp_dispatcher_impl* dispatcher;
    
    // Callbacks
    mcp_connection_state_callback_t state_callback = nullptr;
    mcp_data_callback_t data_callback = nullptr;
    mcp_error_callback_t error_callback = nullptr;
    void* callback_user_data = nullptr;
    
    // State tracking
    mcp_connection_state_t current_state = MCP_CONNECTION_STATE_DISCONNECTED;
    
    // Statistics
    uint64_t bytes_read = 0;
    uint64_t bytes_written = 0;
};

/**
 * Listener implementation
 */
struct mcp_listener_impl : public HandleBase {
    std::unique_ptr<mcp::network::Listener> listener;
    mcp_dispatcher_impl* dispatcher;
    
    // Callbacks
    mcp_accept_callback_t accept_callback = nullptr;
    void* callback_user_data = nullptr;
};

/**
 * MCP Client implementation
 */
struct mcp_client_impl : public HandleBase {
    void* client;  // TODO: Will be properly implemented when MCPClient is available
    mcp_dispatcher_impl* dispatcher;
    mcp_connection_impl* connection = nullptr;
    
    // Callbacks
    mcp_request_callback_t request_callback = nullptr;
    mcp_response_callback_t response_callback = nullptr;
    mcp_notification_callback_t notification_callback = nullptr;
    void* callback_user_data = nullptr;
    
    // Request tracking
    std::map<mcp::RequestId, mcp_request_id_t> request_map;
    uint64_t next_request_id = 1;
};

/**
 * MCP Server implementation
 */
struct mcp_server_impl : public HandleBase {
    void* server;  // TODO: Will be properly implemented when MCPServer is available
    mcp_dispatcher_impl* dispatcher;
    mcp_listener_impl* listener = nullptr;
    
    // Callbacks
    mcp_request_callback_t request_callback = nullptr;
    mcp_notification_callback_t notification_callback = nullptr;
    void* callback_user_data = nullptr;
    
    // Registered capabilities
    std::vector<mcp::Tool> tools;
    std::vector<mcp::ResourceTemplate> resources;
    std::vector<mcp::Prompt> prompts;
};

/**
 * JSON Value implementation
 */
struct mcp_json_value_impl : public HandleBase {
    mcp::json::JsonValue value;
};

/* ============================================================================
 * Type Conversion Utilities
 * ============================================================================ */

/**
 * Convert C string to C++ string
 */
inline std::string to_cpp_string(mcp_string_t str) {
    if (str.data == nullptr) {
        return std::string();
    }
    return std::string(str.data, str.length);
}

/**
 * Convert C++ string to C string (temporary)
 */
inline mcp_string_t to_c_string_temp(const std::string& str) {
    mcp_string_t result;
    result.data = str.c_str();
    result.length = str.length();
    return result;
}

/**
 * Convert C++ optional to C optional
 */
template<typename T>
inline mcp_optional_t to_c_optional(const mcp::optional<T>& opt) {
    mcp_optional_t result;
    result.has_value = opt.has_value();
    result.value = opt.has_value() ? new T(opt.value()) : nullptr;
    return result;
}

/**
 * Convert C optional to C++ optional
 */
template<typename T>
inline mcp::optional<T> to_cpp_optional(const mcp_optional_t& opt) {
    if (opt.has_value && opt.value) {
        return mcp::optional<T>(*static_cast<T*>(opt.value));
    }
    return mcp::optional<T>();
}

/**
 * Convert MCP RequestId between C and C++
 */
inline mcp_request_id_t to_c_request_id(const mcp::RequestId& id) {
    mcp_request_id_t result;
    if (mcp::holds_alternative<std::string>(id)) {
        result.type = mcp_request_id::MCP_REQUEST_ID_STRING;
        result.value.string_value = to_c_string_temp(mcp::get<std::string>(id));
    } else {
        result.type = mcp_request_id::MCP_REQUEST_ID_NUMBER;
        result.value.number_value = mcp::get<int>(id);
    }
    return result;
}

inline mcp::RequestId to_cpp_request_id(const mcp_request_id_t& id) {
    if (id.type == mcp_request_id::MCP_REQUEST_ID_STRING) {
        return mcp::RequestId(to_cpp_string(id.value.string_value));
    } else {
        return mcp::RequestId(id.value.number_value);
    }
}

/**
 * Convert address between C and C++
 */
inline mcp::network::Address::InstanceConstSharedPtr to_cpp_address(const mcp_address_t* addr) {
    if (!addr) return nullptr;
    
    if (addr->family == MCP_AF_INET) {
        return std::make_shared<mcp::network::Address::Ipv4Instance>(
            std::string(addr->addr.inet.host),
            addr->addr.inet.port
        );
    } else if (addr->family == MCP_AF_INET6) {
        return std::make_shared<mcp::network::Address::Ipv6Instance>(
            std::string(addr->addr.inet.host),
            addr->addr.inet.port
        );
    } else if (addr->family == MCP_AF_UNIX) {
        return std::make_shared<mcp::network::Address::PipeInstance>(
            std::string(addr->addr.unix.path)
        );
    }
    return nullptr;
}

/* ============================================================================
 * Callback Bridges
 * ============================================================================ */

/**
 * Connection callbacks bridge
 */
class ConnectionCallbackBridge : public mcp::network::ConnectionCallbacks {
public:
    ConnectionCallbackBridge(mcp_connection_impl* impl) : impl_(impl) {}
    
    void onEvent(mcp::network::ConnectionEvent event) override {
        if (!impl_) return;
        
        // Map event to state change
        mcp_connection_state_t new_state = impl_->current_state;
        switch (event) {
            case mcp::network::ConnectionEvent::Connected:
            case mcp::network::ConnectionEvent::ConnectedZeroRtt:
                new_state = MCP_CONNECTION_STATE_CONNECTED;
                break;
            case mcp::network::ConnectionEvent::RemoteClose:
            case mcp::network::ConnectionEvent::LocalClose:
                new_state = MCP_CONNECTION_STATE_DISCONNECTED;
                break;
            default:
                break;
        }
        
        if (new_state != impl_->current_state && impl_->state_callback) {
            mcp_connection_state_t old_state = impl_->current_state;
            impl_->current_state = new_state;
            impl_->state_callback(
                reinterpret_cast<mcp_connection_t>(impl_),
                old_state,
                new_state,
                impl_->callback_user_data
            );
        }
    }
    
    void onAboveWriteBufferHighWatermark() override {
        // Could notify about backpressure if needed
    }
    
    void onBelowWriteBufferLowWatermark() override {
        // Could notify about flow control if needed
    }
    
private:
    mcp_connection_impl* impl_;
};

/**
 * MCP Client callbacks bridge
 */
class MCPClientCallbackBridge {
public:
    MCPClientCallbackBridge(mcp_client_impl* impl) : impl_(impl) {}
    
    void onRequest(const mcp::jsonrpc::Request& request) {
        if (!impl_ || !impl_->request_callback) return;
        
        mcp_request_id_t id = to_c_request_id(request.id);
        mcp_string_t method = to_c_string_temp(request.method);
        
        // TODO: Convert params to JSON value properly
        mcp_json_value_impl* params_impl = nullptr;
        
        impl_->request_callback(
            id,
            method,
            reinterpret_cast<mcp_json_value_t>(params_impl),
            impl_->callback_user_data
        );
    }
    
    void onResponse(const mcp::jsonrpc::Response& response) {
        if (!impl_ || !impl_->response_callback) return;
        
        mcp_request_id_t id = to_c_request_id(response.id);
        
        if (response.error.has_value()) {
            mcp_jsonrpc_error_t error;
            error.code = response.error.value().code;
            error.message = to_c_string_temp(response.error.value().message);
            // TODO: error.data = to_c_optional(response.error.value().data);
            
            impl_->response_callback(
                id,
                nullptr,
                &error,
                impl_->callback_user_data
            );
        } else if (response.result.has_value()) {
            // TODO: Convert result to JSON value properly
            mcp_json_value_impl* result_impl = nullptr;
            
            impl_->response_callback(
                id,
                reinterpret_cast<mcp_json_value_t>(result_impl),
                nullptr,
                impl_->callback_user_data
            );
        }
    }
    
    void onNotification(const mcp::jsonrpc::Notification& notification) {
        if (!impl_ || !impl_->notification_callback) return;
        
        mcp_string_t method = to_c_string_temp(notification.method);
        
        // TODO: Convert params to JSON value properly
        mcp_json_value_impl* params_impl = nullptr;
        
        impl_->notification_callback(
            method,
            reinterpret_cast<mcp_json_value_t>(params_impl),
            impl_->callback_user_data
        );
    }
    
private:
    mcp_client_impl* impl_;
};

/* ============================================================================
 * Memory Management
 * ============================================================================ */

/**
 * Global allocator instance
 */
class GlobalAllocator {
public:
    static GlobalAllocator& instance() {
        static GlobalAllocator instance;
        return instance;
    }
    
    void set_allocator(const mcp_allocator_t* allocator) {
        if (allocator) {
            allocator_ = *allocator;
            has_custom_ = true;
        } else {
            has_custom_ = false;
        }
    }
    
    void* alloc(size_t size) {
        if (has_custom_) {
            return allocator_.alloc(size, allocator_.context);
        }
        return std::malloc(size);
    }
    
    void* realloc(void* ptr, size_t size) {
        if (has_custom_) {
            return allocator_.realloc(ptr, size, allocator_.context);
        }
        return std::realloc(ptr, size);
    }
    
    void free(void* ptr) {
        if (has_custom_) {
            allocator_.free(ptr, allocator_.context);
        } else {
            std::free(ptr);
        }
    }
    
private:
    mcp_allocator_t allocator_;
    bool has_custom_ = false;
};

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/**
 * Thread-local error message storage
 */
class ErrorManager {
public:
    static void set_error(const std::string& error) {
        thread_local_error_ = error;
    }
    
    static const char* get_error() {
        return thread_local_error_.c_str();
    }
    
    static void clear_error() {
        thread_local_error_.clear();
    }
    
private:
    static thread_local std::string thread_local_error_;
};

/* ============================================================================
 * Helper Macros
 * ============================================================================ */

#define CHECK_HANDLE(handle) \
    do { \
        if (!(handle)) { \
            ErrorManager::set_error("Invalid handle"); \
            return MCP_ERROR_INVALID_ARGUMENT; \
        } \
    } while(0)

#define CHECK_HANDLE_RETURN_NULL(handle) \
    do { \
        if (!(handle)) { \
            ErrorManager::set_error("Invalid handle"); \
            return nullptr; \
        } \
    } while(0)

#define TRY_CATCH(code) \
    try { \
        code \
    } catch (const std::exception& e) { \
        ErrorManager::set_error(e.what()); \
        return MCP_ERROR; \
    } catch (...) { \
        ErrorManager::set_error("Unknown error"); \
        return MCP_ERROR; \
    }

#define TRY_CATCH_NULL(code) \
    try { \
        code \
    } catch (const std::exception& e) { \
        ErrorManager::set_error(e.what()); \
        return nullptr; \
    } catch (...) { \
        ErrorManager::set_error("Unknown error"); \
        return nullptr; \
    }

} // namespace c_api
} // namespace mcp

#endif /* MCP_C_BRIDGE_H */