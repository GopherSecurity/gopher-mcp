using System;
using System.Runtime.InteropServices;
using System.Text;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP struct types including mcp_error_info_t, mcp_allocator_t, and mcp_optional_t
    /// </summary>
    public class McpStructTypesTests
    {
        #region mcp_error_info_t Tests

        [Fact]
        public void MCP_ERROR_INFO_T_Should_Have_Expected_Size()
        {
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_error_info_t>();

            // Assert
            // code (4) + message (256) + file (256) + line (4) = 520 bytes
            size.Should().Be(520);
        }

        [Fact]
        public void MCP_ERROR_INFO_T_Should_Initialize_With_Default_Values()
        {
            // Arrange & Act
            var errorInfo = new mcp_error_info_t();

            // Assert
            errorInfo.code.Should().Be((mcp_result_t)0);
            errorInfo.line.Should().Be(0);
            errorInfo.message.Should().BeNull();
            errorInfo.file.Should().BeNull();
        }

        [Fact]
        public void MCP_ERROR_INFO_T_GetMessage_Should_Return_Empty_For_Null()
        {
            // Arrange
            var errorInfo = new mcp_error_info_t();

            // Act
            var message = errorInfo.GetMessage();

            // Assert
            message.Should().BeEmpty();
        }

        [Fact]
        public void MCP_ERROR_INFO_T_GetMessage_Should_Extract_String()
        {
            // Arrange
            var errorInfo = new mcp_error_info_t
            {
                message = new byte[256]
            };
            var testMessage = "Test error message";
            var bytes = Encoding.UTF8.GetBytes(testMessage);
            Array.Copy(bytes, errorInfo.message, bytes.Length);

            // Act
            var message = errorInfo.GetMessage();

            // Assert
            message.Should().Be(testMessage);
        }

        [Fact]
        public void MCP_ERROR_INFO_T_GetFile_Should_Extract_String()
        {
            // Arrange
            var errorInfo = new mcp_error_info_t
            {
                file = new byte[256]
            };
            var testFile = "/path/to/file.cpp";
            var bytes = Encoding.UTF8.GetBytes(testFile);
            Array.Copy(bytes, errorInfo.file, bytes.Length);

            // Act
            var file = errorInfo.GetFile();

            // Assert
            file.Should().Be(testFile);
        }

        [Fact]
        public void MCP_ERROR_INFO_T_Should_Handle_Full_Length_Strings()
        {
            // Arrange
            var errorInfo = new mcp_error_info_t
            {
                message = new byte[256],
                file = new byte[256]
            };
            
            // Fill entire arrays (no null terminator)
            for (int i = 0; i < 256; i++)
            {
                errorInfo.message[i] = (byte)'A';
                errorInfo.file[i] = (byte)'B';
            }

            // Act
            var message = errorInfo.GetMessage();
            var file = errorInfo.GetFile();

            // Assert
            message.Should().HaveLength(256);
            message.Should().Be(new string('A', 256));
            file.Should().HaveLength(256);
            file.Should().Be(new string('B', 256));
        }

        #endregion

        #region mcp_allocator_t Tests

        [Fact]
        public void MCP_ALLOCATOR_T_Should_Have_Expected_Size()
        {
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_allocator_t>();

            // Assert
            // 4 pointers: alloc, realloc, free, user_data
            // On 64-bit: 8 * 4 = 32 bytes
            // On 32-bit: 4 * 4 = 16 bytes
            if (IntPtr.Size == 8)
            {
                size.Should().Be(32);
            }
            else
            {
                size.Should().Be(16);
            }
        }

        [Fact]
        public void MCP_ALLOCATOR_T_Should_Initialize_With_Null_Pointers()
        {
            // Arrange & Act
            var allocator = new mcp_allocator_t();

            // Assert
            allocator.alloc.Should().Be(IntPtr.Zero);
            allocator.realloc.Should().Be(IntPtr.Zero);
            allocator.free.Should().Be(IntPtr.Zero);
            allocator.user_data.Should().Be(IntPtr.Zero);
        }

        [Fact]
        public void MCP_ALLOCATOR_T_Should_Store_Function_Pointers()
        {
            // Arrange
            var allocPtr = new IntPtr(0x1000);
            var reallocPtr = new IntPtr(0x2000);
            var freePtr = new IntPtr(0x3000);
            var userData = new IntPtr(0x4000);

            // Act
            var allocator = new mcp_allocator_t
            {
                alloc = allocPtr,
                realloc = reallocPtr,
                free = freePtr,
                user_data = userData
            };

            // Assert
            allocator.alloc.Should().Be(allocPtr);
            allocator.realloc.Should().Be(reallocPtr);
            allocator.free.Should().Be(freePtr);
            allocator.user_data.Should().Be(userData);
        }

        #endregion

        #region mcp_optional_t Tests

        [Fact]
        public void MCP_OPTIONAL_T_Should_Have_Expected_Size()
        {
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_optional_t>();

            // Assert
            // has_value (1 byte) + padding + value (pointer)
            // On 64-bit: 1 + 7 (padding) + 8 = 16 bytes
            // On 32-bit: 1 + 3 (padding) + 4 = 8 bytes
            if (IntPtr.Size == 8)
            {
                size.Should().Be(16);
            }
            else
            {
                size.Should().Be(8);
            }
        }

        [Fact]
        public void MCP_OPTIONAL_T_Should_Initialize_As_Empty()
        {
            // Arrange & Act
            var optional = new mcp_optional_t();

            // Assert
            ((bool)optional.has_value).Should().BeFalse();
            optional.value.Should().Be(IntPtr.Zero);
        }

        [Fact]
        public void MCP_OPTIONAL_T_Should_Represent_Present_Value()
        {
            // Arrange
            var valuePtr = Marshal.AllocHGlobal(sizeof(int));
            Marshal.WriteInt32(valuePtr, 42);

            try
            {
                // Act
                var optional = new mcp_optional_t
                {
                    has_value = true,
                    value = valuePtr
                };

                // Assert
                ((bool)optional.has_value).Should().BeTrue();
                optional.value.Should().NotBe(IntPtr.Zero);
                Marshal.ReadInt32(optional.value).Should().Be(42);
            }
            finally
            {
                Marshal.FreeHGlobal(valuePtr);
            }
        }

        [Fact]
        public void MCP_OPTIONAL_T_Should_Distinguish_Present_And_Absent()
        {
            // Arrange
            var present = new mcp_optional_t
            {
                has_value = true,
                value = new IntPtr(0x1234)
            };

            var absent = new mcp_optional_t
            {
                has_value = false,
                value = IntPtr.Zero
            };

            // Act & Assert
            ((bool)present.has_value).Should().BeTrue();
            ((bool)absent.has_value).Should().BeFalse();
        }

        #endregion
    }
}