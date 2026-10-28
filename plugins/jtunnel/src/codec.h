// Turns frames of interleaved stereo floats into packet bytes and back: Opus, or plain 24-bit PCM.
// setup() allocates (main thread); everything else is safe on the audio thread.
#pragma once

#include <cstdint>

#include "protocol.h"

struct OpusEncoder;
struct OpusDecoder;

namespace jtunnel {

class Encoder {
public:
   Encoder() = default;
   ~Encoder();
   Encoder(const Encoder &) = delete;
   Encoder &operator=(const Encoder &) = delete;

   bool setup();
   // Starts a fresh stream: no memory of the audio before.
   void reset();
   void setBitrate(int kbps);

   // Codes `frames` samples (kOpusFrameSamples for Opus) into out. Returns the byte count, or 0.
   int encode(Codec codec, const float *interleaved, int frames, uint8_t *out, int capacity);

private:
   OpusEncoder *opus_ = nullptr;
   int bitrate_ = 0;
};

class Decoder {
public:
   Decoder() = default;
   ~Decoder();
   Decoder(const Decoder &) = delete;
   Decoder &operator=(const Decoder &) = delete;

   bool setup();
   void reset();

   // Decodes one frame into `frames` interleaved stereo samples. False (and silence) if the bytes
   // don't decode.
   bool decode(Codec codec, const uint8_t *data, int bytes, float *interleaved, int frames);
   // Fills a frame that never arrived: Opus guesses on from what came before and fades out over a
   // few lost frames; PCM fades its last sample to zero.
   void conceal(Codec codec, float *interleaved, int frames);

private:
   OpusDecoder *opus_ = nullptr;
   float lastL_ = 0.0f, lastR_ = 0.0f;
};

} // namespace jtunnel
