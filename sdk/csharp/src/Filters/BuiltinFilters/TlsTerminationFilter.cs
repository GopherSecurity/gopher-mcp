using System;
using System.Collections.Generic;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;
using Microsoft.Extensions.Logging;

namespace GopherMcp.Filters.BuiltinFilters
{
    /// <summary>
    /// Configuration for TLS termination filter.
    /// </summary>
    public class TlsTerminationConfig : FilterConfigBase
    {
        /// <summary>
        /// Gets or sets the server certificate.
        /// </summary>
        public X509Certificate2 ServerCertificate { get; set; }

        /// <summary>
        /// Gets or sets the certificate file path.
        /// </summary>
        public string CertificateFilePath { get; set; }

        /// <summary>
        /// Gets or sets the private key file path.
        /// </summary>
        public string PrivateKeyFilePath { get; set; }

        /// <summary>
        /// Gets or sets the certificate password.
        /// </summary>
        public string CertificatePassword { get; set; }

        /// <summary>
        /// Gets or sets the supported SSL/TLS protocols.
        /// </summary>
        public SslProtocols SslProtocols { get; set; } = SslProtocols.Tls12 | SslProtocols.Tls13;

        /// <summary>
        /// Gets or sets whether client certificates are required.
        /// </summary>
        public bool RequireClientCertificate { get; set; } = false;

        /// <summary>
        /// Gets or sets whether certificate revocation should be checked.
        /// </summary>
        public bool CheckCertificateRevocation { get; set; } = true;

        /// <summary>
        /// Gets or sets the cipher suites preference.
        /// </summary>
        public CipherSuitesPolicy CipherSuitesPolicy { get; set; }

        /// <summary>
        /// Gets or sets the certificate validation callback.
        /// </summary>
        public RemoteCertificateValidationCallback CertificateValidationCallback { get; set; }

        /// <summary>
        /// Gets or sets the local certificate selection callback.
        /// </summary>
        public LocalCertificateSelectionCallback LocalCertificateSelectionCallback { get; set; }

        /// <summary>
        /// Gets or sets whether to enable session resumption.
        /// </summary>
        public bool EnableSessionResumption { get; set; } = true;

        /// <summary>
        /// Gets or sets the handshake timeout in milliseconds.
        /// </summary>
        public int HandshakeTimeoutMs { get; set; } = 10000;

        /// <summary>
        /// Gets or sets additional server names for SNI support.
        /// </summary>
        public List<string> ServerNames { get; set; } = new List<string>();

        /// <summary>
        /// Gets or sets whether to enable OCSP stapling.
        /// </summary>
        public bool EnableOcspStapling { get; set; } = false;

        public TlsTerminationConfig() : base()
        {
            Type = "TlsTermination";
        }

        public TlsTerminationConfig(string name) : base(name)
        {
            Type = "TlsTermination";
        }
    }

    /// <summary>
    /// TLS connection context.
    /// </summary>
    public class TlsConnectionContext
    {
        public SslStream SslStream { get; set; }
        public X509Certificate2 ClientCertificate { get; set; }
        public string ServerName { get; set; }
        public SslProtocols NegotiatedProtocol { get; set; }
        public DateTime HandshakeStartTime { get; set; }
        public DateTime HandshakeEndTime { get; set; }
        public bool IsAuthenticated { get; set; }
        public Dictionary<string, object> Properties { get; set; } = new Dictionary<string, object>();
    }

    /// <summary>
    /// TLS termination filter for SSL/TLS termination and certificate management.
    /// </summary>
    public class TlsTerminationFilter : Filter
    {
        private readonly TlsTerminationConfig _config;
        private readonly ILogger<TlsTerminationFilter> _logger;
        private readonly Dictionary<string, X509Certificate2> _certificateCache;
        private readonly object _cacheLock = new object();

        /// <summary>
        /// Initializes a new instance of the TlsTerminationFilter class.
        /// </summary>
        /// <param name="config">The TLS termination configuration.</param>
        /// <param name="logger">Optional logger.</param>
        public TlsTerminationFilter(TlsTerminationConfig config, ILogger<TlsTerminationFilter> logger = null) 
            : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _logger = logger;
            _certificateCache = new Dictionary<string, X509Certificate2>();
        }

        /// <summary>
        /// Initializes the filter asynchronously.
        /// </summary>
        protected override async Task OnInitializeAsync()
        {
            _logger?.LogInformation("Initializing TLS termination filter: {Name}", _config.Name);

            try
            {
                // Load server certificate if not provided
                if (_config.ServerCertificate == null && !string.IsNullOrEmpty(_config.CertificateFilePath))
                {
                    _config.ServerCertificate = LoadCertificate(_config.CertificateFilePath, _config.CertificatePassword);
                }

                if (_config.ServerCertificate == null)
                {
                    throw new InvalidOperationException("Server certificate must be provided");
                }

                // Validate certificate
                if (!_config.ServerCertificate.HasPrivateKey)
                {
                    throw new InvalidOperationException("Server certificate must include private key");
                }

                // Check certificate expiration
                if (_config.ServerCertificate.NotAfter < DateTime.UtcNow.AddDays(7))
                {
                    _logger?.LogWarning("Server certificate expires soon: {ExpirationDate}", 
                        _config.ServerCertificate.NotAfter);
                }

                _logger?.LogInformation("TLS termination filter initialized successfully");
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Failed to initialize TLS termination filter");
                throw;
            }

            await Task.CompletedTask;
        }

        /// <summary>
        /// Processes data through the TLS termination filter.
        /// </summary>
        protected override async Task<FilterResult> ProcessInternal(
            byte[] buffer, 
            ProcessingContext context, 
            CancellationToken cancellationToken)
        {
            try
            {
                _logger?.LogDebug("Processing TLS data: {ByteCount} bytes", buffer.Length);

                // Handle TLS handshake and termination
                var tlsContext = context?.GetProperty<TlsConnectionContext>("TlsContext");
                if (tlsContext == null)
                {
                    tlsContext = new TlsConnectionContext();
                    context?.SetProperty("TlsContext", tlsContext);
                }

                // Process based on TLS connection state
                if (!tlsContext.IsAuthenticated)
                {
                    var handshakeResult = await ProcessTlsHandshake(buffer, tlsContext, context, cancellationToken);
                    if (handshakeResult.Status != FilterStatus.Continue)
                    {
                        return handshakeResult;
                    }
                }

                // Decrypt TLS data
                var decryptedData = await DecryptTlsData(buffer, tlsContext, cancellationToken);
                if (decryptedData == null || decryptedData.Length == 0)
                {
                    return new FilterResult(FilterStatus.NeedMoreData);
                }

                _logger?.LogDebug("Decrypted TLS data: {DecryptedSize} bytes", decryptedData.Length);

                // Update context with TLS information
                context?.SetMetadata("TlsProtocol", tlsContext.NegotiatedProtocol.ToString());
                context?.SetMetadata("ClientCertificate", tlsContext.ClientCertificate?.Subject);
                context?.SetMetadata("ServerName", tlsContext.ServerName);

                return new FilterResult(FilterStatus.Continue, decryptedData, 0, decryptedData.Length);
            }
            catch (AuthenticationException ex)
            {
                _logger?.LogWarning(ex, "TLS authentication failed");
                return FilterResult.Error(FilterError.ProcessingFailed, "TLS authentication failed");
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Error processing TLS data");
                return FilterResult.Error(FilterError.ProcessingFailed, ex.Message);
            }
        }

        /// <summary>
        /// Processes TLS handshake.
        /// </summary>
        private async Task<FilterResult> ProcessTlsHandshake(
            byte[] data, 
            TlsConnectionContext tlsContext, 
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
            try
            {
                tlsContext.HandshakeStartTime = DateTime.UtcNow;

                // Parse TLS handshake messages
                var handshakeType = ParseTlsHandshake(data);
                
                switch (handshakeType)
                {
                    case TlsHandshakeType.ClientHello:
                        return await ProcessClientHello(data, tlsContext, cancellationToken);
                    
                    case TlsHandshakeType.Certificate:
                        return await ProcessCertificate(data, tlsContext, cancellationToken);
                    
                    case TlsHandshakeType.Finished:
                        return await ProcessHandshakeFinished(data, tlsContext, cancellationToken);
                    
                    default:
                        return new FilterResult(FilterStatus.Continue);
                }
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "TLS handshake processing failed");
                return FilterResult.Error(FilterError.ProcessingFailed, "TLS handshake failed");
            }
        }

        /// <summary>
        /// Processes ClientHello message.
        /// </summary>
        private async Task<FilterResult> ProcessClientHello(
            byte[] data, 
            TlsConnectionContext tlsContext,
            CancellationToken cancellationToken)
        {
            // Extract SNI from ClientHello
            var serverName = ExtractServerNameFromClientHello(data);
            if (!string.IsNullOrEmpty(serverName))
            {
                tlsContext.ServerName = serverName;
                
                // Select appropriate certificate for SNI
                var certificate = await SelectCertificateForServerName(serverName, cancellationToken);
                if (certificate != null)
                {
                    _logger?.LogDebug("Selected certificate for SNI: {ServerName}", serverName);
                }
            }

            return new FilterResult(FilterStatus.Continue);
        }

        /// <summary>
        /// Processes certificate message.
        /// </summary>
        private async Task<FilterResult> ProcessCertificate(
            byte[] data, 
            TlsConnectionContext tlsContext,
            CancellationToken cancellationToken)
        {
            if (_config.RequireClientCertificate)
            {
                var clientCert = ExtractClientCertificate(data);
                if (clientCert != null)
                {
                    tlsContext.ClientCertificate = clientCert;
                    
                    // Validate client certificate
                    var validationResult = await ValidateClientCertificate(clientCert, cancellationToken);
                    if (!validationResult)
                    {
                        _logger?.LogWarning("Client certificate validation failed");
                        return FilterResult.Error(FilterError.ProcessingFailed, "Client certificate validation failed");
                    }
                }
                else
                {
                    _logger?.LogWarning("Client certificate required but not provided");
                    return FilterResult.Error(FilterError.ProcessingFailed, "Client certificate required");
                }
            }

            return new FilterResult(FilterStatus.Continue);
        }

        /// <summary>
        /// Processes handshake finished message.
        /// </summary>
        private async Task<FilterResult> ProcessHandshakeFinished(
            byte[] data, 
            TlsConnectionContext tlsContext,
            CancellationToken cancellationToken)
        {
            tlsContext.HandshakeEndTime = DateTime.UtcNow;
            tlsContext.IsAuthenticated = true;

            var handshakeDuration = tlsContext.HandshakeEndTime - tlsContext.HandshakeStartTime;
            _logger?.LogInformation("TLS handshake completed in {Duration}ms", handshakeDuration.TotalMilliseconds);

            return new FilterResult(FilterStatus.Continue);
        }

        /// <summary>
        /// Decrypts TLS application data.
        /// </summary>
        private async Task<byte[]> DecryptTlsData(
            byte[] encryptedData, 
            TlsConnectionContext tlsContext,
            CancellationToken cancellationToken)
        {
            // Simulate TLS decryption
            // In a real implementation, this would use the SSL stream
            await Task.Delay(1, cancellationToken);
            
            // For demonstration, return the data as-is
            return encryptedData;
        }

        /// <summary>
        /// Loads certificate from file.
        /// </summary>
        private X509Certificate2 LoadCertificate(string certificatePath, string password)
        {
            try
            {
                return new X509Certificate2(certificatePath, password, X509KeyStorageFlags.MachineKeySet);
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Failed to load certificate from {Path}", certificatePath);
                throw;
            }
        }

        /// <summary>
        /// Selects certificate for server name (SNI).
        /// </summary>
        private async Task<X509Certificate2> SelectCertificateForServerName(
            string serverName, 
            CancellationToken cancellationToken)
        {
            lock (_cacheLock)
            {
                if (_certificateCache.TryGetValue(serverName, out var cachedCert))
                {
                    return cachedCert;
                }
            }

            // Load certificate for specific server name
            // This is a simplified implementation
            await Task.CompletedTask;
            return _config.ServerCertificate;
        }

        /// <summary>
        /// Validates client certificate.
        /// </summary>
        private async Task<bool> ValidateClientCertificate(
            X509Certificate2 clientCertificate, 
            CancellationToken cancellationToken)
        {
            try
            {
                using var chain = new X509Chain();
                chain.ChainPolicy.RevocationMode = _config.CheckCertificateRevocation 
                    ? X509RevocationMode.Online 
                    : X509RevocationMode.NoCheck;

                var result = chain.Build(clientCertificate);
                
                if (!result && _logger != null)
                {
                    foreach (X509ChainElement element in chain.ChainElements)
                    {
                        foreach (X509ChainStatus status in element.ChainElementStatus)
                        {
                            _logger.LogWarning("Certificate validation issue: {Status}", status.StatusInformation);
                        }
                    }
                }

                await Task.CompletedTask;
                return result;
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Client certificate validation failed");
                return false;
            }
        }

        /// <summary>
        /// Updates configuration asynchronously.
        /// </summary>
        protected override async Task OnConfigurationUpdateAsync(FilterConfig oldConfig, FilterConfig newConfig)
        {
            _logger?.LogInformation("Updating TLS termination filter configuration");
            
            // Clear certificate cache on config update
            lock (_cacheLock)
            {
                foreach (var cert in _certificateCache.Values)
                {
                    cert?.Dispose();
                }
                _certificateCache.Clear();
            }
            
            await Task.CompletedTask;
        }

        /// <summary>
        /// Validates configuration.
        /// </summary>
        protected override bool OnValidateConfig(FilterConfig config)
        {
            if (config is not TlsTerminationConfig tlsConfig)
                return false;

            if (tlsConfig.ServerCertificate == null && string.IsNullOrEmpty(tlsConfig.CertificateFilePath))
                return false;

            if (tlsConfig.HandshakeTimeoutMs <= 0)
                return false;

            return true;
        }

        /// <summary>
        /// Disposes resources.
        /// </summary>
        protected override void OnDispose(bool disposing)
        {
            if (disposing)
            {
                lock (_cacheLock)
                {
                    foreach (var cert in _certificateCache.Values)
                    {
                        cert?.Dispose();
                    }
                    _certificateCache.Clear();
                }

                _config?.ServerCertificate?.Dispose();
            }
        }

        #region Private Helper Methods

        /// <summary>
        /// Parses TLS handshake message type.
        /// </summary>
        private TlsHandshakeType ParseTlsHandshake(byte[] data)
        {
            if (data.Length < 6)
                return TlsHandshakeType.Unknown;

            // TLS record header: [Content Type][Version][Length][Handshake Type]
            if (data[0] == 0x16) // Handshake record
            {
                return (TlsHandshakeType)data[5];
            }

            return TlsHandshakeType.Unknown;
        }

        /// <summary>
        /// Extracts server name from ClientHello message.
        /// </summary>
        private string ExtractServerNameFromClientHello(byte[] data)
        {
            // Simplified SNI extraction - in practice this would be more complex
            // This is a placeholder implementation
            return "example.com";
        }

        /// <summary>
        /// Extracts client certificate from certificate message.
        /// </summary>
        private X509Certificate2 ExtractClientCertificate(byte[] data)
        {
            // Simplified certificate extraction
            // In practice, this would parse the TLS Certificate message
            try
            {
                return new X509Certificate2(data);
            }
            catch
            {
                return null;
            }
        }

        #endregion

        /// <summary>
        /// TLS handshake message types.
        /// </summary>
        private enum TlsHandshakeType : byte
        {
            Unknown = 0,
            ClientHello = 1,
            ServerHello = 2,
            Certificate = 11,
            ServerKeyExchange = 12,
            CertificateRequest = 13,
            ServerHelloDone = 14,
            CertificateVerify = 15,
            ClientKeyExchange = 16,
            Finished = 20
        }
    }
}