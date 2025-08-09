/**
 * @file echo_transport_advanced.h
 * @brief Transport abstraction interface for echo client/server
 */

#pragma once

#include "mcp/types.h"
#include "mcp/core/result.h"
#include <string>
#include <functional>
#include <memory>

namespace mcp {
namespace echo {

// Simple success type for transport operations
struct Success {};

/**
 * @brief Advanced transport interface for echo communication
 * 
 * This interface allows echo client and server to work with different
 * transport mechanisms (stdio, TCP, HTTP, WebSocket, etc.) transparently.
 * 
 * This is the advanced interface used by advanced echo examples with
 * sophisticated features like circuit breakers, flow control, and metrics.
 * For basic usage, see EchoTransportBase in echo_basic.h
 */
class EchoTransportAdvanced {
public:
  enum class Status {
    Disconnected,
    Connecting,
    Connected,
    Error
  };
  
  /**
   * @brief Callbacks for transport events
   */
  struct Callbacks {
    std::function<void(const std::string& data)> onDataReceived;
    std::function<void(Status status)> onStatusChange;
    std::function<void(const Error& error)> onError;
  };
  
  virtual ~EchoTransportAdvanced() = default;
  
  /**
   * @brief Initialize the transport
   * @return Success or error
   */
  virtual variant<Success, Error> initialize() = 0;
  
  /**
   * @brief Connect to a remote endpoint (for clients)
   * @param endpoint Transport-specific endpoint (e.g., "stdio", "tcp://host:port", "http://url")
   * @return Success or error
   */
  virtual variant<Success, Error> connect(const std::string& endpoint) = 0;
  
  /**
   * @brief Listen on an endpoint (for servers)
   * @param endpoint Transport-specific endpoint
   * @return Success or error
   */
  virtual variant<Success, Error> listen(const std::string& endpoint) = 0;
  
  /**
   * @brief Send data through the transport
   * @param data Data to send
   * @return Success or error
   */
  virtual variant<Success, Error> send(const std::string& data) = 0;
  
  /**
   * @brief Close the transport connection
   */
  virtual void close() = 0;
  
  /**
   * @brief Get current transport status
   * @return Current status
   */
  virtual Status getStatus() const = 0;
  
  /**
   * @brief Set callbacks for transport events
   * @param callbacks Event callbacks
   */
  virtual void setCallbacks(const Callbacks& callbacks) = 0;
  
  /**
   * @brief Check if transport supports bidirectional communication
   * @return true if bidirectional, false otherwise
   */
  virtual bool isBidirectional() const = 0;
  
  /**
   * @brief Get transport type name
   * @return Transport type (e.g., "stdio", "tcp", "http")
   */
  virtual std::string getTransportType() const = 0;
};

using EchoTransportPtr = std::shared_ptr<EchoTransportAdvanced>;

} // namespace echo
} // namespace mcp