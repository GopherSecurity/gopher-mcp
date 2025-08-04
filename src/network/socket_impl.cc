#include "mcp/network/socket_impl.h"

#include <algorithm>

#include "mcp/network/io_socket_handle_impl.h"
#include "mcp/network/socket_interface.h"
#include "mcp/network/socket_option_impl.h"

namespace mcp {
namespace network {

// ===== ConnectionInfoSetterImpl =====

ConnectionInfoSetterImpl::ConnectionInfoSetterImpl(
    const Address::InstanceConstSharedPtr& local_address,
    const Address::InstanceConstSharedPtr& remote_address)
    : local_address_(local_address),
      direct_local_address_(local_address),
      remote_address_(remote_address),
      direct_remote_address_(remote_address) {}

const Address::InstanceConstSharedPtr& 
ConnectionInfoSetterImpl::localAddress() const {
  return local_address_;
}

const Address::InstanceConstSharedPtr& 
ConnectionInfoSetterImpl::directLocalAddress() const {
  return direct_local_address_;
}

const Address::InstanceConstSharedPtr& 
ConnectionInfoSetterImpl::remoteAddress() const {
  return remote_address_;
}

const Address::InstanceConstSharedPtr& 
ConnectionInfoSetterImpl::directRemoteAddress() const {
  return direct_remote_address_;
}

std::string ConnectionInfoSetterImpl::requestedServerName() const {
  return requested_server_name_;
}

optional<uint64_t> ConnectionInfoSetterImpl::connectionID() const {
  return connection_id_;
}

optional<std::string> ConnectionInfoSetterImpl::interfaceName() const {
  return interface_name_;
}

optional<std::chrono::milliseconds> ConnectionInfoSetterImpl::roundTripTime() const {
  return round_trip_time_;
}

void ConnectionInfoSetterImpl::setLocalAddress(
    const Address::InstanceConstSharedPtr& address) {
  local_address_ = address;
}

void ConnectionInfoSetterImpl::restoreLocalAddress(
    const Address::InstanceConstSharedPtr& address) {
  local_address_ = address;
  local_address_restored_ = true;
}

bool ConnectionInfoSetterImpl::localAddressRestored() const {
  return local_address_restored_;
}

void ConnectionInfoSetterImpl::setRemoteAddress(
    const Address::InstanceConstSharedPtr& address) {
  remote_address_ = address;
}

void ConnectionInfoSetterImpl::setRequestedServerName(
    const std::string& server_name) {
  requested_server_name_ = server_name;
}

void ConnectionInfoSetterImpl::setConnectionID(uint64_t id) {
  connection_id_ = id;
}

void ConnectionInfoSetterImpl::setInterfaceName(const std::string& name) {
  interface_name_ = name;
}

void ConnectionInfoSetterImpl::setSslProtocol(const std::string& protocol) {
  ssl_protocol_ = protocol;
}

void ConnectionInfoSetterImpl::setSslCipherSuite(const std::string& cipher) {
  ssl_cipher_suite_ = cipher;
}

void ConnectionInfoSetterImpl::setSslPeerCertificate(const std::string& cert) {
  ssl_peer_certificate_ = cert;
}

void ConnectionInfoSetterImpl::setJA3Hash(const std::string& hash) {
  ja3_hash_ = hash;
}

void ConnectionInfoSetterImpl::setJA4Hash(const std::string& hash) {
  ja4_hash_ = hash;
}

void ConnectionInfoSetterImpl::setRoundTripTime(std::chrono::milliseconds rtt) {
  round_trip_time_ = rtt;
}

// ===== SocketImpl =====

SocketImpl::SocketImpl(IoHandlePtr io_handle,
                       const Address::InstanceConstSharedPtr& local_address,
                       const Address::InstanceConstSharedPtr& remote_address)
    : io_handle_(std::move(io_handle)),
      connection_info_provider_(
          std::make_shared<ConnectionInfoSetterImpl>(local_address, remote_address)),
      options_(std::make_shared<std::vector<SocketOptionConstSharedPtr>>()) {}

SocketImpl::~SocketImpl() {
  close();
}

ConnectionInfoSetter& SocketImpl::connectionInfoProvider() {
  return *connection_info_provider_;
}

const ConnectionInfoProvider& SocketImpl::connectionInfoProvider() const {
  return *connection_info_provider_;
}

ConnectionInfoProviderSharedPtr SocketImpl::connectionInfoProviderSharedPtr() const {
  return connection_info_provider_;
}

IoHandle& SocketImpl::ioHandle() {
  return *io_handle_;
}

const IoHandle& SocketImpl::ioHandle() const {
  return *io_handle_;
}

void SocketImpl::close() {
  if (io_handle_) {
    io_handle_->close();
  }
}

bool SocketImpl::isOpen() const {
  return io_handle_ && io_handle_->isOpen();
}

IoResult<int> SocketImpl::bind(const Address::InstanceConstSharedPtr& address) {
  if (!io_handle_) {
    return IoResult<int>::error(EBADF);
  }
  
  auto result = io_handle_->bind(address);
  if (result.ok()) {
    // Update local address
    connection_info_provider_->setLocalAddress(address);
  }
  
  return result;
}

IoResult<int> SocketImpl::listen(int backlog) {
  if (!io_handle_) {
    return IoResult<int>::error(EBADF);
  }
  
  return io_handle_->listen(backlog);
}

IoResult<int> SocketImpl::connect(const Address::InstanceConstSharedPtr& address) {
  if (!io_handle_) {
    return IoResult<int>::error(EBADF);
  }
  
  auto result = io_handle_->connect(address);
  if (result.ok()) {
    // Update remote address
    connection_info_provider_->setRemoteAddress(address);
  }
  
  return result;
}

IoResult<int> SocketImpl::setSocketOption(int level, int optname,
                                          const void* optval, socklen_t optlen) {
  if (!io_handle_) {
    return IoResult<int>::error(EBADF);
  }
  
  return io_handle_->setSocketOption(level, optname, optval, optlen);
}

IoResult<int> SocketImpl::getSocketOption(int level, int optname,
                                          void* optval, socklen_t* optlen) const {
  if (!io_handle_) {
    return IoResult<int>::error(EBADF);
  }
  
  return io_handle_->getSocketOption(level, optname, optval, optlen);
}

IoResult<int> SocketImpl::ioctl(unsigned long request, void* argp) {
  if (!io_handle_) {
    return IoResult<int>::error(EBADF);
  }
  
  return io_handle_->ioctl(request, argp);
}

void SocketImpl::addOption(const SocketOptionConstSharedPtr& option) {
  options_->push_back(option);
}

void SocketImpl::addOptions(const SocketOptionsSharedPtr& options) {
  if (options) {
    options_->insert(options_->end(), options->begin(), options->end());
  }
}

const SocketOptionsSharedPtr& SocketImpl::options() const {
  return options_;
}

SocketPtr SocketImpl::duplicate() {
  if (!io_handle_) {
    return nullptr;
  }
  
  auto new_handle = io_handle_->duplicate();
  if (!new_handle) {
    return nullptr;
  }
  
  auto new_socket = std::make_unique<SocketImpl>(
      std::move(new_handle),
      connection_info_provider_->localAddress(),
      connection_info_provider_->remoteAddress());
  
  // Copy options
  new_socket->options_ = options_;
  
  return new_socket;
}

IoResult<int> SocketImpl::setBlocking(bool blocking) {
  if (!io_handle_) {
    return IoResult<int>::error(EBADF);
  }
  
  return io_handle_->setBlocking(blocking);
}

// ===== ConnectionSocketImpl =====

ConnectionSocketImpl::ConnectionSocketImpl(
    IoHandlePtr io_handle,
    const Address::InstanceConstSharedPtr& local_address,
    const Address::InstanceConstSharedPtr& remote_address)
    : SocketImpl(std::move(io_handle), local_address, remote_address) {}

std::string ConnectionSocketImpl::requestedServerName() const {
  return connectionInfoProvider().requestedServerName();
}

void ConnectionSocketImpl::setHalfClose(bool enabled) {
  half_close_enabled_ = enabled;
}

bool ConnectionSocketImpl::isHalfClose() const {
  return half_close_enabled_;
}

ConnectionSocket::DetectedCloseType ConnectionSocketImpl::detectedCloseType() const {
  return detected_close_type_;
}

SocketType ConnectionSocketImpl::socketType() const {
  // Connection sockets are typically stream sockets
  return SocketType::Stream;
}

Address::Type ConnectionSocketImpl::addressType() const {
  if (connectionInfoProvider().localAddress()) {
    return connectionInfoProvider().localAddress()->type();
  }
  return Address::Type::Ip;
}

optional<Address::IpVersion> ConnectionSocketImpl::ipVersion() const {
  if (connectionInfoProvider().localAddress() &&
      connectionInfoProvider().localAddress()->type() == Address::Type::Ip) {
    return connectionInfoProvider().localAddress()->ip()->version();
  }
  return nullopt;
}

// ===== ListenSocketImpl =====

ListenSocketImpl::ListenSocketImpl(
    IoHandlePtr io_handle,
    const Address::InstanceConstSharedPtr& local_address)
    : SocketImpl(std::move(io_handle), local_address, nullptr) {}

void ListenSocketImpl::setListenSocketOptions(const SocketCreationOptions& options) {
  auto socket_options = buildSocketOptions(options);
  addOptions(socket_options);
}

SocketType ListenSocketImpl::socketType() const {
  // Listen sockets are typically stream sockets
  return SocketType::Stream;
}

Address::Type ListenSocketImpl::addressType() const {
  if (connectionInfoProvider().localAddress()) {
    return connectionInfoProvider().localAddress()->type();
  }
  return Address::Type::Ip;
}

optional<Address::IpVersion> ListenSocketImpl::ipVersion() const {
  if (connectionInfoProvider().localAddress() &&
      connectionInfoProvider().localAddress()->type() == Address::Type::Ip) {
    return connectionInfoProvider().localAddress()->ip()->version();
  }
  return nullopt;
}

// ===== Factory Functions =====

ConnectionSocketPtr createConnectionSocket(
    Address::Type address_type,
    const Address::InstanceConstSharedPtr& remote_address,
    const Address::InstanceConstSharedPtr& local_address,
    const SocketCreationOptions& options) {
  
  // Determine IP version if applicable
  optional<Address::IpVersion> ip_version;
  if (address_type == Address::Type::Ip) {
    if (remote_address && remote_address->type() == Address::Type::Ip) {
      ip_version = remote_address->ip()->version();
    } else if (local_address && local_address->type() == Address::Type::Ip) {
      ip_version = local_address->ip()->version();
    }
  }
  
  // Create socket
  auto socket_result = socketInterface().socket(
      SocketType::Stream, address_type, ip_version, options.v6_only);
  
  if (!socket_result.ok()) {
    return nullptr;
  }
  
  // Create IO handle
  auto io_handle = socketInterface().ioHandleForFd(
      *socket_result, options.v6_only, 
      address_type == Address::Type::Ip 
          ? optional<int>(ip_version == Address::IpVersion::v4 ? AF_INET : AF_INET6)
          : nullopt);
  
  // Create socket object
  auto socket = std::make_unique<ConnectionSocketImpl>(
      std::move(io_handle), local_address, remote_address);
  
  // Apply socket options
  auto socket_options = buildSocketOptions(options);
  socket->addOptions(socket_options);
  
  // Apply pre-bind options
  for (const auto& option : *socket_options) {
    option->setOption(*socket);
  }
  
  return socket;
}

ListenSocketPtr createListenSocket(
    const Address::InstanceConstSharedPtr& address,
    const SocketCreationOptions& options,
    bool bind_to_port) {
  
  if (!address) {
    return nullptr;
  }
  
  // Determine socket type and IP version
  Address::Type addr_type = address->type();
  optional<Address::IpVersion> ip_version;
  
  if (addr_type == Address::Type::Ip) {
    ip_version = address->ip()->version();
  }
  
  // Create socket
  auto socket_result = socketInterface().socket(
      SocketType::Stream, addr_type, ip_version, options.v6_only);
  
  if (!socket_result.ok()) {
    return nullptr;
  }
  
  // Create IO handle
  auto io_handle = socketInterface().ioHandleForFd(
      *socket_result, options.v6_only,
      addr_type == Address::Type::Ip 
          ? optional<int>(ip_version == Address::IpVersion::v4 ? AF_INET : AF_INET6)
          : nullopt);
  
  // Create socket object
  auto socket = std::make_unique<ListenSocketImpl>(
      std::move(io_handle), address);
  
  // Set socket options
  socket->setListenSocketOptions(options);
  
  // Apply pre-bind options
  for (const auto& option : *socket->options()) {
    option->setOption(*socket);
  }
  
  // Bind if requested
  if (bind_to_port) {
    auto bind_result = socket->bind(address);
    if (!bind_result.ok()) {
      return nullptr;
    }
  }
  
  return socket;
}

SocketOptionsSharedPtr createSocketOptions(const SocketCreationOptions& options) {
  return buildSocketOptions(options);
}

bool applySocketOptions(Socket& socket,
                       const SocketOptionsSharedPtr& options,
                       SocketOptionName phase) {
  if (!options) {
    return true;
  }
  
  bool success = true;
  
  for (const auto& option : *options) {
    // Apply options based on phase
    // For now, we don't distinguish phases
    success &= option->setOption(socket);
  }
  
  return success;
}

}  // namespace network
}  // namespace mcp