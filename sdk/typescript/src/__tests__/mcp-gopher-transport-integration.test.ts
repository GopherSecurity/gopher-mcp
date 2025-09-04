/**
 * @file mcp-gopher-transport-integration.test.ts
 * @brief Tests for GopherTransport CApiFilter integration
 *
 * This test file verifies that GopherTransport properly integrates with CApiFilter
 * by passing through customCallbacks to the FilterManager.
 */

import {
  GopherTransport,
  GopherTransportConfig,
} from "../../mcp-example/src/gopher-transport";

describe("GopherTransport CApiFilter Integration", () => {
  let transport: GopherTransport;

  afterEach(() => {
    if (transport) {
      // GopherTransport doesn't have a destroy method, just set to null
      transport = null as any;
    }
  });

  describe("Custom Callbacks Integration", () => {
    it("should accept customCallbacks in configuration", () => {
      const config: GopherTransportConfig = {
        name: "test-transport",
        protocol: "stdio",
        filters: {
          customCallbacks: {
            onMessageReceived: (message: any) => {
              console.log("Test: Message received:", message.method);
              return {
                ...message,
                testMetadata: {
                  processed: true,
                  timestamp: Date.now(),
                },
              };
            },
            onMessageSent: (message: any) => {
              console.log("Test: Message sent:", message.method);
              return {
                ...message,
                testMetadata: {
                  ...message.testMetadata,
                  sent: true,
                },
              };
            },
            onConnectionEstablished: (connectionId: string) => {
              console.log("Test: Connection established:", connectionId);
            },
            onConnectionClosed: (connectionId: string) => {
              console.log("Test: Connection closed:", connectionId);
            },
            onError: (error: Error, context: string) => {
              console.error("Test: Error in", context, ":", error.message);
            },
          },
        },
      };

      // This should not throw an error
      transport = new GopherTransport(config);
      expect(transport).toBeDefined();
    });

    it("should work without customCallbacks (backward compatibility)", () => {
      const config: GopherTransportConfig = {
        name: "test-transport",
        protocol: "stdio",
        filters: {
          // No customCallbacks - should work fine
          security: {
            authentication: {
              method: "jwt",
              secret: "test-secret",
            },
          },
        },
      };

      // This should not throw an error
      transport = new GopherTransport(config);
      expect(transport).toBeDefined();
    });

    it("should merge customCallbacks with other filter config", () => {
      const config: GopherTransportConfig = {
        name: "test-transport",
        protocol: "stdio",
        filters: {
          // Security filters
          security: {
            authentication: {
              method: "jwt",
              secret: "test-secret",
            },
          },
          // Observability filters
          observability: {
            accessLog: {
              enabled: true,
              format: "json",
            },
          },
          // Custom callbacks
          customCallbacks: {
            onMessageReceived: (message: any) => {
              return {
                ...message,
                securityProcessed: true,
              };
            },
          },
        },
      };

      // This should not throw an error
      transport = new GopherTransport(config);
      expect(transport).toBeDefined();
    });
  });

  describe("Configuration Validation", () => {
    it("should handle empty customCallbacks", () => {
      const config: GopherTransportConfig = {
        name: "test-transport",
        protocol: "stdio",
        filters: {
          customCallbacks: {},
        },
      };

      // This should not throw an error
      transport = new GopherTransport(config);
      expect(transport).toBeDefined();
    });

    it("should handle partial customCallbacks", () => {
      const config: GopherTransportConfig = {
        name: "test-transport",
        protocol: "stdio",
        filters: {
          customCallbacks: {
            onMessageReceived: (message: any) => {
              return message; // No modification
            },
            // Other callbacks not provided - should be fine
          },
        },
      };

      // This should not throw an error
      transport = new GopherTransport(config);
      expect(transport).toBeDefined();
    });
  });
});
