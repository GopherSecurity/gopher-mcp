// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * The Tasks extension, io.modelcontextprotocol/tasks.
 *
 * A server may answer tools/call with a task instead of a result: a handle
 * the client polls with tasks/get, or listens to through
 * subscriptions/listen, until the work finishes. A task can stop to ask
 * the client for input, answered with tasks/update, and the client can
 * cancel it with tasks/cancel.
 *
 *   tools/call ─► {resultType: "task", taskId, status, createdAt,
 *                  lastUpdatedAt, ttlMs, pollIntervalMs?}
 *   tasks/get  ─► {resultType: "complete", ...the task, with inputRequests,
 *                  result or error as its status has them}
 *
 * Both sides declare the extension under capabilities.extensions, and a
 * server answers with a task only to a request that declared it. These are
 * the shapes both ends read and write.
 */

#pragma once

#include <chrono>
#include <string>

#include "mcp/core/compat.h"
#include "mcp/json/json_bridge.h"
#include "mcp/types.h"

namespace mcp {
namespace protocol {
namespace tasks {

constexpr const char* kExtensionId = "io.modelcontextprotocol/tasks";

constexpr const char* kMethodGet = "tasks/get";
constexpr const char* kMethodUpdate = "tasks/update";
constexpr const char* kMethodCancel = "tasks/cancel";
constexpr const char* kNotificationTasks = "notifications/tasks";

/** The resultType of a result that is a task rather than the answer. */
constexpr const char* kResultTypeTask = "task";

/** What a subscription names to hear about tasks. */
constexpr const char* kFilterTaskIds = "taskIds";

enum class Status { Working, InputRequired, Completed, Failed, Cancelled };

const char* statusName(Status status);
optional<Status> statusNamed(const std::string& name);

/** Completed, failed and cancelled: a task in one of these never changes. */
inline bool isTerminal(Status status) {
  return status == Status::Completed || status == Status::Failed ||
         status == Status::Cancelled;
}

/** Whether a method is one of the task methods. */
inline bool isTaskMethod(const std::string& method) {
  return method == kMethodGet || method == kMethodUpdate ||
         method == kMethodCancel;
}

/**
 * A task, as tasks/get and notifications/tasks describe it: its state, and
 * whichever of inputRequests, result and error that state carries.
 */
struct Task {
  std::string taskId;
  Status status = Status::Working;
  optional<std::string> statusMessage;
  // ISO 8601, UTC.
  std::string createdAt;
  std::string lastUpdatedAt;
  // From creation; unset is unlimited, written as null.
  optional<int64_t> ttlMs;
  // How often a client should poll.
  optional<int64_t> pollIntervalMs;
  // What the task is waiting on the client for, while input_required: an
  // object of requests by key.
  json::JsonValue inputRequests;
  // The answer, once completed: what the original request would have
  // returned.
  optional<json::JsonValue> result;
  // The JSON-RPC error, once failed.
  optional<Error> error;
};

/** The task's fields, without a resultType. */
json::JsonValue toJson(const Task& task);

/**
 * A task read from any peer; nothing when it lacks anything a task must
 * have: its id, a known status, its timestamps, ttlMs (a number or null),
 * and what its status carries — inputRequests, result, or an error with a
 * code and message.
 */
optional<Task> fromJson(const json::JsonValue& json);

/** The answer to a request that became a task: resultType "task". */
json::JsonValue createTaskResult(const Task& task);

/** The answer to tasks/get: resultType "complete", and the task. */
json::JsonValue getTaskResult(const Task& task);

/** Whether a result is a task rather than the answer. */
bool isTaskResult(const json::JsonValue& result);

/** An id no one can guess: 128 random bits, as hex. */
std::string newTaskId();

/** A time as ISO 8601 in UTC, to the millisecond. */
std::string iso8601(std::chrono::system_clock::time_point when);

/**
 * The data of a refusal to a client that did not declare the extension,
 * naming it as the capability required.
 */
json::JsonValue requiredExtensionData();

}  // namespace tasks
}  // namespace protocol
}  // namespace mcp
