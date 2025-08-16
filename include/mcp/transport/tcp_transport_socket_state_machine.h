/**
 * @file tcp_transport_socket_state_machine.h
 * @brief TCP transport socket with integrated state machine
 *
 * This demonstrates how to integrate the state machine with
 * an actual transport socket implementation, following patterns
 * from production systems and MCP's existing transports.
 */

#ifndef MCP_TRANSPORT_TCP_TRANSPORT_SOCKET_STATE_MACHINE_H
#define MCP_TRANSPORT_TCP_TRANSPORT_SOCKET_STATE_MACHINE_H

#include "mcp/transport/transport_socket_state_machine.h"
#include "mcp/transport/transport_socket.h"
#include "mcp/network/connection.h"
#include "mcp/buffer.h"

namespace mcp {
namespace transport {

/**
 * TCP-specific state machine configuration
 */
struct TcpStateMachineConfig : public StateMachineConfig {
  // TCP-specific settings
  bool tcp_nodelay{true};           // Enable TCP_NODELAY
  bool keep_alive{true};            // Enable keep-alive
  uint32_t keep_alive_idle{60};     // Seconds before first probe
  uint32_t keep_alive_interval{10}; // Seconds between probes
  uint32_t keep_alive_count{3};     // Number of probes before timeout
  
  // Buffer limits
  uint32_t read_buffer_limit{65536};
  uint32_t write_buffer_limit{65536};
  
  // Retry configuration
  bool enable_auto_reconnect{false};
  std::chrono::milliseconds reconnect_delay{1000};
  uint32_t max_reconnect_attempts{3};
};

/**
 * TCP transport socket with state machine
 *
 * This class demonstrates the integration pattern:
 * - State machine manages connection lifecycle
 * - Transport socket handles actual I/O
 * - Callbacks coordinate between layers
 */
class TcpTransportSocketStateMachine : public TransportSocket,
                                       public TransportSocketStateMachine {
public:
  /**
   * Constructor for client mode
   */
  TcpTransportSocketStateMachine(network::Connection& connection,
                                 event::Dispatcher& dispatcher,
                                 TcpStateMachineConfig config);
  
  /**
   * Constructor for server mode (accepted connection)
   */
  TcpTransportSocketStateMachine(network::ConnectionSocket& socket,
                                 event::Dispatcher& dispatcher,
                                 TcpStateMachineConfig config);
  
  ~TcpTransportSocketStateMachine() override;

  // ===== TransportSocket Interface =====
  
  IoResult doRead(Buffer& buffer) override;
  IoResult doWrite(Buffer& buffer, bool end_stream) override;
  IoResult doConnect(network::ConnectionSocket& socket) override;
  IoResult doHandshake() override { return {PostIoAction::Continue, 0, false}; }
  void closeSocket(event::ConnectionCloseType type) override;
  void onConnected() override;
  
  std::string protocol() const override { return "tcp"; }
  bool canFlushClose() override { return true; }
  TransportSocketOptionsConstSharedPtr options() const override { return nullptr; }
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
  
  void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // ===== State Machine Overrides =====
  
protected:
  void onStateEnter(TransportSocketState state,
                   CompletionCallback callback) override;
  
  void onStateExit(TransportSocketState state,
                  CompletionCallback callback) override;
  
  std::unordered_set<TransportSocketState> 
  getValidTransitions(TransportSocketState from) const override;
  
  void onStateTimeout(TransportSocketState state) override;

private:
  // ===== Private Methods =====
  
  /**
   * Configure TCP socket options
   */
  void configureTcpOptions();
  
  /**
   * Handle read operation based on current state
   */
  IoResult handleRead(Buffer& buffer);
  
  /**
   * Handle write operation based on current state
   */
  IoResult handleWrite(Buffer& buffer, bool end_stream);
  
  /**
   * Initiate reconnection (if enabled)
   */
  void initiateReconnection();
  
  /**
   * Update metrics
   */
  void updateMetrics();

  // ===== Private Members =====
  
  network::Connection* connection_{nullptr};
  network::ConnectionSocket* socket_{nullptr};
  TransportSocketCallbacks* callbacks_{nullptr};
  
  TcpStateMachineConfig tcp_config_;
  
  // Buffers for data that arrives in wrong state
  BufferPtr pending_read_data_;
  BufferPtr pending_write_data_;
  
  // Reconnection state
  uint32_t reconnect_attempts_{0};
  event::TimerPtr reconnect_timer_;
  
  // Metrics
  struct Metrics {
    uint64_t bytes_read{0};
    uint64_t bytes_written{0};
    uint64_t read_errors{0};
    uint64_t write_errors{0};
    uint64_t connections{0};
    uint64_t reconnections{0};
  } metrics_;
};

/**
 * Factory for creating TCP transport sockets with state machines
 */
class TcpTransportSocketStateMachineFactory : public TransportSocketFactory,
                                              public TransportSocketStateMachineFactory {
public:
  TcpTransportSocketStateMachineFactory(TcpStateMachineConfig config)
      : config_(config) {}
  
  // TransportSocketFactory interface
  TransportSocketPtr createTransportSocket(
      TransportSocketOptionsConstSharedPtr options,
      std::shared_ptr<const Upstream::HostDescription> host) const override;
  
  bool implementsSecureTransport() const override { return false; }
  
  // TransportSocketStateMachineFactory interface
  std::unique_ptr<TransportSocketStateMachine> 
  createStateMachine(event::Dispatcher& dispatcher,
                    StateMachineConfig config) override;

private:
  TcpStateMachineConfig config_;
};

}  // namespace transport
}  // namespace mcp

#endif  // MCP_TRANSPORT_TCP_TRANSPORT_SOCKET_STATE_MACHINE_H