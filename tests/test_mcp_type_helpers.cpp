#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/type_helpers.h"
#include "mcp/types.h"

using namespace mcp;

class MCPTypeHelpersTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// ==================== OPTIONAL HELPERS ====================

TEST_F(MCPTypeHelpersTest, OptionalHelpers) {
  // Test opt() with various types
  auto opt1 = opt(42);
  EXPECT_TRUE(opt1.has_value());
  EXPECT_EQ(opt1.value(), 42);

  auto opt2 = opt(std::string("test"));
  EXPECT_TRUE(opt2.has_value());
  EXPECT_EQ(opt2.value(), "test");

  // Test with rvalue
  auto opt3 = opt(std::vector<int>{1, 2, 3});
  EXPECT_TRUE(opt3.has_value());
  EXPECT_EQ(opt3->size(), 3u);

  // Test with nullptr
  auto opt4 = opt(nullptr);
  EXPECT_TRUE(opt4.has_value());
  EXPECT_EQ(opt4.value(), nullptr);

  // Test none() function
  auto none1 = none<int>();
  EXPECT_FALSE(none1.has_value());

  auto none2 = none<std::string>();
  EXPECT_FALSE(none2.has_value());

  auto none3 = none<std::vector<double>>();
  EXPECT_FALSE(none3.has_value());
}

// ==================== TYPE DISCRIMINATOR ====================

TEST_F(MCPTypeHelpersTest, TypeDiscriminatorBasic) {
  using MyDiscriminator = TypeDiscriminator<int, double, std::string>;

  // Test creation with different types
  auto var1 = MyDiscriminator::create(42);
  EXPECT_TRUE(MyDiscriminator::is_type<int>(var1));
  EXPECT_FALSE(MyDiscriminator::is_type<double>(var1));
  EXPECT_FALSE(MyDiscriminator::is_type<std::string>(var1));

  auto var2 = MyDiscriminator::create(3.14);
  EXPECT_FALSE(MyDiscriminator::is_type<int>(var2));
  EXPECT_TRUE(MyDiscriminator::is_type<double>(var2));

  auto var3 = MyDiscriminator::create(std::string("hello"));
  EXPECT_TRUE(MyDiscriminator::is_type<std::string>(var3));

  // Test get_if
  auto* int_ptr = MyDiscriminator::get_if<int>(var1);
  ASSERT_NE(int_ptr, nullptr);
  EXPECT_EQ(*int_ptr, 42);

  auto* str_ptr = MyDiscriminator::get_if<std::string>(var1);
  EXPECT_EQ(str_ptr, nullptr);

  // Test const get_if
  const auto& const_var = var3;
  auto* const_str_ptr = MyDiscriminator::get_if<std::string>(const_var);
  ASSERT_NE(const_str_ptr, nullptr);
  EXPECT_EQ(*const_str_ptr, "hello");
}

TEST_F(MCPTypeHelpersTest, TypeDiscriminatorComplex) {
  struct CustomType {
    int id;
    std::string name;
  };

  using ComplexDiscriminator = TypeDiscriminator<CustomType, std::vector<int>,
                                                 std::map<std::string, double>>;

  // Test with custom type
  CustomType custom{123, "test"};
  auto var1 = ComplexDiscriminator::create(custom);
  EXPECT_TRUE(ComplexDiscriminator::is_type<CustomType>(var1));

  auto* custom_ptr = ComplexDiscriminator::get_if<CustomType>(var1);
  ASSERT_NE(custom_ptr, nullptr);
  EXPECT_EQ(custom_ptr->id, 123);
  EXPECT_EQ(custom_ptr->name, "test");

  // Test with vector
  auto var2 = ComplexDiscriminator::create(std::vector<int>{1, 2, 3, 4, 5});
  EXPECT_TRUE(ComplexDiscriminator::is_type<std::vector<int>>(var2));

  // Test with map
  std::map<std::string, double> myMap{{"pi", 3.14}, {"e", 2.71}};
  auto var3 = ComplexDiscriminator::create(myMap);
  using MapType = std::map<std::string, double>;
  EXPECT_TRUE(ComplexDiscriminator::is_type<MapType>(var3));
}

// ==================== METHOD DISCRIMINATOR ====================

TEST_F(MCPTypeHelpersTest, MethodDiscriminatorBasic) {
  using MyMethodDiscriminator = MethodDiscriminator<int, std::string, bool>;

  // Test creation
  auto disc1 = MyMethodDiscriminator::create("method1", 42);
  EXPECT_TRUE(disc1.has_method("method1"));
  EXPECT_FALSE(disc1.has_method("method2"));
  EXPECT_TRUE(disc1.is_type<int>());

  // Test get_if
  auto* int_ptr = disc1.get_if<int>();
  ASSERT_NE(int_ptr, nullptr);
  EXPECT_EQ(*int_ptr, 42);

  // Test with string data
  auto disc2 =
      MyMethodDiscriminator::create("test/method", std::string("data"));
  EXPECT_EQ(disc2.method, "test/method");
  EXPECT_TRUE(disc2.is_type<std::string>());

  // Test const get_if
  const auto& const_disc = disc2;
  auto* const_str_ptr = const_disc.get_if<std::string>();
  ASSERT_NE(const_str_ptr, nullptr);
  EXPECT_EQ(*const_str_ptr, "data");
}

TEST_F(MCPTypeHelpersTest, MethodDiscriminatorEdgeCases) {
  struct Request {
    std::string id;
    std::map<std::string, std::string> params;
  };

  using RequestDiscriminator =
      MethodDiscriminator<Request, Error, std::nullptr_t>;

  // Test with complex type
  Request req{"req-123", {{"key1", "value1"}, {"key2", "value2"}}};
  auto disc = RequestDiscriminator::create("rpc/call", req);

  EXPECT_TRUE(disc.has_method("rpc/call"));
  EXPECT_TRUE(disc.is_type<Request>());

  auto* req_ptr = disc.get_if<Request>();
  ASSERT_NE(req_ptr, nullptr);
  EXPECT_EQ(req_ptr->id, "req-123");
  EXPECT_EQ(req_ptr->params.size(), 2u);

  // Test with empty method
  auto disc2 = RequestDiscriminator::create("", nullptr);
  EXPECT_TRUE(disc2.has_method(""));
  EXPECT_TRUE(disc2.is_type<std::nullptr_t>());
}

// ==================== MAKE_METHOD_NOTIFICATION ====================

TEST_F(MCPTypeHelpersTest, MakeMethodNotification) {
  // Test with simple type
  auto notif1 = make_method_notification("test", 123);
  EXPECT_EQ(notif1.method, "test");
  EXPECT_TRUE(notif1.is_type<int>());

  // Test with complex type
  std::vector<std::string> data{"a", "b", "c"};
  auto notif2 = make_method_notification("complex/notification", data);
  EXPECT_EQ(notif2.method, "complex/notification");

  auto* vec_ptr = notif2.get_if<std::vector<std::string>>();
  ASSERT_NE(vec_ptr, nullptr);
  EXPECT_EQ(vec_ptr->size(), 3u);

  // Test with rvalue
  auto notif3 =
      make_method_notification("rvalue", std::map<int, int>{{1, 10}, {2, 20}});
  using IntMapType = std::map<int, int>;
  EXPECT_TRUE(notif3.is_type<IntMapType>());
}

// ==================== METADATA ====================

TEST_F(MCPTypeHelpersTest, MetadataBasic) {
  // Test empty metadata
  auto meta1 = make_metadata();
  EXPECT_TRUE(meta1.empty());

  // Test adding different types
  add_metadata(meta1, "null", nullptr);
  add_metadata(meta1, "bool", true);
  add_metadata(meta1, "int", 42);
  add_metadata(meta1, "double", 3.14);
  add_metadata(meta1, "string", std::string("test"));

  EXPECT_EQ(meta1.size(), 5u);
  EXPECT_TRUE(meta1["null"].holds_alternative<std::nullptr_t>());
  EXPECT_TRUE(meta1["bool"].holds_alternative<bool>());
  EXPECT_TRUE(meta1["int"].holds_alternative<int64_t>());
  EXPECT_TRUE(meta1["double"].holds_alternative<double>());
  EXPECT_TRUE(meta1["string"].holds_alternative<std::string>());

  // Test additional basic metadata types  
  add_metadata(meta1, "string_value", std::string("test_string"));
  add_metadata(meta1, "int64_value", static_cast<int64_t>(9999));
  add_metadata(meta1, "bool_false", false);
  add_metadata(meta1, "null_value", nullptr);

  EXPECT_TRUE(meta1["string_value"].holds_alternative<std::string>());
  EXPECT_TRUE(meta1["int64_value"].holds_alternative<int64_t>());
  EXPECT_TRUE(meta1["bool_false"].holds_alternative<bool>());
  EXPECT_TRUE(meta1["null_value"].holds_alternative<std::nullptr_t>());
}

TEST_F(MCPTypeHelpersTest, MetadataEdgeCases) {
  // Test with empty strings as keys
  auto meta = make_metadata();
  add_metadata(meta, "", "empty_key");
  EXPECT_TRUE(meta.count("") > 0);
  EXPECT_TRUE(meta[""].holds_alternative<std::string>());

  // Test overwriting values
  add_metadata(meta, "key", 1);
  EXPECT_TRUE(meta["key"].holds_alternative<int64_t>());

  add_metadata(meta, "key", "overwritten");
  EXPECT_FALSE(meta["key"].holds_alternative<int>());
  EXPECT_TRUE(meta["key"].holds_alternative<std::string>());

  // Test with special characters in keys
  add_metadata(meta, "key!@#$%^&*()", "special");
  EXPECT_TRUE(meta["key!@#$%^&*()"].holds_alternative<std::string>());

  // Test large metadata
  for (int i = 0; i < 1000; ++i) {
    add_metadata(meta, "key" + std::to_string(i), i);
  }
  EXPECT_GE(meta.size(), 1000u);
}

// ==================== CAPABILITY HELPERS ====================

TEST_F(MCPTypeHelpersTest, CapabilityBasic) {
  struct Features {
    bool feature1;
    bool feature2;
    std::string version;
  };

  // Test without experimental
  auto cap1 = Capability<Features>::without_experimental();
  EXPECT_FALSE(cap1.experimental.has_value());

  // Test with experimental
  Features features{true, false, "1.0"};
  auto cap2 = Capability<Features>::with_experimental(std::move(features));
  EXPECT_TRUE(cap2.experimental.has_value());
  EXPECT_TRUE(cap2.experimental->feature1);
  EXPECT_FALSE(cap2.experimental->feature2);
  EXPECT_EQ(cap2.experimental->version, "1.0");

  // Test factory functions
  auto cap3 = make_capability<Features>();
  EXPECT_FALSE(cap3.experimental.has_value());

  auto cap4 = make_capability(Features{false, true, "2.0"});
  EXPECT_TRUE(cap4.experimental.has_value());
  EXPECT_FALSE(cap4.experimental->feature1);
  EXPECT_TRUE(cap4.experimental->feature2);
}

TEST_F(MCPTypeHelpersTest, CapabilityComplex) {
  struct ComplexFeatures {
    std::map<std::string, bool> flags;
    std::vector<std::string> supported_versions;
    optional<int> max_connections;
  };

  ComplexFeatures features;
  features.flags["async"] = true;
  features.flags["streaming"] = false;
  features.supported_versions = {"1.0", "1.1", "2.0"};
  features.max_connections = opt(100);

  auto cap = make_capability(std::move(features));
  ASSERT_TRUE(cap.experimental.has_value());
  EXPECT_EQ(cap.experimental->flags.size(), 2u);
  EXPECT_TRUE(cap.experimental->flags["async"]);
  EXPECT_EQ(cap.experimental->supported_versions.size(), 3u);
  ASSERT_TRUE(cap.experimental->max_connections.has_value());
  EXPECT_EQ(cap.experimental->max_connections.value(), 100);
}

// ==================== RESULT TYPE ====================

TEST_F(MCPTypeHelpersTest, ResultType) {
  // Test Result type alias
  Result<int> result1 = 42;
  EXPECT_TRUE(result1.holds_alternative<int>());
  EXPECT_EQ(result1.get<int>(), 42);

  Result<int> result2 = Error(404, "Not found");
  EXPECT_TRUE(result2.holds_alternative<Error>());
  EXPECT_EQ(result2.get<Error>().code, 404);

  // Test with complex types
  struct Data {
    std::string id;
    std::vector<int> values;
  };

  Result<Data> result3 = Data{"id-123", {1, 2, 3}};
  EXPECT_TRUE(result3.holds_alternative<Data>());

  auto& data = result3.get<Data>();
  EXPECT_EQ(data.id, "id-123");
  EXPECT_EQ(data.values.size(), 3u);

  // Test with void-like result (using nullptr)
  Result<std::nullptr_t> result4 = nullptr;
  EXPECT_TRUE(result4.holds_alternative<std::nullptr_t>());

  Result<std::nullptr_t> result5 = Error(500, "Internal error");
  EXPECT_TRUE(result5.holds_alternative<Error>());
}

// ==================== ARRAY FACTORIES ====================

TEST_F(MCPTypeHelpersTest, ArrayFactories) {
  // Test empty array
  auto arr1 = make_array<int>();
  EXPECT_TRUE(arr1.empty());

  // Test with initializer list
  auto arr2 = make_array<int>({1, 2, 3, 4, 5});
  EXPECT_EQ(arr2.size(), 5u);
  EXPECT_EQ(arr2[0], 1);
  EXPECT_EQ(arr2[4], 5);

  // Test with variadic arguments
  auto arr3 = make_array<std::string>("one", "two", "three");
  EXPECT_EQ(arr3.size(), 3u);
  EXPECT_EQ(arr3[0], "one");
  EXPECT_EQ(arr3[2], "three");

  // Test with complex types
  struct Point {
    int x, y;
  };
  auto arr4 = make_array<Point>(Point{1, 2}, Point{3, 4}, Point{5, 6});
  EXPECT_EQ(arr4.size(), 3u);
  EXPECT_EQ(arr4[1].x, 3);
  EXPECT_EQ(arr4[1].y, 4);

  // Test large array
  auto arr5 = make_array<int>();
  for (int i = 0; i < 10000; ++i) {
    arr5.push_back(i);
  }
  EXPECT_EQ(arr5.size(), 10000u);
}

// ==================== OBJECT BUILDER ====================

TEST_F(MCPTypeHelpersTest, ObjectBuilderBasic) {
  struct Person {
    std::string name;
    int age;
    optional<std::string> email;
    optional<std::string> phone;
    bool active;
  };

  // Test basic building
  auto person1 = make_object<Person>()
                     .set(&Person::name, std::string("John Doe"))
                     .set(&Person::age, 30)
                     .set(&Person::active, true)
                     .build();

  EXPECT_EQ(person1.name, "John Doe");
  EXPECT_EQ(person1.age, 30);
  EXPECT_TRUE(person1.active);
  EXPECT_FALSE(person1.email.has_value());
  EXPECT_FALSE(person1.phone.has_value());

  // Test set_optional
  auto person2 =
      make_object<Person>()
          .set(&Person::name, std::string("Jane Doe"))
          .set(&Person::age, 25)
          .set_optional(&Person::email, std::string("jane@example.com"))
          .set_optional(&Person::phone, std::string("+1234567890"))
          .set(&Person::active, false)
          .build();

  EXPECT_EQ(person2.name, "Jane Doe");
  EXPECT_EQ(person2.age, 25);
  EXPECT_FALSE(person2.active);
  ASSERT_TRUE(person2.email.has_value());
  EXPECT_EQ(person2.email.value(), "jane@example.com");
  ASSERT_TRUE(person2.phone.has_value());
  EXPECT_EQ(person2.phone.value(), "+1234567890");
}

TEST_F(MCPTypeHelpersTest, ObjectBuilderComplex) {
  struct Address {
    std::string street;
    std::string city;
    std::string country;
    optional<std::string> zip;
  };

  struct Company {
    std::string name;
    Address headquarters;
    std::vector<std::string> departments;
    std::map<std::string, int> employee_count;
    optional<std::string> website;
  };

  // Build complex nested structure
  Address addr = make_object<Address>()
                     .set(&Address::street, std::string("123 Main St"))
                     .set(&Address::city, std::string("Tech City"))
                     .set(&Address::country, std::string("Techland"))
                     .set_optional(&Address::zip, std::string("12345"))
                     .build();

  auto company =
      make_object<Company>()
          .set(&Company::name, std::string("TechCorp"))
          .set(&Company::headquarters, std::move(addr))
          .set(&Company::departments,
               std::vector<std::string>{"Engineering", "Sales", "HR"})
          .set(&Company::employee_count,
               std::map<std::string, int>{
                   {"Engineering", 100}, {"Sales", 50}, {"HR", 10}})
          .set_optional(&Company::website,
                        std::string("https://techcorp.example"))
          .build();

  EXPECT_EQ(company.name, "TechCorp");
  EXPECT_EQ(company.headquarters.city, "Tech City");
  EXPECT_EQ(company.departments.size(), 3u);
  EXPECT_EQ(company.employee_count["Engineering"], 100);
  ASSERT_TRUE(company.website.has_value());
  EXPECT_EQ(company.website.value(), "https://techcorp.example");

  // Test const& build
  ObjectBuilder<Company> builder;
  builder.set(&Company::name, std::string("Another Corp"));
  const auto& const_builder = builder;
  Company copy = const_builder.build();
  EXPECT_EQ(copy.name, "Another Corp");
}

// ==================== STRING LITERAL ====================

TEST_F(MCPTypeHelpersTest, StringLiteralBasic) {
  // Test construction and comparison
  constexpr auto lit1 = make_string_literal("hello");
  constexpr auto lit2 = make_string_literal("hello");
  constexpr auto lit3 = make_string_literal("world");

  static_assert(lit1 == lit2, "Same literals should be equal");
  static_assert(!(lit1 == lit3), "Different literals should not be equal");

  // Test c_str and size
  EXPECT_STREQ(lit1.c_str(), "hello");
  EXPECT_EQ(lit1.size(), 5u);

  EXPECT_STREQ(lit3.c_str(), "world");
  EXPECT_EQ(lit3.size(), 5u);

  // Test empty string
  constexpr auto empty = make_string_literal("");
  EXPECT_STREQ(empty.c_str(), "");
  EXPECT_EQ(empty.size(), 0u);
}

TEST_F(MCPTypeHelpersTest, StringLiteralEdgeCases) {
  // Test with special characters
  constexpr auto special = make_string_literal("Hello\nWorld\t!");
  EXPECT_STREQ(special.c_str(), "Hello\nWorld\t!");
  EXPECT_EQ(special.size(), 13u);

  // Test with numbers
  constexpr auto numbers = make_string_literal("12345");
  EXPECT_STREQ(numbers.c_str(), "12345");
  EXPECT_EQ(numbers.size(), 5u);

  // Test long string
  constexpr auto long_str = make_string_literal(
      "This is a very long string literal that tests the string_literal "
      "template");
  EXPECT_EQ(long_str.size(), 73u);
}

// ==================== TYPE TRAITS HELPERS ====================

TEST_F(MCPTypeHelpersTest, RemoveCvrefT) {
  // Test remove_cvref_t
  static_assert(std::is_same<remove_cvref_t<int>, int>::value, "");
  static_assert(std::is_same<remove_cvref_t<const int>, int>::value, "");
  static_assert(std::is_same<remove_cvref_t<int&>, int>::value, "");
  static_assert(std::is_same<remove_cvref_t<const int&>, int>::value, "");
  static_assert(std::is_same<remove_cvref_t<int&&>, int>::value, "");
  static_assert(std::is_same<remove_cvref_t<const int&&>, int>::value, "");
  static_assert(std::is_same<remove_cvref_t<volatile int&>, int>::value, "");
  static_assert(std::is_same<remove_cvref_t<const volatile int&>, int>::value,
                "");
}

TEST_F(MCPTypeHelpersTest, IsSameDecayed) {
  // Test is_same_decayed
  static_assert(is_same_decayed<int, int>::value, "");
  static_assert(is_same_decayed<int, const int>::value, "");
  static_assert(is_same_decayed<int&, int>::value, "");
  static_assert(is_same_decayed<const int&, int&&>::value, "");
  static_assert(!is_same_decayed<int, double>::value, "");
  static_assert(!is_same_decayed<std::string, const char*>::value, "");
}

// ==================== MATCH HELPER ====================

TEST_F(MCPTypeHelpersTest, MatchHelper) {
  using TestVariant = variant<int, double, std::string, bool>;

  // Test with int
  TestVariant v1 = 42;
  auto result1 = match(
      v1, [](int i) { return std::string("int: ") + std::to_string(i); },
      [](double d) { return std::string("double: ") + std::to_string(d); },
      [](const std::string& s) { return std::string("string: ") + s; },
      [](bool b) { return std::string("bool: ") + (b ? "true" : "false"); });
  EXPECT_EQ(result1, "int: 42");

  // Test with string
  TestVariant v2 = std::string("hello");
  auto result2 = match(
      v2, [](int) { return 0; }, [](double) { return 1; },
      [](const std::string& s) { return static_cast<int>(s.length()); },
      [](bool) { return 3; });
  EXPECT_EQ(result2, 5);

  // Test with bool
  TestVariant v3 = true;
  bool result3 = match(
      v3, [](int) { return false; }, [](double) { return false; },
      [](const std::string&) { return false; }, [](bool b) { return b; });
  EXPECT_TRUE(result3);

  // Test with mutable variant
  TestVariant v4 = 3.14;
  match(
      v4, [](int& i) { i *= 2; }, [](double& d) { d *= 2; },
      [](std::string& s) { s += s; }, [](bool& b) { b = !b; });
  EXPECT_DOUBLE_EQ(v4.get<double>(), 6.28);
}

TEST_F(MCPTypeHelpersTest, MatchComplexTypes) {
  struct A {
    int value;
  };
  struct B {
    double value;
  };
  struct C {
    std::string value;
  };

  using ComplexVariant = variant<A, B, C>;

  ComplexVariant v1 = A{100};
  auto result1 = match(
      v1, [](const A& a) { return a.value; },
      [](const B& b) { return static_cast<int>(b.value); },
      [](const C& c) { return static_cast<int>(c.value.length()); });
  EXPECT_EQ(result1, 100);

  ComplexVariant v2 = C{"test string"};
  std::string result2 = match(
      v2, [](const A& a) { return std::to_string(a.value); },
      [](const B& b) { return std::to_string(b.value); },
      [](const C& c) { return c.value; });
  EXPECT_EQ(result2, "test string");
}

// ==================== INTEGRATION TESTS ====================

TEST_F(MCPTypeHelpersTest, IntegrationComplexScenario) {
  // Simulate a complex RPC scenario using multiple helpers

  // Define request and response types
  struct GetUserRequest {
    std::string user_id;
    optional<std::vector<std::string>> fields;
  };

  struct User {
    std::string id;
    std::string name;
    int age;
    optional<std::string> email;
    Metadata extra_data;
  };

  using RpcResult = Result<User>;
  using RpcMessage = MethodDiscriminator<GetUserRequest, User, Error>;

  // Build request
  auto request =
      make_object<GetUserRequest>()
          .set(&GetUserRequest::user_id, std::string("user-123"))
          .set_optional(&GetUserRequest::fields,
                        make_array<std::string>("name", "age", "email"))
          .build();

  // Create method message
  auto message = RpcMessage::create("getUser", request);
  EXPECT_EQ(message.method, "getUser");
  EXPECT_TRUE(message.is_type<GetUserRequest>());

  // Simulate processing and response
  auto* req_ptr = message.get_if<GetUserRequest>();
  ASSERT_NE(req_ptr, nullptr);

  // Build user response
  auto user_meta = make_metadata();
  add_metadata(user_meta, "last_login", "2023-01-01");
  add_metadata(user_meta, "account_type", "premium");
  add_metadata(user_meta, "verified", true);

  auto user = make_object<User>()
                  .set(&User::id, std::string(req_ptr->user_id))
                  .set(&User::name, std::string("John Doe"))
                  .set(&User::age, 30)
                  .set_optional(&User::email, std::string("john@example.com"))
                  .set(&User::extra_data, std::move(user_meta))
                  .build();

  // Create result
  RpcResult result = user;
  EXPECT_TRUE(result.holds_alternative<User>());

  // Process result using match
  auto summary = match(
      result,
      [](const User& u) {
        return "User: " + u.name + " (age: " + std::to_string(u.age) + ")";
      },
      [](const Error& e) {
        return "Error " + std::to_string(e.code) + ": " + e.message;
      });

  EXPECT_EQ(summary, "User: John Doe (age: 30)");

  // Test error case
  RpcResult error_result = Error(404, "User not found");
  auto error_summary = match(
      error_result, [](const User&) { return std::string("Success"); },
      [](const Error& e) { return "Error: " + e.message; });

  EXPECT_EQ(error_summary, "Error: User not found");
}

TEST_F(MCPTypeHelpersTest, IntegrationCapabilityNegotiation) {
  // Simulate capability negotiation scenario
  struct ProtocolCapabilities {
    bool supports_streaming;
    bool supports_compression;
    int max_message_size;
    std::vector<std::string> supported_encodings;
  };

  // Server capabilities
  auto server_caps =
      make_object<ProtocolCapabilities>()
          .set(&ProtocolCapabilities::supports_streaming, true)
          .set(&ProtocolCapabilities::supports_compression, true)
          .set(&ProtocolCapabilities::max_message_size, 1048576)  // 1MB
          .set(&ProtocolCapabilities::supported_encodings,
               make_array<std::string>("json", "msgpack", "protobuf"))
          .build();

  // Client capabilities
  auto client_caps =
      make_object<ProtocolCapabilities>()
          .set(&ProtocolCapabilities::supports_streaming, false)
          .set(&ProtocolCapabilities::supports_compression, true)
          .set(&ProtocolCapabilities::max_message_size, 524288)  // 512KB
          .set(&ProtocolCapabilities::supported_encodings,
               make_array<std::string>("json", "msgpack"))
          .build();

  // Negotiate capabilities (take minimum/intersection)
  auto negotiated =
      make_object<ProtocolCapabilities>()
          .set(&ProtocolCapabilities::supports_streaming,
               server_caps.supports_streaming && client_caps.supports_streaming)
          .set(&ProtocolCapabilities::supports_compression,
               server_caps.supports_compression &&
                   client_caps.supports_compression)
          .set(&ProtocolCapabilities::max_message_size,
               static_cast<int>(std::min(server_caps.max_message_size,
                                         client_caps.max_message_size)))
          .build();

  EXPECT_FALSE(negotiated.supports_streaming);     // Client doesn't support
  EXPECT_TRUE(negotiated.supports_compression);    // Both support
  EXPECT_EQ(negotiated.max_message_size, 524288);  // Minimum of both

  // Wrap in Capability
  auto final_capability = make_capability(std::move(negotiated));
  ASSERT_TRUE(final_capability.experimental.has_value());
  EXPECT_FALSE(final_capability.experimental->supports_streaming);
}

// ==================== PERFORMANCE TESTS ====================

TEST_F(MCPTypeHelpersTest, PerformanceLargeMetadata) {
  auto start = std::chrono::steady_clock::now();

  auto meta = make_metadata();
  for (int i = 0; i < 10000; ++i) {
    add_metadata(meta, "key_" + std::to_string(i), i);
  }

  auto end = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  EXPECT_EQ(meta.size(), 10000u);
  // Just ensure it completes in reasonable time
  EXPECT_LT(duration.count(), 1000);  // Less than 1 second
}

TEST_F(MCPTypeHelpersTest, PerformanceObjectBuilder) {
  struct LargeObject {
    int field1, field2, field3, field4, field5;
    int field6, field7, field8, field9, field10;
    std::string str1, str2, str3, str4, str5;
  };

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < 1000; ++i) {
    auto obj = make_object<LargeObject>()
                   .set(&LargeObject::field1, int(i))
                   .set(&LargeObject::field2, int(i * 2))
                   .set(&LargeObject::field3, int(i * 3))
                   .set(&LargeObject::field4, int(i * 4))
                   .set(&LargeObject::field5, int(i * 5))
                   .set(&LargeObject::field6, int(i * 6))
                   .set(&LargeObject::field7, int(i * 7))
                   .set(&LargeObject::field8, int(i * 8))
                   .set(&LargeObject::field9, int(i * 9))
                   .set(&LargeObject::field10, int(i * 10))
                   .set(&LargeObject::str1, std::to_string(i))
                   .set(&LargeObject::str2, std::to_string(i * 2))
                   .set(&LargeObject::str3, std::to_string(i * 3))
                   .set(&LargeObject::str4, std::to_string(i * 4))
                   .set(&LargeObject::str5, std::to_string(i * 5))
                   .build();

    // Use the object to prevent optimization
    EXPECT_EQ(obj.field1, i);
  }

  auto end = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // Just ensure it completes in reasonable time
  EXPECT_LT(duration.count(), 1000);  // Less than 1 second
}