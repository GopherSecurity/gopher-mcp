// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "mcp/client/mcp_client.h"

// Override the default log component for this file
#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "client"

#include <algorithm>
#include <future>
#include <sstream>
#include <thread>

#include "mcp/event/libevent_dispatcher.h"
#include "mcp/http/http_parser.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"
#include "mcp/mcp_application_base.h"
#include "mcp/mcp_connection_manager.h"
#include "mcp/network/socket_interface_impl.h"

namespace mcp {
namespace client {

using namespace mcp::network;
using namespace mcp::event;
using namespace mcp::application;

// Import specific types
using mcp::Buffer;
using mcp::CallToolResult;
using mcp::CreateMessageRequest;
using mcp::CreateMessageResult;
using mcp::Error;
using mcp::get;
using mcp::get_error;
using mcp::GetPromptResult;
using mcp::holds_alternative;
using mcp::ImageContent;
using mcp::Implementation;
using mcp::InitializeResult;
using mcp::is_error;
using mcp::ListPromptsResult;
using mcp::ListResourcesResult;
using mcp::ListToolsResult;
using mcp::make_optional;
using mcp::makeVoidError;
using mcp::Metadata;
using mcp::MetadataBuilder;
using mcp::nullopt;
using mcp::optional;
using mcp::ReadResourceResult;
using mcp::RequestId;
using mcp::ServerCapabilities;
using mcp::TextContent;
using mcp::variant;
using mcp::VoidResult;
using mcp::jsonrpc::Notification;
using mcp::jsonrpc::Request;
using mcp::jsonrpc::Response;

namespace jsonrpc = mcp::jsonrpc;

namespace {
// Cap a payload string in a log line so large tool args / results don't flood
// the log. Appends "...(<N> bytes)" when truncation happens so the full size
// stays visible.
std::string logTruncate(const std::string& s, size_t max = 512) {
  if (s.size() <= max) {
    return s;
  }
  return s.substr(0, max) + "...(" + std::to_string(s.size()) + " bytes)";
}

std::future<Response> makeReadyResponseFuture(const Response& response) {
  std::promise<Response> promise;
  promise.set_value(response);
  return promise.get_future();
}

// How long shutdown waits for the request that ends the session to be
// written before closing on top of it. Bounded because a peer that has
// stopped reading must not be able to hold a client open, and short
// because nothing is waiting for an answer — only for the bytes to have
// been handed over.
constexpr std::chrono::milliseconds kSessionDeleteFlushWait{250};
/**
 * The JSON-RPC error a refused request's body carries, if it carries one.
 *
 * A server may answer with an HTTP error status and still say, in JSON-RPC,
 * exactly what went wrong: the 2026-07-28 transport does for a header that
 * does not match, a revision it does not serve and a capability that was
 * not declared, and any server may for anything else. The error is this
 * request's when it names this request's id, or names none, which a server
 * refusing a request before reading its id may do.
 */
optional<Error> jsonRpcErrorIn(const std::string& body, const RequestId& id) {
  if (body.empty()) {
    return nullopt;
  }
  json::JsonValue json;
  try {
    json = json::JsonValue::parse(body);
  } catch (const std::exception&) {
    return nullopt;
  }
  if (!json.isObject() || !json.contains("error") ||
      !json["error"].isObject() || !json["error"].contains("code") ||
      !json["error"]["code"].isInteger() ||
      !json["error"].contains("message") ||
      !json["error"]["message"].isString()) {
    return nullopt;
  }
  if (json.contains("id") && !json["id"].isNull()) {
    const auto& named = json["id"];
    const bool same = (named.isString() && holds_alternative<std::string>(id) &&
                       named.getString() == get<std::string>(id)) ||
                      (named.isInteger() && holds_alternative<int64_t>(id) &&
                       named.getInt64() == get<int64_t>(id));
    if (!same) {
      return nullopt;
    }
  }
  try {
    return mcp::make_optional(json::from_json<Error>(json["error"]));
  } catch (const std::exception&) {
    // The code and message are what matter most, and they were checked
    // above; whatever went wrong was in the data.
    return mcp::make_optional(
        Error(static_cast<int>(json["error"]["code"].getInt64()),
              json["error"]["message"].getString()));
  }
}

// Who this client says it is, as configured.
json::JsonValue clientInfoOf(const McpClientConfig& config) {
  Implementation self(config.client_name, config.client_version);
  if (!config.client_title.empty()) {
    self.title = config.client_title;
  }
  if (!config.client_description.empty()) {
    self.description = config.client_description;
  }
  if (!config.client_website_url.empty()) {
    self.websiteUrl = config.client_website_url;
  }
  if (!config.client_icons.empty()) {
    self.icons = config.client_icons;
  }
  return json::to_json(self);
}

// Who a server says it is, if it names itself. Only a string name is
// needed; anything else malformed is passed over.
optional<Implementation> implementationIn(const json::JsonValue& who) {
  if (!who.isObject() || !who.contains("name") || !who["name"].isString()) {
    return nullopt;
  }
  return mcp::make_optional(json::from_json<Implementation>(who));
}

}  // namespace

// Out-of-class definition for static constexpr member (required for C++14)
// In C++17+, constexpr static members are implicitly inline, but C++14 requires
// explicit out-of-class definition when the member is ODR-used
constexpr int McpClient::kConnectionIdleTimeoutSec;

// Constructor
McpClient::McpClient(const McpClientConfig& config)
    : ApplicationBase(config), config_(config) {
  // An extension identifier that breaks the rules would be sent to every
  // server and recognised by none: refused here, when the client is made.
  protocol::extensions::checkConfigured(config_.extensions);
  if (config_.capabilities.extensions.has_value()) {
    if (!config_.capabilities.extensions->isObject()) {
      throw std::invalid_argument("capabilities.extensions must be an object");
    }
    std::map<std::string, json::JsonValue> declared;
    for (const auto& id : config_.capabilities.extensions->keys()) {
      declared[id] = (*config_.capabilities.extensions)[id];
    }
    protocol::extensions::checkConfigured(declared);
  }
  // Set callbacks for protocol state changes
  protocol::McpProtocolStateMachineConfig protocol_config;
  protocol_config.initialization_timeout =
      config_.protocol_initialization_timeout;
  protocol_config.connection_timeout = config_.protocol_connection_timeout;
  protocol_config.drain_timeout = config_.protocol_drain_timeout;
  protocol_config.auto_reconnect = config_.protocol_auto_reconnect;
  protocol_config.max_reconnect_attempts =
      config_.protocol_max_reconnect_attempts;
  protocol_config.reconnect_delay = config_.protocol_reconnect_delay;

  // Initialize request tracker
  request_tracker_ = std::make_unique<RequestTracker>(config_.request_timeout);

  // Initialize circuit breaker
  circuit_breaker_ = std::make_unique<CircuitBreaker>(
      config_.circuit_breaker_threshold, config_.circuit_breaker_timeout,
      0.5);  // 50% error rate threshold

  // Initialize protocol callbacks
  protocol_callbacks_ = std::make_unique<ProtocolCallbacksImpl>(*this);

  // Set callbacks for protocol state changes
  protocol_config.state_change_callback =
      [this](const protocol::ProtocolStateTransitionContext& ctx) {
        handleProtocolStateChange(ctx);
      };

  protocol_config.error_callback = [this](const Error& error) {
    handleError(error);
  };

  // Protocol state machine will be created in dispatcher thread during
  // initialization
}
// Destructor
McpClient::~McpClient() { shutdown(); }

// Connect to server
VoidResult McpClient::connect(const std::string& uri) {
  // Check if already shutting down
  if (shutting_down_) {
    return makeVoidError(
        Error(::mcp::jsonrpc::INTERNAL_ERROR, "Client is shutting down"));
  }

  // Check if already connected
  if (connected_) {
    return makeVoidError(
        Error(::mcp::jsonrpc::INVALID_REQUEST, "Already connected"));
  }

  // Create main dispatcher
  main_dispatcher_ = new LibeventDispatcher("client");

  // Start dispatcher in a separate thread
  // Store thread handle for proper cleanup (reference pattern)
  dispatcher_thread_ =
      std::thread([this]() { main_dispatcher_->run(RunType::Block); });

  // Give dispatcher thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Get socket interface after dispatcher is created
  socket_interface_ = std::make_unique<SocketInterfaceImpl>();

  // Create connect promise - this will be fulfilled by handleConnectionEvent
  // when the TCP+SSL handshake completes, not when connect() is initiated
  auto connect_promise = std::make_shared<std::promise<VoidResult>>();
  auto connect_future = connect_promise->get_future();

  // Store the promise so handleConnectionEvent can fulfill it
  {
    std::lock_guard<std::mutex> lock(connect_promise_mutex_);
    pending_connect_promise_ = connect_promise;
  }

  postCarryingTrace([this, uri, connect_promise]() {
    try {
      // Initialize protocol state machine if not already created
      if (!protocol_state_machine_) {
        protocol::McpProtocolStateMachineConfig protocol_config;
        protocol_config.initialization_timeout =
            config_.protocol_initialization_timeout;
        protocol_config.connection_timeout =
            config_.protocol_connection_timeout;
        protocol_config.drain_timeout = config_.protocol_drain_timeout;
        protocol_config.auto_reconnect = config_.protocol_auto_reconnect;
        protocol_config.max_reconnect_attempts =
            config_.protocol_max_reconnect_attempts;
        protocol_config.reconnect_delay = config_.protocol_reconnect_delay;

        protocol_config.state_change_callback =
            [this](const protocol::ProtocolStateTransitionContext& ctx) {
              handleProtocolStateChange(ctx);
            };

        protocol_config.error_callback = [this](const Error& error) {
          handleError(error);
        };

        protocol_state_machine_ =
            std::make_unique<protocol::McpProtocolStateMachine>(
                *main_dispatcher_, protocol_config);
      }

      // Trigger protocol connection state
      // We're already in dispatcher thread from the outer post() at line 142
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(
            protocol::McpProtocolEvent::CONNECT_REQUESTED);
      }

      // Transport negotiation flow:
      // 1. Parse URI to determine transport type
      // 2. Create connection configuration with transport settings
      // 3. Create connection manager and connect

      // Store URI before creating config so it's available
      current_uri_ = uri;
      ladder_notes_.clear();

      // Either somebody has already decided what this server speaks, or
      // the server is about to be asked. Nothing in between, and no
      // reading of the URL: a path is not evidence.
      if (detectsTransport(uri)) {
        runTransportLadder(uri);
      } else {
        const TransportType transport = negotiateTransport(uri);
        // Naming the transport says which one, not which revision. Only
        // the newest is found by asking, so a client that may speak it
        // still asks, once, before starting.
        if (transport == TransportType::StreamableHttp &&
            config_.streamable_http.enable_modern_era &&
            (uri.find("http://") == 0 || uri.find("https://") == 0)) {
          discoverRevisionThenStart(uri);
        } else {
          startTransport(transport);
        }
      }

      // On success, DON'T fulfill the promise here!
      // handleConnectionEvent will fulfill it when the connection is
      // established This ensures connect() waits for the actual TCP+SSL
      // handshake to complete
      last_activity_time_ = std::chrono::steady_clock::now();
    } catch (const std::exception& e) {
      // Fulfill promise with error on exception
      std::lock_guard<std::mutex> lock(connect_promise_mutex_);
      if (pending_connect_promise_) {
        pending_connect_promise_->set_value(
            makeVoidError(Error(::mcp::jsonrpc::INTERNAL_ERROR, e.what())));
        pending_connect_promise_.reset();
      }
    }
  });

  // Wait for connection to be established
  auto status = connect_future.wait_for(std::chrono::seconds(10));
  if (status == std::future_status::timeout) {
    return makeVoidError(
        Error(::mcp::jsonrpc::INTERNAL_ERROR, "Connection timeout"));
  }

  return connect_future.get();
}

// Disconnect from server
void McpClient::disconnect() {
  // Don't create timers if we're shutting down
  if (shutting_down_) {
    return;
  }

  // Check if we're in dispatcher thread or post to it
  if (main_dispatcher_ && !main_dispatcher_->isThreadSafe()) {
    // We're not in dispatcher thread, post the disconnect
    postCarryingTrace([this]() {
      if (protocol_state_machine_ && !shutting_down_) {
        protocol_state_machine_->handleEvent(
            protocol::McpProtocolEvent::SHUTDOWN_REQUESTED);
      }
    });
    return;
  }

  // We're in dispatcher thread or no dispatcher, proceed directly
  if (protocol_state_machine_) {
    protocol_state_machine_->handleEvent(
        protocol::McpProtocolEvent::SHUTDOWN_REQUESTED);
  }

  // Close connection
  if (connection_manager_) {
    connection_manager_->close();
  }

  // Reset state
  connected_ = false;
  initialized_ = false;
}

// Check if the underlying connection is actually open
bool McpClient::isConnectionOpen() const {
  if (!connected_ || !connection_manager_) {
    return false;
  }
  return connection_manager_->isConnected();
}

std::chrono::milliseconds McpClient::reconnectWaitBudgetForRequestTimeout(
    std::chrono::milliseconds request_timeout) {
  return std::min(std::max(request_timeout / 3, std::chrono::milliseconds(250)),
                  std::chrono::milliseconds(5000));
}

// Reconnect using stored URI
VoidResult McpClient::reconnect() {
  if (current_uri_.empty()) {
    return makeVoidError(Error(::mcp::jsonrpc::INTERNAL_ERROR,
                               "No URI stored for reconnection"));
  }

  // Now reconnect - reuse existing dispatcher if available
  if (!main_dispatcher_) {
    return makeVoidError(Error(::mcp::jsonrpc::INTERNAL_ERROR,
                               "No dispatcher available for reconnection"));
  }

  // CRITICAL FIX: Check if we're on the dispatcher thread
  // reconnect() is typically called from sendRequestInternal() which runs
  // on user threads. McpConnectionManager operations MUST run on the
  // dispatcher thread for thread safety (network I/O, filters, callbacks).
  if (!main_dispatcher_->isThreadSafe()) {
    // We're NOT on dispatcher thread - post the reconnection work
    // Use a promise/future to return the result synchronously to caller
    auto reconnect_promise = std::make_shared<std::promise<VoidResult>>();
    auto reconnect_future = reconnect_promise->get_future();

    postCarryingTrace([reconnect_promise, this]() {
      // Now on dispatcher thread - perform reconnection
      VoidResult result = reconnectInternal();
      reconnect_promise->set_value(result);
    });

    // Wait for reconnection to complete
    return reconnect_future.get();
  }

  // We're already on dispatcher thread - do work directly to avoid deadlock
  return reconnectInternal();
}

// Internal reconnection logic (must be called on dispatcher thread)
VoidResult McpClient::reconnectInternal() {
  // Disconnect first if we think we're connected
  if (connected_ || connection_manager_) {
    // Close the old connection
    if (connection_manager_) {
      connection_manager_->close();
      connection_manager_.reset();
    }
    connected_ = false;
    initialized_ = false;
  }

  try {
    // Whatever was settled on the way in. Asking again would be asking
    // a question that has been answered, and the answer to it is not
    // something this URL can be read for.
    TransportType transport = settled_transport_.has_value()
                                  ? settled_transport_.value()
                                  : negotiateTransport(current_uri_);

    // Create connection configuration
    McpConnectionConfig conn_config = createConnectionConfig(transport);

    // Create new connection manager
    connection_manager_ = std::make_unique<McpConnectionManager>(
        *main_dispatcher_, *socket_interface_, conn_config);

    // Set message callback handler
    connection_manager_->setProtocolCallbacks(*protocol_callbacks_);
    connection_manager_->setStreamIdleTimeout(
        config_.streamable_http.stream_idle_timeout);

    // Initiate connection (asynchronous - doesn't wait for TCP handshake)
    VoidResult result = connection_manager_->connect();

    if (is_error<std::nullptr_t>(result)) {
      auto error = get_error<std::nullptr_t>(result);
      return makeVoidError(*error);
    }

    // The connection_manager_->connect() initiates the TCP connection
    // asynchronously. The dispatcher needs to process events for the connection
    // to complete.
    //
    // Simply mark that reconnection is in progress. The handleConnectionEvent
    // callback will set connected_=true when the TCP handshake completes.
    // We return success here - the connection will be ready shortly.
    last_activity_time_ = std::chrono::steady_clock::now();

    return makeSuccess<std::nullptr_t>(nullptr);
  } catch (const std::exception& e) {
    return makeVoidError(Error(::mcp::jsonrpc::INTERNAL_ERROR, e.what()));
  }
}

// Shutdown client
void McpClient::clearConnectionCallbacksForShutdown() {
  if (connection_manager_) {
    connection_manager_->clearProtocolCallbacks();
  }
}

void McpClient::shutdown() {
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  // Said here rather than left to a stream event that is not coming:
  // the callbacks are cut below, so nothing will report the stream
  // closing, and a client that has stopped listening must not still
  // claim the server can reach it.
  server_stream_open_ = false;

  // Give the session back before anything is torn down. It happens
  // here, ahead of alive_ being dropped and the callbacks being cut,
  // because after either of those there is no way left to write. A
  // server that is never told keeps the session until it times out,
  // which is why this is worth an attempt rather than nothing.
  if (connection_manager_ && connected_ && streamable_session_ &&
      streamable_session_->hasId() && main_dispatcher_) {
    if (main_dispatcher_->isThreadSafe()) {
      connection_manager_->sendSessionDelete();
    } else {
      // Waited on, not merely posted: the close below would otherwise
      // race the write and usually win.
      //
      // And posted twice, because a message takes two posts to be
      // written: one here and one in the connection manager. Anything
      // asked for before this — notifications/initialized, sent as the
      // handshake completes — has had its first by now but maybe not its
      // second, and ending the session ahead of it would send it under
      // no session at all.
      auto written = std::make_shared<std::promise<void>>();
      auto done = written->get_future();
      event::Dispatcher* dispatcher = main_dispatcher_;
      dispatcher->post([this, dispatcher, written]() {
        dispatcher->post([this, written]() {
          if (connection_manager_) {
            connection_manager_->sendSessionDelete();
          }
          written->set_value();
        });
      });
      done.wait_for(kSessionDeleteFlushWait);
    }
  }

  alive_.reset();

  // Close connection directly without triggering state machine
  if (connection_manager_) {
    // Break the callback ownership link synchronously. shutdown() may be called
    // off the dispatcher thread and immediately request dispatcher exit; a
    // posted close task is not guaranteed to run before teardown continues.
    clearConnectionCallbacksForShutdown();
    if (main_dispatcher_ && !main_dispatcher_->isThreadSafe()) {
      // Post to dispatcher thread
      postCarryingTrace([this]() {
        if (connection_manager_) {
          connection_manager_->close();
        }
      });
    } else {
      connection_manager_->close();
    }
  }

  connected_ = false;

  // Request dispatcher shutdown
  shutdown_requested_ = true;

  // Notify dispatcher to exit
  if (main_dispatcher_) {
    main_dispatcher_->exit();
  }

  // Join dispatcher thread if it's joinable (reference pattern)
  if (dispatcher_thread_.joinable()) {
    dispatcher_thread_.join();
  }

  // Clean up dispatcher-owned resources before destroying the dispatcher.
  // Deferred connection teardown can still enqueue work on the dispatcher even
  // when shutdown skipped a posted close task.
  protocol_state_machine_.reset();
  connection_manager_.reset();
  request_tracker_.reset();
  circuit_breaker_.reset();
  // A timer belongs to the dispatcher that made it and must not outlive
  // it; this one is the only one that can still be armed by the time we
  // get here, since it is the only one that fires without a request
  // behind it.
  server_stream_timer_.reset();
  legacy_probe_timer_.reset();
  classic_probe_.reset();
  modern_probe_.reset();

  // Clean up dispatcher after thread has exited and its owners are gone.
  if (main_dispatcher_) {
    delete main_dispatcher_;
    main_dispatcher_ = nullptr;
  }

  // Client resources are cleaned up above
}

// Initialize protocol
std::future<InitializeResult> McpClient::initializeProtocol() {
  // Create promise for InitializeResult
  auto result_promise = std::make_shared<std::promise<InitializeResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // Defer all protocol operations to dispatcher thread
  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<jsonrpc::Response>>();
  std::weak_ptr<bool> alive = alive_;
  event::Dispatcher* dispatcher = main_dispatcher_;
  McpClient* client = this;

  // Which era this conversation is in was settled when the transport was
  // chosen, and it decides what an introduction even is here: the newest
  // revision has none, so what would have been asked in one is asked of
  // the one method that answers it.
  const bool modern =
      streamable_session_ &&
      protocol::modern::isModernVersion(streamable_session_->protocolVersion());
  std::string protocol_version = modern ? streamable_session_->protocolVersion()
                                        : config_.protocol_version;
  // Read here, on the caller's thread, so the worker below reads nothing of
  // the client's.
  const std::vector<std::string> accepted_versions =
      acceptedHandshakeVersions();

  // Step 1: Post to dispatcher to send the request (non-blocking)
  dispatcher->post([this, alive, request_future_ptr, modern]() {
    if (alive.expired()) {
      *request_future_ptr = makeReadyResponseFuture(Response::make_error(
          RequestId(0),
          Error(::mcp::jsonrpc::INTERNAL_ERROR,
                "Client shut down before initialize request was sent")));
      return;
    }

    // Notify protocol state machine that initialization is starting
    if (protocol_state_machine_) {
      protocol_state_machine_->handleEvent(
          protocol::McpProtocolEvent::INITIALIZE_REQUESTED);
    }

    if (modern) {
      // Nothing to offer and nothing to negotiate: the version is already
      // settled and travels on every request. This only asks what the
      // server is and what it can do.
      GOPHER_LOG_FLOW_DEBUG("MCP invoke: {}",
                            protocol::modern::kMethodServerDiscover);
      *request_future_ptr = sendRequest(protocol::modern::kMethodServerDiscover,
                                        optional<Metadata>());
      return;
    }

    auto init_params = buildInitializeParams();

    // Send request - do NOT block here!
    GOPHER_LOG_FLOW_DEBUG(
        "MCP invoke: initialize (protocolVersion={}, client={}/{})",
        config_.protocol_version, config_.client_name, config_.client_version);
    *request_future_ptr =
        sendRequest("initialize", mcp::make_optional(init_params));
    GOPHER_LOG_TRACE("initializeProtocol: request sent, callback returning");
    // Callback returns immediately - response will be processed elsewhere
  });

  // Step 2: Block on the response on a worker thread so we don't stall the
  // dispatcher. When the response parses cleanly, hand the dispatcher-thread
  // state mutations (protocol_state_machine_, server_capabilities_,
  // initialized_) back to the dispatcher via post() — those fields are read
  // from the dispatcher elsewhere, so writing them from this worker thread
  // would be a data race. Only the final promise resolution runs on whichever
  // thread (dispatcher or worker) completes parsing.
  std::thread([alive, dispatcher, protocol_version, accepted_versions, client,
               result_promise, request_future_ptr, modern]() {
    try {
      // Wait for dispatcher to publish the request future.
      while (!request_future_ptr->valid()) {
        if (alive.expired()) {
          throw std::runtime_error(
              "Client shut down before initialize request was sent");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      GOPHER_LOG_TRACE(
          "initializeProtocol: waiting for response on worker thread");
      auto response = request_future_ptr->get();
      GOPHER_LOG_TRACE("initializeProtocol: got response");

      if (response.error.has_value()) {
        GOPHER_LOG_ERROR("MCP invoke: initialize failed: {}",
                         response.error->message);
        result_promise->set_exception(
            std::make_exception_ptr(RequestError(response.error.value())));
        return;
      }
      GOPHER_LOG_FLOW_DEBUG("MCP invoke: initialize succeeded");

      // Parse InitializeResult from response (pure parsing — no shared state).
      InitializeResult init_result =
          modern ? parseDiscoverResponse(response, protocol_version)
                 : parseInitializeResponse(response, protocol_version,
                                           accepted_versions);

      // Commit state on the dispatcher thread, then fulfill the promise.
      // The promise is fulfilled after the post completes so callers who
      // proceed on future.get() see initialized_/server_capabilities_
      // already published.
      if (!dispatcher || alive.expired()) {
        result_promise->set_exception(
            std::make_exception_ptr(std::runtime_error("Client shut down")));
        return;
      }
      dispatcher->post([client, alive, protocol_version, result_promise,
                        init_result, modern]() {
        if (alive.expired()) {
          result_promise->set_exception(
              std::make_exception_ptr(std::runtime_error("Client shut down")));
          return;
        }
        client->server_capabilities_ = init_result.capabilities;
        client->initialized_ = true;
        // The revision every request after this one declares. It comes
        // out of the response body rather than its headers, so this is
        // the first place that both knows it and is on the thread the
        // session is read from. A server that named none leaves us
        // declaring what we asked for, which is what we are speaking.
        if (client->streamable_session_) {
          client->streamable_session_->setProtocolVersion(
              init_result.protocolVersion.empty()
                  ? protocol_version
                  : init_result.protocolVersion);
        }
        if (client->protocol_state_machine_) {
          client->protocol_state_machine_->handleEvent(
              protocol::McpProtocolEvent::INITIALIZED);
        }
        // Only the handshake of the earlier revisions ends with this.
        // The newest has no handshake to end, and a server speaking it
        // has no meaning for the notification.
        if (!modern) {
          client->sendInitializedNotification();
        }
        // Only now: a stream belongs to a session, and until the
        // handshake landed there was no session to hold one under.
        if (client->config_.streamable_http.open_server_stream) {
          client->openServerStream(std::string());
        }
        result_promise->set_value(init_result);
      });
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

std::vector<std::string> McpClient::acceptedHandshakeVersions() const {
  std::vector<std::string> accepted;
  accepted.push_back(config_.protocol_version);
  // A server may answer with an older version than the one offered, never
  // a newer one: offering a version is saying it is the newest this client
  // will speak. Versions are dates, so they order as strings do.
  for (const auto& version : transport::handshakeProtocolVersions(
           config_.streamable_http.protocol_versions)) {
    if (version < config_.protocol_version) {
      accepted.push_back(version);
    }
  }
  return accepted;
}

InitializeResult McpClient::parseInitializeResponse(
    const jsonrpc::Response& response,
    const std::string& protocol_version,
    const std::vector<std::string>& accepted) {
  if (!response.result.has_value()) {
    throw std::runtime_error("Initialize response missing result");
  }

  InitializeResult init_result;
  init_result.capabilities = ServerCapabilities();

  json::JsonValue result;
  if (!resultAsJson(response, &result) || !result.isObject()) {
    throw std::runtime_error(
        "The server's answer to initialize is not an object");
  }

  // The version the server will speak. One this client does not speak, or
  // none at all, is not carried on in: the conversation would be in a
  // revision neither side can rely on the other to follow.
  if (!result.contains("protocolVersion") ||
      !result["protocolVersion"].isString()) {
    throw std::runtime_error(
        "The server's answer to initialize names no protocolVersion");
  }
  const std::string offered = result["protocolVersion"].getString();
  const bool supported = accepted.empty()
                             ? offered == protocol_version
                             : std::find(accepted.begin(), accepted.end(),
                                         offered) != accepted.end();
  if (!supported) {
    throw std::runtime_error(
        "The server answered initialize with protocol "
        "version '" +
        offered + "', which this client does not support");
  }
  init_result.protocolVersion = offered;

  if (result.contains("capabilities") && result["capabilities"].isObject()) {
    init_result.capabilities =
        json::from_json<ServerCapabilities>(result["capabilities"]);
  }

  if (result.contains("serverInfo")) {
    init_result.serverInfo = implementationIn(result["serverInfo"]);
  }

  if (result.contains("instructions") && result["instructions"].isString()) {
    init_result.instructions =
        mcp::make_optional(result["instructions"].getString());
  }

  return init_result;
}

InitializeResult McpClient::parseDiscoverResponse(
    const jsonrpc::Response& response, const std::string& protocol_version) {
  if (!response.result.has_value()) {
    throw std::runtime_error("Discovery answered with nothing");
  }

  // What a handshake would have said, from the one method that says it in
  // an era without one. The version is not read out of the answer: this
  // client settled which revision it is speaking before it sent anything,
  // and a server listing several is listing what it serves rather than
  // what is being spoken.
  InitializeResult init_result;
  init_result.protocolVersion = protocol_version;
  init_result.capabilities = ServerCapabilities();

  json::JsonValue result;
  if (!resultAsJson(response, &result)) {
    return init_result;
  }

  if (result.isObject() && result.contains("capabilities")) {
    init_result.capabilities =
        json::from_json<ServerCapabilities>(result["capabilities"]);
  }

  // With no handshake there is no `serverInfo` field to carry it; the
  // revision puts it under the metadata key instead.
  if (result.isObject() && result.contains("_meta") &&
      result["_meta"].isObject() &&
      result["_meta"].contains(protocol::modern::kMetaServerInfo)) {
    init_result.serverInfo =
        implementationIn(result["_meta"][protocol::modern::kMetaServerInfo]);
  }

  if (result.isObject() && result.contains("instructions") &&
      result["instructions"].isString()) {
    init_result.instructions =
        mcp::make_optional(result["instructions"].getString());
  }

  // How long this answer may be cached, and whether it may be shared.
  if (result.isObject() && result.contains("ttlMs") &&
      result["ttlMs"].isInteger()) {
    init_result.ttlMs = mcp::make_optional(result["ttlMs"].getInt64());
  }
  if (result.isObject() && result.contains("cacheScope") &&
      result["cacheScope"].isString()) {
    init_result.cacheScope =
        mcp::make_optional(result["cacheScope"].getString());
  }

  return init_result;
}

ListPromptsResult McpClient::parseListPromptsResponse(
    const jsonrpc::Response& response) {
  ListPromptsResult result;
  if (!response.result.has_value()) {
    return result;
  }
  const auto& answer = response.result.value();

  // The spec's shape: an object with the prompts under "prompts" and,
  // when there are more, a cursor for the next page. The decoder reads it
  // into this type, so it arrives already typed.
  if (holds_alternative<ListPromptsResult>(answer)) {
    return get<ListPromptsResult>(answer);
  }
  if (holds_alternative<json::JsonValue>(answer)) {
    const auto& value = get<json::JsonValue>(answer);
    if (value.isArray() && value.size() == 0) {
      return result;
    }
  }

  // A bare array, as older servers sent. The decoder cannot tell prompts
  // from tools by shape alone, and reads prompts with no arguments as
  // tools; a name and a description are all either one has in common.
  if (holds_alternative<std::vector<Prompt>>(answer)) {
    result.prompts = get<std::vector<Prompt>>(answer);
    return result;
  }
  if (holds_alternative<std::vector<Tool>>(answer)) {
    for (const auto& tool : get<std::vector<Tool>>(answer)) {
      Prompt prompt(tool.name);
      prompt.description = tool.description;
      result.prompts.push_back(prompt);
    }
    return result;
  }

  throw std::runtime_error("prompts/list answered with no list of prompts");
}

Metadata McpClient::buildInitializeParams() const {
  // MCP spec requires: protocolVersion, capabilities, clientInfo (nested
  // object)
  auto init_params = make_metadata();
  init_params["protocolVersion"] = config_.protocol_version;

  // clientInfo and capabilities are nested objects. The flat map holds them
  // as their JSON text, and the serializer puts them back on the wire as
  // the objects they are. Built as JSON so a name with a quote in it
  // cannot break the text.
  init_params["clientInfo"] = clientInfoOf(config_).toString();

  // What this client can do, derived the same way the newer era declares
  // it on every request: what was configured, plus what the registered
  // handlers can answer. Without it a server never asks this client for
  // anything, however many handlers it has.
  init_params["capabilities"] = declaredCapabilities().toString();
  return init_params;
}

void McpClient::sendInitializedNotification() {
  // The handshake is not over when the response arrives — the server is
  // told so, and only then may either side use what was agreed.
  GOPHER_LOG_FLOW_DEBUG("MCP invoke: notifications/initialized");
  sendNotification("notifications/initialized", nullopt);
}

void McpClient::sendInternalRequest(
    const std::string& method,
    const optional<Metadata>& params,
    std::function<void(const Response&)> on_response) {
  RequestId id = static_cast<int64_t>(next_request_id_++);
  auto context = std::make_shared<RequestContext>(id, method);
  context->params = params;
  context->start_time = std::chrono::steady_clock::now();
  context->on_response = std::move(on_response);

  request_tracker_->trackRequest(context);
  sendRequestInternal(context);
}

void McpClient::completeRequestWithError(
    const std::shared_ptr<RequestContext>& context, const Error& error) {
  if (!context || context->completed) {
    return;
  }
  releaseIfSubscription(*context);
  context->completed = true;
  const Response failed = Response::make_error(context->id, error);
  context->finish(failed);
  request_tracker_->removeRequest(context->id);
  client_stats_.requests_failed++;
  // Work the client carries on with hears of a failure as it would of an
  // answer: otherwise it waits for one that is never coming.
  if (context->on_response) {
    context->on_response(failed);
  }
}

void McpClient::releaseIfSubscription(const RequestContext& request) {
  // A subscription is over however its request ends. Refused, failed or
  // answered, what is left behind is the same — a callback nothing will
  // call and a connection nobody reads — so every way out goes through
  // here rather than only the one that succeeds.
  if (request.method != protocol::modern::kMethodSubscriptionsListen ||
      !holds_alternative<int64_t>(request.id)) {
    return;
  }
  releaseSubscription(get<int64_t>(request.id));
}

void McpClient::startReinitialize() {
  if (reinitializing_) {
    return;
  }
  if (streamable_session_ && protocol::modern::isModernVersion(
                                 streamable_session_->protocolVersion())) {
    // Nothing to start again. This era has no session to have been
    // forgotten and no introduction to make a second time, so a request
    // that failed failed on its own terms.
    return;
  }
  reinitializing_ = true;
  initialized_ = false;

  GOPHER_LOG_INFO("Session is gone; starting a new one");

  sendInternalRequest(
      "initialize", mcp::make_optional(buildInitializeParams()),
      [this](const Response& response) {
        reinitializing_ = false;

        // Held requests are answered with the handshake's own failure
        // rather than a fabricated one: what went wrong is that the
        // client could not get a session, and saying anything else
        // about the request itself would be inventing a cause.
        auto held = std::move(held_for_new_session_);
        held_for_new_session_.clear();

        if (response.error.has_value()) {
          GOPHER_LOG_ERROR("Could not start a new session: {}",
                           response.error->message);
          for (const auto& context : held) {
            completeRequestWithError(context, *response.error);
          }
          return;
        }

        try {
          InitializeResult init_result = parseInitializeResponse(
              response, config_.protocol_version, acceptedHandshakeVersions());
          server_capabilities_ = init_result.capabilities;
          initialized_ = true;
          if (streamable_session_) {
            streamable_session_->setProtocolVersion(
                init_result.protocolVersion.empty()
                    ? config_.protocol_version
                    : init_result.protocolVersion);
          }
        } catch (const std::exception& e) {
          Error parse_error(::mcp::jsonrpc::INTERNAL_ERROR,
                            std::string("Could not read the new session's "
                                        "initialize response: ") +
                                e.what());
          for (const auto& context : held) {
            completeRequestWithError(context, parse_error);
          }
          return;
        }

        sendInitializedNotification();

        // Sent again under the new session, in the order they were
        // refused. They are still tracked and still hold their original
        // ids, so whoever is waiting on them is waiting on these.
        for (const auto& context : held) {
          GOPHER_LOG_DEBUG("Sending {} again under the new session",
                           context->method);
          sendRequestInternal(context);
        }
      });
}

void McpClient::openServerStream(const std::string& last_event_id) {
  if (server_stream_refused_ || !connection_manager_ || !streamable_session_) {
    return;
  }
  if (!connection_manager_->openServerStream(last_event_id)) {
    GOPHER_LOG_DEBUG("No server stream opened");
  }
}

void McpClient::scheduleServerStreamReopen(const std::string& last_event_id) {
  if (server_stream_refused_ || shutting_down_ || !main_dispatcher_) {
    return;
  }

  if (!server_stream_backoff_) {
    const auto& stream_config = config_.streamable_http;
    // Retries are not counted here — a standalone stream is asked for
    // again as long as the client is up, and what grows is only the
    // waiting. The count this is built with is therefore irrelevant;
    // only the window matters.
    server_stream_backoff_.reset(new RetryManager(
        /*max_retries=*/0, stream_config.stream_reconnect_min,
        /*backoff_multiplier=*/2.0, stream_config.stream_reconnect_max));
  }

  // What the stream said to wait, when it said, in place of the window:
  // the server is the one that knows when it wants this client back.
  auto delay = server_stream_backoff_->getRetryDelay(
      server_stream_attempts_ == 0 ? 0 : server_stream_attempts_ - 1);
  if (server_stream_retry_.has_value()) {
    delay = server_stream_retry_.value();
  }
  ++server_stream_attempts_;

  GOPHER_LOG_DEBUG(
      "Asking for the server stream again in {}ms{}", delay.count(),
      last_event_id.empty() ? std::string()
                            : std::string(", from ") + last_event_id);
  reopenServerStreamAfter(delay, last_event_id);
}

void McpClient::reopenServerStreamAfter(std::chrono::milliseconds delay,
                                        const std::string& last_event_id) {
  if (!main_dispatcher_) {
    return;
  }
  if (!server_stream_timer_) {
    server_stream_timer_ = main_dispatcher_->createTimer(
        [this]() { openServerStream(pending_stream_cursor_); });
  }
  pending_stream_cursor_ = last_event_id;
  server_stream_timer_->enableTimer(delay);
}

void McpClient::handleClientStreamRetry(const optional<RequestId>& request_id,
                                        std::chrono::milliseconds retry) {
  // Within bounds: a server must not have this client reconnect in a tight
  // loop, nor keep it away for good.
  const auto& bounds = config_.streamable_http;
  const auto clamped = std::max(bounds.stream_retry_min,
                                std::min(retry, bounds.stream_retry_max));
  if (request_id.has_value()) {
    // Said by the stream carrying this answer, and about that stream only.
    // A request no longer tracked has no stream left to come back to, and
    // what its stream said is no guide to any other.
    auto context = request_tracker_->getRequest(request_id.value());
    if (context) {
      context->stream_retry = clamped;
    }
    return;
  }
  // The standalone stream, which is also what carries an answer being
  // picked up again.
  server_stream_retry_ = clamped;
}

void McpClient::handleClientStreamEvent(ClientStreamEvent event,
                                        const optional<RequestId>& request_id,
                                        const std::string& last_event_id) {
  switch (event) {
    case ClientStreamEvent::Opened:
      // A stream that opened is the evidence that the waiting worked, so
      // the next one that closes starts the window again from the floor.
      // And a new stream has said nothing yet about when to come back.
      GOPHER_LOG_DEBUG("Server stream open");
      server_stream_attempts_ = 0;
      server_stream_retry_.reset();
      server_stream_open_ = true;
      return;

    case ClientStreamEvent::Refused:
      // A standing answer. Asking again would be asking the same
      // question of the same server and would get the same answer.
      GOPHER_LOG_INFO(
          "Server does not serve a standalone stream; carrying on without one");
      server_stream_refused_ = true;
      server_stream_open_ = false;
      if (server_stream_timer_) {
        server_stream_timer_->disableTimer();
      }
      return;

    case ClientStreamEvent::Closed:
      server_stream_open_ = false;
      // A stream that was carrying an interrupted answer is still
      // carrying it, so losing it again is another failed attempt at
      // that answer rather than merely a stream that closed.
      if (stream_recovering_.has_value()) {
        auto context = request_tracker_->getRequest(stream_recovering_.value());
        if (context) {
          resumeAnswer(context, last_event_id);
          return;
        }
        stream_recovering_.reset();
      }

      // Ask for it back, from where it got to. What was missed is
      // replayed; what was not is not sent twice.
      if (config_.streamable_http.open_server_stream) {
        scheduleServerStreamReopen(last_event_id);
      }
      return;

    case ClientStreamEvent::AnswerSevered: {
      // The answer is not lost, only interrupted: it is still being
      // produced, and a stream that says where this one got to is given
      // the rest of it.
      std::shared_ptr<RequestContext> context;
      if (request_id.has_value()) {
        context = request_tracker_->getRequest(request_id.value());
      }
      if (context) {
        resumeAnswer(context, last_event_id);
      }
      return;
    }
  }
}

void McpClient::resumeAnswer(const std::shared_ptr<RequestContext>& context,
                             const std::string& last_event_id) {
  if (streamable_session_ && protocol::modern::isModernVersion(
                                 streamable_session_->protocolVersion())) {
    // Nothing to pick it up with. This era numbers no events, so there
    // is no place to carry on from, and it serves no standalone stream
    // to carry on over — asking for one would be asking for a method
    // this server refuses, and reading that refusal as a fact about
    // streams rather than about this request would leave the request
    // outstanding forever.
    //
    // So a stream cut short is the end of what it was carrying. Said
    // rather than waited on, and said through the ordinary failure path,
    // which is also what lets go of a subscription.
    GOPHER_LOG_DEBUG("The stream carrying {} was cut and cannot be resumed",
                     context->method);
    stream_recovering_.reset();
    completeRequestWithError(
        context, Error(::mcp::jsonrpc::INTERNAL_ERROR,
                       "The stream carrying this answer was cut off, and this "
                       "revision has no way to pick one up again"));
    return;
  }

  if (context->resume_attempts >= config_.streamable_http.resume_attempts) {
    GOPHER_LOG_WARN("Giving up on the answer to {} after {} attempts",
                    context->method, context->resume_attempts);
    stream_recovering_.reset();
    completeRequestWithError(
        context, Error(::mcp::jsonrpc::INTERNAL_ERROR,
                       "The answer to this request was cut off and could not "
                       "be picked up again"));
    // The stream is still worth having for its own sake, even though
    // this answer is not coming back on it.
    if (config_.streamable_http.open_server_stream) {
      scheduleServerStreamReopen(std::string());
    }
    return;
  }

  ++context->resume_attempts;
  const bool was_recovering = stream_recovering_.has_value();
  stream_recovering_ = mcp::make_optional(context->id);
  GOPHER_LOG_DEBUG(
      "Picking up the answer to {} from {}", context->method,
      last_event_id.empty() ? "<the beginning>" : last_event_id.c_str());

  // After the wait the stream that was cut said to take, when it said:
  // the answer's own stream the first time, the stream picking it up
  // after that.
  const optional<std::chrono::milliseconds> said =
      was_recovering ? server_stream_retry_ : context->stream_retry;
  if (said.has_value()) {
    reopenServerStreamAfter(said.value(), last_event_id);
    return;
  }
  // Otherwise straight away rather than after a wait: this is not a
  // server that went away, it is one still working on an answer a caller
  // is being held for.
  openServerStream(last_event_id);
}

void McpClient::handleTransportStatus(int status_code,
                                      const optional<RequestId>& request_id,
                                      const std::string& detail) {
  last_activity_time_ = std::chrono::steady_clock::now();

  // 2xx is the message layer's business: an answer arrives through
  // onResponse, and a 202 for a notification has no answer to arrive.
  if (status_code >= 200 && status_code < 300) {
    return;
  }

  std::shared_ptr<RequestContext> context;
  if (request_id.has_value()) {
    context = request_tracker_->getRequest(request_id.value());
  }

  if (status_code == static_cast<int>(http::HttpStatusCode::NotFound) &&
      streamable_session_) {
    // Recoverable only if there was a session to lose. Once the first
    // 404 has let go of it, the rest of what was in flight arrives with
    // nothing held — they were refused for the same reason and are
    // held for the same handshake.
    const bool recoverable = streamable_session_->hasId() || reinitializing_;
    if (recoverable && context && !context->session_retried) {
      context->session_retried = true;
      held_for_new_session_.push_back(context);
      if (!reinitializing_) {
        streamable_session_->forget();
        startReinitialize();
      }
      return;
    }
    if (recoverable && context) {
      // Already sent once under a session this server then forgot as
      // well. Answering is what stops it going round again.
      GOPHER_LOG_WARN(
          "Request {} was refused under a second session; not trying again",
          context->method);
    }
  }

  if (!context) {
    return;
  }

  // What the server said, when it said it in JSON-RPC: its code, message
  // and data are the answer, and the HTTP status only how it was sent.
  auto said = jsonRpcErrorIn(detail, context->id);
  if (said.has_value()) {
    // A header mismatch comes back as a 400, and is recovered from here as
    // it would be from an answer that arrived any other way.
    if (!context->completed &&
        recoverFromHeaderMismatch(
            context, Response::make_error(context->id, said.value()))) {
      return;
    }
    completeRequestWithError(context, said.value());
    return;
  }

  completeRequestWithError(
      context, Error(::mcp::jsonrpc::INTERNAL_ERROR,
                     "Server refused the request with HTTP " +
                         std::to_string(status_code) +
                         (detail.empty() ? std::string() : ": " + detail)));
}

// Send request with future-based async API
std::future<Response> McpClient::sendRequest(const std::string& method,
                                             const optional<Metadata>& params) {
  return sendRequest(method, params, {});
}

std::future<Response> McpClient::sendRequest(
    const std::string& method,
    const optional<Metadata>& params,
    const std::map<std::string, std::string>& http_headers) {
  // Check if circuit breaker allows request
  if (!circuit_breaker_->allowRequest()) {
    client_stats_.circuit_breaker_opens++;
    auto promise = std::make_shared<std::promise<Response>>();
    promise->set_value(Response::make_error(
        "", Error(::mcp::jsonrpc::INTERNAL_ERROR, "Circuit breaker open")));
    return promise->get_future();
  }

  // Generate request ID
  RequestId id = static_cast<int64_t>(next_request_id_++);

  // Create request context
  auto context = std::make_shared<RequestContext>(id, method);
  context->params = params;
  context->http_headers = http_headers;
  context->start_time = std::chrono::steady_clock::now();
  traceRequest(*context);
  startClock(context);

  // Track request
  request_tracker_->trackRequest(context);
  // Track request sent

  // Send request through internal pathway
  sendRequestInternal(context);

  return context->promise.get_future();
}

std::future<Response> McpClient::sendRequestWithParams(
    const std::string& method,
    const json::JsonValue& params,
    const std::map<std::string, std::string>& http_headers) {
  if (!circuit_breaker_->allowRequest()) {
    client_stats_.circuit_breaker_opens++;
    auto promise = std::make_shared<std::promise<Response>>();
    promise->set_value(Response::make_error(
        "", Error(::mcp::jsonrpc::INTERNAL_ERROR, "Circuit breaker open")));
    return promise->get_future();
  }

  RequestId id = static_cast<int64_t>(next_request_id_++);
  auto context = std::make_shared<RequestContext>(id, method);
  context->params_json = mcp::make_optional(params);
  context->http_headers = http_headers;
  context->start_time = std::chrono::steady_clock::now();
  traceRequest(*context);
  startClock(context);
  request_tracker_->trackRequest(context);
  sendRequestInternal(context);
  return context->promise.get_future();
}

CancellableRequest McpClient::sendCancellableRequest(
    const std::string& method,
    const json::JsonValue& params,
    const RequestOptions& options) {
  CancellableRequest sent;
  RequestId id = static_cast<int64_t>(next_request_id_++);
  sent.id = id;
  auto context = std::make_shared<RequestContext>(id, method);
  context->options = options;
  context->apart = true;
  context->start_time = std::chrono::steady_clock::now();

  json::JsonValue body = params.isObject() ? params : json::JsonValue::object();
  if (options.on_progress) {
    // The token is the request's own id, which nothing else can be using.
    json::JsonValue meta = body.contains("_meta") && body["_meta"].isObject()
                               ? body["_meta"]
                               : json::JsonValue::object();
    meta.set("progressToken", json::JsonValue(get<int64_t>(id)));
    body.set("_meta", meta);
    context->progress_token =
        mcp::make_optional(ProgressToken(get<int64_t>(id)));
    std::lock_guard<std::mutex> lock(progress_mutex_);
    progress_routes_[requestIdKeyToString(requestIdKey(id))] = id;
  }
  context->params_json = mcp::make_optional(body);
  sent.response = context->promise.get_future();

  if (!circuit_breaker_->allowRequest()) {
    client_stats_.circuit_breaker_opens++;
    context->finish(Response::make_error(
        id, Error(::mcp::jsonrpc::INTERNAL_ERROR, "Circuit breaker open")));
    return sent;
  }

  traceRequest(*context);
  startClock(context);
  request_tracker_->trackRequest(context);
  // Sent from the dispatcher, which is where a connection of its own can
  // be opened; from anywhere else it would share the connection, and
  // could not be cancelled alone.
  std::weak_ptr<bool> alive = alive_;
  postCarryingTrace([this, alive, context]() {
    if (!alive.expired()) {
      sendRequestInternal(context);
    }
  });
  return sent;
}

bool McpClient::cancelRequest(const RequestId& id, const std::string& reason) {
  auto context = request_tracker_->findByOrigin(id);
  if (!context || context->method == "initialize" || !main_dispatcher_) {
    return false;
  }
  std::weak_ptr<bool> alive = alive_;
  main_dispatcher_->post([this, alive, id, reason]() {
    if (alive.expired()) {
      return;
    }
    // Looked up again here: it may have been answered on the way.
    auto current = request_tracker_->findByOrigin(id);
    if (current) {
      abandonRequest(
          current,
          Error(::mcp::jsonrpc::REQUEST_CANCELLED, "the request was cancelled"),
          reason);
    }
  });
  return true;
}

bool McpClient::speaksModernHttp() const {
  return streamable_session_ && settled_transport_.has_value() &&
         settled_transport_.value() == TransportType::StreamableHttp &&
         protocol::modern::isModernVersion(
             streamable_session_->protocolVersion());
}

void McpClient::abandonRequest(const std::shared_ptr<RequestContext>& context,
                               const Error& error,
                               const std::string& reason) {
  if (!context || context->settled.load()) {
    return;
  }
  request_tracker_->removeRequest(context->id);
  if (context->task_id.has_value()) {
    // A task is cancelled with tasks/cancel and nothing else.
    json::JsonValue params = json::JsonValue::object();
    params.set("taskId", json::JsonValue(context->task_id.value()));
    sendInternalJson(protocol::tasks::kMethodCancel, params,
                     [](const Response&) {});
  } else if (context->sent_apart) {
    // Closing its own stream is the cancellation, and nothing is sent.
    // The close itself happens as it settles, below.
  } else if (speaksModernHttp()) {
    // On the shared connection: closing it would cancel every other
    // request on it, so this one is only let go of.
    GOPHER_LOG_DEBUG("{} abandoned without a signal on the shared connection",
                     context->method);
  } else if (context->method != "initialize" && connection_manager_ &&
             connected_) {
    // On stdio and in the earlier revisions, the server is told which
    // request the client no longer wants, by the id the server knows.
    json::JsonValue params = json::JsonValue::object();
    params.set("requestId", holds_alternative<std::string>(context->id)
                                ? json::JsonValue(get<std::string>(context->id))
                                : json::JsonValue(get<int64_t>(context->id)));
    if (!reason.empty()) {
      params.set("reason", json::JsonValue(reason));
    }
    jsonrpc::Notification cancelled("notifications/cancelled");
    cancelled.params_json = mcp::make_optional(params);
    connection_manager_->sendNotification(cancelled);
  }
  if (error.code == ::mcp::jsonrpc::REQUEST_TIMED_OUT) {
    client_stats_.requests_timeout++;
  }
  context->finish(Response::make_error(context->id, error));
}

void McpClient::armRequestTimeout(const RequestId& origin,
                                  std::chrono::milliseconds after) {
  const RequestIdKey key = requestIdKey(origin);
  auto deadline = request_deadlines_.find(key);
  if (deadline == request_deadlines_.end()) {
    return;
  }
  // Never past the overall maximum, whatever progress says.
  const auto now = std::chrono::steady_clock::now();
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline->second - now);
  if (left < after) {
    after = std::max(left, std::chrono::milliseconds(0));
  }
  auto& timer = request_timers_[key];
  if (!timer) {
    std::weak_ptr<bool> alive = alive_;
    timer = main_dispatcher_->createTimer([this, alive, origin]() {
      if (!alive.expired()) {
        onRequestTimedOut(origin);
      }
    });
  }
  timer->disableTimer();
  timer->enableTimer(after);
}

void McpClient::startClock(const std::shared_ptr<RequestContext>& context) {
  if (!main_dispatcher_) {
    return;
  }
  const RequestId origin = context->origin_id;
  const auto max = config_.request_timeout_max;
  const auto timeout =
      std::min(context->options.timeout.value_or(config_.request_timeout), max);
  const auto deadline = context->start_time + max;
  std::weak_ptr<bool> alive = alive_;

  auto previous = std::move(context->on_settled);
  context->on_settled = [this, alive, origin, previous]() {
    if (previous) {
      previous();
    }
    {
      std::lock_guard<std::mutex> lock(progress_mutex_);
      progress_routes_.erase(requestIdKeyToString(requestIdKey(origin)));
    }
    if (alive.expired() || !main_dispatcher_) {
      return;
    }
    main_dispatcher_->post([this, alive, origin]() {
      if (alive.expired()) {
        return;
      }
      const RequestIdKey key = requestIdKey(origin);
      auto timer = request_timers_.find(key);
      if (timer != request_timers_.end()) {
        timer->second->disableTimer();
        request_timers_.erase(timer);
      }
      request_deadlines_.erase(key);
    });
  };

  main_dispatcher_->post([this, alive, origin, deadline, timeout]() {
    if (alive.expired() || !request_tracker_->findByOrigin(origin)) {
      return;
    }
    request_deadlines_[requestIdKey(origin)] = deadline;
    armRequestTimeout(origin, timeout);
  });
}

void McpClient::onRequestTimedOut(const RequestId& origin) {
  auto context = request_tracker_->findByOrigin(origin);
  if (!context) {
    return;
  }
  abandonRequest(
      context,
      Error(::mcp::jsonrpc::REQUEST_TIMED_OUT, "the request timed out"),
      "the request timed out");
}

bool McpClient::routeProgress(const jsonrpc::Notification& notification) {
  if (notification.method != "notifications/progress") {
    return false;
  }
  ProgressNotification progress;
  try {
    const json::JsonValue params =
        notification.params_json.has_value()
            ? notification.params_json.value()
            : (notification.params.has_value()
                   ? json::metadataToExactJson(notification.params.value())
                   : json::JsonValue::object());
    progress = json::from_json<ProgressNotification>(params);
  } catch (const std::exception&) {
    return false;
  }
  const std::string token =
      holds_alternative<std::string>(progress.progressToken)
          ? get<std::string>(progress.progressToken)
          : std::to_string(get<int64_t>(progress.progressToken));

  std::function<void(double)> tracked;
  optional<RequestId> origin;
  {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    auto callback = progress_callbacks_.find(token);
    if (callback != progress_callbacks_.end()) {
      tracked = callback->second;
    }
    auto route = progress_routes_.find(token);
    if (route != progress_routes_.end()) {
      origin = route->second;
    }
  }
  if (tracked) {
    try {
      tracked(progress.progress);
    } catch (...) {
    }
  }
  if (!origin.has_value()) {
    return static_cast<bool>(tracked);
  }
  auto context = request_tracker_->findByOrigin(origin.value());
  if (!context || context->settled.load()) {
    return true;
  }
  if (context->options.on_progress) {
    try {
      context->options.on_progress(progress);
    } catch (...) {
      // A caller's callback failing is no reason to lose its request.
    }
  }
  if (context->options.progress_resets_timeout && main_dispatcher_) {
    const auto timeout =
        context->options.timeout.value_or(config_.request_timeout);
    const RequestId id = origin.value();
    if (main_dispatcher_->isThreadSafe()) {
      armRequestTimeout(id, timeout);
    } else {
      std::weak_ptr<bool> alive = alive_;
      main_dispatcher_->post([this, alive, id, timeout]() {
        if (!alive.expired()) {
          armRequestTimeout(id, timeout);
        }
      });
    }
  }
  return true;
}

protocol::trace::TraceContext McpClient::traceToSend() const {
  // A scope around the call wins, even an empty one: that is how a caller
  // says this request carries no trace, whatever the provider would say.
  if (protocol::trace::inScope()) {
    return protocol::trace::current();
  }
  if (config_.trace_context_provider) {
    return protocol::trace::sanitized(config_.trace_context_provider());
  }
  return protocol::trace::TraceContext();
}

void McpClient::traceRequest(RequestContext& context) const {
  context.trace = traceToSend();
  if (config_.span_hook) {
    protocol::trace::SpanStart start;
    start.kind = protocol::trace::SpanKind::Client;
    start.method = context.method;
    start.context = context.trace;
    context.span =
        std::make_shared<protocol::trace::Span>(config_.span_hook, start);
  }
}

void McpClient::postCarryingTrace(std::function<void()> task) {
  // Carried only when a scope is active here, so that one being active,
  // empty or not, is as true there as here.
  if (!protocol::trace::inScope()) {
    main_dispatcher_->post(std::move(task));
    return;
  }
  const protocol::trace::TraceContext trace = protocol::trace::current();
  main_dispatcher_->post([trace, task]() {
    protocol::trace::TraceScope scope(trace);
    task();
  });
}

// Send notification (fire-and-forget, no response expected)
VoidResult McpClient::sendNotification(const std::string& method,
                                       const optional<Metadata>& params) {
  // Check if connected
  if (!connected_ || !connection_manager_) {
    return makeError<std::nullptr_t>(
        Error(::mcp::jsonrpc::INTERNAL_ERROR, "Not connected"));
  }

  // Build JSON-RPC notification (no id field)
  Notification notification;
  notification.jsonrpc = "2.0";
  notification.method = method;
  notification.params = params;
  notification = protocol::trace::withContext(notification, traceToSend());

  // Send through connection manager
  // Post to dispatcher thread to ensure thread safety
  postCarryingTrace([this, notification]() {
    if (connection_manager_) {
      connection_manager_->sendNotification(notification);
    }
  });

  return makeSuccess<std::nullptr_t>(nullptr);
}

// Send request internally with retry logic
void McpClient::sendRequestInternal(std::shared_ptr<RequestContext> context) {
  GOPHER_LOG_DEBUG(
      "sendRequestInternal: method={}, connected_={}, isConnectionOpen()={}, "
      "retry_count={}",
      context->method, connected_.load(), isConnectionOpen(),
      context->retry_count);

  // Settled already: it ran out of time, or was cancelled, while waiting
  // to be sent, perhaps for a reconnect. Sending it now would start work
  // on the server for a caller that has been told it ended.
  if (context->settled.load()) {
    return;
  }
  // Check if connection is stale (idle for too long)
  auto now = std::chrono::steady_clock::now();
  auto idle_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                          now - last_activity_time_)
                          .count();
  bool is_stale = connected_ && (idle_seconds >= kConnectionIdleTimeoutSec);

  GOPHER_LOG_DEBUG(
      "sendRequestInternal stale check: idle_seconds={}, timeout={}, "
      "is_stale={}",
      idle_seconds, kConnectionIdleTimeoutSec, is_stale);

  // Check if connection is stale or not open - need to reconnect.
  //
  // Reconnect readiness is driven by dispatcher I/O and can take several
  // seconds for remote HTTPS/SSE backends, but it must leave request-deadline
  // headroom for the actual send and response.
  static constexpr int kReconnectRetryDelayMs = 10;
  const auto reconnect_wait_budget =
      reconnectWaitBudgetForRequestTimeout(config_.request_timeout);
  const auto max_reconnect_retries = static_cast<size_t>(std::max<int64_t>(
      1, reconnect_wait_budget.count() / kReconnectRetryDelayMs));

  // THREAD SAFETY: Use atomic connected_ flag instead of isConnectionOpen()
  // isConnectionOpen() reads McpConnectionManager::active_connection_ without
  // synchronization, creating a data race when called from user threads.
  // The atomic connected_ flag is safe to read from any thread.
  if (is_stale || !connected_) {
    // Track if this is a retry after reconnect
    if (context->retry_count > 0 &&
        context->retry_count <= max_reconnect_retries) {
      // This is a retry - check if we're connected now
      if (!connected_) {
        // Still not connected, schedule another retry with timer delay
        // Timer allows event loop to process I/O events (like TCP connect)
        // between retries
        context->retry_count++;
        // Held weakly: the context owns this timer, so a strong hold here
        // would keep both alive forever once the request settles. While it
        // waits, the tracker keeps it.
        std::weak_ptr<RequestContext> waiting = context;
        context->retry_timer = main_dispatcher_->createTimer([this, waiting]() {
          if (auto still = waiting.lock()) {
            sendRequestInternal(still);
          }
        });
        context->retry_timer->enableTimer(
            std::chrono::milliseconds(kReconnectRetryDelayMs));
        return;
      }
      // Connected now, proceed with send below
    } else if (context->retry_count > max_reconnect_retries) {
      // Too many retries
      context->finish(Response::make_error(
          context->id, Error(::mcp::jsonrpc::INTERNAL_ERROR,
                             "Connection not ready after reconnect")));
      request_tracker_->removeRequest(context->id);
      client_stats_.requests_failed++;
      return;
    } else {
      // First attempt - need to reconnect
      // Attempt to reconnect (async - just initiates connection)
      auto reconnect_result = reconnect();
      if (is_error<std::nullptr_t>(reconnect_result)) {
        context->finish(Response::make_error(
            context->id, Error(::mcp::jsonrpc::INTERNAL_ERROR,
                               "Connection closed and reconnect failed")));
        request_tracker_->removeRequest(context->id);
        client_stats_.requests_failed++;
        return;
      }

      // Reconnect initiated - schedule retry to allow connection event to be
      // processed
      context->retry_count = 1;
      postCarryingTrace([this, context]() { sendRequestInternal(context); });
      return;
    }
  }

  // Double-check connection after potential reconnect
  if (!connected_ || !connection_manager_) {
    context->finish(Response::make_error(
        context->id, Error(::mcp::jsonrpc::INTERNAL_ERROR, "Not connected")));
    request_tracker_->removeRequest(context->id);
    client_stats_.requests_failed++;
    return;
  }

  // Build JSON-RPC request
  Request request;
  request.jsonrpc = "2.0";
  request.method = context->method;
  request.params = context->params;
  request.params_json = context->params_json;
  request.id = context->id;
  // Added at each send, not once: a retry is built afresh from the
  // context, and the keys an application set in _meta itself are kept.
  request = protocol::trace::withContext(request, context->trace);

  // A request the caller may cancel goes out on a connection of its own
  // in 2026-07-28 Streamable HTTP, so that closing it cancels this request
  // and no other. Anywhere else it shares the connection like any other
  // and is cancelled by message.
  if (context->apart && speaksModernHttp()) {
    if (!main_dispatcher_->isThreadSafe() ||
        !connection_manager_->openSubscription(request.id,
                                               json::to_json(request))) {
      // Not sent on the shared connection instead: there it could not be
      // cancelled without cancelling every other request, and a caller who
      // asked for a request it can cancel is not handed one it cannot.
      request_tracker_->removeRequest(context->id);
      client_stats_.requests_failed++;
      context->finish(Response::make_error(
          context->id,
          Error(::mcp::jsonrpc::INTERNAL_ERROR,
                "could not open a connection of its own for a request that "
                "has to be cancellable")));
      return;
    }
    context->sent_apart = true;
    const RequestId sent_as = request.id;
    std::weak_ptr<bool> alive = alive_;
    auto previous = std::move(context->on_settled);
    context->on_settled = [this, alive, sent_as, previous]() {
      if (previous) {
        previous();
      }
      // Posted: the answer that settles it is being read off that very
      // connection, and closing it there tears down what is being read.
      if (alive.expired() || !main_dispatcher_) {
        return;
      }
      main_dispatcher_->post([this, alive, sent_as]() {
        if (!alive.expired() && connection_manager_) {
          connection_manager_->closeSubscription(sent_as);
        }
      });
    };
    client_stats_.requests_total++;
    return;
  }

  GOPHER_LOG_DEBUG("Sending request through connection_manager: method={}",
                   context->method);

  // CRITICAL FIX: Update activity time BEFORE sending request
  // This prevents stale connection detection while waiting for response
  // Without this, connections are marked stale if idle_seconds >= timeout,
  // causing reconnection while the request is in flight
  last_activity_time_ = std::chrono::steady_clock::now();

  // Send through connection manager
  auto send_result =
      connection_manager_->sendRequest(request, context->http_headers);

  GOPHER_LOG_DEBUG("sendRequest result: is_error={}",
                   is_error<std::nullptr_t>(send_result));

  if (is_error<std::nullptr_t>(send_result)) {
    // Send failed, check if we should retry
    if (context->retry_count < config_.max_retries) {
      context->retry_count++;
      client_stats_.requests_retried++;

      // Schedule retry with exponential backoff
      auto delay = std::chrono::milliseconds(100 * (1 << context->retry_count));
      // Note: In production, this would use a timer to retry
      // For now, we'll fail immediately
      context->finish(Response::make_error(
          context->id, *get_error<std::nullptr_t>(send_result)));
    } else {
      // Max retries exceeded
      context->finish(Response::make_error(
          context->id, *get_error<std::nullptr_t>(send_result)));
      client_stats_.requests_failed++;
    }

    request_tracker_->removeRequest(context->id);
    circuit_breaker_->recordFailure();
  } else {
    // Request sent successfully
    // Track bytes sent
  }
}

// Handle incoming response
void McpClient::handleResponse(const Response& response) {
  // Update last activity time - we received data from the server
  last_activity_time_ = std::chrono::steady_clock::now();

  // Who answered, when the result says. Kept for display and logging, and
  // never acted on; one that is missing or malformed changes nothing.
  if (response.result.has_value()) {
    json::JsonValue result;
    if (resultAsJson(response, &result) && result.isObject() &&
        result.contains("_meta") && result["_meta"].isObject() &&
        result["_meta"].contains(protocol::modern::kMetaServerInfo)) {
      auto who =
          implementationIn(result["_meta"][protocol::modern::kMetaServerInfo]);
      if (who.has_value()) {
        std::lock_guard<std::mutex> lock(server_info_mutex_);
        last_server_info_ = std::move(who);
      }
    }
  }

  // Find corresponding request
  auto request = request_tracker_->getRequest(response.id);
  if (!request) {
    // No matching request
    return;
  }

  // Complete request
  if (request->completed) {
    return;
  }

  // Refused for headers that no longer match what the server designates:
  // learnt again and sent once more, rather than failed for something
  // the caller did not get wrong.
  if (recoverFromHeaderMismatch(request, response)) {
    return;
  }

  // Answered with a task rather than the answer: followed to its end.
  if (followTask(request, response)) {
    return;
  }

  // An answer that turns out to be a question is not this request's
  // answer. Either it goes out again carrying what was asked for — in
  // which case there is nothing to complete here, the same caller now
  // waiting on a request with a different id — or it could not, and the
  // failure is the answer.
  Error unanswerable(0, std::string());
  if (answerIsAQuestion(response)) {
    if (askAndSendAgain(request, response, &unanswerable)) {
      return;
    }
    request->completed = true;
    completeRequest(request, Response::make_error(response.id, unanswerable));
    return;
  }

  request->completed = true;
  completeRequest(request, response);
}

bool McpClient::answerIsAQuestion(const Response& response) const {
  if (!streamable_session_ || !protocol::modern::isModernVersion(
                                  streamable_session_->protocolVersion())) {
    // No older revision can ask, so no older revision's answer is ever
    // read as a question.
    return false;
  }
  if (response.error.has_value() || !response.result.has_value()) {
    return false;
  }
  json::JsonValue result;
  if (!resultAsJson(response, &result)) {
    return false;
  }
  return protocol::modern::askedForIn(result).asked;
}

bool McpClient::resultAsJson(const Response& response, json::JsonValue* out) {
  if (!response.result.has_value()) {
    return false;
  }
  if (holds_alternative<json::JsonValue>(response.result.value())) {
    *out = get<json::JsonValue>(response.result.value());
    return true;
  }
  if (holds_alternative<Metadata>(response.result.value())) {
    *out = json::metadataToJson(get<Metadata>(response.result.value()));
    return true;
  }
  return false;
}

bool McpClient::askAndSendAgain(const std::shared_ptr<RequestContext>& request,
                                const Response& response,
                                Error* why_not) {
  json::JsonValue result;
  if (!resultAsJson(response, &result)) {
    return false;
  }
  const auto asked = protocol::modern::askedForIn(result);

  if (request->input_rounds >= config_.streamable_http.mrtr_max_rounds) {
    // A server that answers every round by asking for something else
    // would otherwise keep one request going forever.
    *why_not = Error(::mcp::jsonrpc::INTERNAL_ERROR,
                     "the server asked for something " +
                         std::to_string(request->input_rounds) +
                         " times running and never answered");
    return false;
  }

  // Built as JSON, not as the flat map: a state handed back through the
  // map would come out as an object if it happened to look like one, and
  // the one rule the state has is that it comes back byte for byte.
  json::JsonValue params = json::JsonValue::object();
  if (request->params_json.has_value() && request->params_json->isObject()) {
    params = request->params_json.value();
  } else if (request->params.has_value()) {
    params = json::metadataToJson(request->params.value());
  }

  // Every question gets an entry, including the ones nothing could
  // answer: a server that asked two and gets one key back cannot tell
  // which of the two went unanswered.
  std::map<std::string, json::JsonValue> answers;
  for (const auto& entry : asked.requests) {
    answers[entry.first] = askOurselves(entry.second);
  }

  if (!answers.empty()) {
    params.set(protocol::modern::kInputResponsesField,
               protocol::modern::renderInputResponses(answers));
  }
  if (asked.request_state.has_value()) {
    params.set(protocol::modern::kRequestStateField,
               json::JsonValue(asked.request_state.value()));
  }

  // A new request, not a retry of this one. The revision is explicit
  // that the second round carries an id of its own — the two rounds are
  // independent requests, and a server must never be able to read a
  // repeated id as one conversation it is expected to remember.
  auto again = carryOver(request, params);
  again->input_rounds = request->input_rounds + 1;

  GOPHER_LOG_DEBUG("{} is being sent again with what was asked for (round {})",
                   request->method, again->input_rounds);
  sendRequestInternal(again);
  return true;
}

std::shared_ptr<RequestContext> McpClient::carryOver(
    const std::shared_ptr<RequestContext>& request,
    const json::JsonValue& params) {
  RequestId fresh = static_cast<int64_t>(next_request_id_++);
  auto again = std::make_shared<RequestContext>(fresh, request->method);
  again->params = request->params;
  again->params_json = mcp::make_optional(params);
  again->http_headers = request->http_headers;
  again->start_time = request->start_time;
  again->input_rounds = request->input_rounds;
  again->header_retried = request->header_retried;
  // The caller is waiting on the first request's future and knows
  // nothing of this one, so what it waits on has to move across.
  again->promise = std::move(request->promise);
  again->on_response = request->on_response;
  // Still the caller's one request, under the id it knows, with its own
  // way of being sent and its trace. Its clock is kept by that id, so it
  // runs on across both.
  again->origin_id = request->origin_id;
  again->options = request->options;
  again->apart = request->apart;
  again->progress_token = request->progress_token;
  again->trace = request->trace;
  again->span = std::move(request->span);
  again->on_settled = std::move(request->on_settled);
  // The first one's own connection, if it had one, is done with.
  if (request->sent_apart && main_dispatcher_) {
    const RequestId first = request->id;
    std::weak_ptr<bool> alive = alive_;
    main_dispatcher_->post([this, alive, first]() {
      if (!alive.expired() && connection_manager_) {
        connection_manager_->closeSubscription(first);
      }
    });
  }
  request->settled = true;
  request->completed = true;
  request_tracker_->removeRequest(request->id);
  request_tracker_->trackRequest(again);
  return again;
}

bool McpClient::recoverFromHeaderMismatch(
    const std::shared_ptr<RequestContext>& request, const Response& response) {
  if (!response.error.has_value() ||
      response.error->code != protocol::modern::kHeaderMismatch ||
      request->method != "tools/call" || request->header_retried ||
      !speaksModernHttp()) {
    return false;
  }
  // Once only, whatever happens next: a second mismatch is the answer.
  request->header_retried = true;
  GOPHER_LOG_DEBUG(
      "tools/call refused for its mirrored headers; listing tools again");

  // Kept tracked under its first id while the tools are listed, so a
  // cancellation or timeout in the meantime still finds it.
  std::weak_ptr<RequestContext> waiting = request;
  relistToolsThen(nullopt, 0, [this, waiting]() {
    auto request = waiting.lock();
    if (!request || request->settled.load()) {
      return;
    }
    json::JsonValue params = json::JsonValue::object();
    if (request->params_json.has_value()) {
      params = request->params_json.value();
    } else if (request->params.has_value()) {
      params = json::metadataToJson(request->params.value());
    }
    // Sent once more whatever the listing said: the server's answer to it
    // is the caller's answer, a tool that is gone included.
    auto again = carryOver(request, params);
    sendRequestInternal(again);
  });
  return true;
}

void McpClient::relistToolsThen(const optional<std::string>& cursor,
                                size_t pages,
                                std::function<void()> done) {
  // Bounded, so a server that never stops paging cannot keep the call
  // waiting forever.
  constexpr size_t kMaxPages = 100;
  optional<Metadata> params;
  if (cursor.has_value()) {
    Metadata page;
    page["cursor"] = cursor.value();
    params = mcp::make_optional(page);
  }
  std::weak_ptr<bool> alive = alive_;
  sendInternalRequest(
      "tools/list", params,
      [this, alive, pages, done](const Response& response) {
        if (alive.expired()) {
          return;
        }
        optional<std::string> next;
        // Read as listTools reads it: the answer may arrive typed, or as
        // the JSON it was sent as.
        optional<ListToolsResult> listed;
        if (!response.error.has_value() && response.result.has_value()) {
          const auto& result = response.result.value();
          if (holds_alternative<ListToolsResult>(result)) {
            listed = get<ListToolsResult>(result);
          } else if (holds_alternative<std::vector<Tool>>(result)) {
            ListToolsResult tools;
            tools.tools = get<std::vector<Tool>>(result);
            listed = tools;
          } else {
            json::JsonValue body;
            if (resultAsJson(response, &body) && body.isObject()) {
              try {
                listed = json::from_json<ListToolsResult>(body);
              } catch (const std::exception& e) {
                GOPHER_LOG_DEBUG("tools could not be listed again: {}",
                                 e.what());
              }
            }
          }
        }
        if (listed.has_value()) {
          if (streamable_session_) {
            streamable_session_->acceptListing(listed->tools);
          }
          if (listed->nextCursor.has_value() && !listed->nextCursor->empty()) {
            next = listed->nextCursor;
          }
        }
        if (next.has_value() && pages + 1 < kMaxPages) {
          relistToolsThen(next, pages + 1, done);
          return;
        }
        done();
      });
}

void McpClient::sendInternalJson(
    const std::string& method,
    const json::JsonValue& params,
    std::function<void(const Response&)> on_response) {
  RequestId id = static_cast<int64_t>(next_request_id_++);
  auto context = std::make_shared<RequestContext>(id, method);
  context->params_json = mcp::make_optional(params);
  context->start_time = std::chrono::steady_clock::now();
  context->on_response = std::move(on_response);
  request_tracker_->trackRequest(context);
  sendRequestInternal(context);
}

bool McpClient::followTask(const std::shared_ptr<RequestContext>& request,
                           const Response& response) {
  if (request->method != "tools/call" || response.error.has_value() ||
      !response.result.has_value()) {
    return false;
  }
  json::JsonValue body;
  if (!resultAsJson(response, &body) || !protocol::tasks::isTaskResult(body)) {
    return false;
  }
  auto fail = [this, &request](const std::string& why) {
    request->completed = true;
    completeRequest(
        request, Response::make_error(
                     request->id, Error(::mcp::jsonrpc::INTERNAL_ERROR, why)));
  };
  if (!config_.accept_tasks) {
    fail("the server answered with a task, which this client did not accept");
    return true;
  }
  const auto task = protocol::tasks::fromJson(body);
  if (!task.has_value()) {
    fail("the server answered with a task that could not be read");
    return true;
  }

  // The call now lasts as long as its task: its own clock stops, and only
  // the task's end, or the caller cancelling, answers it.
  request->task_id = task->taskId;
  const RequestIdKey key = requestIdKey(request->origin_id);
  auto timer = request_timers_.find(key);
  if (timer != request_timers_.end()) {
    timer->second->disableTimer();
    request_timers_.erase(timer);
  }
  request_deadlines_.erase(key);
  GOPHER_LOG_DEBUG("tools/call became task {}", task->taskId);
  onTaskState(request, task.value());
  return true;
}

void McpClient::onTaskState(const std::shared_ptr<RequestContext>& request,
                            const protocol::tasks::Task& task) {
  if (request->settled.load()) {
    return;
  }
  namespace tasks = protocol::tasks;
  const auto settle = [this, &request](const Response& answer) {
    request->completed = true;
    request_tracker_->removeRequest(request->id);
    completeRequest(request, answer);
  };
  switch (task.status) {
    case tasks::Status::Completed:
      // A completed task always carries its result; one that didn't was
      // never read as a task.
      settle(Response::success(request->id,
                               jsonrpc::ResponseResult(task.result.value())));
      return;
    case tasks::Status::Failed:
      settle(Response::make_error(
          request->id,
          task.error.has_value()
              ? task.error.value()
              : Error(::mcp::jsonrpc::INTERNAL_ERROR, "the task failed")));
      return;
    case tasks::Status::Cancelled:
      settle(Response::make_error(
          request->id,
          Error(::mcp::jsonrpc::REQUEST_CANCELLED, "the task was cancelled")));
      return;
    case tasks::Status::InputRequired: {
      // Each request answered once, however often it is seen again.
      json::JsonValue asking = json::JsonValue::object();
      asking.set(protocol::modern::kResultTypeField,
                 json::JsonValue(protocol::modern::kResultTypeInputRequired));
      asking.set(protocol::modern::kInputRequestsField, task.inputRequests);
      const auto asked = protocol::modern::askedForIn(asking);
      std::map<std::string, json::JsonValue> answers;
      for (const auto& entry : asked.requests) {
        if (request->input_answers.count(entry.first) == 0) {
          request->input_answers[entry.first] = askOurselves(entry.second);
        }
        if (request->input_sent.insert(entry.first).second) {
          answers[entry.first] = request->input_answers[entry.first];
        }
      }
      if (!answers.empty()) {
        json::JsonValue params = json::JsonValue::object();
        params.set("taskId", json::JsonValue(task.taskId));
        params.set(protocol::modern::kInputResponsesField,
                   protocol::modern::renderInputResponses(answers));
        std::vector<std::string> keys;
        for (const auto& answer : answers) {
          keys.push_back(answer.first);
        }
        std::weak_ptr<RequestContext> waiting = request;
        sendInternalJson(protocol::tasks::kMethodUpdate, params,
                         [waiting, keys](const Response& response) {
                           // Not taken: sent again on the next poll that still
                           // asks.
                           auto request = waiting.lock();
                           if (request && response.error.has_value()) {
                             for (const auto& key : keys) {
                               request->input_sent.erase(key);
                             }
                           }
                         });
      }
      break;
    }
    case tasks::Status::Working:
      break;
  }
  pollTaskLater(request,
                std::chrono::milliseconds(task.pollIntervalMs.value_or(1000)));
}

void McpClient::pollTaskLater(const std::shared_ptr<RequestContext>& request,
                              std::chrono::milliseconds after) {
  if (!main_dispatcher_ || !request->task_id.has_value()) {
    return;
  }
  // At the server's pace, but never so fast as to be a busy loop, nor so
  // slow a finished task goes unnoticed for long.
  after = std::max(after, std::chrono::milliseconds(10));
  after = std::min(after, std::chrono::milliseconds(60000));
  std::weak_ptr<RequestContext> waiting = request;
  std::weak_ptr<bool> alive = alive_;
  request->poll_timer = main_dispatcher_->createTimer([this, alive, waiting]() {
    auto request = waiting.lock();
    if (alive.expired() || !request || request->settled.load()) {
      return;
    }
    json::JsonValue params = json::JsonValue::object();
    params.set("taskId", json::JsonValue(request->task_id.value()));
    sendInternalJson(
        protocol::tasks::kMethodGet, params,
        [this, alive, waiting](const Response& response) {
          auto request = waiting.lock();
          if (alive.expired() || !request || request->settled.load()) {
            return;
          }
          if (response.error.has_value()) {
            // Gone or expired: what the server said is the answer.
            request->completed = true;
            request_tracker_->removeRequest(request->id);
            completeRequest(request, Response::make_error(
                                         request->id, response.error.value()));
            return;
          }
          json::JsonValue body;
          optional<protocol::tasks::Task> task;
          if (resultAsJson(response, &body)) {
            task = protocol::tasks::fromJson(body);
          }
          if (!task.has_value()) {
            request->completed = true;
            request_tracker_->removeRequest(request->id);
            completeRequest(
                request,
                Response::make_error(request->id,
                                     Error(::mcp::jsonrpc::INTERNAL_ERROR,
                                           "tasks/get answered with no task")));
            return;
          }
          onTaskState(request, task.value());
        });
  });
  request->poll_timer->enableTimer(after);
}

std::future<jsonrpc::Response> McpClient::sendFromDispatcher(
    const std::string& method, const json::JsonValue& params) {
  auto answered = std::make_shared<std::promise<Response>>();
  auto future = answered->get_future();
  if (!main_dispatcher_) {
    answered->set_value(Response::make_error(
        RequestId(), Error(::mcp::jsonrpc::INTERNAL_ERROR, "No dispatcher")));
    return future;
  }
  std::weak_ptr<bool> alive = alive_;
  postCarryingTrace([this, alive, method, params, answered]() {
    if (alive.expired()) {
      answered->set_value(Response::make_error(
          RequestId(), Error(::mcp::jsonrpc::INTERNAL_ERROR, "client gone")));
      return;
    }
    sendInternalJson(method, params, [answered](const Response& response) {
      answered->set_value(response);
    });
  });
  return future;
}

std::future<protocol::tasks::Task> McpClient::getTask(
    const std::string& task_id) {
  json::JsonValue params = json::JsonValue::object();
  params.set("taskId", json::JsonValue(task_id));
  auto asked = std::make_shared<std::future<Response>>(
      sendFromDispatcher(protocol::tasks::kMethodGet, params));
  auto task = std::make_shared<std::promise<protocol::tasks::Task>>();
  auto future = task->get_future();
  std::thread([asked, task]() {
    try {
      const Response response = asked->get();
      if (response.error.has_value()) {
        throw RequestError(response.error.value());
      }
      json::JsonValue body;
      optional<protocol::tasks::Task> read;
      if (resultAsJson(response, &body)) {
        read = protocol::tasks::fromJson(body);
      }
      if (!read.has_value()) {
        throw std::runtime_error("tasks/get answered with no task");
      }
      task->set_value(read.value());
    } catch (...) {
      task->set_exception(std::current_exception());
    }
  }).detach();
  return future;
}

std::future<jsonrpc::Response> McpClient::updateTask(
    const std::string& task_id,
    const std::map<std::string, json::JsonValue>& input_responses) {
  json::JsonValue params = json::JsonValue::object();
  params.set("taskId", json::JsonValue(task_id));
  params.set(protocol::modern::kInputResponsesField,
             protocol::modern::renderInputResponses(input_responses));
  return sendFromDispatcher(protocol::tasks::kMethodUpdate, params);
}

std::future<jsonrpc::Response> McpClient::cancelTask(
    const std::string& task_id) {
  json::JsonValue params = json::JsonValue::object();
  params.set("taskId", json::JsonValue(task_id));
  return sendFromDispatcher(protocol::tasks::kMethodCancel, params);
}

json::JsonValue McpClient::askOurselves(
    const protocol::modern::InputRequest& asked) {
  std::function<jsonrpc::ResponseResult(const jsonrpc::Request&)> handler;
  {
    std::lock_guard<std::mutex> lock(request_handlers_mutex_);
    auto it = request_handlers_.find(asked.method);
    if (it != request_handlers_.end()) {
      handler = it->second;
    }
  }
  if (!handler) {
    GOPHER_LOG_WARN("asked for {}, which this client has no handler for",
                    asked.method);
    return json::JsonValue();
  }

  // The same handlers that answer a server which asks by sending a
  // request. What is being asked has not changed with the era; only how
  // the asking travels has.
  jsonrpc::Request question;
  question.jsonrpc = "2.0";
  question.id = static_cast<int64_t>(next_request_id_++);
  question.method = asked.method;
  question.params = mcp::make_optional(json::jsonToMetadata(asked.params));
  question.params_json = mcp::make_optional(asked.params);

  try {
    jsonrpc::Response answered;
    answered.jsonrpc = "2.0";
    answered.id = question.id;
    answered.result = mcp::make_optional(handler(question));
    const auto as_json = json::to_json(answered);
    return as_json.contains("result") ? as_json["result"] : json::JsonValue();
  } catch (const std::exception& e) {
    // Nothing for this one, and the server is told which one by its name
    // being there with nothing under it.
    GOPHER_LOG_WARN("could not answer {}: {}", asked.method, e.what());
    return json::JsonValue();
  }
}

void McpClient::completeRequest(const std::shared_ptr<RequestContext>& request,
                                const Response& response) {
  // A subscription's answer is what says it has ended, and a server may
  // end one itself. Nothing else here would notice: the request is
  // completed like any other.
  releaseIfSubscription(*request);

  // If the stream was carrying this answer, it has carried it, and is a
  // plain stream again.
  if (stream_recovering_.has_value() &&
      holds_alternative<int64_t>(stream_recovering_.value()) &&
      holds_alternative<int64_t>(response.id) &&
      get<int64_t>(stream_recovering_.value()) == get<int64_t>(response.id)) {
    stream_recovering_.reset();
  }
  request->finish(response);
  request_tracker_->removeRequest(response.id);

  // Work the client itself has to carry on with, on this thread. A
  // caller waits on the future; the client cannot, because the wait
  // would be on the thread the answer arrives on.
  if (request->on_response) {
    request->on_response(response);
  }

  // Update stats
  if (response.error.has_value()) {
    client_stats_.requests_failed++;
    circuit_breaker_->recordFailure();
  } else {
    client_stats_.requests_success++;
    circuit_breaker_->recordSuccess();

    // Track latency
    auto duration = std::chrono::steady_clock::now() - request->start_time;
    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    client_stats_.request_duration_ms_total += duration_ms;
    client_stats_.request_duration_ms_min =
        std::min(client_stats_.request_duration_ms_min.load(),
                 static_cast<uint64_t>(duration_ms));
    client_stats_.request_duration_ms_max =
        std::max(client_stats_.request_duration_ms_max.load(),
                 static_cast<uint64_t>(duration_ms));
  }
}

// Handle incoming request (server calling client)
void McpClient::handleRequest(const Request& request) {
  std::function<jsonrpc::ResponseResult(const jsonrpc::Request&)> handler;
  {
    std::lock_guard<std::mutex> lock(request_handlers_mutex_);
    auto it = request_handlers_.find(request.method);
    if (it != request_handlers_.end()) {
      handler = it->second;
    }
  }

  if (!handler && request.method == "ping") {
    // Answered by every client, with an empty result and nothing else, as
    // the spec requires; an application's own handler still comes first.
    Response response;
    response.jsonrpc = "2.0";
    response.id = request.id;
    response.result =
        mcp::make_optional(jsonrpc::ResponseResult(json::JsonValue::object()));
    connection_manager_->sendResponse(response);
    return;
  }

  if (!handler) {
    // Refused, but answered: a server that asked is waiting, and an
    // unanswered question is worse for it than a refused one.
    connection_manager_->sendResponse(Response::make_error(
        request.id, Error(::mcp::jsonrpc::METHOD_NOT_FOUND,
                          "This client does not answer " + request.method)));
    return;
  }

  try {
    Response response;
    response.jsonrpc = "2.0";
    response.id = request.id;
    response.result = mcp::make_optional(handler(request));
    connection_manager_->sendResponse(response);
  } catch (const std::exception& e) {
    // Whatever went wrong in the handler is the answer, because the
    // server is waiting for one either way.
    connection_manager_->sendResponse(Response::make_error(
        request.id, Error(::mcp::jsonrpc::INTERNAL_ERROR, e.what())));
  }
}

void McpClient::registerRequestHandler(
    const std::string& method,
    std::function<jsonrpc::ResponseResult(const jsonrpc::Request&)> handler) {
  {
    std::lock_guard<std::mutex> lock(request_handlers_mutex_);
    request_handlers_[method] = std::move(handler);
  }

  // What this client says it can do follows from what it can actually
  // answer, so the two cannot drift apart. It matters in the newest
  // revision, where every request declares it and a server refuses to
  // ask for anything not declared — a client with a handler and no
  // declaration would be refused the very question it could answer.
  if (streamable_session_) {
    streamable_session_->setClientCapabilities(declaredCapabilities());
  }
}

bool McpClient::runOnDispatcher(std::function<void()> work) {
  if (!main_dispatcher_) {
    return false;
  }
  if (main_dispatcher_->isThreadSafe()) {
    work();
    return true;
  }
  if (shutting_down_) {
    // The loop this would be posted to is being told to stop, and a
    // callback queued behind that may never run. Refused rather than
    // waited for: a caller that cannot be served is better told so than
    // held until the process ends.
    return false;
  }

  // Waited for rather than fired and forgotten: what these do is decide
  // whether a subscription exists, and a caller told "yes" before that
  // was settled would be told something not yet true.
  //
  // Bounded, because "the dispatcher will get to it" stops being true
  // exactly when this matters — a shutdown that began after the check
  // above still leaves the post unrun, and an unbounded wait for it is
  // a hang with no way out.
  auto done = std::make_shared<std::promise<void>>();
  auto waiting = done->get_future();
  auto ran = std::make_shared<std::atomic<bool>>(false);

  // Everything the posted work touches is held by the post itself. A
  // caller that stops waiting returns and its locals go, so work
  // reaching the dispatcher afterwards must not be reading any of them —
  // which is why this takes the work by value and callers hand it
  // nothing by reference.
  postCarryingTrace([work, done, ran]() {
    work();
    ran->store(true);
    done->set_value();
  });

  if (waiting.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
    // It may still run. What is promised here is only that this call did
    // not see it happen, and a caller told so has to undo anything it
    // would have done — this cannot, having no idea what the work was.
    GOPHER_LOG_WARN(
        "the dispatcher did not run subscription work within its window; "
        "treating it as not done");
    return false;
  }
  return ran->load();
}

int64_t McpClient::listen(
    const protocol::modern::NotificationFilter& what,
    std::function<void(const jsonrpc::Notification&)> on_notification) {
  if (shutting_down_) {
    // Opening one now would be holding a connection this client is in
    // the middle of letting go of.
    return 0;
  }
  if (!connection_manager_ || !streamable_session_ ||
      !protocol::modern::isModernVersion(
          streamable_session_->protocolVersion())) {
    // Every earlier revision reaches its client by other means, and this
    // is not one of them.
    GOPHER_LOG_WARN(
        "subscriptions belong to a revision this conversation "
        "is not in");
    return 0;
  }
  if (what.empty()) {
    // A subscription that asked for nothing would hear nothing, which is
    // a connection held open for no reason.
    GOPHER_LOG_WARN("a subscription that asks for nothing was not opened");
    return 0;
  }

  const int64_t id = static_cast<int64_t>(next_request_id_++);

  json::JsonValue params = json::JsonValue::object();
  params.set(protocol::modern::kFilterField, what.render());

  json::JsonValue message = json::JsonValue::object();
  message.set("jsonrpc", json::JsonValue("2.0"));
  message.set("id", json::JsonValue(id));
  message.set("method",
              json::JsonValue(protocol::modern::kMethodSubscriptionsListen));
  message.set("params", params);

  {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    subscriptions_[id] = std::move(on_notification);
  }

  // Tracked like any other request, because that is what it is: its
  // answer arrives when the subscription ends, and until then nothing
  // completes it. Whoever wants to know when that happens waits on this.
  auto context = std::make_shared<RequestContext>(
      RequestId(id), std::string(protocol::modern::kMethodSubscriptionsListen));
  context->start_time = std::chrono::steady_clock::now();
  request_tracker_->trackRequest(context);

  // Held by the work rather than by this frame: if the wait below gives
  // up, this frame goes and the post may still run.
  auto opened = std::make_shared<std::atomic<bool>>(false);
  std::weak_ptr<bool> alive = alive_;
  const bool ran = runOnDispatcher([this, alive, opened, id, message]() {
    if (alive.expired() || !connection_manager_) {
      return;
    }
    opened->store(
        connection_manager_->openSubscription(RequestId(id), message));
  });

  if (!ran || !opened->load()) {
    // Through the same path an ending takes, because this is one: it
    // forgets what was wanted and lets go of the connection, and a
    // subscription that opened after this call gave up on it is exactly
    // what that last part is for — an orphan nobody was told about is a
    // connection held open for a subscription no caller can end.
    releaseSubscription(id);
    request_tracker_->removeRequest(RequestId(id));
    return 0;
  }

  GOPHER_LOG_DEBUG("listening under {}", id);
  return id;
}

void McpClient::stopListening(int64_t subscription) {
  if (releaseSubscription(subscription)) {
    request_tracker_->removeRequest(RequestId(subscription));
  }
}

bool McpClient::releaseSubscription(int64_t subscription) {
  {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    if (subscriptions_.erase(subscription) == 0) {
      return false;
    }
  }
  if (!connection_manager_ || !main_dispatcher_) {
    return true;
  }

  // The connection exists for this subscription and nothing else, so it
  // goes when the subscription does — whether this client ended it or
  // the server did.
  //
  // Handed to the dispatcher rather than done here, and that matters
  // most in the case that looks like it needs it least: a server ending
  // a subscription is discovered while its own connection's bytes are
  // being parsed, and closing it there tears down the buffer the parse
  // is still walking. Posted, it happens once that has finished.
  //
  // Dropped unrun if the loop stops first, which is safe: shutdown lets
  // go of every subscription connection on its way out.
  std::weak_ptr<bool> alive = alive_;
  postCarryingTrace([this, alive, subscription]() {
    if (alive.expired() || !connection_manager_) {
      return;
    }
    connection_manager_->closeSubscription(RequestId(subscription));
  });
  return true;
}

size_t McpClient::subscriptionsHeld() const {
  std::lock_guard<std::mutex> lock(subscriptions_mutex_);
  return subscriptions_.size();
}

bool McpClient::routeToSubscription(const jsonrpc::Notification& notification) {
  if (!notification.params.has_value()) {
    return false;
  }
  const auto& params = notification.params.value();
  auto meta = params.find("_meta");
  if (meta == params.end() || !holds_alternative<std::string>(meta->second)) {
    return false;
  }

  int64_t id = 0;
  try {
    auto parsed = json::JsonValue::parse(get<std::string>(meta->second));
    if (!parsed.isObject() ||
        !parsed.contains(protocol::modern::kMetaSubscriptionId) ||
        !parsed[protocol::modern::kMetaSubscriptionId].isInteger()) {
      return false;
    }
    id = parsed[protocol::modern::kMetaSubscriptionId].getInt64();
  } catch (const std::exception&) {
    return false;
  }

  std::function<void(const jsonrpc::Notification&)> wanted;
  {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    auto it = subscriptions_.find(id);
    if (it == subscriptions_.end()) {
      // Named a subscription this client is not holding. Not routed and
      // not handed on either: it was addressed to something, and that
      // something is not the application's notification handlers.
      GOPHER_LOG_DEBUG(
          "a message arrived for subscription {}, which is not "
          "one this client holds",
          id);
      return true;
    }
    wanted = it->second;
  }

  if (wanted) {
    wanted(notification);
  }
  return true;
}

json::JsonValue McpClient::declaredCapabilities() const {
  json::JsonValue declared = json::to_json(config_.capabilities);
  if (!declared.isObject()) {
    declared = json::JsonValue::object();
  }
  std::map<std::string, json::JsonValue> configured = config_.extensions;
  if (config_.accept_tasks &&
      configured.count(protocol::tasks::kExtensionId) == 0) {
    configured[protocol::tasks::kExtensionId] = json::JsonValue::object();
  }
  const json::JsonValue extensions =
      protocol::extensions::merged(config_.capabilities.extensions, configured);
  if (!extensions.keys().empty()) {
    declared.set(protocol::extensions::kField, extensions);
  }

  std::lock_guard<std::mutex> lock(request_handlers_mutex_);
  for (const auto& entry : request_handlers_) {
    const std::string capability = protocol::modern::capabilityFor(entry.first);
    if (capability.empty() || declared.contains(capability)) {
      continue;
    }
    // An empty object is a declaration: it says this can be done and
    // nothing further about how.
    declared.set(capability, json::JsonValue::object());
  }
  return declared;
}

// Register an application-level notification handler for a given method.
// Safe to call from any thread; the handler itself is always invoked in the
// dispatcher thread by handleNotification().
void McpClient::registerNotificationHandler(
    const std::string& method,
    std::function<void(const jsonrpc::Notification&)> handler) {
  std::lock_guard<std::mutex> lock(notification_handlers_mutex_);
  notification_handlers_[method] = std::move(handler);
}

// Handle notifications from server.
//
// Invoked in the dispatcher thread via ProtocolCallbacksImpl::onNotification,
// which is driven by the JSON-RPC protocol filter. Routes the notification to
// the application handler registered for its method, if any. Unhandled
// notification methods are ignored (per JSON-RPC, notifications are never
// answered), matching the server-side onNotification behaviour.
void McpClient::handleNotification(const Notification& notification) {
  // A message carrying a subscription's id belongs to that subscription
  // and to nothing else: on a transport where several share one client,
  // that id is the only thing telling them apart.
  if (routeToSubscription(notification)) {
    return;
  }
  // Progress for a request that is following its own goes to it; the
  // handlers registered for progress still hear it.
  routeProgress(notification);

  std::function<void(const jsonrpc::Notification&)> handler;
  {
    std::lock_guard<std::mutex> lock(notification_handlers_mutex_);
    auto it = notification_handlers_.find(notification.method);
    if (it != notification_handlers_.end()) {
      handler = it->second;
    }
  }

  // Invoke outside the lock so a handler may (re)register handlers without
  // deadlocking, and so a slow handler does not block registration.
  if (handler) {
    try {
      handler(notification);
    } catch (const std::exception&) {
      // Notifications carry no response; swallow handler exceptions so a
      // misbehaving callback cannot tear down the dispatcher. Count it as a
      // client-side error for observability.
      client_stats_.errors_total++;
    }
  }
}

// Handle errors
void McpClient::handleError(const Error& error) {
  client_stats_.errors_total++;

  // Notify protocol state machine
  if (protocol_state_machine_) {
    protocol_state_machine_->handleError(error);
  }

  // Check if we should disconnect
  if (error.code == ::mcp::jsonrpc::INTERNAL_ERROR) {
    // Serious error, disconnect
    disconnect();
  }
}

// Transport negotiation
TransportType McpClient::negotiateTransport(const std::string& uri) {
  // Parse URI scheme to determine transport
  if (uri.find("stdio://") == 0) {
    return TransportType::Stdio;
  } else if (uri.find("ws://") == 0 || uri.find("wss://") == 0) {
    return TransportType::WebSocket;
  } else if (uri.find("http://") == 0 || uri.find("https://") == 0) {
    if (!config_.auto_negotiate_transport) {
      return config_.preferred_transport;
    }
    if (config_.preferred_transport == TransportType::StreamableHttp ||
        config_.preferred_transport == TransportType::HttpSse) {
      return config_.preferred_transport;
    }

    // Nothing about a URL says what a server speaks. Where that has to
    // be worked out, the ladder works it out by asking; this is only
    // the rung it starts from, and what it falls back to if asking is
    // somehow not possible.
    return TransportType::StreamableHttp;
  } else {
    // Default to Streamable HTTP for unknown schemes
    return TransportType::StreamableHttp;
  }
}

bool McpClient::detectsTransport(const std::string& uri) const {
  // Only an HTTP URL has eras to tell apart.
  if (uri.find("http://") != 0 && uri.find("https://") != 0) {
    return false;
  }
  // Turned off, or already decided. Both are somebody saying they know,
  // and asking anyway would be a request they did not ask for.
  if (!config_.auto_negotiate_transport) {
    return false;
  }
  return config_.preferred_transport != TransportType::StreamableHttp &&
         config_.preferred_transport != TransportType::HttpSse;
}

void McpClient::settleConnect(const VoidResult& result) {
  std::lock_guard<std::mutex> lock(connect_promise_mutex_);
  if (pending_connect_promise_) {
    pending_connect_promise_->set_value(result);
    pending_connect_promise_.reset();
  }
}

void McpClient::startTransport(TransportType transport) {
  settled_transport_ = mcp::make_optional(transport);
  McpConnectionConfig conn_config = createConnectionConfig(transport);
  connection_manager_ = std::make_unique<McpConnectionManager>(
      *main_dispatcher_, *socket_interface_, conn_config);
  connection_manager_->setProtocolCallbacks(*protocol_callbacks_);
  connection_manager_->setStreamIdleTimeout(
      config_.streamable_http.stream_idle_timeout);

  VoidResult result = connection_manager_->connect();
  if (is_error<std::nullptr_t>(result)) {
    auto error = get_error<std::nullptr_t>(result);
    settleConnect(makeVoidError(*error));
    if (protocol_state_machine_) {
      protocol_state_machine_->handleError(*error);
    }
  }
}

void McpClient::failDetection(const std::string& reason) {
  legacy_probing_ = false;
  if (legacy_probe_timer_) {
    legacy_probe_timer_->disableTimer();
  }

  const std::string message =
      ladder_notes_.empty() ? reason : reason + " (" + ladder_notes_ + ")";
  GOPHER_LOG_ERROR("Could not work out what {} speaks: {}", current_uri_,
                   message);

  // Answered before anything is torn down. Closing first raises a
  // connection event that settles the answer itself — with the last
  // thing that happened to a socket, rather than with what was learned
  // about the server — and by the time this got to say anything there
  // was nobody left to say it to.
  Error error(::mcp::jsonrpc::INTERNAL_ERROR, message);
  settleConnect(makeVoidError(error));

  if (connection_manager_) {
    connection_manager_->close();
  }
  if (protocol_state_machine_) {
    protocol_state_machine_->handleError(error);
  }
}

void McpClient::runTransportLadder(const std::string& uri) {
  if (!modern_probe_) {
    modern_probe_.reset(
        new ModernProbe(*main_dispatcher_, *socket_interface_,
                        config_.client_name, config_.client_version,
                        config_.streamable_http.fallback_probe_timeout));
  }

  // The newest revision first, because it has no introduction to make:
  // a server that speaks it, asked to introduce itself, refuses — and
  // that refusal is indistinguishable from a server that does not serve
  // this endpoint at all unless it was asked in the right order.
  modern_probe_->probe(uri, [this, uri](const ProbeResult& result) {
    if (result.verdict == ProbeResult::Verdict::Modern) {
      // Stopping here rather than falling through is the whole point of
      // asking first. A server that speaks only this revision would
      // refuse the introduction below, and a client that read that
      // refusal as "not this transport" would try the oldest one, fail
      // there too, and report the wrong thing about the wrong attempt.
      const std::string settled = transport::modernVersionInCommon(
          config_.streamable_http, result.supported_versions);
      if (!settled.empty()) {
        // Settled here and nowhere else. Everything downstream — what a
        // request declares about itself, which headers mirror the body,
        // whether there is a handshake at all — hangs off this one
        // string, so it is written once, before the transport that
        // reads it exists.
        GOPHER_LOG_INFO("{} speaks {}, and so does this client", uri, settled);
        enterModernRevision(settled);
        startTransport(TransportType::StreamableHttp);
        return;
      }

      // Speaking that era is not the same as being one of its servers
      // only. Such a server may serve older revisions beside it, and a
      // client that cannot enter the era — or was told not to — should
      // meet it on one of those rather than be told there is nothing to
      // talk about. Falling through is how it finds out: the rung below
      // asks, and answers this question by being answered.
      bool older_in_common = false;
      std::string served;
      for (const auto& version : result.supported_versions) {
        if (!served.empty()) {
          served += ", ";
        }
        served += version;
        if (protocol::modern::isModernVersion(version)) {
          continue;
        }
        for (const auto& wanted : config_.streamable_http.protocol_versions) {
          if (wanted == version) {
            older_in_common = true;
            break;
          }
        }
      }

      if (older_in_common) {
        GOPHER_LOG_INFO(
            "{} speaks a revision this client does not; falling back to one "
            "both know",
            uri);
        runClassicRung(uri);
        return;
      }

      failDetection(
          "this server speaks the modern protocol, which this client cannot" +
          (served.empty() ? std::string()
                          : std::string("; it serves ") + served));
      return;
    }
    runClassicRung(uri);
  });
}

void McpClient::enterModernRevision(const std::string& version) {
  if (!streamable_session_) {
    // Ordinarily made with the first connection; made here because what it
    // is about to be told has to be true before that connection sends
    // anything.
    streamable_session_ =
        std::make_shared<transport::StreamableHttpClientSession>();
  }
  streamable_session_->setProtocolVersion(version);
  streamable_session_->setClientInfo(clientInfoOf(config_));
  streamable_session_->setClientCapabilities(declaredCapabilities());
}

void McpClient::discoverRevisionThenStart(const std::string& uri) {
  if (!modern_probe_) {
    modern_probe_.reset(
        new ModernProbe(*main_dispatcher_, *socket_interface_,
                        config_.client_name, config_.client_version,
                        config_.streamable_http.fallback_probe_timeout));
  }

  modern_probe_->probe(uri, [this, uri](const ProbeResult& result) {
    if (result.verdict == ProbeResult::Verdict::Modern) {
      const std::string settled = transport::modernVersionInCommon(
          config_.streamable_http, result.supported_versions);
      if (!settled.empty()) {
        GOPHER_LOG_INFO("{} speaks {}, and so does this client", uri, settled);
        enterModernRevision(settled);
      }
    }
    // Whatever the answer, the transport is the one that was named. A
    // server with no revision in common with this one is met through the
    // handshake, which is where it says so.
    startTransport(TransportType::StreamableHttp);
  });
}

void McpClient::runClassicRung(const std::string& uri) {
  classic_probe_.reset(new ClassicProbe(
      *main_dispatcher_, *socket_interface_, config_.protocol_version,
      config_.client_name, config_.client_version,
      config_.streamable_http.fallback_probe_timeout));

  classic_probe_->probe(uri, [this, uri](const ProbeResult& result) {
    if (result.verdict == ProbeResult::Verdict::Unreachable) {
      ladder_notes_ = "POST: " + result.error;
      runLegacyRung(uri);
      return;
    }

    if (isInitializeAnswer(result.status_code, result.content_type,
                           result.body)) {
      GOPHER_LOG_INFO("{} speaks Streamable HTTP", uri);
      // The session the introduction was given is deliberately let go
      // of rather than carried onto the connection that follows.
      //
      // Carrying it looked like the tidier choice — one session instead
      // of two — until a reference server refused the connection's own
      // introduction with "Server already initialized". A session that
      // has been introduced to will not be introduced to again, and the
      // connection has to introduce itself, so the session it does that
      // under has to be a new one. The probe's expires on its own timer.
      GOPHER_LOG_DEBUG("{} speaks Streamable HTTP{}", uri,
                       result.session_id.empty()
                           ? ""
                           : "; the session it offered the probe is left "
                             "to expire");
      startTransport(TransportType::StreamableHttp);
      return;
    }

    if (isModernRefusal(result.status_code, result.body)) {
      // Stop here rather than fall through. A modern server refusing an
      // introduction is not a server that speaks something older, and
      // trying something older would fail for a reason that says
      // nothing about why.
      failDetection(
          "this server speaks the modern protocol, which this client cannot");
      return;
    }

    ladder_notes_ = "POST: HTTP " + std::to_string(result.status_code) +
                    (result.status_code >= 200 && result.status_code < 300
                         ? " with no introduction in it"
                         : "");
    runLegacyRung(uri);
  });
}

void McpClient::runLegacyRung(const std::string& uri) {
  GOPHER_LOG_DEBUG("Trying the older transport at {}", uri);

  // Not asked about but attempted: the older transport has proved
  // itself when the server says where to post, and a connection that is
  // merely up proves nothing. So the connect comes up and the answer is
  // withheld until one of those two things happens.
  legacy_probing_ = true;

  if (!legacy_probe_timer_) {
    legacy_probe_timer_ = main_dispatcher_->createTimer([this]() {
      if (!legacy_probing_) {
        return;
      }
      ladder_notes_ +=
          "; GET: no endpoint within " +
          std::to_string(
              config_.streamable_http.fallback_probe_timeout.count()) +
          "ms";
      failDetection(
          "nothing at this address speaks a protocol this client "
          "knows");
    });
  }
  legacy_probe_timer_->enableTimer(
      config_.streamable_http.fallback_probe_timeout);

  startTransport(TransportType::HttpSse);
}

void McpClient::handleMessageEndpoint(const std::string& endpoint) {
  if (!legacy_probing_) {
    return;
  }
  // The one thing that could prove it. Everything the connection has
  // already done — accepting, opening a stream — a server of any era
  // would have done too.
  GOPHER_LOG_INFO("{} speaks the older HTTP+SSE transport", current_uri_);
  legacy_probing_ = false;
  if (legacy_probe_timer_) {
    legacy_probe_timer_->disableTimer();
  }
  (void)endpoint;
  settleConnect(VoidResult(nullptr));
}

// Create connection configuration
McpConnectionConfig McpClient::createConnectionConfig(TransportType transport) {
  McpConnectionConfig config;

  // Set transport type
  config.transport_type = transport;

  // Set common configuration
  config.buffer_limit = 1024 * 1024;  // 1MB
  config.connection_timeout = config_.request_timeout;
  config.use_message_framing = true;
  config.use_protocol_detection = false;

  // Set transport-specific configuration
  switch (transport) {
    case TransportType::HttpSse: {
      transport::HttpSseTransportSocketConfig http_config;
      http_config.mode = transport::HttpSseTransportSocketConfig::Mode::CLIENT;

      // Extract server address from URI
      // URI format: http://host:port/path or https://host:port/path
      std::string server_addr;
      bool is_https = false;
      if (current_uri_.find("http://") == 0) {
        server_addr = current_uri_.substr(7);  // Remove "http://"
      } else if (current_uri_.find("https://") == 0) {
        server_addr = current_uri_.substr(8);  // Remove "https://"
        is_https = true;
      } else {
        server_addr = current_uri_;
      }

      // Extract path component (e.g., /sse from https://host/sse)
      std::string http_path = "/";
      size_t slash_pos = server_addr.find('/');
      if (slash_pos != std::string::npos) {
        http_path = server_addr.substr(slash_pos);
        server_addr = server_addr.substr(0, slash_pos);
      }

      http_config.server_address = server_addr;
      config.http_path = http_path;
      config.http_host = server_addr;
      config.http_headers = config_.http_headers;
      config.current_http_headers =
          std::make_shared<std::map<std::string, std::string>>(
              config_.http_headers);

      // Set SSL transport for HTTPS URLs
      if (is_https) {
        http_config.underlying_transport =
            transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
        transport::HttpSseTransportSocketConfig::SslConfig ssl_cfg;
        ssl_cfg.verify_peer = false;
        ssl_cfg.alpn_protocols = std::vector<std::string>{"http/1.1"};
        std::string sni_host = server_addr;
        size_t colon_pos = sni_host.find(':');
        if (colon_pos != std::string::npos) {
          sni_host = sni_host.substr(0, colon_pos);
        }
        ssl_cfg.sni_hostname = mcp::make_optional(sni_host);
        http_config.ssl_config = mcp::make_optional(ssl_cfg);
      }

      config.http_sse_config = mcp::make_optional(http_config);
      // A connection that is still proving this is what the server
      // speaks gets the probe's window rather than the patient one: the
      // wait is the question, and 30 seconds is longer than anyone
      // waiting on connect() is prepared to give it.
      if (legacy_probing_) {
        config.sse_negotiation_timeout =
            config_.streamable_http.fallback_probe_timeout;
      }
      break;
    }

    case TransportType::StreamableHttp: {
      // Streamable HTTP uses the same config as HttpSse but with a different
      // transport type The connection manager will handle the simpler
      // request/response pattern
      transport::HttpSseTransportSocketConfig http_config;
      http_config.mode = transport::HttpSseTransportSocketConfig::Mode::CLIENT;

      // Extract server address from URI (same logic as HttpSse)
      std::string server_addr;
      bool is_https = false;
      if (current_uri_.find("http://") == 0) {
        server_addr = current_uri_.substr(7);
      } else if (current_uri_.find("https://") == 0) {
        server_addr = current_uri_.substr(8);
        is_https = true;
      } else {
        server_addr = current_uri_;
      }

      // Extract path component
      std::string http_path = "/";
      size_t slash_pos = server_addr.find('/');
      if (slash_pos != std::string::npos) {
        http_path = server_addr.substr(slash_pos);
        server_addr = server_addr.substr(0, slash_pos);
      }

      http_config.server_address = server_addr;
      config.http_path = http_path;
      config.http_host = server_addr;
      config.http_headers = config_.http_headers;
      config.current_http_headers =
          std::make_shared<std::map<std::string, std::string>>(
              config_.http_headers);

      // Set SSL transport for HTTPS URLs
      if (is_https) {
        http_config.underlying_transport =
            transport::HttpSseTransportSocketConfig::UnderlyingTransport::SSL;
        transport::HttpSseTransportSocketConfig::SslConfig ssl_cfg;
        ssl_cfg.verify_peer = false;
        ssl_cfg.alpn_protocols = std::vector<std::string>{"http/1.1"};
        std::string sni_host = server_addr;
        size_t colon_pos = sni_host.find(':');
        if (colon_pos != std::string::npos) {
          sni_host = sni_host.substr(0, colon_pos);
        }
        ssl_cfg.sni_hostname = mcp::make_optional(sni_host);
        http_config.ssl_config = mcp::make_optional(ssl_cfg);
      }

      config.http_sse_config = mcp::make_optional(http_config);

      // The session belongs to the conversation, not to the socket, so
      // it is made once and handed to every connection after that. A
      // reconnect keeps the session it already has and does not start a
      // new handshake for it.
      if (!streamable_session_) {
        streamable_session_ =
            std::make_shared<transport::StreamableHttpClientSession>();
      }
      config.streamable_client_session = streamable_session_;
      break;
    }

    case TransportType::WebSocket:
      // WebSocket not yet implemented
      break;

    case TransportType::Stdio: {
      transport::StdioTransportSocketConfig stdio_config;
      config.stdio_config = mcp::make_optional(stdio_config);
      break;
    }
  }

  return config;
}

// Process queued requests after protocol becomes ready
void McpClient::processQueuedRequests() {
  // For now, we don't queue requests
  // In a full implementation, we would process any requests
  // that were queued while waiting for protocol initialization
}

// List available resources
std::future<ListResourcesResult> McpClient::listResources(
    const optional<std::string>& cursor) {
  auto result_promise = std::make_shared<std::promise<ListResourcesResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  // The cursor goes back exactly as the server gave it. Through the flat
  // map a cursor that happens to look like JSON would be sent as JSON.
  json::JsonValue params = json::JsonValue::object();
  if (cursor.has_value()) {
    params.set("cursor", json::JsonValue(cursor.value()));
  }
  auto params_ptr = std::make_shared<json::JsonValue>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: resources/list (cursor={})",
                        cursor.has_value() ? cursor.value() : "<none>");

  // Step 1: Post to dispatcher to send the request (non-blocking)
  postCarryingTrace([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequestWithParams("resources/list", *params_ptr, {});
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(RequestError(response.error.value())));
      } else if (response.result.has_value()) {
        // Extract ListResourcesResult from response
        // ResponseResult variant directly contains ListResourcesResult
        if (holds_alternative<ListResourcesResult>(response.result.value())) {
          result_promise->set_value(
              get<ListResourcesResult>(response.result.value()));
        } else {
          // Fallback: return empty result if type doesn't match
          result_promise->set_value(ListResourcesResult());
        }
      } else {
        result_promise->set_value(ListResourcesResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

std::future<ListResourceTemplatesResult> McpClient::listResourceTemplates(
    const optional<Cursor>& cursor) {
  auto result_promise =
      std::make_shared<std::promise<ListResourceTemplatesResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // The cursor goes back exactly as the server gave it.
  json::JsonValue params = json::JsonValue::object();
  if (cursor.has_value()) {
    params.set("cursor", json::JsonValue(cursor.value()));
  }
  auto params_ptr = std::make_shared<json::JsonValue>(std::move(params));
  auto request_future_ptr = std::make_shared<std::future<Response>>();

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: resources/templates/list (cursor={})",
                        cursor.has_value() ? cursor.value() : "<none>");

  postCarryingTrace([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequestWithParams("resources/templates/list", *params_ptr, {});
  });

  // Waited on off the dispatcher, which is what delivers the answer.
  std::thread([result_promise, request_future_ptr]() {
    try {
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(RequestError(response.error.value())));
        return;
      }
      if (!response.result.has_value()) {
        result_promise->set_value(ListResourceTemplatesResult());
        return;
      }
      // No alternative of its own in the result variant: it arrives as
      // the JSON it is, and is read as a listing here.
      const json::JsonValue body = json::to_json(response.result.value());
      if (!body.isObject() || !body.contains("resourceTemplates") ||
          !body["resourceTemplates"].isArray()) {
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error("resources/templates/list answered with "
                               "something that is not a listing")));
        return;
      }
      result_promise->set_value(
          json::from_json<ListResourceTemplatesResult>(body));
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Read resource content
std::future<ReadResourceResult> McpClient::readResource(
    const std::string& uri) {
  auto result_promise = std::make_shared<std::promise<ReadResourceResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["uri"] = uri;
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: resources/read uri={}", uri);

  // Step 1: Post to dispatcher to send the request (non-blocking)
  postCarryingTrace([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("resources/read", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(RequestError(response.error.value())));
      } else if (response.result.has_value()) {
        // The ResponseResult variant directly contains a ReadResourceResult
        // (the deserializer recognizes the "contents" array and builds one),
        // mirroring how listResources/listTools extract their results.
        ReadResourceResult result;
        if (holds_alternative<ReadResourceResult>(response.result.value())) {
          result = get<ReadResourceResult>(response.result.value());
        }
        result_promise->set_value(result);
      } else {
        // No result payload at all; return an empty (but valid) result.
        result_promise->set_value(ReadResourceResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Subscribe to resource updates
std::future<VoidResult> McpClient::subscribeResource(const std::string& uri) {
  auto result_promise = std::make_shared<std::promise<VoidResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["uri"] = uri;
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  postCarryingTrace([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("resources/subscribe", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_value(makeVoidError(*response.error));
      } else {
        result_promise->set_value(VoidResult(nullptr));
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Unsubscribe from resource updates
std::future<VoidResult> McpClient::unsubscribeResource(const std::string& uri) {
  auto result_promise = std::make_shared<std::promise<VoidResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["uri"] = uri;
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  postCarryingTrace([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("resources/unsubscribe", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_value(makeVoidError(*response.error));
      } else {
        result_promise->set_value(VoidResult(nullptr));
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// List available tools
std::future<ListToolsResult> McpClient::listTools(
    const optional<std::string>& cursor) {
  return listTools(cursor, {});
}

std::future<ListToolsResult> McpClient::listTools(
    const optional<std::string>& cursor,
    const std::map<std::string, std::string>& http_headers) {
  auto result_promise = std::make_shared<std::promise<ListToolsResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  // The cursor goes back exactly as the server gave it. Through the flat
  // map a cursor that happens to look like JSON would be sent as JSON.
  json::JsonValue params = json::JsonValue::object();
  if (cursor.has_value()) {
    params.set("cursor", json::JsonValue(cursor.value()));
  }
  auto params_ptr = std::make_shared<json::JsonValue>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: tools/list (cursor={})",
                        cursor.has_value() ? cursor.value() : "<none>");

  // Step 1: Post to dispatcher to send the request (non-blocking)
  postCarryingTrace([this, request_future_ptr, params_ptr, http_headers]() {
    *request_future_ptr =
        sendRequestWithParams("tools/list", *params_ptr, http_headers);
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  auto session = streamable_session_;
  std::thread([result_promise, request_future_ptr, session]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        GOPHER_LOG_ERROR("MCP invoke: tools/list failed: {}",
                         response.error->message);
        result_promise->set_exception(
            std::make_exception_ptr(RequestError(response.error.value())));
      } else if (response.result.has_value()) {
        // Extract tools from response
        // The response.result contains ListToolsResult
        ListToolsResult result;
        if (holds_alternative<ListToolsResult>(response.result.value())) {
          result = get<ListToolsResult>(response.result.value());
        } else if (holds_alternative<std::vector<Tool>>(
                       response.result.value())) {
          // Backward compatibility: if it's a vector of tools directly
          result.tools = get<std::vector<Tool>>(response.result.value());
        }
        // Read as a listing rather than taken on trust: a tool whose
        // designations this client cannot resolve is one it would call
        // wrongly every time, and it is dropped here with the reason
        // logged rather than offered.
        //
        // Done here rather than handed to the dispatcher: what a listing
        // teaches is read from there while a request is decorated, and
        // the state it writes guards itself. Posting would mean holding
        // a pointer to a dispatcher that shutdown is free to delete
        // before the post runs.
        if (session) {
          result.tools = session->acceptListing(result.tools);
        }
        GOPHER_LOG_FLOW_DEBUG("MCP invoke: tools/list -> {} tools",
                              result.tools.size());
        result_promise->set_value(result);
      } else {
        GOPHER_LOG_FLOW_DEBUG(
            "MCP invoke: tools/list -> 0 tools (empty result)");
        result_promise->set_value(ListToolsResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Call a tool
std::future<CallToolResult> McpClient::callTool(
    const std::string& name, const optional<Metadata>& arguments) {
  return callTool(name, arguments, {});
}

std::future<CallToolResult> McpClient::callTool(
    const std::string& name,
    const optional<Metadata>& arguments,
    const std::map<std::string, std::string>& http_headers) {
  return callToolWith(
      name,
      arguments.has_value()
          ? mcp::make_optional(json::metadataToExactJson(arguments.value()))
          : optional<json::JsonValue>(),
      http_headers);
}

namespace {

/** A call whose arguments are not an object, refused before it is sent. */
template <typename Result>
std::future<Result> refuseArguments(const std::string& method) {
  std::promise<Result> refused;
  refused.set_exception(std::make_exception_ptr(
      std::invalid_argument(method + " arguments must be a JSON object")));
  return refused.get_future();
}

}  // namespace

std::future<CallToolResult> McpClient::callToolWith(
    const std::string& name,
    const optional<json::JsonValue>& arguments,
    const std::map<std::string, std::string>& http_headers) {
  if (arguments.has_value() && !arguments->isObject()) {
    return refuseArguments<CallToolResult>("tools/call");
  }
  auto result_promise = std::make_shared<std::promise<CallToolResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // The arguments go out as the JSON they are. Through the flat map they
  // would be rewritten on the way: a string that looks like JSON parsed
  // into an object, a wide integer narrowed.
  json::JsonValue params = json::JsonValue::object();
  params.set("name", json::JsonValue(name));
  if (arguments.has_value()) {
    params.set("arguments", arguments.value());
  }
  auto params_ptr = std::make_shared<json::JsonValue>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: tools/call name={} args={}", name,
                        arguments.has_value()
                            ? logTruncate(arguments.value().toString())
                            : "<none>");

  // Step 1: Post to dispatcher to send the request (non-blocking)
  postCarryingTrace([this, request_future_ptr, params_ptr, http_headers]() {
    *request_future_ptr =
        sendRequestWithParams("tools/call", *params_ptr, http_headers);
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr, name]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        GOPHER_LOG_ERROR("MCP invoke: tools/call name={} failed: {}", name,
                         response.error->message);
        result_promise->set_exception(
            std::make_exception_ptr(RequestError(response.error.value())));
      } else if (response.result.has_value()) {
        // Read as the shape it is rather than picked apart by hand. What
        // a tool answers with is a list of content blocks, and reading
        // only a string out of it handed the caller the JSON that list
        // travelled in as though the tool had said it.
        CallToolResult result;
        json::JsonValue body;
        if (holds_alternative<json::JsonValue>(response.result.value())) {
          body = get<json::JsonValue>(response.result.value());
        } else if (holds_alternative<Metadata>(response.result.value())) {
          // Nested JSON arrives through the flat map stringified; this
          // is what puts it back.
          body = json::metadataToJson(get<Metadata>(response.result.value()));
        }

        if (body.isObject() && body.contains("content") &&
            body["content"].isArray()) {
          result = json::impl::deserialize_CallToolResult(body);
        } else if (body.isObject() && body.contains("content") &&
                   body["content"].isString()) {
          // A server that answered with one string rather than a list.
          result.content.push_back(
              ExtendedContentBlock(TextContent(body["content"].getString())));
          if (body.contains("isError") && body["isError"].isBoolean()) {
            result.isError = body["isError"].getBool();
          }
        }

        GOPHER_LOG_FLOW_DEBUG("MCP invoke: tools/call name={} ok (isError={})",
                              name, result.isError ? "true" : "false");
        result_promise->set_value(result);
      } else {
        GOPHER_LOG_FLOW_DEBUG("MCP invoke: tools/call name={} -> empty result",
                              name);
        result_promise->set_value(CallToolResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// List available prompts
std::future<ListPromptsResult> McpClient::listPrompts(
    const optional<std::string>& cursor) {
  auto result_promise = std::make_shared<std::promise<ListPromptsResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  // The cursor goes back exactly as the server gave it. Through the flat
  // map a cursor that happens to look like JSON would be sent as JSON.
  json::JsonValue params = json::JsonValue::object();
  if (cursor.has_value()) {
    params.set("cursor", json::JsonValue(cursor.value()));
  }
  auto params_ptr = std::make_shared<json::JsonValue>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: prompts/list (cursor={})",
                        cursor.has_value() ? cursor.value() : "<none>");

  // Step 1: Post to dispatcher to send the request (non-blocking)
  postCarryingTrace([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequestWithParams("prompts/list", *params_ptr, {});
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(RequestError(response.error.value())));
      } else if (response.result.has_value()) {
        result_promise->set_value(parseListPromptsResponse(response));
      } else {
        result_promise->set_value(ListPromptsResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Get a prompt
std::future<GetPromptResult> McpClient::getPrompt(
    const std::string& name, const optional<Metadata>& arguments) {
  return getPromptWith(
      name,
      arguments.has_value()
          ? mcp::make_optional(json::metadataToExactJson(arguments.value()))
          : optional<json::JsonValue>());
}

std::future<GetPromptResult> McpClient::getPromptWith(
    const std::string& name, const optional<json::JsonValue>& arguments) {
  if (arguments.has_value() && !arguments->isObject()) {
    return refuseArguments<GetPromptResult>("prompts/get");
  }
  auto result_promise = std::make_shared<std::promise<GetPromptResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // The arguments go out as the JSON they are. Through the flat map they
  // would be rewritten on the way: a string that looks like JSON parsed
  // into an object, a wide integer narrowed.
  json::JsonValue params = json::JsonValue::object();
  params.set("name", json::JsonValue(name));
  if (arguments.has_value()) {
    params.set("arguments", arguments.value());
  }
  auto params_ptr = std::make_shared<json::JsonValue>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: prompts/get name={}", name);

  // Step 1: Post to dispatcher to send the request (non-blocking)
  postCarryingTrace([this, request_future_ptr, params_ptr]() {
    *request_future_ptr = sendRequestWithParams("prompts/get", *params_ptr, {});
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(RequestError(response.error.value())));
      } else if (response.result.has_value()) {
        // A result that is not a GetPromptResult is an error, not an
        // empty prompt: an answer the caller cannot tell from "no
        // messages" would hide a server that sent something else.
        json::JsonValue body;
        if (!resultAsJson(response, &body) || !body.isObject()) {
          throw std::runtime_error("prompts/get answered with no object");
        }
        GetPromptResult result = json::from_json<GetPromptResult>(body);
        result_promise->set_value(result);
      } else {
        result_promise->set_value(GetPromptResult());
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

namespace {
CompleteRequest completionOf(
    const variant<PromptReference, ResourceTemplateReference>& ref,
    const std::string& argument,
    const std::string& value,
    const std::map<std::string, std::string>& chosen) {
  CompleteRequest request;
  request.ref = ref;
  request.argument.name = argument;
  request.argument.value = value;
  if (!chosen.empty()) {
    CompleteRequest::Context context;
    context.arguments = chosen;
    request.context = context;
  }
  return request;
}
}  // namespace

std::future<CompleteResult> McpClient::completePromptArgument(
    const std::string& prompt,
    const std::string& argument,
    const std::string& value,
    const std::map<std::string, std::string>& chosen) {
  return complete(
      completionOf(PromptReference(prompt), argument, value, chosen));
}

std::future<CompleteResult> McpClient::completeResourceTemplateArgument(
    const std::string& uri_template,
    const std::string& argument,
    const std::string& value,
    const std::map<std::string, std::string>& chosen) {
  return complete(completionOf(ResourceTemplateReference(uri_template),
                               argument, value, chosen));
}

std::future<CompleteResult> McpClient::complete(
    const CompleteRequest& request) {
  auto result_promise = std::make_shared<std::promise<CompleteResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // The params as the spec shapes them. The typed request writes its id
  // too when it has one; that belongs to the envelope, not the params.
  const json::JsonValue written = json::to_json(request);
  json::JsonValue params = json::JsonValue::object();
  for (const auto& key : written.keys()) {
    if (key != "id") {
      params.set(key, written[key]);
    }
  }
  auto params_ptr = std::make_shared<json::JsonValue>(std::move(params));
  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Sent on the dispatcher, and waited for on a worker thread: waiting on
  // the dispatcher would block the very thread that reads the answer.
  postCarryingTrace([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequestWithParams("completion/complete", *params_ptr, {});
  });

  std::thread([result_promise, request_future_ptr]() {
    try {
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_exception(
            std::make_exception_ptr(RequestError(response.error.value())));
        return;
      }
      // An answer that is no completion is an error, not "no
      // suggestions": the caller couldn't tell the two apart.
      json::JsonValue body;
      if (!response.result.has_value() || !resultAsJson(response, &body) ||
          !body.isObject() || !body.contains("completion")) {
        throw std::runtime_error(
            "completion/complete answered with no completion");
      }
      result_promise->set_value(json::from_json<CompleteResult>(body));
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Set logging level
std::future<VoidResult> McpClient::setLogLevel(
    enums::LoggingLevel::Value level) {
  auto result_promise = std::make_shared<std::promise<VoidResult>>();

  if (!main_dispatcher_) {
    result_promise->set_exception(
        std::make_exception_ptr(std::runtime_error("No dispatcher")));
    return result_promise->get_future();
  }

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we send the request in the dispatcher, then wait on a worker
  // thread.

  auto request_future_ptr = std::make_shared<std::future<Response>>();

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["level"] = static_cast<int64_t>(level);
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  // Step 1: Post to dispatcher to send the request (non-blocking)
  postCarryingTrace([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("logging/setLevel", mcp::make_optional(*params_ptr));
  });

  // Step 2: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([result_promise, request_future_ptr]() {
    try {
      // Wait for the request to be sent
      while (!request_future_ptr->valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto response = request_future_ptr->get();
      if (response.error.has_value()) {
        result_promise->set_value(makeVoidError(*response.error));
      } else {
        result_promise->set_value(VoidResult(nullptr));
      }
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_promise->get_future();
}

// Create a message (completion request)
std::future<CreateMessageResult> McpClient::createMessage(
    const std::vector<SamplingMessage>& messages,
    const optional<ModelPreferences>& preferences) {
  // Build parameters from request
  auto params = make_metadata();

  // Add messages (simplified - real implementation needs proper serialization)
  params["messages.count"] = static_cast<int64_t>(messages.size());

  // Add optional preferences
  if (preferences.has_value()) {
    // Add model preferences as metadata fields
    // This is a simplified implementation
    params["preferences"] = "provided";
  }

  // Request-specific parameters were removed since signature changed
  // to use messages and preferences parameters directly

  // Send request
  RequestId id = static_cast<int64_t>(next_request_id_++);
  auto context = std::make_shared<RequestContext>(id, "messages/create");
  context->params = mcp::make_optional(params);
  context->start_time = std::chrono::steady_clock::now();

  // Build parameters with proper structure
  MetadataBuilder builder;

  // Add messages array
  for (size_t i = 0; i < messages.size(); ++i) {
    const auto& msg = messages[i];
    std::string prefix = "messages." + std::to_string(i) + ".";
    builder.add(prefix + "role", static_cast<int64_t>(msg.role));

    // Handle content based on type
    if (holds_alternative<TextContent>(msg.content)) {
      const auto& text = get<TextContent>(msg.content);
      builder.add(prefix + "content.type", "text");
      builder.add(prefix + "content.text", text.text);
    } else if (holds_alternative<ImageContent>(msg.content)) {
      const auto& image = get<ImageContent>(msg.content);
      builder.add(prefix + "content.type", "image");
      builder.add(prefix + "content.data", image.data);
      builder.add(prefix + "content.mimeType", image.mimeType);
    }
  }

  // Add model preferences if provided
  if (preferences.has_value()) {
    const auto& prefs = preferences.value();
    // TODO: For now, just mark that preferences were provided
    // Full serialization would require JSON conversion
    builder.add("modelPreferences", "provided");
    if (prefs.costPriority.has_value()) {
      builder.add("modelPreferences.costPriority", prefs.costPriority.value());
    }
    if (prefs.speedPriority.has_value()) {
      builder.add("modelPreferences.speedPriority",
                  prefs.speedPriority.value());
    }
    if (prefs.intelligencePriority.has_value()) {
      builder.add("modelPreferences.intelligencePriority",
                  prefs.intelligencePriority.value());
    }
  }

  context->params = mcp::make_optional(builder.build());

  sendRequestInternal(context);

  // Return future that will convert response to CreateMessageResult
  auto result_promise = std::make_shared<std::promise<CreateMessageResult>>();
  auto result_future = result_promise->get_future();

  // CRITICAL: We must NOT block on future.get() inside the dispatcher callback!
  // That would deadlock because the dispatcher thread processes Read events.
  // Instead, we wait on a worker thread.

  // Step: Use std::thread to wait for response on a worker thread (not
  // dispatcher!)
  std::thread([context, result_promise]() {
    try {
      auto response = context->promise.get_future().get();
      CreateMessageResult result;
      // Parse response into result structure
      if (!response.error.has_value() && response.result.has_value()) {
        // Extract created message
        TextContent text_content;
        text_content.type = "text";
        text_content.text = "";
        result.content = text_content;
        result.model = "unknown";
        result.role = enums::Role::ASSISTANT;
      }
      result_promise->set_value(result);
    } catch (...) {
      result_promise->set_exception(std::current_exception());
    }
  }).detach();

  return result_future;
}

// Protocol state coordination - handle protocol state changes
void McpClient::handleProtocolStateChange(
    const protocol::ProtocolStateTransitionContext& context) {
  // Take action based on new state
  switch (context.to_state) {
    case protocol::McpProtocolState::READY:
      // Protocol is ready - can now send normal requests
      // Process any queued requests
      processQueuedRequests();
      break;

    case protocol::McpProtocolState::ERROR:
      // Protocol error - may need to reconnect
      if (context.error.has_value()) {
        // Circuit breaker should handle this
        circuit_breaker_->recordFailure();
      }
      break;

    case protocol::McpProtocolState::DISCONNECTED:
      // Protocol disconnected - clear state
      initialized_ = false;
      break;

    case protocol::McpProtocolState::DRAINING:
      // Graceful shutdown in progress
      // Stop accepting new requests
      break;

    default:
      // Other states don't require specific action
      break;
  }
}
// Coordinate protocol state with network connection state
void McpClient::coordinateProtocolState() {
  if (!protocol_state_machine_) {
    return;
  }

  // Check current states
  auto protocol_state = protocol_state_machine_->currentState();

  // Coordinate based on current situation
  if (connected_ && protocol_state == protocol::McpProtocolState::CONNECTED) {
    // Network is connected but protocol not initialized
    // Trigger initialization if not already in progress
    if (!initialized_ &&
        protocol_state != protocol::McpProtocolState::INITIALIZING) {
      // Auto-initialize protocol after connection
      // We're already in dispatcher thread from synchronizeState
      // DISABLED: Let the user explicitly call initializeProtocol()
      // initializeProtocol();
    }
  } else if (!connected_ &&
             protocol_state != protocol::McpProtocolState::DISCONNECTED) {
    // Network disconnected but protocol thinks it's connected
    // Already in dispatcher thread from caller
    protocol_state_machine_->handleEvent(
        protocol::McpProtocolEvent::NETWORK_DISCONNECTED);
  }
}

// Handle connection events from network layer
void McpClient::handleConnectionEvent(network::ConnectionEvent event) {
  GOPHER_LOG_DEBUG("handleConnectionEvent called, event={}",
                   static_cast<int>(event));
  // Handle connection events in dispatcher context
  switch (event) {
    case network::ConnectionEvent::Connected:
    case network::ConnectionEvent::ConnectedZeroRtt:
      GOPHER_LOG_DEBUG("Setting connected_=true");
      connected_ = true;
      last_activity_time_ =
          std::chrono::steady_clock::now();  // Reset idle timer on connection
      client_stats_.connections_active++;

      // Fulfill the pending connect promise - connection established!
      //
      // Unless the older transport is still proving that it is what
      // this server speaks. A connection being up is not that proof —
      // a server of any era would have accepted it — so the answer
      // waits for the server to say where to post, or for the window
      // in which it could have to close.
      if (!legacy_probing_) {
        std::lock_guard<std::mutex> lock(connect_promise_mutex_);
        if (pending_connect_promise_) {
          GOPHER_LOG_DEBUG("Fulfilling connect promise with success");
          pending_connect_promise_->set_value(VoidResult(nullptr));
          pending_connect_promise_.reset();
        }
      }

      // Notify protocol state machine of network connection
      // We're already in dispatcher thread from connection callback
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(
            protocol::McpProtocolEvent::NETWORK_CONNECTED);
      }
      break;

    case network::ConnectionEvent::RemoteClose:
    case network::ConnectionEvent::LocalClose:
      connected_ = false;
      client_stats_.connections_active--;

      // A connection lost while the older transport is still proving
      // itself is that attempt failing, not the connect failing. Saying
      // so here would replace what was actually learned — which server
      // said what, to which question — with the last thing that
      // happened to the socket.
      if (legacy_probing_) {
        ladder_notes_ += "; GET: the connection closed";
        failDetection(
            "nothing at this address speaks a protocol this client knows");
        break;
      }

      // Fulfill the pending connect promise with error - connection failed
      {
        std::lock_guard<std::mutex> lock(connect_promise_mutex_);
        if (pending_connect_promise_) {
          GOPHER_LOG_DEBUG("Fulfilling connect promise with error");
          pending_connect_promise_->set_value(
              makeVoidError(Error(::mcp::jsonrpc::INTERNAL_ERROR,
                                  "Connection closed before establishing")));
          pending_connect_promise_.reset();
        }
      }

      // Notify protocol state machine of network disconnection (already in
      // dispatcher thread)
      if (protocol_state_machine_) {
        protocol_state_machine_->handleEvent(
            protocol::McpProtocolEvent::NETWORK_DISCONNECTED);
      }

      // Fail all pending requests
      auto pending = request_tracker_->getTimedOutRequests();
      for (const auto& request : pending) {
        request->finish(jsonrpc::Response::make_error(
            request->id, Error(jsonrpc::INTERNAL_ERROR, "Connection closed")));
      }
      break;
  }

  // Coordinate protocol state with connection state
  coordinateProtocolState();
}

// Setup filter chain for the application
void McpClient::setupFilterChain(application::FilterChainBuilder& builder) {
  // Add filters as needed for the client
  // This is typically configured based on transport type
}

// Initialize worker thread
void McpClient::initializeWorker(application::WorkerContext& context) {
  // Worker initialization logic
  // Clients typically don't need special worker setup
}

// Send batch of requests
std::vector<std::future<Response>> McpClient::sendBatch(
    const std::vector<std::pair<std::string, optional<Metadata>>>& requests) {
  std::vector<std::future<Response>> futures;

  for (const auto& request : requests) {
    futures.push_back(sendRequest(request.first, request.second));
  }

  return futures;
}

// Track progress for a given token
void McpClient::trackProgress(const ProgressToken& token,
                              std::function<void(double)> callback) {
  const std::string key = holds_alternative<std::string>(token)
                              ? get<std::string>(token)
                              : std::to_string(get<int64_t>(token));
  std::lock_guard<std::mutex> lock(progress_mutex_);
  if (callback) {
    progress_callbacks_[key] = std::move(callback);
  } else {
    progress_callbacks_.erase(key);
  }
}

}  // namespace client
}  // namespace mcp
