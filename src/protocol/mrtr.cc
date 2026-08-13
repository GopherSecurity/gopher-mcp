/**
 * Asking without sending a request. See the header for why.
 */

#include "mcp/protocol/mrtr.h"

#include <algorithm>

namespace mcp {
namespace protocol {
namespace modern {

json::JsonValue renderInputRequired(const NeedsInput& needed) {
  json::JsonValue result = json::JsonValue::object();
  result.set(kResultTypeField, json::JsonValue(kResultTypeInputRequired));

  if (!needed.requests.empty()) {
    json::JsonValue requests = json::JsonValue::object();
    for (const auto& entry : needed.requests) {
      json::JsonValue one = json::JsonValue::object();
      one.set("method", json::JsonValue(entry.second.method));
      one.set("params", entry.second.params.isObject()
                            ? entry.second.params
                            : json::JsonValue::object());
      requests.set(entry.first, one);
    }
    result.set(kInputRequestsField, requests);
  }

  if (needed.request_state.has_value()) {
    result.set(kRequestStateField,
               json::JsonValue(needed.request_state.value()));
  }

  return result;
}

namespace {

/**
 * Whether a capabilities object actually declares this one.
 *
 * Present is not the same as declared: a capability is announced by an
 * object saying how it is supported, and `false` or `null` under the
 * same key is a client saying it does not. Reading the key alone would
 * let a client that said no be asked anyway.
 */
bool declares(const json::JsonValue& capabilities, const std::string& name) {
  if (!capabilities.isObject() || !capabilities.contains(name)) {
    return false;
  }
  const auto& declared = capabilities[name];
  if (declared.isNull()) {
    return false;
  }
  if (declared.isBoolean()) {
    return declared.getBool();
  }
  return true;
}

}  // namespace

std::vector<std::string> capabilitiesMissingFor(const InputRequests& requests,
                                                const std::string& declared) {
  std::vector<std::string> missing;

  json::JsonValue capabilities = json::JsonValue::object();
  if (!declared.empty()) {
    try {
      auto parsed = json::JsonValue::parse(declared);
      if (parsed.isObject()) {
        capabilities = parsed;
      }
    } catch (const std::exception&) {
      // Unreadable is read as declaring nothing, which refuses every
      // request rather than waving them all through — the wrong way to
      // be wrong here is the permissive one.
    }
  }

  for (const auto& entry : requests) {
    const std::string needed = capabilityFor(entry.second.method);
    if (needed.empty()) {
      // Not one of the three this revision defines, so there is no
      // capability to check it against — and a request that cannot be
      // checked cannot be shown to be supported. Named here rather than
      // waved through: whether it is a typo or something newer, sending
      // it would be asking a client for something it never said it can
      // do, which is the one thing this exists to prevent.
      const std::string unknown = "(" + entry.second.method + ")";
      if (std::find(missing.begin(), missing.end(), unknown) == missing.end()) {
        missing.push_back(unknown);
      }
      continue;
    }
    if (declares(capabilities, needed)) {
      continue;
    }
    if (std::find(missing.begin(), missing.end(), needed) == missing.end()) {
      missing.push_back(needed);
    }
  }

  return missing;
}

std::string declaredCapabilitiesIn(const std::string& raw_meta) {
  if (raw_meta.empty()) {
    return std::string();
  }
  try {
    auto meta = json::JsonValue::parse(raw_meta);
    if (meta.isObject() && meta.contains(kMetaClientCapabilities)) {
      return meta[kMetaClientCapabilities].toString();
    }
  } catch (const std::exception&) {
    // Unreadable metadata declares nothing, which is the same answer as
    // saying nothing — and the safe one.
  }
  return std::string();
}

std::string declaredVersionIn(const std::string& raw_meta) {
  if (raw_meta.empty()) {
    return std::string();
  }
  try {
    auto meta = json::JsonValue::parse(raw_meta);
    if (meta.isObject() && meta.contains(kMetaProtocolVersion) &&
        meta[kMetaProtocolVersion].isString()) {
      return meta[kMetaProtocolVersion].getString();
    }
  } catch (const std::exception&) {
    // Unreadable metadata declares nothing, and a request that declared
    // no version is not one of an era where every request declares one.
  }
  return std::string();
}

json::JsonValue requiredCapabilitiesData(
    const std::vector<std::string>& missing) {
  // Shaped as a capabilities object rather than a list of names: it is
  // what the client would have had to declare, so it can be compared
  // against what it did declare without translating either.
  json::JsonValue required = json::JsonValue::object();
  for (const auto& capability : missing) {
    required.set(capability, json::JsonValue::object());
  }

  json::JsonValue data = json::JsonValue::object();
  data.set(kRequiredCapabilitiesField, required);
  return data;
}

CarriedInput carriedInputOf(const json::JsonValue& params) {
  CarriedInput carried;
  carried.responses = json::JsonValue::object();

  if (!params.isObject()) {
    return carried;
  }
  if (params.contains(kInputResponsesField) &&
      params[kInputResponsesField].isObject()) {
    carried.responses = params[kInputResponsesField];
  }
  if (params.contains(kRequestStateField) &&
      params[kRequestStateField].isString()) {
    // Taken as it came and never parsed. What it means is the handler's
    // business, and treating it as anything but bytes here would be this
    // layer trusting something a client could have written.
    carried.request_state =
        mcp::make_optional(params[kRequestStateField].getString());
  }
  return carried;
}

AskedFor askedForIn(const json::JsonValue& result) {
  AskedFor asked;
  if (!result.isObject() || !result.contains(kResultTypeField) ||
      !result[kResultTypeField].isString() ||
      result[kResultTypeField].getString() != kResultTypeInputRequired) {
    return asked;
  }

  if (result.contains(kInputRequestsField) &&
      result[kInputRequestsField].isObject()) {
    const auto& requests = result[kInputRequestsField];
    for (const auto& name : requests.keys()) {
      const auto& one = requests[name];
      if (!one.isObject() || !one.contains("method") ||
          !one["method"].isString()) {
        // A question with no method is one nothing could be asked of.
        // Skipped rather than guessed at.
        continue;
      }
      InputRequest request;
      request.method = one["method"].getString();
      request.params = one.contains("params") && one["params"].isObject()
                           ? one["params"]
                           : json::JsonValue::object();
      asked.requests[name] = request;
    }
  }

  if (result.contains(kRequestStateField) &&
      result[kRequestStateField].isString()) {
    asked.request_state =
        mcp::make_optional(result[kRequestStateField].getString());
  }

  // Only now: a question that asks nothing and carries nothing would send
  // the same request again unchanged, and be answered the same way.
  asked.asked = !asked.requests.empty() || asked.request_state.has_value();
  return asked;
}

json::JsonValue renderInputResponses(
    const std::map<std::string, json::JsonValue>& answers) {
  json::JsonValue responses = json::JsonValue::object();
  for (const auto& entry : answers) {
    responses.set(entry.first, entry.second);
  }
  return responses;
}

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
