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
      // Not one of the three a client declares support for. Nothing to
      // check, and refusing on a name we do not recognize would refuse
      // whatever the next revision adds.
      continue;
    }
    if (capabilities.contains(needed)) {
      continue;
    }
    if (std::find(missing.begin(), missing.end(), needed) == missing.end()) {
      missing.push_back(needed);
    }
  }

  return missing;
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

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
