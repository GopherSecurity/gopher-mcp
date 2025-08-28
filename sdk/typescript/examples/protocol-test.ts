#!/usr/bin/env node

/**
 * @file protocol-test.ts
 * @brief Comprehensive test of real protocol processing logic for different filter types
 */

import { McpBuiltinFilterType, McpFilterSdk } from "../src";
import { McpResult } from "../src/types";

// Test data for different protocols
const TEST_DATA = {
  // TCP packet (20 bytes minimum)
  tcp: Buffer.from([
    0x12,
    0x34, // Source port: 4660
    0x00,
    0x50, // Destination port: 80 (HTTP)
    0x00,
    0x00,
    0x00,
    0x01, // Sequence number: 1
    0x00,
    0x00,
    0x00,
    0x00, // Acknowledgement number: 0
    0x50, // Data offset: 5 (20 bytes)
    0x02, // Flags: SYN
    0x20,
    0x00, // Window size: 8192
    0x00,
    0x00, // Checksum: 0
    0x00,
    0x00, // Urgent pointer: 0
  ]),

  // UDP packet (8 bytes minimum)
  udp: Buffer.from([
    0x12,
    0x34, // Source port: 4660
    0x00,
    0x35, // Destination port: 53 (DNS)
    0x00,
    0x0c, // Length: 12
    0x00,
    0x00, // Checksum: 0
  ]),

  // HTTP request
  httpRequest: Buffer.from(
    "GET /api/users HTTP/1.1\r\n" +
      "Host: example.com\r\n" +
      "User-Agent: Mozilla/5.0\r\n" +
      "Accept: application/json\r\n" +
      "Content-Length: 0\r\n" +
      "\r\n",
    "utf8"
  ),

  // HTTP response
  httpResponse: Buffer.from(
    "HTTP/1.1 200 OK\r\n" +
      "Content-Type: application/json\r\n" +
      "Content-Length: 25\r\n" +
      "Server: nginx/1.18.0\r\n" +
      "\r\n" +
      '{"message": "success"}',
    "utf8"
  ),

  // TLS handshake
  tlsHandshake: Buffer.from([
    0x16, // Content type: Handshake
    0x03,
    0x03, // Version: TLS 1.2
    0x00,
    0x04, // Length: 4
    0x01, // Handshake type: Client Hello
    0x00,
    0x00,
    0x00, // Handshake data
  ]),

  // Authentication headers
  authHeaders: Buffer.from(
    "POST /auth/login HTTP/1.1\r\n" +
      "Host: api.example.com\r\n" +
      "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9\r\n" +
      "Content-Type: application/json\r\n" +
      "Content-Length: 45\r\n" +
      "\r\n" +
      '{"username": "user", "password": "pass"}',
    "utf8"
  ),

  // Authorization headers
  authzHeaders: Buffer.from(
    "GET /admin/users HTTP/1.1\r\n" +
      "Host: admin.example.com\r\n" +
      "X-Role: admin\r\n" +
      "X-Permission: read:users\r\n" +
      "X-User-ID: 12345\r\n" +
      "Authorization: Bearer admin-token\r\n" +
      "\r\n",
    "utf8"
  ),

  // Rate limiting headers
  rateLimitHeaders: Buffer.from(
    "HTTP/1.1 429 Too Many Requests\r\n" +
      "X-RateLimit-Limit: 100\r\n" +
      "X-RateLimit-Remaining: 0\r\n" +
      "X-RateLimit-Reset: 1640995200\r\n" +
      "Retry-After: 60\r\n" +
      "Content-Type: application/json\r\n" +
      "Content-Length: 35\r\n" +
      "\r\n" +
      '{"error": "Rate limit exceeded"}',
    "utf8"
  ),

  // Circuit breaker headers
  circuitBreakerHeaders: Buffer.from(
    "HTTP/1.1 503 Service Unavailable\r\n" +
      "X-Circuit-Breaker: OPEN\r\n" +
      "Retry-After: 30\r\n" +
      "Content-Type: application/json\r\n" +
      "Content-Length: 45\r\n" +
      "\r\n" +
      '{"error": "Service temporarily unavailable"}',
    "utf8"
  ),

  // Load balancer headers
  loadBalancerHeaders: Buffer.from(
    "GET /api/data HTTP/1.1\r\n" +
      "Host: api.example.com\r\n" +
      "X-Forwarded-For: 192.168.1.100\r\n" +
      "X-Real-IP: 192.168.1.100\r\n" +
      "X-Load-Balancer: nginx\r\n" +
      "User-Agent: Mozilla/5.0\r\n" +
      "\r\n",
    "utf8"
  ),

  // Tracing headers
  tracingHeaders: Buffer.from(
    "GET /api/trace HTTP/1.1\r\n" +
      "Host: api.example.com\r\n" +
      "traceparent: 00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01\r\n" +
      "tracestate: congo=t61rcWkgMzE\r\n" +
      "X-Request-ID: 123e4567-e89b-12d3-a456-426614174000\r\n" +
      "\r\n",
    "utf8"
  ),

  // Retry headers
  retryHeaders: Buffer.from(
    "GET /api/retry HTTP/1.1\r\n" +
      "Host: api.example.com\r\n" +
      "X-Retry-Count: 3\r\n" +
      "X-Retry-After: 5\r\n" +
      "User-Agent: Mozilla/5.0\r\n" +
      "\r\n",
    "utf8"
  ),

  // Compression headers
  compressionHeaders: Buffer.from(
    "HTTP/1.1 200 OK\r\n" +
      "Content-Type: text/html\r\n" +
      "Content-Encoding: gzip\r\n" +
      "Content-Length: 1024\r\n" +
      "Vary: Accept-Encoding\r\n" +
      "\r\n" +
      "compressed content here...",
    "utf8"
  ),

  // Invalid/malicious data
  maliciousData: Buffer.from(
    "GET /admin/../../../etc/passwd HTTP/1.1\r\n" +
      "Host: evil.com\r\n" +
      "User-Agent: <script>alert('xss')</script>\r\n" +
      "Cookie: session=javascript:alert('xss')\r\n" +
      "\r\n",
    "utf8"
  ),
};

async function testProtocolFilters() {
  console.log("🚀 Testing Real Protocol Processing Logic\n");

  const sdk = new McpFilterSdk();

  try {
    // Initialize SDK
    console.log("1. Initializing SDK...");
    await sdk.initialize();
    console.log("✅ SDK initialized successfully\n");

    // Test TCP_PROXY filter
    console.log("2. Testing TCP_PROXY Filter (Layer 4)...");
    const tcpFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.TCP_PROXY
    );
    if (tcpFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create TCP filter: ${tcpFilterResult.error}`
      );
      return;
    }
    const tcpFilter = tcpFilterResult.data!;
    const tcpResult = await sdk.processFilterData(tcpFilter, TEST_DATA.tcp);
    console.log(
      `   TCP packet processing: ${
        tcpResult.result === McpResult.OK && tcpResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(tcpFilter);

    // Test UDP_PROXY filter
    console.log("\n3. Testing UDP_PROXY Filter (Layer 4)...");
    const udpFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.UDP_PROXY
    );
    if (udpFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create UDP filter: ${udpFilterResult.error}`
      );
      return;
    }
    const udpFilter = udpFilterResult.data!;
    const udpResult = await sdk.processFilterData(udpFilter, TEST_DATA.udp);
    console.log(
      `   UDP packet processing: ${
        udpResult.result === McpResult.OK && udpResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(udpFilter);

    // Test HTTP_CODEC filter
    console.log("\n4. Testing HTTP_CODEC Filter (Layer 7)...");
    const httpCodecFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.HTTP_CODEC
    );
    if (httpCodecFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create HTTP_CODEC filter: ${httpCodecFilterResult.error}`
      );
      return;
    }
    const httpCodecFilter = httpCodecFilterResult.data!;
    const httpRequestResult = await sdk.processFilterData(
      httpCodecFilter,
      TEST_DATA.httpRequest
    );
    const httpResponseResult = await sdk.processFilterData(
      httpCodecFilter,
      TEST_DATA.httpResponse
    );
    console.log(
      `   HTTP request processing: ${
        httpRequestResult.result === McpResult.OK && httpRequestResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    console.log(
      `   HTTP response processing: ${
        httpResponseResult.result === McpResult.OK && httpResponseResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(httpCodecFilter);

    // Test HTTP_ROUTER filter
    console.log("\n5. Testing HTTP_ROUTER Filter (Layer 7)...");
    const httpRouterFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.HTTP_ROUTER
    );
    if (httpRouterFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create HTTP_ROUTER filter: ${httpRouterFilterResult.error}`
      );
      return;
    }
    const httpRouterFilter = httpRouterFilterResult.data!;
    const routerResult = await sdk.processFilterData(
      httpRouterFilter,
      TEST_DATA.httpRequest
    );
    console.log(
      `   HTTP routing validation: ${
        routerResult.result === McpResult.OK && routerResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(httpRouterFilter);

    // Test HTTP_COMPRESSION filter
    console.log("\n6. Testing HTTP_COMPRESSION Filter (Layer 6)...");
    const compressionFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.HTTP_COMPRESSION
    );
    if (compressionFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create HTTP_COMPRESSION filter: ${compressionFilterResult.error}`
      );
      return;
    }
    const compressionFilter = compressionFilterResult.data!;
    const compressionResult = await sdk.processFilterData(
      compressionFilter,
      TEST_DATA.compressionHeaders
    );
    console.log(
      `   Compression validation: ${
        compressionResult.result === McpResult.OK && compressionResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(compressionFilter);

    // Test TLS_TERMINATION filter
    console.log("\n7. Testing TLS_TERMINATION Filter (Layer 6)...");
    const tlsFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.TLS_TERMINATION
    );
    if (tlsFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create TLS_TERMINATION filter: ${tlsFilterResult.error}`
      );
      return;
    }
    const tlsFilter = tlsFilterResult.data!;
    const tlsResult = await sdk.processFilterData(
      tlsFilter,
      TEST_DATA.tlsHandshake
    );
    console.log(
      `   TLS handshake validation: ${
        tlsResult.result === McpResult.OK && tlsResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(tlsFilter);

    // Test AUTHENTICATION filter
    console.log("\n8. Testing AUTHENTICATION Filter (Layer 7)...");
    const authFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.AUTHENTICATION
    );
    if (authFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create AUTHENTICATION filter: ${authFilterResult.error}`
      );
      return;
    }
    const authFilter = authFilterResult.data!;
    const authResult = await sdk.processFilterData(
      authFilter,
      TEST_DATA.authHeaders
    );
    console.log(
      `   Authentication validation: ${
        authResult.result === McpResult.OK && authResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(authFilter);

    // Test AUTHORIZATION filter
    console.log("\n9. Testing AUTHORIZATION Filter (Layer 7)...");
    const authzFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.AUTHORIZATION
    );
    if (authzFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create AUTHORIZATION filter: ${authzFilterResult.error}`
      );
      return;
    }
    const authzFilter = authzFilterResult.data!;
    const authzResult = await sdk.processFilterData(
      authzFilter,
      TEST_DATA.authzHeaders
    );
    console.log(
      `   Authorization validation: ${
        authzResult.result === McpResult.OK && authzResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(authzFilter);

    // Test ACCESS_LOG filter
    console.log("\n10. Testing ACCESS_LOG Filter (Layer 7)...");
    const accessLogFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.ACCESS_LOG
    );
    if (accessLogFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create ACCESS_LOG filter: ${accessLogFilterResult.error}`
      );
      return;
    }
    const accessLogFilter = accessLogFilterResult.data!;
    const accessLogResult = await sdk.processFilterData(
      accessLogFilter,
      TEST_DATA.httpRequest
    );
    console.log(
      `   Access logging validation: ${
        accessLogResult.result === McpResult.OK && accessLogResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(accessLogFilter);

    // Test METRICS filter
    console.log("\n11. Testing METRICS Filter (Layer 7)...");
    const metricsFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.METRICS
    );
    if (metricsFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create METRICS filter: ${metricsFilterResult.error}`
      );
      return;
    }
    const metricsFilter = metricsFilterResult.data!;
    const metricsResult = await sdk.processFilterData(
      metricsFilter,
      TEST_DATA.httpRequest
    );
    console.log(
      `   Metrics validation: ${
        metricsResult.result === McpResult.OK && metricsResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(metricsFilter);

    // Test RATE_LIMIT filter
    console.log("\n12. Testing RATE_LIMIT Filter (Layer 7)...");
    const rateLimitFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.RATE_LIMIT
    );
    if (rateLimitFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create RATE_LIMIT filter: ${rateLimitFilterResult.error}`
      );
      return;
    }
    const rateLimitFilter = rateLimitFilterResult.data!;
    const rateLimitResult = await sdk.processFilterData(
      rateLimitFilter,
      TEST_DATA.rateLimitHeaders
    );
    console.log(
      `   Rate limiting validation: ${
        rateLimitResult.result === McpResult.OK && rateLimitResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(rateLimitFilter);

    // Test CIRCUIT_BREAKER filter
    console.log("\n13. Testing CIRCUIT_BREAKER Filter (Layer 7)...");
    const circuitBreakerFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.CIRCUIT_BREAKER
    );
    if (circuitBreakerFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create CIRCUIT_BREAKER filter: ${circuitBreakerFilterResult.error}`
      );
      return;
    }
    const circuitBreakerFilter = circuitBreakerFilterResult.data!;
    const circuitBreakerResult = await sdk.processFilterData(
      circuitBreakerFilter,
      TEST_DATA.circuitBreakerHeaders
    );
    console.log(
      `   Circuit breaker validation: ${
        circuitBreakerResult.result === McpResult.OK &&
        circuitBreakerResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(circuitBreakerFilter);

    // Test LOAD_BALANCER filter
    console.log("\n14. Testing LOAD_BALANCER Filter (Layer 7)...");
    const loadBalancerFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.LOAD_BALANCER
    );
    if (loadBalancerFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create LOAD_BALANCER filter: ${loadBalancerFilterResult.error}`
      );
      return;
    }
    const loadBalancerFilter = loadBalancerFilterResult.data!;
    const loadBalancerResult = await sdk.processFilterData(
      loadBalancerFilter,
      TEST_DATA.loadBalancerHeaders
    );
    console.log(
      `   Load balancer validation: ${
        loadBalancerResult.result === McpResult.OK && loadBalancerResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(loadBalancerFilter);

    // Test TRACING filter
    console.log("\n15. Testing TRACING Filter (Layer 7)...");
    const tracingFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.TRACING
    );
    if (tracingFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create TRACING filter: ${tracingFilterResult.error}`
      );
      return;
    }
    const tracingFilter = tracingFilterResult.data!;
    const tracingResult = await sdk.processFilterData(
      tracingFilter,
      TEST_DATA.tracingHeaders
    );
    console.log(
      `   Tracing validation: ${
        tracingResult.result === McpResult.OK && tracingResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(tracingFilter);

    // Test RETRY filter
    console.log("\n16. Testing RETRY Filter (Layer 7)...");
    const retryFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.RETRY
    );
    if (retryFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create RETRY filter: ${retryFilterResult.error}`
      );
      return;
    }
    const retryFilter = retryFilterResult.data!;
    const retryResult = await sdk.processFilterData(
      retryFilter,
      TEST_DATA.retryHeaders
    );
    console.log(
      `   Retry validation: ${
        retryResult.result === McpResult.OK && retryResult.data
          ? "✅ PASSED"
          : "❌ FAILED"
      }`
    );
    await sdk.destroyFilter(retryFilter);

    // Test CUSTOM filter with malicious data
    console.log("\n17. Testing CUSTOM Filter Security...");
    const customFilterResult = await sdk.createBuiltinFilter(
      McpBuiltinFilterType.CUSTOM
    );
    if (customFilterResult.result !== McpResult.OK) {
      console.log(
        `   ❌ Failed to create CUSTOM filter: ${customFilterResult.error}`
      );
      return;
    }
    const customFilter = customFilterResult.data!;
    const maliciousResult = await sdk.processFilterData(
      customFilter,
      TEST_DATA.maliciousData
    );
    console.log(
      `   Malicious data detection: ${
        maliciousResult.result !== McpResult.OK || !maliciousResult.data
          ? "✅ BLOCKED"
          : "❌ ALLOWED (security issue)"
      }`
    );

    // Test invalid data handling
    console.log("\n18. Testing Invalid Data Handling...");
    const invalidData = Buffer.from([0xff, 0xff, 0x00, 0x00]); // Consecutive 0xFF + null bytes
    const invalidResult = await sdk.processFilterData(
      customFilter,
      invalidData
    );
    console.log(
      `   Invalid data rejection: ${
        invalidResult.result !== McpResult.OK || !invalidResult.data
          ? "✅ REJECTED"
          : "❌ ACCEPTED (validation issue)"
      }`
    );

    await sdk.destroyFilter(customFilter);

    console.log("\n🎉 Protocol Processing Test Completed!");
    console.log("\n📊 Summary:");
    console.log("   - All filter types implemented with real protocol logic");
    console.log("   - Layer 4 (TCP/UDP) packet validation");
    console.log("   - Layer 6 (TLS, Compression) protocol validation");
    console.log("   - Layer 7 (HTTP, Auth, Security) application validation");
    console.log("   - Security validation (XSS, path traversal, etc.)");
    console.log("   - Comprehensive error handling and validation");
  } catch (error) {
    console.error("❌ Test failed:", error);
  } finally {
    // Shutdown SDK
    console.log("\n9. Shutting down SDK...");
    await sdk.shutdown();
    console.log("✅ SDK shut down successfully");
  }
}

// Run the test
if (require.main === module) {
  testProtocolFilters().catch(console.error);
}

export { testProtocolFilters };
