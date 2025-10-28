/**
 * @file mcp_config_example_server.cc
 * @brief Config-driven MCP server demonstrating filter pipeline configuration
 *
 * This example demonstrates the new configuration-driven filter pipeline system
 * introduced in Option A implementation. Key features:
 *
 * - JSON-based listener and filter chain configuration
 * - Config-driven HTTP+SSE+JSON-RPC filter pipeline
 * - Runtime filter chain assembly from configuration
 * - Example MCP tools demonstrating the complete pipeline
 * - Server introspection tools for debugging configuration
 *
 * USAGE:
 *   mcp_config_example_server --config <config.json> [--verbose]
 *
 * EXAMPLE:
 *   ./mcp_config_example_server --config examples/configs/mcp_server_example.json --verbose
 *
 * The server will load the filter chain configuration from the JSON file and
 * demonstrate a complete HTTP→SSE→JSON-RPC pipeline working with real MCP
 * protocol functionality.
 */

#include <iostream>
#include <memory>
#include <fstream>
#include <cstring>
#include <csignal>
#include <thread>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <vector>
#include <utility>
#include <variant>
#include <string_view>

// Core MCP includes
#include "mcp/server/mcp_server.h"
#include "mcp/core/type_helpers.h"
#include "mcp/core/compat.h"

// Networking includes
#include "mcp/network/server_listener_impl.h"
#include "mcp/network/listener.h"
#include "mcp/network/connection.h"
#include "mcp/network/transport_socket.h"
#include "mcp/config/listener_config.h"
#include "mcp/filter/filter_chain_assembler.h"
#include "mcp/filter/filter_registry.h"
#include "mcp/json/json_bridge.h"
#include "mcp/event/libevent_dispatcher.h"
#include "mcp/mcp_connection_manager.h"

#define GOPHER_LOG_COMPONENT "config.example.server"

using namespace mcp;
using namespace mcp::config;
using namespace mcp::filter;

/**
 * Config-driven example server implementation
 */
class ConfigDrivenExampleServer : public network::ListenerCallbacks {
public:
  explicit ConfigDrivenExampleServer(bool verbose = false)
      : verbose_(verbose), running_(false) {}

  /**
   * Load server configuration from JSON file
   */
  bool loadServerConfig(const std::string& config_path) {
    try {
      if (verbose_) {
        std::cout << "Loading server configuration from: " << config_path << std::endl;
      }

      std::ifstream file(config_path);
      if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open config file: " << config_path << std::endl;
        return false;
      }
      file.close();

      // Parse listeners from JSON
      listener_config_ = ListenerServerConfig::fromJsonFile(config_path);

      if (listener_config_.listeners.empty()) {
        std::cerr << "ERROR: No listeners configured in " << config_path << std::endl;
        return false;
      }

      // Normalize filter config fields before validation
      for (auto& listener : listener_config_.listeners) {
        for (auto& chain : listener.filter_chains) {
          for (auto& filter : chain.filters) {
            if (filter.type.empty() && !filter.name.empty()) {
              filter.type = filter.name;
              if (verbose_) {
                std::cout << "  [Config] Populated filter type with name: " << filter.type << std::endl;
              }
            }
            if (filter.name.empty() && !filter.type.empty()) {
              filter.name = filter.type;
            }
          }
        }
      }

      // Validate all listeners
      for (const auto& listener : listener_config_.listeners) {
        try {
          listener.validate();
        } catch (const ConfigValidationError& e) {
          std::cerr << "ERROR: Listener validation failed: " << e.what() << std::endl;
          return false;
        }
      }

      if (verbose_) {
        std::cout << "Successfully loaded " << listener_config_.listeners.size()
                  << " listener(s)" << std::endl;

        for (const auto& listener : listener_config_.listeners) {
          std::cout << "  Listener '" << listener.name << "' on "
                    << listener.address.socket_address.address << ":"
                    << listener.address.socket_address.port_value << std::endl;

          for (size_t i = 0; i < listener.filter_chains.size(); ++i) {
            const auto& chain = listener.filter_chains[i];
            std::cout << "    Filter chain " << i << " with "
                      << chain.filters.size() << " filter(s): ";
            for (const auto& filter : chain.filters) {
              const std::string& label = !filter.name.empty() ? filter.name : filter.type;
              std::cout << label << " ";
            }
            std::cout << std::endl;
          }
        }
      }

      return true;

    } catch (const json::JsonException& e) {
      std::cerr << "ERROR: JSON parse error: " << e.what() << std::endl;
      return false;
    } catch (const std::exception& e) {
      std::cerr << "ERROR: Failed to load config: " << e.what() << std::endl;
      return false;
    }
  }

  /**
   * Start the server with config-driven listeners
   */
  bool startServer() {
    if (listener_config_.listeners.empty()) {
      std::cerr << "ERROR: No listeners configured" << std::endl;
      return false;
    }

    try {
      auto dispatcher_factory = mcp::event::createLibeventDispatcherFactory();
      dispatcher_ = dispatcher_factory->createDispatcher("config_example_server");
      if (!dispatcher_) {
        std::cerr << "ERROR: Failed to create event dispatcher" << std::endl;
        return false;
      }

      // Prime dispatcher thread ownership so we can register listeners before
      // entering the main event loop. The dispatcher asserts that certain
      // operations (like createFileEvent) run on its owning thread, which is
      // normally recorded inside run(). A quick non-blocking iteration sets up
      // that thread affinity without stalling startup.
      dispatcher_->run(mcp::event::RunType::NonBlock);

      tcp_listeners_.clear();
      active_connections_.clear();
      connection_observers_.clear();

      callbacks_ = std::make_unique<ExampleProtocolCallbacks>(*this);

      for (const auto& listener_config : listener_config_.listeners) {
        if (!startListener(listener_config)) {
          std::cerr << "ERROR: Failed to start listener '"
                    << listener_config.name << "'" << std::endl;
          return false;
        }
      }

      running_ = true;

      if (verbose_) {
        std::cout << "Server started successfully" << std::endl;
        std::cout << "Configuration-driven filter chains active:" << std::endl;

        for (const auto& listener : listener_config_.listeners) {
          std::cout << "  " << listener.name << " -> ";
          if (!listener.filter_chains.empty()) {
            for (const auto& filter : listener.filter_chains[0].filters) {
              const std::string& label = !filter.name.empty() ? filter.name : filter.type;
              std::cout << label << " -> ";
            }
          }
          std::cout << "Application" << std::endl;
        }
      }

      return true;

    } catch (const std::exception& e) {
      std::cerr << "ERROR: Failed to start server: " << e.what() << std::endl;
      return false;
    }
  }

  /**
   * Run the server event loop
   */
  void run() {
    if (!running_ || !dispatcher_) {
      std::cerr << "ERROR: Server not properly initialized" << std::endl;
      return;
    }

    std::cout << "Server running... Press Ctrl+C to stop" << std::endl;

    try {
      dispatcher_->run(mcp::event::RunType::RunUntilExit);
    } catch (const std::exception& e) {
      std::cerr << "ERROR: Server runtime error: " << e.what() << std::endl;
    }
  }

  /**
   * Stop the server gracefully
   */
  void stop() {
    if (!running_) {
      return;
    }

    running_ = false;

    std::vector<uint64_t> to_close;
    to_close.reserve(active_connections_.size());
    for (const auto& entry : active_connections_) {
      to_close.push_back(entry.first);
    }
    for (auto id : to_close) {
      removeConnection(id);
    }
    active_connections_.clear();
    connection_observers_.clear();

    for (auto& listener : tcp_listeners_) {
      if (listener) {
        listener->disable();
      }
    }
    tcp_listeners_.clear();

    if (dispatcher_) {
      dispatcher_->exit();
    }

    std::cout << "Server stopped gracefully" << std::endl;
  }

private:
  /**
   * Example protocol callbacks implementation
   */
  class ExampleProtocolCallbacks : public McpProtocolCallbacks {
  public:
    explicit ExampleProtocolCallbacks(ConfigDrivenExampleServer& server)
        : server_(server) {}

    void onRequest(const jsonrpc::Request& request) override {
      if (server_.verbose_) {
        std::cout << "[Protocol] Received JSON-RPC request: " << request.method << std::endl;
      }

      if (request.method == "tools/call") {
        handleToolCall(request);
      } else if (request.method == "tools/list") {
        handleToolsList(request);
      } else {
        sendError(request.id, -32601, "Method not found: " + request.method);
      }
    }

    void onNotification(const jsonrpc::Notification& notification) override {
      if (server_.verbose_) {
        std::cout << "[Protocol] Notification: " << notification.method << std::endl;
      }
    }

    void onResponse(const jsonrpc::Response&) override {}

    void onError(const Error& error) override {
      std::cerr << "[Protocol] Error: " << error.message << std::endl;
    }

    void onConnectionEvent(network::ConnectionEvent event) override {
      if (server_.verbose_) {
        std::cout << "[Protocol] Connection event: " << static_cast<int>(event) << std::endl;
      }
    }

  private:
    void handleToolCall(const jsonrpc::Request& request) {
      try {
        json::JsonValue params_json = paramsToJson(request.params);
        std::string tool_name;
        if (params_json.isObject() && params_json.contains("name")) {
          tool_name = params_json["name"].getString("");
        }

        if (tool_name.empty() || tool_name == "echo") {
          handleEchoTool(request, params_json);
        } else if (tool_name == "config_info") {
          handleConfigInfoTool(request);
        } else {
          sendError(request.id, -32600, "Unknown tool: " + tool_name);
        }
      } catch (const std::exception& e) {
        sendError(request.id, -32603, "Tool execution error: " + std::string(e.what()));
      }
    }

    void handleEchoTool(const jsonrpc::Request& request,
                        const json::JsonValue& params_json) {
      if (server_.verbose_) {
        std::cout << "[Tool] Echo params: " << params_json.toString()
                  << std::endl;
      }

      auto trimView = [](std::string_view value) -> std::string_view {
        const char* whitespace = " \t\n\r";
        const auto first = value.find_first_not_of(whitespace);
        if (first == std::string_view::npos) {
          return std::string_view{};
        }
        const auto last = value.find_last_not_of(whitespace);
        return value.substr(first, last - first + 1);
      };

      auto respondWithText = [&](const std::string& text_value) {
        json::JsonObjectBuilder result;
        result.add("echoed", text_value);
        result.add("timestamp",
                   json::JsonValue(static_cast<int64_t>(std::time(nullptr))));
        result.add("server", "config-driven-example");
        sendResult(request.id, result.build());
      };

      auto logRawArgument = [&](const char* source, const json::JsonValue& value) {
        if (!server_.verbose_) {
          return;
        }
        std::cerr << "[Tool] Raw arguments from " << source << ": "
                  << value.toString() << std::endl;
        std::cerr << "[Tool] Raw arguments type: "
                  << (value.isString()   ? "string"
                      : value.isObject() ? "object"
                      : value.isArray()  ? "array"
                                         : value.isNull() ? "null" : "other")
                  << std::endl;
      };

      auto tryParseArgumentsObject = [&](const json::JsonValue& candidate,
                                         const char* source) -> bool {
        if (!candidate.isObject()) {
          return false;
        }
        if (server_.verbose_) {
          std::cerr << "[Tool] Decoding arguments object from " << source
                    << ": " << candidate.toString() << std::endl;
        }
        if (candidate.contains("text")) {
          respondWithText(candidate["text"].getString("Hello World"));
          return true;
        }
        return false;
      };

      auto tryDecodeArgumentsString = [&](const std::string& raw,
                                          const char* source) -> bool {
        std::string_view trimmed = trimView(raw);
        if (trimmed.empty()) {
          return false;
        }
        if (trimmed.front() == '{' || trimmed.front() == '[') {
          try {
            json::JsonValue parsed = json::JsonValue::parse(std::string(trimmed));
            return tryParseArgumentsObject(parsed, source);
          } catch (const std::exception& e) {
            if (server_.verbose_) {
              std::cerr << "[Tool] Failed to parse JSON arguments from "
                        << source << ": " << e.what() << std::endl;
            }
            return false;
          }
        }
        if (server_.verbose_) {
          std::cerr << "[Tool] Treating arguments from " << source
                    << " as plain text" << std::endl;
        }
        respondWithText(std::string(trimmed));
        return true;
      };

      bool handled = false;

      if (request.params.has_value()) {
        const Metadata& metadata = request.params.value();
        auto it = metadata.find("arguments");
        if (it != metadata.end()) {
          const MetadataValue& raw_value = it->second;
          if (server_.verbose_) {
            std::cerr << "[Tool] Found metadata arguments of type "
                      << raw_value.index() << std::endl;
          }
          if (mcp::holds_alternative<std::string>(raw_value)) {
            handled = tryDecodeArgumentsString(
                mcp::get<std::string>(raw_value), "metadata");
          } else if (mcp::holds_alternative<int64_t>(raw_value)) {
            respondWithText(std::to_string(mcp::get<int64_t>(raw_value)));
            handled = true;
          } else if (mcp::holds_alternative<double>(raw_value)) {
            respondWithText(std::to_string(mcp::get<double>(raw_value)));
            handled = true;
          } else if (mcp::holds_alternative<bool>(raw_value)) {
            respondWithText(mcp::get<bool>(raw_value) ? "true" : "false");
            handled = true;
          }
        }
      }

      if (!handled && params_json.isObject() &&
          params_json.contains("arguments")) {
        const json::JsonValue& field = params_json["arguments"];
        logRawArgument("params_json", field);
        if (!handled && field.isObject()) {
          handled = tryParseArgumentsObject(field, "params_json");
        }
        if (!handled && field.isString()) {
          handled =
              tryDecodeArgumentsString(field.getString(""), "params_json");
        }
        if (!handled && !field.isNull()) {
          respondWithText(field.toString());
          handled = true;
        }
      }

      if (!handled) {
        if (server_.verbose_) {
          std::cerr << "[Tool] Falling back to default echo message" << std::endl;
        }
        respondWithText("Hello World");
      }
    }

    void handleConfigInfoTool(const jsonrpc::Request& request) {
      json::JsonObjectBuilder result;
      result.add("listener_count", static_cast<int>(server_.listener_config_.listeners.size()));

      json::JsonArrayBuilder listeners;
      for (const auto& listener : server_.listener_config_.listeners) {
        json::JsonObjectBuilder listener_info;
        listener_info.add("name", listener.name);
        listener_info.add("address", listener.address.socket_address.address);
        listener_info.add("port", static_cast<int>(listener.address.socket_address.port_value));
        listener_info.add("filter_chain_count", static_cast<int>(listener.filter_chains.size()));

        if (!listener.filter_chains.empty()) {
          json::JsonArrayBuilder filters;
          for (const auto& filter : listener.filter_chains[0].filters) {
            const std::string& label = !filter.name.empty() ? filter.name : filter.type;
            filters.add(label);
          }
          listener_info.add("filters", filters.build());
        }

        listeners.add(listener_info.build());
      }
      result.add("listeners", listeners.build());

      auto registry_filters = FilterRegistry::instance().listContextFactories();
      json::JsonArrayBuilder registered_filters;
      for (const auto& filter_name : registry_filters) {
        registered_filters.add(filter_name);
      }
      result.add("registered_filters", registered_filters.build());

      sendResult(request.id, result.build());
    }

    void handleToolsList(const jsonrpc::Request& request) {
      json::JsonArrayBuilder tools;

      json::JsonObjectBuilder echo_tool;
      echo_tool.add("name", "echo");
      echo_tool.add("description", "Echo input text with timestamp");
      json::JsonObjectBuilder echo_schema;
      echo_schema.add("type", "object");
      json::JsonObjectBuilder echo_props;
      json::JsonObjectBuilder text_prop;
      text_prop.add("type", "string");
      text_prop.add("description", "Text to echo");
      echo_props.add("text", text_prop.build());
      echo_schema.add("properties", echo_props.build());
      echo_tool.add("inputSchema", echo_schema.build());
      tools.add(echo_tool.build());

      json::JsonObjectBuilder config_tool;
      config_tool.add("name", "config_info");
      config_tool.add("description", "Get server configuration information");
      json::JsonObjectBuilder config_schema;
      config_schema.add("type", "object");
      config_tool.add("inputSchema", config_schema.build());
      tools.add(config_tool.build());

      json::JsonObjectBuilder result;
      result.add("tools", tools.build());

      sendResult(request.id, result.build());
    }

    void sendResult(const RequestId& id, const json::JsonValue& result) {
      server_.sendJsonRpcResponse(makeJsonId(id), result);
    }

    void sendError(const RequestId& id, int code, const std::string& message) {
      server_.sendJsonRpcError(makeJsonId(id), code, message);
    }

    json::JsonValue makeJsonId(const RequestId& id) const {
      if (mcp::holds_alternative<std::string>(id)) {
        return json::JsonValue(mcp::get<std::string>(id));
      }
      return json::JsonValue(mcp::get<int64_t>(id));
    }

    json::JsonValue paramsToJson(const optional<Metadata>& params) const {
      if (!params.has_value()) {
        return json::JsonValue::object();
      }
      return json::to_json(params.value());
    }

    ConfigDrivenExampleServer& server_;
  };

  class ConnectionObserver : public network::ConnectionCallbacks {
  public:
    ConnectionObserver(ConfigDrivenExampleServer& server, uint64_t id)
        : server_(server), connection_id_(id) {}

    void onEvent(network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override;
    void onBelowWriteBufferLowWatermark() override;

  private:
    ConfigDrivenExampleServer& server_;
    uint64_t connection_id_;
  };

  bool startListener(const ListenerConfig& listener_config);
  void onAccept(network::ConnectionSocketPtr&& socket) override;
  void onNewConnection(network::ConnectionPtr&& connection) override;
  void registerConnection(network::ConnectionPtr&& connection);
  void handleConnectionEvent(uint64_t connection_id, network::ConnectionEvent event);

  void sendJsonRpcResponse(const json::JsonValue& id, const json::JsonValue& payload);
  void sendJsonRpcError(const json::JsonValue& id, int code, const std::string& message);
  void broadcastJson(const json::JsonValue& message);
  void removeConnection(uint64_t connection_id);

  bool verbose_;
  bool running_;
  ListenerServerConfig listener_config_;
  std::unique_ptr<mcp::event::Dispatcher> dispatcher_;
  std::unique_ptr<ExampleProtocolCallbacks> callbacks_;
  std::vector<std::unique_ptr<network::TcpActiveListener>> tcp_listeners_;
  std::unordered_map<uint64_t, std::unique_ptr<network::Connection>> active_connections_;
  std::unordered_map<uint64_t, std::unique_ptr<ConnectionObserver>> connection_observers_;
};

bool ConfigDrivenExampleServer::startListener(const ListenerConfig& listener_config) {
  if (listener_config.filter_chains.empty()) {
    std::cerr << "ERROR: Listener '" << listener_config.name
              << "' has no filter chains" << std::endl;
    return false;
  }

  const auto& filter_chain = listener_config.filter_chains.front();

  ConfigurableFilterChainFactory verifier(filter_chain);
  if (!verifier.validateConfiguration()) {
    std::cerr << "ERROR: Filter chain configuration invalid for listener '"
              << listener_config.name << "'" << std::endl;
    for (const auto& error : verifier.getValidationErrors()) {
      std::cerr << "  - " << error << std::endl;
    }
    return false;
  }

  std::vector<std::string> filter_types;
  filter_types.reserve(filter_chain.filters.size());
  for (const auto& filter_cfg : filter_chain.filters) {
    filter_types.push_back(!filter_cfg.type.empty() ? filter_cfg.type : filter_cfg.name);
  }

  if (!FilterRegistry::instance().validateBasicFilterChain(filter_types)) {
    std::cerr << "ERROR: Filter registry rejected chain for listener '"
              << listener_config.name << "'" << std::endl;
    return false;
  }

  auto listener = std::make_unique<network::TcpActiveListener>(
      *dispatcher_, listener_config, *this);

  if (!listener || listener->listener() == nullptr) {
    std::cerr << "ERROR: Failed to initialize TCP listener for '"
              << listener_config.name << "'" << std::endl;
    return false;
  }

  listener->setProtocolCallbacks(*callbacks_);
  listener->enable();
  tcp_listeners_.push_back(std::move(listener));

  if (verbose_) {
    std::cout << "[Listener] Started '" << listener_config.name << "' on "
              << listener_config.address.socket_address.address << ":"
              << listener_config.address.socket_address.port_value << std::endl;
  }

  return true;
}

void ConfigDrivenExampleServer::onAccept(network::ConnectionSocketPtr&& socket) {
  if (verbose_) {
    std::cout << "[Listener] Raw socket accepted" << std::endl;
  }
  (void)socket;  // Ownership handled by TcpActiveListener.
}

void ConfigDrivenExampleServer::onNewConnection(network::ConnectionPtr&& connection) {
  registerConnection(std::move(connection));
}

void ConfigDrivenExampleServer::registerConnection(network::ConnectionPtr&& connection) {
  if (!connection) {
    return;
  }

  const uint64_t connection_id = connection->id();
  if (verbose_) {
    std::cout << "[Connection] New connection id=" << connection_id << std::endl;
  }

  // Remove any stale state for recycled connection ids
  removeConnection(connection_id);

  auto observer = std::make_unique<ConnectionObserver>(*this, connection_id);
  auto* observer_ptr = observer.get();
  connection->addConnectionCallbacks(*observer_ptr);

  active_connections_[connection_id] = std::move(connection);
  connection_observers_[connection_id] = std::move(observer);
}

void ConfigDrivenExampleServer::ConnectionObserver::onEvent(network::ConnectionEvent event) {
  server_.handleConnectionEvent(connection_id_, event);
}

void ConfigDrivenExampleServer::ConnectionObserver::onAboveWriteBufferHighWatermark() {
  if (server_.verbose_) {
    std::cout << "[Connection] High watermark reached for id="
              << connection_id_ << std::endl;
  }
}

void ConfigDrivenExampleServer::ConnectionObserver::onBelowWriteBufferLowWatermark() {
  if (server_.verbose_) {
    std::cout << "[Connection] Low watermark for id=" << connection_id_ << std::endl;
  }
}

void ConfigDrivenExampleServer::handleConnectionEvent(uint64_t connection_id,
                                                      network::ConnectionEvent event) {
  if (verbose_) {
    std::cout << "[Connection] Event " << static_cast<int>(event)
              << " for id=" << connection_id << std::endl;
  }

  switch (event) {
    case network::ConnectionEvent::Connected:
    case network::ConnectionEvent::ConnectedZeroRtt:
      break;
    case network::ConnectionEvent::RemoteClose:
    case network::ConnectionEvent::LocalClose:
      if (dispatcher_) {
        // Defer removal until after the current callback unwinds to avoid
        // deleting observer instances while ConnectionImpl iterates
        dispatcher_->post([this, connection_id]() {
          removeConnection(connection_id);
        });
      } else {
        removeConnection(connection_id);
      }
      break;
  }
}

void ConfigDrivenExampleServer::sendJsonRpcResponse(const json::JsonValue& id,
                                                    const json::JsonValue& payload) {
  json::JsonObjectBuilder response;
  response.add("jsonrpc", "2.0");
  response.add("id", id);
  response.add("result", payload);
  broadcastJson(response.build());
}

void ConfigDrivenExampleServer::sendJsonRpcError(const json::JsonValue& id,
                                                 int code,
                                                 const std::string& message) {
  json::JsonObjectBuilder error;
  error.add("code", code);
  error.add("message", message);

  json::JsonObjectBuilder response;
  response.add("jsonrpc", "2.0");
  response.add("id", id);
  response.add("error", error.build());
  broadcastJson(response.build());
}

void ConfigDrivenExampleServer::broadcastJson(const json::JsonValue& message) {
  if (active_connections_.empty()) {
    if (verbose_) {
      std::cout << "[Connection] No active connections to broadcast response" << std::endl;
    }
    return;
  }

  const std::string serialized = message.toString();
  const bool ends_with_newline = !serialized.empty() && serialized.back() == '\n';

  std::vector<uint64_t> targets;
  targets.reserve(active_connections_.size());
  for (const auto& entry : active_connections_) {
    if (entry.second) {
      targets.push_back(entry.first);
    }
  }

  for (auto id : targets) {
    auto it = active_connections_.find(id);
    if (it == active_connections_.end() || !it->second) {
      continue;
    }

    OwnedBuffer buffer;
    buffer.add(serialized);
    if (!ends_with_newline) {
      buffer.add("\n", 1);
    }

    it->second->write(buffer, false);

    if (verbose_) {
      std::cout << "[Connection] Sent message to id=" << id << std::endl;
    }
  }
}

void ConfigDrivenExampleServer::removeConnection(uint64_t connection_id) {
  auto observer_it = connection_observers_.find(connection_id);
  auto connection_it = active_connections_.find(connection_id);

  bool had_entry = false;

  if (connection_it != active_connections_.end() && observer_it != connection_observers_.end()) {
    if (connection_it->second) {
      connection_it->second->removeConnectionCallbacks(*observer_it->second);
    }
  }

  if (connection_it != active_connections_.end()) {
    active_connections_.erase(connection_it);
    had_entry = true;
  }
  if (observer_it != connection_observers_.end()) {
    connection_observers_.erase(observer_it);
    had_entry = true;
  }

  if (had_entry && verbose_) {
    std::cout << "[Connection] Closed id=" << connection_id << std::endl;
  }
}

// Global server instance for signal handling
ConfigDrivenExampleServer* g_server = nullptr;

void signalHandler(int signal) {
  std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
  if (g_server) {
    g_server->stop();
  }
}

void printUsage(const char* program_name) {
  std::cout << "Usage: " << program_name << " --config <config.json> [--verbose] [--help]" << std::endl;
  std::cout << std::endl;
  std::cout << "Config-driven MCP server demonstrating filter pipeline configuration" << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --config <file>     JSON configuration file (required)" << std::endl;
  std::cout << "  --verbose           Enable verbose output" << std::endl;
  std::cout << "  --help, -h          Show this help message" << std::endl;
  std::cout << std::endl;
  std::cout << "Example:" << std::endl;
  std::cout << "  " << program_name << " --config examples/configs/mcp_server_example.json --verbose" << std::endl;
  std::cout << std::endl;
  std::cout << "The server will demonstrate HTTP→SSE→JSON-RPC filter pipeline with:" << std::endl;
  std::cout << "  - echo tool: Echo input text with timestamp" << std::endl;
  std::cout << "  - config_info tool: Display server configuration" << std::endl;
}

int main(int argc, char* argv[]) {
  std::string config_path;
  bool verbose = false;

  // Parse command line arguments
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      config_path = argv[++i];
    } else if (std::strcmp(argv[i], "--verbose") == 0) {
      verbose = true;
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      printUsage(argv[0]);
      return 0;
    } else {
      std::cerr << "ERROR: Unknown argument: " << argv[i] << std::endl;
      printUsage(argv[0]);
      return 1;
    }
  }

  if (config_path.empty()) {
    std::cerr << "ERROR: Configuration file is required" << std::endl;
    printUsage(argv[0]);
    return 1;
  }

  // Create server instance
  ConfigDrivenExampleServer server(verbose);
  g_server = &server;

  // Set up signal handlers for graceful shutdown
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  // Load configuration
  if (!server.loadServerConfig(config_path)) {
    std::cerr << "ERROR: Failed to load server configuration" << std::endl;
    return 1;
  }

  // Start server
  if (!server.startServer()) {
    std::cerr << "ERROR: Failed to start server" << std::endl;
    return 1;
  }

  // Run server
  server.run();
  server.stop();

  return 0;
}
