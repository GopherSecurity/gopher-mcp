using System;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using Moq;

namespace GopherMcp.Tests
{
    /// <summary>
    /// Unit tests for McpTypesApi P/Invoke bindings
    /// </summary>
    public class McpTypesApiTest : IDisposable
    {
        public McpTypesApiTest()
        {
            // Initialize MCP library before running tests
            // Note: In a real implementation, you'd need to ensure the native library is loaded
            // NativeMethods.mcp_init(IntPtr.Zero);
        }

        public void Dispose()
        {
            // Cleanup after tests
            // NativeMethods.mcp_shutdown();
        }

        #region Request ID Functions Tests

        [Fact]
        public void RequestId_CreateString_ShouldReturnValidHandle()
        {
            // Arrange
            const string testId = "test-request-123";
            
            // Act
            var requestId = McpTypesApi.mcp_request_id_create_string(testId);
            
            // Assert
            requestId.Should().NotBe(IntPtr.Zero, "a valid string request ID should return a non-null handle");
            
            // Cleanup
            if (requestId != IntPtr.Zero)
            {
                McpTypesApi.mcp_request_id_free(requestId);
            }
        }

        [Fact]
        public void RequestId_CreateNumber_ShouldReturnValidHandle()
        {
            // Arrange
            const long testNumber = 42;
            
            // Act
            var requestId = McpTypesApi.mcp_request_id_create_number(testNumber);
            
            // Assert
            requestId.Should().NotBe(IntPtr.Zero, "a valid number request ID should return a non-null handle");
            
            // Cleanup
            if (requestId != IntPtr.Zero)
            {
                McpTypesApi.mcp_request_id_free(requestId);
            }
        }

        [Fact]
        public void RequestId_IsString_ShouldReturnTrueForStringId()
        {
            // Arrange
            const string testId = "string-id";
            var requestId = McpTypesApi.mcp_request_id_create_string(testId);
            
            try
            {
                // Act
                var isString = McpTypesApi.mcp_request_id_is_string(requestId);
                var isNumber = McpTypesApi.mcp_request_id_is_number(requestId);
                
                // Assert
                isString.Should().BeTrue("the ID was created as a string");
                isNumber.Should().BeFalse("the ID was not created as a number");
            }
            finally
            {
                // Cleanup
                if (requestId != IntPtr.Zero)
                {
                    McpTypesApi.mcp_request_id_free(requestId);
                }
            }
        }

        [Fact]
        public void RequestId_IsNumber_ShouldReturnTrueForNumberId()
        {
            // Arrange
            const long testNumber = 999;
            var requestId = McpTypesApi.mcp_request_id_create_number(testNumber);
            
            try
            {
                // Act
                var isString = McpTypesApi.mcp_request_id_is_string(requestId);
                var isNumber = McpTypesApi.mcp_request_id_is_number(requestId);
                
                // Assert
                isNumber.Should().BeTrue("the ID was created as a number");
                isString.Should().BeFalse("the ID was not created as a string");
            }
            finally
            {
                // Cleanup
                if (requestId != IntPtr.Zero)
                {
                    McpTypesApi.mcp_request_id_free(requestId);
                }
            }
        }

        [Fact]
        public void RequestId_GetType_ShouldReturnCorrectType()
        {
            // Test string type
            var stringId = McpTypesApi.mcp_request_id_create_string("test");
            try
            {
                var stringType = McpTypesApi.mcp_request_id_get_type(stringId);
                stringType.Should().Be(McpRequestIdType.String, "string ID should return String type");
            }
            finally
            {
                if (stringId != IntPtr.Zero)
                    McpTypesApi.mcp_request_id_free(stringId);
            }

            // Test number type
            var numberId = McpTypesApi.mcp_request_id_create_number(123);
            try
            {
                var numberType = McpTypesApi.mcp_request_id_get_type(numberId);
                numberType.Should().Be(McpRequestIdType.Number, "number ID should return Number type");
            }
            finally
            {
                if (numberId != IntPtr.Zero)
                    McpTypesApi.mcp_request_id_free(numberId);
            }
        }

        [Fact]
        public void RequestId_GetString_ShouldReturnCorrectValue()
        {
            // Arrange
            const string expectedValue = "unique-request-id";
            var requestId = McpTypesApi.mcp_request_id_create_string(expectedValue);
            
            try
            {
                // Act
                var stringPtr = McpTypesApi.mcp_request_id_get_string(requestId);
                var actualValue = Marshal.PtrToStringUTF8(stringPtr);
                
                // Assert
                actualValue.Should().Be(expectedValue, "the retrieved string should match the original");
            }
            finally
            {
                // Cleanup
                if (requestId != IntPtr.Zero)
                {
                    McpTypesApi.mcp_request_id_free(requestId);
                }
            }
        }

        [Fact]
        public void RequestId_GetNumber_ShouldReturnCorrectValue()
        {
            // Arrange
            const long expectedValue = 12345;
            var requestId = McpTypesApi.mcp_request_id_create_number(expectedValue);
            
            try
            {
                // Act
                var actualValue = McpTypesApi.mcp_request_id_get_number(requestId);
                
                // Assert
                actualValue.Should().Be(expectedValue, "the retrieved number should match the original");
            }
            finally
            {
                // Cleanup
                if (requestId != IntPtr.Zero)
                {
                    McpTypesApi.mcp_request_id_free(requestId);
                }
            }
        }

        [Fact]
        public void RequestId_Clone_ShouldCreateIndependentCopy()
        {
            // Arrange
            const string originalValue = "original-id";
            var originalId = McpTypesApi.mcp_request_id_create_string(originalValue);
            IntPtr clonedId = IntPtr.Zero;
            
            try
            {
                // Act
                clonedId = McpTypesApi.mcp_request_id_clone(originalId);
                
                // Assert
                clonedId.Should().NotBe(IntPtr.Zero, "clone should return a valid handle");
                clonedId.Should().NotBe(originalId, "clone should return a different handle");
                
                var clonedStringPtr = McpTypesApi.mcp_request_id_get_string(clonedId);
                var clonedValue = Marshal.PtrToStringUTF8(clonedStringPtr);
                clonedValue.Should().Be(originalValue, "cloned ID should have the same value");
            }
            finally
            {
                // Cleanup
                if (originalId != IntPtr.Zero)
                    McpTypesApi.mcp_request_id_free(originalId);
                if (clonedId != IntPtr.Zero)
                    McpTypesApi.mcp_request_id_free(clonedId);
            }
        }

        [Fact]
        public void RequestId_IsValid_ShouldReturnTrueForValidHandle()
        {
            // Arrange
            var requestId = McpTypesApi.mcp_request_id_create_string("valid-id");
            
            try
            {
                // Act
                var isValid = McpTypesApi.mcp_request_id_is_valid(requestId);
                
                // Assert
                isValid.Should().BeTrue("a properly created request ID should be valid");
            }
            finally
            {
                // Cleanup
                if (requestId != IntPtr.Zero)
                {
                    McpTypesApi.mcp_request_id_free(requestId);
                }
            }
        }

        [Fact]
        public void RequestId_IsValid_ShouldReturnFalseForNullHandle()
        {
            // Act
            var isValid = McpTypesApi.mcp_request_id_is_valid(IntPtr.Zero);
            
            // Assert
            isValid.Should().BeFalse("a null handle should not be valid");
        }

        [Theory]
        [InlineData("")]
        [InlineData("a")]
        [InlineData("simple-id")]
        [InlineData("complex-id-with-numbers-123456")]
        [InlineData("id-with-special-chars-!@#$%")]
        public void RequestId_StringCreation_ShouldHandleVariousInputs(string input)
        {
            // Act
            var requestId = McpTypesApi.mcp_request_id_create_string(input);
            
            try
            {
                // Assert
                if (!string.IsNullOrEmpty(input))
                {
                    requestId.Should().NotBe(IntPtr.Zero, $"should create valid handle for input: {input}");
                    
                    var retrievedPtr = McpTypesApi.mcp_request_id_get_string(requestId);
                    var retrievedValue = Marshal.PtrToStringUTF8(retrievedPtr);
                    retrievedValue.Should().Be(input, "retrieved value should match input");
                }
            }
            finally
            {
                // Cleanup
                if (requestId != IntPtr.Zero)
                {
                    McpTypesApi.mcp_request_id_free(requestId);
                }
            }
        }

        [Theory]
        [InlineData(0)]
        [InlineData(1)]
        [InlineData(-1)]
        [InlineData(int.MaxValue)]
        [InlineData(int.MinValue)]
        [InlineData(long.MaxValue)]
        [InlineData(long.MinValue)]
        public void RequestId_NumberCreation_ShouldHandleVariousInputs(long input)
        {
            // Act
            var requestId = McpTypesApi.mcp_request_id_create_number(input);
            
            try
            {
                // Assert
                requestId.Should().NotBe(IntPtr.Zero, $"should create valid handle for input: {input}");
                
                var retrievedValue = McpTypesApi.mcp_request_id_get_number(requestId);
                retrievedValue.Should().Be(input, "retrieved value should match input");
            }
            finally
            {
                // Cleanup
                if (requestId != IntPtr.Zero)
                {
                    McpTypesApi.mcp_request_id_free(requestId);
                }
            }
        }

        [Fact]
        public void RequestId_Free_ShouldNotThrowOnNullHandle()
        {
            // Act & Assert
            var action = () => McpTypesApi.mcp_request_id_free(IntPtr.Zero);
            action.Should().NotThrow("freeing a null handle should be safe");
        }

        [Fact]
        public void RequestId_MultipleFree_ShouldBeSafe()
        {
            // Arrange
            var requestId = McpTypesApi.mcp_request_id_create_string("test");
            
            // Act
            McpTypesApi.mcp_request_id_free(requestId);
            
            // Second free on already freed handle - should not crash
            // Note: This is testing defensive programming, but in practice
            // double-free should be avoided
            var action = () => McpTypesApi.mcp_request_id_free(requestId);
            
            // Assert
            action.Should().NotThrow("the native library should handle double-free safely");
        }

        #endregion

        #region Helper Methods

        /// <summary>
        /// Helper method to safely execute test with automatic cleanup
        /// </summary>
        private void WithRequestId(string value, Action<IntPtr> action)
        {
            var requestId = McpTypesApi.mcp_request_id_create_string(value);
            try
            {
                action(requestId);
            }
            finally
            {
                if (requestId != IntPtr.Zero)
                {
                    McpTypesApi.mcp_request_id_free(requestId);
                }
            }
        }

        /// <summary>
        /// Helper method to safely execute test with automatic cleanup for number IDs
        /// </summary>
        private void WithRequestId(long value, Action<IntPtr> action)
        {
            var requestId = McpTypesApi.mcp_request_id_create_number(value);
            try
            {
                action(requestId);
            }
            finally
            {
                if (requestId != IntPtr.Zero)
                {
                    McpTypesApi.mcp_request_id_free(requestId);
                }
            }
        }

        #endregion
    }
}