// The sending end, on the audio thread: cuts the host's audio into frames (resampled to 48 kHz for
// Opus), codes them with the MIDI that falls in each, and hands finished packets to a sink. The
// sink is the queue to the network thread, so nothing here touches a socket.
#pragma once

#include <cstddef>
#include <cstdint>

#include "codec.h"
#include "protocol.h"
#include "resampler.h"

namespace jtunnel {

class PacketSink {
public:
   virtual ~PacketSink() = default;
   virtual void send(const uint8_t *data, size_t size) = 0;
};

// An event in the wire's form (see protocol.h), at a sample offset in the block.
struct TimedMidi {
   uint32_t time;
   uint8_t size;
   uint8_t data[kMaxEventBytes];
};

class Sender {
public:
   // Main thread: allocates the encoder. `seed` makes stream ids differ between instances.
   bool setup(double hostRate, uint32_t seed);

   // Audio thread, before each block. A new codec starts a new stream.
   void configure(Codec codec, int bitrateKbps, bool redundancy);
   // Starts a new stream with the next frame, as after a gap in the audio.
   void restart();

   void process(const float *left, const float *right, uint32_t frames, const TimedMidi *midi, int midiCount,
                PacketSink &sink);

   uint32_t streamId() const { return streamId_; }
   uint32_t streamRate() const { return streamRate_; }
   Codec codec() const { return codec_; }
   int frameSamples() const { return frameSamples_; }

private:
   void begin();
   void append(const float *left, const float *right, int n, PacketSink &sink);
   void emitFrame(PacketSink &sink);
   uint32_t nextRandom();

   // Room for the longest SysEx message and then some.
   static constexpr int kMaxPending = 512;
   struct PendingMidi {
      uint64_t pos; // stream sample
      uint8_t size;
      uint8_t data[kMaxEventBytes];
   };

   double hostRate_ = 48000.0;
   Codec codec_ = Codec::Opus;
   int bitrate_ = 192;
   bool redundancy_ = true;
   bool needsBegin_ = true;

   uint32_t streamRate_ = kOpusSampleRate;
   int frameSamples_ = kOpusFrameSamples;
   bool resample_ = false;
   double ratio_ = 1.0;
   Resampler resampler_;

   float frame_[kMaxFrameSamples * kChannels] = {};
   int frameFill_ = 0;
   uint64_t frameStart_ = 0; // stream sample at frame_[0]

   uint32_t rng_ = 1;
   uint32_t streamId_ = 0, seq_ = 0;
   Frame current_, previous_;
   bool hasPrevious_ = false;
   PendingMidi pending_[kMaxPending];
   int pendingCount_ = 0;
   Encoder encoder_;
   uint8_t packet_[kMaxPacketBytes];
};

} // namespace jtunnel
