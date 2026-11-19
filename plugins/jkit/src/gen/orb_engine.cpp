#include "gen/orb_engine.h"

#include <algorithm>
#include <cmath>

namespace jkit::gen {

namespace {
// How freely the other variants' plans are drawn: low enough that they stay on the prompt.
constexpr double kVariantTemperature = 0.8;
} // namespace

std::vector<float> resample48To441(const std::vector<double> &x) {
   const size_t n = x.size();
   if (n == 0)
      return {};
   const size_t m = static_cast<size_t>(std::llround(static_cast<double>(n) * 441.0 / 480.0));
   // Pad by a multiple of 160 samples (147 out), so the padding maps exactly and comes off whole.
   const size_t np = (n + 160 * 32) / 160 * 160 + 160, mp = np / 160 * 147;
   const orb::CVec X = orb::rfft(x, np);
   orb::CVec Y(mp / 2 + 1);
   std::copy_n(X.begin(), Y.size(), Y.begin());
   if (mp % 2 == 0)
      Y.back() = std::complex<double>(Y.back().real(), 0.0);
   const orb::Vec y = orb::irfft(Y, mp);
   const double scale = static_cast<double>(mp) / static_cast<double>(np);
   std::vector<float> out(m);
   for (size_t i = 0; i < m; ++i)
      out[i] = static_cast<float>(y[i] * scale);
   return out;
}

bool OrbEngine::load(const std::filesystem::path &folder, std::string &error) {
   return planner_.load(folder / orb::Planner::kFile, error);
}

orb::Spec OrbEngine::fit(const orb::Spec &planned, double seconds) {
   orb::Spec spec;
   double t0 = planned.duration;
   for (const orb::SpecEvent &e : planned.events)
      t0 = std::min(t0, e.t);
   if (planned.events.empty())
      t0 = 0.0;
   spec.duration = std::clamp(std::min(planned.duration - t0, seconds), 0.05, kMaxSeconds);
   for (orb::SpecEvent e : planned.events) {
      e.t -= t0;
      if (e.t < spec.duration - 0.02)
         spec.events.push_back(e);
   }
   return spec;
}

bool OrbEngine::plan(const std::string &prompt, double seconds, orb::Spec &spec, std::string &error,
                     const std::atomic<bool> *cancel, double temperature, uint64_t sampleSeed) const {
   if (!planner_.loaded()) {
      error = "the planner isn't loaded";
      return false;
   }
   orb::PlanOptions options;
   options.noBed = true;
   options.temperature = temperature;
   options.sampleSeed = sampleSeed;
   for (int r = 0; r < static_cast<int>(orb::roleNames().size()); ++r)
      if (orb::roleRendered(r))
         options.roles.push_back(r);
   const std::vector<int> ids = planner_.planIds(prompt, options, cancel);
   if (cancel && cancel->load()) {
      error = "cancelled";
      return false;
   }
   orb::Spec planned;
   if (!orb::Planner::decode(ids, planned) || planned.events.empty()) {
      error = "the planner wrote no plan";
      return false;
   }
   spec = fit(planned, seconds);
   return true;
}

Sound OrbEngine::render(const orb::Spec &spec, uint32_t seed, bool vary) {
   orb::Spec s = spec;
   // Every event its own seed from the sound's, 1..255 (ORB's seeds are a byte; 0 means "by position").
   uint64_t h = (static_cast<uint64_t>(seed) + 1) * 0x9E3779B97F4A7C15ull;
   for (orb::SpecEvent &e : s.events) {
      h ^= h >> 29;
      h *= 0xBF58476D1CE4E5B9ull;
      h ^= h >> 32;
      e.seed = 1 + static_cast<int>(h % 255);
      if (vary) {
         const double u = static_cast<double>((h >> 8) & 0xFFFF) / 65535.0;
         const double w = static_cast<double>((h >> 24) & 0xFFFF) / 65535.0;
         e.pitch = std::exp2((u * 8.0 - 4.0) / 12.0);
         e.decay = 0.7 * std::exp2(w); // 0.7 to 1.4
      }
   }
   orb::Vec l, r;
   orb::renderSpec(s, l, r);
   Sound out;
   out.seed = seed;
   out.left = resample48To441(l);
   out.right = resample48To441(r);
   return out;
}

bool OrbEngine::generate(const Request &request, std::vector<Sound> &out, const std::function<void(double)> &progress,
                         const std::atomic<bool> &cancel, std::string &error) {
   out.clear();
   orb::Spec greedy;
   if (!plan(request.prompt, request.seconds, greedy, error, &cancel))
      return false;
   const int variants = std::max(1, request.variants);
   for (int k = 0; k < variants; ++k) {
      if (cancel.load()) {
         error = "cancelled";
         out.clear();
         return false;
      }
      const uint32_t seed = request.seed + static_cast<uint32_t>(k);
      orb::Spec spec = greedy;
      std::string ignored;
      // A sampled plan that fails (or is cancelled) falls back to ORB's own.
      if (k > 0 && !plan(request.prompt, request.seconds, spec, ignored, &cancel, kVariantTemperature,
                         (static_cast<uint64_t>(seed) << 32) ^ 0x6A6B6974ull))
         spec = greedy;
      out.push_back(render(spec, seed, k > 0));
      progress(static_cast<double>(k + 1) / variants);
   }
   if (cancel.load()) {
      error = "cancelled";
      out.clear();
      return false;
   }
   return true;
}

} // namespace jkit::gen
