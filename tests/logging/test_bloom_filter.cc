#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mcp/logging/bloom_filter.h"

using namespace mcp::logging;

class BloomFilterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    filter_ = std::make_unique<BloomFilter<std::string>>(1024, 3);
  }

  std::unique_ptr<BloomFilter<std::string>> filter_;
};

TEST_F(BloomFilterTest, EmptyFilterReturnsFalse) {
  EXPECT_FALSE(filter_->mayContain("test"));
  EXPECT_FALSE(filter_->mayContain("hello"));
  EXPECT_FALSE(filter_->mayContain("world"));
}

TEST_F(BloomFilterTest, AddedItemMayBeContained) {
  filter_->add("test");
  EXPECT_TRUE(filter_->mayContain("test"));
}

TEST_F(BloomFilterTest, MultipleItems) {
  std::vector<std::string> items = {"logger1", "logger2", "logger3",
                                    "component.server"};

  for (const auto& item : items) {
    filter_->add(item);
  }

  for (const auto& item : items) {
    EXPECT_TRUE(filter_->mayContain(item)) << "Failed for: " << item;
  }
}

TEST_F(BloomFilterTest, NotAddedItemsMayReturnFalse) {
  filter_->add("added");

  // These should likely return false (no false negatives)
  EXPECT_FALSE(filter_->mayContain("not_added"));
  EXPECT_FALSE(filter_->mayContain("another"));
}

TEST_F(BloomFilterTest, ClearResetsFilter) {
  filter_->add("test1");
  filter_->add("test2");

  EXPECT_TRUE(filter_->mayContain("test1"));
  EXPECT_TRUE(filter_->mayContain("test2"));

  filter_->clear();

  EXPECT_FALSE(filter_->mayContain("test1"));
  EXPECT_FALSE(filter_->mayContain("test2"));
}

TEST_F(BloomFilterTest, DifferentSizesAndHashes) {
  BloomFilter<std::string> small_filter(64, 2);
  BloomFilter<std::string> large_filter(4096, 5);

  std::string test_item = "test_logger";

  small_filter.add(test_item);
  large_filter.add(test_item);

  EXPECT_TRUE(small_filter.mayContain(test_item));
  EXPECT_TRUE(large_filter.mayContain(test_item));

  EXPECT_EQ(small_filter.size(), 64);
  EXPECT_EQ(small_filter.numHashes(), 2);
  EXPECT_EQ(large_filter.size(), 4096);
  EXPECT_EQ(large_filter.numHashes(), 5);
}

TEST_F(BloomFilterTest, FalsePositiveRate) {
  // Add some items
  const int num_items = 100;
  for (int i = 0; i < num_items; ++i) {
    filter_->add("item_" + std::to_string(i));
  }

  // Check for false positives
  int false_positives = 0;
  const int test_items = 1000;
  for (int i = num_items; i < num_items + test_items; ++i) {
    if (filter_->mayContain("item_" + std::to_string(i))) {
      false_positives++;
    }
  }

  // With 1024 bits, 3 hashes, and 100 items, false positive rate should be
  // reasonable
  double fp_rate = static_cast<double>(false_positives) / test_items;
  EXPECT_LT(fp_rate, 0.1) << "False positive rate too high: " << fp_rate;
}

TEST_F(BloomFilterTest, IntegerType) {
  BloomFilter<int> int_filter(512, 3);

  int_filter.add(42);
  int_filter.add(100);
  int_filter.add(-5);

  EXPECT_TRUE(int_filter.mayContain(42));
  EXPECT_TRUE(int_filter.mayContain(100));
  EXPECT_TRUE(int_filter.mayContain(-5));

  EXPECT_FALSE(int_filter.mayContain(999));
  EXPECT_FALSE(int_filter.mayContain(0));
}