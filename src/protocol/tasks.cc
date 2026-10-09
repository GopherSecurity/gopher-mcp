// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * The Tasks extension's shapes. See the header.
 */

#include "mcp/protocol/tasks.h"

#include <ctime>
#include <random>
#include <stdexcept>

#include <openssl/rand.h>

#include "mcp/json/json_serialization.h"
#include "mcp/protocol/modern_era.h"

namespace mcp {
namespace protocol {
namespace tasks {

namespace {

optional<std::string> stringAt(const json::JsonValue& json, const char* key) {
  if (json.contains(key) && json[key].isString()) {
    return json[key].getString();
  }
  return nullopt;
}

optional<int64_t> integerAt(const json::JsonValue& json, const char* key) {
  if (json.contains(key) && json[key].isInteger()) {
    return json[key].getInt64();
  }
  return nullopt;
}

}  // namespace

const char* statusName(Status status) {
  switch (status) {
    case Status::Working:
      return "working";
    case Status::InputRequired:
      return "input_required";
    case Status::Completed:
      return "completed";
    case Status::Failed:
      return "failed";
    case Status::Cancelled:
      return "cancelled";
  }
  return "working";
}

optional<Status> statusNamed(const std::string& name) {
  for (Status status : {Status::Working, Status::InputRequired,
                        Status::Completed, Status::Failed, Status::Cancelled}) {
    if (name == statusName(status)) {
      return status;
    }
  }
  return nullopt;
}

json::JsonValue toJson(const Task& task) {
  json::JsonValue json = json::JsonValue::object();
  json.set("taskId", json::JsonValue(task.taskId));
  json.set("status", json::JsonValue(statusName(task.status)));
  if (task.statusMessage.has_value()) {
    json.set("statusMessage", json::JsonValue(task.statusMessage.value()));
  }
  json.set("createdAt", json::JsonValue(task.createdAt));
  json.set("lastUpdatedAt", json::JsonValue(task.lastUpdatedAt));
  // Required, and null when the task may be kept for ever.
  json.set("ttlMs", task.ttlMs.has_value() ? json::JsonValue(task.ttlMs.value())
                                           : json::JsonValue());
  if (task.pollIntervalMs.has_value()) {
    json.set("pollIntervalMs", json::JsonValue(task.pollIntervalMs.value()));
  }
  // What each status carries, and only that status.
  if (task.status == Status::InputRequired) {
    json.set(modern::kInputRequestsField, task.inputRequests.isObject()
                                              ? task.inputRequests
                                              : json::JsonValue::object());
  }
  if (task.status == Status::Completed) {
    json.set("result", task.result.has_value() && task.result->isObject()
                           ? task.result.value()
                           : json::JsonValue::object());
  }
  if (task.status == Status::Failed) {
    json.set("error", json::to_json(task.error.has_value()
                                        ? task.error.value()
                                        : Error(jsonrpc::INTERNAL_ERROR,
                                                "the task failed")));
  }
  return json;
}

optional<Task> fromJson(const json::JsonValue& json) {
  if (!json.isObject()) {
    return nullopt;
  }
  const auto id = stringAt(json, "taskId");
  const auto status_name = stringAt(json, "status");
  const auto status =
      status_name.has_value() ? statusNamed(status_name.value()) : nullopt;
  if (!id.has_value() || id->empty() || !status.has_value()) {
    return nullopt;
  }
  Task task;
  task.taskId = id.value();
  task.status = status.value();
  task.statusMessage = stringAt(json, "statusMessage");
  task.createdAt = stringAt(json, "createdAt").value_or("");
  task.lastUpdatedAt = stringAt(json, "lastUpdatedAt").value_or("");
  task.ttlMs = integerAt(json, "ttlMs");
  task.pollIntervalMs = integerAt(json, "pollIntervalMs");
  if (json.contains(modern::kInputRequestsField) &&
      json[modern::kInputRequestsField].isObject()) {
    task.inputRequests = json[modern::kInputRequestsField];
  }
  if (json.contains("result") && json["result"].isObject()) {
    task.result = json["result"];
  }
  if (json.contains("error") && json["error"].isObject()) {
    try {
      task.error = json::from_json<Error>(json["error"]);
    } catch (const std::exception&) {
      task.error = Error(jsonrpc::INTERNAL_ERROR, "the task failed");
    }
  }
  return task;
}

json::JsonValue createTaskResult(const Task& task) {
  json::JsonValue result = toJson(task);
  result.set(modern::kResultTypeField, json::JsonValue(kResultTypeTask));
  return result;
}

json::JsonValue getTaskResult(const Task& task) {
  json::JsonValue result = toJson(task);
  result.set(modern::kResultTypeField,
             json::JsonValue(modern::kResultTypeComplete));
  return result;
}

bool isTaskResult(const json::JsonValue& result) {
  return result.isObject() && result.contains(modern::kResultTypeField) &&
         result[modern::kResultTypeField].isString() &&
         result[modern::kResultTypeField].getString() == kResultTypeTask;
}

std::string newTaskId() {
  unsigned char bytes[16];
  if (RAND_bytes(bytes, static_cast<int>(sizeof(bytes))) != 1) {
    // A task id is a bearer of the task: nothing short of the system's
    // own randomness will do, and without it no task is made.
    throw std::runtime_error("no randomness for a task id");
  }
  static const char kHex[] = "0123456789abcdef";
  std::string id;
  id.reserve(sizeof(bytes) * 2);
  for (unsigned char byte : bytes) {
    id.push_back(kHex[byte >> 4]);
    id.push_back(kHex[byte & 0x0f]);
  }
  return id;
}

std::string iso8601(std::chrono::system_clock::time_point when) {
  const auto since = when.time_since_epoch();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since);
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(since - seconds);
  const std::time_t time = static_cast<std::time_t>(seconds.count());
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &utc);
  char out[40];
  std::snprintf(out, sizeof(out), "%s.%03dZ", text,
                static_cast<int>(millis.count()));
  return out;
}

json::JsonValue requiredExtensionData() {
  json::JsonValue extensions = json::JsonValue::object();
  extensions.set(kExtensionId, json::JsonValue::object());
  json::JsonValue required = json::JsonValue::object();
  required.set("extensions", extensions);
  json::JsonValue data = json::JsonValue::object();
  data.set("requiredCapabilities", required);
  return data;
}

}  // namespace tasks
}  // namespace protocol
}  // namespace mcp
