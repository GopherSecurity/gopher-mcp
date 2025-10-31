#!/usr/bin/env node
/**
 * @file calculator-server-hybrid.ts
 * @brief MCP Calculator Server using Hybrid SDK + Gopher Filters
 *
 * This example demonstrates a calculator server using the official MCP SDK for
 * protocol handling (HTTP/SSE/JSON-RPC) combined with Gopher-MCP C++ filters
 * for enterprise features (rate limiting, circuit breaker, metrics).
 *
 * Hybrid SDK + Filters:
 * - Protocol: Official @modelcontextprotocol/sdk
 * - Filters: C++ implementation via wrapper
 * - Deployment: Drop-in enhancement to SDK
 * - Flexibility: Best of both worlds
 */

import { Server } from "../../../sdk/typescript/node_modules/@modelcontextprotocol/sdk/dist/esm/server/index.js";
import { StreamableHTTPServerTransport } from "../../../sdk/typescript/node_modules/@modelcontextprotocol/sdk/dist/esm/server/streamableHttp.js";
import { ListToolsRequestSchema, CallToolRequestSchema } from "../../../sdk/typescript/node_modules/@modelcontextprotocol/sdk/dist/esm/types.js";
import { GopherFilteredTransport } from "../../../sdk/typescript/src/gopher-filtered-transport.js";
import { createHybridDispatcher, destroyHybridDispatcher } from "../../../sdk/typescript/src/filter-dispatcher.js";
import type { CanonicalConfig } from "../../../sdk/typescript/src/filter-types.js";
import {
  registerMetricsCallbacks,
  unregisterMetricsCallbacks,
  type MetricsCallbacks,
  type MetricsSnapshot,
  type MetricsThresholdEvent
} from "../../../sdk/typescript/src/index.js";
import * as fs from "fs";
import * as path from "path";
import { fileURLToPath } from "url";
import * as http from "node:http";
import { randomUUID } from "node:crypto";

// Get directory name in ES modules
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Configuration
const PORT = process.env['PORT'] ? parseInt(process.env['PORT']) : 8080;
const HOST = process.env['HOST'] || "127.0.0.1";
const MCP_ENDPOINT = "/mcp";

// Load filter configuration from JSON file
const configPath = path.join(__dirname, "config-hybrid.json");
const filterConfig: CanonicalConfig = JSON.parse(fs.readFileSync(configPath, "utf-8"));

type ServerMode = "stateless" | "stateful";

function parseServerMode(argv: string[]): ServerMode {
  let mode: ServerMode = "stateless";
  for (const arg of argv) {
    if (arg === "--stateful" || arg === "--mode=stateful") {
      mode = "stateful";
    } else if (arg === "--stateless" || arg === "--mode=stateless") {
      mode = "stateless";
    }
  }
  return mode;
}

const SERVER_MODE: ServerMode = parseServerMode(process.argv.slice(2));
const IS_STATEFUL = SERVER_MODE === "stateful";

// Global state (shared across connections)
let globalDispatcher: number | null = null;
let serverInstance: Server | null = null;
let sdkTransport: StreamableHTTPServerTransport | null = null;
let filteredTransport: GopherFilteredTransport | null = null;
let httpServer: http.Server | null = null;
let metricsCallbackHandle: any = null;

/**
 * Format bytes for human-readable display
 */
function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes}B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(2)}KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)}MB`;
}

/**
 * Handle incoming HTTP requests
 */
async function handleRequest(req: http.IncomingMessage, res: http.ServerResponse) {
  const hostHeader = req.headers.host || `${HOST}:${PORT}`;
  const url = new URL(req.url || "/", `http://${hostHeader}`);

  console.log(`\n🌐 [HTTP] ${req.method} ${url.pathname} from ${req.socket.remoteAddress}`);
  console.log(`📋 [HTTP] Accept: ${req.headers.accept}`);
  console.log(`📋 [HTTP] Content-Type: ${req.headers['content-type']}`);

  if (url.pathname === "/health") {
    console.log(`✅ [HTTP] Health check - returning OK`);
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ status: "ok" }));
    return;
  }

  // Only handle MCP endpoint
  if (!url.pathname.startsWith(MCP_ENDPOINT)) {
    console.log(`❌ [HTTP] 404 - Invalid endpoint: ${url.pathname}`);
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("Not Found\n\nAvailable endpoints:\n  • POST /mcp\n  • GET /health");
    return;
  }

  console.log(`✅ [HTTP] Valid MCP endpoint request (mode=${SERVER_MODE})`);

  // Stateless mode disables standalone SSE streams to avoid shared session state conflicts.
  if (!IS_STATEFUL && req.method === "GET") {
    console.log(`❌ [HTTP] 405 - SSE stream not supported for ${url.pathname}`);
    res.writeHead(405, { "Content-Type": "application/json" });
    res.end(JSON.stringify({
      jsonrpc: "2.0",
      error: {
        code: -32000,
        message: "Method Not Allowed: Server does not expose a standalone SSE stream in stateless mode (use --stateful to enable)"
      },
      id: null
    }));
    return;
  }

  if (!filteredTransport) {
    console.log(`❌ [HTTP] 503 - Filtered transport not initialized`);
    res.writeHead(503, { "Content-Type": "text/plain" });
    res.end("Service Unavailable\n");
    return;
  }

  try {
    console.log(`📨 [SDK] Delegating to filtered transport.handleRequest()...`);
    await (filteredTransport as any).handleRequest(req, res);
    console.log(`✅ [HTTP] Request handling completed`);
  } catch (error) {
    console.error("❌ [HTTP] Error handling request:", error);
    if (!res.headersSent) {
      res.writeHead(500, { "Content-Type": "text/plain" });
      res.end("Internal Server Error");
    }
  }
}

// Calculator state management
class CalculatorState {
    private memory: number = 0;
    private history: Array<{
        id: string;
        operation: string;
        result: number;
        timestamp: Date;
    }> = [];
    private nextId: number = 1;

    storeMemory(value: number): void {
        this.memory = value;
    }

    recallMemory(): number {
        return this.memory;
    }

    clearMemory(): void {
        this.memory = 0;
    }

    addToHistory(operation: string, result: number): string {
        const id = `calc_${this.nextId++}`;
        this.history.push({
            id,
            operation,
            result,
            timestamp: new Date()
        });

        // Keep only last 1000 operations
        if (this.history.length > 1000) {
            this.history = this.history.slice(-1000);
        }

        return id;
    }

    getHistory(limit: number = 10): typeof this.history {
        return this.history.slice(-limit);
    }

    clearHistory(): number {
        const count = this.history.length;
        this.history = [];
        return count;
    }

    getStatistics(): any {
        const operations = this.history.reduce((acc, h) => {
            const op = h.operation.split(' ')[1]; // Extract operator
            acc[op] = (acc[op] || 0) + 1;
            return acc;
        }, {} as Record<string, number>);

        return {
            totalCalculations: this.history.length,
            memoryValue: this.memory,
            operationCounts: operations,
            oldestCalculation: this.history[0]?.timestamp,
            newestCalculation: this.history[this.history.length - 1]?.timestamp
        };
    }
}

// Create and configure server
async function createCalculatorServer() {
    console.log('🚀 Starting Calculator Server (Scenario 2: Hybrid SDK + Filters)');
    console.log('━'.repeat(60));
    console.log(`🔧 Server mode: ${SERVER_MODE === "stateful" ? "Stateful (session-managed, SSE enabled)" : "Stateless (JSON responses, SSE disabled)"}`);

    // Create MCP server using official SDK
    const server = new Server({
        name: 'calculator-server-hybrid',
        version: '1.0.0'
    }, {
        capabilities: {
            tools: {}
        }
    });

    // Create calculator state
    const calculatorState = new CalculatorState();

    // Register tools/list handler
    server.setRequestHandler(ListToolsRequestSchema, async () => ({
            tools: [
                {
                    name: 'calculate',
                    description: 'Perform arithmetic calculations',
                    inputSchema: {
                        type: 'object',
                        properties: {
                            operation: {
                                type: 'string',
                                enum: ['add', 'subtract', 'multiply', 'divide', 'power', 'sqrt', 'factorial'],
                                description: 'The arithmetic operation to perform'
                            },
                            a: {
                                type: 'number',
                                description: 'First operand'
                            },
                            b: {
                                type: 'number',
                                description: 'Second operand (not needed for sqrt, factorial)'
                            },
                            precision: {
                                type: 'number',
                                default: 2,
                                description: 'Decimal precision'
                            }
                        },
                        required: ['operation', 'a']
                    }
                },
                {
                    name: 'memory',
                    description: 'Manage calculator memory',
                    inputSchema: {
                        type: 'object',
                        properties: {
                            action: {
                                type: 'string',
                                enum: ['store', 'recall', 'clear'],
                                description: 'Memory action'
                            },
                            value: {
                                type: 'number',
                                description: 'Value to store'
                            }
                        },
                        required: ['action']
                    }
                },
                {
                    name: 'history',
                    description: 'View calculation history',
                    inputSchema: {
                        type: 'object',
                        properties: {
                            action: {
                                type: 'string',
                                enum: ['list', 'clear', 'stats'],
                                description: 'History action'
                            },
                            limit: {
                                type: 'number',
                                default: 10,
                                description: 'Number of entries to show'
                            }
                        },
                        required: ['action']
                    }
                }
            ]
    }));

    // Register tools/call handler
    server.setRequestHandler(CallToolRequestSchema, async (request) => {
            const { name, arguments: args } = request.params;

            console.log(`🔧 Tool call: ${name}`, args);

            if (name === 'calculate') {
                const { operation, a, b, precision = 2 } = args;

                let result: number;
                let expression: string;

                try {
                    switch (operation) {
                        case 'add':
                            if (b === undefined) throw new Error('Second operand required');
                            result = a + b;
                            expression = `${a} + ${b}`;
                            break;

                        case 'subtract':
                            if (b === undefined) throw new Error('Second operand required');
                            result = a - b;
                            expression = `${a} - ${b}`;
                            break;

                        case 'multiply':
                            if (b === undefined) throw new Error('Second operand required');
                            result = a * b;
                            expression = `${a} × ${b}`;
                            break;

                        case 'divide':
                            if (b === undefined) throw new Error('Second operand required');
                            if (b === 0) throw new Error('Division by zero');
                            result = a / b;
                            expression = `${a} ÷ ${b}`;
                            break;

                        case 'power':
                            if (b === undefined) throw new Error('Second operand required');
                            result = Math.pow(a, b);
                            expression = `${a}^${b}`;
                            break;

                        case 'sqrt':
                            if (a < 0) throw new Error('Cannot calculate square root of negative number');
                            result = Math.sqrt(a);
                            expression = `√${a}`;
                            break;

                        case 'factorial':
                            if (a < 0) throw new Error('Cannot calculate factorial of negative number');
                            if (!Number.isInteger(a)) throw new Error('Factorial requires integer');
                            if (a > 170) throw new Error('Factorial too large (max 170)');
                            result = 1;
                            for (let i = 2; i <= a; i++) result *= i;
                            expression = `${a}!`;
                            break;

                        default:
                            throw new Error(`Unknown operation: ${operation}`);
                    }

                    const rounded = Math.round(result * Math.pow(10, precision)) / Math.pow(10, precision);
                    const historyId = calculatorState.addToHistory(expression, rounded);

                    return {
                        content: [{
                            type: 'text',
                            text: `${expression} = ${rounded}`
                        }]
                    };
                } catch (error) {
                    return {
                        content: [{
                            type: 'text',
                            text: `Error: ${error instanceof Error ? error.message : String(error)}`
                        }],
                        isError: true
                    };
                }
            } else if (name === 'memory') {
                const { action, value } = args;

                let message: string;
                let memoryValue: number;

                switch (action) {
                    case 'store':
                        if (value === undefined) throw new Error('Value required');
                        calculatorState.storeMemory(value);
                        message = `Stored ${value} in memory`;
                        memoryValue = value;
                        break;

                    case 'recall':
                        memoryValue = calculatorState.recallMemory();
                        message = `Memory value: ${memoryValue}`;
                        break;

                    case 'clear':
                        calculatorState.clearMemory();
                        memoryValue = 0;
                        message = 'Memory cleared';
                        break;

                    default:
                        throw new Error(`Unknown memory action: ${action}`);
                }

                return {
                    content: [{ type: 'text', text: message }]
                };
            } else if (name === 'history') {
                const { action, limit = 10 } = args;

                switch (action) {
                    case 'list':
                        const history = calculatorState.getHistory(limit);
                        const entries = history.map(h =>
                            `• ${h.operation} = ${h.result} (${new Date(h.timestamp).toLocaleTimeString()})`
                        ).join('\n');
                        return {
                            content: [{
                                type: 'text',
                                text: `Showing ${history.length} calculation(s):\n${entries}`
                            }]
                        };

                    case 'clear':
                        const cleared = calculatorState.clearHistory();
                        return {
                            content: [{
                                type: 'text',
                                text: `Cleared ${cleared} calculation(s) from history`
                            }]
                        };

                    case 'stats':
                        const stats = calculatorState.getStatistics();
                        return {
                            content: [{
                                type: 'text',
                                text: `📊 Calculator Statistics:
• Total calculations: ${stats.totalCalculations}
• Memory value: ${stats.memoryValue}
• Operations: ${Object.entries(stats.operationCounts).map(([op, count]) => `${op}: ${count}`).join(', ')}`
                            }]
                        };

                    default:
                        throw new Error(`Unknown history action: ${action}`);
                }
            }

            throw new Error(`Unknown tool: ${name}`);
    });

    // Create dispatcher for filter chain
    console.log('📡 Creating dispatcher for filter chain...');
    globalDispatcher = createHybridDispatcher();
    console.log('✅ Dispatcher created');

    // Display filter configuration
    console.log('\n📋 Loaded canonical filter configuration:');
    filterConfig.listeners.forEach(listener => {
        listener.filter_chains.forEach(chain => {
            console.log(`   Chain: ${chain.name || listener.name}`);
            chain.filters.forEach(f => {
                console.log(`     - ${f.name || f.type} (${f.type})`);
            });
        });
    });

    // Initialize MCP server and transport stack
    serverInstance = server;
    const transportOptions = IS_STATEFUL
        ? {
            sessionIdGenerator: () => randomUUID(),
        }
        : {
            // Stateless mode allows multiple clients without session conflicts.
            sessionIdGenerator: undefined,
            enableJsonResponse: true,
        };
    sdkTransport = new StreamableHTTPServerTransport(transportOptions);
    filteredTransport = new GopherFilteredTransport(sdkTransport, {
        dispatcherHandle: globalDispatcher,
        filterConfig: filterConfig,
        debugLogging: process.env['DEBUG'] === '1',
    });

    console.log('\n🔌 Connecting MCP server to filtered transport...');
    await server.connect(filteredTransport);
    console.log('✅ Server connected');

    // Register metrics callbacks
    console.log('\n📊 Registering metrics callbacks...');
    try {
        const callbacks: MetricsCallbacks = {
            onMetricsUpdate: (snapshot: MetricsSnapshot) => {
                const avgLatency = snapshot.latencySamples > 0n
                    ? Number(snapshot.totalLatencyMs) / Number(snapshot.latencySamples)
                    : 0;
                const timestamp = new Date().toISOString();

                console.log(`\n📊 [Metrics] Requests: ${snapshot.requestsReceived}, Avg Latency: ${avgLatency.toFixed(2)}ms`);
                console.log(`   Bytes RX: ${formatBytes(Number(snapshot.bytesReceived))}, TX: ${formatBytes(Number(snapshot.bytesSent))}`);
                console.log(`   Errors: ${snapshot.errorsReceived}, Protocol Errors: ${snapshot.protocolErrors}`);
            },
            onThresholdExceeded: (event: MetricsThresholdEvent) => {
                console.log(`\n⚠️ [Metrics Alert] ${event.metric} exceeded threshold!`);
                console.log(`   Value: ${event.value}, Threshold: ${event.threshold}`);
            },
            onError: (error: Error) => {
                console.error('❌ [Metrics] Callback error:', error);
            }
        };

        metricsCallbackHandle = registerMetricsCallbacks(
            filteredTransport.getChainHandle(),
            callbacks
        );

        if (metricsCallbackHandle) {
            console.log('✅ Metrics callbacks registered');
        } else {
            console.log('⚠️  No metrics filter in chain');
        }
    } catch (error) {
        console.error('❌ Failed to register metrics callbacks:', error);
    }

    // Create HTTP server
    const httpSrv = http.createServer((req, res) => {
        handleRequest(req, res).catch((error) => {
            console.error("❌ Unhandled request error:", error);
            if (!res.headersSent) {
                res.writeHead(500, { "Content-Type": "text/plain" });
                res.end("Internal Server Error");
            }
        });
    });
    httpServer = httpSrv;

    // Start listening
    await new Promise<void>((resolve) => {
        httpSrv.listen(PORT, HOST, () => {
            console.log('\n━'.repeat(60));
            console.log('✅ MCP Calculator Server is running');
            console.log('');
            console.log('🏗️  Architecture:');
            console.log('  • Protocol: Official MCP SDK');
            console.log(`  • Transport: StreamableHTTPServerTransport (${IS_STATEFUL ? 'HTTP/SSE (stateful sessions)' : 'HTTP (stateless JSON responses)'})`);
            console.log('  • Filters: Gopher-MCP C++ via wrapper');
            console.log('');
            console.log('📚 Available Tools:');
            console.log('  • calculate - Arithmetic operations (add, subtract, multiply, divide, power, sqrt, factorial)');
            console.log('  • memory - Memory management (store, recall, clear)');
            console.log('  • history - Calculation history (list, clear, stats)');
            console.log('');
            console.log('🛡️  Active Filters:');
            console.log('  • Request Logger - Prints JSON-RPC traffic');
            console.log('');
            console.log(`🌐 Server Address: http://${HOST}:${PORT}${MCP_ENDPOINT}`);
            console.log('');
            console.log('📝 Test with curl:');
            console.log(`   curl -X POST http://${HOST}:${PORT}${MCP_ENDPOINT} \\`);
            console.log(`     -H "Content-Type: application/json" \\`);
            console.log(`     -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'`);
            console.log('━'.repeat(60));
            console.log('\n🎯 Server ready and waiting for connections...\n');
            resolve();
        });
    });

    // Graceful shutdown
    const shutdown = async (signal: string) => {
        console.log(`\n\n🛑 Received ${signal}, shutting down server...`);

        // Close HTTP server
        const activeServer = httpServer;
        if (activeServer) {
            await new Promise<void>((resolve) => {
                activeServer.close(() => {
                    console.log('✅ HTTP server closed');
                    resolve();
                });
            });
            httpServer = null;
        }

        // Unregister metrics callbacks
        if (metricsCallbackHandle && filteredTransport) {
            try {
                unregisterMetricsCallbacks(
                    filteredTransport.getChainHandle(),
                    metricsCallbackHandle
                );
                console.log('✅ Metrics callbacks unregistered');
            } catch (error) {
                console.error('⚠️  Error unregistering metrics callbacks:', error);
            }
            metricsCallbackHandle = null;
        }

        // Close filtered transport
        if (filteredTransport) {
            try {
                await filteredTransport.close();
                console.log('✅ Filtered transport closed');
            } catch (error) {
                console.error('⚠️  Error while closing transport:', error);
            }
            filteredTransport = null;
            sdkTransport = null;
        }

        serverInstance = null;

        // Destroy dispatcher
        if (globalDispatcher !== null) {
            destroyHybridDispatcher(globalDispatcher);
            console.log('✅ Dispatcher destroyed');
            globalDispatcher = null;
        }

        console.log('✅ Server shutdown complete');
        process.exit(0);
    };

    process.on('SIGINT', () => shutdown('SIGINT'));
    process.on('SIGTERM', () => shutdown('SIGTERM'));
}

// Start server
createCalculatorServer().catch(error => {
    console.error('❌ Failed to start server:', error);
    if (globalDispatcher !== null) {
        destroyHybridDispatcher(globalDispatcher);
    }
    process.exit(1);
});
