#include <gtest/gtest.h>
#include <thread>
#include <numeric>
#include <cstring>
#include "mcp/buffer.h"

using namespace mcp;

class BufferTest : public ::testing::Test {
protected:
    std::unique_ptr<Buffer> buffer_;
    
    void SetUp() override {
        buffer_ = createBuffer();
    }
};

// Basic Operations Tests
TEST_F(BufferTest, EmptyBuffer) {
    EXPECT_EQ(buffer_->length(), 0u);
    EXPECT_TRUE(buffer_->empty());
    EXPECT_EQ(buffer_->sliceCount(), 0u);
}

TEST_F(BufferTest, AddData) {
    const char* data = "Hello, World!";
    buffer_->add(data, strlen(data));
    
    EXPECT_EQ(buffer_->length(), strlen(data));
    EXPECT_FALSE(buffer_->empty());
    EXPECT_GE(buffer_->sliceCount(), 1u);
}

TEST_F(BufferTest, AddString) {
    std::string data = "Hello, World!";
    buffer_->add(data);
    
    EXPECT_EQ(buffer_->length(), data.size());
    EXPECT_FALSE(buffer_->empty());
}

TEST_F(BufferTest, AddBuffer) {
    auto source = createBuffer();
    const char* data = "Hello, World!";
    source->add(data, strlen(data));
    
    buffer_->add(*source);
    
    EXPECT_EQ(buffer_->length(), strlen(data));
    EXPECT_EQ(source->length(), strlen(data)); // Source should remain unchanged
}

TEST_F(BufferTest, DrainData) {
    const char* data = "Hello, World!";
    buffer_->add(data, strlen(data));
    
    buffer_->drain(5);
    EXPECT_EQ(buffer_->length(), strlen(data) - 5);
    
    buffer_->drain(buffer_->length());
    EXPECT_TRUE(buffer_->empty());
}

TEST_F(BufferTest, DrainMoreThanAvailable) {
    const char* data = "Hello";
    buffer_->add(data, strlen(data));
    
    EXPECT_THROW(buffer_->drain(10), std::runtime_error);
}

TEST_F(BufferTest, PrependData) {
    buffer_->add("World!", 6);
    buffer_->prepend("Hello, ", 7);
    
    EXPECT_EQ(buffer_->length(), 13u);
    EXPECT_EQ(buffer_->toString(), "Hello, World!");
    
    char output[13];
    size_t copied = buffer_->copyOut(0, 13, output);
    EXPECT_EQ(copied, 13u);
    EXPECT_EQ(std::string(output, 13), "Hello, World!");
}

TEST_F(BufferTest, PrependString) {
    buffer_->add("World!");
    buffer_->prepend(std::string("Hello, "));
    
    EXPECT_EQ(buffer_->toString(), "Hello, World!");
}

// Move Operations Tests
TEST_F(BufferTest, MoveEntireBuffer) {
    const char* data = "Hello, World!";
    buffer_->add(data, strlen(data));
    size_t original_length = buffer_->length();
    
    auto destination = createBuffer();
    buffer_->move(*destination);
    
    EXPECT_EQ(buffer_->length(), 0u);
    EXPECT_TRUE(buffer_->empty());
    EXPECT_EQ(destination->length(), original_length);
}

TEST_F(BufferTest, MovePartialBuffer) {
    const char* data = "Hello, World!";
    buffer_->add(data, strlen(data));
    
    auto destination = createBuffer();
    buffer_->move(*destination, 7);
    
    EXPECT_EQ(buffer_->length(), 6u);
    EXPECT_EQ(destination->length(), 7u);
    EXPECT_EQ(buffer_->toString(), "World!");
    EXPECT_EQ(destination->toString(), "Hello, ");
}

// Linearize Tests
TEST_F(BufferTest, LinearizeSmallData) {
    buffer_->add("Hello", 5);
    buffer_->add(" ", 1);
    buffer_->add("World", 5);
    
    void* ptr = buffer_->linearize(11);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(std::string(static_cast<char*>(ptr), 11), "Hello World");
}

TEST_F(BufferTest, LinearizePartialData) {
    buffer_->add("Hello, World!", 13);
    
    void* ptr = buffer_->linearize(5);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(std::string(static_cast<char*>(ptr), 5), "Hello");
}

// CopyOut Tests
TEST_F(BufferTest, CopyOutFullBuffer) {
    const char* data = "Hello, World!";
    buffer_->add(data, strlen(data));
    
    char output[14];
    size_t copied = buffer_->copyOut(0, 13, output);
    output[13] = '\0';
    
    EXPECT_EQ(copied, 13u);
    EXPECT_STREQ(output, data);
}

TEST_F(BufferTest, CopyOutPartialBuffer) {
    const char* data = "Hello, World!";
    buffer_->add(data, strlen(data));
    
    char output[6];
    size_t copied = buffer_->copyOut(7, 5, output);
    output[5] = '\0';
    
    EXPECT_EQ(copied, 5u);
    EXPECT_STREQ(output, "World");
}

TEST_F(BufferTest, CopyOutBeyondBuffer) {
    buffer_->add("Hello", 5);
    
    char output[10];
    size_t copied = buffer_->copyOut(0, 10, output);
    
    EXPECT_EQ(copied, 5u);
}

// Search Operations Tests
TEST_F(BufferTest, SearchFound) {
    buffer_->add("Hello, World! Hello again!");
    
    auto result = buffer_->search("World", 5);
    EXPECT_TRUE(result.found_);
    EXPECT_EQ(result.position_, 7u);
}

TEST_F(BufferTest, SearchNotFound) {
    buffer_->add("Hello, World!");
    
    auto result = buffer_->search("Goodbye", 7);
    EXPECT_FALSE(result.found_);
}

TEST_F(BufferTest, SearchWithStartPosition) {
    buffer_->add("Hello, World! Hello again!");
    
    auto result = buffer_->search("Hello", 5, 10);
    EXPECT_TRUE(result.found_);
    EXPECT_EQ(result.position_, 14u);
}

TEST_F(BufferTest, StartsWith) {
    buffer_->add("Hello, World!");
    
    EXPECT_TRUE(buffer_->startsWith("Hello", 5));
    EXPECT_FALSE(buffer_->startsWith("World", 5));
    EXPECT_FALSE(buffer_->startsWith("Hello, World! Extra", 19));
}

// Integer I/O Tests
TEST_F(BufferTest, WriteReadLEInt) {
    buffer_->writeLEInt<uint32_t>(0x12345678);
    
    EXPECT_EQ(buffer_->length(), 4u);
    
    uint32_t value = buffer_->readLEInt<uint32_t>();
    EXPECT_EQ(value, 0x12345678u);
    EXPECT_TRUE(buffer_->empty());
}

TEST_F(BufferTest, WriteReadBEInt) {
    buffer_->writeBEInt<uint32_t>(0x12345678);
    
    EXPECT_EQ(buffer_->length(), 4u);
    
    uint32_t value = buffer_->readBEInt<uint32_t>();
    EXPECT_EQ(value, 0x12345678u);
    EXPECT_TRUE(buffer_->empty());
}

TEST_F(BufferTest, PeekLEInt) {
    buffer_->writeLEInt<uint16_t>(0x1234);
    buffer_->writeLEInt<uint32_t>(0x56789ABC);
    
    EXPECT_EQ(buffer_->peekLEInt<uint16_t>(0), 0x1234u);
    EXPECT_EQ(buffer_->peekLEInt<uint32_t>(2), 0x56789ABCu);
    EXPECT_EQ(buffer_->length(), 6u); // Data not consumed
}

TEST_F(BufferTest, PeekBEInt) {
    buffer_->writeBEInt<uint16_t>(0x1234);
    buffer_->writeBEInt<uint32_t>(0x56789ABC);
    
    EXPECT_EQ(buffer_->peekBEInt<uint16_t>(0), 0x1234u);
    EXPECT_EQ(buffer_->peekBEInt<uint32_t>(2), 0x56789ABCu);
    EXPECT_EQ(buffer_->length(), 6u); // Data not consumed
}

TEST_F(BufferTest, ReadIntUnderflow) {
    buffer_->writeLEInt<uint16_t>(0x1234);
    
    EXPECT_THROW(buffer_->readLEInt<uint32_t>(), std::runtime_error);
}

TEST_F(BufferTest, PeekIntUnderflow) {
    buffer_->writeLEInt<uint16_t>(0x1234);
    
    EXPECT_THROW(buffer_->peekLEInt<uint32_t>(0), std::runtime_error);
    EXPECT_THROW(buffer_->peekLEInt<uint16_t>(1), std::runtime_error);
}

// Raw Slices Tests
TEST_F(BufferTest, GetRawSlices) {
    buffer_->add("Hello", 5);
    buffer_->add(" ", 1);
    buffer_->add("World", 5);
    
    RawSlice slices[3];
    size_t num_slices = buffer_->getRawSlices(slices, 3);
    
    EXPECT_GE(num_slices, 1u);
    EXPECT_LE(num_slices, 3u);
    
    // Verify total size
    size_t total_size = 0;
    for (size_t i = 0; i < num_slices; ++i) {
        total_size += slices[i].len_;
    }
    EXPECT_EQ(total_size, 11u);
}

TEST_F(BufferTest, GetConstRawSlices) {
    buffer_->add("Hello, World!");
    
    ConstRawSlice slices[2];
    size_t num_slices = buffer_->getRawSlices(slices, 2);
    
    EXPECT_GE(num_slices, 1u);
    EXPECT_LE(num_slices, 2u);
}

TEST_F(BufferTest, FrontSlice) {
    buffer_->add("Hello");
    
    RawSlice slice = buffer_->frontSlice();
    EXPECT_NE(slice.mem_, nullptr);
    EXPECT_EQ(slice.len_, 5u);
    EXPECT_EQ(std::string(static_cast<char*>(slice.mem_), slice.len_), "Hello");
}

TEST_F(BufferTest, FrontSliceEmpty) {
    RawSlice slice = buffer_->frontSlice();
    EXPECT_EQ(slice.mem_, nullptr);
    EXPECT_EQ(slice.len_, 0u);
}

// Reservation Tests
TEST_F(BufferTest, ReserveForRead) {
    auto reservation = buffer_->reserveForRead();
    
    EXPECT_GT(reservation->numSlices(), 0u);
    EXPECT_GT(reservation->length(), 0u);
    
    // Write data to reservation
    RawSlice* slices = reservation->slices();
    const char* data = "Hello";
    memcpy(slices[0].mem_, data, 5);
    
    reservation->commit(5);
    EXPECT_EQ(buffer_->length(), 5u);
    EXPECT_EQ(buffer_->toString(), "Hello");
}

TEST_F(BufferTest, ReserveSingleSlice) {
    RawSlice slice;
    void* mem = buffer_->reserveSingleSlice(100, slice);
    
    EXPECT_NE(mem, nullptr);
    EXPECT_EQ(slice.mem_, mem);
    EXPECT_EQ(slice.len_, 100u);
    
    const char* data = "Test data";
    memcpy(mem, data, strlen(data));
    
    buffer_->commit(slice, strlen(data));
    EXPECT_EQ(buffer_->length(), strlen(data));
    EXPECT_EQ(buffer_->toString(), data);
}

// Buffer Fragment Tests
class TestBufferFragment : public BufferFragment {
public:
    TestBufferFragment(const std::string& data, bool* done_called)
        : data_(data), done_called_(done_called) {}
    
    ~TestBufferFragment() { *done_called_ = true; }
    
    const void* data() const override { return data_.data(); }
    size_t size() const override { return data_.size(); }
    void done() override { }  // In our implementation, cleanup happens in destructor
    
private:
    std::string data_;
    bool* done_called_;
};

TEST_F(BufferTest, AddBufferFragment) {
    bool done_called = false;
    auto fragment = std::make_unique<TestBufferFragment>("Hello, Fragment!", &done_called);
    
    buffer_->addBufferFragment(std::move(fragment));
    
    EXPECT_EQ(buffer_->length(), 16u);
    EXPECT_EQ(buffer_->toString(), "Hello, Fragment!");
    
    // Fragment should be released when buffer is destroyed
    buffer_.reset();
    EXPECT_TRUE(done_called);
}

// Drain Tracker Tests
class TestDrainTracker : public DrainTracker {
public:
    TestDrainTracker(size_t* total_drained) : total_drained_(total_drained) {}
    
    void onDrain(size_t bytes_drained) override {
        *total_drained_ += bytes_drained;
    }
    
private:
    size_t* total_drained_;
};

TEST_F(BufferTest, DrainTracker) {
    size_t total_drained = 0;
    auto tracker = std::make_shared<TestDrainTracker>(&total_drained);
    
    buffer_->add("Hello, World!");
    EXPECT_EQ(buffer_->sliceCount(), 1u);
    
    buffer_->attachDrainTracker(tracker);
    
    // Drain the entire buffer to trigger tracker
    buffer_->drain(13);
    EXPECT_EQ(total_drained, 13u);
}

// Watermark Buffer Tests
class WatermarkBufferTest : public ::testing::Test {
protected:
    bool below_low_watermark_called_ = false;
    bool above_high_watermark_called_ = false;
    bool above_overflow_watermark_called_ = false;
    
    std::unique_ptr<WatermarkBuffer> buffer_;
    
    void SetUp() override {
        buffer_ = std::make_unique<WatermarkBuffer>(
            [this]() { below_low_watermark_called_ = true; },
            [this]() { above_high_watermark_called_ = true; },
            [this]() { above_overflow_watermark_called_ = true; }
        );
        
        buffer_->setWatermarks(100, 5); // high=100, overflow=500
    }
    
    void ResetCallbacks() {
        below_low_watermark_called_ = false;
        above_high_watermark_called_ = false;
        above_overflow_watermark_called_ = false;
    }
};

TEST_F(WatermarkBufferTest, BelowLowWatermark) {
    // Start below low watermark
    EXPECT_FALSE(buffer_->aboveHighWatermark());
}

TEST_F(WatermarkBufferTest, AboveHighWatermark) {
    // Add data to go above high watermark
    std::string data(150, 'x');
    buffer_->add(data);
    
    EXPECT_TRUE(above_high_watermark_called_);
    EXPECT_FALSE(above_overflow_watermark_called_);
    EXPECT_TRUE(buffer_->aboveHighWatermark());
}

TEST_F(WatermarkBufferTest, AboveOverflowWatermark) {
    // Add data to go above overflow watermark
    std::string data(600, 'x');
    buffer_->add(data);
    
    EXPECT_TRUE(above_high_watermark_called_);
    EXPECT_TRUE(above_overflow_watermark_called_);
    EXPECT_TRUE(buffer_->aboveHighWatermark());
}

TEST_F(WatermarkBufferTest, BackBelowLowWatermark) {
    // Go above high watermark
    std::string data(150, 'x');
    buffer_->add(data);
    
    EXPECT_TRUE(buffer_->aboveHighWatermark());
    
    ResetCallbacks();
    
    // Drain to go below low watermark (low watermark is 50)
    buffer_->drain(101);  // 150 - 101 = 49, which is below 50
    
    EXPECT_TRUE(below_low_watermark_called_);
    EXPECT_FALSE(buffer_->aboveHighWatermark());
}

TEST_F(WatermarkBufferTest, MultipleTransitions) {
    // Test multiple transitions
    std::string data50(50, 'x');
    std::string data60(60, 'x');
    
    // Add 50 (below high)
    buffer_->add(data50);
    EXPECT_FALSE(above_high_watermark_called_);
    
    // Add 60 more (total 110, above high)
    buffer_->add(data60);
    EXPECT_TRUE(above_high_watermark_called_);
    
    ResetCallbacks();
    
    // Drain 20 (total 90, still above low=50)
    buffer_->drain(20);
    EXPECT_FALSE(below_low_watermark_called_);
    
    // Drain 50 more (total 40, below low)
    buffer_->drain(50);
    EXPECT_TRUE(below_low_watermark_called_);
}

// Move Constructor/Assignment Tests
TEST_F(BufferTest, MoveConstructor) {
    buffer_->add("Hello, World!");
    size_t original_length = buffer_->length();
    
    OwnedBuffer moved(std::move(*static_cast<OwnedBuffer*>(buffer_.get())));
    
    EXPECT_EQ(moved.length(), original_length);
    EXPECT_EQ(moved.toString(), "Hello, World!");
}

TEST_F(BufferTest, MoveAssignment) {
    buffer_->add("Hello, World!");
    size_t original_length = buffer_->length();
    
    OwnedBuffer moved;
    moved = std::move(*static_cast<OwnedBuffer*>(buffer_.get()));
    
    EXPECT_EQ(moved.length(), original_length);
    EXPECT_EQ(moved.toString(), "Hello, World!");
}

// Stress Tests
TEST_F(BufferTest, LargeDataHandling) {
    // Test with large data
    std::string large_data(1024 * 1024, 'x'); // 1MB
    buffer_->add(large_data);
    
    EXPECT_EQ(buffer_->length(), large_data.size());
    
    // Test partial drain
    buffer_->drain(512 * 1024);
    EXPECT_EQ(buffer_->length(), 512 * 1024);
    
    // Test copy out
    std::vector<char> output(512 * 1024);
    buffer_->copyOut(0, output.size(), output.data());
    EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](char c) { return c == 'x'; }));
}

TEST_F(BufferTest, ManySmallWrites) {
    // Test many small writes
    for (int i = 0; i < 1000; ++i) {
        buffer_->add(std::to_string(i));
    }
    
    EXPECT_GT(buffer_->length(), 0u);
    EXPECT_GT(buffer_->sliceCount(), 0u);
    
    // Linearize to verify data integrity
    void* data = buffer_->linearize(buffer_->length());
    EXPECT_NE(data, nullptr);
}

TEST_F(BufferTest, InterleavedOperations) {
    // Test interleaved add/drain operations
    for (int i = 0; i < 100; ++i) {
        buffer_->add("Hello");
        if (i % 2 == 0) {
            buffer_->drain(3);
        }
    }
    
    size_t expected_length = 100 * 5 - 50 * 3;
    EXPECT_EQ(buffer_->length(), expected_length);
}

// Edge Cases
TEST_F(BufferTest, EmptyOperations) {
    // Test operations on empty buffer
    EXPECT_EQ(buffer_->toString(), "");
    EXPECT_NO_THROW(buffer_->drain(0));
    
    auto dest = createBuffer();
    EXPECT_NO_THROW(buffer_->move(*dest));
    EXPECT_NO_THROW(buffer_->move(*dest, 0));
    
    char output[10];
    EXPECT_EQ(buffer_->copyOut(0, 10, output), 0u);
}

TEST_F(BufferTest, ZeroSizeOperations) {
    // Test zero-size operations
    buffer_->add("", 0);
    EXPECT_TRUE(buffer_->empty());
    
    buffer_->add("Hello");
    buffer_->prepend("", 0);
    EXPECT_EQ(buffer_->toString(), "Hello");
    
    auto result = buffer_->search("", 0);
    EXPECT_TRUE(result.found_);
    EXPECT_EQ(result.position_, 0u);
}