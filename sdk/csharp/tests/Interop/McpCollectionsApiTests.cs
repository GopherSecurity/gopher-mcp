using System;
using System.Linq;
using System.Runtime.InteropServices;
using Xunit;
using FluentAssertions;
using GopherMcp.Interop;
using static GopherMcp.Interop.McpTypes;
using static GopherMcp.Interop.McpCollectionsApi;

namespace GopherMcp.Tests.Interop
{
    /// <summary>
    /// Tests for MCP Collections API P/Invoke declarations
    /// Note: These tests verify the P/Invoke declarations are properly formed
    /// and can be called without runtime errors (when the native library is available)
    /// </summary>
    public class McpCollectionsApiTests
    {
        #region Helper Method Tests

        [Fact]
        public void GetMapIteratorKey_Helper_Method_Should_Exist()
        {
            // Verify helper method exists
            var method = typeof(McpCollectionsApi).GetMethod("GetMapIteratorKey");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(string));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpMapIteratorHandle));
        }

        [Fact]
        public void GetJsonString_Helper_Method_Should_Exist()
        {
            // Verify helper method exists
            var method = typeof(McpCollectionsApi).GetMethod("GetJsonString");
            method.Should().NotBeNull();
            method.ReturnType.Should().Be(typeof(string));
            
            var parameters = method.GetParameters();
            parameters.Should().HaveCount(1);
            parameters[0].ParameterType.Should().Be(typeof(McpHandles.McpJsonHandle));
        }

        #endregion

        #region List Function Tests

        [Fact]
        public void List_Functions_Should_Have_Correct_Signatures()
        {
            // Verify list functions exist
            var methods = new[]
            {
                "mcp_list_create",
                "mcp_list_create_with_capacity",
                "mcp_list_free",
                "mcp_list_append",
                "mcp_list_insert",
                "mcp_list_get",
                "mcp_list_set",
                "mcp_list_remove",
                "mcp_list_size",
                "mcp_list_capacity",
                "mcp_list_clear",
                "mcp_list_is_valid",
                "mcp_list_element_type"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpCollectionsApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void List_Create_Functions_Should_Return_Handle()
        {
            // Verify create functions return proper handle type
            var createMethod = typeof(McpCollectionsApi).GetMethod("mcp_list_create");
            createMethod.Should().NotBeNull();
            createMethod.ReturnType.Should().Be(typeof(McpHandles.McpListHandle));

            var createWithCapacityMethod = typeof(McpCollectionsApi).GetMethod("mcp_list_create_with_capacity");
            createWithCapacityMethod.Should().NotBeNull();
            createWithCapacityMethod.ReturnType.Should().Be(typeof(McpHandles.McpListHandle));
        }

        [Fact]
        public void List_Size_Functions_Should_Return_UIntPtr()
        {
            // Verify size functions return UIntPtr (size_t)
            var sizeMethod = typeof(McpCollectionsApi).GetMethod("mcp_list_size");
            sizeMethod.Should().NotBeNull();
            sizeMethod.ReturnType.Should().Be(typeof(UIntPtr));

            var capacityMethod = typeof(McpCollectionsApi).GetMethod("mcp_list_capacity");
            capacityMethod.Should().NotBeNull();
            capacityMethod.ReturnType.Should().Be(typeof(UIntPtr));
        }

        [Fact]
        public void List_Validation_Function_Should_Return_Bool()
        {
            // Verify validation function returns mcp_bool_t
            var isValidMethod = typeof(McpCollectionsApi).GetMethod("mcp_list_is_valid");
            isValidMethod.Should().NotBeNull();
            isValidMethod.ReturnType.Should().Be(typeof(mcp_bool_t));
        }

        #endregion

        #region List Iterator Function Tests

        [Fact]
        public void List_Iterator_Functions_Should_Have_Correct_Signatures()
        {
            // Verify list iterator functions exist
            var methods = new[]
            {
                "mcp_list_iterator_create",
                "mcp_list_iterator_free",
                "mcp_list_iterator_has_next",
                "mcp_list_iterator_next",
                "mcp_list_iterator_reset"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpCollectionsApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void List_Iterator_Create_Should_Return_Handle()
        {
            // Verify iterator create returns proper handle type
            var createMethod = typeof(McpCollectionsApi).GetMethod("mcp_list_iterator_create");
            createMethod.Should().NotBeNull();
            createMethod.ReturnType.Should().Be(typeof(McpHandles.McpListIteratorHandle));
        }

        #endregion

        #region Map Function Tests

        [Fact]
        public void Map_Functions_Should_Have_Correct_Signatures()
        {
            // Verify map functions exist
            var methods = new[]
            {
                "mcp_map_create",
                "mcp_map_create_with_capacity",
                "mcp_map_free",
                "mcp_map_set",
                "mcp_map_get",
                "mcp_map_has",
                "mcp_map_remove",
                "mcp_map_size",
                "mcp_map_clear",
                "mcp_map_is_valid",
                "mcp_map_value_type"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpCollectionsApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Map_Create_Functions_Should_Return_Handle()
        {
            // Verify create functions return proper handle type
            var createMethod = typeof(McpCollectionsApi).GetMethod("mcp_map_create");
            createMethod.Should().NotBeNull();
            createMethod.ReturnType.Should().Be(typeof(McpHandles.McpMapHandle));

            var createWithCapacityMethod = typeof(McpCollectionsApi).GetMethod("mcp_map_create_with_capacity");
            createWithCapacityMethod.Should().NotBeNull();
            createWithCapacityMethod.ReturnType.Should().Be(typeof(McpHandles.McpMapHandle));
        }

        [Fact]
        public void Map_String_Parameters_Should_Have_Marshalling()
        {
            // Verify string parameters use proper marshalling
            var setMethod = typeof(McpCollectionsApi).GetMethod("mcp_map_set");
            setMethod.Should().NotBeNull();

            var parameters = setMethod.GetParameters();
            parameters.Should().HaveCount(3);

            var keyParam = parameters[1];
            var marshalAsAttr = keyParam.GetCustomAttributes(typeof(MarshalAsAttribute), false)
                .FirstOrDefault() as MarshalAsAttribute;
            marshalAsAttr.Should().NotBeNull();
            marshalAsAttr.Value.Should().Be(UnmanagedType.LPStr);
        }

        #endregion

        #region Map Iterator Function Tests

        [Fact]
        public void Map_Iterator_Functions_Should_Have_Correct_Signatures()
        {
            // Verify map iterator functions exist
            var methods = new[]
            {
                "mcp_map_iterator_create",
                "mcp_map_iterator_free",
                "mcp_map_iterator_has_next",
                "mcp_map_iterator_next_key",
                "mcp_map_iterator_next_value",
                "mcp_map_iterator_reset"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpCollectionsApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Map_Iterator_Next_Key_Should_Return_IntPtr()
        {
            // Verify next_key returns IntPtr (const char*)
            var nextKeyMethod = typeof(McpCollectionsApi).GetMethod("mcp_map_iterator_next_key");
            nextKeyMethod.Should().NotBeNull();
            nextKeyMethod.ReturnType.Should().Be(typeof(IntPtr));
        }

        #endregion

        #region JSON Function Tests

        [Fact]
        public void JSON_Create_Functions_Should_Have_Correct_Signatures()
        {
            // Verify JSON create functions exist
            var methods = new[]
            {
                "mcp_json_create_null",
                "mcp_json_create_bool",
                "mcp_json_create_number",
                "mcp_json_create_string",
                "mcp_json_create_array",
                "mcp_json_create_object"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpCollectionsApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void JSON_Create_Functions_Should_Return_Handle()
        {
            // Verify JSON create functions return proper handle type
            var createNullMethod = typeof(McpCollectionsApi).GetMethod("mcp_json_create_null");
            createNullMethod.Should().NotBeNull();
            createNullMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonHandle));

            var createBoolMethod = typeof(McpCollectionsApi).GetMethod("mcp_json_create_bool");
            createBoolMethod.Should().NotBeNull();
            createBoolMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonHandle));
        }

        [Fact]
        public void JSON_Get_Functions_Should_Have_Correct_Return_Types()
        {
            // Verify JSON get functions have correct return types
            var getTypeMethod = typeof(McpCollectionsApi).GetMethod("mcp_json_get_type");
            getTypeMethod.Should().NotBeNull();
            getTypeMethod.ReturnType.Should().Be(typeof(mcp_json_type_t));

            var getBoolMethod = typeof(McpCollectionsApi).GetMethod("mcp_json_get_bool");
            getBoolMethod.Should().NotBeNull();
            getBoolMethod.ReturnType.Should().Be(typeof(mcp_bool_t));

            var getNumberMethod = typeof(McpCollectionsApi).GetMethod("mcp_json_get_number");
            getNumberMethod.Should().NotBeNull();
            getNumberMethod.ReturnType.Should().Be(typeof(double));

            var getStringMethod = typeof(McpCollectionsApi).GetMethod("mcp_json_get_string");
            getStringMethod.Should().NotBeNull();
            getStringMethod.ReturnType.Should().Be(typeof(IntPtr));
        }

        [Fact]
        public void JSON_Array_Functions_Should_Have_Correct_Signatures()
        {
            // Verify JSON array functions exist
            var methods = new[]
            {
                "mcp_json_array_size",
                "mcp_json_array_get",
                "mcp_json_array_append"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpCollectionsApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void JSON_Object_Functions_Should_Have_Correct_Signatures()
        {
            // Verify JSON object functions exist
            var methods = new[]
            {
                "mcp_json_object_set",
                "mcp_json_object_get",
                "mcp_json_object_has"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpCollectionsApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        #endregion

        #region Metadata Function Tests

        [Fact]
        public void Metadata_Functions_Should_Have_Correct_Signatures()
        {
            // Verify metadata functions exist
            var methods = new[]
            {
                "mcp_metadata_create",
                "mcp_metadata_free",
                "mcp_metadata_from_json",
                "mcp_metadata_to_json"
            };

            foreach (var methodName in methods)
            {
                var methodInfo = typeof(McpCollectionsApi).GetMethod(methodName);
                methodInfo.Should().NotBeNull($"Method {methodName} should exist");
            }
        }

        [Fact]
        public void Metadata_Create_Should_Return_Handle()
        {
            // Verify metadata create returns proper handle type
            var createMethod = typeof(McpCollectionsApi).GetMethod("mcp_metadata_create");
            createMethod.Should().NotBeNull();
            createMethod.ReturnType.Should().Be(typeof(McpHandles.McpMetadataHandle));
        }

        [Fact]
        public void Metadata_ToJson_Should_Return_JsonHandle()
        {
            // Verify metadata to_json returns JSON handle
            var toJsonMethod = typeof(McpCollectionsApi).GetMethod("mcp_metadata_to_json");
            toJsonMethod.Should().NotBeNull();
            toJsonMethod.ReturnType.Should().Be(typeof(McpHandles.McpJsonHandle));
        }

        #endregion

        #region DllImport Attribute Tests

        [Fact]
        public void All_PInvoke_Methods_Should_Use_Cdecl_Calling_Convention()
        {
            // Get all methods in McpCollectionsApi
            var methods = typeof(McpCollectionsApi).GetMethods(
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
        public void Free_Functions_Should_Return_Void()
        {
            // Verify that free functions return void
            var freeMethods = new[]
            {
                "mcp_list_free",
                "mcp_list_iterator_free",
                "mcp_map_free",
                "mcp_map_iterator_free",
                "mcp_json_free",
                "mcp_metadata_free"
            };

            foreach (var methodName in freeMethods)
            {
                var method = typeof(McpCollectionsApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(void),
                    $"Method {methodName} should return void");
            }
        }

        [Fact]
        public void Result_Functions_Should_Return_MCP_Result()
        {
            // Verify that operation functions return mcp_result_t
            var resultMethods = new[]
            {
                "mcp_list_append",
                "mcp_list_insert",
                "mcp_list_set",
                "mcp_list_remove",
                "mcp_list_clear",
                "mcp_map_set",
                "mcp_map_remove",
                "mcp_map_clear",
                "mcp_json_array_append",
                "mcp_json_object_set",
                "mcp_metadata_from_json"
            };

            foreach (var methodName in resultMethods)
            {
                var method = typeof(McpCollectionsApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(mcp_result_t),
                    $"Method {methodName} should return mcp_result_t");
            }
        }

        [Fact]
        public void Has_Functions_Should_Return_MCP_Bool()
        {
            // Verify that has/validation functions return mcp_bool_t
            var boolMethods = new[]
            {
                "mcp_list_is_valid",
                "mcp_list_iterator_has_next",
                "mcp_map_has",
                "mcp_map_is_valid",
                "mcp_map_iterator_has_next",
                "mcp_json_get_bool",
                "mcp_json_object_has"
            };

            foreach (var methodName in boolMethods)
            {
                var method = typeof(McpCollectionsApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                method.ReturnType.Should().Be(typeof(mcp_bool_t),
                    $"Method {methodName} should return mcp_bool_t");
            }
        }

        [Fact]
        public void Type_Functions_Should_Return_Type_ID()
        {
            // Verify that type functions return mcp_type_id_t
            var typeMethods = new[]
            {
                "mcp_list_element_type",
                "mcp_map_value_type",
                "mcp_json_get_type"
            };

            foreach (var methodName in typeMethods)
            {
                var method = typeof(McpCollectionsApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                
                if (methodName == "mcp_json_get_type")
                {
                    method.ReturnType.Should().Be(typeof(mcp_json_type_t),
                        $"Method {methodName} should return mcp_json_type_t");
                }
                else
                {
                    method.ReturnType.Should().Be(typeof(mcp_type_id_t),
                        $"Method {methodName} should return mcp_type_id_t");
                }
            }
        }

        #endregion

        #region Parameter Type Tests

        [Fact]
        public void Index_Parameters_Should_Be_UIntPtr()
        {
            // Verify that index parameters use UIntPtr (size_t)
            var indexMethods = new[]
            {
                ("mcp_list_insert", 1),
                ("mcp_list_get", 1),
                ("mcp_list_set", 1),
                ("mcp_list_remove", 1),
                ("mcp_json_array_get", 1)
            };

            foreach (var (methodName, paramIndex) in indexMethods)
            {
                var method = typeof(McpCollectionsApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                
                var parameters = method.GetParameters();
                parameters.Length.Should().BeGreaterThan(paramIndex);
                parameters[paramIndex].ParameterType.Should().Be(typeof(UIntPtr),
                    $"Index parameter of {methodName} should be UIntPtr");
            }
        }

        [Fact]
        public void Item_Parameters_Should_Be_IntPtr()
        {
            // Verify that item/value parameters use IntPtr (void*)
            var itemMethods = new[]
            {
                ("mcp_list_append", 1),
                ("mcp_list_insert", 2),
                ("mcp_list_set", 2),
                ("mcp_map_set", 2)
            };

            foreach (var (methodName, paramIndex) in itemMethods)
            {
                var method = typeof(McpCollectionsApi).GetMethod(methodName);
                method.Should().NotBeNull($"Method {methodName} should exist");
                
                var parameters = method.GetParameters();
                parameters.Length.Should().BeGreaterThan(paramIndex);
                parameters[paramIndex].ParameterType.Should().Be(typeof(IntPtr),
                    $"Item parameter of {methodName} should be IntPtr");
            }
        }

        #endregion
    }
}