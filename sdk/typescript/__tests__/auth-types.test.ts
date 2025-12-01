/**
 * @file auth-types.test.ts
 * @brief Tests for authentication type definitions
 */

import {
  AuthErrorCode,
  ValidationResult,
  TokenPayload,
  AuthClientConfig,
  isSuccess,
  isAuthError,
  errorCodeToString,
  ValidationOptions,
  OAuthMetadata,
  JsonWebKey,
  WwwAuthenticateParams,
  OAuthError,
  ScopeValidationResult
} from '../src/auth-types';

describe('Auth Types', () => {
  describe('AuthErrorCode', () => {
    test('should have correct error code values', () => {
      expect(AuthErrorCode.SUCCESS).toBe(0);
      expect(AuthErrorCode.INVALID_TOKEN).toBe(-1000);
      expect(AuthErrorCode.EXPIRED_TOKEN).toBe(-1001);
      expect(AuthErrorCode.INVALID_SIGNATURE).toBe(-1002);
      expect(AuthErrorCode.INVALID_ISSUER).toBe(-1003);
      expect(AuthErrorCode.INVALID_AUDIENCE).toBe(-1004);
      expect(AuthErrorCode.INSUFFICIENT_SCOPE).toBe(-1005);
      expect(AuthErrorCode.JWKS_FETCH_FAILED).toBe(-1006);
      expect(AuthErrorCode.INVALID_KEY).toBe(-1007);
      expect(AuthErrorCode.NETWORK_ERROR).toBe(-1008);
      expect(AuthErrorCode.INVALID_CONFIG).toBe(-1009);
      expect(AuthErrorCode.OUT_OF_MEMORY).toBe(-1010);
      expect(AuthErrorCode.INVALID_PARAMETER).toBe(-1011);
      expect(AuthErrorCode.NOT_INITIALIZED).toBe(-1012);
      expect(AuthErrorCode.INTERNAL_ERROR).toBe(-1013);
    });
  });

  describe('Type Guards', () => {
    test('isSuccess should identify success code', () => {
      expect(isSuccess(AuthErrorCode.SUCCESS)).toBe(true);
      expect(isSuccess(AuthErrorCode.INVALID_TOKEN)).toBe(false);
      expect(isSuccess(AuthErrorCode.EXPIRED_TOKEN)).toBe(false);
    });

    test('isAuthError should identify auth errors', () => {
      expect(isAuthError(AuthErrorCode.INVALID_TOKEN)).toBe(true);
      expect(isAuthError(AuthErrorCode.EXPIRED_TOKEN)).toBe(true);
      expect(isAuthError(AuthErrorCode.INSUFFICIENT_SCOPE)).toBe(true);
      expect(isAuthError(AuthErrorCode.SUCCESS)).toBe(false);
    });
  });

  describe('errorCodeToString', () => {
    test('should convert error codes to strings', () => {
      expect(errorCodeToString(AuthErrorCode.SUCCESS)).toBe('Success');
      expect(errorCodeToString(AuthErrorCode.INVALID_TOKEN)).toBe('Invalid token');
      expect(errorCodeToString(AuthErrorCode.EXPIRED_TOKEN)).toBe('Token expired');
      expect(errorCodeToString(AuthErrorCode.INVALID_SIGNATURE)).toBe('Invalid signature');
      expect(errorCodeToString(AuthErrorCode.INSUFFICIENT_SCOPE)).toBe('Insufficient scope');
      expect(errorCodeToString(999 as AuthErrorCode)).toContain('Unknown error');
    });
  });

  describe('ValidationResult', () => {
    test('should create valid result', () => {
      const result: ValidationResult = {
        valid: true,
        errorCode: AuthErrorCode.SUCCESS
      };
      expect(result.valid).toBe(true);
      expect(result.errorCode).toBe(AuthErrorCode.SUCCESS);
    });

    test('should create invalid result with error', () => {
      const result: ValidationResult = {
        valid: false,
        errorCode: AuthErrorCode.EXPIRED_TOKEN,
        errorMessage: 'Token has expired'
      };
      expect(result.valid).toBe(false);
      expect(result.errorCode).toBe(AuthErrorCode.EXPIRED_TOKEN);
      expect(result.errorMessage).toBe('Token has expired');
    });
  });

  describe('TokenPayload', () => {
    test('should create token payload with standard claims', () => {
      const payload: TokenPayload = {
        subject: 'user123',
        issuer: 'https://auth.example.com',
        audience: 'api.example.com',
        scopes: 'read write',
        expiration: 1234567890,
        notBefore: 1234567800,
        issuedAt: 1234567800,
        jwtId: 'unique-id-123'
      };
      
      expect(payload.subject).toBe('user123');
      expect(payload.issuer).toBe('https://auth.example.com');
      expect(payload.scopes).toBe('read write');
      expect(payload.expiration).toBe(1234567890);
    });

    test('should support custom claims', () => {
      const payload: TokenPayload = {
        subject: 'user123',
        customClaims: {
          role: 'admin',
          department: 'engineering'
        }
      };
      
      expect(payload.customClaims).toBeDefined();
      expect(payload.customClaims?.role).toBe('admin');
      expect(payload.customClaims?.department).toBe('engineering');
    });
  });

  describe('AuthClientConfig', () => {
    test('should create minimal config', () => {
      const config: AuthClientConfig = {
        jwksUri: 'https://auth.example.com/.well-known/jwks.json',
        issuer: 'https://auth.example.com'
      };
      
      expect(config.jwksUri).toBeDefined();
      expect(config.issuer).toBeDefined();
    });

    test('should create config with optional fields', () => {
      const config: AuthClientConfig = {
        jwksUri: 'https://auth.example.com/.well-known/jwks.json',
        issuer: 'https://auth.example.com',
        cacheDuration: 3600,
        autoRefresh: true,
        requestTimeout: 30
      };
      
      expect(config.cacheDuration).toBe(3600);
      expect(config.autoRefresh).toBe(true);
      expect(config.requestTimeout).toBe(30);
    });
  });

  describe('ValidationOptions', () => {
    test('should create validation options', () => {
      const options: ValidationOptions = {
        scopes: 'read:users write:users',
        audience: 'api.example.com',
        clockSkew: 60
      };
      
      expect(options.scopes).toBe('read:users write:users');
      expect(options.audience).toBe('api.example.com');
      expect(options.clockSkew).toBe(60);
    });
  });

  describe('OAuthMetadata', () => {
    test('should create OAuth metadata with required fields', () => {
      const metadata: OAuthMetadata = {
        issuer: 'https://auth.example.com',
        authorizationEndpoint: 'https://auth.example.com/authorize',
        tokenEndpoint: 'https://auth.example.com/token',
        jwksUri: 'https://auth.example.com/.well-known/jwks.json',
        responseTypesSupported: ['code', 'token'],
        subjectTypesSupported: ['public'],
        idTokenSigningAlgValuesSupported: ['RS256', 'ES256']
      };
      
      expect(metadata.issuer).toBe('https://auth.example.com');
      expect(metadata.responseTypesSupported).toContain('code');
      expect(metadata.idTokenSigningAlgValuesSupported).toContain('RS256');
    });

    test('should support optional OAuth 2.1 fields', () => {
      const metadata: OAuthMetadata = {
        issuer: 'https://auth.example.com',
        authorizationEndpoint: 'https://auth.example.com/authorize',
        tokenEndpoint: 'https://auth.example.com/token',
        jwksUri: 'https://auth.example.com/.well-known/jwks.json',
        responseTypesSupported: ['code'],
        subjectTypesSupported: ['public'],
        idTokenSigningAlgValuesSupported: ['RS256'],
        codeChallengeMethodsSupported: ['S256', 'plain'],
        requirePkce: true
      };
      
      expect(metadata.codeChallengeMethodsSupported).toContain('S256');
      expect(metadata.requirePkce).toBe(true);
    });
  });

  describe('JsonWebKey', () => {
    test('should create RSA key', () => {
      const key: JsonWebKey = {
        kid: 'rsa-key-1',
        kty: 'RSA',
        use: 'sig',
        alg: 'RS256',
        n: 'modulus',
        e: 'AQAB'
      };
      
      expect(key.kty).toBe('RSA');
      expect(key.alg).toBe('RS256');
      expect(key.n).toBe('modulus');
      expect(key.e).toBe('AQAB');
    });

    test('should create EC key', () => {
      const key: JsonWebKey = {
        kid: 'ec-key-1',
        kty: 'EC',
        use: 'sig',
        alg: 'ES256',
        crv: 'P-256',
        x: 'x-coordinate',
        y: 'y-coordinate'
      };
      
      expect(key.kty).toBe('EC');
      expect(key.alg).toBe('ES256');
      expect(key.crv).toBe('P-256');
      expect(key.x).toBe('x-coordinate');
      expect(key.y).toBe('y-coordinate');
    });
  });

  describe('WwwAuthenticateParams', () => {
    test('should create WWW-Authenticate parameters', () => {
      const params: WwwAuthenticateParams = {
        realm: 'api',
        scope: 'read:users',
        error: 'invalid_token',
        errorDescription: 'The token has expired'
      };
      
      expect(params.realm).toBe('api');
      expect(params.scope).toBe('read:users');
      expect(params.error).toBe('invalid_token');
      expect(params.errorDescription).toBe('The token has expired');
    });
  });

  describe('OAuthError', () => {
    test('should create OAuth error', () => {
      const error: OAuthError = {
        error: 'invalid_request',
        errorDescription: 'Missing required parameter',
        errorUri: 'https://docs.example.com/errors/invalid_request',
        statusCode: 400
      };
      
      expect(error.error).toBe('invalid_request');
      expect(error.errorDescription).toBe('Missing required parameter');
      expect(error.statusCode).toBe(400);
    });
  });

  describe('ScopeValidationResult', () => {
    test('should create valid scope result', () => {
      const result: ScopeValidationResult = {
        valid: true
      };
      
      expect(result.valid).toBe(true);
    });

    test('should create invalid scope result with details', () => {
      const result: ScopeValidationResult = {
        valid: false,
        missingScopes: ['write:users'],
        extraScopes: ['admin:system']
      };
      
      expect(result.valid).toBe(false);
      expect(result.missingScopes).toContain('write:users');
      expect(result.extraScopes).toContain('admin:system');
    });
  });
});