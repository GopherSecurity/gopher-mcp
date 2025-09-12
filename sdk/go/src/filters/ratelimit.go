// Package filters provides built-in filters for the MCP Filter SDK.
package filters

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/GopherSecurity/gopher-mcp/src/core"
	"github.com/GopherSecurity/gopher-mcp/src/types"
)

// RateLimiter is the interface for different rate limiting algorithms.
type RateLimiter interface {
	TryAcquire(n int) bool
	LastAccess() time.Time
}

// TokenBucket implements token bucket rate limiting algorithm.
type TokenBucket struct {
	// Current number of tokens
	tokens float64
	
	// Maximum token capacity
	capacity float64
	
	// Token refill rate per second
	refillRate float64
	
	// Last refill timestamp
	lastRefill time.Time
	
	// Synchronization
	mu sync.Mutex
}

// NewTokenBucket creates a new token bucket rate limiter.
func NewTokenBucket(capacity float64, refillRate float64) *TokenBucket {
	return &TokenBucket{
		tokens:     capacity,
		capacity:   capacity,
		refillRate: refillRate,
		lastRefill: time.Now(),
	}
}

// TryAcquire attempts to acquire n tokens from the bucket.
// Returns true if successful, false if insufficient tokens.
func (tb *TokenBucket) TryAcquire(n int) bool {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	
	// Refill tokens based on elapsed time
	now := time.Now()
	elapsed := now.Sub(tb.lastRefill).Seconds()
	tb.lastRefill = now
	
	// Add tokens based on refill rate
	tokensToAdd := elapsed * tb.refillRate
	tb.tokens = tb.tokens + tokensToAdd
	
	// Cap at maximum capacity
	if tb.tokens > tb.capacity {
		tb.tokens = tb.capacity
	}
	
	// Check if we have enough tokens
	if tb.tokens >= float64(n) {
		tb.tokens -= float64(n)
		return true
	}
	
	return false
}

// LastAccess returns the last time the bucket was accessed.
func (tb *TokenBucket) LastAccess() time.Time {
	tb.mu.Lock()
	defer tb.mu.Unlock()
	return tb.lastRefill
}

// RateLimitStatistics tracks rate limiting metrics.
type RateLimitStatistics struct {
	TotalRequests   uint64
	AllowedRequests uint64
	DeniedRequests  uint64
	ActiveLimiters  int
	ByKeyStats      map[string]*KeyStatistics
}

// KeyStatistics tracks per-key rate limit metrics.
type KeyStatistics struct {
	Allowed uint64
	Denied  uint64
	LastSeen time.Time
}

// RateLimitConfig configures the rate limiting behavior.
// Supports multiple algorithms for different use cases.
type RateLimitConfig struct {
	// Algorithm specifies the rate limiting algorithm to use.
	// Options: "token-bucket", "sliding-window", "fixed-window"
	Algorithm string
	
	// RequestsPerSecond defines the sustained request rate.
	RequestsPerSecond int
	
	// BurstSize defines the maximum burst capacity.
	// Only used with token-bucket algorithm.
	BurstSize int
	
	// KeyExtractor extracts the rate limit key from context.
	// If nil, a global rate limit is applied.
	KeyExtractor func(context.Context) string
	
	// WindowSize defines the time window for rate limiting.
	// Used with sliding-window and fixed-window algorithms.
	WindowSize time.Duration
}

// RateLimitFilter implements rate limiting with multiple algorithms.
type RateLimitFilter struct {
	*FilterBase
	
	// Rate limiters per key
	limiters sync.Map // map[string]RateLimiter
	
	// Configuration
	config RateLimitConfig
	
	// Cleanup timer
	cleanupTicker *time.Ticker
	
	// Statistics
	stats RateLimitStatistics
	
	// Synchronization
	statsMu sync.RWMutex
}

// NewRateLimitFilter creates a new rate limit filter.
func NewRateLimitFilter(maxRequests int, window time.Duration) *RateLimitFilter {
	f := &RateLimitFilter{
		maxRequests: maxRequests,
		window:      window,
		burstSize:   maxRequests * 2, // Default burst is 2x normal rate
		tokens:      float64(maxRequests),
		lastCheck:   time.Now(),
	}
	f.SetName("rate-limit")
	f.SetType("security")
	return f
}

// SetBurstSize sets the maximum burst size.
func (f *RateLimitFilter) SetBurstSize(size int) {
	f.burstSize = size
}

// Process implements the Filter interface.
func (f *RateLimitFilter) Process(ctx context.Context, data []byte) (*types.FilterResult, error) {

	// Check rate limit
	if !f.allowRequest() {
		return types.ErrorResult(
			fmt.Errorf("rate limit exceeded"),
			types.TooManyRequests,
		), nil
	}

	// Track processing
	startTime := time.Now()
	defer func() {
		duration := time.Since(startTime).Microseconds()
		_ = duration // Statistics tracking would go here
	}()

	// Pass through
	return types.ContinueWith(data), nil
}

// allowRequest checks if a request is allowed under the rate limit.
func (f *RateLimitFilter) allowRequest() bool {
	f.mu.Lock()
	defer f.mu.Unlock()

	now := time.Now()
	elapsed := now.Sub(f.lastCheck)
	f.lastCheck = now

	// Refill tokens based on elapsed time
	tokensToAdd := elapsed.Seconds() * (float64(f.maxRequests) / f.window.Seconds())
	f.tokens += tokensToAdd

	// Cap at burst size
	if f.tokens > float64(f.burstSize) {
		f.tokens = float64(f.burstSize)
	}

	// Check if we have tokens available
	if f.tokens >= 1.0 {
		f.tokens--
		return true
	}

	return false
}

// GetRemainingTokens returns the current number of available tokens.
func (f *RateLimitFilter) GetRemainingTokens() float64 {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.tokens
}

// Reset resets the rate limiter state.
func (f *RateLimitFilter) Reset() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.tokens = float64(f.maxRequests)
	f.lastCheck = time.Now()
}