/**
 * What a client puts on a request beside its body. See the header.
 */

#include "mcp/transport/streamable_http_client_session.h"

#include "mcp/logging/log_macros.h"

namespace mcp {
namespace transport {

void StreamableHttpClientSession::decorate(
    std::map<std::string, std::string>& headers,
    const json::JsonValue& message) const {
  decorate(headers);

  if (!protocol::modern::isModernVersion(protocol_version_)) {
    // Every older revision mirrors nothing, so there is nothing to add
    // and nothing to get wrong.
    return;
  }
  if (!message.isObject() || !message.contains("method") ||
      !message["method"].isString()) {
    return;
  }

  const std::string method = message["method"].getString();
  headers[protocol::modern::kMethodHeader] = method;

  json::JsonValue params;
  if (message.contains("params") && message["params"].isObject()) {
    params = message["params"];
  }

  // Two of the three call it `name` and one calls it `uri`, so which
  // field carries it is known from the method rather than guessed from
  // the body.
  if (protocol::modern::carriesName(method)) {
    const std::string field = protocol::modern::nameFieldFor(method);
    if (params.isObject() && params.contains(field)) {
      std::string text;
      if (protocol::modern::headerTextForScalar(params[field], &text)) {
        headers[protocol::modern::kNameHeader] =
            protocol::modern::encodeHeaderValue(text);
      }
    }
  }

  if (method != protocol::modern::kMethodToolsCall || !params.isObject() ||
      !params.contains("name") || !params["name"].isString()) {
    return;
  }
  std::vector<protocol::modern::DesignatedParam> designated;
  if (!designationsFor(params["name"].getString(), &designated)) {
    return;
  }

  json::JsonValue arguments = json::JsonValue::object();
  if (params.contains("arguments") && params["arguments"].isObject()) {
    arguments = params["arguments"];
  }

  for (const auto& param : designated) {
    json::JsonValue value;
    if (!protocol::modern::valueAtPath(arguments, param.path, &value)) {
      // An argument this call does not carry gets no header, and a
      // server that saw one would refuse the call for naming a value it
      // was never sent.
      continue;
    }
    if (value.isInteger() &&
        !protocol::modern::isExactlyCarryableInteger(value.getInt64())) {
      // Outside the range a schema is supposed to designate — but the
      // header still goes, because leaving it out is worse than sending
      // it. A server that finds the value in the body and no header
      // beside it refuses the call, so suppressing the header here would
      // make such an argument impossible to send at all. Sent, the two
      // ends compare it exactly and agree.
      GOPHER_LOG_WARN(
          "{} carries {}, which is outside the range a designated integer is "
          "meant to hold; a peer that rounds it will disagree about it",
          param.headerName(), value.getInt64());
    }
    std::string text;
    if (!protocol::modern::headerTextForScalar(value, &text)) {
      continue;
    }
    headers[param.headerName()] = protocol::modern::encodeHeaderValue(text);
  }
}

json::JsonValue StreamableHttpClientSession::declareSelf(
    const json::JsonValue& message) const {
  if (!protocol::modern::isModernVersion(protocol_version_)) {
    return message;
  }
  if (!message.isObject() || !message.contains("method") ||
      !message.contains("id") || message["id"].isNull()) {
    // Only requests. This revision defines nothing about what a
    // notification carries, so nothing is added to one.
    return message;
  }

  json::JsonValue params =
      message.contains("params") && message["params"].isObject()
          ? message["params"]
          : json::JsonValue::object();
  json::JsonValue meta = params.contains("_meta") && params["_meta"].isObject()
                             ? params["_meta"]
                             : json::JsonValue::object();

  meta.set(protocol::modern::kMetaProtocolVersion,
           json::JsonValue(protocol_version_));

  // Required, and empty is a perfectly good answer: it says this client
  // can do nothing beyond the core protocol, which is true of one that
  // declared nothing.
  meta.set(protocol::modern::kMetaClientCapabilities,
           client_capabilities_.isObject() ? client_capabilities_
                                           : json::JsonValue::object());

  // Optional, and left out rather than filled with a placeholder: a
  // server must serve a request that never says who is calling, and a
  // made-up name would be worse than none.
  if (!client_name_.empty()) {
    json::JsonValue who = json::JsonValue::object();
    who.set("name", json::JsonValue(client_name_));
    who.set("version", json::JsonValue(client_version_));
    meta.set(protocol::modern::kMetaClientInfo, who);
  }

  params.set("_meta", meta);
  json::JsonValue declared = message;
  declared.set("params", params);
  return declared;
}

std::vector<Tool> StreamableHttpClientSession::acceptListing(
    const std::vector<Tool>& tools) {
  std::vector<Tool> usable;
  usable.reserve(tools.size());

  for (const auto& tool : tools) {
    std::vector<protocol::modern::DesignatedParam> designated;
    auto readable = protocol::modern::designatedParams(tool, &designated);
    if (!holds_alternative<std::nullptr_t>(readable)) {
      GOPHER_LOG_WARN("not offering tool '{}': {}", tool.name,
                      get<Error>(readable).message);
      forgetDesignations(tool.name);
      continue;
    }
    rememberDesignations(tool.name, designated);
    usable.push_back(tool);
  }

  return usable;
}

}  // namespace transport
}  // namespace mcp
