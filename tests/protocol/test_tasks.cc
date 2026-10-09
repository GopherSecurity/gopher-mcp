// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * The Tasks extension's shapes, and the store a server keeps its tasks in.
 *
 *   task  {"taskId", "status", "statusMessage"?, "createdAt",
 *          "lastUpdatedAt", "ttlMs": n | null, "pollIntervalMs"?,
 *          + "inputRequests" | "result" | "error" as the status has them}
 *
 * A task belongs to the caller that made it: one that doesn't exist, has
 * expired, or is another's all look the same. A finished task never
 * changes again.
 */

#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/protocol/tasks.h"
#include "mcp/server/task_store.h"
#include "mcp/types.h"

namespace mcp {
namespace {

using json::JsonValue;
namespace tasks = protocol::tasks;
using server::TaskHandle;
using server::TaskStore;

#define EXPECT_SAME_JSON(a, b) EXPECT_EQ((a).toString(), (b).toString())

tasks::Task taskIn(tasks::Status status) {
  tasks::Task task;
  task.taskId = "786512e2";
  task.status = status;
  task.createdAt = "2025-11-25T10:30:00Z";
  task.lastUpdatedAt = "2025-11-25T10:40:00Z";
  task.ttlMs = 60000;
  task.pollIntervalMs = 5000;
  return task;
}

protocol::modern::InputRequests askName() {
  protocol::modern::InputRequest ask;
  ask.method = protocol::modern::kMethodElicitation;
  ask.params = JsonValue::parse(R"({"mode":"form","message":"Name?",
      "requestedSchema":{"type":"object","properties":{"name":{"type":"string"}}}})");
  protocol::modern::InputRequests requests;
  requests["name"] = ask;
  return requests;
}

std::shared_ptr<TaskStore> storeWith(
    std::vector<tasks::Task>* changes = nullptr,
    std::chrono::milliseconds ttl = std::chrono::hours(1),
    size_t max_per_caller = 100) {
  TaskStore::Config config;
  config.ttl = ttl;
  config.poll_interval = std::chrono::milliseconds(250);
  config.max_per_caller = max_per_caller;
  return TaskStore::make(config, [changes](const tasks::Task& task) {
    if (changes) {
      changes->push_back(task);
    }
  });
}

}  // namespace

// ── The shapes ─────────────────────────────────────────────────────────

TEST(Tasks, EachStatusCarriesOnlyWhatItHas) {
  EXPECT_SAME_JSON(tasks::toJson(taskIn(tasks::Status::Working)),
                   JsonValue::parse(R"({"taskId":"786512e2","status":"working",
      "createdAt":"2025-11-25T10:30:00Z","lastUpdatedAt":"2025-11-25T10:40:00Z",
      "ttlMs":60000,"pollIntervalMs":5000})"));

  auto asking = taskIn(tasks::Status::InputRequired);
  asking.inputRequests =
      JsonValue::parse(R"({"name":{"method":"x","params":{}}})");
  EXPECT_TRUE(tasks::toJson(asking).contains("inputRequests"));

  auto done = taskIn(tasks::Status::Completed);
  done.result = JsonValue::parse(R"({"content":[],"isError":true})");
  done.inputRequests = JsonValue::parse(R"({"stale":{}})");
  const JsonValue done_json = tasks::toJson(done);
  EXPECT_SAME_JSON(done_json["result"],
                   JsonValue::parse(R"({"content":[],"isError":true})"));
  EXPECT_FALSE(done_json.contains("inputRequests"))
      << "a finished task carried what it no longer waits for";

  auto failed = taskIn(tasks::Status::Failed);
  failed.error = Error(jsonrpc::INTERNAL_ERROR, "rate limited");
  EXPECT_EQ(tasks::toJson(failed)["error"]["code"].getInt64(),
            jsonrpc::INTERNAL_ERROR);

  auto forever = taskIn(tasks::Status::Cancelled);
  forever.ttlMs = nullopt;
  const JsonValue forever_json = tasks::toJson(forever);
  ASSERT_TRUE(forever_json.contains("ttlMs"));
  EXPECT_TRUE(forever_json["ttlMs"].isNull()) << "an unlimited ttl is null";
}

TEST(Tasks, ATaskRoundTrips) {
  for (auto status : {tasks::Status::Working, tasks::Status::InputRequired,
                      tasks::Status::Completed, tasks::Status::Failed,
                      tasks::Status::Cancelled}) {
    SCOPED_TRACE(tasks::statusName(status));
    auto task = taskIn(status);
    task.statusMessage = std::string("on it");
    if (status == tasks::Status::InputRequired) {
      task.inputRequests = JsonValue::parse(
          R"({"name":{"method":"elicitation/create","params":{}}})");
    }
    if (status == tasks::Status::Completed) {
      task.result = JsonValue::parse(R"({"content":[]})");
    }
    if (status == tasks::Status::Failed) {
      task.error = Error(jsonrpc::INVALID_PARAMS, "bad");
    }
    const JsonValue written = tasks::toJson(task);
    const auto read = tasks::fromJson(written);
    ASSERT_TRUE(read.has_value());
    EXPECT_SAME_JSON(tasks::toJson(read.value()), written);
  }
}

TEST(Tasks, AMalformedTaskIsNoTask) {
  for (const char* bad :
       {R"({"status":"working"})", R"({"taskId":"","status":"working"})",
        R"({"taskId":"a","status":"pending"})",
        R"({"taskId":7,"status":"working"})", R"("task")"}) {
    SCOPED_TRACE(bad);
    EXPECT_FALSE(tasks::fromJson(JsonValue::parse(bad)).has_value());
  }
}

TEST(Tasks, ResultTypesTellATaskFromAnAnswer) {
  const auto task = taskIn(tasks::Status::Working);
  const JsonValue created = tasks::createTaskResult(task);
  EXPECT_EQ(created["resultType"].getString(), "task");
  EXPECT_TRUE(tasks::isTaskResult(created));
  const JsonValue polled = tasks::getTaskResult(task);
  EXPECT_EQ(polled["resultType"].getString(), "complete");
  EXPECT_FALSE(tasks::isTaskResult(polled));
  EXPECT_FALSE(tasks::isTaskResult(JsonValue::parse(R"({"content":[]})")));
}

TEST(Tasks, IdsAreUnguessableAndTimesAreUtc) {
  std::set<std::string> ids;
  for (int i = 0; i < 1000; ++i) {
    const std::string id = tasks::newTaskId();
    EXPECT_EQ(id.size(), 32u);
    EXPECT_EQ(id.find_first_not_of("0123456789abcdef"), std::string::npos);
    ids.insert(id);
  }
  EXPECT_EQ(ids.size(), 1000u);
  EXPECT_EQ(tasks::iso8601(std::chrono::system_clock::time_point(
                std::chrono::milliseconds(1764066600123))),
            "2025-11-25T10:30:00.123Z");
}

// ── The store ──────────────────────────────────────────────────────────

TEST(TaskStore, ATaskIsFoundOnlyByItsOwner) {
  auto store = storeWith();
  auto handle = store->create("caller:alice");
  ASSERT_TRUE(handle);
  const auto found = store->get("caller:alice", handle->id());
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, tasks::Status::Working);
  EXPECT_EQ(found->pollIntervalMs, mcp::make_optional<int64_t>(250));

  // Another caller's, and one that doesn't exist, are the same: nothing.
  EXPECT_FALSE(store->get("caller:mallory", handle->id()).has_value());
  EXPECT_FALSE(store->get("caller:alice", "no-such-task").has_value());
  EXPECT_FALSE(
      store->update("caller:mallory", handle->id(), JsonValue::object()));
  EXPECT_FALSE(store->cancel("caller:mallory", handle->id()));
  EXPECT_FALSE(store->owns("caller:mallory", handle->id()));
  EXPECT_FALSE(handle->isCancelled());
}

TEST(TaskStore, AnExpiredTaskIsGone) {
  auto store = storeWith(nullptr, std::chrono::milliseconds(20));
  auto handle = store->create("caller:alice");
  ASSERT_TRUE(store->get("caller:alice", handle->id()).has_value());
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  EXPECT_FALSE(store->get("caller:alice", handle->id()).has_value());
  store->sweep();
  EXPECT_EQ(store->size(), 0u);
}

TEST(TaskStore, OneCallerMayHoldOnlySoMany) {
  auto store = storeWith(nullptr, std::chrono::hours(1), 2);
  EXPECT_TRUE(store->create("caller:alice"));
  EXPECT_TRUE(store->create("caller:alice"));
  EXPECT_FALSE(store->create("caller:alice")) << "a third task was made";
  EXPECT_TRUE(store->create("caller:bob")) << "another caller was limited too";
}

TEST(TaskStore, AFinishedTaskNeverChanges) {
  std::vector<tasks::Task> changes;
  auto store = storeWith(&changes);
  auto handle = store->create("caller:alice");
  handle->complete(JsonValue::parse(R"({"content":[]})"));
  handle->fail(Error(jsonrpc::INTERNAL_ERROR, "too late"));
  handle->setStatusMessage("too late");
  EXPECT_FALSE(
      handle->askForInput(askName(), [](const TaskHandle::Answers&) {}));
  EXPECT_TRUE(store->cancel("caller:alice", handle->id()));

  const auto task = store->get("caller:alice", handle->id());
  EXPECT_EQ(task->status, tasks::Status::Completed);
  EXPECT_FALSE(task->statusMessage.has_value());
  EXPECT_FALSE(handle->isCancelled());
  EXPECT_EQ(changes.size(), 1u) << "a finished task was changed again";
}

TEST(TaskStore, InputIsAskedForAndAnswered) {
  std::vector<tasks::Task> changes;
  auto store = storeWith(&changes);
  auto handle = store->create("caller:alice");

  protocol::modern::InputRequests two = askName();
  two["age"] = two["name"];
  TaskHandle::Answers answered;
  int told = 0;
  ASSERT_TRUE(handle->askForInput(two, [&](const TaskHandle::Answers& a) {
    answered = a;
    ++told;
  }));
  auto task = store->get("caller:alice", handle->id());
  EXPECT_EQ(task->status, tasks::Status::InputRequired);
  EXPECT_TRUE(task->inputRequests.contains("name"));
  EXPECT_TRUE(task->inputRequests.contains("age"));

  // An answer to something not asked is ignored; a partial answer leaves
  // the rest asked for.
  EXPECT_TRUE(store->update(
      "caller:alice", handle->id(),
      JsonValue::parse(R"({"other":{},"name":{"action":"accept"}})")));
  task = store->get("caller:alice", handle->id());
  EXPECT_EQ(task->status, tasks::Status::InputRequired);
  EXPECT_FALSE(task->inputRequests.contains("name"));
  EXPECT_TRUE(task->inputRequests.contains("age"));

  // Answered again, the same key changes nothing.
  EXPECT_TRUE(
      store->update("caller:alice", handle->id(),
                    JsonValue::parse(R"({"name":{"action":"decline"}})")));
  EXPECT_TRUE(
      store->update("caller:alice", handle->id(),
                    JsonValue::parse(R"({"age":{"action":"accept"}})")));
  task = store->get("caller:alice", handle->id());
  EXPECT_EQ(task->status, tasks::Status::Working);
  EXPECT_FALSE(task->inputRequests.isObject());
  EXPECT_EQ(answered.count("age"), 1u);
  EXPECT_GE(told, 1);

  // A key names one request for a task's whole life.
  EXPECT_FALSE(handle->askForInput(askName(), [](const TaskHandle::Answers&) {
  })) << "a key was used twice";
}

TEST(TaskStore, CancellingTellsTheWork) {
  auto store = storeWith();
  auto handle = store->create("caller:alice");
  std::atomic<int> told{0};
  handle->onCancelled([&told]() { ++told; });
  ASSERT_TRUE(
      handle->askForInput(askName(), [](const TaskHandle::Answers&) {}));

  EXPECT_TRUE(store->cancel("caller:alice", handle->id()));
  EXPECT_TRUE(handle->isCancelled());
  EXPECT_EQ(told.load(), 1);
  const auto task = store->get("caller:alice", handle->id());
  EXPECT_EQ(task->status, tasks::Status::Cancelled);
  EXPECT_FALSE(task->inputRequests.isObject());

  // Cancelled again, nothing more; and work finishing afterwards is too late.
  EXPECT_TRUE(store->cancel("caller:alice", handle->id()));
  EXPECT_EQ(told.load(), 1);
  handle->complete(JsonValue::object());
  EXPECT_EQ(store->get("caller:alice", handle->id())->status,
            tasks::Status::Cancelled);

  // An observer arriving late is told at once.
  handle->onCancelled([&told]() { ++told; });
  EXPECT_EQ(told.load(), 2);
}

}  // namespace mcp
