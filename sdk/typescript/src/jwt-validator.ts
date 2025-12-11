/**
 * Pure JavaScript JWT validation fallback
 * Used when C++ validation causes memory errors
 */

import { createRemoteJWKSet, jwtVerify } from 'jose';
import type { ValidationResult, TokenPayload } from './auth-types';
import { AuthErrorCode } from './auth-types';

/**
 * Simple JWT validator using jose library
 * This is a fallback when the C++ implementation has issues
 */
export class JWTValidator {
  private jwks: ReturnType<typeof createRemoteJWKSet> | null = null;
  private jwksUri: string;
  private issuer: string;
  
  constructor(jwksUri: string, issuer: string) {
    this.jwksUri = jwksUri;
    this.issuer = issuer;
  }
  
  /**
   * Initialize JWKS fetcher
   */
  private async initJWKS() {
    if (!this.jwks) {
      this.jwks = createRemoteJWKSet(new URL(this.jwksUri));
    }
  }
  
  /**
   * Validate JWT token
   */
  async validate(
    token: string, 
    options?: {
      audience?: string;
      scopes?: string;
    }
  ): Promise<ValidationResult> {
    try {
      await this.initJWKS();
      
      const verifyOptions: any = {
        issuer: this.issuer,
      };
      
      if (options?.audience) {
        verifyOptions.audience = options.audience;
      }
      
      const { payload } = await jwtVerify(token, this.jwks!, verifyOptions);
      
      // Check scopes if required
      if (options?.scopes) {
        const requiredScopes = options.scopes.split(' ');
        const tokenScopes = (payload.scope as string || '').split(' ');
        const hasAllScopes = requiredScopes.every(scope => tokenScopes.includes(scope));
        
        if (!hasAllScopes) {
          return {
            valid: false,
            errorCode: AuthErrorCode.INSUFFICIENT_SCOPE,
            errorMessage: 'Token missing required scopes'
          };
        }
      }
      
      return {
        valid: true,
        errorCode: 0,
        errorMessage: undefined
      };
      
    } catch (error: any) {
      console.log('JWT validation error:', error.message);
      
      return {
        valid: false,
        errorCode: AuthErrorCode.INVALID_TOKEN,
        errorMessage: error.message || 'Token validation failed'
      };
    }
  }
  
  /**
   * Extract payload from JWT without validation
   */
  extractPayload(token: string): TokenPayload {
    try {
      const parts = token.split('.');
      if (parts.length !== 3) {
        throw new Error('Invalid JWT format');
      }
      
      const base64Payload = parts[1];
      if (!base64Payload) {
        throw new Error('Invalid JWT structure');
      }
      
      const payload = JSON.parse(
        Buffer.from(base64Payload, 'base64url').toString()
      );
      
      return {
        issuer: payload.iss,
        subject: payload.sub,
        audience: Array.isArray(payload.aud) ? payload.aud : [payload.aud].filter(Boolean),
        expiration: payload.exp,
        notBefore: payload.nbf,
        issuedAt: payload.iat,
        jwtId: payload.jti,
        scopes: payload.scope ? payload.scope.split(' ') : []
      };
    } catch (error: any) {
      throw new Error(`Failed to extract token payload: ${error.message}`);
    }
  }
}