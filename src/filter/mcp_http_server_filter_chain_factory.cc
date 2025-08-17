/**
 * MCP HTTP Server Filter Chain Factory Implementation
 * 
 * Following production architecture exactly:
 * - Transport sockets handle ONLY I/O
 * - Filters handle ALL protocol processing
 * - ConnectionImpl orchestrates everything
 */

#include "mcp/filter/mcp_http_server_filter_chain_factory.h"
#include "mcp/filter/http_server_codec_filter.h"
#include "mcp/filter/sse_codec_filter.h"
#include "mcp/network/connection.h"
#include "mcp/jsonrpc/request.h"
#include "mcp/jsonrpc/response.h"
#include "mcp/jsonrpc/notification.h"
#include <nlohmann/json.hpp>

namespace mcp {
namespace filter {

bool McpHttpServerFilterChainFactory::createFilterChain(
    network::FilterManager& filter_manager) const {
  
  // Create protocol bridge that connects filters to MCP callbacks
  // Note: We need to make bridges_ mutable or change the design
  auto bridge = std::make_unique<McpProtocolBridge>(
      message_callbacks_, nullptr, nullptr);
  
  // Create HTTP server codec filter
  // This filter parses HTTP requests and generates HTTP responses
  auto http_filter = std::make_shared<HttpServerCodecFilter>(
      *bridge, dispatcher_);
  
  // Create SSE codec filter  
  // This filter handles Server-Sent Events protocol
  auto sse_filter = std::make_shared<SseCodecFilter>(
      *bridge, true /* server mode */);
  
  // Store references in bridge for response handling
  bridge->http_filter_ = http_filter.get();
  bridge->sse_filter_ = sse_filter.get();
  
  // Add filters to filter manager
  // Following production pattern: FilterManager manages filters
  filter_manager.addReadFilter(http_filter);
  filter_manager.addWriteFilter(http_filter);
  filter_manager.addReadFilter(sse_filter);
  filter_manager.addWriteFilter(sse_filter);
  
  // Store bridge for lifetime management
  // Cast away const for now - we should redesign this to be cleaner
  const_cast<McpHttpServerFilterChainFactory*>(this)->bridges_.push_back(std::move(bridge));
  
  return true;
}

bool McpHttpServerFilterChainFactory::createNetworkFilterChain(
    network::FilterManager& filter_manager,
    const std::vector<network::FilterFactoryCb>& filter_factories) const {
  // Delegate to the main implementation
  return createFilterChain(filter_manager);
}

// McpProtocolBridge implementation

void McpHttpServerFilterChainFactory::McpProtocolBridge::onHeaders(
    const std::map<std::string, std::string>& headers,
    bool keep_alive) {
  
  // Determine request mode based on headers
  auto accept = headers.find("accept");
  if (accept != headers.end() && 
      accept->second.find("text/event-stream") != std::string::npos) {
    // SSE mode - send SSE response headers
    mode_ = RequestMode::SSE_STREAM;
    
    std::map<std::string, std::string> response_headers = {
      {"content-type", "text/event-stream"},
      {"cache-control", "no-cache"},
      {"connection", keep_alive ? "keep-alive" : "close"},
      {"access-control-allow-origin", "*"}
    };
    
    http_filter_.responseEncoder().encodeHeaders(200, response_headers, false);
    
    // Start SSE stream
    sse_filter_.startEventStream();
    
    // Send initial MCP initialize event if this is the first connection
    // In SSE mode, we typically send the protocol version immediately
    nlohmann::json init_event = {
      {"jsonrpc", "2.0"},
      {"method", "initialize"},
      {"params", {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()}
      }}
    };
    
    std::string event_data = init_event.dump();
    sse_filter_.eventEncoder().encodeEvent("message", event_data, nullopt);
    
  } else {
    // HTTP RPC mode
    mode_ = RequestMode::HTTP_RPC;
  }
}

void McpHttpServerFilterChainFactory::McpProtocolBridge::onBody(
    const std::string& data, bool end_stream) {
  
  current_request_body_ += data;
  
  if (end_stream && mode_ == RequestMode::HTTP_RPC) {
    // Parse JSON-RPC request
    try {
      auto json = nlohmann::json::parse(current_request_body_);
      
      if (json.contains("method")) {
        if (json.contains("id")) {
          // Request
          jsonrpc::Request request;
          request.id = json["id"];
          request.method = json["method"].get<std::string>();
          if (json.contains("params")) {
            request.params = json["params"];
          }
          
          // Forward to MCP callbacks
          message_callbacks_.onRequest(request);
          
        } else {
          // Notification
          jsonrpc::Notification notification;
          notification.method = json["method"].get<std::string>();
          if (json.contains("params")) {
            notification.params = json["params"];
          }
          
          // Forward to MCP callbacks
          message_callbacks_.onNotification(notification);
        }
      }
    } catch (const std::exception& e) {
      // Send error response
      nlohmann::json error_response = {
        {"jsonrpc", "2.0"},
        {"error", {
          {"code", -32700},
          {"message", "Parse error"}
        }},
        {"id", nullptr}
      };
      
      sendResponse(error_response.dump());
    }
    
    // Clear for next request
    current_request_body_.clear();
  }
}

void McpHttpServerFilterChainFactory::McpProtocolBridge::onMessageComplete() {
  // Request processing complete
  // In HTTP mode, we've already sent the response
  // In SSE mode, the connection stays open
}

void McpHttpServerFilterChainFactory::McpProtocolBridge::onError(
    const std::string& error) {
  // HTTP parsing error
  // Send error response
  nlohmann::json error_response = {
    {"jsonrpc", "2.0"},
    {"error", {
      {"code", -32600},
      {"message", error}
    }},
    {"id", nullptr}
  };
  
  sendResponse(error_response.dump());
  
  // Notify MCP layer
  Error mcp_error(jsonrpc::INTERNAL_ERROR, error);
  message_callbacks_.onError(mcp_error);
}

void McpHttpServerFilterChainFactory::McpProtocolBridge::onEvent(
    const std::string& event,
    const std::string& data,
    const optional<std::string>& id) {
  
  if (event == "message") {
    // Parse JSON-RPC message from SSE event
    try {
      auto json = nlohmann::json::parse(data);
      
      if (json.contains("method")) {
        if (json.contains("id")) {
          // Request
          jsonrpc::Request request;
          request.id = json["id"];
          request.method = json["method"].get<std::string>();
          if (json.contains("params")) {
            request.params = json["params"];
          }
          
          // Forward to MCP callbacks
          message_callbacks_.onRequest(request);
          
        } else {
          // Notification
          jsonrpc::Notification notification;
          notification.method = json["method"].get<std::string>();
          if (json.contains("params")) {
            notification.params = json["params"];
          }
          
          // Forward to MCP callbacks
          message_callbacks_.onNotification(notification);
        }
      } else if (json.contains("result") || json.contains("error")) {
        // Response
        jsonrpc::Response response;
        response.id = json["id"];
        if (json.contains("result")) {
          response.result = json["result"];
        } else {
          response.error = json["error"];
        }
        
        // Forward to MCP callbacks
        message_callbacks_.onResponse(response);
      }
    } catch (const std::exception& e) {
      // Invalid JSON in SSE event
      Error error(jsonrpc::PARSE_ERROR, "Invalid JSON in SSE event");
      message_callbacks_.onError(error);
    }
  }
}

void McpHttpServerFilterChainFactory::McpProtocolBridge::onComment(
    const std::string& comment) {
  // SSE comments are used for keep-alive, ignore them
}

void McpHttpServerFilterChainFactory::McpProtocolBridge::sendResponse(
    const std::string& response) {
  
  if (mode_ == RequestMode::HTTP_RPC) {
    // Send HTTP response
    std::map<std::string, std::string> response_headers = {
      {"content-type", "application/json"},
      {"content-length", std::to_string(response.length())},
      {"access-control-allow-origin", "*"}
    };
    
    http_filter_.responseEncoder().encodeHeaders(200, response_headers, false);
    
    Buffer response_data;
    response_data.add(response.c_str(), response.length());
    http_filter_.responseEncoder().encodeData(response_data, true);
    
  } else if (mode_ == RequestMode::SSE_STREAM) {
    // Send SSE event
    sse_filter_.eventEncoder().encodeEvent("message", response, nullopt);
  }
}

} // namespace filter
} // namespace mcp