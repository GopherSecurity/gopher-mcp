#define GOPHER_LOG_COMPONENT "config.merge"

#include <algorithm>
#include <set>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "mcp/json/json_bridge.h"
#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

namespace mcp {
namespace config {

// Local adapter functions for JSON conversion
namespace {

// Convert from nlohmann::json to mcp::json::JsonValue
mcp::json::JsonValue fromNlohmann(const nlohmann::json& nj) {
  if (nj.is_null()) {
    return mcp::json::JsonValue::null();
  } else if (nj.is_boolean()) {
    return mcp::json::JsonValue(nj.get<bool>());
  } else if (nj.is_number_integer()) {
    return mcp::json::JsonValue(nj.get<int64_t>());
  } else if (nj.is_number_float()) {
    return mcp::json::JsonValue(nj.get<double>());
  } else if (nj.is_string()) {
    return mcp::json::JsonValue(nj.get<std::string>());
  } else if (nj.is_array()) {
    mcp::json::JsonValue arr = mcp::json::JsonValue::array();
    for (const auto& item : nj) {
      arr.push_back(fromNlohmann(item));
    }
    return arr;
  } else if (nj.is_object()) {
    mcp::json::JsonValue obj = mcp::json::JsonValue::object();
    for (auto it = nj.begin(); it != nj.end(); ++it) {
      obj[it.key()] = fromNlohmann(it.value());
    }
    return obj;
  }
  return mcp::json::JsonValue::null();
}

// Convert from mcp::json::JsonValue to nlohmann::json
nlohmann::json toNlohmann(const mcp::json::JsonValue& jv) {
  if (jv.isNull()) {
    return nullptr;
  } else if (jv.isBool()) {
    return jv.getBool();
  } else if (jv.isInt()) {
    return jv.getInt();
  } else if (jv.isDouble()) {
    return jv.getDouble();
  } else if (jv.isString()) {
    return jv.getString();
  } else if (jv.isArray()) {
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < jv.size(); ++i) {
      arr.push_back(toNlohmann(jv[i]));
    }
    return arr;
  } else if (jv.isObject()) {
    nlohmann::json obj = nlohmann::json::object();
    for (const auto& key : jv.getMemberNames()) {
      obj[key] = toNlohmann(jv[key]);
    }
    return obj;
  }
  return nullptr;
}

}  // namespace

// Configuration merger class
class ConfigMerger {
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

  ConfigMerger() = default;

  // Main merge function using JsonValue
  mcp::json::JsonValue merge(
      const std::vector<std::pair<std::string, mcp::json::JsonValue>>& sources,
      const std::string& snapshot_id = "",
      const std::string& version_id = "") {
    
    MergeContext context;
    context.snapshot_id = snapshot_id;
    context.version_id = version_id;
    
    // Log source layering order
    for (const auto& [source_name, _] : sources) {
      context.source_order.push_back(source_name);
    }
    
    LOG_INFO() << "Starting configuration merge:"
               << " sources=" << sources.size()
               << " order=[" << joinStrings(context.source_order, ", ") << "]"
               << (snapshot_id.empty() ? "" : " snapshot_id=" + snapshot_id)
               << (version_id.empty() ? "" : " version_id=" + version_id);
    
    if (sources.empty()) {
      LOG_WARNING() << "No configuration sources provided for merge";
      return mcp::json::JsonValue::object();
    }
    
    // Start with the base configuration
    mcp::json::JsonValue result = sources[0].second;
    
    // Apply overlays in order
    for (size_t i = 1; i < sources.size(); ++i) {
      const auto& [source_name, overlay] = sources[i];
      context.overlay_count++;
      
      LOG_DEBUG() << "Applying overlay: " << source_name 
                  << " (overlay " << context.overlay_count << ")";
      
      result = mergeObjects(result, overlay, context);
    }
    
    // Log merge summary
    LOG_INFO() << "Configuration merge completed:"
               << " overlays_applied=" << context.overlay_count
               << " conflicts_resolved=" << context.conflicts_resolved.size();
    
    if (!context.conflicts_resolved.empty() && 
        context.conflicts_resolved.size() <= 10) {
      LOG_DEBUG() << "Conflicts resolved for keys: [" 
                  << joinStrings(context.conflicts_resolved, ", ") << "]";
    } else if (context.conflicts_resolved.size() > 10) {
      LOG_DEBUG() << "Conflicts resolved for " 
                  << context.conflicts_resolved.size() 
                  << " keys (showing first 10): ["
                  << joinStrings(std::vector<std::string>(
                        context.conflicts_resolved.begin(),
                        std::next(context.conflicts_resolved.begin(), 10)), ", ")
                  << ", ...]";
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
      LOG_ERROR() << "Maximum merge depth exceeded at path: " << path;
      throw std::runtime_error("Configuration merge depth limit exceeded");
    }
    
    // Handle non-object cases
    if (!base.isObject() || !overlay.isObject()) {
      if (base != overlay) {
        context.conflicts_resolved.insert(path.empty() ? "root" : path);
      }
      context.merge_depth--;
      return overlay;  // Overlay wins for non-objects
    }
    
    mcp::json::JsonValue result = base;
    
    // Merge each key from overlay
    for (const auto& key : overlay.getMemberNames()) {
      std::string current_path = path.empty() ? key : path + "." + key;
      
      if (!base.isMember(key)) {
        // New key from overlay
        result[key] = overlay[key];
      } else {
        // Key exists in both - need to merge
        const auto& base_value = base[key];
        const auto& overlay_value = overlay[key];
        
        // Determine merge strategy based on key category
        if (isFilterList(key)) {
          // Filter lists: replace by default
          if (base_value != overlay_value) {
            context.conflicts_resolved.insert(current_path);
          }
          result[key] = overlay_value;
        } else if (isNamedResourceArray(key) && 
                   base_value.isArray() && overlay_value.isArray()) {
          // Named resources: merge by name
          result[key] = mergeNamedResourceArrays(
              base_value, overlay_value, context, current_path);
        } else if (base_value.isObject() && overlay_value.isObject()) {
          // Nested objects: deep merge
          result[key] = mergeObjects(
              base_value, overlay_value, context, current_path);
        } else if (base_value.isArray() && overlay_value.isArray()) {
          // Other arrays: replace by default
          if (base_value != overlay_value) {
            context.conflicts_resolved.insert(current_path);
          }
          result[key] = overlay_value;
        } else {
          // Different types or primitives: overlay wins
          if (base_value != overlay_value) {
            context.conflicts_resolved.insert(current_path);
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
      
      if (!base_item.isObject() || !base_item.isMember("name")) {
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
            overlay_item.isMember("name") &&
            overlay_item["name"].getString() == name) {
          // Found matching named resource - merge them
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
      
      if (!overlay_item.isObject() || !overlay_item.isMember("name")) {
        // Not a named resource, append
        result.push_back(overlay_item);
        continue;
      }
      
      std::string name = overlay_item["name"].getString();
      if (processed_names.find(name) == processed_names.end()) {
        // New named resource from overlay
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

// Factory function for creating a merger
std::unique_ptr<ConfigMerger> createConfigMerger() {
  return std::make_unique<ConfigMerger>();
}

}  // namespace config
}  // namespace mcp