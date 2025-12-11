/**
 * Example: Simple Pattern with AuthenticatedMcpServer
 * 
 * This shows the existing simple pattern that works with
 * code like /Users/james/Desktop/temp/1/src
 */

import dotenv from 'dotenv';
import { AuthenticatedMcpServer, Tool } from '@mcp/filter-sdk/auth';

// Load environment variables
dotenv.config();

// Define tools
const getWeather: Tool = {
  name: 'get-weather',
  description: 'Get current weather',
  inputSchema: {
    type: 'object',
    properties: {
      location: { type: 'string' }
    },
    required: ['location']
  },
  handler: async (request) => {
    return {
      content: [{
        type: 'text',
        text: `Weather in ${request.params.location}: Sunny, 72°F`
      }]
    };
  }
};

const getForecast: Tool = {
  name: 'get-forecast',
  description: 'Get weather forecast (requires mcp:weather scope)',
  inputSchema: {
    type: 'object',
    properties: {
      location: { type: 'string' },
      days: { type: 'number' }
    },
    required: ['location']
  },
  handler: async (request) => {
    return {
      content: [{
        type: 'text',
        text: `${request.params.days}-day forecast for ${request.params.location}`
      }]
    };
  }
};

const getAlerts: Tool = {
  name: 'get-weather-alerts',
  description: 'Get weather alerts (requires mcp:weather scope)',
  inputSchema: {
    type: 'object',
    properties: {
      location: { type: 'string' }
    },
    required: ['location']
  },
  handler: async (request) => {
    return {
      content: [{
        type: 'text',
        text: `No weather alerts for ${request.params.location}`
      }]
    };
  }
};

// Define available tools
const tools = [getWeather, getForecast, getAlerts];

// Create and configure server
// All configuration comes from environment variables
const server = new AuthenticatedMcpServer({
  // These can be set via config or environment variables
  // serverName: 'weather-server',
  // serverVersion: '1.0.0',
  // requireAuth: true,
  // toolScopes: {
  //   'get-forecast': 'mcp:weather',
  //   'get-weather-alerts': 'mcp:weather',
  // }
});

// Register tools
server.register(tools);

// Start the server
server.start().catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});

console.log('✨ Using simple pattern with AuthenticatedMcpServer');
console.log('   Configuration from environment variables');
console.log('   Compatible with existing code patterns');