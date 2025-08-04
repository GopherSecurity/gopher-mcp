#ifndef MCP_NETWORK_IO_HANDLE_H
#define MCP_NETWORK_IO_HANDLE_H

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "mcp/buffer.h"
#include "mcp/compat.h"
#include "mcp/event/event_loop.h"
#include "mcp/network/address.h"
#include "mcp/optional.h"
#include "mcp/io_result.h"

namespace mcp {
namespace network {

// Forward declarations
class IoHandle;
using IoHandlePtr = std::unique_ptr<IoHandle>;

// Platform-specific socket type
#ifdef _WIN32
using os_fd_t = uintptr_t;
constexpr os_fd_t INVALID_SOCKET_FD = static_cast<os_fd_t>(-1);
#else
using os_fd_t = int;
constexpr os_fd_t INVALID_SOCKET_FD = -1;
#endif

// Bring IoResult types into this namespace
using ::mcp::IoResult;
using ::mcp::IoCallResult;
using ::mcp::IoVoidResult;
using ::mcp::SystemError;

// UDP receive message output
struct RecvMsgOutput {
  struct ReceivedMessage {
    OwnedBuffer data;
    Address::InstanceConstSharedPtr peer_address;
    Address::InstanceConstSharedPtr local_address;
    optional<uint32_t> packets_dropped;
    bool truncated{false};
  };

  std::vector<ReceivedMessage> messages;
};

// Configuration for saving control messages
struct UdpSaveCmsgConfig {
  bool save_local_address{true};
  bool save_receive_time{false};
  bool save_packet_info{true};
};

/**
 * Abstract interface for I/O operations on sockets.
 *
 * This design provides a clean abstraction
 * over platform-specific socket operations while integrating with our
 * event loop and buffer systems.
 */
class IoHandle {
 public:
  virtual ~IoHandle() = default;

  // ===== Core I/O Operations =====

  /**
   * Read data into buffer slices (vectored I/O).
   * @param max_length Maximum bytes to read
   * @param slices Buffer slices to read into
   * @param num_slices Number of slices
   * @return Number of bytes read or error
   */
  virtual IoCallResult readv(size_t max_length,
                             RawSlice* slices,
                             size_t num_slices) = 0;

  /**
   * Read data into a buffer.
   * @param buffer Buffer to read into
   * @param max_length Maximum bytes to read (nullopt = no limit)
   * @return Number of bytes read or error
   */
  virtual IoCallResult read(Buffer& buffer,
                            optional<size_t> max_length = nullopt) = 0;

  /**
   * Write data from buffer slices (vectored I/O).
   * @param slices Buffer slices to write from
   * @param num_slices Number of slices
   * @return Number of bytes written or error
   */
  virtual IoCallResult writev(const ConstRawSlice* slices,
                              size_t num_slices) = 0;

  /**
   * Write data from a buffer.
   * @param buffer Buffer to write from
   * @return Number of bytes written or error
   */
  virtual IoCallResult write(Buffer& buffer) = 0;

  // ===== UDP Operations =====

  /**
   * Send a UDP message with control information.
   * @param slices Data to send
   * @param num_slices Number of slices
   * @param flags Send flags
   * @param self_ip Source IP to use (optional)
   * @param peer_address Destination address
   * @return Number of bytes sent or error
   */
  virtual IoCallResult sendmsg(const ConstRawSlice* slices,
                               size_t num_slices,
                               int flags,
                               const Address::Ip* self_ip,
                               const Address::Instance& peer_address) = 0;

  /**
   * Receive UDP messages with control information.
   * @param slices Buffer slices to receive into
   * @param num_slices Number of slices
   * @param self_port Local port
   * @param save_cmsg_config Configuration for control messages
   * @param output Output messages
   * @return Number of messages received or error
   */
  virtual IoCallResult recvmsg(RawSlice* slices,
                               size_t num_slices,
                               uint32_t self_port,
                               const UdpSaveCmsgConfig& save_cmsg_config,
                               RecvMsgOutput& output) = 0;

  /**
   * Receive multiple UDP messages at once (Linux recvmmsg).
   * @param slices Arrays of buffer slices
   * @param self_port Local port
   * @param save_cmsg_config Configuration for control messages
   * @param output Output messages
   * @return Number of messages received or error
   */
  virtual IoCallResult recvmmsg(std::vector<RawSlice>& slices,
                                uint32_t self_port,
                                const UdpSaveCmsgConfig& save_cmsg_config,
                                RecvMsgOutput& output) = 0;

  // ===== Socket Operations =====

  /**
   * Close the socket.
   * @return Success or error
   */
  virtual IoVoidResult close() = 0;

  /**
   * Check if the socket is open.
   */
  virtual bool isOpen() const = 0;

  /**
   * Bind to an address.
   * @param address Address to bind to
   * @return Success or error code
   */
  virtual IoResult<int> bind(
      const Address::InstanceConstSharedPtr& address) = 0;

  /**
   * Listen for connections.
   * @param backlog Maximum pending connections
   * @return Success or error code
   */
  virtual IoResult<int> listen(int backlog) = 0;

  /**
   * Accept a new connection.
   * @return New IoHandle or error
   */
  virtual IoResult<IoHandlePtr> accept() = 0;

  /**
   * Connect to an address.
   * @param address Address to connect to
   * @return Success or error code
   */
  virtual IoResult<int> connect(
      const Address::InstanceConstSharedPtr& address) = 0;

  /**
   * Shutdown the socket.
   * @param how SHUT_RD, SHUT_WR, or SHUT_RDWR
   * @return Success or error code
   */
  virtual IoResult<int> shutdown(int how) = 0;

  // ===== Socket Options =====

  /**
   * Set a socket option.
   * @param level Socket level (SOL_SOCKET, IPPROTO_TCP, etc.)
   * @param optname Option name
   * @param optval Option value
   * @param optlen Option value length
   * @return Success or error code
   */
  virtual IoResult<int> setSocketOption(int level,
                                        int optname,
                                        const void* optval,
                                        socklen_t optlen) = 0;

  /**
   * Get a socket option.
   * @param level Socket level
   * @param optname Option name
   * @param optval Buffer for option value
   * @param optlen Option value length (in/out)
   * @return Success or error code
   */
  virtual IoResult<int> getSocketOption(int level,
                                        int optname,
                                        void* optval,
                                        socklen_t* optlen) const = 0;

  /**
   * Platform-specific ioctl.
   * @param request Request code
   * @param argp Argument pointer
   * @return Success or error code
   */
  virtual IoResult<int> ioctl(unsigned long request, void* argp) = 0;

  // ===== Event Integration =====

  /**
   * Initialize file event monitoring.
   * @param dispatcher Event dispatcher
   * @param cb Callback for events
   * @param trigger Trigger type (edge/level)
   * @param events Events to monitor
   */
  virtual void initializeFileEvent(event::Dispatcher& dispatcher,
                                   event::FileReadyCb cb,
                                   event::FileTriggerType trigger,
                                   uint32_t events) = 0;

  /**
   * Activate file events (for edge-triggered mode).
   * @param events Events to activate
   */
  virtual void activateFileEvents(uint32_t events) = 0;

  /**
   * Enable file events.
   * @param events Events to enable
   */
  virtual void enableFileEvents(uint32_t events) = 0;

  /**
   * Reset file events (remove from event loop).
   */
  virtual void resetFileEvents() = 0;

  // ===== Information =====

  /**
   * Get the file descriptor.
   * Note: Should only be used for debugging/logging, not for operations.
   */
  virtual os_fd_t fd() const = 0;

  /**
   * Get local address.
   * @return Local address or error
   */
  virtual IoResult<Address::InstanceConstSharedPtr> localAddress() const = 0;

  /**
   * Get peer address.
   * @return Peer address or error
   */
  virtual IoResult<Address::InstanceConstSharedPtr> peerAddress() const = 0;

  /**
   * Get network interface name.
   * @return Interface name or nullopt
   */
  virtual optional<std::string> interfaceName() const = 0;

  /**
   * Set blocking mode (for testing).
   * @param blocking True for blocking, false for non-blocking
   * @return Success or error code
   */
  virtual IoResult<int> setBlocking(bool blocking) = 0;

  /**
   * Get the last round-trip time (for TCP).
   * @return RTT in milliseconds or nullopt
   */
  virtual optional<std::chrono::milliseconds> lastRoundTripTime() const = 0;

  /**
   * Configure initial congestion window.
   * @param bandwidth_bits_per_sec Bandwidth in bits per second
   * @param rtt Round-trip time
   */
  virtual void configureInitialCongestionWindow(
      uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt) = 0;

  /**
   * Duplicate the handle.
   * @return Duplicated handle or nullptr if not supported
   */
  virtual IoHandlePtr duplicate() = 0;
};

/**
 * Create a socket IoHandle.
 * @param fd File descriptor (INVALID_SOCKET_FD to create new)
 * @param socket_v6only True if IPv6-only socket
 * @param domain Socket domain (AF_INET, AF_INET6, AF_UNIX)
 * @return IoHandle instance
 */
IoHandlePtr createIoSocketHandle(os_fd_t fd = INVALID_SOCKET_FD,
                                 bool socket_v6only = false,
                                 optional<int> domain = nullopt);

/**
 * Create an io_uring socket handle (Linux only).
 * @param io_uring_worker io_uring worker instance
 * @param fd File descriptor
 * @param socket_v6only True if IPv6-only socket
 * @param domain Socket domain
 * @return IoHandle instance
 */
#ifdef __linux__
IoHandlePtr createIoUringSocketHandle(
    void* io_uring_worker,  // TODO: proper io_uring types
    os_fd_t fd,
    bool socket_v6only = false,
    optional<int> domain = nullopt);
#endif

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_IO_HANDLE_H