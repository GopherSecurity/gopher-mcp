/**
 * Subscriptions, and what goes down each one. See the header.
 */

#include "mcp/server/listen_registry.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "mcp/logging/log_macros.h"
#include "mcp/protocol/modern_era.h"

#undef GOPHER_LOG_COMPONENT
#define GOPHER_LOG_COMPONENT "server"

namespace mcp {
namespace server {

namespace {

namespace modern = protocol::modern;

/** A subscription id as it travels: whichever kind of id it was. */
json::JsonValue idAsJson(const RequestId& id) {
  if (holds_alternative<std::string>(id)) {
    return json::JsonValue(get<std::string>(id));
  }
  return json::JsonValue(static_cast<int64_t>(get<int64_t>(id)));
}

bool asBool(const json::JsonValue& object, const char* key) {
  return object.isObject() && object.contains(key) && object[key].isBoolean() &&
         object[key].getBool();
}

/**
 * The metadata every message on a subscription stream carries, as the
 * flat map a notification's params are held in.
 *
 * Nested JSON travels stringified through that map and is rebuilt on the
 * way out, which is how anything but a scalar is carried here.
 */
void tagWithSubscription(Metadata& params, const RequestId& id) {
  json::JsonValue meta = json::JsonValue::object();
  meta.set(modern::kMetaSubscriptionId, idAsJson(id));
  params["_meta"] = MetadataValue(meta.toString());
}

}  // namespace

NotificationFilter NotificationFilter::parse(const json::JsonValue& params) {
  NotificationFilter filter;
  if (!params.isObject() || !params.contains(modern::kFilterField) ||
      !params[modern::kFilterField].isObject()) {
    return filter;
  }
  const auto& asked = params[modern::kFilterField];

  filter.tools_list_changed = asBool(asked, modern::kFilterToolsListChanged);
  filter.prompts_list_changed =
      asBool(asked, modern::kFilterPromptsListChanged);
  filter.resources_list_changed =
      asBool(asked, modern::kFilterResourcesListChanged);

  if (asked.contains(modern::kFilterResourceSubscriptions) &&
      asked[modern::kFilterResourceSubscriptions].isArray()) {
    const auto& uris = asked[modern::kFilterResourceSubscriptions];
    for (size_t i = 0; i < uris.size(); ++i) {
      if (uris[i].isString()) {
        filter.resource_uris.push_back(uris[i].getString());
      }
    }
  }
  return filter;
}

json::JsonValue NotificationFilter::render() const {
  // Only what is actually honoured. A type left out is one the client
  // now knows not to wait for, which is the point of echoing at all.
  json::JsonValue asked = json::JsonValue::object();
  if (tools_list_changed) {
    asked.set(modern::kFilterToolsListChanged, json::JsonValue(true));
  }
  if (prompts_list_changed) {
    asked.set(modern::kFilterPromptsListChanged, json::JsonValue(true));
  }
  if (resources_list_changed) {
    asked.set(modern::kFilterResourcesListChanged, json::JsonValue(true));
  }
  if (!resource_uris.empty()) {
    json::JsonValue uris = json::JsonValue::array();
    for (const auto& uri : resource_uris) {
      uris.push_back(json::JsonValue(uri));
    }
    asked.set(modern::kFilterResourceSubscriptions, uris);
  }
  return asked;
}

bool NotificationFilter::wants(const std::string& method,
                               const std::string& uri) const {
  if (method == modern::kNotificationToolsListChanged) {
    return tools_list_changed;
  }
  if (method == modern::kNotificationPromptsListChanged) {
    return prompts_list_changed;
  }
  if (method == modern::kNotificationResourcesListChanged) {
    return resources_list_changed;
  }
  if (method == modern::kNotificationResourcesUpdated) {
    // Named resources only. A subscription that asked about one file has
    // not asked about every file.
    return std::find(resource_uris.begin(), resource_uris.end(), uri) !=
           resource_uris.end();
  }
  // Anything else — progress, logging — belongs to the request it
  // relates to and never to a subscription.
  return false;
}

bool ListenRegistry::open(const RequestId& id,
                          const ResponseStreamPtr& stream,
                          const NotificationFilter& filter) {
  if (!stream) {
    return false;
  }
  const RequestIdKey key = requestIdKey(id);
  if (subscriptions_.count(key) != 0) {
    // Two subscriptions answering to one name would make every message
    // on either ambiguous.
    GOPHER_LOG_WARN("a subscription already answers to {}",
                    requestIdKeyToString(key));
    return false;
  }

  Subscription subscription;
  subscription.id = id;
  subscription.stream = stream;
  subscription.filter = filter;

  // Said first, before anything else goes down this stream: it tells the
  // client which subscription the stream is, and which of the things it
  // asked for it will actually be getting.
  jsonrpc::Notification acknowledgement;
  acknowledgement.jsonrpc = "2.0";
  acknowledgement.method = modern::kNotificationSubscriptionsAcknowledged;
  Metadata params;
  tagWithSubscription(params, id);
  params[modern::kFilterField] = MetadataValue(filter.render().toString());
  acknowledgement.params = mcp::make_optional(params);

  auto sent = stream->sendNotification(acknowledgement);
  if (holds_alternative<Error>(sent)) {
    GOPHER_LOG_WARN("subscription {} could not be acknowledged: {}",
                    requestIdKeyToString(key), get<Error>(sent).message);
    return false;
  }

  subscriptions_.emplace(key, std::move(subscription));

  // A subscription ends when its client stops reading it: there is no
  // message for cancelling one, and on the revision this belongs to
  // closing the stream is the only way to ask. Dropped rather than closed
  // — a stream nobody is reading cannot be told anything, and the response
  // that says an ending was graceful would go nowhere.
  std::weak_ptr<int> alive = alive_;
  const RequestId id_copy = id;
  stream->onCancelled([this, alive, id_copy]() {
    if (alive.expired()) {
      return;
    }
    if (forget(id_copy)) {
      GOPHER_LOG_DEBUG("subscription {} ended by its client",
                       requestIdKeyToString(requestIdKey(id_copy)));
    }
  });

  GOPHER_LOG_DEBUG("subscription {} opened", requestIdKeyToString(key));
  return true;
}

size_t ListenRegistry::publish(const std::string& method,
                               const json::JsonValue& params,
                               const std::string& uri) {
  size_t delivered = 0;

  for (auto& entry : subscriptions_) {
    Subscription& subscription = entry.second;
    if (!subscription.filter.wants(method, uri)) {
      continue;
    }

    // Built per subscription rather than once and reused: each carries
    // the id of the stream it is going down, and one message tagged with
    // another subscription's id would be worse than none.
    jsonrpc::Notification notification;
    notification.jsonrpc = "2.0";
    notification.method = method;

    Metadata carried;
    if (params.isObject()) {
      for (const auto& key : params.keys()) {
        if (key == "_meta") {
          // Replaced rather than merged: which subscription a message
          // belongs to is this layer's to say.
          continue;
        }
        const auto& value = params[key];
        if (value.isString()) {
          carried[key] = MetadataValue(value.getString());
        } else if (value.isBoolean()) {
          carried[key] = MetadataValue(value.getBool());
        } else if (value.isInteger()) {
          carried[key] = MetadataValue(value.getInt64());
        } else if (value.isFloat()) {
          carried[key] = MetadataValue(value.getFloat());
        } else {
          carried[key] = MetadataValue(value.toString());
        }
      }
    }
    tagWithSubscription(carried, subscription.id);
    notification.params = mcp::make_optional(carried);

    auto sent = subscription.stream->sendNotification(notification);
    if (holds_alternative<Error>(sent)) {
      GOPHER_LOG_DEBUG("subscription {} did not take {}: {}",
                       requestIdKeyToString(entry.first), method,
                       get<Error>(sent).message);
      continue;
    }
    ++delivered;
  }

  return delivered;
}

bool ListenRegistry::close(const RequestId& id) {
  auto it = subscriptions_.find(requestIdKey(id));
  if (it == subscriptions_.end()) {
    return false;
  }

  // The response its listen request never got. A client that receives it
  // knows this ended rather than dropped, and a transport that closes
  // without one is telling it the opposite.
  json::JsonValue result = json::JsonValue::object();
  result.set(modern::kResultTypeField,
             json::JsonValue(modern::kResultTypeComplete));
  json::JsonValue meta = json::JsonValue::object();
  meta.set(modern::kMetaSubscriptionId, idAsJson(it->second.id));
  result.set("_meta", meta);

  it->second.stream->sendResponse(jsonrpc::Response::success(
      it->second.id, jsonrpc::ResponseResult(result)));

  GOPHER_LOG_DEBUG("subscription {} closed", requestIdKeyToString(it->first));
  subscriptions_.erase(it);
  return true;
}

void ListenRegistry::closeAll() {
  // Collected first: closing rearranges what the walk is reading.
  std::vector<RequestId> held;
  held.reserve(subscriptions_.size());
  for (const auto& entry : subscriptions_) {
    held.push_back(entry.second.id);
  }
  for (const auto& id : held) {
    close(id);
  }
}

bool ListenRegistry::forget(const RequestId& id) {
  return subscriptions_.erase(requestIdKey(id)) != 0;
}

size_t ListenRegistry::forgetDead() {
  size_t dropped = 0;
  for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
    if (it->second.stream && it->second.stream->alive()) {
      ++it;
      continue;
    }
    // Nothing to say goodbye to: a stream that is gone cannot be told
    // that its subscription ended.
    GOPHER_LOG_DEBUG("subscription {} dropped with its stream",
                     requestIdKeyToString(it->first));
    it = subscriptions_.erase(it);
    ++dropped;
  }
  return dropped;
}

}  // namespace server
}  // namespace mcp
