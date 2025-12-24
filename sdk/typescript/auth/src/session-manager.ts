/**
 * @file session-manager.ts
 * @brief Enhanced session management with automatic token re-exchange
 * 
 * This implementation adds automatic token refresh/re-exchange when
 * external IDP tokens expire, matching the TypeScript-only SDK feature.
 */

import type { Request, Response } from 'express';
import * as crypto from 'crypto';
import type { McpAuthClient } from './mcp-auth-api.js';

/**
 * Token storage with expiration tracking
 */
interface StoredToken {
  // External IDP token
  externalToken: string;
  externalTokenType?: string;
  externalTokenExpiresAt: number;
  
  // Keycloak token for re-exchange
  kcToken: string;
  kcTokenExpiresAt?: number;
  
  // IDP information
  idpAlias: string;
  
  // User context
  subject?: string;
  scopes?: string;
  audience?: string;
  
  // Metadata
  createdAt: number;
  lastRefreshed?: number;
}

/**
 * Enhanced session manager with automatic token re-exchange
 */
export class EnhancedSessionManager {
  // In-memory token storage (use Redis in production)
  private tokenStore = new Map<string, StoredToken>();
  
  // Session cookie name
  private readonly SESSION_COOKIE_NAME = 'mcp_session';
  
  // Re-exchange buffer time (seconds before expiry)
  private readonly REFRESH_BUFFER_SECONDS = 30;
  
  // Auth client for token re-exchange
  private authClient?: McpAuthClient;
  
  // Cleanup interval handle
  private cleanupInterval?: NodeJS.Timeout;
  
  constructor(authClient?: McpAuthClient) {
    this.authClient = authClient;
    
    // Start cleanup interval
    this.cleanupInterval = setInterval(() => {
      this.cleanupExpiredSessions();
    }, 5 * 60 * 1000); // Every 5 minutes
  }
  
  /**
   * Generate a secure session ID
   */
  generateSessionId(): string {
    return crypto.randomBytes(32).toString('hex');
  }
  
  /**
   * Store tokens in session with expiration tracking
   */
  storeTokens(
    sessionId: string,
    externalToken: string,
    kcToken: string,
    idpAlias: string,
    expiresIn: number = 3600,
    payload?: any
  ): void {
    const now = Date.now();
    const externalTokenExpiresAt = now + (expiresIn * 1000);
    
    const storedToken: StoredToken = {
      externalToken,
      externalTokenType: 'Bearer',
      externalTokenExpiresAt,
      kcToken,
      idpAlias,
      subject: payload?.subject || payload?.sub,
      scopes: payload?.scopes || payload?.scope,
      audience: payload?.aud || payload?.audience,
      createdAt: now
    };
    
    // If we have the KC token expiry, store it
    if (payload?.exp) {
      storedToken.kcTokenExpiresAt = payload.exp * 1000; // Convert to milliseconds
    }
    
    this.tokenStore.set(sessionId, storedToken);
    
    console.log(`📝 Stored tokens in session ${sessionId}:`);
    console.log(`   External token expires at: ${new Date(externalTokenExpiresAt).toISOString()} (${expiresIn}s)`);
    console.log(`   KC token available for re-exchange: YES`);
    console.log(`   IDP: ${idpAlias}`);
  }
  
  /**
   * Get token from session with automatic re-exchange if expired
   */
  async getTokenFromSession(
    sessionId: string,
    forceRefresh: boolean = false
  ): Promise<{
    token: string;
    tokenType: string;
    needsRefresh: boolean;
  } | null> {
    const session = this.tokenStore.get(sessionId);
    
    if (!session) {
      console.log(`❌ No session found: ${sessionId}`);
      return null;
    }
    
    const now = Date.now();
    const bufferMs = this.REFRESH_BUFFER_SECONDS * 1000;
    const expiresWithBuffer = session.externalTokenExpiresAt - bufferMs;
    
    // Check if token is still valid (with buffer)
    if (!forceRefresh && expiresWithBuffer > now) {
      const remainingSecs = Math.round((session.externalTokenExpiresAt - now) / 1000);
      console.log(`✅ Using cached token for session ${sessionId} (expires in ${remainingSecs}s)`);
      
      return {
        token: session.externalToken,
        tokenType: session.externalTokenType || 'Bearer',
        needsRefresh: false
      };
    }
    
    // Token expired or about to expire - attempt re-exchange
    console.log(`⚠️ Token expired/expiring for session ${sessionId}, attempting re-exchange...`);
    
    // Check if KC token is still valid
    if (session.kcTokenExpiresAt && session.kcTokenExpiresAt <= now) {
      console.log(`❌ KC token also expired, cannot re-exchange`);
      this.tokenStore.delete(sessionId);
      return null;
    }
    
    // Attempt re-exchange if auth client is available
    if (this.authClient) {
      try {
        console.log(`🔄 Re-exchanging token with IDP: ${session.idpAlias}`);
        
        const result = await this.authClient.exchangeToken(
          session.kcToken,
          session.idpAlias,
          session.audience,
          session.scopes
        );
        
        // Update stored token
        const newExpiresAt = now + ((result.expires_in || 300) * 1000);
        session.externalToken = result.access_token;
        session.externalTokenType = result.token_type || 'Bearer';
        session.externalTokenExpiresAt = newExpiresAt;
        session.lastRefreshed = now;
        
        this.tokenStore.set(sessionId, session);
        
        console.log(`✅ Token re-exchanged successfully for session ${sessionId}`);
        console.log(`   New token expires at: ${new Date(newExpiresAt).toISOString()}`);
        
        return {
          token: result.access_token,
          tokenType: result.token_type || 'Bearer',
          needsRefresh: false
        };
      } catch (error: any) {
        console.error(`❌ Token re-exchange failed for session ${sessionId}: ${error.message}`);
        
        // Return expired token as fallback (API will return 401)
        return {
          token: session.externalToken,
          tokenType: session.externalTokenType || 'Bearer',
          needsRefresh: true
        };
      }
    }
    
    // No auth client available for re-exchange
    console.log(`⚠️ No auth client available for re-exchange`);
    
    // Return expired token as fallback
    return {
      token: session.externalToken,
      tokenType: session.externalTokenType || 'Bearer',
      needsRefresh: true
    };
  }
  
  /**
   * Update KC token for a session (when main token is refreshed)
   */
  updateKCToken(sessionId: string, newKCToken: string, payload?: any): void {
    const session = this.tokenStore.get(sessionId);
    if (!session) return;
    
    session.kcToken = newKCToken;
    if (payload?.exp) {
      session.kcTokenExpiresAt = payload.exp * 1000;
    }
    
    this.tokenStore.set(sessionId, session);
    console.log(`🔄 Updated KC token for session ${sessionId}`);
  }
  
  /**
   * Extract session ID from request cookies
   */
  extractSessionId(req: Request): string | null {
    const cookies = req.headers.cookie;
    if (!cookies) return null;
    
    const sessionCookie = cookies.split(';')
      .map(c => c.trim())
      .find(c => c.startsWith(`${this.SESSION_COOKIE_NAME}=`));
    
    if (!sessionCookie) return null;
    
    return sessionCookie.split('=')[1] || null;
  }
  
  /**
   * Set session cookie in response
   */
  setSessionCookie(res: Response, sessionId: string, maxAge: number = 3600): void {
    res.cookie(this.SESSION_COOKIE_NAME, sessionId, {
      httpOnly: true,
      secure: process.env.NODE_ENV === 'production',
      sameSite: 'lax',
      maxAge: maxAge * 1000, // Convert to milliseconds
      path: '/'
    });
    
    console.log(`🍪 Set session cookie: ${sessionId} (expires in ${maxAge}s)`);
  }
  
  /**
   * Clear session cookie
   */
  clearSessionCookie(res: Response): void {
    res.clearCookie(this.SESSION_COOKIE_NAME);
    console.log('🗑️ Cleared session cookie');
  }
  
  /**
   * Clean up expired sessions
   */
  cleanupExpiredSessions(): void {
    const now = Date.now();
    let cleaned = 0;
    
    for (const [sessionId, session] of this.tokenStore.entries()) {
      // Remove if both tokens are expired
      const kcExpired = session.kcTokenExpiresAt ? session.kcTokenExpiresAt <= now : false;
      const externalExpired = session.externalTokenExpiresAt <= now;
      
      if (kcExpired || (externalExpired && !session.kcToken)) {
        this.tokenStore.delete(sessionId);
        cleaned++;
      }
    }
    
    if (cleaned > 0) {
      console.log(`🧹 Cleaned up ${cleaned} expired sessions`);
    }
  }
  
  /**
   * Get session info (for debugging)
   */
  getSessionInfo(sessionId: string): any | null {
    const session = this.tokenStore.get(sessionId);
    if (!session) return null;
    
    const now = Date.now();
    return {
      id: sessionId.substring(0, 8) + '...',
      subject: session.subject,
      scopes: session.scopes,
      idpAlias: session.idpAlias,
      externalTokenExpiry: {
        expiresAt: new Date(session.externalTokenExpiresAt).toISOString(),
        remainingSecs: Math.round((session.externalTokenExpiresAt - now) / 1000),
        isExpired: session.externalTokenExpiresAt <= now
      },
      kcTokenExpiry: session.kcTokenExpiresAt ? {
        expiresAt: new Date(session.kcTokenExpiresAt).toISOString(),
        remainingSecs: Math.round((session.kcTokenExpiresAt - now) / 1000),
        isExpired: session.kcTokenExpiresAt <= now
      } : null,
      createdAt: new Date(session.createdAt).toISOString(),
      lastRefreshed: session.lastRefreshed ? new Date(session.lastRefreshed).toISOString() : null
    };
  }
  
  /**
   * Get all active sessions (for debugging)
   */
  getActiveSessions(): any[] {
    return Array.from(this.tokenStore.keys()).map(id => this.getSessionInfo(id)).filter(s => s !== null);
  }
  
  /**
   * Destroy the session manager (cleanup)
   */
  destroy(): void {
    if (this.cleanupInterval) {
      clearInterval(this.cleanupInterval);
      this.cleanupInterval = undefined;
    }
    this.tokenStore.clear();
  }
}

// Singleton instance
let sessionManager: EnhancedSessionManager | null = null;

/**
 * Initialize the enhanced session manager
 */
export function initializeEnhancedSessionManager(authClient?: McpAuthClient): EnhancedSessionManager {
  if (!sessionManager) {
    sessionManager = new EnhancedSessionManager(authClient);
  } else if (authClient && !sessionManager['authClient']) {
    // Update auth client if not set
    sessionManager['authClient'] = authClient;
  }
  return sessionManager;
}

/**
 * Get the enhanced session manager instance
 */
export function getEnhancedSessionManager(): EnhancedSessionManager {
  if (!sessionManager) {
    sessionManager = new EnhancedSessionManager();
  }
  return sessionManager;
}

// Legacy function exports for backward compatibility
const SESSION_COOKIE_NAME = 'mcp_session';

export function generateSessionId(): string {
  return getEnhancedSessionManager().generateSessionId();
}

export function storeTokenInSession(sessionId: string, token: string, expiresIn: number = 3600, payload?: any): void {
  // For backward compatibility - store as both external and KC token
  getEnhancedSessionManager().storeTokens(
    sessionId,
    token,
    token,
    'default',
    expiresIn,
    payload
  );
}

export function getTokenFromSession(sessionId: string): string | null {
  // Synchronous wrapper for backward compatibility
  const manager = getEnhancedSessionManager();
  const session = manager['tokenStore'].get(sessionId);
  
  if (!session) return null;
  
  const now = Date.now();
  if (session.externalTokenExpiresAt > now) {
    return session.externalToken;
  }
  
  return null;
}

export function extractSessionId(req: Request): string | null {
  return getEnhancedSessionManager().extractSessionId(req);
}

export function setSessionCookie(res: Response, sessionId: string, maxAge: number = 3600): void {
  getEnhancedSessionManager().setSessionCookie(res, sessionId, maxAge);
}

export function clearSessionCookie(res: Response): void {
  getEnhancedSessionManager().clearSessionCookie(res);
}

export function cleanupExpiredSessions(): void {
  getEnhancedSessionManager().cleanupExpiredSessions();
}

export function getActiveSessions(): number {
  return getEnhancedSessionManager().getActiveSessions().length;
}

// Export types for convenience
export type { StoredToken };