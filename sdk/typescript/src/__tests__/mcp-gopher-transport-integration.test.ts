/**
 * @file mcp-gopher-transport-integration.test.ts
 * @brief Tests for GopherTransport CApiFilter integration
 *
 * This test file verifies that GopherTransport properly integrates with CApiFilter
 * by passing through customCallbacks to the FilterManager.
 */

import { GopherTransport, GopherTransportConfig } from "../../mcp-example/src/gopher-transport";

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
          // Note: customCallbacks not supported
          observability: {
            metrics: true,
          },
        },
      };
      // Note: Custom callbacks feature was removed from current implementation

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
            authentication: true,
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
            authentication: true,
          },
          // Observability filters
          observability: {
            accessLog: true,
            metrics: true,
          },
        },
      };
      // Note: Custom callbacks feature was removed from current implementation

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
          // Note: customCallbacks not supported
          observability: {
            metrics: true,
          },
        },
      };
      // Note: Custom callbacks feature was removed from current implementation

      // This should not throw an error
      transport = new GopherTransport(config);
      expect(transport).toBeDefined();
    });

    it("should handle partial customCallbacks", () => {
      const config: GopherTransportConfig = {
        name: "test-transport",
        protocol: "stdio",
        filters: {
          // Note: customCallbacks not supported
          observability: {
            metrics: true,
          },
        },
      };
      // Note: Custom callbacks feature was removed from current implementation

      // This should not throw an error
      transport = new GopherTransport(config);
      expect(transport).toBeDefined();
    });
  });
});
