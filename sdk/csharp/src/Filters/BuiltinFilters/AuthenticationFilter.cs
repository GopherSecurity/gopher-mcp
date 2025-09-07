using System;
using System.Collections.Generic;
using System.IdentityModel.Tokens.Jwt;
using System.Linq;
using System.Security.Claims;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;
using Microsoft.IdentityModel.Tokens;

namespace GopherMcp.Filters.BuiltinFilters
{
    public enum AuthenticationMethod
    {
        None,
        JWT,
        ApiKey,
        OAuth2,
        Basic
    }

    public class AuthenticationConfig : FilterConfigBase
    {
        public AuthenticationMethod Method { get; set; } = AuthenticationMethod.JWT;
        public string? Secret { get; set; }
        public string? Issuer { get; set; }
        public string? Audience { get; set; }
        public List<string> BypassPaths { get; set; } = new();
        public bool RequireHttps { get; set; } = true;
        public int TokenExpirationMinutes { get; set; } = 60;
        public Dictionary<string, string> ApiKeys { get; set; } = new();
        public string? OAuth2TokenEndpoint { get; set; }
        public string? OAuth2ClientId { get; set; }
        public string? OAuth2ClientSecret { get; set; }

        public AuthenticationConfig() : base("Authentication", "AuthenticationFilter")
        {
            Priority = 100; // High priority to run early
        }

        public override bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (!base.Validate(out var baseErrors))
            {
                errors.AddRange(baseErrors);
            }

            if (Method == AuthenticationMethod.JWT)
            {
                if (string.IsNullOrEmpty(Secret))
                {
                    errors.Add("JWT Secret is required for JWT authentication");
                }
                if (Secret?.Length < 32)
                {
                    errors.Add("JWT Secret must be at least 32 characters");
                }
            }
            else if (Method == AuthenticationMethod.ApiKey)
            {
                if (ApiKeys.Count == 0)
                {
                    errors.Add("At least one API key must be configured");
                }
            }
            else if (Method == AuthenticationMethod.OAuth2)
            {
                if (string.IsNullOrEmpty(OAuth2TokenEndpoint))
                {
                    errors.Add("OAuth2 token endpoint is required");
                }
                if (string.IsNullOrEmpty(OAuth2ClientId))
                {
                    errors.Add("OAuth2 client ID is required");
                }
            }

            return errors.Count == 0;
        }
    }

    public class AuthenticationFilter : Filter
    {
        private readonly AuthenticationConfig _config;
        private readonly JwtSecurityTokenHandler _jwtHandler;
        private TokenValidationParameters? _tokenValidationParameters;

        public AuthenticationFilter(AuthenticationConfig config) : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _jwtHandler = new JwtSecurityTokenHandler();
            InitializeTokenValidation();
        }

        private void InitializeTokenValidation()
        {
            if (_config.Method == AuthenticationMethod.JWT && !string.IsNullOrEmpty(_config.Secret))
            {
                _tokenValidationParameters = new TokenValidationParameters
                {
                    ValidateIssuerSigningKey = true,
                    IssuerSigningKey = new SymmetricSecurityKey(Encoding.UTF8.GetBytes(_config.Secret)),
                    ValidateIssuer = !string.IsNullOrEmpty(_config.Issuer),
                    ValidIssuer = _config.Issuer,
                    ValidateAudience = !string.IsNullOrEmpty(_config.Audience),
                    ValidAudience = _config.Audience,
                    ValidateLifetime = true,
                    ClockSkew = TimeSpan.Zero
                };
            }
        }

        public override async Task<FilterResult> ProcessAsync(byte[] data, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            try
            {
                // Check bypass rules
                if (ShouldBypass(context))
                {
                    return FilterResult.Continue(data);
                }

                // Extract authentication token
                var token = ExtractToken(context);
                if (string.IsNullOrEmpty(token))
                {
                    return FilterResult.Error("Authentication required", FilterError.Unauthorized);
                }

                // Validate based on authentication method
                var validationResult = await ValidateTokenAsync(token, context, cancellationToken);
                if (!validationResult.IsValid)
                {
                    return FilterResult.Error(validationResult.ErrorMessage ?? "Authentication failed", FilterError.Unauthorized);
                }

                // Store user information in context
                if (validationResult.Principal != null)
                {
                    context.SetProperty("User", validationResult.Principal);
                    context.SetProperty("UserId", validationResult.Principal.FindFirst(ClaimTypes.NameIdentifier)?.Value);
                    context.SetProperty("UserName", validationResult.Principal.FindFirst(ClaimTypes.Name)?.Value);
                }

                UpdateStatistics(true);
                await RaiseOnDataAsync(new FilterDataEventArgs(data, context));
                return FilterResult.Continue(data);
            }
            catch (Exception ex)
            {
                UpdateStatistics(false);
                await RaiseOnErrorAsync(new FilterErrorEventArgs(ex, context));
                return FilterResult.Error($"Authentication error: {ex.Message}", FilterError.InternalError);
            }
        }

        private bool ShouldBypass(ProcessingContext context)
        {
            var path = context.GetProperty<string>("Path");
            if (string.IsNullOrEmpty(path))
            {
                return false;
            }

            return _config.BypassPaths.Any(bypassPath => 
                path.StartsWith(bypassPath, StringComparison.OrdinalIgnoreCase));
        }

        private string? ExtractToken(ProcessingContext context)
        {
            // Try to get token from different sources based on method
            switch (_config.Method)
            {
                case AuthenticationMethod.JWT:
                case AuthenticationMethod.OAuth2:
                    // Extract from Authorization header
                    var authHeader = context.GetProperty<string>("Authorization");
                    if (!string.IsNullOrEmpty(authHeader))
                    {
                        if (authHeader.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase))
                        {
                            return authHeader.Substring(7);
                        }
                    }
                    break;

                case AuthenticationMethod.ApiKey:
                    // Extract from X-API-Key header or query parameter
                    var apiKey = context.GetProperty<string>("X-API-Key") ?? 
                                context.GetProperty<string>("ApiKey");
                    return apiKey;

                case AuthenticationMethod.Basic:
                    // Extract from Authorization header
                    var basicAuth = context.GetProperty<string>("Authorization");
                    if (!string.IsNullOrEmpty(basicAuth) && basicAuth.StartsWith("Basic ", StringComparison.OrdinalIgnoreCase))
                    {
                        return basicAuth.Substring(6);
                    }
                    break;
            }

            return null;
        }

        private async Task<ValidationResult> ValidateTokenAsync(string token, ProcessingContext context, CancellationToken cancellationToken)
        {
            switch (_config.Method)
            {
                case AuthenticationMethod.JWT:
                    return ValidateJwtToken(token);

                case AuthenticationMethod.ApiKey:
                    return ValidateApiKey(token);

                case AuthenticationMethod.OAuth2:
                    return await ValidateOAuth2TokenAsync(token, cancellationToken);

                case AuthenticationMethod.Basic:
                    return ValidateBasicAuth(token);

                default:
                    return new ValidationResult { IsValid = false, ErrorMessage = "Unsupported authentication method" };
            }
        }

        private ValidationResult ValidateJwtToken(string token)
        {
            if (_tokenValidationParameters == null)
            {
                return new ValidationResult { IsValid = false, ErrorMessage = "JWT validation not configured" };
            }

            try
            {
                var principal = _jwtHandler.ValidateToken(token, _tokenValidationParameters, out var validatedToken);
                return new ValidationResult 
                { 
                    IsValid = true, 
                    Principal = principal 
                };
            }
            catch (SecurityTokenExpiredException)
            {
                return new ValidationResult { IsValid = false, ErrorMessage = "Token has expired" };
            }
            catch (SecurityTokenInvalidSignatureException)
            {
                return new ValidationResult { IsValid = false, ErrorMessage = "Invalid token signature" };
            }
            catch (Exception ex)
            {
                return new ValidationResult { IsValid = false, ErrorMessage = $"Token validation failed: {ex.Message}" };
            }
        }

        private ValidationResult ValidateApiKey(string apiKey)
        {
            if (_config.ApiKeys.TryGetValue(apiKey, out var userId))
            {
                var claims = new[]
                {
                    new Claim(ClaimTypes.NameIdentifier, userId),
                    new Claim(ClaimTypes.AuthenticationMethod, "ApiKey")
                };
                var identity = new ClaimsIdentity(claims, "ApiKey");
                var principal = new ClaimsPrincipal(identity);

                return new ValidationResult 
                { 
                    IsValid = true, 
                    Principal = principal 
                };
            }

            return new ValidationResult { IsValid = false, ErrorMessage = "Invalid API key" };
        }

        private async Task<ValidationResult> ValidateOAuth2TokenAsync(string token, CancellationToken cancellationToken)
        {
            // TODO: Implement OAuth2 token validation
            // This would typically involve calling the OAuth2 provider's introspection endpoint
            await Task.Delay(0, cancellationToken); // Placeholder
            
            return new ValidationResult 
            { 
                IsValid = false, 
                ErrorMessage = "OAuth2 validation not yet implemented" 
            };
        }

        private ValidationResult ValidateBasicAuth(string encodedCredentials)
        {
            try
            {
                var credentials = Encoding.UTF8.GetString(Convert.FromBase64String(encodedCredentials));
                var parts = credentials.Split(':');
                if (parts.Length != 2)
                {
                    return new ValidationResult { IsValid = false, ErrorMessage = "Invalid Basic auth format" };
                }

                var username = parts[0];
                var password = parts[1];

                // TODO: Validate against user store
                // This is a placeholder implementation
                if (username == "admin" && password == "password")
                {
                    var claims = new[]
                    {
                        new Claim(ClaimTypes.NameIdentifier, username),
                        new Claim(ClaimTypes.Name, username),
                        new Claim(ClaimTypes.AuthenticationMethod, "Basic")
                    };
                    var identity = new ClaimsIdentity(claims, "Basic");
                    var principal = new ClaimsPrincipal(identity);

                    return new ValidationResult 
                    { 
                        IsValid = true, 
                        Principal = principal 
                    };
                }

                return new ValidationResult { IsValid = false, ErrorMessage = "Invalid credentials" };
            }
            catch (Exception ex)
            {
                return new ValidationResult { IsValid = false, ErrorMessage = $"Basic auth validation failed: {ex.Message}" };
            }
        }

        private class ValidationResult
        {
            public bool IsValid { get; set; }
            public string? ErrorMessage { get; set; }
            public ClaimsPrincipal? Principal { get; set; }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                // Clean up managed resources
            }
            base.Dispose(disposing);
        }
    }
}