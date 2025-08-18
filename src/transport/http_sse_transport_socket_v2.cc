/**
 * @file http_sse_transport_socket_v2.cc
 * @brief HTTP+SSE Transport Socket implementation following Envoy's architecture
 *
 * This implementation provides a clean transport layer that:
 * - Handles raw I/O operations only
 * - Delegates protocol processing to filter chain
 * - Manages underlying transport (TCP/SSL/STDIO)
 * - Maintains clear layer separation
 */

#include "mcp/transport/http_sse_transport_socket_v2.h"
#include "mcp/transport/tcp_transport_socket.h"
#include "mcp/transport/ssl_transport_socket.h"
#include "mcp/transport/stdio_transport_socket.h"
#include "mcp/filter/http_codec_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection_impl.h"
#include "mcp/network/address_impl.h"

namespace mcp {
namespace transport {

// ===== HttpSseTransportSocketV2 Implementation =====

HttpSseTransportSocketV2::HttpSseTransportSocketV2(
    const HttpSseTransportSocketConfigV2& config,
    event::Dispatcher& dispatcher,
    std::unique_ptr<network::FilterChain> filter_chain)
    : config_(config),
      dispatcher_(dispatcher),
      filter_chain_(std::move(filter_chain)),
      last_activity_time_(std::chrono::steady_clock::now()) {
  
  initialize();
}

HttpSseTransportSocketV2::~HttpSseTransportSocketV2() {
  // Cancel all timers
  if (connect_timer_) {
    connect_timer_->disableTimer();
  }
  if (idle_timer_) {
    idle_timer_->disableTimer();
  }
  
  // Close underlying transport if still open
  if (underlying_transport_ && connected_) {
    underlying_transport_->closeSocket(network::ConnectionEvent::LocalClose);
  }
}

void HttpSseTransportSocketV2::initialize() {
  // Create underlying transport based on configuration
  underlying_transport_ = createUnderlyingTransport();
  
  // Create timers
  connect_timer_ = dispatcher_.createTimer([this]() { onConnectTimeout(); });
  idle_timer_ = dispatcher_.createTimer([this]() { onIdleTimeout(); });
}

std::unique_ptr<network::TransportSocket> 
HttpSseTransportSocketV2::createUnderlyingTransport() {
  switch (config_.underlying_transport) {
    case HttpSseTransportSocketConfigV2::UnderlyingTransport::TCP: {
      // Create TCP transport socket
      TcpTransportSocketConfig tcp_config;
      // Parse server address
      auto colon_pos = config_.server_address.find(':');
      if (colon_pos != std::string::npos) {
        tcp_config.host = config_.server_address.substr(0, colon_pos);
        tcp_config.port = static_cast<uint16_t>(
            std::stoi(config_.server_address.substr(colon_pos + 1)));
      }
      return std::make_unique<TcpTransportSocket>(tcp_config, dispatcher_);
    }
    
    case HttpSseTransportSocketConfigV2::UnderlyingTransport::SSL: {
      // Create SSL transport socket over TCP
      if (!config_.ssl_config) {
        throw std::runtime_error("SSL configuration required for SSL transport");
      }
      
      SslTransportSocketConfig ssl_config;
      ssl_config.verify_peer = config_.ssl_config->verify_peer;
      if (config_.ssl_config->ca_cert_path) {
        ssl_config.ca_cert = *config_.ssl_config->ca_cert_path;
      }
      if (config_.ssl_config->client_cert_path) {
        ssl_config.cert_chain = *config_.ssl_config->client_cert_path;
      }
      if (config_.ssl_config->client_key_path) {
        ssl_config.private_key = *config_.ssl_config->client_key_path;
      }
      if (config_.ssl_config->sni_hostname) {
        ssl_config.sni = *config_.ssl_config->sni_hostname;
      }
      
      // Create TCP transport first
      auto tcp_transport = createUnderlyingTransport();  // Recursive with TCP
      return std::make_unique<SslTransportSocket>(
          ssl_config, dispatcher_, std::move(tcp_transport));
    }
    
    case HttpSseTransportSocketConfigV2::UnderlyingTransport::STDIO: {
      // Create STDIO transport socket
      StdioTransportSocketConfig stdio_config;
      return std::make_unique<StdioTransportSocket>(stdio_config, dispatcher_);
    }
    
    default:
      throw std::runtime_error("Unknown underlying transport type");
  }
}

void HttpSseTransportSocketV2::setTransportSocketCallbacks(
    network::TransportSocketCallbacks& callbacks) {
  assertInDispatcherThread();
  callbacks_ = &callbacks;
  
  // Set callbacks on underlying transport if it exists
  if (underlying_transport_) {
    underlying_transport_->setTransportSocketCallbacks(callbacks);
  }
}

void HttpSseTransportSocketV2::setFilterChain(
    std::unique_ptr<network::FilterChain> filter_chain) {
  assertInDispatcherThread();
  filter_chain_ = std::move(filter_chain);
}

bool HttpSseTransportSocketV2::canFlushClose() {
  // Can flush close if write buffer is empty
  return write_buffer_.length() == 0;
}

VoidResult HttpSseTransportSocketV2::connect(network::Socket& socket) {
  assertInDispatcherThread();
  
  if (connected_ || connecting_) {
    return VoidResult::Failure("Already connected or connecting");
  }
  
  connecting_ = true;
  stats_.connect_attempts++;
  
  // Start connect timer
  startConnectTimer();
  
  // Initiate connection on underlying transport
  if (underlying_transport_) {
    auto result = underlying_transport_->connect(socket);
    if (!result.success) {
      connecting_ = false;
      cancelConnectTimer();
      failure_reason_ = "Underlying transport connect failed: " + result.error;
      return result;
    }
  }
  
  return VoidResult::Success();
}

void HttpSseTransportSocketV2::closeSocket(network::ConnectionEvent event) {
  assertInDispatcherThread();
  
  if (closing_) {
    return;  // Already closing
  }
  
  closing_ = true;
  connected_ = false;
  connecting_ = false;
  
  // Cancel all timers
  cancelConnectTimer();
  cancelIdleTimer();
  
  // Notify filter chain of close
  if (filter_chain_) {
    filter_chain_->onConnectionEvent(event);
  }
  
  // Close underlying transport
  if (underlying_transport_) {
    underlying_transport_->closeSocket(event);
  }
  
  // Notify callbacks
  if (callbacks_) {
    callbacks_->raiseEvent(event);
  }
}

TransportIoResult HttpSseTransportSocketV2::doRead(Buffer& buffer) {
  assertInDispatcherThread();
  
  if (!connected_) {
    return TransportIoResult{
        TransportIoResult::Type::Error,
        0,
        TransportIoResult::PostIoAction::Close,
        "Not connected"
    };
  }
  
  // Reset idle timer on activity
  resetIdleTimer();
  last_activity_time_ = std::chrono::steady_clock::now();
  
  // Read from underlying transport
  TransportIoResult result{TransportIoResult::Type::Success, 0};
  
  if (underlying_transport_) {
    // Read into our internal buffer first
    result = underlying_transport_->doRead(read_buffer_);
    
    if (result.type == TransportIoResult::Type::Error) {
      failure_reason_ = result.error_details.value_or("Read error");
      return result;
    }
    
    stats_.bytes_received += result.bytes_processed;
  }
  
  // Process through filter chain if we have data
  if (read_buffer_.length() > 0 && filter_chain_) {
    result = processFilterChainRead(buffer);
  } else {
    // No filter chain, pass through directly
    buffer.move(read_buffer_);
    result.bytes_processed = buffer.length();
  }
  
  return result;
}

TransportIoResult HttpSseTransportSocketV2::doWrite(
    Buffer& buffer, bool end_stream) {
  assertInDispatcherThread();
  
  if (!connected_) {
    return TransportIoResult{
        TransportIoResult::Type::Error,
        0,
        TransportIoResult::PostIoAction::Close,
        "Not connected"
    };
  }
  
  // Reset idle timer on activity
  resetIdleTimer();
  last_activity_time_ = std::chrono::steady_clock::now();
  
  TransportIoResult result{TransportIoResult::Type::Success, 0};
  
  // Process through filter chain first
  if (filter_chain_) {
    result = processFilterChainWrite(buffer, end_stream);
    if (result.type == TransportIoResult::Type::Error) {
      return result;
    }
  } else {
    // No filter chain, buffer directly for write
    write_buffer_.move(buffer);
    result.bytes_processed = write_buffer_.length();
  }
  
  // Write to underlying transport
  if (underlying_transport_ && write_buffer_.length() > 0) {
    auto write_result = underlying_transport_->doWrite(write_buffer_, end_stream);
    
    if (write_result.type == TransportIoResult::Type::Error) {
      failure_reason_ = write_result.error_details.value_or("Write error");
      return write_result;
    }
    
    stats_.bytes_sent += write_result.bytes_processed;
    result.bytes_processed = write_result.bytes_processed;
    result.action = write_result.action;
  }
  
  return result;
}

void HttpSseTransportSocketV2::onConnected() {
  assertInDispatcherThread();
  
  connecting_ = false;
  connected_ = true;
  stats_.connect_time = std::chrono::steady_clock::now();
  
  // Cancel connect timer
  cancelConnectTimer();
  
  // Start idle timer
  startIdleTimer();
  
  // Notify filter chain
  if (filter_chain_) {
    filter_chain_->onConnectionEvent(network::ConnectionEvent::Connected);
  }
  
  // Notify underlying transport
  if (underlying_transport_) {
    underlying_transport_->onConnected();
  }
  
  // Notify callbacks
  if (callbacks_) {
    callbacks_->raiseEvent(network::ConnectionEvent::Connected);
  }
}

TransportIoResult HttpSseTransportSocketV2::processFilterChainRead(
    Buffer& buffer) {
  // Process data through read filters
  network::FilterStatus status = network::FilterStatus::Continue;
  
  // Move data from read buffer to filter chain
  if (filter_chain_) {
    status = filter_chain_->onData(read_buffer_, false);
  }
  
  // Check filter status
  if (status == network::FilterStatus::StopIteration) {
    return TransportIoResult{
        TransportIoResult::Type::Success,
        0,
        TransportIoResult::PostIoAction::KeepOpen
    };
  }
  
  // Move processed data to output buffer
  buffer.move(read_buffer_);
  
  return TransportIoResult{
      TransportIoResult::Type::Success,
      buffer.length(),
      TransportIoResult::PostIoAction::KeepOpen
  };
}

TransportIoResult HttpSseTransportSocketV2::processFilterChainWrite(
    Buffer& buffer, bool end_stream) {
  // Process data through write filters
  network::FilterStatus status = network::FilterStatus::Continue;
  
  if (filter_chain_) {
    status = filter_chain_->onWrite(buffer, end_stream);
  }
  
  // Check filter status
  if (status == network::FilterStatus::StopIteration) {
    return TransportIoResult{
        TransportIoResult::Type::Success,
        0,
        TransportIoResult::PostIoAction::KeepOpen
    };
  }
  
  // Move processed data to write buffer
  write_buffer_.move(buffer);
  
  return TransportIoResult{
      TransportIoResult::Type::Success,
      write_buffer_.length(),
      TransportIoResult::PostIoAction::KeepOpen
  };
}

void HttpSseTransportSocketV2::onConnectTimeout() {
  failure_reason_ = "Connect timeout";
  closeSocket(network::ConnectionEvent::LocalClose);
}

void HttpSseTransportSocketV2::onIdleTimeout() {
  auto now = std::chrono::steady_clock::now();
  auto idle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - last_activity_time_);
  
  if (idle_duration >= config_.idle_timeout) {
    failure_reason_ = "Idle timeout";
    closeSocket(network::ConnectionEvent::LocalClose);
  } else {
    // Reschedule timer for remaining time
    auto remaining = config_.idle_timeout - idle_duration;
    idle_timer_->enableTimer(remaining);
  }
}

void HttpSseTransportSocketV2::startConnectTimer() {
  if (connect_timer_ && config_.connect_timeout.count() > 0) {
    connect_timer_->enableTimer(config_.connect_timeout);
  }
}

void HttpSseTransportSocketV2::cancelConnectTimer() {
  if (connect_timer_) {
    connect_timer_->disableTimer();
  }
}

void HttpSseTransportSocketV2::startIdleTimer() {
  if (idle_timer_ && config_.idle_timeout.count() > 0) {
    idle_timer_->enableTimer(config_.idle_timeout);
  }
}

void HttpSseTransportSocketV2::resetIdleTimer() {
  cancelIdleTimer();
  startIdleTimer();
}

void HttpSseTransportSocketV2::cancelIdleTimer() {
  if (idle_timer_) {
    idle_timer_->disableTimer();
  }
}

// ===== HttpSseTransportSocketFactoryV2 Implementation =====

HttpSseTransportSocketFactoryV2::HttpSseTransportSocketFactoryV2(
    const HttpSseTransportSocketConfigV2& config,
    event::Dispatcher& dispatcher,
    std::shared_ptr<network::FilterChainFactory> filter_factory)
    : config_(config),
      dispatcher_(dispatcher),
      filter_factory_(filter_factory) {}

bool HttpSseTransportSocketFactoryV2::implementsSecureTransport() const {
  return config_.underlying_transport == 
         HttpSseTransportSocketConfigV2::UnderlyingTransport::SSL;
}

network::TransportSocketPtr 
HttpSseTransportSocketFactoryV2::createTransportSocket() const {
  // Create filter chain if factory is provided
  std::unique_ptr<network::FilterChain> filter_chain;
  if (filter_factory_) {
    filter_chain = filter_factory_->createFilterChain();
  }
  
  return std::make_unique<HttpSseTransportSocketV2>(
      config_, dispatcher_, std::move(filter_chain));
}

network::TransportSocketPtr 
HttpSseTransportSocketFactoryV2::createTransportSocket(
    network::TransportSocketOptionsSharedPtr options) const {
  // Options could modify the configuration
  // For now, just create with default config
  return createTransportSocket();
}

// ===== HttpSseTransportBuilder Implementation =====

HttpSseTransportBuilder& HttpSseTransportBuilder::withMode(
    HttpSseTransportSocketConfigV2::Mode mode) {
  config_.mode = mode;
  return *this;
}

HttpSseTransportBuilder& HttpSseTransportBuilder::withServerAddress(
    const std::string& address) {
  config_.server_address = address;
  return *this;
}

HttpSseTransportBuilder& HttpSseTransportBuilder::withSsl(
    const HttpSseTransportSocketConfigV2::SslConfig& ssl) {
  config_.underlying_transport = 
      HttpSseTransportSocketConfigV2::UnderlyingTransport::SSL;
  config_.ssl_config = ssl;
  return *this;
}

HttpSseTransportBuilder& HttpSseTransportBuilder::withConnectTimeout(
    std::chrono::milliseconds timeout) {
  config_.connect_timeout = timeout;
  return *this;
}

HttpSseTransportBuilder& HttpSseTransportBuilder::withIdleTimeout(
    std::chrono::milliseconds timeout) {
  config_.idle_timeout = timeout;
  return *this;
}

HttpSseTransportBuilder& HttpSseTransportBuilder::withHttpFilter(bool is_server) {
  // Create HTTP codec filter factory
  auto factory = std::make_shared<filter::HttpCodecFilterFactory>(
      dispatcher_, is_server);
  filter_factories_.push_back(factory);
  return *this;
}

HttpSseTransportBuilder& HttpSseTransportBuilder::withSseFilter(bool is_server) {
  // Create SSE codec filter factory
  auto factory = std::make_shared<filter::SseCodecFilterFactory>(
      dispatcher_, is_server);
  filter_factories_.push_back(factory);
  return *this;
}

HttpSseTransportBuilder& HttpSseTransportBuilder::withCustomFilter(
    std::shared_ptr<network::FilterFactory> factory) {
  filter_factories_.push_back(factory);
  return *this;
}

std::unique_ptr<HttpSseTransportSocketV2> HttpSseTransportBuilder::build() {
  // Create composite filter chain from all factories
  auto filter_chain = std::make_unique<network::FilterChainImpl>();
  
  for (const auto& factory : filter_factories_) {
    factory->createFilters(*filter_chain);
  }
  
  return std::make_unique<HttpSseTransportSocketV2>(
      config_, dispatcher_, std::move(filter_chain));
}

std::unique_ptr<HttpSseTransportSocketFactoryV2> 
HttpSseTransportBuilder::buildFactory() {
  // Create composite filter chain factory
  auto chain_factory = std::make_shared<network::CompositeFilterChainFactory>(
      filter_factories_);
  
  return std::make_unique<HttpSseTransportSocketFactoryV2>(
      config_, dispatcher_, chain_factory);
}

} // namespace transport
} // namespace mcp