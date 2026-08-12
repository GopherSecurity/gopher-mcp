/**
 * Which tool arguments travel in headers. See the header for why the
 * constraints are strict.
 */

#include "mcp/protocol/designated_params.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "mcp/protocol/modern_era.h"

namespace mcp {
namespace protocol {
namespace modern {

namespace {

const char kAnnotation[] = "x-mcp-header";

VoidResult refuse(const std::string& tool, const std::string& why) {
  return makeVoidError(Error(jsonrpc::INVALID_PARAMS,
                             "tool '" + tool + "' cannot be served: " + why));
}

/** RFC 9110's token characters, which is all a header name may hold. */
bool isTokenChar(char c) {
  if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
    return true;
  }
  switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
      return true;
    default:
      return false;
  }
}

std::string lowered(const std::string& text) {
  std::string out = text;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

/** Whether this schema node says its instance is one of the three. */
bool hasCarryableType(const json::JsonValue& schema) {
  if (!schema.isObject() || !schema.contains("type")) {
    return false;
  }
  const auto& type = schema["type"];
  if (!type.isString()) {
    // A union of types has no single header form, whatever is in it.
    return false;
  }
  const std::string named = type.getString();
  // `number` is excluded on purpose: a value written 42 in one place and
  // 42.0 in another is the same number and a different string, and the
  // two ends would disagree about which they were carrying.
  return named == "string" || named == "integer" || named == "boolean";
}

/** The keys a schema can nest under that no fixed path can traverse. */
const char* const kUnreachableKeys[] = {"items", "oneOf", "anyOf",
                                        "allOf", "not",   "if",
                                        "then",  "else",  "prefixItems"};

bool mentionsAnnotation(const json::JsonValue& node) {
  if (node.isObject()) {
    if (node.contains(kAnnotation)) {
      return true;
    }
    for (const auto& key : node.keys()) {
      if (mentionsAnnotation(node[key])) {
        return true;
      }
    }
    return false;
  }
  if (node.isArray()) {
    for (size_t i = 0; i < node.size(); ++i) {
      if (mentionsAnnotation(node[i])) {
        return true;
      }
    }
  }
  return false;
}

/**
 * Walk the one chain a value can be found by, collecting what it
 * designates and refusing anything designated off it.
 */
VoidResult walk(const std::string& tool,
                const json::JsonValue& schema,
                std::vector<std::string>& path,
                std::set<std::string>& taken,
                std::vector<DesignatedParam>* out) {
  if (!schema.isObject()) {
    return makeVoidSuccess();
  }

  // Anywhere a fixed chain of property names cannot go. An annotation
  // down there could not be found again at call time, so its presence
  // invalidates the definition rather than being ignored.
  for (const char* key : kUnreachableKeys) {
    if (schema.contains(key) && mentionsAnnotation(schema[key])) {
      return refuse(tool, std::string("a parameter is designated under '") +
                              key +
                              "', where no fixed path can reach it at call "
                              "time");
    }
  }
  if (schema.contains("$ref")) {
    return refuse(tool,
                  "its schema designates parameters behind a $ref, which "
                  "cannot be followed without resolving the schema");
  }

  if (!schema.contains("properties") || !schema["properties"].isObject()) {
    return makeVoidSuccess();
  }

  const auto& properties = schema["properties"];
  for (const auto& name : properties.keys()) {
    const auto& property = properties[name];
    if (!property.isObject()) {
      continue;
    }

    path.push_back(name);

    if (property.contains(kAnnotation)) {
      const auto& annotation = property[kAnnotation];
      if (!annotation.isString()) {
        return refuse(tool, "the designation on '" + name + "' is not a name");
      }
      const std::string header = annotation.getString();
      if (header.empty()) {
        return refuse(tool, "the designation on '" + name + "' is empty");
      }
      for (const char c : header) {
        if (!isTokenChar(c)) {
          return refuse(tool, "the designation on '" + name + "' is '" +
                                  header +
                                  "', which is not a usable header name");
        }
      }
      if (!taken.insert(lowered(header)).second) {
        return refuse(tool, "two parameters are designated '" + header +
                                "', and header names do not distinguish case");
      }
      if (!hasCarryableType(property)) {
        return refuse(tool, "'" + name +
                                "' is designated but has no single form a "
                                "header can carry; only a string, an integer "
                                "or a boolean does");
      }

      DesignatedParam param;
      param.header_name = header;
      param.path = path;
      out->push_back(param);
    }

    // Nested objects are reachable as long as every step is a property.
    auto nested = walk(tool, property, path, taken, out);
    if (!holds_alternative<std::nullptr_t>(nested)) {
      return nested;
    }

    path.pop_back();
  }

  return makeVoidSuccess();
}

}  // namespace

VoidResult designatedParams(const Tool& tool,
                            std::vector<DesignatedParam>* out) {
  if (out == nullptr) {
    return makeVoidError(Error(jsonrpc::INTERNAL_ERROR, "nowhere to answer"));
  }
  out->clear();
  if (!tool.inputSchema.has_value()) {
    return makeVoidSuccess();
  }

  std::vector<std::string> path;
  std::set<std::string> taken;
  auto walked = walk(tool.name, tool.inputSchema.value(), path, taken, out);
  if (!holds_alternative<std::nullptr_t>(walked)) {
    out->clear();
    return walked;
  }
  return makeVoidSuccess();
}

bool valueAtPath(const json::JsonValue& arguments,
                 const std::vector<std::string>& path,
                 json::JsonValue* value) {
  if (value == nullptr || path.empty()) {
    return false;
  }

  json::JsonValue node = arguments;
  for (const auto& step : path) {
    if (!node.isObject() || !node.contains(step)) {
      return false;
    }
    node = node[step];
  }
  // A null is an argument that was not given, which is the same as one
  // that was never there: the client omits the header either way.
  if (node.isNull()) {
    return false;
  }
  *value = node;
  return true;
}

bool isExactlyCarryableInteger(int64_t value) {
  // 2^53 - 1 either way: the range a double holds without rounding, and
  // therefore the range both ends can agree on.
  const int64_t limit = 9007199254740991LL;
  return value <= limit && value >= -limit;
}

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
