/**
 * @file auth-types.ts
 * @brief TypeScript type definitions for authentication module
 */

/**
 * Authentication error codes matching C API
 */
export enum AuthErrorCode {
  SUCCESS = 0,
  INVALID_TOKEN = -1000,
  EXPIRED_TOKEN = -1001,
  INVALID_SIGNATURE = -1002,
  INVALID_ISSUER = -1003,
  INVALID_AUDIENCE = -1004,
  INSUFFICIENT_SCOPE = -1005,
  JWKS_FETCH_FAILED = -1006,
  INVALID_KEY = -1007,
  NETWORK_ERROR = -1008,
  INVALID_CONFIG = -1009,
  OUT_OF_MEMORY = -1010,
  INVALID_PARAMETER = -1011,
  NOT_INITIALIZED = -1012,
  INTERNAL_ERROR = -1013
}

/**
 * JWT validation result
 */
export interface ValidationResult {
  /** Whether the token validation succeeded */
  valid: boolean;
  /** Error code if validation failed */
  errorCode: AuthErrorCode;
  /** Human-readable error message */
  errorMessage?: string;
}

/**
 * Token validation options
 */
export interface ValidationOptions {
  /** Required scopes (space-separated) */
  scopes?: string;
  /** Expected audience */
  audience?: string;
  /** Clock skew tolerance in seconds */
  clockSkew?: number;
}

/**
 * JWT token payload
 */
export interface TokenPayload {
  /** Subject (sub claim) */
  subject?: string;
  /** Issuer (iss claim) */
  issuer?: string;
  /** Audience (aud claim) */
  audience?: string;
  /** Scopes (scope claim) */
  scopes?: string;
  /** Expiration timestamp (exp claim) */
  expiration?: number;
  /** Not before timestamp (nbf claim) */
  notBefore?: number;
  /** Issued at timestamp (iat claim) */
  issuedAt?: number;
  /** JWT ID (jti claim) */
  jwtId?: string;
  /** Custom claims */
  customClaims?: Record<string, any>;
}

/**
 * Authentication client configuration
 */
export interface AuthClientConfig {
  /** JWKS endpoint URI */
  jwksUri: string;
  /** Expected token issuer */
  issuer: string;
  /** Cache duration in seconds */
  cacheDuration?: number;
  /** Enable auto-refresh of JWKS */
  autoRefresh?: boolean;
  /** Request timeout in seconds */
  requestTimeout?: number;
}

/**
 * OAuth 2.0 metadata (RFC 8414)
 */
export interface OAuthMetadata {
  /** Issuer identifier */
  issuer: string;
  /** Authorization endpoint URL */
  authorizationEndpoint: string;
  /** Token endpoint URL */
  tokenEndpoint: string;
  /** JWKS endpoint URL */
  jwksUri: string;
  /** Supported response types */
  responseTypesSupported: string[];
  /** Supported subject types */
  subjectTypesSupported: string[];
  /** Supported signing algorithms */
  idTokenSigningAlgValuesSupported: string[];
  /** UserInfo endpoint (optional) */
  userinfoEndpoint?: string;
  /** Client registration endpoint (optional) */
  registrationEndpoint?: string;
  /** Supported scopes (optional) */
  scopesSupported?: string[];
  /** Supported claims (optional) */
  claimsSupported?: string[];
  /** Token revocation endpoint (optional) */
  revocationEndpoint?: string;
  /** Token introspection endpoint (optional) */
  introspectionEndpoint?: string;
  /** Supported response modes (optional) */
  responseModesSupported?: string[];
  /** Supported grant types (optional) */
  grantTypesSupported?: string[];
  /** PKCE code challenge methods (optional) */
  codeChallengeMethodsSupported?: string[];
  /** Whether PKCE is required (optional) */
  requirePkce?: boolean;
}

/**
 * WWW-Authenticate header parameters
 */
export interface WwwAuthenticateParams {
  /** Authentication realm */
  realm: string;
  /** Required scope (optional) */
  scope?: string;
  /** Error code (optional) */
  error?: string;
  /** Error description (optional) */
  errorDescription?: string;
  /** Error URI (optional) */
  errorUri?: string;
}

/**
 * OAuth error response
 */
export interface OAuthError {
  /** Error code */
  error: string;
  /** Error description (optional) */
  errorDescription?: string;
  /** Error documentation URI (optional) */
  errorUri?: string;
  /** HTTP status code (optional) */
  statusCode?: number;
}

/**
 * JWKS response
 */
export interface JwksResponse {
  /** Array of JSON Web Keys */
  keys: JsonWebKey[];
}

/**
 * JSON Web Key
 */
export interface JsonWebKey {
  /** Key ID */
  kid: string;
  /** Key type (RSA, EC, etc.) */
  kty: string;
  /** Key use (sig, enc) */
  use?: string;
  /** Algorithm */
  alg?: string;
  /** RSA modulus */
  n?: string;
  /** RSA exponent */
  e?: string;
  /** EC curve */
  crv?: string;
  /** EC x coordinate */
  x?: string;
  /** EC y coordinate */
  y?: string;
  /** X.509 certificate chain */
  x5c?: string[];
  /** X.509 thumbprint */
  x5t?: string;
}

/**
 * Scope validation result
 */
export interface ScopeValidationResult {
  /** Whether all required scopes are present */
  valid: boolean;
  /** Missing scopes */
  missingScopes?: string[];
  /** Extra scopes */
  extraScopes?: string[];
}

/**
 * Auth library version info
 */
export interface AuthVersion {
  /** Version string */
  version: string;
  /** Major version number */
  major: number;
  /** Minor version number */
  minor: number;
  /** Patch version number */
  patch: number;
}

/**
 * Auth statistics
 */
export interface AuthStats {
  /** Total tokens validated */
  tokensValidated: number;
  /** Successful validations */
  validationSuccesses: number;
  /** Failed validations */
  validationFailures: number;
  /** JWKS cache hits */
  cacheHits: number;
  /** JWKS cache misses */
  cacheMisses: number;
  /** JWKS refresh count */
  refreshCount: number;
}

/**
 * Type guard to check if error code indicates success
 */
export function isSuccess(code: AuthErrorCode): boolean {
  return code === AuthErrorCode.SUCCESS;
}

/**
 * Type guard to check if error is authentication related
 */
export function isAuthError(code: AuthErrorCode): boolean {
  return code >= AuthErrorCode.INTERNAL_ERROR && code <= AuthErrorCode.INVALID_TOKEN;
}

/**
 * Convert error code to string
 */
export function errorCodeToString(code: AuthErrorCode): string {
  switch (code) {
    case AuthErrorCode.SUCCESS:
      return 'Success';
    case AuthErrorCode.INVALID_TOKEN:
      return 'Invalid token';
    case AuthErrorCode.EXPIRED_TOKEN:
      return 'Token expired';
    case AuthErrorCode.INVALID_SIGNATURE:
      return 'Invalid signature';
    case AuthErrorCode.INVALID_ISSUER:
      return 'Invalid issuer';
    case AuthErrorCode.INVALID_AUDIENCE:
      return 'Invalid audience';
    case AuthErrorCode.INSUFFICIENT_SCOPE:
      return 'Insufficient scope';
    case AuthErrorCode.JWKS_FETCH_FAILED:
      return 'JWKS fetch failed';
    case AuthErrorCode.INVALID_KEY:
      return 'Invalid key';
    case AuthErrorCode.NETWORK_ERROR:
      return 'Network error';
    case AuthErrorCode.INVALID_CONFIG:
      return 'Invalid configuration';
    case AuthErrorCode.OUT_OF_MEMORY:
      return 'Out of memory';
    case AuthErrorCode.INVALID_PARAMETER:
      return 'Invalid parameter';
    case AuthErrorCode.NOT_INITIALIZED:
      return 'Library not initialized';
    case AuthErrorCode.INTERNAL_ERROR:
      return 'Internal error';
    default:
      return `Unknown error (${code})`;
  }
}

/**
 * Token Exchange options (RFC 8693)
 */
export interface TokenExchangeOptions {
  /** The Keycloak access token to exchange */
  subjectToken: string;
  /** The alias of the external IDP in Keycloak (e.g., "google", "github", "my-custom-idp") */
  requestedIssuer: string;
  /** Optional: requested token type (defaults to access_token) */
  requestedTokenType?: 'access_token' | 'refresh_token' | 'id_token';
  /** Optional: audience for the exchanged token */
  audience?: string;
  /** Optional: scopes to request for the exchanged token */
  scope?: string;
}

/**
 * Result of a token exchange operation
 */
export interface TokenExchangeResult {
  /** The exchanged access token from the external IDP */
  access_token: string;
  /** Token type (usually "Bearer") */
  token_type: string;
  /** Token expiration in seconds */
  expires_in?: number;
  /** The type of token issued */
  issued_token_type: string;
  /** Refresh token (if available) */
  refresh_token?: string;
  /** Scopes granted */
  scope?: string;
}

/**
 * Token Exchange error
 */
export class TokenExchangeError extends Error {
  public readonly errorCode: string;
  public readonly errorDescription?: string;

  constructor(errorCode: string, errorDescription?: string) {
    super(
      errorDescription
        ? `Token exchange failed: ${errorCode} - ${errorDescription}`
        : `Token exchange failed: ${errorCode}`
    );
    this.name = 'TokenExchangeError';
    this.errorCode = errorCode;
    this.errorDescription = errorDescription;
  }
}

// Re-export for convenience
export type {
  AuthErrorCode as ErrorCode,
  ValidationResult as Result,
  TokenPayload as Payload,
  AuthClientConfig as ClientConfig
};