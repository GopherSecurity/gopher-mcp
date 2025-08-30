using System;
using System.Runtime.InteropServices;
using System.Text;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;

namespace GopherMcp.Tests.Interop
{
    public class McpTypesTests : IDisposable
    {
        private readonly List<IntPtr> _allocatedMemory = new();

        public void Dispose()
        {
            // Clean up any allocated memory
            foreach (var ptr in _allocatedMemory)
            {
                if (ptr != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(ptr);
                }
            }
            _allocatedMemory.Clear();
        }

        [Fact]
        public void MCP_STRING_REF_Should_Initialize_Empty()
        {
            // Arrange & Act
            var stringRef = new mcp_string_ref();

            // Assert
            stringRef.data.Should().Be(IntPtr.Zero);
            stringRef.length.Should().Be(UIntPtr.Zero);
        }

        [Fact]
        public void MCP_STRING_REF_ToManagedString_Should_Return_Null_For_Empty()
        {
            // Arrange
            var stringRef = new mcp_string_ref
            {
                data = IntPtr.Zero,
                length = UIntPtr.Zero
            };

            // Act
            var result = stringRef.ToManagedString();

            // Assert
            result.Should().BeNull();
        }

        [Fact]
        public void MCP_STRING_REF_ToManagedString_Should_Convert_ASCII_String()
        {
            // Arrange
            const string testString = "Hello, World!";
            var bytes = Encoding.UTF8.GetBytes(testString);
            var ptr = Marshal.AllocHGlobal(bytes.Length);
            _allocatedMemory.Add(ptr);
            Marshal.Copy(bytes, 0, ptr, bytes.Length);

            var stringRef = new mcp_string_ref
            {
                data = ptr,
                length = new UIntPtr((uint)bytes.Length)
            };

            // Act
            var result = stringRef.ToManagedString();

            // Assert
            result.Should().Be(testString);
        }

        [Fact]
        public void MCP_STRING_REF_ToManagedString_Should_Handle_UTF8_String()
        {
            // Arrange
            const string testString = "Hello, 世界! 🌍";
            var bytes = Encoding.UTF8.GetBytes(testString);
            var ptr = Marshal.AllocHGlobal(bytes.Length);
            _allocatedMemory.Add(ptr);
            Marshal.Copy(bytes, 0, ptr, bytes.Length);

            var stringRef = new mcp_string_ref
            {
                data = ptr,
                length = new UIntPtr((uint)bytes.Length)
            };

            // Act
            var result = stringRef.ToManagedString();

            // Assert
            result.Should().Be(testString);
        }

        [Fact]
        public void MCP_STRING_REF_FromString_Should_Handle_Null()
        {
            // Arrange & Act
            var stringRef = mcp_string_ref.FromString(null);

            // Assert
            stringRef.data.Should().Be(IntPtr.Zero);
            stringRef.length.Should().Be(UIntPtr.Zero);
        }

        [Fact]
        public void MCP_STRING_REF_FromString_Should_Convert_ASCII_String()
        {
            // Arrange
            const string testString = "Test String";

            // Act
            var stringRef = mcp_string_ref.FromString(testString);
            _allocatedMemory.Add(stringRef.data); // Track for cleanup

            // Assert
            stringRef.data.Should().NotBe(IntPtr.Zero);
            stringRef.length.Should().Be(new UIntPtr((uint)Encoding.UTF8.GetByteCount(testString)));

            // Verify the data
            var result = stringRef.ToManagedString();
            result.Should().Be(testString);
        }

        [Fact]
        public void MCP_STRING_REF_FromString_Should_Handle_Empty_String()
        {
            // Arrange & Act
            var stringRef = mcp_string_ref.FromString(string.Empty);
            if (stringRef.data != IntPtr.Zero)
            {
                _allocatedMemory.Add(stringRef.data);
            }

            // Assert
            stringRef.length.Should().Be(UIntPtr.Zero);
        }

        [Fact]
        public void MCP_STRING_REF_Should_Handle_Large_String()
        {
            // Arrange
            var largeString = new string('A', 10000);

            // Act
            var stringRef = mcp_string_ref.FromString(largeString);
            _allocatedMemory.Add(stringRef.data);

            // Assert
            stringRef.data.Should().NotBe(IntPtr.Zero);
            stringRef.length.Should().Be(new UIntPtr(10000));

            var result = stringRef.ToManagedString();
            result.Should().Be(largeString);
            result.Length.Should().Be(10000);
        }

        [Fact]
        public void MCP_STRING_REF_Should_Handle_String_With_Special_Characters()
        {
            // Arrange
            const string testString = "Line1\nLine2\tTab\r\nCRLF\0Null";
            var bytes = Encoding.UTF8.GetBytes(testString);
            var ptr = Marshal.AllocHGlobal(bytes.Length);
            _allocatedMemory.Add(ptr);
            Marshal.Copy(bytes, 0, ptr, bytes.Length);

            var stringRef = new mcp_string_ref
            {
                data = ptr,
                length = new UIntPtr((uint)bytes.Length)
            };

            // Act
            var result = stringRef.ToManagedString();

            // Assert
            result.Should().Be(testString);
        }

        [Fact]
        public void MCP_STRING_T_Should_Be_Compatible_With_MCP_STRING_REF()
        {
            // Arrange
            const string testString = "Compatibility Test";
            var stringRef = mcp_string_ref.FromString(testString);
            _allocatedMemory.Add(stringRef.data);

            var stringT = new mcp_string_t
            {
                data = stringRef.data,
                length = stringRef.length
            };

            // Act
            mcp_string_ref converted = stringT; // Implicit conversion

            // Assert
            converted.data.Should().Be(stringRef.data);
            converted.length.Should().Be(stringRef.length);
            converted.ToManagedString().Should().Be(testString);
        }

        [Theory]
        [InlineData("")]
        [InlineData("a")]
        [InlineData("abc")]
        [InlineData("The quick brown fox jumps over the lazy dog")]
        [InlineData("αβγδε")]
        [InlineData("你好世界")]
        [InlineData("🎈🎉🎊")]
        public void MCP_STRING_REF_RoundTrip_Should_Preserve_String(string original)
        {
            // Arrange & Act
            var stringRef = mcp_string_ref.FromString(original);
            if (stringRef.data != IntPtr.Zero)
            {
                _allocatedMemory.Add(stringRef.data);
            }
            var result = stringRef.ToManagedString();

            // Assert
            result.Should().Be(original);
        }

        [Fact]
        public void MCP_STRING_REF_Should_Calculate_UTF8_Length_Correctly()
        {
            // Arrange
            const string asciiString = "ASCII";
            const string unicodeString = "Unicode: 世界";
            const string emojiString = "Emoji: 🌍";

            // Act
            var asciiRef = mcp_string_ref.FromString(asciiString);
            var unicodeRef = mcp_string_ref.FromString(unicodeString);
            var emojiRef = mcp_string_ref.FromString(emojiString);

            _allocatedMemory.Add(asciiRef.data);
            _allocatedMemory.Add(unicodeRef.data);
            _allocatedMemory.Add(emojiRef.data);

            // Assert
            // ASCII: 5 bytes
            asciiRef.length.Should().Be(new UIntPtr(5));
            
            // "Unicode: " (9 bytes) + "世界" (6 bytes for 2 Chinese characters) = 15 bytes
            unicodeRef.length.Should().Be(new UIntPtr((uint)Encoding.UTF8.GetByteCount(unicodeString)));
            
            // "Emoji: " (7 bytes) + "🌍" (4 bytes for emoji) = 11 bytes
            emojiRef.length.Should().Be(new UIntPtr((uint)Encoding.UTF8.GetByteCount(emojiString)));
        }

        [Fact]
        public void MCP_STRING_REF_Should_Handle_Partial_String()
        {
            // Arrange
            const string fullString = "Hello, World!";
            var bytes = Encoding.UTF8.GetBytes(fullString);
            var ptr = Marshal.AllocHGlobal(bytes.Length);
            _allocatedMemory.Add(ptr);
            Marshal.Copy(bytes, 0, ptr, bytes.Length);

            // Create a string ref that only covers "Hello"
            var partialRef = new mcp_string_ref
            {
                data = ptr,
                length = new UIntPtr(5) // Just "Hello"
            };

            // Act
            var result = partialRef.ToManagedString();

            // Assert
            result.Should().Be("Hello");
        }

        [Fact]
        public void MCP_STRING_REF_Size_Should_Match_Native_Size()
        {
            // This test verifies the struct layout matches expected C ABI
            
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_string_ref>();

            // Assert
            // On 64-bit systems: pointer (8 bytes) + size_t (8 bytes) = 16 bytes
            // On 32-bit systems: pointer (4 bytes) + size_t (4 bytes) = 8 bytes
            if (IntPtr.Size == 8)
            {
                size.Should().Be(16);
            }
            else
            {
                size.Should().Be(8);
            }
        }
    }
}