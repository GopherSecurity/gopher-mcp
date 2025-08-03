# C++14 Alternatives for Class Template Argument Deduction (CTAD)

This document describes the C++14-compatible alternatives we've implemented to replace C++17's class template argument deduction for the MCP C++ SDK.

## Overview

Since C++17's CTAD is not available in C++14, we've implemented factory functions and helper utilities that provide similar ergonomics while maintaining type safety.

## Key Components

### 1. Optional Type Helpers (`type_helpers.h`)

- `opt(value)` - Creates an optional with automatic type deduction
- `none<T>()` - Creates an empty optional of type T
- `make_optional(value)` - Standard factory function

### 2. Variant Type Helpers

- `make_request_id(value)` - Creates RequestId variant from string or int
- `make_progress_token(value)` - Creates ProgressToken variant
- `make_content_block()` family - Creates ContentBlock variants

### 3. Discriminated Union Helpers

#### Type-based Discriminator
```cpp
using ContentDiscriminator = TypeDiscriminator<TextContent, ImageContent, ResourceContent>;
auto content = ContentDiscriminator::create(TextContent("Hello"));
if (ContentDiscriminator::is_type<TextContent>(content)) {
    auto* text = ContentDiscriminator::get_if<TextContent>(content);
}
```

#### Method-based Discriminator
```cpp
auto notification = make_method_notification("initialize", InitializeParams{1});
if (notification.has_method("initialize")) {
    auto* params = notification.get_if<InitializeParams>();
}
```

### 4. Enum Helpers

String literal enums with conversion functions:
```cpp
enums::Role::to_string(enums::Role::USER) // "user"
enums::Role::from_string("user") // optional<Role::Value>
```

### 5. Metadata Pattern

Extensible key-value storage for unknown fields:
```cpp
auto meta = make_metadata();
add_metadata(meta, "key", "value");
add_metadata(meta, "number", 42);
```

### 6. Builder Pattern

For complex type construction:
```cpp
auto tool = build_tool("calculator")
    .description("Math calculator")
    .parameter("expression", "string", true)
    .build();

auto resource = build_resource("file:///doc.pdf", "document.pdf")
    .description("Important document")
    .mimeType("application/pdf")
    .build();
```

### 7. Result/Error Pattern

Type-safe error handling:
```cpp
Result<int> result = make_result(42);
if (is_success<int>(result)) {
    auto* value = get_value<int>(result);
}

Result<int> error = make_error_result<int>(Error(404, "Not found"));
```

### 8. Factory Functions

For all MCP types:
- `make_text_content(text)`
- `make_image_content(data, mimeType)`
- `make_resource_content(resource)`
- `make_user_message(text)`
- `make_assistant_message(text)`
- `make_request(id, method, params)`
- `make_response(id, result)`
- `make_error_response(id, error)`

### 9. Object Builder

Generic builder for structs:
```cpp
auto person = make_object<Person>()
    .set(&Person::name, "John")
    .set(&Person::age, 30)
    .set_optional(&Person::email, "john@example.com")
    .build();
```

### 10. Match Helper

Pattern matching for variants:
```cpp
auto result = match(variant_value,
    [](int i) { return i * 2; },
    [](double d) { return int(d); },
    [](const string& s) { return s.length(); }
);
```

## Benefits

1. **Type Safety**: All factory functions maintain full type safety
2. **Ergonomics**: Similar ease of use to C++17's CTAD
3. **Performance**: Zero-cost abstractions with inline functions
4. **Extensibility**: Easy to add new types and factories
5. **Compatibility**: Works with C++14 compilers

## Usage Example

```cpp
// Create an MCP request
auto request = make_request(
    make_request_id("req-123"),
    "tools/call",
    make_tool_call("calculator", metadata)
);

// Create a response
auto response = make_response(
    request.id,
    std::vector<ContentBlock>{
        make_text_content("Result: 42")
    }
);

// Pattern match on content
match(response.content,
    [](const TextContent& text) { 
        std::cout << "Text: " << text.text << std::endl;
    },
    [](const ImageContent& image) {
        std::cout << "Image: " << image.mimeType << std::endl;
    },
    [](const ResourceContent& resource) {
        std::cout << "Resource: " << resource.resource.uri << std::endl;
    }
);
```

## Implementation Notes

- All factory functions use perfect forwarding to minimize copies
- Builder patterns use method chaining for fluent interfaces
- Discriminated unions use visitor pattern for type-safe access
- Metadata uses recursive variant for nested structures
- String literal helpers enable compile-time string processing

This approach provides a clean, type-safe API that closely matches the TypeScript MCP schema while working within C++14's constraints.