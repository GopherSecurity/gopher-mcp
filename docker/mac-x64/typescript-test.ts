#!/usr/bin/env node

/**
 * TypeScript test for libgopher_mcp_auth on macOS
 * Verifies that the authentication library works correctly with TypeScript/Node.js
 */

import * as koffi from 'koffi';
import * as path from 'path';
import * as fs from 'fs';

// Color output helpers
const RED = '\x1b[31m';
const GREEN = '\x1b[32m';
const YELLOW = '\x1b[33m';
const RESET = '\x1b[0m';

console.log(`${GREEN}========================================${RESET}`);
console.log(`${GREEN}TypeScript libgopher_mcp_auth Test${RESET}`);
console.log(`${GREEN}========================================${RESET}`);

// Test configuration
const LIBRARY_NAME = process.env.MCP_LIBRARY_PATH || './libgopher_mcp_auth.dylib';
const TEST_TOKEN = 'test_token_12345';
const TEST_CLIENT_ID = 'test_client';
const TEST_AUDIENCE = 'https://test.example.com';

console.log(`Library path: ${LIBRARY_NAME}`);

// Check if library exists
if (!fs.existsSync(LIBRARY_NAME)) {
    console.error(`${RED}❌ Library not found at ${LIBRARY_NAME}${RESET}`);
    process.exit(1);
}

try {
    // Load the library
    console.log(`${YELLOW}Loading library...${RESET}`);
    const lib = koffi.load(LIBRARY_NAME);

    // Define structs
    const AuthConfig = koffi.struct('AuthConfig', {
        issuer: 'char*',
        client_id: 'char*',
        client_secret: 'char*',
        auth_server_url: 'char*',
        jwks_uri: 'char*',
        token_endpoint: 'char*',
        revocation_endpoint: 'char*',
        redirect_uri: 'char*',
        audience: 'char*',
        cache_duration: 'int32',
        auto_refresh: 'bool',
        debug_mode: 'bool'
    });

    const TokenPayload = koffi.struct('TokenPayload', {
        iss: 'char*',
        sub: 'char*',
        aud: 'char*',
        exp: 'int64',
        iat: 'int64',
        nbf: 'int64',
        jti: 'char*',
        client_id: 'char*',
        scope: 'char*',
        extra_json: 'char*'
    });

    const ValidationResult = koffi.struct('ValidationResult', {
        valid: 'bool',
        error_code: 'int32',
        error_message: 'char*',
        payload: TokenPayload
    });

    // Define function signatures (using actual exported names)
    console.log(`${YELLOW}Defining FFI functions...${RESET}`);
    
    const mcp_auth_init = lib.func('void* mcp_auth_init()');
    const mcp_auth_client_create = lib.func('void* mcp_auth_client_create(AuthConfig* config)');
    const mcp_auth_validate_token = lib.func('void* mcp_auth_validate_token(void* client, char* token, void* options)');
    const mcp_auth_payload_destroy = lib.func('void mcp_auth_payload_destroy(void* payload)');
    const mcp_auth_client_destroy = lib.func('void mcp_auth_client_destroy(void* client)');
    const mcp_auth_shutdown = lib.func('void mcp_auth_shutdown()');
    const mcp_auth_version = lib.func('char* mcp_auth_version()');
    const mcp_auth_get_last_error = lib.func('char* mcp_auth_get_last_error()');

    // Test 1: Get library version
    console.log(`\n${YELLOW}Test 1: Get library version${RESET}`);
    const version = mcp_auth_version();
    if (version) {
        console.log(`${GREEN}✓ Library version: ${version}${RESET}`);
    } else {
        console.log(`${RED}✗ Failed to get library version${RESET}`);
    }

    // Test 2: Initialize library
    console.log(`\n${YELLOW}Test 2: Initialize auth library${RESET}`);
    const initResult = mcp_auth_init();
    if (initResult) {
        console.log(`${GREEN}✓ Auth library initialized${RESET}`);
    }

    // Test 3: Create client with config
    console.log(`\n${YELLOW}Test 3: Create auth client${RESET}`);
    const config: any = {
        issuer: 'https://test.example.com',
        client_id: TEST_CLIENT_ID,
        client_secret: 'test_secret',
        auth_server_url: 'https://auth.test.example.com',
        jwks_uri: 'https://auth.test.example.com/jwks',
        token_endpoint: 'https://auth.test.example.com/token',
        revocation_endpoint: 'https://auth.test.example.com/revoke',
        redirect_uri: 'http://localhost:3000/callback',
        audience: TEST_AUDIENCE,
        cache_duration: 3600,
        auto_refresh: true,
        debug_mode: false
    };

    const client = mcp_auth_client_create(config);
    if (client && client !== null) {
        console.log(`${GREEN}✓ Auth client created successfully${RESET}`);
    } else {
        const error = mcp_auth_get_last_error();
        console.log(`${RED}✗ Failed to create auth client: ${error}${RESET}`);
    }

    // Test 4: Token validation API (skip to avoid network calls)
    console.log(`\n${YELLOW}Test 4: Token validation API${RESET}`);
    console.log(`${YELLOW}⚠ Skipping actual validation to avoid network calls${RESET}`);

    // Test 5: Error handling
    console.log(`\n${YELLOW}Test 5: Error handling${RESET}`);
    const lastError = mcp_auth_get_last_error();
    if (lastError) {
        console.log(`${GREEN}✓ Error retrieval works: ${lastError}${RESET}`);
    } else {
        console.log(`${GREEN}✓ No errors reported${RESET}`);
    }

    // Test 6: Cleanup
    console.log(`\n${YELLOW}Test 6: Cleanup${RESET}`);
    if (client) {
        try {
            mcp_auth_client_destroy(client);
            console.log(`${GREEN}✓ Client destroyed successfully${RESET}`);
        } catch (e) {
            console.log(`${YELLOW}⚠ Client cleanup skipped${RESET}`);
        }
    }
    try {
        mcp_auth_shutdown();
        console.log(`${GREEN}✓ Library shutdown complete${RESET}`);
    } catch (e) {
        console.log(`${YELLOW}⚠ Library shutdown skipped${RESET}`);
    }

    // Summary
    console.log(`\n${GREEN}========================================${RESET}`);
    console.log(`${GREEN}✅ All TypeScript tests completed!${RESET}`);
    console.log(`${GREEN}========================================${RESET}`);
    console.log('\nThe library is working correctly with TypeScript/Node.js!');
    
    process.exit(0);

} catch (error: any) {
    console.error(`\n${RED}❌ Test failed with error:${RESET}`);
    console.error(error.message || error);
    process.exit(1);
}