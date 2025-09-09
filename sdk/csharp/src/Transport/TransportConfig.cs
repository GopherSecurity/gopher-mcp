using System;
using System.Collections.Generic;
using GopherMcp.Manager;

namespace GopherMcp.Transport
{
    /// <summary>
    /// Transport configuration with all connection options
    /// </summary>
    public class TransportConfig
    {
        /// <summary>
        /// Protocol selection
        /// </summary>
        public TransportProtocol Protocol { get; set; } = TransportProtocol.Tcp;

        /// <summary>
        /// Host name or IP address
        /// </summary>
        public string Host { get; set; } = "localhost";

        /// <summary>
        /// Port number
        /// </summary>
        public int Port { get; set; } = 9000;

        /// <summary>
        /// Connection timeout
        /// </summary>
        public TimeSpan ConnectTimeout { get; set; } = TimeSpan.FromSeconds(30);

        /// <summary>
        /// Send operation timeout
        /// </summary>
        public TimeSpan SendTimeout { get; set; } = TimeSpan.FromSeconds(30);

        /// <summary>
        /// Receive operation timeout
        /// </summary>
        public TimeSpan ReceiveTimeout { get; set; } = TimeSpan.FromSeconds(30);

        /// <summary>
        /// Maximum message size in bytes
        /// </summary>
        public int MaxMessageSize { get; set; } = 4 * 1024 * 1024; // 4MB

        /// <summary>
        /// Send buffer size
        /// </summary>
        public int SendBufferSize { get; set; } = 8192;

        /// <summary>
        /// Receive buffer size
        /// </summary>
        public int ReceiveBufferSize { get; set; } = 8192;

        /// <summary>
        /// Enable keep-alive for TCP connections
        /// </summary>
        public bool EnableKeepAlive { get; set; } = true;

        /// <summary>
        /// Keep-alive interval
        /// </summary>
        public TimeSpan KeepAliveInterval { get; set; } = TimeSpan.FromSeconds(30);

        /// <summary>
        /// Filter manager configuration
        /// </summary>
        public FilterManagerConfig? Filters { get; set; }

        /// <summary>
        /// Connection retry settings
        /// </summary>
        public ConnectionRetryConfig RetryConfig { get; set; } = new();

        /// <summary>
        /// SSL/TLS configuration for secure connections
        /// </summary>
        public SslConfig? SslConfig { get; set; }

        /// <summary>
        /// Custom connection options
        /// </summary>
        public Dictionary<string, object> ConnectionOptions { get; set; } = new();

        /// <summary>
        /// Enable automatic reconnection
        /// </summary>
        public bool AutoReconnect { get; set; } = true;

        /// <summary>
        /// Maximum reconnection attempts
        /// </summary>
        public int MaxReconnectAttempts { get; set; } = 3;

        /// <summary>
        /// Delay between reconnection attempts
        /// </summary>
        public TimeSpan ReconnectDelay { get; set; } = TimeSpan.FromSeconds(5);

        /// <summary>
        /// Enable message compression
        /// </summary>
        public bool EnableCompression { get; set; } = false;

        /// <summary>
        /// Compression level (1-9)
        /// </summary>
        public int CompressionLevel { get; set; } = 6;

        /// <summary>
        /// Validate configuration
        /// </summary>
        public bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (Port <= 0 || Port > 65535)
            {
                errors.Add($"Invalid port number: {Port}");
            }

            if (string.IsNullOrWhiteSpace(Host) && Protocol != TransportProtocol.Stdio)
            {
                errors.Add("Host cannot be empty for non-stdio protocols");
            }

            if (MaxMessageSize <= 0)
            {
                errors.Add("MaxMessageSize must be greater than 0");
            }

            if (SendBufferSize <= 0)
            {
                errors.Add("SendBufferSize must be greater than 0");
            }

            if (ReceiveBufferSize <= 0)
            {
                errors.Add("ReceiveBufferSize must be greater than 0");
            }

            if (ConnectTimeout <= TimeSpan.Zero)
            {
                errors.Add("ConnectTimeout must be greater than 0");
            }

            if (SendTimeout <= TimeSpan.Zero)
            {
                errors.Add("SendTimeout must be greater than 0");
            }

            if (ReceiveTimeout <= TimeSpan.Zero)
            {
                errors.Add("ReceiveTimeout must be greater than 0");
            }

            if (MaxReconnectAttempts < 0)
            {
                errors.Add("MaxReconnectAttempts cannot be negative");
            }

            if (ReconnectDelay < TimeSpan.Zero)
            {
                errors.Add("ReconnectDelay cannot be negative");
            }

            if (CompressionLevel < 1 || CompressionLevel > 9)
            {
                errors.Add("CompressionLevel must be between 1 and 9");
            }

            if (RetryConfig != null && !RetryConfig.Validate(out var retryErrors))
            {
                errors.AddRange(retryErrors);
            }

            if (SslConfig != null && !SslConfig.Validate(out var sslErrors))
            {
                errors.AddRange(sslErrors);
            }

            return errors.Count == 0;
        }

        /// <summary>
        /// Create a copy of the configuration
        /// </summary>
        public TransportConfig Clone()
        {
            return new TransportConfig
            {
                Protocol = Protocol,
                Host = Host,
                Port = Port,
                ConnectTimeout = ConnectTimeout,
                SendTimeout = SendTimeout,
                ReceiveTimeout = ReceiveTimeout,
                MaxMessageSize = MaxMessageSize,
                SendBufferSize = SendBufferSize,
                ReceiveBufferSize = ReceiveBufferSize,
                EnableKeepAlive = EnableKeepAlive,
                KeepAliveInterval = KeepAliveInterval,
                Filters = Filters,
                RetryConfig = RetryConfig?.Clone() ?? new ConnectionRetryConfig(),
                SslConfig = SslConfig?.Clone(),
                ConnectionOptions = new Dictionary<string, object>(ConnectionOptions),
                AutoReconnect = AutoReconnect,
                MaxReconnectAttempts = MaxReconnectAttempts,
                ReconnectDelay = ReconnectDelay,
                EnableCompression = EnableCompression,
                CompressionLevel = CompressionLevel
            };
        }
    }

    /// <summary>
    /// Transport protocol selection
    /// </summary>
    public enum TransportProtocol
    {
        Tcp,
        Udp,
        Stdio,
        Http,
        WebSocket,
        NamedPipe,
        UnixSocket
    }

    /// <summary>
    /// Connection retry configuration
    /// </summary>
    public class ConnectionRetryConfig
    {
        public bool Enabled { get; set; } = true;
        public int MaxAttempts { get; set; } = 3;
        public TimeSpan InitialDelay { get; set; } = TimeSpan.FromSeconds(1);
        public TimeSpan MaxDelay { get; set; } = TimeSpan.FromSeconds(30);
        public double BackoffMultiplier { get; set; } = 2.0;
        public bool UseJitter { get; set; } = true;

        public bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (MaxAttempts < 0)
            {
                errors.Add("MaxAttempts cannot be negative");
            }

            if (InitialDelay < TimeSpan.Zero)
            {
                errors.Add("InitialDelay cannot be negative");
            }

            if (MaxDelay < InitialDelay)
            {
                errors.Add("MaxDelay must be greater than or equal to InitialDelay");
            }

            if (BackoffMultiplier <= 1.0)
            {
                errors.Add("BackoffMultiplier must be greater than 1.0");
            }

            return errors.Count == 0;
        }

        public ConnectionRetryConfig Clone()
        {
            return new ConnectionRetryConfig
            {
                Enabled = Enabled,
                MaxAttempts = MaxAttempts,
                InitialDelay = InitialDelay,
                MaxDelay = MaxDelay,
                BackoffMultiplier = BackoffMultiplier,
                UseJitter = UseJitter
            };
        }
    }

    /// <summary>
    /// SSL/TLS configuration
    /// </summary>
    public class SslConfig
    {
        public bool Enabled { get; set; } = false;
        public string? ServerCertificatePath { get; set; }
        public string? ServerCertificatePassword { get; set; }
        public string? ClientCertificatePath { get; set; }
        public string? ClientCertificatePassword { get; set; }
        public bool ValidateServerCertificate { get; set; } = true;
        public List<string> AllowedSslProtocols { get; set; } = new() { "Tls12", "Tls13" };
        public string? ServerName { get; set; }

        public bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (Enabled)
            {
                if (string.IsNullOrEmpty(ServerCertificatePath) && string.IsNullOrEmpty(ClientCertificatePath))
                {
                    errors.Add("At least one certificate path must be specified when SSL is enabled");
                }

                if (!string.IsNullOrEmpty(ServerCertificatePath) && !System.IO.File.Exists(ServerCertificatePath))
                {
                    errors.Add($"Server certificate file not found: {ServerCertificatePath}");
                }

                if (!string.IsNullOrEmpty(ClientCertificatePath) && !System.IO.File.Exists(ClientCertificatePath))
                {
                    errors.Add($"Client certificate file not found: {ClientCertificatePath}");
                }

                if (AllowedSslProtocols.Count == 0)
                {
                    errors.Add("At least one SSL protocol must be allowed");
                }
            }

            return errors.Count == 0;
        }

        public SslConfig Clone()
        {
            return new SslConfig
            {
                Enabled = Enabled,
                ServerCertificatePath = ServerCertificatePath,
                ServerCertificatePassword = ServerCertificatePassword,
                ClientCertificatePath = ClientCertificatePath,
                ClientCertificatePassword = ClientCertificatePassword,
                ValidateServerCertificate = ValidateServerCertificate,
                AllowedSslProtocols = new List<string>(AllowedSslProtocols),
                ServerName = ServerName
            };
        }
    }
}
