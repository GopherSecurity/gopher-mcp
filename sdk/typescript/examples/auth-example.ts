/**
 * @file auth-example.ts
 * @brief Basic authentication usage example for MCP Filter SDK
 *
 * This example demonstrates:
 * - JWT token validation
 * - Scope checking
 * - Authentication client configuration
 * - Error handling for authentication failures
 * - Payload extraction from tokens
 */

// Import auth module types and functions
// Note: In production, you would import from '@mcp/filter-sdk/auth'
import * as authModule from '../src/auth';
import type {
  AuthClientConfig,
  ValidationOptions,
  ValidationResult,
  TokenPayload,
  AuthErrorCode,
  WwwAuthenticateParams
} from '../src/auth-types';

/**
 * Example 1: Basic token validation with a configured client
 */
async function basicTokenValidation() {
  console.log('🔐 Example 1: Basic Token Validation');
  console.log('=' .repeat(50));

  // Check if authentication is available
  if (!authModule.isAuthAvailable()) {
    console.error('❌ Authentication support is not available.');
    console.log('   Please ensure the MCP C++ SDK is built with auth support.');
    return;
  }

  // Configuration for the auth client
  const config: AuthClientConfig = {
    jwksUri: 'https://auth.example.com/.well-known/jwks.json',
    issuer: 'https://auth.example.com',
    cacheDuration: 3600, // Cache keys for 1 hour
    autoRefresh: true,    // Automatically refresh keys
    requestTimeout: 5000  // 5 second timeout for JWKS requests
  };

  // Create auth client
  const client = new authModule.McpAuthClient(config);
  
  try {
    // Initialize the client
    await client.initialize();
    console.log('✅ Auth client initialized successfully');
    
    // Example JWT token (in real usage, this would come from a request)
    const token = 'eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...';
    
    // Validation options
    const options: ValidationOptions = {
      scopes: 'read:users write:users',  // Required scopes
      audience: 'api.example.com',        // Expected audience
      clockSkew: 60                       // Allow 60 seconds clock skew
    };
    
    // Validate the token
    console.log('\n📝 Validating token...');
    const result: ValidationResult = await client.validateToken(token, options);
    
    if (result.valid) {
      console.log('✅ Token is valid!');
    } else {
      console.log(`❌ Token validation failed: ${result.errorMessage}`);
      console.log(`   Error code: ${result.errorCode}`);
    }
    
    // Check scopes separately
    const hasRequiredScopes = await client.validateScopes(
      'read:users',
      'read:users write:users admin'
    );
    console.log(`\n🔍 Scope check: ${hasRequiredScopes ? '✅ Passed' : '❌ Failed'}`);
    
  } catch (error) {
    console.error('❌ Error during validation:', error);
  } finally {
    // Always cleanup
    await client.destroy();
    console.log('\n🧹 Client destroyed');
  }
}

/**
 * Example 2: Extract and examine token payload without validation
 */
async function extractPayloadExample() {
  console.log('\n\n📋 Example 2: Token Payload Extraction');
  console.log('=' .repeat(50));
  
  if (!authModule.isAuthAvailable()) {
    console.error('❌ Authentication support is not available.');
    return;
  }
  
  // Example JWT token
  const token = 'eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...';
  
  try {
    // Extract payload without validation (useful for debugging)
    console.log('📝 Extracting token payload...');
    const payload: TokenPayload = await authModule.extractTokenPayload(token);
    
    console.log('\n📦 Token Payload:');
    console.log(`   Subject: ${payload.subject || 'N/A'}`);
    console.log(`   Issuer: ${payload.issuer || 'N/A'}`);
    console.log(`   Audience: ${payload.audience || 'N/A'}`);
    console.log(`   Scopes: ${payload.scopes || 'N/A'}`);
    
    if (payload.expiration) {
      const expirationDate = new Date(payload.expiration * 1000);
      console.log(`   Expiration: ${expirationDate.toISOString()}`);
      
      const isExpired = payload.expiration < Math.floor(Date.now() / 1000);
      console.log(`   Status: ${isExpired ? '❌ Expired' : '✅ Active'}`);
    }
    
    if (payload.customClaims) {
      console.log('\n📎 Custom Claims:');
      for (const [key, value] of Object.entries(payload.customClaims)) {
        console.log(`   ${key}: ${JSON.stringify(value)}`);
      }
    }
    
  } catch (error) {
    console.error('❌ Error extracting payload:', error);
  }
}

/**
 * Example 3: One-shot validation without creating a client
 */
async function oneShotValidation() {
  console.log('\n\n⚡ Example 3: One-Shot Token Validation');
  console.log('=' .repeat(50));
  
  if (!authModule.isAuthAvailable()) {
    console.error('❌ Authentication support is not available.');
    return;
  }
  
  const config: AuthClientConfig = {
    jwksUri: 'https://auth.example.com/.well-known/jwks.json',
    issuer: 'https://auth.example.com'
  };
  
  const token = 'eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...';
  
  try {
    // Validate token in a single call (creates and destroys client automatically)
    console.log('📝 Performing one-shot validation...');
    const result = await authModule.validateToken(token, config, {
      audience: 'api.example.com'
    });
    
    console.log(`\n📊 Validation Result: ${result.valid ? '✅ Valid' : '❌ Invalid'}`);
    if (!result.valid) {
      console.log(`   Reason: ${result.errorMessage || 'Unknown'}`);
    }
    
  } catch (error) {
    console.error('❌ Error during one-shot validation:', error);
  }
}

/**
 * Example 4: Error handling scenarios
 */
async function errorHandlingExamples() {
  console.log('\n\n🚨 Example 4: Error Handling Scenarios');
  console.log('=' .repeat(50));
  
  if (!authModule.isAuthAvailable()) {
    console.error('❌ Authentication support is not available.');
    return;
  }
  
  // Test various error scenarios
  const scenarios = [
    {
      name: 'Invalid JWKS URI',
      config: {
        jwksUri: 'https://invalid-domain-that-does-not-exist.com/jwks',
        issuer: 'https://auth.example.com'
      },
      token: 'any-token'
    },
    {
      name: 'Malformed token',
      config: {
        jwksUri: 'https://auth.example.com/.well-known/jwks.json',
        issuer: 'https://auth.example.com'
      },
      token: 'not-a-valid-jwt-token'
    },
    {
      name: 'Empty token',
      config: {
        jwksUri: 'https://auth.example.com/.well-known/jwks.json',
        issuer: 'https://auth.example.com'
      },
      token: ''
    }
  ];
  
  for (const scenario of scenarios) {
    console.log(`\n🧪 Testing: ${scenario.name}`);
    
    const client = new authModule.McpAuthClient(scenario.config as AuthClientConfig);
    
    try {
      await client.initialize();
      const result = await client.validateToken(scenario.token);
      
      if (!result.valid) {
        console.log(`   Expected failure: ${authModule.errorCodeToString(result.errorCode)}`);
        if (result.errorMessage) {
          console.log(`   Details: ${result.errorMessage}`);
        }
      }
      
    } catch (error: any) {
      console.log(`   Caught error: ${error.message}`);
      if (error.code !== undefined) {
        console.log(`   Error code: ${error.code}`);
      }
    } finally {
      await client.destroy();
    }
  }
}

/**
 * Example 5: WWW-Authenticate header generation
 */
async function wwwAuthenticateExample() {
  console.log('\n\n🌐 Example 5: WWW-Authenticate Header Generation');
  console.log('=' .repeat(50));
  
  if (!authModule.isAuthAvailable()) {
    console.error('❌ Authentication support is not available.');
    return;
  }
  
  const config: AuthClientConfig = {
    jwksUri: 'https://auth.example.com/.well-known/jwks.json',
    issuer: 'https://auth.example.com'
  };
  
  const client = new authModule.McpAuthClient(config);
  
  try {
    await client.initialize();
    
    // Generate WWW-Authenticate header for various scenarios
    const scenarios = [
      {
        name: 'Missing token',
        params: {
          realm: 'api',
          error: undefined,
          errorDescription: undefined
        }
      },
      {
        name: 'Invalid token',
        params: {
          realm: 'api',
          error: 'invalid_token',
          errorDescription: 'The access token is malformed'
        }
      },
      {
        name: 'Expired token',
        params: {
          realm: 'api',
          error: 'invalid_token',
          errorDescription: 'The access token expired',
          errorUri: 'https://docs.example.com/errors/expired-token'
        }
      },
      {
        name: 'Insufficient scope',
        params: {
          realm: 'api',
          scope: 'read:users write:users',
          error: 'insufficient_scope',
          errorDescription: 'The access token does not have the required scopes'
        }
      }
    ];
    
    for (const scenario of scenarios) {
      console.log(`\n📝 ${scenario.name}:`);
      const header = await client.generateWwwAuthenticate(scenario.params);
      console.log(`   ${header}`);
    }
    
  } finally {
    await client.destroy();
  }
}

/**
 * Main function to run all examples
 */
async function main() {
  console.log('🚀 MCP Authentication Examples');
  console.log('=' .repeat(60));
  console.log('This example demonstrates various authentication scenarios');
  console.log('using the MCP Filter SDK authentication module.\n');
  
  // Run examples sequentially
  await basicTokenValidation();
  await extractPayloadExample();
  await oneShotValidation();
  await errorHandlingExamples();
  await wwwAuthenticateExample();
  
  console.log('\n\n✨ All examples completed!');
  console.log('=' .repeat(60));
}

// Run the examples
if (require.main === module) {
  main().catch(error => {
    console.error('Fatal error:', error);
    process.exit(1);
  });
}

export { main };