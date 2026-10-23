#pragma once

// Hands the latest audio samples from the audio thread to the GUI, for scopes and waveform views.
// One writer (the audio thread) and one reader (the GUI timer). The writer never blocks or
// allocates; the reader copies the most recent samples and may see a few overwritten mid-copy,
// which a display doesn't notice.

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
         buffer_[(w + i) & (Size - 1)] = samples[i];
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
         out[i] = buffer_[(w - count + i) & (Size - 1)];
      return w;
   }

   static constexpr size_t capacity() { return Size; }

private:
   float buffer_[Size] = {};
   std::atomic<uint32_t> writePos_{0};
};

} // namespace jclap::core
