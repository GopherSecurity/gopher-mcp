/**
 * @file session-manager.ts
 * @brief Session management for OAuth tokens
 * 
 * This is a workaround for MCP Inspector which doesn't properly handle OAuth tokens.
 * It connects successfully but doesn't send the token back in subsequent requests.
 */

import * as crypto from 'crypto';

interface SessionData {
  token: string;
  expiresAt: number;
  payload?: any;
}

// In-memory session storage (for development/testing)
// In production, use Redis or another persistent store
const sessions = new Map<string, SessionData>();

/**
 * Generate a cryptographically secure session ID
 */
export function generateSessionId(): string {
  return crypto.randomBytes(32).toString('hex');
}

/**
 * Store token in session
 */
export function storeTokenInSession(sessionId: string, token: string, expiresIn: number = 3600, payload?: any): void {
  // Override with very short expiry for MCP Inspector
  const shortExpiresIn = 60; // 60 seconds only
  const expiresAt = Date.now() + (shortExpiresIn * 1000);
  
  sessions.set(sessionId, {
    token,
    expiresAt,
    payload
  });
  
  console.log(`🍪 Session ${sessionId.substring(0, 8)}... created, expires in ${shortExpiresIn}s (MCP Inspector workaround)`);
  
  // Clean up expired sessions periodically
  cleanupExpiredSessions();
}

/**
 * Get token from session
 */
export function getTokenFromSession(sessionId: string): string | undefined {
  const session = sessions.get(sessionId);
  
  if (!session) {
    return undefined;
  }
  
  // Check if session has expired
  if (Date.now() > session.expiresAt) {
    sessions.delete(sessionId);
    console.log(`🍪 Session ${sessionId.substring(0, 8)}... expired`);
    return undefined;
  }
  
  return session.token;
}

/**
 * Clean up expired sessions
 */
function cleanupExpiredSessions(): void {
  const now = Date.now();
  for (const [sessionId, data] of sessions.entries()) {
    if (now > data.expiresAt) {
      sessions.delete(sessionId);
    }
  }
}

/**
 * Extract session ID from request
 * @param req Express-like request object or headers object
 */
export function extractSessionId(req: any): string | undefined {
  // Try cookie header first
  const cookieHeader = req.headers?.cookie || req.cookie;
  if (cookieHeader) {
    const cookies = parseCookies(cookieHeader);
    if (cookies.mcp_session) {
      return cookies.mcp_session;
    }
  }
  
  // Try x-session-id header
  if (req.headers?.['x-session-id']) {
    return req.headers['x-session-id'];
  }
  
  return undefined;
}

/**
 * Parse cookie string
 */
function parseCookies(cookieStr: string): Record<string, string> {
  const cookies: Record<string, string> = {};
  cookieStr.split(';').forEach(cookie => {
    const [key, value] = cookie.trim().split('=');
    if (key && value) {
      cookies[key] = value;
    }
  });
  return cookies;
}

/**
 * Set session cookie on response
 * @param res Express-like response object
 */
export function setSessionCookie(res: any, sessionId: string, maxAge: number = 3600): void {
  if (res && typeof res.setHeader === 'function') {
    // Use short expiry for MCP Inspector workaround
    const shortMaxAge = 60; // 60 seconds
    res.setHeader('Set-Cookie', `mcp_session=${sessionId}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${shortMaxAge}`);
  } else if (res && typeof res.cookie === 'function') {
    // Express-style response
    res.cookie('mcp_session', sessionId, {
      httpOnly: true,
      sameSite: 'lax',
      path: '/',
      maxAge: 60 * 1000 // 60 seconds in milliseconds
    });
  }
}

/**
 * Clear session
 */
export function clearSession(sessionId: string): void {
  sessions.delete(sessionId);
}

/**
 * Get all active sessions (for debugging)
 */
export function getActiveSessions(): number {
  cleanupExpiredSessions();
  return sessions.size;
}