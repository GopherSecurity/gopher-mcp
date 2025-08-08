#include <algorithm>
#include <deque>
#include <forward_list>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <random>
#include <set>
#include <stack>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/core/memory_utils.h"
#include "mcp/core/optional.h"

// Advanced test scenarios for mcp::optional
// Focus on stress tests, performance, and complex interactions

namespace {

// Helper for alignment tests
template <size_t N>
struct alignas(N) AlignedStorage {
  char data[N];
  AlignedStorage() { std::fill(data, data + N, 'a'); }
};

// Helper for testing with standard algorithms
struct Counter {
  static int count;
  int id;

  Counter() : id(++count) {}
  Counter(const Counter& other) : id(other.id) { ++count; }
  Counter(Counter&& other) : id(other.id) {
    other.id = 0;
    ++count;
  }
  ~Counter() { --count; }

  Counter& operator=(const Counter& other) {
    id = other.id;
    return *this;
  }
  Counter& operator=(Counter&& other) {
    id = other.id;
    other.id = 0;
    return *this;
  }

  bool operator<(const Counter& other) const { return id < other.id; }
  bool operator==(const Counter& other) const { return id == other.id; }

  friend void swap(Counter& a, Counter& b) {
    using std::swap;
    swap(a.id, b.id);
  }
};
int Counter::count = 0;

// Type with complex constructors
struct ComplexConstruct {
  std::vector<int> v;
  std::map<std::string, double> m;
  std::unique_ptr<std::string> p;

  ComplexConstruct()
      : v{1, 2, 3},
        m{{"pi", 3.14}, {"e", 2.71}},
        p(mcp::make_unique<std::string>("default")) {}

  ComplexConstruct(int n, const std::string& s)
      : v(n, 42),
        m{{"key", static_cast<double>(n)}},
        p(mcp::make_unique<std::string>(s)) {}

  // Move constructor and assignment
  ComplexConstruct(ComplexConstruct&&) = default;
  ComplexConstruct& operator=(ComplexConstruct&&) = default;

  // Delete copy operations due to unique_ptr
  ComplexConstruct(const ComplexConstruct&) = delete;
  ComplexConstruct& operator=(const ComplexConstruct&) = delete;
};

// Recursive structure
struct TreeNode {
  int value;
  mcp::optional<std::unique_ptr<TreeNode>> left;
  mcp::optional<std::unique_ptr<TreeNode>> right;

  explicit TreeNode(int v) : value(v) {}
};

// Type that logs all operations
struct LoggingType {
  static std::vector<std::string> log;
  int value;

  LoggingType(int v = 0) : value(v) {
    log.push_back("construct(" + std::to_string(v) + ")");
  }

  LoggingType(const LoggingType& other) : value(other.value) {
    log.push_back("copy(" + std::to_string(value) + ")");
  }

  LoggingType(LoggingType&& other) : value(other.value) {
    log.push_back("move(" + std::to_string(value) + ")");
    other.value = -1;
  }

  ~LoggingType() { log.push_back("destroy(" + std::to_string(value) + ")"); }

  LoggingType& operator=(const LoggingType& other) {
    log.push_back("copy_assign(" + std::to_string(other.value) + ")");
    value = other.value;
    return *this;
  }

  LoggingType& operator=(LoggingType&& other) {
    log.push_back("move_assign(" + std::to_string(other.value) + ")");
    value = other.value;
    other.value = -1;
    return *this;
  }
};
std::vector<std::string> LoggingType::log;

}  // namespace

class OptionalAdvancedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Counter::count = 0;
    LoggingType::log.clear();
  }
};

// Standard algorithm compatibility tests
TEST_F(OptionalAdvancedTest, StandardAlgorithms) {
  std::vector<mcp::optional<int>> vec;
  vec.push_back(3);
  vec.push_back(mcp::nullopt);
  vec.push_back(1);
  vec.push_back(4);
  vec.push_back(mcp::nullopt);
  vec.push_back(2);

  // Count engaged optionals
  auto engaged = std::count_if(
      vec.begin(), vec.end(),
      [](const mcp::optional<int>& opt) { return opt.has_value(); });
  EXPECT_EQ(engaged, 4);

  // Sort (nullopt comes first)
  std::sort(vec.begin(), vec.end());
  EXPECT_FALSE(vec[0]);
  EXPECT_FALSE(vec[1]);
  EXPECT_TRUE(vec[2]);
  EXPECT_EQ(*vec[2], 1);
  EXPECT_EQ(*vec[5], 4);

  // Transform
  std::vector<int> values;
  std::transform(
      vec.begin(), vec.end(), std::back_inserter(values),
      [](const mcp::optional<int>& opt) { return opt.value_or(-1); });
  EXPECT_EQ(values[0], -1);
  EXPECT_EQ(values[1], -1);
  EXPECT_EQ(values[2], 1);
}

// Container adapter tests
TEST_F(OptionalAdvancedTest, ContainerAdapters) {
  // Priority queue of optionals
  {
    std::priority_queue<mcp::optional<int>> pq;
    pq.push(42);
    pq.push(mcp::nullopt);
    pq.push(99);
    pq.push(1);

    EXPECT_EQ(*pq.top(), 99);  // Largest value
    pq.pop();
    EXPECT_EQ(*pq.top(), 42);
    pq.pop();
    EXPECT_EQ(*pq.top(), 1);
    pq.pop();
    EXPECT_FALSE(pq.top());  // nullopt is smallest
  }

  // Stack of optionals
  {
    std::stack<mcp::optional<std::string>> s;
    s.push("bottom");
    s.push(mcp::nullopt);
    s.push("top");

    EXPECT_EQ(*s.top(), "top");
    s.pop();
    EXPECT_FALSE(s.top());
    s.pop();
    EXPECT_EQ(*s.top(), "bottom");
  }
}

// Associative container tests
TEST_F(OptionalAdvancedTest, AssociativeContainers) {
  // Map with optional values
  {
    std::map<int, mcp::optional<std::string>> m;
    m[1] = "one";
    m[2] = mcp::nullopt;
    m[3] = "three";

    EXPECT_TRUE(m[1]);
    EXPECT_EQ(*m[1], "one");
    EXPECT_FALSE(m[2]);
    EXPECT_TRUE(m[3]);

    // Find and modify
    auto it = m.find(1);
    if (it != m.end() && it->second) {
      *it->second += " modified";
      EXPECT_EQ(*m[1], "one modified");
    }
  }

  // Set of optionals
  {
    std::set<mcp::optional<Counter>> s;
    s.insert(Counter());  // id = 1
    s.insert(Counter());  // id = 2
    s.insert(mcp::nullopt);
    s.insert(Counter());  // id = 3

    EXPECT_EQ(s.size(), 4u);
    EXPECT_FALSE(*s.begin());  // nullopt is first

    auto it = s.begin();
    ++it;
    EXPECT_TRUE(*it);
    EXPECT_EQ((*it)->id, 1);
  }
}

// Unordered container tests
TEST_F(OptionalAdvancedTest, UnorderedContainers) {
  // Unordered map with optional keys (requires hash)
  {
    std::unordered_map<mcp::optional<int>, std::string,
                       mcp::hash<mcp::optional<int>>>
        um;
    um[42] = "forty-two";
    um[mcp::nullopt] = "empty";
    um[99] = "ninety-nine";

    EXPECT_EQ(um.size(), 3u);
    EXPECT_EQ(um[42], "forty-two");
    EXPECT_EQ(um[mcp::nullopt], "empty");
    EXPECT_EQ(um[99], "ninety-nine");
  }

  // Unordered set of optionals
  {
    std::unordered_set<mcp::optional<std::string>,
                       mcp::hash<mcp::optional<std::string>>>
        us;
    us.insert("hello");
    us.insert(mcp::nullopt);
    us.insert("world");
    us.insert("hello");  // Duplicate

    EXPECT_EQ(us.size(), 3u);
    EXPECT_NE(us.find("hello"), us.end());
    EXPECT_NE(us.find(mcp::nullopt), us.end());
    EXPECT_EQ(us.find("goodbye"), us.end());
  }
}

// Complex type tests
TEST_F(OptionalAdvancedTest, ComplexTypes) {
  // Optional of complex type
  {
    mcp::optional<ComplexConstruct> opt;
    EXPECT_FALSE(opt);

    opt.emplace(5, "test");
    EXPECT_TRUE(opt);
    EXPECT_EQ(opt->v.size(), 5u);
    EXPECT_EQ(*opt->p, "test");
    EXPECT_EQ(opt->m["key"], 5.0);
  }

  // Nested containers
  {
    using NestedType = std::map<std::string, std::vector<mcp::optional<int>>>;
    mcp::optional<NestedType> opt(mcp::in_place);

    (*opt)["evens"] = {2, 4, mcp::nullopt, 6, 8};
    (*opt)["odds"] = {1, mcp::nullopt, 3, 5, 7};

    EXPECT_EQ(opt->size(), 2u);
    EXPECT_EQ((*opt)["evens"].size(), 5u);
    EXPECT_FALSE((*opt)["evens"][2]);
    EXPECT_TRUE((*opt)["odds"][0]);
    EXPECT_EQ(*(*opt)["odds"][0], 1);
  }
}

// Recursive structure tests
TEST_F(OptionalAdvancedTest, RecursiveStructures) {
  // Build a tree
  TreeNode root(1);
  root.left = mcp::make_unique<TreeNode>(2);
  root.right = mcp::make_unique<TreeNode>(3);
  (*root.left)->left = mcp::make_unique<TreeNode>(4);
  (*root.left)->right = mcp::make_unique<TreeNode>(5);

  // Traverse the tree
  EXPECT_EQ(root.value, 1);
  EXPECT_TRUE(root.left);
  EXPECT_TRUE(root.right);
  EXPECT_EQ((*root.left)->value, 2);
  EXPECT_EQ((*root.right)->value, 3);
  EXPECT_TRUE((*root.left)->left);
  EXPECT_EQ((*(*root.left)->left)->value, 4);
}

// Lifetime tracking tests
TEST_F(OptionalAdvancedTest, DetailedLifetimeTracking) {
  // Clear the log to start fresh
  LoggingType::log.clear();

  {
    // Direct construction via in_place avoids temporaries
    mcp::optional<LoggingType> opt1(mcp::in_place, 42);
    EXPECT_EQ(LoggingType::log.size(), 1u);  // Only one construction
    EXPECT_EQ(LoggingType::log[0], "construct(42)");

    mcp::optional<LoggingType> opt2(opt1);
    EXPECT_TRUE(std::find(LoggingType::log.begin(), LoggingType::log.end(),
                          "copy(42)") != LoggingType::log.end());

    // Assignment to existing value
    LoggingType::log.clear();
    opt2 = LoggingType(99);  // Creates temp, then move assigns
    // Should see: construct(99), move_assign(99), destroy(-1)
    bool has_construct = false;
    bool has_move_assign = false;
    for (const auto& entry : LoggingType::log) {
      if (entry == "construct(99)")
        has_construct = true;
      if (entry == "move_assign(99)")
        has_move_assign = true;
    }
    EXPECT_TRUE(has_construct);
    EXPECT_TRUE(has_move_assign);

    LoggingType::log.clear();
    opt1 = mcp::nullopt;
    // Should destroy the contained value
    EXPECT_TRUE(std::find(LoggingType::log.begin(), LoggingType::log.end(),
                          "destroy(42)") != LoggingType::log.end());

    LoggingType::log.clear();
    opt2.reset();
    // Should destroy the contained value
    EXPECT_TRUE(std::find(LoggingType::log.begin(), LoggingType::log.end(),
                          "destroy(99)") != LoggingType::log.end());
  }

  // Both optionals are now destroyed, should see no additional destructions
  // since we already explicitly reset them
}

// Alignment stress tests
TEST_F(OptionalAdvancedTest, AlignmentStress) {
  // Test various alignments
  {
    mcp::optional<AlignedStorage<8>> opt8;
    mcp::optional<AlignedStorage<16>> opt16;
    mcp::optional<AlignedStorage<32>> opt32;
    mcp::optional<AlignedStorage<64>> opt64;

    EXPECT_EQ(alignof(decltype(opt8)), 8u);
    EXPECT_EQ(alignof(decltype(opt16)), 16u);
    EXPECT_EQ(alignof(decltype(opt32)), 32u);
    EXPECT_EQ(alignof(decltype(opt64)), 64u);

    // Emplace and verify
    opt8.emplace();
    opt16.emplace();
    opt32.emplace();
    opt64.emplace();

    EXPECT_TRUE(opt8);
    EXPECT_TRUE(opt16);
    EXPECT_TRUE(opt32);
    EXPECT_TRUE(opt64);
  }
}

// Performance-oriented tests
TEST_F(OptionalAdvancedTest, PerformancePatterns) {
  // Move-only type performance
  {
    std::vector<mcp::optional<std::unique_ptr<int>>> vec;
    vec.reserve(1000);

    for (int i = 0; i < 1000; ++i) {
      if (i % 3 == 0) {
        vec.push_back(mcp::nullopt);
      } else {
        vec.push_back(mcp::make_unique<int>(i));
      }
    }

    // Move elements around
    std::rotate(vec.begin(), vec.begin() + 333, vec.end());

    // Verify
    int nullcount = 0;
    for (const auto& opt : vec) {
      if (!opt) {
        ++nullcount;
      }
    }
    EXPECT_EQ(nullcount, 334);  // Every third element
  }

  // Large object handling
  {
    struct LargeObject {
      char data[4096];
      LargeObject() { std::fill(data, data + 4096, 'x'); }
      LargeObject(const LargeObject&) = default;
      LargeObject& operator=(const LargeObject&) = default;
    };

    std::deque<mcp::optional<LargeObject>> dq;

    // Push many large objects
    for (int i = 0; i < 100; ++i) {
      if (i % 2 == 0) {
        dq.push_back(LargeObject());
      } else {
        dq.push_back(mcp::nullopt);
      }
    }

    // Random access
    EXPECT_TRUE(dq[0]);
    EXPECT_FALSE(dq[1]);
    EXPECT_EQ(dq[0]->data[0], 'x');
  }
}

// Move ThrowingType outside of the function to avoid local struct static
// members
struct ThrowingTypeForStress {
  static int throw_after;
  static int count;

  ThrowingTypeForStress() {
    if (++count > throw_after) {
      throw std::runtime_error("ThrowingType");
    }
  }

  ThrowingTypeForStress(const ThrowingTypeForStress&) {
    if (++count > throw_after) {
      throw std::runtime_error("ThrowingType copy");
    }
  }

  ThrowingTypeForStress& operator=(const ThrowingTypeForStress&) = default;
};

int ThrowingTypeForStress::throw_after = 0;
int ThrowingTypeForStress::count = 0;

// Exception safety stress test
TEST_F(OptionalAdvancedTest, ExceptionSafetyStress) {
  ThrowingTypeForStress::count = 0;
  ThrowingTypeForStress::throw_after = 5;

  std::vector<mcp::optional<ThrowingTypeForStress>> vec;

  try {
    for (int i = 0; i < 10; ++i) {
      vec.push_back(ThrowingTypeForStress());
    }
    FAIL() << "Should have thrown";
  } catch (const std::runtime_error&) {
    // All optionals should be properly cleaned up
    EXPECT_LT(vec.size(), 10u);
  }
}

// Functional programming patterns
TEST_F(OptionalAdvancedTest, FunctionalPatterns) {
  // Chain of transformations
  auto transform =
      [](const mcp::optional<int>& opt) -> mcp::optional<std::string> {
    if (opt) {
      return std::to_string(*opt * 2);
    }
    return mcp::nullopt;
  };

  mcp::optional<int> start(21);
  mcp::optional<std::string> result = transform(start);
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "42");

  mcp::optional<int> empty;
  result = transform(empty);
  EXPECT_FALSE(result);

  // Filter pattern
  auto filter = [](const mcp::optional<int>& opt) -> mcp::optional<int> {
    if (opt && *opt > 10) {
      return opt;
    }
    return mcp::nullopt;
  };

  EXPECT_TRUE(filter(mcp::optional<int>(42)));
  EXPECT_FALSE(filter(mcp::optional<int>(5)));
  EXPECT_FALSE(filter(mcp::optional<int>()));
}

// Thread safety considerations (single-threaded test)
TEST_F(OptionalAdvancedTest, ThreadSafetyPatterns) {
  // Const optional can be safely shared between threads
  const mcp::optional<int> shared_opt(42);

  // Simulate multiple readers
  int sum = 0;
  for (int i = 0; i < 100; ++i) {
    if (shared_opt) {
      sum += *shared_opt;
    }
  }
  EXPECT_EQ(sum, 4200);

  // Non-const requires synchronization (not tested here)
}

// Memory layout tests
TEST_F(OptionalAdvancedTest, MemoryLayout) {
  // Verify compact layout
  struct TinyStruct {
    char c;
  };

  // Optional should add minimal overhead
  EXPECT_LE(sizeof(mcp::optional<TinyStruct>),
            sizeof(TinyStruct) + sizeof(bool) + 7);  // Padding

  // For larger types, overhead should be negligible percentage-wise
  struct LargeStruct {
    char data[1024];
  };

  size_t overhead = sizeof(mcp::optional<LargeStruct>) - sizeof(LargeStruct);
  EXPECT_LT(overhead, 16u);  // Should be just a bool + padding
}

// Edge case: optional of incomplete type (through pointer)
TEST_F(OptionalAdvancedTest, IncompleteTypes) {
  struct Incomplete;  // Forward declaration only

  // This should compile
  mcp::optional<Incomplete*> opt1;
  // Note: unique_ptr with incomplete type won't work due to destructor
  // requirements mcp::optional<std::unique_ptr<Incomplete>> opt2;  // This
  // would fail
  mcp::optional<std::shared_ptr<Incomplete>> opt3;

  EXPECT_FALSE(opt1);
  EXPECT_FALSE(opt3);

  opt1 = nullptr;
  EXPECT_TRUE(opt1);
  EXPECT_EQ(*opt1, nullptr);
}

// Integration with other library components
TEST_F(OptionalAdvancedTest, LibraryIntegration) {
  // With std::pair
  {
    using OptPair = std::pair<mcp::optional<int>, mcp::optional<std::string>>;
    OptPair p1(42, "hello");
    OptPair p2(mcp::nullopt, "world");
    OptPair p3(99, mcp::nullopt);

    EXPECT_TRUE(p1.first);
    EXPECT_TRUE(p1.second);
    EXPECT_FALSE(p2.first);
    EXPECT_TRUE(p2.second);
  }

  // With std::tuple (if available in C++11)
  {
    mcp::optional<int> opt1(42);
    mcp::optional<double> opt2(3.14);
    mcp::optional<std::string> opt3("test");

    auto get_all = [](const mcp::optional<int>& o1,
                      const mcp::optional<double>& o2,
                      const mcp::optional<std::string>& o3) {
      return o1.has_value() && o2.has_value() && o3.has_value();
    };

    EXPECT_TRUE(get_all(opt1, opt2, opt3));
  }
}

// Extreme stress test
TEST_F(OptionalAdvancedTest, ExtremeStress) {
  // Many optionals with various states
  const int N = 10000;
  std::vector<mcp::optional<Counter>> vec;
  vec.reserve(N);

  // Fill with pattern
  for (int i = 0; i < N; ++i) {
    if (i % 3 == 0) {
      vec.push_back(mcp::nullopt);
    } else {
      vec.push_back(Counter());
    }
  }

  // Many operations
  for (int iter = 0; iter < 10; ++iter) {
    // Shuffle
    // Use std::shuffle with random_device for C++11
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(vec.begin(), vec.end(), gen);

    // Modify some
    for (int i = 0; i < N; i += 10) {
      if (vec[i]) {
        vec[i] = mcp::nullopt;
      } else {
        vec[i] = Counter();
      }
    }

    // Copy range
    std::vector<mcp::optional<Counter>> vec2(vec.begin() + N / 4,
                                             vec.begin() + N / 2);

    // Clear and refill
    vec2.clear();
    vec2.insert(vec2.end(), vec.begin(), vec.begin() + N / 10);
  }

  // Verify Counter integrity
  vec.clear();
  EXPECT_EQ(Counter::count, 0);  // All destroyed
}
