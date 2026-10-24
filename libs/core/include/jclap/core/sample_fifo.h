#pragma once

// Hands the latest audio samples from the audio thread to the GUI, for scopes and waveform views.
// One writer (the audio thread) and one reader (the GUI timer). The writer never blocks or
// allocates; the reader copies the most recent samples and may see a few overwritten mid-copy,
// which a display doesn't notice. Each sample is a relaxed atomic, so that overlap is well defined
// (and quiet under ThreadSanitizer) at no cost: on every target it is a plain load or store.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace jclap::core {

template <size_t Size = 4096>
class SampleFifo {
   static_assert((Size & (Size - 1)) == 0, "Size must be a power of two");

public:
   // Audio thread.
   void write(const float *samples, uint32_t count) {
      uint32_t w = writePos_.load(std::memory_order_relaxed);
      for (uint32_t i = 0; i < count; ++i)
         buffer_[(w + i) & (Size - 1)].store(samples[i], std::memory_order_relaxed);
      writePos_.store(w + count, std::memory_order_release);
   }
   void push(float sample) { write(&sample, 1); }

   // GUI thread: copies the newest `count` samples (oldest first) into `out`. Returns how many
   // samples have ever been written, so the caller can tell whether anything new arrived.
   uint32_t read(float *out, size_t count) const {
      const uint32_t w = writePos_.load(std::memory_order_acquire);
      if (count > Size)
         count = Size;
      for (size_t i = 0; i < count; ++i)
         out[i] = buffer_[(w - count + i) & (Size - 1)].load(std::memory_order_relaxed);
      return w;
   }

   static constexpr size_t capacity() { return Size; }

private:
   std::atomic<float> buffer_[Size] = {};
   static_assert(std::atomic<float>::is_always_lock_free);
   std::atomic<uint32_t> writePos_{0};
};

} // namespace jclap::core
