/**
 * @file auth-integration.test.ts
 * @brief Integration tests for authentication module
 */

import * as filterSDK from '../src';
import { auth } from '../src';
import { McpAuthClient, AuthErrorCode, isAuthAvailable } from '../src/auth';

describe('Authentication Integration', () => {
  describe('Module Structure', () => {
    test('should export auth namespace from main SDK', () => {
      expect(filterSDK.auth).toBeDefined();
      expect(typeof filterSDK.auth).toBe('object');
    });

    test('should export auth types', () => {
      expect(filterSDK.auth.AuthErrorCode).toBeDefined();
      expect(filterSDK.auth.McpAuthClient).toBeDefined();
      expect(filterSDK.auth.AuthError).toBeDefined();
    });

    test('should export utility functions', () => {
      expect(typeof filterSDK.auth.isAuthAvailable).toBe('function');
      expect(typeof filterSDK.auth.errorCodeToString).toBe('function');
      expect(typeof filterSDK.auth.isSuccess).toBe('function');
    });
  });

  describe('Direct auth import', () => {
    test('should allow direct import from auth module', () => {
      expect(auth).toBeDefined();
      expect(auth.AuthErrorCode).toBeDefined();
      expect(auth.McpAuthClient).toBeDefined();
    });

    test('should have matching exports between namespace and direct import', () => {
      expect(auth.AuthErrorCode).toBe(filterSDK.auth.AuthErrorCode);
      expect(auth.McpAuthClient).toBe(filterSDK.auth.McpAuthClient);
    });
  });

  describe('Type definitions', () => {
    test('should create valid auth client config', () => {
      const config: filterSDK.auth.AuthClientConfig = {
        jwksUri: 'https://auth.example.com/.well-known/jwks.json',
        issuer: 'https://auth.example.com',
        cacheDuration: 3600,
        autoRefresh: true
      };

      expect(config.jwksUri).toBeDefined();
      expect(config.issuer).toBeDefined();
      expect(config.cacheDuration).toBe(3600);
    });

    test('should create validation options', () => {
      const options: filterSDK.auth.ValidationOptions = {
        scopes: 'read:users write:users',
        audience: 'api.example.com',
        clockSkew: 60
      };

      expect(options.scopes).toBeDefined();
      expect(options.audience).toBeDefined();
      expect(options.clockSkew).toBe(60);
    });
  });

  describe('Error handling', () => {
    test('should handle auth not available', () => {
      // This may or may not be available depending on the environment
      const available = isAuthAvailable();
      expect(typeof available).toBe('boolean');

      if (!available) {
        expect(() => {
          new McpAuthClient({
            jwksUri: 'https://example.com/jwks',
            issuer: 'https://example.com'
          });
        }).toThrow();
      }
    });

    test('should convert error codes to strings', () => {
      const errorStr = filterSDK.auth.errorCodeToString(AuthErrorCode.EXPIRED_TOKEN);
      expect(errorStr).toBe('Token expired');

      const successStr = filterSDK.auth.errorCodeToString(AuthErrorCode.SUCCESS);
      expect(successStr).toBe('Success');
    });
  });

  describe('OAuth metadata types', () => {
    test('should create valid OAuth metadata', () => {
      const metadata: filterSDK.auth.OAuthMetadata = {
        issuer: 'https://auth.example.com',
        authorizationEndpoint: 'https://auth.example.com/authorize',
        tokenEndpoint: 'https://auth.example.com/token',
        jwksUri: 'https://auth.example.com/.well-known/jwks.json',
        responseTypesSupported: ['code', 'token'],
        subjectTypesSupported: ['public'],
        idTokenSigningAlgValuesSupported: ['RS256', 'ES256'],
        codeChallengeMethodsSupported: ['S256'],
        requirePkce: true
      };

      expect(metadata.issuer).toBeDefined();
      expect(metadata.responseTypesSupported).toContain('code');
      expect(metadata.codeChallengeMethodsSupported).toContain('S256');
      expect(metadata.requirePkce).toBe(true);
    });
  });

  describe('Integration with filter SDK', () => {
    test('filter SDK types should still be available', () => {
      expect(filterSDK.FilterResultCode).toBeDefined();
      expect(filterSDK.FilterDecision).toBeDefined();
      expect(filterSDK.TransportType).toBeDefined();
    });

    test('should not have naming conflicts', () => {
      // Check that auth exports don't conflict with filter exports
      expect(filterSDK.auth.AuthErrorCode).not.toBe(filterSDK.FilterResultCode);
      
      // Both should have their own error handling
      expect(typeof filterSDK.auth.AuthError).toBe('function');
      expect(typeof filterSDK.FilterDeniedError).toBe('function');
    });
  });

  describe('Token payload types', () => {
    test('should support standard JWT claims', () => {
      const payload: filterSDK.auth.TokenPayload = {
        subject: 'user123',
        issuer: 'https://auth.example.com',
        audience: 'api.example.com',
        scopes: 'read write',
        expiration: Math.floor(Date.now() / 1000) + 3600,
        notBefore: Math.floor(Date.now() / 1000),
        issuedAt: Math.floor(Date.now() / 1000),
        jwtId: 'unique-jwt-id',
        customClaims: {
          role: 'admin',
          department: 'engineering'
        }
      };

      expect(payload.subject).toBe('user123');
      expect(payload.customClaims?.role).toBe('admin');
    });
  });

  describe('WWW-Authenticate support', () => {
    test('should create WWW-Authenticate params', () => {
      const params: filterSDK.auth.WwwAuthenticateParams = {
        realm: 'api',
        scope: 'read:users',
        error: 'invalid_token',
        errorDescription: 'The access token expired',
        errorUri: 'https://docs.example.com/errors/invalid_token'
      };

      expect(params.realm).toBe('api');
      expect(params.error).toBe('invalid_token');
      expect(params.errorDescription).toContain('expired');
    });
  });

  describe('Type guards', () => {
    test('should identify success codes', () => {
      expect(filterSDK.auth.isSuccess(AuthErrorCode.SUCCESS)).toBe(true);
      expect(filterSDK.auth.isSuccess(AuthErrorCode.INVALID_TOKEN)).toBe(false);
    });

    test('should identify auth errors', () => {
      expect(filterSDK.auth.isAuthError(AuthErrorCode.EXPIRED_TOKEN)).toBe(true);
      expect(filterSDK.auth.isAuthError(AuthErrorCode.INSUFFICIENT_SCOPE)).toBe(true);
      expect(filterSDK.auth.isAuthError(AuthErrorCode.SUCCESS)).toBe(false);
    });
  });
});