// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Extensions in capabilities. See the header.
 */

#include "mcp/protocol/extensions.h"

#include <stdexcept>

namespace mcp {
namespace protocol {
namespace extensions {

namespace {

bool isLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlnum(char c) { return isLetter(c) || isDigit(c); }

bool isLabel(const std::string& label) {
  if (label.empty() || !isLetter(label.front()) || !isAlnum(label.back())) {
    return false;
  }
  for (char c : label) {
    if (!isAlnum(c) && c != '-') {
      return false;
    }
  }
  return true;
}

bool isName(const std::string& name) {
  if (name.empty() || !isAlnum(name.front()) || !isAlnum(name.back())) {
    return false;
  }
  for (char c : name) {
    if (!isAlnum(c) && c != '-' && c != '_' && c != '.') {
      return false;
    }
  }
  return true;
}

}  // namespace

bool isValidId(const std::string& id) {
  const size_t slash = id.find('/');
  if (slash == std::string::npos || slash == 0) {
    return false;
  }
  const std::string prefix = id.substr(0, slash);
  size_t start = 0;
  while (true) {
    const size_t dot = prefix.find('.', start);
    const std::string label = prefix.substr(
        start, dot == std::string::npos ? std::string::npos : dot - start);
    if (!isLabel(label)) {
      return false;
    }
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }
  return isName(id.substr(slash + 1));
}

json::JsonValue sanitized(const json::JsonValue& extensions) {
  json::JsonValue kept = json::JsonValue::object();
  if (!extensions.isObject()) {
    return kept;
  }
  for (const auto& id : extensions.keys()) {
    if (isValidId(id) && extensions[id].isObject()) {
      kept.set(id, extensions[id]);
    }
  }
  return kept;
}

void checkConfigured(const std::map<std::string, json::JsonValue>& configured) {
  for (const auto& entry : configured) {
    if (!isValidId(entry.first)) {
      throw std::invalid_argument("'" + entry.first +
                                  "' is not a valid extension identifier");
    }
    if (!entry.second.isObject()) {
      throw std::invalid_argument("the settings of extension '" + entry.first +
                                  "' must be a JSON object");
    }
  }
}

json::JsonValue merged(
    const optional<json::JsonValue>& declared,
    const std::map<std::string, json::JsonValue>& configured) {
  json::JsonValue all = declared.has_value() ? sanitized(declared.value())
                                             : json::JsonValue::object();
  for (const auto& entry : configured) {
    if (!all.contains(entry.first) && isValidId(entry.first) &&
        entry.second.isObject()) {
      all.set(entry.first, entry.second);
    }
  }
  return all;
}

optional<json::JsonValue> settingsOf(
    const optional<json::JsonValue>& extensions, const std::string& id) {
  if (!extensions.has_value() || !extensions->isObject() ||
      !extensions->contains(id) || !(*extensions)[id].isObject() ||
      !isValidId(id)) {
    return nullopt;
  }
  return mcp::make_optional((*extensions)[id]);
}

}  // namespace extensions
}  // namespace protocol
}  // namespace mcp
