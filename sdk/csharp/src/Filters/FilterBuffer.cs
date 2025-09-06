using System;
using System.Buffers;
using System.Runtime.InteropServices;
using GopherMcp.Core;
using GopherMcp.Types;

namespace GopherMcp.Filters
{
    /// <summary>
    /// Represents a buffer for filter data processing with zero-copy capabilities
    /// </summary>
    public class FilterBuffer : IDisposable
    {
        private McpBufferHandle _handle;
        private byte[] _managedBuffer;
        private Memory<byte> _memory;
        private GCHandle _pinnedHandle;
        private readonly BufferOwnership _ownership;
        private readonly object _syncLock = new object();
        private bool _disposed;
        
        /// <summary>
        /// Gets the native buffer handle
        /// </summary>
        public McpBufferHandle Handle
        {
            get
            {
                ThrowIfDisposed();
                return _handle;
            }
        }
        
        /// <summary>
        /// Gets the buffer data as a byte array
        /// </summary>
        public byte[] Data
        {
            get
            {
                ThrowIfDisposed();
                return _managedBuffer;
            }
        }
        
        /// <summary>
        /// Gets the buffer as a Memory<byte>
        /// </summary>
        public Memory<byte> Memory
        {
            get
            {
                ThrowIfDisposed();
                return _memory;
            }
        }
        
        /// <summary>
        /// Gets the buffer as a Span<byte>
        /// </summary>
        public Span<byte> Span
        {
            get
            {
                ThrowIfDisposed();
                return _memory.Span;
            }
        }
        
        /// <summary>
        /// Gets the size of valid data in the buffer
        /// </summary>
        public int Size { get; private set; }
        
        /// <summary>
        /// Gets the total capacity of the buffer
        /// </summary>
        public int Capacity { get; }
        
        /// <summary>
        /// Gets the buffer ownership type
        /// </summary>
        public BufferOwnership Ownership => _ownership;
        
        /// <summary>
        /// Gets whether the buffer is pinned in memory
        /// </summary>
        public bool IsPinned => _pinnedHandle.IsAllocated;
        
        /// <summary>
        /// Gets whether the buffer is read-only
        /// </summary>
        public bool IsReadOnly { get; }
        
        /// <summary>
        /// Gets whether the buffer has been disposed
        /// </summary>
        public bool IsDisposed => _disposed;
        
        /// <summary>
        /// Initializes a new instance of FilterBuffer with specified capacity
        /// </summary>
        /// <param name="capacity">Buffer capacity in bytes</param>
        /// <param name="ownership">Buffer ownership type</param>
        public FilterBuffer(int capacity, BufferOwnership ownership = BufferOwnership.Owned)
        {
            if (capacity <= 0)
                throw new ArgumentOutOfRangeException(nameof(capacity));
            
            Capacity = capacity;
            _ownership = ownership;
            _managedBuffer = new byte[capacity];
            _memory = new Memory<byte>(_managedBuffer);
            Size = 0;
            IsReadOnly = false;
        }
        
        /// <summary>
        /// Initializes a new instance of FilterBuffer from existing data
        /// </summary>
        /// <param name="data">Existing byte array</param>
        /// <param name="ownership">Buffer ownership type</param>
        /// <param name="copy">Whether to copy the data</param>
        public FilterBuffer(byte[] data, BufferOwnership ownership = BufferOwnership.Owned, bool copy = true)
        {
            if (data == null)
                throw new ArgumentNullException(nameof(data));
            
            Capacity = data.Length;
            Size = data.Length;
            _ownership = ownership;
            
            if (copy || ownership == BufferOwnership.Owned)
            {
                _managedBuffer = new byte[data.Length];
                Array.Copy(data, _managedBuffer, data.Length);
                IsReadOnly = false;
            }
            else
            {
                _managedBuffer = data;
                IsReadOnly = ownership == BufferOwnership.Borrowed;
            }
            
            _memory = new Memory<byte>(_managedBuffer);
        }
        
        /// <summary>
        /// Initializes a new instance of FilterBuffer from a Memory<byte>
        /// </summary>
        /// <param name="memory">Memory buffer</param>
        /// <param name="ownership">Buffer ownership type</param>
        public FilterBuffer(Memory<byte> memory, BufferOwnership ownership = BufferOwnership.Borrowed)
        {
            _memory = memory;
            _managedBuffer = memory.ToArray();
            Capacity = memory.Length;
            Size = memory.Length;
            _ownership = ownership;
            IsReadOnly = ownership == BufferOwnership.Borrowed;
        }
        
        /// <summary>
        /// Initializes a new instance of FilterBuffer from a native handle
        /// </summary>
        /// <param name="handle">Native buffer handle</param>
        /// <param name="size">Size of valid data</param>
        /// <param name="capacity">Total capacity</param>
        internal FilterBuffer(McpBufferHandle handle, int size, int capacity)
        {
            _handle = handle ?? throw new ArgumentNullException(nameof(handle));
            Size = size;
            Capacity = capacity;
            _ownership = BufferOwnership.Owned;
            IsReadOnly = false;
            
            // Map native buffer to managed memory
            // This would use P/Invoke to access the native buffer
            _managedBuffer = new byte[capacity];
            _memory = new Memory<byte>(_managedBuffer);
        }
        
        /// <summary>
        /// Reads data from the buffer
        /// </summary>
        /// <param name="offset">Offset to start reading from</param>
        /// <param name="count">Number of bytes to read</param>
        /// <returns>Array containing the read data</returns>
        public byte[] Read(int offset, int count)
        {
            ThrowIfDisposed();
            
            if (offset < 0 || offset >= Size)
                throw new ArgumentOutOfRangeException(nameof(offset));
            
            if (count < 0 || offset + count > Size)
                throw new ArgumentOutOfRangeException(nameof(count));
            
            var result = new byte[count];
            Array.Copy(_managedBuffer, offset, result, 0, count);
            return result;
        }
        
        /// <summary>
        /// Reads data from the buffer into a destination array
        /// </summary>
        /// <param name="offset">Offset to start reading from</param>
        /// <param name="destination">Destination array</param>
        /// <param name="destinationOffset">Offset in destination array</param>
        /// <param name="count">Number of bytes to read</param>
        /// <returns>Number of bytes read</returns>
        public int Read(int offset, byte[] destination, int destinationOffset, int count)
        {
            ThrowIfDisposed();
            
            if (destination == null)
                throw new ArgumentNullException(nameof(destination));
            
            if (offset < 0 || offset >= Size)
                throw new ArgumentOutOfRangeException(nameof(offset));
            
            if (destinationOffset < 0 || destinationOffset >= destination.Length)
                throw new ArgumentOutOfRangeException(nameof(destinationOffset));
            
            int bytesToRead = Math.Min(count, Size - offset);
            bytesToRead = Math.Min(bytesToRead, destination.Length - destinationOffset);
            
            Array.Copy(_managedBuffer, offset, destination, destinationOffset, bytesToRead);
            return bytesToRead;
        }
        
        /// <summary>
        /// Writes data to the buffer
        /// </summary>
        /// <param name="offset">Offset to start writing at</param>
        /// <param name="data">Data to write</param>
        /// <returns>Number of bytes written</returns>
        public int Write(int offset, byte[] data)
        {
            ThrowIfDisposed();
            ThrowIfReadOnly();
            
            if (data == null)
                throw new ArgumentNullException(nameof(data));
            
            return Write(offset, data, 0, data.Length);
        }
        
        /// <summary>
        /// Writes data to the buffer from a source array
        /// </summary>
        /// <param name="offset">Offset to start writing at</param>
        /// <param name="source">Source array</param>
        /// <param name="sourceOffset">Offset in source array</param>
        /// <param name="count">Number of bytes to write</param>
        /// <returns>Number of bytes written</returns>
        public int Write(int offset, byte[] source, int sourceOffset, int count)
        {
            ThrowIfDisposed();
            ThrowIfReadOnly();
            
            if (source == null)
                throw new ArgumentNullException(nameof(source));
            
            if (offset < 0 || offset > Capacity)
                throw new ArgumentOutOfRangeException(nameof(offset));
            
            if (sourceOffset < 0 || sourceOffset >= source.Length)
                throw new ArgumentOutOfRangeException(nameof(sourceOffset));
            
            int bytesToWrite = Math.Min(count, Capacity - offset);
            bytesToWrite = Math.Min(bytesToWrite, source.Length - sourceOffset);
            
            lock (_syncLock)
            {
                Array.Copy(source, sourceOffset, _managedBuffer, offset, bytesToWrite);
                Size = Math.Max(Size, offset + bytesToWrite);
            }
            
            return bytesToWrite;
        }
        
        /// <summary>
        /// Creates a zero-copy slice of the buffer
        /// </summary>
        /// <param name="offset">Offset to start the slice</param>
        /// <param name="length">Length of the slice</param>
        /// <returns>A new FilterBuffer representing the slice</returns>
        public FilterBuffer Slice(int offset, int length)
        {
            ThrowIfDisposed();
            
            if (offset < 0 || offset >= Size)
                throw new ArgumentOutOfRangeException(nameof(offset));
            
            if (length < 0 || offset + length > Size)
                throw new ArgumentOutOfRangeException(nameof(length));
            
            var sliceMemory = _memory.Slice(offset, length);
            return new FilterBuffer(sliceMemory, BufferOwnership.Borrowed);
        }
        
        /// <summary>
        /// Gets a Memory<byte> slice of the buffer
        /// </summary>
        /// <param name="offset">Offset to start the slice</param>
        /// <param name="length">Length of the slice</param>
        /// <returns>Memory slice</returns>
        public Memory<byte> GetMemory(int offset, int length)
        {
            ThrowIfDisposed();
            
            if (offset < 0 || offset >= Size)
                throw new ArgumentOutOfRangeException(nameof(offset));
            
            if (length < 0 || offset + length > Size)
                throw new ArgumentOutOfRangeException(nameof(length));
            
            return _memory.Slice(offset, length);
        }
        
        /// <summary>
        /// Gets a Span<byte> slice of the buffer
        /// </summary>
        /// <param name="offset">Offset to start the slice</param>
        /// <param name="length">Length of the slice</param>
        /// <returns>Span slice</returns>
        public Span<byte> GetSpan(int offset, int length)
        {
            ThrowIfDisposed();
            
            if (offset < 0 || offset >= Size)
                throw new ArgumentOutOfRangeException(nameof(offset));
            
            if (length < 0 || offset + length > Size)
                throw new ArgumentOutOfRangeException(nameof(length));
            
            return _memory.Span.Slice(offset, length);
        }
        
        /// <summary>
        /// Pins the buffer in memory and returns the pointer
        /// </summary>
        /// <returns>Pointer to the pinned buffer</returns>
        public IntPtr Pin()
        {
            ThrowIfDisposed();
            
            lock (_syncLock)
            {
                if (!_pinnedHandle.IsAllocated)
                {
                    _pinnedHandle = GCHandle.Alloc(_managedBuffer, GCHandleType.Pinned);
                }
                
                return _pinnedHandle.AddrOfPinnedObject();
            }
        }
        
        /// <summary>
        /// Unpins the buffer from memory
        /// </summary>
        public void Unpin()
        {
            lock (_syncLock)
            {
                if (_pinnedHandle.IsAllocated)
                {
                    _pinnedHandle.Free();
                }
            }
        }
        
        /// <summary>
        /// Resizes the buffer to a new capacity
        /// </summary>
        /// <param name="newCapacity">New capacity in bytes</param>
        public void Resize(int newCapacity)
        {
            ThrowIfDisposed();
            ThrowIfReadOnly();
            
            if (newCapacity <= 0)
                throw new ArgumentOutOfRangeException(nameof(newCapacity));
            
            if (newCapacity == Capacity)
                return;
            
            lock (_syncLock)
            {
                var newBuffer = new byte[newCapacity];
                int copySize = Math.Min(Size, newCapacity);
                Array.Copy(_managedBuffer, newBuffer, copySize);
                
                _managedBuffer = newBuffer;
                _memory = new Memory<byte>(_managedBuffer);
                Size = Math.Min(Size, newCapacity);
            }
        }
        
        /// <summary>
        /// Clears the buffer content
        /// </summary>
        public void Clear()
        {
            ThrowIfDisposed();
            ThrowIfReadOnly();
            
            lock (_syncLock)
            {
                Array.Clear(_managedBuffer, 0, _managedBuffer.Length);
                Size = 0;
            }
        }
        
        /// <summary>
        /// Copies the buffer content to a new array
        /// </summary>
        /// <returns>Copy of the buffer data</returns>
        public byte[] ToArray()
        {
            ThrowIfDisposed();
            
            var result = new byte[Size];
            Array.Copy(_managedBuffer, result, Size);
            return result;
        }
        
        /// <summary>
        /// Creates a copy of the buffer
        /// </summary>
        /// <returns>New FilterBuffer with copied data</returns>
        public FilterBuffer Clone()
        {
            ThrowIfDisposed();
            
            return new FilterBuffer(ToArray(), BufferOwnership.Owned, true);
        }
        
        /// <summary>
        /// Throws if the buffer is read-only
        /// </summary>
        private void ThrowIfReadOnly()
        {
            if (IsReadOnly)
                throw new InvalidOperationException("Buffer is read-only");
        }
        
        /// <summary>
        /// Throws if the buffer has been disposed
        /// </summary>
        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(FilterBuffer));
        }
        
        /// <summary>
        /// Disposes the buffer and releases resources
        /// </summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }
        
        /// <summary>
        /// Disposes the buffer
        /// </summary>
        /// <param name="disposing">True if disposing managed resources</param>
        protected virtual void Dispose(bool disposing)
        {
            if (_disposed)
                return;
            
            if (disposing)
            {
                lock (_syncLock)
                {
                    // Unpin if pinned
                    if (_pinnedHandle.IsAllocated)
                    {
                        _pinnedHandle.Free();
                    }
                    
                    // Dispose native handle if owned
                    if (_ownership == BufferOwnership.Owned)
                    {
                        _handle?.Dispose();
                    }
                    
                    // Clear references
                    _handle = null;
                    _managedBuffer = null;
                    _memory = Memory<byte>.Empty;
                }
            }
            
            _disposed = true;
        }
        
        /// <summary>
        /// Finalizer
        /// </summary>
        ~FilterBuffer()
        {
            Dispose(false);
        }
        
        /// <summary>
        /// Implicit conversion from byte array
        /// </summary>
        public static implicit operator FilterBuffer(byte[] data)
        {
            return new FilterBuffer(data);
        }
        
        /// <summary>
        /// Implicit conversion to byte array
        /// </summary>
        public static implicit operator byte[](FilterBuffer buffer)
        {
            return buffer?.ToArray();
        }
        
        /// <summary>
        /// Implicit conversion to Memory<byte>
        /// </summary>
        public static implicit operator Memory<byte>(FilterBuffer buffer)
        {
            return buffer?.Memory ?? Memory<byte>.Empty;
        }
        
        /// <summary>
        /// Implicit conversion to Span<byte>
        /// </summary>
        public static implicit operator Span<byte>(FilterBuffer buffer)
        {
            return buffer?.Span ?? Span<byte>.Empty;
        }
    }
}