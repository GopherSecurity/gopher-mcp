#include "mcp/filter/filter_registry.h"

#include <algorithm>
#include <sstream>

#include "mcp/json/json_serialization.h"
#include "mcp/logging/log_macros.h"

// Set the logging component for this file
#define GOPHER_LOG_COMPONENT "config.registry"

namespace mcp {
namespace filter {

// FilterRegistry implementation

FilterRegistry::FilterRegistry() {
  initialize();
}

FilterRegistry& FilterRegistry::instance() {
  static FilterRegistry instance;
  return instance;
}

void FilterRegistry::initialize() {
  bool expected = false;
  if (initialized_.compare_exchange_strong(expected, true)) {
    GOPHER_LOG(Info, "Filter registry initialized");
  }
}

bool FilterRegistry::registerFactory(const std::string& name,
                                    FilterFactoryPtr factory) {
  if (!factory) {
    GOPHER_LOG(Error, "Attempted to register null factory for filter: {}", name);
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  
  // Check for duplicate registration
  auto it = factories_.find(name);
  if (it != factories_.end()) {
    GOPHER_LOG(Warning, "Filter factory '{}' already registered - ignoring duplicate", 
               name);
    return false;
  }

  // Register the factory
  factories_[name] = factory;
  
  // Get metadata for logging
  const auto& metadata = factory->getMetadata();
  GOPHER_LOG(Info, "Registered filter factory '{}' version {} (total: {})",
             name, metadata.version, factories_.size());

  // Log DEBUG info about all registered factories (bounded output)
  if (factories_.size() <= 20) {
    std::stringstream ss;
    ss << "Registered factories: [";
    bool first = true;
    for (const auto& pair : factories_) {
      if (!first) ss << ", ";
      ss << pair.first;
      first = false;
    }
    ss << "]";
    GOPHER_LOG(Debug, "{}", ss.str());
  } else {
    // For large lists, just show count and first few
    std::stringstream ss;
    ss << "Registered " << factories_.size() << " factories. First 10: [";
    int count = 0;
    for (const auto& pair : factories_) {
      if (count > 0) ss << ", ";
      ss << pair.first;
      if (++count >= 10) {
        ss << ", ...";
        break;
      }
    }
    ss << "]";
    GOPHER_LOG(Debug, "{}", ss.str());
  }

  return true;
}

network::FilterSharedPtr FilterRegistry::createFilter(
    const std::string& name,
    const json::JsonValue& config) const {
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = factories_.find(name);
  if (it == factories_.end()) {
    GOPHER_LOG(Error, "Unknown filter type '{}' requested", name);
    throw std::runtime_error("Unknown filter type: " + name);
  }

  FilterFactoryPtr factory = it->second;
  
  // Validate configuration if the factory supports it
  if (!factory->validateConfig(config)) {
    GOPHER_LOG(Error, "Invalid configuration for filter '{}'", name);
    throw std::runtime_error("Invalid configuration for filter: " + name);
  }

  try {
    GOPHER_LOG(Debug, "Creating filter instance of type '{}'", name);
    auto filter = factory->createFilter(config);
    
    if (!filter) {
      GOPHER_LOG(Error, "Factory for '{}' returned null filter", name);
      throw std::runtime_error("Failed to create filter: " + name);
    }
    
    GOPHER_LOG(Debug, "Successfully created filter instance of type '{}'", name);
    return filter;
    
  } catch (const std::exception& e) {
    GOPHER_LOG(Error, "Failed to create filter '{}': {}", name, e.what());
    throw std::runtime_error("Failed to create filter '" + name + "': " + e.what());
  }
}

FilterFactoryPtr FilterRegistry::getFactory(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = factories_.find(name);
  if (it != factories_.end()) {
    return it->second;
  }
  
  GOPHER_LOG(Debug, "Factory '{}' not found in registry", name);
  return nullptr;
}

std::vector<std::string> FilterRegistry::listFactories() const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  std::vector<std::string> names;
  names.reserve(factories_.size());
  
  for (const auto& pair : factories_) {
    names.push_back(pair.first);
  }
  
  // Sort for consistent ordering
  std::sort(names.begin(), names.end());
  
  return names;
}

size_t FilterRegistry::getFactoryCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.size();
}

bool FilterRegistry::hasFactory(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.find(name) != factories_.end();
}

void FilterRegistry::clearFactories() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  size_t count = factories_.size();
  factories_.clear();
  
  GOPHER_LOG(Info, "Cleared {} filter factories from registry", count);
}

}  // namespace filter
}  // namespace mcp