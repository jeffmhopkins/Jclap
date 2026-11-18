#pragma once

// Stable Audio Open Small, run with ONNX Runtime: text in, up to 11.9 s of 44.1 kHz stereo out.
//
// The model comes as three graphs (an ONNX export of the original weights):
//
//    prompt -> tokenizer.json   T5 Unigram ids
//           -> cond.onnx        T5 text encoder and the length condition
//           -> dit.onnx         the diffusion transformer, run once per step (ping-pong sampling)
//           -> dec.onnx         the autoencoder's decoder: latents to audio
//
// It is the distilled model: 8 ping-pong steps at a guidance of 1 is what it is tuned for. Guidance
// above 1 runs each step twice (with and without the prompt) and pushes away from the prompt-less
// sound; it costs twice the time.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "gen/engines.h"
#include "gen/ort.h"
#include "gen/tokenizer.h"

namespace jkit::gen {

constexpr double kSampleRate = 44100.0;
constexpr double kMaxSeconds = 11.88;

// A seeded source of normal random numbers, the same on every system (std::normal_distribution
// isn't), so a seed makes the same sound everywhere.
class Noise {
public:
   explicit Noise(uint64_t seed) : state_(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
   double uniform();
   float normal();
   void fill(std::vector<float> &v) {
      for (float &x : v)
         x = normal();
   }

private:
   uint64_t state_;
   bool spare_ = false;
   float spareValue_ = 0.0f;
};

struct Request {
   Engine engine = Engine::SaoSmall;
   std::string prompt;
   double seconds = 1.0;
   int steps = 8;
   double guidance = 1.0;
   // The first variant's seed; variant k uses seed + k.
   uint32_t seed = 1;
   int variants = 1;  // (ORB: same plan, each sound its own seed)
   // How far apart the variants are: 0 starts every variant from the first one's noise (they differ
   // only in the noise added along the way), 1 gives each its own (as if generated one at a time).
   double variety = 1.0;
};

struct Sound {
   uint32_t seed = 0;
   std::vector<float> left, right; // at kSampleRate
};

class StableAudio {
public:
   // The files it needs, in the model folder.
   static const std::vector<std::string> &files();

   // Loads the three graphs and the tokenizer from `folder`. Slow (seconds) and large (2 GB).
   bool load(const Runtime &runtime, const std::filesystem::path &folder, int threads, std::string &error);
   bool loaded() const { return dit_.isOpen(); }
   // Frees the graphs (2 GB).
   void unload() {
      cond_.close();
      dit_.close();
      dec_.close();
   }

   // Makes `request.variants` sounds. `progress` gets 0..1 now and then; `cancel` stops it early
   // (returning false with `error` "cancelled").
   bool generate(const Request &request, std::vector<Sound> &out, const std::function<void(double)> &progress,
                 const std::atomic<bool> &cancel, std::string &error);

   // The ping-pong schedule for `steps` steps: steps + 1 times from 1 down to 0.
   static std::vector<float> schedule(int steps);

private:
   Tokenizer tokenizer_;
   Session cond_, dit_, dec_;
};

} // namespace jkit::gen
