/**
 * Working out which protocol era a server speaks.
 */

#include "mcp/client/transport_probe.h"

#include "mcp/json/json_bridge.h"
#include "mcp/logging/log_macros.h"

#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "client"

namespace mcp {
namespace client {

namespace {

/** True when the error's data names the version complaint by name. */
bool namesUnsupportedVersion(const json::JsonValue& error) {
  const auto mentions = [](const std::string& text) {
    return text.find(modern_error::kUnsupportedProtocolVersion) !=
           std::string::npos;
  };

  if (error.contains("data")) {
    const auto& data = error["data"];
    if (data.isString() && mentions(data.getString())) {
      return true;
    }
    if (data.isObject() && data.contains("type") && data["type"].isString() &&
        mentions(data["type"].getString())) {
      return true;
    }
  }
  // Some servers put it in the message instead. Reading both costs
  // nothing and means the classification does not depend on which one a
  // given implementation chose.
  return error.contains("message") && error["message"].isString() &&
         mentions(error["message"].getString());
}

}  // namespace

bool isModernRefusal(int status_code, const std::string& body) {
  // Only these two. A modern server refusing an introduction it has no
  // concept of answers with one of them; anything else is a different
  // conversation, and reading a 500 or a 200 this way would stop the
  // ladder over something that says nothing about the era.
  if (status_code != 400 && status_code != 404) {
    return false;
  }
  if (body.empty()) {
    return false;
  }

  json::JsonValue parsed;
  try {
    parsed = json::JsonValue::parse(body);
  } catch (const std::exception&) {
    // A body that is not JSON is not a JSON-RPC refusal. A plain 404
    // page is the ordinary case.
    return false;
  }

  if (!parsed.isObject() || !parsed.contains("error")) {
    return false;
  }

  // An `error` that is not an object is not a JSON-RPC error, whatever
  // it says. This project's own 404 for an unknown path is
  // {"error":"not_found"} — reading that as a modern server would have
  // a client refuse to fall back to the transport that would have
  // worked.
  const auto& error = parsed["error"];
  if (!error.isObject()) {
    return false;
  }

  if (error.contains("code") && error["code"].isInteger()) {
    const int code = error["code"].getInt();
    if (code == modern_error::kHeaderMismatch ||
        code == modern_error::kMethodNotFound) {
      return true;
    }
  }

  return namesUnsupportedVersion(error);
}

void NoModernProbe::probe(const std::string& url, ProbeCallback done) {
  (void)url;
  GOPHER_LOG_DEBUG("Modern probe not built; treating the server as not modern");
  if (done) {
    done(ProbeResult::notModern(0, std::string()));
  }
}

}  // namespace client
}  // namespace mcp
