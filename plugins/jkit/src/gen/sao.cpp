#include "gen/sao.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace jkit::gen {
namespace {

constexpr int kChannels = 64;     // latent channels
constexpr int kLatents = 256;     // latent frames: the graph's fixed length, 11.9 s
constexpr int kTokens = 64;       // the text encoder's fixed length
constexpr int kEmbed = 768;       // T5 base's width
constexpr int kDownsample = 2048; // audio samples per latent frame

} // namespace

double Noise::uniform() {
   // splitmix64
   uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
   z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
   z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
   z ^= z >> 31;
   return (static_cast<double>(z >> 11) + 0.5) * (1.0 / 9007199254740992.0);
}

float Noise::normal() {
   if (spare_) {
      spare_ = false;
      return spareValue_;
   }
   // Box-Muller.
   const double u = uniform(), v = uniform();
   const double r = std::sqrt(-2.0 * std::log(u)), a = 2.0 * 3.14159265358979323846 * v;
   spareValue_ = static_cast<float>(r * std::sin(a));
   spare_ = true;
   return static_cast<float>(r * std::cos(a));
}

const std::vector<std::string> &StableAudio::files() {
   static const std::vector<std::string> f = {"tokenizer.json", "cond.onnx", "dit.onnx", "dec.onnx"};
   return f;
}

std::vector<float> StableAudio::schedule(int steps) {
   // linspace(1, 0, steps + 1) through the log-SNR shift (anchor -6.2, end 2.0) and a sigmoid, ends kept.
   steps = std::max(1, steps);
   std::vector<float> t(static_cast<size_t>(steps) + 1);
   for (int i = 0; i <= steps; ++i) {
      const double lin = 1.0 - static_cast<double>(i) / steps;
      const double logSnr = 2.0 - lin * (2.0 + 6.2);
      double v = 1.0 / (1.0 + std::exp(logSnr));
      if (lin <= 0.0)
         v = 0.0;
      if (lin >= 1.0)
         v = 1.0;
      t[static_cast<size_t>(i)] = static_cast<float>(v);
   }
   return t;
}

bool StableAudio::load(const Runtime &runtime, const std::filesystem::path &folder, int threads, std::string &error) {
   std::ifstream in(folder / "tokenizer.json", std::ios::binary);
   std::stringstream text;
   text << in.rdbuf();
   if (!in || !tokenizer_.load(text.str())) {
      error = "couldn't read the tokenizer";
      return false;
   }
   return cond_.open(runtime, folder / "cond.onnx", threads, error) &&
          dit_.open(runtime, folder / "dit.onnx", threads, error) &&
          dec_.open(runtime, folder / "dec.onnx", threads, error);
}

bool StableAudio::generate(const Request &request, std::vector<Sound> &out, const std::function<void(double)> &progress,
                           const std::atomic<bool> &cancel, std::string &error) {
   out.clear();
   if (!loaded()) {
      error = "the model isn't loaded";
      return false;
   }
   const double seconds = std::clamp(request.seconds, 0.1, kMaxSeconds);
   const int steps = std::clamp(request.steps, 1, 50);
   const int variants = std::clamp(request.variants, 1, 16);
   const bool guided = request.guidance > 1.001;

   // The prompt and length, once for every variant.
   std::vector<int64_t> ids = tokenizer_.encode(request.prompt, kTokens);
   std::vector<int64_t> mask(kTokens, 0);
   for (size_t k = 0; k < ids.size(); ++k)
      mask[k] = 1;
   ids.resize(kTokens, Tokenizer::kPad);
   std::vector<Tensor> condIn = {Tensor::ints("input_ids", {1, kTokens}, ids),
                                 Tensor::ints("attention_mask", {1, kTokens}, mask),
                                 Tensor::floats("seconds_total", {1}, {static_cast<float>(seconds)})};
   std::vector<Tensor> cond;
   if (!cond_.run(condIn, {"cross", "global"}, cond, error))
      return false;
   std::vector<float> cross = cond[0].f, global = cond[1].f;
   const int64_t crossLen = cond[0].shape.size() > 1 ? cond[0].shape[1] : kTokens + 1;
   // Without the prompt, for guidance: the text part of the cross condition zeroed, the length kept.
   std::vector<float> crossNull = cross;
   std::fill(crossNull.begin(), crossNull.begin() + std::min<size_t>(crossNull.size(), static_cast<size_t>(kTokens) * kEmbed),
             0.0f);

   const std::vector<float> t = schedule(steps);
   const size_t latentSize = static_cast<size_t>(kChannels) * kLatents;
   std::vector<float> first(latentSize);
   Noise(request.seed).fill(first);
   const double angle = std::clamp(request.variety, 0.0, 1.0) * 1.5707963267948966;
   const float keep = static_cast<float>(std::cos(angle)), own = static_cast<float>(std::sin(angle));
   const double perVariant = 1.0 / variants;
   const int evals = steps * (guided ? 2 : 1);

   for (int v = 0; v < variants; ++v) {
      const uint32_t seed = request.seed + static_cast<uint32_t>(v);
      Noise noise(seed);
      std::vector<float> x(latentSize);
      noise.fill(x);
      // Variant 0 is the seed's own; the others lean toward it by (1 - variety).
      if (v > 0)
         for (size_t k = 0; k < latentSize; ++k)
            x[k] = keep * first[k] + own * x[k];
      // Its own noise for the steps, after the starting noise.
      int done = 0;
      for (int s = 0; s < steps; ++s) {
         if (cancel.load()) {
            error = "cancelled";
            return false;
         }
         std::vector<Tensor> in = {Tensor::floats("x", {1, kChannels, kLatents}, x),
                                   Tensor::floats("t", {1}, {t[static_cast<size_t>(s)]}),
                                   Tensor::floats("cross", {1, crossLen, kEmbed}, cross),
                                   Tensor::floats("global", {1, kEmbed}, global)};
         std::vector<Tensor> res;
         if (!dit_.run(in, {"v"}, res, error))
            return false;
         std::vector<float> vel = std::move(res[0].f);
         if (progress)
            progress((v + (++done) / static_cast<double>(evals + 1)) * perVariant);
         if (guided) {
            in[2].f = crossNull;
            if (!dit_.run(in, {"v"}, res, error))
               return false;
            const float g = static_cast<float>(request.guidance);
            for (size_t k = 0; k < latentSize; ++k)
               vel[k] = res[0].f[k] + g * (vel[k] - res[0].f[k]);
            if (progress)
               progress((v + (++done) / static_cast<double>(evals + 1)) * perVariant);
         }
         // Ping-pong: denoise fully, then noise back up to the next step's level.
         const float now = t[static_cast<size_t>(s)], next = t[static_cast<size_t>(s) + 1];
         for (size_t k = 0; k < latentSize; ++k) {
            const float denoised = x[k] - now * vel[k];
            x[k] = (1.0f - next) * denoised + next * noise.normal();
         }
      }
      if (cancel.load()) {
         error = "cancelled";
         return false;
      }
      // Decode only the frames the length needs (the decoder takes any length).
      const int frames = std::min(kLatents, static_cast<int>(std::ceil(seconds * kSampleRate / kDownsample)) + 1);
      std::vector<float> z(static_cast<size_t>(kChannels) * frames);
      for (int c = 0; c < kChannels; ++c)
         std::copy_n(x.begin() + static_cast<ptrdiff_t>(c) * kLatents, frames, z.begin() + static_cast<ptrdiff_t>(c) * frames);
      std::vector<Tensor> decIn = {Tensor::floats("z", {1, kChannels, frames}, z)};
      std::vector<Tensor> audio;
      if (!dec_.run(decIn, {"audio"}, audio, error))
         return false;
      const Tensor &a = audio[0];
      const size_t samples = a.shape.size() == 3 ? static_cast<size_t>(a.shape[2]) : a.f.size() / 2;
      const size_t keepSamples = std::min(samples, static_cast<size_t>(seconds * kSampleRate));
      Sound sound;
      sound.seed = seed;
      sound.left.assign(a.f.begin(), a.f.begin() + static_cast<ptrdiff_t>(keepSamples));
      sound.right.assign(a.f.begin() + static_cast<ptrdiff_t>(samples),
                         a.f.begin() + static_cast<ptrdiff_t>(samples + keepSamples));
      out.push_back(std::move(sound));
      if (progress)
         progress((v + 1) * perVariant);
   }
   return true;
}

} // namespace jkit::gen
