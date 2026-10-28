// Where received frames wait for the audio thread: one slot per sequence number (mod the ring
// size), handed over without locks. The network thread stores frames as they arrive, in any order;
// the audio thread takes them in sequence order, and simply finds a slot empty when a frame is late
// or lost.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "protocol.h"

namespace jtunnel {

// What the network thread knows about the stream it is receiving.
struct StreamInfo {
   bool active = false;
   uint32_t streamId = 0;
   uint32_t sampleRate = 0;
   Codec codec = Codec::Opus;
   uint16_t frameSamples = 0;
   uint32_t newest = 0; // the highest sequence number stored
};

class ReceiveRing {
public:
   // 512 Opus frames is 2.56 s, far more than any buffer the receiver keeps.
   static constexpr uint32_t kSlots = 512;

   ReceiveRing();

   // ---- network thread ----

   // Stores one frame. A new stream id replaces the stream (frames of the old one are dropped as
   // they are found). Late frames, ones the audio thread has already played past, are dropped.
   void store(const AudioHeader &header, const Frame &frame);
   // Forgets the stream, as when the receiver stops listening to it.
   void clear();

   // ---- audio thread ----

   StreamInfo info() const;
   // Copies out frame `seq` of `streamId` if it is here, and frees its slot.
   bool take(uint32_t streamId, uint32_t seq, Frame &out);
   // Tells the network thread where playback is, so it drops frames that come too late to play.
   void setReadPosition(uint32_t streamId, uint32_t seq) {
      readSeq_.store(seq, std::memory_order_relaxed);
      readStream_.store(streamId, std::memory_order_release);
   }

private:
   enum State : uint32_t { Empty, Writing, Ready, Reading };
   struct Slot {
      std::atomic<uint32_t> state{Empty};
      uint32_t streamId = 0, seq = 0;
      Frame frame;
   };

   std::unique_ptr<Slot[]> slots_;
   // Stream info, written by the network thread. `generation` makes the reads tear-free: it is odd
   // while a change is being written.
   std::atomic<uint32_t> generation_{0};
   std::atomic<uint32_t> streamId_{0}, sampleRate_{0}, codec_{0}, frameSamples_{0};
   std::atomic<uint32_t> newest_{0};
   std::atomic<bool> active_{false};
   std::atomic<uint32_t> readStream_{0}, readSeq_{0};
};

} // namespace jtunnel
