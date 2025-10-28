#!/usr/bin/env node
/**
 * @file calculator-client-hybrid.ts
 * @brief Simple MCP Calculator Client using Standard SDK (No Filters)
 *
 * This is a basic MCP client that connects to the calculator server
 * using only the standard @modelcontextprotocol/sdk without any filters.
 *
 * Architecture: Pure Standard SDK
 * - Protocol: Official @modelcontextprotocol/sdk
 * - Transport: StreamableHTTPClientTransport (HTTP+SSE)
 * - Filters: None (removed for simplicity)
 * - Approach: Clean, minimal MCP client implementation
 *
 * Reference: Based on test_protocol_detection.sh approach
 */

import { Client } from "../../../sdk/typescript/node_modules/@modelcontextprotocol/sdk/dist/esm/client/index.js";
import { StreamableHTTPClientTransport } from "../../../sdk/typescript/node_modules/@modelcontextprotocol/sdk/dist/esm/client/streamableHttp.js";
import * as readline from 'readline';

// Server configuration
const DEFAULT_SERVER_URL = "http://127.0.0.1:8080/mcp";

/**
 * Simple calculator CLI without filters
 */
class SimpleCalculatorCLI {
    private client: Client;
    private transport: StreamableHTTPClientTransport | null = null;
    private rl: readline.Interface;
    private connected: boolean = false;
    private serverInfo: any = {};

    constructor() {
        // Create MCP client with basic configuration
        this.client = new Client({
            name: 'calculator-client-simple',
            version: '1.0.0'
        }, {
            capabilities: {}
        });

        // Setup readline interface
        this.rl = readline.createInterface({
            input: process.stdin,
            output: process.stdout,
            prompt: 'calc> '
        });
    }

    /**
     * Connect to the calculator server
     */
    async connect(serverUrl: string = DEFAULT_SERVER_URL): Promise<void> {
        console.log(`\n🔌 Connecting to calculator server...`);
        console.log(`📍 Server URL: ${serverUrl}`);

        try {
            // Create simple HTTP+SSE transport (no filters)
            const url = new URL(serverUrl);
            this.transport = new StreamableHTTPClientTransport(url);

            console.log('📡 Using HTTP+SSE transport (standard SDK)');

            // Connect client to server
            await this.client.connect(this.transport);
            this.connected = true;

            console.log('✅ Connection established');

            // Initialize the MCP session
            console.log('🔄 Initializing MCP session...');

            // The SDK handles initialization automatically, but we can get server info
            // by calling tools/list
            console.log('✅ Session initialized');

            // List available tools
            console.log('\n📚 Discovering available tools...');
            const toolsResult = await this.client.listTools();

            console.log(`\n✅ Connected successfully!`);
            console.log(`📦 Available tools: ${toolsResult.tools.length}`);
            toolsResult.tools.forEach(tool => {
                console.log(`   • ${tool.name} - ${tool.description}`);
            });

        } catch (error) {
            console.error(`\n❌ Connection failed: ${error instanceof Error ? error.message : String(error)}`);
            throw error;
        }
    }

    /**
     * Run interactive CLI
     */
    async runInteractive(): Promise<void> {
        if (!this.connected) {
            throw new Error('Not connected to server. Call connect() first.');
        }

        console.log('\n━'.repeat(60));
        console.log('🧮 Simple Calculator Client - Interactive Mode');
        console.log('━'.repeat(60));
        console.log('\nCommands:');
        console.log('  calc <operation> <a> [b]  - Perform calculation');
        console.log('  memory <action> [value]   - Memory operations (store, recall, clear)');
        console.log('  history [limit]           - Show calculation history');
        console.log('  stats                     - Show statistics');
        console.log('  help                      - Show this help');
        console.log('  quit                      - Exit');
        console.log('\nOperations: add, subtract, multiply, divide, power, sqrt, factorial');
        console.log('━'.repeat(60));
        console.log('');

        this.rl.prompt();

        this.rl.on('line', async (line) => {
            const trimmed = line.trim();
            if (!trimmed) {
                this.rl.prompt();
                return;
            }

            const args = trimmed.split(/\s+/);
            const command = args[0];

            try {
                switch (command) {
                    case 'calc':
                        await this.handleCalculate(args.slice(1));
                        break;

                    case 'memory':
                        await this.handleMemory(args.slice(1));
                        break;

                    case 'history':
                        await this.handleHistory(args.slice(1));
                        break;

                    case 'stats':
                        await this.handleStats();
                        break;

                    case 'help':
                        this.showHelp();
                        break;

                    case 'quit':
                    case 'exit':
                        await this.shutdown();
                        return;

                    default:
                        if (command) {
                            console.log(`❓ Unknown command: ${command}. Type 'help' for available commands.`);
                        }
                }
            } catch (error) {
                console.error(`❌ Error: ${error instanceof Error ? error.message : String(error)}`);
            }

            this.rl.prompt();
        });
    }

    /**
     * Handle calculate command
     */
    private async handleCalculate(args: string[]): Promise<void> {
        if (args.length < 2) {
            console.log('Usage: calc <operation> <a> [b]');
            console.log('Example: calc add 5 3');
            console.log('         calc sqrt 16');
            return;
        }

        const [operation, aStr, bStr] = args;
        const a = parseFloat(aStr);
        const b = bStr ? parseFloat(bStr) : undefined;

        if (isNaN(a)) {
            console.log(`❌ Invalid number: ${aStr}`);
            return;
        }

        if (bStr && b !== undefined && isNaN(b)) {
            console.log(`❌ Invalid number: ${bStr}`);
            return;
        }

        const startTime = Date.now();

        try {
            // Use the callTool method from MCP SDK
            const result = await this.client.callTool({
                name: 'calculate',
                arguments: {
                    operation,
                    a,
                    b,
                    precision: 2
                }
            });

            const latency = Date.now() - startTime;

            if (result.content && Array.isArray(result.content) && result.content.length > 0) {
                const content = result.content[0];
                if (content.type === 'text') {
                    console.log(`\n📊 Result: ${content.text}`);
                }
            }

            console.log(`⏱️  Response time: ${latency}ms`);

        } catch (error) {
            console.error(`❌ Calculation error: ${error instanceof Error ? error.message : String(error)}`);
        }
    }

    /**
     * Handle memory command
     */
    private async handleMemory(args: string[]): Promise<void> {
        if (args.length < 1) {
            console.log('Usage: memory <action> [value]');
            console.log('Actions: store, recall, clear');
            console.log('Example: memory store 42');
            console.log('         memory recall');
            return;
        }

        const [action, valueStr] = args;
        const value = valueStr ? parseFloat(valueStr) : undefined;

        try {
            // Use the callTool method from MCP SDK
            const result = await this.client.callTool({
                name: 'memory',
                arguments: {
                    action,
                    value
                }
            });

            if (result.content && Array.isArray(result.content) && result.content.length > 0) {
                const content = result.content[0];
                if (content.type === 'text') {
                    console.log(`\n💾 ${content.text}`);
                }
            }

        } catch (error) {
            console.error(`❌ Memory error: ${error instanceof Error ? error.message : String(error)}`);
        }
    }

    /**
     * Handle history command
     */
    private async handleHistory(args: string[]): Promise<void> {
        const limit = args[0] ? parseInt(args[0]) : 10;

        if (isNaN(limit) || limit < 1) {
            console.log('❌ Invalid limit. Please provide a positive number.');
            return;
        }

        try {
            // Use the callTool method from MCP SDK
            const result = await this.client.callTool({
                name: 'history',
                arguments: {
                    action: 'list',
                    limit
                }
            });

            if (result.content && Array.isArray(result.content) && result.content.length > 0) {
                const content = result.content[0];
                if (content.type === 'text') {
                    console.log('\n📜 Calculation History:');
                    console.log(content.text);
                }
            }

        } catch (error) {
            console.error(`❌ History error: ${error instanceof Error ? error.message : String(error)}`);
        }
    }

    /**
     * Handle stats command
     */
    private async handleStats(): Promise<void> {
        try {
            // Use the callTool method from MCP SDK
            const result = await this.client.callTool({
                name: 'history',
                arguments: {
                    action: 'stats'
                }
            });

            if (result.content && Array.isArray(result.content) && result.content.length > 0) {
                const content = result.content[0];
                if (content.type === 'text') {
                    console.log('\n📊 Calculator Statistics:');
                    console.log(content.text);
                }
            }

        } catch (error) {
            console.error(`❌ Stats error: ${error instanceof Error ? error.message : String(error)}`);
        }
    }

    /**
     * Show help
     */
    private showHelp(): void {
        console.log('\n📖 Calculator Client Commands:');
        console.log('\nCalculator Operations:');
        console.log('  calc add <a> <b>       - Add two numbers');
        console.log('  calc subtract <a> <b>  - Subtract b from a');
        console.log('  calc multiply <a> <b>  - Multiply two numbers');
        console.log('  calc divide <a> <b>    - Divide a by b');
        console.log('  calc power <a> <b>     - Raise a to power b');
        console.log('  calc sqrt <a>          - Square root of a');
        console.log('  calc factorial <a>     - Factorial of a');
        console.log('\nMemory Operations:');
        console.log('  memory store <value>   - Store value in memory');
        console.log('  memory recall          - Recall value from memory');
        console.log('  memory clear           - Clear memory');
        console.log('\nHistory:');
        console.log('  history [limit]        - Show calculation history (default: 10)');
        console.log('  stats                  - Show calculator statistics');
        console.log('\nGeneral:');
        console.log('  help                   - Show this help message');
        console.log('  quit                   - Exit the calculator');
        console.log('');
    }

    /**
     * Shutdown and cleanup
     */
    async shutdown(): Promise<void> {
        console.log('\n👋 Shutting down calculator client...');

        if (this.connected && this.client) {
            try {
                await this.client.close();
                console.log('✅ Disconnected from server');
            } catch (error) {
                console.error('⚠️  Error during disconnect:', error);
            }
        }

        this.rl.close();
        console.log('✅ Calculator client closed');
        process.exit(0);
    }
}

/**
 * Main function
 */
async function main() {
    console.log('━'.repeat(60));
    console.log('🧮 MCP Calculator Client (Simple - No Filters)');
    console.log('━'.repeat(60));
    console.log('Architecture: Pure Standard SDK');
    console.log('  • Protocol: @modelcontextprotocol/sdk');
    console.log('  • Transport: StreamableHTTPClientTransport (HTTP+SSE)');
    console.log('  • Filters: None');
    console.log('━'.repeat(60));

    // Get server URL from command line or use default
    const serverUrl = process.argv[2] || DEFAULT_SERVER_URL;

    const cli = new SimpleCalculatorCLI();

    try {
        await cli.connect(serverUrl);
        await cli.runInteractive();
    } catch (error) {
        console.error('\n❌ Fatal error:', error);
        process.exit(1);
    }
}

/**
 * Handle process signals for graceful shutdown
 */
process.on('SIGINT', () => {
    console.log('\n\n📛 Received SIGINT (Ctrl+C)');
    process.exit(0);
});

process.on('SIGTERM', () => {
    console.log('\n\n📛 Received SIGTERM');
    process.exit(0);
});

// Catch unhandled errors
process.on('unhandledRejection', (reason, promise) => {
    console.error('❌ Unhandled Rejection at:', promise, 'reason:', reason);
    process.exit(1);
});

// Start the client
main().catch((error) => {
    console.error('❌ Failed to start client:', error);
    process.exit(1);
});
