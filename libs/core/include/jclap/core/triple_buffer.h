#pragma once

// Hands a whole struct from one thread to another without locks or waiting, for state the GUI draws
// that the audio thread works out (positions, levels, meters). The writer fills back(), then
// publish(); the reader calls update() and reads front(). Each side always has a slot of its own, so
// neither ever sees a half-written struct, and the reader just gets the newest one published.
//
//    // audio thread                  // GUI timer
//    Frame &f = buffer.back();         if (buffer.update())
//    f.level = level;                     draw(buffer.front());
//    buffer.publish();

#include <atomic>
#include <cstdint>

namespace jclap::core {

template <typename T>
class TripleBuffer {
public:
   // Writer.
   T &back() { return slots_[back_]; }
   void publish() { back_ = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel) & kIndex; }

   // Reader. Takes the newest published slot, if there is one it hasn't seen; returns whether it did.
   bool update() {
      if (!(middle_.load(std::memory_order_relaxed) & kFresh))
         return false;
      front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndex;
      return true;
   }
   const T &front() const { return slots_[front_]; }

private:
   static constexpr uint8_t kIndex = 3, kFresh = 4;

   T slots_[3]{};
   std::atomic<uint8_t> middle_{1};
   uint8_t back_ = 0, front_ = 2;
};

} // namespace jclap::core
