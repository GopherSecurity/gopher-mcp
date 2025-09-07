using System;
using System.Threading;
using System.Threading.Tasks;

namespace GopherMcp.Transport
{
    /// <summary>
    /// Represents a message received through the transport
    /// </summary>
    public class JsonRpcMessage
    {
        public string? JsonRpc { get; set; } = "2.0";
        public object? Id { get; set; }
        public string? Method { get; set; }
        public object? Params { get; set; }
        public object? Result { get; set; }
        public JsonRpcError? Error { get; set; }

        public bool IsRequest => Method != null && Id != null;
        public bool IsNotification => Method != null && Id == null;
        public bool IsResponse => Result != null || Error != null;
    }

    /// <summary>
    /// Represents a JSON-RPC error
    /// </summary>
    public class JsonRpcError
    {
        public int Code { get; set; }
        public string Message { get; set; } = string.Empty;
        public object? Data { get; set; }
    }

    /// <summary>
    /// Event arguments for message received events
    /// </summary>
    public class MessageReceivedEventArgs : EventArgs
    {
        public JsonRpcMessage Message { get; }
        public DateTime ReceivedAt { get; }

        public MessageReceivedEventArgs(JsonRpcMessage message)
        {
            Message = message ?? throw new ArgumentNullException(nameof(message));
            ReceivedAt = DateTime.UtcNow;
        }
    }

    /// <summary>
    /// Event arguments for transport error events
    /// </summary>
    public class TransportErrorEventArgs : EventArgs
    {
        public Exception Exception { get; }
        public string? Context { get; }
        public DateTime OccurredAt { get; }

        public TransportErrorEventArgs(Exception exception, string? context = null)
        {
            Exception = exception ?? throw new ArgumentNullException(nameof(exception));
            Context = context;
            OccurredAt = DateTime.UtcNow;
        }
    }

    /// <summary>
    /// Event arguments for connection state change events
    /// </summary>
    public class ConnectionStateEventArgs : EventArgs
    {
        public ConnectionState State { get; }
        public ConnectionState PreviousState { get; }
        public string? Reason { get; }
        public DateTime ChangedAt { get; }

        public ConnectionStateEventArgs(ConnectionState state, ConnectionState previousState, string? reason = null)
        {
            State = state;
            PreviousState = previousState;
            Reason = reason;
            ChangedAt = DateTime.UtcNow;
        }
    }

    /// <summary>
    /// Represents the state of a transport connection
    /// </summary>
    public enum ConnectionState
    {
        Disconnected,
        Connecting,
        Connected,
        Disconnecting,
        Failed
    }

    /// <summary>
    /// Defines the interface for transport implementations
    /// </summary>
    public interface ITransport : IDisposable
    {
        /// <summary>
        /// Gets the current connection state
        /// </summary>
        ConnectionState State { get; }

        /// <summary>
        /// Gets whether the transport is connected
        /// </summary>
        bool IsConnected { get; }

        /// <summary>
        /// Starts the transport and establishes connection
        /// </summary>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <returns>Task representing the async operation</returns>
        Task StartAsync(CancellationToken cancellationToken = default);

        /// <summary>
        /// Stops the transport and closes connection
        /// </summary>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <returns>Task representing the async operation</returns>
        Task StopAsync(CancellationToken cancellationToken = default);

        /// <summary>
        /// Sends a message through the transport
        /// </summary>
        /// <param name="message">The message to send</param>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <returns>Task representing the async operation</returns>
        Task SendAsync(JsonRpcMessage message, CancellationToken cancellationToken = default);

        /// <summary>
        /// Receives a message from the transport
        /// </summary>
        /// <param name="cancellationToken">Cancellation token</param>
        /// <returns>The received message</returns>
        Task<JsonRpcMessage> ReceiveAsync(CancellationToken cancellationToken = default);

        /// <summary>
        /// Event raised when a message is received
        /// </summary>
        event EventHandler<MessageReceivedEventArgs>? MessageReceived;

        /// <summary>
        /// Event raised when an error occurs
        /// </summary>
        event EventHandler<TransportErrorEventArgs>? Error;

        /// <summary>
        /// Event raised when the transport connects
        /// </summary>
        event EventHandler<ConnectionStateEventArgs>? Connected;

        /// <summary>
        /// Event raised when the transport disconnects
        /// </summary>
        event EventHandler<ConnectionStateEventArgs>? Disconnected;
    }
}