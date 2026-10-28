// The receiving end, on the audio thread: takes frames from the ring in order, decodes them, and
// plays them through a resampler that follows the sender's clock.
//
// Two machines' audio clocks always differ a little (tens of ppm), so a fixed buffer would slowly
// run dry or overflow. Every quarter second the receiver looks at how far ahead of playback the
// freshest audio has been over the last second (its lead) and nudges the playback rate so the lead
// settles on the Buffer setting. That lead is the room a late packet has. With Auto Buffer on, a
// dropout grows it by the length of the stall (at least 5 ms), and a long quiet spell slowly takes
// it back.
//
// A frame that is missing when its turn comes, while later ones have arrived, was lost: it is
// concealed and skipped. A frame missing with nothing after it is late (a Wi-Fi stall): playback
// conceals in place and waits for it, so a stall costs latency rather than audio.
#pragma once

#include <atomic>
#include <cstdint>

#include "codec.h"
#include "protocol.h"
#include "receive_ring.h"
#include "resampler.h"

namespace jtunnel {

struct OutMidi {
   uint32_t time;
   uint8_t data[3];
};

enum class ReceiveState : int { Idle = 0, Buffering = 1, Playing = 2 };

// What the panel shows. Written by the audio thread, read anywhere.
struct ReceiverStats {
   std::atomic<int> state{0};
   std::atomic<float> fillMs{0.0f}, targetMs{0.0f}, driftPpm{0.0f}; // driftPpm NaN while measuring
   std::atomic<uint32_t> lost{0}, concealed{0}, sampleRate{0};
   std::atomic<int> codec{0};
};

class Receiver {
public:
   // Main thread: allocates the decoder.
   bool setup(double hostRate);

   // Audio thread, before each block.
   void setBuffer(double ms, bool autoBuffer) {
      bufferMs_ = ms;
      autoBuffer_ = autoBuffer;
      if (!autoBuffer)
         autoExtraMs_ = 0.0;
   }
   // Drops the stream and starts again from whatever arrives next. Held notes are released.
   void restart() { restartRequested_ = true; }

   // Replaces left/right with what was received, and appends received MIDI to `midi` (at most
   // `capacity` events, in time order). Returns the number of events.
   // `nowUs` is the time on the clock the ring's arrival times use (or -1 if there is none).
   int process(ReceiveRing &ring, float *left, float *right, uint32_t frames, OutMidi *midi, int capacity,
               int64_t nowUs = -1);

   const ReceiverStats &stats() const { return stats_; }
   ReceiveState state() const { return state_; }
   // For tests.
   double correction() const { return correction_; }
   double integral() const { return integral_; }
   // The sender's clock against ours, as a fraction (1e-6 = 1 ppm fast).
   double drift() const { return drift_; }
   double autoExtraMs() const { return autoExtraMs_; }

private:
   void begin(const StreamInfo &info);
   void pullFrame(ReceiveRing &ring, uint32_t newest);
   void endWindow(uint32_t newest);
   void closeSegment();
   double targetSamples() const;
   void emit(uint32_t time, const uint8_t *data);
   void releaseNotes(uint32_t time);
   void publish(double fill);

   static constexpr int kMaxPending = 256;
   static constexpr int kHistory = 4;
   struct PendingMidi {
      double pos; // resampler input index
      uint8_t data[3];
   };

   double hostRate_ = 48000.0;
   double bufferMs_ = 20.0;
   bool autoBuffer_ = true;
   double autoExtraMs_ = 0.0;
   bool restartRequested_ = false;

   ReceiveState state_ = ReceiveState::Idle;
   uint32_t stream_ = 0;
   Codec codec_ = Codec::Opus;
   uint32_t streamRate_ = 0;
   int frameSamples_ = kOpusFrameSamples;
   double nominal_ = 1.0;
   uint32_t startSeq_ = 0, nextSeq_ = 0;
   int lateRun_ = 0;

   // Clock following.
   double correction_ = 0.0, integral_ = 0.0, drift_ = 0.0;
   int longestStall_ = 0;
   // Drift measurement: the finished stretches' centred least-squares sums (time x time, time x
   // offset) and length, and the stretch in progress.
   double driftTT_ = 0.0, driftTX_ = 0.0, driftTime_ = 0.0, measuredSeconds_ = 0.0;
   bool segmentOpen_ = false;
   double segmentLead_ = 0.0, segmentCorrection_ = 0.0, segmentTime_ = 0.0;
   double segN_ = 0.0, segT_ = 0.0, segX_ = 0.0, segTT_ = 0.0, segTX_ = 0.0;
   double windowMax_ = -1e18;
   double history_[kHistory] = {};
   // The same lead as it was when the newest frame arrived, for measuring the clock. Seen only at
   // block boundaries, the lead moves in whole frames: with both ends using one block size the
   // arrivals keep their place against our blocks and the lead stays put, then jumps a frame, for
   // as long as a minute at 80 ppm. Timed from arrival it moves smoothly.
   double windowMaxArrival_ = -1e18;
   // When this block started, smoothed: audio callbacks run up to a block late or early, and the
   // window max would pick out every late one. A second-order loop (half a hertz) on block time.
   double blockTime_ = 0.0, samplePeriod_ = 0.0;
   uint32_t lastFrames_ = 0;
   bool clockLocked_ = false;
   double historyArrival_[kHistory] = {};
   int historyCount_ = 0, historyNext_ = 0;
   uint32_t windowElapsed_ = 0;
   bool dropoutInWindow_ = false;
   double quietSeconds_ = 0.0;
   double fillSmoothed_ = 0.0;
   uint32_t lost_ = 0, concealed_ = 0;

   Resampler resampler_;
   double cutoffRatio_ = 0.0;
   Decoder decoder_;
   float decoded_[kMaxFrameSamples * kChannels] = {};

   PendingMidi pending_[kMaxPending];
   int pendingCount_ = 0;
   bool held_[16][128] = {};
   bool releasePending_ = false;
   OutMidi *out_ = nullptr;
   int outCount_ = 0, outCapacity_ = 0;

   ReceiverStats stats_;
};

} // namespace jtunnel
