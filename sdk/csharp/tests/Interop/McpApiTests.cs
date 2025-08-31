using System;
using System.Linq;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpApi;
using static GopherMcp.Interop.McpCallbacks;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for main MCP API P/Invoke declarations
    /// Note: These tests verify the P/Invoke declarations are properly formed
    /// and can be called without runtime errors (when the native library is available)
    /// </summary>
    public class McpApiTests
    {
        #region Library Initialization Tests

        [Fact]
        public void Library_Init_Functions_Should_Have_Correct_Signatures()
        {
            // Verify library init functions exist
            var methods = new[]
            {
                "mcp_init",
                "mcp_shutdown",
                "mcp_is_initialized",
                "mcp_get_version"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Init_Should_Take_Allocator_And_Return_Result()
        {
            // Verify init function signature
            var method = typeof(McpApi).GetMethod("mcp_init");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
        }

        [Fact]
        public void Shutdown_Should_Return_Void()
        {
            // Verify shutdown function signature
            var method = typeof(McpApi).GetMethod("mcp_shutdown");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(void));
            method.GetParameters().Should().BeEmpty();
        }

        [Fact]
        public void Is_Initialized_Should_Return_Bool()
        {
            // Verify is_initialized returns mcp_bool_t
            var method = typeof(McpApi).GetMethod("mcp_is_initialized");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_bool_t));
            method.GetParameters().Should().BeEmpty();
        }

        [Fact]
        public void Get_Version_Should_Return_IntPtr()
        {
            // Verify get_version returns string pointer
            var method = typeof(McpApi).GetMethod("mcp_get_version");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(IntPtr));
            method.GetParameters().Should().BeEmpty();
        }

        #endregion

        #region RAII Guard Function Tests

        [Fact]
        public void Guard_Functions_Should_Have_Correct_Signatures()
        {
            // Verify guard functions exist
            var methods = new[]
            {
                "mcp_guard_create",
                "mcp_guard_create_custom",
                "mcp_guard_release",
                "mcp_guard_destroy",
                "mcp_guard_is_valid",
                "mcp_guard_get"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Guard_Create_Should_Return_Handle()
        {
            // Verify guard create returns handle
            var method = typeof(McpApi).GetMethod("mcp_guard_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpGuardHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
            parameters[1].ParameterType.Should().Be(typeof(mcp_type_id_t));
        }

        [Fact]
        public void Guard_Cleanup_Delegate_Should_Have_Correct_Signature()
        {
            // Verify cleanup delegate exists
            var delegateType = typeof(McpApi).GetNestedType("mcp_guard_cleanup_fn");
            delegateType.Should().NotBeNull();
            delegateType.IsSubclassOf(typeof(MulticastDelegate)).Should().BeTrue();
            
            var invokeMethod = delegateType.GetMethod("Invoke");
            invokeMethod.Should().NotBeNull();
            invokeMethod.ReturnType.Should().Be(typeof(void));
            
            var parameters = invokeMethod.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
        }

        [Fact]
        public void Guard_Release_Should_Use_Ref_Parameter()
        {
            // Verify guard release uses ref parameter
            var method = typeof(McpApi).GetMethod("mcp_guard_release");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(IntPtr));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpGuardHandle).MakeByRefType());
            parameters[0].IsOut.Should().BeFalse();
            parameters[0].IsIn.Should().BeFalse();
        }

        #endregion

        #region Transaction Management Tests

        [Fact]
        public void Transaction_Functions_Should_Have_Correct_Signatures()
        {
            // Verify transaction functions exist
            var methods = new[]
            {
                "mcp_transaction_create",
                "mcp_transaction_create_ex",
                "mcp_transaction_add",
                "mcp_transaction_add_custom",
                "mcp_transaction_size",
                "mcp_transaction_commit",
                "mcp_transaction_rollback",
                "mcp_transaction_destroy",
                "mcp_transaction_is_valid"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Transaction_Opts_Struct_Should_Have_Expected_Layout()
        {
            // Verify transaction options struct
            var structType = typeof(McpApi).GetNestedType("mcp_transaction_opts_t");
            structType.Should().NotBeNull();
            structType.IsValueType.Should().BeTrue();
            structType.IsLayoutSequential.Should().BeTrue();
            
            var fields = structType.GetFields();
            fields.Should().HaveCount(3);
            
            fields[0].Name.Should().Be("auto_rollback");
            fields[0].FieldType.Should().Be(typeof(mcp_bool_t));
            
            fields[1].Name.Should().Be("strict_ordering");
            fields[1].FieldType.Should().Be(typeof(mcp_bool_t));
            
            fields[2].Name.Should().Be("max_resources");
            fields[2].FieldType.Should().Be(typeof(uint));
        }

        [Fact]
        public void Transaction_Create_Should_Return_Handle()
        {
            // Verify transaction create returns handle
            var method = typeof(McpApi).GetMethod("mcp_transaction_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpTransactionHandle));
            method.GetParameters().Should().BeEmpty();
        }

        [Fact]
        public void Transaction_Size_Should_Return_UIntPtr()
        {
            // Verify transaction size returns size_t
            var method = typeof(McpApi).GetMethod("mcp_transaction_size");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(UIntPtr));
        }

        #endregion

        #region Dispatcher Function Tests

        [Fact]
        public void Dispatcher_Functions_Should_Have_Correct_Signatures()
        {
            // Verify dispatcher functions exist
            var methods = new[]
            {
                "mcp_dispatcher_create",
                "mcp_dispatcher_create_guarded",
                "mcp_dispatcher_run",
                "mcp_dispatcher_run_timeout",
                "mcp_dispatcher_stop",
                "mcp_dispatcher_post",
                "mcp_dispatcher_is_thread",
                "mcp_dispatcher_create_timer",
                "mcp_dispatcher_enable_timer",
                "mcp_dispatcher_disable_timer",
                "mcp_dispatcher_destroy_timer",
                "mcp_dispatcher_destroy"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Dispatcher_Create_Should_Return_Handle()
        {
            // Verify dispatcher create returns handle
            var method = typeof(McpApi).GetMethod("mcp_dispatcher_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
            method.GetParameters().Should().BeEmpty();
        }

        [Fact]
        public void Dispatcher_Create_Guarded_Should_Have_Out_Parameter()
        {
            // Verify dispatcher create guarded has out parameter
            var method = typeof(McpApi).GetMethod("mcp_dispatcher_create_guarded");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].IsOut.Should().BeTrue();
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpGuardHandle).MakeByRefType());
        }

        [Fact]
        public void Dispatcher_Create_Timer_Should_Return_ULong()
        {
            // Verify create timer returns timer ID
            var method = typeof(McpApi).GetMethod("mcp_dispatcher_create_timer");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(ulong));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
            parameters[1].ParameterType.Should().Be(typeof(MCP_TIMER_CALLBACK));
            parameters[2].ParameterType.Should().Be(typeof(IntPtr));
        }

        #endregion

        #region Connection Management Tests

        [Fact]
        public void Connection_Functions_Should_Have_Correct_Signatures()
        {
            // Verify connection functions exist
            var methods = new[]
            {
                "mcp_connection_create_client",
                "mcp_connection_create_client_ex",
                "mcp_connection_create_client_guarded",
                "mcp_connection_configure",
                "mcp_connection_set_callbacks",
                "mcp_connection_set_watermarks",
                "mcp_connection_connect",
                "mcp_connection_write",
                "mcp_connection_close",
                "mcp_connection_get_state",
                "mcp_connection_get_stats",
                "mcp_connection_destroy"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Transport_Config_Struct_Should_Have_Expected_Fields()
        {
            // Verify transport config struct
            var structType = typeof(McpApi).GetNestedType("mcp_transport_config_t");
            structType.Should().NotBeNull();
            structType.IsValueType.Should().BeTrue();
            structType.IsLayoutSequential.Should().BeTrue();
            
            var fields = structType.GetFields();
            fields.Should().HaveCountGreaterOrEqualTo(6);
            
            fields[0].Name.Should().Be("type");
            fields[0].FieldType.Should().Be(typeof(mcp_transport_type_t));
        }

        [Fact]
        public void Connection_Write_Should_Take_Data_Pointer()
        {
            // Verify write function takes data pointer
            var method = typeof(McpApi).GetMethod("mcp_connection_write");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(5);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpConnectionHandle));
            parameters[1].ParameterType.Should().Be(typeof(IntPtr));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[3].ParameterType.Should().Be(typeof(MCP_WRITE_CALLBACK));
            parameters[4].ParameterType.Should().Be(typeof(IntPtr));
        }

        [Fact]
        public void Connection_Get_Stats_Should_Have_Out_Parameters()
        {
            // Verify get stats has out parameters
            var method = typeof(McpApi).GetMethod("mcp_connection_get_stats");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpConnectionHandle));
            parameters[1].IsOut.Should().BeTrue();
            parameters[2].IsOut.Should().BeTrue();
        }

        #endregion

        #region Listener Tests

        [Fact]
        public void Listener_Functions_Should_Have_Correct_Signatures()
        {
            // Verify listener functions exist
            var methods = new[]
            {
                "mcp_listener_create",
                "mcp_listener_create_guarded",
                "mcp_listener_configure",
                "mcp_listener_set_accept_callback",
                "mcp_listener_start",
                "mcp_listener_stop",
                "mcp_listener_destroy"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Listener_Create_Should_Return_Handle()
        {
            // Verify listener create returns handle
            var method = typeof(McpApi).GetMethod("mcp_listener_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpListenerHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_transport_type_t));
        }

        #endregion

        #region MCP Client Tests

        [Fact]
        public void Client_Functions_Should_Have_Correct_Signatures()
        {
            // Verify client functions exist
            var methods = new[]
            {
                "mcp_client_create",
                "mcp_client_create_guarded",
                "mcp_client_set_callbacks",
                "mcp_client_connect",
                "mcp_client_initialize",
                "mcp_client_send_request",
                "mcp_client_send_notification",
                "mcp_client_list_tools",
                "mcp_client_call_tool",
                "mcp_client_list_resources",
                "mcp_client_read_resource",
                "mcp_client_list_prompts",
                "mcp_client_get_prompt",
                "mcp_client_disconnect",
                "mcp_client_destroy"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Client_Create_Should_Take_Config_Reference()
        {
            // Verify client create takes config by reference
            var method = typeof(McpApi).GetMethod("mcp_client_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpClientHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_client_config_t).MakeByRefType());
        }

        [Fact]
        public void Client_Initialize_Should_Return_RequestId()
        {
            // Verify initialize returns request ID
            var method = typeof(McpApi).GetMethod("mcp_client_initialize");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpRequestIdHandle));
        }

        [Fact]
        public void Client_Send_Request_Should_Take_Method_And_Params()
        {
            // Verify send request signature
            var method = typeof(McpApi).GetMethod("mcp_client_send_request");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpRequestIdHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpClientHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_string_t));
            parameters[2].ParameterType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
        }

        #endregion

        #region MCP Server Tests

        [Fact]
        public void Server_Functions_Should_Have_Correct_Signatures()
        {
            // Verify server functions exist
            var methods = new[]
            {
                "mcp_server_create",
                "mcp_server_create_guarded",
                "mcp_server_set_callbacks",
                "mcp_server_register_tool",
                "mcp_server_register_resource",
                "mcp_server_register_prompt",
                "mcp_server_start",
                "mcp_server_send_response",
                "mcp_server_send_error",
                "mcp_server_send_notification",
                "mcp_server_stop",
                "mcp_server_destroy"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Server_Register_Tool_Should_Take_Handle()
        {
            // Verify register tool takes handle
            var method = typeof(McpApi).GetMethod("mcp_server_register_tool");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpServerHandle));
            parameters[1].ParameterType.Should().Be(typeof(McpHandles.McpToolHandle));
        }

        [Fact]
        public void Server_Send_Response_Should_Take_RequestId_And_Result()
        {
            // Verify send response signature
            var method = typeof(McpApi).GetMethod("mcp_server_send_response");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpServerHandle));
            parameters[1].ParameterType.Should().Be(typeof(McpHandles.McpRequestIdHandle));
            parameters[2].ParameterType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
        }

        #endregion

        #region JSON Value Management Tests

        [Fact]
        public void JSON_Management_Functions_Should_Have_Correct_Signatures()
        {
            // Verify JSON management functions exist
            var methods = new[]
            {
                "mcp_json_parse",
                "mcp_json_stringify",
                "mcp_json_clone",
                "mcp_json_release"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void JSON_Parse_Should_Take_String_And_Return_Handle()
        {
            // Verify JSON parse signature
            var method = typeof(McpApi).GetMethod("mcp_json_parse");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(mcp_string_t));
        }

        [Fact]
        public void JSON_Stringify_Should_Take_Pretty_Flag()
        {
            // Verify JSON stringify signature
            var method = typeof(McpApi).GetMethod("mcp_json_stringify");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(IntPtr));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_bool_t));
        }

        #endregion

        #region Utility Function Tests

        [Fact]
        public void String_Utility_Functions_Should_Have_Correct_Signatures()
        {
            // Verify string utility functions exist
            var methods = new[]
            {
                "mcp_string_from_cstr",
                "mcp_string_from_data",
                "mcp_string_dup",
                "mcp_string_buffer_free"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void String_From_CStr_Should_Have_Marshalling()
        {
            // Verify string_from_cstr has marshalling
            var method = typeof(McpApi).GetMethod("mcp_string_from_cstr");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_string_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(string));
            
            var marshalAsAttr = parameters[0].GetCustomAttributes(typeof(MarshalAsAttribute), false)
                .FirstOrDefault() as MarshalAsAttribute;
            marshalAsAttr.Should().NotBeNull();
            marshalAsAttr.Value.Should().Be(UnmanagedType.LPStr);
        }

        [Fact]
        public void Buffer_Functions_Should_Have_Correct_Signatures()
        {
            // Verify buffer functions exist
            var methods = new[]
            {
                "mcp_buffer_create",
                "mcp_buffer_append",
                "mcp_buffer_get_data",
                "mcp_buffer_free"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Buffer_Get_Data_Should_Have_Out_Parameters()
        {
            // Verify buffer get data has out parameters
            var method = typeof(McpApi).GetMethod("mcp_buffer_get_data");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpBufferHandle));
            parameters[1].IsOut.Should().BeTrue();
            parameters[2].IsOut.Should().BeTrue();
        }

        #endregion

        #region Resource Statistics Tests

        [Fact]
        public void Resource_Stats_Functions_Should_Have_Correct_Signatures()
        {
            // Verify resource stats functions exist
            var methods = new[]
            {
                "mcp_get_resource_stats",
                "mcp_check_resource_leaks",
                "mcp_print_leak_report"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Get_Resource_Stats_Should_Have_Out_Parameters()
        {
            // Verify get resource stats has out parameters
            var method = typeof(McpApi).GetMethod("mcp_get_resource_stats");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].IsOut.Should().BeTrue();
            parameters[1].IsOut.Should().BeTrue();
            parameters[2].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Check_Resource_Leaks_Should_Return_UIntPtr()
        {
            // Verify check resource leaks returns size_t
            var method = typeof(McpApi).GetMethod("mcp_check_resource_leaks");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(UIntPtr));
            method.GetParameters().Should().BeEmpty();
        }

        #endregion

        #region Helper Method Tests

        [Fact]
        public void Helper_Methods_Should_Exist()
        {
            // Verify helper methods exist
            var helpers = new[]
            {
                "GetVersion",
                "Initialize",
                "IsInitialized",
                "CreateString",
                "DuplicateString",
                "ConnectionWrite",
                "BufferAppend",
                "BufferGetData"
            };

            foreach (var helperName in helpers)
            {
                var methodInfo = typeof(McpApi).GetMethod(helperName);
                methodInfo.Should().NotBeNull($"Helper method {helperName} should exist");
            }
        }

        [Fact]
        public void Initialize_Helper_Should_Return_Result()
        {
            // Verify Initialize helper
            var method = typeof(McpApi).GetMethod("Initialize");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            method.GetParameters().Should().BeEmpty();
        }

        [Fact]
        public void IsInitialized_Helper_Should_Return_Bool()
        {
            // Verify IsInitialized helper
            var method = typeof(McpApi).GetMethod("IsInitialized");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(bool));
            method.GetParameters().Should().BeEmpty();
        }

        [Fact]
        public void ConnectionWrite_Helper_Should_Take_ByteArray()
        {
            // Verify ConnectionWrite helper
            var method = typeof(McpApi).GetMethod("ConnectionWrite");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpConnectionHandle));
            parameters[1].ParameterType.Should().Be(typeof(byte[]));
        }

        [Fact]
        public void BufferGetData_Helper_Should_Return_ByteArray()
        {
            // Verify BufferGetData helper
            var method = typeof(McpApi).GetMethod("BufferGetData");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(byte[]));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpBufferHandle));
        }

        #endregion

        #region DllImport Attribute Tests

        [Fact]
        public void All_PInvoke_Methods_Should_Use_Cdecl_Calling_Convention()
        {
            // Get all methods in McpApi
            var methods = typeof(McpApi).GetMethods(
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

        #region Return Type Validation Tests

        [Fact]
        public void Destroy_Functions_Should_Return_Void()
        {
            // Verify destroy functions return void
            var destroyMethods = new[]
            {
                "mcp_dispatcher_destroy",
                "mcp_connection_destroy",
                "mcp_listener_destroy",
                "mcp_client_destroy",
                "mcp_server_destroy",
                "mcp_buffer_free",
                "mcp_string_buffer_free"
            };

            foreach (var methodName in destroyMethods)
            {
                var method = typeof(McpApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(void),
                    $"Method {methodName} should return void");
            }
        }

        [Fact]
        public void Stop_Functions_Should_Return_Void()
        {
            // Verify stop functions return void
            var stopMethods = new[]
            {
                "mcp_dispatcher_stop",
                "mcp_listener_stop",
                "mcp_server_stop",
                "mcp_client_disconnect"
            };

            foreach (var methodName in stopMethods)
            {
                var method = typeof(McpApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(void),
                    $"Method {methodName} should return void");
            }
        }

        [Fact]
        public void Create_Functions_Should_Return_Handles()
        {
            // Verify create functions return appropriate handles
            var createFunctions = new[]
            {
                ("mcp_dispatcher_create", typeof(McpHandles.McpDispatcherHandle)),
                ("mcp_connection_create_client", typeof(McpHandles.McpConnectionHandle)),
                ("mcp_listener_create", typeof(McpHandles.McpListenerHandle)),
                ("mcp_client_create", typeof(McpHandles.McpClientHandle)),
                ("mcp_server_create", typeof(McpHandles.McpServerHandle)),
                ("mcp_guard_create", typeof(McpHandles.McpGuardHandle)),
                ("mcp_transaction_create", typeof(McpHandles.McpTransactionHandle)),
                ("mcp_buffer_create", typeof(McpHandles.McpBufferHandle))
            };

            foreach (var (methodName, expectedType) in createFunctions)
            {
                var method = typeof(McpApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(expectedType,
                    $"Method {methodName} should return {expectedType.Name}");
            }
        }

        #endregion
    }
}