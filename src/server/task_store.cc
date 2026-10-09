// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * The tasks a server is running. See the header.
 */

#include "mcp/server/task_store.h"

#include "mcp/json/json_serialization.h"
#include "mcp/protocol/modern_era.h"

namespace mcp {
namespace server {

namespace tasks = protocol::tasks;

// ── The handle ─────────────────────────────────────────────────────────

bool TaskHandle::isCancelled() const {
  auto store = store_.lock();
  return store && store->isCancelled(id_);
}

void TaskHandle::onCancelled(std::function<void()> observer) {
  if (auto store = store_.lock()) {
    store->onCancelled(id_, std::move(observer));
  }
}

void TaskHandle::setStatusMessage(const std::string& message) {
  if (auto store = store_.lock()) {
    store->setStatusMessage(id_, message);
  }
}

void TaskHandle::complete(const json::JsonValue& result) {
  if (auto store = store_.lock()) {
    store->finish(id_, tasks::Status::Completed, mcp::make_optional(result),
                  nullopt);
  }
}

void TaskHandle::complete(const CallToolResult& result) {
  complete(json::to_json(result));
}

void TaskHandle::fail(const Error& error) {
  if (auto store = store_.lock()) {
    store->finish(id_, tasks::Status::Failed, nullopt,
                  mcp::make_optional(error));
  }
}

bool TaskHandle::askForInput(const protocol::modern::InputRequests& requests,
                             std::function<void(const Answers&)> on_answered) {
  auto store = store_.lock();
  return store && store->askForInput(id_, requests, std::move(on_answered));
}

// ── The store ──────────────────────────────────────────────────────────

std::shared_ptr<TaskStore> TaskStore::make(Config config, Changed changed) {
  return std::shared_ptr<TaskStore>(new TaskStore(config, std::move(changed)));
}

std::shared_ptr<TaskHandle> TaskStore::create(const std::string& owner) {
  const auto now = std::chrono::system_clock::now();
  Entry entry;
  entry.owner = owner;
  entry.task.taskId = tasks::newTaskId();
  entry.task.status = tasks::Status::Working;
  entry.task.createdAt = tasks::iso8601(now);
  entry.task.lastUpdatedAt = entry.task.createdAt;
  if (config_.ttl.count() > 0) {
    entry.task.ttlMs =
        mcp::make_optional(static_cast<int64_t>(config_.ttl.count()));
    entry.expires = std::chrono::steady_clock::now() + config_.ttl;
  } else {
    entry.expires = std::chrono::steady_clock::time_point::max();
  }
  entry.task.pollIntervalMs =
      mcp::make_optional(static_cast<int64_t>(config_.poll_interval.count()));

  const std::string id = entry.task.taskId;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    sweepLocked();
    size_t held = 0;
    for (const auto& existing : entries_) {
      if (existing.second.owner == owner) {
        ++held;
      }
    }
    if (held >= config_.max_per_caller) {
      return nullptr;
    }
    // In the store before anyone is told it exists, so a tasks/get for it
    // finds it the moment its id is known.
    entries_[id] = std::move(entry);
  }
  return std::shared_ptr<TaskHandle>(new TaskHandle(shared_from_this(), id));
}

TaskStore::Entry* TaskStore::find(const std::string& owner,
                                  const std::string& id) {
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    return nullptr;
  }
  if (std::chrono::steady_clock::now() >= it->second.expires) {
    return nullptr;
  }
  // Another caller's task is answered as one that doesn't exist.
  return it->second.owner == owner ? &it->second : nullptr;
}

optional<tasks::Task> TaskStore::get(const std::string& owner,
                                     const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  Entry* entry = find(owner, id);
  return entry ? mcp::make_optional(entry->task) : nullopt;
}

bool TaskStore::owns(const std::string& owner, const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return find(owner, id) != nullptr;
}

bool TaskStore::update(const std::string& owner,
                       const std::string& id,
                       const json::JsonValue& input_responses) {
  std::function<void(const TaskHandle::Answers&)> on_answered;
  TaskHandle::Answers answers;
  optional<tasks::Task> changed;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry* entry = find(owner, id);
    if (!entry) {
      return false;
    }
    if (input_responses.isObject()) {
      for (const auto& key : input_responses.keys()) {
        // Only what is still being waited for; anything else is ignored.
        if (entry->outstanding.erase(key) != 0) {
          answers[key] = input_responses[key];
        }
      }
    }
    if (answers.empty()) {
      return true;
    }
    if (entry->outstanding.empty()) {
      on_answered = std::move(entry->on_answered);
      entry->on_answered = nullptr;
      // Back to work once nothing is left unanswered.
      if (entry->task.status == tasks::Status::InputRequired) {
        entry->task.status = tasks::Status::Working;
        entry->task.inputRequests = json::JsonValue();
        touch(*entry);
        changed = entry->task;
      }
    } else {
      on_answered = entry->on_answered;
      // What is still outstanding is all the task now asks for.
      json::JsonValue left = json::JsonValue::object();
      for (const auto& key : entry->outstanding) {
        if (entry->task.inputRequests.isObject() &&
            entry->task.inputRequests.contains(key)) {
          left.set(key, entry->task.inputRequests[key]);
        }
      }
      entry->task.inputRequests = left;
      touch(*entry);
      changed = entry->task;
    }
  }
  if (changed.has_value()) {
    tell(changed.value());
  }
  if (on_answered) {
    on_answered(answers);
  }
  return true;
}

bool TaskStore::cancel(const std::string& owner, const std::string& id) {
  std::vector<std::function<void()>> told;
  optional<tasks::Task> changed;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry* entry = find(owner, id);
    if (!entry) {
      return false;
    }
    if (entry->cancel_requested || tasks::isTerminal(entry->task.status)) {
      // Finished already, or already cancelled: nothing changes.
      return true;
    }
    entry->cancel_requested = true;
    entry->task.status = tasks::Status::Cancelled;
    entry->task.inputRequests = json::JsonValue();
    entry->outstanding.clear();
    entry->on_answered = nullptr;
    touch(*entry);
    changed = entry->task;
    told.swap(entry->on_cancelled);
  }
  tell(changed.value());
  for (auto& observer : told) {
    try {
      observer();
    } catch (...) {
    }
  }
  return true;
}

void TaskStore::sweep() {
  std::lock_guard<std::mutex> lock(mutex_);
  sweepLocked();
}

void TaskStore::sweepLocked() {
  const auto now = std::chrono::steady_clock::now();
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (now >= it->second.expires) {
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

size_t TaskStore::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

void TaskStore::touch(Entry& entry) {
  entry.task.lastUpdatedAt = tasks::iso8601(std::chrono::system_clock::now());
}

void TaskStore::tell(const tasks::Task& task) {
  if (!changed_) {
    return;
  }
  try {
    changed_(task);
  } catch (...) {
  }
}

bool TaskStore::isCancelled(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(id);
  return it != entries_.end() && it->second.cancel_requested;
}

void TaskStore::onCancelled(const std::string& id,
                            std::function<void()> observer) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(id);
    if (it != entries_.end() && !it->second.cancel_requested) {
      it->second.on_cancelled.push_back(std::move(observer));
      return;
    }
    if (it == entries_.end()) {
      return;
    }
  }
  observer();
}

void TaskStore::setStatusMessage(const std::string& id,
                                 const std::string& message) {
  optional<tasks::Task> changed;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(id);
    if (it == entries_.end() || tasks::isTerminal(it->second.task.status)) {
      return;
    }
    it->second.task.statusMessage = message;
    touch(it->second);
    changed = it->second.task;
  }
  tell(changed.value());
}

void TaskStore::finish(const std::string& id,
                       tasks::Status status,
                       const optional<json::JsonValue>& result,
                       const optional<Error>& error) {
  optional<tasks::Task> changed;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(id);
    // Once final, a task never changes: work that finishes after it was
    // cancelled is too late.
    if (it == entries_.end() || tasks::isTerminal(it->second.task.status)) {
      return;
    }
    Entry& entry = it->second;
    entry.task.status = status;
    entry.task.result = result;
    entry.task.error = error;
    entry.task.inputRequests = json::JsonValue();
    entry.outstanding.clear();
    entry.on_answered = nullptr;
    touch(entry);
    changed = entry.task;
  }
  tell(changed.value());
}

bool TaskStore::askForInput(
    const std::string& id,
    const protocol::modern::InputRequests& requests,
    std::function<void(const TaskHandle::Answers&)> on_answered) {
  if (requests.empty()) {
    return false;
  }
  optional<tasks::Task> changed;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(id);
    if (it == entries_.end() || tasks::isTerminal(it->second.task.status)) {
      return false;
    }
    Entry& entry = it->second;
    // One question at a time: until what was asked is answered, there is
    // one callback waiting, and a second would take the first one's
    // answers.
    if (!entry.outstanding.empty()) {
      return false;
    }
    // A key names one request for the whole of a task's life.
    for (const auto& request : requests) {
      if (entry.used_keys.count(request.first) != 0) {
        return false;
      }
    }
    protocol::modern::NeedsInput needed;
    needed.requests = requests;
    const json::JsonValue rendered =
        protocol::modern::renderInputRequired(needed);
    json::JsonValue asked = entry.task.inputRequests.isObject()
                                ? entry.task.inputRequests
                                : json::JsonValue::object();
    const auto& fresh = rendered[protocol::modern::kInputRequestsField];
    for (const auto& request : requests) {
      entry.used_keys.insert(request.first);
      entry.outstanding.insert(request.first);
      asked.set(request.first, fresh[request.first]);
    }
    entry.task.inputRequests = asked;
    entry.task.status = tasks::Status::InputRequired;
    entry.on_answered = std::move(on_answered);
    touch(entry);
    changed = entry.task;
  }
  tell(changed.value());
  return true;
}

}  // namespace server
}  // namespace mcp
