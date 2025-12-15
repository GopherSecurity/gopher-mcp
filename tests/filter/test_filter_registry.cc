/**
 * @file test_filter_registry.cc
 * @brief Unit tests for Filter Registry
 */

#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mcp/filter/filter_registry.h"
#include "mcp/json/json_bridge.h"
#include "mcp/network/filter.h"

using namespace mcp;
using namespace mcp::filter;
using namespace mcp::network;
using namespace testing;

namespace {

// Mock filter for testing
class MockFilter : public NetworkFilterBase {
 public:
  MockFilter(const std::string& name = "mock") : name_(name) {}

  FilterStatus onData(Buffer& data, bool end_stream) override {
    return FilterStatus::Continue;
  }

  FilterStatus onNewConnection() override { return FilterStatus::Continue; }

  FilterStatus onWrite(Buffer& data, bool end_stream) override {
    return FilterStatus::Continue;
  }

  const std::string& getName() const { return name_; }

 private:
  std::string name_;
};

// Test filter factory
class TestFilterFactory : public FilterFactory {
 public:
  TestFilterFactory(const std::string& version = "1.0.0",
                    bool should_validate = true)
      : should_validate_(should_validate) {
    metadata_.name = "test_filter";
    metadata_.version = version;
    metadata_.description = "Test filter for unit tests";
    metadata_.dependencies = {"dep1", "dep2"};

    // Simple schema
    metadata_.config_schema =
        json::JsonObjectBuilder()
            .add("type", "object")
            .add(
                "properties",
                json::JsonObjectBuilder()
                    .add("enabled", json::JsonObjectBuilder()
                                        .add("type", "boolean")
                                        .build())
                    .add(
                        "name",
                        json::JsonObjectBuilder().add("type", "string").build())
                    .build())
            .build();
  }

  network::FilterSharedPtr createFilter(
      const json::JsonValue& config) const override {
    create_count_++;

    // Extract name from config if present
    std::string name = "test";
    if (config.contains("name")) {
      name = config["name"].getString();
    }

    // Check if we should throw an error
    if (config.contains("throw_error") && config["throw_error"].getBool()) {
      throw std::runtime_error("Intentional error for testing");
    }

    return std::make_shared<MockFilter>(name);
  }

  const FilterFactoryMetadata& getMetadata() const override {
    return metadata_;
  }

  json::JsonValue getDefaultConfig() const override {
    return json::JsonObjectBuilder()
        .add("enabled", true)
        .add("name", "default")
        .build();
  }

  bool validateConfig(const json::JsonValue& config) const override {
    if (!should_validate_) {
      return false;
    }

    // Simple validation: check if it's an object
    return config.isObject();
  }

  // Test helper to get creation count
  static int getCreateCount() { return create_count_; }
  static void resetCreateCount() { create_count_ = 0; }

 private:
  FilterFactoryMetadata metadata_;
  bool should_validate_;
  static int create_count_;
};

int TestFilterFactory::create_count_ = 0;

// Another test factory for testing multiple registrations
class AnotherTestFilterFactory : public FilterFactory {
 public:
  AnotherTestFilterFactory() {
    metadata_.name = "another_filter";
    metadata_.version = "2.0.0";
    metadata_.description = "Another test filter";
  }

  network::FilterSharedPtr createFilter(
      const json::JsonValue& config) const override {
    return std::make_shared<MockFilter>("another");
  }

  const FilterFactoryMetadata& getMetadata() const override {
    return metadata_;
  }

 private:
  FilterFactoryMetadata metadata_;
};

class FilterRegistryTest : public Test {
 protected:
  void SetUp() override {
    // Clear any existing factories before each test
    FilterRegistry::instance().clearFactories();
    TestFilterFactory::resetCreateCount();
  }

  void TearDown() override {
    // Clean up after test
    FilterRegistry::instance().clearFactories();
  }
};

// Test basic registration
TEST_F(FilterRegistryTest, RegisterFactory) {
  auto factory = std::make_shared<TestFilterFactory>();

  EXPECT_TRUE(
      FilterRegistry::instance().registerFactory("test_filter", factory));
  EXPECT_EQ(1, FilterRegistry::instance().getFactoryCount());
  EXPECT_TRUE(FilterRegistry::instance().hasFactory("test_filter"));
}

// Test duplicate registration
TEST_F(FilterRegistryTest, DuplicateRegistration) {
  auto factory1 = std::make_shared<TestFilterFactory>();
  auto factory2 = std::make_shared<TestFilterFactory>("2.0.0");

  EXPECT_TRUE(
      FilterRegistry::instance().registerFactory("test_filter", factory1));
  EXPECT_FALSE(
      FilterRegistry::instance().registerFactory("test_filter", factory2));

  // Should still have only one factory
  EXPECT_EQ(1, FilterRegistry::instance().getFactoryCount());
}

// Test null factory registration
TEST_F(FilterRegistryTest, NullFactoryRegistration) {
  EXPECT_FALSE(
      FilterRegistry::instance().registerFactory("null_filter", nullptr));
  EXPECT_EQ(0, FilterRegistry::instance().getFactoryCount());
}

// Test filter creation
TEST_F(FilterRegistryTest, CreateFilter) {
  auto factory = std::make_shared<TestFilterFactory>();
  FilterRegistry::instance().registerFactory("test_filter", factory);

  auto config = json::JsonObjectBuilder()
                    .add("enabled", true)
                    .add("name", "my_filter")
                    .build();

  auto filter = FilterRegistry::instance().createFilter("test_filter", config);
  ASSERT_NE(nullptr, filter);

  // Verify it created our mock filter
  auto mock_filter = std::dynamic_pointer_cast<MockFilter>(filter);
  ASSERT_NE(nullptr, mock_filter);
  EXPECT_EQ("my_filter", mock_filter->getName());
  EXPECT_EQ(1, TestFilterFactory::getCreateCount());
}

// Test creation with unknown filter type
TEST_F(FilterRegistryTest, CreateUnknownFilter) {
  auto config = json::JsonValue::object();

  EXPECT_THROW(
      FilterRegistry::instance().createFilter("unknown_filter", config),
      std::runtime_error);
}

// Test creation with invalid config
TEST_F(FilterRegistryTest, CreateFilterInvalidConfig) {
  auto factory = std::make_shared<TestFilterFactory>(
      "1.0.0", false);  // Will fail validation
  FilterRegistry::instance().registerFactory("test_filter", factory);

  auto config = json::JsonValue::object();

  EXPECT_THROW(FilterRegistry::instance().createFilter("test_filter", config),
               std::runtime_error);
}

// Test creation error handling
TEST_F(FilterRegistryTest, CreateFilterError) {
  auto factory = std::make_shared<TestFilterFactory>();
  FilterRegistry::instance().registerFactory("test_filter", factory);

  auto config = json::JsonObjectBuilder().add("throw_error", true).build();

  EXPECT_THROW(FilterRegistry::instance().createFilter("test_filter", config),
               std::runtime_error);
}

// Test getFactory
TEST_F(FilterRegistryTest, GetFactory) {
  auto factory = std::make_shared<TestFilterFactory>();
  FilterRegistry::instance().registerFactory("test_filter", factory);

  auto retrieved = FilterRegistry::instance().getFactory("test_filter");
  EXPECT_EQ(factory, retrieved);

  auto not_found = FilterRegistry::instance().getFactory("unknown");
  EXPECT_EQ(nullptr, not_found);
}

// Test listFactories
TEST_F(FilterRegistryTest, ListFactories) {
  auto factory1 = std::make_shared<TestFilterFactory>();
  auto factory2 = std::make_shared<AnotherTestFilterFactory>();

  FilterRegistry::instance().registerFactory("zebra_filter", factory1);
  FilterRegistry::instance().registerFactory("alpha_filter", factory2);

  auto factories = FilterRegistry::instance().listFactories();
  ASSERT_EQ(2, factories.size());

  // Should be sorted alphabetically
  EXPECT_EQ("alpha_filter", factories[0]);
  EXPECT_EQ("zebra_filter", factories[1]);
}

// Test clearFactories
TEST_F(FilterRegistryTest, ClearFactories) {
  auto factory1 = std::make_shared<TestFilterFactory>();
  auto factory2 = std::make_shared<AnotherTestFilterFactory>();

  FilterRegistry::instance().registerFactory("filter1", factory1);
  FilterRegistry::instance().registerFactory("filter2", factory2);

  EXPECT_EQ(2, FilterRegistry::instance().getFactoryCount());

  FilterRegistry::instance().clearFactories();

  EXPECT_EQ(0, FilterRegistry::instance().getFactoryCount());
  EXPECT_FALSE(FilterRegistry::instance().hasFactory("filter1"));
  EXPECT_FALSE(FilterRegistry::instance().hasFactory("filter2"));
}

// Test factory metadata
TEST_F(FilterRegistryTest, FactoryMetadata) {
  auto factory = std::make_shared<TestFilterFactory>("3.2.1");
  FilterRegistry::instance().registerFactory("test_filter", factory);

  auto retrieved = FilterRegistry::instance().getFactory("test_filter");
  ASSERT_NE(nullptr, retrieved);

  const auto& metadata = retrieved->getMetadata();
  EXPECT_EQ("test_filter", metadata.name);
  EXPECT_EQ("3.2.1", metadata.version);
  EXPECT_EQ("Test filter for unit tests", metadata.description);
  ASSERT_EQ(2, metadata.dependencies.size());
  EXPECT_EQ("dep1", metadata.dependencies[0]);
  EXPECT_EQ("dep2", metadata.dependencies[1]);
}

// Test default config
TEST_F(FilterRegistryTest, DefaultConfig) {
  auto factory = std::make_shared<TestFilterFactory>();
  FilterRegistry::instance().registerFactory("test_filter", factory);

  auto retrieved = FilterRegistry::instance().getFactory("test_filter");
  ASSERT_NE(nullptr, retrieved);

  auto default_config = retrieved->getDefaultConfig();
  EXPECT_TRUE(default_config.isObject());
  EXPECT_TRUE(default_config.contains("enabled"));
  EXPECT_TRUE(default_config["enabled"].getBool());
  EXPECT_EQ("default", default_config["name"].getString());
}

// Test thread safety of registration
TEST_F(FilterRegistryTest, ThreadSafeRegistration) {
  const int num_threads = 10;
  const int factories_per_thread = 5;
  std::vector<std::thread> threads;
  std::atomic<int> success_count(0);

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([i, &success_count]() {
      for (int j = 0; j < factories_per_thread; ++j) {
        auto factory = std::make_shared<TestFilterFactory>();
        std::string name =
            "filter_" + std::to_string(i) + "_" + std::to_string(j);
        if (FilterRegistry::instance().registerFactory(name, factory)) {
          success_count++;
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All registrations should succeed
  EXPECT_EQ(num_threads * factories_per_thread, success_count.load());
  EXPECT_EQ(num_threads * factories_per_thread,
            FilterRegistry::instance().getFactoryCount());
}

// Test thread safety of creation
TEST_F(FilterRegistryTest, ThreadSafeCreation) {
  auto factory = std::make_shared<TestFilterFactory>();
  FilterRegistry::instance().registerFactory("test_filter", factory);

  const int num_threads = 20;
  const int creates_per_thread = 10;
  std::vector<std::thread> threads;
  std::atomic<int> success_count(0);

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&success_count]() {
      for (int j = 0; j < creates_per_thread; ++j) {
        auto config =
            json::JsonObjectBuilder().add("name", "thread_filter").build();

        try {
          auto filter =
              FilterRegistry::instance().createFilter("test_filter", config);
          if (filter != nullptr) {
            success_count++;
          }
        } catch (...) {
          // Should not happen
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All creations should succeed
  EXPECT_EQ(num_threads * creates_per_thread, success_count.load());
  EXPECT_EQ(num_threads * creates_per_thread,
            TestFilterFactory::getCreateCount());
}

// Test the REGISTER_FILTER_FACTORY macro
// Note: This test verifies that the macro can be used to register a factory
// We'll register it manually in the test to avoid static initialization issues
TEST_F(FilterRegistryTest, RegistrationMacro) {
  // Define a test factory class
  class LocalMacroTestFilterFactory : public FilterFactory {
   public:
    network::FilterSharedPtr createFilter(
        const json::JsonValue& config) const override {
      return std::make_shared<MockFilter>("macro_test");
    }

    const FilterFactoryMetadata& getMetadata() const override {
      static FilterFactoryMetadata metadata;
      metadata.name = "macro_test_filter";
      metadata.version = "1.0.0";
      return metadata;
    }
  };

  // Manually register the factory (simulating what the macro would do)
  auto factory = std::make_shared<LocalMacroTestFilterFactory>();
  EXPECT_TRUE(
      FilterRegistry::instance().registerFactory("macro_test_filter", factory));

  // Verify it was registered
  EXPECT_TRUE(FilterRegistry::instance().hasFactory("macro_test_filter"));

  // Test creating a filter with it
  auto config = json::JsonValue::object();
  auto filter =
      FilterRegistry::instance().createFilter("macro_test_filter", config);
  ASSERT_NE(nullptr, filter);

  auto mock_filter = std::dynamic_pointer_cast<MockFilter>(filter);
  ASSERT_NE(nullptr, mock_filter);
  EXPECT_EQ("macro_test", mock_filter->getName());

  // Note: The actual REGISTER_FILTER_FACTORY macro will be used by real filter
  // implementations. This test verifies the pattern works correctly.
}

}  // namespace