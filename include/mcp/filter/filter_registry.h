#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mcp/core/compat.h"
#include "mcp/json/json_bridge.h"
#include "mcp/network/filter.h"
#include "mcp/filter/filter_context.h"

namespace mcp {
namespace filter {

// Forward declarations
class FilterFactory;
using FilterFactoryPtr = std::shared_ptr<FilterFactory>;

// Context-aware filter factory function type
using ContextAwareFilterFactory = std::function<network::FilterSharedPtr(
    const FilterCreationContext&, const json::JsonValue&)>;

/**
 * Factory metadata for tracking versioning and dependencies
 */
struct FilterFactoryMetadata {
  std::string name;                      // Factory name/type
  std::string version;                    // Factory version (semver format)
  std::vector<std::string> dependencies; // List of required dependencies
  json::JsonValue config_schema;         // JSON schema for configuration validation
  std::string description;               // Human-readable description
  
  FilterFactoryMetadata()
      : config_schema(json::JsonValue::object()) {}
};

/**
 * Abstract base class for filter factories
 * 
 * All filter implementations should create a factory that inherits from this class
 * and registers itself using the REGISTER_FILTER_FACTORY macro.
 */
class FilterFactory {
 public:
  virtual ~FilterFactory() = default;

  /**
   * Create a filter instance with the given configuration
   * 
   * @param config Configuration as JsonValue
   * @return Shared pointer to the created filter
   * @throws std::runtime_error if configuration is invalid
   */
  virtual network::FilterSharedPtr createFilter(
      const json::JsonValue& config) const = 0;

  /**
   * Get the factory metadata
   * 
   * @return Factory metadata including version, dependencies, and schema
   */
  virtual const FilterFactoryMetadata& getMetadata() const = 0;

  /**
   * Get the default configuration for this filter
   * 
   * @return Default configuration as JsonValue
   */
  virtual json::JsonValue getDefaultConfig() const {
    return json::JsonValue::object();
  }

  /**
   * Validate configuration against the factory's schema
   * 
   * @param config Configuration to validate
   * @return true if valid, false otherwise
   */
  virtual bool validateConfig(const json::JsonValue& config) const {
    // Default implementation: accept any config
    // Derived classes should override for proper validation
    return true;
  }
};

/**
 * Filter Registry singleton
 * 
 * Manages registration and creation of filters through their factories.
 * Thread-safe for concurrent access.
 */
class FilterRegistry {
 public:
  /**
   * Get the singleton instance
   */
  static FilterRegistry& instance();

  /**
   * Register a filter factory
   * 
   * @param name Filter type name
   * @param factory Factory instance
   * @return true if registered successfully, false if name already exists
   */
  bool registerFactory(const std::string& name, FilterFactoryPtr factory);

  /**
   * Create a filter by name
   * 
   * @param name Filter type name
   * @param config Configuration for the filter
   * @return Created filter instance
   * @throws std::runtime_error if filter type is unknown or creation fails
   */
  network::FilterSharedPtr createFilter(const std::string& name,
                                       const json::JsonValue& config) const;

  /**
   * Get a factory by name
   * 
   * @param name Filter type name
   * @return Factory instance or nullptr if not found
   */
  FilterFactoryPtr getFactory(const std::string& name) const;

  /**
   * List all registered factory names
   * 
   * @return Vector of registered factory names
   */
  std::vector<std::string> listFactories() const;

  /**
   * Get the count of registered factories
   * 
   * @return Number of registered factories
   */
  size_t getFactoryCount() const;

  /**
   * Check if a factory is registered
   * 
   * @param name Filter type name
   * @return true if registered, false otherwise
   */
  bool hasFactory(const std::string& name) const;

  /**
   * Clear all registered factories (mainly for testing)
   */
  void clearFactories();

  // Context-aware filter factory methods

  /**
   * Register a context-aware filter factory with basic metadata
   *
   * @param name Filter type name
   * @param factory Context-aware factory function
   * @param metadata Basic filter metadata
   * @return true if registered successfully, false if name already exists
   */
  bool registerContextFactory(const std::string& name,
                             ContextAwareFilterFactory factory,
                             const BasicFilterMetadata& metadata);

  /**
   * Create a filter with context
   *
   * @param name Filter type name
   * @param context Filter creation context
   * @param config Configuration for the filter
   * @return Created filter instance
   * @throws std::runtime_error if filter type is unknown or creation fails
   */
  network::FilterSharedPtr createFilterWithContext(
      const std::string& name,
      const FilterCreationContext& context,
      const json::JsonValue& config) const;

  /**
   * Check if a context-aware factory is registered
   *
   * @param name Filter type name
   * @return true if registered, false otherwise
   */
  bool hasContextFactory(const std::string& name) const;

  /**
   * Get basic metadata for a filter
   *
   * @param name Filter type name
   * @return Pointer to metadata or nullptr if not found
   */
  const BasicFilterMetadata* getBasicMetadata(const std::string& name) const;

  /**
   * List all registered context-aware factories
   *
   * @return Vector of registered factory names
   */
  std::vector<std::string> listContextFactories() const;

  /**
   * Validate a basic filter chain
   *
   * @param filter_names List of filter names in order
   * @return true if all filters exist and chain is valid
   */
  bool validateBasicFilterChain(const std::vector<std::string>& filter_names) const;

 private:
  // Private constructor for singleton
  FilterRegistry();
  ~FilterRegistry() = default;

  // Delete copy and move operations
  FilterRegistry(const FilterRegistry&) = delete;
  FilterRegistry& operator=(const FilterRegistry&) = delete;
  FilterRegistry(FilterRegistry&&) = delete;
  FilterRegistry& operator=(FilterRegistry&&) = delete;

  // Thread-safe factory storage
  mutable std::mutex mutex_;
  std::map<std::string, FilterFactoryPtr> factories_;

  // Context-aware factory storage
  std::map<std::string, ContextAwareFilterFactory> context_factories_;
  std::map<std::string, BasicFilterMetadata> basic_metadata_;

  // Initialization flag
  std::atomic<bool> initialized_{false};

  // Initialize registry (called once)
  void initialize();
};

/**
 * Base class for self-registering filter factories
 * 
 * This template simplifies the creation of self-registering factories.
 */
template <typename FactoryType>
class SelfRegisteringFilterFactory {
 public:
  SelfRegisteringFilterFactory(const std::string& name) {
    auto factory = std::make_shared<FactoryType>();
    FilterRegistry::instance().registerFactory(name, factory);
  }
};

/**
 * Macro for registering filter factories
 * 
 * Usage:
 *   REGISTER_FILTER_FACTORY(MyFilterFactory, "my_filter")
 * 
 * This creates a static initializer that registers the factory at startup.
 */
#define REGISTER_FILTER_FACTORY(FactoryClass, FilterName)                    \
  namespace {                                                                \
  static struct FactoryClass##_Registrar {                                  \
    FactoryClass##_Registrar() {                                           \
      auto factory = std::make_shared<FactoryClass>();                     \
      mcp::filter::FilterRegistry::instance().registerFactory(FilterName,   \
                                                             factory);      \
    }                                                                       \
  } FactoryClass##_registrar_instance;                                     \
  }

/**
 * Helper macro for creating filter factory classes
 * 
 * This macro reduces boilerplate when creating new filter factories.
 */
#define DECLARE_FILTER_FACTORY(FactoryClass, FilterClass)                    \
  class FactoryClass : public mcp::filter::FilterFactory {                  \
   public:                                                                  \
    mcp::network::FilterSharedPtr createFilter(                            \
        const mcp::json::JsonValue& config) const override;                \
    const mcp::filter::FilterFactoryMetadata& getMetadata() const override; \
   private:                                                                 \
    static mcp::filter::FilterFactoryMetadata metadata_;                   \
  };

}  // namespace filter
}  // namespace mcp