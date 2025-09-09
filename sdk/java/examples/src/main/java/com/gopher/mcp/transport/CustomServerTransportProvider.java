package com.gopher.mcp.transport;

import io.modelcontextprotocol.spec.McpServerSession;
import io.modelcontextprotocol.spec.McpServerTransportProvider;
import reactor.core.publisher.Mono;

/**
 * Custom server transport provider for queue-based transport.
 *
 * <pre>
 * This provider handles MCP (Model Context Protocol) session management
 * by implementing a queue-based message transport mechanism between
 * client and server communications.
 * </pre>
 */
public class CustomServerTransportProvider implements McpServerTransportProvider {

  private final CustomServerTransport transport;

  private McpServerSession.Factory sessionFactory;

  private McpServerSession session;

  public CustomServerTransportProvider(CustomServerTransport transport) {
    this.transport = transport;
  }

  @Override
  public void setSessionFactory(McpServerSession.Factory sessionFactory) {
    this.sessionFactory = sessionFactory;
    // Create a session and connect it to the transport
    this.session = sessionFactory.create(transport);
    // Start the transport to listen for messages
    transport.startListening();
    // Subscribe the session to handle incoming messages
    transport.messages().flatMap(message -> session.handle(message)).subscribe();
  }

  @Override
  public Mono<Void> closeGracefully() {
    // Close the transport gracefully
    if (transport != null) {
      return transport.closeGracefully();
    }
    return Mono.empty();
  }

  @Override
  public Mono<Void> notifyClients(String method, Object params) {
    // This is for broadcasting notifications to all connected clients
    // In our simple implementation with a single transport, we don't need to
    // implement this
    // as we handle notifications directly through the transport
    return Mono.empty();
  }

  public CustomServerTransport getTransport() {
    return transport;
  }

  public McpServerSession.Factory getSessionFactory() {
    return sessionFactory;
  }
}
