using System;
using System.Linq;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP enum types including roles, logging levels, transport types, etc.
    /// </summary>
    public class McpEnumTypesTests
    {
        #region mcp_role_t Tests

        [Fact]
        public void MCP_ROLE_T_Should_Have_Expected_Values()
        {
            // Assert
            ((int)mcp_role_t.MCP_ROLE_USER).Should().Be(0);
            ((int)mcp_role_t.MCP_ROLE_ASSISTANT).Should().Be(1);
        }

        [Fact]
        public void MCP_ROLE_T_Should_Be_Int32()
        {
            // Arrange & Act
            var size = sizeof(mcp_role_t);

            // Assert
            size.Should().Be(4);
        }

        #endregion

        #region mcp_logging_level_t Tests

        [Fact]
        public void MCP_LOGGING_LEVEL_T_Should_Have_Increasing_Severity()
        {
            // Arrange
            var levels = new[]
            {
                mcp_logging_level_t.MCP_LOG_DEBUG,
                mcp_logging_level_t.MCP_LOG_INFO,
                mcp_logging_level_t.MCP_LOG_NOTICE,
                mcp_logging_level_t.MCP_LOG_WARNING,
                mcp_logging_level_t.MCP_LOG_ERROR,
                mcp_logging_level_t.MCP_LOG_CRITICAL,
                mcp_logging_level_t.MCP_LOG_ALERT,
                mcp_logging_level_t.MCP_LOG_EMERGENCY
            };

            // Act & Assert
            for (int i = 0; i < levels.Length; i++)
            {
                ((int)levels[i]).Should().Be(i);
            }
        }

        [Theory]
        [InlineData(mcp_logging_level_t.MCP_LOG_DEBUG, 0)]
        [InlineData(mcp_logging_level_t.MCP_LOG_INFO, 1)]
        [InlineData(mcp_logging_level_t.MCP_LOG_NOTICE, 2)]
        [InlineData(mcp_logging_level_t.MCP_LOG_WARNING, 3)]
        [InlineData(mcp_logging_level_t.MCP_LOG_ERROR, 4)]
        [InlineData(mcp_logging_level_t.MCP_LOG_CRITICAL, 5)]
        [InlineData(mcp_logging_level_t.MCP_LOG_ALERT, 6)]
        [InlineData(mcp_logging_level_t.MCP_LOG_EMERGENCY, 7)]
        public void MCP_LOGGING_LEVEL_T_Should_Have_Correct_Value(mcp_logging_level_t level, int expectedValue)
        {
            // Assert
            ((int)level).Should().Be(expectedValue);
        }

        #endregion

        #region mcp_transport_type_t Tests

        [Fact]
        public void MCP_TRANSPORT_TYPE_T_Should_Have_Expected_Values()
        {
            // Assert
            ((int)mcp_transport_type_t.MCP_TRANSPORT_HTTP_SSE).Should().Be(0);
            ((int)mcp_transport_type_t.MCP_TRANSPORT_STDIO).Should().Be(1);
            ((int)mcp_transport_type_t.MCP_TRANSPORT_PIPE).Should().Be(2);
        }

        #endregion

        #region mcp_connection_state_t Tests

        [Fact]
        public void MCP_CONNECTION_STATE_T_Should_Have_Expected_Values()
        {
            // Assert
            ((int)mcp_connection_state_t.MCP_CONNECTION_STATE_IDLE).Should().Be(0);
            ((int)mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTING).Should().Be(1);
            ((int)mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTED).Should().Be(2);
            ((int)mcp_connection_state_t.MCP_CONNECTION_STATE_CLOSING).Should().Be(3);
            ((int)mcp_connection_state_t.MCP_CONNECTION_STATE_DISCONNECTED).Should().Be(4);
            ((int)mcp_connection_state_t.MCP_CONNECTION_STATE_ERROR).Should().Be(5);
        }

        [Theory]
        [InlineData(mcp_connection_state_t.MCP_CONNECTION_STATE_IDLE, false)]
        [InlineData(mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTING, false)]
        [InlineData(mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTED, true)]
        [InlineData(mcp_connection_state_t.MCP_CONNECTION_STATE_CLOSING, false)]
        [InlineData(mcp_connection_state_t.MCP_CONNECTION_STATE_DISCONNECTED, false)]
        [InlineData(mcp_connection_state_t.MCP_CONNECTION_STATE_ERROR, false)]
        public void MCP_CONNECTION_STATE_T_Should_Indicate_Connected_Status(mcp_connection_state_t state, bool isConnected)
        {
            // Act
            var connected = state == mcp_connection_state_t.MCP_CONNECTION_STATE_CONNECTED;

            // Assert
            connected.Should().Be(isConnected);
        }

        #endregion

        #region mcp_type_id_t Tests

        [Fact]
        public void MCP_TYPE_ID_T_Should_Have_Expected_Values()
        {
            // Arrange
            var types = new[]
            {
                (mcp_type_id_t.MCP_TYPE_UNKNOWN, 0),
                (mcp_type_id_t.MCP_TYPE_STRING, 1),
                (mcp_type_id_t.MCP_TYPE_NUMBER, 2),
                (mcp_type_id_t.MCP_TYPE_BOOL, 3),
                (mcp_type_id_t.MCP_TYPE_JSON, 4),
                (mcp_type_id_t.MCP_TYPE_RESOURCE, 5),
                (mcp_type_id_t.MCP_TYPE_TOOL, 6),
                (mcp_type_id_t.MCP_TYPE_PROMPT, 7),
                (mcp_type_id_t.MCP_TYPE_MESSAGE, 8),
                (mcp_type_id_t.MCP_TYPE_CONTENT_BLOCK, 9),
                (mcp_type_id_t.MCP_TYPE_ERROR, 10),
                (mcp_type_id_t.MCP_TYPE_REQUEST, 11),
                (mcp_type_id_t.MCP_TYPE_RESPONSE, 12),
                (mcp_type_id_t.MCP_TYPE_NOTIFICATION, 13)
            };

            // Act & Assert
            foreach (var (type, expectedValue) in types)
            {
                ((int)type).Should().Be(expectedValue);
            }
        }

        #endregion

        #region mcp_request_id_type_t Tests

        [Fact]
        public void MCP_REQUEST_ID_TYPE_T_Should_Have_Expected_Values()
        {
            // Assert
            ((int)mcp_request_id_type_t.MCP_REQUEST_ID_TYPE_STRING).Should().Be(0);
            ((int)mcp_request_id_type_t.MCP_REQUEST_ID_TYPE_NUMBER).Should().Be(1);
        }

        #endregion

        #region mcp_progress_token_type_t Tests

        [Fact]
        public void MCP_PROGRESS_TOKEN_TYPE_T_Should_Have_Expected_Values()
        {
            // Assert
            ((int)mcp_progress_token_type_t.MCP_PROGRESS_TOKEN_TYPE_STRING).Should().Be(0);
            ((int)mcp_progress_token_type_t.MCP_PROGRESS_TOKEN_TYPE_NUMBER).Should().Be(1);
        }

        #endregion

        #region mcp_content_block_type_t Tests

        [Fact]
        public void MCP_CONTENT_BLOCK_TYPE_T_Should_Have_Expected_Values()
        {
            // Assert
            ((int)mcp_content_block_type_t.MCP_CONTENT_BLOCK_TYPE_TEXT).Should().Be(0);
            ((int)mcp_content_block_type_t.MCP_CONTENT_BLOCK_TYPE_IMAGE).Should().Be(1);
            ((int)mcp_content_block_type_t.MCP_CONTENT_BLOCK_TYPE_RESOURCE).Should().Be(2);
        }

        #endregion

        #region mcp_json_type_t Tests

        [Fact]
        public void MCP_JSON_TYPE_T_Should_Have_Expected_Values()
        {
            // Arrange
            var types = new[]
            {
                (mcp_json_type_t.MCP_JSON_TYPE_NULL, 0),
                (mcp_json_type_t.MCP_JSON_TYPE_BOOL, 1),
                (mcp_json_type_t.MCP_JSON_TYPE_NUMBER, 2),
                (mcp_json_type_t.MCP_JSON_TYPE_STRING, 3),
                (mcp_json_type_t.MCP_JSON_TYPE_ARRAY, 4),
                (mcp_json_type_t.MCP_JSON_TYPE_OBJECT, 5)
            };

            // Act & Assert
            foreach (var (type, expectedValue) in types)
            {
                ((int)type).Should().Be(expectedValue);
            }
        }

        #endregion

        #region mcp_address_family_t Tests

        [Fact]
        public void MCP_ADDRESS_FAMILY_T_Should_Have_Expected_Values()
        {
            // Assert
            ((int)mcp_address_family_t.MCP_AF_INET).Should().Be(0);
            ((int)mcp_address_family_t.MCP_AF_INET6).Should().Be(1);
            ((int)mcp_address_family_t.MCP_AF_UNIX).Should().Be(2);
        }

        #endregion

        #region Enum Size Tests

        [Fact]
        public void All_Enums_Should_Be_Int32_Size()
        {
            // Arrange & Act & Assert
            sizeof(mcp_role_t).Should().Be(4);
            sizeof(mcp_logging_level_t).Should().Be(4);
            sizeof(mcp_transport_type_t).Should().Be(4);
            sizeof(mcp_connection_state_t).Should().Be(4);
            sizeof(mcp_type_id_t).Should().Be(4);
            sizeof(mcp_request_id_type_t).Should().Be(4);
            sizeof(mcp_progress_token_type_t).Should().Be(4);
            sizeof(mcp_content_block_type_t).Should().Be(4);
            sizeof(mcp_json_type_t).Should().Be(4);
            sizeof(mcp_address_family_t).Should().Be(4);
        }

        #endregion
    }
}