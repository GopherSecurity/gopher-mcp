#include "mcp/json/json_bridge.h"

#include <sstream>

#include <nlohmann/json.hpp>

namespace mcp {
namespace json {

// Implementation class that wraps nlohmann::json
class JsonValueImpl {
 public:
  nlohmann::json json_;

  JsonValueImpl() : json_(nullptr) {}
  explicit JsonValueImpl(const nlohmann::json& j) : json_(j) {}
  explicit JsonValueImpl(nlohmann::json&& j) : json_(std::move(j)) {}
};

// JsonValue constructors
JsonValue::JsonValue() : impl_(std::make_unique<JsonValueImpl>()) {}

JsonValue::JsonValue(std::nullptr_t)
    : impl_(std::make_unique<JsonValueImpl>()) {
  impl_->json_ = nullptr;
}

JsonValue::JsonValue(bool value) : impl_(std::make_unique<JsonValueImpl>()) {
  impl_->json_ = value;
}

JsonValue::JsonValue(int value) : impl_(std::make_unique<JsonValueImpl>()) {
  impl_->json_ = value;
}

JsonValue::JsonValue(int64_t value) : impl_(std::make_unique<JsonValueImpl>()) {
  impl_->json_ = value;
}

JsonValue::JsonValue(double value) : impl_(std::make_unique<JsonValueImpl>()) {
  impl_->json_ = value;
}

JsonValue::JsonValue(const std::string& value)
    : impl_(std::make_unique<JsonValueImpl>()) {
  impl_->json_ = value;
}

JsonValue::JsonValue(const char* value)
    : impl_(std::make_unique<JsonValueImpl>()) {
  impl_->json_ = std::string(value);
}

JsonValue::JsonValue(const JsonValue& other)
    : impl_(std::make_unique<JsonValueImpl>(other.impl_->json_)) {}

JsonValue::JsonValue(JsonValue&& other) noexcept = default;

JsonValue& JsonValue::operator=(const JsonValue& other) {
  if (this != &other) {
    impl_ = std::make_unique<JsonValueImpl>(other.impl_->json_);
  }
  return *this;
}

JsonValue& JsonValue::operator=(JsonValue&& other) noexcept = default;

JsonValue::~JsonValue() = default;

// Type checking
JsonType JsonValue::type() const {
  if (impl_->json_.is_null())
    return JsonType::Null;
  if (impl_->json_.is_boolean())
    return JsonType::Boolean;
  if (impl_->json_.is_number_integer())
    return JsonType::Integer;
  if (impl_->json_.is_number_float())
    return JsonType::Float;
  if (impl_->json_.is_string())
    return JsonType::String;
  if (impl_->json_.is_array())
    return JsonType::Array;
  if (impl_->json_.is_object())
    return JsonType::Object;
  return JsonType::Null;
}

bool JsonValue::isNull() const { return impl_->json_.is_null(); }
bool JsonValue::isBoolean() const { return impl_->json_.is_boolean(); }
bool JsonValue::isInteger() const { return impl_->json_.is_number_integer(); }
bool JsonValue::isFloat() const { return impl_->json_.is_number_float(); }
bool JsonValue::isNumber() const { return impl_->json_.is_number(); }
bool JsonValue::isString() const { return impl_->json_.is_string(); }
bool JsonValue::isArray() const { return impl_->json_.is_array(); }
bool JsonValue::isObject() const { return impl_->json_.is_object(); }

// Value getters
bool JsonValue::getBool() const {
  if (!isBoolean()) {
    throw JsonException("Value is not a boolean");
  }
  return impl_->json_.get<bool>();
}

int JsonValue::getInt() const {
  if (!isInteger() && !isFloat()) {
    throw JsonException("Value is not a number");
  }
  return impl_->json_.get<int>();
}

int64_t JsonValue::getInt64() const {
  if (!isInteger() && !isFloat()) {
    throw JsonException("Value is not a number");
  }
  return impl_->json_.get<int64_t>();
}

double JsonValue::getFloat() const {
  if (!isNumber()) {
    throw JsonException("Value is not a number");
  }
  return impl_->json_.get<double>();
}

std::string JsonValue::getString() const {
  if (!isString()) {
    throw JsonException("Value is not a string");
  }
  return impl_->json_.get<std::string>();
}

// Safe getters with defaults
bool JsonValue::getBool(bool defaultValue) const {
  return isBoolean() ? impl_->json_.get<bool>() : defaultValue;
}

int JsonValue::getInt(int defaultValue) const {
  return (isInteger() || isFloat()) ? impl_->json_.get<int>() : defaultValue;
}

int64_t JsonValue::getInt64(int64_t defaultValue) const {
  return (isInteger() || isFloat()) ? impl_->json_.get<int64_t>()
                                    : defaultValue;
}

double JsonValue::getFloat(double defaultValue) const {
  return isNumber() ? impl_->json_.get<double>() : defaultValue;
}

std::string JsonValue::getString(const std::string& defaultValue) const {
  return isString() ? impl_->json_.get<std::string>() : defaultValue;
}

// Array operations
size_t JsonValue::size() const {
  if (!isArray() && !isObject()) {
    throw JsonException("Value is not an array or object");
  }
  return impl_->json_.size();
}

JsonValue& JsonValue::operator[](size_t index) {
  if (!isArray()) {
    throw JsonException("Value is not an array");
  }
  static JsonValue temp;
  temp.impl_->json_ = impl_->json_[index];
  return temp;
}

const JsonValue& JsonValue::operator[](size_t index) const {
  if (!isArray()) {
    throw JsonException("Value is not an array");
  }
  static JsonValue temp;
  temp.impl_->json_ = impl_->json_[index];
  return temp;
}

void JsonValue::push_back(const JsonValue& value) {
  if (!isArray()) {
    throw JsonException("Value is not an array");
  }
  impl_->json_.push_back(value.impl_->json_);
}

void JsonValue::push_back(JsonValue&& value) {
  if (!isArray()) {
    throw JsonException("Value is not an array");
  }
  impl_->json_.push_back(std::move(value.impl_->json_));
}

// Object operations
bool JsonValue::contains(const std::string& key) const {
  if (!isObject()) {
    return false;
  }
  return impl_->json_.contains(key);
}

JsonValue& JsonValue::operator[](const std::string& key) {
  if (!isObject()) {
    // Convert to object if null
    if (isNull()) {
      impl_->json_ = nlohmann::json::object();
    } else {
      throw JsonException("Value is not an object");
    }
  }

  // Simple approach: when a value is assigned through operator[],
  // we need to handle it in the assignment operator
  // For now, let's use a different approach in JsonObjectBuilder

  static thread_local std::map<std::string, JsonValue> temp_map;
  if (temp_map.find(key) == temp_map.end() ||
      &temp_map[key].impl_->json_ != &impl_->json_[key]) {
    temp_map[key] = JsonValue();
    temp_map[key].impl_->json_ = impl_->json_[key];
  }
  return temp_map[key];
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  static thread_local std::map<std::string, JsonValue> temp_map;
  if (temp_map.find(key) == temp_map.end() ||
      &temp_map[key].impl_->json_ != &impl_->json_[key]) {
    temp_map[key] = JsonValue();
    temp_map[key].impl_->json_ = impl_->json_[key];
  }
  return temp_map[key];
}

JsonValue& JsonValue::at(const std::string& key) {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  if (!contains(key)) {
    throw JsonException("Key not found: " + key);
  }
  return (*this)[key];
}

const JsonValue& JsonValue::at(const std::string& key) const {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  if (!contains(key)) {
    throw JsonException("Key not found: " + key);
  }
  return (*this)[key];
}

void JsonValue::erase(const std::string& key) {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  impl_->json_.erase(key);
}

void JsonValue::set(const std::string& key, const JsonValue& value) {
  if (!isObject()) {
    // Convert to object if null
    if (isNull()) {
      impl_->json_ = nlohmann::json::object();
    } else {
      throw JsonException("Value is not an object");
    }
  }
  impl_->json_[key] = value.impl_->json_;
}

std::vector<std::string> JsonValue::keys() const {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  std::vector<std::string> result;
  for (auto& kv : impl_->json_.items()) {
    result.push_back(kv.key());
  }
  return result;
}

// ObjectIterator implementation
class ObjectIteratorImpl {
 public:
  nlohmann::json::iterator iter_;
  nlohmann::json::iterator end_;

  ObjectIteratorImpl(nlohmann::json& j, bool is_end)
      : iter_(is_end ? j.end() : j.begin()), end_(j.end()) {}
};

JsonValue::ObjectIterator::ObjectIterator(void* impl, bool is_end)
    : impl_(impl) {
  (void)is_end;  // Suppress unused parameter warning
}

JsonValue::ObjectIterator::~ObjectIterator() {
  delete static_cast<ObjectIteratorImpl*>(impl_);
}

JsonValue::ObjectIterator& JsonValue::ObjectIterator::operator++() {
  auto* impl = static_cast<ObjectIteratorImpl*>(impl_);
  ++impl->iter_;
  return *this;
}

bool JsonValue::ObjectIterator::operator!=(const ObjectIterator& other) const {
  auto* impl = static_cast<ObjectIteratorImpl*>(impl_);
  auto* other_impl = static_cast<ObjectIteratorImpl*>(other.impl_);
  return impl->iter_ != other_impl->iter_;
}

std::pair<std::string, JsonValue> JsonValue::ObjectIterator::operator*() const {
  auto* impl = static_cast<ObjectIteratorImpl*>(impl_);
  JsonValue val;
  val.impl_->json_ = impl->iter_.value();
  return {impl->iter_.key(), val};
}

JsonValue::ObjectIterator JsonValue::begin() const {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  auto* impl_ptr = const_cast<JsonValueImpl*>(impl_.get());
  return ObjectIterator(new ObjectIteratorImpl(impl_ptr->json_, false), false);
}

JsonValue::ObjectIterator JsonValue::end() const {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  auto* impl_ptr = const_cast<JsonValueImpl*>(impl_.get());
  return ObjectIterator(new ObjectIteratorImpl(impl_ptr->json_, true), true);
}

// Conversion
std::string JsonValue::toString(bool pretty) const {
  if (pretty) {
    return impl_->json_.dump(2);
  }
  return impl_->json_.dump();
}

// Static factory methods
JsonValue JsonValue::null() { return JsonValue(nullptr); }

JsonValue JsonValue::array() {
  JsonValue val;
  val.impl_->json_ = nlohmann::json::array();
  return val;
}

JsonValue JsonValue::object() {
  JsonValue val;
  val.impl_->json_ = nlohmann::json::object();
  return val;
}

JsonValue JsonValue::parse(const std::string& json_str) {
  try {
    JsonValue val;
    val.impl_->json_ = nlohmann::json::parse(json_str);
    return val;
  } catch (const nlohmann::json::parse_error& e) {
    throw JsonException("Parse error: " + std::string(e.what()));
  }
}

}  // namespace json
}  // namespace mcp