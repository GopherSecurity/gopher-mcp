// Package transport provides communication transports for the MCP Filter SDK.
package transport

import (
	"fmt"
	"net"
	"runtime"
	"syscall"
	"time"
)

// TcpKeepAlive manages TCP keep-alive settings.
type TcpKeepAlive struct {
	Enabled  bool
	Interval time.Duration
	Count    int
	Idle     time.Duration
}

// DefaultTcpKeepAlive returns default keep-alive settings.
func DefaultTcpKeepAlive() TcpKeepAlive {
	return TcpKeepAlive{
		Enabled:  true,
		Interval: 30 * time.Second,
		Count:    9,
		Idle:     30 * time.Second,
	}
}

// Configure applies keep-alive settings to connection.
func (ka *TcpKeepAlive) Configure(conn net.Conn) error {
	tcpConn, ok := conn.(*net.TCPConn)
	if !ok {
		return nil
	}
	
	if !ka.Enabled {
		return tcpConn.SetKeepAlive(false)
	}
	
	if err := tcpConn.SetKeepAlive(true); err != nil {
		return err
	}
	
	if err := tcpConn.SetKeepAlivePeriod(ka.Interval); err != nil {
		return err
	}
	
	// Platform-specific configuration
	if runtime.GOOS == "linux" {
		return ka.configureLinux(tcpConn)
	} else if runtime.GOOS == "darwin" {
		return ka.configureDarwin(tcpConn)
	} else if runtime.GOOS == "windows" {
		return ka.configureWindows(tcpConn)
	}
	
	return nil
}

// configureLinux sets Linux-specific keep-alive options.
func (ka *TcpKeepAlive) configureLinux(conn *net.TCPConn) error {
	file, err := conn.File()
	if err != nil {
		return err
	}
	defer file.Close()
	
	fd := int(file.Fd())
	
	// TCP_KEEPIDLE
	idle := int(ka.Idle.Seconds())
	if err := syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, 0x4, idle); err != nil {
		return err
	}
	
	// TCP_KEEPINTVL
	interval := int(ka.Interval.Seconds())
	if err := syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, 0x5, interval); err != nil {
		return err
	}
	
	// TCP_KEEPCNT
	if err := syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, 0x6, ka.Count); err != nil {
		return err
	}
	
	return nil
}

// configureDarwin sets macOS-specific keep-alive options.
func (ka *TcpKeepAlive) configureDarwin(conn *net.TCPConn) error {
	file, err := conn.File()
	if err != nil {
		return err
	}
	defer file.Close()
	
	fd := int(file.Fd())
	
	// TCP_KEEPALIVE (idle time)
	idle := int(ka.Idle.Seconds())
	if err := syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, 0x10, idle); err != nil {
		return err
	}
	
	// TCP_KEEPINTVL
	interval := int(ka.Interval.Seconds())
	if err := syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, 0x101, interval); err != nil {
		return err
	}
	
	// TCP_KEEPCNT
	if err := syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, 0x102, ka.Count); err != nil {
		return err
	}
	
	return nil
}

// configureWindows sets Windows-specific keep-alive options.
func (ka *TcpKeepAlive) configureWindows(conn *net.TCPConn) error {
	// Windows keep-alive structure
	type tcpKeepAlive struct {
		OnOff    uint32
		Time     uint32
		Interval uint32
	}
	
	file, err := conn.File()
	if err != nil {
		return err
	}
	defer file.Close()
	
	_ = file.Fd()
	
	ka_settings := tcpKeepAlive{
		OnOff:    1,
		Time:     uint32(ka.Idle.Milliseconds()),
		Interval: uint32(ka.Interval.Milliseconds()),
	}
	
	// Windows-specific keepalive is not available on this platform
	// This would need platform-specific build tags for Windows
	_ = ka_settings
	return fmt.Errorf("Windows keepalive not supported on this platform")
}

// DetectDeadConnection checks if connection is alive.
func DetectDeadConnection(conn net.Conn) bool {
	// Try to read with very short timeout
	conn.SetReadDeadline(time.Now().Add(1 * time.Millisecond))
	buf := make([]byte, 1)
	_, err := conn.Read(buf)
	conn.SetReadDeadline(time.Time{}) // Reset deadline
	
	if err != nil {
		if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
			// Timeout is expected, connection is alive
			return false
		}
		// Other error, connection is dead
		return true
	}
	
	// Data available, connection is alive
	return false
}