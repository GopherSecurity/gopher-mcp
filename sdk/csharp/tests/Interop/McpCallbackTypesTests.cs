using System;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpCallbacks;
using static GopherMcp.Interop.McpTypes;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP callback delegate types and helper classes
    /// </summary>
    public class McpCallbackTypesTests : IDisposable
    {
        private CallbackHolder _callbackHolder;

        public void Dispose()
        {
            _callbackHolder?.Dispose();
        }

        #region Callback Delegate Type Tests

        [Fact]
        public void MCP_CALLBACK_Should_Have_Correct_Signature()
        {
            // Arrange
            var callbackInvoked = false;
            IntPtr receivedUserData = IntPtr.Zero;

            MCP_CALLBACK callback = (userData) =>
            {
                callbackInvoked = true;
                receivedUserData = userData;
            };

            // Act
            var testData = new IntPtr(0x1234);
            callback(testData);

            // Assert
            callbackInvoked.Should().BeTrue();
            receivedUserData.Should().Be(testData);
        }

        [Fact]
        public void MCP_TIMER_CALLBACK_Should_Have_Correct_Signature()
        {
            // Arrange
            var callbackInvoked = false;
            IntPtr receivedUserData = IntPtr.Zero;

            MCP_TIMER_CALLBACK callback = (userData) =>
            {
                callbackInvoked = true;
                receivedUserData = userData;
            };

            // Act
            var testData = new IntPtr(0x5678);
            callback(testData);

            // Assert
            callbackInvoked.Should().BeTrue();
            receivedUserData.Should().Be(testData);
        }

        [Fact]
        public void MCP_ERROR_CALLBACK_Should_Have_Correct_Signature()
        {
            // Arrange
            var callbackInvoked = false;
            mcp_result_t receivedError = mcp_result_t.MCP_OK;
            string receivedMessage = null;
            IntPtr receivedUserData = IntPtr.Zero;

            MCP_ERROR_CALLBACK callback = (error, message, userData) =>
            {
                callbackInvoked = true;
                receivedError = error;
                receivedMessage = message;
                receivedUserData = userData;
            };

            // Act
            var testError = mcp_result_t.MCP_ERROR_TIMEOUT;
            var testMessage = "Connection timeout";
            var testData = new IntPtr(0x9ABC);
            callback(testError, testMessage, testData);

            // Assert
            callbackInvoked.Should().BeTrue();
            receivedError.Should().Be(testError);
            receivedMessage.Should().Be(testMessage);
            receivedUserData.Should().Be(testData);
        }

        [Fact]
        public void MCP_DATA_CALLBACK_Should_Have_Correct_Signature()
        {
            // Arrange
            var callbackInvoked = false;
            IntPtr receivedConnection = IntPtr.Zero;
            IntPtr receivedData = IntPtr.Zero;
            UIntPtr receivedLength = UIntPtr.Zero;
            IntPtr receivedUserData = IntPtr.Zero;

            MCP_DATA_CALLBACK callback = (connection, data, length, userData) =>
            {
                callbackInvoked = true;
                receivedConnection = connection;
                receivedData = data;
                receivedLength = length;
                receivedUserData = userData;
            };

            // Act
            var testConnection = new IntPtr(0x1000);
            var testData = new IntPtr(0x2000);
            var testLength = new UIntPtr(1024);
            var testUserData = new IntPtr(0x3000);
            callback(testConnection, testData, testLength, testUserData);

            // Assert
            callbackInvoked.Should().BeTrue();
            receivedConnection.Should().Be(testConnection);
            receivedData.Should().Be(testData);
            receivedLength.Should().Be(testLength);
            receivedUserData.Should().Be(testUserData);
        }

        [Fact]
        public void MCP_WRITE_CALLBACK_Should_Have_Correct_Signature()
        {
            // Arrange
            var callbackInvoked = false;
            IntPtr receivedConnection = IntPtr.Zero;
            mcp_result_t receivedResult = mcp_result_t.MCP_OK;
            UIntPtr receivedBytesWritten = UIntPtr.Zero;
            IntPtr receivedUserData = IntPtr.Zero;

            MCP_WRITE_CALLBACK callback = (connection, result, bytesWritten, userData) =>
            {
                callbackInvoked = true;
                receivedConnection = connection;
                receivedResult = result;
                receivedBytesWritten = bytesWritten;
                receivedUserData = userData;
            };

            // Act
            var testConnection = new IntPtr(0x4000);
            var testResult = mcp_result_t.MCP_OK;
            var testBytesWritten = new UIntPtr(512);
            var testUserData = new IntPtr(0x5000);
            callback(testConnection, testResult, testBytesWritten, testUserData);

            // Assert
            callbackInvoked.Should().BeTrue();
            receivedConnection.Should().Be(testConnection);
            receivedResult.Should().Be(testResult);
            receivedBytesWritten.Should().Be(testBytesWritten);
            receivedUserData.Should().Be(testUserData);
        }

        [Fact]
        public void MCP_CONNECTION_STATE_CALLBACK_Should_Have_Correct_Signature()
        {
            // Arrange
            var callbackInvoked = false;
            IntPtr receivedConnection = IntPtr.Zero;
            mcp_connection_state_t receivedState = mcp_connection_state_t.MCP_CONNECTION_STATE_IDLE;
            IntPtr receivedUserData = IntPtr.Zero;

            MCP_CONNECTION_STATE_CALLBACK callback = (connection, state, userData) =>
            {
                callbackInvoked = true;
                receivedConnection = connection;
                receivedState = state;
                receivedUserData = userData;
            };

            // Act
            var testConnection = new IntPtr(0x6000);
            var testState = mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTED;
            var testUserData = new IntPtr(0x7000);
            callback(testConnection, testState, testUserData);

            // Assert
            callbackInvoked.Should().BeTrue();
            receivedConnection.Should().Be(testConnection);
            receivedState.Should().Be(testState);
            receivedUserData.Should().Be(testUserData);
        }

        [Fact]
        public void MCP_ACCEPT_CALLBACK_Should_Have_Correct_Signature()
        {
            // Arrange
            var callbackInvoked = false;
            IntPtr receivedListener = IntPtr.Zero;
            IntPtr receivedConnection = IntPtr.Zero;
            IntPtr receivedUserData = IntPtr.Zero;

            MCP_ACCEPT_CALLBACK callback = (listener, connection, userData) =>
            {
                callbackInvoked = true;
                receivedListener = listener;
                receivedConnection = connection;
                receivedUserData = userData;
            };

            // Act
            var testListener = new IntPtr(0x8000);
            var testConnection = new IntPtr(0x9000);
            var testUserData = new IntPtr(0xA000);
            callback(testListener, testConnection, testUserData);

            // Assert
            callbackInvoked.Should().BeTrue();
            receivedListener.Should().Be(testListener);
            receivedConnection.Should().Be(testConnection);
            receivedUserData.Should().Be(testUserData);
        }

        #endregion

        #region Memory Allocator Function Tests

        [Fact]
        public void MCP_ALLOC_FUNC_Should_Have_Correct_Signature()
        {
            // Arrange
            var allocInvoked = false;
            UIntPtr receivedSize = UIntPtr.Zero;
            IntPtr receivedUserData = IntPtr.Zero;

            MCP_ALLOC_FUNC allocFunc = (size, userData) =>
            {
                allocInvoked = true;
                receivedSize = size;
                receivedUserData = userData;
                return Marshal.AllocHGlobal((int)size.ToUInt32());
            };

            // Act
            var testSize = new UIntPtr(256);
            var testUserData = new IntPtr(0xB000);
            var result = allocFunc(testSize, testUserData);

            // Assert
            allocInvoked.Should().BeTrue();
            receivedSize.Should().Be(testSize);
            receivedUserData.Should().Be(testUserData);
            result.Should().NotBe(IntPtr.Zero);

            // Cleanup
            Marshal.FreeHGlobal(result);
        }

        [Fact]
        public void MCP_REALLOC_FUNC_Should_Have_Correct_Signature()
        {
            // Arrange
            var reallocInvoked = false;
            IntPtr receivedPtr = IntPtr.Zero;
            UIntPtr receivedNewSize = UIntPtr.Zero;
            IntPtr receivedUserData = IntPtr.Zero;
            var originalPtr = Marshal.AllocHGlobal(128);

            MCP_REALLOC_FUNC reallocFunc = (ptr, newSize, userData) =>
            {
                reallocInvoked = true;
                receivedPtr = ptr;
                receivedNewSize = newSize;
                receivedUserData = userData;
                return Marshal.ReAllocHGlobal(ptr, (IntPtr)(int)newSize.ToUInt32());
            };

            // Act
            var testNewSize = new UIntPtr(256);
            var testUserData = new IntPtr(0xC000);
            var result = reallocFunc(originalPtr, testNewSize, testUserData);

            // Assert
            reallocInvoked.Should().BeTrue();
            receivedPtr.Should().Be(originalPtr);
            receivedNewSize.Should().Be(testNewSize);
            receivedUserData.Should().Be(testUserData);
            result.Should().NotBe(IntPtr.Zero);

            // Cleanup
            Marshal.FreeHGlobal(result);
        }

        [Fact]
        public void MCP_FREE_FUNC_Should_Have_Correct_Signature()
        {
            // Arrange
            var freeInvoked = false;
            IntPtr receivedPtr = IntPtr.Zero;
            IntPtr receivedUserData = IntPtr.Zero;
            var allocatedPtr = Marshal.AllocHGlobal(128);

            MCP_FREE_FUNC freeFunc = (ptr, userData) =>
            {
                freeInvoked = true;
                receivedPtr = ptr;
                receivedUserData = userData;
                Marshal.FreeHGlobal(ptr);
            };

            // Act
            var testUserData = new IntPtr(0xD000);
            freeFunc(allocatedPtr, testUserData);

            // Assert
            freeInvoked.Should().BeTrue();
            receivedPtr.Should().Be(allocatedPtr);
            receivedUserData.Should().Be(testUserData);
        }

        #endregion

        #region CallbackHolder Tests

        [Fact]
        public void CallbackHolder_Should_Throw_On_Null_Callback()
        {
            // Arrange & Act
            Action act = () => new CallbackHolder(null);

            // Assert
            act.Should().Throw<ArgumentNullException>()
                .WithParameterName("callback");
        }

        [Fact]
        public void CallbackHolder_Should_Hold_Callback_Reference()
        {
            // Arrange
            MCP_CALLBACK callback = (userData) => { };

            // Act
            _callbackHolder = new CallbackHolder(callback);

            // Assert
            _callbackHolder.Should().NotBeNull();
        }

        [Fact]
        public void CallbackHolder_GetFunctionPointer_Should_Return_Valid_Pointer()
        {
            // Arrange
            MCP_CALLBACK callback = (userData) => { };
            _callbackHolder = new CallbackHolder(callback);

            // Act
            var functionPointer = _callbackHolder.GetFunctionPointer();

            // Assert
            functionPointer.Should().NotBe(IntPtr.Zero);
        }

        [Fact]
        public void CallbackHolder_Should_Be_Disposable()
        {
            // Arrange
            MCP_CALLBACK callback = (userData) => { };
            var holder = new CallbackHolder(callback);

            // Act
            holder.Dispose();

            // Assert
            // Should not throw
            holder.Dispose(); // Double dispose should be safe
        }

        [Fact]
        public void CallbackHolder_CreateCallback_Should_Create_Valid_Holder()
        {
            // Arrange
            MCP_ERROR_CALLBACK callback = (error, message, userData) => { };

            // Act
            _callbackHolder = CreateCallback(callback);

            // Assert
            _callbackHolder.Should().NotBeNull();
            _callbackHolder.GetFunctionPointer().Should().NotBe(IntPtr.Zero);
        }

        [Fact]
        public void CallbackHolder_Should_Prevent_GC_Collection()
        {
            // Arrange
            var callbackInvoked = false;
            MCP_CALLBACK callback = (userData) => { callbackInvoked = true; };
            
            _callbackHolder = new CallbackHolder(callback);
            var functionPointer = _callbackHolder.GetFunctionPointer();

            // Act
            // Force garbage collection
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();

            // The callback should still be invokable through the function pointer
            // In a real scenario, this would be called from native code
            var delegateFromPointer = Marshal.GetDelegateForFunctionPointer<MCP_CALLBACK>(functionPointer);
            delegateFromPointer(IntPtr.Zero);

            // Assert
            callbackInvoked.Should().BeTrue();
        }

        #endregion

        #region Platform-Specific Calling Convention Tests

        [Fact]
        public void Callbacks_Should_Use_Platform_Appropriate_Calling_Convention()
        {
            // Arrange
            var callbackType = typeof(MCP_CALLBACK);
            var attributes = callbackType.GetCustomAttributes(typeof(UnmanagedFunctionPointerAttribute), false);

            // Assert
            attributes.Should().HaveCount(1);
            var attr = (UnmanagedFunctionPointerAttribute)attributes[0];

#if WINDOWS
            attr.CallingConvention.Should().Be(CallingConvention.StdCall,
                "Windows should use StdCall calling convention");
#else
            attr.CallingConvention.Should().Be(CallingConvention.Cdecl,
                "Non-Windows platforms should use Cdecl calling convention");
#endif
        }

        [Fact]
        public void All_Callback_Delegates_Should_Have_UnmanagedFunctionPointer_Attribute()
        {
            // Arrange
            var callbackTypes = new[]
            {
                typeof(MCP_CALLBACK),
                typeof(MCP_TIMER_CALLBACK),
                typeof(MCP_ERROR_CALLBACK),
                typeof(MCP_DATA_CALLBACK),
                typeof(MCP_WRITE_CALLBACK),
                typeof(MCP_CONNECTION_STATE_CALLBACK),
                typeof(MCP_ACCEPT_CALLBACK),
                typeof(MCP_REQUEST_CALLBACK),
                typeof(MCP_RESPONSE_CALLBACK),
                typeof(MCP_NOTIFICATION_CALLBACK),
                typeof(MCP_ALLOC_FUNC),
                typeof(MCP_REALLOC_FUNC),
                typeof(MCP_FREE_FUNC)
            };

            // Act & Assert
            foreach (var type in callbackTypes)
            {
                var attributes = type.GetCustomAttributes(typeof(UnmanagedFunctionPointerAttribute), false);
                attributes.Should().HaveCount(1, $"{type.Name} should have UnmanagedFunctionPointer attribute");
            }
        }

        #endregion
    }
}
