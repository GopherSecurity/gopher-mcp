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
}  // namespace

// Out-of-class definition for static constexpr member (required for C++14)
// In C++17+, constexpr static members are implicitly inline, but C++14 requires
// explicit out-of-class definition when the member is ODR-used
constexpr int McpClient::kConnectionIdleTimeoutSec;

// Constructor
McpClient::McpClient(const McpClientConfig& config)
    : ApplicationBase(config), config_(config) {
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

  main_dispatcher_->post([this, uri, connect_promise]() {
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
        startTransport(negotiateTransport(uri));
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
    main_dispatcher_->post([this]() {
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

    main_dispatcher_->post([reconnect_promise, this]() {
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
      auto written = std::make_shared<std::promise<void>>();
      auto done = written->get_future();
      main_dispatcher_->post([this, written]() {
        if (connection_manager_) {
          connection_manager_->sendSessionDelete();
        }
        written->set_value();
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
      main_dispatcher_->post([this]() {
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
  std::string protocol_version = config_.protocol_version;
  event::Dispatcher* dispatcher = main_dispatcher_;
  McpClient* client = this;

  // Step 1: Post to dispatcher to send the request (non-blocking)
  dispatcher->post([this, alive, request_future_ptr]() {
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
  std::thread([alive, dispatcher, protocol_version, client, result_promise,
               request_future_ptr]() {
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
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
        return;
      }
      GOPHER_LOG_FLOW_DEBUG("MCP invoke: initialize succeeded");

      // Parse InitializeResult from response (pure parsing — no shared state).
      InitializeResult init_result =
          parseInitializeResponse(response, protocol_version);

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
                        init_result]() {
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
        client->sendInitializedNotification();
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

InitializeResult McpClient::parseInitializeResponse(
    const jsonrpc::Response& response, const std::string& protocol_version) {
  if (!response.result.has_value()) {
    throw std::runtime_error("Initialize response missing result");
  }

  InitializeResult init_result;
  if (holds_alternative<Metadata>(response.result.value())) {
    auto& metadata = get<Metadata>(response.result.value());

    auto proto_it = metadata.find("protocolVersion");
    if (proto_it != metadata.end() &&
        holds_alternative<std::string>(proto_it->second)) {
      init_result.protocolVersion = get<std::string>(proto_it->second);
    }

    auto name_it = metadata.find("serverInfo.name");
    auto version_it = metadata.find("serverInfo.version");
    if (name_it != metadata.end() && version_it != metadata.end()) {
      Implementation server_info(
          holds_alternative<std::string>(name_it->second)
              ? get<std::string>(name_it->second)
              : "",
          holds_alternative<std::string>(version_it->second)
              ? get<std::string>(version_it->second)
              : "");
      init_result.serverInfo = mcp::make_optional(server_info);
    }

    ServerCapabilities caps;

    auto tools_it = metadata.find("capabilities.tools");
    if (tools_it != metadata.end() &&
        holds_alternative<bool>(tools_it->second)) {
      caps.tools = mcp::make_optional(get<bool>(tools_it->second));
    }

    auto prompts_it = metadata.find("capabilities.prompts");
    if (prompts_it != metadata.end() &&
        holds_alternative<bool>(prompts_it->second)) {
      caps.prompts = mcp::make_optional(get<bool>(prompts_it->second));
    }

    auto resources_it = metadata.find("capabilities.resources");
    if (resources_it != metadata.end() &&
        holds_alternative<bool>(resources_it->second)) {
      caps.resources = mcp::make_optional(
          variant<bool, ResourcesCapability>(get<bool>(resources_it->second)));
    }

    auto logging_it = metadata.find("capabilities.logging");
    if (logging_it != metadata.end() &&
        holds_alternative<bool>(logging_it->second)) {
      caps.logging = mcp::make_optional(get<bool>(logging_it->second));
    }

    init_result.capabilities = caps;
  } else {
    init_result.protocolVersion = protocol_version;
    init_result.capabilities = ServerCapabilities();
  }

  return init_result;
}

Metadata McpClient::buildInitializeParams() const {
  // MCP spec requires: protocolVersion, capabilities, clientInfo (nested
  // object)
  auto init_params = make_metadata();
  init_params["protocolVersion"] = config_.protocol_version;

  // clientInfo must be a nested object with name and version
  // Store as JSON string - the serializer will parse it back to an object
  std::string client_info_json = "{\"name\":\"" + config_.client_name +
                                 "\",\"version\":\"" + config_.client_version +
                                 "\"}";
  init_params["clientInfo"] = client_info_json;

  // capabilities must be an object (can be empty)
  init_params["capabilities"] = "{}";
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
  context->completed = true;
  context->promise.set_value(Response::make_error(context->id, error));
  request_tracker_->removeRequest(context->id);
  client_stats_.requests_failed++;
}

void McpClient::startReinitialize() {
  if (reinitializing_) {
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
          InitializeResult init_result =
              parseInitializeResponse(response, config_.protocol_version);
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

  const auto delay = server_stream_backoff_->getRetryDelay(
      server_stream_attempts_ == 0 ? 0 : server_stream_attempts_ - 1);
  ++server_stream_attempts_;

  GOPHER_LOG_DEBUG(
      "Asking for the server stream again in {}ms{}", delay.count(),
      last_event_id.empty() ? std::string()
                            : std::string(", from ") + last_event_id);

  if (!server_stream_timer_) {
    server_stream_timer_ = main_dispatcher_->createTimer(
        [this]() { openServerStream(pending_stream_cursor_); });
  }
  pending_stream_cursor_ = last_event_id;
  server_stream_timer_->enableTimer(delay);
}

void McpClient::handleClientStreamEvent(ClientStreamEvent event,
                                        const optional<RequestId>& request_id,
                                        const std::string& last_event_id) {
  switch (event) {
    case ClientStreamEvent::Opened:
      // A stream that opened is the evidence that the waiting worked, so
      // the next one that closes starts the window again from the floor.
      GOPHER_LOG_DEBUG("Server stream open");
      server_stream_attempts_ = 0;
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
  stream_recovering_ = mcp::make_optional(context->id);
  GOPHER_LOG_DEBUG(
      "Picking up the answer to {} from {}", context->method,
      last_event_id.empty() ? "<the beginning>" : last_event_id.c_str());
  // Straight away rather than after a wait: this is not a server that
  // went away, it is one still working on an answer a caller is being
  // held for.
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

  // Track request
  request_tracker_->trackRequest(context);
  // Track request sent

  // Send request through internal pathway
  sendRequestInternal(context);

  return context->promise.get_future();
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

  // Send through connection manager
  // Post to dispatcher thread to ensure thread safety
  main_dispatcher_->post([this, notification]() {
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
        context->retry_timer = main_dispatcher_->createTimer(
            [this, context]() { sendRequestInternal(context); });
        context->retry_timer->enableTimer(
            std::chrono::milliseconds(kReconnectRetryDelayMs));
        return;
      }
      // Connected now, proceed with send below
    } else if (context->retry_count > max_reconnect_retries) {
      // Too many retries
      context->promise.set_value(Response::make_error(
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
        context->promise.set_value(Response::make_error(
            context->id, Error(::mcp::jsonrpc::INTERNAL_ERROR,
                               "Connection closed and reconnect failed")));
        request_tracker_->removeRequest(context->id);
        client_stats_.requests_failed++;
        return;
      }

      // Reconnect initiated - schedule retry to allow connection event to be
      // processed
      context->retry_count = 1;
      main_dispatcher_->post(
          [this, context]() { sendRequestInternal(context); });
      return;
    }
  }

  // Double-check connection after potential reconnect
  if (!connected_ || !connection_manager_) {
    context->promise.set_value(Response::make_error(
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
  request.id = context->id;

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
      context->promise.set_value(Response::make_error(
          context->id, *get_error<std::nullptr_t>(send_result)));
    } else {
      // Max retries exceeded
      context->promise.set_value(Response::make_error(
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
  request->completed = true;

  // If the stream was carrying this answer, it has carried it, and is a
  // plain stream again.
  if (stream_recovering_.has_value() &&
      holds_alternative<int64_t>(stream_recovering_.value()) &&
      holds_alternative<int64_t>(response.id) &&
      get<int64_t>(stream_recovering_.value()) == get<int64_t>(response.id)) {
    stream_recovering_.reset();
  }
  request->promise.set_value(response);
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
  std::lock_guard<std::mutex> lock(request_handlers_mutex_);
  request_handlers_[method] = std::move(handler);
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
      std::string served;
      for (const auto& version : result.supported_versions) {
        if (!served.empty()) {
          served += ", ";
        }
        served += version;
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
  auto params = make_metadata();
  if (cursor.has_value()) {
    params["cursor"] = cursor.value();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: resources/list (cursor={})",
                        cursor.has_value() ? cursor.value() : "<none>");

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("resources/list", mcp::make_optional(*params_ptr));
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
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
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
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
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
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
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
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
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
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
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
  auto params = make_metadata();
  if (cursor.has_value()) {
    params["cursor"] = cursor.value();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: tools/list (cursor={})",
                        cursor.has_value() ? cursor.value() : "<none>");

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post(
      [this, request_future_ptr, params_ptr, http_headers]() {
        *request_future_ptr = sendRequest(
            "tools/list", mcp::make_optional(*params_ptr), http_headers);
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
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
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
        // Inert until nested JSON survives being parsed here — an
        // inputSchema arrives flattened, so nothing is designated and
        // every tool passes. Wired now so the behaviour appears with the
        // parser rather than having to be remembered.
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

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["name"] = name;
  if (arguments.has_value()) {
    // Convert arguments to JSON string for nested object support
    // Server expects "arguments" as a nested JSON object which is stored
    // as a JSON string in Metadata since MetadataValue doesn't support nesting
    auto args_json = json::metadataToJson(arguments.value());
    params["arguments"] = args_json.toString();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG(
      "MCP invoke: tools/call name={} args={}", name,
      arguments.has_value()
          ? logTruncate(json::metadataToJson(arguments.value()).toString())
          : "<none>");

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post(
      [this, request_future_ptr, params_ptr, http_headers]() {
        *request_future_ptr = sendRequest(
            "tools/call", mcp::make_optional(*params_ptr), http_headers);
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
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else if (response.result.has_value()) {
        // Extract CallToolResult from response
        // Server returns Metadata with "content" (string) and "isError" (bool)
        CallToolResult result;
        if (holds_alternative<Metadata>(response.result.value())) {
          auto metadata = get<Metadata>(response.result.value());
          // Extract content string and convert to TextContent
          auto content_it = metadata.find("content");
          if (content_it != metadata.end() &&
              holds_alternative<std::string>(content_it->second)) {
            result.content.push_back(ExtendedContentBlock(
                TextContent(get<std::string>(content_it->second))));
          }
          // Extract isError flag
          auto error_it = metadata.find("isError");
          if (error_it != metadata.end() &&
              holds_alternative<bool>(error_it->second)) {
            result.isError = get<bool>(error_it->second);
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
  auto params = make_metadata();
  if (cursor.has_value()) {
    params["cursor"] = cursor.value();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: prompts/list (cursor={})",
                        cursor.has_value() ? cursor.value() : "<none>");

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("prompts/list", mcp::make_optional(*params_ptr));
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
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else if (response.result.has_value()) {
        // Extract prompts vector from response and wrap in ListPromptsResult
        // ResponseResult variant contains std::vector<Prompt>
        ListPromptsResult result;
        if (holds_alternative<std::vector<Prompt>>(response.result.value())) {
          result.prompts = get<std::vector<Prompt>>(response.result.value());
        }
        result_promise->set_value(result);
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

  // Prepare params before posting to dispatcher
  auto params = make_metadata();
  params["name"] = name;
  if (arguments.has_value()) {
    // Convert arguments to JSON string for nested object support
    // Server expects "arguments" as a nested JSON object which is stored
    // as a JSON string in Metadata since MetadataValue doesn't support nesting
    auto args_json = json::metadataToJson(arguments.value());
    params["arguments"] = args_json.toString();
  }
  auto params_ptr = std::make_shared<Metadata>(std::move(params));

  GOPHER_LOG_FLOW_DEBUG("MCP invoke: prompts/get name={}", name);

  // Step 1: Post to dispatcher to send the request (non-blocking)
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
    *request_future_ptr =
        sendRequest("prompts/get", mcp::make_optional(*params_ptr));
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
        result_promise->set_exception(std::make_exception_ptr(
            std::runtime_error(response.error->message)));
      } else if (response.result.has_value()) {
        // Extract GetPromptResult from response
        // Server serializes GetPromptResult to Metadata containing:
        // - description (optional string)
        // - messages (JSON string of array)
        GetPromptResult result;
        if (holds_alternative<Metadata>(response.result.value())) {
          auto metadata = get<Metadata>(response.result.value());
          // Extract description
          auto desc_it = metadata.find("description");
          if (desc_it != metadata.end() &&
              holds_alternative<std::string>(desc_it->second)) {
            result.description =
                mcp::make_optional(get<std::string>(desc_it->second));
          }
          // Extract messages from JSON string
          auto msgs_it = metadata.find("messages");
          if (msgs_it != metadata.end() &&
              holds_alternative<std::string>(msgs_it->second)) {
            // Parse messages JSON string back to PromptMessage array
            std::string msgs_json = get<std::string>(msgs_it->second);
            try {
              auto msgs_value = json::JsonValue::parse(msgs_json);
              if (msgs_value.isArray()) {
                size_t size = msgs_value.size();
                for (size_t i = 0; i < size; ++i) {
                  result.messages.push_back(
                      json::from_json<PromptMessage>(msgs_value[i]));
                }
              }
            } catch (...) {
              // Failed to parse messages, leave empty
            }
          }
        }
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
  main_dispatcher_->post([this, request_future_ptr, params_ptr]() {
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
        request->promise.set_value(jsonrpc::Response::make_error(
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
  // Store the callback for this progress token
  // Will be invoked when progress updates are received
}

}  // namespace client
}  // namespace mcp
