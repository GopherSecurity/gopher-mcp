// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * The tasks a server is running, for the Tasks extension.
 *
 * A task is made when a tool answers with one, and lives in the store
 * until its time is up. Every request about a task names it by an id no
 * one can guess, and is checked against the caller that made it: a task
 * that doesn't exist and one that belongs to someone else look the same,
 * so neither can be probed.
 *
 * The work behind a task holds a TaskHandle, which reports how it is
 * going, asks the client for input, and hears about cancellation. Each
 * change is told to the server, which passes it to whoever is listening.
 *
 * Safe from any thread.
 */

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "mcp/json/json_bridge.h"
#include "mcp/protocol/mrtr.h"
#include "mcp/protocol/tasks.h"
#include "mcp/types.h"

namespace mcp {
namespace server {

class TaskStore;

/**
 * What the work behind one task holds. Kept by the work for as long as it
 * runs; the task itself lives in the store.
 */
class TaskHandle {
 public:
  using Answers = std::map<std::string, json::JsonValue>;

  const std::string& id() const { return id_; }

  /** Whether the client asked for the task to be cancelled. */
  bool isCancelled() const;
  /** Told once when it is, at once if it already was. */
  void onCancelled(std::function<void()> observer);

  /** What is happening now, for people to read. */
  void setStatusMessage(const std::string& message);

  /** Done: the result the original request would have returned. */
  void complete(const json::JsonValue& result);
  void complete(const CallToolResult& result);
  /** Failed with a JSON-RPC error. A tool error is a completion. */
  void fail(const Error& error);

  /**
   * Ask the client for something: the task waits in input_required until
   * the client answers, and on_answered gets the answers to these keys.
   * A key may be used once in a task's life. False when a key was used
   * before, or the task has finished.
   */
  bool askForInput(const protocol::modern::InputRequests& requests,
                   std::function<void(const Answers&)> on_answered);

 private:
  friend class TaskStore;
  TaskHandle(std::weak_ptr<TaskStore> store, std::string id)
      : store_(std::move(store)), id_(std::move(id)) {}

  std::weak_ptr<TaskStore> store_;
  std::string id_;
};

class TaskStore : public std::enable_shared_from_this<TaskStore> {
 public:
  struct Config {
    // How long a task is kept from when it was made. Zero keeps it for ever.
    std::chrono::milliseconds ttl{std::chrono::hours(1)};
    // How often a client is asked to poll.
    std::chrono::milliseconds poll_interval{std::chrono::seconds(1)};
    // How many unexpired tasks one caller may hold.
    size_t max_per_caller = 100;
  };

  // Told every change to a task, outside the store's lock.
  using Changed = std::function<void(const protocol::tasks::Task&)>;

  static std::shared_ptr<TaskStore> make(Config config, Changed changed);

  /** A new task for this caller, or null when it holds too many already. */
  std::shared_ptr<TaskHandle> create(const std::string& owner);

  /** The task, if it exists, is this caller's, and hasn't expired. */
  optional<protocol::tasks::Task> get(const std::string& owner,
                                      const std::string& id);

  /**
   * Answers to what the task asked for. Keys not outstanding are ignored.
   * False when there is no such task of this caller's.
   */
  bool update(const std::string& owner,
              const std::string& id,
              const json::JsonValue& input_responses);

  /**
   * The client wants it stopped: an unfinished task is cancelled, and its
   * work told. False when there is no such task of this caller's.
   */
  bool cancel(const std::string& owner, const std::string& id);

  /** Whether a task of this caller's exists, for a subscription to it. */
  bool owns(const std::string& owner, const std::string& id);

  /** Forget tasks whose time is up. */
  void sweep();

  size_t size() const;

 private:
  friend class TaskHandle;

  struct Entry {
    std::string owner;
    protocol::tasks::Task task;
    std::chrono::steady_clock::time_point expires;
    bool cancel_requested{false};
    std::vector<std::function<void()>> on_cancelled;
    // Keys asked for in the task's life, and those still unanswered.
    std::set<std::string> used_keys;
    std::set<std::string> outstanding;
    std::function<void(const TaskHandle::Answers&)> on_answered;
  };

  TaskStore(Config config, Changed changed)
      : config_(config), changed_(std::move(changed)) {}

  // With the lock held. Null when gone, expired, or another caller's.
  Entry* find(const std::string& owner, const std::string& id);
  void sweepLocked();
  void touch(Entry& entry);
  void tell(const protocol::tasks::Task& task);

  // From a handle.
  bool isCancelled(const std::string& id);
  void onCancelled(const std::string& id, std::function<void()> observer);
  void setStatusMessage(const std::string& id, const std::string& message);
  void finish(const std::string& id,
              protocol::tasks::Status status,
              const optional<json::JsonValue>& result,
              const optional<Error>& error);
  bool askForInput(const std::string& id,
                   const protocol::modern::InputRequests& requests,
                   std::function<void(const TaskHandle::Answers&)> on_answered);

  Config config_;
  Changed changed_;
  mutable std::mutex mutex_;
  std::map<std::string, Entry> entries_;
};

}  // namespace server
}  // namespace mcp
