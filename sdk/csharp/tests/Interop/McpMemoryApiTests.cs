using System;
using System.Linq;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpMemoryApi;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP Memory API P/Invoke declarations
    /// Note: These tests verify the P/Invoke declarations are properly formed
    /// and can be called without runtime errors (when the native library is available)
    /// </summary>
    public class McpMemoryApiTests
    {
        #region Error Handling Function Tests

        [Fact]
        public void Error_Handling_Functions_Should_Have_Correct_Signatures()
        {
            // Verify error handling functions exist
            var methods = new[]
            {
                "mcp_get_last_error",
                "mcp_clear_last_error",
                "mcp_set_error_handler"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpMemoryApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Get_Last_Error_Should_Return_IntPtr()
        {
            // Verify get_last_error returns IntPtr
            var method = typeof(McpMemoryApi).GetMethod("mcp_get_last_error");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(IntPtr));
            method.GetParameters().Should().BeEmpty();
        }

        [Fact]
        public void Clear_Last_Error_Should_Return_Void()
        {
            // Verify clear_last_error returns void
            var method = typeof(McpMemoryApi).GetMethod("mcp_clear_last_error");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(void));
            method.GetParameters().Should().BeEmpty();
        }

        [Fact]
        public void Error_Handler_Delegate_Should_Have_Correct_Signature()
        {
            // Verify error handler delegate exists and has correct signature
            var delegateType = typeof(McpMemoryApi).GetNestedType("mcp_error_handler_t");
            delegateType.Should().NotBeNull();
            delegateType.IsSubclassOf(typeof(MulticastDelegate)).Should().BeTrue();

            var invokeMethod = delegateType.GetMethod("Invoke");
            invokeMethod.Should().NotBeNull();
            invokeMethod.ReturnType.Should().Be(typeof(void));
            
            var parameters = invokeMethod.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
            parameters[1].ParameterType.Should().Be(typeof(IntPtr));
        }

        #endregion

        #region Memory Pool Function Tests

        [Fact]
        public void Memory_Pool_Functions_Should_Have_Correct_Signatures()
        {
            // Verify memory pool functions exist
            var methods = new[]
            {
                "mcp_memory_pool_create",
                "mcp_memory_pool_destroy",
                "mcp_memory_pool_alloc",
                "mcp_memory_pool_reset",
                "mcp_memory_pool_stats"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpMemoryApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Memory_Pool_Create_Should_Return_Handle()
        {
            // Verify memory_pool_create returns proper handle type
            var method = typeof(McpMemoryApi).GetMethod("mcp_memory_pool_create");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(McpHandles.McpMemoryPoolHandle));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Memory_Pool_Alloc_Should_Return_IntPtr()
        {
            // Verify memory_pool_alloc returns IntPtr
            var method = typeof(McpMemoryApi).GetMethod("mcp_memory_pool_alloc");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(IntPtr));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpMemoryPoolHandle));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Memory_Pool_Stats_Should_Have_Out_Parameters()
        {
            // Verify memory_pool_stats has out parameters
            var method = typeof(McpMemoryApi).GetMethod("mcp_memory_pool_stats");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(void));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(4);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpMemoryPoolHandle));
            parameters[1].IsOut.Should().BeTrue();
            parameters[2].IsOut.Should().BeTrue();
            parameters[3].IsOut.Should().BeTrue();
        }

        #endregion

        #region Batch Operation Tests

        [Fact]
        public void Batch_Op_Type_Enum_Should_Have_Expected_Values()
        {
            // Verify batch operation type enum values
            ((int)mcp_batch_op_type_t.MCP_BATCH_OP_CREATE).Should().Be(0);
            ((int)mcp_batch_op_type_t.MCP_BATCH_OP_FREE).Should().Be(1);
            ((int)mcp_batch_op_type_t.MCP_BATCH_OP_SET).Should().Be(2);
            ((int)mcp_batch_op_type_t.MCP_BATCH_OP_GET).Should().Be(3);
        }

        [Fact]
        public void Batch_Operation_Struct_Should_Have_Expected_Layout()
        {
            // Verify batch operation struct layout
            var structType = typeof(mcp_batch_operation_t);
            structType.IsValueType.Should().BeTrue();
            structType.IsLayoutSequential.Should().BeTrue();
            
            var fields = structType.GetFields();
            fields.Should().HaveCount(6);
            
            fields[0].Name.Should().Be("type");
            fields[0].FieldType.Should().Be(typeof(mcp_batch_op_type_t));
            
            fields[1].Name.Should().Be("target_type");
            fields[1].FieldType.Should().Be(typeof(mcp_type_id_t));
            
            fields[2].Name.Should().Be("target");
            fields[2].FieldType.Should().Be(typeof(IntPtr));
            
            fields[3].Name.Should().Be("param1");
            fields[3].FieldType.Should().Be(typeof(IntPtr));
            
            fields[4].Name.Should().Be("param2");
            fields[4].FieldType.Should().Be(typeof(IntPtr));
            
            fields[5].Name.Should().Be("result");
            fields[5].FieldType.Should().Be(typeof(mcp_result_t));
        }

        [Fact]
        public void Batch_Execute_Should_Have_Correct_Signature()
        {
            // Verify batch_execute function signature
            var method = typeof(McpMemoryApi).GetMethod("mcp_batch_execute");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_result_t));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(mcp_batch_operation_t[]));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
            
            // Verify [In] attribute
            var inAttr = parameters[0].GetCustomAttributes(typeof(InAttribute), false);
            inAttr.Should().HaveCount(1);
        }

        #endregion

        #region Memory Utility Function Tests

        [Fact]
        public void Memory_Utility_Functions_Should_Have_Correct_Signatures()
        {
            // Verify memory utility functions exist
            var methods = new[]
            {
                "mcp_strdup",
                "mcp_string_free",
                "mcp_malloc",
                "mcp_realloc",
                "mcp_free"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpMemoryApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Strdup_Should_Have_String_Parameter()
        {
            // Verify strdup has marshalled string parameter
            var method = typeof(McpMemoryApi).GetMethod("mcp_strdup");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(IntPtr));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(string));
            
            var marshalAsAttr = parameters[0].GetCustomAttributes(typeof(MarshalAsAttribute), false)
                .FirstOrDefault() as MarshalAsAttribute;
            marshalAsAttr.Should().NotBeNull();
            marshalAsAttr.Value.Should().Be(UnmanagedType.LPStr);
        }

        [Fact]
        public void Malloc_Should_Take_Size_And_Return_IntPtr()
        {
            // Verify malloc signature
            var method = typeof(McpMemoryApi).GetMethod("mcp_malloc");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(IntPtr));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Realloc_Should_Take_Ptr_And_Size()
        {
            // Verify realloc signature
            var method = typeof(McpMemoryApi).GetMethod("mcp_realloc");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(IntPtr));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(2);
            parameters[0].ParameterType.Should().Be(typeof(IntPtr));
            parameters[1].ParameterType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void Free_Functions_Should_Return_Void()
        {
            // Verify free functions return void
            var freeMethods = new[]
            {
                "mcp_string_free",
                "mcp_free",
                "mcp_memory_pool_destroy"
            };

            foreach (var methodName in freeMethods)
            {
                var method = typeof(McpMemoryApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(void),
                    $"Method {methodName} should return void");
            }
        }

        #endregion

        #region Helper Method Tests

        [Fact]
        public void GetLastError_Helper_Should_Exist()
        {
            // Verify GetLastError helper method exists
            var method = typeof(McpMemoryApi).GetMethod("GetLastError");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(mcp_error_info_t?));
            method.GetParameters().Should().BeEmpty();
        }

        [Fact]
        public void DuplicateString_Helper_Should_Exist()
        {
            // Verify DuplicateString helper method exists
            var method = typeof(McpMemoryApi).GetMethod("DuplicateString");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(string));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(string));
        }

        [Fact]
        public void Allocate_Helpers_Should_Exist()
        {
            // Verify Allocate helper methods exist (int and long overloads)
            var methods = typeof(McpMemoryApi).GetMethods()
                .Where(m => m.Name == "Allocate")
                .ToList();
            
            methods.Should().HaveCount(2, "Should have two Allocate overloads");
            
            var intOverload = methods.FirstOrDefault(m => 
                m.GetParameters().Length == 1 && 
                m.GetParameters()[0].ParameterType == typeof(int));
            intOverload.Should().NotBeNull();
            intOverload.ReturnType.Should().Be(typeof(IntPtr));
            
            var longOverload = methods.FirstOrDefault(m => 
                m.GetParameters().Length == 1 && 
                m.GetParameters()[0].ParameterType == typeof(long));
            longOverload.Should().NotBeNull();
            longOverload.ReturnType.Should().Be(typeof(IntPtr));
        }

        [Fact]
        public void ErrorHandlerWrapper_Should_Exist()
        {
            // Verify ErrorHandlerWrapper helper class exists
            var wrapperType = typeof(McpMemoryApi).GetNestedType("ErrorHandlerWrapper");
            wrapperType.Should().NotBeNull();
            wrapperType.IsClass.Should().BeTrue();
            
            // Verify it has required methods
            var constructor = wrapperType.GetConstructor(new[] { typeof(Action<mcp_error_info_t?, object>), typeof(object) });
            constructor.Should().NotBeNull();
            
            var getNativeHandler = wrapperType.GetMethod("GetNativeHandler");
            getNativeHandler.Should().NotBeNull();
            
            var dispose = wrapperType.GetMethod("Dispose");
            dispose.Should().NotBeNull();
        }

        #endregion

        #region DllImport Attribute Tests

        [Fact]
        public void All_PInvoke_Methods_Should_Use_Cdecl_Calling_Convention()
        {
            // Get all methods in McpMemoryApi
            var methods = typeof(McpMemoryApi).GetMethods(
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

        #region Conditional Compilation Tests

#if MCP_DEBUG
        [Fact]
        public void Debug_Functions_Should_Exist_When_MCP_DEBUG_Defined()
        {
            // Verify debug functions exist when MCP_DEBUG is defined
            var methods = new[]
            {
                "mcp_enable_resource_tracking",
                "mcp_get_resource_count",
                "mcp_print_resource_report",
                "mcp_check_leaks"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpMemoryApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist when MCP_DEBUG is defined");
            }
        }
#else
        [Fact]
        public void Debug_Functions_Should_Not_Exist_When_MCP_DEBUG_Not_Defined()
        {
            // Verify debug functions don't exist when MCP_DEBUG is not defined
            var methods = new[]
            {
                "mcp_enable_resource_tracking",
                "mcp_get_resource_count",
                "mcp_print_resource_report",
                "mcp_check_leaks"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpMemoryApi).GetMethod(methodName);
                methodInfo.Should().BeNull($"Method {methodName} should not exist when MCP_DEBUG is not defined");
            }
        }
#endif

        #endregion
    }
}