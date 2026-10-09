// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * The filter both ends of a subscription speak. See the header.
 */

#include "mcp/protocol/subscriptions.h"

#include <algorithm>

namespace mcp {
namespace protocol {
namespace modern {

namespace {

bool asBool(const json::JsonValue& object, const char* key) {
  return object.isObject() && object.contains(key) && object[key].isBoolean() &&
         object[key].getBool();
}

}  // namespace

NotificationFilter NotificationFilter::parse(const json::JsonValue& params) {
  NotificationFilter filter;
  if (!params.isObject() || !params.contains(kFilterField) ||
      !params[kFilterField].isObject()) {
    return filter;
  }
  const auto& asked = params[kFilterField];

  filter.tools_list_changed = asBool(asked, kFilterToolsListChanged);
  filter.prompts_list_changed = asBool(asked, kFilterPromptsListChanged);
  filter.resources_list_changed = asBool(asked, kFilterResourcesListChanged);

  if (asked.contains(kFilterResourceSubscriptions) &&
      asked[kFilterResourceSubscriptions].isArray()) {
    const auto& uris = asked[kFilterResourceSubscriptions];
    for (size_t i = 0; i < uris.size(); ++i) {
      if (uris[i].isString()) {
        filter.resource_uris.push_back(uris[i].getString());
      }
    }
  }
  if (asked.contains("taskIds") && asked["taskIds"].isArray()) {
    const auto& ids = asked["taskIds"];
    for (size_t i = 0; i < ids.size(); ++i) {
      if (ids[i].isString()) {
        filter.task_ids.push_back(ids[i].getString());
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
    asked.set(kFilterToolsListChanged, json::JsonValue(true));
  }
  if (prompts_list_changed) {
    asked.set(kFilterPromptsListChanged, json::JsonValue(true));
  }
  if (resources_list_changed) {
    asked.set(kFilterResourcesListChanged, json::JsonValue(true));
  }
  if (!resource_uris.empty()) {
    json::JsonValue uris = json::JsonValue::array();
    for (const auto& uri : resource_uris) {
      uris.push_back(json::JsonValue(uri));
    }
    asked.set(kFilterResourceSubscriptions, uris);
  }
  if (!task_ids.empty()) {
    json::JsonValue ids = json::JsonValue::array();
    for (const auto& id : task_ids) {
      ids.push_back(json::JsonValue(id));
    }
    asked.set("taskIds", ids);
  }
  return asked;
}

bool NotificationFilter::wants(const std::string& method,
                               const std::string& uri) const {
  if (method == kNotificationToolsListChanged) {
    return tools_list_changed;
  }
  if (method == kNotificationPromptsListChanged) {
    return prompts_list_changed;
  }
  if (method == kNotificationResourcesListChanged) {
    return resources_list_changed;
  }
  if (method == kNotificationResourcesUpdated) {
    // Named resources only. A subscription that asked about one file has
    // not asked about every file.
    return std::find(resource_uris.begin(), resource_uris.end(), uri) !=
           resource_uris.end();
  }
  if (method == "notifications/tasks") {
    return std::find(task_ids.begin(), task_ids.end(), uri) != task_ids.end();
  }
  // Anything else — progress, logging — belongs to the request it
  // relates to and never to a subscription.
  return false;
}

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
