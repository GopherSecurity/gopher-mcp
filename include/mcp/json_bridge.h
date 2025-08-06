#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include "mcp/compat.h"


namespace mcp {
namespace json {

// Forward declaration of implementation
class JsonValueImpl;

// JSON types enum
enum class JsonType {
  Null,
  Boolean,
  Integer,
  Float,
  String,
  Array,
  Object
};

// JSON exception
class JsonException : public std::runtime_error {
public:
  explicit JsonException(const std::string& msg) : std::runtime_error(msg) {}
};

// Main JSON value class - provides abstraction over underlying JSON library
class JsonValue {
public:
  // Constructors
  JsonValue();  // Creates null
  JsonValue(std::nullptr_t);
  JsonValue(bool value);
  JsonValue(int value);
  JsonValue(int64_t value);
  JsonValue(double value);
  JsonValue(const std::string& value);
  JsonValue(const char* value);
  
  // Copy and move constructors
  JsonValue(const JsonValue& other);
  JsonValue(JsonValue&& other) noexcept;
  
  // Assignment operators
  JsonValue& operator=(const JsonValue& other);
  JsonValue& operator=(JsonValue&& other) noexcept;
  
  // Destructor
  ~JsonValue();
  
  // Type checking
  JsonType type() const;
  bool isNull() const;
  bool isBoolean() const;
  bool isInteger() const;
  bool isFloat() const;
  bool isNumber() const;  // Integer or Float
  bool isString() const;
  bool isArray() const;
  bool isObject() const;
  
  // Value getters (throw if wrong type)
  bool getBool() const;
  int getInt() const;
  int64_t getInt64() const;
  double getFloat() const;
  std::string getString() const;
  
  // Safe value getters with defaults
  bool getBool(bool defaultValue) const;
  int getInt(int defaultValue) const;
  int64_t getInt64(int64_t defaultValue) const;
  double getFloat(double defaultValue) const;
  std::string getString(const std::string& defaultValue) const;
  
  // Array operations
  size_t size() const;  // Array or object size
  JsonValue& operator[](size_t index);  // Array access
  const JsonValue& operator[](size_t index) const;
  void push_back(const JsonValue& value);  // Add to array
  void push_back(JsonValue&& value);
  
  // Object operations
  bool contains(const std::string& key) const;
  JsonValue& operator[](const std::string& key);  // Object access/insert
  const JsonValue& operator[](const std::string& key) const;
  JsonValue& at(const std::string& key);  // Object access (throws if not found)
  const JsonValue& at(const std::string& key) const;
  void erase(const std::string& key);
  std::vector<std::string> keys() const;
  
  // Direct key-value setting for objects
  void set(const std::string& key, const JsonValue& value);
  
  // Iteration support for objects
  class ObjectIterator {
  public:
    ObjectIterator(void* impl, bool is_end);
    ~ObjectIterator();
    
    ObjectIterator& operator++();
    bool operator!=(const ObjectIterator& other) const;
    std::pair<std::string, JsonValue> operator*() const;
    
  private:
    void* impl_;  // Hide implementation
  };
  
  ObjectIterator begin() const;
  ObjectIterator end() const;
  
  // Conversion
  std::string toString(bool pretty = false) const;
  
  // Static factory methods
  static JsonValue null();
  static JsonValue array();
  static JsonValue object();
  static JsonValue parse(const std::string& json_str);
  
  // Friend function for implementation access
  friend class JsonValueImpl;
  void* getImpl() const { return impl_.get(); }
  
private:
  std::unique_ptr<JsonValueImpl> impl_;
};

// Convenience builders
class JsonObjectBuilder {
public:
  JsonObjectBuilder() : value_(JsonValue::object()) {}
  
  JsonObjectBuilder& add(const std::string& key, const JsonValue& val) {
    value_.set(key, val);
    return *this;
  }
  
  JsonObjectBuilder& add(const std::string& key, bool val) {
    value_.set(key, JsonValue(val));
    return *this;
  }
  
  JsonObjectBuilder& add(const std::string& key, int val) {
    value_.set(key, JsonValue(val));
    return *this;
  }
  
  JsonObjectBuilder& add(const std::string& key, double val) {
    value_.set(key, JsonValue(val));
    return *this;
  }
  
  JsonObjectBuilder& add(const std::string& key, const std::string& val) {
    value_.set(key, JsonValue(val));
    return *this;
  }
  
  JsonObjectBuilder& add(const std::string& key, const char* val) {
    value_.set(key, JsonValue(val));
    return *this;
  }
  
  JsonObjectBuilder& addNull(const std::string& key) {
    value_.set(key, JsonValue::null());
    return *this;
  }
  
  JsonValue build() const { return value_; }
  
private:
  JsonValue value_;
};

class JsonArrayBuilder {
public:
  JsonArrayBuilder() : value_(JsonValue::array()) {}
  
  JsonArrayBuilder& add(const JsonValue& val) {
    value_.push_back(val);
    return *this;
  }
  
  JsonArrayBuilder& add(bool val) {
    value_.push_back(JsonValue(val));
    return *this;
  }
  
  JsonArrayBuilder& add(int val) {
    value_.push_back(JsonValue(val));
    return *this;
  }
  
  JsonArrayBuilder& add(double val) {
    value_.push_back(JsonValue(val));
    return *this;
  }
  
  JsonArrayBuilder& add(const std::string& val) {
    value_.push_back(JsonValue(val));
    return *this;
  }
  
  JsonArrayBuilder& add(const char* val) {
    value_.push_back(JsonValue(val));
    return *this;
  }
  
  JsonArrayBuilder& addNull() {
    value_.push_back(JsonValue::null());
    return *this;
  }
  
  JsonValue build() const { return value_; }
  
private:
  JsonValue value_;
};

}  // namespace json
}  // namespace mcp

// Internal helper - forward declaration moved to implementation