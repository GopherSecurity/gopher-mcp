package com.gopher.mcp.transport;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.modelcontextprotocol.spec.McpSchema.JSONRPCMessage;
import io.modelcontextprotocol.spec.McpTransport;
import reactor.core.publisher.Mono;
import reactor.core.publisher.Sinks;
import reactor.core.scheduler.Scheduler;
import reactor.core.scheduler.Schedulers;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * A custom queue-based transport implementation for MCP. This transport uses blocking
 * queues to simulate network communication between client and server in the same process.
 *
 *  <pre>
 *  ⏺ Why Custom Transports Were Created
 *
 *   Here's why QueueBasedTransport were created instead of using the official SDK transports:
 *
 *   1. Testing and Examples in Same Process
 *
 *   - Official SDK: StdioClientTransport uses actual process I/O (stdin/stdout) for communication between client and server
 *   - Custom Implementation: QueueBasedTransport uses in-memory BlockingQueue for communication within the same JVM process
 *
 *   This is crucial for:
 *   - Unit testing without spawning separate processes
 *   - Running examples where both client and server are in the same JVM
 *   - Demonstrating filter integration without process overhead
 *
 *    2. Simplicity for Examples
 *
 *   The custom transports are much simpler:
 *   // Custom - simple queue-based
 *   BlockingQueue<JSONRPCMessage> clientToServer = new LinkedBlockingQueue<>();
 *   BlockingQueue<JSONRPCMessage> serverToClient = new LinkedBlockingQueue<>();
 *
 *   // Official - requires process management
 *   ServerParameters params = new ServerParameters("command", List.of("args"));
 *   StdioClientTransport transport = new StdioClientTransport(params);
 *
 *   3. Direct Message Interception
 *
 *   The custom transports allow easy message interception for:
 *   - Filter integration testing (as seen in FilteredClientTransport)
 *   - Encryption example (as seen in EncryptClientTransport)
 *   - Direct access to message queues for testing
 *
 *   4. No External Dependencies
 *
 *   - Official StdioTransport requires an actual external process
 *   - Custom transports work entirely in-memory, making examples self-contained
 *
 *   Could We Use Official Transports?
 *
 *   Yes, but with limitations:
 *
 *   1. For Production: You should definitely use the official transports like StdioClientTransport
 *   2. For Testing: The custom queue-based transports are more suitable
 *   3. For Examples: Custom transports make demonstrations clearer without process management complexity
 *
 *   Recommendation
 *
 *   The custom transports serve a valid purpose for:
 *   - Testing - In-memory, no external processes needed
 *   - Examples - Self-contained demonstrations
 *   - Filter Development - Easy to intercept and modify messages
 *
 *   For production MCP applications, you should use the official SDK transports.
 *   The custom ones are essentially test doubles that simulate the transport layer for development and demonstration purposes.
 *	<pre>
 */
public abstract class QueueBasedTransport implements McpTransport {

	protected final ObjectMapper objectMapper;

	protected final BlockingQueue<JSONRPCMessage> inboundQueue;

	protected final BlockingQueue<JSONRPCMessage> outboundQueue;

	protected final Sinks.Many<JSONRPCMessage> messageSink;

	protected final AtomicBoolean closed = new AtomicBoolean(false);

	protected final Scheduler inboundScheduler;

	protected QueueBasedTransport(BlockingQueue<JSONRPCMessage> inbound, BlockingQueue<JSONRPCMessage> outbound) {
		this.objectMapper = new ObjectMapper();
		this.inboundQueue = inbound;
		this.outboundQueue = outbound;
		this.messageSink = Sinks.many().multicast().onBackpressureBuffer();
		this.inboundScheduler = Schedulers.newSingle("queue-transport-inbound");
	}

	protected void startInboundProcessing() {
		inboundScheduler.schedule(() -> {
			while (!closed.get()) {
				try {
					JSONRPCMessage message = inboundQueue.poll(100, java.util.concurrent.TimeUnit.MILLISECONDS);
					if (message != null && !closed.get()) {
						messageSink.tryEmitNext(message);
					}
				}
				catch (InterruptedException e) {
					Thread.currentThread().interrupt();
					break;
				}
			}
		});
	}

	@Override
	public Mono<Void> sendMessage(JSONRPCMessage message) {
		if (closed.get()) {
			// Return empty instead of error to avoid error propagation when closing
			return Mono.empty();
		}

		return Mono.fromRunnable(() -> {
			try {
				if (!closed.get()) {
					outboundQueue.put(message);
				}
			}
			catch (InterruptedException e) {
				Thread.currentThread().interrupt();
				// Don't throw if we're closing
				if (!closed.get()) {
					throw new RuntimeException("Failed to send message", e);
				}
			}
		}).subscribeOn(Schedulers.boundedElastic()).then();
	}

	@Override
	public Mono<Void> closeGracefully() {
		return Mono.fromRunnable(() -> {
			if (closed.compareAndSet(false, true)) {
				messageSink.tryEmitComplete();
				inboundScheduler.dispose();
				inboundQueue.clear();
				outboundQueue.clear();
			}
		});
	}

	@Override
	public <T> T unmarshalFrom(Object data, TypeReference<T> typeRef) {
		return objectMapper.convertValue(data, typeRef);
	}

	/**
	 * Creates a paired set of transport queues for client-server communication.
	 * @return TransportPair containing connected client and server queues
	 */
	public static TransportPair createPair() {
		BlockingQueue<JSONRPCMessage> clientToServer = new LinkedBlockingQueue<>();
		BlockingQueue<JSONRPCMessage> serverToClient = new LinkedBlockingQueue<>();
		return new TransportPair(clientToServer, serverToClient);
	}

	/**
	 * Container for paired transport queues.
	 */
	public static class TransportPair {

		public final BlockingQueue<JSONRPCMessage> clientToServer;

		public final BlockingQueue<JSONRPCMessage> serverToClient;

		public TransportPair(BlockingQueue<JSONRPCMessage> clientToServer,
				BlockingQueue<JSONRPCMessage> serverToClient) {
			this.clientToServer = clientToServer;
			this.serverToClient = serverToClient;
		}

	}

}
