// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"io/ioutil"
	"net"
	"sync"
	"time"
)

// TcpTLSConfig configures TLS for TCP transport.
type TcpTLSConfig struct {
	Enabled            bool
	ServerName         string
	InsecureSkipVerify bool

	// Certificates
	CertFile       string
	KeyFile        string
	CAFile         string
	ClientCertFile string
	ClientKeyFile  string

	// Cipher suites
	CipherSuites []uint16
	MinVersion   uint16
	MaxVersion   uint16

	// Certificate rotation
	EnableRotation   bool
	RotationInterval time.Duration

	// Session resumption
	SessionCache tls.ClientSessionCache
}

// DefaultTcpTLSConfig returns default TLS configuration.
func DefaultTcpTLSConfig() TcpTLSConfig {
	return TcpTLSConfig{
		Enabled:            false,
		InsecureSkipVerify: false,
		MinVersion:         tls.VersionTLS12,
		MaxVersion:         tls.VersionTLS13,
		EnableRotation:     false,
		RotationInterval:   24 * time.Hour,
	}
}

// TLSManager manages TLS configuration and certificate rotation.
type TLSManager struct {
	config    TcpTLSConfig
	tlsConfig *tls.Config
	mu        sync.RWMutex
	stopCh    chan struct{}
}

// NewTLSManager creates a new TLS manager.
func NewTLSManager(config TcpTLSConfig) (*TLSManager, error) {
	tm := &TLSManager{
		config: config,
		stopCh: make(chan struct{}),
	}

	if err := tm.loadTLSConfig(); err != nil {
		return nil, err
	}

	if config.EnableRotation {
		go tm.watchCertificateRotation()
	}

	return tm, nil
}

// loadTLSConfig loads TLS configuration from files.
func (tm *TLSManager) loadTLSConfig() error {
	tlsConfig := &tls.Config{
		ServerName:         tm.config.ServerName,
		InsecureSkipVerify: tm.config.InsecureSkipVerify,
		MinVersion:         tm.config.MinVersion,
		MaxVersion:         tm.config.MaxVersion,
	}

	// Load CA certificate
	if tm.config.CAFile != "" {
		caCert, err := ioutil.ReadFile(tm.config.CAFile)
		if err != nil {
			return fmt.Errorf("failed to read CA file: %w", err)
		}

		caCertPool := x509.NewCertPool()
		if !caCertPool.AppendCertsFromPEM(caCert) {
			return fmt.Errorf("failed to parse CA certificate")
		}
		tlsConfig.RootCAs = caCertPool
	}

	// Load client certificate
	if tm.config.ClientCertFile != "" && tm.config.ClientKeyFile != "" {
		cert, err := tls.LoadX509KeyPair(tm.config.ClientCertFile, tm.config.ClientKeyFile)
		if err != nil {
			return fmt.Errorf("failed to load client certificate: %w", err)
		}
		tlsConfig.Certificates = []tls.Certificate{cert}
	}

	// Load server certificate (for server mode)
	if tm.config.CertFile != "" && tm.config.KeyFile != "" {
		cert, err := tls.LoadX509KeyPair(tm.config.CertFile, tm.config.KeyFile)
		if err != nil {
			return fmt.Errorf("failed to load server certificate: %w", err)
		}
		tlsConfig.Certificates = append(tlsConfig.Certificates, cert)
	}

	// Set cipher suites
	if len(tm.config.CipherSuites) > 0 {
		tlsConfig.CipherSuites = tm.config.CipherSuites
	}

	// Set session cache
	if tm.config.SessionCache != nil {
		tlsConfig.ClientSessionCache = tm.config.SessionCache
	}

	tm.mu.Lock()
	tm.tlsConfig = tlsConfig
	tm.mu.Unlock()

	return nil
}

// GetTLSConfig returns current TLS configuration.
func (tm *TLSManager) GetTLSConfig() *tls.Config {
	tm.mu.RLock()
	defer tm.mu.RUnlock()
	return tm.tlsConfig.Clone()
}

// UpgradeConnection upgrades existing connection to TLS.
func (tm *TLSManager) UpgradeConnection(conn net.Conn, isServer bool) (net.Conn, error) {
	tlsConfig := tm.GetTLSConfig()

	if isServer {
		return tls.Server(conn, tlsConfig), nil
	}

	return tls.Client(conn, tlsConfig), nil
}

// StartTLS performs STARTTLS upgrade on connection.
func (tm *TLSManager) StartTLS(conn net.Conn, isServer bool) (net.Conn, error) {
	// Send/receive STARTTLS command (protocol-specific)
	// For now, just upgrade the connection
	return tm.UpgradeConnection(conn, isServer)
}

// watchCertificateRotation monitors for certificate changes.
func (tm *TLSManager) watchCertificateRotation() {
	ticker := time.NewTicker(tm.config.RotationInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			if err := tm.reloadCertificates(); err != nil {
				// Log error but continue
				continue
			}
		case <-tm.stopCh:
			return
		}
	}
}

// reloadCertificates reloads certificates from disk.
func (tm *TLSManager) reloadCertificates() error {
	return tm.loadTLSConfig()
}

// Stop stops certificate rotation monitoring.
func (tm *TLSManager) Stop() {
	close(tm.stopCh)
}

// VerifyCertificate verifies peer certificate.
func VerifyCertificate(rawCerts [][]byte, verifiedChains [][]*x509.Certificate) error {
	if len(rawCerts) == 0 {
		return fmt.Errorf("no certificates provided")
	}

	cert, err := x509.ParseCertificate(rawCerts[0])
	if err != nil {
		return fmt.Errorf("failed to parse certificate: %w", err)
	}

	// Check certificate validity
	now := time.Now()
	if now.Before(cert.NotBefore) {
		return fmt.Errorf("certificate not yet valid")
	}
	if now.After(cert.NotAfter) {
		return fmt.Errorf("certificate expired")
	}

	// Additional custom verification can be added here

	return nil
}

// GetSupportedCipherSuites returns recommended cipher suites.
func GetSupportedCipherSuites() []uint16 {
	return []uint16{
		tls.TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
		tls.TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
		tls.TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
		tls.TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		tls.TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305,
		tls.TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305,
	}
}
