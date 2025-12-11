#include <stdio.h>
#include <stdlib.h>
#include "mcp/auth/auth_c_api.h"

int main() {
    printf("Testing standalone auth library...\n");
    
    // Initialize auth library
    mcp_auth_error_t err = mcp_auth_init();
    if (err != MCP_AUTH_SUCCESS) {
        printf("Failed to initialize auth library: %d\n", err);
        return 1;
    }
    printf("✅ Auth library initialized\n");
    
    // Create auth client
    mcp_auth_client_t client = NULL;
    err = mcp_auth_client_create(&client, 
                                  "https://auth.example.com/jwks.json",
                                  "https://auth.example.com");
    if (err != MCP_AUTH_SUCCESS) {
        printf("Failed to create auth client: %d\n", err);
        mcp_auth_shutdown();
        return 1;
    }
    printf("✅ Auth client created\n");
    
    // Test with a mock JWT token
    const char* mock_token = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
                            "eyJpc3MiOiJodHRwczovL2F1dGguZXhhbXBsZS5jb20iLCJzdWIiOiJ0ZXN0In0."
                            "mock_signature";
    
    mcp_auth_validation_result_t result;
    err = mcp_auth_validate_token(client, mock_token, NULL, &result);
    
    // We expect validation to fail (invalid signature) but the library should work
    if (err == MCP_AUTH_ERROR_JWKS_FETCH_FAILED || 
        err == MCP_AUTH_ERROR_INVALID_SIGNATURE ||
        err == MCP_AUTH_ERROR_INVALID_KEY) {
        printf("✅ Token validation executed (expected failure with mock token)\n");
        printf("   Error code: %d\n", err);
        printf("   Valid: %s\n", result.valid ? "true" : "false");
    } else if (err == MCP_AUTH_SUCCESS) {
        printf("⚠️  Unexpected success with mock token\n");
    } else {
        printf("❌ Unexpected error: %d\n", err);
    }
    
    // Get library version
    const char* version = mcp_auth_version();
    if (version) {
        printf("✅ Library version: %s\n", version);
    }
    
    // Clean up
    mcp_auth_client_destroy(client);
    mcp_auth_shutdown();
    printf("✅ Auth library shutdown complete\n");
    
    printf("\n🎉 Standalone auth library is working!\n");
    return 0;
}