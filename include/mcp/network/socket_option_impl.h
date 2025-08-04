#ifndef MCP_NETWORK_SOCKET_OPTION_IMPL_H
#define MCP_NETWORK_SOCKET_OPTION_IMPL_H

#include <memory>
#include <string>
#include <vector>

#include "mcp/compat.h"
#include "mcp/network/socket.h"

namespace mcp {
namespace network {

/**
 * Base socket option implementation.
 * 
 * Provides common functionality for socket options.
 */
class SocketOptionImpl : public SocketOption {
public:
  SocketOptionImpl(const SocketOptionName& optname,
                   const void* value,
                   size_t size);
  
  // SocketOption interface
  bool setOption(Socket& socket) const override;
  bool setOptionForListen(Socket& socket) const override;
  void hashKey(std::vector<uint8_t>& key) const override;
  std::string toString() const override;
  bool isSupported() const override;
  
protected:
  SocketOptionName optname_;
  std::vector<uint8_t> value_;
};

/**
 * Socket option for boolean values.
 */
class BoolSocketOption : public SocketOptionImpl {
public:
  BoolSocketOption(const SocketOptionName& optname, bool value)
      : SocketOptionImpl(optname, &value, sizeof(int)) {
    // Convert bool to int for setsockopt
    int int_value = value ? 1 : 0;
    value_.assign(reinterpret_cast<const uint8_t*>(&int_value),
                  reinterpret_cast<const uint8_t*>(&int_value) + sizeof(int));
  }
};

/**
 * Socket option for integer values.
 */
class IntSocketOption : public SocketOptionImpl {
public:
  IntSocketOption(const SocketOptionName& optname, int value)
      : SocketOptionImpl(optname, &value, sizeof(int)) {}
};

/**
 * Socket option that applies at different phases.
 */
class MultiPhaseSocketOption : public SocketOption {
public:
  MultiPhaseSocketOption(SocketOptionConstSharedPtr pre_bind,
                         SocketOptionConstSharedPtr post_listen)
      : pre_bind_(pre_bind), post_listen_(post_listen) {}
  
  bool setOption(Socket& socket) const override {
    return pre_bind_ ? pre_bind_->setOption(socket) : true;
  }
  
  bool setOptionForListen(Socket& socket) const override {
    return post_listen_ ? post_listen_->setOptionForListen(socket) : true;
  }
  
  void hashKey(std::vector<uint8_t>& key) const override {
    if (pre_bind_) pre_bind_->hashKey(key);
    if (post_listen_) post_listen_->hashKey(key);
  }
  
  std::string toString() const override {
    std::string result;
    if (pre_bind_) {
      result += "pre_bind: " + pre_bind_->toString();
    }
    if (post_listen_) {
      if (!result.empty()) result += ", ";
      result += "post_listen: " + post_listen_->toString();
    }
    return result;
  }
  
  bool isSupported() const override {
    return (!pre_bind_ || pre_bind_->isSupported()) &&
           (!post_listen_ || post_listen_->isSupported());
  }
  
private:
  SocketOptionConstSharedPtr pre_bind_;
  SocketOptionConstSharedPtr post_listen_;
};

/**
 * Socket option for IP_TRANSPARENT (Linux).
 */
class IpTransparentSocketOption : public BoolSocketOption {
public:
  explicit IpTransparentSocketOption(bool enabled = true);
  
  bool setOption(Socket& socket) const override;
};

/**
 * Socket option for IP_FREEBIND (Linux).
 */
class IpFreebindSocketOption : public BoolSocketOption {
public:
  explicit IpFreebindSocketOption(bool enabled = true);
};

/**
 * Socket option for SO_REUSEPORT (with BPF support on Linux).
 */
class ReusePortSocketOption : public BoolSocketOption {
public:
  explicit ReusePortSocketOption(bool enabled = true);
  
  /**
   * Set BPF program for REUSEPORT_CBPF (Linux).
   * @param prog BPF program
   * @param prog_len Program length
   */
  void setBpfProgram(const void* prog, size_t prog_len);
  
  bool setOptionForListen(Socket& socket) const override;
  
private:
  std::vector<uint8_t> bpf_program_;
};

/**
 * Socket option for TCP keepalive settings.
 */
class TcpKeepaliveSocketOption : public SocketOption {
public:
  struct KeepaliveSettings {
    optional<bool> enabled;
    optional<int> idle_time_s;     // TCP_KEEPIDLE
    optional<int> interval_s;       // TCP_KEEPINTVL
    optional<int> probes;          // TCP_KEEPCNT
  };
  
  explicit TcpKeepaliveSocketOption(const KeepaliveSettings& settings)
      : settings_(settings) {}
  
  bool setOption(Socket& socket) const override;
  bool setOptionForListen(Socket& socket) const override { (void)socket; return true; }
  void hashKey(std::vector<uint8_t>& key) const override;
  std::string toString() const override;
  bool isSupported() const override;
  
private:
  KeepaliveSettings settings_;
};

/**
 * Socket option for receive/send buffer sizes.
 */
class BufferSizeSocketOption : public IntSocketOption {
public:
  enum BufferType {
    Receive,  // SO_RCVBUF
    Send      // SO_SNDBUF
  };
  
  BufferSizeSocketOption(BufferType type, int size);
};

/**
 * Socket option for marking packets (SO_MARK on Linux).
 */
class MarkSocketOption : public IntSocketOption {
public:
  explicit MarkSocketOption(uint32_t mark);
};

/**
 * Socket option for Type of Service (IP_TOS).
 */
class TosSocketOption : public SocketOption {
public:
  explicit TosSocketOption(uint8_t tos) : tos_(tos) {}
  
  bool setOption(Socket& socket) const override;
  bool setOptionForListen(Socket& socket) const override { (void)socket; return true; }
  void hashKey(std::vector<uint8_t>& key) const override;
  std::string toString() const override;
  bool isSupported() const override;
  
private:
  uint8_t tos_;
};

/**
 * Create socket options from SocketCreationOptions.
 */
SocketOptionsSharedPtr buildSocketOptions(const SocketCreationOptions& options);

/**
 * Common socket option names
 */
#define MCP_MAKE_SOCKET_OPTION_NAME(level, option) \
  SocketOptionName(level, option, #level "/" #option)

// Define platform-specific socket options
#ifdef SO_REUSEADDR
extern const SocketOptionName SOCKET_SO_REUSEADDR;
#else
extern const SocketOptionName SOCKET_SO_REUSEADDR;
#endif

#ifdef SO_REUSEPORT
extern const SocketOptionName SOCKET_SO_REUSEPORT;
#else
extern const SocketOptionName SOCKET_SO_REUSEPORT;
#endif

#ifdef SO_KEEPALIVE
extern const SocketOptionName SOCKET_SO_KEEPALIVE;
#else
extern const SocketOptionName SOCKET_SO_KEEPALIVE;
#endif

#ifdef TCP_NODELAY
extern const SocketOptionName SOCKET_TCP_NODELAY;
#else
extern const SocketOptionName SOCKET_TCP_NODELAY;
#endif

#ifdef IP_TRANSPARENT
extern const SocketOptionName SOCKET_IP_TRANSPARENT;
#else
extern const SocketOptionName SOCKET_IP_TRANSPARENT;
#endif

#ifdef IPV6_V6ONLY
extern const SocketOptionName SOCKET_IPV6_V6ONLY;
#else
extern const SocketOptionName SOCKET_IPV6_V6ONLY;
#endif

// Additional socket options
extern const SocketOptionName SOCKET_SO_RCVBUF;
extern const SocketOptionName SOCKET_SO_SNDBUF;
extern const SocketOptionName SOCKET_SO_MARK;
extern const SocketOptionName SOCKET_IP_FREEBIND;
extern const SocketOptionName SOCKET_IPV6_TRANSPARENT;
extern const SocketOptionName SOCKET_TCP_KEEPIDLE;
extern const SocketOptionName SOCKET_TCP_KEEPINTVL;
extern const SocketOptionName SOCKET_TCP_KEEPCNT;
extern const SocketOptionName SOCKET_IP_TOS;

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_SOCKET_OPTION_IMPL_H