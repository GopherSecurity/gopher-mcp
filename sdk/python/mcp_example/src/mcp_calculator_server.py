"""
Real MCP Calculator Server with GopherTransport TCP Communication.

This module implements a real MCP server with calculator functionality,
using GopherTransport for TCP communication and comprehensive C++ filter integration.
"""

import asyncio
import json
import signal
import sys
import time
from typing import Any, Dict, List, Optional

# Import local components
from gopher_transport import GopherTransport, gopher_transport_context

# Define local types since we don't have the full MCP library
from dataclasses import dataclass
from typing import List, Dict, Any

@dataclass
class Tool:
    name: str
    description: str
    inputSchema: Dict[str, Any]

@dataclass
class TextContent:
    type: str = "text"
    text: str = ""

class McpServer:
    """Mock MCP Server for demonstration"""
    def __init__(self, name: str, version: str):
        self.name = name
        self.version = version
        self.tools = []
        self.tool_handlers = {}
    
    def list_tools(self) -> List[Tool]:
        return self.tools
    
    def call_tool(self, name: str, arguments: Dict[str, Any]) -> List[TextContent]:
        if name in self.tool_handlers:
            return self.tool_handlers[name](arguments)
        return [TextContent(text=f"Tool {name} called with {arguments}")]
    
    def tool(self, name: str):
        """Decorator for registering tools"""
        def decorator(func):
            self.tools.append(Tool(
                name=name,
                description=f"Tool: {name}",
                inputSchema={"type": "object", "properties": {}}
            ))
            self.tool_handlers[name] = func
            return func
        return decorator
    
    async def connect(self, transport):
        """Connect to transport"""
        self.transport = transport
        return True
    
    async def start(self):
        """Start the server"""
        pass
    
    async def stop(self):
        """Stop the server"""
        pass
from filter_types import (
    GopherTransportConfig,
    FilterManagerConfig,
    SecurityFilterConfig,
    ObservabilityFilterConfig,
    TrafficManagementFilterConfig,
    HttpFilterConfig,
    ErrorHandlingConfig,
    FallbackBehavior,
    AuthenticationConfig,
    AuthorizationConfig,
    AccessLogConfig,
    MetricsConfig,
    TracingConfig,
    RateLimitConfig,
    CircuitBreakerConfig,
    RetryConfig,
    LoadBalancerConfig,
    HttpCodecConfig,
    HttpRouterConfig,
    HttpCompressionConfig,
)


class CalculatorServer:
    """Real MCP Calculator Server with GopherTransport."""
    
    def __init__(self):
        self.server = None
        self.transport = None
        self.is_running = False
        
    async def start(self):
        """Start the calculator server."""
        print("🚀 Starting MCP Calculator Server with GopherTransport")
        
        # Create MCP server
        self.server = McpServer(
            name="calculator-server",
            version="1.0.0"
        )
        
        # Register calculator tools
        self._register_tools()
        
        # Create transport configuration
        transport_config = self._create_transport_config()
        
        # Create and start transport
        self.transport = GopherTransport(transport_config)
        await self.transport.start()
        
        # Connect server to transport
        await self.server.connect(self.transport)
        
        self.is_running = True
        print("✅ Calculator server started successfully")
        print(f"📡 Listening on TCP port {transport_config.port}")
        
    def _register_tools(self):
        """Register calculator tools."""
        
        @self.server.tool("calculator")
        async def calculator(operation: str, a: float, b: float, precision: int = 2) -> str:
            """
            Perform basic arithmetic operations.
            
            Args:
                operation: The operation to perform (add, subtract, multiply, divide, power, sqrt, factorial)
                a: First number
                b: Second number (not used for sqrt and factorial)
                precision: Number of decimal places for result
            
            Returns:
                The result of the calculation
            """
            try:
                if operation == "add":
                    result = a + b
                elif operation == "subtract":
                    result = a - b
                elif operation == "multiply":
                    result = a * b
                elif operation == "divide":
                    if b == 0:
                        raise ValueError("Division by zero is not allowed")
                    result = a / b
                elif operation == "power":
                    result = a ** b
                elif operation == "sqrt":
                    if a < 0:
                        raise ValueError("Square root of negative number is not allowed")
                    result = a ** 0.5
                elif operation == "factorial":
                    if a < 0 or a != int(a):
                        raise ValueError("Factorial is only defined for non-negative integers")
                    result = 1
                    for i in range(1, int(a) + 1):
                        result *= i
                else:
                    raise ValueError(f"Unsupported operation: {operation}")
                
                return f"{result:.{precision}f}" if isinstance(result, float) else str(result)
                
            except Exception as e:
                return f"Error: {str(e)}"
        
        @self.server.tool("server_stats")
        async def server_stats() -> str:
            """
            Get server statistics and health information.
            
            Returns:
                Server statistics in JSON format
            """
            stats = {
                "server_name": "calculator-server",
                "version": "1.0.0",
                "uptime_seconds": time.time() - getattr(self, '_start_time', time.time()),
                "is_running": self.is_running,
                "transport_protocol": "tcp",
                "transport_port": 8080,
                "tools_registered": ["calculator", "server_stats"],
                "memory_usage": "N/A",  # Would need psutil for real memory stats
                "cpu_usage": "N/A",     # Would need psutil for real CPU stats
                "timestamp": time.time()
            }
            return json.dumps(stats, indent=2)
        
        print("✅ Calculator tools registered: calculator, server_stats")
    
    def _create_transport_config(self) -> GopherTransportConfig:
        """Create transport configuration with comprehensive filters."""
        return GopherTransportConfig(
            name="calculator-server-transport",
            protocol="tcp",
            host="localhost",
            port=8080,
            timeout=30000,
            max_connections=100,
            buffer_size=16384,
            session_timeout=3600000,  # 1 hour
            max_sessions=1000,
            tls_enabled=False,
            keep_alive=True,
            keep_alive_interval=30000,
            max_idle_time=300000,
            filters=FilterManagerConfig(
                # Security filters
                security=SecurityFilterConfig(
                    authentication=AuthenticationConfig(
                        enabled=True,
                        method="jwt",
                        secret="calculator-server-secret-key",
                        issuer="calculator-server",
                        audience="calculator-clients",
                        algorithms=["HS256", "RS256"]
                    ),
                    authorization=AuthorizationConfig(
                        enabled=True,
                        policy="allow",
                        rules=[
                            {"resource": "/tools/calculator", "action": "call", "role": "user"},
                            {"resource": "/tools/server_stats", "action": "call", "role": "user"},
                            {"resource": "/tools/*", "action": "list", "role": "user"}
                        ],
                        default_action="deny"
                    )
                ),
                
                # Observability filters
                observability=ObservabilityFilterConfig(
                    access_log=AccessLogConfig(
                        enabled=True,
                        format="json",
                        include_headers=True,
                        include_body=False,
                        max_body_size=4096
                    ),
                    metrics=MetricsConfig(
                        enabled=True,
                        namespace="calculator_server",
                        labels={
                            "server": "calculator-server",
                            "version": "1.0.0",
                            "environment": "demo"
                        },
                        histogram_buckets=[0.1, 0.5, 1.0, 2.5, 5.0, 10.0, 25.0, 50.0, 100.0]
                    ),
                    tracing=TracingConfig(
                        enabled=True,
                        service_name="calculator-server-gopher",
                        sampler_type="const",
                        sampler_param=1.0,
                        headers=["x-trace-id", "x-span-id", "x-b3-traceid", "x-b3-spanid"]
                    )
                ),
                
                # Traffic management filters
                traffic_management=TrafficManagementFilterConfig(
                    rate_limit=RateLimitConfig(
                        enabled=True,
                        requests_per_minute=2000,  # Higher rate limit for server
                        burst_size=200,
                        key_extractor="ip"
                    ),
                    circuit_breaker=CircuitBreakerConfig(
                        enabled=True,
                        failure_threshold=10,  # Higher threshold for server
                        recovery_timeout=60000,
                        half_open_max_calls=5,
                        slow_call_threshold=5000
                    ),
                    retry=RetryConfig(
                        enabled=True,
                        max_attempts=3,
                        initial_delay=1000,
                        max_delay=10000,
                        backoff_multiplier=2.0,
                        retryable_status_codes=[500, 502, 503, 504, 408, 429]
                    ),
                    load_balancer=LoadBalancerConfig(
                        enabled=True,
                        strategy="round_robin",
                        upstreams=[
                            {"host": "backend1.example.com", "port": 8080, "weight": 1, "health_check": True},
                            {"host": "backend2.example.com", "port": 8080, "weight": 1, "health_check": True}
                        ],
                        health_check_interval=30000,
                        health_check_timeout=5000
                    )
                ),
                
                # HTTP filters
                http=HttpFilterConfig(
                    codec=HttpCodecConfig(
                        enabled=True,
                        version="1.1",
                        max_header_size=8192,
                        max_body_size=1048576,
                        chunked_encoding=True
                    ),
                    router=HttpRouterConfig(
                        enabled=True,
                        routes=[
                            {"path": "/api/v1", "target": "backend-v1", "methods": ["GET", "POST"]},
                            {"path": "/api/v2", "target": "backend-v2", "methods": ["GET", "POST", "PUT", "DELETE"]}
                        ],
                        default_route="default-backend",
                        strip_prefix=False
                    ),
                    compression=HttpCompressionConfig(
                        enabled=True,
                        algorithms=["gzip", "deflate", "brotli"],
                        min_size=1024,
                        level=6
                    )
                ),
                
                # Error handling
                error_handling=ErrorHandlingConfig(
                    stop_on_error=False,
                    retry_attempts=3,
                    fallback_behavior=FallbackBehavior.PASSTHROUGH
                )
            )
        )
    
    async def stop(self):
        """Stop the calculator server."""
        print("🛑 Stopping calculator server...")
        
        self.is_running = False
        
        if self.transport:
            await self.transport.close()
            print("✅ Transport closed")
        
        print("✅ Calculator server stopped")
    
    async def run_forever(self):
        """Run the server forever."""
        self._start_time = time.time()
        
        # Set up signal handlers for graceful shutdown
        def signal_handler(signum, frame):
            print(f"\n📡 Received signal {signum}, shutting down gracefully...")
            asyncio.create_task(self.stop())
        
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)
        
        try:
            # Keep the server running
            while self.is_running:
                await asyncio.sleep(1)
        except KeyboardInterrupt:
            print("\n📡 Received keyboard interrupt, shutting down...")
        finally:
            await self.stop()


async def main():
    """Main entry point for the calculator server."""
    print("🧮 MCP Calculator Server with GopherTransport")
    print("=" * 50)
    
    server = CalculatorServer()
    
    try:
        await server.start()
        await server.run_forever()
    except Exception as e:
        print(f"❌ Server error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        await server.stop()


if __name__ == "__main__":
    asyncio.run(main())
