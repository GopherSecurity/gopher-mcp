using System;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpHandles;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP handle types (SafeHandle implementations)
    /// </summary>
    public class McpHandleTypesTests
    {
        #region McpSafeHandle Base Class Tests

        [Fact]
        public void McpSafeHandle_Should_Be_Abstract()
        {
            // Assert
            typeof(McpSafeHandle).IsAbstract.Should().BeTrue();
        }

        [Fact]
        public void McpSafeHandle_Should_Inherit_From_SafeHandleZeroOrMinusOneIsInvalid()
        {
            // Assert
            typeof(Microsoft.Win32.SafeHandles.SafeHandleZeroOrMinusOneIsInvalid)
                .IsAssignableFrom(typeof(McpSafeHandle))
                .Should().BeTrue();
        }

        #endregion

        #region McpConnectionHandle Tests

        [Fact]
        public void McpConnectionHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpConnectionHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpConnectionHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpConnectionHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        [Fact]
        public void McpConnectionHandle_Should_Be_Disposable()
        {
            // Arrange
            var handle = new McpConnectionHandle();

            // Act
            handle.Dispose();

            // Assert
            handle.IsClosed.Should().BeTrue();
        }

        #endregion

        #region McpListenerHandle Tests

        [Fact]
        public void McpListenerHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpListenerHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpListenerHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpListenerHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpClientHandle Tests

        [Fact]
        public void McpClientHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpClientHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpClientHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpClientHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpServerHandle Tests

        [Fact]
        public void McpServerHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpServerHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpServerHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpServerHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpRequestHandle Tests

        [Fact]
        public void McpRequestHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpRequestHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpRequestHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpRequestHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpResponseHandle Tests

        [Fact]
        public void McpResponseHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpResponseHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpResponseHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpResponseHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpNotificationHandle Tests

        [Fact]
        public void McpNotificationHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpNotificationHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpNotificationHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpNotificationHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpToolHandle Tests

        [Fact]
        public void McpToolHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpToolHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpToolHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpToolHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpResourceHandle Tests

        [Fact]
        public void McpResourceHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpResourceHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpResourceHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpResourceHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpPromptHandle Tests

        [Fact]
        public void McpPromptHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpPromptHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpPromptHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpPromptHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpMessageHandle Tests

        [Fact]
        public void McpMessageHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpMessageHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpMessageHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpMessageHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpContentBlockHandle Tests

        [Fact]
        public void McpContentBlockHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpContentBlockHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpContentBlockHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpContentBlockHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpErrorHandle Tests

        [Fact]
        public void McpErrorHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpErrorHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpErrorHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpErrorHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpJsonHandle Tests

        [Fact]
        public void McpJsonHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpJsonHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpJsonHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpJsonHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpTimerHandle Tests

        [Fact]
        public void McpTimerHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpTimerHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpTimerHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpTimerHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpBufferHandle Tests

        [Fact]
        public void McpBufferHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpBufferHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpBufferHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpBufferHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpCallHandle Tests

        [Fact]
        public void McpCallHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpCallHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpCallHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpCallHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region McpArgumentHandle Tests

        [Fact]
        public void McpArgumentHandle_Should_Inherit_From_McpSafeHandle()
        {
            // Assert
            typeof(McpSafeHandle).IsAssignableFrom(typeof(McpArgumentHandle))
                .Should().BeTrue();
        }

        [Fact]
        public void McpArgumentHandle_Should_Initialize_With_Invalid_Handle()
        {
            // Arrange & Act
            using var handle = new McpArgumentHandle();

            // Assert
            handle.IsInvalid.Should().BeTrue();
            handle.IsClosed.Should().BeFalse();
        }

        #endregion

        #region All Handles Type Safety Test

        [Fact]
        public void All_Handle_Types_Should_Be_Sealed()
        {
            // Arrange
            var handleTypes = new[]
            {
                typeof(McpConnectionHandle),
                typeof(McpListenerHandle),
                typeof(McpClientHandle),
                typeof(McpServerHandle),
                typeof(McpRequestHandle),
                typeof(McpResponseHandle),
                typeof(McpNotificationHandle),
                typeof(McpToolHandle),
                typeof(McpResourceHandle),
                typeof(McpPromptHandle),
                typeof(McpMessageHandle),
                typeof(McpContentBlockHandle),
                typeof(McpErrorHandle),
                typeof(McpJsonHandle),
                typeof(McpTimerHandle),
                typeof(McpBufferHandle),
                typeof(McpCallHandle),
                typeof(McpArgumentHandle)
            };

            // Act & Assert
            foreach (var type in handleTypes)
            {
                type.IsSealed.Should().BeTrue($"{type.Name} should be sealed for type safety");
            }
        }

        [Fact]
        public void All_Handle_Types_Should_Implement_IDisposable()
        {
            // Arrange
            var handleTypes = new[]
            {
                typeof(McpConnectionHandle),
                typeof(McpListenerHandle),
                typeof(McpClientHandle),
                typeof(McpServerHandle),
                typeof(McpRequestHandle),
                typeof(McpResponseHandle),
                typeof(McpNotificationHandle),
                typeof(McpToolHandle),
                typeof(McpResourceHandle),
                typeof(McpPromptHandle),
                typeof(McpMessageHandle),
                typeof(McpContentBlockHandle),
                typeof(McpErrorHandle),
                typeof(McpJsonHandle),
                typeof(McpTimerHandle),
                typeof(McpBufferHandle),
                typeof(McpCallHandle),
                typeof(McpArgumentHandle)
            };

            // Act & Assert
            foreach (var type in handleTypes)
            {
                typeof(IDisposable).IsAssignableFrom(type)
                    .Should().BeTrue($"{type.Name} should implement IDisposable");
            }
        }

        #endregion
    }
}
