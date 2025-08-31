using System;
using System.Linq;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpFilterApi;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP Filter API P/Invoke declarations
    /// Note: These tests verify the P/Invoke declarations are properly formed
    /// and can be called without runtime errors (when the native library is available)
    /// </summary>
    public class McpFilterApiTests
    {
        #region Enum Tests

        [Fact]
        public void Filter_Status_Enum_Should_Have_Expected_Values()
        {
            // Verify filter status values
            ((int)mcp_filter_status_t.MCP_FILTER_CONTINUE).Should().Be(0);
            ((int)mcp_filter_status_t.MCP_FILTER_STOP_ITERATION).Should().Be(1);
        }

        [Fact]
        public void Filter_Position_Enum_Should_Have_Expected_Values()
        {
            // Verify filter position values
            ((int)mcp_filter_position_t.MCP_FILTER_POSITION_FIRST).Should().Be(0);
            ((int)mcp_filter_position_t.MCP_FILTER_POSITION_LAST).Should().Be(1);
            ((int)mcp_filter_position_t.MCP_FILTER_POSITION_BEFORE).Should().Be(2);
            ((int)mcp_filter_position_t.MCP_FILTER_POSITION_AFTER).Should().Be(3);
        }

        [Fact]
        public void Protocol_Layer_Enum_Should_Have_OSI_Values()
        {
            // Verify OSI layer values
            ((int)mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_3_NETWORK).Should().Be(3);
            ((int)mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_4_TRANSPORT).Should().Be(4);
            ((int)mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_5_SESSION).Should().Be(5);
            ((int)mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_6_PRESENTATION).Should().Be(6);
            ((int)mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_7_APPLICATION).Should().Be(7);
        }

        [Fact]
        public void Transport_Protocol_Enum_Should_Have_Expected_Values()
        {
            // Verify transport protocol values
            ((int)mcp_transport_protocol_t.MCP_TRANSPORT_PROTOCOL_TCP).Should().Be(0);
            ((int)mcp_transport_protocol_t.MCP_TRANSPORT_PROTOCOL_UDP).Should().Be(1);
            ((int)mcp_transport_protocol_t.MCP_TRANSPORT_PROTOCOL_QUIC).Should().Be(2);
            ((int)mcp_transport_protocol_t.MCP_TRANSPORT_PROTOCOL_SCTP).Should().Be(3);
        }

        [Fact]
        public void App_Protocol_Enum_Should_Have_Expected_Values()
        {
            // Verify application protocol values
            ((int)mcp_app_protocol_t.MCP_APP_PROTOCOL_HTTP).Should().Be(0);
            ((int)mcp_app_protocol_t.MCP_APP_PROTOCOL_HTTPS).Should().Be(1);
            ((int)mcp_app_protocol_t.MCP_APP_PROTOCOL_HTTP2).Should().Be(2);
            ((int)mcp_app_protocol_t.MCP_APP_PROTOCOL_HTTP3).Should().Be(3);
            ((int)mcp_app_protocol_t.MCP_APP_PROTOCOL_GRPC).Should().Be(4);
            ((int)mcp_app_protocol_t.MCP_APP_PROTOCOL_WEBSOCKET).Should().Be(5);
            ((int)mcp_app_protocol_t.MCP_APP_PROTOCOL_JSONRPC).Should().Be(6);
            ((int)mcp_app_protocol_t.MCP_APP_PROTOCOL_CUSTOM).Should().Be(99);
        }

        [Fact]
        public void Builtin_Filter_Type_Enum_Should_Have_Expected_Categories()
        {
            // Network filters
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_TCP_PROXY).Should().Be(0);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_UDP_PROXY).Should().Be(1);

            // HTTP filters
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_HTTP_CODEC).Should().Be(10);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_HTTP_ROUTER).Should().Be(11);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_HTTP_COMPRESSION).Should().Be(12);

            // Security filters
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_TLS_TERMINATION).Should().Be(20);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_AUTHENTICATION).Should().Be(21);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_AUTHORIZATION).Should().Be(22);

            // Observability
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_ACCESS_LOG).Should().Be(30);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_METRICS).Should().Be(31);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_TRACING).Should().Be(32);

            // Traffic management
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_RATE_LIMIT).Should().Be(40);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_CIRCUIT_BREAKER).Should().Be(41);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_RETRY).Should().Be(42);
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_LOAD_BALANCER).Should().Be(43);

            // Custom
            ((int)mcp_builtin_filter_type_t.MCP_FILTER_CUSTOM).Should().Be(100);
        }

        [Fact]
        public void Filter_Error_Enum_Should_Have_Negative_Values()
        {
            // Verify error codes are negative
            ((int)mcp_filter_error_t.MCP_FILTER_ERROR_NONE).Should().Be(0);
            ((int)mcp_filter_error_t.MCP_FILTER_ERROR_INVALID_CONFIG).Should().Be(-1000);
            ((int)mcp_filter_error_t.MCP_FILTER_ERROR_INITIALIZATION_FAILED).Should().Be(-1001);
            ((int)mcp_filter_error_t.MCP_FILTER_ERROR_BUFFER_OVERFLOW).Should().Be(-1002);
            ((int)mcp_filter_error_t.MCP_FILTER_ERROR_PROTOCOL_VIOLATION).Should().Be(-1003);
            ((int)mcp_filter_error_t.MCP_FILTER_ERROR_UPSTREAM_TIMEOUT).Should().Be(-1004);
            ((int)mcp_filter_error_t.MCP_FILTER_ERROR_CIRCUIT_OPEN).Should().Be(-1005);
            ((int)mcp_filter_error_t.MCP_FILTER_ERROR_RESOURCE_EXHAUSTED).Should().Be(-1006);
            ((int)mcp_filter_error_t.MCP_FILTER_ERROR_INVALID_STATE).Should().Be(-1007);
        }

        #endregion

        #region Constant Tests

        [Fact]
        public void Buffer_Flags_Should_Have_Expected_Values()
        {
            // Verify buffer flag values
            MCP_BUFFER_FLAG_READONLY.Should().Be(0x01);
            MCP_BUFFER_FLAG_OWNED.Should().Be(0x02);
            MCP_BUFFER_FLAG_EXTERNAL.Should().Be(0x04);
            MCP_BUFFER_FLAG_ZERO_COPY.Should().Be(0x08);
        }

        #endregion

        #region Struct Tests

        [Fact]
        public void Filter_Config_Struct_Should_Have_Expected_Layout()
        {
            // Verify filter config struct
            var size = Marshal.SizeOf<mcp_filter_config_t>();
            size.Should().BeGreaterThan(0);
        }

        [Fact]
        public void Buffer_Slice_Struct_Should_Have_Expected_Layout()
        {
            // Verify buffer slice struct
            var size = Marshal.SizeOf<mcp_buffer_slice_t>();
            size.Should().BeGreaterThan(0);

            var slice = new mcp_buffer_slice_t();
            slice.data.Should().Be(IntPtr.Zero);
            slice.length.Should().Be(UIntPtr.Zero);
            slice.flags.Should().Be(0);
        }

        [Fact]
        public void Protocol_Metadata_Struct_Should_Have_Expected_Layout()
        {
            // Verify protocol metadata struct has expected layout
            // Note: This is not a true union in C# due to limitations with managed types
            var size = Marshal.SizeOf<mcp_protocol_metadata_t>();
            size.Should().BeGreaterThan(0);

            var metadata = new mcp_protocol_metadata_t();
            metadata.layer = mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_3_NETWORK;
        }

        [Fact]
        public void Filter_Callbacks_Struct_Should_Have_Expected_Layout()
        {
            // Verify filter callbacks struct
            var size = Marshal.SizeOf<mcp_filter_callbacks_t>();
            size.Should().BeGreaterThan(0);
        }

        [Fact]
        public void Filter_Stats_Struct_Should_Have_Expected_Fields()
        {
            // Verify filter stats struct
            var size = Marshal.SizeOf<mcp_filter_stats_t>();
            size.Should().BeGreaterThan(0);

            var stats = new mcp_filter_stats_t();
            stats.bytes_processed.Should().Be(0);
            stats.packets_processed.Should().Be(0);
            stats.errors.Should().Be(0);
            stats.processing_time_us.Should().Be(0);
            stats.throughput_mbps.Should().Be(0.0);
        }

        #endregion

        #region Filter Lifecycle Tests

        [Fact]
        public void Filter_Create_Should_Have_Correct_Signature()
        {
            // Verify filter create function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpFilterHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_filter_config_t).MakeByRefType());
        }

        [Fact]
        public void Filter_Create_Builtin_Should_Have_Correct_Signature()
        {
            // Verify builtin filter create function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_create_builtin");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpFilterHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_builtin_filter_type_t));
            parameters[2].ParameterType.Should().Be(typeof(McpHandles.McpJsonValueHandle));
        }

        [Fact]
        public void Filter_Retain_Release_Should_Have_Correct_Signatures()
        {
            // Verify retain/release functions
            var retain = typeof(McpFilterApi).GetMethod("mcp_filter_retain");
            retain.Should().NotBeNull();
            retain.ReturnType.Should().Be(typeof(void));
            retain.GetParameters().Should().HaveCount(1);

            var release = typeof(McpFilterApi).GetMethod("mcp_filter_release");
            release.Should().NotBeNull();
            release.ReturnType.Should().Be(typeof(void));
            release.GetParameters().Should().HaveCount(1);
        }

        [Fact]
        public void Filter_Set_Callbacks_Should_Have_Correct_Signature()
        {
            // Verify set callbacks function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_set_callbacks");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpFilterHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_filter_callbacks_t).MakeByRefType());
        }

        #endregion

        #region Filter Chain Tests

        [Fact]
        public void Filter_Chain_Builder_Create_Should_Have_Correct_Signature()
        {
            // Verify chain builder create function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_chain_builder_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpFilterChainBuilderHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
        }

        [Fact]
        public void Filter_Chain_Add_Filter_Should_Have_Correct_Signature()
        {
            // Verify add filter to chain function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_chain_add_filter");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(4);
            parameters[0].ParameterType.Should().Be(typeof(McpFilterChainBuilderHandle));
            parameters[1].ParameterType.Should().Be(typeof(McpHandles.McpFilterHandle));
            parameters[2].ParameterType.Should().Be(typeof(mcp_filter_position_t));
            parameters[3].ParameterType.Should().Be(typeof(McpHandles.McpFilterHandle));
        }

        [Fact]
        public void Filter_Chain_Build_Should_Have_Correct_Signature()
        {
            // Verify chain build function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_chain_build");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpFilterChainHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpFilterChainBuilderHandle));
        }

        #endregion

        #region Filter Manager Tests

        [Fact]
        public void Filter_Manager_Create_Should_Have_Correct_Signature()
        {
            // Verify manager create function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_manager_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpFilterManagerHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpConnectionHandle));
            parameters[1].ParameterType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
        }

        [Fact]
        public void Filter_Manager_Add_Methods_Should_Have_Correct_Signatures()
        {
            // Verify add filter method
            var addFilter = typeof(McpFilterApi).GetMethod("mcp_filter_manager_add_filter");
            addFilter.Should().NotBeNull();
            addFilter.ReturnType.Should().Be(typeof(mcp_result_t));

            // Verify add chain method
            var addChain = typeof(McpFilterApi).GetMethod("mcp_filter_manager_add_chain");
            addChain.Should().NotBeNull();
            addChain.ReturnType.Should().Be(typeof(mcp_result_t));
        }

        #endregion

        #region Buffer Operation Tests

        [Fact]
        public void Buffer_Get_Slices_Should_Have_Correct_Signature()
        {
            // Verify get buffer slices function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_get_buffer_slices");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_buffer_slice_t[]));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr).MakeByRefType());
        }

        [Fact]
        public void Buffer_Reserve_Should_Have_Correct_Signature()
        {
            // Verify reserve buffer function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_reserve_buffer");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Buffer_Create_Should_Have_Correct_Signature()
        {
            // Verify buffer create function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_buffer_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].ParameterType.Should().Be(typeof(uint));
        }

        [Fact]
        public void Buffer_Length_Should_Return_UIntPtr()
        {
            // Verify buffer length function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_buffer_length");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(UIntPtr));
        }

        #endregion

        #region Client/Server Integration Tests

        [Fact]
        public void Client_Context_Struct_Should_Have_Expected_Layout()
        {
            // Verify client context struct
            var size = Marshal.SizeOf<mcp_filter_client_context_t>();
            size.Should().BeGreaterThan(0);
        }

        [Fact]
        public void Server_Context_Struct_Should_Have_Expected_Layout()
        {
            // Verify server context struct
            var size = Marshal.SizeOf<mcp_filter_server_context_t>();
            size.Should().BeGreaterThan(0);
        }

        [Fact]
        public void Client_Send_Filtered_Should_Have_Correct_Signature()
        {
            // Verify client send filtered function
            var method = typeof(McpFilterApi).GetMethod("mcp_client_send_filtered");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpRequestIdHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(5);
            parameters[0].ParameterType.Should().Be(typeof(mcp_filter_client_context_t).MakeByRefType());
        }

        [Fact]
        public void Server_Process_Filtered_Should_Have_Correct_Signature()
        {
            // Verify server process filtered function
            var method = typeof(McpFilterApi).GetMethod("mcp_server_process_filtered");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(5);
            parameters[0].ParameterType.Should().Be(typeof(mcp_filter_server_context_t).MakeByRefType());
        }

        #endregion

        #region Thread-Safe Operation Tests

        [Fact]
        public void Filter_Post_Data_Should_Have_Correct_Signature()
        {
            // Verify post data function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_post_data");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(5);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpFilterHandle));
            parameters[1].ParameterType.Should().Be(typeof(IntPtr));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
        }

        #endregion

        #region Memory Management Tests

        [Fact]
        public void Filter_Guard_Create_Should_Have_Correct_Signature()
        {
            // Verify guard create function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_guard_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpFilterResourceGuardHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpDispatcherHandle));
        }

        [Fact]
        public void Filter_Guard_Add_Filter_Should_Have_Correct_Signature()
        {
            // Verify guard add filter function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_guard_add_filter");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpFilterResourceGuardHandle));
            parameters[1].ParameterType.Should().Be(typeof(McpHandles.McpFilterHandle));
        }

        #endregion

        #region Buffer Pool Tests

        [Fact]
        public void Buffer_Pool_Create_Should_Have_Correct_Signature()
        {
            // Verify buffer pool create function
            var method = typeof(McpFilterApi).GetMethod("mcp_buffer_pool_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferPoolHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Buffer_Pool_Acquire_Should_Have_Correct_Signature()
        {
            // Verify buffer pool acquire function
            var method = typeof(McpFilterApi).GetMethod("mcp_buffer_pool_acquire");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferPoolHandle));
        }

        [Fact]
        public void Buffer_Pool_Release_Should_Have_Correct_Signature()
        {
            // Verify buffer pool release function
            var method = typeof(McpFilterApi).GetMethod("mcp_buffer_pool_release");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(void));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferPoolHandle));
            parameters[1].ParameterType.Should().Be(typeof(McpBufferHandle));
        }

        #endregion

        #region Statistics Tests

        [Fact]
        public void Filter_Get_Stats_Should_Have_Correct_Signature()
        {
            // Verify get stats function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_get_stats");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpFilterHandle));
            parameters[1].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Filter_Reset_Stats_Should_Have_Correct_Signature()
        {
            // Verify reset stats function
            var method = typeof(McpFilterApi).GetMethod("mcp_filter_reset_stats");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpFilterHandle));
        }

        #endregion

        #region Callback Delegate Tests

        [Fact]
        public void Filter_Data_Callback_Should_Have_Correct_Signature()
        {
            // Verify data callback delegate
            var delegateType = typeof(mcp_filter_data_cb);
            delegateType.IsSubclassOf(typeof(MulticastDelegate)).Should().BeTrue();
            
            var invokeMethod = delegateType.GetMethod("Invoke");
            invokeMethod.Should().NotBeNull();
            invokeMethod.ReturnType.Should().Be(typeof(mcp_filter_status_t));
            
            var parameters = invokeMethod.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_bool_t));
            parameters[2].ParameterType.Should().Be(typeof(IntPtr));
        }

        [Fact]
        public void Filter_Error_Callback_Should_Have_Correct_Signature()
        {
            // Verify error callback delegate
            var delegateType = typeof(mcp_filter_error_cb);
            delegateType.IsSubclassOf(typeof(MulticastDelegate)).Should().BeTrue();
            
            var invokeMethod = delegateType.GetMethod("Invoke");
            invokeMethod.Should().NotBeNull();
            invokeMethod.ReturnType.Should().Be(typeof(void));
            
            var parameters = invokeMethod.GetParameters();
            parameters.Should().HaveCount(4);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpFilterHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_filter_error_t));
            parameters[2].ParameterType.Should().Be(typeof(string));
            parameters[3].ParameterType.Should().Be(typeof(IntPtr));
        }

        #endregion

        #region Helper Method Tests

        [Fact]
        public void CreateBuffer_Helper_Should_Exist()
        {
            // Verify CreateBuffer helper exists
            var method = typeof(McpFilterApi).GetMethod("CreateBuffer");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferHandle));
            method.GetParameters().Should().HaveCount(2);
        }

        [Fact]
        public void GetBufferData_Helper_Should_Exist()
        {
            // Verify GetBufferData helper exists
            var method = typeof(McpFilterApi).GetMethod("GetBufferData");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(byte[]));
            method.GetParameters().Should().HaveCount(1);
        }

        [Fact]
        public void PostData_Helper_Should_Exist()
        {
            // Verify PostData helper exists
            var method = typeof(McpFilterApi).GetMethod("PostData");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            method.GetParameters().Should().HaveCount(2);
        }

        #endregion

        #region DllImport Attribute Tests

        [Fact]
        public void All_PInvoke_Methods_Should_Use_Cdecl_Calling_Convention()
        {
            // Get all methods in McpFilterApi
            var methods = typeof(McpFilterApi).GetMethods(
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

        #region Handle Type Tests

        [Fact]
        public void All_Handle_Types_Should_Be_Defined()
        {
            // Verify filter-specific handle types exist
            typeof(McpFilterChainHandle).Should().NotBeNull();
            typeof(McpFilterManagerHandle).Should().NotBeNull();
            typeof(McpBufferHandle).Should().NotBeNull();
            typeof(McpFilterFactoryHandle).Should().NotBeNull();
            typeof(McpFilterChainBuilderHandle).Should().NotBeNull();
            typeof(McpFilterResourceGuardHandle).Should().NotBeNull();
            typeof(McpBufferPoolHandle).Should().NotBeNull();
        }

        #endregion

        #region Protocol Metadata Tests

        [Fact]
        public void Protocol_Metadata_Should_Support_All_Layers()
        {
            // Verify we can create metadata for different layers
            var l3Metadata = new mcp_protocol_metadata_t
            {
                layer = mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_3_NETWORK,
                l3 = new mcp_protocol_metadata_t.L3Data
                {
                    src_ip = 0x7F000001,  // 127.0.0.1
                    dst_ip = 0x08080808,  // 8.8.8.8
                    protocol = 6,         // TCP
                    ttl = 64
                }
            };

            var l4Metadata = new mcp_protocol_metadata_t
            {
                layer = mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_4_TRANSPORT,
                l4 = new mcp_protocol_metadata_t.L4Data
                {
                    src_port = 12345,
                    dst_port = 80,
                    protocol = mcp_transport_protocol_t.MCP_TRANSPORT_PROTOCOL_TCP,
                    sequence_num = 1000
                }
            };

            var l7Metadata = new mcp_protocol_metadata_t
            {
                layer = mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_7_APPLICATION,
                l7 = new mcp_protocol_metadata_t.L7Data
                {
                    protocol = mcp_app_protocol_t.MCP_APP_PROTOCOL_HTTP,
                    status_code = 200
                }
            };

            // Verify structures are created correctly
            l3Metadata.layer.Should().Be(mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_3_NETWORK);
            l4Metadata.layer.Should().Be(mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_4_TRANSPORT);
            l7Metadata.layer.Should().Be(mcp_protocol_layer_t.MCP_PROTOCOL_LAYER_7_APPLICATION);
        }

        #endregion
    }
}