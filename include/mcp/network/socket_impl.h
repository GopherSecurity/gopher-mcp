#ifndef MCP_NETWORK_SOCKET_IMPL_H
#define MCP_NETWORK_SOCKET_IMPL_H

#include "mcp/network/socket.h"

namespace mcp {
namespace network {

/**
 * Implementation of ConnectionInfoSetter
 */
class ConnectionInfoSetterImpl : public ConnectionInfoSetter {
 public:
  ConnectionInfoSetterImpl(
      const Address::InstanceConstSharedPtr& local_address,
      const Address::InstanceConstSharedPtr& remote_address);

  // ConnectionInfoProvider interface
  const Address::InstanceConstSharedPtr& localAddress() const override;
  const Address::InstanceConstSharedPtr& directLocalAddress() const override;
  const Address::InstanceConstSharedPtr& remoteAddress() const override;
  const Address::InstanceConstSharedPtr& directRemoteAddress() const override;
  std::string requestedServerName() const override;
  optional<uint64_t> connectionID() const override;
  optional<std::string> interfaceName() const override;
  std::string sslProtocol() const override { return ssl_protocol_; }
  std::string sslCipherSuite() const override { return ssl_cipher_suite_; }
  std::string sslPeerCertificate() const override {
    return ssl_peer_certificate_;
  }
  std::string ja3Hash() const override { return ja3_hash_; }
  std::string ja4Hash() const override { return ja4_hash_; }
  optional<std::chrono::milliseconds> roundTripTime() const override;

  // ConnectionInfoSetter interface
  void setLocalAddress(const Address::InstanceConstSharedPtr& address) override;
  void restoreLocalAddress(
      const Address::InstanceConstSharedPtr& address) override;
  bool localAddressRestored() const override;
  void setRemoteAddress(
      const Address::InstanceConstSharedPtr& address) override;
  void setRequestedServerName(const std::string& server_name) override;
  void setConnectionID(uint64_t id) override;
  void setInterfaceName(const std::string& interface_name) override;
  void setSslProtocol(const std::string& protocol) override;
  void setSslCipherSuite(const std::string& cipher) override;
  void setSslPeerCertificate(const std::string& cert) override;
  void setJA3Hash(const std::string& hash) override;
  void setJA4Hash(const std::string& hash) override;
  void setRoundTripTime(std::chrono::milliseconds rtt) override;

 private:
  Address::InstanceConstSharedPtr local_address_;
  Address::InstanceConstSharedPtr direct_local_address_;
  Address::InstanceConstSharedPtr remote_address_;
  Address::InstanceConstSharedPtr direct_remote_address_;
  std::string requested_server_name_;
  optional<uint64_t> connection_id_;
  optional<std::string> interface_name_;
  std::string ssl_protocol_;
  std::string ssl_cipher_suite_;
  std::string ssl_peer_certificate_;
  std::string ja3_hash_;
  std::string ja4_hash_;
  optional<std::chrono::milliseconds> round_trip_time_;
  bool local_address_restored_{false};
};

/**
 * Base socket implementation
 */
class SocketImpl : public Socket {
 public:
  SocketImpl(IoHandlePtr io_handle,
             const Address::InstanceConstSharedPtr& local_address,
             const Address::InstanceConstSharedPtr& remote_address);
  ~SocketImpl() override;

  // Socket interface
  ConnectionInfoSetter& connectionInfoProvider() override;
  const ConnectionInfoProvider& connectionInfoProvider() const override;
  ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr()
      const override;
  IoHandle& ioHandle() override;
  const IoHandle& ioHandle() const override;
  void close() override;
  bool isOpen() const override;
  IoResult<int> bind(const Address::InstanceConstSharedPtr& address) override;
  IoResult<int> listen(int backlog) override;
  IoResult<int> connect(
      const Address::InstanceConstSharedPtr& address) override;
  IoResult<int> setSocketOption(int level,
                                int optname,
                                const void* optval,
                                socklen_t optlen) override;
  IoResult<int> getSocketOption(int level,
                                int optname,
                                void* optval,
                                socklen_t* optlen) const override;
  IoResult<int> ioctl(unsigned long request, void* argp) override;
  void addOption(const SocketOptionConstSharedPtr& option) override;
  void addOptions(const SocketOptionsSharedPtr& options) override;
  const SocketOptionsSharedPtr& options() const override;
  SocketPtr duplicate() override;
  IoResult<int> setBlocking(bool blocking) override;

 protected:
  IoHandlePtr io_handle_;
  std::shared_ptr<ConnectionInfoSetterImpl> connection_info_provider_;
  SocketOptionsSharedPtr options_;
};

/**
 * Connection socket implementation
 */
class ConnectionSocketImpl : public ConnectionSocket {
 public:
  ConnectionSocketImpl(IoHandlePtr io_handle,
                       const Address::InstanceConstSharedPtr& local_address,
                       const Address::InstanceConstSharedPtr& remote_address);

  // ConnectionSocket interface
  std::string requestedServerName() const override;
  void setHalfClose(bool enabled) override;
  bool isHalfClose() const override;
  DetectedCloseType detectedCloseType() const override;

  // Socket interface
  SocketType socketType() const override;
  Address::Type addressType() const override;
  optional<Address::IpVersion> ipVersion() const override;

  // Socket implementation methods
  ConnectionInfoSetter& connectionInfoProvider() override;
  const ConnectionInfoProvider& connectionInfoProvider() const override;
  ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr()
      const override;
  IoHandle& ioHandle() override;
  const IoHandle& ioHandle() const override;
  void close() override;
  bool isOpen() const override;
  IoResult<int> bind(const Address::InstanceConstSharedPtr& address) override;
  IoResult<int> listen(int backlog) override;
  IoResult<int> connect(
      const Address::InstanceConstSharedPtr& address) override;
  IoResult<int> setSocketOption(int level,
                                int optname,
                                const void* optval,
                                socklen_t optlen) override;
  IoResult<int> getSocketOption(int level,
                                int optname,
                                void* optval,
                                socklen_t* optlen) const override;
  IoResult<int> ioctl(unsigned long request, void* argp) override;
  void addOption(const SocketOptionConstSharedPtr& option) override;
  void addOptions(const SocketOptionsSharedPtr& options) override;
  const SocketOptionsSharedPtr& options() const override;
  SocketPtr duplicate() override;
  IoResult<int> setBlocking(bool blocking) override;

 private:
  IoHandlePtr io_handle_;
  std::shared_ptr<ConnectionInfoSetterImpl> connection_info_provider_;
  SocketOptionsSharedPtr options_;
  bool half_close_enabled_{false};
  DetectedCloseType detected_close_type_{DetectedCloseType::Normal};
};

/**
 * Listen socket implementation
 */
class ListenSocketImpl : public ListenSocket {
 public:
  ListenSocketImpl(IoHandlePtr io_handle,
                   const Address::InstanceConstSharedPtr& local_address);

  // ListenSocket interface
  void setListenSocketOptions(const SocketCreationOptions& options) override;

  // Socket interface
  SocketType socketType() const override;
  Address::Type addressType() const override;
  optional<Address::IpVersion> ipVersion() const override;

  // Socket implementation methods
  ConnectionInfoSetter& connectionInfoProvider() override;
  const ConnectionInfoProvider& connectionInfoProvider() const override;
  ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr()
      const override;
  IoHandle& ioHandle() override;
  const IoHandle& ioHandle() const override;
  void close() override;
  bool isOpen() const override;
  IoResult<int> bind(const Address::InstanceConstSharedPtr& address) override;
  IoResult<int> listen(int backlog) override;
  IoResult<int> connect(
      const Address::InstanceConstSharedPtr& address) override;
  IoResult<int> setSocketOption(int level,
                                int optname,
                                const void* optval,
                                socklen_t optlen) override;
  IoResult<int> getSocketOption(int level,
                                int optname,
                                void* optval,
                                socklen_t* optlen) const override;
  IoResult<int> ioctl(unsigned long request, void* argp) override;
  void addOption(const SocketOptionConstSharedPtr& option) override;
  void addOptions(const SocketOptionsSharedPtr& options) override;
  const SocketOptionsSharedPtr& options() const override;
  SocketPtr duplicate() override;
  IoResult<int> setBlocking(bool blocking) override;

 private:
  IoHandlePtr io_handle_;
  std::shared_ptr<ConnectionInfoSetterImpl> connection_info_provider_;
  SocketOptionsSharedPtr options_;
};

}  // namespace network
}  // namespace mcp

#endif  // MCP_NETWORK_SOCKET_IMPL_H