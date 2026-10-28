#include "sender.h"

#include <algorithm>
#include <cmath>

namespace jtunnel {

bool Sender::setup(double hostRate, uint32_t seed) {
   hostRate_ = hostRate;
   rng_ = seed ? seed : 0x9e3779b9u;
   needsBegin_ = true;
   return encoder_.setup();
}

uint32_t Sender::nextRandom() {
   // xorshift32: plenty for stream ids, and safe on the audio thread.
   rng_ ^= rng_ << 13;
   rng_ ^= rng_ >> 17;
   rng_ ^= rng_ << 5;
   return rng_;
}

void Sender::configure(Codec codec, int bitrateKbps, bool redundancy) {
   if (codec != codec_) {
      codec_ = codec;
      needsBegin_ = true;
   }
   bitrate_ = bitrateKbps;
   redundancy_ = redundancy;
   encoder_.setBitrate(bitrate_);
}

void Sender::restart() { needsBegin_ = true; }

void Sender::begin() {
   needsBegin_ = false;
   if (codec_ == Codec::Opus) {
      streamRate_ = kOpusSampleRate;
      frameSamples_ = kOpusFrameSamples;
   }
   else {
      streamRate_ = static_cast<uint32_t>(std::lround(hostRate_));
      frameSamples_ = kPcmFrameSamples;
   }
   const bool resample = std::abs(hostRate_ - streamRate_) > 0.5;
   const double ratio = hostRate_ / streamRate_;
   if (resample && (!resample_ || ratio != ratio_))
      resampler_.configureFor(ratio);
   resample_ = resample;
   ratio_ = ratio;
   resampler_.reset();
   encoder_.reset();
   encoder_.setBitrate(bitrate_);

   uint32_t id;
   do
      id = nextRandom();
   while (id == streamId_ || id == 0);
   streamId_ = id;
   seq_ = nextRandom();
   frameFill_ = 0;
   frameStart_ = 0;
   hasPrevious_ = false;
   pendingCount_ = 0;
}

void Sender::process(const float *left, const float *right, uint32_t frames, const TimedMidi *midi, int midiCount,
                     PacketSink &sink) {
   if (needsBegin_)
      begin();

   // Where each event lands in the stream. Through the resampler, input sample x comes out at
   // output (x - position) / ratio past the outputs made so far.
   const uint64_t made = frameStart_ + static_cast<uint64_t>(frameFill_);
   for (int i = 0; i < midiCount && pendingCount_ < kMaxPending; ++i) {
      double at = midi[i].time;
      if (resample_)
         at = (static_cast<double>(resampler_.pushed()) + midi[i].time - resampler_.position()) / ratio_;
      PendingMidi &p = pending_[pendingCount_++];
      p.pos = made + static_cast<uint64_t>(std::max(0.0, std::floor(at)));
      std::copy(midi[i].data, midi[i].data + 3, p.data);
   }

   if (!resample_) {
      append(left, right, static_cast<int>(frames), sink);
      return;
   }
   float l[256], r[256];
   uint32_t done = 0;
   while (done < frames) {
      const int chunk = static_cast<int>(std::min<uint32_t>(frames - done, 128));
      resampler_.push(left + done, right + done, chunk);
      done += static_cast<uint32_t>(chunk);
      // At most 256 outputs per 128 inputs: rates up to 2:1 below 48 kHz.
      for (int n = resampler_.producible(ratio_); n > 0; n = resampler_.producible(ratio_)) {
         const int take = std::min(n, 256);
         resampler_.produce(l, r, take, ratio_);
         append(l, r, take, sink);
      }
   }
}

void Sender::append(const float *left, const float *right, int n, PacketSink &sink) {
   for (int i = 0; i < n; ++i) {
      frame_[frameFill_ * kChannels] = left[i];
      frame_[frameFill_ * kChannels + 1] = right[i];
      if (++frameFill_ == frameSamples_)
         emitFrame(sink);
   }
}

void Sender::emitFrame(PacketSink &sink) {
   Frame &f = current_;
   f.seq = seq_;
   f.audioBytes = static_cast<uint16_t>(encoder_.encode(codec_, frame_, frameSamples_, f.audio, kMaxFrameBytes));

   // MIDI up to the end of this frame goes with it; anything that doesn't fit waits for the next.
   const uint64_t end = frameStart_ + static_cast<uint64_t>(frameSamples_);
   f.midiCount = 0;
   int kept = 0;
   for (int i = 0; i < pendingCount_; ++i) {
      const PendingMidi &p = pending_[i];
      if (p.pos < end && f.midiCount < kMaxMidiPerFrame) {
         MidiEvent &m = f.midi[f.midiCount++];
         m.offset = static_cast<uint16_t>(p.pos > frameStart_ ? p.pos - frameStart_ : 0);
         std::copy(p.data, p.data + 3, m.data);
      }
      else
         pending_[kept++] = p;
   }
   pendingCount_ = kept;

   AudioHeader header;
   header.streamId = streamId_;
   header.seq = seq_;
   header.sampleRate = streamRate_;
   header.codec = codec_;
   header.frameSamples = static_cast<uint16_t>(frameSamples_);
   const Frame *frames[kMaxFramesPerPacket] = {&current_, &previous_};
   const int count = redundancy_ && hasPrevious_ ? 2 : 1;
   if (f.audioBytes > 0) {
      const size_t size = writeAudio(packet_, sizeof(packet_), header, frames, count);
      if (size)
         sink.send(packet_, size);
   }

   previous_ = current_;
   hasPrevious_ = f.audioBytes > 0;
   ++seq_;
   frameStart_ = end;
   frameFill_ = 0;
}

} // namespace jtunnel
