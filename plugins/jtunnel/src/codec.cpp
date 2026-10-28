#include "codec.h"

#include <opus.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace jtunnel {
namespace {

int32_t toPcm24(float v) {
   const float clamped = std::clamp(v, -1.0f, 1.0f);
   return static_cast<int32_t>(std::lrint(clamped * 8388607.0f));
}

float fromPcm24(const uint8_t *p) {
   int32_t v = static_cast<int32_t>(p[0]) | static_cast<int32_t>(p[1]) << 8 | static_cast<int32_t>(p[2]) << 16;
   if (v & 0x800000)
      v -= 0x1000000;
   return static_cast<float>(v) / 8388607.0f;
}

} // namespace

Encoder::~Encoder() {
   if (opus_)
      opus_encoder_destroy(opus_);
}

bool Encoder::setup() {
   if (opus_)
      return true;
   int error = 0;
   // Restricted low delay is Opus's music codec (CELT) alone, with 2.5 ms of lookahead.
   opus_ = opus_encoder_create(kOpusSampleRate, kChannels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error);
   if (!opus_ || error != OPUS_OK) {
      opus_ = nullptr;
      return false;
   }
   // 7 of 10: most of the quality for well under half the CPU, as this runs on the audio thread.
   opus_encoder_ctl(opus_, OPUS_SET_COMPLEXITY(7));
   opus_encoder_ctl(opus_, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
   bitrate_ = 0;
   return true;
}

void Encoder::reset() {
   if (opus_)
      opus_encoder_ctl(opus_, OPUS_RESET_STATE);
}

void Encoder::setBitrate(int kbps) {
   if (!opus_ || kbps == bitrate_)
      return;
   bitrate_ = kbps;
   opus_encoder_ctl(opus_, OPUS_SET_BITRATE(kbps * 1000));
}

int Encoder::encode(Codec codec, const float *interleaved, int frames, uint8_t *out, int capacity) {
   if (codec == Codec::None)
      return 0;
   if (codec == Codec::Pcm24) {
      const int bytes = frames * kChannels * 3;
      if (bytes > capacity)
         return 0;
      for (int i = 0; i < frames * kChannels; ++i) {
         const int32_t v = toPcm24(interleaved[i]);
         out[3 * i] = static_cast<uint8_t>(v);
         out[3 * i + 1] = static_cast<uint8_t>(v >> 8);
         out[3 * i + 2] = static_cast<uint8_t>(v >> 16);
      }
      return bytes;
   }
   if (!opus_ || frames != kOpusFrameSamples)
      return 0;
   const opus_int32 n = opus_encode_float(opus_, interleaved, frames, out, capacity);
   return n > 0 ? static_cast<int>(n) : 0;
}

Decoder::~Decoder() {
   if (opus_)
      opus_decoder_destroy(opus_);
}

bool Decoder::setup() {
   if (opus_)
      return true;
   int error = 0;
   opus_ = opus_decoder_create(kOpusSampleRate, kChannels, &error);
   if (!opus_ || error != OPUS_OK) {
      opus_ = nullptr;
      return false;
   }
   return true;
}

void Decoder::reset() {
   if (opus_)
      opus_decoder_ctl(opus_, OPUS_RESET_STATE);
   lastL_ = lastR_ = 0.0f;
}

bool Decoder::decode(Codec codec, const uint8_t *data, int bytes, float *interleaved, int frames) {
   if (codec == Codec::None) {
      std::fill(interleaved, interleaved + frames * kChannels, 0.0f);
      return bytes == 0;
   }
   if (codec == Codec::Pcm24) {
      if (bytes != frames * kChannels * 3) {
         std::fill(interleaved, interleaved + frames * kChannels, 0.0f);
         return false;
      }
      for (int i = 0; i < frames * kChannels; ++i)
         interleaved[i] = fromPcm24(data + 3 * i);
      lastL_ = interleaved[(frames - 1) * kChannels];
      lastR_ = interleaved[(frames - 1) * kChannels + 1];
      return true;
   }
   const int n = opus_ ? opus_decode_float(opus_, data, bytes, interleaved, frames, 0) : -1;
   if (n != frames) {
      std::fill(interleaved, interleaved + frames * kChannels, 0.0f);
      return false;
   }
   return true;
}

void Decoder::conceal(Codec codec, float *interleaved, int frames) {
   if (codec == Codec::Opus && opus_ && opus_decode_float(opus_, nullptr, 0, interleaved, frames, 0) == frames)
      return;
   // A short fade from where the audio stopped, rather than a click to zero.
   for (int i = 0; i < frames; ++i) {
      const float g = std::max(0.0f, 1.0f - static_cast<float>(i + 1) / 48.0f);
      interleaved[i * kChannels] = lastL_ * g;
      interleaved[i * kChannels + 1] = lastR_ * g;
   }
   lastL_ = lastR_ = 0.0f;
}

} // namespace jtunnel
