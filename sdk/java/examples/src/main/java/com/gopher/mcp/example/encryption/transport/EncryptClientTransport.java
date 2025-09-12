package com.gopher.mcp.example.encryption.transport;

import com.gopher.mcp.example.encryption.util.EncryptionUtil;
import com.gopher.mcp.transport.CustomClientTransport;
import io.modelcontextprotocol.spec.McpSchema;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.TimeUnit;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;

public class EncryptClientTransport extends CustomClientTransport {

  private final Logger LOGGER = LoggerFactory.getLogger(getClass());

  public EncryptClientTransport(
      BlockingQueue<McpSchema.JSONRPCMessage> inbound,
      BlockingQueue<McpSchema.JSONRPCMessage> outbound) {
    super(inbound, outbound);
  }

  @Override
  public Mono<Void> sendMessage(McpSchema.JSONRPCMessage message) {
    // Check if transport is closed before processing
    if (closed.get()) {
      LOGGER.debug("[CLIENT] Transport is closed, skipping message send");
      return Mono.error(new IllegalStateException("Transport is closed"));
    }

    try {
      // INTERCEPT OUTGOING MESSAGES HERE
      String messageType = getMessageType(message);
      LOGGER.info("[CLIENT] → Sending {} (will encrypt here)", messageType);
      McpSchema.JSONRPCMessage encrypted = EncryptionUtil.encryptMessage(message);
      return super.sendMessage(encrypted)
          .onErrorMap(
              IllegalStateException.class,
              ex -> {
                LOGGER.debug(
                    "[CLIENT] Transport closed during send operation: {}", ex.getMessage());
                return ex;
              });
    } catch (Exception e) {
      LOGGER.error("[CLIENT] Error processing outbound message: {}", e.getMessage(), e);
      return Mono.error(e);
    }
  }

  @Override
  protected void startInboundProcessing() {
    inboundScheduler.schedule(
        () -> {
          while (!closed.get()) {
            try {
              McpSchema.JSONRPCMessage message = inboundQueue.poll(100, TimeUnit.MILLISECONDS);
              if (message != null && !closed.get()) {
                try {
                  // INTERCEPT INCOMING MESSAGES HERE
                  String messageType = getMessageType(message);
                  LOGGER.info("[CLIENT] ← Receiving {} (will decrypt here)", messageType);
                  McpSchema.JSONRPCMessage decrypted = EncryptionUtil.decryptMessage(message);
                  messageSink.tryEmitNext(decrypted);
                } catch (Exception e) {
                  LOGGER.error("[CLIENT] Error processing inbound message: {}", e.getMessage(), e);
                }
              }
            } catch (InterruptedException e) {
              Thread.currentThread().interrupt();
              break;
            }
          }
        });
  }

  // Helper to identify message type
  private String getMessageType(McpSchema.JSONRPCMessage message) {
    if (message instanceof McpSchema.JSONRPCRequest) {
      return "request";
    } else if (message instanceof McpSchema.JSONRPCResponse) {
      return "response";
    } else if (message instanceof McpSchema.JSONRPCNotification) {
      return "notification";
    }
    return "message";
  }
}
