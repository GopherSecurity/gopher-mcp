using System;
using System.Linq;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpJsonApi;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP JSON API P/Invoke declarations
    /// Note: These tests verify the P/Invoke declarations are properly formed
    /// and can be called without runtime errors (when the native library is available)
    /// </summary>
    public class McpJsonApiTests
    {
        #region JSON Value Operation Tests

        [Fact]
        public void JSON_Value_Functions_Should_Have_Correct_Signatures()
        {
            // Verify JSON value functions exist
            var methods = new[]
            {
                "mcp_json_parse",
                "mcp_json_stringify",
                "mcp_json_free"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpJsonApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void JSON_Parse_Should_Take_String_And_Return_Handle()
        {
            // Verify parse function signature
            var method = typeof(McpJsonApi).GetMethod("mcp_json_parse");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(string));
            
            // Verify MarshalAs attribute
            var marshalAsAttr = parameters[0].GetCustomAttributes(typeof(MarshalAsAttribute), false)
                .FirstOrDefault() as MarshalAsAttribute;
            marshalAsAttr.Should().NotBeNull();
            marshalAsAttr.Value.Should().Be(UnmanagedType.LPStr);
        }

        [Fact]
        public void JSON_Stringify_Should_Take_Handle_And_Return_IntPtr()
        {
            // Verify stringify function signature
            var method = typeof(McpJsonApi).GetMethod("mcp_json_stringify");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(IntPtr));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
        }

        [Fact]
        public void JSON_Free_Should_Take_Handle_And_Return_Void()
        {
            // Verify free function signature
            var method = typeof(McpJsonApi).GetMethod("mcp_json_free");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(void));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
        }

        #endregion

        #region Type Conversion Function Tests

        [Fact]
        public void Request_ID_Conversion_Functions_Should_Exist()
        {
            // Verify request ID conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_request_id_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_request_id_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpRequestIdHandle));
        }

        [Fact]
        public void Progress_Token_Conversion_Functions_Should_Exist()
        {
            // Verify progress token conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_progress_token_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_progress_token_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpProgressTokenHandle));
        }

        [Fact]
        public void Content_Block_Conversion_Functions_Should_Exist()
        {
            // Verify content block conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_content_block_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_content_block_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpContentBlockHandle));
        }

        [Fact]
        public void Tool_Conversion_Functions_Should_Exist()
        {
            // Verify tool conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_tool_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_tool_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpToolHandle));
        }

        [Fact]
        public void Prompt_Conversion_Functions_Should_Exist()
        {
            // Verify prompt conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_prompt_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_prompt_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpPromptHandle));
        }

        [Fact]
        public void Message_Conversion_Functions_Should_Exist()
        {
            // Verify message conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_message_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_message_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpMessageHandle));
        }

        #endregion

        #region JSON-RPC Type Conversion Tests

        [Fact]
        public void JSONRPC_Error_Conversion_Functions_Should_Exist()
        {
            // Verify JSON-RPC error conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_jsonrpc_error_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_jsonrpc_error_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpErrorHandle));
        }

        [Fact]
        public void JSONRPC_Request_Conversion_Functions_Should_Exist()
        {
            // Verify JSON-RPC request conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_jsonrpc_request_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_jsonrpc_request_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpRequestHandle));
        }

        [Fact]
        public void JSONRPC_Response_Conversion_Functions_Should_Exist()
        {
            // Verify JSON-RPC response conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_jsonrpc_response_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_jsonrpc_response_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpResponseHandle));
        }

        [Fact]
        public void JSONRPC_Notification_Conversion_Functions_Should_Exist()
        {
            // Verify JSON-RPC notification conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_jsonrpc_notification_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_jsonrpc_notification_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpNotificationHandle));
        }

        #endregion

        #region Initialize Request/Response Conversion Tests

        [Fact]
        public void Initialize_Request_Conversion_Functions_Should_Exist()
        {
            // Verify initialize request conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_initialize_request_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var parameters = toJsonMethod.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_initialize_request_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(IntPtr));
        }

        [Fact]
        public void Initialize_Result_Conversion_Functions_Should_Exist()
        {
            // Verify initialize result conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_initialize_result_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var parameters = toJsonMethod.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_initialize_result_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(IntPtr));
        }

        #endregion

        #region Enum Conversion Tests

        [Fact]
        public void Role_Conversion_Functions_Should_Have_Correct_Signatures()
        {
            // Verify role conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_role_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var parameters = toJsonMethod.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(mcp_role_t));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_role_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(mcp_role_t));
        }

        [Fact]
        public void Logging_Level_Conversion_Functions_Should_Have_Correct_Signatures()
        {
            // Verify logging level conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_logging_level_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var parameters = toJsonMethod.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(mcp_logging_level_t));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_logging_level_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(mcp_logging_level_t));
        }

        #endregion

        #region Other Type Conversion Tests

        [Fact]
        public void Resource_Conversion_Functions_Should_Exist()
        {
            // Verify resource conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_resource_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_resource_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpResourceHandle));
        }

        [Fact]
        public void Implementation_Conversion_Functions_Should_Use_IntPtr()
        {
            // Verify implementation conversion functions use IntPtr for structs
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_implementation_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var parameters = toJsonMethod.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_implementation_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(IntPtr));
        }

        [Fact]
        public void Capabilities_Conversion_Functions_Should_Use_IntPtr()
        {
            // Verify capabilities conversion functions use IntPtr for structs
            var clientToJson = typeof(McpJsonApi).GetMethod("mcp_client_capabilities_to_json");
            clientToJson.Should().NotBeNull();
            clientToJson.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var clientParams = clientToJson.GetParameters();
            clientParams.Should().HaveCount(1);
            clientParams[0].ParameterType.Should().Be(typeof(IntPtr));
            
            var serverToJson = typeof(McpJsonApi).GetMethod("mcp_server_capabilities_to_json");
            serverToJson.Should().NotBeNull();
            serverToJson.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var serverParams = serverToJson.GetParameters();
            serverParams.Should().HaveCount(1);
            serverParams[0].ParameterType.Should().Be(typeof(IntPtr));
        }

        #endregion

        #region String Conversion Tests

        [Fact]
        public void String_Conversion_Functions_Should_Have_Correct_Signatures()
        {
            // Verify string conversion functions
            var toJsonMethod = typeof(McpJsonApi).GetMethod("mcp_string_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var parameters = toJsonMethod.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(mcp_string_t));
            
            var fromJsonMethod = typeof(McpJsonApi).GetMethod("mcp_string_from_json");
            fromJsonMethod.Should().NotBeNull();
            fromJsonMethod.ReturnType.Should().Be(typeof(mcp_string_t));
        }

        #endregion

        #region Helper Method Tests

        [Fact]
        public void ParseJson_Helper_Should_Exist()
        {
            // Verify ParseJson helper method exists
            var method = typeof(McpJsonApi).GetMethod("ParseJson");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(string));
        }

        [Fact]
        public void StringifyJson_Helper_Should_Exist()
        {
            // Verify StringifyJson helper method exists
            var method = typeof(McpJsonApi).GetMethod("StringifyJson");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(string));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
        }

        [Fact]
        public void Enum_To_JsonString_Helpers_Should_Exist()
        {
            // Verify enum to JSON string helpers exist
            var roleMethod = typeof(McpJsonApi).GetMethod("RoleToJsonString");
            roleMethod.Should().NotBeNull();
            roleMethod.ReturnType.Should().Be(typeof(string));
            
            var levelMethod = typeof(McpJsonApi).GetMethod("LoggingLevelToJsonString");
            levelMethod.Should().NotBeNull();
            levelMethod.ReturnType.Should().Be(typeof(string));
        }

        [Fact]
        public void Enum_From_JsonString_Helpers_Should_Exist()
        {
            // Verify enum from JSON string helpers exist
            var roleMethod = typeof(McpJsonApi).GetMethod("RoleFromJsonString");
            roleMethod.Should().NotBeNull();
            roleMethod.ReturnType.Should().Be(typeof(mcp_role_t));
            
            var levelMethod = typeof(McpJsonApi).GetMethod("LoggingLevelFromJsonString");
            levelMethod.Should().NotBeNull();
            levelMethod.ReturnType.Should().Be(typeof(mcp_logging_level_t));
        }

        #endregion

        #region DllImport Attribute Tests

        [Fact]
        public void All_PInvoke_Methods_Should_Use_Cdecl_Calling_Convention()
        {
            // Get all methods in McpJsonApi
            var methods = typeof(McpJsonApi).GetMethods(
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

        #endregion

        #region Conversion Pattern Tests

        [Fact]
        public void All_ToJson_Methods_Should_Return_JsonValueHandle()
        {
            // Get all to_json methods
            var methods = typeof(McpJsonApi).GetMethods(
                System.Reflection.BindingFlags.Public |
                System.Reflection.BindingFlags.Static)
                .Where(m => m.Name.Contains("_to_json"));

            foreach (var method in methods)
            {
                method.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle),
                    $"Method {method.Name} should return McpJsonValueHandle");
            }
        }

        [Fact]
        public void All_FromJson_Methods_Should_Take_JsonValueHandle()
        {
            // Get all from_json methods
            var methods = typeof(McpJsonApi).GetMethods(
                System.Reflection.BindingFlags.Public |
                System.Reflection.BindingFlags.Static)
                .Where(m => m.Name.Contains("_from_json"));

            foreach (var method in methods)
            {
                var parameters = method.GetParameters();
                parameters.Should().HaveCount(1,
                    $"Method {method.Name} should have exactly one parameter");
                parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpJsonValueHandle),
                    $"Method {method.Name} should take McpJsonValueHandle as parameter");
            }
        }

        [Fact]
        public void Handle_Type_Conversions_Should_Use_Appropriate_Handles()
        {
            // Verify handle-based conversions use the correct handle types
            var handleConversions = new[]
            {
                ("mcp_request_id_to_json", typeof(McpHandles.McpRequestIdHandle)),
                ("mcp_progress_token_to_json", typeof(McpHandles.McpProgressTokenHandle)),
                ("mcp_content_block_to_json", typeof(McpHandles.McpContentBlockHandle)),
                ("mcp_tool_to_json", typeof(McpHandles.McpToolHandle)),
                ("mcp_prompt_to_json", typeof(McpHandles.McpPromptHandle)),
                ("mcp_message_to_json", typeof(McpHandles.McpMessageHandle)),
                ("mcp_jsonrpc_error_to_json", typeof(McpHandles.McpErrorHandle)),
                ("mcp_jsonrpc_request_to_json", typeof(McpHandles.McpRequestHandle)),
                ("mcp_jsonrpc_response_to_json", typeof(McpHandles.McpResponseHandle)),
                ("mcp_jsonrpc_notification_to_json", typeof(McpHandles.McpNotificationHandle)),
                ("mcp_resource_to_json", typeof(McpHandles.McpResourceHandle))
            };

            foreach (var (methodName, expectedHandleType) in handleConversions)
            {
                var method = typeof(McpJsonApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                
                var parameters = method.GetParameters();
                parameters.Should().HaveCount(1);
                parameters[0].ParameterType.Should().Be(expectedHandleType,
                    $"Method {methodName} should take {expectedHandleType.Name} as parameter");
            }
        }

        #endregion
    }
}