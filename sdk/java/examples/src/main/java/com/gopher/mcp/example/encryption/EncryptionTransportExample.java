package com.gopher.mcp.example.encryption;

import com.gopher.mcp.example.encryption.transport.EncryptClientTransport;
import com.gopher.mcp.example.encryption.transport.EncryptServerTransport;
import com.gopher.mcp.transport.CustomServerTransportProvider;
import com.gopher.mcp.transport.QueueBasedTransport;
import io.modelcontextprotocol.spec.McpSchema;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;

import java.util.concurrent.TimeUnit;

/**
 * Simplest possible example showing how to intercept and modify messages.
 *
 * NOTE: You may see warnings about "Unexpected response for unknown id" - these are
 * harmless and occur when the client sends notifications after initialization. The
 * example still demonstrates message interception correctly.
 *
 * @author Example
 */
public class EncryptionTransportExample {

	private static final Logger LOGGER = LoggerFactory.getLogger(EncryptionTransportExample.class);

	public static void main(String[] args) {
		LOGGER.info("========================================");
		LOGGER.info("Simplest Intercept Example");
		LOGGER.info("========================================\n");

		try {
			// Create transport pair
			QueueBasedTransport.TransportPair transportPair = QueueBasedTransport.createPair();

			// Create intercepting transports
			EncryptClientTransport clientTransport = new EncryptClientTransport(transportPair.serverToClient,
					transportPair.clientToServer);

			EncryptServerTransport serverTransport = new EncryptServerTransport(transportPair.clientToServer,
					transportPair.serverToClient);

			// Create server provider
			CustomServerTransportProvider provider = new CustomServerTransportProvider(serverTransport);

			// Create server with capabilities
			var server = io.modelcontextprotocol.server.McpServer.async(provider)
				.serverInfo(new McpSchema.Implementation("test-server", "1.0.0"))
				.capabilities(McpSchema.ServerCapabilities.builder().tools(true).build())
				.tools(createEchoTool())
				.build();

			// Create client
			var client = io.modelcontextprotocol.client.McpClient.async(clientTransport)
				.clientInfo(new McpSchema.Implementation("test-client", "1.0.0"))
				.build();

			// Start server
			LOGGER.info("Starting server...");
			serverTransport.startListening();
			Thread.sleep(500);

			// Test operations
			LOGGER.info("\n--- Testing Intercepted Communication ---\n");

			LOGGER.info("1. Initialize:");
			McpSchema.InitializeResult initResult = client.initialize().toFuture().get(5, TimeUnit.SECONDS);
			LOGGER.info("   ✓ Initialized\n");

			LOGGER.info("2. Ping:");
			client.ping().toFuture().get(5, TimeUnit.SECONDS);
			LOGGER.info("   ✓ Ping successful\n");

			LOGGER.info("3. Call Tool:");
			McpSchema.CallToolResult result = client
				.callTool(new McpSchema.CallToolRequest("echo", java.util.Map.of("text", "Hello!")))
				.toFuture()
				.get(5, TimeUnit.SECONDS);

			if (result.content() != null && !result.content().isEmpty()) {
				McpSchema.TextContent content = (McpSchema.TextContent) result.content().get(0);
				LOGGER.info("   ✓ Response: {}", content.text());
			}

			LOGGER.info("\n========================================");
			LOGGER.info("Success! All messages were intercepted.");
			LOGGER.info("\nTo add encryption:");
			LOGGER.info("1. Implement encryptMessage()");
			LOGGER.info("2. Implement decryptMessage()");
			LOGGER.info("========================================");

			// Clean shutdown
			clientTransport.closeGracefully().block();
			serverTransport.closeGracefully().block();
			System.exit(0); // Exit cleanly

		}
		catch (Exception e) {
			LOGGER.error("Error: {}", e.getMessage());
			LOGGER.error("Stack trace:", e);
			System.exit(1);
		}
	}

	// Simple tool for testing
	private static io.modelcontextprotocol.server.McpServerFeatures.AsyncToolSpecification createEchoTool() {
		String schema = """
				{
					"type": "object",
					"properties": {
						"text": {"type": "string"}
					}
				}
				""";

		return io.modelcontextprotocol.server.McpServerFeatures.AsyncToolSpecification.builder()
			.tool(McpSchema.Tool.builder().name("echo").description("Echo tool").inputSchema(schema).build())
			.callHandler((exchange, request) -> {
				String text = (String) request.arguments().get("text");
				return Mono.just(new McpSchema.CallToolResult(
						java.util.List.of(new McpSchema.TextContent("Echo: " + text)), false));
			})
			.build();
	}

}
