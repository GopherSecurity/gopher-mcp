/**
 * @file config-utils.ts
 * @brief Configuration utilities for MCP Filter SDK
 *
 * Provides utilities for loading, converting, and validating filter chain configurations
 * in both canonical (listener-based) and assembler formats.
 */

import * as fs from 'fs';
import * as path from 'path';
import {
  CanonicalConfig,
  FilterSpec,
} from './mcp-filter-chain';

/**
 * Load configuration from a JSON file
 */
export function loadConfigFromFile(filePath: string): CanonicalConfig {
  try {
    const absolutePath = path.resolve(filePath);
    const configData = fs.readFileSync(absolutePath, 'utf-8');
    const config = JSON.parse(configData);

    // Validate that it's in canonical format
    if (!config.listeners || !Array.isArray(config.listeners)) {
      throw new Error('Configuration must be in canonical format with a "listeners" array');
    }

    return config as CanonicalConfig;
  } catch (error) {
    throw new Error(`Failed to load configuration from ${filePath}: ${error}`);
  }
}

/**
 * Convert nested object configuration to canonical format
 * This handles the legacy nested format used in existing examples
 */
export function convertNestedToCanonical(
  nestedConfig: any,
  listenerName: string = 'default_listener',
  port: number = 8080
): CanonicalConfig {
  const filters: FilterSpec[] = [];

  // Extract filters from nested configuration
  if (nestedConfig.security?.authentication) {
    filters.push({
      name: 'auth',
      type: 'security.authentication',
      config: nestedConfig.security.authentication,
    });
  }

  if (nestedConfig.security?.authorization) {
    filters.push({
      name: 'authz',
      type: 'security.authorization',
      config: nestedConfig.security.authorization,
    });
  }

  if (nestedConfig.observability?.accessLog) {
    filters.push({
      name: 'access_log',
      type: 'observability.access_log',
      config: nestedConfig.observability.accessLog,
    });
  }

  if (nestedConfig.observability?.metrics) {
    filters.push({
      name: 'metrics',
      type: 'observability.metrics',
      config: nestedConfig.observability.metrics,
    });
  }

  if (nestedConfig.observability?.tracing) {
    filters.push({
      name: 'tracing',
      type: 'observability.tracing',
      config: nestedConfig.observability.tracing,
    });
  }

  if (nestedConfig.trafficManagement?.rateLimit) {
    filters.push({
      name: 'rate_limit',
      type: 'traffic.rate_limit',
      config: nestedConfig.trafficManagement.rateLimit,
    });
  }

  if (nestedConfig.trafficManagement?.circuitBreaker) {
    filters.push({
      name: 'circuit_breaker',
      type: 'traffic.circuit_breaker',
      config: nestedConfig.trafficManagement.circuitBreaker,
    });
  }

  // Add custom filters
  if (nestedConfig.customFilters) {
    for (const [name, config] of Object.entries(nestedConfig.customFilters)) {
      filters.push({
        name,
        type: 'custom',
        config,
      });
    }
  }

  return {
    listeners: [
      {
        name: listenerName,
        address: {
          socket_address: {
            address: '127.0.0.1',
            port_value: port,
          },
        },
        filter_chains: [
          {
            filters,
          },
        ],
      },
    ],
  };
}

/**
 * Validate canonical configuration
 */
export function validateCanonicalConfig(config: CanonicalConfig): {
  valid: boolean;
  errors: string[];
  warnings: string[];
} {
  const errors: string[] = [];
  const warnings: string[] = [];

  // Check if listeners array exists and is not empty
  if (!config.listeners || !Array.isArray(config.listeners)) {
    errors.push('Configuration must have a "listeners" array');
    return { valid: false, errors, warnings };
  }

  if (config.listeners.length === 0) {
    errors.push('At least one listener must be defined');
    return { valid: false, errors, warnings };
  }

  // Validate each listener
  config.listeners.forEach((listener, listenerIndex) => {
    if (!listener.name) {
      errors.push(`Listener ${listenerIndex} must have a name`);
    }

    if (!listener.address?.socket_address) {
      errors.push(`Listener ${listener.name || listenerIndex} must have an address with socket_address`);
    } else {
      const { socket_address } = listener.address;
      if (!socket_address.address) {
        errors.push(`Listener ${listener.name || listenerIndex} socket_address must have an address`);
      }
      if (typeof socket_address.port_value !== 'number') {
        errors.push(`Listener ${listener.name || listenerIndex} socket_address must have a numeric port_value`);
      }
    }

    if (!listener.filter_chains || !Array.isArray(listener.filter_chains)) {
      errors.push(`Listener ${listener.name || listenerIndex} must have a filter_chains array`);
    } else if (listener.filter_chains.length === 0) {
      warnings.push(`Listener ${listener.name || listenerIndex} has no filter chains defined`);
    } else {
      // Validate filter chains
      listener.filter_chains.forEach((chain, chainIndex) => {
        if (!chain.filters || !Array.isArray(chain.filters)) {
          errors.push(`Filter chain ${chainIndex} in listener ${listener.name} must have a filters array`);
        } else if (chain.filters.length === 0) {
          warnings.push(`Filter chain ${chainIndex} in listener ${listener.name} has no filters`);
        } else {
          // Validate individual filters
          chain.filters.forEach((filter, filterIndex) => {
            if (!filter.name) {
              errors.push(`Filter ${filterIndex} in chain ${chainIndex} of listener ${listener.name} must have a name`);
            }
            if (!filter.type) {
              errors.push(`Filter ${filter.name || filterIndex} must have a type`);
            }
          });
        }
      });
    }
  });

  return {
    valid: errors.length === 0,
    errors,
    warnings,
  };
}

/**
 * Create default canonical configuration for common scenarios
 */
export function createDefaultConfig(scenario: 'http' | 'tcp' | 'mcp-server' | 'mcp-client'): CanonicalConfig {
  switch (scenario) {
    case 'http':
      return {
        listeners: [
          {
            name: 'http_listener',
            address: {
              socket_address: {
                address: '127.0.0.1',
                port_value: 8080,
              },
            },
            filter_chains: [
              {
                filters: [
                  { name: 'http_codec', type: 'http.codec' },
                  { name: 'router', type: 'http.router' },
                ],
              },
            ],
          },
        ],
      };

    case 'tcp':
      return {
        listeners: [
          {
            name: 'tcp_listener',
            address: {
              socket_address: {
                address: '127.0.0.1',
                port_value: 9090,
              },
            },
            filter_chains: [
              {
                filters: [
                  { name: 'tcp_proxy', type: 'tcp.proxy' },
                ],
              },
            ],
          },
        ],
      };

    case 'mcp-server':
      return {
        listeners: [
          {
            name: 'mcp_server_listener',
            address: {
              socket_address: {
                address: '127.0.0.1',
                port_value: 9090,
              },
            },
            filter_chains: [
              {
                filters: [
                  { name: 'http.codec', type: 'http.codec' },
                  { name: 'sse.codec', type: 'sse.codec' },
                  { name: 'json_rpc.dispatcher', type: 'json_rpc.dispatcher' },
                ],
              },
            ],
          },
        ],
      };

    case 'mcp-client':
      return {
        listeners: [
          {
            name: 'mcp_client_listener',
            address: {
              socket_address: {
                address: '127.0.0.1',
                port_value: 0, // Client uses ephemeral port
              },
            },
            filter_chains: [
              {
                filters: [
                  { name: 'http.codec', type: 'http.codec' },
                  { name: 'sse.codec', type: 'sse.codec' },
                  { name: 'json_rpc.client', type: 'json_rpc.client' },
                ],
              },
            ],
          },
        ],
      };

    default:
      throw new Error(`Unknown scenario: ${scenario}`);
  }
}

/**
 * Merge multiple canonical configurations
 * Useful for combining base configs with overrides
 */
export function mergeCanonicalConfigs(...configs: CanonicalConfig[]): CanonicalConfig {
  const merged: CanonicalConfig = { listeners: [] };

  for (const config of configs) {
    if (config.listeners) {
      merged.listeners.push(...config.listeners);
    }
  }

  return merged;
}

