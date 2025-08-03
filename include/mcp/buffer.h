#ifndef MCP_BUFFER_H
#define MCP_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mcp/compat.h"

namespace mcp {

// Forward declarations
class BufferFragment;
class Slice;
class SliceDeque;
class BufferMemoryAccount;
class Reservation;

// Raw memory slice for vectored I/O operations
struct RawSlice {
  void* mem_ = nullptr;
  size_t len_ = 0;
};

struct ConstRawSlice {
  const void* mem_ = nullptr;
  size_t len_ = 0;
};

// Buffer search result
struct BufferSearchResult {
  size_t position_;  // Position in buffer where pattern found
  bool found_;       // Whether pattern was found
};

// Interface for buffer fragments (external memory)
class BufferFragment {
public:
  virtual ~BufferFragment() = default;
  
  // Get the data pointer
  virtual const void* data() const = 0;
  
  // Get the size of the fragment
  virtual size_t size() const = 0;
  
  // Called when the buffer no longer needs this fragment
  virtual void done() = 0;
};

// Unique pointer to a buffer fragment
using BufferFragmentPtr = std::unique_ptr<BufferFragment>;

// Interface for tracking when data is drained from buffer
class DrainTracker {
public:
  virtual ~DrainTracker() = default;
  
  // Called when the associated data is fully drained
  virtual void onDrain(size_t bytes_drained) = 0;
};

using DrainTrackerPtr = std::shared_ptr<DrainTracker>;

// Main buffer interface
class Buffer {
public:
  virtual ~Buffer() = default;

  // ===== Data Addition =====
  
  // Add data to buffer
  virtual void add(const void* data, size_t size) = 0;
  virtual void add(const std::string& data) = 0;
  virtual void add(const Buffer& data) = 0;
  
  // Add buffer fragment (zero-copy)
  virtual void addBufferFragment(BufferFragmentPtr fragment) = 0;
  
  // Prepend data to front of buffer
  virtual void prepend(const void* data, size_t size) = 0;
  virtual void prepend(const std::string& data) = 0;
  
  // ===== Data Removal =====
  
  // Drain data from front of buffer
  virtual void drain(size_t size) = 0;
  
  // Move data from this buffer to another
  virtual void move(Buffer& destination) = 0;
  virtual void move(Buffer& destination, size_t length) = 0;
  
  // ===== Memory Reservation =====
  
  // Reserve space for reading (returns Reservation object)
  virtual std::unique_ptr<Reservation> reserveForRead() = 0;
  
  // Reserve a single contiguous slice
  virtual void* reserveSingleSlice(size_t length, RawSlice& slice) = 0;
  
  // Commit reserved space after writing
  virtual void commit(RawSlice& slice, size_t size) = 0;
  
  // ===== Data Access =====
  
  // Get raw slices for vectored I/O
  virtual size_t getRawSlices(RawSlice* slices, size_t max_slices) const = 0;
  virtual size_t getRawSlices(ConstRawSlice* slices, size_t max_slices) const = 0;
  
  // Get first slice
  virtual RawSlice frontSlice() const = 0;
  
  // Linearize buffer data (ensure contiguous memory)
  virtual void* linearize(size_t size) = 0;
  
  // ===== Type-Safe I/O =====
  
  // Write integers with endianness control
  template <typename T>
  void writeLEInt(T value);
  
  template <typename T>
  void writeBEInt(T value);
  
  // Read integers with endianness control
  template <typename T>
  T readLEInt();
  
  template <typename T>
  T readBEInt();
  
  // Peek at integers without consuming
  template <typename T>
  T peekLEInt(size_t offset = 0) const;
  
  template <typename T>
  T peekBEInt(size_t offset = 0) const;
  
  // ===== Search Operations =====
  
  // Search for pattern in buffer
  virtual BufferSearchResult search(const void* pattern, size_t pattern_size, 
                                   size_t start_position = 0) const = 0;
  
  // Check if buffer starts with pattern
  virtual bool startsWith(const void* pattern, size_t pattern_size) const = 0;
  
  // ===== Buffer Information =====
  
  // Get total buffer length
  virtual size_t length() const = 0;
  
  // Check if buffer is empty
  bool empty() const { return length() == 0; }
  
  // Get number of slices
  virtual size_t sliceCount() const = 0;
  
  // ===== Drain Tracking =====
  
  // Attach a drain tracker to current data
  virtual void attachDrainTracker(DrainTrackerPtr tracker) = 0;
  
  // ===== Utilities =====
  
  // Copy out data to external buffer
  virtual size_t copyOut(size_t start, size_t size, void* data) const = 0;
  
  // Get string representation (for debugging)
  virtual std::string toString() const = 0;
  
protected:
  // Helper for endianness conversion
  template <typename T>
  static T swapBytes(T value);
};

// Reservation for zero-copy reads
class Reservation {
public:
  virtual ~Reservation() = default;
  
  // Get slices available for writing
  virtual RawSlice* slices() = 0;
  virtual size_t numSlices() const = 0;
  
  // Commit written data
  virtual void commit(size_t size) = 0;
  
  // Total reservable space
  virtual size_t length() const = 0;
};

// Memory account for tracking buffer memory usage
class BufferMemoryAccount {
public:
  virtual ~BufferMemoryAccount() = default;
  
  // Charge memory to account
  virtual void charge(size_t size) = 0;
  
  // Credit memory back to account
  virtual void credit(size_t size) = 0;
  
  // Get current balance
  virtual size_t balance() const = 0;
  
  // Reset account
  virtual void reset() = 0;
};

using BufferMemoryAccountPtr = std::shared_ptr<BufferMemoryAccount>;

// Factory for creating buffers with memory accounting
class BufferFactory {
public:
  virtual ~BufferFactory() = default;
  
  // Create a new buffer
  virtual std::unique_ptr<Buffer> create() = 0;
  
  // Create a buffer with initial data
  virtual std::unique_ptr<Buffer> create(const void* data, size_t size) = 0;
  virtual std::unique_ptr<Buffer> create(const std::string& data) = 0;
  
  // Create a memory account
  virtual BufferMemoryAccountPtr createAccount() = 0;
};

// Implementation of owned buffer
class OwnedBuffer : public Buffer {
public:
  OwnedBuffer();
  explicit OwnedBuffer(BufferMemoryAccountPtr account);
  OwnedBuffer(const OwnedBuffer&) = delete;
  OwnedBuffer& operator=(const OwnedBuffer&) = delete;
  OwnedBuffer(OwnedBuffer&& other) noexcept;
  OwnedBuffer& operator=(OwnedBuffer&& other) noexcept;
  ~OwnedBuffer() override;
  
  // Buffer interface implementation
  void add(const void* data, size_t size) override;
  void add(const std::string& data) override;
  void add(const Buffer& data) override;
  void addBufferFragment(BufferFragmentPtr fragment) override;
  void prepend(const void* data, size_t size) override;
  void prepend(const std::string& data) override;
  void drain(size_t size) override;
  void move(Buffer& destination) override;
  void move(Buffer& destination, size_t length) override;
  std::unique_ptr<Reservation> reserveForRead() override;
  void* reserveSingleSlice(size_t length, RawSlice& slice) override;
  void commit(RawSlice& slice, size_t size) override;
  size_t getRawSlices(RawSlice* slices, size_t max_slices) const override;
  size_t getRawSlices(ConstRawSlice* slices, size_t max_slices) const override;
  RawSlice frontSlice() const override;
  void* linearize(size_t size) override;
  BufferSearchResult search(const void* pattern, size_t pattern_size, 
                           size_t start_position = 0) const override;
  bool startsWith(const void* pattern, size_t pattern_size) const override;
  size_t length() const override;
  size_t sliceCount() const override;
  void attachDrainTracker(DrainTrackerPtr tracker) override;
  size_t copyOut(size_t start, size_t size, void* data) const override;
  std::string toString() const override;
  
private:
  // Internal implementation details
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Watermark buffer with flow control
class WatermarkBuffer : public OwnedBuffer {
public:
  // Callback for watermark events
  using WatermarkCallback = std::function<void()>;
  
  explicit WatermarkBuffer(WatermarkCallback below_low_watermark,
                          WatermarkCallback above_high_watermark,
                          WatermarkCallback above_overflow_watermark)
      : OwnedBuffer(),
        below_low_watermark_(below_low_watermark),
        above_high_watermark_callback_(above_high_watermark),
        above_overflow_watermark_callback_(above_overflow_watermark) {}
  
  // Set watermarks (overflow = high * overflow_multiplier)
  void setWatermarks(uint32_t high_watermark, uint32_t overflow_multiplier = 5);
  
  // Check if above high watermark
  bool aboveHighWatermark() const;
  
  // Override add/drain to trigger watermark callbacks
  void add(const void* data, size_t size) override;
  void add(const std::string& data) override;
  void add(const Buffer& data) override;
  void drain(size_t size) override;
  void move(Buffer& destination) override;
  void move(Buffer& destination, size_t length) override;
  
private:
  void checkWatermarks(size_t old_size);
  
  uint32_t high_watermark_{0};
  uint32_t low_watermark_{0};
  uint32_t overflow_watermark_{0};
  WatermarkCallback below_low_watermark_;
  WatermarkCallback above_high_watermark_callback_;
  WatermarkCallback above_overflow_watermark_callback_;
  bool above_high_watermark_{false};
  bool above_overflow_watermark_{false};
};

// ===== Template Implementation =====

template <typename T>
void Buffer::writeLEInt(T value) {
  static_assert(std::is_integral<T>::value, "T must be integral");
  add(&value, sizeof(T));
}

template <typename T>
void Buffer::writeBEInt(T value) {
  static_assert(std::is_integral<T>::value, "T must be integral");
  T swapped = swapBytes(value);
  add(&swapped, sizeof(T));
}

template <typename T>
T Buffer::readLEInt() {
  static_assert(std::is_integral<T>::value, "T must be integral");
  T value;
  if (length() < sizeof(T)) {
    throw std::runtime_error("Buffer underflow");
  }
  copyOut(0, sizeof(T), &value);
  drain(sizeof(T));
  return value;
}

template <typename T>
T Buffer::readBEInt() {
  static_assert(std::is_integral<T>::value, "T must be integral");
  T value = readLEInt<T>();
  return swapBytes(value);
}

template <typename T>
T Buffer::peekLEInt(size_t offset) const {
  static_assert(std::is_integral<T>::value, "T must be integral");
  T value;
  if (length() < offset + sizeof(T)) {
    throw std::runtime_error("Buffer underflow");
  }
  copyOut(offset, sizeof(T), &value);
  return value;
}

template <typename T>
T Buffer::peekBEInt(size_t offset) const {
  static_assert(std::is_integral<T>::value, "T must be integral");
  T value = peekLEInt<T>(offset);
  return swapBytes(value);
}

template <typename T>
T Buffer::swapBytes(T value) {
  union {
    T value;
    uint8_t bytes[sizeof(T)];
  } src, dst;
  
  src.value = value;
  for (size_t i = 0; i < sizeof(T); ++i) {
    dst.bytes[i] = src.bytes[sizeof(T) - 1 - i];
  }
  return dst.value;
}

// Utility functions
inline std::unique_ptr<Buffer> createBuffer() {
  return std::make_unique<OwnedBuffer>();
}

inline std::unique_ptr<Buffer> createBuffer(const std::string& data) {
  auto buffer = createBuffer();
  buffer->add(data);
  return buffer;
}

} // namespace mcp

#endif // MCP_BUFFER_H