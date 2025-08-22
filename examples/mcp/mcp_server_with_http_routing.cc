/**
 * Example: MCP Server with HTTP Routing (Simplified)
 * 
 * This example demonstrates the architecture for clean HTTP endpoint handling.
 * Due to the current server implementation, the custom filter chain factory
 * would need to be integrated into McpServer::performListen().
 * 
 * This example shows:
 * 1. How to create a custom filter chain factory with HTTP routing
 * 2. The proper architecture for endpoint handling
 * 3. Clean separation of concerns
 */

#include <iostream>
#include <memory>
#include <string>
#include <ctime>

#include "mcp/server/mcp_server.h"
#include "mcp/filter/http_routing_filter.h"
#include "mcp/filter/http_sse_filter_chain_factory.h"
#include "mcp/network/filter.h"
#include "mcp/types.h"

using namespace mcp;

int main(int argc, char* argv[]) {
  // Configure server
  server::McpServerConfig config;
  config.server_name = "mcp-server-with-routing";
  config.server_version = "1.0.0";
  config.instructions = "MCP server demonstrating HTTP routing architecture";
  config.worker_threads = 4;
  
  std::cout << "==================================================" << std::endl;
  std::cout << "MCP Server with HTTP Routing Architecture Demo" << std::endl;
  std::cout << "==================================================" << std::endl;
  std::cout << std::endl;
  std::cout << "This example demonstrates the proper architecture for" << std::endl;
  std::cout << "handling arbitrary HTTP endpoints with clean separation" << std::endl;
  std::cout << "of concerns:" << std::endl;
  std::cout << std::endl;
  std::cout << "1. Filter chain handles protocol translation (HTTP/SSE/JSON-RPC)" << std::endl;
  std::cout << "2. Optional HTTP routing filter handles endpoint routing" << std::endl;
  std::cout << "3. Application registers handlers for specific endpoints" << std::endl;
  std::cout << std::endl;
  
  try {
    // Create standard MCP server
    server::McpServer server(config);
    
    // Register MCP protocol handlers (JSON-RPC methods)
    server.registerRequestHandler("ping",
        [](const jsonrpc::Request& req, server::SessionContext& session) -> jsonrpc::Response {
      jsonrpc::Response resp;
      resp.id = req.id;
      resp.result = "pong";
      return resp;
    });
    
    server.registerRequestHandler("echo",
        [](const jsonrpc::Request& req, server::SessionContext& session) -> jsonrpc::Response {
      jsonrpc::Response resp;
      resp.id = req.id;
      // Echo back the params if present
      if (req.params.has_value()) {
        // Convert Metadata to string for simplicity
        resp.result = std::string("Echo: params received");
      } else {
        resp.result = std::string("Echo: no params");
      }
      return resp;
    });
    
    // Start listening
    auto result = server.listen("http://0.0.0.0:3000");
    if (holds_alternative<Error>(result)) {
      std::cerr << "[ERROR] Failed to start server: " << get<Error>(result).message << std::endl;
      return 1;
    }
    
    std::cout << "[INFO] Server listening on http://0.0.0.0:3000" << std::endl;
    std::cout << "[INFO] The server currently uses the standard MCP filter chain." << std::endl;
    std::cout << "[INFO] To add HTTP routing, modify McpServer::performListen()" << std::endl;
    std::cout << "[INFO] to use a custom filter chain factory that includes" << std::endl;
    std::cout << "[INFO] the HttpRoutingFilter." << std::endl;
    std::cout << std::endl;
    std::cout << "[INFO] With HTTP routing enabled, these endpoints would be available:" << std::endl;
    std::cout << "  - GET /health     - Health check" << std::endl;
    std::cout << "  - GET /metrics    - Prometheus metrics" << std::endl;
    std::cout << "  - GET /info       - Server information" << std::endl;
    std::cout << "  - POST /rpc       - JSON-RPC endpoint" << std::endl;
    std::cout << "  - GET /events     - SSE event stream" << std::endl;
    std::cout << std::endl;
    std::cout << "[INFO] Current endpoints (standard MCP):" << std::endl;
    std::cout << "  - " << config.http_rpc_path << "  - JSON-RPC endpoint" << std::endl;
    std::cout << "  - " << config.http_sse_path << " - SSE event stream" << std::endl;
    std::cout << std::endl;
    
    // Run event loop
    server.run();
    
  } catch (const std::exception& e) {
    std::cerr << "[ERROR] Server failed: " << e.what() << std::endl;
    return 1;
  }
  
  return 0;
}

/**
 * Custom Filter Chain Factory with HTTP Routing (for reference)
 * 
 * This shows how to create a custom filter chain factory that adds
 * HTTP routing to the standard MCP filter chain.
 */
namespace example {

using namespace mcp;
using namespace mcp::filter;

class HttpRoutingFilterChainFactory : public HttpSseFilterChainFactory {
public:
  HttpRoutingFilterChainFactory(mcp::event::Dispatcher& dispatcher,
                               McpProtocolCallbacks& callbacks)
      : HttpSseFilterChainFactory(dispatcher, callbacks, true),
        dispatcher_(dispatcher) {}
  
  bool createFilterChain(network::FilterManager& filter_manager) const override {
    // First create the standard MCP filter chain
    if (!HttpSseFilterChainFactory::createFilterChain(filter_manager)) {
      return false;
    }
    
    // Then add our HTTP routing filter (when properly implemented)
    auto routing_filter = createRoutingFilter();
    // NOTE: HttpRoutingFilter is not a network filter, it's a MessageCallbacks
    // This would need proper integration with the HTTP codec filter
    // filter_manager.addReadFilter(routing_filter);
    // filter_manager.addWriteFilter(routing_filter);
    
    return true;
  }
  
private:
  std::shared_ptr<HttpRoutingFilter> createRoutingFilter() const {
    // NOTE: HttpRoutingFilter requires MessageCallbacks and MessageEncoder
    // This example shows the architecture but needs proper initialization
    // auto filter = std::make_shared<HttpRoutingFilter>(callbacks, encoder, true);
    
    // For now, return nullptr as this is a demonstration
    return nullptr;
    
    /* Example registration code (when properly initialized):
    // Register health endpoint
    filter->registerHandler("GET", "/health", 
        [](const HttpRoutingFilter::RequestContext& req) {
      HttpRoutingFilter::Response resp;
      resp.status_code = 200;
      resp.headers["content-type"] = "application/json";
      resp.headers["cache-control"] = "no-cache";
      
      resp.body = R"({
        "status": "healthy",
        "service": "mcp-server",
        "version": "1.0.0",
        "uptime": )" + std::to_string(std::time(nullptr)) + R"(
      })";
      
      resp.headers["content-length"] = std::to_string(resp.body.length());
      return resp;
    });
    
    // Register metrics endpoint
    filter->registerHandler("GET", "/metrics",
        [](const HttpRoutingFilter::RequestContext& req) {
      HttpRoutingFilter::Response resp;
      resp.status_code = 200;
      resp.headers["content-type"] = "text/plain";
      
      resp.body = R"(# HELP mcp_server_connections_total Total number of connections
# TYPE mcp_server_connections_total counter
mcp_server_connections_total 42

# HELP mcp_server_requests_total Total number of requests
# TYPE mcp_server_requests_total counter
mcp_server_requests_total 1337
)";
      
      resp.headers["content-length"] = std::to_string(resp.body.length());
      return resp;
    });
    
    // Register info endpoint
    filter->registerHandler("GET", "/info",
        [](const HttpRoutingFilter::RequestContext& req) {
      HttpRoutingFilter::Response resp;
      resp.status_code = 200;
      resp.headers["content-type"] = "application/json";
      
      resp.body = R"({
        "server": "MCP Server with HTTP Routing",
        "protocols": ["http", "sse", "json-rpc"],
        "endpoints": {
          "health": "/health",
          "metrics": "/metrics",
          "json_rpc": "/rpc",
          "sse_events": "/events"
        }
      })";
      
      resp.headers["content-length"] = std::to_string(resp.body.length());
      return resp;
    });
    
    return filter;
    */
  }
  
  mcp::event::Dispatcher& dispatcher_;
};

} // namespace example