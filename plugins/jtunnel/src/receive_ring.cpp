#include "receive_ring.h"

namespace jtunnel {

ReceiveRing::ReceiveRing() : slots_(new Slot[kSlots]) {}

void ReceiveRing::clear() {
   generation_.fetch_add(1, std::memory_order_acq_rel);
   active_.store(false, std::memory_order_relaxed);
   generation_.fetch_add(1, std::memory_order_release);
}

void ReceiveRing::store(const AudioHeader &header, const Frame &frame) {
   const bool active = active_.load(std::memory_order_relaxed);
   const uint32_t stream = streamId_.load(std::memory_order_relaxed);
   if (!active || stream != header.streamId) {
      // A new stream: publish its format first, so the audio thread never pairs frames with the
      // wrong codec or rate.
      generation_.fetch_add(1, std::memory_order_acq_rel);
      streamId_.store(header.streamId, std::memory_order_relaxed);
      sampleRate_.store(header.sampleRate, std::memory_order_relaxed);
      codec_.store(static_cast<uint32_t>(header.codec), std::memory_order_relaxed);
      frameSamples_.store(header.frameSamples, std::memory_order_relaxed);
      newest_.store(frame.seq, std::memory_order_relaxed);
      active_.store(true, std::memory_order_relaxed);
      generation_.fetch_add(1, std::memory_order_release);
   }

   // Too late: the audio thread has already played past it (or concealed it).
   if (readStream_.load(std::memory_order_acquire) == header.streamId &&
       seqDiff(frame.seq, readSeq_.load(std::memory_order_relaxed)) < 0)
      return;
   // Implausibly far behind the newest frame: a stray from long ago.
   const uint32_t newest = newest_.load(std::memory_order_relaxed);
   if (seqDiff(frame.seq, newest) <= -static_cast<int32_t>(kSlots / 2))
      return;

   Slot &slot = slots_[frame.seq % kSlots];
   uint32_t state = slot.state.load(std::memory_order_acquire);
   if (state == Ready) {
      // Same frame already here (the redundant copy of one that arrived): keep it. A newer frame of
      // this stream in the slot means this one is ancient: drop it. Anything else is stale.
      if (slot.streamId == header.streamId && seqDiff(slot.seq, frame.seq) >= 0)
         return;
   }
   else if (state != Empty)
      return;
   if (!slot.state.compare_exchange_strong(state, Writing, std::memory_order_acquire))
      return;
   slot.streamId = header.streamId;
   slot.seq = frame.seq;
   slot.frame = frame;
   slot.state.store(Ready, std::memory_order_release);

   if (seqDiff(frame.seq, newest) > 0)
      newest_.store(frame.seq, std::memory_order_release);
}

StreamInfo ReceiveRing::info() const {
   StreamInfo info;
   for (int attempt = 0; attempt < 8; ++attempt) {
      const uint32_t before = generation_.load(std::memory_order_acquire);
      if (before & 1)
         continue;
      info.active = active_.load(std::memory_order_relaxed);
      info.streamId = streamId_.load(std::memory_order_relaxed);
      info.sampleRate = sampleRate_.load(std::memory_order_relaxed);
      info.codec = static_cast<Codec>(codec_.load(std::memory_order_relaxed));
      info.frameSamples = static_cast<uint16_t>(frameSamples_.load(std::memory_order_relaxed));
      info.newest = newest_.load(std::memory_order_acquire);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (generation_.load(std::memory_order_relaxed) == before)
         return info;
   }
   // Mid-change every time we looked: report nothing this block rather than a torn mix.
   return StreamInfo{};
}

bool ReceiveRing::take(uint32_t streamId, uint32_t seq, Frame &out) {
   Slot &slot = slots_[seq % kSlots];
   uint32_t state = Ready;
   if (!slot.state.compare_exchange_strong(state, Reading, std::memory_order_acquire))
      return false;
   if (slot.streamId == streamId && slot.seq == seq) {
      out = slot.frame;
      slot.state.store(Empty, std::memory_order_release);
      return true;
   }
   // A frame from another stream, or one this stream already passed: free the slot. A newer frame
   // (from a full ring later) stays.
   const bool stale = slot.streamId != streamId || seqDiff(slot.seq, seq) < 0;
   slot.state.store(stale ? Empty : Ready, std::memory_order_release);
   return false;
}

} // namespace jtunnel
