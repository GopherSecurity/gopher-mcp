using System;
using System.Linq;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpFilterBufferApi;
using static GopherMcp.Interop.McpHandles;
using McpBufferPoolHandle = GopherMcp.Interop.McpFilterApi.McpBufferPoolHandle;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP Filter Buffer API P/Invoke declarations
    /// Note: These tests verify the P/Invoke declarations are properly formed
    /// and can be called without runtime errors (when the native library is available)
    /// </summary>
    public class McpFilterBufferApiTests
    {
        #region Enum Tests

        [Fact]
        public void Buffer_Ownership_Enum_Should_Have_Expected_Values()
        {
            // Verify buffer ownership values
            ((int)mcp_buffer_ownership_t.MCP_BUFFER_OWNERSHIP_NONE).Should().Be(0);
            ((int)mcp_buffer_ownership_t.MCP_BUFFER_OWNERSHIP_SHARED).Should().Be(1);
            ((int)mcp_buffer_ownership_t.MCP_BUFFER_OWNERSHIP_EXCLUSIVE).Should().Be(2);
            ((int)mcp_buffer_ownership_t.MCP_BUFFER_OWNERSHIP_EXTERNAL).Should().Be(3);
        }

        #endregion

        #region Struct Tests

        [Fact]
        public void Buffer_Fragment_Struct_Should_Have_Expected_Layout()
        {
            // Verify buffer fragment struct
            var size = Marshal.SizeOf<mcp_buffer_fragment_t>();
            size.Should().BeGreaterThan(0);

            var fragment = new mcp_buffer_fragment_t();
            fragment.data.Should().Be(IntPtr.Zero);
            fragment.size.Should().Be(UIntPtr.Zero);
            fragment.release_callback.Should().BeNull();
            fragment.user_data.Should().Be(IntPtr.Zero);
        }

        [Fact]
        public void Buffer_Reservation_Struct_Should_Have_Expected_Layout()
        {
            // Verify buffer reservation struct
            var size = Marshal.SizeOf<mcp_buffer_reservation_t>();
            size.Should().BeGreaterThan(0);

            var reservation = new mcp_buffer_reservation_t();
            reservation.data.Should().Be(IntPtr.Zero);
            reservation.capacity.Should().Be(UIntPtr.Zero);
            reservation.buffer.Should().BeNull();
            reservation.reservation_id.Should().Be(0);
        }

        [Fact]
        public void Buffer_Stats_Struct_Should_Have_Expected_Fields()
        {
            // Verify buffer stats struct
            var size = Marshal.SizeOf<mcp_buffer_stats_t>();
            size.Should().BeGreaterThan(0);

            var stats = new mcp_buffer_stats_t();
            stats.total_bytes.Should().Be(UIntPtr.Zero);
            stats.used_bytes.Should().Be(UIntPtr.Zero);
            stats.slice_count.Should().Be(UIntPtr.Zero);
            stats.fragment_count.Should().Be(UIntPtr.Zero);
            stats.read_operations.Should().Be(0);
            stats.write_operations.Should().Be(0);
        }

        [Fact]
        public void Drain_Tracker_Struct_Should_Have_Expected_Layout()
        {
            // Verify drain tracker struct
            var size = Marshal.SizeOf<mcp_drain_tracker_t>();
            size.Should().BeGreaterThan(0);

            var tracker = new mcp_drain_tracker_t();
            tracker.callback.Should().BeNull();
            tracker.user_data.Should().Be(IntPtr.Zero);
        }

        [Fact]
        public void Buffer_Pool_Config_Struct_Should_Have_Expected_Fields()
        {
            // Verify buffer pool config struct
            var size = Marshal.SizeOf<mcp_buffer_pool_config_t>();
            size.Should().BeGreaterThan(0);

            var config = new mcp_buffer_pool_config_t();
            config.buffer_size.Should().Be(UIntPtr.Zero);
            config.max_buffers.Should().Be(UIntPtr.Zero);
            config.prealloc_count.Should().Be(UIntPtr.Zero);
            ((bool)config.use_thread_local).Should().BeFalse();
            ((bool)config.zero_on_alloc).Should().BeFalse();
        }

        #endregion

        #region Buffer Creation Tests

        [Fact]
        public void Buffer_Create_Owned_Should_Have_Correct_Signature()
        {
            // Verify buffer create owned function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_create_owned");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[1].ParameterType.Should().Be(typeof(mcp_buffer_ownership_t));
        }

        [Fact]
        public void Buffer_Create_View_Should_Have_Correct_Signature()
        {
            // Verify buffer create view function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_create_view");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Buffer_Create_From_Fragment_Should_Have_Correct_Signature()
        {
            // Verify buffer create from fragment function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_create_from_fragment");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(mcp_buffer_fragment_t).MakeByRefType());
        }

        [Fact]
        public void Buffer_Clone_Should_Have_Correct_Signature()
        {
            // Verify buffer clone function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_clone");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
        }

        [Fact]
        public void Buffer_Create_Cow_Should_Have_Correct_Signature()
        {
            // Verify buffer create COW function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_create_cow");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
        }

        #endregion

        #region Buffer Data Operation Tests

        [Fact]
        public void Buffer_Add_Should_Have_Correct_Signature()
        {
            // Verify buffer add function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_add");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(IntPtr));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Buffer_Add_String_Should_Have_Correct_Signature()
        {
            // Verify buffer add string function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_add_string");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(string));
        }

        [Fact]
        public void Buffer_Add_Buffer_Should_Have_Correct_Signature()
        {
            // Verify buffer add buffer function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_add_buffer");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(McpBufferHandle));
        }

        [Fact]
        public void Buffer_Add_Fragment_Should_Have_Correct_Signature()
        {
            // Verify buffer add fragment function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_add_fragment");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_buffer_fragment_t).MakeByRefType());
        }

        [Fact]
        public void Buffer_Prepend_Should_Have_Correct_Signature()
        {
            // Verify buffer prepend function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_prepend");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(IntPtr));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
        }

        #endregion

        #region Buffer Consumption Tests

        [Fact]
        public void Buffer_Drain_Should_Have_Correct_Signature()
        {
            // Verify buffer drain function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_drain");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Buffer_Move_Should_Have_Correct_Signature()
        {
            // Verify buffer move function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_move");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Buffer_Set_Drain_Tracker_Should_Have_Correct_Signature()
        {
            // Verify set drain tracker function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_set_drain_tracker");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(mcp_drain_tracker_t).MakeByRefType());
        }

        #endregion

        #region Buffer Reservation Tests

        [Fact]
        public void Buffer_Reserve_Should_Have_Correct_Signature()
        {
            // Verify buffer reserve function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_reserve");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Buffer_Reserve_Iovec_Should_Have_Correct_Signature()
        {
            // Verify buffer reserve iovec function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_reserve_iovec");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(4);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(IntPtr));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[3].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Buffer_Commit_Reservation_Should_Have_Correct_Signature()
        {
            // Verify commit reservation function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_commit_reservation");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(mcp_buffer_reservation_t).MakeByRefType());
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Buffer_Cancel_Reservation_Should_Have_Correct_Signature()
        {
            // Verify cancel reservation function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_cancel_reservation");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(mcp_buffer_reservation_t).MakeByRefType());
        }

        #endregion

        #region Buffer Access Tests

        [Fact]
        public void Buffer_Get_Contiguous_Should_Have_Correct_Signature()
        {
            // Verify get contiguous function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_get_contiguous");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(5);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[3].IsOut.Should().BeTrue();
            parameters[4].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Buffer_Linearize_Should_Have_Correct_Signature()
        {
            // Verify linearize function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_linearize");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Buffer_Peek_Should_Have_Correct_Signature()
        {
            // Verify peek function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_peek");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(4);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].ParameterType.Should().Be(typeof(IntPtr));
            parameters[3].ParameterType.Should().Be(typeof(UIntPtr));
        }

        #endregion

        #region Type-Safe I/O Tests

        [Fact]
        public void Buffer_Write_Le_Int_Should_Have_Correct_Signature()
        {
            // Verify write little-endian int function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_write_le_int");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(ulong));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Buffer_Write_Be_Int_Should_Have_Correct_Signature()
        {
            // Verify write big-endian int function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_write_be_int");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(ulong));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Buffer_Read_Le_Int_Should_Have_Correct_Signature()
        {
            // Verify read little-endian int function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_read_le_int");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Buffer_Read_Be_Int_Should_Have_Correct_Signature()
        {
            // Verify read big-endian int function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_read_be_int");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].IsOut.Should().BeTrue();
        }

        #endregion

        #region Buffer Search Tests

        [Fact]
        public void Buffer_Search_Should_Have_Correct_Signature()
        {
            // Verify buffer search function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_search");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(5);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(IntPtr));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[3].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[4].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Buffer_Find_Byte_Should_Have_Correct_Signature()
        {
            // Verify find byte function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_find_byte");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(byte));
            parameters[2].IsOut.Should().BeTrue();
        }

        #endregion

        #region Buffer Information Tests

        [Fact]
        public void Buffer_Length_Should_Return_UIntPtr()
        {
            // Verify buffer length function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_length");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(UIntPtr));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
        }

        [Fact]
        public void Buffer_Capacity_Should_Return_UIntPtr()
        {
            // Verify buffer capacity function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_capacity");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(UIntPtr));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
        }

        [Fact]
        public void Buffer_Is_Empty_Should_Return_Bool()
        {
            // Verify buffer is empty function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_is_empty");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_bool_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
        }

        [Fact]
        public void Buffer_Get_Stats_Should_Have_Correct_Signature()
        {
            // Verify get stats function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_get_stats");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].IsOut.Should().BeTrue();
        }

        #endregion

        #region Buffer Watermark Tests

        [Fact]
        public void Buffer_Set_Watermarks_Should_Have_Correct_Signature()
        {
            // Verify set watermarks function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_set_watermarks");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(4);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[3].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Buffer_Above_High_Watermark_Should_Return_Bool()
        {
            // Verify above high watermark function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_above_high_watermark");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_bool_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
        }

        [Fact]
        public void Buffer_Below_Low_Watermark_Should_Return_Bool()
        {
            // Verify below low watermark function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_below_low_watermark");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_bool_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
        }

        #endregion

        #region Buffer Pool Tests

        [Fact]
        public void Buffer_Pool_Create_Ex_Should_Have_Correct_Signature()
        {
            // Verify buffer pool create ex function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_pool_create_ex");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferPoolHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(mcp_buffer_pool_config_t).MakeByRefType());
        }

        [Fact]
        public void Buffer_Pool_Get_Stats_Should_Have_Correct_Signature()
        {
            // Verify pool get stats function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_pool_get_stats");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(4);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferPoolHandle));
            parameters[1].IsOut.Should().BeTrue();
            parameters[2].IsOut.Should().BeTrue();
            parameters[3].IsOut.Should().BeTrue();
        }

        [Fact]
        public void Buffer_Pool_Trim_Should_Have_Correct_Signature()
        {
            // Verify pool trim function
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_pool_trim");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferPoolHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
        }

        #endregion

        #region Callback Delegate Tests

        [Fact]
        public void Buffer_Fragment_Release_Callback_Should_Have_Correct_Signature()
        {
            // Verify fragment release callback delegate
            var delegateType = typeof(mcp_buffer_fragment_release_cb);
            delegateType.IsSubclassOf(typeof(MulticastDelegate)).Should().BeTrue();
            
            var invokeMethod = delegateType.GetMethod("Invoke");
            invokeMethod.Should().NotBeNull();
            invokeMethod.ReturnType.Should().Be(typeof(void));
            
            var parameters = invokeMethod.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[2].ParameterType.Should().Be(typeof(IntPtr));
        }

        [Fact]
        public void Drain_Tracker_Callback_Should_Have_Correct_Signature()
        {
            // Verify drain tracker callback delegate
            var delegateType = typeof(mcp_drain_tracker_cb);
            delegateType.IsSubclassOf(typeof(MulticastDelegate)).Should().BeTrue();
            
            var invokeMethod = delegateType.GetMethod("Invoke");
            invokeMethod.Should().NotBeNull();
            invokeMethod.ReturnType.Should().Be(typeof(void));
            
            var parameters = invokeMethod.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(UIntPtr));
            parameters[1].ParameterType.Should().Be(typeof(IntPtr));
        }

        #endregion

        #region Helper Method Tests

        [Fact]
        public void AddData_Helper_Should_Exist()
        {
            // Verify AddData helper exists
            var method = typeof(McpFilterBufferApi).GetMethod("AddData");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(byte[]));
        }

        [Fact]
        public void PrependData_Helper_Should_Exist()
        {
            // Verify PrependData helper exists
            var method = typeof(McpFilterBufferApi).GetMethod("PrependData");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(byte[]));
        }

        [Fact]
        public void PeekData_Helper_Should_Exist()
        {
            // Verify PeekData helper exists
            var method = typeof(McpFilterBufferApi).GetMethod("PeekData");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(byte[]));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(3);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(uint));
            parameters[2].ParameterType.Should().Be(typeof(uint));
        }

        [Fact]
        public void SearchPattern_Helper_Should_Exist()
        {
            // Verify SearchPattern helper exists
            var method = typeof(McpFilterBufferApi).GetMethod("SearchPattern");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(bool));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(4);
            parameters[0].ParameterType.Should().Be(typeof(McpBufferHandle));
            parameters[1].ParameterType.Should().Be(typeof(byte[]));
            parameters[2].ParameterType.Should().Be(typeof(uint));
            parameters[3].IsOut.Should().BeTrue();
        }

        [Fact]
        public void CreateFromData_Helper_Should_Exist()
        {
            // Verify CreateFromData helper exists
            var method = typeof(McpFilterBufferApi).GetMethod("CreateFromData");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpBufferHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(byte[]));
            parameters[1].ParameterType.Should().Be(typeof(mcp_buffer_ownership_t));
        }

        #endregion

        #region DllImport Attribute Tests

        [Fact]
        public void All_PInvoke_Methods_Should_Use_Cdecl_Calling_Convention()
        {
            // Get all methods in McpFilterBufferApi
            var methods = typeof(McpFilterBufferApi).GetMethods(
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
        public void Buffer_Pool_Handle_Should_Be_Defined()
        {
            // Verify buffer pool handle type exists
            typeof(McpBufferPoolHandle).Should().NotBeNull();
        }

        [Fact]
        public void Buffer_Handle_Should_Be_Defined()
        {
            // Verify buffer handle type exists
            typeof(McpBufferHandle).Should().NotBeNull();
        }

        #endregion

        #region Marshal Attribute Tests

        [Fact]
        public void String_Parameters_Should_Have_Marshal_Attributes()
        {
            // Verify mcp_buffer_add_string has proper marshalling
            var method = typeof(McpFilterBufferApi).GetMethod("mcp_buffer_add_string");
            method.Should().NotBeNull();
            
            var parameters = method.GetParameters();
            var strParam = parameters[1];
            
            var marshalAs = strParam.GetCustomAttributes(typeof(MarshalAsAttribute), false)
                .FirstOrDefault() as MarshalAsAttribute;
            
            marshalAs.Should().NotBeNull();
            marshalAs.Value.Should().Be(UnmanagedType.LPStr);
        }

        #endregion

        #region Struct Initialization Tests

        [Fact]
        public void Buffer_Fragment_Should_Support_Initialization()
        {
            // Test buffer fragment initialization
            var fragment = new mcp_buffer_fragment_t
            {
                data = new IntPtr(0x1000),
                size = new UIntPtr(1024),
                release_callback = null,
                user_data = IntPtr.Zero
            };

            fragment.data.Should().Be(new IntPtr(0x1000));
            fragment.size.Should().Be(new UIntPtr(1024));
        }

        [Fact]
        public void Buffer_Pool_Config_Should_Support_Initialization()
        {
            // Test buffer pool config initialization
            var config = new mcp_buffer_pool_config_t
            {
                buffer_size = new UIntPtr(4096),
                max_buffers = new UIntPtr(100),
                prealloc_count = new UIntPtr(10),
                use_thread_local = mcp_bool_t.True,
                zero_on_alloc = mcp_bool_t.False
            };

            config.buffer_size.Should().Be(new UIntPtr(4096));
            config.max_buffers.Should().Be(new UIntPtr(100));
            config.prealloc_count.Should().Be(new UIntPtr(10));
            ((bool)config.use_thread_local).Should().BeTrue();
            ((bool)config.zero_on_alloc).Should().BeFalse();
        }

        #endregion

        #region Helper Method Implementation Tests

        [Fact]
        public void AddData_Helper_Should_Handle_Null_Data()
        {
            // Test AddData with null data
            var buffer = new McpHandles.McpBufferHandle();
            var result = McpFilterBufferApi.AddData(buffer, null);
            result.Should().Be(mcp_result_t.MCP_ERROR_INVALID_ARGUMENT);
        }

        [Fact]
        public void AddData_Helper_Should_Handle_Empty_Data()
        {
            // Test AddData with empty data
            var buffer = new McpHandles.McpBufferHandle();
            var result = McpFilterBufferApi.AddData(buffer, new byte[0]);
            result.Should().Be(mcp_result_t.MCP_ERROR_INVALID_ARGUMENT);
        }

        [Fact]
        public void PrependData_Helper_Should_Handle_Null_Data()
        {
            // Test PrependData with null data
            var buffer = new McpHandles.McpBufferHandle();
            var result = McpFilterBufferApi.PrependData(buffer, null);
            result.Should().Be(mcp_result_t.MCP_ERROR_INVALID_ARGUMENT);
        }

        [Fact]
        public void SearchPattern_Helper_Should_Handle_Null_Pattern()
        {
            // Test SearchPattern with null pattern
            var buffer = new McpHandles.McpBufferHandle();
            uint position;
            var result = McpFilterBufferApi.SearchPattern(buffer, null, 0, out position);
            result.Should().BeFalse();
            position.Should().Be(0);
        }

        [Fact]
        public void CreateFromData_Helper_Should_Handle_Null_Data()
        {
            // Test CreateFromData with null data
            var buffer = McpFilterBufferApi.CreateFromData(null, mcp_buffer_ownership_t.MCP_BUFFER_OWNERSHIP_EXCLUSIVE);
            buffer.IsInvalid.Should().BeTrue();
        }

        #endregion
    }
}