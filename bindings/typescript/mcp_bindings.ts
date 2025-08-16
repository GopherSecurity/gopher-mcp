/**
 * TypeScript/Node.js bindings for MCP C++ SDK
 * 
 * This demonstrates how to create Node.js bindings using N-API/node-addon-api.
 * The C API provides a stable ABI that can be wrapped for JavaScript/TypeScript.
 * 
 * Similar patterns can be used for:
 * - Deno (via FFI)
 * - Bun (via FFI)
 * - React Native (via JSI)
 */

import { promisify } from 'util';

// For actual implementation, you would use node-addon-api or node-ffi-napi
// This example shows the TypeScript interface design

/**
 * MCP Result codes
 */
export enum MCPResult {
    OK = 0,
    ERROR = -1,
    ERROR_INVALID_ARGUMENT = -2,
    ERROR_OUT_OF_MEMORY = -3,
    ERROR_NOT_CONNECTED = -4,
    ERROR_TIMEOUT = -5,
    ERROR_CANCELLED = -6,
    ERROR_NOT_FOUND = -7,
    ERROR_ALREADY_EXISTS = -8,
    ERROR_PERMISSION_DENIED = -9,
    ERROR_RESOURCE_EXHAUSTED = -10,
    ERROR_INVALID_STATE = -11,
    ERROR_PROTOCOL = -12,
    ERROR_NOT_IMPLEMENTED = -13,
    ERROR_IO = -14,
    ERROR_SSL = -15
}

/**
 * Connection states
 */
export enum ConnectionState {
    CONNECTING = 0,
    CONNECTED = 1,
    DISCONNECTING = 2,
    DISCONNECTED = 3,
    ERROR = 4
}

/**
 * Transport types
 */
export enum TransportType {
    TCP = 0,
    SSL = 1,
    HTTP_SSE = 2,
    STDIO = 3,
    PIPE = 4
}

/**
 * Request ID type
 */
export type RequestId = string | number;

/**
 * MCP Role
 */
export enum Role {
    USER = 'user',
    ASSISTANT = 'assistant'
}

/**
 * Implementation info
 */
export interface Implementation {
    name: string;
    version: string;
}

/**
 * Client capabilities
 */
export interface ClientCapabilities {
    experimental?: Record<string, unknown>;
    sampling?: unknown;
    roots?: unknown;
}

/**
 * Server capabilities
 */
export interface ServerCapabilities {
    experimental?: Record<string, unknown>;
    logging?: unknown;
    prompts?: unknown;
    resources?: unknown;
    tools?: unknown;
}

/**
 * Tool definition
 */
export interface Tool {
    name: string;
    description?: string;
    inputSchema: Record<string, unknown>;
}

/**
 * Resource template
 */
export interface ResourceTemplate {
    uriTemplate: string;
    name: string;
    description?: string;
    mimeType?: string;
}

/**
 * Prompt definition
 */
export interface Prompt {
    name: string;
    description?: string;
    arguments: Array<{
        name: string;
        description?: string;
        required?: boolean;
    }>;
}

/**
 * Native binding interface (would be implemented in C++)
 */
interface NativeBinding {
    // Library management
    init(allocator?: unknown): MCPResult;
    shutdown(): void;
    getVersion(): string;
    getLastError(): string;
    
    // Dispatcher
    dispatcherCreate(): number; // Returns handle
    dispatcherRun(handle: number): MCPResult;
    dispatcherRunTimeout(handle: number, timeoutMs: number): MCPResult;
    dispatcherStop(handle: number): void;
    dispatcherDestroy(handle: number): void;
    
    // Connection
    connectionCreateClient(dispatcher: number, transport: TransportType): number;
    connectionConnect(handle: number): MCPResult;
    connectionWrite(handle: number, data: Buffer): MCPResult;
    connectionClose(handle: number, flush: boolean): MCPResult;
    connectionDestroy(handle: number): void;
    
    // Callbacks (using N-API ThreadSafeFunction)
    connectionSetCallbacks(
        handle: number,
        onState?: (oldState: ConnectionState, newState: ConnectionState) => void,
        onData?: (data: Buffer) => void,
        onError?: (error: MCPResult, message: string) => void
    ): MCPResult;
    
    // MCP Client
    clientCreate(dispatcher: number, config: unknown): number;
    clientConnect(handle: number): MCPResult;
    clientInitialize(handle: number): RequestId;
    clientSendRequest(handle: number, method: string, params: unknown): RequestId;
    clientSendNotification(handle: number, method: string, params: unknown): MCPResult;
    clientDestroy(handle: number): void;
    
    // MCP Server
    serverCreate(dispatcher: number, config: unknown): number;
    serverStart(handle: number): MCPResult;
    serverRegisterTool(handle: number, tool: Tool): MCPResult;
    serverRegisterResource(handle: number, resource: ResourceTemplate): MCPResult;
    serverRegisterPrompt(handle: number, prompt: Prompt): MCPResult;
    serverSendResponse(handle: number, requestId: RequestId, result: unknown): MCPResult;
    serverSendError(handle: number, requestId: RequestId, error: unknown): MCPResult;
    serverDestroy(handle: number): void;
}

// Load native module (in real implementation)
// const native: NativeBinding = require('./build/Release/mcp_native.node');

/**
 * High-level TypeScript wrapper
 */
export class MCPLibrary {
    private native: NativeBinding;
    private initialized = false;
    
    constructor(native: NativeBinding) {
        this.native = native;
    }
    
    /**
     * Initialize the library
     */
    async init(): Promise<void> {
        if (this.initialized) return;
        
        const result = this.native.init();
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to initialize MCP library: ${this.native.getLastError()}`);
        }
        this.initialized = true;
    }
    
    /**
     * Shutdown the library
     */
    async shutdown(): Promise<void> {
        if (!this.initialized) return;
        this.native.shutdown();
        this.initialized = false;
    }
    
    /**
     * Get library version
     */
    get version(): string {
        return this.native.getVersion();
    }
    
    /**
     * Create a new dispatcher
     */
    createDispatcher(): Dispatcher {
        if (!this.initialized) {
            throw new Error('Library not initialized');
        }
        return new Dispatcher(this.native);
    }
}

/**
 * Event dispatcher
 */
export class Dispatcher {
    private handle: number;
    private running = false;
    
    constructor(private native: NativeBinding) {
        this.handle = native.dispatcherCreate();
        if (!this.handle) {
            throw new Error('Failed to create dispatcher');
        }
    }
    
    /**
     * Run the dispatcher (blocks)
     */
    async run(): Promise<void> {
        this.running = true;
        const result = this.native.dispatcherRun(this.handle);
        this.running = false;
        
        if (result !== MCPResult.OK) {
            throw new Error(`Dispatcher run failed: ${result}`);
        }
    }
    
    /**
     * Run dispatcher with timeout
     */
    async runTimeout(timeoutMs: number): Promise<void> {
        this.running = true;
        const result = this.native.dispatcherRunTimeout(this.handle, timeoutMs);
        this.running = false;
        
        if (result !== MCPResult.OK) {
            throw new Error(`Dispatcher run failed: ${result}`);
        }
    }
    
    /**
     * Stop the dispatcher
     */
    stop(): void {
        if (this.running) {
            this.native.dispatcherStop(this.handle);
            this.running = false;
        }
    }
    
    /**
     * Create a connection
     */
    createConnection(transport: TransportType): Connection {
        return new Connection(this.native, this.handle, transport);
    }
    
    /**
     * Create an MCP client
     */
    createClient(config: ClientConfig): MCPClient {
        return new MCPClient(this.native, this.handle, config);
    }
    
    /**
     * Create an MCP server
     */
    createServer(config: ServerConfig): MCPServer {
        return new MCPServer(this.native, this.handle, config);
    }
    
    /**
     * Destroy the dispatcher
     */
    destroy(): void {
        this.stop();
        this.native.dispatcherDestroy(this.handle);
    }
}

/**
 * Network connection
 */
export class Connection extends EventTarget {
    private handle: number;
    private state = ConnectionState.DISCONNECTED;
    
    constructor(
        private native: NativeBinding,
        dispatcher: number,
        transport: TransportType
    ) {
        super();
        this.handle = native.connectionCreateClient(dispatcher, transport);
        if (!this.handle) {
            throw new Error('Failed to create connection');
        }
        
        // Set up callbacks
        this.native.connectionSetCallbacks(
            this.handle,
            (oldState, newState) => {
                this.state = newState;
                this.dispatchEvent(new CustomEvent('statechange', {
                    detail: { oldState, newState }
                }));
            },
            (data) => {
                this.dispatchEvent(new CustomEvent('data', {
                    detail: { data }
                }));
            },
            (error, message) => {
                this.dispatchEvent(new CustomEvent('error', {
                    detail: { error, message }
                }));
            }
        );
    }
    
    /**
     * Connect (async)
     */
    async connect(): Promise<void> {
        const result = this.native.connectionConnect(this.handle);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to connect: ${result}`);
        }
        
        // Wait for connected state
        return new Promise((resolve, reject) => {
            const handler = (e: Event) => {
                const event = e as CustomEvent;
                if (event.detail.newState === ConnectionState.CONNECTED) {
                    this.removeEventListener('statechange', handler);
                    resolve();
                } else if (event.detail.newState === ConnectionState.ERROR) {
                    this.removeEventListener('statechange', handler);
                    reject(new Error('Connection failed'));
                }
            };
            this.addEventListener('statechange', handler);
        });
    }
    
    /**
     * Write data
     */
    async write(data: Buffer | string): Promise<void> {
        const buffer = typeof data === 'string' ? Buffer.from(data, 'utf-8') : data;
        const result = this.native.connectionWrite(this.handle, buffer);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to write: ${result}`);
        }
    }
    
    /**
     * Close connection
     */
    async close(flush = true): Promise<void> {
        const result = this.native.connectionClose(this.handle, flush);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to close: ${result}`);
        }
    }
    
    /**
     * Get current state
     */
    getState(): ConnectionState {
        return this.state;
    }
    
    /**
     * Destroy connection
     */
    destroy(): void {
        this.native.connectionDestroy(this.handle);
    }
}

/**
 * Client configuration
 */
export interface ClientConfig {
    clientInfo: Implementation;
    capabilities: ClientCapabilities;
    transport: TransportType;
    serverAddress?: string;
    reconnectDelayMs?: number;
    maxReconnectAttempts?: number;
}

/**
 * MCP Client
 */
export class MCPClient extends EventTarget {
    private handle: number;
    private pendingRequests = new Map<RequestId, {
        resolve: (value: any) => void;
        reject: (error: Error) => void;
    }>();
    
    constructor(
        private native: NativeBinding,
        dispatcher: number,
        private config: ClientConfig
    ) {
        super();
        this.handle = native.clientCreate(dispatcher, config);
        if (!this.handle) {
            throw new Error('Failed to create client');
        }
    }
    
    /**
     * Connect to server
     */
    async connect(): Promise<void> {
        const result = this.native.clientConnect(this.handle);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to connect: ${result}`);
        }
    }
    
    /**
     * Initialize protocol
     */
    async initialize(): Promise<unknown> {
        const requestId = this.native.clientInitialize(this.handle);
        return this.waitForResponse(requestId);
    }
    
    /**
     * Send request
     */
    async sendRequest(method: string, params?: unknown): Promise<unknown> {
        const requestId = this.native.clientSendRequest(this.handle, method, params);
        return this.waitForResponse(requestId);
    }
    
    /**
     * Send notification
     */
    async sendNotification(method: string, params?: unknown): Promise<void> {
        const result = this.native.clientSendNotification(this.handle, method, params);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to send notification: ${result}`);
        }
    }
    
    /**
     * List available tools
     */
    async listTools(): Promise<Tool[]> {
        return this.sendRequest('tools/list') as Promise<Tool[]>;
    }
    
    /**
     * Call a tool
     */
    async callTool(name: string, args: unknown): Promise<unknown> {
        return this.sendRequest('tools/call', { name, arguments: args });
    }
    
    /**
     * List resources
     */
    async listResources(): Promise<ResourceTemplate[]> {
        return this.sendRequest('resources/list') as Promise<ResourceTemplate[]>;
    }
    
    /**
     * Read resource
     */
    async readResource(uri: string): Promise<unknown> {
        return this.sendRequest('resources/read', { uri });
    }
    
    /**
     * Wait for response to request
     */
    private waitForResponse(requestId: RequestId): Promise<unknown> {
        return new Promise((resolve, reject) => {
            this.pendingRequests.set(requestId, { resolve, reject });
        });
    }
    
    /**
     * Destroy client
     */
    destroy(): void {
        this.native.clientDestroy(this.handle);
    }
}

/**
 * Server configuration
 */
export interface ServerConfig {
    serverInfo: Implementation;
    capabilities: ServerCapabilities;
    transport: TransportType;
    bindAddress?: string;
    maxConnections?: number;
    instructions?: string;
}

/**
 * MCP Server
 */
export class MCPServer extends EventTarget {
    private handle: number;
    private tools = new Map<string, Tool>();
    private resources = new Map<string, ResourceTemplate>();
    private prompts = new Map<string, Prompt>();
    
    constructor(
        private native: NativeBinding,
        dispatcher: number,
        private config: ServerConfig
    ) {
        super();
        this.handle = native.serverCreate(dispatcher, config);
        if (!this.handle) {
            throw new Error('Failed to create server');
        }
    }
    
    /**
     * Register a tool
     */
    registerTool(tool: Tool): void {
        const result = this.native.serverRegisterTool(this.handle, tool);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to register tool: ${result}`);
        }
        this.tools.set(tool.name, tool);
    }
    
    /**
     * Register a resource
     */
    registerResource(resource: ResourceTemplate): void {
        const result = this.native.serverRegisterResource(this.handle, resource);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to register resource: ${result}`);
        }
        this.resources.set(resource.uriTemplate, resource);
    }
    
    /**
     * Register a prompt
     */
    registerPrompt(prompt: Prompt): void {
        const result = this.native.serverRegisterPrompt(this.handle, prompt);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to register prompt: ${result}`);
        }
        this.prompts.set(prompt.name, prompt);
    }
    
    /**
     * Start server
     */
    async start(): Promise<void> {
        const result = this.native.serverStart(this.handle);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to start server: ${result}`);
        }
    }
    
    /**
     * Send response
     */
    async sendResponse(requestId: RequestId, result: unknown): Promise<void> {
        const res = this.native.serverSendResponse(this.handle, requestId, result);
        if (res !== MCPResult.OK) {
            throw new Error(`Failed to send response: ${res}`);
        }
    }
    
    /**
     * Send error response
     */
    async sendError(requestId: RequestId, code: number, message: string, data?: unknown): Promise<void> {
        const error = { code, message, data };
        const result = this.native.serverSendError(this.handle, requestId, error);
        if (result !== MCPResult.OK) {
            throw new Error(`Failed to send error: ${result}`);
        }
    }
    
    /**
     * Destroy server
     */
    destroy(): void {
        this.native.serverDestroy(this.handle);
    }
}

/**
 * Example usage
 */
export async function exampleUsage() {
    // Initialize library
    const lib = new MCPLibrary({} as NativeBinding); // Would use actual native binding
    await lib.init();
    
    try {
        // Create dispatcher
        const dispatcher = lib.createDispatcher();
        
        // Create stdio client
        const client = dispatcher.createClient({
            clientInfo: {
                name: 'TypeScript MCP Client',
                version: '1.0.0'
            },
            capabilities: {},
            transport: TransportType.STDIO
        });
        
        // Connect and initialize
        await client.connect();
        const initResult = await client.initialize();
        console.log('Initialized:', initResult);
        
        // List available tools
        const tools = await client.listTools();
        console.log('Available tools:', tools);
        
        // Call a tool
        if (tools.length > 0) {
            const result = await client.callTool(tools[0].name, {});
            console.log('Tool result:', result);
        }
        
        // Run event loop in background
        dispatcher.runTimeout(5000).catch(console.error);
        
    } finally {
        await lib.shutdown();
    }
}