package com.gopher.mcp.transport;

import io.modelcontextprotocol.spec.McpSchema.JSONRPCMessage;
import io.modelcontextprotocol.spec.McpServerTransport;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Sinks;

import java.util.concurrent.BlockingQueue;

/**
 * Custom server transport implementation using queue-based communication.
 * Provides the standard MCP server behavior that all servers need.
 */
public class CustomServerTransport extends QueueBasedTransport implements McpServerTransport {

	public CustomServerTransport(BlockingQueue<JSONRPCMessage> inbound, BlockingQueue<JSONRPCMessage> outbound) {
		super(inbound, outbound);
	}

	public void startListening() {
		startInboundProcessing();
	}

	public Sinks.Many<JSONRPCMessage> getMessageSink() {
		return messageSink;
	}

	public Flux<JSONRPCMessage> messages() {
		return messageSink.asFlux();
	}

}
