#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>

#include "mcp/buffer.h"

namespace mcp {

namespace {

// Constants for buffer optimization
constexpr size_t kDefaultSliceSize = 16384;  // 16KB default slice
constexpr size_t kInlineDataSize = 512;      // Inline storage for small slices
constexpr size_t kDefaultReservationSize = 4096;  // Default reservation size

// Thread-local memory pool
class SlicePool {
 public:
  static std::shared_ptr<void> allocate(size_t size) {
    thread_local std::vector<std::pair<size_t, std::shared_ptr<void>>> pool;

    // Try to find a suitable slice in the pool
    for (auto it = pool.begin(); it != pool.end(); ++it) {
      if (it->first >= size) {
        auto result = it->second;
        pool.erase(it);
        return result;
      }
    }

    // Allocate new memory
    return std::shared_ptr<void>(new uint8_t[size], [size](void* p) {
      // Return to pool if not too many
      thread_local size_t pool_size = 0;
      if (pool_size < 8) {
        thread_local std::vector<std::pair<size_t, std::shared_ptr<void>>> pool;
        pool.emplace_back(size, std::shared_ptr<void>(p, [](void* ptr) {
                            delete[] static_cast<uint8_t*>(ptr);
                          }));
        pool_size++;
      } else {
        delete[] static_cast<uint8_t*>(p);
      }
    });
  }
};

}  // namespace

// Forward declaration
class Slice;
class SliceDeque;

// Slice implementation - represents a contiguous memory region
class Slice {
 public:
  Slice() = default;

  // Allocate new memory of given size (with inline optimization)
  explicit Slice(size_t size) : immutable_(false) {
    if (size <= kInlineDataSize) {
      base_ = inline_data_;
      capacity_ = kInlineDataSize;
    } else {
      storage_ = SlicePool::allocate(size);
      base_ = static_cast<uint8_t*>(storage_.get());
      capacity_ = size;
    }
    reservable_ = 0;
  }

  explicit Slice(BufferFragmentPtr fragment)
      : base_(const_cast<uint8_t*>(
            static_cast<const uint8_t*>(fragment->data()))),
        capacity_(fragment->size()),
        reservable_(fragment->size()),
        immutable_(true),
        fragment_(std::move(fragment)) {}

  ~Slice() {
    // Fragment will be destroyed and done() called when unique_ptr is destroyed
  }

  Slice(const Slice&) = delete;
  Slice& operator=(const Slice&) = delete;

  Slice(Slice&& other) noexcept
      : base_(other.base_),
        capacity_(other.capacity_),
        reservable_(other.reservable_),
        immutable_(other.immutable_),
        storage_(std::move(other.storage_)),
        fragment_(std::move(other.fragment_)),
        drain_trackers_(std::move(other.drain_trackers_)) {
    if (other.base_ == other.inline_data_) {
      // Need to copy inline data
      std::memcpy(inline_data_, other.inline_data_, kInlineDataSize);
      base_ = inline_data_;
    }
    other.base_ = nullptr;
    other.capacity_ = 0;
    other.reservable_ = 0;
  }

  Slice& operator=(Slice&& other) noexcept {
    if (this != &other) {
      base_ = other.base_;
      capacity_ = other.capacity_;
      reservable_ = other.reservable_;
      immutable_ = other.immutable_;
      storage_ = std::move(other.storage_);
      fragment_ = std::move(other.fragment_);
      drain_trackers_ = std::move(other.drain_trackers_);

      if (other.base_ == other.inline_data_) {
        std::memcpy(inline_data_, other.inline_data_, kInlineDataSize);
        base_ = inline_data_;
      }
      other.base_ = nullptr;
      other.capacity_ = 0;
      other.reservable_ = 0;
    }
    return *this;
  }

  // Get data pointer and length
  const uint8_t* data() const { return base_; }
  size_t dataSize() const { return reservable_; }

  // Get available space for appending
  size_t reservableSize() const {
    return immutable_ ? 0 : capacity_ - reservable_;
  }

  // Reserve space for writing
  void reserve(RawSlice& slice, size_t size) {
    if (immutable_ || size > reservableSize()) {
      throw std::runtime_error("Cannot reserve requested size");
    }

    slice.mem_ =
        const_cast<void*>(static_cast<const void*>(base_ + reservable_));
    slice.len_ = size;
  }

  // Commit reserved space
  void commit(const RawSlice& slice, size_t size) {
    if (immutable_) {
      throw std::runtime_error("Cannot commit to immutable slice");
    }

    // Validate the commit
    if (static_cast<uint8_t*>(slice.mem_) != base_ + reservable_ ||
        size > slice.len_) {
      throw std::runtime_error("Invalid commit");
    }

    reservable_ += size;
  }

  // Append data to slice
  size_t append(const void* data, size_t size) {
    if (immutable_)
      return 0;

    size_t to_copy = std::min(size, reservableSize());
    if (to_copy > 0) {
      std::memcpy(base_ + reservable_, data, to_copy);
      reservable_ += to_copy;
    }
    return to_copy;
  }

  // Prepend data to slice (requires moving existing data)
  size_t prepend(const void* data, size_t size) {
    if (immutable_ || size > capacity_ - reservable_)
      return 0;

    // Move existing data forward
    if (reservable_ > 0) {
      std::memmove(base_ + size, base_, reservable_);
    }

    // Copy new data to front
    std::memcpy(base_, data, size);
    reservable_ += size;
    return size;
  }

  // Drain data from front
  void drain(size_t size) {
    if (size > reservable_) {
      throw std::runtime_error("Drain size exceeds data size");
    }

    if (size == reservable_) {
      // Draining all data
      base_ += size;
      reservable_ = 0;
      capacity_ -= size;

      // Notify drain trackers
      for (auto& tracker : drain_trackers_) {
        tracker->onDrain(size);
      }
      drain_trackers_.clear();
    } else {
      // Partial drain
      base_ += size;
      reservable_ -= size;
      capacity_ -= size;
    }
  }

  // Get raw slice for I/O
  void getRawSlice(RawSlice& slice) const {
    slice.mem_ = const_cast<void*>(static_cast<const void*>(base_));
    slice.len_ = reservable_;
  }

  void getRawSlice(ConstRawSlice& slice) const {
    slice.mem_ = base_;
    slice.len_ = reservable_;
  }

  // Attach drain tracker
  void attachDrainTracker(DrainTrackerPtr tracker) {
    drain_trackers_.push_back(tracker);
  }

  bool isImmutable() const { return immutable_; }
  size_t capacity() const { return capacity_; }
  const uint8_t* base() const { return base_; }
  uint8_t* mutableBase() { return base_; }
  size_t& mutableReservable() { return reservable_; }

 private:
  uint8_t* base_{nullptr};  // Pointer to actual data
  size_t capacity_{0};      // Total capacity of this slice
  size_t reservable_{0};    // Amount of data currently reserved/used
  bool immutable_{false};   // Whether slice is read-only

  // Storage management
  std::shared_ptr<void> storage_;         // Owned storage (if any)
  BufferFragmentPtr fragment_;            // External fragment (if any)
  uint8_t inline_data_[kInlineDataSize];  // Inline storage for small data

  // Drain tracking
  std::vector<DrainTrackerPtr> drain_trackers_;
};

// SliceDeque - container for managing slices
class SliceDeque {
 public:
  using SliceVector = std::vector<Slice>;

  void push_back(Slice&& slice) {
    slices_.push_back(std::move(slice));
    updateSize(slices_.back().dataSize());
  }

  void push_front(Slice&& slice) {
    size_t size = slice.dataSize();
    slices_.insert(slices_.begin(), std::move(slice));
    updateSize(size);
  }

  Slice& back() { return slices_.back(); }
  const Slice& back() const { return slices_.back(); }

  Slice& front() { return slices_.front(); }
  const Slice& front() const { return slices_.front(); }

  bool empty() const { return slices_.empty(); }
  size_t size() const { return slices_.size(); }

  SliceVector::iterator begin() { return slices_.begin(); }
  SliceVector::iterator end() { return slices_.end(); }
  SliceVector::const_iterator begin() const { return slices_.begin(); }
  SliceVector::const_iterator end() const { return slices_.end(); }

  size_t byteSize() const { return byte_size_; }

  void popFront() {
    if (!slices_.empty()) {
      updateSize(-static_cast<ssize_t>(slices_.front().dataSize()));
      slices_.erase(slices_.begin());
    }
  }

  // Update byte size when slice content changes
  void updateByteSize() {
    byte_size_ = 0;
    for (const auto& slice : slices_) {
      byte_size_ += slice.dataSize();
    }
  }

  void updateSize(ssize_t delta) {
    byte_size_ = static_cast<size_t>(static_cast<ssize_t>(byte_size_) + delta);
  }

  // Access to underlying vector for reservation
  SliceVector slices_;

 private:
  size_t byte_size_{0};
};

// Reservation implementation
class ReservationImpl : public Reservation {
 public:
  ReservationImpl(SliceDeque* slices_deque, BufferMemoryAccountPtr account)
      : slices_deque_(slices_deque), account_(account) {
    // Create reservation slices from available space in existing slices
    size_t space_needed = kDefaultReservationSize;

    // First try to use existing slices
    for (auto& slice : slices_deque->slices_) {
      if (!slice.isImmutable() && slice.reservableSize() > 0) {
        RawSlice raw_slice;
        slice.reserve(raw_slice, slice.reservableSize());
        slices_.push_back(raw_slice);
        length_ += raw_slice.len_;
        space_needed = 0;
        break;
      }
    }

    // If we need more space, allocate new slices
    while (space_needed > 0) {
      size_t alloc_size = std::min(space_needed, kDefaultSliceSize);
      slices_deque->push_back(Slice(alloc_size));

      RawSlice raw_slice;
      slices_deque->back().reserve(raw_slice, alloc_size);
      slices_.push_back(raw_slice);
      length_ += alloc_size;

      if (account) {
        account->charge(alloc_size);
      }

      space_needed -= alloc_size;
    }
  }

  RawSlice* slices() override { return slices_.data(); }
  size_t numSlices() const override { return slices_.size(); }
  size_t length() const override { return length_; }

  void commit(size_t size) override {
    if (size > length_) {
      throw std::runtime_error("Commit size exceeds reservation");
    }

    // Update the actual slices with committed data
    size_t remaining = size;
    size_t slice_idx = slices_deque_->size() - slices_.size();

    for (auto& raw_slice : slices_) {
      if (remaining == 0)
        break;

      size_t to_commit = std::min(remaining, raw_slice.len_);
      slices_deque_->slices_[slice_idx].commit(raw_slice, to_commit);
      remaining -= to_commit;
      slice_idx++;
      slices_deque_->updateSize(to_commit);
    }
  }

 private:
  SliceDeque* slices_deque_;
  BufferMemoryAccountPtr account_;
  std::vector<RawSlice> slices_;
  size_t length_{0};
};

// OwnedBuffer::Impl implementation
class OwnedBuffer::Impl {
 public:
  Impl() = default;

  explicit Impl(BufferMemoryAccountPtr account) : account_(account) {}

  void add(const void* data, size_t size) {
    if (size == 0)
      return;

    const uint8_t* src = static_cast<const uint8_t*>(data);

    while (size > 0) {
      // Try to append to last slice
      if (!slices_.empty() && !slices_.back().isImmutable()) {
        size_t copied = slices_.back().append(src, size);
        slices_.updateSize(
            copied);  // Update byte size with actual copied amount
        src += copied;
        size -= copied;
        if (size == 0)
          break;
      }

      // Need new slice
      size_t slice_size = std::max(size, kDefaultSliceSize);
      slices_.push_back(Slice(slice_size));

      if (account_) {
        account_->charge(slice_size);
      }
    }
  }

  void addBufferFragment(BufferFragmentPtr fragment) {
    size_t size = fragment->size();
    slices_.push_back(Slice(std::move(fragment)));

    if (account_) {
      account_->charge(size);
    }
  }

  void prepend(const void* data, size_t size) {
    if (size == 0)
      return;

    // Try to prepend to first slice
    if (!slices_.empty() && !slices_.front().isImmutable()) {
      size_t prepended = slices_.front().prepend(data, size);
      if (prepended > 0) {
        slices_.updateSize(prepended);
      }
      if (prepended == size)
        return;

      // Partial prepend, need new slice for rest
      data = static_cast<const uint8_t*>(data) + prepended;
      size -= prepended;
    }

    // Create new slice for remaining data
    Slice new_slice(size);
    new_slice.append(data, size);
    slices_.push_front(std::move(new_slice));

    if (account_) {
      account_->charge(size);
    }
  }

  void drain(size_t size) {
    if (size > length()) {
      throw std::runtime_error("Drain size exceeds buffer length");
    }

    while (size > 0 && !slices_.empty()) {
      Slice& front = slices_.front();
      size_t slice_size = front.dataSize();

      if (size >= slice_size) {
        // Drain entire slice
        size -= slice_size;

        // Call drain to trigger trackers
        front.drain(slice_size);

        if (account_) {
          account_->credit(front.capacity());
        }

        // Remove the now-empty slice and update size
        slices_.slices_.erase(slices_.slices_.begin());
        slices_.updateSize(-static_cast<ssize_t>(slice_size));
      } else {
        // Partial drain
        slices_.updateSize(-static_cast<ssize_t>(size));
        front.drain(size);
        size = 0;
      }
    }
  }

  void move(Buffer& destination, size_t length) {
    OwnedBuffer* dest = dynamic_cast<OwnedBuffer*>(&destination);
    if (!dest) {
      throw std::runtime_error("Can only move to OwnedBuffer");
    }

    if (length == 0)
      return;
    if (length > this->length()) {
      length = this->length();
    }

    size_t remaining = length;

    while (remaining > 0 && !slices_.empty()) {
      Slice& front = slices_.front();
      size_t slice_size = front.dataSize();

      if (remaining >= slice_size) {
        // Move entire slice
        remaining -= slice_size;
        dest->impl_->slices_.push_back(std::move(front));
        // Remove the now-empty slice from source
        slices_.slices_.erase(slices_.slices_.begin());
        slices_.updateByteSize();  // Recalculate size after move
      } else {
        // Partial move - need to split slice
        // Copy data to destination
        dest->add(front.data(), remaining);

        // Drain from source
        slices_.updateSize(-static_cast<ssize_t>(remaining));
        front.drain(remaining);
        remaining = 0;
      }
    }
  }

  std::unique_ptr<Reservation> reserveForRead() {
    return std::make_unique<ReservationImpl>(&slices_, account_);
  }

  void* reserveSingleSlice(size_t length, RawSlice& slice) {
    // Allocate new slice
    slices_.push_back(Slice(length));
    slices_.back().reserve(slice, length);

    if (account_) {
      account_->charge(length);
    }

    return slice.mem_;
  }

  void commit(RawSlice& slice, size_t size) {
    // Find the slice containing this reservation
    for (auto& s : slices_.slices_) {
      if (static_cast<uint8_t*>(slice.mem_) >= s.base() &&
          static_cast<uint8_t*>(slice.mem_) < s.base() + s.capacity()) {
        s.commit(slice, size);
        slices_.updateSize(size);  // Update total size
        return;
      }
    }
    throw std::runtime_error("Slice not found for commit");
  }

  size_t getRawSlices(RawSlice* out, size_t max_slices) const {
    size_t num_slices = 0;

    for (const auto& slice : slices_.slices_) {
      if (num_slices >= max_slices)
        break;
      if (slice.dataSize() > 0) {
        slice.getRawSlice(out[num_slices]);
        num_slices++;
      }
    }

    return num_slices;
  }

  size_t getRawSlices(ConstRawSlice* out, size_t max_slices) const {
    size_t num_slices = 0;

    for (const auto& slice : slices_.slices_) {
      if (num_slices >= max_slices)
        break;
      if (slice.dataSize() > 0) {
        slice.getRawSlice(out[num_slices]);
        num_slices++;
      }
    }

    return num_slices;
  }

  RawSlice frontSlice() const {
    RawSlice slice;
    if (!slices_.empty() && slices_.front().dataSize() > 0) {
      slices_.front().getRawSlice(slice);
    } else {
      slice.mem_ = nullptr;
      slice.len_ = 0;
    }
    return slice;
  }

  void* linearize(size_t size) {
    if (size > length()) {
      size = length();
    }

    if (size == 0)
      return nullptr;

    // Check if already linear
    if (!slices_.empty() && slices_.front().dataSize() >= size) {
      return const_cast<void*>(
          static_cast<const void*>(slices_.front().data()));
    }

    // Need to coalesce
    Slice new_slice(size);
    size_t offset = 0;

    // Copy data from all slices
    uint8_t* dest = new_slice.mutableBase();
    for (auto& slice : slices_.slices_) {
      if (offset >= size)
        break;

      size_t to_copy = std::min(size - offset, slice.dataSize());
      std::memcpy(dest + offset, slice.data(), to_copy);
      offset += to_copy;
    }
    new_slice.mutableReservable() = size;

    // Replace slices with linearized version
    drain(size);
    slices_.push_front(std::move(new_slice));

    return const_cast<void*>(static_cast<const void*>(slices_.front().data()));
  }

  BufferSearchResult search(const void* pattern,
                            size_t pattern_size,
                            size_t start_position) const {
    if (pattern_size == 0) {
      return {start_position, true};
    }

    // Simple search implementation
    size_t position = 0;
    const uint8_t* pattern_bytes = static_cast<const uint8_t*>(pattern);

    for (const auto& slice : slices_.slices_) {
      if (position + slice.dataSize() <= start_position) {
        position += slice.dataSize();
        continue;
      }

      const uint8_t* data = slice.data();
      size_t offset =
          (position < start_position) ? start_position - position : 0;

      for (size_t i = offset; i + pattern_size <= slice.dataSize(); ++i) {
        if (std::memcmp(data + i, pattern_bytes, pattern_size) == 0) {
          return {position + i, true};
        }
      }

      position += slice.dataSize();
    }

    return {0, false};
  }

  bool startsWith(const void* pattern, size_t pattern_size) const {
    if (pattern_size == 0)
      return true;
    if (pattern_size > length())
      return false;

    const uint8_t* pattern_bytes = static_cast<const uint8_t*>(pattern);
    size_t compared = 0;

    for (const auto& slice : slices_.slices_) {
      size_t to_compare = std::min(pattern_size - compared, slice.dataSize());
      if (std::memcmp(slice.data(), pattern_bytes + compared, to_compare) !=
          0) {
        return false;
      }
      compared += to_compare;
      if (compared >= pattern_size) {
        return true;
      }
    }

    return false;
  }

  size_t length() const { return slices_.byteSize(); }

  size_t sliceCount() const { return slices_.size(); }

  void attachDrainTracker(DrainTrackerPtr tracker) {
    // TODO: drain trackers can be attached to track specific data ranges
    // For simplicity, we attach to all current slices
    for (auto& slice : slices_.slices_) {
      if (slice.dataSize() > 0) {
        slice.attachDrainTracker(tracker);
      }
    }
  }

  size_t copyOut(size_t start, size_t size, void* data) const {
    if (start >= length())
      return 0;

    size_t available = length() - start;
    size = std::min(size, available);

    uint8_t* dst = static_cast<uint8_t*>(data);
    size_t position = 0;
    size_t copied = 0;

    for (const auto& slice : slices_.slices_) {
      if (position + slice.dataSize() <= start) {
        position += slice.dataSize();
        continue;
      }

      size_t offset = (position < start) ? start - position : 0;
      size_t to_copy = std::min(size - copied, slice.dataSize() - offset);

      std::memcpy(dst + copied, slice.data() + offset, to_copy);
      copied += to_copy;

      if (copied >= size)
        break;
      position += slice.dataSize();
    }

    return copied;
  }

  std::string toString() const {
    std::string result;
    result.reserve(length());

    for (const auto& slice : slices_.slices_) {
      result.append(reinterpret_cast<const char*>(slice.data()),
                    slice.dataSize());
    }

    return result;
  }

  SliceDeque slices_;
  BufferMemoryAccountPtr account_;
};

// OwnedBuffer public interface implementation
OwnedBuffer::OwnedBuffer() : impl_(std::make_unique<Impl>()) {}

OwnedBuffer::OwnedBuffer(BufferMemoryAccountPtr account)
    : impl_(std::make_unique<Impl>(account)) {}

OwnedBuffer::OwnedBuffer(OwnedBuffer&& other) noexcept = default;
OwnedBuffer& OwnedBuffer::operator=(OwnedBuffer&& other) noexcept = default;
OwnedBuffer::~OwnedBuffer() = default;

void OwnedBuffer::add(const void* data, size_t size) { impl_->add(data, size); }

void OwnedBuffer::add(const std::string& data) {
  impl_->add(data.data(), data.size());
}

void OwnedBuffer::add(const Buffer& data) {
  // Copy data from other buffer
  constexpr size_t kMaxSlices = 16;
  ConstRawSlice slices[kMaxSlices];

  size_t offset = 0;
  while (offset < data.length()) {
    size_t num_slices = data.getRawSlices(slices, kMaxSlices);
    for (size_t i = 0; i < num_slices; ++i) {
      add(slices[i].mem_, slices[i].len_);
    }
    offset += data.length();  // This will break the loop
  }
}

void OwnedBuffer::addBufferFragment(BufferFragmentPtr fragment) {
  impl_->addBufferFragment(std::move(fragment));
}

void OwnedBuffer::prepend(const void* data, size_t size) {
  impl_->prepend(data, size);
}

void OwnedBuffer::prepend(const std::string& data) {
  impl_->prepend(data.data(), data.size());
}

void OwnedBuffer::drain(size_t size) { impl_->drain(size); }

void OwnedBuffer::move(Buffer& destination) {
  impl_->move(destination, impl_->length());
}

void OwnedBuffer::move(Buffer& destination, size_t length) {
  impl_->move(destination, length);
}

std::unique_ptr<Reservation> OwnedBuffer::reserveForRead() {
  return impl_->reserveForRead();
}

void* OwnedBuffer::reserveSingleSlice(size_t length, RawSlice& slice) {
  return impl_->reserveSingleSlice(length, slice);
}

void OwnedBuffer::commit(RawSlice& slice, size_t size) {
  impl_->commit(slice, size);
}

size_t OwnedBuffer::getRawSlices(RawSlice* slices, size_t max_slices) const {
  return impl_->getRawSlices(slices, max_slices);
}

size_t OwnedBuffer::getRawSlices(ConstRawSlice* slices,
                                 size_t max_slices) const {
  return impl_->getRawSlices(slices, max_slices);
}

RawSlice OwnedBuffer::frontSlice() const { return impl_->frontSlice(); }

void* OwnedBuffer::linearize(size_t size) { return impl_->linearize(size); }

BufferSearchResult OwnedBuffer::search(const void* pattern,
                                       size_t pattern_size,
                                       size_t start_position) const {
  return impl_->search(pattern, pattern_size, start_position);
}

bool OwnedBuffer::startsWith(const void* pattern, size_t pattern_size) const {
  return impl_->startsWith(pattern, pattern_size);
}

size_t OwnedBuffer::length() const { return impl_->length(); }

size_t OwnedBuffer::sliceCount() const { return impl_->sliceCount(); }

void OwnedBuffer::attachDrainTracker(DrainTrackerPtr tracker) {
  impl_->attachDrainTracker(tracker);
}

size_t OwnedBuffer::copyOut(size_t start, size_t size, void* data) const {
  return impl_->copyOut(start, size, data);
}

std::string OwnedBuffer::toString() const { return impl_->toString(); }

// WatermarkBuffer implementation
void WatermarkBuffer::setWatermarks(uint32_t high_watermark,
                                    uint32_t overflow_multiplier) {
  high_watermark_ = high_watermark;
  low_watermark_ = high_watermark_ / 2;  // Default to half of high
  overflow_watermark_ = high_watermark_ * overflow_multiplier;
}

bool WatermarkBuffer::aboveHighWatermark() const {
  return above_high_watermark_;
}

void WatermarkBuffer::checkWatermarks(size_t old_size) {
  size_t new_size = length();

  // Check transitions
  if (old_size <= overflow_watermark_ && new_size > overflow_watermark_) {
    above_overflow_watermark_ = true;
    if (above_overflow_watermark_callback_) {
      above_overflow_watermark_callback_();
    }
  }

  if (old_size <= high_watermark_ && new_size > high_watermark_) {
    above_high_watermark_ = true;
    if (above_high_watermark_callback_) {
      above_high_watermark_callback_();
    }
  }

  if (old_size >= low_watermark_ && new_size < low_watermark_ &&
      above_high_watermark_) {
    above_high_watermark_ = false;
    if (below_low_watermark_) {
      below_low_watermark_();
    }
  }
}

void WatermarkBuffer::add(const void* data, size_t size) {
  size_t old_size = length();
  OwnedBuffer::add(data, size);
  checkWatermarks(old_size);
}

void WatermarkBuffer::add(const std::string& data) {
  size_t old_size = length();
  OwnedBuffer::add(data);
  checkWatermarks(old_size);
}

void WatermarkBuffer::add(const Buffer& data) {
  size_t old_size = length();
  OwnedBuffer::add(data);
  checkWatermarks(old_size);
}

void WatermarkBuffer::drain(size_t size) {
  size_t old_size = length();
  OwnedBuffer::drain(size);
  checkWatermarks(old_size);
}

void WatermarkBuffer::move(Buffer& destination) {
  size_t old_size = length();
  OwnedBuffer::move(destination);
  checkWatermarks(old_size);
}

void WatermarkBuffer::move(Buffer& destination, size_t length) {
  size_t old_size = this->length();
  OwnedBuffer::move(destination, length);
  checkWatermarks(old_size);
}

}  // namespace mcp