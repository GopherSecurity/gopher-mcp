using System;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for primitive MCP types including mcp_bool_t and mcp_result_t
    /// </summary>
    public class McpPrimitiveTypesTests
    {
        #region mcp_bool_t Tests

        [Fact]
        public void MCP_BOOL_T_Should_Have_Size_Of_One_Byte()
        {
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_bool_t>();

            // Assert
            size.Should().Be(1, "mcp_bool_t must be exactly 1 byte for FFI compatibility");
        }

        [Fact]
        public void MCP_BOOL_T_True_Should_Equal_One()
        {
            // Arrange
            var trueValue = mcp_bool_t.True;

            // Act & Assert
            Marshal.SizeOf(trueValue).Should().Be(1);
        }

        [Fact]
        public void MCP_BOOL_T_False_Should_Equal_Zero()
        {
            // Arrange
            var falseValue = mcp_bool_t.False;

            // Act & Assert
            Marshal.SizeOf(falseValue).Should().Be(1);
        }

        [Fact]
        public void MCP_BOOL_T_Should_Convert_To_Bool_Implicitly()
        {
            // Arrange
            var mcpTrue = mcp_bool_t.True;
            var mcpFalse = mcp_bool_t.False;

            // Act
            bool boolTrue = mcpTrue;
            bool boolFalse = mcpFalse;

            // Assert
            boolTrue.Should().BeTrue();
            boolFalse.Should().BeFalse();
        }

        [Fact]
        public void MCP_BOOL_T_Should_Convert_From_Bool_Implicitly()
        {
            // Arrange
            bool dotnetTrue = true;
            bool dotnetFalse = false;

            // Act
            mcp_bool_t mcpTrue = dotnetTrue;
            mcp_bool_t mcpFalse = dotnetFalse;

            // Assert
            ((bool)mcpTrue).Should().BeTrue();
            ((bool)mcpFalse).Should().BeFalse();
        }

        [Theory]
        [InlineData(true)]
        [InlineData(false)]
        public void MCP_BOOL_T_Should_RoundTrip_Bool_Values(bool original)
        {
            // Arrange & Act
            mcp_bool_t mcpBool = original;
            bool result = mcpBool;

            // Assert
            result.Should().Be(original);
        }

        #endregion

        #region mcp_result_t Tests

        [Fact]
        public void MCP_RESULT_T_Should_Be_Int32()
        {
            // Arrange & Act
            var size = sizeof(mcp_result_t);

            // Assert
            size.Should().Be(4, "mcp_result_t should be 4 bytes (int32)");
        }

        [Fact]
        public void MCP_RESULT_T_OK_Should_Be_Zero()
        {
            // Arrange & Act
            var ok = mcp_result_t.MCP_OK;

            // Assert
            ((int)ok).Should().Be(0);
        }

        [Fact]
        public void MCP_RESULT_T_Error_Values_Should_Be_Negative()
        {
            // Arrange
            var errors = new[]
            {
                mcp_result_t.MCP_ERROR_INVALID_ARGUMENT,
                mcp_result_t.MCP_ERROR_NULL_POINTER,
                mcp_result_t.MCP_ERROR_OUT_OF_MEMORY,
                mcp_result_t.MCP_ERROR_NOT_FOUND,
                mcp_result_t.MCP_ERROR_ALREADY_EXISTS,
                mcp_result_t.MCP_ERROR_PERMISSION_DENIED,
                mcp_result_t.MCP_ERROR_IO_ERROR,
                mcp_result_t.MCP_ERROR_TIMEOUT,
                mcp_result_t.MCP_ERROR_CANCELLED,
                mcp_result_t.MCP_ERROR_NOT_IMPLEMENTED,
                mcp_result_t.MCP_ERROR_INVALID_STATE,
                mcp_result_t.MCP_ERROR_BUFFER_TOO_SMALL,
                mcp_result_t.MCP_ERROR_PROTOCOL_ERROR,
                mcp_result_t.MCP_ERROR_CONNECTION_FAILED,
                mcp_result_t.MCP_ERROR_CONNECTION_CLOSED,
                mcp_result_t.MCP_ERROR_ALREADY_INITIALIZED,
                mcp_result_t.MCP_ERROR_NOT_INITIALIZED,
                mcp_result_t.MCP_ERROR_RESOURCE_EXHAUSTED,
                mcp_result_t.MCP_ERROR_INVALID_FORMAT,
                mcp_result_t.MCP_ERROR_CLEANUP_FAILED,
                mcp_result_t.MCP_ERROR_RESOURCE_LIMIT,
                mcp_result_t.MCP_ERROR_NO_MEMORY,
                mcp_result_t.MCP_ERROR_UNKNOWN
            };

            // Act & Assert
            foreach (var error in errors)
            {
                ((int)error).Should().BeLessThan(0, $"{error} should be negative");
            }
        }

        [Fact]
        public void MCP_RESULT_T_Should_Have_Unique_Error_Codes()
        {
            // Arrange
            var errorCodes = new[]
            {
                (int)mcp_result_t.MCP_OK,
                (int)mcp_result_t.MCP_ERROR_INVALID_ARGUMENT,
                (int)mcp_result_t.MCP_ERROR_NULL_POINTER,
                (int)mcp_result_t.MCP_ERROR_OUT_OF_MEMORY,
                (int)mcp_result_t.MCP_ERROR_NOT_FOUND,
                (int)mcp_result_t.MCP_ERROR_ALREADY_EXISTS,
                (int)mcp_result_t.MCP_ERROR_PERMISSION_DENIED,
                (int)mcp_result_t.MCP_ERROR_IO_ERROR,
                (int)mcp_result_t.MCP_ERROR_TIMEOUT,
                (int)mcp_result_t.MCP_ERROR_CANCELLED,
                (int)mcp_result_t.MCP_ERROR_NOT_IMPLEMENTED,
                (int)mcp_result_t.MCP_ERROR_INVALID_STATE,
                (int)mcp_result_t.MCP_ERROR_BUFFER_TOO_SMALL,
                (int)mcp_result_t.MCP_ERROR_PROTOCOL_ERROR,
                (int)mcp_result_t.MCP_ERROR_CONNECTION_FAILED,
                (int)mcp_result_t.MCP_ERROR_CONNECTION_CLOSED,
                (int)mcp_result_t.MCP_ERROR_ALREADY_INITIALIZED,
                (int)mcp_result_t.MCP_ERROR_NOT_INITIALIZED,
                (int)mcp_result_t.MCP_ERROR_RESOURCE_EXHAUSTED,
                (int)mcp_result_t.MCP_ERROR_INVALID_FORMAT,
                (int)mcp_result_t.MCP_ERROR_CLEANUP_FAILED,
                (int)mcp_result_t.MCP_ERROR_RESOURCE_LIMIT,
                (int)mcp_result_t.MCP_ERROR_NO_MEMORY,
                (int)mcp_result_t.MCP_ERROR_UNKNOWN
            };

            // Act & Assert
            errorCodes.Should().OnlyHaveUniqueItems("Each error code should be unique");
        }

        [Theory]
        [InlineData(mcp_result_t.MCP_OK, true)]
        [InlineData(mcp_result_t.MCP_ERROR_INVALID_ARGUMENT, false)]
        [InlineData(mcp_result_t.MCP_ERROR_NULL_POINTER, false)]
        [InlineData(mcp_result_t.MCP_ERROR_UNKNOWN, false)]
        public void MCP_RESULT_T_Should_Indicate_Success_Or_Failure(mcp_result_t result, bool isSuccess)
        {
            // Act
            var success = result == mcp_result_t.MCP_OK;

            // Assert
            success.Should().Be(isSuccess);
        }

        [Fact]
        public void MCP_RESULT_T_Should_Cast_To_Int()
        {
            // Arrange
            var result = mcp_result_t.MCP_ERROR_TIMEOUT;

            // Act
            int intValue = (int)result;

            // Assert
            intValue.Should().Be(-8);
        }

        [Fact]
        public void MCP_RESULT_T_Should_Cast_From_Int()
        {
            // Arrange
            int errorCode = -5;

            // Act
            var result = (mcp_result_t)errorCode;

            // Assert
            result.Should().Be(mcp_result_t.MCP_ERROR_ALREADY_EXISTS);
        }

        #endregion
    }
}