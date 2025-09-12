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
type RateLimitConfig struct {
	Algorithm         string                          // token-bucket, sliding-window, fixed-window
	RequestsPerSecond int                             // Rate limit
	BurstSize         int                             // Maximum burst
	KeyExtractor      func(context.Context) string    // Extract key from context
	WindowSize        time.Duration                   // Window duration
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