using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using GopherMcp.Types;

namespace GopherMcp.Utils
{
    /// <summary>
    /// Manages callback lifecycles for native interop, preventing premature garbage collection
    /// </summary>
    public sealed class CallbackManager : IDisposable
    {
        private readonly ConcurrentDictionary<Guid, CallbackRegistration> _callbacks;
        private readonly ConcurrentDictionary<IntPtr, WeakCallbackRegistration> _weakCallbacks;
        private readonly ReaderWriterLockSlim _callbackLock;
        private readonly Timer _cleanupTimer;
        private readonly CallbackStatistics _statistics;
        private bool _disposed;
        
        /// <summary>
        /// Default instance for shared usage
        /// </summary>
        public static CallbackManager Default { get; } = new CallbackManager();
        
        /// <summary>
        /// Gets the current callback statistics
        /// </summary>
        public CallbackStatistics Statistics => _statistics.Clone();
        
        /// <summary>
        /// Gets or sets whether to throw exceptions from callbacks
        /// </summary>
        public bool ThrowOnCallbackException { get; set; } = false;
        
        /// <summary>
        /// Event raised when a callback exception occurs
        /// </summary>
        public event EventHandler<CallbackExceptionEventArgs> CallbackException;
        
        /// <summary>
        /// Initializes a new instance of the CallbackManager class
        /// </summary>
        public CallbackManager()
        {
            _callbacks = new ConcurrentDictionary<Guid, CallbackRegistration>();
            _weakCallbacks = new ConcurrentDictionary<IntPtr, WeakCallbackRegistration>();
            _callbackLock = new ReaderWriterLockSlim(LockRecursionPolicy.SupportsRecursion);
            _statistics = new CallbackStatistics();
            
            // Start cleanup timer to remove dead weak references (every 30 seconds)
            _cleanupTimer = new Timer(CleanupDeadReferences, null, TimeSpan.FromSeconds(30), TimeSpan.FromSeconds(30));
        }
        
        /// <summary>
        /// Registers a native callback delegate to prevent garbage collection
        /// </summary>
        /// <typeparam name="T">The delegate type</typeparam>
        /// <param name="callback">The callback delegate</param>
        /// <param name="context">Optional context object</param>
        /// <returns>Registration token for unregistering</returns>
        public CallbackToken RegisterCallback<T>(T callback, object context = null) where T : Delegate
        {
            ThrowIfDisposed();
            
            if (callback == null)
                throw new ArgumentNullException(nameof(callback));
            
            var token = Guid.NewGuid();
            var registration = new CallbackRegistration(callback, context, typeof(T));
            
            if (!_callbacks.TryAdd(token, registration))
            {
                throw new InvalidOperationException("Failed to register callback");
            }
            
            // Pin the delegate to prevent GC
            var handle = GCHandle.Alloc(callback);
            registration.Handle = handle;
            
            Interlocked.Increment(ref _statistics._totalRegistrations);
            Interlocked.Increment(ref _statistics._activeCallbacks);
            
            return new CallbackToken(token, this);
        }
        
        /// <summary>
        /// Registers a weak reference to a callback that can be garbage collected
        /// </summary>
        /// <typeparam name="T">The delegate type</typeparam>
        /// <param name="callback">The callback delegate</param>
        /// <param name="context">Optional context object</param>
        /// <returns>Registration token for unregistering</returns>
        public CallbackToken RegisterWeakCallback<T>(T callback, object context = null) where T : Delegate
        {
            ThrowIfDisposed();
            
            if (callback == null)
                throw new ArgumentNullException(nameof(callback));
            
            var token = Guid.NewGuid();
            var weakRef = new WeakReference(callback, false);
            var registration = new WeakCallbackRegistration(weakRef, context, typeof(T), token);
            
            // Use callback's function pointer as key for weak callbacks
            var funcPtr = Marshal.GetFunctionPointerForDelegate(callback);
            if (!_weakCallbacks.TryAdd(funcPtr, registration))
            {
                throw new InvalidOperationException("Weak callback already registered");
            }
            
            Interlocked.Increment(ref _statistics._totalRegistrations);
            Interlocked.Increment(ref _statistics._weakCallbacks);
            
            return new CallbackToken(token, this, true);
        }
        
        /// <summary>
        /// Unregisters a callback using its token
        /// </summary>
        /// <param name="token">The callback token</param>
        public void UnregisterCallback(CallbackToken token)
        {
            if (token == null || token.IsInvalid)
                return;
            
            ThrowIfDisposed();
            
            if (token.IsWeak)
            {
                // Find and remove weak callback
                foreach (var kvp in _weakCallbacks)
                {
                    if (kvp.Value.Token == token.Id)
                    {
                        if (_weakCallbacks.TryRemove(kvp.Key, out _))
                        {
                            Interlocked.Decrement(ref _statistics._weakCallbacks);
                            Interlocked.Increment(ref _statistics._totalUnregistrations);
                        }
                        break;
                    }
                }
            }
            else
            {
                // Remove strong callback
                if (_callbacks.TryRemove(token.Id, out var registration))
                {
                    registration.Handle?.Free();
                    registration.Dispose();
                    
                    Interlocked.Decrement(ref _statistics._activeCallbacks);
                    Interlocked.Increment(ref _statistics._totalUnregistrations);
                }
            }
            
            token.Invalidate();
        }
        
        /// <summary>
        /// Invokes a registered callback safely with exception handling
        /// </summary>
        /// <typeparam name="T">The delegate type</typeparam>
        /// <param name="token">The callback token</param>
        /// <param name="args">Arguments to pass to the callback</param>
        /// <returns>The callback result, or default if failed</returns>
        public object InvokeCallback<T>(CallbackToken token, params object[] args) where T : Delegate
        {
            ThrowIfDisposed();
            
            if (token == null || token.IsInvalid)
                return null;
            
            try
            {
                Delegate callback = null;
                object context = null;
                
                if (token.IsWeak)
                {
                    // Find weak callback
                    foreach (var kvp in _weakCallbacks)
                    {
                        if (kvp.Value.Token == token.Id)
                        {
                            callback = kvp.Value.WeakReference.Target as Delegate;
                            context = kvp.Value.Context;
                            break;
                        }
                    }
                    
                    if (callback == null)
                    {
                        // Callback was garbage collected
                        Interlocked.Increment(ref _statistics._garbageCollectedCallbacks);
                        return null;
                    }
                }
                else
                {
                    // Get strong callback
                    if (_callbacks.TryGetValue(token.Id, out var registration))
                    {
                        callback = registration.Callback;
                        context = registration.Context;
                    }
                }
                
                if (callback == null)
                    return null;
                
                // Record invocation
                Interlocked.Increment(ref _statistics._totalInvocations);
                
                // Invoke the callback
                var stopwatch = Stopwatch.StartNew();
                var result = callback.DynamicInvoke(args);
                stopwatch.Stop();
                
                // Update statistics
                Interlocked.Add(ref _statistics._totalInvocationTimeMs, stopwatch.ElapsedMilliseconds);
                if (stopwatch.ElapsedMilliseconds > _statistics._maxInvocationTimeMs)
                {
                    Interlocked.Exchange(ref _statistics._maxInvocationTimeMs, stopwatch.ElapsedMilliseconds);
                }
                
                return result;
            }
            catch (Exception ex)
            {
                HandleCallbackException(ex, token);
                return null;
            }
        }
        
        /// <summary>
        /// Invokes a callback asynchronously
        /// </summary>
        /// <typeparam name="T">The delegate type</typeparam>
        /// <param name="token">The callback token</param>
        /// <param name="args">Arguments to pass to the callback</param>
        /// <returns>Task with the callback result</returns>
        public Task<object> InvokeCallbackAsync<T>(CallbackToken token, params object[] args) where T : Delegate
        {
            return Task.Run(() => InvokeCallback<T>(token, args));
        }
        
        /// <summary>
        /// Gets the context associated with a callback
        /// </summary>
        /// <param name="token">The callback token</param>
        /// <returns>The context object, or null if not found</returns>
        public object GetCallbackContext(CallbackToken token)
        {
            ThrowIfDisposed();
            
            if (token == null || token.IsInvalid)
                return null;
            
            if (token.IsWeak)
            {
                foreach (var kvp in _weakCallbacks)
                {
                    if (kvp.Value.Token == token.Id)
                        return kvp.Value.Context;
                }
            }
            else
            {
                if (_callbacks.TryGetValue(token.Id, out var registration))
                    return registration.Context;
            }
            
            return null;
        }
        
        /// <summary>
        /// Creates a native function pointer for a managed delegate
        /// </summary>
        /// <typeparam name="T">The delegate type</typeparam>
        /// <param name="callback">The managed callback</param>
        /// <returns>Function pointer that can be passed to native code</returns>
        public IntPtr CreateNativeFunctionPointer<T>(T callback) where T : Delegate
        {
            ThrowIfDisposed();
            
            if (callback == null)
                throw new ArgumentNullException(nameof(callback));
            
            // Register the callback to prevent GC
            var token = RegisterCallback(callback);
            
            // Get the function pointer
            var funcPtr = Marshal.GetFunctionPointerForDelegate(callback);
            
            // Store the association for later cleanup
            if (_callbacks.TryGetValue(token.Id, out var registration))
            {
                registration.FunctionPointer = funcPtr;
            }
            
            return funcPtr;
        }
        
        /// <summary>
        /// Cleans up dead weak references
        /// </summary>
        private void CleanupDeadReferences(object state)
        {
            if (_disposed)
                return;
            
            var deadRefs = new List<IntPtr>();
            
            foreach (var kvp in _weakCallbacks)
            {
                if (!kvp.Value.WeakReference.IsAlive)
                {
                    deadRefs.Add(kvp.Key);
                }
            }
            
            foreach (var key in deadRefs)
            {
                if (_weakCallbacks.TryRemove(key, out _))
                {
                    Interlocked.Increment(ref _statistics._garbageCollectedCallbacks);
                    Interlocked.Decrement(ref _statistics._weakCallbacks);
                }
            }
        }
        
        /// <summary>
        /// Handles exceptions from callback invocations
        /// </summary>
        private void HandleCallbackException(Exception ex, CallbackToken token)
        {
            Interlocked.Increment(ref _statistics._failedInvocations);
            
            var eventArgs = new CallbackExceptionEventArgs(ex, token);
            CallbackException?.Invoke(this, eventArgs);
            
            if (ThrowOnCallbackException)
            {
                throw new CallbackInvocationException("Callback invocation failed", ex, token);
            }
        }
        
        /// <summary>
        /// Throws if the manager has been disposed
        /// </summary>
        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(CallbackManager));
        }
        
        /// <summary>
        /// Disposes the callback manager and releases all resources
        /// </summary>
        public void Dispose()
        {
            if (_disposed)
                return;
            
            _disposed = true;
            
            // Stop the cleanup timer
            _cleanupTimer?.Dispose();
            
            // Unregister all callbacks
            foreach (var registration in _callbacks.Values)
            {
                registration.Handle?.Free();
                registration.Dispose();
            }
            _callbacks.Clear();
            
            _weakCallbacks.Clear();
            
            // Dispose the lock
            _callbackLock?.Dispose();
        }
        
        /// <summary>
        /// Represents a registered callback
        /// </summary>
        private class CallbackRegistration : IDisposable
        {
            public Delegate Callback { get; }
            public object Context { get; }
            public Type CallbackType { get; }
            public GCHandle? Handle { get; set; }
            public IntPtr FunctionPointer { get; set; }
            
            public CallbackRegistration(Delegate callback, object context, Type callbackType)
            {
                Callback = callback;
                Context = context;
                CallbackType = callbackType;
            }
            
            public void Dispose()
            {
                Handle?.Free();
            }
        }
        
        /// <summary>
        /// Represents a weak callback registration
        /// </summary>
        private class WeakCallbackRegistration
        {
            public WeakReference WeakReference { get; }
            public object Context { get; }
            public Type CallbackType { get; }
            public Guid Token { get; }
            
            public WeakCallbackRegistration(WeakReference weakReference, object context, Type callbackType, Guid token)
            {
                WeakReference = weakReference;
                Context = context;
                CallbackType = callbackType;
                Token = token;
            }
        }
        
        /// <summary>
        /// Token representing a registered callback
        /// </summary>
        public sealed class CallbackToken
        {
            private readonly CallbackManager _manager;
            private bool _isInvalid;
            
            /// <summary>
            /// Gets the unique identifier for this callback
            /// </summary>
            public Guid Id { get; }
            
            /// <summary>
            /// Gets whether this is a weak callback reference
            /// </summary>
            public bool IsWeak { get; }
            
            /// <summary>
            /// Gets whether the token is invalid
            /// </summary>
            public bool IsInvalid => _isInvalid;
            
            internal CallbackToken(Guid id, CallbackManager manager, bool isWeak = false)
            {
                Id = id;
                _manager = manager;
                IsWeak = isWeak;
            }
            
            /// <summary>
            /// Unregisters this callback
            /// </summary>
            public void Unregister()
            {
                _manager?.UnregisterCallback(this);
            }
            
            internal void Invalidate()
            {
                _isInvalid = true;
            }
        }
        
        /// <summary>
        /// Callback statistics
        /// </summary>
        public class CallbackStatistics
        {
            internal long _totalRegistrations;
            internal long _totalUnregistrations;
            internal long _activeCallbacks;
            internal long _weakCallbacks;
            internal long _totalInvocations;
            internal long _failedInvocations;
            internal long _garbageCollectedCallbacks;
            internal long _totalInvocationTimeMs;
            internal long _maxInvocationTimeMs;
            
            /// <summary>
            /// Gets the total number of callback registrations
            /// </summary>
            public long TotalRegistrations => _totalRegistrations;
            
            /// <summary>
            /// Gets the total number of callback unregistrations
            /// </summary>
            public long TotalUnregistrations => _totalUnregistrations;
            
            /// <summary>
            /// Gets the number of active callbacks
            /// </summary>
            public long ActiveCallbacks => _activeCallbacks;
            
            /// <summary>
            /// Gets the number of weak callbacks
            /// </summary>
            public long WeakCallbacks => _weakCallbacks;
            
            /// <summary>
            /// Gets the total number of callback invocations
            /// </summary>
            public long TotalInvocations => _totalInvocations;
            
            /// <summary>
            /// Gets the number of failed invocations
            /// </summary>
            public long FailedInvocations => _failedInvocations;
            
            /// <summary>
            /// Gets the number of garbage collected callbacks
            /// </summary>
            public long GarbageCollectedCallbacks => _garbageCollectedCallbacks;
            
            /// <summary>
            /// Gets the total invocation time in milliseconds
            /// </summary>
            public long TotalInvocationTimeMs => _totalInvocationTimeMs;
            
            /// <summary>
            /// Gets the maximum invocation time in milliseconds
            /// </summary>
            public long MaxInvocationTimeMs => _maxInvocationTimeMs;
            
            /// <summary>
            /// Gets the average invocation time in milliseconds
            /// </summary>
            public double AverageInvocationTimeMs => 
                TotalInvocations > 0 ? (double)TotalInvocationTimeMs / TotalInvocations : 0;
            
            /// <summary>
            /// Clones the statistics
            /// </summary>
            public CallbackStatistics Clone()
            {
                return new CallbackStatistics
                {
                    _totalRegistrations = _totalRegistrations,
                    _totalUnregistrations = _totalUnregistrations,
                    _activeCallbacks = _activeCallbacks,
                    _weakCallbacks = _weakCallbacks,
                    _totalInvocations = _totalInvocations,
                    _failedInvocations = _failedInvocations,
                    _garbageCollectedCallbacks = _garbageCollectedCallbacks,
                    _totalInvocationTimeMs = _totalInvocationTimeMs,
                    _maxInvocationTimeMs = _maxInvocationTimeMs
                };
            }
            
            /// <summary>
            /// Gets a string representation of the statistics
            /// </summary>
            public override string ToString()
            {
                return $"CallbackStatistics: Active={ActiveCallbacks}, Weak={WeakCallbacks}, " +
                       $"Invocations={TotalInvocations}, Failed={FailedInvocations}, " +
                       $"AvgTime={AverageInvocationTimeMs:F2}ms";
            }
        }
        
        /// <summary>
        /// Event arguments for callback exceptions
        /// </summary>
        public class CallbackExceptionEventArgs : EventArgs
        {
            /// <summary>
            /// Gets the exception that occurred
            /// </summary>
            public Exception Exception { get; }
            
            /// <summary>
            /// Gets the callback token
            /// </summary>
            public CallbackToken Token { get; }
            
            /// <summary>
            /// Gets the timestamp of the exception
            /// </summary>
            public DateTime Timestamp { get; }
            
            /// <summary>
            /// Initializes a new instance of CallbackExceptionEventArgs
            /// </summary>
            public CallbackExceptionEventArgs(Exception exception, CallbackToken token)
            {
                Exception = exception;
                Token = token;
                Timestamp = DateTime.UtcNow;
            }
        }
        
        /// <summary>
        /// Exception thrown when callback invocation fails
        /// </summary>
        public class CallbackInvocationException : Exception
        {
            /// <summary>
            /// Gets the callback token
            /// </summary>
            public CallbackToken Token { get; }
            
            /// <summary>
            /// Initializes a new instance of CallbackInvocationException
            /// </summary>
            public CallbackInvocationException(string message, Exception innerException, CallbackToken token)
                : base(message, innerException)
            {
                Token = token;
            }
        }
    }
}