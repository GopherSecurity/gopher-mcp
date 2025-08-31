using System;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpTypesApi;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP Types API P/Invoke declarations
    /// Note: These tests verify the P/Invoke declarations are properly formed
    /// and can be called without runtime errors (when the native library is available)
    /// </summary>
    public class McpTypesApiTests
    {
        #region Helper Method Tests

        [Fact]
        public void PtrToString_Should_Handle_Null_Pointer()
        {
            // Arrange
            IntPtr nullPtr = IntPtr.Zero;

            // Act
            var result = McpTypesApi.PtrToString(nullPtr);

            // Assert
            result.Should().BeNull();
        }

        [Fact]
        public void PtrToString_Should_Convert_Valid_Pointer()
        {
            // Arrange
            string testString = "Hello, MCP!";
            IntPtr ptr = Marshal.StringToHGlobalAnsi(testString);

            try
            {
                // Act
                var result = McpTypesApi.PtrToString(ptr);

                // Assert
                result.Should().Be(testString);
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }
        }

        [Fact]
        public void PtrToStringUtf8_Should_Handle_Null_Pointer()
        {
            // Arrange
            IntPtr nullPtr = IntPtr.Zero;

            // Act
            var result = McpTypesApi.PtrToStringUtf8(nullPtr);

            // Assert
            result.Should().BeNull();
        }

        [Fact]
        public void PtrToStringUtf8_Should_Convert_Valid_UTF8_String()
        {
            // Arrange
            string testString = "Hello, 世界! 🌍";
            IntPtr ptr = Marshal.StringToCoTaskMemUTF8(testString);

            try
            {
                // Act
                var result = McpTypesApi.PtrToStringUtf8(ptr);

                // Assert
                result.Should().Be(testString);
            }
            finally
            {
                Marshal.FreeCoTaskMem(ptr);
            }
        }

        #endregion

        #region P/Invoke Declaration Tests

        [Fact]
        public void Request_ID_Functions_Should_Have_Correct_Signatures()
        {
            // This test verifies that the P/Invoke declarations compile correctly
            // and have the expected signatures

            // Request ID creation functions
            Action createString = () =>
            {
                var methodInfo = typeof(McpTypesApi).GetMethod("mcp_request_id_create_string");
                methodInfo.Should().NotBeNull();
                methodInfo.ReturnType.Should().Be(typeof(IntPtr));
            };

            Action createNumber = () =>
            {
                var methodInfo = typeof(McpTypesApi).GetMethod("mcp_request_id_create_number");
                methodInfo.Should().NotBeNull();
                methodInfo.ReturnType.Should().Be(typeof(IntPtr));
            };

            // Verify methods exist
            createString.Should().NotThrow();
            createNumber.Should().NotThrow();
        }

        [Fact]
        public void Progress_Token_Functions_Should_Have_Correct_Signatures()
        {
            // Verify progress token functions exist
            var methods = new[]
            {
                "mcp_progress_token_create_string",
                "mcp_progress_token_create_number",
                "mcp_progress_token_free",
                "mcp_progress_token_is_string",
                "mcp_progress_token_is_number",
                "mcp_progress_token_get_type",
                "mcp_progress_token_get_string",
                "mcp_progress_token_get_number"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Cursor_Functions_Should_Have_Correct_Signatures()
        {
            // Verify cursor functions exist
            var methods = new[]
            {
                "mcp_cursor_create",
                "mcp_cursor_free",
                "mcp_cursor_get_value"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Content_Block_Functions_Should_Have_Correct_Signatures()
        {
            // Verify content block functions exist
            var methods = new[]
            {
                "mcp_content_block_create_text",
                "mcp_content_block_create_image",
                "mcp_content_block_create_resource",
                "mcp_content_block_free",
                "mcp_content_block_get_type",
                "mcp_content_block_get_text",
                "mcp_content_block_get_image",
                "mcp_content_block_get_resource"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Tool_Functions_Should_Have_Correct_Signatures()
        {
            // Verify tool functions exist
            var methods = new[]
            {
                "mcp_tool_create",
                "mcp_tool_free",
                "mcp_tool_get_name",
                "mcp_tool_get_description",
                "mcp_tool_set_input_schema",
                "mcp_tool_get_input_schema",
                "mcp_tool_input_schema_create",
                "mcp_tool_input_schema_free",
                "mcp_tool_input_schema_set_type",
                "mcp_tool_input_schema_get_type",
                "mcp_tool_input_schema_add_property",
                "mcp_tool_input_schema_add_required",
                "mcp_tool_input_schema_get_property_count",
                "mcp_tool_input_schema_get_required_count"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Prompt_Functions_Should_Have_Correct_Signatures()
        {
            // Verify prompt functions exist
            var methods = new[]
            {
                "mcp_prompt_create",
                "mcp_prompt_free",
                "mcp_prompt_get_name",
                "mcp_prompt_get_description",
                "mcp_prompt_add_argument",
                "mcp_prompt_get_argument_count"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Error_Functions_Should_Have_Correct_Signatures()
        {
            // Verify error functions exist
            var methods = new[]
            {
                "mcp_error_create",
                "mcp_error_free",
                "mcp_error_get_code",
                "mcp_error_get_message",
                "mcp_error_set_data",
                "mcp_error_get_data"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Message_Functions_Should_Have_Correct_Signatures()
        {
            // Verify message functions exist
            var methods = new[]
            {
                "mcp_message_create",
                "mcp_message_free",
                "mcp_message_get_role",
                "mcp_message_add_content",
                "mcp_message_get_content_count",
                "mcp_message_get_content"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Resource_Functions_Should_Have_Correct_Signatures()
        {
            // Verify resource functions exist
            var methods = new[]
            {
                "mcp_resource_create",
                "mcp_resource_free",
                "mcp_resource_get_uri",
                "mcp_resource_get_name",
                "mcp_resource_set_description",
                "mcp_resource_get_description",
                "mcp_resource_set_mime_type",
                "mcp_resource_get_mime_type"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void JSONRPC_Request_Functions_Should_Have_Correct_Signatures()
        {
            // Verify JSON-RPC request functions exist
            var methods = new[]
            {
                "mcp_jsonrpc_request_create",
                "mcp_jsonrpc_request_free",
                "mcp_jsonrpc_request_get_jsonrpc",
                "mcp_jsonrpc_request_get_method",
                "mcp_jsonrpc_request_set_id",
                "mcp_jsonrpc_request_get_id",
                "mcp_jsonrpc_request_set_params",
                "mcp_jsonrpc_request_get_params"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void JSONRPC_Response_Functions_Should_Have_Correct_Signatures()
        {
            // Verify JSON-RPC response functions exist
            var methods = new[]
            {
                "mcp_jsonrpc_response_create",
                "mcp_jsonrpc_response_free",
                "mcp_jsonrpc_response_get_jsonrpc",
                "mcp_jsonrpc_response_set_id",
                "mcp_jsonrpc_response_get_id",
                "mcp_jsonrpc_response_set_result",
                "mcp_jsonrpc_response_get_result",
                "mcp_jsonrpc_response_set_error",
                "mcp_jsonrpc_response_get_error"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void JSONRPC_Notification_Functions_Should_Have_Correct_Signatures()
        {
            // Verify JSON-RPC notification functions exist
            var methods = new[]
            {
                "mcp_jsonrpc_notification_create",
                "mcp_jsonrpc_notification_free",
                "mcp_jsonrpc_notification_get_jsonrpc",
                "mcp_jsonrpc_notification_get_method",
                "mcp_jsonrpc_notification_set_params",
                "mcp_jsonrpc_notification_get_params"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Initialize_Request_Response_Functions_Should_Have_Correct_Signatures()
        {
            // Verify initialize request/response functions exist
            var methods = new[]
            {
                "mcp_initialize_request_create",
                "mcp_initialize_request_free",
                "mcp_initialize_request_get_protocol_version",
                "mcp_initialize_request_get_client_name",
                "mcp_initialize_request_get_client_version",
                "mcp_initialize_response_create",
                "mcp_initialize_response_free",
                "mcp_initialize_response_get_protocol_version",
                "mcp_initialize_response_get_server_name",
                "mcp_initialize_response_get_server_version"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Implementation_Functions_Should_Have_Correct_Signatures()
        {
            // Verify implementation functions exist
            var methods = new[]
            {
                "mcp_implementation_create",
                "mcp_implementation_free",
                "mcp_implementation_get_name",
                "mcp_implementation_get_version",
                "mcp_implementation_set_title",
                "mcp_implementation_get_title"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Client_Capabilities_Functions_Should_Have_Correct_Signatures()
        {
            // Verify client capabilities functions exist
            var methods = new[]
            {
                "mcp_client_capabilities_create",
                "mcp_client_capabilities_free",
                "mcp_client_capabilities_has_roots",
                "mcp_client_capabilities_set_roots",
                "mcp_client_capabilities_has_sampling",
                "mcp_client_capabilities_set_sampling"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Server_Capabilities_Functions_Should_Have_Correct_Signatures()
        {
            // Verify server capabilities functions exist
            var methods = new[]
            {
                "mcp_server_capabilities_create",
                "mcp_server_capabilities_free",
                "mcp_server_capabilities_has_tools",
                "mcp_server_capabilities_set_tools",
                "mcp_server_capabilities_has_prompts",
                "mcp_server_capabilities_set_prompts",
                "mcp_server_capabilities_has_resources",
                "mcp_server_capabilities_set_resources",
                "mcp_server_capabilities_has_logging",
                "mcp_server_capabilities_set_logging"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Initialize_Result_Functions_Should_Have_Correct_Signatures()
        {
            // Verify initialize result functions exist
            var methods = new[]
            {
                "mcp_initialize_result_create",
                "mcp_initialize_result_free",
                "mcp_initialize_result_get_protocol_version",
                "mcp_initialize_result_set_server_info",
                "mcp_initialize_result_get_server_info",
                "mcp_initialize_result_set_capabilities",
                "mcp_initialize_result_get_capabilities"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpTypesApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        #endregion

        #region DllImport Attribute Tests

        [Fact]
        public void All_PInvoke_Methods_Should_Use_Cdecl_Calling_Convention()
        {
            // Get all methods in McpTypesApi
            var methods = typeof(McpTypesApi).GetMethods(
                System.Reflection.BindingFlags.Public | 
                System.Reflection.BindingFlags.Static);

            foreach (var method in methods)
            {
                var dllImportAttr = method.GetCustomAttributes(typeof(DllImportAttribute), false)
                    .FirstOrDefault() as DllImportAttribute;

                if (dllImportAttr != null)
                {
                    // All P/Invoke methods should use Cdecl calling convention
                    dllImportAttr.CallingConvention.Should().Be(CallingConvention.Cdecl,
                        $"Method {method.Name} should use Cdecl calling convention");
                }
            }
        }

        [Fact]
        public void All_String_Parameters_Should_Have_Proper_Marshalling()
        {
            // Verify that string parameters use LPStr marshalling
            var createTextMethod = typeof(McpTypesApi).GetMethod("mcp_content_block_create_text");
            createTextMethod.Should().NotBeNull();

            var parameters = createTextMethod.GetParameters();
            parameters.Should().HaveCount(1);

            var marshalAsAttr = parameters[0].GetCustomAttributes(typeof(MarshalAsAttribute), false)
                .FirstOrDefault() as MarshalAsAttribute;
            marshalAsAttr.Should().NotBeNull();
            marshalAsAttr.Value.Should().Be(UnmanagedType.LPStr);
        }

        #endregion

        #region Return Type Tests

        [Fact]
        public void Create_Functions_Should_Return_IntPtr()
        {
            // Verify that create functions return IntPtr (handles)
            var createMethods = new[]
            {
                "mcp_request_id_create_string",
                "mcp_request_id_create_number",
                "mcp_progress_token_create_string",
                "mcp_progress_token_create_number",
                "mcp_cursor_create",
                "mcp_content_block_create_text",
                "mcp_tool_create",
                "mcp_prompt_create",
                "mcp_error_create",
                "mcp_message_create",
                "mcp_resource_create"
            };

            foreach (var methodName in createMethods)
            {
                var method = typeof(McpTypesApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(IntPtr),
                    $"Method {methodName} should return IntPtr");
            }
        }

        [Fact]
        public void Free_Functions_Should_Return_Void()
        {
            // Verify that free functions return void
            var freeMethods = new[]
            {
                "mcp_request_id_free",
                "mcp_progress_token_free",
                "mcp_cursor_free",
                "mcp_content_block_free",
                "mcp_tool_free",
                "mcp_prompt_free",
                "mcp_error_free",
                "mcp_message_free",
                "mcp_resource_free"
            };

            foreach (var methodName in freeMethods)
            {
                var method = typeof(McpTypesApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(void),
                    $"Method {methodName} should return void");
            }
        }

        [Fact]
        public void Is_Functions_Should_Return_MCP_Bool()
        {
            // Verify that is_* functions return mcp_bool_t
            var isMethods = new[]
            {
                "mcp_request_id_is_string",
                "mcp_request_id_is_number",
                "mcp_request_id_is_valid",
                "mcp_progress_token_is_string",
                "mcp_progress_token_is_number"
            };

            foreach (var methodName in isMethods)
            {
                var method = typeof(McpTypesApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(mcp_bool_t),
                    $"Method {methodName} should return mcp_bool_t");
            }
        }

        [Fact]
        public void Get_Count_Functions_Should_Return_Size_T()
        {
            // Verify that count functions return size_t (UIntPtr)
            var countMethods = new[]
            {
                "mcp_tool_input_schema_get_property_count",
                "mcp_tool_input_schema_get_required_count",
                "mcp_prompt_get_argument_count",
                "mcp_message_get_content_count"
            };

            foreach (var methodName in countMethods)
            {
                var method = typeof(McpTypesApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(UIntPtr),
                    $"Method {methodName} should return UIntPtr (size_t)");
            }
        }

        #endregion
    }
}