using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;
using Microsoft.Extensions.Logging;

namespace GopherMcp.Filters.BuiltinFilters
{
    /// <summary>
    /// Authentication methods
    /// </summary>
    public enum AuthenticationMethod
    {
        None,
        Basic,
        Bearer,
        ApiKey,
        OAuth2,
        Custom
    }

    /// <summary>
    /// Configuration for authentication filter
    /// </summary>
    public class AuthenticationConfig : FilterConfigBase
    {
        /// <summary>
        /// Gets or sets the authentication method
        /// </summary>
        public AuthenticationMethod Method { get; set; } = AuthenticationMethod.None;

        /// <summary>
        /// Gets or sets the realm for basic authentication
        /// </summary>
        public string Realm { get; set; } = "Protected";

        /// <summary>
        /// Gets or sets the credentials store
        /// </summary>
        public Dictionary<string, string> Credentials { get; set; } = new Dictionary<string, string>();

        /// <summary>
        /// Gets or sets the API key header name
        /// </summary>
        public string ApiKeyHeader { get; set; } = "X-API-Key";

        /// <summary>
        /// Gets or sets valid API keys
        /// </summary>
        public HashSet<string> ValidApiKeys { get; set; } = new HashSet<string>();

        /// <summary>
        /// Gets or sets the token validation endpoint
        /// </summary>
        public string TokenValidationEndpoint { get; set; }

        /// <summary>
        /// Gets or sets whether to allow anonymous access
        /// </summary>
        public bool AllowAnonymous { get; set; } = false;

        /// <summary>
        /// Gets or sets the shared secret for authentication
        /// </summary>
        public string SharedSecret { get; set; }

        /// <summary>
        /// Gets or sets the authentication timeout
        /// </summary>
        public TimeSpan AuthenticationTimeout { get; set; } = TimeSpan.FromSeconds(5);

        /// <summary>
        /// Gets or sets whether to cache authentication results
        /// </summary>
        public bool EnableCaching { get; set; } = true;

        /// <summary>
        /// Gets or sets the cache duration
        /// </summary>
        public TimeSpan CacheDuration { get; set; } = TimeSpan.FromMinutes(5);

        /// <summary>
        /// Gets or sets custom authentication handler
        /// </summary>
        public Func<string, string, Task<bool>> CustomAuthHandler { get; set; }

        /// <summary>
        /// Validates the configuration
        /// </summary>
        public override IEnumerable<System.ComponentModel.DataAnnotations.ValidationResult> Validate(
            System.ComponentModel.DataAnnotations.ValidationContext validationContext)
        {
            var results = new List<System.ComponentModel.DataAnnotations.ValidationResult>();

            if (Method == AuthenticationMethod.Basic && Credentials.Count == 0)
            {
                results.Add(new System.ComponentModel.DataAnnotations.ValidationResult(
                    "Basic authentication requires at least one credential"));
            }

            if (Method == AuthenticationMethod.ApiKey && ValidApiKeys.Count == 0)
            {
                results.Add(new System.ComponentModel.DataAnnotations.ValidationResult(
                    "API key authentication requires at least one valid key"));
            }

            if (Method == AuthenticationMethod.OAuth2 && string.IsNullOrEmpty(TokenValidationEndpoint))
            {
                results.Add(new System.ComponentModel.DataAnnotations.ValidationResult(
                    "OAuth2 authentication requires a token validation endpoint"));
            }

            return results;
        }
    }

    /// <summary>
    /// Authentication filter for validating requests
    /// </summary>
    public class AuthenticationFilter : Filter
    {
        private readonly AuthenticationConfig _config;
        private readonly ILogger<AuthenticationFilter> _logger;
        private readonly Dictionary<string, (bool IsValid, DateTime Expiry)> _cache;
        private readonly SemaphoreSlim _cacheLock;

        /// <summary>
        /// Initializes a new instance of the AuthenticationFilter class
        /// </summary>
        public AuthenticationFilter(AuthenticationConfig config, ILogger<AuthenticationFilter> logger = null)
            : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _logger = logger;
            _cache = new Dictionary<string, (bool, DateTime)>();
            _cacheLock = new SemaphoreSlim(1, 1);
        }

        /// <summary>
        /// Processes buffer through the authentication filter
        /// </summary>
        protected override async Task<FilterResult> ProcessInternal(
            byte[] buffer,
            ProcessingContext context,
            CancellationToken cancellationToken = default)
        {
            try
            {
                // Allow anonymous if configured
                if (_config.AllowAnonymous)
                {
                    _logger?.LogDebug("Allowing anonymous access");
                    return FilterResult.Success(buffer, 0, buffer.Length);
                }

                // Extract authentication information from context
                var authHeader = context?.GetProperty<string>("Authorization");

                if (string.IsNullOrEmpty(authHeader) && _config.Method != AuthenticationMethod.ApiKey)
                {
                    _logger?.LogWarning("No authorization header found");
                    return FilterResult.Error("Unauthorized", FilterError.Unauthorized);
                }

                bool isAuthenticated = false;
                string cacheKey = null;

                // Check cache if enabled
                if (_config.EnableCaching && !string.IsNullOrEmpty(authHeader))
                {
                    cacheKey = ComputeHash(authHeader);
                    if (await CheckCacheAsync(cacheKey))
                    {
                        _logger?.LogDebug("Authentication result found in cache");
                        return FilterResult.Success(buffer, 0, buffer.Length);
                    }
                }

                // Perform authentication based on method
                using (var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken))
                {
                    cts.CancelAfter(_config.AuthenticationTimeout);

                    switch (_config.Method)
                    {
                        case AuthenticationMethod.Basic:
                            isAuthenticated = await ValidateBasicAuth(authHeader, cts.Token);
                            break;

                        case AuthenticationMethod.Bearer:
                            isAuthenticated = await ValidateBearerToken(authHeader, cts.Token);
                            break;

                        case AuthenticationMethod.ApiKey:
                            var apiKey = context?.GetProperty<string>(_config.ApiKeyHeader) ??
                                        ExtractApiKeyFromAuth(authHeader);
                            isAuthenticated = ValidateApiKey(apiKey);
                            break;

                        case AuthenticationMethod.OAuth2:
                            isAuthenticated = await ValidateOAuth2Token(authHeader, cts.Token);
                            break;

                        case AuthenticationMethod.Custom:
                            if (_config.CustomAuthHandler != null)
                            {
                                var (username, password) = ExtractCredentials(authHeader);
                                isAuthenticated = await _config.CustomAuthHandler(username, password);
                            }
                            break;

                        case AuthenticationMethod.None:
                            isAuthenticated = true;
                            break;
                    }
                }

                // Update cache if authenticated
                if (isAuthenticated && _config.EnableCaching && !string.IsNullOrEmpty(cacheKey))
                {
                    await UpdateCacheAsync(cacheKey, true);
                }

                if (isAuthenticated)
                {
                    _logger?.LogInformation("Authentication successful");
                    UpdateStatistics(buffer.Length, 0, true);
                    return FilterResult.Success(buffer, 0, buffer.Length);
                }
                else
                {
                    _logger?.LogWarning("Authentication failed");
                    UpdateStatistics(0, 1, false);
                    return FilterResult.Error("Unauthorized", FilterError.Unauthorized);
                }
            }
            catch (OperationCanceledException)
            {
                _logger?.LogError("Authentication timeout");
                return FilterResult.Error("Authentication timeout", FilterError.Timeout);
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Authentication error");
                return FilterResult.Error($"Authentication error: {ex.Message}", FilterError.InternalError);
            }
        }

        private async Task<bool> ValidateBasicAuth(string authHeader, CancellationToken cancellationToken)
        {
            if (string.IsNullOrEmpty(authHeader) || !authHeader.StartsWith("Basic ", StringComparison.OrdinalIgnoreCase))
                return false;

            try
            {
                var encodedCredentials = authHeader.Substring(6);
                var credentialBytes = Convert.FromBase64String(encodedCredentials);
                var credentials = Encoding.UTF8.GetString(credentialBytes);
                var parts = credentials.Split(':', 2);

                if (parts.Length != 2)
                    return false;

                var username = parts[0];
                var password = parts[1];

                // Check credentials
                if (_config.Credentials.TryGetValue(username, out var expectedPassword))
                {
                    return password == expectedPassword;
                }

                await Task.CompletedTask;
                return false;
            }
            catch
            {
                return false;
            }
        }

        private async Task<bool> ValidateBearerToken(string authHeader, CancellationToken cancellationToken)
        {
            if (string.IsNullOrEmpty(authHeader) || !authHeader.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase))
                return false;

            var token = authHeader.Substring(7);

            // Simple token validation - in production, validate with JWT or OAuth2 server
            await Task.CompletedTask;
            return !string.IsNullOrEmpty(token);
        }

        private bool ValidateApiKey(string apiKey)
        {
            if (string.IsNullOrEmpty(apiKey))
                return false;

            return _config.ValidApiKeys.Contains(apiKey);
        }

        private string ExtractApiKeyFromAuth(string authHeader)
        {
            if (string.IsNullOrEmpty(authHeader))
                return null;

            if (authHeader.StartsWith("ApiKey ", StringComparison.OrdinalIgnoreCase))
                return authHeader.Substring(7);

            return null;
        }

        private async Task<bool> ValidateOAuth2Token(string authHeader, CancellationToken cancellationToken)
        {
            if (string.IsNullOrEmpty(authHeader) || !authHeader.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase))
                return false;

            var token = authHeader.Substring(7);

            // In production, validate with OAuth2 server
            // This is a placeholder implementation
            await Task.Delay(10, cancellationToken);
            return !string.IsNullOrEmpty(token);
        }

        private (string Username, string Password) ExtractCredentials(string authHeader)
        {
            if (string.IsNullOrEmpty(authHeader))
                return (null, null);

            if (authHeader.StartsWith("Basic ", StringComparison.OrdinalIgnoreCase))
            {
                try
                {
                    var encodedCredentials = authHeader.Substring(6);
                    var credentialBytes = Convert.FromBase64String(encodedCredentials);
                    var credentials = Encoding.UTF8.GetString(credentialBytes);
                    var parts = credentials.Split(':', 2);

                    if (parts.Length == 2)
                        return (parts[0], parts[1]);
                }
                catch
                {
                    // Invalid format
                }
            }

            return (null, null);
        }

        private string ComputeHash(string input)
        {
            using (var sha256 = SHA256.Create())
            {
                var bytes = Encoding.UTF8.GetBytes(input);
                var hash = sha256.ComputeHash(bytes);
                return Convert.ToBase64String(hash);
            }
        }

        private async Task<bool> CheckCacheAsync(string key)
        {
            await _cacheLock.WaitAsync();
            try
            {
                if (_cache.TryGetValue(key, out var entry))
                {
                    if (entry.Expiry > DateTime.UtcNow)
                    {
                        return entry.IsValid;
                    }
                    else
                    {
                        _cache.Remove(key);
                    }
                }
                return false;
            }
            finally
            {
                _cacheLock.Release();
            }
        }

        private async Task UpdateCacheAsync(string key, bool isValid)
        {
            await _cacheLock.WaitAsync();
            try
            {
                _cache[key] = (isValid, DateTime.UtcNow.Add(_config.CacheDuration));

                // Clean expired entries
                var expiredKeys = _cache.Where(kvp => kvp.Value.Expiry <= DateTime.UtcNow)
                                       .Select(kvp => kvp.Key)
                                       .ToList();

                foreach (var expiredKey in expiredKeys)
                {
                    _cache.Remove(expiredKey);
                }
            }
            finally
            {
                _cacheLock.Release();
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _cacheLock?.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
