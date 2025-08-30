using System;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP configuration struct types including socket options, SSL config, watermark config, etc.
    /// </summary>
    public class McpConfigurationTypesTests
    {
        #region mcp_socket_options_t Tests

        [Fact]
        public void MCP_SOCKET_OPTIONS_T_Should_Have_Expected_Size()
        {
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_socket_options_t>();

            // Assert
            // reuse_addr (1) + keep_alive (1) + tcp_nodelay (1) + padding (1) + 
            // send_buffer_size (4) + recv_buffer_size (4) + connect_timeout_ms (4) = 16 bytes
            size.Should().Be(16);
        }

        [Fact]
        public void MCP_SOCKET_OPTIONS_T_Should_Initialize_With_Default_Values()
        {
            // Arrange & Act
            var options = new mcp_socket_options_t();

            // Assert
            ((bool)options.reuse_addr).Should().BeFalse();
            ((bool)options.keep_alive).Should().BeFalse();
            ((bool)options.tcp_nodelay).Should().BeFalse();
            options.send_buffer_size.Should().Be(0);
            options.recv_buffer_size.Should().Be(0);
            options.connect_timeout_ms.Should().Be(0);
        }

        [Fact]
        public void MCP_SOCKET_OPTIONS_T_Should_Store_All_Options()
        {
            // Arrange & Act
            var options = new mcp_socket_options_t
            {
                reuse_addr = true,
                keep_alive = true,
                tcp_nodelay = true,
                send_buffer_size = 65536,
                recv_buffer_size = 131072,
                connect_timeout_ms = 5000
            };

            // Assert
            ((bool)options.reuse_addr).Should().BeTrue();
            ((bool)options.keep_alive).Should().BeTrue();
            ((bool)options.tcp_nodelay).Should().BeTrue();
            options.send_buffer_size.Should().Be(65536);
            options.recv_buffer_size.Should().Be(131072);
            options.connect_timeout_ms.Should().Be(5000);
        }

        #endregion

        #region mcp_ssl_config_t Tests

        [Fact]
        public void MCP_SSL_CONFIG_T_Should_Have_Expected_Size()
        {
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_ssl_config_t>();

            // Assert
            // ca_cert_path pointer (8/4) + client_cert_path pointer (8/4) + client_key_path pointer (8/4) + 
            // verify_peer (1) + padding (3 or 7) + cipher_list pointer (8/4) + 
            // alpn_protocols pointer (8/4) + alpn_count (8/4)
            var expectedSize = IntPtr.Size * 5 + 1 + (IntPtr.Size == 8 ? 7 : 3) + UIntPtr.Size;
            size.Should().Be(expectedSize);
        }

        [Fact]
        public void MCP_SSL_CONFIG_T_Should_Initialize_With_Default_Values()
        {
            // Arrange & Act
            var config = new mcp_ssl_config_t();

            // Assert
            config.ca_cert_path.Should().Be(IntPtr.Zero);
            config.client_cert_path.Should().Be(IntPtr.Zero);
            config.client_key_path.Should().Be(IntPtr.Zero);
            ((bool)config.verify_peer).Should().BeFalse();
            config.cipher_list.Should().Be(IntPtr.Zero);
            config.alpn_protocols.Should().Be(IntPtr.Zero);
            config.alpn_count.Should().Be(UIntPtr.Zero);
        }

        [Fact]
        public void MCP_SSL_CONFIG_T_Should_Store_Pointer_Values()
        {
            // Arrange
            var caCertPtr = new IntPtr(0x1000);
            var clientCertPtr = new IntPtr(0x2000);
            var clientKeyPtr = new IntPtr(0x3000);
            var cipherListPtr = new IntPtr(0x4000);
            var alpnProtocolsPtr = new IntPtr(0x5000);

            // Act
            var config = new mcp_ssl_config_t
            {
                ca_cert_path = caCertPtr,
                client_cert_path = clientCertPtr,
                client_key_path = clientKeyPtr,
                verify_peer = true,
                cipher_list = cipherListPtr,
                alpn_protocols = alpnProtocolsPtr,
                alpn_count = new UIntPtr(2)
            };

            // Assert
            config.ca_cert_path.Should().Be(caCertPtr);
            config.client_cert_path.Should().Be(clientCertPtr);
            config.client_key_path.Should().Be(clientKeyPtr);
            ((bool)config.verify_peer).Should().BeTrue();
            config.cipher_list.Should().Be(cipherListPtr);
            config.alpn_protocols.Should().Be(alpnProtocolsPtr);
            config.alpn_count.Should().Be(new UIntPtr(2));
        }

        #endregion

        #region mcp_watermark_config_t Tests

        [Fact]
        public void MCP_WATERMARK_CONFIG_T_Should_Have_Expected_Size()
        {
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_watermark_config_t>();

            // Assert
            // low_watermark (4) + high_watermark (4) = 8 bytes
            size.Should().Be(8);
        }

        [Fact]
        public void MCP_WATERMARK_CONFIG_T_Should_Initialize_With_Default_Values()
        {
            // Arrange & Act
            var config = new mcp_watermark_config_t();

            // Assert
            config.low_watermark.Should().Be(0);
            config.high_watermark.Should().Be(0);
        }

        [Fact]
        public void MCP_WATERMARK_CONFIG_T_Should_Store_Watermark_Values()
        {
            // Arrange & Act
            var config = new mcp_watermark_config_t
            {
                low_watermark = 1024,
                high_watermark = 8192
            };

            // Assert
            config.low_watermark.Should().Be(1024);
            config.high_watermark.Should().Be(8192);
        }

        [Fact]
        public void MCP_WATERMARK_CONFIG_T_Should_Handle_Large_Values()
        {
            // Arrange
            var maxValue = uint.MaxValue;
            
            // Act
            var config = new mcp_watermark_config_t
            {
                low_watermark = maxValue / 2,
                high_watermark = maxValue
            };

            // Assert
            config.low_watermark.Should().Be(maxValue / 2);
            config.high_watermark.Should().Be(maxValue);
        }

        #endregion

        #region mcp_client_config_t Tests

        [Fact]
        public void MCP_CLIENT_CONFIG_T_Should_Have_Expected_Size()
        {
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_client_config_t>();

            // Assert
            // client_info pointer (8/4) + capabilities pointer (8/4) + transport (4) + padding (4/0) +
            // server_address pointer (8/4) + ssl_config pointer (8/4) + 
            // watermarks (8) + reconnect_delay_ms (4) + max_reconnect_attempts (4)
            var expectedSize = IntPtr.Size * 4 + 4 + (IntPtr.Size == 8 ? 4 : 0) + 8 + 4 + 4;
            size.Should().Be(expectedSize);
        }

        [Fact]
        public void MCP_CLIENT_CONFIG_T_Should_Initialize_With_Default_Values()
        {
            // Arrange & Act
            var config = new mcp_client_config_t();

            // Assert
            config.client_info.Should().Be(IntPtr.Zero);
            config.capabilities.Should().Be(IntPtr.Zero);
            config.transport.Should().Be((mcp_transport_type_t)0);
            config.server_address.Should().Be(IntPtr.Zero);
            config.ssl_config.Should().Be(IntPtr.Zero);
            config.watermarks.low_watermark.Should().Be(0);
            config.watermarks.high_watermark.Should().Be(0);
            config.reconnect_delay_ms.Should().Be(0);
            config.max_reconnect_attempts.Should().Be(0);
        }

        [Fact]
        public void MCP_CLIENT_CONFIG_T_Should_Store_All_Settings()
        {
            // Arrange & Act
            var config = new mcp_client_config_t
            {
                client_info = new IntPtr(0x1000),
                capabilities = new IntPtr(0x2000),
                transport = mcp_transport_type_t.MCP_TRANSPORT_HTTP_SSE,
                server_address = new IntPtr(0x3000),
                ssl_config = new IntPtr(0x4000),
                watermarks = new mcp_watermark_config_t { low_watermark = 1024, high_watermark = 8192 },
                reconnect_delay_ms = 5000,
                max_reconnect_attempts = 3
            };

            // Assert
            config.client_info.Should().Be(new IntPtr(0x1000));
            config.capabilities.Should().Be(new IntPtr(0x2000));
            config.transport.Should().Be(mcp_transport_type_t.MCP_TRANSPORT_HTTP_SSE);
            config.server_address.Should().Be(new IntPtr(0x3000));
            config.ssl_config.Should().Be(new IntPtr(0x4000));
            config.watermarks.low_watermark.Should().Be(1024);
            config.watermarks.high_watermark.Should().Be(8192);
            config.reconnect_delay_ms.Should().Be(5000);
            config.max_reconnect_attempts.Should().Be(3);
        }

        #endregion

        #region mcp_server_config_t Tests

        [Fact]
        public void MCP_SERVER_CONFIG_T_Should_Have_Expected_Size()
        {
            // Arrange & Act
            var size = Marshal.SizeOf<mcp_server_config_t>();

            // Assert
            // server_info pointer (8/4) + capabilities pointer (8/4) + transport (4) + padding (4/0) +
            // bind_address pointer (8/4) + ssl_config pointer (8/4) + 
            // watermarks (8) + max_connections (4) + padding (4/0) + instructions pointer (8/4)
            var expectedSize = IntPtr.Size * 5 + 4 + (IntPtr.Size == 8 ? 4 : 0) + 8 + 4 + (IntPtr.Size == 8 ? 4 : 0);
            size.Should().Be(expectedSize);
        }

        [Fact]
        public void MCP_SERVER_CONFIG_T_Should_Initialize_With_Default_Values()
        {
            // Arrange & Act
            var config = new mcp_server_config_t();

            // Assert
            config.server_info.Should().Be(IntPtr.Zero);
            config.capabilities.Should().Be(IntPtr.Zero);
            config.transport.Should().Be((mcp_transport_type_t)0);
            config.bind_address.Should().Be(IntPtr.Zero);
            config.ssl_config.Should().Be(IntPtr.Zero);
            config.watermarks.low_watermark.Should().Be(0);
            config.watermarks.high_watermark.Should().Be(0);
            config.max_connections.Should().Be(0);
            config.instructions.Should().Be(IntPtr.Zero);
        }

        [Fact]
        public void MCP_SERVER_CONFIG_T_Should_Store_All_Settings()
        {
            // Arrange & Act
            var config = new mcp_server_config_t
            {
                server_info = new IntPtr(0x1000),
                capabilities = new IntPtr(0x2000),
                transport = mcp_transport_type_t.MCP_TRANSPORT_STDIO,
                bind_address = new IntPtr(0x3000),
                ssl_config = new IntPtr(0x4000),
                watermarks = new mcp_watermark_config_t { low_watermark = 2048, high_watermark = 16384 },
                max_connections = 100,
                instructions = new IntPtr(0x5000)
            };

            // Assert
            config.server_info.Should().Be(new IntPtr(0x1000));
            config.capabilities.Should().Be(new IntPtr(0x2000));
            config.transport.Should().Be(mcp_transport_type_t.MCP_TRANSPORT_STDIO);
            config.bind_address.Should().Be(new IntPtr(0x3000));
            config.ssl_config.Should().Be(new IntPtr(0x4000));
            config.watermarks.low_watermark.Should().Be(2048);
            config.watermarks.high_watermark.Should().Be(16384);
            config.max_connections.Should().Be(100);
            config.instructions.Should().Be(new IntPtr(0x5000));
        }

        #endregion
    }
}
