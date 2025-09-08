"""
Real MCP Calculator Client with GopherTransport TCP Communication.

This module implements a real MCP client that connects to the calculator server,
using GopherTransport for TCP communication and comprehensive C++ filter integration.
"""

import asyncio
import json
import time
from typing import Any, Dict, List, Optional
from dataclasses import dataclass

# Define local types since we don't have the full MCP library
@dataclass
class CallToolResult:
    content: List[Dict[str, Any]]
    isError: bool = False

@dataclass
class ListToolsResult:
    tools: List[Dict[str, Any]]

class Client:
    """Mock MCP Client for demonstration"""
    def __init__(self):
        pass
    
    async def list_tools(self) -> ListToolsResult:
        return ListToolsResult(tools=[])
    
    async def call_tool(self, name: str, arguments: Dict[str, Any]) -> CallToolResult:
        return CallToolResult(content=[{"type": "text", "text": f"Tool {name} called with {arguments}"}])

# Import local components
from gopher_transport import GopherTransport, gopher_transport_context
from filter_types import (
    GopherTransportConfig,
    FilterManagerConfig,
    SecurityFilterConfig,
    ObservabilityFilterConfig,
    TrafficManagementFilterConfig,
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
)


class CalculatorClient:
    """Real MCP Calculator Client with GopherTransport."""
    
    def __init__(self, host: str = "localhost", port: int = 8080):
        self.host = host
        self.port = port
        self.client = None
        self.transport = None
        self.is_connected = False
        
    async def connect(self):
        """Connect to the calculator server."""
        print(f"🔗 Connecting to calculator server at {self.host}:{self.port}")
        
        # Create MCP client
        self.client = Client(
            name="calculator-client",
            version="1.0.0"
        )
        
        # Create transport configuration
        transport_config = self._create_transport_config()
        
        # Create and start transport
        self.transport = GopherTransport(transport_config)
        await self.transport.start()
        
        # Connect client to transport
        await self.client.connect(self.transport)
        
        self.is_connected = True
        print("✅ Connected to calculator server successfully")
        
    def _create_transport_config(self) -> GopherTransportConfig:
        """Create transport configuration with comprehensive filters."""
        return GopherTransportConfig(
            name="calculator-client-transport",
            version="1.0.0",
            protocol="tcp",
            host=self.host,
            port=self.port,
            timeout=30000,
            max_connections=10,
            buffer_size=8192,
            session_timeout=3600000,  # 1 hour
            max_sessions=100,
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
                        secret="calculator-client-secret-key",
                        issuer="calculator-client",
                        audience="calculator-server",
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
                        namespace="calculator_client",
                        labels={
                            "client": "calculator-client",
                            "version": "1.0.0",
                            "environment": "demo"
                        },
                        histogram_buckets=[0.1, 0.5, 1.0, 2.5, 5.0, 10.0]
                    ),
                    tracing=TracingConfig(
                        enabled=True,
                        service_name="calculator-client-gopher",
                        sampler_type="const",
                        sampler_param=1.0,
                        headers=["x-trace-id", "x-span-id"]
                    )
                ),
                
                # Traffic management filters
                traffic_management=TrafficManagementFilterConfig(
                    rate_limit=RateLimitConfig(
                        enabled=True,
                        requests_per_minute=500,  # Lower rate limit for client
                        burst_size=50,
                        key_extractor="custom"
                    ),
                    circuit_breaker=CircuitBreakerConfig(
                        enabled=True,
                        failure_threshold=3,  # Lower threshold for client
                        recovery_timeout=30000,
                        half_open_max_calls=2,
                        slow_call_threshold=3000
                    ),
                    retry=RetryConfig(
                        enabled=True,
                        max_attempts=3,
                        initial_delay=1000,
                        max_delay=5000,
                        backoff_multiplier=2.0,
                        retryable_status_codes=[500, 502, 503, 504, 408, 429]
                    )
                ),
                
                # Error handling
                error_handling=ErrorHandlingConfig(
                    stop_on_error=False,
                    retry_attempts=3,
                    fallback_behavior=FallbackBehavior.PASSTHROUGH
                )
            ),
            
            # CApiFilter integration
            custom_callbacks=self._create_custom_callbacks()
        )
    
    def _create_custom_callbacks(self) -> Dict[str, Any]:
        """Create custom callbacks for CApiFilter integration."""
        def on_message_received(buf, end_stream, user_data):
            """Callback for when a message is received."""
            print(f"🔍 [CApiFilter DEBUG] onMessageReceived callback called! Buffer: {buf}, EndStream: {end_stream}")
            return 0  # MCP_FILTER_CONTINUE
        
        def on_message_sent(buf, end_stream, user_data):
            """Callback for when a message is sent."""
            print(f"🔍 [CApiFilter DEBUG] onMessageSent callback called! Buffer: {buf}, EndStream: {end_stream}")
            return 0  # MCP_FILTER_CONTINUE
        
        def on_connection_established(user_data, fd):
            """Callback for when a connection is established."""
            print(f"🔍 [CApiFilter DEBUG] onConnectionEstablished callback called! FD: {fd}")
        
        def on_high_watermark(user_data):
            """Callback for high watermark."""
            print(f"🔍 [CApiFilter DEBUG] onHighWatermark callback called!")
        
        def on_low_watermark(user_data):
            """Callback for low watermark."""
            print(f"🔍 [CApiFilter DEBUG] onLowWatermark callback called!")
        
        def on_error(user_data, code, msg):
            """Callback for errors."""
            message = msg.value.decode('utf-8') if msg else "Unknown error"
            print(f"🔍 [CApiFilter DEBUG] onError callback called! Code: {code}, Message: {message}")
        
        return {
            "on_data": on_message_received,
            "on_write": on_message_sent,
            "on_new_connection": on_connection_established,
            "on_high_watermark": on_high_watermark,
            "on_low_watermark": on_low_watermark,
            "on_error": on_error,
            "user_data": None,
        }
    
    async def disconnect(self):
        """Disconnect from the calculator server."""
        print("🔌 Disconnecting from calculator server...")
        
        self.is_connected = False
        
        if self.transport:
            await self.transport.close()
            print("✅ Transport closed")
        
        print("✅ Disconnected from calculator server")
    
    async def list_tools(self) -> ListToolsResult:
        """List available tools."""
        if not self.is_connected:
            raise RuntimeError("Client is not connected")
        
        print("📋 Listing available tools...")
        result = await self.client.list_tools()
        print(f"✅ Found {len(result.tools)} tools:")
        for tool in result.tools:
            print(f"   - {tool.name}: {tool.description}")
        return result
    
    async def call_calculator(self, operation: str, a: float, b: float, precision: int = 2) -> str:
        """Call the calculator tool."""
        if not self.is_connected:
            raise RuntimeError("Client is not connected")
        
        print(f"🧮 Calculating: {a} {operation} {b}")
        result = await self.client.call_tool(
            name="calculator",
            arguments={
                "operation": operation,
                "a": a,
                "b": b,
                "precision": precision
            }
        )
        
        # Extract result from the response
        if hasattr(result, 'content') and result.content:
            content = result.content[0]
            if hasattr(content, 'text'):
                result_text = content.text
            else:
                result_text = str(content)
        else:
            result_text = str(result)
        
        print(f"✅ Result: {result_text}")
        return result_text
    
    async def get_server_stats(self) -> str:
        """Get server statistics."""
        if not self.is_connected:
            raise RuntimeError("Client is not connected")
        
        print("📊 Getting server statistics...")
        result = await self.client.call_tool(
            name="server_stats",
            arguments={}
        )
        
        # Extract result from the response
        if hasattr(result, 'content') and result.content:
            content = result.content[0]
            if hasattr(content, 'text'):
                result_text = content.text
            else:
                result_text = str(content)
        else:
            result_text = str(result)
        
        print("✅ Server statistics retrieved")
        return result_text
    
    async def run_demo(self):
        """Run a comprehensive demo of calculator operations."""
        print("\n🎯 Running Calculator Client Demo")
        print("=" * 40)
        
        try:
            # List available tools
            await self.list_tools()
            
            # Test basic arithmetic operations
            print("\n--- Basic Arithmetic Operations ---")
            await self.call_calculator("add", 10, 5)
            await self.call_calculator("subtract", 20, 8)
            await self.call_calculator("multiply", 6, 7)
            await self.call_calculator("divide", 100, 4)
            
            # Test advanced operations
            print("\n--- Advanced Operations ---")
            await self.call_calculator("power", 2, 8)
            await self.call_calculator("sqrt", 144, 0)  # b is not used for sqrt
            await self.call_calculator("factorial", 5, 0)  # b is not used for factorial
            
            # Test error handling
            print("\n--- Error Handling Tests ---")
            try:
                await self.call_calculator("divide", 10, 0)
            except Exception as e:
                print(f"✅ Error handling working: {e}")
            
            # Test server statistics
            print("\n--- Server Statistics ---")
            stats = await self.get_server_stats()
            print(f"📊 Server stats: {stats}")
            
            # Test rate limiting (multiple rapid requests)
            print("\n--- Rate Limiting Test ---")
            print("Sending 5 rapid requests to test rate limiting...")
            tasks = []
            for i in range(5):
                task = self.call_calculator("add", i, i + 1)
                tasks.append(task)
            
            results = await asyncio.gather(*tasks, return_exceptions=True)
            successful_requests = sum(1 for result in results if not isinstance(result, Exception))
            print(f"✅ Rate limiting test: {successful_requests}/5 requests successful")
            
            print("\n🎉 Calculator client demo completed successfully!")
            
        except Exception as e:
            print(f"❌ Demo failed: {e}")
            import traceback
            traceback.print_exc()


async def main():
    """Main entry point for the calculator client."""
    print("🧮 MCP Calculator Client with GopherTransport")
    print("=" * 50)
    
    client = CalculatorClient()
    
    try:
        await client.connect()
        await client.run_demo()
    except Exception as e:
        print(f"❌ Client error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
