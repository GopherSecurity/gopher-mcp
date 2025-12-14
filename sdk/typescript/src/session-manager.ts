/**
 * @file session-manager.ts
 * @brief Session management for OAuth tokens to support MCP Inspector
 * 
 * MCP Inspector doesn't complete OAuth flow or send Authorization headers,
 * so we store tokens in sessions and use cookies for authentication.
 */

import type { Request, Response } from 'express';
import crypto from 'crypto';

// In-memory token storage (use Redis in production)
const tokenStore = new Map<string, {
  token: string;
  expiresAt: number;
  subject?: string;
  scopes?: string;
}>();

// Session cookie name
const SESSION_COOKIE_NAME = 'mcp_session';

/**
 * Generate a secure session ID
 */
export function generateSessionId(): string {
  return crypto.randomBytes(32).toString('hex');
}

/**
 * Store token in session after OAuth callback
 * Using very short expiry (60 seconds) to force re-authentication on reconnect
 */
export function storeTokenInSession(sessionId: string, token: string, expiresIn: number = 3600, payload?: any): void {
  // Override with very short expiry for MCP Inspector
  const shortExpiresIn = 60; // 60 seconds only
  const expiresAt = Date.now() + (shortExpiresIn * 1000);
  tokenStore.set(sessionId, {
    token,
    expiresAt,
    subject: payload?.subject || payload?.sub,
    scopes: payload?.scopes || payload?.scope
  });
  
  console.log(`📝 Stored token in session ${sessionId}, expires at ${new Date(expiresAt).toISOString()} (60s expiry)`);
}

/**
 * Get token from session
 */
export function getTokenFromSession(sessionId: string): string | null {
  const session = tokenStore.get(sessionId);
  
  if (!session) {
    return null;
  }
  
  // Check if expired
  if (Date.now() > session.expiresAt) {
    console.log(`⏰ Session ${sessionId} expired`);
    tokenStore.delete(sessionId);
    return null;
  }
  
  console.log(`✅ Retrieved token from session ${sessionId}`);
  return session.token;
}

/**
 * Extract session ID from request cookies
 */
export function extractSessionId(req: Request): string | null {
  const cookies = req.headers.cookie;
  if (!cookies) return null;
  
  const sessionCookie = cookies.split(';')
    .map(c => c.trim())
    .find(c => c.startsWith(`${SESSION_COOKIE_NAME}=`));
  
  if (!sessionCookie) return null;
  
  return sessionCookie.split('=')[1] || null;
}

/**
 * Set session cookie in response
 * Using very short expiry (60 seconds) to force re-authentication on reconnect
 */
export function setSessionCookie(res: Response, sessionId: string, maxAge: number = 3600): void {
  // Override with very short expiry for MCP Inspector
  const shortMaxAge = 60; // 60 seconds only
  res.cookie(SESSION_COOKIE_NAME, sessionId, {
    httpOnly: true,
    secure: false, // Set to true in production with HTTPS
    sameSite: 'lax',
    maxAge: shortMaxAge * 1000, // Convert to milliseconds (60 seconds)
    path: '/'
  });
  
  console.log(`🍪 Set session cookie: ${sessionId} (expires in ${shortMaxAge}s)`);
}

/**
 * Clear session cookie
 */
export function clearSessionCookie(res: Response): void {
  res.clearCookie(SESSION_COOKIE_NAME);
  console.log('🗑️ Cleared session cookie');
}

/**
 * Clean up expired sessions
 */
export function cleanupExpiredSessions(): void {
  const now = Date.now();
  let cleaned = 0;
  
  for (const [sessionId, session] of tokenStore.entries()) {
    if (now > session.expiresAt) {
      tokenStore.delete(sessionId);
      cleaned++;
    }
  }
  
  if (cleaned > 0) {
    console.log(`🧹 Cleaned up ${cleaned} expired sessions`);
  }
}

// Run cleanup every 5 minutes
setInterval(cleanupExpiredSessions, 5 * 60 * 1000);

/**
 * Get all active sessions (for debugging)
 */
export function getActiveSessions(): any[] {
  return Array.from(tokenStore.entries()).map(([id, session]) => ({
    id: id.substring(0, 8) + '...',
    subject: session.subject,
    scopes: session.scopes,
    expiresAt: new Date(session.expiresAt).toISOString()
  }));
}