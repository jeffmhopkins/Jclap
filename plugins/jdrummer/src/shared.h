#pragma once

// The song JDrummer plays, and what the audio thread and the panel pass each other.
//
// A song is seven sections (intro, verse, pre, chorus, bridge, break, outro). Each section has four
// variations of its groove, a fill, the percussionist's part (with its own variations and fill), how
// many bars it runs and how much energy it was made with. A pattern is a grid of hits: up to 32 steps on each of JKit's
// 16 pads. The panel owns the song and sends the whole of it whenever it changes; the audio thread
// publishes a Frame every block for the panel to draw. Neither side waits for the other.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace jdrummer {

constexpr int kPads = 16;
// JKit's pads are notes 36 (pad 1, C2) to 51 (pad 16, D#3).
constexpr int kFirstNote = 36;
constexpr int kMaxSteps = 32;
constexpr int kSections = 7;
constexpr int kVariations = 4;
constexpr int kMaxBars = 64;
// Hit velocities: what the panel and the generator call accented, normal and ghost.
constexpr int kAccent = 118, kNormal = 96, kSoft = 72, kGhost = 40;
constexpr int kMaxRatchet = 4;
constexpr int kMaxNudge = 40; // ms either way

enum SectionId { kIntro, kVerse, kPre, kChorus, kBridge, kBreak, kOutro };
inline const char *const kSectionNames[kSections] = {"Intro", "Verse", "Pre", "Chorus", "Bridge", "Break", "Outro"};
inline const char *const kSectionCaps[kSections] = {"INTRO", "VERSE", "PRE", "CHORUS", "BRIDGE", "BREAK", "OUTRO"};
// Each section's place in a song's arc: how much energy it is made with, before ENERGY moves them all.
inline const double kArcEnergy[kSections] = {0.25, 0.5, 0.65, 0.9, 0.55, 0.3, 0.35};
inline const int kSectionBars[kSections] = {8, 16, 8, 16, 8, 8, 8};

// One hit on one pad on one step. A velocity of 0 is no hit.
struct Hit {
   uint8_t vel = 0;
   uint8_t chance = 100; // percent of the times round it plays
   uint8_t ratchet = 1;  // hits in the step, 1..kMaxRatchet
   int8_t nudge = 0;     // ms early (-) or late (+)
   bool on() const { return vel > 0; }
   bool operator==(const Hit &o) const {
      return vel == o.vel && (!vel || (chance == o.chance && ratchet == o.ratchet && nudge == o.nudge));
   }
   bool operator!=(const Hit &o) const { return !(*this == o); }
};

// Step lengths: 1/16, 1/8, 1/16 triplet, 1/32.
enum Rate : uint8_t { kRate16, kRate8, kRate16T, kRate32 };
inline const char *const kRateNames[] = {"1/16", "1/8", "1/16T", "1/32"};
// Each rate's step in ticks of 1/48 beat.
inline const int kRateTicks[] = {12, 24, 8, 6};
constexpr int kTicksPerBeat = 48;
constexpr int kBeatsPerBar = 4;
constexpr int kTicksPerBar = kTicksPerBeat * kBeatsPerBar;

struct Pattern {
   uint8_t steps = 16;
   uint8_t rate = kRate16;
   std::array<std::array<Hit, kMaxSteps>, kPads> hits{};

   Hit &at(int pad, int step) { return hits[static_cast<size_t>(pad)][static_cast<size_t>(step)]; }
   const Hit &at(int pad, int step) const { return hits[static_cast<size_t>(pad)][static_cast<size_t>(step)]; }

   bool uses(int pad) const {
      for (int s = 0; s < steps; ++s)
         if (at(pad, s).on())
            return true;
      return false;
   }
   int padsUsed() const {
      int n = 0;
      for (int p = 0; p < kPads; ++p)
         n += uses(p);
      return n;
   }
   bool empty() const { return padsUsed() == 0; }
   int count() const {
      int n = 0;
      for (int p = 0; p < kPads; ++p)
         for (int s = 0; s < steps; ++s)
            n += at(p, s).on();
      return n;
   }
   void clear() {
      for (auto &row : hits)
         row.fill(Hit{});
   }
   bool operator==(const Pattern &o) const {
      if (steps != o.steps || rate != o.rate)
         return false;
      for (int p = 0; p < kPads; ++p)
         for (int s = 0; s < kMaxSteps; ++s)
            if (at(p, s) != o.at(p, s))
               return false;
      return true;
   }
   bool operator!=(const Pattern &o) const { return !(*this == o); }
};

// A pattern's length in ticks.
inline int patternTicks(const Pattern &p) { return p.steps * kRateTicks[std::min<int>(p.rate, kRate32)]; }

struct Section {
   std::array<Pattern, kVariations> vars{};
   Pattern fill;
   // The percussionist's part (on its kit's pads), empty without one: its main groove, three
   // variations that play when the drummer's do, and its fill.
   Pattern perc;
   std::array<Pattern, kVariations - 1> percVars{};
   Pattern percFill;
   uint8_t bars = 8;
   // Its bar, in ticks: 192 for 4/4, the groove's length in another meter (168 for 7/8, 240 for 5/4).
   uint16_t meter = kTicksPerBar;
   uint8_t energy = 50; // 0..100, as made
   // A crash on the first beat whenever the section starts.
   uint8_t crash = 0;
   uint8_t locked = 0;
   uint32_t seed = 1;
   // Mutation generations bred into it, for the panel.
   uint16_t generation = 0;

   bool operator==(const Section &o) const {
      return vars == o.vars && fill == o.fill && perc == o.perc && percVars == o.percVars && percFill == o.percFill &&
             bars == o.bars && meter == o.meter && energy == o.energy &&
             crash == o.crash && locked == o.locked && seed == o.seed && generation == o.generation;
   }
   bool operator!=(const Section &o) const { return !(*this == o); }
};

struct Song {
   std::array<Section, kSections> sections{};
   // The kit the percussion parts are written for (the second JKit's): Afro-Latin or Percussion.
   uint8_t percKit = 1; // kPercussionKit
   bool operator==(const Song &o) const { return sections == o.sections && percKit == o.percKit; }
   bool operator!=(const Song &o) const { return !(*this == o); }

   int totalBars() const {
      int n = 0;
      for (const Section &s : sections)
         n += s.bars;
      return n;
   }

   // Saved as bytes: a version, then each section's fields and its patterns (four variations, the
   // fill, the percussion part; from version 2 the percussion's variations and fill, the section's
   // meter, and after the sections the percussion kit), each
   // pattern as its steps, rate and every hit (4 bytes) of its 16 x 32 grid.
   static constexpr uint32_t kVersion = 2;

   std::vector<uint8_t> save() const {
      std::vector<uint8_t> out;
      auto u8 = [&](uint8_t v) { out.push_back(v); };
      auto u32 = [&](uint32_t v) {
         for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<uint8_t>(v >> (8 * i)));
      };
      auto pattern = [&](const Pattern &p) {
         u8(p.steps);
         u8(p.rate);
         for (int pad = 0; pad < kPads; ++pad)
            for (int s = 0; s < kMaxSteps; ++s) {
               const Hit &h = p.at(pad, s);
               u8(h.vel);
               u8(h.chance);
               u8(h.ratchet);
               u8(static_cast<uint8_t>(h.nudge));
            }
      };
      u32(kVersion);
      u32(kSections);
      for (const Section &s : sections) {
         u8(s.bars);
         u8(s.energy);
         u8(s.crash);
         u8(s.locked);
         u32(s.seed);
         u32(s.generation);
         for (const Pattern &p : s.vars)
            pattern(p);
         pattern(s.fill);
         pattern(s.perc);
         for (const Pattern &p : s.percVars)
            pattern(p);
         pattern(s.percFill);
         u8(static_cast<uint8_t>(s.meter & 0xff));
         u8(static_cast<uint8_t>(s.meter >> 8));
      }
      u8(percKit);
      return out;
   }

   bool load(const uint8_t *data, size_t size) {
      size_t at = 0;
      bool ok = true;
      auto u8 = [&]() -> uint8_t {
         if (at >= size) {
            ok = false;
            return 0;
         }
         return data[at++];
      };
      auto u32 = [&]() {
         uint32_t v = 0;
         for (int i = 0; i < 4; ++i)
            v |= static_cast<uint32_t>(u8()) << (8 * i);
         return v;
      };
      auto pattern = [&](Pattern &p) {
         p.steps = static_cast<uint8_t>(std::clamp<int>(u8(), 1, kMaxSteps));
         p.rate = static_cast<uint8_t>(std::min<int>(u8(), kRate32));
         for (int pad = 0; pad < kPads; ++pad)
            for (int s = 0; s < kMaxSteps; ++s) {
               Hit &h = p.at(pad, s);
               h.vel = static_cast<uint8_t>(std::min<int>(u8(), 127));
               h.chance = static_cast<uint8_t>(std::min<int>(u8(), 100));
               h.ratchet = static_cast<uint8_t>(std::clamp<int>(u8(), 1, kMaxRatchet));
               h.nudge = static_cast<int8_t>(std::clamp<int>(static_cast<int8_t>(u8()), -kMaxNudge, kMaxNudge));
            }
      };
      const uint32_t version = u32();
      const uint32_t count = u32();
      if (!ok || version < 1 || version > kVersion || count != kSections)
         return false;
      Song loaded;
      for (Section &s : loaded.sections) {
         s.bars = static_cast<uint8_t>(std::clamp<int>(u8(), 1, kMaxBars));
         s.energy = static_cast<uint8_t>(std::min<int>(u8(), 100));
         s.crash = u8() ? 1 : 0;
         s.locked = u8() ? 1 : 0;
         s.seed = u32();
         s.generation = static_cast<uint16_t>(std::min<uint32_t>(u32(), 65535));
         for (Pattern &p : s.vars)
            pattern(p);
         pattern(s.fill);
         pattern(s.perc);
         if (version >= 2) {
            for (Pattern &p : s.percVars)
               pattern(p);
            pattern(s.percFill);
            const int lo = u8(), hi = u8();
            const int m = lo | hi << 8;
            s.meter = static_cast<uint16_t>(m >= kTicksPerBeat / 2 && m <= kMaxSteps * kTicksPerBeat ? m : kTicksPerBar);
         } else {
            // Saved before the percussion had variations: each is the main part.
            for (Pattern &p : s.percVars)
               p = s.perc;
            s.percFill = s.perc;
         }
      }
      // Version 1 wrote every percussion part on the Percussion kit.
      loaded.percKit = version >= 2 ? static_cast<uint8_t>(u8() == 4 ? 4 : 1) : 1;
      if (!ok)
         return false;
      *this = loaded;
      return true;
   }
};

// What the panel shows of the engine, published every block.
struct Frame {
   bool running = false;   // playing, from the host or on its own
   bool following = false; // following the host's transport (else running free)
   bool hostPlaying = false;
   double tempo = 120.0;
   // Bars and beats since the section started (from 0), and the tick in the bar.
   int bar = 0;
   int beat = 0;
   int section = kChorus;
   int variation = 0;
   bool fill = false;
   // The section and variation queued to come in, or -1.
   int queued = -1;
   int queuedVar = -1;
   // The step playing in the pattern playing, and that pattern's length.
   int step = -1;
   int steps = 16;
   // A count of hits on each pad (drummer, then percussionist), so the panel can light what played.
   std::array<uint32_t, kPads> hits{};
   std::array<uint32_t, kPads> percHits{};
   // Held mutes and HOLD, from notes.
   bool muteHats = false, muteKick = false, hold = false, stopped = false;
};

} // namespace jdrummer
