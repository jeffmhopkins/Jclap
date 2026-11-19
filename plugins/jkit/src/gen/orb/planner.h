#pragma once

// ORB's planner: a 5.1M-parameter GPT that reads a caption and writes a Spec, the timed list of
// events the renderer plays (orb.model.planner.Planner, decoded the way orb.api.plan does it:
// greedy, with SpecGrammar masking every step so the Spec is always valid).
//
// The kit asks for a little more than ORB does: the length is fixed (the pad's LENGTH), there is no
// texture bed, and only the roles this port renders may be chosen. Those are masks on the same
// grammar, so the planner still picks everything else.
//
// The weights are planner.safetensors from the ORB release (float32, 20.8 MB).

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include "gen/orb/render.h"

namespace jkit::orb {

namespace tok {
constexpr int PAD = 256, BOS = 257, EOS = 258, SEP = 259;
constexpr int SPEC = 260, END_SPEC = 261, DUR = 262, TEX = 263, MOOD = 264, EVENTS = 265, END_EVENTS = 266,
              EVENT = 267, T = 268, D = 269, G = 270, P = 271, S = 272;
constexpr int DIGIT = 273, GAIN = 283, PAN = 299, HEX = 308, TEXVAL = 324, MOODVAL = 332, ROLEVAL = 342;
constexpr int NOBED = 394, VOCAB = 395;
constexpr int GAIN_LEVELS = 16, PAN_LEVELS = 9, MOODS = 10, TEXTURES = 13, ROLES = 47, MAX_EVENTS = 8;
} // namespace tok

struct PlanOptions {
   // The cue's length in centiseconds, or < 0 to let the planner choose.
   int durationCs = -1;
   // No texture bed (the kit's sounds are dry one-shots).
   bool noBed = true;
   // Roles the planner may pick; empty means every role.
   std::vector<int> roles;
   // 0 decodes greedily, as ORB does. Above 0, each token is drawn at this temperature (from the
   // grammar's choices, with `sampleSeed`), so the same caption gives a different plan.
   double temperature = 0.0;
   uint64_t sampleSeed = 0;
};

// The constrained-decoding mask (spec_tokenizer.SpecGrammar), with the kit's extra masks.
class Grammar {
public:
   explicit Grammar(const PlanOptions &options);
   std::vector<int> allowed() const;
   void advance(int token);

private:
   enum class St {
      Start, Dur, DurDigits, Tex, TexVal, Mood, MoodVal, Events, EventOrEnd, Role, T, TDigits, D, DDigits, G,
      GainVal, P, PanVal, S, HexDigits, EndSpec, Done
   };
   std::vector<int> digitsBelow(int limit) const;
   PlanOptions options_;
   St state_ = St::Start;
   bool bedless_ = false;
   int left_ = 0, events_ = 0, value_ = 0, durationCs_ = 0, tCs_ = 0;
};

class Planner {
public:
   // The weights' download: name, size, SHA-256.
   static constexpr const char *kFile = "planner.safetensors";
   static constexpr uint64_t kFileBytes = 20795000;

   bool load(const std::filesystem::path &file, std::string &error);
   bool loaded() const { return !tokEmb_.empty(); }
   void unload();

   // Caption to Spec token ids. Stops early (returning what it has) when `cancel` is set.
   std::vector<int> planIds(const std::string &caption, const PlanOptions &options,
                            const std::atomic<bool> *cancel = nullptr) const;

   // Token ids to a Spec (spec_tokenizer.decode_spec). False if they don't make one.
   static bool decode(const std::vector<int> &ids, Spec &spec);

private:
   struct Block {
      std::vector<float> ln1w, ln1b, qkv, proj, ln2w, ln2b, fc1, fc1b, fc2, fc2b;
   };
   struct Cache {
      std::vector<std::vector<float>> k, v; // per layer: (positions, d)
   };
   void step(int token, int pos, Cache &cache, std::vector<float> *logits) const;

   int d_ = 256, layers_ = 6, heads_ = 4, ctx_ = 1024, vocab_ = 395;
   std::vector<float> tokEmb_, posEmb_, lnfW_, lnfB_, head_;
   std::vector<Block> blocks_;
};

} // namespace jkit::orb
