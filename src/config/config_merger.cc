#define GOPHER_LOG_COMPONENT "config.merge"

#include <algorithm>
#include <set>
#include <sstream>
#include <vector>

#include "mcp/config/config_merger.h"
#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace config {

// Configuration merging now uses JsonValue directly

// Internal implementation hidden behind pimpl
struct ConfigMerger::Impl {
 public:
  enum class ArrayMergeStrategy {
    REPLACE,        // Default: replace entire array
    MERGE_BY_NAME,  // Merge objects with 'name' field
    APPEND,         // Append arrays
    UNIQUE_APPEND   // Append only unique values
  };

  struct MergeContext {
    std::vector<std::string> source_order;
    std::set<std::string> conflicts_resolved;
    size_t overlay_count = 0;
    std::string snapshot_id;
    std::string version_id;
    int merge_depth = 0;
    static constexpr int MAX_MERGE_DEPTH = 100;
  };

  Impl() = default;

  // Main merge function using JsonValue
  mcp::json::JsonValue merge(
      const std::vector<std::pair<std::string, mcp::json::JsonValue>>& sources,
      const std::string& snapshot_id = "",
      const std::string& version_id = "") {
    
    MergeContext context;
    context.snapshot_id = snapshot_id;
    context.version_id = version_id;
    
    // Log source layering order
    for (const auto& pair : sources) {
      context.source_order.push_back(pair.first);
    }
    
    std::string order_str = joinStrings(context.source_order, ", ");
    LOG_INFO("Starting configuration merge: sources=%zu order=[%s]%s%s",
             sources.size(), order_str.c_str(),
             snapshot_id.empty() ? "" : (" snapshot_id=" + snapshot_id).c_str(),
             version_id.empty() ? "" : (" version_id=" + version_id).c_str());
    
    if (sources.empty()) {
      LOG_WARNING("No configuration sources provided for merge");
      return mcp::json::JsonValue::object();
    }
    
    // Start with the base configuration
    mcp::json::JsonValue result = sources[0].second;
    
    // Apply overlays in order
    for (size_t i = 1; i < sources.size(); ++i) {
      const auto& [source_name, overlay] = sources[i];
      context.overlay_count++;
      
      LOG_DEBUG("Applying overlay: %s (overlay %zu)", 
                source_name.c_str(), context.overlay_count);
      
      result = mergeObjects(result, overlay, context);
    }
    
    // Log merge summary
    LOG_INFO("Configuration merge completed: overlays_applied=%zu conflicts_resolved=%zu",
             context.overlay_count, context.conflicts_resolved.size());
    
    if (!context.conflicts_resolved.empty() && 
        context.conflicts_resolved.size() <= 10) {
      std::string keys = joinStrings(context.conflicts_resolved, ", ");
      LOG_DEBUG("Conflicts resolved for keys: [%s]", keys.c_str());
    } else if (context.conflicts_resolved.size() > 10) {
      std::vector<std::string> first_ten(
          context.conflicts_resolved.begin(),
          std::next(context.conflicts_resolved.begin(), 10));
      std::string keys = joinStrings(first_ten, ", ");
      LOG_DEBUG("Conflicts resolved for %zu keys (showing first 10): [%s, ...]", 
                context.conflicts_resolved.size(), keys.c_str());
    }
    
    return result;
  }

 private:
  mcp::json::JsonValue mergeObjects(const mcp::json::JsonValue& base,
                                    const mcp::json::JsonValue& overlay,
                                    MergeContext& context,
                                    const std::string& path = "") {
    // Check recursion depth
    if (++context.merge_depth > context.MAX_MERGE_DEPTH) {
      LOG_ERROR("Maximum merge depth exceeded at path: %s", path.c_str());
      throw std::runtime_error("Configuration merge depth limit exceeded");
    }
    
    // Handle non-object cases
    if (!base.isObject() || !overlay.isObject()) {
      // JsonValue doesn't support != directly, use string comparison as fallback
      if (base.toString() != overlay.toString()) {
        context.conflicts_resolved.insert(path.empty() ? "root" : path);
      }
      context.merge_depth--;
      return overlay;  // Overlay wins for non-objects
    }
    
    mcp::json::JsonValue result = base;
    
    // Merge each key from overlay
    for (const auto& key : overlay.keys()) {
      std::string current_path = path.empty() ? key : path + "." + key;
      
      if (!base.contains(key)) {
        // New key from overlay
        result[key] = overlay[key];
      } else {
        // Key exists in both - need to merge
        const auto& base_value = base[key];
        const auto& overlay_value = overlay[key];
        
        // Determine merge strategy based on key category
        if (isFilterList(key)) {
          // Filter lists: replace by default
          LOG_DEBUG("Category chosen for '%s': REPLACE (filter list)", 
                    current_path.c_str());
          if (base_value.toString() != overlay_value.toString()) {
            context.conflicts_resolved.insert(current_path);
            LOG_DEBUG("Conflict detected at '%s': replacing entire array", 
                      current_path.c_str());
          }
          result[key] = overlay_value;
        } else if (isNamedResourceArray(key) && 
                   base_value.isArray() && overlay_value.isArray()) {
          // Named resources: merge by name
          LOG_DEBUG("Category chosen for '%s': MERGE-BY-NAME (named resource array)", 
                    current_path.c_str());
          result[key] = mergeNamedResourceArrays(
              base_value, overlay_value, context, current_path);
        } else if (base_value.isObject() && overlay_value.isObject()) {
          // Nested objects: deep merge
          LOG_DEBUG("Category chosen for '%s': DEEP-MERGE (nested objects)", 
                    current_path.c_str());
          result[key] = mergeObjects(
              base_value, overlay_value, context, current_path);
        } else if (base_value.isArray() && overlay_value.isArray()) {
          // Other arrays: replace by default
          LOG_DEBUG("Category chosen for '%s': REPLACE (default array behavior)", 
                    current_path.c_str());
          if (base_value.toString() != overlay_value.toString()) {
            context.conflicts_resolved.insert(current_path);
            LOG_DEBUG("Conflict detected at '%s': replacing array", 
                      current_path.c_str());
          }
          result[key] = overlay_value;
        } else {
          // Different types or primitives: overlay wins
          LOG_DEBUG("Category chosen for '%s': OVERRIDE (scalar/type mismatch)", 
                    current_path.c_str());
          if (base_value.toString() != overlay_value.toString()) {
            context.conflicts_resolved.insert(current_path);
            LOG_DEBUG("Conflict detected at '%s': overlay value wins", 
                      current_path.c_str());
          }
          result[key] = overlay_value;
        }
      }
    }
    
    context.merge_depth--;
    return result;
  }
  
  mcp::json::JsonValue mergeNamedResourceArrays(
      const mcp::json::JsonValue& base_array,
      const mcp::json::JsonValue& overlay_array,
      MergeContext& context,
      const std::string& path) {
    
    mcp::json::JsonValue result = mcp::json::JsonValue::array();
    std::set<std::string> processed_names;
    
    // First pass: process all items from base
    for (size_t i = 0; i < base_array.size(); ++i) {
      const auto& base_item = base_array[i];
      
      if (!base_item.isObject() || !base_item.contains("name")) {
        // Not a named resource, keep as-is
        result.push_back(base_item);
        continue;
      }
      
      std::string name = base_item["name"].getString();
      processed_names.insert(name);
      
      // Look for matching item in overlay
      bool found = false;
      for (size_t j = 0; j < overlay_array.size(); ++j) {
        const auto& overlay_item = overlay_array[j];
        if (overlay_item.isObject() && 
            overlay_item.contains("name") &&
            overlay_item["name"].getString() == name) {
          // Found matching named resource - merge them
          LOG_DEBUG("Merging named resource '%s' at %s", 
                    name.c_str(), path.c_str());
          result.push_back(mergeObjects(
              base_item, overlay_item, context, 
              path + "[name=" + name + "]"));
          found = true;
          break;
        }
      }
      
      if (!found) {
        // No override for this named resource
        result.push_back(base_item);
      }
    }
    
    // Second pass: add new items from overlay
    for (size_t i = 0; i < overlay_array.size(); ++i) {
      const auto& overlay_item = overlay_array[i];
      
      if (!overlay_item.isObject() || !overlay_item.contains("name")) {
        // Not a named resource, append
        result.push_back(overlay_item);
        continue;
      }
      
      std::string name = overlay_item["name"].getString();
      if (processed_names.find(name) == processed_names.end()) {
        // New named resource from overlay
        LOG_DEBUG("Adding new named resource '%s' from overlay at %s", 
                  name.c_str(), path.c_str());
        result.push_back(overlay_item);
      }
    }
    
    if (base_array.size() != result.size() || overlay_array.size() != result.size()) {
      context.conflicts_resolved.insert(path);
    }
    
    return result;
  }
  
  bool isFilterList(const std::string& key) {
    // Identify filter list keys
    static const std::set<std::string> filter_keys = {
        "filters", "filter_chain", "http_filters", 
        "network_filters", "listener_filters"
    };
    return filter_keys.find(key) != filter_keys.end();
  }
  
  bool isNamedResourceArray(const std::string& key) {
    // Identify named resource arrays
    static const std::set<std::string> named_resource_keys = {
        "listeners", "clusters", "routes", "endpoints",
        "upstreams", "services", "resources", "pools"
    };
    return named_resource_keys.find(key) != named_resource_keys.end();
  }
  
  std::string joinStrings(const std::vector<std::string>& strings, 
                          const std::string& delimiter) {
    std::ostringstream oss;
    for (size_t i = 0; i < strings.size(); ++i) {
      if (i > 0) oss << delimiter;
      oss << strings[i];
    }
    return oss.str();
  }
  
  std::string joinStrings(const std::set<std::string>& strings, 
                          const std::string& delimiter) {
    return joinStrings(std::vector<std::string>(strings.begin(), strings.end()), 
                      delimiter);
  }
};

// Public facade methods
ConfigMerger::ConfigMerger() : impl_(new Impl()) {}
ConfigMerger::~ConfigMerger() = default;
mcp::json::JsonValue ConfigMerger::merge(
    const std::vector<std::pair<std::string, mcp::json::JsonValue>>& sources,
    const std::string& snapshot_id, const std::string& version_id) {
  return impl_->merge(sources, snapshot_id, version_id);
}

// Factory function for creating a merger
std::unique_ptr<ConfigMerger> createConfigMerger() {
  return std::make_unique<ConfigMerger>();
}

}  // namespace config
}  // namespace mcp
