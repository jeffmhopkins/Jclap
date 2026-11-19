#pragma once

// ORB as a JKit engine: its planner reads the prompt and writes a plan (which sounds, when, how loud,
// where), and its physical models play the plan. No neural audio: every sound is synthesised, so it
// needs no ONNX Runtime and the download is only the planner's weights (20.8 MB).
//
// The kit takes ORB's one-shot sounds (27 of its 47: hits, clicks, bells, glass, metal, doors,
// gunshots...), with no texture bed. The planner picks the length it wants; the plan is moved to
// start at 0 and fitted to the pad's LENGTH. The first variant is ORB's own plan, played as ORB plays
// it. The others each sample a plan of their own from the planner (so they may pick other sounds,
// timings and levels for the same prompt), with every sound at its own pitch and decay.
//
// ORB renders at 48 kHz; the sounds come out at 44.1 kHz like every other engine's.

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "gen/orb/planner.h"
#include "gen/sao.h"

namespace jkit::gen {

class OrbEngine {
public:
   bool load(const std::filesystem::path &folder, std::string &error);
   bool loaded() const { return planner_.loaded(); }
   void unload() { planner_.unload(); }

   // The plan for a prompt (fitted to `seconds`), for tests and the generate below.
   // `temperature` above 0 samples the plan (with `sampleSeed`) instead of taking ORB's own.
   bool plan(const std::string &prompt, double seconds, orb::Spec &spec, std::string &error,
             const std::atomic<bool> *cancel = nullptr, double temperature = 0.0, uint64_t sampleSeed = 0) const;
   // Moves a plan to start at 0 and fits it to `seconds`.
   static orb::Spec fit(const orb::Spec &planned, double seconds);
   // Plays a plan with every event's seed drawn from `seed`, at 44.1 kHz. With `vary`, every event
   // also gets its own pitch (within 4 semitones) and decay (0.7 to 1.4 times).
   static Sound render(const orb::Spec &spec, uint32_t seed, bool vary = false);

   bool generate(const Request &request, std::vector<Sound> &out, const std::function<void(double)> &progress,
                 const std::atomic<bool> &cancel, std::string &error);

private:
   orb::Planner planner_;
};

// 48 kHz to 44.1 kHz, band-limited (in the frequency domain, padded so nothing wraps around).
std::vector<float> resample48To441(const std::vector<double> &x);

} // namespace jkit::gen
