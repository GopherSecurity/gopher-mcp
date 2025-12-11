#!/bin/bash
# test-server-mock.sh - Mock MCP server for testing without C++ dependencies

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
SERVER_PORT="${PORT:-8080}"
SERVER_HOST="${HOST:-127.0.0.1}"
PID_FILE="/tmp/mock-server-$$.pid"

echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}🎭 Mock MCP Calculator Server${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}This is a simple mock server for testing without C++ dependencies${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}🧹 Stopping mock server...${NC}"
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            kill "$PID" 2>/dev/null || true
            sleep 1
            kill -9 "$PID" 2>/dev/null || true
        fi
        rm -f "$PID_FILE"
    fi
    echo -e "${GREEN}✅ Mock server stopped${NC}"
}

trap cleanup EXIT INT TERM

# Create a simple Node.js mock server
cat > /tmp/mock-server-$$.js << 'EOF'
const http = require('http');

const PORT = process.env.PORT || 8080;
const HOST = process.env.HOST || '127.0.0.1';

// Mock calculator state
let memory = 0;
let history = [];
let requestId = 1;

const server = http.createServer((req, res) => {
    console.log(`[${new Date().toISOString()}] ${req.method} ${req.url}`);
    
    // CORS headers
    res.setHeader('Content-Type', 'application/json');
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    
    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }
    
    // Health check endpoint
    if (req.url === '/health') {
        res.writeHead(200);
        res.end(JSON.stringify({ status: 'ok' }));
        return;
    }
    
    // MCP endpoint
    if (req.url === '/mcp' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const request = JSON.parse(body);
                let response = {
                    jsonrpc: '2.0',
                    id: request.id
                };
                
                // Handle different MCP methods
                if (request.method === 'tools/list') {
                    response.result = {
                        tools: [
                            {
                                name: 'calculate',
                                description: 'Perform arithmetic calculations',
                                inputSchema: {
                                    type: 'object',
                                    properties: {
                                        operation: { type: 'string' },
                                        a: { type: 'number' },
                                        b: { type: 'number' }
                                    }
                                }
                            },
                            {
                                name: 'memory',
                                description: 'Manage calculator memory',
                                inputSchema: {
                                    type: 'object',
                                    properties: {
                                        action: { type: 'string' },
                                        value: { type: 'number' }
                                    }
                                }
                            },
                            {
                                name: 'history',
                                description: 'View calculation history',
                                inputSchema: {
                                    type: 'object',
                                    properties: {
                                        action: { type: 'string' },
                                        limit: { type: 'number' }
                                    }
                                }
                            }
                        ]
                    };
                } else if (request.method === 'tools/call') {
                    const { name, arguments: args } = request.params;
                    
                    if (name === 'calculate') {
                        const { operation, a, b } = args;
                        let result;
                        let opSymbol;
                        
                        switch(operation) {
                            case 'add':
                                result = a + b;
                                opSymbol = '+';
                                break;
                            case 'subtract':
                                result = a - b;
                                opSymbol = '-';
                                break;
                            case 'multiply':
                                result = a * b;
                                opSymbol = '×';
                                break;
                            case 'divide':
                                result = a / b;
                                opSymbol = '÷';
                                break;
                            case 'power':
                                result = Math.pow(a, b);
                                opSymbol = '^';
                                break;
                            case 'sqrt':
                                result = Math.sqrt(a);
                                opSymbol = '√';
                                break;
                            default:
                                result = 0;
                                opSymbol = '?';
                        }
                        
                        const calculation = operation === 'sqrt' ? 
                            `√${a} = ${result}` : 
                            `${a} ${opSymbol} ${b} = ${result}`;
                            
                        history.push({
                            id: `calc_${requestId++}`,
                            operation: calculation,
                            result: result,
                            timestamp: new Date().toISOString()
                        });
                        
                        response.result = {
                            content: [{
                                type: 'text',
                                text: calculation
                            }]
                        };
                    } else if (name === 'memory') {
                        const { action, value } = args;
                        
                        switch(action) {
                            case 'store':
                                memory = value;
                                response.result = {
                                    content: [{
                                        type: 'text',
                                        text: `Stored ${value} in memory`
                                    }]
                                };
                                break;
                            case 'recall':
                                response.result = {
                                    content: [{
                                        type: 'text',
                                        text: `Memory value: ${memory}`
                                    }]
                                };
                                break;
                            case 'clear':
                                memory = 0;
                                response.result = {
                                    content: [{
                                        type: 'text',
                                        text: 'Memory cleared'
                                    }]
                                };
                                break;
                        }
                    } else if (name === 'history') {
                        const { limit = 10 } = args;
                        const recentHistory = history.slice(-limit);
                        
                        response.result = {
                            content: [{
                                type: 'text',
                                text: `History (last ${limit} calculations):\n${recentHistory.map(h => 
                                    `• ${h.operation} (${new Date(h.timestamp).toLocaleTimeString()})`
                                ).join('\n')}`
                            }]
                        };
                    }
                } else {
                    response.error = {
                        code: -32601,
                        message: 'Method not found'
                    };
                }
                
                res.writeHead(200);
                res.end(JSON.stringify(response));
            } catch (error) {
                res.writeHead(400);
                res.end(JSON.stringify({
                    jsonrpc: '2.0',
                    error: {
                        code: -32700,
                        message: 'Parse error'
                    },
                    id: null
                }));
            }
        });
    } else {
        res.writeHead(404);
        res.end(JSON.stringify({ error: 'Not found' }));
    }
});

server.listen(PORT, HOST, () => {
    console.log(`🎭 Mock MCP Server running at http://${HOST}:${PORT}/mcp`);
    console.log(`🏥 Health check at http://${HOST}:${PORT}/health`);
    console.log('📝 Ready to handle requests...');
});

// Graceful shutdown
process.on('SIGTERM', () => {
    console.log('Shutting down mock server...');
    server.close(() => {
        process.exit(0);
    });
});
EOF

# Start the mock server
echo -e "${YELLOW}Starting mock server on http://$SERVER_HOST:$SERVER_PORT${NC}"
node /tmp/mock-server-$$.js &
SERVER_PID=$!
echo $SERVER_PID > "$PID_FILE"

# Wait for server to start
sleep 2

# Test if server is running
if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo -e "${GREEN}✅ Mock server started successfully (PID: $SERVER_PID)${NC}\n"
    
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}Mock Server Information${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "Server URL: ${GREEN}http://$SERVER_HOST:$SERVER_PORT/mcp${NC}"
    echo -e "Health URL: ${GREEN}http://$SERVER_HOST:$SERVER_PORT/health${NC}"
    echo -e "\n${CYAN}Available Tools:${NC}"
    echo -e "  • calculate - Arithmetic operations"
    echo -e "  • memory - Memory management"
    echo -e "  • history - Calculation history"
    
    echo -e "\n${CYAN}Test Commands:${NC}"
    echo -e "  ${YELLOW}# Health check${NC}"
    echo -e "  curl http://$SERVER_HOST:$SERVER_PORT/health"
    echo -e "\n  ${YELLOW}# List tools${NC}"
    echo -e "  curl -X POST http://$SERVER_HOST:$SERVER_PORT/mcp \\"
    echo -e "    -H 'Content-Type: application/json' \\"
    echo -e "    -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}'"
    echo -e "\n  ${YELLOW}# Calculate${NC}"
    echo -e "  curl -X POST http://$SERVER_HOST:$SERVER_PORT/mcp \\"
    echo -e "    -H 'Content-Type: application/json' \\"
    echo -e "    -d '{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
    echo -e "        \"params\":{\"name\":\"calculate\",\"arguments\":{\"operation\":\"add\",\"a\":5,\"b\":3}}}'"
    
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${YELLOW}Press Ctrl+C to stop the mock server${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"
    
    # Keep running
    wait $SERVER_PID
else
    echo -e "${RED}❌ Failed to start mock server${NC}"
    exit 1
fi