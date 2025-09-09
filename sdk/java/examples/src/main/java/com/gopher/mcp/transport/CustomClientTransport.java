package com.gopher.mcp.transport;

import io.modelcontextprotocol.spec.McpClientTransport;
import io.modelcontextprotocol.spec.McpSchema.JSONRPCMessage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;
import reactor.core.scheduler.Schedulers;

import java.util.concurrent.BlockingQueue;
import java.util.function.Consumer;
import java.util.function.Function;

/**
 * Custom client transport implementation using queue-based communication.
 * Provides the standard MCP client behavior that all clients need.
 */
public class CustomClientTransport extends QueueBasedTransport implements McpClientTransport {

	private static final Logger LOGGER = LoggerFactory.getLogger(CustomClientTransport.class);

	private Consumer<Throwable> exceptionHandler;

	private Function<Mono<JSONRPCMessage>, Mono<JSONRPCMessage>> messageHandler;

	public CustomClientTransport(BlockingQueue<JSONRPCMessage> inbound, BlockingQueue<JSONRPCMessage> outbound) {
		super(inbound, outbound);
	}

	@Override
	public Mono<Void> connect(Function<Mono<JSONRPCMessage>, Mono<JSONRPCMessage>> handler) {
		this.messageHandler = handler;

		return Mono.fromRunnable(() -> {
			startInboundProcessing();
			startMessageHandling();
		});
	}

	private void startMessageHandling() {
		messageSink.asFlux().subscribeOn(Schedulers.boundedElastic()).flatMap(message -> {
			if (messageHandler != null && !closed.get()) {
				return messageHandler.apply(Mono.just(message)).onErrorResume(error -> {
					handleException(error);
					return Mono.empty();
				});
			}
			return Mono.empty();
		}).subscribe(response -> {
			if (response != null && !closed.get()) {
				sendMessage(response)
					.doOnError(this::handleException)
					.onErrorResume(error -> Mono.empty())
					.subscribe();
			}
		}, this::handleException);
	}

	@Override
	public void setExceptionHandler(Consumer<Throwable> handler) {
		this.exceptionHandler = handler;
	}

	private void handleException(Throwable error) {
		if (exceptionHandler != null) {
			exceptionHandler.accept(error);
		}
		else {
			LOGGER.error("Client transport error: {}", error.getMessage());
		}
	}

}
