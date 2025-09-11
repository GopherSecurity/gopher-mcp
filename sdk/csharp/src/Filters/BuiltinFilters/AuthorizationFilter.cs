using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Claims;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;

namespace GopherMcp.Filters.BuiltinFilters
{
    public class AuthorizationPolicy
    {
        public string Name { get; set; } = string.Empty;
        public List<string> RequiredRoles { get; set; } = new();
        public List<string> RequiredClaims { get; set; } = new();
        public Func<ClaimsPrincipal, ProcessingContext, bool>? CustomHandler { get; set; }
        public bool RequireAuthenticatedUser { get; set; } = true;
    }

    public class ResourceAuthorizationRule
    {
        public string Resource { get; set; } = string.Empty;
        public string Action { get; set; } = string.Empty;
        public List<string> AllowedRoles { get; set; } = new();
        public List<string> AllowedUsers { get; set; } = new();
        public Func<ClaimsPrincipal, string, string, bool>? CustomEvaluator { get; set; }
    }

    public class AuthorizationConfig : FilterConfigBase
    {
        public Dictionary<string, AuthorizationPolicy> Policies { get; set; } = new();
        public List<ResourceAuthorizationRule> ResourceRules { get; set; } = new();
        public string DefaultPolicy { get; set; } = "Default";
        public bool RequireAuthenticatedUser { get; set; } = true;
        public bool AllowAnonymousOnBypass { get; set; } = false;
        public List<string> BypassPaths { get; set; } = new();
        public bool EnableRoleBasedAccess { get; set; } = true;
        public bool EnableResourceBasedAccess { get; set; } = false;
        public Dictionary<string, List<string>> RoleHierarchy { get; set; } = new();

        public AuthorizationConfig() : base("Authorization", "AuthorizationFilter")
        {
            Priority = 90; // Run after authentication
            InitializeDefaultPolicies();
        }

        private void InitializeDefaultPolicies()
        {
            Policies["Default"] = new AuthorizationPolicy
            {
                Name = "Default",
                RequireAuthenticatedUser = true
            };

            Policies["Admin"] = new AuthorizationPolicy
            {
                Name = "Admin",
                RequiredRoles = new List<string> { "Admin" },
                RequireAuthenticatedUser = true
            };

            Policies["User"] = new AuthorizationPolicy
            {
                Name = "User",
                RequiredRoles = new List<string> { "User", "Admin" },
                RequireAuthenticatedUser = true
            };
        }

        public override bool Validate(out List<string> errors)
        {
            errors = new List<string>();

            if (!base.Validate(out var baseErrors))
            {
                errors.AddRange(baseErrors);
            }

            if (!Policies.ContainsKey(DefaultPolicy))
            {
                errors.Add($"Default policy '{DefaultPolicy}' not found in configured policies");
            }

            foreach (var policy in Policies.Values)
            {
                if (string.IsNullOrEmpty(policy.Name))
                {
                    errors.Add("Policy name cannot be empty");
                }
            }

            foreach (var rule in ResourceRules)
            {
                if (string.IsNullOrEmpty(rule.Resource))
                {
                    errors.Add("Resource authorization rule must specify a resource");
                }
                if (string.IsNullOrEmpty(rule.Action))
                {
                    errors.Add("Resource authorization rule must specify an action");
                }
            }

            return errors.Count == 0;
        }
    }

    public class AuthorizationFilter : Filter
    {
        private readonly AuthorizationConfig _config;
        private readonly Dictionary<string, List<IAuthorizationHandler>> _handlers;

        public AuthorizationFilter(AuthorizationConfig config) : base(config)
        {
            _config = config ?? throw new ArgumentNullException(nameof(config));
            _handlers = new Dictionary<string, List<IAuthorizationHandler>>();
            InitializeHandlers();
        }

        private void InitializeHandlers()
        {
            // Initialize default handlers for each policy
            foreach (var policy in _config.Policies.Values)
            {
                var handlers = new List<IAuthorizationHandler>();

                if (policy.RequireAuthenticatedUser)
                {
                    handlers.Add(new AuthenticatedUserHandler());
                }

                if (policy.RequiredRoles.Any())
                {
                    handlers.Add(new RoleHandler(policy.RequiredRoles, _config.RoleHierarchy));
                }

                if (policy.RequiredClaims.Any())
                {
                    handlers.Add(new ClaimHandler(policy.RequiredClaims));
                }

                if (policy.CustomHandler != null)
                {
                    handlers.Add(new CustomHandler(policy.CustomHandler));
                }

                _handlers[policy.Name] = handlers;
            }
        }

        public override async Task<FilterResult> ProcessAsync(byte[] buffer, ProcessingContext context, CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();

            try
            {
                // Check bypass rules
                if (ShouldBypass(context))
                {
                    if (_config.AllowAnonymousOnBypass)
                    {
                        return FilterResult.Continue(buffer);
                    }
                }

                // Get user principal from context (set by AuthenticationFilter)
                var principal = context.GetProperty<ClaimsPrincipal>("User");
                
                // Determine which policy to apply
                var policyName = DeterminePolicy(context);
                
                // Check if user is authenticated when required
                if (_config.RequireAuthenticatedUser && (principal == null || !principal.Identity?.IsAuthenticated == true))
                {
                    return FilterResult.Error("User is not authenticated", FilterError.Unauthorized);
                }

                // Apply policy-based authorization
                if (!string.IsNullOrEmpty(policyName))
                {
                    var authResult = await AuthorizeWithPolicyAsync(principal, policyName, context, cancellationToken);
                    if (!authResult.Succeeded)
                    {
                        return FilterResult.Error(authResult.FailureReason ?? "Authorization failed", FilterError.Forbidden);
                    }
                }

                // Apply resource-based authorization if enabled
                if (_config.EnableResourceBasedAccess)
                {
                    var resourceAuthResult = await AuthorizeResourceAccessAsync(principal, context, cancellationToken);
                    if (!resourceAuthResult.Succeeded)
                    {
                        return FilterResult.Error(resourceAuthResult.FailureReason ?? "Resource access denied", FilterError.Forbidden);
                    }
                }

                // Store authorization result in context
                context.SetProperty("AuthorizationSucceeded", true);
                context.SetProperty("AppliedPolicy", policyName);

                UpdateStatistics(1L, 0, true);
                await RaiseOnDataAsync(buffer, 0, buffer.Length, FilterStatus.Continue);
                return FilterResult.Continue(buffer);
            }
            catch (Exception ex)
            {
                UpdateStatistics(0L, 0, false);
                await RaiseOnErrorAsync(ex);
                return FilterResult.Error($"Authorization error: {ex.Message}", FilterError.InternalError);
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

        private string DeterminePolicy(ProcessingContext context)
        {
            // Check if a specific policy is requested
            var requestedPolicy = context.GetProperty<string>("AuthorizationPolicy");
            if (!string.IsNullOrEmpty(requestedPolicy) && _config.Policies.ContainsKey(requestedPolicy))
            {
                return requestedPolicy;
            }

            // Check for method-based policy selection
            var method = context.GetProperty<string>("Method");
            if (!string.IsNullOrEmpty(method))
            {
                var methodPolicy = $"{method}Policy";
                if (_config.Policies.ContainsKey(methodPolicy))
                {
                    return methodPolicy;
                }
            }

            // Return default policy
            return _config.DefaultPolicy;
        }

        private async Task<AuthorizationResult> AuthorizeWithPolicyAsync(
            ClaimsPrincipal? principal, 
            string policyName, 
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
            if (!_config.Policies.TryGetValue(policyName, out var policy))
            {
                return AuthorizationResult.Fail($"Policy '{policyName}' not found");
            }

            if (!_handlers.TryGetValue(policyName, out var handlers))
            {
                return AuthorizationResult.Fail($"No handlers configured for policy '{policyName}'");
            }

            foreach (var handler in handlers)
            {
                var result = await handler.HandleAsync(principal, context, cancellationToken);
                if (!result.Succeeded)
                {
                    return result;
                }
            }

            return AuthorizationResult.Success();
        }

        private async Task<AuthorizationResult> AuthorizeResourceAccessAsync(
            ClaimsPrincipal? principal,
            ProcessingContext context,
            CancellationToken cancellationToken)
        {
            var resource = context.GetProperty<string>("Resource");
            var action = context.GetProperty<string>("Action");

            if (string.IsNullOrEmpty(resource) || string.IsNullOrEmpty(action))
            {
                // No resource/action specified, allow by default
                return AuthorizationResult.Success();
            }

            var applicableRules = _config.ResourceRules
                .Where(r => r.Resource == resource && r.Action == action)
                .ToList();

            if (!applicableRules.Any())
            {
                // No rules defined for this resource/action, use default behavior
                return AuthorizationResult.Success();
            }

            foreach (var rule in applicableRules)
            {
                if (await EvaluateResourceRuleAsync(principal, rule, resource, action, cancellationToken))
                {
                    return AuthorizationResult.Success();
                }
            }

            return AuthorizationResult.Fail($"Access to resource '{resource}' with action '{action}' is denied");
        }

        private async Task<bool> EvaluateResourceRuleAsync(
            ClaimsPrincipal? principal,
            ResourceAuthorizationRule rule,
            string resource,
            string action,
            CancellationToken cancellationToken)
        {
            if (principal == null || !principal.Identity?.IsAuthenticated == true)
            {
                return false;
            }

            // Check custom evaluator first
            if (rule.CustomEvaluator != null)
            {
                return await Task.Run(() => rule.CustomEvaluator(principal, resource, action), cancellationToken);
            }

            // Check allowed users
            var userId = principal.FindFirst(ClaimTypes.NameIdentifier)?.Value;
            if (!string.IsNullOrEmpty(userId) && rule.AllowedUsers.Contains(userId))
            {
                return true;
            }

            // Check allowed roles (considering hierarchy)
            var userRoles = principal.FindAll(ClaimTypes.Role).Select(c => c.Value).ToList();
            foreach (var role in userRoles)
            {
                if (rule.AllowedRoles.Contains(role))
                {
                    return true;
                }

                // Check role hierarchy
                if (_config.RoleHierarchy.TryGetValue(role, out var inheritedRoles))
                {
                    if (inheritedRoles.Any(ir => rule.AllowedRoles.Contains(ir)))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _handlers.Clear();
            }
            base.Dispose(disposing);
        }
    }

    // Authorization handler interfaces and implementations
    internal interface IAuthorizationHandler
    {
        Task<AuthorizationResult> HandleAsync(ClaimsPrincipal? principal, ProcessingContext context, CancellationToken cancellationToken);
    }

    internal class AuthorizationResult
    {
        public bool Succeeded { get; private set; }
        public string? FailureReason { get; private set; }

        private AuthorizationResult(bool succeeded, string? failureReason = null)
        {
            Succeeded = succeeded;
            FailureReason = failureReason;
        }

        public static AuthorizationResult Success() => new(true);
        public static AuthorizationResult Fail(string reason) => new(false, reason);
    }

    internal class AuthenticatedUserHandler : IAuthorizationHandler
    {
        public Task<AuthorizationResult> HandleAsync(ClaimsPrincipal? principal, ProcessingContext context, CancellationToken cancellationToken)
        {
            if (principal == null || !principal.Identity?.IsAuthenticated == true)
            {
                return Task.FromResult(AuthorizationResult.Fail("User is not authenticated"));
            }
            return Task.FromResult(AuthorizationResult.Success());
        }
    }

    internal class RoleHandler : IAuthorizationHandler
    {
        private readonly List<string> _requiredRoles;
        private readonly Dictionary<string, List<string>> _roleHierarchy;

        public RoleHandler(List<string> requiredRoles, Dictionary<string, List<string>> roleHierarchy)
        {
            _requiredRoles = requiredRoles;
            _roleHierarchy = roleHierarchy;
        }

        public Task<AuthorizationResult> HandleAsync(ClaimsPrincipal? principal, ProcessingContext context, CancellationToken cancellationToken)
        {
            if (principal == null)
            {
                return Task.FromResult(AuthorizationResult.Fail("No user principal available"));
            }

            var userRoles = principal.FindAll(ClaimTypes.Role).Select(c => c.Value).ToList();
            
            foreach (var requiredRole in _requiredRoles)
            {
                // Direct role check
                if (userRoles.Contains(requiredRole))
                {
                    return Task.FromResult(AuthorizationResult.Success());
                }

                // Check role hierarchy
                foreach (var userRole in userRoles)
                {
                    if (_roleHierarchy.TryGetValue(userRole, out var inheritedRoles))
                    {
                        if (inheritedRoles.Contains(requiredRole))
                        {
                            return Task.FromResult(AuthorizationResult.Success());
                        }
                    }
                }
            }

            return Task.FromResult(AuthorizationResult.Fail($"User does not have required role(s): {string.Join(", ", _requiredRoles)}"));
        }
    }

    internal class ClaimHandler : IAuthorizationHandler
    {
        private readonly List<string> _requiredClaims;

        public ClaimHandler(List<string> requiredClaims)
        {
            _requiredClaims = requiredClaims;
        }

        public Task<AuthorizationResult> HandleAsync(ClaimsPrincipal? principal, ProcessingContext context, CancellationToken cancellationToken)
        {
            if (principal == null)
            {
                return Task.FromResult(AuthorizationResult.Fail("No user principal available"));
            }

            foreach (var requiredClaim in _requiredClaims)
            {
                if (!principal.HasClaim(c => c.Type == requiredClaim))
                {
                    return Task.FromResult(AuthorizationResult.Fail($"User does not have required claim: {requiredClaim}"));
                }
            }

            return Task.FromResult(AuthorizationResult.Success());
        }
    }

    internal class CustomHandler : IAuthorizationHandler
    {
        private readonly Func<ClaimsPrincipal, ProcessingContext, bool> _handler;

        public CustomHandler(Func<ClaimsPrincipal, ProcessingContext, bool> handler)
        {
            _handler = handler;
        }

        public Task<AuthorizationResult> HandleAsync(ClaimsPrincipal? principal, ProcessingContext context, CancellationToken cancellationToken)
        {
            if (principal == null)
            {
                return Task.FromResult(AuthorizationResult.Fail("No user principal available"));
            }

            try
            {
                var result = _handler(principal, context);
                return Task.FromResult(result 
                    ? AuthorizationResult.Success() 
                    : AuthorizationResult.Fail("Custom authorization handler denied access"));
            }
            catch (Exception ex)
            {
                return Task.FromResult(AuthorizationResult.Fail($"Custom authorization handler failed: {ex.Message}"));
            }
        }
    }
}