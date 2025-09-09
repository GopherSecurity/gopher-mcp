using System;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

namespace GopherMcp.Utils
{
    /// <summary>
    /// Compatibility helpers for different .NET versions
    /// </summary>
    internal static class CompatibilityHelpers
    {
        // Empty class - extensions defined below
    }

#if !NET6_0_OR_GREATER
    /// <summary>
    /// Extension methods to provide WaitAsync for older .NET versions
    /// </summary>
    public static class TaskExtensions
    {
        public static async Task<T> WaitAsync<T>(this Task<T> task, TimeSpan timeout, CancellationToken cancellationToken = default)
        {
            using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            cts.CancelAfter(timeout);
            
            var completedTask = await Task.WhenAny(task, Task.Delay(Timeout.Infinite, cts.Token));
            
            if (completedTask != task)
            {
                throw new TimeoutException($"The operation has timed out after {timeout}");
            }
            
            return await task;
        }
        
        public static async Task WaitAsync(this Task task, TimeSpan timeout, CancellationToken cancellationToken = default)
        {
            using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            cts.CancelAfter(timeout);
            
            var completedTask = await Task.WhenAny(task, Task.Delay(Timeout.Infinite, cts.Token));
            
            if (completedTask != task)
            {
                throw new TimeoutException($"The operation has timed out after {timeout}");
            }
            
            await task;
        }
        
        public static Task<T> WaitAsync<T>(this Task<T> task, CancellationToken cancellationToken)
        {
            return WaitAsync(task, Timeout.InfiniteTimeSpan, cancellationToken);
        }
        
        public static Task WaitAsync(this Task task, CancellationToken cancellationToken)
        {
            return WaitAsync(task, Timeout.InfiniteTimeSpan, cancellationToken);
        }
    }
    
    /// <summary>
    /// Provides ArgumentNullException.ThrowIfNull for older .NET versions
    /// </summary>
    internal static class ArgumentValidation
    {
        /// <summary>
        /// Throws an ArgumentNullException if the argument is null.
        /// This mimics the .NET 6+ ArgumentNullException.ThrowIfNull method.
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static void ThrowIfNull(object? argument, [CallerArgumentExpression("argument")] string? paramName = null)
        {
            if (argument is null)
            {
                throw new ArgumentNullException(paramName);
            }
        }
    }
#endif
}

#if !NET5_0_OR_GREATER
namespace System.Runtime.CompilerServices
{
    [AttributeUsage(AttributeTargets.Parameter, AllowMultiple = false, Inherited = false)]
    internal sealed class CallerArgumentExpressionAttribute : Attribute
    {
        public CallerArgumentExpressionAttribute(string parameterName)
        {
            ParameterName = parameterName;
        }

        public string ParameterName { get; }
    }
}
#endif
