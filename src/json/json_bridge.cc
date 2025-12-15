#include "mcp/json/json_bridge.h"

#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace mcp {
namespace json {

// Forward declaration for child storage
class JsonValue;

// Implementation class that wraps nlohmann::json
class JsonValueImpl {
 public:
  nlohmann::json json_;
  // If non-null, this JsonValue views an external json node; otherwise uses
  // json_
  nlohmann::json* json_ref_ = nullptr;
  nlohmann::json* parent_ptr_ = nullptr;
  std::string parent_key_;
  size_t parent_index_ = SIZE_MAX;  // For array indices
  bool is_array_child_ = false;  // Flag to distinguish array vs object children

  // Child storage to avoid static temporary variables
  mutable std::unordered_map<std::string, std::unique_ptr<JsonValue>>
      child_objects_;
  mutable std::vector<std::unique_ptr<JsonValue>> child_arrays_;

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
    : impl_(std::make_unique<JsonValueImpl>(other.impl_->json_ref_
                                                ? *other.impl_->json_ref_
                                                : other.impl_->json_)) {}

JsonValue::JsonValue(JsonValue&& other) noexcept = default;

JsonValue& JsonValue::operator=(const JsonValue& other) {
  if (this != &other) {
    if (impl_ && impl_->json_ref_) {
      // Direct view assignment writes through to referenced node
      *impl_->json_ref_ =
          other.impl_->json_ref_ ? *other.impl_->json_ref_ : other.impl_->json_;
    } else if (impl_ && impl_->parent_ptr_) {
      if (impl_->is_array_child_) {
        (*impl_->parent_ptr_)[impl_->parent_index_] = other.impl_->json_;
        impl_->json_ = (*impl_->parent_ptr_)[impl_->parent_index_];
      } else {
        (*impl_->parent_ptr_)[impl_->parent_key_] = other.impl_->json_;
        impl_->json_ = (*impl_->parent_ptr_)[impl_->parent_key_];
      }
    } else {
      impl_ = std::make_unique<JsonValueImpl>(other.impl_->json_ref_
                                                  ? *other.impl_->json_ref_
                                                  : other.impl_->json_);
    }
  }
  return *this;
}

JsonValue& JsonValue::operator=(JsonValue&& other) noexcept {
  if (this != &other) {
    if (impl_ && impl_->json_ref_) {
      *impl_->json_ref_ = other.impl_->json_ref_
                              ? std::move(*other.impl_->json_ref_)
                              : std::move(other.impl_->json_);
    } else if (impl_ && impl_->parent_ptr_) {
      if (impl_->is_array_child_) {
        (*impl_->parent_ptr_)[impl_->parent_index_] =
            std::move(other.impl_->json_);
        impl_->json_ = (*impl_->parent_ptr_)[impl_->parent_index_];
      } else {
        (*impl_->parent_ptr_)[impl_->parent_key_] =
            std::move(other.impl_->json_);
        impl_->json_ = (*impl_->parent_ptr_)[impl_->parent_key_];
      }
    } else {
      impl_ = std::move(other.impl_);
    }
  }
  return *this;
}

JsonValue::~JsonValue() = default;

// Type checking
// Helper to access underlying json node (owned or view)
static inline const nlohmann::json& access_json_const(
    const std::unique_ptr<JsonValueImpl>& impl) {
  return impl->json_ref_ ? *impl->json_ref_ : impl->json_;
}
static inline nlohmann::json& access_json(
    std::unique_ptr<JsonValueImpl>& impl) {
  return impl->json_ref_ ? *impl->json_ref_ : impl->json_;
}
// Allow access from const methods when we need a mutable reference (for
// caching/indexing semantics
static inline nlohmann::json& access_json(
    const std::unique_ptr<JsonValueImpl>& impl) {
  return impl->json_ref_ ? *impl->json_ref_
                         : const_cast<nlohmann::json&>(impl->json_);
}

JsonType JsonValue::type() const {
  if (access_json_const(impl_).is_null())
    return JsonType::Null;
  if (access_json_const(impl_).is_boolean())
    return JsonType::Boolean;
  if (access_json_const(impl_).is_number_integer())
    return JsonType::Integer;
  if (access_json_const(impl_).is_number_float())
    return JsonType::Float;
  if (access_json_const(impl_).is_string())
    return JsonType::String;
  if (access_json_const(impl_).is_array())
    return JsonType::Array;
  if (access_json_const(impl_).is_object())
    return JsonType::Object;
  return JsonType::Null;
}

bool JsonValue::isNull() const { return access_json_const(impl_).is_null(); }
bool JsonValue::isBoolean() const {
  return access_json_const(impl_).is_boolean();
}
bool JsonValue::isInteger() const {
  return access_json_const(impl_).is_number_integer();
}
bool JsonValue::isFloat() const {
  return access_json_const(impl_).is_number_float();
}
bool JsonValue::isNumber() const {
  return access_json_const(impl_).is_number();
}
bool JsonValue::isString() const {
  return access_json_const(impl_).is_string();
}
bool JsonValue::isArray() const { return access_json_const(impl_).is_array(); }
bool JsonValue::isObject() const {
  return access_json_const(impl_).is_object();
}

bool JsonValue::empty() const {
  const auto& j = access_json_const(impl_);
  if (j.is_null())
    return true;
  if (j.is_string())
    return j.get<std::string>().empty();
  if (j.is_array() || j.is_object())
    return j.empty();
  // Numbers and booleans are not considered empty
  return false;
}

// Value getters
bool JsonValue::getBool() const {
  if (!isBoolean()) {
    throw JsonException("Value is not a boolean");
  }
  return access_json_const(impl_).get<bool>();
}

int JsonValue::getInt() const {
  if (!isInteger() && !isFloat()) {
    throw JsonException("Value is not a number");
  }
  return access_json_const(impl_).get<int>();
}

int64_t JsonValue::getInt64() const {
  if (!isInteger() && !isFloat()) {
    throw JsonException("Value is not a number");
  }
  return access_json_const(impl_).get<int64_t>();
}

double JsonValue::getFloat() const {
  if (!isNumber()) {
    throw JsonException("Value is not a number");
  }
  return access_json_const(impl_).get<double>();
}

std::string JsonValue::getString() const {
  if (!isString()) {
    throw JsonException("Value is not a string");
  }
  return access_json_const(impl_).get<std::string>();
}

// Safe getters with defaults
bool JsonValue::getBool(bool defaultValue) const {
  return isBoolean() ? access_json_const(impl_).get<bool>() : defaultValue;
}

int JsonValue::getInt(int defaultValue) const {
  return (isInteger() || isFloat()) ? access_json_const(impl_).get<int>()
                                    : defaultValue;
}

int64_t JsonValue::getInt64(int64_t defaultValue) const {
  return (isInteger() || isFloat()) ? access_json_const(impl_).get<int64_t>()
                                    : defaultValue;
}

double JsonValue::getFloat(double defaultValue) const {
  return isNumber() ? access_json_const(impl_).get<double>() : defaultValue;
}

std::string JsonValue::getString(const std::string& defaultValue) const {
  return isString() ? access_json_const(impl_).get<std::string>()
                    : defaultValue;
}

// Array operations
size_t JsonValue::size() const {
  if (!isArray() && !isObject()) {
    throw JsonException("Value is not an array or object");
  }
  return access_json_const(impl_).size();
}

JsonValue& JsonValue::operator[](size_t index) {
  if (!isArray()) {
    throw JsonException("Value is not an array");
  }
  auto& self_json = access_json(impl_);
  // Ensure vector is large enough
  if (impl_->child_arrays_.size() <= index) {
    impl_->child_arrays_.resize(index + 1);
  }

  // Get or create child
  auto& child_ptr = impl_->child_arrays_[index];
  if (!child_ptr) {
    child_ptr = std::make_unique<JsonValue>();
    // Set up parent-child relationship for assignment operations
    child_ptr->impl_->parent_ptr_ = &self_json;
    child_ptr->impl_->parent_index_ = index;
    child_ptr->impl_->is_array_child_ = true;
  }
  // Point child directly to underlying json node
  child_ptr->impl_->json_ref_ = &self_json[index];
  // Clear child's own children cache since node changed
  child_ptr->impl_->child_objects_.clear();
  child_ptr->impl_->child_arrays_.clear();

  return *child_ptr;
}

const JsonValue& JsonValue::operator[](size_t index) const {
  if (!isArray()) {
    throw JsonException("Value is not an array");
  }
  auto& self_json = access_json(impl_);
  // Ensure vector is large enough (mutable for const correctness)
  if (impl_->child_arrays_.size() <= index) {
    impl_->child_arrays_.resize(index + 1);
  }

  // Get or create child
  auto& child_ptr = impl_->child_arrays_[index];
  if (!child_ptr) {
    child_ptr = std::make_unique<JsonValue>();
    // Set up parent-child relationship for const version
    child_ptr->impl_->parent_ptr_ = const_cast<nlohmann::json*>(&self_json);
    child_ptr->impl_->parent_index_ = index;
    child_ptr->impl_->is_array_child_ = true;
  }
  // Point child directly to underlying json node
  child_ptr->impl_->json_ref_ = &self_json[index];
  // Clear child's own children cache since node changed
  child_ptr->impl_->child_objects_.clear();
  child_ptr->impl_->child_arrays_.clear();

  return *child_ptr;
}

void JsonValue::push_back(const JsonValue& value) {
  if (!isArray()) {
    throw JsonException("Value is not an array");
  }
  access_json(impl_).push_back(value.impl_->json_ref_ ? *value.impl_->json_ref_
                                                      : value.impl_->json_);
}

void JsonValue::push_back(JsonValue&& value) {
  if (!isArray()) {
    throw JsonException("Value is not an array");
  }
  access_json(impl_).push_back(value.impl_->json_ref_
                                   ? std::move(*value.impl_->json_ref_)
                                   : std::move(value.impl_->json_));
}

// Object operations
bool JsonValue::contains(const std::string& key) const {
  if (!isObject()) {
    return false;
  }
  return access_json_const(impl_).contains(key);
}

JsonValue& JsonValue::operator[](const std::string& key) {
  if (!isObject()) {
    // Convert to object if null
    if (isNull()) {
      access_json(impl_) = nlohmann::json::object();
    } else {
      throw JsonException("Value is not an object");
    }
  }
  auto& self_json = access_json(impl_);
  // Get or create child object in our instance storage
  auto& child_ptr = impl_->child_objects_[key];
  if (!child_ptr) {
    child_ptr = std::make_unique<JsonValue>();
    // Set up parent-child relationship for assignment operations
    child_ptr->impl_->parent_ptr_ = &self_json;
    child_ptr->impl_->parent_key_ = key;
    child_ptr->impl_->is_array_child_ = false;
  }
  // Point child directly to underlying json node (creates null if missing)
  child_ptr->impl_->json_ref_ = &self_json[key];
  // Clear child's own children cache since node changed
  child_ptr->impl_->child_objects_.clear();
  child_ptr->impl_->child_arrays_.clear();

  return *child_ptr;
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  auto& self_json = access_json(impl_);
  // Get or create child object in our instance storage (mutable for const
  // correctness)
  auto& child_ptr = impl_->child_objects_[key];
  if (!child_ptr) {
    child_ptr = std::make_unique<JsonValue>();
    // Set up parent-child relationship (const version doesn't need assignment
    // support)
    child_ptr->impl_->parent_ptr_ = const_cast<nlohmann::json*>(&self_json);
    child_ptr->impl_->parent_key_ = key;
    child_ptr->impl_->is_array_child_ = false;
  }
  // Point child directly to underlying json node
  child_ptr->impl_->json_ref_ = &self_json[key];
  // Clear child's own children cache since node changed
  child_ptr->impl_->child_objects_.clear();
  child_ptr->impl_->child_arrays_.clear();

  return *child_ptr;
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
  impl_->json_[key] = access_json_const(value.impl_);
}

std::vector<std::string> JsonValue::keys() const {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  std::vector<std::string> result;
  for (auto& kv : access_json_const(impl_).items()) {
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
  nlohmann::json& j =
      impl_ptr->json_ref_ ? *impl_ptr->json_ref_ : impl_ptr->json_;
  return ObjectIterator(new ObjectIteratorImpl(j, false), false);
}

JsonValue::ObjectIterator JsonValue::end() const {
  if (!isObject()) {
    throw JsonException("Value is not an object");
  }
  auto* impl_ptr = const_cast<JsonValueImpl*>(impl_.get());
  nlohmann::json& j =
      impl_ptr->json_ref_ ? *impl_ptr->json_ref_ : impl_ptr->json_;
  return ObjectIterator(new ObjectIteratorImpl(j, true), true);
}

// Conversion
std::string JsonValue::toString(bool pretty) const {
  const auto& j = access_json_const(impl_);
  if (pretty) {
    return j.dump(2);
  }
  return j.dump();
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
