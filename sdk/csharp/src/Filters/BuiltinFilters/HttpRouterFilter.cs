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
    public class HttpRouterConfig : FilterConfig
    {
        /// <summary>
        /// Gets or sets the route definitions.
        /// </summary>
        public List<RouteDefinition> Routes { get; set; } = new List<RouteDefinition>();

        /// <summary>
        /// Gets or sets the default route handler.
        /// </summary>
        public string DefaultHandler { get; set; }

        /// <summary>
        /// Gets or sets whether to enable case-sensitive matching.
        /// </summary>
        public bool CaseSensitive { get; set; } = false;

        /// <summary>
        /// Gets or sets whether to enable trailing slash matching.
        /// </summary>
        public bool MatchTrailingSlash { get; set; } = true;

        /// <summary>
        /// Gets or sets the maximum route parameters.
        /// </summary>
        public int MaxRouteParameters { get; set; } = 20;
    }

    /// <summary>
    /// Defines a route.
    /// </summary>
    public class RouteDefinition
    {
        /// <summary>
        /// Gets or sets the route pattern.
        /// </summary>
        public string Pattern { get; set; }

        /// <summary>
        /// Gets or sets the HTTP methods this route handles.
        /// </summary>
        public List<string> Methods { get; set; } = new List<string>();

        /// <summary>
        /// Gets or sets the handler name.
        /// </summary>
        public string Handler { get; set; }

        /// <summary>
        /// Gets or sets route metadata.
        /// </summary>
        public Dictionary<string, object> Metadata { get; set; } = new Dictionary<string, object>();

        /// <summary>
        /// Gets or sets the route priority (higher = higher priority).
        /// </summary>
        public int Priority { get; set; } = 0;
    }

    /// <summary>
    /// Represents a route match result.
    /// </summary>
    public class RouteMatch
    {
        /// <summary>
        /// Gets or sets the matched route.
        /// </summary>
        public RouteDefinition Route { get; set; }

        /// <summary>
        /// Gets or sets the extracted parameters.
        /// </summary>
        public Dictionary<string, string> Parameters { get; set; } = new Dictionary<string, string>();

        /// <summary>
        /// Gets or sets the handler name.
        /// </summary>
        public string Handler { get; set; }
    }

    /// <summary>
    /// HTTP router filter for path-based routing.
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
            _compiledRoutes = CompileRoutes(config.Routes);
        }

        /// <summary>
        /// Processes data through the HTTP router.
        /// </summary>
        /// <param name="data">The data to process.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>The processed result.</returns>
        public override async Task<FilterResult> ProcessAsync(object data, CancellationToken cancellationToken = default)
        {
            try
            {
                if (data is not HttpMessage httpMessage)
                {
                    return FilterResult.Error("HttpRouterFilter requires HttpMessage input");
                }

                if (!httpMessage.IsRequest)
                {
                    // Pass through responses unchanged
                    return FilterResult.Success(data);
                }

                // Match route
                var match = MatchRoute(httpMessage.Method, httpMessage.Path);
                if (match == null)
                {
                    _logger?.LogWarning("No route matched for {Method} {Path}", httpMessage.Method, httpMessage.Path);
                    
                    if (!string.IsNullOrEmpty(_config.DefaultHandler))
                    {
                        match = new RouteMatch { Handler = _config.DefaultHandler };
                    }
                    else
                    {
                        return FilterResult.Error("No matching route found", 404);
                    }
                }

                // Add route information to message
                if (httpMessage.Headers == null)
                {
                    httpMessage.Headers = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
                }

                // Add route handler header
                httpMessage.Headers["X-Route-Handler"] = new List<string> { match.Handler };

                // Add route parameters as headers
                foreach (var param in match.Parameters)
                {
                    httpMessage.Headers[$"X-Route-Param-{param.Key}"] = new List<string> { param.Value };
                }

                _logger?.LogDebug("Routed {Method} {Path} to handler {Handler}",
                    httpMessage.Method, httpMessage.Path, match.Handler);

                return FilterResult.Success(httpMessage);
            }
            catch (Exception ex)
            {
                _logger?.LogError(ex, "Error in HTTP routing");
                return FilterResult.Error($"Routing error: {ex.Message}");
            }
        }

        /// <summary>
        /// Matches a route for the given method and path.
        /// </summary>
        private RouteMatch MatchRoute(string method, string path)
        {
            if (string.IsNullOrEmpty(method) || string.IsNullOrEmpty(path))
            {
                return null;
            }

            // Normalize path
            path = NormalizePath(path);

            foreach (var compiledRoute in _compiledRoutes)
            {
                // Check method
                if (compiledRoute.Route.Methods.Count > 0 && 
                    !compiledRoute.Route.Methods.Contains(method, StringComparer.OrdinalIgnoreCase))
                {
                    continue;
                }

                // Try to match path
                var match = compiledRoute.Regex.Match(path);
                if (match.Success)
                {
                    var routeMatch = new RouteMatch
                    {
                        Route = compiledRoute.Route,
                        Handler = compiledRoute.Route.Handler
                    };

                    // Extract parameters
                    for (int i = 0; i < compiledRoute.ParameterNames.Count && i < match.Groups.Count - 1; i++)
                    {
                        var paramName = compiledRoute.ParameterNames[i];
                        var paramValue = match.Groups[i + 1].Value;
                        routeMatch.Parameters[paramName] = Uri.UnescapeDataString(paramValue);
                    }

                    return routeMatch;
                }
            }

            return null;
        }

        /// <summary>
        /// Compiles route definitions into regex patterns.
        /// </summary>
        private List<CompiledRoute> CompileRoutes(List<RouteDefinition> routes)
        {
            var compiledRoutes = new List<CompiledRoute>();

            foreach (var route in routes.OrderByDescending(r => r.Priority))
            {
                var compiledRoute = CompileRoute(route);
                if (compiledRoute != null)
                {
                    compiledRoutes.Add(compiledRoute);
                }
            }

            return compiledRoutes;
        }

        /// <summary>
        /// Compiles a single route definition.
        /// </summary>
        private CompiledRoute CompileRoute(RouteDefinition route)
        {
            if (string.IsNullOrEmpty(route.Pattern))
            {
                return null;
            }

            var pattern = route.Pattern;
            var parameterNames = new List<string>();

            // Extract parameter names and build regex pattern
            // Support patterns like: /users/{id}, /api/{version}/items/{*path}
            var regexPattern = "^" + Regex.Replace(pattern, @"\{([^}]+)\}", match =>
            {
                var paramName = match.Groups[1].Value;
                
                // Handle wildcard parameters
                if (paramName.StartsWith("*"))
                {
                    paramName = paramName.Substring(1);
                    parameterNames.Add(paramName);
                    return "(.*)"; // Match everything
                }
                else
                {
                    parameterNames.Add(paramName);
                    return "([^/]+)"; // Match until next slash
                }
            }) + "$";

            var options = _config.CaseSensitive ? RegexOptions.None : RegexOptions.IgnoreCase;
            var regex = new Regex(regexPattern, options | RegexOptions.Compiled);

            return new CompiledRoute
            {
                Route = route,
                Regex = regex,
                ParameterNames = parameterNames
            };
        }

        /// <summary>
        /// Normalizes a path for matching.
        /// </summary>
        private string NormalizePath(string path)
        {
            if (string.IsNullOrEmpty(path))
            {
                return "/";
            }

            // Remove query string
            var queryIndex = path.IndexOf('?');
            if (queryIndex >= 0)
            {
                path = path.Substring(0, queryIndex);
            }

            // Ensure path starts with /
            if (!path.StartsWith("/"))
            {
                path = "/" + path;
            }

            // Handle trailing slash
            if (_config.MatchTrailingSlash && path.Length > 1 && path.EndsWith("/"))
            {
                path = path.TrimEnd('/');
            }

            return path;
        }

        /// <summary>
        /// Adds a route dynamically.
        /// </summary>
        public void AddRoute(RouteDefinition route)
        {
            ArgumentNullException.ThrowIfNull(route);

            _config.Routes.Add(route);
            var compiled = CompileRoute(route);
            if (compiled != null)
            {
                _compiledRoutes.Add(compiled);
                _compiledRoutes.Sort((a, b) => b.Route.Priority.CompareTo(a.Route.Priority));
            }

            _logger?.LogInformation("Added route: {Pattern} -> {Handler}", route.Pattern, route.Handler);
        }

        /// <summary>
        /// Removes a route by pattern.
        /// </summary>
        public bool RemoveRoute(string pattern)
        {
            var route = _config.Routes.FirstOrDefault(r => r.Pattern == pattern);
            if (route != null)
            {
                _config.Routes.Remove(route);
                _compiledRoutes.RemoveAll(cr => cr.Route.Pattern == pattern);
                _logger?.LogInformation("Removed route: {Pattern}", pattern);
                return true;
            }
            return false;
        }

        /// <summary>
        /// Compiled route with regex.
        /// </summary>
        private class CompiledRoute
        {
            public RouteDefinition Route { get; set; }
            public Regex Regex { get; set; }
            public List<string> ParameterNames { get; set; }
        }
    }
}