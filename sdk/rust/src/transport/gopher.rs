//! # GopherTransport Implementation
//!
//! This module provides the GopherTransport implementation for MCP protocol communication.

use crate::filter::manager::FilterManager;
use crate::filter::api::FilterManagerConfig;
use serde_json::{Value, json};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::io::AsyncWriteExt;
use tokio::net::{TcpListener, TcpStream, UdpSocket};
use tokio::sync::mpsc;
use tracing::{info, warn, debug, error};

/// GopherTransport configuration
#[derive(Debug, Clone, serde::Serialize)]
pub struct GopherTransportConfig {
    /// Transport name
    pub name: String,
    /// Transport version
    pub version: String,
    /// Protocol type
    pub protocol: ProtocolType,
    /// Host address
    pub host: Option<String>,
    /// Port number
    pub port: Option<u16>,
    /// Connection timeout
    pub connect_timeout: Option<Duration>,
    /// Send timeout
    pub send_timeout: Option<Duration>,
    /// Receive timeout
    pub receive_timeout: Option<Duration>,
    /// Maximum connections
    pub max_connections: Option<usize>,
    /// Buffer size
    pub buffer_size: Option<usize>,
    /// Filter configuration
    pub filter_config: Option<FilterManagerConfig>,
}

impl Default for GopherTransportConfig {
    fn default() -> Self {
        Self {
            name: "gopher-transport".to_string(),
            version: "1.0.0".to_string(),
            protocol: ProtocolType::Stdio,
            host: None,
            port: None,
            connect_timeout: Some(Duration::from_secs(30)),
            send_timeout: Some(Duration::from_secs(10)),
            receive_timeout: Some(Duration::from_secs(30)),
            max_connections: Some(10),
            buffer_size: Some(8192),
            filter_config: None,
        }
    }
}

/// Protocol types supported by GopherTransport
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub enum ProtocolType {
    /// Standard input/output
    Stdio,
    /// TCP protocol
    Tcp,
    /// UDP protocol
    Udp,
}

/// Message event handler type
pub type MessageHandler = Box<dyn Fn(Value) + Send + Sync>;

/// Error event handler type
pub type ErrorHandler = Box<dyn Fn(Box<dyn std::error::Error + Send + Sync>) + Send + Sync>;

/// Close event handler type
pub type CloseHandler = Box<dyn Fn() + Send + Sync>;

/// GopherTransport implementation
pub struct GopherTransport {
    config: GopherTransportConfig,
    filter_manager: FilterManager,
    is_connected: bool,
    is_destroyed: bool,
    session_id: Option<String>,
    connections: Arc<Mutex<HashMap<String, TcpStream>>>,
    message_buffers: Arc<Mutex<HashMap<String, String>>>,
    tcp_listener: Option<TcpListener>,
    udp_socket: Option<UdpSocket>,
    message_tx: Option<mpsc::UnboundedSender<Value>>,
    message_rx: Option<mpsc::UnboundedReceiver<Value>>,
    on_message: Option<MessageHandler>,
    on_error: Option<ErrorHandler>,
    on_close: Option<CloseHandler>,
}

impl GopherTransport {
    /// Create a new GopherTransport
    pub fn new(config: GopherTransportConfig) -> Self {
        let (message_tx, message_rx) = mpsc::unbounded_channel();
        
        Self {
            config,
            filter_manager: FilterManager::new(),
            is_connected: false,
            is_destroyed: false,
            session_id: None,
            connections: Arc::new(Mutex::new(HashMap::new())),
            message_buffers: Arc::new(Mutex::new(HashMap::new())),
            tcp_listener: None,
            udp_socket: None,
            message_tx: Some(message_tx),
            message_rx: Some(message_rx),
            on_message: None,
            on_error: None,
            on_close: None,
        }
    }

    /// Set message handler
    pub fn on_message<F>(&mut self, handler: F)
    where
        F: Fn(Value) + Send + Sync + 'static,
    {
        self.on_message = Some(Box::new(handler));
    }

    /// Set error handler
    pub fn on_error<F>(&mut self, handler: F)
    where
        F: Fn(Box<dyn std::error::Error + Send + Sync>) + Send + Sync + 'static,
    {
        self.on_error = Some(Box::new(handler));
    }

    /// Set close handler
    pub fn on_close<F>(&mut self, handler: F)
    where
        F: Fn() + Send + Sync + 'static,
    {
        self.on_close = Some(Box::new(handler));
    }

    /// Start the transport
    pub async fn start(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        if self.is_destroyed {
            return Err("GopherTransport has been destroyed".into());
        }

        if self.is_connected {
            warn!("GopherTransport is already connected");
            return Ok(());
        }

        info!("🚀 Starting GopherTransport ({:?})", self.config.protocol);

        // Generate session ID
        self.session_id = Some(self.generate_session_id());
        info!("📋 Session ID: {}", self.session_id.as_ref().unwrap());

        // Start transport based on protocol
        self.start_transport().await?;

        self.is_connected = true;
        info!("✅ GopherTransport started successfully");

        // Start message processing loop
        self.start_message_processing().await;

        Ok(())
    }

    /// Send a message through the transport
    pub async fn send(&self, message: Value) -> Result<(), Box<dyn std::error::Error>> {
        if self.is_destroyed {
            return Err("GopherTransport has been destroyed".into());
        }

        if !self.is_connected {
            return Err("GopherTransport is not connected".into());
        }

        let message_info = if let Some(method) = message.get("method").and_then(|v| v.as_str()) {
            let id = message.get("id").map(|v| v.to_string()).unwrap_or_else(|| "N/A".to_string());
            format!("{} (id: {})", method, id)
        } else {
            "notification".to_string()
        };

        info!("📤 Sending message: {}", message_info);

        // Process message through FilterManager
        let processed_message = self.filter_manager.process(message).await?;

        let processed_info = if let Some(method) = processed_message.get("method").and_then(|v| v.as_str()) {
            let id = processed_message.get("id").map(|v| v.to_string()).unwrap_or_else(|| "N/A".to_string());
            format!("{} (id: {})", method, id)
        } else {
            "notification".to_string()
        };

        info!("✅ Message processed through filters: {}", processed_info);
        info!("✅ Message ready for transport: {}", serde_json::to_string_pretty(&processed_message)?);

        // Send the processed message through the actual transport
        self.send_through_transport(processed_message).await?;

        Ok(())
    }

    /// Close the transport
    pub async fn close(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        if self.is_destroyed {
            warn!("GopherTransport is already destroyed");
            return Ok(());
        }

        info!("🔌 Closing GopherTransport connection");

        self.is_connected = false;

        // Close transport connections
        if let Some(listener) = self.tcp_listener.take() {
            drop(listener);
        }

        if let Some(socket) = self.udp_socket.take() {
            drop(socket);
        }

        // Close all active connections
        {
            let mut connections = self.connections.lock().unwrap();
            connections.clear();
        }

        {
            let mut buffers = self.message_buffers.lock().unwrap();
            buffers.clear();
        }

        // Clean up message channels
        self.message_tx.take();
        self.message_rx.take();

        self.is_destroyed = true;
        info!("✅ GopherTransport closed successfully");

        // Notify close event
        if let Some(handler) = &self.on_close {
            handler();
        }

        Ok(())
    }

    /// Check if the transport is connected
    pub fn is_connected(&self) -> bool {
        self.is_connected
    }

    /// Check if the transport is destroyed
    pub fn is_destroyed(&self) -> bool {
        self.is_destroyed
    }

    /// Get session ID
    pub fn session_id(&self) -> Option<&String> {
        self.session_id.as_ref()
    }

    /// Get transport statistics
    pub fn get_stats(&self) -> Value {
        json!({
            "is_connected": self.is_connected,
            "is_destroyed": self.is_destroyed,
            "session_id": self.session_id,
            "config": self.config,
            "connections": self.connections.lock().unwrap().len(),
        })
    }

    /// Process a received message through FilterManager
    pub async fn process_received_message(&self, message: Value) -> Result<(), Box<dyn std::error::Error>> {
        if !self.is_connected || self.is_destroyed {
            return Ok(());
        }

        let message_info = if let Some(method) = message.get("method").and_then(|v| v.as_str()) {
            let id = message.get("id").map(|v| v.to_string()).unwrap_or_else(|| "N/A".to_string());
            format!("{} (id: {})", method, id)
        } else {
            "notification".to_string()
        };

        info!("📥 Processing received message: {}", message_info);

        // Process response through FilterManager
        let processed_message = self.filter_manager.process(message).await?;

        let processed_info = if let Some(method) = processed_message.get("method").and_then(|v| v.as_str()) {
            let id = processed_message.get("id").map(|v| v.to_string()).unwrap_or_else(|| "N/A".to_string());
            format!("{} (id: {})", method, id)
        } else {
            "notification".to_string()
        };

        info!("✅ Response processed through filters: {}", processed_info);

        // Notify message event
        if let Some(handler) = &self.on_message {
            handler(processed_message);
        }

        Ok(())
    }

    /// Generate a unique session ID
    fn generate_session_id(&self) -> String {
        let timestamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();
        let random = rand::random::<u64>();
        format!("gopher-{}-{}", timestamp, random)
    }

    /// Start the actual transport layer based on protocol
    async fn start_transport(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        match self.config.protocol {
            ProtocolType::Stdio => self.start_stdio_transport().await,
            ProtocolType::Tcp => self.start_tcp_transport().await,
            ProtocolType::Udp => self.start_udp_transport().await,
        }
    }

    /// Start stdio transport
    async fn start_stdio_transport(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        info!("📡 Starting stdio transport");
        // For stdio, we'll use tokio::io::stdin/stdout for real communication
        // This is a simplified implementation - in practice, you'd want more robust handling
        info!("✅ Stdio transport ready for input");
        Ok(())
    }

    /// Start TCP transport
    async fn start_tcp_transport(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        let host = self.config.host.as_deref().unwrap_or("127.0.0.1");
        let port = self.config.port.unwrap_or(8080);
        
        info!("📡 Starting TCP transport on {}:{}", host, port);

        if self.config.host.is_some() {
            // Client mode - connect to server
            let stream = TcpStream::connect(format!("{}:{}", host, port)).await?;
            info!("🔗 Connected to TCP server");
            
            // Store connection
            let connection_id = format!("{}:{}", host, port);
            self.connections.lock().unwrap().insert(connection_id, stream);
        } else {
            // Server mode - listen for connections
            let listener = TcpListener::bind(format!("{}:{}", host, port)).await?;
            info!("🚀 TCP server listening on port {}", port);
            self.tcp_listener = Some(listener);
            
            // Start accepting connections in the background
            self.start_tcp_server_loop().await?;
        }

        Ok(())
    }

    /// Start TCP server loop to accept incoming connections
    async fn start_tcp_server_loop(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        let listener = self.tcp_listener.as_ref().unwrap();
        let connections = Arc::clone(&self.connections);
        let max_connections = self.config.max_connections.unwrap_or(10);
        
        info!("🔄 Starting TCP server loop to accept connections...");
        
        loop {
            // Accept incoming connections
            match listener.accept().await {
                Ok((stream, addr)) => {
                    info!("🔗 New connection from {}", addr);
                    
                    // Check connection limit
                    let current_connections = connections.lock().unwrap().len();
                    if current_connections >= max_connections {
                        warn!("⚠️ Connection limit reached ({}), rejecting connection from {}", max_connections, addr);
                        drop(stream);
                        continue;
                    }
                    
                    // Store the connection
                    let connection_id = format!("{}", addr);
                    connections.lock().unwrap().insert(connection_id, stream);
                    
                    info!("✅ Connection stored, total connections: {}", current_connections + 1);
                }
                Err(e) => {
                    error!("❌ Failed to accept connection: {}", e);
                    // Continue accepting other connections
                    tokio::time::sleep(Duration::from_millis(100)).await;
                }
            }
        }
    }

    /// Start UDP transport
    async fn start_udp_transport(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        let host = self.config.host.as_deref().unwrap_or("127.0.0.1");
        let port = self.config.port.unwrap_or(8080);
        
        info!("📡 Starting UDP transport on {}:{}", host, port);

        let socket = UdpSocket::bind(format!("{}:{}", host, port)).await?;
        info!("🚀 UDP server listening on port {}", port);
        self.udp_socket = Some(socket);

        Ok(())
    }

    /// Send message through the actual transport
    async fn send_through_transport(&self, message: Value) -> Result<(), Box<dyn std::error::Error>> {
        let message_data = format!("{}\n", serde_json::to_string(&message)?);

        match self.config.protocol {
            ProtocolType::Stdio => {
                // For stdio, write to stdout
                tokio::io::stdout().write_all(message_data.as_bytes()).await?;
            }
            ProtocolType::Tcp => {
                let connections = self.connections.lock().unwrap();
                for (_connection_id, stream) in connections.iter() {
                    // In a real implementation, you'd need to handle async writing to multiple streams
                    // This is simplified for demonstration
                    debug!("Would send TCP message: {}", message_data.trim());
                }
            }
            ProtocolType::Udp => {
                if let Some(socket) = &self.udp_socket {
                    if let Some(host) = &self.config.host {
                        let port = self.config.port.unwrap_or(8080);
                        socket.send_to(message_data.as_bytes(), format!("{}:{}", host, port)).await?;
                    }
                }
            }
        }

        Ok(())
    }

    /// Start message processing loop
    async fn start_message_processing(&mut self) {
        if let Some(mut message_rx) = self.message_rx.take() {
            tokio::spawn(async move {
                while let Some(message) = message_rx.recv().await {
                    debug!("Processing message: {}", message);
                    // In a real implementation, you'd process the message here
                }
            });
        }
    }
}
