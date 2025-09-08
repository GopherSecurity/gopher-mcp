/**
 * @file config_merger.h
 * @brief Public interface for configuration merger utilities
 */

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mcp/json/json_bridge.h"

namespace mcp {
namespace config {

// Forward-declared, fully defined in src/config/config_merger.cc
class ConfigMerger {
 public:
  ConfigMerger();
  ~ConfigMerger();

  // Merge layered configuration sources into a single JsonValue.
  // sources: vector of (source_name, JsonValue) pairs in layering order
  // snapshot_id, version_id: optional identifiers for logging/trace
  mcp::json::JsonValue merge(
      const std::vector<std::pair<std::string, mcp::json::JsonValue>>& sources,
      const std::string& snapshot_id = "",
      const std::string& version_id = "");

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Factory function to create a merger instance
std::unique_ptr<ConfigMerger> createConfigMerger();

}  // namespace config
}  // namespace mcp
