#include <iostream>
#include <cassert>
#include "mcp/json_serialization.h"
#include "mcp/types.h"

using namespace mcp;
using namespace mcp::json;

void testTemplateSerialization() {
    // Test template-based serialization
    {
        TextContent content;
        content.type = "text";
        content.text = "Hello World";
        
        // Old way - specific function name
        JsonValue json1 = JsonSerializer::serialize(content);
        
        // New way - template-based
        JsonValue json2 = JsonSerializer::serialize<TextContent>(content);
        
        assert(json1.toString() == json2.toString());
        std::cout << "✓ TextContent serialization works\n";
    }
    
    // Test vector serialization
    {
        std::vector<TextContent> contents;
        TextContent c1, c2;
        c1.type = "text";
        c1.text = "Item 1";
        c2.type = "text";
        c2.text = "Item 2";
        contents.push_back(c1);
        contents.push_back(c2);
        
        // Template-based vector serialization
        JsonValue jsonArray = JsonSerializer::serializeVector(contents);
        
        assert(jsonArray.isArray());
        assert(jsonArray.size() == 2);
        assert(jsonArray[0]["text"].getString() == "Item 1");
        std::cout << "✓ Vector serialization works\n";
    }
    
    // Test optional serialization
    {
        Tool tool;
        tool.name = "test-tool";
        tool.description = make_optional(std::string("A test tool"));
        
        JsonObjectBuilder builder;
        builder.add("name", tool.name);
        JsonSerializer::serializeOptional(builder, "description", tool.description);
        JsonValue json = builder.build();
        
        assert(json.contains("description"));
        assert(json["description"].getString() == "A test tool");
        std::cout << "✓ Optional serialization works\n";
    }
    
    // Test round-trip: serialize then deserialize
    {
        Error error;
        error.code = -32601;
        error.message = "Method not found";
        
        // Serialize using template
        JsonValue json = JsonSerializer::serialize<Error>(error);
        
        // Deserialize using template
        Error error2 = JsonDeserializer::deserialize<Error>(json);
        
        assert(error.code == error2.code);
        assert(error.message == error2.message);
        std::cout << "✓ Round-trip serialization/deserialization works\n";
    }
    
    std::cout << "\n✅ All template serialization tests passed!\n\n";
}

void testTemplateDeserialization() {
    // Example 1: Before - specific function for each type
    {
        JsonValue json = JsonObjectBuilder()
            .add("type", "text")
            .add("text", "Hello World")
            .build();
        
        // Old way - specific function name
        TextContent content1 = JsonDeserializer::deserializeTextContent(json);
        
        // New way - template-based
        TextContent content2 = JsonDeserializer::deserialize<TextContent>(json);
        
        assert(content1.text == content2.text);
        std::cout << "✓ TextContent deserialization works\n";
    }
    
    // Example 2: Simplified vector deserialization
    {
        JsonArrayBuilder builder;
        builder.add(JsonObjectBuilder()
            .add("type", "text")
            .add("text", "Item 1")
            .build());
        builder.add(JsonObjectBuilder()
            .add("type", "text")
            .add("text", "Item 2")
            .build());
        JsonValue jsonArray = builder.build();
        
        // Old way - pass function pointer
        std::vector<TextContent> contents1 = JsonDeserializer::deserializeVector(
            jsonArray, &JsonDeserializer::deserializeTextContent);
        
        // New way - template-based
        std::vector<TextContent> contents2 = JsonDeserializer::deserializeVector<TextContent>(jsonArray);
        
        assert(contents1.size() == contents2.size());
        assert(contents1[0].text == contents2[0].text);
        std::cout << "✓ Vector deserialization works\n";
    }
    
    // Example 3: Optional deserialization
    {
        JsonValue json = JsonObjectBuilder()
            .add("name", "test-tool")
            .add("description", "A test tool")
            .add("inputSchema", JsonObjectBuilder()
                .add("type", "object")
                .build())
            .build();
        
        // New template-based optional deserialization
        optional<std::string> description = JsonDeserializer::deserializeOptional<std::string>(json, "description");
        optional<std::string> missing = JsonDeserializer::deserializeOptional<std::string>(json, "missing_field");
        
        assert(description.has_value());
        assert(description.value() == "A test tool");
        assert(!missing.has_value());
        std::cout << "✓ Optional deserialization works\n";
    }
    
    // Example 4: Complex nested types
    {
        JsonValue json = JsonObjectBuilder()
            .add("name", "test-prompt")
            .add("description", "Test prompt")
            .add("arguments", JsonArrayBuilder()
                .add(JsonObjectBuilder()
                    .add("name", "arg1")
                    .add("description", "First argument")
                    .add("required", true)
                    .build())
                .build())
            .build();
        
        // Both ways work, but template is more consistent
        Prompt prompt1 = JsonDeserializer::deserializePrompt(json);
        Prompt prompt2 = JsonDeserializer::deserialize<Prompt>(json);
        
        assert(prompt1.name == prompt2.name);
        std::cout << "✓ Complex type deserialization works\n";
    }
    
    // Example 5: Generic code that works with any deserializable type
    {
        std::string jsonStr = R"({
            "code": -32601,
            "message": "Method not found"
        })";
        
        JsonValue json = JsonValue::parse(jsonStr);
        Error error = JsonDeserializer::deserialize<Error>(json);
        assert(error.code == -32601);
        assert(error.message == "Method not found");
        std::cout << "✓ Generic template function works\n";
    }
    
    std::cout << "\n✅ All template deserialization tests passed!\n";
}

int main() {
    try {
        testTemplateSerialization();
        testTemplateDeserialization();
        
        std::cout << "\n📝 Benefits of template-based approach:\n";
        std::cout << "1. Consistent API - serialize<T>() and deserialize<T>()\n";
        std::cout << "2. Better for generic programming and template metaprogramming\n";
        std::cout << "3. Easier to extend with new types - just add trait specialization\n";
        std::cout << "4. Works seamlessly with containers (vector, optional, etc.)\n";
        std::cout << "5. Type deduction in template contexts\n";
        std::cout << "6. Backward compatible - old functions still work\n";
        std::cout << "7. Symmetric API for both serialization and deserialization\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}