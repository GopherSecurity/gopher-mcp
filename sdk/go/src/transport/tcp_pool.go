// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"context"
	"fmt"
	"net"
	"sync"
	"sync/atomic"
	"time"
)

// TcpConnectionPool manages a pool of TCP connections.
type TcpConnectionPool struct {
	config      PoolConfig
	connections []*PooledConnection
	available   chan *PooledConnection
	factory     ConnectionFactory
	stats       PoolStats
	closed      atomic.Bool
	mu          sync.RWMutex
}

// PoolConfig configures connection pool behavior.
type PoolConfig struct {
	MinConnections   int
	MaxConnections   int
	IdleTimeout      time.Duration
	MaxLifetime      time.Duration
	HealthCheckInterval time.Duration
	Address          string
}

// DefaultPoolConfig returns default pool configuration.
func DefaultPoolConfig() PoolConfig {
	return PoolConfig{
		MinConnections:      2,
		MaxConnections:      10,
		IdleTimeout:         5 * time.Minute,
		MaxLifetime:         30 * time.Minute,
		HealthCheckInterval: 30 * time.Second,
	}
}

// PooledConnection wraps a connection with metadata.
type PooledConnection struct {
	conn       net.Conn
	id         int
	created    time.Time
	lastUsed   time.Time
	useCount   int64
	healthy    bool
	inUse      bool
}

// ConnectionFactory creates new connections.
type ConnectionFactory func(ctx context.Context) (net.Conn, error)

// PoolStats contains pool statistics.
type PoolStats struct {
	TotalConnections   int
	ActiveConnections  int
	IdleConnections    int
	TotalRequests      int64
	FailedRequests     int64
	AverageWaitTime    time.Duration
}

// NewTcpConnectionPool creates a new connection pool.
func NewTcpConnectionPool(config PoolConfig, factory ConnectionFactory) (*TcpConnectionPool, error) {
	pool := &TcpConnectionPool{
		config:      config,
		connections: make([]*PooledConnection, 0, config.MaxConnections),
		available:   make(chan *PooledConnection, config.MaxConnections),
		factory:     factory,
	}
	
	// Create initial connections
	for i := 0; i < config.MinConnections; i++ {
		conn, err := pool.createConnection(context.Background())
		if err != nil {
			return nil, fmt.Errorf("failed to create initial connection: %w", err)
		}
		pool.connections = append(pool.connections, conn)
		pool.available <- conn
	}
	
	// Start health checking
	go pool.healthCheckLoop()
	
	// Start idle timeout checking
	go pool.idleTimeoutLoop()
	
	return pool, nil
}

// Get retrieves a connection from the pool.
func (pool *TcpConnectionPool) Get(ctx context.Context) (*PooledConnection, error) {
	if pool.closed.Load() {
		return nil, ErrPoolClosed
	}
	
	atomic.AddInt64(&pool.stats.TotalRequests, 1)
	startTime := time.Now()
	
	select {
	case conn := <-pool.available:
		// Check if connection is still valid
		if pool.isConnectionValid(conn) {
			conn.inUse = true
			conn.lastUsed = time.Now()
			atomic.AddInt64(&conn.useCount, 1)
			pool.updateWaitTime(time.Since(startTime))
			return conn, nil
		}
		// Connection invalid, create new one
		pool.removeConnection(conn)
		
	case <-ctx.Done():
		atomic.AddInt64(&pool.stats.FailedRequests, 1)
		return nil, ctx.Err()
		
	default:
		// No available connections, try to create new one
		if len(pool.connections) < pool.config.MaxConnections {
			conn, err := pool.createConnection(ctx)
			if err != nil {
				atomic.AddInt64(&pool.stats.FailedRequests, 1)
				return nil, err
			}
			conn.inUse = true
			pool.updateWaitTime(time.Since(startTime))
			return conn, nil
		}
		
		// Wait for available connection
		select {
		case conn := <-pool.available:
			if pool.isConnectionValid(conn) {
				conn.inUse = true
				conn.lastUsed = time.Now()
				atomic.AddInt64(&conn.useCount, 1)
				pool.updateWaitTime(time.Since(startTime))
				return conn, nil
			}
			pool.removeConnection(conn)
			return pool.Get(ctx) // Retry
			
		case <-ctx.Done():
			atomic.AddInt64(&pool.stats.FailedRequests, 1)
			return nil, ctx.Err()
		}
	}
	
	// Fallback: create new connection
	return pool.createConnection(ctx)
}

// Put returns a connection to the pool.
func (pool *TcpConnectionPool) Put(conn *PooledConnection) {
	if pool.closed.Load() {
		conn.conn.Close()
		return
	}
	
	conn.inUse = false
	conn.lastUsed = time.Now()
	
	if pool.isConnectionValid(conn) {
		select {
		case pool.available <- conn:
			// Successfully returned to pool
		default:
			// Pool is full, close connection
			conn.conn.Close()
			pool.removeConnection(conn)
		}
	} else {
		// Invalid connection, remove from pool
		conn.conn.Close()
		pool.removeConnection(conn)
	}
}

// createConnection creates a new pooled connection.
func (pool *TcpConnectionPool) createConnection(ctx context.Context) (*PooledConnection, error) {
	conn, err := pool.factory(ctx)
	if err != nil {
		return nil, err
	}
	
	pooledConn := &PooledConnection{
		conn:     conn,
		id:       len(pool.connections),
		created:  time.Now(),
		lastUsed: time.Now(),
		healthy:  true,
	}
	
	pool.mu.Lock()
	pool.connections = append(pool.connections, pooledConn)
	pool.mu.Unlock()
	
	return pooledConn, nil
}

// removeConnection removes a connection from the pool.
func (pool *TcpConnectionPool) removeConnection(conn *PooledConnection) {
	pool.mu.Lock()
	defer pool.mu.Unlock()
	
	for i, c := range pool.connections {
		if c.id == conn.id {
			pool.connections = append(pool.connections[:i], pool.connections[i+1:]...)
			break
		}
	}
}

// isConnectionValid checks if a connection is still valid.
func (pool *TcpConnectionPool) isConnectionValid(conn *PooledConnection) bool {
	// Check lifetime
	if time.Since(conn.created) > pool.config.MaxLifetime {
		return false
	}
	
	// Check health
	if !conn.healthy {
		return false
	}
	
	return true
}

// healthCheckLoop periodically checks connection health.
func (pool *TcpConnectionPool) healthCheckLoop() {
	ticker := time.NewTicker(pool.config.HealthCheckInterval)
	defer ticker.Stop()
	
	for !pool.closed.Load() {
		<-ticker.C
		pool.checkHealth()
	}
}

// checkHealth checks health of all connections.
func (pool *TcpConnectionPool) checkHealth() {
	pool.mu.RLock()
	connections := make([]*PooledConnection, len(pool.connections))
	copy(connections, pool.connections)
	pool.mu.RUnlock()
	
	for _, conn := range connections {
		if !conn.inUse {
			// Perform health check (simple write test)
			conn.conn.SetWriteDeadline(time.Now().Add(1 * time.Second))
			_, err := conn.conn.Write([]byte{})
			conn.conn.SetWriteDeadline(time.Time{})
			
			conn.healthy = err == nil
		}
	}
}

// idleTimeoutLoop removes idle connections.
func (pool *TcpConnectionPool) idleTimeoutLoop() {
	ticker := time.NewTicker(pool.config.IdleTimeout / 2)
	defer ticker.Stop()
	
	for !pool.closed.Load() {
		<-ticker.C
		pool.removeIdleConnections()
	}
}

// removeIdleConnections removes connections that have been idle too long.
func (pool *TcpConnectionPool) removeIdleConnections() {
	pool.mu.RLock()
	connections := make([]*PooledConnection, len(pool.connections))
	copy(connections, pool.connections)
	pool.mu.RUnlock()
	
	for _, conn := range connections {
		if !conn.inUse && time.Since(conn.lastUsed) > pool.config.IdleTimeout {
			// Keep minimum connections
			if len(pool.connections) > pool.config.MinConnections {
				conn.conn.Close()
				pool.removeConnection(conn)
			}
		}
	}
}

// updateWaitTime updates average wait time statistic.
func (pool *TcpConnectionPool) updateWaitTime(duration time.Duration) {
	// Simple moving average
	currentAvg := pool.stats.AverageWaitTime
	pool.stats.AverageWaitTime = (currentAvg + duration) / 2
}

// GetStats returns pool statistics.
func (pool *TcpConnectionPool) GetStats() PoolStats {
	pool.mu.RLock()
	defer pool.mu.RUnlock()
	
	stats := pool.stats
	stats.TotalConnections = len(pool.connections)
	
	active := 0
	for _, conn := range pool.connections {
		if conn.inUse {
			active++
		}
	}
	stats.ActiveConnections = active
	stats.IdleConnections = stats.TotalConnections - active
	
	return stats
}

// Close closes all connections and stops the pool.
func (pool *TcpConnectionPool) Close() error {
	if !pool.closed.CompareAndSwap(false, true) {
		return nil
	}
	
	// Close all connections
	pool.mu.Lock()
	defer pool.mu.Unlock()
	
	for _, conn := range pool.connections {
		conn.conn.Close()
	}
	
	close(pool.available)
	pool.connections = nil
	
	return nil
}

// LoadBalance selects a connection using round-robin.
type LoadBalancer struct {
	pool    *TcpConnectionPool
	current atomic.Uint64
}

// NewLoadBalancer creates a new load balancer.
func NewLoadBalancer(pool *TcpConnectionPool) *LoadBalancer {
	return &LoadBalancer{
		pool: pool,
	}
}

// GetConnection gets a load-balanced connection.
func (lb *LoadBalancer) GetConnection(ctx context.Context) (*PooledConnection, error) {
	return lb.pool.Get(ctx)
}

// Error definitions
var (
	ErrPoolClosed = &TransportError{
		Code:    "POOL_CLOSED",
		Message: "connection pool is closed",
	}
)