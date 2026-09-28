// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Elicitation, typed, in either revision. See the header for the two ways
 * it travels.
 */

#include "mcp/protocol/elicitation.h"

#include <stdexcept>

#include "mcp/json/json_serialization.h"

namespace mcp {
namespace protocol {
namespace elicitation {

jsonrpc::Request toRequest(const ElicitRequest& elicit, const RequestId& id) {
  jsonrpc::Request request;
  request.jsonrpc = "2.0";
  request.id = id;
  request.method = modern::kMethodElicitation;
  // Carried as JSON: the form nests objects the flat params map cannot.
  request.params_json = mcp::make_optional(json::to_json(elicit));
  request.params =
      mcp::make_optional(json::jsonToMetadata(request.params_json.value()));
  return request;
}

modern::InputRequest toInputRequest(const ElicitRequest& elicit) {
  modern::InputRequest input;
  input.method = modern::kMethodElicitation;
  input.params = json::to_json(elicit);
  return input;
}

ElicitRequest fromRequest(const jsonrpc::Request& request) {
  const json::JsonValue params =
      request.params_json.has_value()
          ? request.params_json.value()
          : (request.params.has_value()
                 ? json::metadataToJson(request.params.value())
                 : json::JsonValue::object());
  ElicitRequest elicit = json::from_json<ElicitRequest>(params);
  elicit.id = request.id;
  return elicit;
}

jsonrpc::ResponseResult toResult(const ElicitResult& result) {
  return jsonrpc::ResponseResult(json::to_json(result));
}

ElicitResult resultFrom(const json::JsonValue& answer) {
  return json::from_json<ElicitResult>(answer);
}

ElicitResult resultFrom(const jsonrpc::Response& response) {
  if (response.error.has_value()) {
    throw std::runtime_error(response.error->message);
  }
  if (!response.result.has_value()) {
    throw json::JsonException(
        "the client answered the elicitation with nothing");
  }
  return resultFrom(json::to_json(response.result.value()));
}

}  // namespace elicitation
}  // namespace protocol
}  // namespace mcp
