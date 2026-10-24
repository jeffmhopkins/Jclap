#pragma once

// A fixed-size single-producer single-consumer queue that never blocks or allocates, for passing
// small messages between the GUI and the audio thread (e.g. notes played on an on-screen keyboard).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace jclap::core {

template <typename T, size_t Size = 256>
class SpscQueue {
   static_assert((Size & (Size - 1)) == 0, "Size must be a power of two");

public:
   // Producer. Returns false (and drops the item) when the queue is full.
   bool push(const T &item) {
      const uint32_t w = write_.load(std::memory_order_relaxed);
      if (w - read_.load(std::memory_order_acquire) >= Size)
         return false;
      items_[w & (Size - 1)] = item;
      write_.store(w + 1, std::memory_order_release);
      return true;
   }

   // How many items are waiting. Exact for the producer's own pushes; the consumer may have taken
   // some since, so it can only be higher than the truth, never lower.
   size_t size() const { return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire); }
   static constexpr size_t capacity() { return Size; }

   // Consumer. Returns false when the queue is empty.
   bool pop(T &item) {
      const uint32_t r = read_.load(std::memory_order_relaxed);
      if (r == write_.load(std::memory_order_acquire))
         return false;
      item = items_[r & (Size - 1)];
      read_.store(r + 1, std::memory_order_release);
      return true;
   }

private:
   std::array<T, Size> items_{};
   std::atomic<uint32_t> write_{0}, read_{0};
};

} // namespace jclap::core
