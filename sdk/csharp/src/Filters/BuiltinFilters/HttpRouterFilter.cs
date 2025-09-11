using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;
using Microsoft.Extensions.Logging;

namespace GopherMcp.Filters.BuiltinFilters
{
    /// <summary>
    /// Configuration for HTTP router filter.
    /// </summary>
    public class HttpRouterConfig : FilterConfigBase
    {
        /// <summary>
        /// Gets or sets the route definitions.
        /// </summary>
        public List<RouteConfig> Routes { get; set; } = new List<RouteConfig>();

        /// <summary>
        /// Gets or sets the default handler for unmatched routes.
        /// </summary>
        public string DefaultHandler { get; set; }

        /// <summary>
        /// Gets or sets whether to enable case-sensitive routing.
        /// </summary>
        public bool CaseSensitive { get; set; } = false;

        /// <summary>
        /// Gets or sets whether to enable trailing slash matching.
        /// </summary>
        public bool StrictSlashes { get; set; } = false;

        /// <summary>
        /// Gets or sets whether to enable method override via headers.
        /// </summary>
        public bool EnableMethodOverride { get; set; } = false;

        /// <summary>
        /// Gets or sets the method override header name.
        /// </summary>
        public string MethodOverrideHeader { get; set; } = "X-HTTP-Method-Override";
    }

    /// <summary>
    /// Route configuration.
    /// </summary>
    public class RouteConfig
    {
        /// <summary>
        /// Gets or sets the route pattern.
        /// </summary>
        public string Pattern { get; set; }

        /// <summary>
        /// Gets or sets the allowed HTTP methods.
        /// </summary>
        public List<string> Methods { get; set; } = new List<string>();

        /// <summary>
        /// Gets or sets the target handler.
        /// </summary>
        public string Target { get; set; }

        /// <summary>
        /// Gets or sets route-specific middleware.
        /// </summary>
        public List<string> Middleware { get; set; } = new List<string>();

        /// <summary>
        /// Gets or sets route constraints.
        /// </summary>
        public Dictionary<string, string> Constraints { get; set; } = new Dictionary<string, string>();

        /// <summary>
        /// Gets or sets route metadata.
        /// </summary>
        public Dictionary<string, object> Metadata { get; set; } = new Dictionary<string, object>();
    }

    /// <summary>
    /// HTTP router filter for routing HTTP requests.
    /// </summary>
    public class HttpRouterFilter : Filter
    {
        private readonly HttpRouterConfig _config;
        private readonly ILogger<HttpRouterFilter> _logger;
        private readonly List<CompiledRoute> _compiledRoutes;

        /// <summary>
        /// Initializes a new instance of the HttpRouterFilter class.
        /// </summary>
        /// <param name="config">The HTTP router configuration.</param>
        /// <param name="logger">Optional logger.</param>
        public HttpRouterFilter(HttpRouterConfig config, ILogger<HttpRouterFilter> logger = null)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _logger = logger;
            _compiledRoutes = CompileRoutes(_config.Routes);
        }

        /// <summary>
        /// Processes buffer through the HTTP router filter.
        /// </summary>
        protected override async Task<FilterResult> ProcessInternal(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            try
            {
                // Extract path from context (would be parsed from HTTP request in real implementation)
                var path = context?.GetProperty<string>("Path") ?? "/";
                var method = context?.GetProperty<string>("Method") ?? "GET";
                
                // Handle method override if enabled
                if (_config.EnableMethodOverride)
                {
                    var overrideMethod = context?.GetProperty<string>(_config.MethodOverrideHeader);
                    if (!string.IsNullOrEmpty(overrideMethod))
                    {
                        method = overrideMethod;
                    }
                }
                
                // Find matching route
                var match = MatchRoute(path, method);
                
                if (match == null)
                {
                    _logger?.LogWarning("No route found for {Method} {Path}", method, path);
                    
                    if (!string.IsNullOrEmpty(_config.DefaultHandler))
                    {
                        context?.SetProperty("RouteTarget", _config.DefaultHandler);
                        _logger?.LogInformation("Using default handler for {Method} {Path}", method, path);
                    }
                    else
                    {
                        return FilterResult.Error("Not Found", FilterError.NotFound);
                    }
                }
                else
                {
                    // Store route information in context
                    context?.SetProperty("MatchedRoute", match.Route.Pattern);
                    context?.SetProperty("RouteTarget", match.Route.Target);
                    context?.SetProperty("RouteParams", match.Parameters);
                    
                    // Add route metadata to context
                    foreach (var metadata in match.Route.Metadata)
                    {
                        context?.SetProperty($"Route.{metadata.Key}", metadata.Value);
                    }
                    
                    _logger?.LogInformation("Routed {Method} {Path} to {Target}", method, path, match.Route.Target);
                }
                
                await Task.CompletedTask; // Satisfy async requirement
                return FilterResult.Success(buffer, 0, buffer.Length);
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Error in HTTP router");
                return FilterResult.Error($"Router error: {ex.Message}", FilterError.ProcessingFailed);
            }
        }

        /// <summary>
        /// Compiles routes into regex patterns.
        /// </summary>
        private List<CompiledRoute> CompileRoutes(List<RouteConfig> routes)
        {
            var compiled = new List<CompiledRoute>();
            
            foreach (var route in routes)
            {
                var pattern = route.Pattern;
                var paramNames = new List<string>();
                
                // Extract parameter names and create regex pattern
                // Example: /users/{id} -> /users/(?<id>[^/]+)
                var regex = Regex.Replace(pattern, @"\{([^}]+)\}", match =>
                {
                    var paramName = match.Groups[1].Value;
                    paramNames.Add(paramName);
                    
                    // Apply constraint if specified
                    if (route.Constraints.TryGetValue(paramName, out var constraint))
                    {
                        return $"(?<{paramName}>{constraint})";
                    }
                    
                    return $"(?<{paramName}>[^/]+)";
                });
                
                // Handle trailing slashes
                if (!_config.StrictSlashes)
                {
                    regex = regex.TrimEnd('/') + "/?";
                }
                
                compiled.Add(new CompiledRoute
                {
                    Route = route,
                    Pattern = new Regex($"^{regex}$", 
                        _config.CaseSensitive ? RegexOptions.None : RegexOptions.IgnoreCase),
                    ParameterNames = paramNames
                });
            }
            
            return compiled;
        }

        /// <summary>
        /// Matches a route for the given path and method.
        /// </summary>
        private RouteMatch MatchRoute(string path, string method)
        {
            foreach (var compiledRoute in _compiledRoutes)
            {
                // Check method constraint
                if (compiledRoute.Route.Methods.Count > 0 && 
                    !compiledRoute.Route.Methods.Contains(method, StringComparer.OrdinalIgnoreCase))
                {
                    continue;
                }
                
                // Match path pattern
                var match = compiledRoute.Pattern.Match(path);
                if (match.Success)
                {
                    var parameters = new Dictionary<string, string>();
                    
                    foreach (var paramName in compiledRoute.ParameterNames)
                    {
                        parameters[paramName] = match.Groups[paramName].Value;
                    }
                    
                    return new RouteMatch
                    {
                        Route = compiledRoute.Route,
                        Parameters = parameters
                    };
                }
            }
            
            return null;
        }

        /// <summary>
        /// Compiled route with regex pattern.
        /// </summary>
        private class CompiledRoute
        {
            public RouteConfig Route { get; set; }
            public Regex Pattern { get; set; }
            public List<string> ParameterNames { get; set; }
        }

        /// <summary>
        /// Route match result.
        /// </summary>
        private class RouteMatch
        {
            public RouteConfig Route { get; set; }
            public Dictionary<string, string> Parameters { get; set; }
        }
    }
}