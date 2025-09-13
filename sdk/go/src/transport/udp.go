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

// UdpTransport implements Transport using UDP sockets.
type UdpTransport struct {
	TransportBase

	// Connection
	conn       *net.UDPConn
	remoteAddr *net.UDPAddr
	localAddr  *net.UDPAddr

	// Configuration
	config UdpConfig

	// Reliability layer
	reliability *UdpReliability

	// Packet handling
	packetBuffer chan UdpPacket
	sequenceNum  atomic.Uint64

	// Multicast
	multicastGroup *net.UDPAddr

	mu sync.RWMutex
}

// UdpConfig configures UDP transport behavior.
type UdpConfig struct {
	LocalAddress  string
	RemoteAddress string
	Port          int
	MaxPacketSize int
	BufferSize    int

	// Reliability
	EnableReliability bool
	RetransmitTimeout time.Duration
	MaxRetransmits    int

	// Multicast
	EnableMulticast  bool
	MulticastAddress string
	MulticastTTL     int

	// Broadcast
	EnableBroadcast bool
}

// DefaultUdpConfig returns default UDP configuration.
func DefaultUdpConfig() UdpConfig {
	return UdpConfig{
		LocalAddress:      "0.0.0.0",
		Port:              8081,
		MaxPacketSize:     1472, // Typical MTU minus headers
		BufferSize:        65536,
		EnableReliability: false,
		RetransmitTimeout: 100 * time.Millisecond,
		MaxRetransmits:    3,
		EnableMulticast:   false,
		MulticastTTL:      1,
		EnableBroadcast:   false,
	}
}

// UdpPacket represents a UDP packet.
type UdpPacket struct {
	Data      []byte
	Addr      *net.UDPAddr
	Sequence  uint64
	Timestamp time.Time
}

// NewUdpTransport creates a new UDP transport.
func NewUdpTransport(config UdpConfig) *UdpTransport {
	baseConfig := DefaultTransportConfig()
	baseConfig.ReadBufferSize = config.BufferSize
	baseConfig.WriteBufferSize = config.BufferSize

	transport := &UdpTransport{
		TransportBase: NewTransportBase(baseConfig),
		config:        config,
		packetBuffer:  make(chan UdpPacket, 1000),
	}

	if config.EnableReliability {
		transport.reliability = NewUdpReliability(config)
	}

	return transport
}

// Connect establishes UDP connection.
func (ut *UdpTransport) Connect(ctx context.Context) error {
	if !ut.SetConnected(true) {
		return ErrAlreadyConnected
	}

	// Parse addresses
	localAddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", ut.config.LocalAddress, ut.config.Port))
	if err != nil {
		ut.SetConnected(false)
		return err
	}

	// Create UDP connection
	conn, err := net.ListenUDP("udp", localAddr)
	if err != nil {
		ut.SetConnected(false)
		return err
	}

	// Configure socket options
	if err := ut.configureSocket(conn); err != nil {
		conn.Close()
		ut.SetConnected(false)
		return err
	}

	ut.mu.Lock()
	ut.conn = conn
	ut.localAddr = localAddr
	ut.mu.Unlock()

	// Parse remote address if specified
	if ut.config.RemoteAddress != "" {
		remoteAddr, err := net.ResolveUDPAddr("udp", ut.config.RemoteAddress)
		if err != nil {
			conn.Close()
			ut.SetConnected(false)
			return err
		}
		ut.remoteAddr = remoteAddr
	}

	// Setup multicast if enabled
	if ut.config.EnableMulticast {
		if err := ut.setupMulticast(); err != nil {
			conn.Close()
			ut.SetConnected(false)
			return err
		}
	}

	// Start packet receiver
	go ut.receivePackets(ctx)

	// Start reliability layer if enabled
	if ut.reliability != nil {
		ut.reliability.Start(ut)
	}

	ut.UpdateConnectTime()
	return nil
}

// configureSocket applies socket options.
func (ut *UdpTransport) configureSocket(conn *net.UDPConn) error {
	// Set buffer sizes
	if err := conn.SetReadBuffer(ut.config.BufferSize); err != nil {
		return err
	}
	if err := conn.SetWriteBuffer(ut.config.BufferSize); err != nil {
		return err
	}

	// Enable broadcast if configured
	if ut.config.EnableBroadcast {
		file, err := conn.File()
		if err != nil {
			return err
		}
		defer file.Close()

		// Set SO_BROADCAST option
		// Platform-specific implementation would go here
	}

	return nil
}

// setupMulticast configures multicast.
func (ut *UdpTransport) setupMulticast() error {
	addr, err := net.ResolveUDPAddr("udp", ut.config.MulticastAddress)
	if err != nil {
		return err
	}

	ut.multicastGroup = addr

	// Join multicast group
	// Platform-specific multicast join would go here

	return nil
}

// Send sends data via UDP.
func (ut *UdpTransport) Send(data []byte) error {
	ut.mu.RLock()
	conn := ut.conn
	addr := ut.remoteAddr
	ut.mu.RUnlock()

	if conn == nil {
		return ErrNotConnected
	}

	// Fragment if needed
	packets := ut.fragmentData(data)

	for _, packet := range packets {
		var err error
		if addr != nil {
			_, err = conn.WriteToUDP(packet, addr)
		} else if ut.config.EnableBroadcast {
			broadcastAddr := &net.UDPAddr{
				IP:   net.IPv4(255, 255, 255, 255),
				Port: ut.config.Port,
			}
			_, err = conn.WriteToUDP(packet, broadcastAddr)
		} else if ut.multicastGroup != nil {
			_, err = conn.WriteToUDP(packet, ut.multicastGroup)
		} else {
			return fmt.Errorf("no destination address specified")
		}

		if err != nil {
			ut.RecordSendError()
			return err
		}

		ut.RecordBytesSent(len(packet))

		// Add to reliability layer if enabled
		if ut.reliability != nil {
			ut.reliability.TrackPacket(packet, ut.sequenceNum.Add(1))
		}
	}

	return nil
}

// Receive receives data from UDP.
func (ut *UdpTransport) Receive() ([]byte, error) {
	select {
	case packet := <-ut.packetBuffer:
		ut.RecordBytesReceived(len(packet.Data))

		// Handle reliability layer if enabled
		if ut.reliability != nil {
			if err := ut.reliability.ProcessReceived(packet); err != nil {
				return nil, err
			}
		}

		return packet.Data, nil

	case <-time.After(time.Second):
		return nil, fmt.Errorf("receive timeout")
	}
}

// receivePackets continuously receives UDP packets.
func (ut *UdpTransport) receivePackets(ctx context.Context) {
	buffer := make([]byte, ut.config.MaxPacketSize)

	for {
		select {
		case <-ctx.Done():
			return
		default:
		}

		ut.mu.RLock()
		conn := ut.conn
		ut.mu.RUnlock()

		if conn == nil {
			return
		}

		n, addr, err := conn.ReadFromUDP(buffer)
		if err != nil {
			ut.RecordReceiveError()
			continue
		}

		// Create packet copy
		data := make([]byte, n)
		copy(data, buffer[:n])

		packet := UdpPacket{
			Data:      data,
			Addr:      addr,
			Timestamp: time.Now(),
		}

		// Handle packet reordering if reliability enabled
		if ut.reliability != nil {
			packet = ut.reliability.ReorderPacket(packet)
		}

		select {
		case ut.packetBuffer <- packet:
		default:
			// Buffer full, drop packet
			ut.SetCustomMetric("dropped_packets", 1)
		}
	}
}

// fragmentData splits data into UDP-sized packets.
func (ut *UdpTransport) fragmentData(data []byte) [][]byte {
	if len(data) <= ut.config.MaxPacketSize {
		return [][]byte{data}
	}

	var packets [][]byte
	for i := 0; i < len(data); i += ut.config.MaxPacketSize {
		end := i + ut.config.MaxPacketSize
		if end > len(data) {
			end = len(data)
		}

		packet := make([]byte, end-i)
		copy(packet, data[i:end])
		packets = append(packets, packet)
	}

	return packets
}

// Disconnect closes UDP connection.
func (ut *UdpTransport) Disconnect() error {
	if !ut.SetConnected(false) {
		return nil
	}

	// Stop reliability layer
	if ut.reliability != nil {
		ut.reliability.Stop()
	}

	ut.mu.Lock()
	if ut.conn != nil {
		ut.conn.Close()
		ut.conn = nil
	}
	ut.mu.Unlock()

	ut.UpdateDisconnectTime()
	return nil
}

// UdpReliability implements optional reliability layer.
type UdpReliability struct {
	config          UdpConfig
	pendingPackets  map[uint64]*PendingPacket
	receivedPackets map[uint64]time.Time
	mu              sync.Mutex
	stopCh          chan struct{}
}

// PendingPacket tracks packet for retransmission.
type PendingPacket struct {
	Data          []byte
	Sequence      uint64
	Transmissions int
	LastSent      time.Time
}

// NewUdpReliability creates reliability layer.
func NewUdpReliability(config UdpConfig) *UdpReliability {
	return &UdpReliability{
		config:          config,
		pendingPackets:  make(map[uint64]*PendingPacket),
		receivedPackets: make(map[uint64]time.Time),
		stopCh:          make(chan struct{}),
	}
}

// Start starts reliability processing.
func (ur *UdpReliability) Start(transport *UdpTransport) {
	go ur.retransmitLoop(transport)
	go ur.cleanupLoop()
}

// Stop stops reliability processing.
func (ur *UdpReliability) Stop() {
	close(ur.stopCh)
}

// TrackPacket adds packet to reliability tracking.
func (ur *UdpReliability) TrackPacket(data []byte, seq uint64) {
	ur.mu.Lock()
	defer ur.mu.Unlock()

	ur.pendingPackets[seq] = &PendingPacket{
		Data:          data,
		Sequence:      seq,
		Transmissions: 1,
		LastSent:      time.Now(),
	}
}

// ProcessReceived processes received packet for reliability.
func (ur *UdpReliability) ProcessReceived(packet UdpPacket) error {
	ur.mu.Lock()
	defer ur.mu.Unlock()

	// Check for duplicate
	if _, exists := ur.receivedPackets[packet.Sequence]; exists {
		return fmt.Errorf("duplicate packet")
	}

	ur.receivedPackets[packet.Sequence] = time.Now()

	// Send ACK if needed
	// ACK implementation would go here

	return nil
}

// ReorderPacket handles packet reordering.
func (ur *UdpReliability) ReorderPacket(packet UdpPacket) UdpPacket {
	// Simple reordering buffer implementation
	// More sophisticated reordering would go here
	return packet
}

// retransmitLoop handles packet retransmission.
func (ur *UdpReliability) retransmitLoop(transport *UdpTransport) {
	ticker := time.NewTicker(ur.config.RetransmitTimeout)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			ur.checkRetransmits(transport)
		case <-ur.stopCh:
			return
		}
	}
}

// checkRetransmits checks for packets needing retransmission.
func (ur *UdpReliability) checkRetransmits(transport *UdpTransport) {
	ur.mu.Lock()
	defer ur.mu.Unlock()

	now := time.Now()
	for seq, packet := range ur.pendingPackets {
		if now.Sub(packet.LastSent) > ur.config.RetransmitTimeout {
			if packet.Transmissions < ur.config.MaxRetransmits {
				// Retransmit
				transport.Send(packet.Data)
				packet.Transmissions++
				packet.LastSent = now
			} else {
				// Max retransmits reached, remove
				delete(ur.pendingPackets, seq)
			}
		}
	}
}

// cleanupLoop cleans old received packet records.
func (ur *UdpReliability) cleanupLoop() {
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			ur.cleanup()
		case <-ur.stopCh:
			return
		}
	}
}

// cleanup removes old packet records.
func (ur *UdpReliability) cleanup() {
	ur.mu.Lock()
	defer ur.mu.Unlock()

	cutoff := time.Now().Add(-30 * time.Second)
	for seq, timestamp := range ur.receivedPackets {
		if timestamp.Before(cutoff) {
			delete(ur.receivedPackets, seq)
		}
	}
}
