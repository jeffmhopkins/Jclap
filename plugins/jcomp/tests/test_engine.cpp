// JComp's harmony, library, generator and engine on their own: chord names, voicings that keep to
// their window and the low-interval limits, every style's progressions and rhythms, songs made the
// same from the same seed, mutations, and the engine playing chords with the host, from held notes
// and from section notes.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine.h"
#include "recipe.h"

#include <cstdlib>
#include <new>

namespace {
extern bool gCount;
extern int gAllocs;
} // namespace

void *operator new(std::size_t n) {
   if (gCount)
      ++gAllocs;
   if (void *p = std::malloc(n ? n : 1))
      return p;
   throw std::bad_alloc();
}
void *operator new(std::size_t n, const std::nothrow_t &) noexcept {
   if (gCount)
      ++gAllocs;
   return std::malloc(n ? n : 1);
}
void *operator new[](std::size_t n) { return operator new(n); }
void *operator new[](std::size_t n, const std::nothrow_t &t) noexcept { return operator new(n, t); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }

using namespace jcomp;

// Each group of tests in a function of its own, kept out of main(): MSVC gives every temporary in a
// function its own stack slot, so a main() holding every group inlined comes near Windows' 1 MB stack
// and the first deep voicing call on top of it goes over.
#if defined(_MSC_VER)
#define JCOMP_TEST_NOINLINE __declspec(noinline)
#else
#define JCOMP_TEST_NOINLINE __attribute__((noinline))
#endif

namespace {

constexpr uint32_t kBlock = 256;

std::vector<double> defaultValues() {
   std::vector<double> v(kParamCount);
   for (clap_id i = 0; i < kParamCount; ++i)
      v[i] = kParams[i].defaultValue;
   return v;
}

struct Timed {
   int64_t time;
   MidiOut m;
   bool on() const { return (m.status & 0xF0) == 0x90 && m.data2 > 0; }
   bool off() const { return (m.status & 0xF0) == 0x80 || ((m.status & 0xF0) == 0x90 && m.data2 == 0); }
};

// Runs the engine for `samples` at 120 BPM, following a playing host, with chord keys held from
// `holdAt` (sample) on.
std::vector<Timed> run(Engine &e, int64_t samples, std::vector<int> hold = {}, int64_t holdAt = 0) {
   std::vector<Timed> all;
   auto out = std::make_unique<OutBuffer>();
   bool held = false;
   for (int64_t at = 0; at < samples; at += kBlock) {
      if (!held && !hold.empty() && at >= holdAt) {
         for (int k : hold)
            e.noteOn(0, k, 100, 0);
         held = true;
      }
      Block b;
      b.frames = kBlock;
      b.tempo = 120.0;
      b.hasTransport = b.playing = true;
      b.beats = at / 24000.0;
      out->clear();
      e.process(b, *out);
      for (size_t i = 0; i < out->count; ++i)
         all.push_back({at + out->items[i].time, out->items[i]});
   }
   return all;
}

// Counts the allocations made while `gCount` is on (the audio thread must make none).
bool gCount = false;
int gAllocs = 0;

// A song of one section (all seven alike): `rhythm` for every take and the fill, and the chords two
// a bar ("" for a bar's second half holds its first).
// (On the heap: the tests' frames would outgrow a 1 MB stack.)
std::unique_ptr<Song> songOf(const char *rhythm, std::vector<std::pair<const char *, const char *>> bars) {
   auto result = std::make_unique<Song>();
   Song &song = *result;
   song.key = 0;
   for (Section &sec : song.sections) {
      for (Pattern &t : sec.takes)
         t = fromRhythm(rhythm);
      sec.fill = fromRhythm(rhythm);
      sec.progBars = static_cast<uint8_t>(bars.size());
      sec.bars = 8;
      sec.split = 0;
      for (size_t b = 0; b < bars.size(); ++b) {
         sec.chords[b * 2] = chord(bars[b].first);
         const bool second = bars[b].second && *bars[b].second;
         sec.chords[b * 2 + 1] = chord(second ? bars[b].second : bars[b].first);
         if (second)
            sec.split = static_cast<uint16_t>(sec.split | (1u << b));
      }
   }
   return result;
}

// Plain settings: on the grid, nothing left to chance.
Settings plain() {
   Settings s;
   s.key = 0;
   s.scale = kMajor;
   s.chordsFrom = 0;
   s.timing = 0;
   s.velocityVar = 0;
   s.lengths = 0;
   s.vary = 0;
   s.loose = 0;
   s.runs = 0;
   s.autoFillBars = 0;
   s.section = 0;
   return s;
}

// Runs the engine from beat `from` at 120 BPM with a playing host, calling `each(at, block)` before
// every block (to play notes in, or change the block: a loop).
template <typename Each>
std::vector<Timed> runWith(Engine &e, int64_t samples, double from, Each each) {
   std::vector<Timed> all;
   auto out = std::make_unique<OutBuffer>();
   double beats = from;
   for (int64_t at = 0; at < samples; at += kBlock) {
      Block b;
      b.frames = kBlock;
      b.tempo = 120.0;
      b.hasTransport = b.playing = true;
      b.beats = beats;
      each(at, b);
      out->clear();
      gCount = true;
      e.process(b, *out);
      gCount = false;
      for (size_t i = 0; i < out->count; ++i)
         all.push_back({at + out->items[i].time, out->items[i]});
      beats = b.beats + kBlock / 24000.0;
   }
   return all;
}

// The pitch classes of the notes starting within 30 ms of `sample`.
std::set<int> startingAt(const std::vector<Timed> &out, int64_t sample) {
   std::set<int> pcs;
   for (const Timed &t : out)
      if (t.on() && std::abs(t.time - sample) < 1440)
         pcs.insert(t.m.data1 % 12);
   return pcs;
}

Settings settingsFor(const Recipe &) {
   std::vector<double> v = defaultValues();
   Settings s;
   s.voicing = voiceSettings([&](clap_id id) { return v[id]; });
   s.chordsFrom = 0;
   s.timing = 0;
   s.section = kChorus;
   return s;
}

Recipe recipeOf(int genre, int style, uint32_t seed) {
   std::vector<double> v = defaultValues();
   for (const auto &[id, x] : styleValues(genre, style))
      v[id] = x;
   v[kSeed] = seed;
   return recipeFrom([&](clap_id id) { return v[id]; });
}

// A song in the version-2 layout (before 0.6.0: no meter, fills down, ending or band hits).
std::vector<uint8_t> saveV2(const Song &s) {
   std::vector<uint8_t> out;
   auto u8 = [&](uint8_t v) { out.push_back(v); };
   auto u32 = [&](uint32_t v) {
      for (int i = 0; i < 4; ++i)
         out.push_back(static_cast<uint8_t>(v >> (8 * i)));
   };
   auto pat = [&](const Pattern &p) {
      u8(p.steps);
      u8(p.rate);
      for (const Hit &h : p.hits)
         for (uint8_t b : {h.vel, h.len, h.chance, static_cast<uint8_t>(h.nudge), h.art, h.part, h.run, h.push,
                           static_cast<uint8_t>(h.voicing), h.degree})
            u8(b);
   };
   u32(2);
   u32(kSections);
   u8(s.key);
   for (const Section &x : s.sections) {
      u8(x.bars);
      u8(x.energy);
      u8(x.locked);
      u8(x.progBars);
      u32(x.split);
      u32(x.seed);
      u32(x.generation);
      for (const Chord &c : x.chords) {
         u8(static_cast<uint8_t>(c.root));
         u8(c.quality);
         u8(static_cast<uint8_t>(c.bass));
      }
      for (const Pattern &p : x.takes)
         pat(p);
      pat(x.fill);
   }
   return out;
}

} // namespace

// The eighth review's fixes, in a function of their own: main's frame is already large, and on
// MSVC its locals don't share stack slots (a Song is 32 KB), so the generator's calls under it ran
// past Windows' 1 MB stack.
JCOMP_TEST_NOINLINE static void roundEight() {
   {
      const auto find = [](const char *gn, const char *sn, int &g, int &st) {
         for (g = 0; g < genreCount(); ++g)
            if (std::string(genreAt(g).name) == gn)
               for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
                  if (std::string(genreAt(g).styles[static_cast<size_t>(st)].name) == sn)
                     return true;
         return false;
      };
      int g = 0, st = 0;
      // A02: a three-note 13th keeps its 13th; D13: a three-note 13sus is its 4, b7 and 9.
      Chord c;
      JCLAP_CHECK(parseChord("A13", c));
      const Ints t = pickTones(c, 3);
      JCLAP_CHECK(std::find(t.begin(), t.end(), 21) != t.end());
      JCLAP_CHECK(parseChord("A13sus", c));
      JCLAP_CHECK(pickTones(c, 3) == (Ints{5, 10, 14}));
      // A04: the Highlife bell is a timeline: its onsets are the bell's at every energy and WEIRD.
      JCLAP_CHECK(find("Highlife", "Bell Twelve", g, st));
      for (double e : {0.1, 0.5, 0.9})
         for (double w : {0.0, 1.0}) {
            Recipe r = recipeOf(g, st, 3);
            r.energy = e;
            r.weird = w;
            auto songPtr = std::make_unique<Song>();
            Song &song = *songPtr; // (on the heap: a Song is too big for MSVC's 1 MB stack, several to a frame)
            generateSong(r, song);
            for (const Section &sec : song.sections)
               for (const Pattern &p : sec.takes)
                  for (int k = 0; k < p.steps; ++k) {
                     const int in = k % 12;
                     const bool bell = in == 0 || in == 2 || in == 4 || in == 5 || in == 7 || in == 9 || in == 11;
                     JCLAP_CHECK(!p.at(k).on() || bell);
                  }
         }
      // A05: Chic's chucking strikes the bar's downbeat.
      JCLAP_CHECK(find("Disco", "Chic Chucking", g, st));
      {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, 1), *song);
         for (const Section &sec : song->sections)
            for (int k = 0; k < sec.takes[0].steps; k += 16)
               JCLAP_CHECK(sec.takes[0].at(k).on() && sec.takes[0].at(k).vel > kGhost + 8);
      }
      // A06: the Dorian Groove's e-piano plays no clav riff.
      JCLAP_CHECK(find("Funk", "Dorian Groove", g, st));
      for (uint32_t seed = 1; seed <= 8; ++seed) {
         auto songPtr = std::make_unique<Song>();
         Song &song = *songPtr;
         generateSong(recipeOf(g, st, seed), song);
         for (const Section &sec : song.sections)
            for (const Pattern &p : sec.takes)
               for (int k = 0; k < p.steps; ++k)
                  JCLAP_CHECK(!p.at(k).on() || p.at(k).part != kPartLine);
      }
      // B06: a section locked under a 3/4 waltz is made again on the 6/8 grid, and back.
      int wg = 0, ws = 0, sg = 0, ss = 0;
      JCLAP_CHECK(find("Swing Jazz", "Jazz Waltz", wg, ws));
      bool six = false;
      for (sg = 0; sg < genreCount() && !six; ++sg)
         for (ss = 0; ss < static_cast<int>(genreAt(sg).styles.size()) && !six; ++ss)
            six = (genreAt(sg).styles[static_cast<size_t>(ss)].mods & kSixEight) != 0;
      JCLAP_CHECK(six);
      if (six) {
         --sg;
         --ss;
         auto songPtr = std::make_unique<Song>();
         Song &song = *songPtr;
         generateSong(recipeOf(wg, ws, 2), song);
         song.sections[kVerse].locked = 1;
         generateSong(recipeOf(sg, ss, 2), song);
         for (const Pattern &p : song.sections[kVerse].takes)
            JCLAP_CHECK_EQ(static_cast<int>(p.rate), static_cast<int>(kRate68));
         generateSong(recipeOf(wg, ws, 2), song);
         for (const Pattern &p : song.sections[kVerse].takes)
            JCLAP_CHECK_EQ(static_cast<int>(p.rate), static_cast<int>(kRate34));
      }
      // D10: ENERGY keeps a progression edited to start with N.C.
      {
         const Recipe r = recipeOf(0, 0, 4);
         auto songPtr = std::make_unique<Song>();
         Song &song = *songPtr;
         generateSong(r, song);
         song.sections[kVerse].chords[0] = Chord{};
         song.sections[kVerse].chords[1] = Chord{};
         const auto before = song.sections[kVerse].chords;
         generateSong(r, song, true);
         JCLAP_CHECK(song.sections[kVerse].chords == before);
      }
      // B10: an ending's last chord is its loudest; C06: a fill down's left hand keeps off a
      // downbeat its take leaves empty.
      for (int gi = 0; gi < genreCount(); ++gi)
         for (int si = 0; si < static_cast<int>(genreAt(gi).styles.size()); ++si) {
            auto songPtr = std::make_unique<Song>();
         Song &song = *songPtr;
            generateSong(recipeOf(gi, si, 5), song);
            const Pattern &end = song.sections[kOutro].ending;
            int last = -1, peak = 0;
            for (int k = 0; k < end.steps; ++k)
               if (end.at(k).on() && !leftHand(end.at(k).part)) {
                  last = k;
                  peak = std::max<int>(peak, end.at(k).vel);
               }
            JCLAP_CHECK(last >= 0 && end.at(last).vel >= peak);
            for (const Section &sec : song.sections) {
               const Pattern &take = sec.takes[0], &d = sec.fillDown;
               if (d.at(0).on() && leftHand(d.at(0).part))
                  JCLAP_CHECK(take.at(0).on() && leftHand(take.at(0).part));
            }
         }
      // D08: a diminished triad's or a half-diminished chord's inversions are named with their own notes.
      JCLAP_CHECK(chordName(nameChord(Ints{51, 54, 60}), 0) != "Ab7");
      JCLAP_CHECK(chordName(nameChord(Ints{54, 58, 60, 63}), 0) != "Ab9");
      // D05: 4THS keeps a 7b9's b9.
      {
         VoiceSettings vs;
         vs.shape = kQuartal;
         JCLAP_CHECK(parseChord("C7b9", c));
         const Voicing v = voice(c, nullptr, vs);
         bool b9 = false;
         for (int i = 0; i < v.count; ++i)
            b9 = b9 || v.notes[static_cast<size_t>(i)] % 12 == 1;
         JCLAP_CHECK(b9);
      }
   }
}

// The ninth review's fixes (in a function of their own, for the Windows stack as above).
JCOMP_TEST_NOINLINE static void roundNine() {
   const auto find = [](const char *gn, const char *sn, int &g, int &st) {
      for (g = 0; g < genreCount(); ++g)
         if (std::string(genreAt(g).name) == gn)
            for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
               if (std::string(genreAt(g).styles[static_cast<size_t>(st)].name) == sn)
                  return true;
      return false;
   };
   const auto name = [](Ints n, bool slash = true) { return chordName(nameChord(n, slash), 0); };
   int g = 0, st = 0;
   // D01: the stock rootless left-hand forms are the unaltered dominant.
   JCLAP_CHECK(name({52, 58, 62}) == "C9");
   JCLAP_CHECK(name({64, 69, 70}) == "C13");
   JCLAP_CHECK(name({41, 47, 52}) == "G13");
   // D02: a shape is named the same in every key.
   for (const Ints &shape : {Ints{52, 55, 57, 60}, Ints{55, 58, 60, 63}, Ints{48, 53, 55, 58, 62, 69}, Ints{52, 55, 58, 60}})
      for (bool slash : {true, false}) {
         std::string first;
         for (int t = 0; t < 12; ++t) {
            Ints n = shape;
            for (int &x : n)
               x += t;
            Chord c = nameChord(n, slash);
            c.root = static_cast<int8_t>((c.root - t + 12) % 12);
            if (c.bass >= 0)
               c.bass = static_cast<int8_t>((c.bass - t + 12) % 12);
            const std::string got = chordName(c, 0);
            if (t == 0)
               first = got;
            JCLAP_CHECK(got == first);
         }
      }
   // D03: a whole chord over its own third keeps its quality.
   JCLAP_CHECK(name({52, 55, 60, 62}) == "Cadd9/E");
   JCLAP_CHECK(name({52, 55, 58, 63, 72}) == "C7#9/E");
   JCLAP_CHECK(name({51, 55, 58, 62, 65, 72}) == "Cm11/Eb");
   JCLAP_CHECK(name({55, 60, 65}) == "Csus/G");
   // D04: the keys' fifth below never goes under FLOOR.
   for (int floor = 36; floor <= 52; ++floor)
      for (int root = 0; root < 12; ++root)
         for (int under = floor + 3; under < floor + 24; ++under)
            JCLAP_CHECK(bassNote(Chord{static_cast<int8_t>(root), kMaj, -1}, floor, true, false, under) >= floor);
   // D08 and D13: 4THS keeps a half-diminished's b5; ROOTLESS at three keeps a #11.
   {
      VoiceSettings vs;
      vs.shape = kQuartal;
      for (int n : {3, 4, 5}) {
         vs.voices = n;
         Chord c;
         JCLAP_CHECK(parseChord("Cm7b5", c));
         const Voicing v = voice(c, nullptr, vs);
         bool b5 = false;
         for (int i = 0; i < v.count; ++i)
            b5 = b5 || v.notes[static_cast<size_t>(i)] % 12 == 6;
         JCLAP_CHECK(b5);
      }
      vs.shape = kRootless;
      vs.voices = 3;
      Chord c;
      JCLAP_CHECK(parseChord("C7#11", c));
      const Voicing v = voice(c, nullptr, vs);
      bool s11 = false;
      for (int i = 0; i < v.count; ++i)
         s11 = s11 || v.notes[static_cast<size_t>(i)] % 12 == 6;
      JCLAP_CHECK(s11);
   }
   // D09: OCTAVES never puts a major seventh right under a doubled root.
   {
      VoiceSettings vs;
      vs.shape = kOctaves;
      for (int n : {3, 4, 5})
         for (int root = 0; root < 12; ++root) {
            vs.voices = n;
            const Voicing v = voice(Chord{static_cast<int8_t>(root), kMaj7, -1}, nullptr, vs);
            for (int i = 1; i < v.count; ++i)
               JCLAP_CHECK(!(v.notes[static_cast<size_t>(i)] % 12 == root && v.notes[static_cast<size_t>(i)] - v.notes[static_cast<size_t>(i - 1)] == 1));
         }
   }
   // B06: the bolero broadens into its last chord, no cha-cha tag.
   JCLAP_CHECK(find("Latin Jazz", "Bolero", g, st));
   {
      auto song = std::make_unique<Song>();
      generateSong(recipeOf(g, st, 1), *song);
      const Pattern &end = song->sections[kOutro].ending;
      JCLAP_CHECK(!end.empty());
      for (int s = 0; s < end.stepsPerBar(); ++s)
         JCLAP_CHECK(!end.at(s).on() || end.at(s).len >= 4);
   }
   for (uint32_t seed = 1; seed <= 12; ++seed) {
      // A01: Dorian Groove's chorus is on the One.
      JCLAP_CHECK(find("Funk", "Dorian Groove", g, st));
      {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         const Pattern &p = song->sections[kChorus].takes[0];
         for (int b = 0; b < p.bars(); ++b)
            JCLAP_CHECK(p.at(b * p.stepsPerBar()).on() && !leftHand(p.at(b * p.stepsPerBar()).part));
      }
      // A03: the son montuno changes chord twice a bar in its verse.
      JCLAP_CHECK(find("Salsa", "Montuno 2-3", g, st));
      {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         JCLAP_CHECK((song->sections[kVerse].split & 1) != 0);
      }
      // A07: the bubble's left hand stays off the beats in every take.
      JCLAP_CHECK(find("Reggae", "Organ Bubble", g, st));
      {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         for (const Section &sec : song->sections)
            for (const Pattern &p : sec.takes)
               for (int s = 0; s < p.steps; s += p.stepsPerBar() / 2)
                  JCLAP_CHECK(!(p.at(s).on() && leftHand(p.at(s).part)));
      }
      // A05, A12: a figure that is the style, and a held take, aren't chopped by a mutation.
      for (const auto &[gn, sn] : {std::pair{"Swing Jazz", "Held Comp"}, std::pair{"Bossa Nova", "Batida"},
                                   std::pair{"Bossa Nova", "Two-Bar Bossa"}}) {
         JCLAP_CHECK(find(gn, sn, g, st));
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         for (const Section &sec : song->sections)
            for (int t = 1; t < kTakes; ++t) {
               const Pattern &p = sec.takes[static_cast<size_t>(t)], &p0 = sec.takes[0];
               for (int s = 0; s < p.steps; ++s)
                  JCLAP_CHECK(!(p.at(s).on() && p.at(s).art == kArtChop && !(s < p0.steps && p0.at(s).art == kArtChop)));
            }
      }
      // C05: the cha-cha's and the bell's fill gains no push its style lacks.
      for (const auto &[gn, sn] : {std::pair{"Latin Jazz", "Cha-Cha"}, std::pair{"Highlife", "Bell Twelve"}}) {
         JCLAP_CHECK(find(gn, sn, g, st));
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         for (const Section &sec : song->sections)
            for (int s = 0; s < sec.fill.steps; ++s)
               JCLAP_CHECK(!sec.fill.at(s).push);
      }
      // C08: a riff's fill doesn't push its line's last note.
      JCLAP_CHECK(find("Afrobeat", "Tenor Line", g, st));
      {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         for (const Section &sec : song->sections)
            for (int s = 0; s < sec.fill.steps; ++s)
               JCLAP_CHECK(!(sec.fill.at(s).push && sec.fill.at(s).part == kPartLine));
      }
      // C10: a choked style's takes never ring a chord.
      JCLAP_CHECK(find("Reggae", "One Drop Skank", g, st));
      {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         for (const Section &sec : song->sections)
            for (const Pattern &p : sec.takes)
               for (int s = 0; s < p.steps; ++s)
                  JCLAP_CHECK(!(p.at(s).on() && !leftHand(p.at(s).part) && p.at(s).len <= 1 &&
                                (p.at(s).art == kArtRing || p.at(s).art == kArtTopRings)));
      }
   }
   // A09: the clav riff stays in its window in every key.
   JCLAP_CHECK(find("Funk", "Clav Riff", g, st));
   for (int key = 0; key < 12; ++key) {
      Recipe r = recipeOf(g, st, 1);
      r.key = key;
      auto song = std::make_unique<Song>();
      generateSong(r, *song);
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = settingsFor(r);
      applyRecipe(s, r);
      s.section = kVerse;
      s.key = song->key;
      e.setSettings(s);
      e.reset();
      const auto out = runWith(e, 24000LL * 32, 0.0, [](int64_t, Block &) {});
      for (const Timed &t : out)
         if (t.on())
            JCLAP_CHECK(t.m.data1 <= s.voicing.hi + 2);
   }
   // B05: a locked section is remade between 4/4 sixteenths and 12/8.
   {
      int g2 = 0, st2 = 0;
      JCLAP_CHECK(find("Piano Ballad", "Ballad Block", g, st));
      JCLAP_CHECK(find("Piano Ballad", "12/8 Ballad", g2, st2));
      auto song = std::make_unique<Song>();
      generateSong(recipeOf(g, st, 1), *song);
      song->sections[kVerse].locked = 1;
      const auto chords = song->sections[kVerse].chords;
      generateSong(recipeOf(g2, st2, 1), *song, true);
      JCLAP_CHECK(song->sections[kVerse].takes[0].rate == kRate8T);
      JCLAP_CHECK(song->sections[kVerse].chords == chords);
      generateSong(recipeOf(g, st, 1), *song, true);
      JCLAP_CHECK(song->sections[kVerse].takes[0].rate == kRate16);
   }
   // E04: each section throws its own dice.
   {
      Section a, b;
      a.seed = 1;
      b.seed = 2;
      JCLAP_CHECK(sectionSalt(a) != sectionSalt(b));
   }
}

// The song's structure: the order SONG mode plays the sections in.
// The tenth review's grooves, fills, takes and chord names.
JCOMP_TEST_NOINLINE static void roundTen() {
   const auto style = [](const char *sn, int &g, int &st) {
      for (g = 0; g < genreCount(); ++g)
         for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            if (std::string(genreAt(g).styles[static_cast<size_t>(st)].name) == sn)
               return true;
      return false;
   };
   const auto name = [](Ints n, int key = -1) { return chordName(nameChord(n, true, key), 0); };
   int g = 0, st = 0;
   // D03, D06, D10: chord names.
   JCLAP_CHECK(name({52, 58, 60}) == "C7/E");
   JCLAP_CHECK(name({48, 52, 57, 62}) == "C6/9");
   JCLAP_CHECK(name({48, 51, 55, 58, 62, 65}) == "Cm11");
   JCLAP_CHECK(name({52, 58, 62}) == "C9");
   JCLAP_CHECK(name({58, 64}, 0) == "C7");
   // D04: a guitar's open C, x32010.
   {
      VoiceSettings vs;
      vs.shape = kGrip;
      vs.voices = 5;
      const Voicing v = voice(Chord{0, kMaj, -1}, nullptr, vs);
      const int want[5] = {48, 52, 55, 60, 64};
      JCLAP_CHECK_EQ(v.count, 5);
      for (int i = 0; i < 5 && i < v.count; ++i)
         JCLAP_CHECK_EQ(static_cast<int>(v.notes[static_cast<size_t>(i)]), want[i]);
   }
   // A01: Bell Twelve's chords are close triads (their thirds kept).
   JCLAP_CHECK(style("Bell Twelve", g, st));
   JCLAP_CHECK(genreAt(g).styles[static_cast<size_t>(st)].shape == kClose && genreAt(g).styles[static_cast<size_t>(st)].voices == 3);
   // Every style's fills: their anticipation sure (C14), no louder than the take's peak and a bit
   // (C05), a line as written (C04).
   for (g = 0; g < genreCount(); ++g)
      for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
         for (uint32_t seed : {1u, 2u}) {
            Recipe r = recipeOf(g, st, seed);
            r.weird = 0.8;
            auto song = std::make_unique<Song>();
            generateSong(r, *song);
            for (const Section &sec : song->sections) {
               const Pattern &f = sec.fill;
               if (f.empty())
                  continue;
               if (!(genreAt(g).styles[static_cast<size_t>(st)].mods & kFixed))
                  JCLAP_CHECK(chordPeak(f) <= std::max(chordPeak(sec.takes[0]) + 12, kGhost + 12));
               for (int s = 0; s < f.steps; ++s) {
                  if (f.at(s).on() && f.at(s).push && chordPart(f.at(s).part))
                     JCLAP_CHECK_EQ(static_cast<int>(f.at(s).chance), 100);
                  if (f.at(s).on() && f.at(s).part == kPartLine)
                     JCLAP_CHECK(!f.at(s).push);
               }
            }
         }
   // C01: a boom-chick's fill isn't its groove: something leads into the next section.
   for (const char *sn : {"Boom-Chick", "Two-Step", "Boogie Figure", "Broken Chords"}) {
      JCLAP_CHECK(style(sn, g, st));
      for (uint32_t seed = 1; seed <= 6; ++seed) {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         for (const Section &sec : song->sections) {
            const Pattern was = lastBar(sec.takes[0]), &f = sec.fill;
            if (f.empty())
               continue;
            bool same = true;
            for (int s = 0; s < f.stepsPerBar() && same; ++s)
               same = f.at(s).on() == was.at(s).on() && f.at(s).push == was.at(s).push &&
                      (!f.at(s).on() || f.at(s).vel == was.at(s).vel);
            JCLAP_CHECK(!same);
         }
      }
   }
   // C10: the 6/8 fill's lead-in is its last eighth.
   JCLAP_CHECK(style("6/8 Ballad", g, st));
   for (uint32_t seed = 1; seed <= 20; ++seed) {
      auto song = std::make_unique<Song>();
      generateSong(recipeOf(g, st, seed), *song);
      for (const Section &sec : song->sections)
         for (int s = 0; s < sec.fill.steps; ++s)
            if (sec.fill.at(s).on() && sec.fill.at(s).push)
               JCLAP_CHECK(s >= 10);
   }
   // Takes: C02 a backbone bass stays on its strong beats; C03 nothing struck inside a held chord;
   // C06 an organ's chords held.
   for (g = 0; g < genreCount(); ++g)
      for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
         for (uint32_t seed : {1u, 2u, 3u}) {
            const Recipe r = recipeOf(g, st, seed);
            auto song = std::make_unique<Song>();
            generateSong(r, *song);
            const unsigned mods = genreAt(g).styles[static_cast<size_t>(st)].mods;
            const bool organ = playerOf(r).instrument == kOrgan && !(mods & kShorts);
            for (const Section &sec : song->sections) {
               const Pattern &t0 = sec.takes[0];
               for (int k = 1; k < kTakes; ++k) {
                  const Pattern &t = sec.takes[static_cast<size_t>(k)];
                  if (t.steps != t0.steps)
                     continue;
                  for (int s = 0; s < t.steps; ++s) {
                     if (t0.at(s).on() && leftHand(t0.at(s).part) && s % strongSteps(t0) == 0)
                        JCLAP_CHECK(t.at(s).on() && leftHand(t.at(s).part));
                     // (A chord pushed into the next bar, rung, is the held comp's own anticipation; and a
                     // held chord the take moved or left out holds nothing.)
                     // (A quarter pulse's beats are struck, not held: a chop between them is the take's own.)
                     if (heldTake(t0) && !pulseTake(t0) && !t0.at(s).on() && heldOver(t0, s) && t.at(s).on() && !t.at(s).push) {
                        int holder = s - 1;
                        while (holder > 0 && !t0.at(holder).on())
                           --holder;
                        JCLAP_CHECK(leftHand(t.at(s).part) || !(t.at(holder) == t0.at(holder)));
                     }
                     if (organ && t.at(s).on() && chordPart(t.at(s).part) && t.at(s).len <= 1 && !(t0.at(s) == t.at(s)))
                        JCLAP_CHECK(t.at(s).art != kArtAsWritten);
                  }
               }
            }
         }
   // A06: the shout's organ bounces its upbeats, half held, and rings its downbeats.
   JCLAP_CHECK(style("Shout", g, st));
   {
      auto song = std::make_unique<Song>();
      generateSong(recipeOf(g, st, 1), *song);
      const Pattern &t = song->sections[kVerse].takes[0];
      int half = 0;
      for (int s = 0; s < t.steps; ++s)
         if (t.at(s).on() && chordPart(t.at(s).part) && t.at(s).len <= 1) {
            JCLAP_CHECK(t.at(s).art != kArtRing || s % t.perBeat() == 0);
            half += t.at(s).art == kArtHalf;
         }
      JCLAP_CHECK(half > 0);
   }
   // A07: Chic's second bar breathes: two of its three rests stay in most takes.
   JCLAP_CHECK(style("Chic Chucking", g, st));
   {
      int kept = 0, all = 0;
      for (uint32_t seed = 1; seed <= 30; ++seed) {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         for (const Pattern &t : song->sections[kChorus].takes) {
            if (t.steps < 32)
               continue;
            ++all;
            kept += (!t.at(21).on()) + (!t.at(25).on()) + (!t.at(29).on()) >= 2;
         }
      }
      JCLAP_CHECK(all > 0 && kept * 10 >= all * 8);
   }
   // C07: an ambient drift has no fill down to re-strike its held chord.
   JCLAP_CHECK(style("Mirror Drift", g, st));
   {
      auto song = std::make_unique<Song>();
      generateSong(recipeOf(g, st, 1), *song);
      for (const Section &sec : song->sections)
         JCLAP_CHECK(sec.fillDown.empty());
   }
   // A05: WEIRD reharmonises no Caribbean or African progression.
   for (g = 0; g < genreCount(); ++g) {
      if (genreAt(g).family != kFamilies[5])
         continue;
      Recipe r = recipeOf(g, 0, 4), w = r;
      r.weird = 0.0;
      w.weird = 1.0;
      auto a = std::make_unique<Song>(), b = std::make_unique<Song>();
      generateSong(r, *a);
      generateSong(w, *b);
      for (int i = 0; i < kSections; ++i)
         for (int bar = 0; bar < a->sections[static_cast<size_t>(i)].progBars; ++bar)
            JCLAP_CHECK(a->sections[static_cast<size_t>(i)].chordAt(bar, false).root ==
                        b->sections[static_cast<size_t>(i)].chordAt(bar, false).root);
   }
}

JCOMP_TEST_NOINLINE static void roundEleven() {
   const auto name = [](Ints n, int key = -1, int scale = kMajor) { return chordName(nameChord(n, true, key, scale), 0); };
   // D06, D07, D14: chord names by the key's scale, a bass a fifth under the rest, a power chord.
   JCLAP_CHECK(name({59, 65}, 9, kMinor) == "G7");
   JCLAP_CHECK(name({48, 64, 71, 78}) == "Cmaj7#11");
   JCLAP_CHECK(name({60, 64, 66, 71}) == "D13");
   JCLAP_CHECK(name({45, 52}, 9, kMinor) == "Am");
   // D12: B major's raised fourth is E#.
   JCLAP_CHECK(chordName(Chord{1, kMaj, 5}, 11) == "C#/E#");
   // D02: a guitar's open chords: G 320003 (its top string left for the five a grip holds), D xx0232.
   {
      VoiceSettings vs;
      vs.shape = kGrip;
      vs.voices = 5;
      vs.lo = 40;
      vs.hi = 76;
      vs.center = 58;
      vs.floor = 40;
      const auto is = [&](const char *n, Ints want) {
         const Voicing v = voice(chord(n), nullptr, vs);
         Ints got;
         for (int i = 0; i < v.count; ++i)
            got.push_back(v.notes[static_cast<size_t>(i)]);
         JCLAP_CHECK(got == want);
      };
      // (A six-string open shape rings all six at five voices: a guitarist strums the open G whole.)
      is("G", {43, 47, 50, 55, 59, 67});
      is("D", {50, 57, 62, 66});
      is("E", {40, 47, 52, 56, 59, 64});
   }
   // D08: a minor key's V7 runs the harmonic minor's b9 and b13: E7 into Am never runs a C#.
   for (int from : {59, 62, 67})
      for (int to : {64, 69})
         for (int n : passingRun(from, to, 3, 9, kMinor, false, chord("E7"), chord("Am")))
            JCLAP_CHECK(n % 12 != 1 && n % 12 != 6);
   // D09: 4THS at five voices keeps a dominant's seventh, in every key.
   // D11: SHELL at four voices doesn't double the root on top.
   // D16: ROOTLESS at three voices keeps an m11's 11th and a 13sus's 13th.
   for (int root = 0; root < 12; ++root) {
      VoiceSettings vs;
      const auto pcs = [&](const Voicing &v) {
         std::set<int> out;
         for (int i = 0; i < v.count; ++i)
            out.insert((v.notes[static_cast<size_t>(i)] - root + 12) % 12);
         return out;
      };
      vs.shape = kQuartal;
      vs.voices = 5;
      vs.lo = 50;
      vs.hi = 76;
      JCLAP_CHECK(pcs(voice(Chord{static_cast<int8_t>(root), kDom7, -1}, nullptr, vs)).count(10) == 1);
      vs = VoiceSettings{};
      vs.shape = kShell;
      vs.voices = 4;
      const Voicing shell = voice(Chord{static_cast<int8_t>(root), kMin7, -1}, nullptr, vs);
      for (int i = 1; i < shell.count; ++i)
         JCLAP_CHECK(shell.notes[static_cast<size_t>(i)] % 12 != root);
      vs.shape = kRootless;
      vs.voices = 3;
      JCLAP_CHECK(pcs(voice(Chord{static_cast<int8_t>(root), kMin11, -1}, nullptr, vs)).count(5) == 1);
      JCLAP_CHECK(pcs(voice(Chord{static_cast<int8_t>(root), kSus13, -1}, nullptr, vs)).count(9) == 1);
      // D10: SPREAD's 7#9 has its #9 over the 3rd.
      vs.shape = kSpread;
      vs.voices = 4;
      const Chord hendrix{static_cast<int8_t>(root), kSharp9, -1};
      JCLAP_CHECK(clashes(voice(hendrix, nullptr, vs), hendrix) <= 0.0);
   }
   // B02, B05 (E14): a section that closes a phrase ends at home. AABA's second A before the bridge,
   // and the last entry of I V C, end on the outro's last chord, the cadence a bar earlier.
   {
      auto song = songOf("x...............", {{"C", nullptr}, {"Am", nullptr}, {"Dm", nullptr}, {"G7", nullptr}});
      for (Section &sec : song->sections)
         sec.bars = 4;
      for (Chord &c : song->sections[kOutro].chords)
         c = chord("C");
      song->setForm(4); // AABA: I V V B V O
      JCLAP_CHECK(chordName(phraseChord(*song, 2, true, kVerse, 3, false), 0) == "C");
      JCLAP_CHECK(chordName(phraseChord(*song, 2, true, kVerse, 2, false), 0) == "G7");
      JCLAP_CHECK(chordName(phraseChord(*song, 1, true, kVerse, 3, false), 0) == "G7"); // the first A goes on
      JCLAP_CHECK(chordName(phraseChord(*song, -1, true, kVerse, 3, false), 0) == "G7");
      song->setForm(kCustomForm, {kIntro, kVerse, kChorus});
      JCLAP_CHECK(chordName(phraseChord(*song, 2, true, kChorus, 3, false), 0) == "C");
      JCLAP_CHECK(chordName(phraseChord(*song, 2, false, kChorus, 3, false), 0) == "G7"); // on repeat
   }
   // A03: the Afrobeat figures (Chord Chop, Interlock) are played as written.
   for (int g = 0; g < genreCount(); ++g)
      for (const Style &st : genreAt(g).styles)
         if (std::string(st.name) == "Chord Chop" || std::string(st.name) == "Interlock")
            JCLAP_CHECK((st.mods & kFixed) != 0);
   // E01: a chord played a moment late, with a strumming player, never lets the old chord's last
   // strings sound over the new one.
   for (int onsets : {1, 2}) {
      auto song = songOf("x...............", {{"C", nullptr}});
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.chordsFrom = 1;
      s.onsets = onsets;
      s.changeAt = 3;
      s.voicing.voices = 5;
      e.setSettings(s);
      e.reset();
      const int64_t late = 96000 + 20 * 48;
      bool c = false, f = false;
      const auto out = runWith(e, 96000 * 3, 0.0, [&](int64_t at, Block &b) {
         if (!c) {
            for (int k : {60, 64, 67})
               e.noteOn(0, k, 100, 0);
            c = true;
         }
         if (!f && at + b.frames > late) {
            const uint32_t fr = static_cast<uint32_t>(late - at);
            for (int k : {60, 64, 67})
               e.noteOff(0, k, fr);
            for (int k : {65, 69, 72})
               e.noteOn(0, k, 100, fr);
            f = true;
         }
      });
      for (const Timed &t : out)
         if (t.on() && t.time >= late && t.time < 96000 * 2)
            JCLAP_CHECK(t.m.data1 % 12 == 5 || t.m.data1 % 12 == 9 || t.m.data1 % 12 == 0);
   }
   // E17: in LOOP, a host's count-in rests; the bass coming in during it brings no chord in SONG.
   {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}});
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      e.setSettings(s);
      e.reset();
      const auto out = runWith(e, 96000 * 3, -8.0, [](int64_t, Block &) {});
      int early = 0, after = 0;
      for (const Timed &t : out)
         (t.on() ? (t.time < 96000 * 2 - 480 ? early : after) : after) += 1;
      JCLAP_CHECK_EQ(early, 0);
      JCLAP_CHECK(after > 0);
   }
}

JCOMP_TEST_NOINLINE static void songStructure() {
   // Templates by name, a hand-written order that is one named as it, an empty one once through.
   {
      auto song = std::make_unique<Song>();
      JCLAP_CHECK_EQ(song->entries(), kSections);
      song->setForm(3); // Short
      JCLAP_CHECK_EQ(static_cast<int>(song->length), 6);
      const int shortForm[6] = {kIntro, kVerse, kChorus, kVerse, kChorus, kOutro};
      for (int i = 0; i < 6; ++i)
         JCLAP_CHECK_EQ(song->entry(i), shortForm[i]);
      JCLAP_CHECK_EQ(song->find(kChorus, 3), 4);
      JCLAP_CHECK_EQ(song->find(kVerse, 4), 1); // wrapping
      JCLAP_CHECK_EQ(song->find(kPre), -1);
      song->setStructure({kIntro, kVerse, kVerse, kBridge, kVerse, kOutro});
      JCLAP_CHECK_EQ(static_cast<int>(song->form), 4); // AABA
      song->setStructure({kChorus, kChorus});
      JCLAP_CHECK(song->form == kCustomForm);
      JCLAP_CHECK_EQ(song->entries(), 2);
      song->setStructure({});
      JCLAP_CHECK_EQ(static_cast<int>(song->form), 0);
      JCLAP_CHECK_EQ(song->entries(), kSections);
      // Once through is the plain order, stored as none.
      song->setStructure({kIntro, kVerse, kPre, kChorus, kBridge, kBreak, kOutro});
      JCLAP_CHECK(song->form == 0 && song->length == 0);
   }
   // Saved with the song; a song from before (version 4) plays once through.
   {
      auto song = std::make_unique<Song>();
      generateSong(Recipe{}, *song);
      song->setForm(kCustomForm, {kChorus, kVerse, kChorus, kChorus, kOutro});
      const std::vector<uint8_t> saved = song->save();
      auto back = std::make_unique<Song>();
      JCLAP_CHECK(back->load(saved.data(), saved.size()));
      JCLAP_CHECK(*back == *song);
      JCLAP_CHECK(back->form == kCustomForm && back->length == 5 && back->entry(3) == kChorus);
      std::vector<uint8_t> old = saved;
      old.resize(old.size() - 2 - 5); // no structure
      old[0] = 4;
      auto four = std::make_unique<Song>();
      JCLAP_CHECK(four->load(old.data(), old.size()));
      JCLAP_CHECK(four->length == 0 && four->form == 0);
   }
   // What a run plays at each bar: the section and the entry, read half way through the bar.
   struct Bar {
      int section, formPos;
      bool fill, fillDown, ending, ended;
   };
   const auto bars = [](Engine &e, int count, double from, bool host) {
      std::vector<Bar> got;
      auto out = std::make_unique<OutBuffer>();
      double beats = from;
      for (int64_t at = 0; at < count * 96000; at += kBlock) {
         Block b;
         b.frames = kBlock;
         b.tempo = 120.0;
         b.hasTransport = b.playing = host;
         b.beats = beats;
         out->clear();
         e.process(b, *out);
         beats += kBlock / 24000.0;
         if (at % 96000 == 256 * 187) {
            Frame f;
            e.fillFrame(f);
            got.push_back({f.section, f.formPos, f.fill, f.fillDown, f.ending, f.ended});
         }
      }
      return got;
   };
   const auto twoBars = [] {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}});
      for (Section &sec : song->sections) {
         sec.bars = 2;
         sec.fill = fromRhythm("x.x.x.x.x.x.x.x.");
         sec.fillDown = fromRhythm("X-------........");
         sec.ending = fromRhythm("X---------------");
         sec.fill.steps = sec.fillDown.steps = sec.ending.steps = 16; // a bar each
      }
      return song;
   };
   // SONG walks the structure, the fill going into each entry's next and the ending only on the last
   // (a chorus that comes back plays its fill, not its ending, until it's the last entry).
   for (bool host : {false, true}) {
      auto song = twoBars();
      song->setForm(kCustomForm, {kIntro, kChorus, kVerse, kChorus});
      song->sections[kChorus].energy = 90;
      song->sections[kVerse].energy = 40;
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      s.sync = host ? 1 : 2;
      s.play = !host;
      s.freeTempo = 120.0;
      e.setSettings(s);
      e.reset();
      const std::vector<Bar> got = bars(e, 10, 0.0, host);
      const int want[8] = {kIntro, kIntro, kChorus, kChorus, kVerse, kVerse, kChorus, kChorus};
      for (int i = 0; i < 8; ++i) {
         JCLAP_CHECK_EQ(got[static_cast<size_t>(i)].section, want[i]);
         JCLAP_CHECK_EQ(got[static_cast<size_t>(i)].formPos, i / 2);
      }
      // The first chorus's last bar: the fill down into the quieter verse; the last chorus's: its ending.
      JCLAP_CHECK(got[3].fill && got[3].fillDown && !got[3].ending);
      JCLAP_CHECK(!got[2].ending && !got[6].ending);
      JCLAP_CHECK(got[7].ending);
      JCLAP_CHECK(got[9].ended);
   }
   // REPEAT goes round the structure: after its last entry, its first.
   {
      auto song = twoBars();
      song->setForm(3); // Short: I V C V C O
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      s.songEnds = false;
      s.sync = 2;
      s.play = true;
      s.freeTempo = 120.0;
      e.setSettings(s);
      e.reset();
      const std::vector<Bar> got = bars(e, 16, 0.0, false);
      JCLAP_CHECK_EQ(got[10].section, kOutro);
      JCLAP_CHECK_EQ(got[12].section, kIntro);
      JCLAP_CHECK_EQ(got[12].formPos, 0);
      JCLAP_CHECK_EQ(got[14].section, kVerse);
      JCLAP_CHECK(!got[11].ending);
   }
   // Following the host, the song is placed where the host is: bar 5 of Short (two bars a section)
   // is its first chorus's second bar, bar 6 the second verse.
   {
      auto song = twoBars();
      song->setForm(3);
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      e.setSettings(s);
      e.reset();
      const std::vector<Bar> got = bars(e, 3, 5 * 4.0, true);
      JCLAP_CHECK_EQ(got[0].section, kChorus);
      JCLAP_CHECK_EQ(got[0].formPos, 2);
      JCLAP_CHECK_EQ(got[1].section, kVerse);
      JCLAP_CHECK_EQ(got[1].formPos, 3);
      // Starting from the Section param's section: its first entry.
      auto engine2 = std::make_unique<Engine>();
      Engine &e2 = *engine2;
      e2.setSong(*song);
      s.section = kChorus;
      e2.setSettings(s);
      e2.reset();
      const std::vector<Bar> from = bars(e2, 5, 0.0, true);
      JCLAP_CHECK_EQ(from[0].section, kChorus);
      JCLAP_CHECK_EQ(from[2].section, kVerse);
      JCLAP_CHECK_EQ(from[2].formPos, 3);
   }
   // A section switched to by hand carries on from its next entry: in Pop (I V P C V P C B C C O),
   // the chorus asked for in the first verse goes on to the second verse.
   {
      auto song = twoBars();
      song->setForm(2);
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      s.sync = 2;
      s.play = true;
      s.freeTempo = 120.0;
      s.fillInto = false;
      e.setSettings(s);
      e.reset();
      std::vector<Bar> got = bars(e, 3, 0.0, false); // into the verse's first bar
      JCLAP_CHECK_EQ(got[2].section, kVerse);
      s.section = kChorus;
      e.setSettings(s);
      got = bars(e, 6, 0.0, false); // the chorus from the next bar line, for two bars, then on
      JCLAP_CHECK_EQ(got[0].section, kVerse);
      JCLAP_CHECK_EQ(got[1].section, kChorus);
      JCLAP_CHECK_EQ(got[1].formPos, 3);
      JCLAP_CHECK_EQ(got[3].section, kVerse);
      JCLAP_CHECK_EQ(got[3].formPos, 4);
      JCLAP_CHECK_EQ(got[5].section, kPre);
   }
   // A push at an entry's end plays the next entry's chord, not the next section's.
   {
      auto song = songOf("x.............x.", {{"C", nullptr}});
      for (int i = 0; i < kSections; ++i) {
         Section &sec = song->sections[static_cast<size_t>(i)];
         sec.bars = 2;
         sec.fill = sec.takes[0];
         for (Pattern &t : sec.takes)
            for (int k = 14; k < t.steps; k += 16)
               t.at(k).push = 1;
         for (Chord &c : sec.chords)
            c = chord(i == kChorus ? "F" : i == kVerse ? "Ab" : "C");
      }
      song->setForm(kCustomForm, {kIntro, kChorus, kOutro});
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      s.fillInto = false;
      e.setSettings(s);
      e.reset();
      const auto out = runWith(e, 24000 * 12, 0.0, [](int64_t, Block &) {});
      const std::set<int> push = startingAt(out, 24000 * 7 + 12000);
      JCLAP_CHECK(push.count(5) == 1 && push.count(8) == 0);
   }
   // NEXT steps through the structure's entries, a section that comes twice running included: in
   // Pop's C C, NEXT in the first chorus goes to the second, then the outro.
   {
      auto song = twoBars();
      song->setForm(2);
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      s.sync = 2;
      s.play = true;
      s.freeTempo = 120.0;
      s.fillInto = false;
      e.setSettings(s);
      e.reset();
      std::vector<Bar> got = bars(e, 16, 0.0, false); // to the first bar of the first of the two choruses
      JCLAP_CHECK_EQ(got[15].formPos, 7);
      // (Pressed once the chorus is heard, past the clock's lookahead: a NEXT in the last moments of
      // the entry before asks for the chorus itself.)
      {
         auto o = std::make_unique<OutBuffer>();
         for (int k = 0; k < 20; ++k) {
            Block b;
            b.frames = kBlock;
            b.tempo = 120.0;
            o->clear();
            e.process(b, *o);
         }
      }
      JCLAP_CHECK(e.noteOn(0, s.noteFor(kActNext), 100));
      got = bars(e, 4, 0.0, false);
      JCLAP_CHECK_EQ(got[0].formPos, 8);
      JCLAP_CHECK_EQ(got[1].section, kChorus);
      JCLAP_CHECK_EQ(got[1].formPos, 9);
      JCLAP_CHECK_EQ(got[3].section, kOutro);
      JCLAP_CHECK_EQ(got[3].formPos, 10);
   }
   // B01: NEXT twice in Short's first verse goes to the second verse (the section playing, another
   // entry), not back to nothing queued.
   {
      auto song = twoBars();
      song->setForm(3); // I V C V C O
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      s.sync = 2;
      s.play = true;
      s.freeTempo = 120.0;
      s.fillInto = false;
      e.setSettings(s);
      e.reset();
      std::vector<Bar> got = bars(e, 3, 0.0, false); // into the verse's first bar
      JCLAP_CHECK_EQ(got[2].formPos, 1);
      JCLAP_CHECK(e.noteOn(0, s.noteFor(kActNext), 100));
      JCLAP_CHECK(e.noteOn(0, s.noteFor(kActNext), 100));
      got = bars(e, 2, 0.0, false);
      JCLAP_CHECK_EQ(got[1].section, kVerse);
      JCLAP_CHECK_EQ(got[1].formPos, 3);
   }
   // A structure that ends on a section with no ending of its own ends on the outro's.
   {
      auto song = twoBars();
      song->setForm(kCustomForm, {kIntro, kChorus});
      song->sections[kChorus].ending = Pattern{};
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      s.sync = 2;
      s.play = true;
      s.freeTempo = 120.0;
      e.setSettings(s);
      e.reset();
      const std::vector<Bar> got = bars(e, 5, 0.0, false);
      JCLAP_CHECK(!got[2].ending && got[3].ending);
      JCLAP_CHECK(got[4].ended);
   }
   // A host's count-in: the song waits on its first entry, then starts from it at bar 1.
   {
      auto song = twoBars();
      song->setForm(3);
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      e.setSettings(s);
      e.reset();
      const std::vector<Bar> got = bars(e, 5, -8.0, true);
      for (int i = 0; i < 4; ++i) {
         JCLAP_CHECK_EQ(got[static_cast<size_t>(i)].section, kIntro);
         JCLAP_CHECK_EQ(got[static_cast<size_t>(i)].formPos, 0);
      }
      JCLAP_CHECK_EQ(got[4].section, kVerse);
      JCLAP_CHECK_EQ(got[4].formPos, 1);
   }
}


// The twelfth review's fixes.
JCOMP_TEST_NOINLINE static void roundTwelve() {
   const auto genreNamed = [](const char *name) {
      for (int i = 0; i < genreCount(); ++i)
         if (std::string(genreAt(i).name) == name)
            return i;
      return -1;
   };
   const auto styleNamed = [](int g, const char *name) {
      const Genre &gen = genreAt(g);
      for (size_t i = 0; i < gen.styles.size(); ++i)
         if (std::string(gen.styles[i].name) == name)
            return static_cast<int>(i);
      return -1;
   };
   const auto made = [](const Recipe &r) {
      auto s = std::make_unique<Song>();
      generateSong(r, *s);
      return s;
   };
   const auto pcsOf = [](const Voicing &v) {
      std::set<int> out;
      for (int i = 0; i < v.count; ++i)
         out.insert(v.notes[static_cast<size_t>(i)] % 12);
      return out;
   };
   // D06, D13: over a slash bass, SHELL, GRIP and CLOSE keep the chord's root, the bass lowest.
   for (int shape : {kShell, kGrip, kClose})
      for (int n = 3; n <= 5; ++n)
         for (const char *name : {"C/G", "G/B", "C7/E", "C7/G", "Cmaj7/E", "C9/E"}) {
            const Chord c = chord(name);
            VoiceSettings vs;
            vs.shape = shape;
            vs.voices = n;
            vs.lo = 48;
            vs.hi = 76;
            vs.center = 62;
            vs.floor = 40;
            const Voicing v = voice(c, nullptr, vs);
            JCLAP_CHECK(v.count > 0 && pcsOf(v).count(c.root) == 1);
            JCLAP_CHECK(v.count > 0 && v.notes[0] % 12 == c.bass);
         }
   // D05: 4THS voices an m9 at four and five voices with its third and seventh (the 9th where a
   // stack holds them too: the fourteenth review's D01, the So What stack keeps its third).
   for (int n : {4, 5}) {
      VoiceSettings vs;
      vs.shape = kQuartal;
      vs.voices = n;
      vs.lo = 52;
      vs.hi = 76;
      vs.center = 64;
      vs.key = 9;
      vs.scale = kMinor;
      for (const char *name : {"Am9", "Dm9"}) {
         const Chord c = chord(name);
         const std::set<int> p = pcsOf(voice(c, nullptr, vs));
         JCLAP_CHECK(p.count((c.root + 3) % 12) == 1 && p.count((c.root + 10) % 12) == 1);
      }
   }
   // D08: 4THS at five voices never plays fewer notes than at four.
   for (int q = 0; q < qualityCount(); ++q)
      for (int r = 0; r < 12; ++r) {
         VoiceSettings vs;
         vs.shape = kQuartal;
         vs.lo = 43;
         vs.hi = 86;
         vs.center = 62;
         vs.floor = 43;
         const Chord c{static_cast<int8_t>(r), static_cast<uint8_t>(q), -1};
         vs.voices = 4;
         const int four = voice(c, nullptr, vs).count;
         vs.voices = 5;
         JCLAP_CHECK(voice(c, nullptr, vs).count >= four);
      }
   // D11: a 7#9 stays under HIGHEST.
   for (int r = 0; r < 12; ++r)
      for (int shape : {kClose, kOctaves, kRootless}) {
         VoiceSettings vs;
         vs.shape = shape;
         vs.voices = 3;
         vs.lo = 55;
         vs.hi = 76;
         vs.center = 65;
         const Voicing v = voice(Chord{static_cast<int8_t>(r), static_cast<uint8_t>(chord("C7#9").quality), -1}, nullptr, vs);
         JCLAP_CHECK(v.count == 0 || v.top() <= vs.hi);
      }
   // D07: OCTAVES' line moves by steps and thirds, not a sixth's leap, through a montuno's changes.
   {
      VoiceSettings vs;
      vs.shape = kOctaves;
      vs.voices = 4;
      vs.lo = 55;
      vs.hi = 84;
      vs.center = 68;
      Voicing prev;
      int leaps = 0;
      for (int rep = 0; rep < 2; ++rep)
         for (const char *name : {"E", "A", "B7", "A"})
            for (int k = 0; k < 4; ++k) {
               const Voicing v = octaveLine(chord(name), prev.empty() ? nullptr : &prev, vs, k);
               if (!prev.empty() && std::abs(v.notes[0] - prev.notes[0]) > 5)
                  ++leaps;
               prev = v;
            }
      JCLAP_CHECK_EQ(leaps, 0);
   }
   // D12: a bass a fifth under the rest whose chord holds them is the root: Db7b9, not Ddim/Db.
   JCLAP_CHECK(nameChord(Ints{37, 65, 68, 74}).root == 1);
   // D14: a sharp minor key's major third keeps its letter; Gb blues's IV is Cb7.
   JCLAP_CHECK(std::string(pitchName(5, 1, kMinor)) == "E#");
   JCLAP_CHECK(std::string(pitchName(11, 6, kBlues)) == "Cb");
   // A03, A04: Interlock's chops are choked, and Mambo plays the Montuno player.
   {
      const int g = genreNamed("Afrobeat"), st = styleNamed(g, "Interlock");
      JCLAP_CHECK((genreAt(g).styles[static_cast<size_t>(st)].mods & kShorts) != 0);
      const int lj = genreNamed("Latin Jazz"), mambo = styleNamed(lj, "Mambo");
      JCLAP_CHECK_EQ(genreAt(lj).styles[static_cast<size_t>(mambo)].player, static_cast<int>(kMontuno));
      // A03: the tenor's line notes aren't chopped with the chords.
      const Recipe r = recipeOf(g, st, 1);
      auto song = made(r);
      const Pattern &p = song->sections[kVerse].takes[0];
      for (int s = 0; s < p.steps; ++s)
         if (p.at(s).on() && p.at(s).part == kPartLine)
            JCLAP_CHECK(p.at(s).art != kArtChop);
   }
   // C09: "run in" is never offered to a player that plays no runs, nor to a held style.
   {
      const Pattern p = fromRhythm("x...x...x...x...");
      JCLAP_CHECK(!mutationFits("run in", 0, p, nullptr, false));
      JCLAP_CHECK(!mutationFits("run in", kLongs, p, nullptr, true));
   }
   // C13: a tag never names a mutation twice.
   for (uint32_t seed = 1; seed < 200; ++seed) {
      Rng rng(seed);
      std::string tag;
      mutate(fromRhythm("x.x.x.x.x.x.x.x."), 1.0, rng, tag);
      std::set<std::string> seen;
      size_t at = 0;
      while (at < tag.size()) {
         const size_t plus = tag.find(" + ", at);
         const std::string one = tag.substr(at, plus == std::string::npos ? std::string::npos : plus - at);
         JCLAP_CHECK(seen.insert(one).second);
         at = plus == std::string::npos ? tag.size() : plus + 3;
      }
   }
   // C08: Tenor Line's takes aren't all the same as take 1.
   {
      const int g = genreNamed("Afrobeat"), st = styleNamed(g, "Tenor Line");
      int differ = 0;
      for (uint32_t seed = 1; seed <= 6; ++seed) {
         auto song = made(recipeOf(g, st, seed));
         const Section &v = song->sections[kVerse];
         for (int t = 1; t < kTakes; ++t)
            differ += !(v.takes[static_cast<size_t>(t)] == v.takes[0]);
      }
      JCLAP_CHECK(differ > 6);
   }
   // B01, B02: a NEXT queued before a stop doesn't fire after the restart, and stopped nothing shows
   // queued.
   {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}});
      for (Section &sec : song->sections)
         sec.bars = 2;
      song->setForm(3); // Short: I V C V C O
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.song = true;
      s.sync = 1;
      e.setSettings(s);
      e.reset();
      auto out = std::make_unique<OutBuffer>();
      double beats = 0.0;
      const auto run = [&](int blocks, bool playing) {
         for (int i = 0; i < blocks; ++i) {
            Block b;
            b.frames = kBlock;
            b.tempo = 120.0;
            b.hasTransport = true;
            b.playing = playing;
            b.beats = beats;
            out->clear();
            e.process(b, *out);
            if (playing)
               beats += kBlock / 24000.0;
         }
      };
      run(375 * 3, true); // into the first verse (bar 3: 96000 frames a bar)
      e.request(kActNext);
      run(10, true);
      run(10, false);
      Frame f;
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.queued, -1);
      beats = 0.0;
      std::vector<int> got;
      for (int bar = 0; bar < 4; ++bar) {
         run(187, true);
         e.fillFrame(f);
         got.push_back(f.section);
         run(188, true);
      }
      JCLAP_CHECK(got == (std::vector<int>{kIntro, kIntro, kVerse, kVerse}));
   }
}

// The thirteenth review's fixes.
JCOMP_TEST_NOINLINE static void roundThirteen() {
   const auto genreNamed = [](const char *name) {
      for (int i = 0; i < genreCount(); ++i)
         if (std::string(genreAt(i).name) == name)
            return i;
      return -1;
   };
   const auto styleNamed = [](int g, const char *name) {
      const Genre &gen = genreAt(g);
      for (size_t i = 0; i < gen.styles.size(); ++i)
         if (std::string(gen.styles[i].name) == name)
            return static_cast<int>(i);
      return -1;
   };
   const auto made = [](const Recipe &r) {
      auto s = std::make_unique<Song>();
      generateSong(r, *s);
      return s;
   };
   const auto pcsOf = [](const Voicing &v) {
      std::set<int> out;
      for (int i = 0; i < v.count; ++i)
         out.insert(v.notes[static_cast<size_t>(i)] % 12);
      return out;
   };
   const auto settings = [](int shape, int n, int lo, int hi, int center, int floor = 40) {
      VoiceSettings vs;
      vs.shape = shape;
      vs.voices = n;
      vs.lo = lo;
      vs.hi = hi;
      vs.center = center;
      vs.floor = floor;
      return vs;
   };
   // D01: 4THS at five voices gives a III7 (a minor key's V7) four notes or more, in every key.
   for (int k = 0; k < 12; ++k) {
      VoiceSettings vs = settings(kQuartal, 5, 43, 86, 62, 43);
      vs.key = k;
      const Chord c{static_cast<int8_t>((k + 4) % 12), static_cast<uint8_t>(chord("E7").quality), -1};
      JCLAP_CHECK(voice(c, nullptr, vs).count >= 4);
   }
   // D02: ROOTLESS at five voices: an altered dominant never holds the fifth beside its b13.
   for (const char *name : {"G7b9", "G7#9", "C7b9", "C7#9"}) {
      const Chord c = chord(name);
      const std::set<int> p = pcsOf(voice(c, nullptr, settings(kRootless, 5, 50, 72, 60)));
      JCLAP_CHECK(!(p.count((c.root + 7) % 12) && p.count((c.root + 8) % 12)));
   }
   // D03: a slash bass takes its chord tone's letter; passing diminished chords are raised steps.
   JCLAP_CHECK(chordName(chord("E7/G#"), 0) == "E7/G#");
   JCLAP_CHECK(chordName(chord("A7/C#"), 0) == "A7/C#");
   JCLAP_CHECK(chordName(chord("C#dim7"), 0) == "C#dim7");
   JCLAP_CHECK(chordName(chord("D#dim7"), 0) == "D#dim7");
   JCLAP_CHECK(chordName(chord("Ab"), 0) == "Ab");
   JCLAP_CHECK(chordName(chord("F/A"), 0) == "F/A");
   // D04: GRIP's open G7 (320001) is played at five strings, its F kept.
   {
      const Voicing v = voice(chord("G7"), nullptr, settings(kGrip, 5, 40, 76, 58));
      JCLAP_CHECK(pcsOf(v).count(5) == 1);
      JCLAP_CHECK(gripFrets(v) >= 0.0);
   }
   // D05: GRIP at four voices strums four strings on a seventh chord.
   for (const char *name : {"F7", "Bb7", "G7", "Dm7", "Bm7"}) {
      const Voicing v = voice(chord(name), nullptr, settings(kGrip, 4, 40, 76, 58));
      JCLAP_CHECK_EQ(v.count, 4);
      JCLAP_CHECK(gripFrets(v) >= 0.0);
   }
   // D06: a held fifth is the mode's own v in Dorian; a minor key's V is major.
   JCLAP_CHECK(nameChord(Ints{52, 59}, true, 9, kDorian).quality == chord("Em").quality);
   JCLAP_CHECK(nameChord(Ints{52, 59}, true, 9, kMinor).quality == chord("E").quality);
   // D07: 4THS at four voices keeps a dominant 9th's third and seventh.
   for (const char *name : {"G9", "F9", "C9"}) {
      const Chord c = chord(name);
      const std::set<int> p = pcsOf(voice(c, nullptr, settings(kQuartal, 4, 43, 86, 62, 43)));
      JCLAP_CHECK(p.count((c.root + 4) % 12) == 1 && p.count((c.root + 10) % 12) == 1);
   }
   // D09: an altered slash dominant at three voices keeps its seventh and its root.
   {
      const Ints t = pickTones(chord("C7b9/E"), 3);
      JCLAP_CHECK(std::find(t.begin(), t.end(), 10) != t.end());
      JCLAP_CHECK(std::find(t.begin(), t.end(), 0) != t.end());
   }
   // D10, D11: OCTAVES voices a 7#9 without a rub against its doubled note (the #9 on top, the
   // fourteenth review's D10), and holds C11's fourth.
   {
      const Voicing v = voice(chord("C7#9"), nullptr, settings(kOctaves, 4, 55, 84, 68));
      JCLAP_CHECK(v.count > 0 && clashes(v, chord("C7#9")) == 0.0);
      JCLAP_CHECK(pcsOf(voice(chord("C11"), nullptr, settings(kOctaves, 4, 55, 84, 68))).count(5) == 1);
   }
   // D14: a slash triad at four voices is four notes.
   for (int shape : {kClose, kDrop2})
      for (const char *name : {"C/E", "G/B"})
         JCLAP_CHECK_EQ(voice(chord(name), nullptr, settings(shape, 4, 43, 72, 58)).count, 4);
   // A01: Clav Riff's takes 2-4 keep its stab a chord.
   {
      const int g = genreNamed("Funk"), st = styleNamed(g, "Clav Riff");
      int lost = 0, varied = 0;
      for (uint32_t seed = 1; seed <= 20; ++seed) {
         auto song = made(recipeOf(g, st, seed));
         for (int sec : {kVerse, kChorus}) {
            const Section &S = song->sections[static_cast<size_t>(sec)];
            for (int t = 1; t < kTakes; ++t) {
               varied += !(S.takes[static_cast<size_t>(t)] == S.takes[0]);
               for (int k = 0; k < S.takes[0].steps; ++k)
                  if (S.takes[0].at(k).on() && S.takes[0].at(k).part == kPartChord && S.takes[0].at(k).vel >= kAccent)
                     lost += S.takes[static_cast<size_t>(t)].at(k).part != kPartChord;
            }
         }
      }
      JCLAP_CHECK_EQ(lost, 0);
      JCLAP_CHECK(varied > 60);
   }
   // A02: Interlock lays its two parts the same way in every section.
   {
      const int g = genreNamed("Afrobeat"), st = styleNamed(g, "Interlock");
      for (uint32_t seed = 1; seed <= 10; ++seed) {
         auto song = made(recipeOf(g, st, seed));
         const Pattern &a = song->sections[kIntro].takes[0];
         for (int sec = 1; sec < kSections; ++sec) {
            const Pattern &b = song->sections[static_cast<size_t>(sec)].takes[0];
            for (int k = 0; k < std::min<int>(a.steps, b.steps); ++k)
               JCLAP_CHECK(a.at(k).on() == b.at(k).on() && (!a.at(k).on() || a.at(k).part == b.at(k).part));
         }
      }
   }
   // A03: the batida's thumb takes the guitar's bass: the fifth under the root.
   {
      const int g = genreNamed("Bossa Nova"), st = styleNamed(g, "Batida");
      Settings s;
      applyRecipe(s, recipeOf(g, st, 1));
      JCLAP_CHECK(s.thumb);
   }
   // C04: the held styles' takes 2-4 are takes of their own.
   for (const char *name : {"Pad Intro", "Held Like Strings", "Long Tones", "Mirror Drift", "Bell Twelve"}) {
      int differ = 0;
      for (int g = 0; g < genreCount(); ++g) {
         const int st = styleNamed(g, name);
         if (st < 0)
            continue;
         for (uint32_t seed = 1; seed <= 5; ++seed) {
            auto song = made(recipeOf(g, st, seed));
            for (const Section &S : song->sections)
               for (int t = 1; t < kTakes; ++t)
                  differ += !(S.takes[static_cast<size_t>(t)] == S.takes[0]);
         }
      }
      JCLAP_CHECK(differ > 20);
   }
   // C05: WEIRD gives TOP RINGS only to a piano's pedal.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         Recipe r = recipeOf(g, st, 2);
         r.weird = 1.0;
         const int inst = playerOf(r).instrument;
         if (inst == kPiano || inst == kEPiano)
            continue;
         auto song = made(r);
         for (const Section &S : song->sections) {
            for (const Pattern &p : S.takes)
               for (int k = 0; k < p.steps; ++k)
                  JCLAP_CHECK(!(p.at(k).on() && p.at(k).art == kArtTopRings));
         }
      }
   // C03: DENSITY at its lowest keeps a broken chord's bass on the One.
   {
      const Pattern p = fromRhythm("b...3.5.8...5.3.");
      for (int d = 0; d < 10; ++d)
         JCLAP_CHECK(!thinnedBy(p, 0, 0.0, d / 10.0, false));
   }
   // C02: DENSITY thins a straight-eighth take a little at every notch, not a class at a time.
   {
      const Pattern p = fromRhythm("x.x.x.x.x.x.x.x.");
      int all = 0, last = -1;
      for (int k = 0; k < p.steps; ++k)
         all += p.at(k).on() ? 20 : 0;
      for (int notch = 0; notch <= 10; ++notch) {
         const double d = 0.5 - 0.05 * notch;
         int kept = 0;
         for (int k = 0; k < p.steps; ++k)
            for (int r = 0; r < 20; ++r)
               kept += p.at(k).on() && !thinnedBy(p, k, d, r / 20.0, false);
         if (notch == 2)
            JCLAP_CHECK(kept < all);
         if (last >= 0)
            JCLAP_CHECK(last - kept <= all / 5);
         last = kept;
      }
   }
   // C08: a fill's last chord, its lead into the change, counts as its lead pushed or not.
   JCLAP_CHECK(fillLead(fromRhythm("x...x...x...x.x."), 14));
   JCLAP_CHECK(!fillLead(fromRhythm("x...x...x...x.x."), 12));
   // A04: a fixed figure's push ties over an empty beat, not a struck one.
   {
      const Pattern p = fromRhythm("x...x.x...x...x.|..x...x...x...x.");
      JCLAP_CHECK(pushTies(p, 14, true));  // into the three side's silent one
      JCLAP_CHECK(!pushTies(p, 30, true)); // into the two side's struck one
   }
   const auto block = [](Engine &e, OutBuffer &out, double beats, bool playing, double barStart = -1, double barBeats = 0) {
      Block b;
      b.frames = kBlock;
      b.tempo = 120.0;
      b.hasTransport = true;
      b.playing = playing;
      b.beats = beats;
      if (barStart >= 0) {
         b.hasBar = true;
         b.barStart = barStart;
         b.barBeats = barBeats;
      }
      out.clear();
      e.process(b, out);
   };
   const double perBlock = kBlock / 24000.0;
   // B01: HOLD CHORD keeps its chord over the host's loop.
   {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}, {"G", nullptr}});
      for (Section &sec : song->sections)
         sec.bars = 2;
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.sync = 1;
      e.setSettings(s);
      e.reset();
      auto out = std::make_unique<OutBuffer>();
      double beats = 0.0;
      bool held = false;
      std::set<int> after;
      for (int pass = 0; pass < 3; ++pass)
         for (beats = 0.0; beats < 8.0 - 1e-9; beats += perBlock) {
            if (!held && pass == 0 && beats >= 5.0) {
               e.request(kActHoldChord);
               held = true;
            }
            block(e, *out, beats, true);
            if (pass > 0)
               for (size_t i = 0; i < out->count; ++i)
                  if ((out->items[i].status & 0xF0) == 0x90 && out->items[i].data2 > 0)
                     after.insert(out->items[i].data1 % 12);
         }
      JCLAP_CHECK(!after.empty());
      for (int pc : after)
         JCLAP_CHECK(pc == 7 || pc == 11 || pc == 2);
   }
   // B02, E11: a 2/4 pickup bar rests, as a count-in does, and the bar after it is struck once.
   for (int pass = 0; pass < 2; ++pass) {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}});
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.sync = 1;
      e.setSettings(s);
      e.reset();
      auto out = std::make_unique<OutBuffer>();
      int early = 0;
      std::map<int, double> lastOn;
      int twice = 0;
      for (double beats = 0.0; beats < 6.0; beats += perBlock) {
         if (beats < 2.0)
            block(e, *out, beats, true, 0.0, 2.0);
         else
            block(e, *out, beats, true, 2.0, 4.0);
         for (size_t i = 0; i < out->count; ++i)
            if ((out->items[i].status & 0xF0) == 0x90 && out->items[i].data2 > 0) {
               early += beats + 2 * perBlock < 2.0;
               const auto it = lastOn.find(out->items[i].data1);
               twice += it != lastOn.end() && beats - it->second < 0.1;
               lastOn[out->items[i].data1] = beats;
            }
      }
      JCLAP_CHECK_EQ(early, 0);
      JCLAP_CHECK_EQ(twice, 0);
   }
   // B04: SAME DICE Never following the host: a bar's chance hits are the same wherever playback
   // started (where they fall: the voicing leads on from what came before).
   {
      auto song = songOf("x.x.x.x.x.x.x.x.", {{"C", nullptr}, {"F", nullptr}});
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes)
            for (int k = 0; k < t.steps; ++k)
               if (t.at(k).on() && k % 4)
                  t.at(k).chance = 50;
      const auto bar10 = [&](double from) {
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.sync = 1;
         s.sameDiceBars = 0;
         e.setSettings(s);
         e.reset();
         auto out = std::make_unique<OutBuffer>();
         std::vector<int> got;
         for (double beats = from; beats < 44.0; beats += perBlock) {
            block(e, *out, beats, true);
            for (size_t i = 0; i < out->count; ++i)
               if ((out->items[i].status & 0xF0) == 0x90 && out->items[i].data2 > 0 && beats >= 40.0)
                  got.push_back(static_cast<int>(std::lround((beats - 40.0) * 4.0)));
         }
         got.erase(std::unique(got.begin(), got.end()), got.end());
         return got;
      };
      JCLAP_CHECK(bar10(0.0) == bar10(36.0));
   }
}

// The fourteenth review's fixes (the engine's; the generator's and the harmony's are in roundFourteenB).
JCOMP_TEST_NOINLINE static void roundFourteen() {
   const double perBlock = kBlock / 24000.0;
   const auto block = [](Engine &e, OutBuffer &out, double beats, bool playing, double barStart = -1, double barBeats = 0) {
      Block b;
      b.frames = kBlock;
      b.tempo = 120.0;
      b.hasTransport = true;
      b.playing = playing;
      b.beats = beats;
      if (barStart >= 0) {
         b.hasBar = true;
         b.barStart = barStart;
         b.barBeats = barBeats;
      }
      out.clear();
      e.process(b, out);
   };
   // The pitch classes struck in [from, to) beats of a run.
   struct Onset {
      double beat;
      int key;
   };
   const auto pcsIn = [](const std::vector<Onset> &on, double from, double to) {
      std::set<int> pcs;
      for (const Onset &o : on)
         if (o.beat >= from - 1e-6 && o.beat < to)
            pcs.insert(o.key % 12);
      return pcs;
   };
   const auto collect = [&](OutBuffer &out, double beats, std::vector<Onset> &on) {
      for (size_t i = 0; i < out.count; ++i)
         if ((out.items[i].status & 0xF0) == 0x90 && out.items[i].data2 > 0)
            on.push_back({beats + out.items[i].time / 24000.0 * 2.0, out.items[i].data1});
   };
   const auto engineFor = [](const Song &song, const Settings &s) {
      auto e = std::make_unique<Engine>();
      e->setSong(song);
      e->setSettings(s);
      e->reset();
      return e;
   };
   const std::set<int> cMajor{0, 4, 7}, gMajor{7, 11, 2};
   // B01: CHORDS FROM BOTH with LATCH: a chord tapped in one playback doesn't come back in the next,
   // and one tapped while stopped plays only to the first change.
   for (int pass = 0; pass < 2; ++pass) {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}, {"G", nullptr}});
      Settings s = plain();
      s.chordsFrom = 2;
      s.latch = true;
      s.sync = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      for (double b = 0.0; b < 30.5; b += perBlock)
         block(*e, *out, b, true);
      if (pass == 0) {
         for (int k : {67, 71, 74})
            e->noteOn(0, k, 100);
         block(*e, *out, 30.5, true);
         for (double b = 30.5 + perBlock; b < 30.8; b += perBlock)
            block(*e, *out, b, true);
         for (int k : {67, 71, 74})
            e->noteOff(0, k);
         for (double b = 30.8; b < 31.5; b += perBlock)
            block(*e, *out, b, true);
         block(*e, *out, 31.5, false);
      } else {
         block(*e, *out, 30.5, false);
         for (int k : {69, 72, 76}) // A minor, tapped while stopped
            e->noteOn(0, k, 100);
         block(*e, *out, 30.5, false);
         for (int k : {69, 72, 76})
            e->noteOff(0, k);
         block(*e, *out, 30.5, false);
      }
      std::vector<Onset> on;
      for (double b = 0.0; b < 12.0; b += perBlock) {
         block(*e, *out, b, true);
         collect(*out, b, on);
      }
      if (pass == 0) {
         JCLAP_CHECK(pcsIn(on, 0.0, 0.5) == cMajor);
      }
      else {
         JCLAP_CHECK(pcsIn(on, 0.0, 0.5) == (std::set<int>{9, 0, 4}));
         JCLAP_CHECK(pcsIn(on, 4.0, 4.5) == gMajor); // the first change lets it go
      }
      JCLAP_CHECK(pcsIn(on, 8.0, 8.5) == cMajor);
   }
   // B02: a 2/4 project under a 4/4 style counts from beat 0, wherever playback starts: no pickup.
   {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}, {"G", nullptr}});
      Settings s = plain();
      s.sync = 1;
      const auto run = [&](double from) {
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         std::vector<Onset> on;
         for (double b = from; b < 20.0; b += perBlock) {
            block(*e, *out, b, true, std::floor(b / 2.0) * 2.0, 2.0);
            collect(*out, b, on);
         }
         return on;
      };
      const auto a = run(0.0), b = run(8.0);
      // (The first playback's first bar, short, rests as a pickup would, and the song keeps that
      // footing until playback stops rather than jumping onto beat 0's bars mid-play.)
      JCLAP_CHECK(pcsIn(a, 6.0, 6.5) == gMajor);
      JCLAP_CHECK(pcsIn(a, 14.0, 14.5) == gMajor);
      JCLAP_CHECK(pcsIn(b, 12.0, 12.5) == gMajor);
      JCLAP_CHECK(pcsIn(b, 16.0, 16.5) == cMajor);
   }
   // B06: HOLD CHORD pressed while stopped holds the first chord struck, not the last playback's.
   {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}, {"G", nullptr}});
      Settings s = plain();
      s.sync = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      for (double b = 0.0; b < 6.0; b += perBlock) // stops on G
         block(*e, *out, b, true);
      block(*e, *out, 6.0, false);
      e->request(kActHoldChord);
      block(*e, *out, 6.0, false);
      std::vector<Onset> on;
      for (double b = 0.0; b < 8.0; b += perBlock) {
         block(*e, *out, b, true);
         collect(*out, b, on);
      }
      JCLAP_CHECK(pcsIn(on, 0.0, 8.0) == cMajor);
   }
   // C07: LENGTH never adds a strike: a push tied over its beat leaves that beat unstruck at every
   // LENGTH.
   {
      auto song = songOf("x.....x.....x...", {{"C", nullptr}});
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes)
            t.at(6).push = 1;
      const auto onsets = [&](double length) {
         Settings s = plain();
         s.sync = 1;
         s.length = length;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         std::vector<Onset> on;
         for (double b = 0.0; b < 8.0; b += perBlock) {
            block(*e, *out, b, true);
            collect(*out, b, on);
         }
         std::set<long> at;
         for (const Onset &o : on)
            at.insert(std::lround(o.beat * 8.0));
         return at;
      };
      JCLAP_CHECK(onsets(0.49) == onsets(0.5));
      JCLAP_CHECK(onsets(0.1) == onsets(0.5));
   }
   // E10: a note placed in the past (a start's catch-up of the step just gone) still has a length.
   {
      auto song = songOf("g.g.g.g.g.g.g.g.", {{"C", nullptr}});
      Settings s = plain();
      s.sync = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      std::map<int, uint32_t> onAt;
      int zero = 0;
      for (double b = 0.06; b < 2.0; b += perBlock) {
         block(*e, *out, b, true);
         for (size_t i = 0; i < out->count; ++i) {
            const auto &m = out->items[i];
            if ((m.status & 0xF0) == 0x90 && m.data2 > 0)
               onAt[m.data1] = m.time;
            else if (onAt.count(m.data1) && onAt[m.data1] != UINT32_MAX) {
               zero += m.time == onAt[m.data1];
               onAt[m.data1] = UINT32_MAX;
            }
         }
      }
      JCLAP_CHECK_EQ(zero, 0);
   }
}

// The fourteenth review's harmony fixes (review D).
JCOMP_TEST_NOINLINE static void roundFourteenB() {
   const auto pcsOf = [](const Voicing &v) {
      std::set<int> p;
      for (int i = 0; i < v.count; ++i)
         p.insert(v.notes[static_cast<size_t>(i)] % 12);
      return p;
   };
   const auto wide = [](int shape, int n) {
      VoiceSettings vs;
      vs.shape = shape;
      vs.voices = n;
      vs.lo = 43;
      vs.hi = 86;
      vs.center = 62;
      vs.floor = 43;
      return vs;
   };
   const auto at = [](const char *name, int r) {
      Chord c = chord(name);
      const int d = (r - c.root + 12) % 12;
      c.root = static_cast<int8_t>((c.root + d) % 12);
      if (c.bass >= 0)
         c.bass = static_cast<int8_t>((c.bass + d) % 12);
      return c;
   };
   for (int r = 0; r < 12; ++r) {
      // D01: 4THS keeps an m9's and an m11's minor third.
      for (int n : {3, 4, 5})
         for (const char *name : {"Cm9", "Cm11"}) {
            const Chord c = at(name, r);
            JCLAP_CHECK(pcsOf(voice(c, nullptr, wide(kQuartal, n))).count((r + 3) % 12) == 1);
         }
      // D03, D04: 4THS never drops a chord's seventh.
      for (int n : {4, 5}) {
         const Chord c = at("C7b9", r);
         JCLAP_CHECK(pcsOf(voice(c, nullptr, wide(kQuartal, n))).count((r + 10) % 12) == 1);
      }
      for (int n : {3, 4}) {
         JCLAP_CHECK(pcsOf(voice(at("Cmaj9", r), nullptr, wide(kQuartal, n))).count((r + 11) % 12) == 1);
         JCLAP_CHECK(pcsOf(voice(at("C9", r), nullptr, wide(kQuartal, n))).count((r + 10) % 12) == 1);
         JCLAP_CHECK(pcsOf(voice(at("C13", r), nullptr, wide(kQuartal, n))).count((r + 10) % 12) == 1);
      }
      // D02: slash chords keep the bass lowest and the chord's root above it, in every shape that
      // voices from a bass.
      for (int shape : {kClose, kDrop2, kDrop24, kShell, kQuartal, kSpread, kGrip})
         for (int n : {3, 4, 5})
            for (const char *name : {"C/E", "C/G", "C7/E", "C7/G", "Cmaj7/G", "Cm/A", "Cm7b5/Gb", "Cadd9/E", "C7b9/E"}) {
               const Chord c = at(name, r);
               VoiceSettings vs = wide(shape, n);
               if (shape == kGrip) {
                  vs.lo = 40;
                  vs.hi = 76;
                  vs.center = 58;
                  vs.floor = 40;
               }
               const Voicing v = voice(c, nullptr, vs);
               JCLAP_CHECK(v.count > 0 && v.notes[0] % 12 == c.bass && pcsOf(v).count(c.root) == 1);
            }
      // D11: SHELL keeps an add9's 9th, and at three voices m7b5's b5 (a 7#9 keeps its 3rd and 7th: the
      // fifteenth review's D05).
      {
         JCLAP_CHECK(pcsOf(voice(at("Cadd9", r), nullptr, wide(kShell, 4))).count((r + 2) % 12) == 1);
         JCLAP_CHECK(pcsOf(voice(at("Cm7b5", r), nullptr, wide(kShell, 3))).count((r + 6) % 12) == 1);
         JCLAP_CHECK(pcsOf(voice(at("C7#9", r), nullptr, wide(kShell, 3))).count((r + 10) % 12) == 1);
      }
      // D16: ROOTLESS 13 at three voices has its 13th.
      JCLAP_CHECK(pcsOf(voice(at("C13", r), nullptr, wide(kRootless, 3))).count((r + 9) % 12) == 1);
      // D06: a fretted GRIP needs four fingers at most (gripFrets refuses a fifth).
      for (int q = 0; q < qualityCount(); ++q)
         for (int n : {4, 5}) {
            VoiceSettings vs = wide(kGrip, n);
            vs.lo = 40;
            vs.hi = 76;
            vs.center = 58;
            vs.floor = 40;
            const Voicing v = voice(Chord{static_cast<int8_t>(r), static_cast<uint8_t>(q), -1}, nullptr, vs);
            JCLAP_CHECK(v.count == 0 || gripFrets(v) >= 0.0);
         }
   }
   // D12: GRIP's open G7 at four strings keeps its 7th on neighbouring strings.
   {
      VoiceSettings vs = wide(kGrip, 4);
      vs.lo = 40;
      vs.hi = 76;
      vs.center = 58;
      vs.floor = 40;
      const Voicing v = voice(chord("G7"), nullptr, vs);
      JCLAP_CHECK(pcsOf(v).count(5) == 1 && gripFrets(v) >= 0.0);
   }
   // D13: the DROP 2+4 slash fallback doesn't double the bass or lose the third.
   {
      const Voicing v = voice(chord("Cm7b5/Gb"), nullptr, wide(kDrop24, 4));
      int gb = 0;
      for (int i = 0; i < v.count; ++i)
         gb += v.notes[static_cast<size_t>(i)] % 12 == 6;
      JCLAP_CHECK(pcsOf(v).count(3) == 1 && gb == 1);
   }
   // D07, D08, D15: spellings.
   JCLAP_CHECK(chordName(Chord{1, kMin7, -1}, 7) == "C#m7");
   JCLAP_CHECK(chordName(Chord{8, kMin7, -1}, 2) == "G#m7");
   JCLAP_CHECK(chordName(Chord{6, kDom7, -1}, 0) == "Gb7");
   JCLAP_CHECK(chordName(Chord{1, kDom7, 5}, 9) == "C#7/E#");
   JCLAP_CHECK(chordName(Chord{8, kDom7, 0}, 4) == "G#7/B#");
   JCLAP_CHECK(chordName(Chord{0, kDim7, -1}, 9, kMinor) == "Cdim7");
   JCLAP_CHECK(chordName(Chord{5, kDim7, -1}, 9, kMinor) == "Fdim7");
   JCLAP_CHECK(chordName(Chord{11, kDim7, -1}, 6) != "Cbdim7");
   JCLAP_CHECK(chordName(Chord{5, kMaj, -1}, 1, kMinor) == "F");
   JCLAP_CHECK(chordName(chord("C#dim7"), 0) == "C#dim7");
   // D01, D14: the So What stack and its kin are minor elevenths.
   JCLAP_CHECK(chordName(nameChord(Ints{50, 55, 60, 65}), 0) == "Dm11");
   JCLAP_CHECK(chordName(nameChord(Ints{52, 57, 62, 67, 71}), 0) == "Em11");
   JCLAP_CHECK(chordName(nameChord(Ints{43, 46, 48, 50, 53}), 0) == "Gm11");
   JCLAP_CHECK(chordName(nameChord(Ints{48, 53, 58, 63, 67}), 0) == "Cm11");
}

// Round fourteen, review C (takes, mutations, the generator) and A06/A07.
JCOMP_TEST_NOINLINE static void roundFourteenC() {
   const auto styleOfName = [](const char *sn, int &g, int &st) {
      for (g = 0; g < genreCount(); ++g)
         for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            if (std::strcmp(genreAt(g).styles[static_cast<size_t>(st)].name, sn) == 0)
               return true;
      return false;
   };
   // The four takes of every section of every style, as makeSection bred them, with their tags.
   const auto eachSection = [](const auto &fn) {
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            for (uint32_t seed = 1; seed <= 6; ++seed) {
               auto song = std::make_unique<Song>();
               const Recipe r = recipeOf(g, st, seed);
               generateSong(r, *song);
               for (int sec = 0; sec < kSections; ++sec)
                  fn(r, sec, song->sections[static_cast<size_t>(sec)]);
            }
   };
   // C03: takes 1-4 are four different takes in every style.
   eachSection([](const Recipe &, int, const Section &s) {
      for (int a = 0; a < kTakes; ++a)
         for (int b = a + 1; b < kTakes; ++b)
            JCLAP_CHECK(s.takes[static_cast<size_t>(a)] != s.takes[static_cast<size_t>(b)]);
   });
   // C01, C04, C05: no take loses its parent's backbeat accent or a bar's first chord, nor changes a
   // left hand's note; C06: a pulse's beats keep one articulation.
   eachSection([](const Recipe &r, int, const Section &s) {
      const Pattern &p = s.takes[0];
      const Player &pl = playerOf(r);
      const unsigned mods = styleOf(r).mods;
      for (int t = 1; t < kTakes; ++t) {
         const Pattern &c = s.takes[static_cast<size_t>(t)];
         for (int z = 0; z < p.steps; ++z) {
            if (p.at(z).on() && leftHand(p.at(z).part))
               JCLAP_CHECK(c.at(z) == p.at(z));
            if (p.at(z).on() && !leftHand(p.at(z).part) && (accentAt(p, z) || firstChord(p, z)))
               JCLAP_CHECK(c.at(z).on());
         }
      }
      const bool keys = pl.instrument != kOrgan && pl.kind != kGuitarKind && !(mods & (kFixed | kShorts));
      for (int t = 0; t < kTakes && keys && pulseTake(p); ++t) {
         const Pattern &q = s.takes[static_cast<size_t>(t)];
         if (!pulseTake(q))
            continue;
         int art = -1;
         for (int z = 0; z < q.steps; z += q.perBeat())
            if (pulseBeat(q, z) && q.at(z).len <= 1) {
               if (art < 0)
                  art = q.at(z).art;
               JCLAP_CHECK(q.at(z).art == art);
            }
      }
   });
   // C01: shift moves no chord off a beat; sparser keeps accents, the bar's first chord, and every
   // stroke of a 2-and-4 take.
   for (uint64_t k = 0; k < 50; ++k) {
      Rng rng(k);
      Pattern p = fromRhythm("x...X.o.x...X.o.");
      applyMutation(p, "shift", rng);
      for (int s = 0; s < p.steps; s += 4)
         JCLAP_CHECK(p.at(s).on());
      Pattern q = fromRhythm("x...X.o.x...X.o.");
      applyMutation(q, "sparser", rng);
      for (int s : {0, 4, 12, 16, 20, 28})
         JCLAP_CHECK(q.at(s).on());
   }
   JCLAP_CHECK(!mutationFits("sparser", kShorts, fromRhythm("....g.......g...")));
   // C02: one second bow, and none in an ambient drift.
   for (uint64_t k = 0; k < 40; ++k) {
      Rng rng(k);
      Pattern p = fromRhythm("x---------------|----------------");
      JCLAP_CHECK(applyMutation(p, "restrike", rng));
      JCLAP_CHECK_EQ(p.count(), 2);
      JCLAP_CHECK(p.at(8).on());
   }
   JCLAP_CHECK(!mutationFits("restrike", kLongs, fromRhythm("x---------------|----------------"), "Ambient"));
   JCLAP_CHECK(mutationFits("restrike", kLongs, fromRhythm("x---------------|----------------"), "Pop"));
   // C04 and C05: accents and chop it leave the left hand's fifth as it is; chop it isn't offered to
   // a choked style.
   for (uint64_t k = 0; k < 50; ++k) {
      Rng rng(k);
      Pattern p = fromRhythm("x.f.X...x...X...");
      p.at(2).vel = kSoft;
      Pattern a = p, c = p;
      applyMutation(a, "accents", rng);
      applyMutation(c, "chop it", rng);
      JCLAP_CHECK(a.at(2) == p.at(2));
      JCLAP_CHECK(c.at(2) == p.at(2));
   }
   JCLAP_CHECK(!mutationFits("chop it", kShorts | kBusy, fromRhythm("X.g.x.g.X.g.x.g.")));
   // C06: "let it ring" and "chop it" treat a pulse's beats together.
   for (uint64_t k = 0; k < 50; ++k)
      for (const char *t : {"let it ring", "chop it"}) {
         Rng rng(k);
         Pattern p = fromRhythm("x...X...x...X...");
         applyMutation(p, t, rng);
         for (int s = 0; s < p.steps; s += 4)
            JCLAP_CHECK(p.at(s).art == p.at(0).art);
      }
   // C11: a one drop's ghosts only follow its skank, inside the beat.
   for (uint64_t k = 0; k < 50; ++k) {
      Rng rng(k);
      Pattern p = fromRhythm("....g.......g...");
      applyMutation(p, "ghosts", rng);
      for (int s = 0; s < p.steps; ++s)
         if (p.at(s).on())
            JCLAP_CHECK(s % 16 == 4 || s % 16 == 6 || s % 16 == 12 || s % 16 == 14);
   }
   // C12: a 3/4 restrike falls on the bar's third beat (8, 20), never on bar 2's second (16); a 6/8
   // push ties to the next dotted beat (6, 12).
   {
      bool both = false;
      for (uint64_t k = 1; k <= 40; ++k) {
         Rng rng(k);
         Pattern p = fromRhythm("x-----------|x-----------", kRate34);
         JCLAP_CHECK(applyMutation(p, "restrike", rng));
         for (int s = 0; s < p.steps; ++s)
            if (p.at(s).on())
               JCLAP_CHECK(s == 0 || s == 8 || s == 12 || s == 20);
         both = both || (p.at(8).on() && p.at(20).on());
      }
      JCLAP_CHECK(both);
      const Pattern p = fromRhythm("x.....x.....|x...........", kRate68);
      JCLAP_CHECK(!pushTies(p, 4, true));  // into 6, struck
      JCLAP_CHECK(!pushTies(p, 10, true)); // into 12, struck
      JCLAP_CHECK(pushTies(p, 16, true));  // into 18, empty
   }
   // C09: MUTATE THIS TAKE on the ending keeps it an ending; on a fill, its lead; on a fill down,
   // its onsets and its pushes (a fixed figure's, as written).
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Recipe r = recipeOf(g, st, 1);
         auto song = std::make_unique<Song>();
         generateSong(r, *song);
         const Style &sy = styleOf(r);
         const Pattern &end = song->sections[kOutro].ending;
         int last = -1;
         for (int s = end.steps - 1; s >= 0 && last < 0; --s)
            if (end.at(s).on() && !leftHand(end.at(s).part))
               last = s;
         for (uint64_t k = 0; k < 50 && last >= 0; ++k) {
            Rng rng(1000 + k);
            std::string tag;
            const Pattern m = mutate(end, 0.4, rng, tag, sy.mods, genreAt(g).name, false, sy.bpm, true, true, kMutateEnding);
            JCLAP_CHECK(m.at(last).on() && m.at(last).art == kArtRing && m.at(last).vel >= chordPeak(m));
            for (int s = 0; s < m.steps; ++s) {
               JCLAP_CHECK(m.at(s).on() == end.at(s).on());
               if (s >= m.steps - m.stepsPerBar())
                  JCLAP_CHECK(!m.at(s).push);
            }
         }
         for (const Section &sec : song->sections) {
            for (uint64_t k = 0; k < 20 && !sec.fill.empty(); ++k) {
               Rng rng(77 + k);
               std::string tag;
               const Pattern m = mutate(sec.fill, 0.4, rng, tag, sy.mods, genreAt(g).name, false, sy.bpm, true, true, kMutateFill);
               for (int s = 0; s < sec.fill.steps; ++s)
                  if (fillLead(sec.fill, s))
                     JCLAP_CHECK(m.at(s) == sec.fill.at(s));
            }
            for (uint64_t k = 0; k < 20; ++k) {
               Rng rng(99 + k);
               std::string tag;
               const Pattern m = mutate(sec.fillDown, 0.4, rng, tag, sy.mods, genreAt(g).name, false, sy.bpm, true, true, kMutateFillDown);
               JCLAP_CHECK(m.empty() == sec.fillDown.empty());
               for (int s = 0; s < m.steps; ++s)
                  JCLAP_CHECK(m.at(s).on() == sec.fillDown.at(s).on() && m.at(s).push == sec.fillDown.at(s).push);
            }
         }
      }
   // C10: a line style at a near-silent section keeps its line and its bass on each one.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
         for (uint32_t seed = 1; seed <= 6; ++seed) {
            Recipe r = recipeOf(g, st, seed), loud = r;
            r.sections.fill(0.05);
            loud.sections.fill(0.5);
            auto a = std::make_unique<Song>(), b = std::make_unique<Song>();
            generateSong(r, *a);
            generateSong(loud, *b);
            for (int sec = 0; sec < kSections; ++sec) {
               const Pattern &q = a->sections[static_cast<size_t>(sec)].takes[0], &w = b->sections[static_cast<size_t>(sec)].takes[0];
               if (!hasLine(w))
                  continue;
               int lw = 0, lq = 0;
               for (int s = 0; s < w.steps; ++s)
                  lw += w.at(s).on() && w.at(s).part == kPartLine;
               for (int s = 0; s < q.steps; ++s)
                  lq += q.at(s).on() && q.at(s).part == kPartLine;
               JCLAP_CHECK(lq * 2 >= lw);
               if (hasLeftHand(w))
                  for (int s = 0; s < q.steps; s += q.stepsPerBar()) {
                     bool bass = false;
                     for (int k = s; k < s + q.perBeat(); ++k)
                        bass = bass || (q.at(k).on() && leftHand(q.at(k).part));
                     JCLAP_CHECK(bass);
                  }
            }
         }
   // C13: the first notch below the middle thins little: no hit of a written sixteenth groove with
   // dice above 0.2 goes at DENSITY 0.45.
   {
      const Pattern p = fromRhythm("x.g.x.gxx.g.x.gx");
      int gone = 0, hits = 0;
      for (int s = 0; s < p.steps; ++s)
         for (double dice : {0.1, 0.3, 0.5, 0.7, 0.9})
            if (p.at(s).on()) {
               ++hits;
               gone += thinnedBy(p, s, 0.45, dice, false);
            }
      JCLAP_CHECK(gone * 100 <= hits * 15);
   }
   // C14: a line style ends on its own last bar; C15: the palm-wine fill keeps its chord on 4.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Recipe r = recipeOf(g, st, 1);
         if (styleOf(r).mods & kLongs)
            continue;
         auto song = std::make_unique<Song>();
         generateSong(r, *song);
         const Section &o = song->sections[kOutro];
         if (!hasLine(o.takes[0]))
            continue;
         const Pattern lb = lastBar(o.takes[0]);
         for (int s = 0; s < lb.steps; ++s) {
            Hit h = lb.at(s);
            h.push = 0;
            // (a montuno's anticipated last chord takes the and of 4: the mambo's broken chord's)
            if (!(o.ending.at(s).push && o.ending.at(s).len > lb.steps - s))
               JCLAP_CHECK(o.ending.at(s) == h);
         }
      }
   {
      int g = 0, st = 0;
      JCLAP_CHECK(styleOfName("Palm-Wine", g, st));
      for (uint32_t seed = 1; seed <= 6; ++seed) {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         for (const Section &s : song->sections)
            JCLAP_CHECK(s.fill.at(12).on() && chordPart(s.fill.at(12).part));
      }
   }
   // A06: Mambo's chorus always plays its guajeo (no line); A07: the Cha-Cha's sometimes does.
   {
      int g = 0, st = 0, chaGuajeo = 0;
      JCLAP_CHECK(styleOfName("Mambo", g, st));
      for (uint32_t seed = 1; seed <= 50; ++seed) {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         JCLAP_CHECK(!hasLine(song->sections[kChorus].takes[0]));
      }
      JCLAP_CHECK(styleOfName("Cha-Cha", g, st));
      for (uint32_t seed = 1; seed <= 50; ++seed) {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         const Pattern &t = song->sections[kChorus].takes[0];
         int held = 0;
         for (int s = 0; s < t.steps; ++s) {
            held += t.at(s).on() && t.at(s).len == 2;
            JCLAP_CHECK(!t.at(s).push); // (the cha-cha doesn't anticipate)
         }
         chaGuajeo += held >= 4;
         // (its verse is still the quarters or the cell)
         JCLAP_CHECK(song->sections[kVerse].takes[0].at(2).len <= 1);
      }
      JCLAP_CHECK(chaGuajeo >= 25 && chaGuajeo < 50);
   }
}

// Round fifteen (D): 4THS built of fourths, SHELL guide tones, GRIP five strings, SPREAD over a slash bass,
// the drops, slash chords within VOICES, held-note names, V spellings, and voicing picks close by.
JCOMP_TEST_NOINLINE static void roundFifteenD() {

   // D01: 4THS at VOICES 4 and 5 is built of fourths: every dominant has a perfect fourth between
   // neighbouring notes over its lowest (two at five, but a 7#11's, whose #11 takes odd intervals),
   // its 3rd and 7th held.
   for (const char *name : {"C7", "C9", "C13", "C7#11"})
      for (int r = 0; r < 12; ++r)
         for (int voices : {4, 5}) {
            VoiceSettings vs;
            vs.shape = kQuartal;
            vs.voices = voices;
            vs.hi = 79;
            const Chord c = chord(name, r);
            const Voicing v = voice(c, nullptr, vs);
            int fourths = 0;
            std::set<int> pcs;
            for (int i = 0; i < v.count; ++i) {
               pcs.insert((v.notes[static_cast<size_t>(i)] - c.root + 120) % 12);
               if (i >= 2 && v.notes[static_cast<size_t>(i)] - v.notes[static_cast<size_t>(i - 1)] == 5)
                  ++fourths;
            }
            JCLAP_CHECK(fourths >= (voices == 5 && std::string(name) != "C7#11" ? 2 : 1));
            JCLAP_CHECK(pcs.count(4) == 1 && pcs.count(10) == 1);
         }
   // D01: C9 at four is the left hand's Bb E A D, its root left to the bass, not C E Bb D.
   {
      VoiceSettings vs;
      vs.shape = kQuartal;
      const Voicing v = voice(chord("C9"), nullptr, vs);
      JCLAP_CHECK(v.count == 4 && v.notes[0] % 12 == 10 && v.notes[3] % 12 == 2);
   }
   // D02: 4THS voices a major or minor triad in four notes with a fourth in it at VOICES 4 and 5,
   // in context after each chord of I-IV-V-vi.
   for (int key = 0; key < 12; ++key)
      for (int voices : {4, 5}) {
         VoiceSettings vs;
         vs.shape = kQuartal;
         vs.voices = voices;
         vs.key = key;
         Voicing prev;
         Chord before;
         for (int pass = 0; pass < 2; ++pass)
            for (const char *name : {"C", "F", "G", "Am"}) {
               const Chord c = chord(name, key);
               const Voicing v = voice(c, prev.empty() ? nullptr : &prev, vs, -1, before.none() ? nullptr : &before);
               bool fourth = false;
               for (int i = 1; i < v.count; ++i)
                  fourth = fourth || v.notes[static_cast<size_t>(i)] - v.notes[static_cast<size_t>(i - 1)] == 5;
               JCLAP_CHECK(v.count >= 4 && fourth);
               prev = v;
               before = c;
            }
      }
   // D03: 4THS keeps a 7b9 in fourths (b7-3-b13-b9 or b7-3-13-b9), not SPREAD's voicing.
   {
      VoiceSettings vs;
      vs.shape = kQuartal;
      vs.voices = 5;
      const Voicings a = candidates(chord("C7b9"), vs), b = candidates(chord("C7b9"), vs, kSpread);
      bool same = a.size() == b.size();
      for (size_t i = 0; same && i < a.size(); ++i)
         same = a[i] == b[i];
      JCLAP_CHECK(!same);
      const Voicing v = voice(chord("C7b9"), nullptr, vs);
      std::set<int> pcs;
      for (int i = 0; i < v.count; ++i)
         pcs.insert(v.notes[static_cast<size_t>(i)] % 12);
      JCLAP_CHECK(pcs.count(1) == 1 && pcs.count(4) == 1 && pcs.count(10) == 1);
   }
   // D04: SPREAD never doubles a seventh chord's slash bass above it.
   for (const char *name : {"D7/F#", "E7/G#", "C7/Bb", "Cmaj7/E"})
      for (int voices : {4, 5}) {
         VoiceSettings vs;
         vs.shape = kSpread;
         vs.voices = voices;
         const Chord c = chord(name);
         const Voicing v = voice(c, nullptr, vs);
         JCLAP_CHECK(v.count > 0 && v.notes[0] % 12 == c.bass);
         for (int i = 1; i < v.count; ++i)
            JCLAP_CHECK(v.notes[static_cast<size_t>(i)] % 12 != c.bass);
      }
   // D05: SHELL at three voices is root, 3rd and b7 for every dominant, altered or not.
   for (const char *name : {"C7", "C9", "C13", "C7b9", "C7#9", "C7#11", "C7#5"})
      for (int r = 0; r < 12; ++r) {
         VoiceSettings vs;
         vs.shape = kShell;
         vs.voices = 3;
         const Chord c = chord(name, r);
         const Voicing v = voice(c, nullptr, vs);
         std::set<int> pcs;
         for (int i = 0; i < v.count; ++i)
            pcs.insert((v.notes[static_cast<size_t>(i)] - c.root + 120) % 12);
         JCLAP_CHECK(v.count == 3 && pcs.count(0) && pcs.count(4) && pcs.count(10));
      }
   // D06: GRIP at five strings holds a 13th's 13th, and a hand can make it.
   for (const char *name : {"C13", "F13", "C13sus"}) {
      VoiceSettings vs;
      vs.shape = kGrip;
      vs.voices = 5;
      vs.lo = 40;
      vs.hi = 76;
      vs.center = 58;
      vs.floor = 40;
      const Chord c = chord(name);
      const Voicing v = voice(c, nullptr, vs);
      bool thirteen = false;
      for (int i = 0; i < v.count; ++i)
         thirteen = thirteen || (v.notes[static_cast<size_t>(i)] - c.root + 120) % 12 == 9;
      JCLAP_CHECK(v.count == 5 && thirteen && gripFrets(v) >= 0.0);
   }
   // D09: SHELL at three voices keeps a dominant's b7 over its fifth: C7/G is G C Bb.
   {
      VoiceSettings vs;
      vs.shape = kShell;
      vs.voices = 3;
      const Voicing v = voice(chord("C7/G"), nullptr, vs);
      bool flat7 = false;
      for (int i = 0; i < v.count; ++i)
         flat7 = flat7 || v.notes[static_cast<size_t>(i)] % 12 == 10;
      JCLAP_CHECK(v.count == 3 && v.notes[0] % 12 == 7 && flat7);
   }
   // D10: GRIP at four strings is four strings where a full grip sits low: Fm7 1311xx, Ab7 4645xx.
   for (const char *name : {"Fm7", "Ab7"}) {
      VoiceSettings vs;
      vs.shape = kGrip;
      vs.voices = 4;
      vs.lo = 40;
      vs.hi = 76;
      vs.center = 58;
      vs.floor = 40;
      const Voicing v = voice(chord(name), nullptr, vs);
      JCLAP_CHECK(v.count == 4 && gripFrets(v) >= 0.0);
   }
   // D11: a seventh chord over its own seventh is that chord inverted.
   {
      const Chord c = nameChord(Ints{58, 60, 64, 67});
      JCLAP_CHECK(c.root == 0 && c.quality == kDom7 && c.bass == 10);
      JCLAP_CHECK(chordName(nameChord(Ints{59, 60, 64, 67})) == "Cmaj7/B");
      JCLAP_CHECK(chordName(nameChord(Ints{52, 60, 67})) == "C/E");
   }
   // D12: a slash chord's voicing in CLOSE, DROP 2 and DROP 2+4 is never more than VOICES notes.
   for (int shape : {kClose, kDrop2, kDrop24})
      for (int voices = 3; voices <= 5; ++voices)
         for (const char *name : {"C13/E", "C6/9/G", "C9/E", "C7/Bb", "Cm11/Eb", "C/E"}) {
            VoiceSettings vs;
            vs.shape = shape;
            vs.voices = voices;
            const Chord c = chord(name);
            const Voicing v = voice(c, nullptr, vs);
            JCLAP_CHECK(v.count > 0 && v.count <= voices && v.notes[0] % 12 == c.bass);
         }
   // D13: DROP 2 and DROP 2+4 drop a triad from its four-note close position.
   {
      VoiceSettings vs;
      vs.voices = 4;
      vs.shape = kClose;
      const Voicing close = voice(chord("C"), nullptr, vs);
      vs.shape = kDrop2;
      const Voicing drop2 = voice(chord("C"), nullptr, vs);
      vs.shape = kDrop24;
      const Voicing drop24 = voice(chord("C"), nullptr, vs);
      JCLAP_CHECK(!(drop2 == close) && !(drop24 == close) && !(drop24 == drop2));
      JCLAP_CHECK(drop24.top() - drop24.bottom() >= 19);
   }
   // D14: a V of a chord is spelled a fifth over it: F#9 into B7 in Bb, B9 into E7b9 in Eb minor.
   {
      const Chord b7 = chord("B7"), e7b9 = chord("E7b9");
      JCLAP_CHECK(chordName(chord("Gb9"), 10, kMajor, &b7) == "F#9");
      JCLAP_CHECK(chordName(chord("B9"), 3, kMinor, &e7b9) == "B9");
      const Chord g = chord("G"), cm = chord("Cm");
      JCLAP_CHECK(chordName(chord("D7"), 0, kMajor, &g) == "D7");
      JCLAP_CHECK(chordName(chord("G7b9"), 0, kMinor, &cm) == "G7b9");
   }
   // D15: ROOTLESS at five voices gives a 7#11 its 13th, not the root's whole-tone cluster; a run off
   // an augmented chord walks its whole-tone scale.
   {
      VoiceSettings vs;
      vs.shape = kRootless;
      vs.voices = 5;
      const Voicing v = voice(chord("C7#11"), nullptr, vs);
      std::set<int> pcs;
      for (int i = 0; i < v.count; ++i)
         pcs.insert(v.notes[static_cast<size_t>(i)] % 12);
      JCLAP_CHECK(v.count == 5 && pcs.count(9) == 1 && pcs.count(0) == 0);
      for (int n : passingRun(64, 69, 3, 0, kMajor, false, chord("F"), chord("Caug")))
         JCLAP_CHECK(n % 12 != 5);
   }
   // A01, C01: a hit's own voicing (picks 1-4) is another voicing close by: never the best an octave
   // away, the same count, its top and bottom within a fifth of the best's.
   for (int shape : {kClose, kDrop2, kSpread, kQuartal, kRootless})
      for (const char *name : {"Eadd9", "Amaj9", "B7sus", "Cm7", "G13"}) {
         VoiceSettings vs;
         vs.shape = shape;
         vs.voices = 5;
         vs.lo = 43;
         vs.hi = 86;
         vs.center = 64;
         const Chord c = chord(name);
         const Voicing best = voice(c, nullptr, vs, 0);
         for (int pick = 1; pick <= 4; ++pick) {
            const Voicing v = voice(c, nullptr, vs, pick);
            bool octave = v.count == best.count && !(v == best);
            for (int i = 0; i < v.count && octave; ++i)
               octave = v.notes[static_cast<size_t>(i)] - best.notes[static_cast<size_t>(i)] == v.notes[0] - best.notes[0] &&
                        (v.notes[0] - best.notes[0]) % 12 == 0;
            JCLAP_CHECK(!octave);
            JCLAP_CHECK(v.count == best.count);
            JCLAP_CHECK(std::abs(v.top() - best.top()) <= 7 && std::abs(v.bottom() - best.bottom()) <= 7);
         }
      }
}

// Round fifteen (C): TRY INSTEAD ranked to the key and the next chord, takes 1-3 heard apart in
// every style, fixed figures varied by phrasing, backbeats kept, the left hand stopping at an
// ending's last chord, and bred litters that don't repeat themselves.
JCOMP_TEST_NOINLINE static void roundFifteenC() {
   const auto genreNamed = [](const char *name) {
      for (int i = 0; i < genreCount(); ++i)
         if (std::string(genreAt(i).name) == name)
            return i;
      return -1;
   };
   const auto styleNamed = [](int g, const char *name) {
      const Genre &gen = genreAt(g);
      for (size_t i = 0; i < gen.styles.size(); ++i)
         if (std::string(gen.styles[i].name) == name)
            return static_cast<int>(i);
      return -1;
   };
   const auto made = [](const Recipe &r) {
      auto s = std::make_unique<Song>();
      generateSong(r, *s);
      return s;
   };
   // ---- TRY INSTEAD: a V7 into a minor offers its flat nine first; nothing out of the key ----
   {
      const Chord g7{7, kDom7, -1}, cm{0, kMin, -1};
      const auto alt = tryInstead(g7, cm, 0, kMinor);
      bool flat9 = false;
      for (size_t i = 0; i < alt.size() && i < 4; ++i)
         flat9 = flat9 || (alt[i].first.root == 7 && alt[i].first.quality == kFlat9);
      JCLAP_CHECK(flat9);
      // C Mixolydian has no B: no Em7 (iii for I) or Cmaj7#11 for its C.
      for (const auto &[c, why] : tryInstead(Chord{0, kMaj, -1}, Chord{10, kMaj, -1}, 0, kMixolydian)) {
         JCLAP_CHECK(!(c.root == 4 && c.quality == kMin7));
         JCLAP_CHECK(!(c.root == 0 && (c.quality == kMaj7 || c.quality == kMaj7Sharp11)));
      }
      // E minor in C major: no Em6 (C# isn't in the key).
      for (const auto &[c, why] : tryInstead(Chord{4, kMin, -1}, Chord{5, kMaj, -1}, 0, kMajor))
         JCLAP_CHECK(!(c.root == 4 && c.quality == kMin6));
   }
   // ---- the player each style asks for ----
   {
      const int g = genreNamed("Latin Jazz"), st = styleNamed(g, "Cha-Cha");
      JCLAP_CHECK(g >= 0 && st >= 0);
      JCLAP_CHECK(styleOf(recipeOf(g, st, 1)).player == kMontuno);
   }
   // ---- takes 1-3 are heard apart from take 0 and from each other in every style ----
   {
      int weak = 0, sections = 0;
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            for (uint32_t seed = 1; seed <= 3; ++seed) {
               auto song = made(recipeOf(g, st, seed));
               const bool tenorLine = std::string(genreAt(g).name) == "Afrobeat" &&
                                      std::string(genreAt(g).styles[st].name) == "Tenor Line";
               // (A fixed figure varies only by lean and voicing, and two styles have little room: Quarter
               // Pound's quarters can only lean on beat 3, Quiet Bossa has four strokes. Their takes still
               // differ, as checked above, but needn't differ twice over.)
               const std::string stName = genreAt(g).styles[st].name;
               const bool roomless = (styleOf(recipeOf(g, st, seed)).mods & kFixed) || stName == "Quarter Pound" ||
                                     stName == "Quiet Bossa";
               for (const Section &sec : song->sections) {
                  ++sections;
                  int strong = 0;
                  for (int t = 1; t < kTakes; ++t) {
                     JCLAP_CHECK(heardChange(sec.takes[0], sec.takes[t]) >= 1);
                     strong += heardChange(sec.takes[0], sec.takes[t]) >= 2;
                     for (int u = 1; u < t; ++u)
                        JCLAP_CHECK(!playedSame(sec.takes[t], sec.takes[u]));
                  }
                  // A tenor line plays without variation, so only its levels move.
                  if (strong < 2 && !tenorLine && !roomless)
                     ++weak;
               }
            }
      JCLAP_CHECK_EQ(weak, 0);
      (void)sections;
   }
   // ---- a backbeat take keeps its backbeat the loudest stroke in every bar ----
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
         for (uint32_t seed = 1; seed <= 3; ++seed) {
            auto song = made(recipeOf(g, st, seed));
            for (const Section &sec : song->sections) {
               const Pattern &p = sec.takes[0];
               if (p.rate != kRate16)
                  continue;
               bool bb = false, only = true;
               for (int s = 0; s < p.steps; ++s)
                  if (accentAt(p, s)) {
                     const bool back = s % 16 == 4 || s % 16 == 12;
                     bb = bb || back;
                     only = only && back;
                  }
               if (!bb || !only)
                  continue;
               for (int t = 1; t < kTakes; ++t) {
                  const Pattern &q = sec.takes[t];
                  for (int b = 0; b < q.steps / 16; ++b) {
                     int peak = 0;
                     bool onBack = false;
                     for (int s = b * 16; s < b * 16 + 16; ++s)
                        if (q.at(s).on() && !leftHand(q.at(s).part))
                           peak = std::max<int>(peak, q.at(s).vel);
                     for (int s = b * 16; s < b * 16 + 16; ++s)
                        if (q.at(s).on() && !leftHand(q.at(s).part) && q.at(s).vel == peak &&
                            (s % 16 == 4 || s % 16 == 12))
                           onBack = true;
                     JCLAP_CHECK(onBack);
                  }
               }
            }
         }
   // ---- an ending's left hand stops where its last chord sounds ----
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         auto song = made(recipeOf(g, st, 1));
         const Pattern &end = song->sections[kOutro].ending;
         int last = -1;
         for (int s = end.steps - 1; s >= 0 && last < 0; --s)
            if (end.at(s).on() && !leftHand(end.at(s).part))
               last = s;
         for (int s = 0; s < last; ++s)
            if (end.at(s).on() && leftHand(end.at(s).part)) {
               JCLAP_CHECK(s + end.at(s).len <= last);
               JCLAP_CHECK(end.at(s).art != kArtRing && end.at(s).art != kArtTopRings);
            }
      }
   // ---- a fixed figure's ending carries the figure into its last bar ----
   {
      const int g = genreNamed("Salsa"), st = 0;
      auto song = made(recipeOf(g, st, 1));
      const Pattern &end = song->sections[kOutro].ending;
      const Pattern &take = song->sections[kOutro].takes[0];
      const int bar = take.stepsPerBar();
      // The first bar of the ending plays the take's own figure (its last bar's strokes).
      int same = 0, strokes = 0;
      const int from = ((take.steps / bar - 2) % (take.steps / bar) + take.steps / bar) % (take.steps / bar) * bar;
      for (int s = 0; s < bar; ++s)
         if (take.at(from + s).on() && chordPart(take.at(from + s).part)) {
            ++strokes;
            same += end.at(s).on();
         }
      JCLAP_CHECK(strokes > 0 && same * 4 >= strokes * 3);
   }
   // ---- a bred litter never repeats a take or itself ----
   for (int g = 0; g < genreCount(); g += 3)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Recipe r = recipeOf(g, st, 2);
         auto song = made(r);
         const Style &sy = styleOf(r);
         for (const Section &sec : song->sections) {
            Rng rng(sec.seed + 9);
            std::vector<std::string> tags;
            const auto litter = breedLitter(sec, 4, 0.5, rng, tags, sy.mods, genreAt(g).name, false, sy.bpm);
            for (size_t i = 0; i < litter.size(); ++i) {
               if (tags[i].empty())
                  continue;
               for (int t = 0; t < kTakes; ++t)
                  JCLAP_CHECK(!playedSame(litter[i], sec.takes[t]));
               for (size_t j = 0; j < i; ++j)
                  if (!tags[j].empty())
                     JCLAP_CHECK(!playedSame(litter[i], litter[j]));
            }
         }
      }
   // ---- a mutated fill keeps the rest its parent left before the lead ----
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Recipe r = recipeOf(g, st, 1);
         auto song = made(r);
         const Style &sy = styleOf(r);
         const Pattern &fill = song->sections[kVerse].fill;
         if (fill.empty())
            continue;
         for (uint64_t k = 0; k < 10; ++k) {
            Rng rng(300 + k);
            std::string tag;
            const Pattern m = mutate(fill, 0.6, rng, tag, sy.mods, genreAt(g).name, false, sy.bpm, true, true, kMutateFill);
            for (int s = 0; s < fill.steps; ++s)
               if (!fill.at(s).on() && fillInKeepsOff(fill, s, true))
                  JCLAP_CHECK(!(m.at(s).on() && chordPart(m.at(s).part)));
         }
      }
}

// Round fifteen: the engine and roll findings (reviews B, E, C02, C03, C12, C15).
JCOMP_TEST_NOINLINE static void roundFifteenE() {
   const double perBlock = kBlock / 24000.0;
   const auto block = [](Engine &e, OutBuffer &out, double beats, bool playing, double tempo = 120.0) {
      Block b;
      b.frames = kBlock;
      b.tempo = tempo;
      b.hasTransport = true;
      b.playing = playing;
      b.beats = beats;
      out.clear();
      e.process(b, out);
   };
   struct Note {
      double on, off;
      int key, ch;
   };
   // Every note of a run (on and off in beats), at `tempo`.
   struct Notes {
      std::vector<Note> all;
      std::map<std::pair<int, int>, size_t> open;
      void add(const OutBuffer &out, double beats, double tempo = 120.0) {
         for (size_t i = 0; i < out.count; ++i) {
            const MidiOut &m = out.items[i];
            const double at = beats + m.time / 48000.0 * tempo / 60.0;
            const int ch = m.status & 15;
            const auto k = std::make_pair(ch, static_cast<int>(m.data1));
            if ((m.status & 0xF0) == 0x90 && m.data2 > 0) {
               open[k] = all.size();
               all.push_back({at, -1.0, m.data1, ch});
            } else if (open.count(k)) {
               all[open[k]].off = at;
               open.erase(k);
            }
         }
      }
      std::set<int> pcs(double from, double to) const {
         std::set<int> s;
         for (const Note &n : all)
            if (n.on >= from - 1e-6 && n.on < to)
               s.insert(n.key % 12);
         return s;
      }
      int onsIn(double from, double to) const {
         int c = 0;
         for (const Note &n : all)
            c += n.on >= from - 1e-6 && n.on < to;
         return c;
      }
   };
   const auto engineFor = [](const Song &song, const Settings &s) {
      auto e = std::make_unique<Engine>();
      e->setSong(song);
      e->setSettings(s);
      e->reset();
      return e;
   };
   // One chord a section, for all its bars: a section switch is heard as a change of chord.
   const auto sectionsOf = [](const char *rhythm, std::vector<const char *> chords, int bars) {
      auto r = std::make_unique<Song>();
      r->key = 0;
      for (int i = 0; i < kSections; ++i) {
         Section &sec = r->sections[static_cast<size_t>(i)];
         for (Pattern &t : sec.takes)
            t = fromRhythm(rhythm);
         sec.fill = fromRhythm(rhythm);
         sec.fillDown = Pattern{};
         sec.ending = Pattern{};
         sec.progBars = 1;
         sec.bars = static_cast<uint8_t>(bars);
         sec.split = 0;
         sec.chords[0] = sec.chords[1] = chord(chords[static_cast<size_t>(i) % chords.size()]);
      }
      return r;
   };
   const auto styleIndex = [](const char *genre, const char *style, int &gi, int &si) {
      for (gi = 0; gi < genreCount(); ++gi)
         if (std::string(genreAt(gi).name) == genre)
            for (si = 0; si < static_cast<int>(genreAt(gi).styles.size()); ++si)
               if (std::string(genreAt(gi).styles[static_cast<size_t>(si)].name) == style)
                  return true;
      return false;
   };
   const auto styleSettings = [](int gi, int si, uint32_t seed, Song &song) {
      std::vector<double> v = defaultValues();
      for (const auto &[id, x] : styleValues(gi, si))
         v[id] = x;
      v[kSeed] = seed;
      const Recipe r = recipeFrom([&](clap_id id) { return v[id]; });
      generateSong(r, song);
      Settings s = plain();
      s.voicing = voiceSettings([&](clap_id id) { return v[id]; });
      s.key = song.key;
      s.sync = 1;
      applyRecipe(s, r);
      return s;
   };
   const std::set<int> cMajor{0, 4, 7};

   // B01: a take with a line strikes the same downbeats on every pass of a loop, an AUTO FILL's chord
   // before or not (Right-Hand Arpeggio: the bass on 1 brings the change).
   {
      int gi = 0, si = 0;
      JCLAP_CHECK(styleIndex("Piano Ballad", "Right-Hand Arpeggio", gi, si));
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(gi, si, 1, *song);
      s.section = kVerse;
      s.autoFillBars = 8;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      const int bars = song->sections[kVerse].bars, bb = song->beats == 3 ? 3 : 4;
      for (double b = 0.0; b < bars * bb * 2; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      for (int bar = 0; bar < bars; ++bar)
         JCLAP_CHECK(n.pcs(bar * bb, bar * bb + 0.05) == n.pcs((bar + bars) * bb, (bar + bars) * bb + 0.05));
   }
   // B02: AUTO FILLS fills a section shorter than its setting by the host's bars: a 4-bar section
   // looped with Every 8 fills bars 8 and 16, an 8-bar one with Every 16 its bar 16.
   for (const auto &[bars, every] : {std::pair<int, int>{4, 8}, {8, 16}}) {
      auto song = sectionsOf("x...x...x...x...", {"C", "F", "G", "Bb", "Dm", "Em", "Eb"}, bars);
      for (Section &sec : song->sections)
         sec.fill = fromRhythm("x.x.x.x.x.x.x.x.");
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      s.autoFillBars = every;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 4.0 * 32; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      for (int bar = 0; bar < 32; ++bar) {
         const bool fill = n.onsIn(bar * 4, bar * 4 + 4) > 6 * 3;
         JCLAP_CHECK_EQ(fill, (bar + 1) % every == 0);
      }
   }
   // B03, B04: in SONG a tile for the entry heard, clicked in its last moment, changes nothing; a
   // late RESTART or PREV acts as it does a moment earlier, with nothing of the next entry first.
   {
      const auto play = [&](int action, double at) {
         auto song = sectionsOf("x...x...x...x...", {"C", "F", "G", "Am", "Dm", "Em", "Bb"}, 2);
         song->setForm(1);
         Settings s = plain();
         s.sync = 1;
         s.song = true;
         s.songEnds = true;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         bool done = false;
         for (double b = 0.0; b < 40.0; b += perBlock) {
            if (action >= 0 && !done && b >= at) {
               e->request(action);
               done = true;
            }
            block(*e, *out, b, true);
            n.add(*out, b);
         }
         std::vector<std::set<int>> bars;
         for (int bar = 0; bar < 10; ++bar)
            bars.push_back(n.pcs(bar * 4, bar * 4 + 0.1));
         return bars;
      };
      JCLAP_CHECK(play(kActIntro, 7.95) == play(-1, 0.0));
      JCLAP_CHECK(play(kActRestart, 7.95) == play(kActRestart, 7.5));
      JCLAP_CHECK(play(kActPrev, 15.95) == play(kActPrev, 15.5));
      const auto again = play(kActRestart, 7.95);
      for (int pc : again[2])
         JCLAP_CHECK(cMajor.count(pc) == 1); // the intro's C again at beat 8
   }
   // B05: with CHORDS FROM BOTH and LATCH, a chord tapped before a cue is let go at the switch.
   for (int songMode = 0; songMode < 2; ++songMode) {
      auto song = sectionsOf("x...x...x...x...", {"C", "F", "G", "Bb", "Dm", "Em", "Eb"}, 8);
      song->setForm(1);
      Settings s = plain();
      s.sync = 1;
      s.song = songMode;
      s.chordsFrom = 2;
      s.latch = true;
      s.switchAt = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      int st = 0;
      for (double b = 0.0; b < 24.0; b += perBlock) {
         if (st == 0 && b >= 1.9) {
            for (int k : {69, 72, 76})
               e->noteOn(0, k, 100, 0);
            st = 1;
         } else if (st == 1 && b >= 2.2) {
            for (int k : {69, 72, 76})
               e->noteOff(0, k, 0);
            st = 2;
         } else if (st == 2 && b >= 5.0) {
            e->request(songMode ? kActNext : kActVerse);
            st = 3;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.pcs(8.0, 8.1).count(5) == 1);  // F's at the switch
      JCLAP_CHECK(n.pcs(8.0, 16.0).count(4) == 0); // and no tapped Am's E after it
   }
   // B06: PAD LAYER voice-leads afresh at every start: two renders (a stop between, then a reset)
   // give the same pad.
   {
      auto song = sectionsOf("x...............", {"E", "A", "B", "C#m", "E", "A", "B"}, 2);
      for (Section &sec : song->sections)
         sec.chords[1] = chord("A");
      Settings s = plain();
      s.sync = 1;
      s.pad = true;
      s.padChannel = 2;
      s.section = kVerse;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      std::vector<std::set<int>> first;
      for (int render = 0; render < 3; ++render) {
         if (render == 2)
            e->reset();
         std::set<int> pad;
         for (double b = 0.0; b < 14.0; b += perBlock) {
            block(*e, *out, b, true);
            for (size_t i = 0; i < out->count; ++i)
               if (out->items[i].status == 0x92 && out->items[i].data2 > 0 && b < 0.5)
                  pad.insert(out->items[i].data1);
         }
         block(*e, *out, 14.0, false);
         first.push_back(pad);
      }
      JCLAP_CHECK(!first[0].empty());
      JCLAP_CHECK(first[0] == first[1]);
      JCLAP_CHECK(first[0] == first[2]);
   }
   // B07: a 3/4 project under a 4/4 style: once a playback has shown its every bar is 3/4, the next
   // from the top counts from beat 0 (C at 0, G at 4), no pickup rested.
   {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}, {"G", nullptr}});
      Settings s = plain();
      s.sync = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (int pass = 0; pass < 2; ++pass) {
         for (double b = 0.0; b < 12.0; b += perBlock) {
            Block k;
            k.frames = kBlock;
            k.tempo = 120.0;
            k.hasTransport = k.playing = true;
            k.beats = b;
            k.hasBar = true;
            k.barBeats = 3.0;
            k.barStart = std::floor(b / 3.0) * 3.0;
            out->clear();
            e->process(k, *out);
            if (pass == 1)
               n.add(*out, b);
         }
         block(*e, *out, 12.0, false);
      }
      JCLAP_CHECK(n.pcs(0.0, 0.5) == cMajor);
      JCLAP_CHECK(n.pcs(3.9, 4.5) == (std::set<int>{7, 11, 2}));
   }
   // B08: SAME DICE 1 bar and VARY: the take swaps are thrown a bar of the phrase at a time, so no
   // section swaps on most of its bars.
   {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}});
      for (Section &sec : song->sections)
         sec.takes[1] = fromRhythm("x.x.x.x.x.x.x.x.");
      int worst = 0;
      for (uint32_t seed = 1; seed <= 200; ++seed) {
         for (Section &sec : song->sections)
            sec.seed = seed;
         Settings s = plain();
         s.sync = 1;
         s.vary = 0.2;
         s.sameDiceBars = 1;
         s.take = 1;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         int swapped = 0;
         for (double b = 0.0; b < 32.0; b += perBlock) {
            block(*e, *out, b, true);
            if (std::fmod(b, 4.0) >= 2.0 && std::fmod(b, 4.0) < 2.0 + perBlock) {
               Frame f;
               e->fillFrame(f);
               swapped += f.take != 1;
            }
         }
         worst = std::max(worst, swapped);
      }
      JCLAP_CHECK(worst <= 4);
   }
   // B09: HOLD CHORD clicked off and on again before its let-go's bar line stays held, and shows
   // let go while it waits.
   {
      auto song = sectionsOf("x...x...x...x...", {"C", "F", "G", "Am", "Dm", "Em", "Bb"}, 2);
      song->setForm(1);
      Settings s = plain();
      s.sync = 1;
      s.song = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      for (double b = 0.0; b < 21.5; b += perBlock) {
         if (b >= 5.0 && b < 5.0 + perBlock)
            e->toggleHeld(kActHoldChord);
         if (b >= 21.0 && b < 21.0 + perBlock)
            e->toggleHeld(kActHoldChord);
         block(*e, *out, b, true);
      }
      Frame f;
      e->fillFrame(f);
      JCLAP_CHECK(!f.hold);
      e->toggleHeld(kActHoldChord);
      e->fillFrame(f);
      JCLAP_CHECK(f.hold);
      for (double b = 21.5; b < 30.0; b += perBlock)
         block(*e, *out, b, true);
      e->fillFrame(f);
      JCLAP_CHECK(f.hold);
   }
   // B10: HOLD CHORD let go running free in SONG a moment before the held pass's end: the next
   // entry comes in at that end, as when let go earlier in the pass.
   {
      const auto play = [&](double release) {
         auto song = sectionsOf("x...x...x...x...", {"C", "F", "G", "Am", "Dm", "Em", "Bb"}, 2);
         song->setForm(1);
         Settings s = plain();
         s.sync = 2;
         s.play = true;
         s.song = true;
         s.freeTempo = 120;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         int st = 0;
         double beat = 0.0;
         for (int k = 0; k < static_cast<int>(40.0 / perBlock); ++k, beat += perBlock) {
            if (st == 0 && beat >= 9.0) {
               e->toggleHeld(kActHoldChord);
               st = 1;
            }
            if (st == 1 && beat >= release) {
               e->toggleHeld(kActHoldChord);
               st = 2;
            }
            Block b;
            b.frames = kBlock;
            out->clear();
            e->process(b, *out);
            n.add(*out, beat);
         }
         return n.pcs(24.0, 27.9);
      };
      JCLAP_CHECK(play(23.95) == play(21.0));
   }
   // B11: a two-bar clave figure in odd-length SONG entries: NEXT in an entry's last bar doesn't
   // make it play an extra bar, and the next entry comes in whole.
   {
      const auto play = [&](bool next) {
         auto song = sectionsOf("x...x.x...x...x.|..x...x...x...x.", {"C", "F", "G", "Am", "Dm", "Em", "Bb"}, 3);
         song->setForm(1);
         Settings s = plain();
         s.sync = 1;
         s.song = true;
         s.fixed = true;
         s.switchAt = 1;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         bool done = false;
         for (double b = 0.0; b < 52.0; b += perBlock) {
            if (next && !done && b >= 33.0) {
               e->request(kActNext);
               done = true;
            }
            block(*e, *out, b, true);
            n.add(*out, b);
         }
         std::vector<std::set<int>> bars;
         for (int bar = 0; bar < 13; ++bar)
            bars.push_back(n.pcs(bar * 4, bar * 4 + 4));
         return bars;
      };
      JCLAP_CHECK(play(true) == play(false));
   }
   // B12: Sync AUTO with PLAY on waits through the host's pre-roll, as following does.
   {
      auto song = sectionsOf("x...x...x...x...", {"C", "F", "G", "Am", "Dm", "Em", "Bb"}, 2);
      Settings s = plain();
      s.sync = 0;
      s.play = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      int ons = 0;
      for (double b = 24.0; b < 32.0; b += perBlock) {
         Block k;
         k.frames = kBlock;
         k.tempo = 120.0;
         k.hasTransport = true;
         k.playing = false;
         k.preRoll = true;
         k.beats = b;
         out->clear();
         e->process(k, *out);
         for (size_t i = 0; i < out->count; ++i)
            ons += (out->items[i].status & 0xF0) == 0x90 && out->items[i].data2 > 0;
      }
      JCLAP_CHECK_EQ(ons, 0);
   }
   // B13: a start inside a held style's long chord strikes it there, not seconds later.
   {
      int gi = 0, si = 0;
      JCLAP_CHECK(styleIndex("Ambient", "Mirror Drift", gi, si));
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(gi, si, 3, *song);
      song->setForm(3);
      s.song = true;
      s.songEnds = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 50.0; b < 56.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(50.0, 50.5) > 0);
   }
   // E01 (C02): below LENGTH's middle a tied push's length is decided as it's struck: the same in
   // beats at every tempo, shorter as LENGTH falls, and its beat stays unstruck.
   {
      auto song = songOf("x.....x.x.......", {{"C", nullptr}});
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes)
            t.at(6).push = 1;
      double prevLen = 0.0;
      for (double length : {0.05, 0.2, 0.3, 0.45}) {
         std::vector<double> lens;
         for (double tempo : {80.0, 120.0, 200.0}) {
            Settings s = plain();
            s.sync = 1;
            s.length = length;
            auto e = engineFor(*song, s);
            auto out = std::make_unique<OutBuffer>();
            Notes n;
            const double per = kBlock / 48000.0 * tempo / 60.0;
            for (double b = 0.0; b < 4.0; b += per) {
               block(*e, *out, b, true, tempo);
               n.add(*out, b, tempo);
            }
            double len = -1.0;
            for (const Note &x : n.all)
               if (x.on > 1.4 && x.on < 1.6 && x.off > 0)
                  len = std::max(len, x.off - x.on);
            JCLAP_CHECK(len > 0.0);
            JCLAP_CHECK_EQ(n.onsIn(1.9, 2.1), 0);
            lens.push_back(len);
         }
         for (double l : lens)
            JCLAP_CHECK(std::abs(l - lens[0]) < 0.004);
         JCLAP_CHECK(lens[0] > prevLen);
         prevLen = lens[0];
      }
   }
   // E07: RHYTHM FROM BOTH with FOLLOW every note: a bass note let go on the beat doesn't cut the
   // take's chord struck there a moment early to nothing.
   {
      auto song = songOf("x...x...x...x...", {{"G", nullptr}});
      Settings s = plain();
      s.sync = 1;
      s.rhythmFrom = 2;
      s.follow = 0;
      s.onsets = 0;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      const int bassCh = s.bassChannel;
      const double perFrame = 2.0 / 48000.0;
      int next = 1;
      for (double b = 0.0; b < 8.0; b += perBlock) {
         if (b + perBlock > next && next < 8) {
            const uint32_t frame = static_cast<uint32_t>(std::lround((next - b) / perFrame));
            e->noteOff(bassCh, 43, frame);
            e->noteOn(bassCh, 43, 100, frame);
            ++next;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      for (const Note &x : n.all)
         if (x.off >= 0)
            JCLAP_CHECK(x.off - x.on > 0.01);
   }
   // E12: PAD LAYER on the comp's own channel: the pad's keys go on sounding past the comp's stabs
   // on them, to the change.
   {
      auto song = songOf("x.......x.......", {{"G", nullptr}});
      Settings s = plain();
      s.sync = 1;
      s.pad = true;
      s.padChannel = s.channel;
      s.length = 0.2;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 3.9; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.open.size() >= 4);
   }
   // C03: a song in a left-hand style ends on the left hand's root under its last chord, nothing of
   // the left hand from before ringing under it.
   // (At LENGTH above the middle too.)
   for (const auto &[genre, style] : {std::pair<const char *, const char *>{"Highlife", "Palm-Wine"}, {"Gospel", "Shout"},
                                      {"Reggae", "Organ Bubble"}})
   for (double length : {0.5, 0.9}) {
      int gi = 0, si = 0;
      JCLAP_CHECK(styleIndex(genre, style, gi, si));
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(gi, si, 1, *song);
      s.length = length;
      song->setForm(0);
      s.song = true;
      s.songEnds = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      Chord last;
      for (double b = 0.0; b < 600.0 && !e->ended(); b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
         Frame f;
         e->fillFrame(f);
         if (out->count && !f.chord.none())
            last = f.chord;
      }
      JCLAP_CHECK(e->ended());
      double final = -1.0;
      for (const Note &x : n.all)
         final = std::max(final, x.on);
      int lowest = 128;
      for (const Note &x : n.all)
         if (x.on > final - 0.05)
            lowest = std::min(lowest, x.key);
      const int bass = last.bass >= 0 ? last.bass : last.root;
      JCLAP_CHECK_EQ(lowest % 12, ((bass % 12) + 12) % 12);
      for (const Note &x : n.all)
         if (x.on < final - 0.05)
            JCLAP_CHECK(x.off >= 0 && x.off <= final + 0.01);
   }
   // C12: a push tied over its beat takes the voicing the beat's own stroke asks for (that beat is
   // never struck).
   {
      const auto pushed = [&](int pick) {
         auto song = songOf("x.....x.x.......", {{"C", nullptr}, {"F", nullptr}});
         for (Section &sec : song->sections)
            for (Pattern &t : sec.takes) {
               t.at(6).push = 1;
               t.at(8).voicing = static_cast<int8_t>(pick);
            }
         Settings s = plain();
         s.sync = 1;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         for (double b = 0.0; b < 2.0; b += perBlock) {
            block(*e, *out, b, true);
            n.add(*out, b);
         }
         std::set<int> keys;
         for (const Note &x : n.all)
            if (x.on > 1.4 && x.on < 1.6)
               keys.insert(x.key);
         return keys;
      };
      JCLAP_CHECK(pushed(2) != pushed(-1));
   }
   // C15: an OCTAVES ending's last chord lands on the root.
   {
      int gi = 0, si = 0;
      JCLAP_CHECK(styleIndex("Salsa", "Montuno 3-2", gi, si));
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(gi, si, 1, *song);
      song->setForm(0);
      s.song = true;
      s.songEnds = true;
      JCLAP_CHECK_EQ(s.voicing.shape, kOctaves);
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      Chord last;
      for (double b = 0.0; b < 900.0 && !e->ended(); b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
         Frame f;
         e->fillFrame(f);
         if (out->count && !f.chord.none())
            last = f.chord;
      }
      double final = -1.0;
      for (const Note &x : n.all)
         final = std::max(final, x.on);
      int lowest = 128;
      for (const Note &x : n.all)
         if (x.on > final - 0.05)
            lowest = std::min(lowest, x.key);
      JCLAP_CHECK_EQ(lowest % 12, ((last.root % 12) + 12) % 12);
   }
}

// Round sixteen: the harmony findings (review D, A01, A03, C02).
JCOMP_TEST_NOINLINE static void roundSixteenD() {
   const auto fourthsIn = [](const Voicing &v) {
      int k = 0;
      for (int i = 1; i < v.count; ++i)
         k += v.notes[static_cast<size_t>(i)] - v.notes[static_cast<size_t>(i - 1)] == 5;
      return k;
   };
   const auto settings = [](int shape, int voices, int lo = 50, int hi = 76, int center = 62) {
      VoiceSettings vs;
      vs.shape = shape;
      vs.voices = voices;
      vs.lo = lo;
      vs.hi = hi;
      vs.center = center;
      vs.maxSpan = 17;
      vs.floor = std::min(43, lo);
      return vs;
   };
   // D01: 4THS plays the stacks with the most perfect fourths among those that keep the guide tones,
   // within one of the most a stack of its size has, afresh and after a ii-V-I's chords.
   for (const char *name : {"Cm7", "Cmaj7", "C7", "Cm9", "C6", "C7sus"})
      for (int r = 0; r < 12; ++r)
         for (int voices : {4, 5})
            for (int wide : {0, 1}) {
               const VoiceSettings vs = wide ? settings(kQuartal, voices, 43, 86, 64) : settings(kQuartal, voices);
               const Chord c = chord(name, r);
               const Voicing prev = voice(chord("Dm7", r), nullptr, vs);
               for (const Voicing *p : {static_cast<const Voicing *>(nullptr), &prev}) {
                  const Voicing v = voice(c, p, vs);
                  int most = 0;
                  for (const Voicing &x : rankShapes(c, p, vs))
                     if (x.count == v.count)
                        most = std::max(most, fourthsIn(x));
                  JCLAP_CHECK(fourthsIn(v) >= most - 1);
               }
            }
   {
      const Voicing v = voice(chord("Dm7"), nullptr, settings(kQuartal, 4));
      std::vector<int> pcs;
      for (int i = 0; i < v.count; ++i)
         pcs.push_back(v.notes[static_cast<size_t>(i)] % 12);
      JCLAP_CHECK(pcs == (std::vector<int>{2, 7, 0, 5}) || pcs == (std::vector<int>{4, 9, 2, 7})); // D G C F or E A D G
   }
   // D02, C02: a hit's own voicing never clashes more than the best, nor adds mud; picks 1 to
   // voicingChoices() are each a different voicing, and the rest go round them again.
   for (int shape = 0; shape < kShapeCount; ++shape)
      for (int voices = 3; voices <= 5; ++voices)
         for (int q = 0; q < qualityCount(); ++q)
            for (int r = 0; r < 12; r += 5) {
               Chord c;
               c.root = static_cast<int8_t>(r);
               c.quality = static_cast<uint8_t>(q);
               const VoiceSettings vs = settings(shape, voices);
               const Voicing best = voice(c, nullptr, vs);
               const int n = voicingChoices(c, nullptr, vs);
               JCLAP_CHECK(n >= 0 && n <= 4);
               std::vector<Voicing> seen;
               for (int pick = 1; pick <= 4; ++pick) {
                  const Voicing v = voice(c, nullptr, vs, pick);
                  JCLAP_CHECK(clashes(v, c) <= clashes(best, c));
                  const Breaks a = breaksOf(v, nullptr, vs), b = breaksOf(best, nullptr, vs);
                  JCLAP_CHECK(!(a.mud && !b.mud && shape != kGrip && shape != kShell) && !(a.rub && !b.rub));
                  if (pick <= n) {
                     JCLAP_CHECK(!(v == best) && std::find(seen.begin(), seen.end(), v) == seen.end());
                     seen.push_back(v);
                  } else
                     JCLAP_CHECK(n == 0 ? v == best : v == seen[static_cast<size_t>((pick - 1) % n)]);
               }
            }
   {
      // (Cmaj7's CLOSE neighbours all put B under C: no pick there. Its DROP 2 and the 7#9's rootless
      // forms pick only clean ones.)
      JCLAP_CHECK(voicingChoices(chord("Cmaj7"), nullptr, settings(kClose, 4)) == 0);
      for (int pick = 1; pick <= 4; ++pick) {
         JCLAP_CHECK(clashes(voice(chord("Cmaj7"), nullptr, settings(kDrop24, 4), pick), chord("Cmaj7")) == 0.0);
         JCLAP_CHECK(clashes(voice(chord("C7#9"), nullptr, settings(kRootless, 4), pick), chord("C7#9")) == 0.0);
      }
      JCLAP_CHECK(voicingChoices(chord("Dm7"), nullptr, settings(kDrop2, 4)) >= 2);
   }
   // D04: a slash triad or seventh over its 3rd, 5th or 7th keeps VOICES notes in CLOSE and the
   // drops, its bass at the bottom (a voice fewer only where the window is too narrow for them).
   for (const char *q : {"", "m", "7", "m7", "maj7"})
      for (int r = 0; r < 12; ++r)
         for (int iv : {3, 4, 7, 10, 11})
            for (int shape : {kClose, kDrop2, kDrop24})
               for (int voices = 3; voices <= 5; ++voices)
                  for (int narrow : {0, 1}) {
                     Chord c = chord((std::string("C") + q).c_str(), r);
                     const std::vector<int> &t = qualityOf(c).tones;
                     if (std::find(t.begin(), t.end(), iv) == t.end())
                        continue;
                     c.bass = static_cast<int8_t>((r + iv) % 12);
                     const Voicing v = voice(c, nullptr, narrow ? settings(shape, voices, 55, 76, 64) : settings(shape, voices));
                     JCLAP_CHECK(v.bottom() % 12 == c.bass);
                     // (At five in a window from G3, and CLOSE's F/A at five, a voice fewer.)
                     if (voices <= 4 && !(narrow && shape != kDrop2 && voices == 4))
                        JCLAP_CHECK(v.count == voices);
                  }
   {
      const Voicing v = voice(chord("F/C"), nullptr, settings(kDrop2, 4));
      // (The bass goes under the others rather than the top leaping: C3 A3 C4 F4.)
      JCLAP_CHECK(v.count == 4 && v.notes[0] == 48 && v.notes[3] % 12 == 5);
   }
   // D05: GRIP plays a slash chord on VOICES strings, its open shape where it has one (C/G 33201x,
   // G/B x20003, C/E 03201x, D/F# 2x0232), and a shape a hand can make in every key.
   {
      VoiceSettings vs = settings(kGrip, 5, 40, 76, 58);
      vs.floor = 40;
      for (const char *name : {"C/G", "G/B", "C/E", "D/F#"})
         for (int k = 0; k < 12; ++k) {
            const Chord c = chord(name, k);
            const Voicing v = voice(c, nullptr, vs);
            // (Five strings where a hand can make them: Bb/D's five, D3 F3 Bb3 D4 F4, is no grip.)
            JCLAP_CHECK(v.count >= 4 && v.bottom() % 12 == c.bass && gripFrets(v) >= 0.0);
            JCLAP_CHECK(k != 0 || (gripFrets(v) == 0.0 && v.count >= 5));
         }
      const Voicing v = voice(chord("C/G"), nullptr, vs);
      JCLAP_CHECK(v.notes[0] == 43 && v.notes[1] == 48 && v.notes[2] == 52);
   }
   // D06, A01, A03: OCTAVES' line over a seventh chord moves 5, 3, 1, 3 by a step or a third, its
   // fifth sounded, never the stroke before an octave away, in every key (the montuno's window).
   for (const char *name : {"C7", "D7", "G7", "A7", "Dm7", "F#m7", "E7b9", "A9"})
      for (int k = 0; k < 12; ++k) {
         VoiceSettings vs = settings(kOctaves, 4, 55, 84, 68);
         vs.key = k;
         const Chord c = chord(name, k), before = chord("Em7", k);
         Voicing prev = octaveLine(before, nullptr, vs, 0);
         bool fifth = false;
         for (int step = 0; step < 4; ++step) {
            const Voicing v = octaveLine(c, &prev, vs, step);
            const int move = std::abs(v.notes[0] - prev.notes[0]);
            JCLAP_CHECK(v.top() - v.bottom() == 12);
            if (step > 0)
               JCLAP_CHECK(move >= 1 && move <= 5);
            fifth = fifth || ((v.notes[0] - c.root) % 12 + 12) % 12 == (qualityOf(c).tones[2] % 12);
            prev = v;
         }
         JCLAP_CHECK(fifth);
      }
   {
      // A9 after Em7 in E, the cha-cha's vamp: its first stroke is the fifth, not the root a sixth
      // over the third that follows.
      VoiceSettings vs = settings(kOctaves, 4, 55, 84, 68);
      vs.key = 4;
      Voicing prev = octaveLine(chord("Em7"), nullptr, vs, 3);
      const Voicing first = octaveLine(chord("A9"), &prev, vs, 0);
      JCLAP_CHECK(first.notes[0] % 12 == 4);
      const Voicing second = octaveLine(chord("A9"), &first, vs, 1);
      JCLAP_CHECK(std::abs(second.top() - first.top()) <= 5);
   }
   // D07: SPREAD at five, a seventh over its third: five notes, the double in the middle.
   for (const char *name : {"D7/F#", "E7/G#", "A7/C#", "C7/E"}) {
      const Voicing v = voice(chord(name), nullptr, settings(kSpread, 5, 48, 79, 64));
      JCLAP_CHECK(v.count == 5 && v.notes[1] - v.notes[0] <= 16);
      std::set<int> pcs;
      for (int i = 0; i < v.count; ++i)
         pcs.insert(v.notes[static_cast<size_t>(i)] % 12);
      JCLAP_CHECK(pcs.size() >= 4);
      for (int i = 0; i + 1 < v.count; ++i)
         JCLAP_CHECK(v.notes[static_cast<size_t>(i)] % 12 != v.top() % 12);
   }
   // D08: the first-position sixths and Dm7 are open grips.
   for (int voices : {4, 5}) {
      VoiceSettings vs = settings(kGrip, voices, 40, 76, 58);
      vs.floor = 40;
      for (const char *name : {"Dm7", "C6", "D6", "Dm6"})
         JCLAP_CHECK(gripFrets(voice(chord(name), nullptr, vs)) == 0.0);
      if (voices == 4)
         JCLAP_CHECK(gripFrets(voice(chord("G6"), nullptr, vs)) == 0.0);
   }
   // D09: DROP 2 and DROP 2+4 play a diminished or augmented triad in four notes.
   for (int shape : {kDrop2, kDrop24})
      for (const char *name : {"Cdim", "Caug"})
         for (int r = 0; r < 12; ++r)
            JCLAP_CHECK(voice(chord(name, r), nullptr, settings(shape, 4)).count == 4);
   // D10, D13: where a clean voicing is near, none that clashes (DROP 2+4 Dbmaj7, DROP 2 Ab13 at three).
   for (const char *name : {"Cmaj7", "C13", "Cm9"})
      for (int r = 0; r < 12; ++r) {
         const Chord c = chord(name, r);
         JCLAP_CHECK(clashes(voice(c, nullptr, settings(kDrop24, 4)), c) == 0.0);
         for (int shape : {kClose, kDrop2, kDrop24})
            JCLAP_CHECK(clashes(voice(c, nullptr, settings(shape, 3)), c) == 0.0);
      }
   // D12: 4THS at three gives an m9 no 13th where it can't hold its 9th (Am9: D G C, not G C F#).
   for (int r = 0; r < 12; ++r) {
      const Chord c = chord("Cm9", r);
      const Voicing v = voice(c, nullptr, settings(kQuartal, 3));
      for (int i = 0; i < v.count; ++i)
         JCLAP_CHECK((v.notes[static_cast<size_t>(i)] - c.root + 120) % 12 != 9);
   }
   // D14: CLOSE at five voices a minor triad in five notes in every key (its b3 doubled where the
   // root and fifth doubled are too few), so G to Em needn't leap.
   for (int r = 0; r < 12; ++r)
      JCLAP_CHECK(voice(chord("Cm", r), nullptr, settings(kClose, 5)).count == 5);
   {
      const VoiceSettings vs = settings(kClose, 5);
      const Voicing g = voice(chord("G"), nullptr, vs);
      const Voicing em = voice(chord("Em"), &g, vs, -1, nullptr);
      JCLAP_CHECK(std::abs(em.top() - g.top()) <= 5);
   }
   // D15: a guitar's alternate bass never goes under a slash bass.
   for (const char *name : {"B/D#", "C/E", "G/B", "D/F#", "C/G"})
      for (int floor : {40, 43})
         JCLAP_CHECK(bassNote(chord(name), floor, true, true) > bassNote(chord(name), floor));
   // D16: a bII7 is spelled on the letter over its target's as written (B major's Gb9 into its bV, F;
   // C# minor's Db9 into C).
   {
      const Chord f = chord("F"), cc = chord("C");
      JCLAP_CHECK(chordName(chord("F#9"), 11, kMajor, &f) == "Gb9");
      JCLAP_CHECK(chordName(chord("C#9"), 1, kMinor, &cc) == "Db9");
      const Chord c2 = chord("C");
      JCLAP_CHECK(chordName(chord("Db9"), 0, kMajor, &c2) == "Db9");
   }
}

// Round sixteen: the styles, takes and mutation findings (reviews A, C, D03, D11).
JCOMP_TEST_NOINLINE static void roundSixteenC() {
   // A style by its name, in whichever genre has it.
   const auto styleCalled = [](const char *name, int &g, int &st) {
      for (g = 0; g < genreCount(); ++g)
         for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            if (std::string(genreAt(g).styles[st].name) == name)
               return true;
      return false;
   };
   const auto made = [](const Recipe &r) {
      auto s = std::make_unique<Song>();
      generateSong(r, *s);
      return s;
   };
   // A02: a chorus never draws its style's sparsest rhythm, nor one quieter than its verse's.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         if (style.mods & kInterlock)
            continue;
         const auto list = rhythmsFor(genreAt(g), style);
         double lo = 9, hi = -9;
         for (const Rhythm *rh : list) {
            lo = std::min(lo, rh->energy);
            hi = std::max(hi, rh->energy);
         }
         for (uint32_t seed = 1; seed <= 8; ++seed) {
            const Recipe r = recipeOf(g, st, seed);
            const double verse = sectionRhythmEnergy(r, kVerse, mix(r.seed, kVerse));
            Rng rng(mix(mix(r.seed, kChorus), static_cast<uint32_t>(kChorus) * 977u + 13u));
            Section s;
            makeChords(r, kChorus, s, rng);
            const Rhythm *rh = sectionRhythm(r, kChorus, rng, verse);
            if (!rh || hi <= lo)
               continue;
            JCLAP_CHECK(rh->energy > lo);
            JCLAP_CHECK(rh->energy >= verse - 1e-6);
         }
      }
   // A04: a quiet calypso keeps its and of 2 (3+3+2), not the four.
   {
      int g, st;
      JCLAP_CHECK(styleCalled("Calypso Strum", g, st));
      const auto list = rhythmsFor(genreAt(g), genreAt(g).styles[st]);
      const Rhythm *quiet = list.front();
      for (const Rhythm *rh : list)
         if (rh->energy < quiet->energy)
            quiet = rh;
      const Pattern p = fromRhythm(*quiet);
      JCLAP_CHECK(p.at(0).on() && p.at(6).on() && !p.at(12).on());
   }
   // A07: a blend never writes into a phrase's rest.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         auto song = made(recipeOf(g, st, 3));
         const Pattern &a = song->sections[kVerse].takes[0], &b = song->sections[kChorus].takes[0];
         if (a.steps != b.steps || a.rate != b.rate)
            continue;
         for (uint32_t k = 0; k < 4; ++k) {
            Pattern p = a;
            Rng rng(k + 1);
            blendInto(p, b, 1.0, rng);
            for (int s = 0; s < p.steps; ++s)
               if (phraseRest(a, s) && !a.at(s).on())
                  JCLAP_CHECK(!p.at(s).on());
         }
      }
   // A08, C04: a fixed figure's children only lean or take another voicing; a broken chord's turn
   // never makes a repeated note.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         for (uint32_t seed = 1; seed <= 3; ++seed) {
            auto song = made(recipeOf(g, st, seed));
            for (const Section &sec : song->sections) {
               Rng rng(seed * 7u);
               std::vector<std::string> tags;
               const auto litter = breedLitter(sec, 6, 0.7, rng, tags, style.mods, genreAt(g).name);
               for (size_t i = 0; i < litter.size(); ++i) {
                  if (style.mods & kFixed)
                     JCLAP_CHECK(tags[i].find("lean") != std::string::npos || tags[i].find("voicing") != std::string::npos ||
                                 tags[i].empty());
                  if (tags[i] != "turn")
                     continue;
                  const Pattern &p = litter[i], &t0 = sec.takes[0];
                  // (No new repeat: neighbouring line notes on the same degree only where take 0 had them.)
                  int was = 0, now = 0, prev = -1, prevWas = -1;
                  for (int s = 0; s < p.steps; ++s) {
                     if (t0.at(s).on() && t0.at(s).part == kPartLine) {
                        was += prevWas == t0.at(s).degree;
                        prevWas = t0.at(s).degree;
                     }
                     if (p.at(s).on() && p.at(s).part == kPartLine) {
                        now += prev == p.at(s).degree;
                        prev = p.at(s).degree;
                     }
                  }
                  JCLAP_CHECK(now <= was);
               }
            }
         }
      }
   // C01: a backbeat take's 2 and 4 are never lightened by a child, even struck alike.
   for (const char *name : {"Organ Bubble", "One Drop Skank", "Memphis Backbeat", "Boom-Chick", "Backbeat Piano"}) {
      int g, st;
      JCLAP_CHECK(styleCalled(name, g, st));
      const Style &style = genreAt(g).styles[st];
      for (uint32_t seed = 1; seed <= 8; ++seed) {
         auto song = made(recipeOf(g, st, seed));
         for (const Section &sec : song->sections) {
            const Pattern &t0 = sec.takes[0];
            if (!backbeatTake(t0))
               continue;
            Rng rng(seed);
            std::vector<std::string> tags;
            const auto litter = breedLitter(sec, 6, 0.7, rng, tags, style.mods, genreAt(g).name);
            for (const Pattern &p : litter)
               for (int s = 0; s < t0.steps; ++s)
                  if (backbeatStroke(t0, s) && p.at(s).on())
                     JCLAP_CHECK(p.at(s).vel >= t0.at(s).vel);
         }
      }
   }
   // C05: under a pad, a child's added stroke inside a held chord is quieter than its chord's onset.
   for (const char *name : {"Held Comp", "Slow Swells", "Long Tones", "Mirror Drift", "Held Like Strings", "Pad Intro"}) {
      int g, st;
      if (!styleCalled(name, g, st))
         continue;
      const Style &style = genreAt(g).styles[st];
      for (uint32_t seed = 1; seed <= 6; ++seed) {
         auto song = made(recipeOf(g, st, seed));
         for (const Section &sec : song->sections) {
            const Pattern &t0 = sec.takes[0];
            Rng rng(seed * 7u);
            std::vector<std::string> tags;
            for (const Pattern &p : breedLitter(sec, 6, 0.7, rng, tags, style.mods, genreAt(g).name))
               for (int s = 1; s < t0.steps; ++s) {
                  if (t0.at(s).on() || !heldOver(t0, s) || !p.at(s).on() || leftHand(p.at(s).part) || p.at(s).push)
                     continue;
                  int holder = s - 1;
                  while (holder > 0 && !(t0.at(holder).on() && chordPart(t0.at(holder).part)))
                     --holder;
                  JCLAP_CHECK(p.at(s).vel <= std::max(20, t0.at(holder).vel * 4 / 5));
               }
         }
      }
   }
   // C07: DENSITY thins from its first turn below the middle, for every style that thins at all.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         if (style.mods & kFixed)
            continue;
         int at0 = 0, at45 = 0, at50 = 0;
         for (uint32_t seed = 1; seed <= 3; ++seed) {
            auto song = made(recipeOf(g, st, seed));
            for (const Section &sec : song->sections) {
               const Pattern &p = sec.takes[0];
               for (uint32_t d = 0; d < 8; ++d) {
                  Rng dice(mix(seed, d));
                  for (int s = 0; s < p.steps; ++s) {
                     if (!p.at(s).on())
                        continue;
                     const double x = dice.uniform();
                     at0 += !thinnedBy(p, s, 0.0, x, false);
                     at45 += !thinnedBy(p, s, 0.45, x, false);
                     at50 += !thinnedBy(p, s, 0.5, x, false);
                  }
               }
            }
         }
         if (at0 < at50)
            JCLAP_CHECK(at45 < at50);
         // (And it thins by about a tenth of the way to 0 in that first tenth of the travel.)
         if (at0 < at50 && at50 - at0 >= 40)
            JCLAP_CHECK(at50 - at45 >= (at50 - at0) / 20);
      }
   // C10: a sparse player never empties an offbeat take's bar.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
         for (uint32_t seed = 1; seed <= 2; ++seed) {
            auto song = made(recipeOf(g, st, seed));
            for (const Section &sec : song->sections) {
               const Pattern &p = sec.takes[0];
               if (!offbeatTake(p))
                  continue;
               const int bar = p.stepsPerBar();
               for (int b = 0; b * bar < p.steps; ++b) {
                  bool any = false;
                  for (int s = b * bar; s < (b + 1) * bar && s < p.steps; ++s)
                     any = any || (p.at(s).on() && chordPart(p.at(s).part));
                  JCLAP_CHECK(any);
               }
            }
         }
   // C11: WEIRD's strays keep off a backbeat's empty downbeat and a phrase's rest.
   for (const char *name : {"Memphis Backbeat", "One Drop Skank", "Organ Bubble"}) {
      int g, st;
      JCLAP_CHECK(styleCalled(name, g, st));
      auto song = made(recipeOf(g, st, 2));
      for (const Section &sec : song->sections)
         for (uint32_t k = 1; k <= 16; ++k) {
            const Pattern w = sec.takes[0];
            Pattern p = w;
            Rng rng(k);
            applyWeird(p, 1.0, rng, genreAt(g).styles[st].mods);
            for (int s = 0; s < p.steps; ++s)
               if (!w.at(s).on() && (offBackbeat(w, s) || phraseRest(w, s)))
                  JCLAP_CHECK(!p.at(s).on());
         }
   }
   // C14: a backbeat's fill leads into the next section: its last chord, off the beat, is pushed.
   {
      int g, st;
      JCLAP_CHECK(styleCalled("Memphis Backbeat", g, st));
      int pushed = 0, offLast = 0;
      for (uint32_t seed = 1; seed <= 8; ++seed) {
         const Recipe r = recipeOf(g, st, seed);
         auto song = made(r);
         const Pattern &base = song->sections[kVerse].takes[0];
         Rng rng(seed);
         const Pattern f = makeFill(r, base, 0.6, rng);
         for (int s = f.stepsPerBar() - 1; s >= 0; --s)
            if (f.at(s).on() && !leftHand(f.at(s).part)) {
               if (!onBeat(f, s)) {
                  ++offLast;
                  pushed += f.at(s).push != 0;
               }
               break;
            }
      }
      JCLAP_CHECK(offLast > 0);
      JCLAP_CHECK_EQ(pushed, offLast);
   }
   // C02, C15: a litter's children are each heard apart from take 0 and from each other where the
   // style has room to vary (the engine's ear: a pick it voices alike is no change).
   for (const char *name : {"Cha-Cha", "Memphis Backbeat", "Slow Blues", "Calypso Strum", "Backbeat Piano"}) {
      int g, st;
      JCLAP_CHECK(styleCalled(name, g, st));
      const Style &style = genreAt(g).styles[st];
      const Recipe r = recipeOf(g, st, 4);
      auto song = made(r);
      const VoiceSettings vs = styleVoicing(r);
      for (const Section &sec : song->sections) {
         Rng rng(11);
         std::vector<std::string> tags;
         const auto litter = breedLitter(sec, 5, 0.6, rng, tags, style.mods, genreAt(g).name, false, 0, true, true, &vs);
         const Ear ear(&sec, sec.takes[0], &vs, style.mods);
         int dups = 0;
         for (size_t i = 0; i < litter.size(); ++i) {
            dups += heardChange(sec.takes[0], litter[i], &ear) == 0;
            for (size_t j = 0; j < i; ++j)
               dups += playedSame(litter[i], litter[j], true, &ear);
         }
         JCLAP_CHECK(dups <= 1);
      }
   }
   // D03, D11: TRY INSTEAD never offers the next chord itself, and keeps to the key: no lydian IV in
   // a Mixolydian key, a half-diminished ii before a V into minor.
   for (int key = 0; key < 12; ++key)
      for (int scale : {kMajor, kMinor})
         for (int a = 0; a < 7; ++a)
            for (int b = 0; b < 7; ++b) {
               const Chord c{static_cast<int8_t>((key + a * 2) % 12), kMaj, -1};
               const Chord next{static_cast<int8_t>((key + b * 2 + 1) % 12), kMin, -1};
               for (const auto &[x, why] : tryInstead(c, next, key, scale))
                  JCLAP_CHECK(!(x.root == next.root && x.root != c.root && minorChord(x) == minorChord(next)));
            }
   {
      // G Mixolydian: C (the IV) isn't offered as Cmaj7#11 (no F#).
      for (const auto &[x, why] : tryInstead(Chord{0, kMaj, -1}, Chord{7, kMaj, -1}, 7, kMixolydian))
         JCLAP_CHECK(std::strcmp(why, "lydian") != 0);
      // A minor: the ii before E7 into Am is Bm7b5.
      bool half = false;
      for (const auto &[x, why] : tryInstead(Chord{4, kDom7, -1}, Chord{9, kMin, -1}, 9, kMinor))
         if (std::strcmp(why, "ii before it") == 0)
            half = x.root == 11 && x.quality == kHalfDim;
      JCLAP_CHECK(half);
   }
}

// Round sixteen: the engine and roll findings (reviews B, E, C06).
JCOMP_TEST_NOINLINE static void roundSixteenE() {
   const double perBlock = kBlock / 24000.0;
   // A block at 120 BPM: following the host (playing), or with the host's bars (`barBeats` > 0).
   const auto block = [](Engine &e, OutBuffer &out, double beats, bool playing, double barStart = 0.0, double barBeats = 0.0) {
      Block b;
      b.frames = kBlock;
      b.tempo = 120.0;
      b.hasTransport = true;
      b.playing = playing;
      b.beats = beats;
      b.hasBar = barBeats > 0.0;
      b.barStart = barStart;
      b.barBeats = barBeats;
      out.clear();
      e.process(b, out);
   };
   struct Note {
      double on, off;
      int key, ch;
   };
   struct Notes {
      std::vector<Note> all;
      std::map<std::pair<int, int>, size_t> open;
      void add(const OutBuffer &out, double beats) {
         for (size_t i = 0; i < out.count; ++i) {
            const MidiOut &m = out.items[i];
            const double at = beats + m.time / 24000.0;
            const auto k = std::make_pair(m.status & 15, static_cast<int>(m.data1));
            if ((m.status & 0xF0) == 0x90 && m.data2 > 0) {
               open[k] = all.size();
               all.push_back({at, -1.0, m.data1, m.status & 15});
            } else if (open.count(k)) {
               all[open[k]].off = at;
               open.erase(k);
            }
         }
      }
      std::set<int> pcs(double from, double to) const {
         std::set<int> s;
         for (const Note &n : all)
            if (n.on >= from - 1e-6 && n.on < to)
               s.insert(n.key % 12);
         return s;
      }
      int onsIn(double from, double to) const {
         int c = 0;
         for (const Note &n : all)
            c += n.on >= from - 1e-6 && n.on < to;
         return c;
      }
      double firstOn(double from) const {
         double f = 1e9;
         for (const Note &n : all)
            if (n.on >= from - 1e-6)
               f = std::min(f, n.on);
         return f;
      }
   };
   const auto engineFor = [](const Song &song, const Settings &s) {
      auto e = std::make_unique<Engine>();
      e->setSong(song);
      e->setSettings(s);
      e->reset();
      return e;
   };
   const auto styleIndex = [](const char *genre, const char *style, int &gi, int &si) {
      for (gi = 0; gi < genreCount(); ++gi)
         if (std::string(genreAt(gi).name) == genre)
            for (si = 0; si < static_cast<int>(genreAt(gi).styles.size()); ++si)
               if (std::string(genreAt(gi).styles[static_cast<size_t>(si)].name) == style)
                  return true;
      return false;
   };
   const auto styleSettings = [](int gi, int si, uint32_t seed, Song &song) {
      std::vector<double> v = defaultValues();
      for (const auto &[id, x] : styleValues(gi, si))
         v[id] = x;
      v[kSeed] = seed;
      const Recipe r = recipeFrom([&](clap_id id) { return v[id]; });
      generateSong(r, song);
      Settings s = plain();
      s.voicing = voiceSettings([&](clap_id id) { return v[id]; });
      s.key = song.key;
      s.sync = 1;
      s.section = kVerse;
      applyRecipe(s, r);
      return s;
   };
   // The pitch classes of a chord as the progression names it (KEY moves it).
   const auto chordPcs = [](const Chord &c, int transpose) {
      std::set<int> s;
      for (int iv : qualityOf(c).tones)
         s.insert((c.root + transpose + iv) % 12);
      if (c.bass >= 0)
         s.insert((c.bass + transpose) % 12);
      return s;
   };

   // E01: RHYTHM FROM BASS, FOLLOW every note, a rolled chord and a bass note shorter than the roll:
   // the chord's last strings never start, and nothing is left sounding.
   for (int onsets : {1, 2}) {
      auto song = songOf("x...............", {{"C", nullptr}, {"F", nullptr}});
      Settings s = plain();
      s.sync = 1;
      s.rhythmFrom = 1;
      s.bassChannel = 1;
      s.follow = 0;
      s.onsets = onsets;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      int blocks = 0;
      for (double b = 0.0; b < 16.0; b += perBlock, ++blocks) {
         if (blocks == 375 || blocks == 750) // (beats 4 and 8)
            e->noteOn(1, 40, 100, 0);
         if (blocks == 377 || blocks == 752) // (21 ms later)
            e->noteOff(1, 40, 0);
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(3.9, 4.2) >= 1);
      for (const Note &x : n.all)
         JCLAP_CHECK(x.off > x.on && x.off - x.on < 1.0);
   }

   // B03: a push struck before a stop: the next playback from its target strikes that downbeat.
   {
      auto song = songOf("x...x...x...x.x.", {{"C", nullptr}, {"F", nullptr}});
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes)
            for (int k = 14; k < t.steps; k += 16)
               t.at(k).push = 1;
      Settings s = plain();
      s.sync = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n, fresh;
      double b = 0.0;
      for (; b < 3.72; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      block(*e, *out, b, false);
      for (b = 4.0; b < 6.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      auto f = engineFor(*song, s);
      for (b = 4.0; b < 6.0; b += perBlock) {
         block(*f, *out, b, true);
         fresh.add(*out, b);
      }
      JCLAP_CHECK(!fresh.pcs(3.97, 4.03).empty());
      JCLAP_CHECK(n.pcs(3.97, 4.03) == fresh.pcs(3.97, 4.03));
   }

   // B04: the first playback of a 3/4 (and a 2/4) project under a 4/4 style: the short first bar
   // rests, and the song's bar 1 comes in on the host's bar 2, struck once, no blip.
   for (int meter : {3, 2}) {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}, {"G", nullptr}});
      Settings s = plain();
      s.sync = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 20.0; b += perBlock) {
         block(*e, *out, b, true, std::floor(b / meter) * meter, meter);
         n.add(*out, b);
      }
      const double first = n.firstOn(0.0);
      JCLAP_CHECK(first > meter - 0.01 && first < meter + 0.02);
      JCLAP_CHECK(n.pcs(first, first + 0.05) == (std::set<int>{0, 4, 7}));
      for (size_t i = 0; i < n.all.size(); ++i) {
         JCLAP_CHECK(n.all[i].off < 0 || n.all[i].off - n.all[i].on > 0.04);
         for (size_t j = 0; j < i; ++j)
            JCLAP_CHECK(!(n.all[j].key == n.all[i].key && std::abs(n.all[j].on - n.all[i].on) < 0.06));
      }
   }

   // B05: RESTART 70 ms before the song's end, running free: the outro starts again at the end's bar
   // line, not a bar of silence later.
   {
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(0, 0, 1, *song);
      song->setForm(1);
      s.song = true;
      s.songEnds = true;
      s.section = kIntro;
      s.sync = 2;
      s.play = true;
      s.freeTempo = 120.0;
      const double end = song->totalBars() * (song->beats == 3 ? 3.0 : 4.0);
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool pressed = false;
      for (double b = 0.0; b < end + 8.0; b += perBlock) {
         if (!pressed && b >= end - 0.14) {
            e->request(kActRestart);
            pressed = true;
         }
         block(*e, *out, b, false);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(end - 0.02, end + 0.05) > 0);
   }

   // B06: a start inside Memphis Backbeat's HALF organ chord strikes it, not 1.75 beats of silence.
   {
      int gi = 0, si = 0;
      JCLAP_CHECK(styleIndex("Soul", "Memphis Backbeat", gi, si));
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(gi, si, 1, *song);
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 13.25; b < 16.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.firstOn(13.0) < 13.35);
   }

   // B07: a NEXT queued running free (Sync AUTO, PLAY on) is dropped when the host starts: the
   // host's bar places the song.
   {
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(0, 0, 1, *song);
      song->setForm(1);
      s.song = true;
      s.songEnds = true;
      s.section = kIntro;
      s.sync = 0;
      s.play = true;
      s.freeTempo = 120.0;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      for (double b = 0.0; b < 5.0; b += perBlock)
         block(*e, *out, b, false);
      e->request(kActNext);
      block(*e, *out, 5.0, false);
      const int introBeats = song->sections[kIntro].bars * 4;
      for (double b = 0.0; b < introBeats - 0.5; b += perBlock)
         block(*e, *out, b, true);
      Frame f;
      e->fillFrame(f);
      JCLAP_CHECK(f.section == kIntro);
      JCLAP_CHECK(f.queued == -1);
   }

   // B01: HOLD CHORD holds the chord heard, in a line take too (Right-Hand Arpeggio), and in a take
   // of line notes and chord chops (Clav Riff) the bar's own chord, not the last chop's.
   {
      int gi = 0, si = 0;
      JCLAP_CHECK(styleIndex("Piano Ballad", "Right-Hand Arpeggio", gi, si));
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(gi, si, 1, *song);
      const std::set<int> held = chordPcs(song->sections[kVerse].chordAt(0, false), 0);
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      const int bb = song->beats == 3 ? 3 : 4;
      bool pressed = false;
      for (double b = 0.0; b < 6.0 * bb; b += perBlock) {
         if (!pressed && b >= 2.0) {
            e->toggleHeld(kActHoldChord);
            pressed = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      const std::set<int> after = n.pcs(bb, 6.0 * bb);
      JCLAP_CHECK(!after.empty());
      for (int pc : after)
         JCLAP_CHECK(held.count(pc) > 0);
   }
   {
      int gi = 0, si = 0;
      JCLAP_CHECK(styleIndex("Funk", "Clav Riff", gi, si));
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(gi, si, 1, *song);
      const Chord bar2 = song->sections[kVerse].chordAt(1, false);
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool pressed = false;
      for (double b = 0.0; b < 16.0; b += perBlock) {
         if (!pressed && b >= 5.0) {
            e->toggleHeld(kActHoldChord);
            pressed = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      Frame f;
      e->fillFrame(f);
      JCLAP_CHECK(sameHarmony(f.chord, bar2));
      const std::set<int> pcs = n.pcs(8.0, 16.0), ok = chordPcs(bar2, 0);
      JCLAP_CHECK(!pcs.empty());
      int outside = 0;
      for (int pc : pcs)
         outside += !ok.count(pc);
      JCLAP_CHECK(outside <= 1); // (a line note may pass through a tone of the key)
   }
   // (And pressed a moment before a change the clock has already struck, the chord heard.)
   {
      auto song = songOf("x.......x.......", {{"C", nullptr}, {"G", nullptr}});
      Settings s = plain();
      s.sync = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool pressed = false;
      for (double b = 0.0; b < 12.0; b += perBlock) {
         if (!pressed && b >= 3.88) {
            e->toggleHeld(kActHoldChord);
            pressed = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.pcs(3.95, 12.0) == (std::set<int>{0, 4, 7}));
   }

   // B02: HOLD pressed while stopped, SONG following the host, the host starting in the first
   // chorus: the chorus plays (its first chord held), not the intro.
   {
      auto song = std::make_unique<Song>();
      Settings s = styleSettings(0, 0, 1, *song);
      song->setForm(1);
      s.song = true;
      s.songEnds = true;
      s.section = kIntro;
      int at = 0, from = 0;
      for (; at < song->entries() && song->entry(at) != kChorus; ++at)
         from += song->sections[static_cast<size_t>(song->entry(at))].bars * 4;
      JCLAP_CHECK(at < song->entries());
      auto e = engineFor(*song, s);
      e->toggleHeld(kActHoldChord);
      auto out = std::make_unique<OutBuffer>();
      for (double b = from; b < from + 8.0; b += perBlock)
         block(*e, *out, b, true);
      Frame f;
      e->fillFrame(f);
      JCLAP_CHECK(f.section == kChorus);
      JCLAP_CHECK(f.formPos == at);
   }

   // E09: below the middle of LENGTH, a push under a HOLD keeps its tie: the push's length moves
   // smoothly through LENGTH's middle (the hit it rests isn't one that lets it go).
   {
      auto song = songOf("x.............xx", {{"C", nullptr}, {"F", nullptr}});
      for (Section &sec : song->sections) {
         for (Pattern &t : sec.takes)
            for (int k = 14; k < t.steps; k += 16)
               t.at(k).push = 1;
         sec.marks[0] = kMarkHold;
      }
      double len[2] = {0.0, 0.0};
      for (int i = 0; i < 2; ++i) {
         Settings s = plain();
         s.sync = 1;
         s.length = i == 0 ? 0.45 : 0.5;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         for (double b = 0.0; b < 8.0; b += perBlock) {
            block(*e, *out, b, true);
            n.add(*out, b);
         }
         for (const Note &x : n.all)
            if (std::abs(x.on - 3.5) < 0.02 && x.off > 0)
               len[i] = std::max(len[i], x.off - x.on);
      }
      JCLAP_CHECK(len[0] > 0.0 && len[1] > 0.0);
      JCLAP_CHECK(len[0] > len[1] * 0.75);
   }

   // E10: a push into an ACC with nothing written on its beat stays tied over it: DENSITY's fill-in
   // there doesn't strike the accented chord again.
   {
      auto song = songOf("x.....x.........", {{"C", "F"}});
      for (Section &sec : song->sections) {
         for (Pattern &t : sec.takes)
            for (int k = 6; k < t.steps; k += 16)
               t.at(k).push = 1;
         sec.marks[1] = kMarkAccent;
      }
      Settings s = plain();
      s.sync = 1;
      s.density = 0.9;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 16.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      for (int bar = 0; bar < 4; ++bar) {
         std::map<int, int> ons;
         for (const Note &x : n.all)
            if (x.on >= bar * 4 + 1.45 && x.on < bar * 4 + 2.2)
               ++ons[x.key];
         for (const auto &[key, c] : ons)
            JCLAP_CHECK(c == 1);
      }
   }

   // E12: VARY's borrowed bar isn't shown as the take playing: the frame's take stays the TAKE set.
   {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}});
      for (Section &sec : song->sections)
         sec.takes[3] = fromRhythm("x.x.x.x.x.x.x.x.");
      Settings s = plain();
      s.sync = 1;
      s.vary = 1.0;
      s.take = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      int other = 0;
      for (double b = 0.0; b < 64.0; b += perBlock) {
         block(*e, *out, b, true);
         Frame f;
         e->fillFrame(f);
         other += f.take != 1;
      }
      JCLAP_CHECK(other == 0);
   }

   // E16: PAD CHANNEL moved while the pad holds a one-chord vamp: the old channel lets go at once,
   // and the pad goes on on the new one.
   {
      auto song = songOf("x...x...x...x...", {{"C", nullptr}});
      Settings s = plain();
      s.sync = 1;
      s.pad = true;
      s.padChannel = 2;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 12.0; b += perBlock) {
         if (std::abs(b - 6.0) < perBlock / 2) {
            s.padChannel = 5;
            e->setSettings(s);
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      bool oldOn = false, newOn = false;
      for (const Note &x : n.all) {
         if (x.ch == 2)
            oldOn = oldOn || x.off < 0 || x.off > 6.2;
         if (x.ch == 5)
            newOn = newOn || x.on < 6.5;
      }
      JCLAP_CHECK(!oldOn);
      JCLAP_CHECK(newOn);
   }

   // C06: DENSITY thins a chucking guitar to fewer chucks: no chord of Chic Chucking's choked take
   // rings on for beats where its thinned neighbours were.
   {
      int gi = 0, si = 0;
      JCLAP_CHECK(styleIndex("Disco", "Chic Chucking", gi, si));
      for (double density : {0.0, 0.3, 0.45}) {
         auto song = std::make_unique<Song>();
         Settings s = styleSettings(gi, si, 1, *song);
         s.density = density;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         for (double b = 0.0; b < 16.0; b += perBlock) {
            block(*e, *out, b, true);
            n.add(*out, b);
         }
         double longest = 0.0;
         for (const Note &x : n.all)
            if (x.off > 0)
               longest = std::max(longest, x.off - x.on);
         JCLAP_CHECK(!n.all.empty());
         JCLAP_CHECK(longest <= 1.0);
      }
   }
}

// Round seventeen: the harmony findings (review D).
// Round seventeen (D): a slash chord's doubles, OCTAVES in the window, GRIP's six strings and open
// slash grips, 4THS by step, the half-diminished shell's name.
JCOMP_TEST_NOINLINE static void roundSeventeenD() {
   const auto settings = [](int shape, int voices, int lo = 50, int hi = 76, int center = 62) {
      VoiceSettings vs;
      vs.shape = shape;
      vs.voices = voices;
      vs.lo = lo;
      vs.hi = hi;
      vs.center = center;
      vs.maxSpan = 17;
      vs.floor = std::min(43, lo);
      return vs;
   };
   const auto notesOf = [](const Voicing &v) {
      Ints got;
      for (int i = 0; i < v.count; ++i)
         got.push_back(v.notes[static_cast<size_t>(i)]);
      return got;
   };
   const auto pcCount = [](const Voicing &v, int pc) {
      int k = 0;
      for (int i = 0; i < v.count; ++i)
         k += v.notes[static_cast<size_t>(i)] % 12 == ((pc % 12) + 12) % 12;
      return k;
   };
   // D01: a maj7 over its 9th (Fmaj7/G) doubles no tone the bass sounds first: no maj7 under a
   // doubled root, in every key, in the Pad Player's window.
   for (int r = 0; r < 12; ++r) {
      const Chord c = chord("Fmaj7/G", r);
      const Voicing v = voice(c, nullptr, settings(kDrop24, 5, 45, 79, 60));
      JCLAP_CHECK(v.count == 5 && v.bottom() % 12 == c.bass && clashes(v, c) <= 0.0);
   }
   // D02: a six-four doubles its bass rather than sound the third twice over it; C/G at five is
   // G3 C4 E4 G4 C5-like, never muddy, one E.
   for (int r = 0; r < 12; ++r) {
      const Chord c = chord("C/G", r);
      const VoiceSettings vs = settings(kDrop24, 5, 48, 74, 62);
      const Voicing v = voice(c, nullptr, vs);
      JCLAP_CHECK(v.count == 5 && v.bottom() % 12 == c.bass);
      JCLAP_CHECK(!breaksOf(v, nullptr, vs).mud);
      JCLAP_CHECK(pcCount(v, c.root + 4) == 1);
   }
   for (int r = 0; r < 12; ++r)
      JCLAP_CHECK(voice(chord("B/F#", r), nullptr, settings(kClose, 5, 48, 74, 62)).count == 5);
   // D03: a maj7 over its third keeps clear of the maj7-under-root clash where a clean voicing exists.
   for (int shape : {kClose, kDrop2, kDrop24})
      for (int r = 0; r < 12; ++r) {
         const Chord c = chord("Cmaj7/E", r);
         const Voicing v = voice(c, nullptr, settings(shape, 4));
         JCLAP_CHECK(v.bottom() % 12 == c.bass && clashes(v, c) <= 0.0);
      }
   // D04: OCTAVES' line stays in the Montuno's window: over Cmaj7 Am7 Dm7 G7 (16 passes, the step
   // reset at each change), the mean bottom within 7 of CENTER - 6, under 10% of strokes under
   // LOWEST, the wanted degree on 75% at least, and the root struck.
   {
      VoiceSettings vs = settings(kOctaves, 4, 55, 84, 68);
      vs.key = 0;
      const char *names[] = {"Cmaj7", "Am7", "Dm7", "G7"};
      Voicing prev;
      bool has = false;
      long total = 0, wanted = 0, under = 0, roots = 0;
      double sum = 0.0;
      for (int rep = 0; rep < 16; ++rep)
         for (const char *n : names) {
            const Chord c = chord(n);
            const auto &q = qualityOf(c).tones;
            const int want[4] = {q[2] % 12, q[1] % 12, 0, q[1] % 12};
            for (int step = 0; step < 4; ++step) {
               const Voicing v = octaveLine(c, has ? &prev : nullptr, vs, step);
               const int deg = ((v.notes[0] - c.root) % 12 + 12) % 12;
               if (rep > 0) {
                  ++total;
                  wanted += deg == want[step];
                  roots += deg == 0;
                  under += v.notes[0] < vs.lo;
                  sum += v.notes[0];
               }
               prev = v;
               has = true;
            }
         }
      JCLAP_CHECK(std::abs(sum / total - (vs.center - 6)) <= 7.0);
      JCLAP_CHECK(under * 10 < total);
      JCLAP_CHECK(wanted * 4 >= total * 3);
      JCLAP_CHECK(roots * 8 >= total);
   }
   // D05: GRIP at five rings a six-string open chord's six strings (G7 320001, E9 020102, E 022100).
   {
      VoiceSettings vs = settings(kGrip, 5, 40, 76, 58);
      vs.floor = 40;
      JCLAP_CHECK(notesOf(voice(chord("G7"), nullptr, vs)) == (Ints{43, 47, 50, 55, 59, 65}));
      JCLAP_CHECK(notesOf(voice(chord("E9"), nullptr, vs)) == (Ints{40, 47, 50, 56, 59, 66}));
      JCLAP_CHECK(notesOf(voice(chord("E"), nullptr, vs)) == (Ints{40, 47, 52, 56, 59, 64}));
      // (Emaj7 is 021100: its D#3 under the open strings is the instrument's, no clash to avoid.)
      JCLAP_CHECK(notesOf(voice(chord("Emaj7"), nullptr, vs)) == (Ints{40, 47, 51, 56, 59, 64}));
   }
   // D06: the open slash grips a guitarist plays, in reach of three fingers.
   {
      VoiceSettings vs = settings(kGrip, 5, 40, 76, 58);
      vs.floor = 40;
      JCLAP_CHECK(notesOf(voice(chord("A/E"), nullptr, vs)) == (Ints{40, 45, 52, 57, 61, 64}));
      JCLAP_CHECK(notesOf(voice(chord("Am/E"), nullptr, vs)) == (Ints{40, 45, 52, 57, 60, 64}));
      JCLAP_CHECK(notesOf(voice(chord("D/A"), nullptr, vs)) == (Ints{45, 50, 57, 62, 66}));
      JCLAP_CHECK(notesOf(voice(chord("F/A"), nullptr, vs)) == (Ints{45, 53, 57, 60, 65}));
      for (const char *name : {"A/E", "Am/E", "D/A", "F/A", "E7/G#", "E/B"}) {
         const Chord c = chord(name);
         const Voicing v = voice(c, nullptr, vs);
         JCLAP_CHECK(v.bottom() % 12 == c.bass && gripFrets(v) >= 0.0 && gripFrets(v) <= 4.0);
      }
   }
   // D08: C Gb Bb is Cm7b5's shell, not a rootless Ab9, where the hand or the key says so: its root
   // doubled under it in any key, or bare in Bb minor (its ii); bare in no key, or in Db where Ab9 is
   // the V, it stays the stock rootless left hand, as E Bb D stays C9.
   for (int key : {-1, 0, 3, 10})
      JCLAP_CHECK(chordName(nameChord(Ints{36, 48, 54, 58}, true, key), 0) == "Cm7b5");
   JCLAP_CHECK(chordName(nameChord(Ints{48, 54, 58}, true, 10, kMinor), 0) == "Cm7b5");
   JCLAP_CHECK(chordName(nameChord(Ints{48, 54, 58}, true, 1), 0) == "Ab9");
   JCLAP_CHECK(chordName(nameChord(Ints{52, 58, 62}), 0) == "C9");
   // D10: SPREAD at five over a triad's third sounds that third twice at most (G/B: no B three times).
   for (const char *name : {"G/B", "C/E", "D/F#", "F/A"})
      for (int r = 0; r < 12; ++r) {
         const Chord c = chord(name, r);
         const Voicing v = voice(c, nullptr, settings(kSpread, 5, 48, 79, 64));
         JCLAP_CHECK(v.bottom() % 12 == c.bass && pcCount(v, c.bass) <= 2);
      }
   // D11: a diminished triad doubles its minor third, not the tritone over its root (Cdim's Eb).
   for (int r = 0; r < 12; ++r) {
      const Chord c = chord("Cdim", r);
      const Voicing v = voice(c, nullptr, settings(kDrop2, 4));
      JCLAP_CHECK(v.count == 4 && pcCount(v, c.root + 3) == 2 && pcCount(v, c.root + 6) == 1);
   }
   // D12: 4THS at four follows the top as 0.17.0 did: over the ii-V-Is, no more than a quarter of
   // the changes leap past TOP MOVE, and every stack still within one of the most fourths.
   {
      const VoiceSettings vs = settings(kQuartal, 4, 48, 74, 62);
      const char *progs[][4] = {{"Dm7", "G7", "Cmaj7", "Cmaj7"}, {"Em7", "A7", "Dm7", "G7"},
                                {"Cm7", "F7", "Bbmaj7", "Ebmaj7"}, {"Cmaj7", "Am7", "Dm7", "G7"}};
      long changes = 0, leaps = 0;
      for (auto &p : progs)
         for (int r = 0; r < 12; ++r) {
            Voicing prev;
            bool has = false;
            for (int rep = 0; rep < 2; ++rep)
               for (const char *n : p) {
                  const Voicing v = voice(chord(n, r), has ? &prev : nullptr, vs);
                  if (has && rep == 1) {
                     ++changes;
                     leaps += std::abs(v.top() - prev.top()) > vs.topMove;
                  }
                  prev = v;
                  has = true;
               }
         }
      JCLAP_CHECK(leaps * 4 <= changes);
   }
   // D13: the open Bm7 is x20202 as its table comment says, B2 D3 A3 B3 F#4, not the second fret's
   // barre.
   {
      VoiceSettings vs = settings(kGrip, 5, 40, 76, 58);
      vs.floor = 40;
      const Voicing v = voice(chord("Bm7"), nullptr, vs);
      JCLAP_CHECK(notesOf(v) == (Ints{47, 50, 57, 59, 66}) && gripFrets(v) == 0.0);
   }
}

// Round seventeen: the styles, takes and mutation findings (reviews A, C, D07, D09, D14, D15).
JCOMP_TEST_NOINLINE static void roundSeventeenC() {
   const auto styleCalled = [](const char *name, int &g, int &st) {
      for (g = 0; g < genreCount(); ++g)
         for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            if (std::string(genreAt(g).styles[st].name) == name)
               return true;
      return false;
   };
   // A01, A02: thinned to a quiet section, a backbeat keeps its 2 and its 4 together, and a sparse
   // figure (three hits or fewer a bar) keeps every hit.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         if (style.mods & kFixed)
            continue;
         for (const Rhythm *rh : rhythmsFor(genreAt(g), style)) {
            const Pattern written = fromRhythm(*rh);
            const int bar = written.stepsPerBar(), beat = written.perBeat();
            for (double e : {0.1, 0.25, 0.35})
               for (uint32_t k = 1; k <= 24; ++k) {
                  Pattern p = written;
                  Rng rng(k * 101u);
                  shapeEnergy(p, e, rng, false, style.mods, false);
                  // (Sparse is judged over the take: every bar three chords or fewer.)
                  bool sparse = true;
                  for (int from = 0; from + bar <= p.steps; from += bar)
                     sparse = sparse && hitsInBar(written, from / bar, true) <= 3;
                  for (int from = 0; from + bar <= p.steps; from += bar) {
                     if (backbeatStroke(written, from + beat) && backbeatStroke(written, from + 3 * beat))
                        JCLAP_CHECK(p.at(from + beat).on() == p.at(from + 3 * beat).on());
                     if (e >= 0.15 && sparse)
                        for (int s = from; s < from + bar; ++s)
                           if (written.at(s).on() && written.at(s).part == kPartChord)
                              JCLAP_CHECK(p.at(s).on());
                  }
               }
         }
      }
   // A09: a pre-chorus builds: with three rhythms or more to draw from, it never draws the busiest
   // (that is the chorus's), at any ENERGY.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         if (style.mods & (kInterlock | kFixed))
            continue;
         const auto list = rhythmsFor(genreAt(g), style);
         if (list.size() < 3)
            continue;
         double lo = 9, hi = -9;
         for (const Rhythm *rh : list) {
            lo = std::min(lo, rh->energy);
            hi = std::max(hi, rh->energy);
         }
         if (hi <= lo)
            continue;
         for (double energy : {0.2, 0.5, 0.9})
            for (uint32_t seed = 1; seed <= 12; ++seed) {
               Recipe r = recipeOf(g, st, seed);
               r.energy = energy;
               Rng rng(mix(mix(r.seed, kPre), static_cast<uint32_t>(kPre) * 977u + 13u));
               Section s;
               makeChords(r, kPre, s, rng);
               const Rhythm *rh = sectionRhythm(r, kPre, rng);
               JCLAP_CHECK(rh && rh->energy < hi - 1e-6);
            }
      }
   // A07: rolling the verse alone never leaves the chorus below its new verse.
   {
      int g, st;
      JCLAP_CHECK(styleCalled("Charleston Comp", g, st));
      for (uint32_t seed = 1; seed <= 12; ++seed) {
         const Recipe r = recipeOf(g, st, seed);
         auto song = std::make_unique<Song>();
         generateSong(r, *song);
         for (uint32_t k = 1; k <= 4; ++k) {
            auto rolled = std::make_unique<Song>(*song);
            rollSection(r, *rolled, kVerse, mix(seed * 31u + k, 777u));
            Recipe rr = r;
            rr.key = rolled->key;
            const double verse = sectionRhythmEnergy(rr, kVerse, rolled->sections[kVerse].seed);
            const bool remade = rolled->sections[kChorus].generation != song->sections[kChorus].generation;
            const double floor = remade ? verse : sectionRhythmEnergy(rr, kVerse, song->sections[kVerse].seed);
            JCLAP_CHECK(sectionRhythmEnergy(rr, kChorus, rolled->sections[kChorus].seed, floor) >= verse - 1e-6);
         }
      }
   }
   // D07, D09: a V over its own third is never a sus; a V into a minor chord takes no 9 or 13.
   for (uint32_t k = 1; k <= 200; ++k)
      for (int key = 0; key < 12; ++key) {
         Rng rng(k);
         Chord v;
         v.root = static_cast<int8_t>((key + 7) % 12);
         v.quality = kMaj;
         Chord home;
         home.root = static_cast<int8_t>(key);
         home.quality = k % 2 ? kMin : kMaj;
         Chord over = v;
         over.bass = static_cast<int8_t>((v.root + 4) % 12);
         const Chord a = richer(over, rng, &home, key, k % 2 ? kMinor : kMajor);
         JCLAP_CHECK(a.quality != kSus && a.quality != kSus7 && a.quality != kSus13);
         const Chord b = richer(v, rng, &home, key, k % 2 ? kMinor : kMajor);
         if (k % 2)
            JCLAP_CHECK(b.quality != kDom9 && b.quality != kDom13 && b.quality != kSus13);
         Chord sus = v;
         sus.quality = kSus;
         const Chord c = richer(sus, rng, &home, key, k % 2 ? kMinor : kMajor);
         if (k % 2)
            JCLAP_CHECK(c.quality != kSus13);
      }
   // D09, D14: TRY INSTEAD's sus into a minor chord is a 7sus; no two suggestions on the same root
   // as the same dominant (a tritone sub and a half step above are one chord).
   for (int key = 0; key < 12; ++key)
      for (int minor = 0; minor < 2; ++minor) {
         Chord v, home;
         v.root = static_cast<int8_t>((key + 7) % 12);
         v.quality = kDom7;
         home.root = static_cast<int8_t>(key);
         home.quality = minor ? kMin : kMaj;
         const auto list = tryInstead(v, home, key, minor ? kMinor : kMajor);
         for (size_t i = 0; i < list.size(); ++i) {
            if (minor && std::string(list[i].second) == "sus it")
               JCLAP_CHECK(list[i].first.quality != kSus13);
            for (size_t j = i + 1; j < list.size(); ++j)
               JCLAP_CHECK(!(list[i].first.root == list[j].first.root && list[i].first.quality == list[j].first.quality &&
                             list[i].first.bass == list[j].first.bass));
         }
      }
   // C01, C02: a litter never holds a copy: every child is tagged, heard to change, and sounds like
   // no take and no other child.
   for (const char *name : {"Quarter Pound", "Minor Rock", "Quiet Bossa", "Sunday Ballad", "Gospel Push"}) {
      int g, st;
      JCLAP_CHECK(styleCalled(name, g, st));
      const Style &style = genreAt(g).styles[st];
      for (uint32_t seed = 1; seed <= 3; ++seed) {
         const Recipe r = recipeOf(g, st, seed);
         auto song = std::make_unique<Song>();
         generateSong(r, *song);
         const VoiceSettings vs = styleVoicing(r);
         for (const Section &sec : song->sections) {
            Rng rng(seed * 17u);
            std::vector<std::string> tags;
            const auto litter = breedLitter(sec, 5, 0.5, rng, tags, style.mods, genreAt(g).name,
                                            playerOf(r).instrument == kOrgan, style.bpm,
                                            playerOf(r).instrument == kPiano || playerOf(r).instrument == kEPiano,
                                            playerOf(r).runs > 0.0, &vs, styleRuns(r));
            JCLAP_CHECK(litter.size() <= 5 && tags.size() == litter.size());
            const Ear ear(&sec, sec.takes[0], &vs, style.mods, styleRuns(r));
            for (size_t i = 0; i < litter.size(); ++i) {
               JCLAP_CHECK(!tags[i].empty());
               JCLAP_CHECK(heardChange(sec.takes[0], litter[i], &ear) > 0);
               for (size_t j = 0; j < i; ++j)
                  JCLAP_CHECK(!playedSame(litter[i], litter[j], true, &ear));
            }
         }
      }
   }
   // C07: a moved onset under 5 ms is the same hit.
   {
      Hit a, b;
      a.vel = b.vel = kNormal;
      a.nudge = 0;
      b.nudge = 3;
      JCLAP_CHECK(playedSame(a, b));
      b.nudge = 8;
      JCLAP_CHECK(!playedSame(a, b));
   }
   // The reviewers' tests (review-A).
   const auto picked = [](const Recipe &r, int section, uint32_t seed, double floor) {
      Rng rng(mix(seed, static_cast<uint32_t>(section) * 977u + 13u));
      Section s;
      makeChords(r, section, s, rng);
      return sectionRhythm(r, section, rng, floor);
   };
   const auto indexOf = [](const Genre &g, const Rhythm *rh) {
      for (size_t i = 0; i < g.rhythms.size(); ++i)
         if (&g.rhythms[i] == rh)
            return static_cast<int>(i);
      return -1;
   };
   const auto chordAt = [](const Pattern &p, int s) { return s < p.steps && p.at(s).on() && p.at(s).part == kPartChord; };
   // A01: a quiet intro or break keeps its 2 and its 4 together.
   for (const char *name : {"Organ Bubble", "Rockers", "Boom-Chick"})
      for (int sec : {kIntro, kBreak})
         for (double e : {0.2, 0.5})
            for (uint32_t seed = 1; seed <= 40; ++seed) {
               int g, st;
               JCLAP_CHECK(styleCalled(name, g, st));
               Recipe r = recipeOf(g, st, seed);
               r.energy = e;
               auto song = std::make_unique<Song>();
               generateSong(r, *song);
               const Pattern &p = song->sections[static_cast<size_t>(sec)].takes[0];
               const int bar = p.stepsPerBar(), beat = p.perBeat();
               for (int from = 0; !p.inThree() && from + bar <= p.steps; from += bar)
                  JCLAP_CHECK(chordAt(p, from + beat) == chordAt(p, from + 3 * beat));
            }
   // A03, A04: the Charleston and the ballad figure come back to their verses; a chorus lifts past them.
   for (const auto &[name, want] : {std::pair<const char *, int>{"Charleston Comp", 0}, {"Ballad Block", 5}}) {
      int g, st;
      JCLAP_CHECK(styleCalled(name, g, st));
      int verse = 0, chorus = 0;
      for (uint32_t seed = 1; seed <= 300; ++seed) {
         const Recipe r = recipeOf(g, st, seed);
         const double floor = sectionRhythmEnergy(r, kVerse, mix(r.seed, kVerse));
         verse += indexOf(genreAt(g), picked(r, kVerse, mix(r.seed, kVerse), -1.0)) == want;
         chorus += indexOf(genreAt(g), picked(r, kChorus, mix(r.seed, kChorus), floor)) == want;
      }
      JCLAP_CHECK(verse >= 60);
      JCLAP_CHECK(chorus == 0);
   }
   // A05: Garland's chorus pushes in every bar.
   {
      int g, st;
      JCLAP_CHECK(styleCalled("Garland Pushes", g, st));
      for (uint32_t seed = 1; seed <= 40; ++seed) {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         const Pattern &p = song->sections[kChorus].takes[0];
         const int bar = p.stepsPerBar();
         for (int from = 0; from + bar <= p.steps; from += bar) {
            bool pushed = false;
            for (int s = from; s < from + bar; ++s)
               pushed = pushed || (p.at(s).on() && p.at(s).push);
            JCLAP_CHECK(pushed);
         }
      }
   }
   // A08: Boom-Chick's chorus fill leads in like its verse fill.
   {
      int g, st;
      JCLAP_CHECK(styleCalled("Boom-Chick", g, st));
      const auto lastPush = [](const Pattern &p) {
         for (int s = p.steps - 1; s >= 0; --s)
            if (p.at(s).on() && p.at(s).part == kPartChord)
               return p.at(s).push != 0;
         return false;
      };
      for (uint32_t seed = 1; seed <= 30; ++seed) {
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, seed), *song);
         if (lastPush(song->sections[kVerse].fill))
            JCLAP_CHECK(lastPush(song->sections[kChorus].fill));
      }
   }
   // A10: a loud New Jack Swing chorus plays no chord on its figure's rests (WEIRD off: its nudges
   // are WEIRD's own).
   {
      int g, st;
      JCLAP_CHECK(styleCalled("New Jack Swing", g, st));
      for (uint32_t seed = 1; seed <= 40; ++seed) {
         Recipe r = recipeOf(g, st, seed);
         r.sections[kChorus] = 0.9;
         r.weird = 0.0;
         auto song = std::make_unique<Song>();
         generateSong(r, *song);
         const double floor = sectionRhythmEnergy(r, kVerse, song->sections[kVerse].seed);
         const Rhythm *rh = picked(r, kChorus, song->sections[kChorus].seed, floor);
         JCLAP_CHECK(rh != nullptr);
         const Pattern written = fromRhythm(*rh), &p = song->sections[kChorus].takes[0];
         if (rh && written.rate == p.rate)
            for (int s = 0; s < p.steps; ++s)
               if (chordAt(p, s))
                  JCLAP_CHECK(written.at(s % written.steps).on());
      }
   }
}

// Round seventeen: the engine and roll findings (reviews B, E, A06).
// The seventeenth review's engine fixes (B01-B10, B12-B13, E06-E07, A06).
JCOMP_TEST_NOINLINE static void roundSeventeenE() {
   const double perBlock = kBlock / 24000.0;
   // A block at `tempo` BPM (48 kHz): following the host (playing), or with the host's bars
   // (`barBeats` > 0); `frames` short for a stop's block.
   const auto block = [](Engine &e, OutBuffer &out, double beats, bool playing, double barStart = 0.0, double barBeats = 0.0,
                         double tempo = 120.0, uint32_t frames = kBlock) {
      Block b;
      b.frames = frames;
      b.sampleRate = 48000.0;
      b.tempo = tempo;
      b.hasTransport = true;
      b.playing = playing;
      b.beats = beats;
      b.hasBar = barBeats > 0.0;
      b.barStart = barStart;
      b.barBeats = barBeats;
      out.clear();
      e.process(b, out);
   };
   const auto freeBlock = [](Engine &e, OutBuffer &out) {
      Block b;
      b.frames = kBlock;
      b.sampleRate = 48000.0;
      b.tempo = 120.0;
      b.hasTransport = false;
      out.clear();
      e.process(b, out);
   };
   struct Note {
      double on, off;
      int key, ch;
   };
   struct Notes {
      std::vector<Note> all;
      std::map<std::pair<int, int>, size_t> open;
      int doubled = 0;
      void add(const OutBuffer &out, double beats, double tempo = 120.0) {
         for (size_t i = 0; i < out.count; ++i) {
            const MidiOut &m = out.items[i];
            const double at = beats + m.time / 48000.0 * tempo / 60.0;
            const auto k = std::make_pair(m.status & 15, static_cast<int>(m.data1));
            if ((m.status & 0xF0) == 0x90 && m.data2 > 0) {
               doubled += open.count(k) > 0;
               open[k] = all.size();
               all.push_back({at, -1.0, m.data1, m.status & 15});
            } else if (open.count(k)) {
               all[open[k]].off = at;
               open.erase(k);
            }
         }
      }
      int onsIn(double from, double to, int ch = -1) const {
         int c = 0;
         for (const Note &n : all)
            c += n.on >= from - 1e-6 && n.on < to && (ch < 0 || n.ch == ch);
         return c;
      }
      std::set<int> keysIn(double from, double to, int ch = -1) const {
         std::set<int> s;
         for (const Note &n : all)
            if (n.on >= from - 1e-6 && n.on < to && (ch < 0 || n.ch == ch))
               s.insert(n.key);
         return s;
      }
      std::set<int> pcs(double from, double to, int ch = -1) const {
         std::set<int> s;
         for (const Note &n : all)
            if (n.on >= from - 1e-6 && n.on < to && (ch < 0 || n.ch == ch))
               s.insert(n.key % 12);
         return s;
      }
      int soundingAt(double b, int ch = -1) const {
         int c = 0;
         for (const Note &n : all)
            c += n.on <= b && (n.off < 0 || n.off > b) && (ch < 0 || n.ch == ch);
         return c;
      }
      double shortest() const {
         double s = 1e9;
         for (const Note &n : all)
            if (n.off >= 0.0)
               s = std::min(s, n.off - n.on);
         return s;
      }
   };
   const auto engineFor = [](const Song &song, const Settings &s) {
      auto e = std::make_unique<Engine>();
      e->setSong(song);
      e->setSettings(s);
      e->reset();
      return e;
   };
   // Every section alike, but its chord: `chords[i]` (round) the whole of section i.
   const auto sectionsOf = [](const char *rhythm, std::vector<const char *> chords, int bars) {
      auto r = std::make_unique<Song>();
      r->key = 0;
      for (int i = 0; i < kSections; ++i) {
         Section &sec = r->sections[static_cast<size_t>(i)];
         for (Pattern &t : sec.takes)
            t = fromRhythm(rhythm);
         sec.fill = fromRhythm(rhythm);
         sec.fillDown = Pattern{};
         sec.ending = Pattern{};
         sec.progBars = 1;
         sec.bars = static_cast<uint8_t>(bars);
         sec.split = 0;
         sec.chords[0] = sec.chords[1] = chord(chords[static_cast<size_t>(i) % chords.size()]);
      }
      return r;
   };

   // B01: a push into a section asked for (a tile in LOOP, NEXT in SONG) ties over its downbeat,
   // as one into the song's own next entry does: the downbeat isn't struck again.
   for (int mode : {0, 1, 2}) {
      auto song = sectionsOf("x.......x.....x.", {"C", "F", "G", "A", "Bb", "D", "E"}, 2);
      for (Section &sec : song->sections) {
         for (Pattern &t : sec.takes)
            for (int k = 14; k < t.steps; k += 16)
               t.at(k).push = 1;
         sec.fill = sec.takes[0];
      }
      song->setForm(0);
      Settings s = plain();
      s.sync = 1;
      s.section = kIntro;
      s.song = mode >= 1;
      s.switchAt = 1;
      s.length = 0.5;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool asked = false;
      for (double b = 0.0; b < 12.0; b += perBlock) {
         if (!asked && b >= 4.5) {
            if (mode == 0)
               e->request(kActVerse);
            if (mode == 2)
               e->request(kActNext);
            asked = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(7.45, 7.55, 0) >= 3); // the push, into the verse's F
      JCLAP_CHECK(n.onsIn(7.9, 8.1, 0) == 0);   // tied over
      JCLAP_CHECK(n.soundingAt(8.2, 0) >= 3);
   }

   // B02: a tile pressed a moment before the bar line it may switch at (SWITCH AT next bar) switches
   // there, not a bar later: the bar line the clock has ticked is taken back.
   for (double d : {0.12, 0.08, 0.04}) {
      auto song = sectionsOf("x...x...x...x...", {"C", "D", "E", "F", "G", "A", "B"}, 8);
      song->setForm(0);
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      s.switchAt = 1;
      s.fillInto = false;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool asked = false;
      for (double b = 0.0; b < 24.0; b += perBlock) {
         if (!asked && b >= 16.0 - d) {
            e->request(kActChorus);
            asked = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.pcs(15.9, 16.1, 0) == std::set<int>({5, 9, 0})); // the chorus's F at 16
      JCLAP_CHECK(n.pcs(16.1, 20.0, 0).count(4) == 0);                // (no more of the verse's E)
   }

   // B03: a tile pressed just as a free SONG ends (its last bar line taken back) brings the pad
   // back with the section, as the comp.
   {
      auto song = sectionsOf("x...............", {"C", "D", "E", "F", "G", "A", "C"}, 1);
      song->setForm(0);
      Settings s = plain();
      s.sync = 2;
      s.play = true;
      s.freeTempo = 120;
      s.song = true;
      s.songEnds = true;
      s.section = kIntro;
      s.pad = true;
      s.padChannel = 2;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool asked = false;
      for (double b = 0.0; b < 40.0; b += perBlock) {
         if (!asked && b >= 28.0 - 0.05) {
            e->request(kActIntro);
            asked = true;
         }
         freeBlock(*e, *out);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(27.95, 28.1, 0) >= 3);
      JCLAP_CHECK(n.soundingAt(28.5, 2) >= 3); // the pad too (the same chord ringing on, not struck again)
   }

   // B04: the song over and the transport stopped, SONG switched to LOOP: nothing is left cued, and
   // the next playback starts on its take, bar 1 once.
   {
      auto song = sectionsOf("x...x...x...x...", {"C", "D", "E", "F", "G", "A", "C"}, 4);
      for (Section &sec : song->sections) {
         sec.progBars = 4;
         sec.chords[2] = sec.chords[3] = chord("Bb");
         sec.chords[4] = sec.chords[5] = chord("Ab");
         sec.chords[6] = sec.chords[7] = chord("Db");
         sec.fill = fromRhythm("x.x.x.x.x.x.x.x.");
      }
      song->setForm(0);
      Settings s = plain();
      s.sync = 1;
      s.song = true;
      s.songEnds = true;
      s.section = kIntro;
      s.switchAt = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      double b = 0.0;
      for (; b < 7 * 16 + 2; b += perBlock)
         block(*e, *out, b, true);
      block(*e, *out, b, false);
      s.song = false;
      e->setSettings(s);
      block(*e, *out, b, false);
      Frame f;
      e->fillFrame(f);
      JCLAP_CHECK(f.queued < 0);
      JCLAP_CHECK(!f.ended); // B10: nor does it say the song has ended
      Notes n;
      bool fill = false;
      for (b = 0.0; b < 16.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
         if (b < 1.0) {
            e->fillFrame(f);
            fill = fill || f.fill;
         }
      }
      JCLAP_CHECK(!fill);
      JCLAP_CHECK(n.onsIn(0.0, 3.9, 0) <= 20);        // bar 1's quarters, not the fill's eighths
      JCLAP_CHECK(n.pcs(3.9, 7.9, 0).count(10) == 1); // and bar 2 is the Bb, not bar 1 again
   }

   // B10: stopped after the song's end, following or free, the song is over with the stop.
   for (int sync : {1, 2}) {
      auto song = sectionsOf("x...............", {"C", "D", "E", "F", "G", "A", "C"}, 1);
      song->setForm(0);
      Settings s = plain();
      s.sync = sync;
      s.play = true;
      s.freeTempo = 120;
      s.song = true;
      s.songEnds = true;
      s.section = kIntro;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      for (double b = 0.0; b < 34.0; b += perBlock)
         if (sync == 2)
            freeBlock(*e, *out);
         else
            block(*e, *out, b, true);
      Frame f;
      e->fillFrame(f);
      JCLAP_CHECK(f.ended);
      if (sync == 2) {
         s.play = false;
         e->setSettings(s);
         freeBlock(*e, *out);
      } else
         block(*e, *out, 34.0, false);
      e->fillFrame(f);
      JCLAP_CHECK(!f.ended);
   }

   // B05: HOLD CHORD in the song's ending holds its last chord as a fermata, past the song's end,
   // until let go (the song then ends at the next bar line); a press in the ending's first bar keeps
   // the ending's last chord.
   for (int sync : {1, 2}) {
      auto song = sectionsOf("x...x...x...x...", {"C", "D", "E", "F", "G", "A", "C"}, 2);
      Section &outro = song->sections[kOutro];
      outro.ending = fromRhythm("x...x...x...x...x---------------");
      song->setForm(0);
      Settings s = plain();
      s.sync = sync;
      s.play = true;
      s.freeTempo = 120;
      s.song = true;
      s.songEnds = true;
      s.section = kIntro;
      const double end = song->totalBars() * 4.0;
      for (double press : {-2.5, -6.0}) {
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         bool held = false, let = false;
         const double release = press < -3.0 ? -1.0 : 6.0;
         Frame f;
         bool ending = true;
         for (double b = 0.0; b < end + 12.0; b += perBlock) {
            if (!held && b >= end + press) {
               e->toggleHeld(kActHoldChord);
               held = true;
            }
            if (!let && b >= end + release) {
               e->toggleHeld(kActHoldChord);
               let = true;
            }
            if (sync == 2)
               freeBlock(*e, *out);
            else
               block(*e, *out, b, true);
            n.add(*out, b);
            if (press > -3.0 && b > end + 1.0 && b < end + 5.0) {
               e->fillFrame(f);
               ending = ending && f.ending && !f.ended;
            }
         }
         JCLAP_CHECK(ending); // (the ending held, the song not over)
         JCLAP_CHECK(n.onsIn(end - 4.05, end - 3.9, 0) >= 3); // the ending's last chord
         JCLAP_CHECK(n.onsIn(end - 3.9, end + 12.0, 0) == 0);
         if (press > -3.0) {
            JCLAP_CHECK(n.soundingAt(end + 4.0, 0) >= 3); // rung on
            JCLAP_CHECK(n.soundingAt(end + 8.5, 0) == 0); // and let go at the bar line after HOLD
         } else
            JCLAP_CHECK(n.soundingAt(end - 1.0, 0) >= 3);
         JCLAP_CHECK(n.open.empty());
         e->fillFrame(f);
         JCLAP_CHECK(f.ended);
      }
   }

   // B06: HOLD CHORD lit over a stop and a start elsewhere places the song where the host starts,
   // pressed while playing as when pressed while stopped.
   {
      int sections[2] = {-1, -1}, forms[2] = {-1, -1};
      for (int whilePlaying = 0; whilePlaying < 2; ++whilePlaying) {
         auto song = sectionsOf("x...x...x...x...", {"C", "D", "E", "F", "G", "A", "B"}, 2);
         for (Section &sec : song->sections) {
            sec.progBars = 2;
            sec.chords[2] = sec.chords[3] = chord("Bb");
         }
         song->setForm(1);
         Settings s = plain();
         s.sync = 1;
         s.song = true;
         s.songEnds = true;
         s.section = kIntro;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         if (!whilePlaying)
            e->toggleHeld(kActHoldChord);
         double b = 0.0;
         for (; b < 6.0; b += perBlock) {
            if (whilePlaying && b >= 2.0 && b < 2.0 + perBlock)
               e->toggleHeld(kActHoldChord);
            block(*e, *out, b, true);
         }
         block(*e, *out, b, false);
         for (b = 18.0; b < 30.0; b += perBlock)
            block(*e, *out, b, true);
         Frame f;
         e->fillFrame(f);
         sections[whilePlaying] = f.section;
         forms[whilePlaying] = f.formPos;
      }
      JCLAP_CHECK(sections[0] == sections[1]);
      JCLAP_CHECK(forms[0] == forms[1]);
      JCLAP_CHECK(forms[1] > 0);
   }

   // B07: PAD CHANNEL moved just before a pad change: the new pad sounds once, on the new channel,
   // on time.
   {
      auto song = std::make_unique<Song>();
      song->key = 0;
      for (Section &sec : song->sections) {
         for (Pattern &t : sec.takes)
            t = fromRhythm("x...x...x...x...");
         sec.fill = sec.takes[0];
         sec.fillDown = sec.ending = Pattern{};
         sec.progBars = 2;
         sec.bars = 8;
         sec.split = 0;
         sec.chords[0] = sec.chords[1] = chord("C");
         sec.chords[2] = sec.chords[3] = chord("G");
      }
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      s.pad = true;
      s.padChannel = 2;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool moved = false;
      double b = 0.0;
      for (; b < 12.0; b += perBlock) {
         if (!moved && b >= 3.95) {
            s.padChannel = 5;
            e->setSettings(s);
            moved = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      for (int i = 0; i < 8; ++i) {
         block(*e, *out, b, false);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(3.9, 12.0, 2) == 0);
      JCLAP_CHECK(n.onsIn(3.99, 4.01, 5) == 4);
      JCLAP_CHECK(n.onsIn(4.01, 7.9, 5) == 0);
      JCLAP_CHECK(n.open.empty());
   }

   // B08: a chord played on the keys a little late, over a rolled chord already begun: the cut
   // notes last 20 ms at least, whatever the tempo.
   for (double tempo : {120.0, 200.0})
      for (int onsets : {1, 2}) {
         auto song = sectionsOf("x...............", {"C"}, 4);
         Settings s = plain();
         s.sync = 1;
         s.chordsFrom = 2;
         s.onsets = onsets;
         s.changeAt = 2;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         bool pressed = false;
         const double pb = kBlock / 48000.0 * tempo / 60.0;
         for (double b = 0.0; b < 8.0; b += pb) {
            if (!pressed && b >= 4.0 + 0.02 * tempo / 60.0) {
               for (int k : {53, 57, 60})
                  e->noteOn(0, k, 100, 0);
               pressed = true;
            }
            block(*e, *out, b, true, 0.0, 0.0, tempo);
            n.add(*out, b, tempo);
         }
         JCLAP_CHECK(n.shortest() * 60.0 / tempo >= 0.0199);
      }

   // B12: a host bar of another meter (a 2/4 bar in 4/4): the chord on the next bar line is struck
   // once.
   {
      auto song = sectionsOf("x...x...x...x...", {"C"}, 4);
      for (Section &sec : song->sections) {
         sec.progBars = 4;
         sec.chords[2] = sec.chords[3] = chord("F");
         sec.chords[4] = sec.chords[5] = chord("G");
         sec.chords[6] = sec.chords[7] = chord("Am");
      }
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 16.0; b += perBlock) {
         double bs, bb;
         if (b < 8.0) {
            bs = std::floor(b / 4.0) * 4.0;
            bb = 4.0;
         } else if (b < 10.0) {
            bs = 8.0;
            bb = 2.0;
         } else {
            bs = 10.0 + std::floor((b - 10.0) / 4.0) * 4.0;
            bb = 4.0;
         }
         block(*e, *out, b, true, bs, bb);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(9.95, 10.1, 0) == 4);
      JCLAP_CHECK(n.shortest() > 0.05);
      JCLAP_CHECK(n.doubled == 0);
   }

   // B13: HOLD pressed while a run is under way to the next chord: the run's notes still to come
   // aren't played, and the one sounding is let go.
   {
      auto song = sectionsOf("x...............", {"C"}, 4);
      for (Section &sec : song->sections) {
         sec.progBars = 2;
         sec.chords[2] = sec.chords[3] = chord("F");
         for (Pattern &t : sec.takes)
            t.at(0).run = 1;
      }
      Settings s = plain();
      s.sync = 1;
      s.runs = 1.0;
      s.runEveryBars = 1;
      s.section = kVerse;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool pressed = false;
      for (double b = 0.0; b < 10.0; b += perBlock) {
         if (!pressed && b >= 6.6) {
            e->toggleHeld(kActHoldChord);
            pressed = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(6.0, 6.6, 0) >= 2);  // the run begun
      JCLAP_CHECK(n.onsIn(6.65, 7.9, 0) == 0); // and stopped
      JCLAP_CHECK(n.soundingAt(7.0, 0) == 0);
      JCLAP_CHECK(n.pcs(7.9, 8.1, 0) == std::set<int>({5, 9, 0})); // the F held
   }

   // B09: HOLD pressed on the block a bar line's chord has begun in: that chord is held, no note
   // of it is cut to a blip, and the panel shows it.
   {
      auto song = sectionsOf("x...............", {"C"}, 4);
      for (Section &sec : song->sections) {
         sec.progBars = 2;
         sec.chords[2] = sec.chords[3] = chord("F");
      }
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      s.onsets = 2; // (a roll: its notes start over the block)
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool pressed = false, shown = true;
      Frame f;
      for (double b = 0.0; b < 12.0; b += perBlock) {
         if (!pressed && b >= 4.0) {
            e->toggleHeld(kActHoldChord);
            pressed = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
         if (pressed && b < 4.5) {
            e->fillFrame(f);
            shown = shown && f.chord == chord("F");
         }
      }
      JCLAP_CHECK(shown);
      JCLAP_CHECK(n.shortest() > 0.06);
      JCLAP_CHECK(n.pcs(7.9, 8.1, 0) == std::set<int>({5, 9, 0})); // the F held over the C
   }

   // E06: a count-in into a short first bar (a 3/4 host, a 4/4 song): nothing is struck at 0 and cut
   // to a blip by the pickup's shift.
   {
      auto song = sectionsOf("x...x...x...x...", {"C", "F", "G", "A", "Bb", "D", "E"}, 4);
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      s.pad = true;
      s.padChannel = 2;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = -4.005; b < 8.0; b += perBlock) {
         const double bs = b < 0.0 ? -3.0 : std::floor(b / 3.0) * 3.0;
         block(*e, *out, b, true, bs, 3.0);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.shortest() > 0.04);
      JCLAP_CHECK(n.doubled == 0);
   }

   // E07: a note struck a moment before a stop sounds 20 ms, its off in a block after a short stop
   // block; nothing is left on.
   {
      auto song = sectionsOf("x...............", {"C"}, 4);
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      double b = 0.0;
      for (; b < 4.002; b += perBlock) { // (the bar line's chord out in the last block)
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      block(*e, *out, b, false, 0.0, 0.0, 120.0, 16); // (a 16-frame stop)
      n.add(*out, b);
      double wall = b + 16 / 24000.0; // (stopped, the host's beat stands still: the time goes on)
      for (int i = 0; i < 8; ++i, wall += perBlock) {
         block(*e, *out, b, false);
         n.add(*out, wall);
      }
      JCLAP_CHECK(n.onsIn(3.99, 4.01, 0) >= 3);
      JCLAP_CHECK(n.shortest() >= 0.0399);
      JCLAP_CHECK(n.open.empty());
   }

   // A06: a phrase's written rest on a bar's one stays a rest: the chord change there isn't struck,
   // and the bar before's chord has let go by then.
   {
      auto song = sectionsOf("x---x---x---x---x---x---x---x---....x---x---x---x---x---x---x---", {"C"}, 4);
      for (Section &sec : song->sections) {
         sec.progBars = 4;
         sec.chords[2] = sec.chords[3] = chord("G/B");
         sec.chords[4] = sec.chords[5] = chord("Am");
         sec.chords[6] = sec.chords[7] = chord("F");
      }
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      s.length = 0.5;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 17.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(7.9, 8.9, 0) == 0);        // bar 3's one left out
      JCLAP_CHECK(n.soundingAt(8.1, 0) == 0);        // and silent
      JCLAP_CHECK(n.pcs(8.9, 9.1, 0).count(9) == 1); // the Am from beat 2
      JCLAP_CHECK(n.onsIn(3.9, 4.1, 0) >= 3);        // (the changes into bars 2 and 4 on their ones)
      JCLAP_CHECK(n.onsIn(11.9, 12.1, 0) >= 3);
   }
}

JCOMP_TEST_NOINLINE static void roundEighteenC() {
   const auto styleCalled = [](const char *name, int &g, int &st) {
      for (g = 0; g < genreCount(); ++g)
         for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            if (std::string(genreAt(g).styles[st].name) == name)
               return true;
      return false;
   };
   const auto chordAt = [](const Pattern &p, int s) {
      return s < p.steps && p.at(s).on() && !leftHand(p.at(s).part) && p.at(s).part != kPartLine;
   };
   // A01, A06: a quiet section's backbeat keeps 2 and 4 (and drops its 1 when 2 and 4 are its
   // strokes), and no even pulse thins to the 1, 2 and 4.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         if (style.mods & kFixed)
            continue;
         for (const Rhythm *rh : rhythmsFor(genreAt(g), style)) {
            const Pattern written = fromRhythm(*rh);
            if (written.inThree())
               continue;
            const int bar = written.stepsPerBar(), beat = written.perBeat();
            for (uint32_t k = 1; k <= 24; ++k) {
               Pattern p = written;
               Rng rng(k * 37u + 5u);
               shapeEnergy(p, 0.06, rng, false, style.mods, false);
               for (int from = 0; from + bar <= p.steps; from += bar) {
                  if (backbeatTake(written) && backbeatStroke(written, from + beat) && backbeatStroke(written, from + 3 * beat))
                     JCLAP_CHECK(chordAt(p, from + beat) && chordAt(p, from + 3 * beat));
                  bool all = true;
                  for (int b = 0; b < 4; ++b)
                     all = all && chordAt(written, from + b * beat);
                  if (all)
                     JCLAP_CHECK(!(chordAt(p, from) && chordAt(p, from + beat) && !chordAt(p, from + 2 * beat) &&
                                   chordAt(p, from + 3 * beat)));
               }
            }
         }
      }
   // A01: an even pulse (Driving Eighths) is no backbeat; a backbeat over a softer 1 and 3 is.
   JCLAP_CHECK(!backbeatTake(fromRhythm("x.x.x.x.x.x.x.x.")));
   JCLAP_CHECK(!backbeatTake(fromRhythm("x...x...x...x...")));
   JCLAP_CHECK(backbeatTake(fromRhythm("o...X...o...X...")));
   // A03, A05: a busy section adds no fill-in to a dotted 3+3+2, nor into a phrase's rest.
   for (uint32_t k = 1; k <= 24; ++k) {
      const Pattern written = fromRhythm("x..x..x.x..x..x.");
      Pattern p = written;
      Rng rng(k * 11u + 3u);
      shapeEnergy(p, 0.9, rng, false, 0, false);
      for (int s = 0; s < p.steps; ++s)
         if (chordAt(p, s))
            JCLAP_CHECK(written.at(s).on());
   }
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         if (style.mods & kFixed)
            continue;
         for (const Rhythm *rh : rhythmsFor(genreAt(g), style)) {
            const Pattern written = fromRhythm(*rh);
            for (uint32_t k = 1; k <= 8; ++k) {
               Pattern p = written;
               Rng rng(k * 53u + 1u);
               shapeEnergy(p, 0.9, rng, false, style.mods, false);
               for (int s = 0; s < p.steps; ++s)
                  if (!written.at(s).on() && phraseRest(written, s))
                     JCLAP_CHECK(!p.at(s).on());
            }
         }
      }
   // A04: a pre-chorus builds from the verse: never below it, at any ENERGY.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
         for (double energy : {0.2, 0.5, 0.9})
            for (uint32_t seed = 1; seed <= 6; ++seed) {
               Recipe r = recipeOf(g, st, seed);
               r.energy = energy;
               auto song = std::make_unique<Song>();
               generateSong(r, *song);
               const double verse = sectionRhythmEnergy(r, kVerse, song->sections[kVerse].seed);
               JCLAP_CHECK(sectionRhythmEnergy(r, kPre, song->sections[kPre].seed, verse) >= verse - 1e-6);
            }
   // C01, C06: thirty adopted children of a fixed figure keep its level: its strokes stay audible,
   // its peak where it was, and a montuno or Freddie Green's four always has a child to give.
   for (const char *name : {"Montuno 2-3", "Montuno 3-2", "Four To The Bar"}) {
      int g, st;
      JCLAP_CHECK(styleCalled(name, g, st));
      for (uint32_t chain = 1; chain <= 2; ++chain) {
         std::vector<double> v = defaultValues();
         for (const auto &[id, x] : styleValues(g, st))
            v[id] = x;
         v[kSeed] = chain;
         const Recipe r = recipeFrom([&](clap_id id) { return v[id]; });
         const VoiceSettings vs = voiceSettings([&](clap_id id) { return v[id]; });
         auto song = std::make_unique<Song>();
         generateSong(r, *song);
         Section sec = song->sections[kVerse];
         const Pattern first = sec.takes[0];
         const int peak = chordPeak(first), level = figureLevel(first);
         Rng pick(chain * 99u + 7u);
         for (uint32_t gen = 0; gen < 30; ++gen) {
            Rng rng(gen * 31u + chain);
            std::vector<std::string> tags;
            const auto litter = breedLitter(sec, 5, v[kMutation], rng, tags, styleOf(r).mods, genreAt(g).name, false,
                                            styleOf(r).bpm, true, false, &vs);
            JCLAP_CHECK(!litter.empty());
            if (litter.empty())
               break;
            adoptChild(r, sec, litter[static_cast<size_t>(pick.range(0, static_cast<int>(litter.size()) - 1))], gen * 13u + 1u,
                       kVerse);
         }
         const Pattern &now = sec.takes[0];
         JCLAP_CHECK(std::abs(chordPeak(now) - peak) <= 5);
         for (int s = 0; s < now.steps; ++s)
            if (chordAt(first, s) && chordAt(now, s))
               JCLAP_CHECK(now.at(s).vel >= 0.75 * level);
      }
   }
   // C04: a player that runs into its changes offers a "run in" child when RUNS is up (Gospel
   // Push's are written in already).
   {
      int g, st;
      JCLAP_CHECK(styleCalled("Western Swing", g, st));
      bool runIn = false;
      for (uint32_t seed = 1; seed <= 12 && !runIn; ++seed) {
         std::vector<double> v = defaultValues();
         for (const auto &[id, x] : styleValues(g, st))
            v[id] = x;
         v[kSeed] = seed;
         v[kRuns] = 0.85;
         const Recipe r = recipeFrom([&](clap_id id) { return v[id]; });
         const VoiceSettings vs = voiceSettings([&](clap_id id) { return v[id]; });
         auto song = std::make_unique<Song>();
         generateSong(r, *song);
         for (int si : {kVerse, kChorus}) {
            Rng rng(seed * 7u + static_cast<uint32_t>(si));
            std::vector<std::string> tags;
            breedLitter(song->sections[si], 6, 0.7, rng, tags, styleOf(r).mods, genreAt(g).name, false, styleOf(r).bpm, true,
                        true, &vs, 0.85, 2);
            for (const std::string &t : tags)
               runIn = runIn || t.find("run in") != std::string::npos;
         }
      }
      JCLAP_CHECK(runIn);
   }
   // C05: a skank's chops are never spared: they are the figure.
   {
      const Pattern skank = fromRhythm("....x.......x...");
      for (int s = 0; s < skank.steps; ++s)
         JCLAP_CHECK(!sparable(skank, s));
   }
   // D07: TRY INSTEAD over a slash chord never offers the same chord with its bass dropped.
   for (int key = 0; key < 12; ++key) {
      Chord v;
      v.root = static_cast<int8_t>((key + 7) % 12);
      v.quality = kDom7;
      v.bass = static_cast<int8_t>((key + 11) % 12);
      Chord home;
      home.root = static_cast<int8_t>(key);
      home.quality = kMaj;
      for (const auto &[c, why] : tryInstead(v, home, key, kMajor))
         JCLAP_CHECK(!(c.root == v.root && c.quality == v.quality && c.bass != v.bass));
   }
   // D08: the ii of a tritone sub in a major key is a minor seventh (Abm7 before Db7 in C), not a
   // half-diminished one.
   {
      Chord sub;
      sub.root = 1;
      sub.quality = kDom7;
      Chord home;
      home.root = 0;
      home.quality = kMaj;
      bool minor7 = false;
      for (const auto &[c, why] : tryInstead(sub, home, 0, kMajor)) {
         JCLAP_CHECK(!(c.root == 8 && c.quality == kHalfDim));
         minor7 = minor7 || (c.root == 8 && c.quality == kMin7);
      }
      JCLAP_CHECK(minor7);
   }
   // A09: Palm-Wine's pinch is the top strings plucked, not the grip strummed.
   {
      int g, st;
      JCLAP_CHECK(styleCalled("Palm-Wine", g, st));
      bool upper = false;
      for (const Rhythm *rh : rhythmsFor(genreAt(g), genreAt(g).styles[st])) {
         const Pattern p = fromRhythm(*rh);
         for (int s = 0; s < p.steps; ++s) {
            upper = upper || (p.at(s).on() && p.at(s).part == kPartUpper);
            JCLAP_CHECK(!(p.at(s).on() && p.at(s).part == kPartChord && s % p.stepsPerBar() == 2));
         }
      }
      JCLAP_CHECK(upper);
   }
}

JCOMP_TEST_NOINLINE static void roundEighteenD() {
   const auto settings = [](int shape, int voices, int lo = 50, int hi = 76, int center = 62) {
      VoiceSettings vs;
      vs.shape = shape;
      vs.voices = voices;
      vs.lo = lo;
      vs.hi = hi;
      vs.center = center;
      return vs;
   };
   const auto has = [](const Voicing &v, int pc) {
      for (int i = 0; i < v.count; ++i)
         if (v.notes[i] % 12 == pc)
            return true;
      return false;
   };
   const auto count = [](const Voicing &v, int pc) {
      int k = 0;
      for (int i = 0; i < v.count; ++i)
         k += v.notes[i] % 12 == pc;
      return k;
   };
   // D01: a 3-7-9 left hand is the key's dominant on I, IV or V: the blues and Mixolydian I9, the
   // Dorian IV9, Db major's V9. A minor key's ii (D F Ab C's D Ab C in C minor) is still m7b5.
   for (int key = 0; key < 12; ++key) {
      const auto shell = [&](int root, int scale) {
         const int b = 48 + (root + 4) % 12;
         return nameChord(Ints{b, b + 6, b + 10}, true, key, scale).root;
      };
      JCLAP_CHECK_EQ(shell(key, kBlues), key);
      JCLAP_CHECK_EQ(shell(key, kMixolydian), key);
      JCLAP_CHECK_EQ(shell((key + 5) % 12, kDorian), (key + 5) % 12);
      JCLAP_CHECK_EQ(shell((key + 7) % 12, kMajor), (key + 7) % 12);
      JCLAP_CHECK_EQ(shell((key + 10) % 12, kMinor), (key + 2) % 12);
   }
   // D02: a slash chord after an inversion keeps its top near, the bass going under the others:
   // F F#dim7 C/G's C/G is G3 C4 E4 G4 in CLOSE, not a leap to C5; and G/A at five has no mud.
   for (int shape : {kClose, kDrop2, kDrop24})
      for (int r = 0; r < 12; ++r) {
         Voicing prev;
         for (const char *name : {"F", "F#dim7", "C/G"}) {
            const Voicing v = voice(chord(name, r), prev.empty() ? nullptr : &prev, settings(shape, 4));
            if (!prev.empty() && name[0] == 'C')
               JCLAP_CHECK(std::abs(v.top() - prev.top()) <= 4);
            prev = v;
         }
      }
   for (int r = 0; r < 12; ++r) {
      const Voicing v = voice(chord("G/A", r), nullptr, settings(kClose, 5));
      JCLAP_CHECK(v.count == 5 && v.notes[0] % 12 == (r + 9) % 12);
      JCLAP_CHECK(v.notes[1] - v.notes[0] >= 7 || v.notes[0] >= 52);
   }
   // D03: OCTAVES' V7 after the iv strokes its third, Mauleon's B3 D4 G4 B4, in every key.
   for (int r = 0; r < 12; ++r) {
      VoiceSettings vs = settings(kOctaves, 4, 55, 84, 68);
      Voicing prev;
      for (int rep = 0; rep < 3; ++rep)
         for (const char *name : {"Cm", "Fm", "G7", "Cm"}) {
            bool third = false;
            const Chord c = chord(name, r);
            for (int k = 0; k < 4; ++k) {
               const Voicing v = octaveLine(c, prev.empty() ? nullptr : &prev, vs, k);
               third = third || (v.notes[0] - c.root + 120) % 12 == 4;
               prev = v;
            }
            if (rep > 0 && name[0] == 'G')
               JCLAP_CHECK(third);
         }
   }
   // D04: 4THS counts a stack once at any octave, and keeps an in-reach stack a fourth short: Am7
   // after D3 G3 C4 E4 is A2 G3 C4 E4, not A3 D4 G4 C5; Dbmaj7 after Ab2 Gb3 C4 F4 is Db3 Ab3 C4 F4.
   for (int r = 0; r < 12; ++r) {
      const auto held = [&](std::initializer_list<int> notes) {
         Voicing v;
         for (int x : notes)
            v.notes[static_cast<size_t>(v.count++)] = static_cast<uint8_t>(x + r);
         return v;
      };
      const Voicing c = held({50, 55, 60, 64}), ab7 = held({44, 54, 60, 65});
      JCLAP_CHECK(std::abs(voice(chord("Am7", r), &c, settings(kQuartal, 4, 43 + r, 86, 62 + r)).top() - c.top()) <= 4);
      JCLAP_CHECK(std::abs(voice(chord("Dbmaj7", r), &ab7, settings(kQuartal, 4, 50 + r, 76, 62 + r)).top() - ab7.top()) <= 4);
   }
   // D05: an augmented chord's #5 leads up, so it isn't doubled at five: Caug is C E G# C E.
   for (int shape : {kClose, kDrop2, kDrop24, kSpread, kRootless})
      for (const char *name : {"Caug", "C7#5"})
         for (int r = 0; r < 12; ++r) {
            const Voicing v = voice(chord(name, r), nullptr, settings(shape, 5));
            JCLAP_CHECK(count(v, (r + 8) % 12) <= 1);
         }
   // D06: GRIP's open slash shapes: C7/E 032310, Fmaj7/A x03210, Fmaj7/G 3x3210.
   {
      VoiceSettings vs = settings(kGrip, 5, 40, 76, 58);
      vs.floor = 40;
      const Voicing c7 = voice(chord("C7/E"), nullptr, vs);
      JCLAP_CHECK(c7.count == 6 && c7.notes[0] == 40 && c7.notes[1] == 48 && c7.notes[2] == 52 &&
                  c7.notes[3] == 58 && c7.notes[4] == 60 && c7.notes[5] == 64);
      const Voicing fa = voice(chord("Fmaj7/A"), nullptr, vs);
      JCLAP_CHECK(fa.count == 5 && fa.notes[0] == 45 && fa.notes[1] == 53 && fa.notes[2] == 57 &&
                  fa.notes[3] == 60 && fa.notes[4] == 64);
      const Voicing fg = voice(chord("Fmaj7/G"), nullptr, vs);
      JCLAP_CHECK(fg.count >= 4 && fg.notes[0] == 43);
   }
   // D09: OCTAVES' maj7 over its third keeps its root: Cmaj7/E at five is C4 E4 G4 B4 C5.
   for (int r = 0; r < 12; ++r)
      JCLAP_CHECK(has(voice(chord("Cmaj7/E", r), nullptr, settings(kOctaves, 5, 55, 84, 68)), r));
   // D10: ROOTLESS keeps a slash chord's root: Dm7/C's F C E alone would be Fmaj7/C.
   for (int r = 0; r < 12; ++r)
      JCLAP_CHECK(has(voice(chord("Dm7/C", r), nullptr, settings(kRootless, 4)), (r + 2) % 12));
   // D11 (as 0.21.0 has it): SPREAD over a major triad's third keeps the triad whole in the right
   // hand, its third included (G/B is B B D G).
   for (int r = 0; r < 12; ++r)
      for (int voices = 4; voices <= 5; ++voices) {
         const Voicing v = voice(chord("G/B", r), nullptr, settings(kSpread, voices));
         JCLAP_CHECK(count(v, (r + 11) % 12) >= 2 && v.count >= 4);
      }
}

JCOMP_TEST_NOINLINE static void roundEighteenE() {
   // A block of `frames` at 120 BPM (48 kHz), following the host (playing); `bar` sets the host's
   // bar on it (a pickup, a count-in, 12/8).
   struct Bar {
      double start = 0.0, beats = 0.0;
      bool twelveEight = false;
   };
   const auto block = [](Engine &e, OutBuffer &out, double beats, bool playing, uint32_t frames = kBlock, Bar bar = Bar{}) {
      Block b;
      b.frames = frames;
      b.sampleRate = 48000.0;
      b.tempo = 120.0;
      b.hasTransport = true;
      b.playing = playing;
      b.beats = beats;
      b.hasBar = bar.beats > 0.0;
      b.barStart = bar.start;
      b.barBeats = bar.beats;
      b.twelveEight = bar.twelveEight;
      out.clear();
      e.process(b, out);
   };
   struct Note {
      double on, off;
      int key, ch;
   };
   struct Notes {
      std::vector<Note> all;
      std::map<std::pair<int, int>, size_t> open;
      int doubled = 0;
      void add(const OutBuffer &out, double beats) {
         for (size_t i = 0; i < out.count; ++i) {
            const MidiOut &m = out.items[i];
            const double at = beats + m.time / 48000.0 * 2.0;
            const auto k = std::make_pair(m.status & 15, static_cast<int>(m.data1));
            if ((m.status & 0xF0) == 0x90 && m.data2 > 0) {
               doubled += open.count(k) > 0;
               open[k] = all.size();
               all.push_back({at, -1.0, m.data1, m.status & 15});
            } else if (open.count(k)) {
               all[open[k]].off = at;
               open.erase(k);
            }
         }
      }
      int onsIn(double from, double to, int ch = -1) const {
         int c = 0;
         for (const Note &n : all)
            c += n.on >= from - 1e-6 && n.on < to && (ch < 0 || n.ch == ch);
         return c;
      }
      double firstIn(double from, double to, int ch = -1) const {
         for (const Note &n : all)
            if (n.on >= from - 1e-6 && n.on < to && (ch < 0 || n.ch == ch))
               return n.on;
         return -1e9;
      }
      std::set<int> pcs(double from, double to, int ch = -1) const {
         std::set<int> s;
         for (const Note &n : all)
            if (n.on >= from - 1e-6 && n.on < to && (ch < 0 || n.ch == ch))
               s.insert(n.key % 12);
         return s;
      }
      std::set<int> soundingAt(double b, int ch = -1) const {
         std::set<int> s;
         for (const Note &n : all)
            if (n.on <= b && (n.off < 0 || n.off > b) && (ch < 0 || n.ch == ch))
               s.insert(n.key % 12);
         return s;
      }
      double shortest(int ch = -1) const {
         double s = 1e9;
         for (const Note &n : all)
            if (n.off >= 0.0 && (ch < 0 || n.ch == ch))
               s = std::min(s, n.off - n.on);
         return s;
      }
   };
   const auto engineFor = [](const Song &song, const Settings &s) {
      auto e = std::make_unique<Engine>();
      e->setSong(song);
      e->setSettings(s);
      e->reset();
      return e;
   };
   // Every section alike, but its chords: `chords` over its bars (a progression of that many).
   const auto sectionsOf = [](const char *rhythm, std::vector<const char *> chords, int bars, int rate = -1) {
      auto r = std::make_unique<Song>();
      r->key = 0;
      const int pb = std::max<int>(1, std::min<int>(static_cast<int>(chords.size()), kMaxBars));
      for (int i = 0; i < kSections; ++i) {
         Section &sec = r->sections[static_cast<size_t>(i)];
         for (Pattern &t : sec.takes)
            t = fromRhythm(rhythm, rate);
         sec.fill = fromRhythm(rhythm, rate);
         sec.fillDown = Pattern{};
         sec.ending = Pattern{};
         sec.progBars = static_cast<uint8_t>(pb);
         sec.bars = static_cast<uint8_t>(bars);
         sec.split = 0;
         for (int b = 0; b < pb; ++b)
            sec.chords[static_cast<size_t>(b * 2)] = sec.chords[static_cast<size_t>(b * 2 + 1)] = chord(chords[static_cast<size_t>(b)]);
      }
      return r;
   };
   const std::set<int> C{0, 4, 7}, F{5, 9, 0}, G{7, 11, 2}, Am{9, 0, 4};
   const auto within = [](const std::set<int> &a, const std::set<int> &b) {
      return !a.empty() && std::includes(b.begin(), b.end(), a.begin(), a.end());
   };
   const double perBlock = kBlock / 24000.0;

   // B01: a held style's ending (one chord on the second-last bar's one, the last bar empty) strikes
   // the song's last chord on the last bar: its empty bar is no phrase's rest.
   {
      auto song = sectionsOf("X-------------------------------", {"G", "C"}, 2);
      song->setForm(kCustomForm, {kChorus, kOutro});
      song->sections[kOutro].ending = fromRhythm("X-------------------------------");
      Settings s = plain();
      s.sync = 1;
      s.song = true;
      s.songEnds = true;
      s.section = kChorus;
      s.longs = true;
      s.fillInto = false;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 20.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(11.95, 12.1, 0) >= 3);       // the last bar's C, struck
      JCLAP_CHECK(within(n.pcs(11.95, 12.1, 0), C));
      JCLAP_CHECK(within(n.soundingAt(14.0, 0), C)); // and rung
   }
   // ... and a take's phrase rest (Ballad Block's third bar, nothing on its one) stays a rest.
   {
      auto song = sectionsOf("x...x...x...x...x...x...x...x.......x...x...x...x...x...x...x...", {"C", "F", "Am", "G"}, 4);
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 16.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(7.95, 8.9, 0) == 0); // the one of bar 3 rests
      JCLAP_CHECK(within(n.pcs(8.95, 9.1, 0), Am));
   }

   // B02: after a 2/4 pickup, bar 1's downbeat is queued by the pickup's last block, on time
   // (with blocks that don't fall on the bar line, at 1024 and 2048 frames).
   for (uint32_t frames : {1024u, 2048u})
      for (double off : {0.02, 0.05}) {
         auto song = sectionsOf("x...x...x...x...", {"C"}, 2);
         Settings s = plain();
         s.sync = 1;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         const double pb = frames / 24000.0;
         for (double b = -4.0 - off; b < 6.0; b += pb) {
            Bar bar;
            bar.beats = b < 0.0 ? 4.0 : b < 2.0 ? 2.0 : 4.0;
            bar.start = b < 0.0 ? std::floor(b / 4.0) * 4.0 : b < 2.0 ? 0.0 : 2.0 + std::floor((b - 2.0) / 4.0) * 4.0;
            block(*e, *out, b, true, frames, bar);
            n.add(*out, b);
         }
         JCLAP_CHECK(n.onsIn(-8.0, 1.99, 0) == 0); // the pickup rests
         JCLAP_CHECK(std::abs(n.firstIn(0.0, 3.0, 0) - 2.0) < 0.002);
      }

   // B03: a start on a pushed step, pad on (with a nudge either way): one pad chord, the push's,
   // and no pad blip.
   for (int nudge : {-10, 0, 10}) {
      auto song = sectionsOf("x...x...x...x.x.", {"C", "F"}, 8);
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes) {
            t.at(14).push = 1;
            t.at(14).nudge = static_cast<int8_t>(nudge);
         }
      Settings s = plain();
      s.sync = 1;
      s.pad = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 3.5; b < 9.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.soundingAt(3.8, s.padChannel) == F);
      JCLAP_CHECK(n.onsIn(3.4, 3.6, s.padChannel) == 4);
      JCLAP_CHECK(n.shortest(s.padChannel) >= 0.04);
   }

   // B04: HOLD pressed after a run's last note, before its landing is heard: the chord heard is
   // held, the landing struck again as it, and nothing of the chord the run went to sounds.
   for (double press : {7.93, 7.96, 7.98}) {
      auto song = sectionsOf("x...x...x.......", {"C", "F", "G", "Am"}, 8);
      for (Section &sec : song->sections) {
         for (Pattern &t : sec.takes)
            t.at(0).run = 1;
         sec.fill = sec.takes[0];
      }
      Settings s = plain();
      s.sync = 1;
      s.runs = 1.0;
      s.runEveryBars = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool done = false;
      for (double b = 0.0; b < 12.0; b += perBlock) {
         if (!done && b >= press) {
            e->toggleHeld(kActHoldChord);
            done = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(6.4, 7.6, 0) >= 2); // the run into G
      JCLAP_CHECK(within(n.pcs(7.95, 12.0, 0), F));
      JCLAP_CHECK(n.onsIn(7.95, 8.1, 0) >= 3); // the landing, as F
   }

   // B05: HOLD pressed just before a change the clock has struck (and just before a push into it):
   // the pad stays on the chord held, no pad of the next chord under it, no fresh attack.
   for (int pushed : {0, 1}) {
      auto song = pushed ? sectionsOf("x...x...x...x.x.", {"C", "F"}, 8) : sectionsOf("x...x...x...x...", {"C", "F", "G", "Am"}, 8);
      if (pushed)
         for (Section &sec : song->sections)
            for (Pattern &t : sec.takes)
               t.at(14).push = 1;
      Settings s = plain();
      s.sync = 1;
      s.pad = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      const double press = pushed ? 3.46 : 7.95, change = pushed ? 3.5 : 8.0;
      bool done = false;
      for (double b = 0.0; b < change + 2.0; b += perBlock) {
         if (!done && b >= press) {
            e->toggleHeld(kActHoldChord);
            done = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      const std::set<int> held = pushed ? C : F;
      JCLAP_CHECK(n.onsIn(press, change + 1.0, s.padChannel) == 0);
      JCLAP_CHECK(n.soundingAt(change + 0.2, s.padChannel) == held);
      JCLAP_CHECK(within(n.pcs(press, change + 1.0, 0), held));
   }
   // ... and chord keys played a moment after the beat: the pad changes with the comp, there.
   {
      auto song = sectionsOf("x...x...x...x...", {"C"}, 8);
      Settings s = plain();
      s.sync = 1;
      s.pad = true;
      s.chordsFrom = 1;
      s.changeAt = 2;
      s.latch = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      int st = 0;
      for (double b = 0.0; b < 8.0; b += perBlock) {
         if (st == 0) {
            for (int k : {60, 64, 67})
               e->noteOn(0, k, 100, 0);
            st = 1;
         } else if (st == 1 && b >= 1.5) {
            for (int k : {60, 64, 67})
               e->noteOff(0, k, 0);
            st = 2;
         } else if (st == 2 && b >= 4.03) {
            for (int k : {65, 69, 72})
               e->noteOn(0, k, 100, 0);
            st = 3;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(within(n.pcs(4.0, 4.2, 0), std::set<int>{0, 4, 5, 7, 9}));
      JCLAP_CHECK(n.soundingAt(4.15, s.padChannel) == F);
   }

   // B06: a take pressed a moment before the bar line SONG was turned on to be placed at (following
   // the host) takes that bar line back and places the song there again: the host's bars play the
   // same entries as with no press.
   {
      std::vector<std::set<int>> bars[2];
      for (int press : {0, 1}) {
         auto song = sectionsOf("x...x...x...x...", {"C", "F", "G", "A", "D", "E", "Bb"}, 4);
         for (int i = 0; i < kSections; ++i)
            song->sections[static_cast<size_t>(i)].chords[0] = song->sections[static_cast<size_t>(i)].chords[1] =
                chord(std::vector<const char *>{"C", "F", "G", "Am", "Dm", "Em", "Bb"}[static_cast<size_t>(i)]);
         song->setForm(kCustomForm, {kIntro, kVerse, kChorus, kBridge, kOutro});
         Settings s = plain();
         s.sync = 1;
         s.section = kVerse;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         int st = 0;
         for (double b = 0.0; b < 48.0; b += perBlock) {
            if (st == 0 && b >= 26.5) {
               s.song = true;
               e->setSettings(s);
               st = 1;
            }
            if (st == 1 && press && b >= 27.95) {
               e->request(kActTake2);
               st = 2;
            }
            block(*e, *out, b, true);
            n.add(*out, b);
         }
         for (int bar = 7; bar < 12; ++bar)
            bars[press].push_back(n.pcs(bar * 4 + 0.01, bar * 4 + 3.9, 0));
      }
      JCLAP_CHECK(bars[0] == bars[1]);
   }

   // B07: in the block after an entry's downbeat has sounded (1024 frames), RESTART restarts that
   // entry and the tile of the entry before queues it, as a moment later does.
   for (int act : {kActRestart, kActIntro}) {
      auto song = sectionsOf("x...x...x...x...", {"C"}, 4);
      for (int i = 0; i < kSections; ++i)
         song->sections[static_cast<size_t>(i)].chords[0] = song->sections[static_cast<size_t>(i)].chords[1] =
             chord(std::vector<const char *>{"C", "F", "G", "Am", "Dm", "Em", "Bb"}[static_cast<size_t>(i)]);
      song->setForm(kCustomForm, {kIntro, kVerse, kChorus, kBridge, kOutro});
      Settings s = plain();
      s.sync = 1;
      s.song = true;
      s.switchAt = 1;
      s.section = kIntro;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool done = false;
      const double pb = 1024 / 24000.0;
      for (double b = 0.02; b < 30.0; b += pb) {
         if (!done && b >= 16.02) {
            JCLAP_CHECK(n.onsIn(15.9, 16.1, 0) > 0); // (the verse's downbeat out)
            e->request(act);
            done = true;
         }
         block(*e, *out, b, true, 1024);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.pcs(16.0 - 0.01, 19.99, 0) == F);
      JCLAP_CHECK(n.pcs(20.0 - 0.01, 23.99, 0) == (act == kActRestart ? F : C));
   }

   // B08: Freddie Green's four rings the song's last chord, which its ending writes to ring.
   {
      auto song = sectionsOf("x...x...x...x...", {"G", "C"}, 2);
      song->setForm(kCustomForm, {kChorus, kOutro});
      Pattern end = fromRhythm("x...x...x...x...x---------------");
      end.at(16).art = kArtRing;
      song->sections[kOutro].ending = end;
      Settings s = plain();
      s.sync = 1;
      s.song = true;
      s.songEnds = true;
      s.section = kChorus;
      s.fixed = s.freddie = s.shorts = true;
      s.fillInto = false;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 20.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(11.95, 12.1, 0) >= 2);
      JCLAP_CHECK(within(n.soundingAt(15.5, 0), C)); // (0.55 of a beat, it was)
      JCLAP_CHECK(n.soundingAt(9.9, 0).empty()); // (the four's own chords still choked)
   }

   // E02: a run goes only into a hit that plays: one its chance leaves out brings no run, and
   // doesn't play.
   {
      auto song = sectionsOf("x...............x...............", {"C", "F"}, 2);
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes) {
            t.at(16).run = 1;
            t.at(16).chance = 0;
         }
      Settings s = plain();
      s.sync = 1;
      s.runs = 1.0;
      s.runEveryBars = 1;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 8.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(0.0, 0.1, 0) >= 3);
      JCLAP_CHECK(n.onsIn(0.1, 7.9, 0) == 0);
   }

   // E03: letting the chord keys go (PLAY ONLY WHILE HELD) a moment after a strum's first string:
   // its strings still to come within a cut's shortest never start, and nothing is a blip.
   for (double after : {0.04, 0.05, 0.06, 0.07}) {
      auto song = sectionsOf("x...x...x...x...", {"C"}, 4);
      Settings s = plain();
      s.sync = 1;
      s.heldOnly = true;
      s.chordsFrom = 1;
      s.onsets = 1;
      s.voicing.voices = 5;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool down = false, up = false;
      const double release = 2.0 + after;
      for (double b = 0.0; b < 4.0; b += perBlock) {
         if (!down) {
            for (int k : {60, 64, 67})
               e->noteOn(0, k, 100, 0);
            down = true;
         }
         if (!up && b + perBlock > release) {
            const uint32_t frame = static_cast<uint32_t>(std::lround((release - b) * 24000.0));
            for (int k : {60, 64, 67})
               e->noteOff(0, k, frame);
            up = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(1.99, 2.01, 0) >= 1);
      JCLAP_CHECK(n.onsIn(release, 4.0, 0) == 0);
      JCLAP_CHECK(n.shortest(0) >= 0.04 - 1e-4); // 20 ms at 120 BPM
      JCLAP_CHECK(n.open.empty());
   }

   // E04: after the song has ended, a section asked for waits for its switch point, and so does
   // the bass's chord (RHYTHM FROM BOTH): no chords from the bass while the take rests.
   {
      auto song = sectionsOf("x...x...x...x...", {"C"}, 2);
      song->setForm(kCustomForm, {kChorus});
      Settings s = plain();
      s.sync = 1;
      s.song = true;
      s.songEnds = true;
      s.section = kChorus;
      s.switchAt = 2;
      s.rhythmFrom = 2;
      s.follow = 0;
      s.fillInto = false;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool asked = false;
      double nextBass = 9.5;
      for (double b = 0.0; b < 24.0; b += perBlock) {
         if (!asked && b >= 9.3) {
            e->request(kActVerse);
            asked = true;
         }
         if (b >= nextBass && nextBass < 15.0) {
            e->noteOn(s.bassChannel, 36, 110, 0);
            e->noteOff(s.bassChannel, 36, 100);
            nextBass += 0.5;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(8.5, 15.9, 0) == 0); // ended, the verse waiting for bar 5's line
      JCLAP_CHECK(n.onsIn(15.9, 16.1, 0) >= 3); // the verse
   }

   // A07: a 12/8 project under a triplet-grid style: its bar is the style's four beats, a dotted
   // quarter each, the bar's first chord on the host's bar line.
   {
      auto song = sectionsOf("x..x..x..x..", {"C", "F"}, 2, kRate8T);
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 0.0; b < 30.0; b += perBlock) {
         Bar bar;
         bar.beats = 6.0;
         bar.start = std::floor(b / 6.0) * 6.0;
         bar.twelveEight = true;
         block(*e, *out, b, true, kBlock, bar);
         n.add(*out, b);
      }
      for (int k = 0; k < 4; ++k) {
         const double line = 6.0 * k;
         JCLAP_CHECK(n.onsIn(line - 0.01, line + 0.02, 0) >= 3);
         JCLAP_CHECK(n.pcs(line - 0.01, line + 0.02, 0) == (k % 2 ? F : C));
         JCLAP_CHECK(n.onsIn(line + 0.02, line + 1.49, 0) == 0); // (the next beat a dotted quarter on)
         JCLAP_CHECK(n.onsIn(line + 1.49, line + 1.52, 0) >= 3);
      }
   }
}

JCOMP_TEST_NOINLINE static void roundNineteenC() {
   const auto styleCalled = [](const char *name, int &g, int &st) {
      for (g = 0; g < genreCount(); ++g)
         for (st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            if (std::string(genreAt(g).styles[st].name) == name)
               return true;
      return false;
   };
   const auto chordAt = [](const Pattern &p, int s) {
      return s >= 0 && s < p.steps && p.at(s).on() && !leftHand(p.at(s).part) && p.at(s).part != kPartLine;
   };
   // A verse of a style, as the plugin makes it.
   struct Made {
      Recipe r;
      VoiceSettings vs;
      std::vector<double> v;
      std::unique_ptr<Song> song;
   };
   const auto made = [&](int g, int st, uint32_t seed) {
      Made m;
      m.v = defaultValues();
      for (const auto &[id, x] : styleValues(g, st))
         m.v[id] = x;
      m.v[kSeed] = seed;
      m.r = recipeFrom([&](clap_id id) { return m.v[id]; });
      m.vs = voiceSettings([&](clap_id id) { return m.v[id]; });
      m.song = std::make_unique<Song>();
      generateSong(m.r, *m.song);
      return m;
   };

   // A01: a quiet take is judged per take, not per bar: a bar written with more chords never comes
   // out with fewer than one written with fewer (one throw at 0.06; the mean of 40 at 0.3).
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         if (style.mods & kFixed)
            continue;
         for (const Rhythm *rh : rhythmsFor(genreAt(g), style)) {
            Rng r0(7);
            Pattern w = fromRhythm(*rh);
            applyMods(w, style.mods, r0, 0.0);
            if (w.bars() < 2)
               continue;
            for (double e : {0.06, 0.3}) {
               const int n = e < 0.15 ? 1 : 40;
               std::vector<double> mean(static_cast<size_t>(w.bars()), 0.0);
               for (int k = 0; k < n; ++k) {
                  Rng rng(static_cast<uint32_t>(k * 37 + 5));
                  Pattern p = w;
                  shapeEnergy(p, e, rng, false, style.mods, swungEighths(genreAt(g), style));
                  for (int b = 0; b < p.bars(); ++b)
                     mean[static_cast<size_t>(b)] += hitsInBar(p, b, true) / double(n);
               }
               for (int a = 0; a < w.bars(); ++a)
                  for (int b = 0; b < w.bars(); ++b)
                     if (hitsInBar(w, a, true) > hitsInBar(w, b, true))
                        JCLAP_CHECK(mean[static_cast<size_t>(a)] + 0.1 >= mean[static_cast<size_t>(b)]);
            }
         }
      }

   // A05a: a quiet backbeat keeps its 2 and 4, not its 1.
   {
      Pattern p = fromRhythm("o...X...o...X...");
      Rng rng(3);
      shapeEnergy(p, 0.06, rng, false, 0, false);
      JCLAP_CHECK(chordAt(p, 4) && chordAt(p, 12));
      JCLAP_CHECK(!chordAt(p, 0));
   }

   // A06: a backbeat leans on 2 and 4 in every bar that strikes a beat: Samba's MMA phrase is none.
   JCLAP_CHECK(!backbeatTake(fromRhythm("x.....x.x...x...|..x.....x...x...|x.....x...x...x.|..x.x.......x...")));
   JCLAP_CHECK(backbeatTake(fromRhythm("o...X...o...X...")));
   JCLAP_CHECK(backbeatTake(fromRhythm("o...X...o...X...|x...x...x...x...")));

   // A07: a rest ends a hold ('-' after '.' holds nothing), and no rhythm writes one so.
   JCLAP_CHECK(fromRhythm("x.--x...").at(0).len == 1);
   JCLAP_CHECK(fromRhythm("x---x...").at(0).len == 4);
   for (int g = 0; g < genreCount(); ++g) {
      for (const Rhythm &r : genreAt(g).rhythms)
         JCLAP_CHECK(std::strstr(r.hits, ".-") == nullptr);
      for (const Rhythm &r : genreAt(g).fills)
         JCLAP_CHECK(std::strstr(r.hits, ".-") == nullptr);
   }

   // A03: a quiet take keeps the left hand's notes on the beats.
   // A04: a busy take's fill-in never lands next to a chord off the eighths.
   // A05b: a dotted or syncopated figure gets no fill-in.
   // A09: a swung take gets one fill-in a bar at most.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         if (style.mods & kFixed)
            continue;
         const bool swung = swungEighths(genreAt(g), style);
         for (const Rhythm *rh : rhythmsFor(genreAt(g), style)) {
            Rng r0(7);
            Pattern w = fromRhythm(*rh);
            applyMods(w, style.mods, r0, 0.0);
            if (hasLine(w))
               continue;
            for (uint32_t k = 1; k <= 6; ++k) {
               Pattern quiet = w;
               Rng rq(k * 13u + 1u);
               shapeEnergy(quiet, 0.06, rq, false, style.mods, swung);
               for (int s = 0; s < w.steps; s += w.perBeat())
                  if (w.at(s).on() && leftHand(w.at(s).part))
                     JCLAP_CHECK(quiet.at(s).on() && leftHand(quiet.at(s).part));
               Pattern busy = w;
               Rng rb(k * 17u + 3u);
               shapeEnergy(busy, 0.95, rb, false, style.mods, swung);
               const int bar = w.stepsPerBar();
               std::vector<int> added(static_cast<size_t>(w.bars()), 0);
               for (int s = 0; s < w.steps; ++s) {
                  if (!chordAt(busy, s) || w.at(s).on())
                     continue;
                  JCLAP_CHECK(!syncopatedTake(w));
                  if (w.rate == kRate16)
                     JCLAP_CHECK(!(chordAt(w, s - 1) && (s - 1) % 2 == 1) && !(chordAt(w, s + 1) && (s + 1) % 2 == 1));
                  ++added[static_cast<size_t>(s / bar)];
               }
               if (swung)
                  for (int a : added)
                     JCLAP_CHECK(a <= 1);
            }
         }
      }

   // A08: a held house ending (kLongs) stays the held chord, not the stab ending.
   // (Covered where the ending is built: endings are generator-side, see A08 in the report.)

   // C02, C03, C13: offbeat motors, skanks and syncopated or dotted figures keep their gaps under
   // every mutation and WEIRD: no child strikes a chord where `fillsGap` says the figure rests.
   // C01: the finish step lifts chords only, never a riff's line.
   // C08: the bar's first sounding chord is never lowered by a ghost laid before it.
   for (int g = 0; g < genreCount(); ++g)
      for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         const Style &style = genreAt(g).styles[st];
         for (uint32_t seed = 1; seed <= 2; ++seed) {
            Made m = made(g, st, seed);
            const Section &sec = m.song->sections[kVerse];
            const Pattern &parent = sec.takes[0];
            Rng rng(seed * 5u + 1u);
            std::vector<std::string> tags;
            const auto litter = breedLitter(sec, 5, m.v[kMutation], rng, tags, style.mods, genreAt(g).name, false,
                                            style.bpm, true, false, &m.vs);
            for (size_t i = 0; i < litter.size(); ++i) {
               const Pattern &child = litter[i];
               for (int s = 0; s < child.steps && s < parent.steps; ++s) {
                  if (chordAt(child, s) && !parent.at(s).on() && !(style.mods & kFixed))
                     JCLAP_CHECK(!fillsGap(parent, s) || child.at(s).push);
                  if (parent.at(s).on() && parent.at(s).part == kPartLine && child.at(s).on() &&
                      child.at(s).part == kPartLine && tags[i].find("accents") == std::string::npos)
                     JCLAP_CHECK(child.at(s).vel <= parent.at(s).vel + 2);
               }
            }
            Pattern w = parent;
            Rng rw(seed * 3u + 2u);
            applyWeird(w, 0.6, rw, style.mods);
            for (int s = 0; s < w.steps; ++s)
               if (chordAt(w, s) && !parent.at(s).on())
                  JCLAP_CHECK(!fillsGap(parent, s));
         }
      }

   // C04: sparser leaves each bar of a take with a line at least one stab.
   {
      const Pattern riff = fromRhythm("1.1.1.1.1.1.x...|1.1.1.1.1.1.x...");
      for (int o = 0; o < riff.stepsPerBar(); ++o)
         JCLAP_CHECK(!sparableColumn(riff, o));
   }

   // C05, C06, C09, C07: takes bred from takes neither fill up, empty out nor sink: over 40
   // adopted generations a take keeps within two columns of its chords and at its level.
   for (const char *name : {"Country Waltz", "Charleston Comp", "Dilla Push", "Two-Step", "Chicago Stabs", "Jersey Push",
                            "Driving Eighths", "New Jack Swing", "Upstroke", "Four To The Bar"}) {
      int g, st;
      JCLAP_CHECK(styleCalled(name, g, st));
      const Style &style = genreAt(g).styles[st];
      for (uint32_t chain = 1; chain <= 2; ++chain) {
         Made m = made(g, st, chain);
         Section sec = m.song->sections[kVerse];
         const Pattern first = sec.takes[0];
         Rng pick(chain * 99u + 7u);
         for (uint32_t gen = 0; gen < 40; ++gen) {
            Rng rng(gen * 31u + chain);
            std::vector<std::string> tags;
            const auto litter = breedLitter(sec, 5, m.v[kMutation], rng, tags, style.mods, genreAt(g).name, false,
                                            style.bpm, true, false, &m.vs);
            JCLAP_CHECK(!litter.empty());
            if (litter.empty())
               break;
            adoptChild(m.r, sec, litter[static_cast<size_t>(pick.range(0, static_cast<int>(litter.size()) - 1))],
                       gen * 13u + 1u, kVerse);
         }
         const Pattern &now = sec.takes[0];
         JCLAP_CHECK(std::abs(chordColumns(now) - chordColumns(first)) <= 2);
         JCLAP_CHECK(std::abs(chordPeak(now) - chordPeak(first)) <= 5);
         int ghosts = 0, chords = 0;
         for (int s = 0; s < now.steps; ++s)
            if (chordAt(now, s)) {
               ++chords;
               ghosts += now.at(s).vel <= kGhost + 8;
            }
         int ghosts0 = 0;
         for (int s = 0; s < first.steps; ++s)
            ghosts0 += chordAt(first, s) && first.at(s).vel <= kGhost + 8;
         JCLAP_CHECK(ghosts - ghosts0 <= 2 * (now.steps / now.stepsPerBar()));
         JCLAP_CHECK(chords >= 1);
      }
   }

   // C05: busier and sparser lean back to the figure a take was written from; that figure is the
   // style's own (the two-step's strum thinned to two chords is still the strum, not the boom-chick).
   {
      int g, st;
      JCLAP_CHECK(styleCalled("Two-Step", g, st));
      const Pattern thinned = fromRhythm("b...x...f...x...");
      const Pattern figure = writtenFigure(genreAt(g).name, thinned, 0.5, genreAt(g).styles[st].mods);
      JCLAP_CHECK(chordAt(figure, 6) && chordAt(figure, 14));
      JCLAP_CHECK(styleCalled("Country Waltz", g, st));
      const Pattern waltz = fromRhythm(Rhythm{"b...x.x.x...|f...x.x.x...", 0.5, 3});
      const Pattern own = writtenFigure(genreAt(g).name, waltz, 0.5, genreAt(g).styles[st].mods);
      JCLAP_CHECK(chordAt(own, 4) && !chordAt(own, 6) && chordAt(own, 8));
   }

   // C12: the Ear hears LENGTH: a breed with LENGTH full or none still gives a litter.
   {
      int g, st;
      JCLAP_CHECK(styleCalled("Pocket Stabs", g, st));
      Made m = made(g, st, 1);
      const Section &sec = m.song->sections[kVerse];
      for (double length : {0.0, 1.0}) {
         Rng rng(11);
         std::vector<std::string> tags;
         const auto litter = breedLitter(sec, 5, m.v[kMutation], rng, tags, genreAt(g).styles[st].mods, genreAt(g).name,
                                         false, genreAt(g).styles[st].bpm, true, false, &m.vs, 0.0, 2, 0.25, length);
         JCLAP_CHECK(!litter.empty());
      }
   }

   // D10: a dominant over its own fifth into a minor chord is never coloured 7#5 (the voicer drops
   // the #5 a half step over the bass); a root-position one still is, now and then.
   {
      Rng a(5), b(5);
      int over = 0, root = 0;
      for (int k = 0; k < 400; ++k) {
         over += minorDominant(a, 0.5, true) == kSharp5;
         root += minorDominant(b, 0.5, false) == kSharp5;
      }
      JCLAP_CHECK(over == 0);
      JCLAP_CHECK(root > 0);
      for (const auto &[x, why] : tryInstead(chord("C7/G"), chord("Fm"), 0, 1))
         JCLAP_CHECK(!(x.quality == kSharp5 && x.bass >= 0));
   }
}

JCOMP_TEST_NOINLINE static void roundNineteenD() {
   // The players' windows, at the engine's SPREAD of 17.
   const auto settings = [](int shape, int voices, int lo = 50, int hi = 76, int center = 62, int floor = 43) {
      VoiceSettings vs;
      vs.shape = shape;
      vs.voices = voices;
      vs.lo = lo;
      vs.hi = hi;
      vs.center = center;
      vs.floor = floor;
      vs.maxSpan = 17;
      return vs;
   };
   const auto has = [](const Voicing &v, int pc, int from = 0) {
      for (int i = from; i < v.count; ++i)
         if (v.notes[static_cast<size_t>(i)] % 12 == pc)
            return true;
      return false;
   };
   const auto count = [](const Voicing &v, int pc) {
      int k = 0;
      for (int i = 0; i < v.count; ++i)
         k += v.notes[static_cast<size_t>(i)] % 12 == pc;
      return k;
   };
   // D01: SPREAD's right hand over a triad's third holds the triad, third and all, as the pop
   // pianist's slash chord does: Piano Ballad's verse, E B/D# C#m A, keeps 0.19's D#3 B3 D#4 F#4
   // and its top moves by step.
   {
      const VoiceSettings vs = settings(kSpread, 4, 48, 76, 62);
      const Voicing e = voice(chord("E"), nullptr, vs);
      const Chord bd = chord("B/D#");
      const Voicing b = voice(bd, &e, vs, -1, nullptr);
      JCLAP_CHECK(b.bottom() % 12 == 3 && has(b, 3, 1) && std::abs(b.top() - e.top()) <= 2);
   }
   for (int r = 0; r < 12; ++r)
      for (int voices = 3; voices <= 5; ++voices)
         for (const char *name : {"G/B", "Gm/Bb"}) {
            const Chord c = chord(name, r);
            const Voicing v = voice(c, nullptr, settings(kSpread, voices, 48, 76, 62));
            JCLAP_CHECK(v.bottom() % 12 == c.bass && has(v, c.bass, 1) && has(v, (r + 7) % 12, 1) &&
                        has(v, (r + 2) % 12, 1));
         }
   // D02: CLOSE at three over a triad's third or fifth is a chop inside an octave, in the guitars'
   // and the clav's windows: G/B is B3 D4 G4, not B3 G4 D5.
   for (int r = 0; r < 12; ++r)
      for (const char *name : {"C/E", "C/G", "Cm/Eb", "Cm/G"})
         for (const VoiceSettings &vs : {settings(kClose, 3, 52, 79, 66), settings(kClose, 3, 55, 76, 66),
                                         settings(kClose, 3, 57, 79, 67)}) {
            const Chord c = chord(name, r);
            const Voicing v = voice(c, nullptr, vs);
            JCLAP_CHECK(v.count == 3 && v.bottom() % 12 == c.bass && v.top() - v.bottom() <= 12 && has(v, r, 1));
         }
   {
      const VoiceSettings vs = settings(kClose, 3, 55, 76, 66);
      const Voicing v = voice(chord("G/B"), nullptr, vs);
      JCLAP_CHECK(v.count == 3 && v.notes[0] == 59 && v.notes[1] == 62 && v.notes[2] == 67);
   }
   // D03: CLOSE at four over a fifth is close over its bass, no hole a sixth wide between the notes
   // over it: C/G in a window from G3 is G3 E4 G4 C5, not G3 C4 C5 E5.
   for (int r = 0; r < 12; ++r)
      for (const char *name : {"C/G", "Cm/G", "Asus/E"})
         for (const VoiceSettings &vs : {settings(kClose, 4, 55, 84, 68), settings(kClose, 4, 52, 79, 66),
                                         settings(kClose, 4, 55, 76, 66), settings(kClose, 4, 57, 79, 67)}) {
            const Voicing v = voice(chord(name, r), nullptr, vs);
            bool hole = false;
            for (int i = 2; i < v.count; ++i)
               hole = hole || v.notes[static_cast<size_t>(i)] - v.notes[static_cast<size_t>(i - 1)] >= 8;
            JCLAP_CHECK(v.count == 4 && !hole && v.top() - v.notes[1] <= 12);
         }
   // D04: ROOTLESS at three over a slash bass keeps its 3rd and 7th: G7/D is D3 F3 B3, Cmaj7/G G3
   // B3 E4. Over a guide tone the root stays beside the other: G7/B is B3 F4 G4.
   struct Guide {
      const char *name;
      int third, seventh; // over the root
   };
   for (int r = 0; r < 12; ++r)
      for (const Guide &g : {Guide{"G7/D", 4, 10}, Guide{"C7/G", 4, 10}, Guide{"Cmaj7/G", 4, 11}, Guide{"Fmaj7/G", 4, 11},
                             Guide{"Dm7/A", 3, 10}, Guide{"G9/D", 4, 10}, Guide{"Bb13/F", 4, 10}}) {
         const Chord c = chord(g.name, r);
         const Voicing v = voice(c, nullptr, settings(kRootless, 3, 50, 72, 60));
         JCLAP_CHECK(v.count == 3 && v.bottom() % 12 == c.bass && has(v, (c.root + g.third) % 12) &&
                     has(v, (c.root + g.seventh) % 12));
      }
   for (int r = 0; r < 12; ++r) {
      const Voicing v = voice(chord("G7/B", r), nullptr, settings(kRootless, 3, 50, 72, 60));
      JCLAP_CHECK(v.bottom() % 12 == (r + 11) % 12 && has(v, (r + 7) % 12) && has(v, (r + 5) % 12));
   }
   // D05: OCTAVES over a slash bass keeps a major seventh's root and leaves the seventh out, so no
   // stroke has the 7 a half step under it: Cmaj7/E is C E G C.
   for (int r = 0; r < 12; ++r)
      for (const char *name : {"Cmaj7/E", "Cmaj7/G", "Cmaj9/E", "Fmaj7/A", "Cm(maj7)/G"})
         for (int voices = 4; voices <= 5; ++voices) {
            const Chord c = chord(name, r);
            const VoiceSettings vs = settings(kOctaves, voices, 55, 84, 68);
            const Voicing v = voice(c, nullptr, vs);
            JCLAP_CHECK(clashes(v, c) <= 0.0 && has(v, c.root % 12));
            Voicing prev = v;
            for (int step = 0; step < 4; ++step) {
               const Voicing s = octaveLine(c, &prev, vs, step);
               JCLAP_CHECK(clashes(s, c) <= 0.0);
               prev = s;
            }
         }
   // D06: DROP 2 and DROP 2+4 never double a 7#5's #5 nor a slash m7b5's b5 over the bass: D7#5/C
   // is C3 Bb3 D4 F#4 D5, its root doubled.
   for (int r = 0; r < 12; ++r)
      for (const char *name : {"D7#5/C", "D7#5/F#", "Em7b5/D", "Am7b5/C", "Bm7b5/A"})
         for (int shape : {kDrop2, kDrop24})
            for (const VoiceSettings &vs : {settings(shape, 5), settings(shape, 5, 48, 79, 62), settings(shape, 5, 50, 72, 60)}) {
               const Chord c = chord(name, r);
               const int altered = (c.root + (name[1] == '7' ? 8 : 6)) % 12; // (the #5 or b5)
               const Voicing v = voice(c, nullptr, vs);
               JCLAP_CHECK(v.bottom() % 12 == c.bass && count(v, altered) == 1);
            }
   // (Nor an augmented or diminished triad's in DROP 2 at four, where the root or third will do:
   // Ebaug in a window from D3 is Eb3 G3 B3 G4, not G3 B3 Eb4 B4; Ddim's F3 D4 F4 Ab4.)
   for (int r = 0; r < 12; ++r)
      for (const char *name : {"Caug", "Cdim"})
         for (int shape : {kDrop2, kDrop24})
            for (const VoiceSettings &vs : {settings(shape, 4), settings(shape, 4, 50, 72, 60), settings(shape, 4, 52, 76, 63)}) {
               const Chord c = chord(name, r);
               const Voicing v = voice(c, nullptr, vs);
               JCLAP_CHECK(count(v, (c.root + (name[1] == 'a' ? 8 : 6)) % 12) == 1);
            }
   {
      const Voicing v = voice(chord("Ddim"), nullptr, settings(kDrop24, 4, 52, 76, 63));
      JCLAP_CHECK(v.count == 4 && v.notes[0] == 53 && v.notes[1] == 62 && v.notes[2] == 65 && v.notes[3] == 68);
   }
   // D07: a slash chord's rebuilt voicings don't keep a fifth voice in more mud than a four: E7/B in
   // DROP 2+4 at five in the jazz window is B2 D3 G#3 E4, not B2 D3 E3 G#3 E4.
   {
      const Voicing v = voice(chord("E7/B"), nullptr, settings(kDrop24, 5, 50, 72, 60));
      JCLAP_CHECK(v.count == 4 && v.notes[0] == 47 && v.notes[1] == 50 && v.notes[2] == 56 && v.notes[3] == 64);
   }
   // D08: a 7#5 in DROP 2 at five, where no whole stack fits the window, doubles its root in the
   // middle (Eb7#5's Eb3 B3 Db4 Eb4 G4) rather than its #5. (Where only its third could double,
   // Db7#5 between D3 and C5, it plays four: F3 B3 Db4 A4.) (An augmented triad at
   // five in CLOSE has no voicing inside SPREAD's 17 without its #5 twice: it stays at four.)
   for (int r = 0; r < 12; ++r) {
      const Chord c = chord("C7#5", r);
      const Voicing v = voice(c, nullptr, settings(kDrop2, 5, 50, 72, 60));
      JCLAP_CHECK(v.count >= 4 && count(v, (r + 8) % 12) == 1);
      const Voicing a = voice(chord("Caug", r), nullptr, settings(kClose, 5, 50, 72, 60));
      JCLAP_CHECK(a.count >= 4 && count(a, (r + 8) % 12) == 1);
   }
   {
      const Voicing v = voice(chord("Eb7#5"), nullptr, settings(kDrop2, 5, 50, 72, 60));
      JCLAP_CHECK(v.count == 5 && v.notes[0] == 51 && v.notes[1] == 59 && v.notes[2] == 61 && v.notes[3] == 63 &&
                  v.notes[4] == 67);
   }
   // D09: an open grip is the chord's own: G11 at five strings is 33323x (G2 C3 F3 A3 D4), not
   // Fmaj7/G's 3x321x without its E.
   for (const VoiceSettings &vs : {settings(kGrip, 5, 48, 79, 64), settings(kGrip, 5, 48, 76, 62),
                                   settings(kGrip, 5, 40, 76, 58, 40)}) {
      const Voicing v = voice(chord("G11"), nullptr, vs);
      JCLAP_CHECK(v.count == 5 && v.notes[0] == 43 && has(v, 2) && has(v, 0));
   }
   {
      // (G2 F3 A3 C4 is 3x321x, cut from Fmaj7/G's 3x3210: an open grip, but not G11's, whose
      // chord has no E for the grip's open string.)
      const Chord g11 = chord("G11"), fmaj7g = chord("Fmaj7/G");
      Voicing g;
      g.count = 4;
      g.notes = {43, 53, 57, 60};
      JCLAP_CHECK(detail::openGrip(g) && detail::openGrip(g, &fmaj7g) && !detail::openGrip(g, &g11));
      g.count = 5;
      g.notes = {43, 53, 57, 60, 64};
      JCLAP_CHECK(detail::openGrip(g) && detail::openGrip(g, &fmaj7g) && !detail::openGrip(g, &g11));
   }
   // D11: three strings name the chord: Faug/G holds its C#, Abm7b5 its F# and Abdim7 its F, not a
   // bare tritone and its octave.
   {
      const VoiceSettings vs = settings(kGrip, 3, 40, 76, 58, 40);
      JCLAP_CHECK(has(voice(chord("Faug/G"), nullptr, vs), 1));
      const Voicing h = voice(chord("Abm7b5"), nullptr, vs);
      JCLAP_CHECK(h.count == 3 && h.bottom() % 12 == 8 && has(h, 6) && has(h, 2));
      const Voicing d = voice(chord("Abdim7"), nullptr, vs);
      JCLAP_CHECK(d.count == 3 && d.bottom() % 12 == 8 && has(d, 5) && has(d, 11));
   }
   // E02: the open grips' lookup is the scan it replaced, cut for cut.
   {
      const auto scan = [](const Voicing &v) {
         for (const auto &shape : detail::kOpenGrips)
            for (bool top : {false, true}) {
               if (top && v.count < 4)
                  continue;
               const Ints cut = detail::gripCut(shape, v.count == 6 ? 5 : v.count, top);
               bool same = static_cast<int>(cut.size()) == v.count;
               for (int i = 0; i < v.count && same; ++i)
                  same = cut[static_cast<size_t>(i)] == v.notes[static_cast<size_t>(i)];
               if (same)
                  return true;
            }
         return false;
      };
      int open = 0;
      for (const auto &shape : detail::kOpenGrips)
         for (int n = 1; n <= 6; ++n)
            for (bool top : {false, true}) {
               const Ints cut = detail::gripCut(shape, n == 6 ? 5 : n, top);
               Voicing v;
               v.count = std::min(static_cast<int>(cut.size()), kMaxVoices);
               for (int i = 0; i < v.count; ++i)
                  v.notes[static_cast<size_t>(i)] = static_cast<uint8_t>(cut[static_cast<size_t>(i)]);
               for (int i = -1; i < v.count; ++i)
                  for (int d : {-1, 0, 1}) {
                     Voicing w = v;
                     if (i >= 0)
                        w.notes[static_cast<size_t>(i)] = static_cast<uint8_t>(w.notes[static_cast<size_t>(i)] + d);
                     JCLAP_CHECK(detail::openGrip(w) == scan(w));
                     open += scan(w);
                  }
            }
      JCLAP_CHECK(open > 0);
   }
}

JCOMP_TEST_NOINLINE static void roundNineteenE() {
   // A block of `frames` (48 kHz), following the host at `tempo`; `bar` sets the host's bar on it
   // (a pickup, 12/8, a 6/8 bar).
   struct Bar {
      double start = 0.0, beats = 0.0;
      bool twelveEight = false;
   };
   const auto block = [](Engine &e, OutBuffer &out, double beats, bool playing, uint32_t frames = kBlock, Bar bar = Bar{},
                         double tempo = 120.0) {
      Block b;
      b.frames = frames;
      b.sampleRate = 48000.0;
      b.tempo = tempo;
      b.hasTransport = true;
      b.playing = playing;
      b.beats = beats;
      b.hasBar = bar.beats > 0.0;
      b.barStart = bar.start;
      b.barBeats = bar.beats;
      b.twelveEight = bar.twelveEight;
      out.clear();
      e.process(b, out);
   };
   struct Note {
      double on, off;
      int key, ch;
   };
   // The notes out, in beats at 120 (or at `spb` samples a beat).
   struct Notes {
      std::vector<Note> all;
      std::map<std::pair<int, int>, size_t> open;
      int doubled = 0;
      double spb = 24000.0;
      void add(const OutBuffer &out, double beats) {
         for (size_t i = 0; i < out.count; ++i) {
            const MidiOut &m = out.items[i];
            const double at = beats + m.time / spb;
            const auto k = std::make_pair(m.status & 15, static_cast<int>(m.data1));
            if ((m.status & 0xF0) == 0x90 && m.data2 > 0) {
               doubled += open.count(k) > 0;
               open[k] = all.size();
               all.push_back({at, -1.0, m.data1, m.status & 15});
            } else if (open.count(k)) {
               all[open[k]].off = at;
               open.erase(k);
            }
         }
      }
      int onsIn(double from, double to, int ch = -1) const {
         int c = 0;
         for (const Note &n : all)
            c += n.on >= from - 1e-6 && n.on < to && (ch < 0 || n.ch == ch);
         return c;
      }
      std::set<int> pcs(double from, double to, int ch = -1) const {
         std::set<int> s;
         for (const Note &n : all)
            if (n.on >= from - 1e-6 && n.on < to && (ch < 0 || n.ch == ch))
               s.insert(n.key % 12);
         return s;
      }
      std::set<int> soundingAt(double b, int ch = -1) const {
         std::set<int> s;
         for (const Note &n : all)
            if (n.on <= b && (n.off < 0 || n.off > b) && (ch < 0 || n.ch == ch))
               s.insert(n.key % 12);
         return s;
      }
      double lastOff(int ch = -1) const {
         double s = -1e9;
         for (const Note &n : all)
            if (ch < 0 || n.ch == ch)
               s = std::max(s, n.off < 0 ? 1e9 : n.off);
         return s;
      }
      std::vector<double> onsets(double from, double to, int ch) const {
         std::vector<double> v;
         for (const Note &n : all)
            if (n.on >= from - 1e-6 && n.on < to && n.ch == ch)
               v.push_back(std::round(n.on * 1000.0) / 1000.0);
         std::sort(v.begin(), v.end());
         v.erase(std::unique(v.begin(), v.end()), v.end());
         return v;
      }
   };
   const auto engineFor = [](const Song &song, const Settings &s) {
      auto e = std::make_unique<Engine>();
      e->setSong(song);
      e->setSettings(s);
      e->reset();
      return e;
   };
   // Every section alike, but its chords: `chords` over its bars (a progression of that many).
   const auto sectionsOf = [](const char *rhythm, std::vector<const char *> chords, int bars, int rate = -1) {
      auto r = std::make_unique<Song>();
      r->key = 0;
      const int pb = std::max<int>(1, std::min<int>(static_cast<int>(chords.size()), kMaxBars));
      for (int i = 0; i < kSections; ++i) {
         Section &sec = r->sections[static_cast<size_t>(i)];
         for (Pattern &t : sec.takes)
            t = fromRhythm(rhythm, rate);
         sec.fill = fromRhythm(rhythm, rate);
         sec.fillDown = Pattern{};
         sec.ending = Pattern{};
         sec.progBars = static_cast<uint8_t>(pb);
         sec.bars = static_cast<uint8_t>(bars);
         sec.split = 0;
         for (int b = 0; b < pb; ++b)
            sec.chords[static_cast<size_t>(b * 2)] = sec.chords[static_cast<size_t>(b * 2 + 1)] = chord(chords[static_cast<size_t>(b)]);
      }
      return r;
   };
   const std::set<int> C{0, 4, 7}, F{5, 9, 0};
   const auto within = [](const std::set<int> &a, const std::set<int> &b) {
      return !a.empty() && std::includes(b.begin(), b.end(), a.begin(), a.end());
   };
   const double perBlock = kBlock / 24000.0;

   // B01: FILL pressed in bar 2's last beat, the last 30 ms included: bar 3 is the fill, every
   // eighth of it.
   for (double press : {7.02, 7.5, 7.8, 7.94}) {
      auto song = sectionsOf("x...............", {"C"}, 8);
      for (Section &sec : song->sections)
         sec.fill = fromRhythm("x.x.x.x.x.x.x.x.");
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      s.fillInto = false;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      auto f = std::make_unique<Frame>();
      Notes n;
      bool done = false, filled = false;
      for (double b = 0.0; b < 14.0; b += perBlock) {
         if (!done && b >= press) {
            e->request(kActFill);
            done = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
         e->fillFrame(*f);
         filled = filled || (b > 8.1 && b < 11.9 && f->fill);
      }
      JCLAP_CHECK(filled);
      for (int k = 0; k < 8; ++k) // the fill's eighths
         JCLAP_CHECK(n.onsIn(7.99 + 0.5 * k, 8.02 + 0.5 * k, 0) >= 3);
      JCLAP_CHECK(n.onsIn(12.1, 13.99, 0) == 0); // and the take again
   }

   // B02: HOLD pressed after a change only the pad has sounded (a skank, off the beats): the
   // chord held is the change's. B05: pressed 70 ms before it, the pad strikes nothing of it.
   for (double press : {4.3, 4.12, 3.86}) {
      auto song = sectionsOf("..x...x...x...x.", {"C", "F"}, 8);
      Settings s = plain();
      s.shorts = true;
      s.sync = 1;
      s.section = kVerse;
      s.pad = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool done = false;
      for (double b = 0.0; b < 16.0; b += perBlock) {
         if (!done && b >= press) {
            e->toggleHeld(kActHoldChord);
            done = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      const std::set<int> &held = press > 4.0 ? F : C;
      JCLAP_CHECK(within(n.pcs(press, 16.0, 0), held));
      JCLAP_CHECK(within(n.soundingAt(10.0, s.padChannel), held));
      if (press < 4.0) {
         JCLAP_CHECK(n.onsIn(3.8, 16.0, s.padChannel) == 0); // (no pad of F for a moment, no fresh pad of C)
      }
   }

   // B03: a 6/8 bar in a 12/8 project: the bar shown never goes back, nor skips one.
   for (double six : {24.0, 18.0}) {
      auto song = sectionsOf("x..x..x..x..", {"C", "F", "C", "F"}, 8, kRate8T);
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      auto f = std::make_unique<Frame>();
      Notes n;
      int was = 0, back = 0, skips = 0;
      for (double b = 0.0; b < 42.0; b += perBlock) {
         Bar bar;
         const bool after = b >= six + 3.0;
         bar.beats = b >= six && !after ? 3.0 : 6.0;
         bar.start = b < six ? std::floor(b / 6.0) * 6.0 : !after ? six : six + 3.0 + std::floor((b - six - 3.0) / 6.0) * 6.0;
         bar.twelveEight = bar.beats == 6.0;
         block(*e, *out, b, true, kBlock, bar);
         n.add(*out, b);
         e->fillFrame(*f);
         back += f->bar < was;
         skips += f->bar > was + 1;
         was = f->bar;
      }
      JCLAP_CHECK(back == 0);
      JCLAP_CHECK(skips == 0);
      JCLAP_CHECK(was >= 5);
      JCLAP_CHECK(n.doubled == 0);
   }

   // B04: RESTART pressed while a placement is pending (SONG turned on, HOLD let go): the verse
   // starts again at the bar line.
   for (int setup : {0, 1, 2}) {
      auto song = sectionsOf("x...x...x...x...", {"C", "F", "C", "F"}, 8);
      song->setForm(kCustomForm, {kVerse, kChorus});
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      s.song = setup != 1;
      s.songEnds = false;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      auto f = std::make_unique<Frame>();
      bool held = false, let = false, on = false, pressed = false;
      int barAfter = -1;
      for (double b = 0.0; b < 14.0; b += perBlock) {
         if (setup == 2 && !held && b >= 6.0) {
            e->toggleHeld(kActHoldChord);
            held = true;
         }
         if (setup == 2 && !let && b >= 10.0) {
            e->toggleHeld(kActHoldChord);
            let = true;
         }
         if (setup == 1 && !on && b >= 10.0) {
            s.song = true;
            e->setSettings(s);
            on = true;
         }
         if (!pressed && b >= 11.7) {
            e->request(kActRestart);
            pressed = true;
         }
         block(*e, *out, b, true);
         e->fillFrame(*f);
         if (b >= 12.3 && barAfter < 0)
            barAfter = f->bar;
      }
      JCLAP_CHECK_EQ(barAfter, 0);
   }

   // B06: NEXT pressed 30 ms before a bar line (taken back: the clock has struck bar 2's F there)
   // or 150 ms before (not): the next section's C, the chord ringing, rings on alike.
   {
      std::vector<double> pads[2];
      int k = 0;
      for (double press : {3.94, 3.7}) {
         auto song = sectionsOf("x...x...x...x...", {"C", "F"}, 8);
         Settings s = plain();
         s.sync = 1;
         s.section = kVerse;
         s.pad = true;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         bool done = false;
         for (double b = 0.0; b < 10.0; b += perBlock) {
            if (!done && b >= press) {
               e->request(kActNext);
               done = true;
            }
            block(*e, *out, b, true);
            n.add(*out, b);
         }
         pads[k++] = n.onsets(0.5, 10.0, s.padChannel);
         JCLAP_CHECK(n.soundingAt(4.5, s.padChannel) == C);
         JCLAP_CHECK(n.onsIn(0.5, 7.9, s.padChannel) == 0);
         JCLAP_CHECK(n.doubled == 0);
      }
      JCLAP_CHECK(pads[0] == pads[1]);
   }

   // B07: a start inside a held chord, just before a hit its chance leaves out: the ringing chord
   // is struck at the start.
   {
      auto song = sectionsOf("X-----------x---", {"C"}, 8);
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes)
            for (int k = 12; k < t.steps; k += 16)
               t.at(k).chance = 0;
      Settings s = plain();
      s.sync = 1;
      s.section = kVerse;
      s.longs = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      for (double b = 2.8; b < 8.0; b += perBlock) {
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(2.79, 2.9, 0) >= 3);
      JCLAP_CHECK(within(n.pcs(2.79, 2.9, 0), C));
   }

   // B08: SAME DICE never, a 2/4 pickup: a start at host bar 6 throws the dice as from the top.
   {
      std::vector<double> ons[2];
      int k = 0;
      for (double from : {0.0, 22.0}) {
         auto song = sectionsOf("x.x.x.x.x.x.x.x.", {"C", "F"}, 8);
         for (Section &sec : song->sections)
            for (Pattern &t : sec.takes)
               for (int j = 0; j < t.steps; ++j)
                  if (t.at(j).on() && j % 16 != 0)
                     t.at(j).chance = 50;
         Settings s = plain();
         s.sync = 1;
         s.section = kVerse;
         s.sameDiceBars = 0;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         for (double b = from; b < 40.0; b += perBlock) {
            Bar bar;
            bar.beats = b < 2.0 ? 2.0 : 4.0;
            bar.start = b < 2.0 ? 0.0 : 2.0 + std::floor((b - 2.0) / 4.0) * 4.0;
            block(*e, *out, b, true, kBlock, bar);
            n.add(*out, b);
         }
         ons[k++] = n.onsets(22.5, 40.0, 0);
      }
      JCLAP_CHECK(!ons[0].empty());
      JCLAP_CHECK(ons[0] == ons[1]);
   }

   // B09, E03: a chord struck just before a stop (Sync AUTO, the free tempo slower than the host's):
   // no note under 20 ms.
   for (int c = 0; c < 4; ++c) {
      const double host = c == 2 ? 160.0 : c == 3 ? 240.0 : 180.0, free = c == 2 ? 80.0 : c == 3 ? 40.0 : 60.0;
      const bool play = c == 1 || c == 2;
      double shortest = 1e9;
      for (int k = 0; k < 120; k += 7) {
         auto song = sectionsOf("x...x...x...x...", {"C", "F"}, 8);
         Settings s = plain();
         s.sync = 0;
         s.play = play;
         s.freeTempo = free;
         s.section = kVerse;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         const double spb = 48000.0 * 60.0 / host;
         n.spb = spb;
         const int64_t stopAt = static_cast<int64_t>(4.0 * spb + k * 4.8);
         for (int64_t at = 0; at < stopAt + 48000; at += 64) {
            Bar bar;
            bar.beats = 4.0;
            const bool playing = at < stopAt;
            const double beats = (playing ? at : stopAt) / spb;
            bar.start = std::floor(beats / 4.0) * 4.0;
            block(*e, *out, beats, playing, 64, bar, host);
            n.add(*out, at / spb);
         }
         for (const Note &x : n.all)
            if (x.off >= 0.0)
               shortest = std::min(shortest, (x.off - x.on) * 60.0 / host * 1000.0);
      }
      JCLAP_CHECK(shortest >= 19.9);
   }

   // E01: HOLD held past the end of SONG and let go there: the song ends at the next bar line,
   // nothing sounding after it (not the push struck into it, held to the next stroke).
   for (double let : {18.3, 19.7, 26.2}) {
      auto song = sectionsOf("X-------x.....X-", {"C", "F"}, 2);
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes)
            for (int k = 14; k < t.steps; k += 16)
               t.at(k).push = 1;
      song->setForm(kCustomForm, {kVerse, kOutro});
      song->sections[kOutro].ending = fromRhythm("X-------------------------------");
      Settings s = plain();
      s.sync = 1;
      s.song = true;
      s.songEnds = true;
      s.section = kVerse;
      s.pad = true;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool held = false, done = false;
      for (double b = 0.0; b < 36.0; b += perBlock) {
         if (!held && b >= 1.0) {
            e->toggleHeld(kActHoldChord);
            held = true;
         }
         if (!done && b >= let) {
            e->toggleHeld(kActHoldChord);
            done = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      const double end = std::ceil(let / 4.0) * 4.0;
      JCLAP_CHECK(n.onsIn(end - 0.01, 40.0) == 0);
      JCLAP_CHECK(n.lastOff() <= end + 0.01);
      JCLAP_CHECK(n.onsIn(end - 4.0, end, 0) > 0); // (held over the song's end, the last bar played)
   }

   // E04: HOLD pressed while stopped, then played, let go and pressed again within a bar: still
   // held, the song where it stays while held (not placed at the host's entry), notes coming.
   for (double let : {10.2, 22.2}) {
      auto song = sectionsOf("x...x...x...x...", {"C", "F"}, 2);
      song->setForm(kCustomForm, {kVerse, kChorus, kVerse, kOutro});
      Settings s = plain();
      s.sync = 1;
      s.song = true;
      s.songEnds = true;
      s.section = kVerse;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      auto f = std::make_unique<Frame>();
      Notes n;
      for (int k = 0; k < 8; ++k)
         block(*e, *out, 0.0, false);
      e->toggleHeld(kActHoldChord);
      bool gone = false, again = false;
      int section = -1, entry = -1;
      for (double b = 0.0; b < let + 6.0; b += perBlock) {
         if (!gone && b >= let) {
            e->fillFrame(*f);
            section = f->section;
            entry = f->formPos;
            e->toggleHeld(kActHoldChord);
            gone = true;
         }
         if (!again && b >= let + 0.6) {
            e->toggleHeld(kActHoldChord);
            again = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      e->fillFrame(*f);
      JCLAP_CHECK(f->hold);
      JCLAP_CHECK(!f->ended);
      JCLAP_CHECK_EQ(f->section, section);
      JCLAP_CHECK_EQ(f->formPos, entry);
      JCLAP_CHECK(n.onsIn(let + 2.0, let + 6.0, 0) >= 3);
      JCLAP_CHECK(within(n.pcs(0.0, let + 6.0, 0), C));
   }

   // E06: Cramer's slip never strikes again a key the chord before still holds.
   {
      int g = 0, st = 0;
      bool found = false;
      for (g = 0; g < genreCount() && !found; ++g)
         for (st = 0; st < static_cast<int>(genreAt(g).styles.size()) && !found; ++st)
            found = std::string(genreAt(g).styles[static_cast<size_t>(st)].name) == "Nashville Slip";
      JCLAP_CHECK(found);
      --g;
      --st;
      int slips = 0, again = 0;
      for (uint32_t seed = 1; seed <= 4; ++seed) {
         Recipe r = recipeOf(g, st, seed);
         auto song = std::make_unique<Song>();
         generateSong(r, *song);
         Settings s = settingsFor(r);
         applyRecipe(s, r);
         s.section = kVerse;
         s.key = song->key;
         s.sync = 1;
         s.loose = 0;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         for (double b = 0.0; b < 64.0; b += perBlock) {
            block(*e, *out, b, true);
            n.add(*out, b);
         }
         for (const Note &x : n.all) {
            const double ms = (x.off - x.on) * 500.0;
            if (x.off < 0.0 || ms < 25.0 || ms > 40.0)
               continue;
            ++slips;
            for (const Note &y : n.all)
               again += &y != &x && y.key == x.key && y.ch == x.ch && std::abs(y.off - x.on) < 1e-6;
         }
      }
      JCLAP_CHECK(slips > 0);
      JCLAP_CHECK_EQ(again, 0);
   }

   // E07: RHYTHM FROM bass, a bass note just before the song's end: the chord it brings is let go
   // at the end.
   for (int follow : {1, 2}) {
      auto song = sectionsOf("x...............", {"C", "F"}, 2);
      song->setForm(kCustomForm, {kVerse, kOutro});
      Settings s = plain();
      s.sync = 1;
      s.song = true;
      s.songEnds = true;
      s.section = kVerse;
      s.rhythmFrom = 1;
      s.bassChannel = 1;
      s.follow = follow;
      auto e = engineFor(*song, s);
      auto out = std::make_unique<OutBuffer>();
      Notes n;
      bool on = false, off = false;
      for (double b = 0.0; b < 24.0; b += perBlock) {
         if (!on && b >= 15.4) {
            e->noteOn(1, 41, 100, 0);
            on = true;
         }
         if (!off && b >= 17.5) {
            e->noteOff(1, 41, 0);
            off = true;
         }
         block(*e, *out, b, true);
         n.add(*out, b);
      }
      JCLAP_CHECK(n.onsIn(0.0, 16.0, 0) > 0);
      JCLAP_CHECK(n.lastOff(0) <= 16.06);
   }

   // C10: DENSITY above the middle adds nothing to a dotted figure (New Jack Swing's 3+3+2), and
   // still fills in a straight one.
   {
      const auto count = [&](const char *rhythm, double density) {
         auto song = sectionsOf(rhythm, {"C", "F"}, 8);
         Settings s = plain();
         s.sync = 1;
         s.section = kVerse;
         s.density = density;
         auto e = engineFor(*song, s);
         auto out = std::make_unique<OutBuffer>();
         Notes n;
         for (double b = 0.0; b < 32.0; b += perBlock) {
            block(*e, *out, b, true);
            n.add(*out, b);
         }
         return n.onsets(0.0, 32.0, 0).size();
      };
      JCLAP_CHECK(count("x..x..x.x..x..x.", 1.0) == count("x..x..x.x..x..x.", 0.5));
      JCLAP_CHECK(count("x..x..x...x..x..", 1.0) == count("x..x..x...x..x..", 0.5));
      JCLAP_CHECK(count("x.......x...x...", 1.0) > count("x.......x...x...", 0.5));
   }
}

int main() {
   // ---- chord names ----
   {
      Chord c;
      JCLAP_CHECK(parseChord("E9", c));
      JCLAP_CHECK_EQ(c.root, 4);
      JCLAP_CHECK_EQ(static_cast<int>(c.quality), static_cast<int>(kDom9));
      JCLAP_CHECK(chordName(c, 4) == "E9");
      JCLAP_CHECK(parseChord("Bbmaj7/D", c));
      JCLAP_CHECK_EQ(c.root, 10);
      JCLAP_CHECK_EQ(c.bass, 2);
      JCLAP_CHECK(chordName(c, 5) == "Bbmaj7/D");
      JCLAP_CHECK(!parseChord("H7", c));
      // A chord named from its notes.
      JCLAP_CHECK(chordName(nameChord(Ints{52, 56, 59, 62}), 4) == "E7");
   }

   // ---- voicings ----
   {
      // Every quality, at every root, in every shape: inside the window, never under the floor, and
      // no interval lower than its limit (Hoffmann 2019; Pease & Pullig).
      VoiceSettings vs;
      int checked = 0;
      for (int shape = 0; shape < kShapeCount; ++shape)
         for (int q = 0; q < qualityCount(); ++q)
            for (int root = 0; root < 12; ++root) {
               vs.shape = shape;
               Chord c;
               c.root = static_cast<int8_t>(root);
               c.quality = static_cast<uint8_t>(q);
               const Voicing v = voice(c, nullptr, vs);
               JCLAP_CHECK(!v.empty());
               if (v.empty())
                  continue;
               JCLAP_CHECK(v.bottom() >= vs.floor);
               JCLAP_CHECK(v.top() <= vs.hi + 2);
               for (int i = 1; i < v.count; ++i) {
                  const int a = v.notes[static_cast<size_t>(i - 1)], b = v.notes[static_cast<size_t>(i)];
                  JCLAP_CHECK(b > a);
                  // (Not a guitar's grip: the open G's B2 over G2 is the open G.)
                  if (b - a < 12 && b - a > 0 && shape != kGrip)
                     JCLAP_CHECK(a >= lowIntervalLimit(b - a) - 2);
               }
               ++checked;
            }
      JCLAP_CHECK(checked > 500);
      // Led voicings move little: a ii-V-I's top voice moves at most TOP MOVE plus a step.
      vs = VoiceSettings{};
      Voicing prev;
      for (const char *name : {"Dm7", "G7", "Cmaj7", "A7", "Dm7", "G7", "Cmaj7"}) {
         const Voicing v = voice(chord(name), prev.empty() ? nullptr : &prev, vs);
         if (!prev.empty())
            JCLAP_CHECK(std::abs(v.top() - prev.top()) <= vs.topMove + 2);
         prev = v;
      }
      // A passing run steps through the scale between the notes, never re-striking the one it leaves.
      const Ints r = passingRun(67, 72, 3, 0, kMajor, false);
      JCLAP_CHECK_EQ(static_cast<int>(r.size()), 2);
      for (int x : r)
         JCLAP_CHECK(x > 67 && x < 72);
   }

   // ---- the library ----
   {
      JCLAP_CHECK(genreCount() >= 20);
      int styles = 0;
      for (int g = 0; g < genreCount(); ++g) {
         const Genre &gen = genreAt(g);
         JCLAP_CHECK(gen.source && *gen.source);
         JCLAP_CHECK(!gen.styles.empty());
         JCLAP_CHECK(!gen.progs.empty());
         // Every chord in every progression parses.
         for (const ProgSet &ps : gen.progs)
            for (const char *text : ps.roles) {
               if (!text)
                  continue;
               Section s;
               writeProgression(s, text, 0);
               for (int b = 0; b < s.progBars; ++b)
                  JCLAP_CHECK(!s.chordAt(b, false).none());
            }
         // Every style makes a song that plays: every section has chords and a take with hits.
         for (int st = 0; st < static_cast<int>(gen.styles.size()); ++st, ++styles) {
            auto songPtr = std::make_unique<Song>();
            Song &song = *songPtr;
            generateSong(recipeOf(g, st, 99), song);
            for (int sec = 0; sec < kSections; ++sec) {
               const Section &s = song.sections[static_cast<size_t>(sec)];
               JCLAP_CHECK(s.progBars >= 1);
               JCLAP_CHECK(s.bars % s.progBars == 0);
               JCLAP_CHECK(!s.chordAt(0, false).none());
               int hits = 0;
               for (int k = 0; k < s.takes[0].steps; ++k)
                  hits += s.takes[0].at(k).on();
               JCLAP_CHECK(hits > 0);
            }
         }
      }
      JCLAP_CHECK(styles >= 60);
   }

   // ---- songs ----
   {
      // The same recipe makes the same song, and it saves and loads unchanged.
      Song a, b;
      generateSong(recipeOf(0, 0, 1234), a);
      generateSong(recipeOf(0, 0, 1234), b);
      JCLAP_CHECK(a.save() == b.save());
      Song c;
      generateSong(recipeOf(0, 0, 1235), c);
      JCLAP_CHECK(a.save() != c.save());
      const std::vector<uint8_t> bytes = a.save();
      Song d;
      JCLAP_CHECK(d.load(bytes.data(), bytes.size()));
      JCLAP_CHECK(d.save() == bytes);
      // A locked section survives a new roll.
      a.sections[kVerse].locked = 1;
      const std::vector<uint8_t> verse = [&] {
         Song one;
         one.sections[0] = a.sections[kVerse];
         return one.save();
      }();
      generateSong(recipeOf(0, 0, 777), a);
      Song one;
      one.sections[0] = a.sections[kVerse];
      JCLAP_CHECK(one.save() == verse);
      // A mutation changes the take, and says how.
      Rng rng(5);
      std::string tag;
      const Pattern child = mutate(b.sections[kChorus].takes[0], 0.5, rng, tag);
      JCLAP_CHECK(!tag.empty());
      bool differs = false;
      for (int k = 0; k < kMaxSteps; ++k)
         differs = differs || child.at(k % std::max<int>(1, child.steps)) != b.sections[kChorus].takes[0].at(k % std::max<int>(1, b.sections[kChorus].takes[0].steps));
      JCLAP_CHECK(differs);
   }

   // ---- the engine ----
   {
      // With the host: chords out, each note with its off, in the window, on channel 1.
      auto songPtr = std::make_unique<Song>();
      Song &song = *songPtr;
      const Recipe r = recipeOf(0, 0, 42);
      generateSong(r, song);
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(song);
      Settings s = settingsFor(r);
      s.key = song.key;
      e.setSettings(s);
      e.reset();
      const std::vector<Timed> out = run(e, 48000 * 8);
      int ons = 0, offs = 0;
      for (const Timed &t : out) {
         JCLAP_CHECK_EQ(t.m.status & 0x0F, 0);
         ons += t.on();
         offs += t.off();
         if (t.on())
            JCLAP_CHECK(t.m.data1 >= s.voicing.floor - 1 && t.m.data1 <= s.voicing.hi + 12);
      }
      JCLAP_CHECK(ons > 12);
      JCLAP_CHECK(offs >= ons - 8);
      // More than one note sounds at once: chords, not single notes.
      std::set<int64_t> times;
      int together = 0;
      for (const Timed &t : out)
         if (t.on() && !times.insert(t.time / 200).second)
            ++together;
      JCLAP_CHECK(together > 6);
   }
   {
      // DENSITY thins or fills the take, LENGTH shortens or lengthens every chord, and LOWEST,
      // HIGHEST and OFFSET bound the voicings as the window moves.
      auto songPtr = std::make_unique<Song>();
      Song &song = *songPtr;
      generateSong(recipeOf(0, 0, 42), song);
      // The window bounds the chords; a left hand's bass notes sit under it, on the bass floor.
      for (Section &sec : song.sections)
         for (Pattern &p : sec.takes)
            for (Hit &h : p.hits)
               if (leftHand(h.part))
                  h = Hit{};
      struct Stats {
         int ons = 0, lo = 127, hi = 0;
         double meanLen = 0;
      };
      auto play = [&](double density, double length, int offset, int lowest = 50, int highest = 76) {
         std::vector<double> v = defaultValues();
         v[kRangeLow] = lowest;
         v[kRangeHigh] = highest;
         v[kOffset] = offset;
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(song);
         Settings s = settingsFor(Recipe{});
         s.voicing = voiceSettings([&](clap_id id) { return v[id]; });
         s.density = density;
         s.length = length;
         s.lengths = 0;
         s.runs = 0;
         s.key = song.key;
         e.setSettings(s);
         e.reset();
         const std::vector<Timed> out = run(e, 48000 * 8);
         Stats st;
         std::map<int, int64_t> started;
         double total = 0;
         int ended = 0;
         for (const Timed &t : out) {
            if (t.on()) {
               ++st.ons;
               st.lo = std::min<int>(st.lo, t.m.data1);
               st.hi = std::max<int>(st.hi, t.m.data1);
               started[t.m.data1] = t.time;
            } else if (t.off() && started.count(t.m.data1)) {
               total += static_cast<double>(t.time - started[t.m.data1]);
               ++ended;
               started.erase(t.m.data1);
            }
         }
         st.meanLen = ended ? total / ended : 0;
         return st;
      };
      const Stats mid = play(0.5, 0.5, 0), thin = play(0.0, 0.5, 0), busy = play(1.0, 0.5, 0);
      JCLAP_CHECK(thin.ons < mid.ons);
      JCLAP_CHECK(thin.ons > 0); // the downbeats stay
      JCLAP_CHECK(busy.ons > mid.ons);
      const Stats shortNotes = play(0.5, 0.0, 0), longNotes = play(0.5, 1.0, 0);
      JCLAP_CHECK(shortNotes.meanLen < mid.meanLen * 0.6);
      JCLAP_CHECK(longNotes.meanLen > mid.meanLen);
      // Thinned and legato: each chord held across the hits that dropped out, to the next one played.
      const Stats sparseLong = play(0.0, 1.0, 0);
      JCLAP_CHECK(sparseLong.meanLen > longNotes.meanLen);
      // The window, and the window moved: every note inside it.
      for (int offset : {-12, -5, 0, 7, 12}) {
         const Stats w = play(0.5, 0.5, offset, 55, 72);
         JCLAP_CHECK(w.ons > 0);
         JCLAP_CHECK(w.lo >= 55 + offset);
         JCLAP_CHECK(w.hi <= 72 + offset);
      }
   }
   {
      // The review's corrections (docs/sources.md, Part E).
      auto genreNamed = [](const char *name) {
         for (int i = 0; i < genreCount(); ++i)
            if (std::string(genreAt(i).name) == name)
               return i;
         return -1;
      };
      // Guajeos and the bossa move in eighths: no hit on a sixteenth between them.
      for (const char *name : {"Salsa", "Latin Jazz", "Bossa Nova"}) {
         const int g = genreNamed(name);
         JCLAP_CHECK(g >= 0);
         for (const Rhythm &rh : genreAt(g).rhythms) {
            int step = 0;
            for (const char *c = rh.hits; *c; ++c) {
               if (*c == '|')
                  continue;
               if (*c != '.' && *c != '-')
                  JCLAP_CHECK(step % 2 == 0);
               ++step;
            }
         }
      }
      // The batida's thumb: twice a bar, on 1 and 3, not on every beat.
      {
         const Rhythm &batida = genreAt(genreNamed("Bossa Nova")).rhythms[0];
         int thumbs = 0;
         for (const char *c = batida.hits; *c; ++c)
            thumbs += *c == 'b' || *c == 'f';
         JCLAP_CHECK(thumbs + 1 == 2); // the thumb on 1 sounds with the chord
      }
      // The reverse Charleston: the and of 1 and beat 3.
      {
         bool found = false;
         for (const Rhythm &rh : genreAt(genreNamed("Swing Jazz")).rhythms)
            found = found || std::string(rh.hits) == "..x.....x.......";
         JCLAP_CHECK(found);
      }
      // Four To The Bar voices Freddie Green's low shells, whatever the player's grips.
      {
         const int g = genreNamed("Swing Jazz");
         int st = -1;
         for (size_t i = 0; i < genreAt(g).styles.size(); ++i)
            if (std::string(genreAt(g).styles[i].name) == "Four To The Bar")
               st = static_cast<int>(i);
         JCLAP_CHECK(st >= 0);
         std::map<clap_id, double> vals;
         for (const auto &[id, x] : styleValues(g, st))
            vals[id] = x;
         JCLAP_CHECK_EQ(static_cast<int>(vals[kShape]), static_cast<int>(kShell));
         JCLAP_CHECK(vals[kRangeLow] <= 45);
         JCLAP_CHECK(vals[kRangeHigh] <= 64);
      }
   }
   {
      // Ambient: long chords, and few of them.
      int g = -1;
      for (int i = 0; i < genreCount(); ++i)
         if (std::string(genreAt(i).name) == "Ambient")
            g = i;
      JCLAP_CHECK(g >= 0);
      for (int st = 0; g >= 0 && st < static_cast<int>(genreAt(g).styles.size()); ++st) {
         auto songPtr = std::make_unique<Song>();
         Song &song = *songPtr;
         const Recipe r = recipeOf(g, st, 7);
         generateSong(r, song);
         std::vector<double> v = defaultValues();
         for (const auto &[id, x] : styleValues(g, st))
            v[id] = x;
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(song);
         Settings s = settingsFor(r);
         s.voicing = voiceSettings([&](clap_id id) { return v[id]; });
         s.key = song.key;
         s.runs = 0;
         e.setSettings(s);
         e.reset();
         const std::vector<Timed> out = run(e, 48000 * 16); // 8 bars at 120
         std::map<int, int64_t> started;
         double total = 0;
         int ended = 0, starts = 0;
         for (const Timed &t : out) {
            if (t.on()) {
               started[t.m.data1] = t.time;
               ++starts;
            } else if (t.off() && started.count(t.m.data1)) {
               total += static_cast<double>(t.time - started[t.m.data1]);
               ++ended;
               started.erase(t.m.data1);
            }
         }
         JCLAP_CHECK(starts > 0);
         // The held styles average over half a bar a chord; the swells (half notes) over a beat and a half.
         JCLAP_CHECK(ended > 0 && total / ended > (st < 2 ? 2.5 : 1.5) * 24000.0);
         JCLAP_CHECK(starts <= 8 * 3 * 5); // at most three chords of five notes a bar
      }
   }
   {
      // Held notes: E G# B D held above the split, with CHORDS FROM held notes, play an E7.
      auto songPtr = std::make_unique<Song>();
      Song &song = *songPtr;
      generateSong(recipeOf(0, 0, 42), song);
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(song);
      Settings s = settingsFor(Recipe{});
      s.chordsFrom = 1;
      s.changeAt = 0;
      e.setSettings(s);
      e.reset();
      JCLAP_CHECK(e.noteOn(0, 64, 100));
      JCLAP_CHECK(e.noteOn(0, 68, 100));
      JCLAP_CHECK(e.noteOn(0, 71, 100));
      JCLAP_CHECK(e.noteOn(0, 74, 100));
      const std::vector<Timed> out = run(e, 48000 * 4);
      std::set<int> pcs;
      for (const Timed &t : out)
         if (t.on())
            pcs.insert(t.m.data1 % 12);
      JCLAP_CHECK(!pcs.empty());
      for (int pc : pcs)
         JCLAP_CHECK(pc == 4 || pc == 8 || pc == 11 || pc == 2 || pc == 6 || pc == 1); // E7 and its 9th/13th
      Frame f;
      e.fillFrame(f);
      JCLAP_CHECK(chordName(f.held, 4) == "E7");
   }
   {
      // A section note (the bridge is the keyboard map's G0) is taken and queues the bridge.
      auto songPtr = std::make_unique<Song>();
      Song &song = *songPtr;
      generateSong(recipeOf(0, 0, 42), song);
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(song);
      Settings s = settingsFor(Recipe{});
      e.setSettings(s);
      e.reset();
      run(e, 48000);
      JCLAP_CHECK(e.noteOn(0, s.noteFor(kActBridge), 100));
      run(e, 48000 * 3);
      Frame f;
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.section, static_cast<int>(kBridge));
      // A note under the split that isn't a section's passes on.
      JCLAP_CHECK(!e.noteOn(0, 40, 100));
   }

   {
      // The second review's corrections.
      auto genreNamed = [](const char *name) {
         for (int i = 0; i < genreCount(); ++i)
            if (std::string(genreAt(i).name) == name)
               return i;
         return -1;
      };
      auto styleNamed = [](int g, const char *name) {
         for (size_t i = 0; i < genreAt(g).styles.size(); ++i)
            if (std::string(genreAt(g).styles[i].name) == name)
               return static_cast<int>(i);
         return -1;
      };
      // Rootless voicings (Levine's A and B): no root, the third or seventh at the bottom.
      {
         VoiceSettings vs;
         vs.shape = kRootless;
         vs.voices = 4;
         vs.lo = 50;
         vs.hi = 72;
         vs.center = 60;
         for (const char *name : {"Dm7", "G7", "Cmaj7", "Am7", "C6", "Bb7"}) {
            const Chord c = chord(name);
            const Voicing v = voice(c, nullptr, vs);
            JCLAP_CHECK_EQ(v.count, 4);
            bool root = false;
            for (int i = 0; i < v.count; ++i)
               root = root || v.notes[static_cast<size_t>(i)] % 12 == c.root;
            JCLAP_CHECK(!root);
            const int low = (v.bottom() - c.root + 12) % 12;
            const bool sixth = c.quality == kSix;
            JCLAP_CHECK(low == 3 || low == 4 || low == 10 || low == 11 || (sixth && low == 9));
         }
         // A dominant's 13th stands for its fifth: G7 is B-E-F-A or F-A-B-E.
         const Voicing g7 = voice(chord("G7"), nullptr, vs);
         bool thirteenth = false;
         for (int i = 0; i < g7.count; ++i)
            thirteenth = thirteenth || g7.notes[static_cast<size_t>(i)] % 12 == 4;
         JCLAP_CHECK(thirteenth);
      }
      // Octaves: the montuno's right hand, its bottom note doubled on top.
      {
         VoiceSettings vs;
         vs.shape = kOctaves;
         vs.voices = 4;
         vs.lo = 55;
         vs.hi = 84;
         vs.center = 68;
         for (const char *name : {"C", "F", "G7", "Cm7"}) {
            const Voicing v = voice(chord(name), nullptr, vs);
            JCLAP_CHECK_EQ(v.count, 4);
            JCLAP_CHECK_EQ(v.top() - v.bottom(), 12);
         }
      }
      // Fourths hold the chord's root, so a diatonic progression's changes are heard.
      {
         VoiceSettings vs;
         vs.shape = kQuartal;
         vs.voices = 5;
         vs.lo = 43;
         vs.hi = 86;
         vs.center = 62;
         const Voicing a = voice(chord("Cadd9"), nullptr, vs);
         const Voicing b = voice(chord("Fmaj9"), &a, vs);
         JCLAP_CHECK(!(a == b));
         for (const auto &[v, root] : {std::pair<Voicing, int>{a, 0}, {b, 5}}) {
            bool has = false;
            for (int i = 0; i < v.count; ++i)
               has = has || v.notes[static_cast<size_t>(i)] % 12 == root;
            JCLAP_CHECK(has);
         }
      }
      // Eighth-note idioms stay on eighths: no busy player's ghost, mutation or stray adds a
      // sixteenth to a montuno, a bossa, or Freddie Green's four.
      for (const auto &[gn, sn] : {std::pair<const char *, const char *>{"Salsa", "Montuno 2-3"}, {"Salsa", "Montuno 3-2"},
                                   {"Latin Jazz", "Latin Jazz Guajeo"}, {"Bossa Nova", "Batida"},
                                   {"Bossa Nova", "Two-Bar Bossa"}, {"Swing Jazz", "Four To The Bar"},
                                   {"Swing Jazz", "Charleston Comp"}}) {
         const int g = genreNamed(gn), st = styleNamed(g, sn);
         JCLAP_CHECK(g >= 0 && st >= 0);
         for (uint32_t seed = 1; seed <= 12; ++seed) {
            Recipe r = recipeOf(g, st, seed);
            r.weird = 0.6;
            auto songPtr = std::make_unique<Song>();
            Song &song = *songPtr;
            generateSong(r, song);
            for (const Section &sec : song.sections)
               for (const Pattern *p : {&sec.takes[0], &sec.takes[1], &sec.takes[2], &sec.takes[3]})
                  for (int s = 0; s < p->steps; ++s)
                     if (p->at(s).on())
                        JCLAP_CHECK(s % 2 == 0);
         }
      }
      // The Latin Jazz guajeo keeps one clave direction (2-3) in every section: its first bar starts on
      // the one, its second off it.
      {
         const int g = genreNamed("Latin Jazz"), st = styleNamed(g, "Latin Jazz Guajeo");
         for (uint32_t seed = 1; seed <= 12; ++seed) {
            auto songPtr = std::make_unique<Song>();
            Song &song = *songPtr;
            generateSong(recipeOf(g, st, seed), song);
            for (int i = kVerse; i <= kBridge; ++i) {
               const Pattern &p = song.sections[static_cast<size_t>(i)].takes[0];
               JCLAP_CHECK(p.steps == 32);
               JCLAP_CHECK(!p.at(16).on() || p.at(16).part != kPartChord);
            }
         }
      }
      // The montuno styles play the guajeo in the verse, not only the clave strokes.
      {
         const int g = genreNamed("Salsa"), st = styleNamed(g, "Montuno 2-3");
         int guajeo = 0;
         for (uint32_t seed = 1; seed <= 20; ++seed) {
            auto songPtr = std::make_unique<Song>();
            Song &song = *songPtr;
            generateSong(recipeOf(g, st, seed), song);
            const Pattern &p = song.sections[kVerse].takes[0];
            guajeo += p.count() >= 8;
         }
         JCLAP_CHECK(guajeo >= 14);
      }
      // Spelled by degree: the flat side for borrowed chords, the key's own letters otherwise.
      JCLAP_CHECK(std::string(pitchName(8, 0)) == "Ab");
      JCLAP_CHECK(std::string(pitchName(10, 0)) == "Bb");
      JCLAP_CHECK(std::string(pitchName(3, 0)) == "Eb");
      JCLAP_CHECK(std::string(pitchName(6, 0)) == "F#");
      JCLAP_CHECK(std::string(pitchName(8, 4)) == "G#");
      JCLAP_CHECK(std::string(pitchName(1, 0)) == "Db");
      JCLAP_CHECK(chordName(chord("Abmaj7"), 0) == "Abmaj7");
      // A run into a chord from outside the key bends to it: into Abmaj7 in C Dorian, no A natural.
      for (int len = 2; len <= 4; ++len)
         for (int from : {60, 72}) {
            const Ints run = passingRun(from, 67, len, 0, kDorian, false, chord("Abmaj7"));
            for (int n : run)
               JCLAP_CHECK(n % 12 != 9);
         }
      // Country boom-chick: the root on 1, the fifth on 3, and the chord on 2 and 4 without the
      // left hand's low note under it.
      {
         const int g = genreNamed("Country"), st = styleNamed(g, "Boom-Chick");
         auto songPtr = std::make_unique<Song>();
         Song &song = *songPtr;
         const Recipe r = recipeOf(g, st, 3);
         generateSong(r, song);
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(song);
         std::vector<double> v = defaultValues();
         for (const auto &[id, x] : styleValues(g, st))
            v[id] = x;
         Settings s = settingsFor(r);
         s.voicing = voiceSettings([&](clap_id id) { return v[id]; });
         s.section = kVerse;
         s.runs = 0.0;
         s.vary = 0.0;
         s.loose = 0.0;
         e.setSettings(s);
         e.reset();
         const std::vector<Timed> out = run(e, 24000 * 16);
         std::map<int, std::vector<int>> beats;
         for (const Timed &t : out)
            if (t.on()) {
               const int beat = static_cast<int>((t.time + 1200) / 24000);
               if (std::abs(t.time - beat * 24000) < 1200)
                  beats[beat].push_back(t.m.data1);
            }
         std::set<int> ones, threes;
         int chords = 0;
         for (const auto &[beat, notes] : beats) {
            const int low = *std::min_element(notes.begin(), notes.end());
            if (beat % 2 == 1) {
               ++chords;
               JCLAP_CHECK(low >= 52);
            } else if (notes.size() == 1)
               (beat % 4 == 0 ? ones : threes).insert(low);
         }
         JCLAP_CHECK(chords >= 4);
         JCLAP_CHECK(!ones.empty() && !threes.empty());
         bool alternates = false;
         for (int a : ones)
            for (int b : threes)
               alternates = alternates || a != b;
         JCLAP_CHECK(alternates);
      }
      // A strum both ways on the triplet grid: the offbeat triplet strums up (highest note first).
      {
         const int g = genreNamed("Ska"), st = styleNamed(g, "Shuffle Ska");
         auto songPtr = std::make_unique<Song>();
         Song &song = *songPtr;
         const Recipe r = recipeOf(g, st, 5);
         generateSong(r, song);
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(song);
         std::vector<double> v = defaultValues();
         for (const auto &[id, x] : styleValues(g, st))
            v[id] = x;
         Settings s = settingsFor(r);
         s.voicing = voiceSettings([&](clap_id id) { return v[id]; });
         s.onsets = 3;
         s.section = kVerse;
         s.runs = 0.0;
         s.vary = 0.0;
         e.setSettings(s);
         e.reset();
         const std::vector<Timed> out = run(e, 24000 * 8);
         // Group the note-ons into strums (within 40 ms) and check each goes high to low.
         int strums = 0, upStrums = 0;
         for (size_t i = 0; i < out.size();) {
            if (!out[i].on()) {
               ++i;
               continue;
            }
            std::vector<const Timed *> g2;
            size_t j = i;
            for (; j < out.size() && out[j].time - out[i].time < 1920; ++j)
               if (out[j].on())
                  g2.push_back(&out[j]);
            if (g2.size() >= 3) {
               ++strums;
               upStrums += g2.front()->m.data1 > g2.back()->m.data1;
            }
            i = j;
         }
         JCLAP_CHECK(strums >= 6);
         JCLAP_CHECK(upStrums * 4 >= strums * 3);
      }
   }

   // ---- the third review's fixes ----
   {
      // A chord that changes between hits still sounds, on time, and the chord before doesn't ring
      // over it: C held through the bar, G from beat 3 with no hit of its own.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("x---------------", {{"C", "G"}}));
      e.setSettings(plain());
      e.reset();
      const std::vector<Timed> out = runWith(e, 24000 * 8, 0.0, [](int64_t, Block &) {});
      const std::set<int> at3 = startingAt(out, 48000);
      JCLAP_CHECK(at3.count(11) && at3.count(2) && !at3.count(4));
      // Every note of the C chord (its E) is off by the change.
      std::map<int, int64_t> on;
      for (const Timed &t : out) {
         if (t.time >= 96000)
            break;
         if (t.on())
            on[t.m.data1] = t.time;
         else if (t.off() && t.m.data1 % 12 == 4 && on.count(t.m.data1) && on[t.m.data1] < 1000)
            JCLAP_CHECK(t.time <= 48000);
      }
   }
   {
      // A hit an eighth before the change, with nothing after it until then, anticipates it.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("x.....x.........", {{"C", "G"}}));
      e.setSettings(plain());
      e.reset();
      const std::vector<Timed> out = runWith(e, 24000 * 4, 0.0, [](int64_t, Block &) {});
      const std::set<int> pushed = startingAt(out, 36000);
      JCLAP_CHECK(pushed.count(11) && !pushed.count(4));
   }
   {
      // Notes come in pairs (an on, then its off, per channel and key) and none is cut to a blip by
      // the other hand: every style, with its feel.
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
            auto songPtr = std::make_unique<Song>();
            Song &song = *songPtr;
            const Recipe r = recipeOf(g, st, 11);
            generateSong(r, song);
            std::vector<double> v = defaultValues();
            for (const auto &[id, x] : styleValues(g, st))
               v[id] = x;
            auto engine = std::make_unique<Engine>();
            Engine &e = *engine;
            e.setSong(song);
            Settings s = settingsFor(r);
            s.voicing = voiceSettings([&](clap_id id) { return v[id]; });
            s.key = song.key;
            s.swing = v[kSwing];
            s.timing = v[kTiming];
            s.onsets = static_cast<int>(v[kOnsets]);
            s.runs = v[kRuns];
            applyRecipe(s, r);
            e.setSettings(s);
            e.reset();
            const std::vector<Timed> out = run(e, 24000 * 16);
            std::map<int, int64_t> sounding;
            int blips = 0, unpaired = 0;
            for (const Timed &t : out) {
               const int k = (t.m.status & 15) * 128 + t.m.data1;
               if (t.on()) {
                  unpaired += sounding.count(k) > 0;
                  sounding[k] = t.time;
               } else if (t.off() && sounding.count(k)) {
                  blips += t.time - sounding[k] < 1200; // 25 ms
                  sounding.erase(k);
               }
            }
            if (blips || unpaired)
               std::printf("%s / %s: %d blips, %d unpaired\n", genreAt(g).name, genreAt(g).styles[static_cast<size_t>(st)].name,
                           blips, unpaired);
            JCLAP_CHECK_EQ(unpaired, 0);
            JCLAP_CHECK_EQ(blips, 0);
         }
   }
   {
      // A two-bar take keeps its phase to the host's bars: started on bar 2 it plays its second bar.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("x...............|..x.............", {{"C", nullptr}}));
      e.setSettings(plain());
      e.reset();
      const std::vector<Timed> out = runWith(e, 24000 * 4, 4.0, [](int64_t, Block &) {});
      JCLAP_CHECK(startingAt(out, 0).empty());
      JCLAP_CHECK(!startingAt(out, 12000).empty());
   }
   {
      // DENSITY adds nothing to a figure played as written, nor a chord on the beat to an offbeat take.
      const auto count = [](const char *rhythm, bool fixed, double density, bool beats) {
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*songOf(rhythm, {{"C", nullptr}}));
         Settings s = plain();
         s.fixed = fixed;
         s.density = density;
         e.setSettings(s);
         e.reset();
         int n = 0;
         for (const Timed &t : run(e, 24000 * 8))
            if (t.on())
               n += !beats || (t.time % 24000 < 600 || t.time % 24000 > 23400);
         return n;
      };
      JCLAP_CHECK_EQ(count("x..x..x...x..x..", true, 1.0, false), count("x..x..x...x..x..", true, 0.5, false));
      // (A figure with room between its strokes: a dotted one keeps its gaps, its rhythm.)
      JCLAP_CHECK(count("x.......x...x...", false, 1.0, false) > count("x.......x...x...", false, 0.5, false));
      JCLAP_CHECK_EQ(count("..x...x...x...x.", false, 1.0, true), 0);
   }
   {
      // The guitar's grip: the bass, its fifth and its octave, the chord above (a power chord with
      // three notes); and a slash chord keeps its bass at the bottom in every shape.
      VoiceSettings vs;
      vs.shape = kGrip;
      vs.voices = 3;
      vs.lo = 40;
      vs.hi = 64;
      vs.floor = 40;
      const Voicing g = voice(chord("E"), nullptr, vs);
      JCLAP_CHECK(g.count == 3 && g.notes[0] % 12 == 4 && g.notes[1] - g.notes[0] == 7 && g.notes[2] - g.notes[0] == 12);
      vs.voices = 5;
      const Voicing g5 = voice(chord("E"), nullptr, vs);
      JCLAP_CHECK(g5.count >= 4 && g5.notes[1] - g5.notes[0] == 7);
      for (int shape : {kClose, kDrop2, kDrop24, kQuartal, kGrip}) {
         VoiceSettings w;
         w.shape = shape;
         const Voicing v = voice(chord("C/E"), nullptr, w);
         JCLAP_CHECK(!v.empty() && v.notes[0] % 12 == 4);
      }
   }
   {
      // A passing run moves by step (three semitones at most) from its first note to the chord.
      for (int from = 55; from < 80; from += 3)
         for (int target : {60, 64, 67, 71})
            for (int len = 2; len <= 4; ++len)
               for (bool half : {false, true}) {
                  const Ints r = passingRun(from, target, len, 0, kMajor, half, chord("C"));
                  for (size_t k = 1; k < r.size(); ++k)
                     JCLAP_CHECK(std::abs(r[k] - r[k - 1]) <= 3 || std::abs(from - target) > 3 * len + 3);
               }
   }
   {
      // LATCH keeps the chord as its keys are let go one at a time, and after.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("x...x...x...x...", {{"F", nullptr}}));
      Settings s = plain();
      s.chordsFrom = 1;
      s.changeAt = 0;
      e.setSettings(s);
      e.reset();
      for (int k : {60, 64, 67})
         e.noteOn(0, k, 100);
      JCLAP_CHECK(chordName(e.heldChord(), 0) == "C");
      e.noteOff(0, 67);
      e.noteOff(0, 64);
      JCLAP_CHECK(chordName(e.heldChord(), 0) == "C");
      e.noteOff(0, 60);
      JCLAP_CHECK(chordName(e.heldChord(), 0) == "C");
      // A note-off for every key (or All Notes Off) lets go of what's held: without LATCH, no chord.
      s.latch = false;
      e.setSettings(s);
      for (int k : {62, 65, 69})
         e.noteOn(0, k, 100);
      JCLAP_CHECK(chordName(e.heldChord(), 0) == "Dm");
      e.releaseAll();
      JCLAP_CHECK(e.heldChord().none());
   }
   {
      // A chord played on the keys on the beat, a moment late or early, takes that beat.
      for (int64_t late : {-480, 0, 240, 1200}) {
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*songOf("x...x...x...x...", {{"C", nullptr}}));
         Settings s = plain();
         s.chordsFrom = 1;
         s.changeAt = 2;
         e.setSettings(s);
         e.reset();
         const int64_t when = 48000 + late;
         bool first = false, second = false;
         const std::vector<Timed> out = runWith(e, 24000 * 4, 0.0, [&](int64_t at, Block &) {
            if (!first) {
               for (int k : {60, 64, 67})
                  e.noteOn(0, k, 100, 0);
               first = true;
            }
            if (!second && at + static_cast<int64_t>(kBlock) > when) {
               for (int k : {60, 64, 67})
                  e.noteOff(0, k, 0);
               for (int k : {65, 69, 72})
                  e.noteOn(0, k, 100, static_cast<uint32_t>(std::max<int64_t>(0, when - at)));
               second = true;
            }
         });
         const std::set<int> at3 = startingAt(out, 48000);
         // On time or early: the beat plays the new chord. A moment late: the new chord comes in
         // where it was played, and the old one lets go.
         JCLAP_CHECK(at3.count(5));
         if (late <= 0)
            JCLAP_CHECK(!at3.count(4));
         std::map<int, int64_t> on;
         for (const Timed &t : out)
            if (t.on())
               on[t.m.data1] = t.time;
            else if (t.off() && t.m.data1 == 64 && on.count(64) && on[64] < 48000 + 1440 && on[64] > 48000 - 1440)
               // (No sooner than the shortest cut, 20 ms, after it began.)
               JCLAP_CHECK(t.time <= std::max<int64_t>({48000, when, on[64] + 960}) + 300);
      }
   }
   {
      // PLAY ONLY WHILE A CHORD IS HELD works with the progression's chords too.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("x...x...x...x...", {{"C", nullptr}}));
      Settings s = plain();
      s.heldOnly = true;
      e.setSettings(s);
      e.reset();
      JCLAP_CHECK(run(e, 24000 * 2).empty());
      JCLAP_CHECK(!run(e, 24000 * 2, {72}).empty());
   }
   {
      // A host loop: every pass starts with its downbeat chord, held its length.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("x---............", {{"C", nullptr}}));
      e.setSettings(plain());
      e.reset();
      const std::vector<Timed> out = runWith(e, 24000 * 16, 0.0, [](int64_t, Block &b) {
         b.loopActive = true;
         b.loopStart = 0.0;
         b.loopEnd = 4.0;
         if (b.beats >= 4.0 - 1e-9)
            b.beats -= 4.0;
      });
      for (int pass = 0; pass < 4; ++pass) {
         JCLAP_CHECK_EQ(static_cast<int>(startingAt(out, pass * 96000).size()), 3); // C, E, G
         std::map<int, int64_t> on;
         for (const Timed &t : out) {
            if (t.time < pass * 96000 || t.time >= (pass + 1) * 96000)
               continue;
            if (t.on())
               on[t.m.data1] = t.time;
            else if (t.off() && on.count(t.m.data1) && on[t.m.data1] - pass * 96000 < 1440)
               JCLAP_CHECK(t.time - on[t.m.data1] > 18000);
         }
      }
   }
   {
      // Notes let go on the channel they were played on, when CHANNEL changes while they sound.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("x---------------", {{"C", nullptr}}));
      Settings s = plain();
      e.setSettings(s);
      e.reset();
      std::vector<Timed> out = runWith(e, 24000 * 8, 0.0, [&](int64_t at, Block &) {
         if (at == 24064) {
            s.channel = 4;
            e.setSettings(s);
         }
      });
      std::map<int, int> sounding;
      for (const Timed &t : out) {
         const int k = (t.m.status & 15) * 128 + t.m.data1;
         sounding[k] += t.on() ? 1 : t.off() ? -1 : 0;
      }
      for (const auto &[k, n] : sounding)
         JCLAP_CHECK(n == 0 || n == 1); // the last chord may still sound
      bool five = false;
      for (const Timed &t : out)
         five = five || (t.on() && (t.m.status & 15) == 4);
      JCLAP_CHECK(five);
   }
   {
      // SWITCH AT Next Beat keeps the bar line: the new section's first bar is the bar it comes in.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      auto songPtr = songOf("x...............", {{"C", nullptr}});
      Song &song = *songPtr;
      for (Pattern &t : song.sections[kVerse].takes)
         t = fromRhythm("x.......x.......");
      for (int b = 0; b < 2; ++b)
         song.sections[kVerse].chords[static_cast<size_t>(b)] = chord("A");
      e.setSong(song);
      Settings s = plain();
      s.switchAt = 0;
      e.setSettings(s);
      e.reset();
      const std::vector<Timed> out = runWith(e, 24000 * 12, 0.0, [&](int64_t at, Block &) {
         if (at == 120064)
            e.request(kVerse);
      });
      int verse = 0;
      for (const Timed &t : out)
         if (t.on() && t.m.data1 % 12 == 1) { // the A chord's C#
            ++verse;
            JCLAP_CHECK(t.time % 48000 < 1440 || t.time % 48000 > 48000 - 1440);
         }
      JCLAP_CHECK(verse >= 2);
   }
   {
      // A line: one note a hit, the degree of the chord it names.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("1...5...8...3...", {{"C", nullptr}}));
      e.setSettings(plain());
      e.reset();
      std::vector<int> notes;
      for (const Timed &t : run(e, 24000 * 4))
         if (t.on())
            notes.push_back(t.m.data1);
      JCLAP_CHECK(notes.size() >= 4);
      if (notes.size() >= 4) {
         JCLAP_CHECK(notes[0] % 12 == 0 && notes[1] - notes[0] == 7 && notes[2] - notes[0] == 12 && notes[3] - notes[0] == 4);
      }
   }
   {
      // OCTAVES moves its octave line through the chord, not one block repeated.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("x.x.x.x.x.x.x.x.", {{"C", nullptr}}));
      Settings s = plain();
      s.voicing.shape = kOctaves;
      s.voicing.voices = 4;
      e.setSettings(s);
      e.reset();
      std::set<int> tops;
      int64_t lastAt = -10000;
      for (const Timed &t : run(e, 24000 * 4))
         if (t.on()) {
            if (t.time - lastAt > 1000)
               tops.insert(t.m.data1 % 12);
            lastAt = t.time;
         }
      JCLAP_CHECK(tops.size() >= 2);
   }
   {
      // The slip note: a grace note a whole step under an inner note of the chord, just before it,
      // now and then on a new chord, never on a chord struck again.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("X...X...X...X...", {{"C", "F"}, {"G", "Am"}}));
      Settings s = plain();
      s.slip = true;
      e.setSettings(s);
      e.reset();
      const std::vector<Timed> out = run(e, 24000 * 64);
      int slips = 0, onRepeat = 0;
      for (const Timed &t : out)
         if (t.on() && t.time % 12000 > 10000) {
            for (const Timed &u : out)
               if (u.on() && u.m.data1 == t.m.data1 + 2 && u.time > t.time && u.time - t.time < 2400) {
                  ++slips;
                  onRepeat += (u.time + 2400) / 24000 % 2 == 1; // beats 2 and 4 repeat the chord
               }
         }
      JCLAP_CHECK(slips >= 4);
      JCLAP_CHECK(slips <= 20);
      JCLAP_CHECK_EQ(onRepeat, 0);
   }
   {
      // Nothing allocated while playing: held chords, runs, the bass coming in.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      auto songPtr = std::make_unique<Song>();
      Song &song = *songPtr;
      generateSong(recipeOf(0, 0, 3), song);
      e.setSong(song);
      Settings s = settingsFor(Recipe{});
      s.chordsFrom = 2;
      s.runs = 1.0;
      s.density = 0.9;
      e.setSettings(s);
      e.reset();
      gAllocs = 0;
      runWith(e, 24000 * 16, 0.0, [&](int64_t at, Block &) {
         gCount = true;
         if (at % (24000 * 2) < kBlock) {
            const bool on = (at / (24000 * 2)) % 2 == 0;
            for (int k : {62, 65, 69, 72})
               on ? (void)e.noteOn(0, k, 100, 3) : (void)e.noteOff(0, k, 3);
         }
         gCount = false;
      });
      JCLAP_CHECK_EQ(gAllocs, 0);
   }

   // ---- the fourth review's corrections ----
   {
      auto genreNamed = [](const char *name) {
         for (int i = 0; i < genreCount(); ++i)
            if (std::string(genreAt(i).name) == name)
               return i;
         return -1;
      };
      auto styleNamed = [](int g, const char *name) {
         const Genre &gen = genreAt(g);
         for (size_t i = 0; i < gen.styles.size(); ++i)
            if (std::string(gen.styles[i].name) == name)
               return static_cast<int>(i);
         return -1;
      };
      const auto made = [](const Recipe &r) {
         auto s = std::make_unique<Song>();
         generateSong(r, *s);
         return s;
      };
      // SONG mode: a push at a section's end plays the next section's chord, and a run into it lands
      // on that chord, which then keeps sounding as itself.
      {
         auto song = songOf("x.............x.", {{"C", nullptr}});
         Section &next = song->sections[1];
         for (int b = 0; b < 4; ++b)
            next.chords[static_cast<size_t>(b * 2)] = next.chords[static_cast<size_t>(b * 2 + 1)] = chord("F");
         next.progBars = 1;
         for (Pattern &t : song->sections[0].takes)
            for (int k = 14; k < t.steps; k += 16)
               t.at(k).push = 1;
         song->sections[0].fill = song->sections[0].takes[0];
         song->sections[0].bars = 2;
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.song = true;
         s.fillInto = false;
         e.setSettings(s);
         e.reset();
         const auto out = runWith(e, 24000 * 12, 0.0, [](int64_t, Block &) {});
         // The and of 4 of the verse's last bar (beat 7.5).
         const std::set<int> push = startingAt(out, 24000 * 7 + 12000);
         JCLAP_CHECK(push.count(9) == 1 && push.count(4) == 0);
      }
      {
         auto song = songOf("x...x...x...x...", {{"C", nullptr}, {"F", nullptr}});
         Section &next = song->sections[1];
         for (int b = 0; b < 2; ++b)
            next.chords[static_cast<size_t>(b * 2)] = next.chords[static_cast<size_t>(b * 2 + 1)] = chord("Ab");
         for (Pattern &t : next.takes)
            t.at(0).run = 1;
         for (Pattern &t : song->sections[0].takes)
            t.at(0).run = 1;
         song->sections[0].bars = 2;
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.song = true;
         s.fillInto = false;
         s.runs = 1.0;
         s.runEveryBars = 1;
         e.setSettings(s);
         e.reset();
         const auto out = runWith(e, 24000 * 16, 0.0, [](int64_t, Block &) {});
         for (int beat = 8; beat < 16; ++beat)
            for (int pc : startingAt(out, 24000 * beat))
               JCLAP_CHECK(pc == 8 || pc == 0 || pc == 3);
      }
      // A run doesn't take the place of a chord: in a bar split C | G with the G's only hit on the
      // and of 3, the G is struck.
      {
         auto song = songOf("x.........x.....", {{"C", "G"}, {"Am", nullptr}});
         for (Section &sec : song->sections)
            for (Pattern &t : sec.takes)
               t.at(0).run = 1;
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.runs = 1.0;
         s.runEveryBars = 1;
         e.setSettings(s);
         e.reset();
         const auto out = runWith(e, 24000 * 8, 0.0, [](int64_t, Block &) {});
         const std::set<int> g = startingAt(out, 24000 * 2 + 12000);
         JCLAP_CHECK(g.count(7) == 1 && g.count(11) == 1);
      }
      // 4THS: different chords, different stacks; a seventh chord's seventh in it; no natural 9th
      // against a b9.
      {
         VoiceSettings q;
         q.shape = kQuartal;
         q.voices = 5;
         q.lo = 52;
         q.hi = 84;
         q.center = 66;
         for (const char *name : {"C7", "Cmaj7", "Cm7", "B7sus", "C7#9", "Fmaj7"}) {
            const Chord c = chord(name);
            const Voicing v = voice(c, nullptr, q);
            JCLAP_CHECK(!v.empty());
            const auto &t = qualityOf(c).tones;
            int seventh = -1;
            for (int i : {10, 11})
               if (std::find(t.begin(), t.end(), i) != t.end())
                  seventh = (c.root + i) % 12;
            bool has = seventh < 0;
            for (int i = 0; i < v.count; ++i)
               has = has || v.notes[static_cast<size_t>(i)] % 12 == seventh;
            JCLAP_CHECK(has);
         }
         const Voicing b9 = voice(chord("C7b9"), nullptr, q);
         for (int i = 0; i < b9.count; ++i)
            JCLAP_CHECK(b9.notes[static_cast<size_t>(i)] % 12 != 2);
         Voicing prev;
         Chord prevChord;
         std::vector<std::vector<int>> seen;
         for (const char *name : {"Eadd9", "C#m9", "B7sus", "Amaj9"}) {
            const Chord c = chord(name);
            const Voicing v = voice(c, prev.empty() ? nullptr : &prev, q, -1, prevChord.none() ? nullptr : &prevChord);
            std::vector<int> notes(v.notes.begin(), v.notes.begin() + v.count);
            JCLAP_CHECK(std::find(seen.begin(), seen.end(), notes) == seen.end());
            seen.push_back(notes);
            prev = v;
            prevChord = c;
         }
      }
      // DENSITY: every bar keeps a chord at 0, and the knob thins steadily.
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
            const Recipe r = recipeOf(g, st, 5);
            auto song = made(r);
            auto engine = std::make_unique<Engine>();
            Engine &e = *engine;
            e.setSong(*song);
            std::vector<double> sv = defaultValues();
            for (const auto &[id, x] : styleValues(g, st))
               sv[id] = x;
            Settings s = settingsFor(r);
            s.voicing = voiceSettings([&](clap_id id) { return sv[id]; });
            applyRecipe(s, r);
            s.section = kVerse;
            s.density = 0.0;
            s.autoFillBars = 0;
            s.key = song->key;
            e.setSettings(s);
            e.reset();
            const auto out = runWith(e, 24000 * 16, 0.0, [](int64_t, Block &) {});
            // A line style (one note at a time) keeps its line; a chord held over two bars has
            // nothing new in the second.
            const bool line = hasLine(song->sections[kVerse].takes[0]);
            bool before = false;
            for (int bar = 0; bar < 4; ++bar) {
               std::map<int64_t, int> at;
               for (const Timed &t : out)
                  if (t.on() && t.time >= bar * 96000 - 2400 && t.time < (bar + 1) * 96000 - 2400)
                     ++at[t.time / 960];
               int most = 0;
               for (const auto &[k, n] : at)
                  most = std::max(most, n);
               if (line && at.size() >= 3)
                  most = 2;
               if (at.empty() && before)
                  most = 2;
               before = !at.empty();
               if (most < 2) {
                  std::printf("no chord: %s / %s bar %d:", genreAt(g).name, genreAt(g).styles[st].name, bar + 1);
                  for (const Timed &t : out)
                     if (t.on() && t.time >= bar * 96000 && t.time < (bar + 1) * 96000)
                        std::printf(" %lld:%d", static_cast<long long>(t.time), t.m.data1);
                  std::printf("\n");
               }
               JCLAP_CHECK(most >= 2);
            }
         }
      {
         const int g = genreNamed("Funk"), st = styleNamed(g, "Pocket Stabs");
         const Recipe r = recipeOf(g, st, 1);
         auto song = made(r);
         int last = 1 << 30;
         for (double d : {0.5, 0.3, 0.1}) {
            auto engine = std::make_unique<Engine>();
            Engine &e = *engine;
            e.setSong(*song);
            Settings s = settingsFor(r);
            s.section = kVerse;
            s.density = d;
            s.autoFillBars = 0;
            s.vary = 0;
            e.setSettings(s);
            e.reset();
            const auto out = runWith(e, 24000 * 32, 0.0, [](int64_t, Block &) {});
            int ons = 0;
            for (const Timed &t : out)
               ons += t.on();
            JCLAP_CHECK(ons < last);
            last = ons;
         }
      }
      // A montuno chorus plays the guajeo, not the sparse strokes on the clave.
      {
         const int g = genreNamed("Salsa");
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            for (uint32_t seed = 1; seed <= 30; ++seed) {
               const Recipe r = recipeOf(g, st, seed);
               auto song = made(r);
               JCLAP_CHECK(song->sections[kChorus].takes[0].count() >= 8);
            }
      }
      // BLEND leaves a fixed figure alone.
      {
         const int g = genreNamed("Salsa"), st = styleNamed(g, "Montuno 2-3");
         Recipe r = recipeOf(g, st, 3);
         auto plainSong = made(r);
         r.genre2 = genreNamed("Funk");
         r.blend = 1.0;
         auto blended = made(r);
         for (int i = 0; i < kSections; ++i)
            for (int s = 0; s < plainSong->sections[i].takes[0].steps; ++s)
               JCLAP_CHECK(plainSong->sections[i].takes[0].at(s).on() == blended->sections[i].takes[0].at(s).on());
      }
      // The one drop keeps 1 and 3 empty at DENSITY 1.
      {
         const int g = genreNamed("Reggae");
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
            const Recipe r = recipeOf(g, st, 5);
            auto song = made(r);
            if (!offbeatTake(song->sections[kVerse].takes[0]))
               continue;
            auto engine = std::make_unique<Engine>();
            Engine &e = *engine;
            e.setSong(*song);
            Settings s = settingsFor(r);
            applyRecipe(s, r);
            s.section = kVerse;
            s.density = 1.0;
            s.vary = 0;
            s.autoFillBars = 0;
            e.setSettings(s);
            e.reset();
            const auto out = runWith(e, 24000 * 16, 0.0, [](int64_t, Block &) {});
            for (const Timed &t : out)
               if (t.on() && t.m.data1 >= 52) {
                  const int64_t inBar = t.time % 96000;
                  JCLAP_CHECK(std::abs(inBar) > 800 && std::abs(inBar - 48000) > 800 && 96000 - inBar > 800);
               }
         }
      }
      // A quiet section is soft, not loud: velocity rises with energy (the loudest chord's: a quiet
      // motor keeps its ghosted strokes, which pull the mean down).
      for (int g = 0; g < genreCount(); g += 3) {
         double before = 0.0;
         for (double e : {0.05, 0.12, 0.3, 0.6, 0.9}) {
            Recipe r = recipeOf(g, 0, 7);
            r.sections[kVerse] = e;
            r.energy = 0.5;
            Section sec = makeSection(r, kVerse, 99);
            double mean = 0.0;
            for (int s = 0; s < sec.takes[0].steps; ++s)
               if (sec.takes[0].at(s).on() && !leftHand(sec.takes[0].at(s).part))
                  mean = std::max(mean, static_cast<double>(sec.takes[0].at(s).vel));
            JCLAP_CHECK(mean >= before - 6.0);
            before = mean;
         }
      }
      // A held-chord style's fill holds: from its downbeat, nothing chopped. Ambient's drift holds
      // on through it: nothing struck within a quarter of the bar line, and no fresh downbeat over a
      // chord held into the bar.
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
            if (!(genreAt(g).styles[st].mods & kLongs))
               continue;
            const bool drift = genreAt(g).family == kFamilies[6];
            const Recipe r = recipeOf(g, st, 2);
            auto song = made(r);
            for (const Section &sec : song->sections) {
               if (sec.fill.empty())
                  continue;
               if (drift) {
                  const Pattern last = lastBar(sec.takes[0]);
                  const int bar = sec.fill.stepsPerBar();
                  for (int s = bar - sec.fill.perBeat(); s < bar; ++s)
                     JCLAP_CHECK(!sec.fill.at(s).on() || last.at(s).on());
                  JCLAP_CHECK(sec.fill.at(0).on() == last.at(0).on());
               } else
                  // (Not over a backbeat take's rest: a slow blues on 2 and 4 has no downbeat chord.)
                  JCLAP_CHECK(sec.fill.at(0).on() || keepsOff(sec.takes[0], 0));
               for (int s = 0; s < sec.fill.steps; ++s)
                  JCLAP_CHECK(!sec.fill.at(s).on() || sec.fill.at(s).art != kArtChop);
            }
            for (const Section &sec : song->sections)
               for (const Pattern &t : sec.takes)
                  for (int s = 0; s < t.steps; ++s)
                     JCLAP_CHECK(!t.at(s).on() || (t.at(s).art != kArtChop && t.at(s).art != kArtHalf));
         }
      // WEIRD never tritone-subs the tonic.
      for (const char *name : {"Funk", "Blues"}) {
         const int g = genreNamed(name);
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            for (uint32_t seed = 1; seed <= 20; ++seed) {
               Recipe r = recipeOf(g, st, seed);
               r.weird = 0.0;
               auto written = made(r);
               r.weird = 1.0;
               auto song = made(r);
               for (int i = 0; i < kSections; ++i)
                  for (int b = 0; b < song->sections[i].progBars; ++b)
                     if (written->sections[i].chordAt(b, false).root == written->key)
                        JCLAP_CHECK(song->sections[i].chordAt(b, false).root == song->key);
            }
      }
      // SPREAD adds no perfect fifth to a diminished or augmented triad.
      {
         VoiceSettings sp;
         sp.shape = kSpread;
         sp.voices = 5;
         for (const char *name : {"Cdim", "Caug"}) {
            const Voicing v = voice(chord(name), nullptr, sp);
            for (int i = 0; i < v.count; ++i)
               JCLAP_CHECK(v.notes[static_cast<size_t>(i)] % 12 != 7);
         }
      }
      // Held notes named as held: inversions, sus, sixths.
      {
         JCLAP_CHECK(chordName(nameChord(Ints{51, 55, 60}, true), 0) == "Cm/Eb");
         JCLAP_CHECK(chordName(nameChord(Ints{55, 60, 65}, true), 0) == "Csus/G");
         JCLAP_CHECK(chordName(nameChord(Ints{52, 60}, true), 0) == "C/E");
         JCLAP_CHECK(chordName(nameChord(Ints{48, 57}, true), 0) == "Am/C");
         JCLAP_CHECK(chordName(nameChord(Ints{48, 58}, true), 0) == "C7");
      }
      // A section's energy changed: its rhythm made again, its chords (edits too) kept, in the song's key.
      {
         const Recipe r = recipeOf(genreNamed("Pop"), 0, 4);
         auto song = made(r);
         song->sections[kVerse].chords[0] = chord("F#m7b5");
         rollSection(r, *song, kVerse, 77, true);
         JCLAP_CHECK(chordName(song->sections[kVerse].chords[0], 0) == "F#m7b5");
         Recipe moved = r;
         moved.key = (r.key + 3) % 12;
         auto fresh = made(r);
         rollSection(moved, *fresh, kChorus, fresh->sections[kChorus].seed);
         auto again = made(r);
         JCLAP_CHECK(fresh->sections[kChorus].chords == again->sections[kChorus].chords);
      }
      // Spelled in the key's own accidentals: F# minor with sharps, a half-step approach as a flat II.
      {
         const Chord fsm = chord("F#m");
         JCLAP_CHECK(chordName(fsm, 6, kMinor) == "F#m");
         JCLAP_CHECK(chordName(chord("C#m"), 1, kMinor) == "C#m");
         const Chord d = chord("D");
         JCLAP_CHECK(chordName(chord("D#9"), 4, kMajor, &d) == "Eb9");
         JCLAP_CHECK(roman(chord("Bb7"), 4) == "bV7");
      }
      // A run into D7 in C walks its own scale: no G#, no step bigger than a whole tone.
      for (int from = 60; from <= 72; ++from)
         for (int len = 1; len <= 4; ++len) {
            const Ints r = passingRun(from, 69, len, 0, kMajor, false, chord("D7"), chord("C"));
            for (size_t k = 0; k < r.size(); ++k) {
               JCLAP_CHECK(r[k] % 12 != 8);
               JCLAP_CHECK(k == 0 || std::abs(r[k] - r[k - 1]) <= 2);
               JCLAP_CHECK(r[k] >= std::min(from, 69) && r[k] <= std::max(from, 69));
            }
         }
      // A shuffle never plays its middle triplet.
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
            if (!(genreAt(g).styles[st].mods & kTriplet) || !(genreAt(g).styles[st].mods & kLeftHand))
               continue;
            for (uint32_t seed = 1; seed <= 10; ++seed) {
               auto song = made(recipeOf(g, st, seed));
               for (const Section &sec : song->sections)
                  for (int s = 0; s < sec.takes[0].steps; ++s)
                     if (sec.takes[0].at(s).on() && sec.takes[0].at(s).part == kPartFifth)
                        JCLAP_CHECK(s % 3 != 1);
            }
         }
      // The Hendrix chord: the #9 over the 3rd.
      {
         VoiceSettings v;
         v.shape = kClose;
         v.voices = 4;
         v.lo = 50;
         v.hi = 76;
         v.center = 62;
         const Voicing h = voice(chord("E7#9"), nullptr, v);
         int third = -1, nine = -1;
         for (int i = 0; i < h.count; ++i) {
            const int n = h.notes[static_cast<size_t>(i)];
            if (n % 12 == 8)
               third = std::max(third, n);
            if (n % 12 == 7)
               nine = std::max(nine, n);
         }
         JCLAP_CHECK(third >= 0 && nine > third);
      }
      // A chord played on the keys a moment before the beat takes that beat, the first one too, and
      // AT ONCE plays it there rather than the old one.
      for (int mode = 0; mode < 3; ++mode) {
         auto song = songOf("x...x...x...x...", {{"C", nullptr}});
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.chordsFrom = mode == 0 ? 1 : 2;
         s.heldOnly = mode == 1;
         s.changeAt = mode == 2 ? 0 : 2;
         e.setSettings(s);
         e.reset();
         const int64_t press = 24000 * 8 - 720; // 30 ms early
         const auto out = runWith(e, 24000 * 10, 0.0, [&](int64_t at, Block &) {
            if (press >= at && press < at + kBlock)
               for (int k : {64, 67, 71})
                  e.noteOn(0, k, 100, static_cast<uint32_t>(press - at));
         });
         const std::set<int> beat = startingAt(out, 24000 * 8);
         JCLAP_CHECK(beat.count(4) == 1 && beat.count(11) == 1 && beat.count(0) == 0);
      }
      // A host loop plays the same chords on every pass, the first included.
      {
         auto song = songOf("x...............", {{"C", nullptr}, {"F", nullptr}, {"G", nullptr}, {"Am", nullptr}});
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         e.setSettings(plain());
         e.reset();
         std::vector<std::set<int>> passes;
         auto out = std::make_unique<OutBuffer>();
         double beats = 0.0;
         std::set<int> cur;
         for (int blk = 0; blk < 3000; ++blk) {
            Block b;
            b.frames = kBlock;
            b.tempo = 120.0;
            b.hasTransport = b.playing = true;
            b.beats = beats;
            b.loopActive = true;
            b.loopStart = 8.0;
            b.loopEnd = 12.0;
            out->clear();
            e.process(b, *out);
            for (size_t i = 0; i < out->count; ++i) {
               // Where the note falls on the host's timeline, the loop's wrap included.
               double at = beats + out->items[i].time / 24000.0;
               if (at >= 12.0)
                  at -= 4.0;
               if ((out->items[i].status & 0xF0) == 0x90 && out->items[i].data2 > 0 && at >= 7.95 && at < 8.05)
                  cur.insert(out->items[i].data1 % 12);
            }
            beats += kBlock / 24000.0;
            if (beats >= 12.0) {
               beats = 8.0 + (beats - 12.0);
               passes.push_back(cur);
               cur.clear();
            }
         }
         JCLAP_CHECK(passes.size() >= 3);
         for (const auto &p : passes)
            JCLAP_CHECK(p == passes.front());
      }
      // PLAY ONLY WHILE HELD stops when the chord is let go.
      {
         auto song = songOf("x---------------", {{"C", nullptr}});
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.heldOnly = true;
         s.chordsFrom = 0;
         e.setSettings(s);
         e.reset();
         const int64_t up = 24000 * 6;
         std::map<int, int> sounding;
         const auto out = runWith(e, 24000 * 10, 0.0, [&](int64_t at, Block &) {
            if (at == 0)
               for (int k : {60, 64, 67})
                  e.noteOn(0, k, 100, 0);
            if (up >= at && up < at + kBlock)
               for (int k : {60, 64, 67})
                  e.noteOff(0, k, static_cast<uint32_t>(up - at));
         });
         int on = 0;
         for (const Timed &t : out) {
            if (t.time > up + 2400)
               break;
            on += t.on() ? 1 : t.off() ? -1 : 0;
         }
         JCLAP_CHECK_EQ(on, 0);
      }
      // The same setting plays the same every time it starts.
      {
         const Recipe r = recipeOf(genreNamed("Funk"), 0, 1);
         auto song = made(r);
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = settingsFor(r);
         s.density = 0.8;
         e.setSettings(s);
         std::vector<std::vector<int>> takes;
         for (int pass = 0; pass < 2; ++pass) {
            e.reset();
            const auto out = runWith(e, 24000 * 8, 0.0, [](int64_t, Block &) {});
            std::vector<int> notes;
            for (const Timed &t : out)
               if (t.on())
                  notes.push_back(static_cast<int>(t.time / 64) * 128 + t.m.data1);
            takes.push_back(notes);
            Block stop;
            stop.frames = kBlock;
            stop.hasTransport = true;
            stop.playing = false;
            auto o = std::make_unique<OutBuffer>();
            e.process(stop, *o);
         }
         JCLAP_CHECK(takes[0] == takes[1]);
      }
      // A guitar's boom-chick and palm-wine strum leave the bass string to the thumb.
      for (const auto &[gn, sn] : {std::pair<const char *, const char *>{"Country", "Two-Step"}, {"Highlife", "Palm-Wine"}}) {
         const int g = genreNamed(gn), st = styleNamed(g, sn);
         JCLAP_CHECK(g >= 0 && st >= 0);
         if (g < 0 || st < 0)
            continue;
         const Recipe r = recipeOf(g, st, 1);
         auto song = made(r);
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         std::vector<double> sv = defaultValues();
         for (const auto &[id, x] : styleValues(g, st))
            sv[id] = x;
         Settings s = settingsFor(r);
         s.voicing = voiceSettings([&](clap_id id) { return sv[id]; });
         applyRecipe(s, r);
         s.section = kVerse;
         s.key = song->key;
         e.setSettings(s);
         e.reset();
         const auto out = runWith(e, 24000 * 16, 0.0, [](int64_t, Block &) {});
         std::map<int64_t, std::vector<int>> at;
         for (const Timed &t : out)
            if (t.on())
               at[t.time / 960].push_back(t.m.data1);
         int bass = -1, chords = 0;
         for (const auto &[k, ns] : at) {
            if (ns.size() == 1) {
               bass = ns[0];
            } else if (ns.size() >= 2 && bass >= 0) { // (Palm-Wine's pinch is two strings)
               ++chords;
               JCLAP_CHECK(*std::min_element(ns.begin(), ns.end()) != bass);
            }
         }
         JCLAP_CHECK(chords > 4);
      }
   }
   // ---- round 5 ----
   {
      // Held notes: a tritone is a dominant's shell, a rootless voicing its dominant, a triad over a
      // foreign bass a slash chord; a 6/9 over a bass parses.
      JCLAP_CHECK(chordName(nameChord(Ints{59, 65}), 0) == "G7");
      JCLAP_CHECK(chordName(nameChord(Ints{53, 57, 59, 64}), 0) == "G13");
      JCLAP_CHECK(chordName(nameChord(Ints{47, 50, 53, 57}), 0) == "Bm7b5");
      JCLAP_CHECK(chordName(nameChord(Ints{43, 53, 57, 60}, true), 0) == "F/G");
      Chord six;
      JCLAP_CHECK(parseChord("C6/9/E", six) && six.bass == 4 && chordName(six, 0) == "C6/9/E");
      // F# minor's leading tone is E#.
      JCLAP_CHECK(std::string(pitchName(5, 6, kMinor)) == "E#");
   }
   {
      // A grip comes back to the same open chord, whatever came between; and every grip is one a
      // hand can make.
      VoiceSettings vs;
      vs.shape = kGrip;
      vs.voices = 5;
      vs.lo = 40;
      vs.hi = 76;
      vs.center = 58;
      vs.floor = 40;
      Voicing prev, firstE;
      Chord pc;
      for (const char *name : {"E", "A", "E", "B7", "E", "A", "E"}) {
         const Chord c = chord(name);
         const Voicing v = voice(c, prev.empty() ? nullptr : &prev, vs, -1, pc.none() ? nullptr : &pc);
         JCLAP_CHECK(gripFrets(v) >= 0.0);
         if (std::string(name) == "E") {
            if (firstE.empty())
               firstE = v;
            JCLAP_CHECK(v == firstE);
         }
         prev = v;
         pc = c;
      }
      // 4THS keeps a triad and a sus in fourths (no fall back to SPREAD), its bass under a slash chord.
      VoiceSettings q;
      q.shape = kQuartal;
      q.voices = 5;
      const Voicing d = voice(chord("Dsus"), nullptr, q);
      for (int i = 1; i < d.count; ++i)
         JCLAP_CHECK(d.notes[static_cast<size_t>(i)] - d.notes[static_cast<size_t>(i - 1)] >= 4);
      // ROOTLESS at five voices never puts the root a half step over the major seventh.
      VoiceSettings r;
      r.shape = kRootless;
      r.voices = 5;
      const Voicing m = voice(chord("Cmaj7"), nullptr, r);
      for (int i = 0; i < m.count; ++i)
         JCLAP_CHECK(m.notes[static_cast<size_t>(i)] % 12 != 0);
      // Three voices: no half-step cluster where a plainer three will do.
      VoiceSettings three;
      three.voices = 3;
      const Voicing e9 = voice(chord("Em9"), nullptr, three);
      for (int i = 1; i < e9.count; ++i)
         JCLAP_CHECK(e9.notes[static_cast<size_t>(i)] - e9.notes[static_cast<size_t>(i - 1)] != 1);
   }
   {
      // A fill is the take's own instrument: a line fills with its line, a take with no left hand
      // gets no thumb, and the one drop gains no chops before its skank.
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
            auto song = std::make_unique<Song>();
            generateSong(recipeOf(g, st, 3), *song);
            for (const Section &sec : song->sections) {
               const Pattern &t = sec.takes[0];
               if (hasLine(t))
                  JCLAP_CHECK(hasLine(sec.fill));
               if (!hasLeftHand(t))
                  JCLAP_CHECK(!hasLeftHand(sec.fill));
            }
         }
   }
   {
      // PLAY ONLY WHILE HELD holds for the bass coming in too.
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*songOf("x...x...x...x...", {{"C", nullptr}}));
      Settings s = plain();
      s.heldOnly = true;
      s.rhythmFrom = 1;
      e.setSettings(s);
      e.reset();
      auto out = std::make_unique<OutBuffer>();
      int ons = 0;
      for (int64_t at = 0; at < 24000 * 8; at += kBlock) {
         if (at % 24000 == 0)
            e.noteOn(s.bassChannel, 36, 100, 0);
         Block b;
         b.frames = kBlock;
         b.tempo = 120.0;
         b.hasTransport = b.playing = true;
         b.beats = at / 24000.0;
         out->clear();
         e.process(b, *out);
         for (size_t i = 0; i < out->count; ++i)
            ons += (out->items[i].status & 0xF0) == 0x90 && out->items[i].data2 > 0;
      }
      JCLAP_CHECK_EQ(ons, 0);
   }
   {
      // A start a few ms past the bar line still plays the downbeat; a host loop's first pass plays
      // what the later ones do, note for note.
      for (double startAt : {0.03, 0.06}) {
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*songOf("x...x...x...x...", {{"C", nullptr}}));
         e.setSettings(plain());
         e.reset();
         auto out = std::make_unique<OutBuffer>();
         Block b;
         b.frames = kBlock;
         b.tempo = 180.0;
         b.hasTransport = b.playing = true;
         b.beats = startAt;
         e.process(b, *out);
         int ons = 0;
         for (size_t i = 0; i < out->count; ++i)
            ons += (out->items[i].status & 0xF0) == 0x90 && out->items[i].data2 > 0 && out->items[i].time == 0;
         JCLAP_CHECK(ons >= 3);
      }
      const Recipe rec = recipeOf(0, 0, 5);
      auto song = std::make_unique<Song>();
      generateSong(rec, *song);
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = settingsFor(rec);
      s.runs = 0.6;
      applyRecipe(s, rec);
      e.setSettings(s);
      e.reset();
      auto out = std::make_unique<OutBuffer>();
      std::vector<std::vector<std::pair<int, int>>> passes(1);
      double beats = 0.0;
      for (int blk = 0; blk < 6000 && passes.size() < 4; ++blk) {
         Block b;
         b.frames = kBlock;
         b.tempo = 120.0;
         b.hasTransport = b.playing = true;
         b.beats = beats;
         b.loopActive = true;
         b.loopStart = 0.0;
         b.loopEnd = 16.0;
         out->clear();
         e.process(b, *out);
         for (size_t i = 0; i < out->count; ++i)
            if ((out->items[i].status & 0xF0) == 0x90 && out->items[i].data2 > 0)
               passes.back().push_back({static_cast<int>(std::lround((beats * 24000.0 + out->items[i].time) / 48.0)),
                                        out->items[i].data1});
         beats += kBlock / 24000.0;
         if (beats >= 16.0) {
            beats -= 16.0;
            passes.emplace_back();
         }
      }
      JCLAP_CHECK(passes.size() >= 3);
      // The same notes, each within a millisecond or two (the clock's rounding).
      JCLAP_CHECK_EQ(passes[0].size(), passes[1].size());
      int differ = 0;
      for (size_t i = 0; i < std::min(passes[0].size(), passes[1].size()); ++i)
         differ += passes[0][i].second != passes[1][i].second || std::abs(passes[0][i].first - passes[1][i].first) > 2;
      JCLAP_CHECK_EQ(differ, 0);
   }
   // ---- the comparison with MMA, Impro-Visor and JJazzLab (0.6.0) ----
   {
      const auto genreNamed = [](const char *name) {
         for (int i = 0; i < genreCount(); ++i)
            if (std::string(genreAt(i).name) == name)
               return i;
         return -1;
      };
      const auto styleNamed = [](int g, const char *name) {
         const Genre &gen = genreAt(g);
         for (size_t i = 0; i < gen.styles.size(); ++i)
            if (std::string(gen.styles[i].name) == name)
               return static_cast<int>(i);
         return -1;
      };
      const auto made = [](const Recipe &r) {
         auto s = std::make_unique<Song>();
         generateSong(r, *s);
         return s;
      };
      // Runs an engine at `bpm` from the host's beat 0, for `beats` beats.
      const auto runAt = [](Engine &e, double bpm, double beats) {
         std::vector<std::pair<double, MidiOut>> all; // (beat, event)
         auto out = std::make_unique<OutBuffer>();
         const double perFrame = bpm / 60.0 / 48000.0;
         for (double at = 0.0; at < beats; at += kBlock * perFrame) {
            Block b;
            b.frames = kBlock;
            b.sampleRate = 48000.0;
            b.tempo = bpm;
            b.hasTransport = b.playing = true;
            b.beats = at;
            out->clear();
            e.process(b, *out);
            for (size_t i = 0; i < out->count; ++i)
               all.push_back({at + out->items[i].time * perFrame, out->items[i]});
         }
         return all;
      };
      const auto isOn = [](const MidiOut &m) { return (m.status & 0xF0) == 0x90 && m.data2 > 0; };

      // Every style's song: the outro has an ending, nothing in it anticipated, its last chord rung;
      // every section's fill down is quieter than its fill and leaves the next downbeat clear; a
      // three-beat style's song is in three, every take on the three-beat grid.
      int waltzes = 0;
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
            auto song = made(recipeOf(g, st, 31));
            const Section &out = song->sections[kOutro];
            JCLAP_CHECK(!out.ending.empty());
            int last = -1;
            // (A montuno's or guajeo's last chord comes on the and of 4, pushed; the cha-cha's on the 1.)
            const std::string gn = genreAt(g).name, sn = genreAt(g).styles[static_cast<size_t>(st)].name;
            const bool anticipates = gn == "Salsa" || (gn == "Latin Jazz" && sn != "Cha-Cha" && sn != "Bolero");
            for (int k = 0; k < out.ending.steps; ++k)
               if (out.ending.at(k).on()) {
                  JCLAP_CHECK(!out.ending.at(k).push || (anticipates && k == 14));
                  last = k;
               }
            if (sn == "Cha-Cha")
               JCLAP_CHECK(last == 16);
            JCLAP_CHECK(last >= 0 && out.ending.at(last).art == kArtRing);
            JCLAP_CHECK(out.ending.rate == out.takes[0].rate);
            for (const Section &sec : song->sections) {
               const Pattern &d = sec.fillDown;
               const bool intro = &sec == &song->sections[kIntro];
               // (An ambient drift has none: its fill, or the take, plays on into the change.)
               if (genreAt(g).family == kFamilies[6]) {
                  JCLAP_CHECK(d.empty());
                  continue;
               }
               JCLAP_CHECK(!d.empty() && d.rate == sec.takes[0].rate);
               JCLAP_CHECK(d.count() <= std::max(2, sec.fill.count()));
               if (genreAt(g).styles[static_cast<size_t>(st)].mods & kFixed) {
                  // A fixed figure settles as itself: its onsets and pushes are the take's (Freddie
                  // Green's four, the clave's side, a montuno's and of 4 into the next chord). The
                  // intro's set-up too (B05).
                  (void)intro;
                  for (int k = 0; k < d.steps; ++k) {
                     const Hit &w = sec.takes[0].at(k % sec.takes[0].steps);
                     JCLAP_CHECK(!d.at(k).on() || w.on());
                     JCLAP_CHECK(!d.at(k).push || w.push);
                  }
               } else {
                  for (int k = d.stepsPerBar() - d.perBeat(); k < d.steps; ++k)
                     JCLAP_CHECK(!d.at(k).on());
                  for (int k = 0; k < d.steps; ++k)
                     JCLAP_CHECK(!d.at(k).push);
               }
            }
            const bool waltz = genreAt(g).styles[static_cast<size_t>(st)].mods & kWaltz;
            JCLAP_CHECK_EQ(static_cast<int>(song->beats), waltz ? 3 : 4);
            if (waltz) {
               ++waltzes;
               for (const Section &sec : song->sections)
                  for (const Pattern &t : sec.takes)
                     JCLAP_CHECK(t.rate == styleRate(genreAt(g).styles[static_cast<size_t>(st)].mods) && t.inThree() && t.stepsPerBar() == 12);
            }
         }
      JCLAP_CHECK_EQ(waltzes, 3);
      // Four-bar rhythms read whole; a three-beat rhythm isn't the triplet grid; four bars of triplets
      // are the triplet grid.
      JCLAP_CHECK_EQ(static_cast<int>(fromRhythm("x.....x...x...x.|x.........x.....|x.........x...x.|..........x...x.").steps), 64);
      JCLAP_CHECK(fromRhythm(Rhythm{"x-------x---", 0.35, 3}).rate == kRate34);
      JCLAP_CHECK(fromRhythm(Rhythm{"x-----x-----", 0.35, 6}).rate == kRate68);
      {
         const Pattern t = fromRhythm("xxxXxxxxxXxx|xxxXxxxxxXxx|xxxXxxxxxXxx|x..x..x..x..");
         JCLAP_CHECK(t.rate == kRate8T && t.steps == 48);
      }
      // ANSWER leaves a four-bar phrase as it is.
      {
         Pattern p = fromRhythm("x.......x.......|x.......x.......|x.......x.......|x...x...x...x...");
         const Pattern before = p;
         Rng rng(5);
         applyMutation(p, "answer", rng);
         JCLAP_CHECK(p == before);
      }
      // Freddie Green's four: four to the bar at every energy (B05), quieter when the section is.
      {
         const int g = genreNamed("Swing Jazz");
         const Style &fg = styleAt(genreAt(g), styleNamed(g, "Four To The Bar"));
         const auto list = rhythmsFor(genreAt(g), fg);
         for (uint32_t k = 1; k <= 40; ++k)
            for (double e : {0.1, 0.3, 0.9}) {
               Rng a(k);
               JCLAP_CHECK_EQ(fromRhythm(*pickRhythm(list, e, a)).count(), 8); // four a bar
            }
      }
      // The right hand's arpeggio is single line notes on 1, 2, its and, 3, 4 and its and.
      {
         const Pattern p = fromRhythm("1...3.5.8...5.3.|1.....5.8...5.3.");
         for (int k : {0, 4, 6, 8, 12, 14})
            JCLAP_CHECK(p.at(k).on() && p.at(k).part == kPartLine);
      }
      // The song saves and loads with its meter, fills down, ending and band hits; an older song loads
      // in four.
      {
         auto song = made(recipeOf(genreNamed("Swing Jazz"), styleNamed(genreNamed("Swing Jazz"), "Jazz Waltz"), 3));
         song->sections[kVerse].marks[2] = kMarkHold;
         song->sections[kVerse].marks[5] = kMarkShot;
         const std::vector<uint8_t> data = song->save();
         auto back = std::make_unique<Song>();
         JCLAP_CHECK(back->load(data.data(), data.size()));
         JCLAP_CHECK(*back == *song);
         JCLAP_CHECK_EQ(static_cast<int>(back->beats), 3);
      }

      // SONG ends: from the outro, its ending plays, then nothing (the intro's chord never comes);
      // on repeat it goes round to the intro.
      for (bool ends : {true, false}) {
         auto song = made(recipeOf(0, 0, 9));
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = settingsFor(Recipe{});
         s.key = song->key;
         s.song = true;
         s.songEnds = ends;
         s.section = kOutro;
         s.autoFillBars = 0;
         e.setSettings(s);
         e.reset();
         const double outroBeats = song->sections[kOutro].bars * 4.0;
         const auto all = runAt(e, 120.0, outroBeats + 8.0);
         int after = 0, inEnding = 0;
         for (const auto &[beat, m] : all)
            if (isOn(m)) {
               after += beat >= outroBeats + 0.1;
               inEnding += beat >= outroBeats - 8.0 && beat < outroBeats;
            }
         JCLAP_CHECK(inEnding > 0);
         if (ends) {
            JCLAP_CHECK_EQ(after, 0);
            Frame f;
            e.fillFrame(f);
            JCLAP_CHECK(f.ended);
         } else
            JCLAP_CHECK(after > 0);
      }
      // The fill into a quieter section is the fill down; INTRO SETS UP plays the intro's last bar as
      // one held chord.
      {
         auto song = songOf("x.x.x.x.x.x.x.x.", {{"C", nullptr}, {"F", nullptr}});
         for (Section &sec : song->sections) {
            sec.bars = 2;
            sec.fillDown = fromRhythm("X-------........");
            sec.fillDown.steps = 16;
         }
         song->sections[kIntro].energy = 30;
         song->sections[kVerse].energy = 60;
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = settingsFor(Recipe{});
         s.song = true;
         s.section = kIntro;
         s.autoFillBars = 0;
         s.onsets = 0;
         s.runs = 0;
         e.setSettings(s);
         e.reset();
         const auto all = runAt(e, 120.0, 8.0);
         std::set<int> strikes; // in the intro's last bar, by sixteenth
         for (const auto &[beat, m] : all)
            if (isOn(m) && beat >= 4.0 - 0.05 && beat < 8.0 - 0.05)
               strikes.insert(static_cast<int>(std::lround(beat * 4.0)));
         JCLAP_CHECK_EQ(static_cast<int>(strikes.size()), 1);
      }
      // SWING EASES WHEN FAST: at 280 BPM a 64% swing's and of 1 comes before 56% of the beat; at 120
      // the full swing.
      for (double bpm : {120.0, 280.0}) {
         auto song = songOf("x.x.x.x.x.x.x.x.", {{"C", nullptr}});
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = settingsFor(Recipe{});
         s.swing = 0.64;
         s.swingFeel = 1;
         s.loose = 0;
         s.vary = 0;
         s.runs = 0;
         s.onsets = 0;
         s.autoFillBars = 0;
         e.setSettings(s);
         e.reset();
         const auto all = runAt(e, bpm, 8.0);
         double worst = bpm > 200 ? 0.0 : 1.0;
         for (const auto &[beat, m] : all) {
            const double f = beat - std::floor(beat);
            if (isOn(m) && f > 0.3 && f < 0.8)
               worst = bpm > 200 ? std::max(worst, f) : std::min(worst, f);
         }
         if (bpm > 200)
            JCLAP_CHECK(worst > 0.5 && worst < 0.56);
         else
            JCLAP_CHECK(worst > 0.62);
      }
      // A waltz: a bar is three beats (the next downbeat three beats on, nothing on beat 4).
      {
         auto song = songOf("x.x.x.x.x.x.x.x.", {{"C", nullptr}, {"F", nullptr}});
         song->beats = 3;
         for (Section &sec : song->sections) {
            for (Pattern &t : sec.takes)
               t = fromRhythm("x...........", kRate34);
            sec.fill = sec.takes[0];
         }
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = settingsFor(Recipe{});
         s.autoFillBars = 0;
         s.runs = 0;
         s.density = 0.5;
         e.setSettings(s);
         e.reset();
         const auto all = runAt(e, 120.0, 12.0);
         std::set<int> beatsOn;
         for (const auto &[beat, m] : all)
            if (isOn(m) && beat < 11.9)
               beatsOn.insert(static_cast<int>(std::lround(beat)));
         JCLAP_CHECK(beatsOn == (std::set<int>{0, 3, 6, 9}));

      }
      // Band hits: a held chord strikes once for its bar, a shot once and short; the take plays on
      // round them.
      for (int mark : {kMarkHold, kMarkShot, kMarkAccent}) {
         auto song = songOf("x.x.x.x.x.x.x.x.", {{"C", nullptr}, {"F", nullptr}});
         for (Section &sec : song->sections)
            sec.marks[0] = static_cast<uint8_t>(mark);
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = settingsFor(Recipe{});
         s.autoFillBars = 0;
         s.runs = 0;
         s.onsets = 0;
         s.vary = 0;
         e.setSettings(s);
         e.reset();
         const auto all = runAt(e, 120.0, 8.0);
         std::set<int> bar1, bar2;
         double longest = 0.0;
         std::map<int, double> started;
         for (const auto &[beat, m] : all) {
            if (isOn(m)) {
               if (beat < 7.95)
                  (beat < 3.95 ? bar1 : bar2).insert(static_cast<int>(std::lround(beat * 4.0)));
               if (beat < 0.1)
                  started[m.data1] = beat;
            } else if (started.count(m.data1)) {
               longest = std::max(longest, beat - started[m.data1]);
               started.erase(m.data1);
            }
         }
         JCLAP_CHECK_EQ(static_cast<int>(bar2.size()), 8);
         if (mark == kMarkAccent)
            JCLAP_CHECK_EQ(static_cast<int>(bar1.size()), 8);
         else
            JCLAP_CHECK_EQ(static_cast<int>(bar1.size()), 1);
         if (mark == kMarkHold)
            JCLAP_CHECK(longest > 3.0);
         if (mark == kMarkShot)
            JCLAP_CHECK(longest < 0.3);
      }
      // The pad layer: the chord held on its own channel, struck again only when it changes.
      {
         auto song = songOf("x...x...x...x...", {{"C", nullptr}, {"F", nullptr}});
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = settingsFor(Recipe{});
         s.autoFillBars = 0;
         s.pad = true;
         s.padChannel = 5;
         s.key = song->key;
         e.setSettings(s);
         e.reset();
         const auto all = runAt(e, 120.0, 16.0);
         int padOns = 0;
         std::set<int> padPcs;
         for (const auto &[beat, m] : all)
            if (isOn(m) && (m.status & 15) == 5) {
               ++padOns;
               padPcs.insert(m.data1 % 12);
            }
         JCLAP_CHECK(padOns >= 12 && padOns <= 20); // four chords of four notes
         JCLAP_CHECK(padPcs.count(0) && padPcs.count(5));

      }
   }
   // ---- the sixth review's fixes (0.7.0) ----
   {
      // D1: root, 3rd and 7th over the lowest note is that note's dominant, not its tritone sub.
      JCLAP_CHECK(chordName(nameChord(Ints{48, 52, 58}), 0) == "C7");
      JCLAP_CHECK(chordName(nameChord(Ints{48, 58, 64}), 0) == "C7");
      JCLAP_CHECK(chordName(nameChord(Ints{40, 44, 50}), 4) == "E7");
      JCLAP_CHECK(chordName(nameChord(Ints{48, 52, 58, 61}), 0) == "C7b9");
      JCLAP_CHECK(chordName(nameChord(Ints{53, 57, 59, 64}), 7) == "G13");
      JCLAP_CHECK(chordName(nameChord(Ints{59, 65}), 7) == "G7");
   }
   {
      // A1, A2, A3, A9, C04, E9: every style's song ends on its home chord, in the song's key, the
      // last bar whole (no approach chord), at any WEIRD.
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            for (uint32_t seed = 1; seed <= 6; ++seed)
               for (double weird : {-1.0, 1.0}) {
                  Recipe r = recipeOf(g, st, seed);
                  if (weird >= 0)
                     r.weird = weird;
                  Song song;
                  generateSong(r, song);
                  const Section &o = song.sections[kOutro];
                  JCLAP_CHECK_EQ(static_cast<int>(o.chordAt(o.bars - 1, false).root), static_cast<int>(song.key));
                  JCLAP_CHECK_EQ(static_cast<int>(o.chordAt(o.bars - 1, true).root), static_cast<int>(song.key));
               }
   }
   {
      // C01, B02: a spread chord's low note is the left hand's root, so the alternating bass's next
      // note is the fifth (1-5-8-5), not the root again.
      for (const char *rhythm : {"x.f.b.f.x.f.b.f.", "x.......f.......", "x.f.X..........."}) {
         auto song = songOf(rhythm, {{"C", nullptr}, {"F", nullptr}});
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.voicing.shape = kSpread;
         e.setSettings(s);
         e.reset();
         const auto all = runWith(e, 24000 * 8, 0.0, [](int64_t, Block &) {});
         const int64_t fifthAt = rhythm[2] == 'f' ? 12000 : 48000;
         JCLAP_CHECK(startingAt(all, fifthAt) == std::set<int>{7});           // C's fifth
         JCLAP_CHECK(startingAt(all, 96000 + fifthAt) == std::set<int>{0});   // F's fifth
      }
   }
   {
      // C02, C03, D2: 6/8 is two dotted-quarter beats. Its ending and fill down land on eighths 1
      // and 4, and DENSITY drops the weak eighths before the second beat.
      int sixEights = 0;
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
            if (!(genreAt(g).styles[static_cast<size_t>(st)].mods & kSixEight))
               continue;
            ++sixEights;
            for (uint32_t seed = 1; seed <= 8; ++seed) {
               Song song;
               generateSong(recipeOf(g, st, seed), song);
               const Pattern &end = song.sections[kOutro].ending;
               JCLAP_CHECK(end.rate == kRate68);
               for (int k = 0; k < end.steps; ++k)
                  if (end.at(k).on())
                     JCLAP_CHECK(k % 6 == 0);
               for (const Section &sec : song.sections)
                  for (int k = 0; k < sec.fillDown.steps; ++k)
                     if (sec.fillDown.at(k).on() && !leftHand(sec.fillDown.at(k).part))
                        JCLAP_CHECK(k < 6);
            }
         }
      JCLAP_CHECK_EQ(sixEights, 1);
      const Pattern six = fromRhythm("X.x.x.X.x.x.", kRate68);
      JCLAP_CHECK_EQ(six.perBeat(), 6);
      JCLAP_CHECK_EQ(strongSteps(six), 6);
      for (double density : {0.1, 0.2, 0.3, 0.4})
         for (double d : {0.0, 0.5, 1.0})
            if (!thinnedBy(six, 4, density, d, false) || !thinnedBy(six, 8, density, d, false))
               JCLAP_CHECK(!thinnedBy(six, 6, density, d, false));
   }
   {
      // B05, D3, E10: in 3/4 a bar's second chord comes in on beat 3, not on the and of 2.
      auto song = songOf("x.x.x.x.x.x.x.x.", {{"C", "G"}});
      song->beats = 3;
      for (Section &sec : song->sections) {
         for (Pattern &t : sec.takes)
            t = fromRhythm("x...x...x...", kRate34);
         sec.fill = sec.takes[0];
      }
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      e.setSettings(plain());
      e.reset();
      const auto all = runWith(e, 24000 * 6, 0.0, [](int64_t, Block &) {});
      JCLAP_CHECK(startingAt(all, 24000).count(0));  // beat 2: still C
      JCLAP_CHECK(!startingAt(all, 24000).count(11));
      JCLAP_CHECK(startingAt(all, 48000).count(7));  // beat 3: G
      JCLAP_CHECK(startingAt(all, 48000).count(11));
   }
   {
      // D6: a song saved before 0.6.0 gets its fills down and its ending when it loads.
      const Recipe r = recipeOf(0, 0, 5);
      Song made;
      generateSong(r, made);
      const std::vector<uint8_t> old = saveV2(made);
      Song loaded;
      JCLAP_CHECK(loaded.load(old.data(), old.size()));
      JCLAP_CHECK(loaded.sections[kOutro].ending.empty());
      completeSong(r, loaded, old);
      JCLAP_CHECK(!loaded.sections[kOutro].ending.empty());
      for (const Section &sec : loaded.sections)
         JCLAP_CHECK(!sec.fillDown.empty());
      // A version-3 song is left as it was saved (a fill down cleared in EDIT stays cleared).
      Song cleared = made;
      cleared.sections[kVerse].fillDown = Pattern{};
      const std::vector<uint8_t> now = cleared.save();
      Song back;
      JCLAP_CHECK(back.load(now.data(), now.size()));
      completeSong(r, back, now);
      JCLAP_CHECK(back.sections[kVerse].fillDown.empty());
   }
   {
      // E3, E4: the pad holds a one-chord vamp as long as it lasts, and lets go when switched off.
      auto song = songOf("x...x...x...x...", {{"C", nullptr}});
      for (bool off : {false, true}) {
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.pad = true;
         s.padChannel = 5;
         e.setSettings(s);
         e.reset();
         const auto all = runWith(e, 24000LL * 160, 0.0, [&](int64_t at, Block &) {
            if (off && at >= 24000LL * 8 && e.settings().pad) {
               Settings t = e.settings();
               t.pad = false;
               e.setSettings(t);
            }
         });
         // Each pad note's span (on to off, or on to the end).
         std::vector<std::pair<int64_t, int64_t>> spans;
         std::map<int, int64_t> started;
         double lastPad = -1.0;
         for (const Timed &t : all) {
            if ((t.m.status & 15) != 5)
               continue;
            if (t.on())
               started[t.m.data1] = t.time;
            else if (started.count(t.m.data1)) {
               spans.push_back({started[t.m.data1], t.time});
               started.erase(t.m.data1);
            }
            lastPad = static_cast<double>(t.time) / 24000.0;
         }
         for (const auto &[k, from] : started)
            spans.push_back({from, INT64_MAX});
         bool everyBar = true;
         for (int bar = 1; bar < 40; ++bar) {
            const int64_t at = bar * 4 * 24000LL + 100;
            everyBar = everyBar && std::any_of(spans.begin(), spans.end(), [&](const auto &sp) { return sp.first <= at && sp.second > at; });
         }
         if (!off)
            JCLAP_CHECK(everyBar);
         else
            JCLAP_CHECK(lastPad >= 0.0 && lastPad < 8.5);
      }
   }
   {
      // A5, E7, B11: a four-bar phrase counts from its section's start, not the host's bar, after a
      // switch on an odd bar: the turnaround bar of quarters is the section's fourth.
      auto song = songOf("x...............", {{"C", nullptr}, {"F", nullptr}, {"G", nullptr}, {"C", nullptr}});
      for (Pattern &t : song->sections[kChorus].takes)
         t = fromRhythm("x...............|x...............|x...............|x...x...x...x...");
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.section = kVerse;
      e.setSettings(s);
      e.reset();
      const auto all = runWith(e, 24000LL * 28, 0.0, [&](int64_t at, Block &) {
         if (at >= 24000LL * 5 && e.settings().section != kChorus) {
            Settings t = e.settings();
            t.section = kChorus;
            e.setSettings(t);
         }
      });
      // The chorus starts at beat 8 (the next bar); its fourth bar is beats 20 to 24.
      JCLAP_CHECK(!startingAt(all, 24000LL * 21).empty());
      JCLAP_CHECK(!startingAt(all, 24000LL * 23).empty());
      JCLAP_CHECK(startingAt(all, 24000LL * 9).empty());
      JCLAP_CHECK(startingAt(all, 24000LL * 13).empty());
   }
   {
      // B03: a clave figure played as written gets no chord on its 2-side's one at a change.
      auto song = songOf("....x...x.......|x.....x.....x...", {{"C", nullptr}, {"F", nullptr}});
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.fixed = true;
      e.setSettings(s);
      e.reset();
      const auto all = runWith(e, 24000LL * 16, 0.0, [](int64_t, Block &) {});
      JCLAP_CHECK(startingAt(all, 24000LL * 8).empty());   // bar 3, the 2-side, over C
      JCLAP_CHECK(!startingAt(all, 24000LL * 9).empty());  // its beat 2
      // B04: a waltz comp on beat 3 leaves the bar line's one to the bass at every change.
      auto waltz = songOf("x.x.x.x.x.x.x.x.", {{"C", nullptr}, {"F", nullptr}, {"G", nullptr}});
      waltz->beats = 3;
      for (Section &sec : waltz->sections) {
         for (Pattern &t : sec.takes)
            t = fromRhythm("........x...|........x...", kRate34);
         sec.fill = sec.takes[0];
      }
      auto engine2 = std::make_unique<Engine>();
      Engine &w = *engine2;
      w.setSong(*waltz);
      w.setSettings(plain());
      w.reset();
      const auto three = runWith(w, 24000LL * 12, 0.0, [](int64_t, Block &) {});
      for (int bar = 1; bar < 4; ++bar) {
         JCLAP_CHECK(startingAt(three, 24000LL * 3 * bar).empty());
         JCLAP_CHECK(!startingAt(three, 24000LL * (3 * bar + 2)).empty());
      }
   }
   {
      // A6: a pushed chord is tied over the beat it anticipates, not struck again on it, and rings
      // through it.
      auto song = songOf("x.....x.......x.", {{"C", nullptr}, {"F", nullptr}});
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes)
            t.at(14).push = t.at(30).push = 1;
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      e.setSettings(plain());
      e.reset();
      const auto all = runWith(e, 24000LL * 8, 0.0, [](int64_t, Block &) {});
      JCLAP_CHECK(startingAt(all, 84000).count(5));   // F pushed on the and of 4
      JCLAP_CHECK(startingAt(all, 96000).empty());    // not struck again on 1
      bool ringing = false;
      std::map<int, int64_t> on;
      for (const Timed &t : all) {
         if (t.on())
            on[t.m.data1] = t.time;
         else if (t.off() && on.count(t.m.data1) && on[t.m.data1] < 90000 && on[t.m.data1] > 80000)
            ringing = ringing || t.time > 100000;
      }
      JCLAP_CHECK(ringing);
      JCLAP_CHECK(!startingAt(all, 96000 + 36000).empty()); // bar 2's own hit on beat 2.5 plays
   }
   {
      // A7: Sixties R&B keeps a chord on a beat through ENERGY, WEIRD and the mutations: it's a
      // pushed figure, not a skank, so its changes and fills don't wait for an upbeat.
      for (uint32_t seed = 1; seed <= 30; ++seed)
         for (double weird : {0.2, 0.9}) {
            Recipe r = recipeOf(1, 3, seed);
            r.weird = weird;
            Song song;
            generateSong(r, song);
            for (const Section &sec : song.sections)
               for (const Pattern &t : sec.takes)
                  JCLAP_CHECK(chordOnBeat(t));
         }
      // A11: at ENERGY 0 the boogie's and the shout's left hand keep each bar's one; A10: the shout's
      // fill isn't its groove.
      for (const auto &[g, st] : {std::pair{2, 2}, std::pair{7, 1}}) {
         Recipe r = recipeOf(g, st, 3);
         r.energy = 0.0;
         Song song;
         generateSong(r, song);
         const Pattern &t = song.sections[kVerse].takes[0];
         for (int s = 0; s < t.steps; s += t.stepsPerBar())
            JCLAP_CHECK(t.at(s).on() && leftHand(t.at(s).part));
      }
      Song shout;
      generateSong(recipeOf(7, 1, 3), shout);
      for (const Section &sec : shout.sections) {
         bool differs = false;
         for (int s = 0; s < sec.fill.steps; ++s)
            differs = differs || sec.fill.at(s).vel != sec.takes[0].at(s).vel;
         JCLAP_CHECK(differs);
      }
   }
   {
      // E5, E11: a song that has ended stays quiet, the bass coming in too, until SONG stops ending
      // it (REPEAT, LOOP): then it plays again.
      for (bool rhythmFromBass : {false, true})
         for (int change : {0, 1, 2}) {
            auto song = songOf("x...x...x...x...", {{"C", nullptr}, {"F", nullptr}});
            for (Section &sec : song->sections)
               sec.bars = 2;
            auto engine = std::make_unique<Engine>();
            Engine &e = *engine;
            e.setSong(*song);
            Settings s = plain();
            s.song = true;
            s.songEnds = true;
            s.section = kOutro;
            s.rhythmFrom = rhythmFromBass ? 1 : 0;
            e.setSettings(s);
            e.reset();
            const int64_t end = 24000LL * 8, swap = 24000LL * 12;
            const auto all = runWith(e, 24000LL * 24, 0.0, [&](int64_t at, Block &) {
               if (rhythmFromBass && at % 24000 < kBlock)
                  e.noteOn(s.bassChannel, 36, 100, 0);
               if (rhythmFromBass && (at + 12000) % 24000 < kBlock)
                  e.noteOff(s.bassChannel, 36, 0);
               if (at <= swap && at + kBlock > swap && change) {
                  Settings n = s;
                  if (change == 1)
                     n.songEnds = false;
                  else
                     n.song = false;
                  e.setSettings(n);
               }
            });
            int quiet = 0, again = 0;
            for (const Timed &t : all)
               if (t.on()) {
                  quiet += t.time > end + 1440 && t.time < swap;
                  again += t.time >= swap && t.time < swap + 24000LL * 8;
               }
            JCLAP_CHECK_EQ(quiet, 0);
            if (change)
               JCLAP_CHECK(again > 0);
            else
               JCLAP_CHECK_EQ(again, 0);
         }
   }
   {
      // E6: a waltz picked while playing keeps its bars on the host's 3/4 bar lines, though the
      // last section came in on a bar that isn't one.
      auto four = songOf("x...............", {{"C", nullptr}});
      auto three = songOf("x...............", {{"C", nullptr}});
      three->beats = 3;
      for (Section &sec : three->sections) {
         for (Pattern &t : sec.takes)
            t = fromRhythm("x...........", kRate34);
         sec.fill = fromRhythm("x...........", kRate34);
      }
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*four);
      Settings s = plain();
      e.setSettings(s);
      e.reset();
      const auto all = runWith(e, 24000LL * 30, 0.0, [&](int64_t at, Block &) {
         if (at <= 24000LL * 5 && at + kBlock > 24000LL * 5) {
            Settings n = s;
            n.section = kChorus;
            e.setSettings(n);
         }
         if (at <= 24000LL * 13 && at + kBlock > 24000LL * 13)
            e.setSong(*three);
      });
      int strikes = 0;
      for (const Timed &t : all)
         if (t.on() && t.time > 24000LL * 14) {
            ++strikes;
            const int64_t beat = (t.time + 1440) / 24000;
            JCLAP_CHECK_EQ(static_cast<int>(beat % 3), 0);
         }
      JCLAP_CHECK(strikes > 0);
   }
   {
      // E8: a section queued for the section's end leaves this section's band hits to play.
      auto song = songOf("x.x.x.x.x.x.x.x.", {{"C", nullptr}, {"F", nullptr}});
      for (Section &sec : song->sections)
         sec.marks[2] = kMarkHold;
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.switchAt = 4;
      e.setSettings(s);
      e.reset();
      const auto all = runWith(e, 24000LL * 8, 0.0, [&](int64_t at, Block &) {
         if (at <= 24000LL && at + kBlock > 24000LL) {
            Settings n = s;
            n.section = kChorus;
            e.setSettings(n);
         }
      });
      int bar2 = 0;
      for (const Timed &t : all)
         bar2 += t.on() && t.time >= 24000LL * 4 - 1440 && t.time < 24000LL * 8 - 1440 &&
                 std::abs(t.time - 24000LL * 4) > 1440;
      JCLAP_CHECK_EQ(bar2, 0);
   }
   {
      // The sixth review's smaller fixes.
      const auto named = [](const char *genre, const char *style) {
         for (int g = 0; g < genreCount(); ++g)
            if (std::string(genreAt(g).name) == genre)
               for (size_t i = 0; i < genreAt(g).styles.size(); ++i)
                  if (std::string(genreAt(g).styles[i].name) == style)
                     return std::make_pair(g, static_cast<int>(i));
         return std::make_pair(-1, -1);
      };
      const auto peak = [](const Pattern &p, int from, int to) {
         int v = 0;
         for (int s = from; s < std::min<int>(to, p.steps); ++s)
            if (p.at(s).on() && !leftHand(p.at(s).part))
               v = std::max<int>(v, p.at(s).vel);
         return v;
      };
      // B09: DENSITY in swing adds offbeat stabs, never chords on the beats.
      {
         auto song = songOf("x.....x.........", {{"C", nullptr}});
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.density = 1.0;
         s.swing = 0.64;
         s.swingFeel = 1; // swung eighths (C18: the rule is a swing comper's, not a swung-sixteenths groove's)
         e.setSettings(s);
         e.reset();
         const auto all = runWith(e, 24000LL * 16, 0.0, [](int64_t, Block &) {});
         int onBeats = 0, added = 0;
         for (const Timed &t : all)
            if (t.on()) {
               const int64_t beat = (t.time + 1440) / 24000;
               const bool near = std::abs(t.time - beat * 24000) < 1440;
               onBeats += near && beat % 4 != 0;
               added += !near;
            }
         JCLAP_CHECK_EQ(onBeats, 0);
         JCLAP_CHECK(added > 0);
      }
      // B10: Freddie Green's four in the verse, each stroke most of a beat.
      {
         const auto [g, st] = named("Swing Jazz", "Four To The Bar");
         JCLAP_CHECK(g >= 0);
         int fours = 0;
         for (uint32_t seed = 1; seed <= 6; ++seed) {
            Recipe r = recipeOf(g, st, seed);
            r.weird = 0.0;
            auto song = std::make_unique<Song>();
            generateSong(r, *song);
            const Pattern &p = song->sections[kVerse].takes[0];
            int hits = 0;
            for (int s = 0; s < p.stepsPerBar(); ++s)
               if (p.at(s).on() && !leftHand(p.at(s).part)) {
                  ++hits;
                  JCLAP_CHECK(p.at(s).len >= 3 && p.at(s).art != kArtChop);
               }
            fours += hits == 4;
         }
         JCLAP_CHECK(fours >= 4);
      }
      // B12: a bossa carries its batida into the last chord.
      {
         const auto [g, st] = named("Bossa Nova", "Batida");
         JCLAP_CHECK(g >= 0);
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, 1), *song);
         const Pattern &end = song->sections[kOutro].ending;
         const Pattern &take = song->sections[kOutro].takes[0];
         // B06: the cycle's first bar, where the outro's second-last bar falls.
         const int bar = end.stepsPerBar();
         for (int s = 0; s < bar; ++s)
            JCLAP_CHECK_EQ(end.at(s).on(), take.at(s).on());
      }
      // B13: a fill down settles: never louder than the groove it leaves.
      // C12: a fill (up) is never quieter than the groove's own accents.
      // C11: a boom-chick's ending keeps its alternating bass.
      // D9: a fill's last chord anticipates the next section at any energy.
      // D10: "push it" and "shift" leave the left hand unpushed and no push on a beat.
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            for (double energy : {0.0, 0.5}) {
               Recipe r = recipeOf(g, st, 3);
               r.energy = energy;
               r.weird = 0.6;
               auto song = std::make_unique<Song>();
               generateSong(r, *song);
               const unsigned mods = genreAt(g).styles[static_cast<size_t>(st)].mods;
               for (int si = 0; si < kSections; ++si) {
                  const Section &sec = song->sections[static_cast<size_t>(si)];
                  const Pattern &take = sec.takes[0];
                  const int bar = take.stepsPerBar();
                  if (si != kIntro && !sec.fillDown.empty())
                     JCLAP_CHECK(peak(sec.fillDown, 0, bar) <= peak(take, 0, take.steps));
                  if (!(mods & (kFixed | kLongs)) && !sec.fill.empty())
                     JCLAP_CHECK(peak(sec.fill, 0, bar) >= peak(take, 0, take.steps));
                  if (!(mods & kFixed) && !keepsOff(take, 0) && !sec.fill.empty()) {
                     int last = -1;
                     for (int s = 0; s < sec.fill.steps; ++s)
                        if (sec.fill.at(s).on() && !leftHand(sec.fill.at(s).part))
                           last = s;
                     // (pushed when it's off the beat; funk's fill is a pickup, its next One struck)
                     JCLAP_CHECK(last >= 0);
                     if (last >= 0 && std::string(genreAt(g).name) != "Funk")
                        JCLAP_CHECK(onBeat(sec.fill, last) || sec.fill.at(last).push ||
                                    sec.fill.at(last).part == kPartLine || // a riff's last note is its own
                                    genreAt(g).family == kFamilies[6]); // an ambient drift's fill pushes nothing
                     if (last >= 0 && std::string(genreAt(g).name) == "Funk")
                        JCLAP_CHECK(!sec.fill.at(last).push);
                  }
                  for (const Pattern &t : sec.takes)
                     for (int s = 0; s < t.steps; ++s)
                        if (t.at(s).on() && t.at(s).push)
                           JCLAP_CHECK(!leftHand(t.at(s).part) && !onBeat(t, s));
               }
            }
      for (const char *style : {"Boom-Chick", "Two-Step"}) {
         const auto [g, st] = named("Country", style);
         JCLAP_CHECK(g >= 0);
         auto song = std::make_unique<Song>();
         generateSong(recipeOf(g, st, 1), *song);
         const Pattern &end = song->sections[kOutro].ending;
         bool bass = false;
         for (int s = 1; s < end.stepsPerBar(); ++s)
            bass = bass || (end.at(s).on() && leftHand(end.at(s).part));
         JCLAP_CHECK(bass);
      }
      // D13: Gb's signature has Cb (its IV), and its modes'.
      JCLAP_CHECK(std::string(pitchName(11, 6, kMajor)) == "Cb");
      JCLAP_CHECK(std::string(pitchName(11, 3, kMinor)) == "Cb");
      JCLAP_CHECK(std::string(pitchName(11, 8, kDorian)) == "Cb");
      JCLAP_CHECK(std::string(pitchName(11, 1, kMixolydian)) == "Cb");
      JCLAP_CHECK(std::string(pitchName(11, 1, kMajor)) == "B");
      JCLAP_CHECK(std::string(pitchName(4, 6, kMajor)) == "E");
      {
         Chord c;
         JCLAP_CHECK(parseChord("Cb", c));
         JCLAP_CHECK_EQ(static_cast<int>(c.root), 11);
      }
      // D14: a damaged song's quality is held to the table; a pattern off the song's meter is refused.
      JCLAP_CHECK_EQ(kQualityLimit, qualityCount());
      {
         auto song = songOf("x...x...x...x...", {{"C", nullptr}});
         std::vector<uint8_t> data = song->save();
         // The first section's first chord's quality: after the header (version, count, key, beats)
         // and the section's bars, energy, locked, progBars, split, seed and generation.
         const size_t at = 4 + 4 + 1 + 1 + 4 + 4 + 4 + 4 + 1;
         JCLAP_CHECK_EQ(static_cast<int>(data[at]), static_cast<int>(song->sections[0].chords[0].quality));
         data[at] = 200;
         auto back = std::make_unique<Song>();
         JCLAP_CHECK(back->load(data.data(), data.size()));
         JCLAP_CHECK(back->sections[0].chords[0].quality < qualityCount());
         data = song->save();
         data[9] = 3; // a waltz's bars, its patterns in sixteenths
         JCLAP_CHECK(!back->load(data.data(), data.size()));
      }
      // E14: ACC is louder than the take's own accents.
      {
         auto song = songOf("X...x...x...x...", {{"C", nullptr}, {"F", nullptr}});
         for (Section &sec : song->sections)
            sec.marks[2] = kMarkAccent;
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         e.setSettings(plain());
         e.reset();
         const auto all = runWith(e, 24000LL * 8, 0.0, [](int64_t, Block &) {});
         int bar1 = 0, bar2 = 0;
         for (const Timed &t : all)
            if (t.on()) {
               if (std::abs(t.time) < 1440)
                  bar1 = std::max<int>(bar1, t.m.data2);
               if (std::abs(t.time - 96000) < 1440)
                  bar2 = std::max<int>(bar2, t.m.data2);
            }
         JCLAP_CHECK(bar2 > bar1);
      }
      // E16: a one-bar outro still ends on its last chord rung out.
      {
         auto song = songOf("x...x...x...x...", {{"C", nullptr}});
         for (Section &sec : song->sections) {
            sec.bars = 1;
            sec.ending = fromRhythm("x...x...x...x...|X---------------");
         }
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.song = true;
         s.songEnds = true;
         s.section = kOutro;
         e.setSettings(s);
         e.reset();
         const auto all = runWith(e, 24000LL * 8, 0.0, [](int64_t, Block &) {});
         std::set<int64_t> strikes;
         for (const Timed &t : all)
            if (t.on())
               strikes.insert((t.time + 1440) / 6000);
         JCLAP_CHECK(strikes == (std::set<int64_t>{0}));
      }
   }
   {
      // A12: auto fills follow a looping 12-bar section's form: its 8th and 12th bars, then the
      // 20th and 24th, not every 8th bar of the host.
      auto song = songOf("x...x...x...x...", {{"C", nullptr}});
      for (Section &sec : song->sections) {
         sec.bars = 12;
         for (Pattern &t : sec.takes)
            t = fromRhythm("x...............");
         sec.fill = fromRhythm("x...x...x...x...");
      }
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.autoFillBars = 8;
      e.setSettings(s);
      e.reset();
      const auto all = runWith(e, 24000LL * 4 * 24, 0.0, [](int64_t, Block &) {});
      std::set<int> fills;
      for (const Timed &t : all)
         if (t.on() && std::abs(t.time % 96000 - 48000) < 1440)
            fills.insert(static_cast<int>(t.time / 96000) + 1);
      JCLAP_CHECK(fills == (std::set<int>{8, 12, 20, 24}));
   }
   {
      // C07: DENSITY keeps a boom-chick's (and a waltz's) alternating bass and drops chicks first.
      const Pattern boom = fromRhythm("b...x...f...x...");
      const Pattern oom = fromRhythm("b...x...x...|f...x...x...", kRate34);
      for (double density : {0.05, 0.2, 0.35, 0.45})
         for (double d : {0.0, 0.5, 1.0}) {
            JCLAP_CHECK(!thinnedBy(boom, 0, density, d, false));
            JCLAP_CHECK(!thinnedBy(boom, 8, density, d, false));
            JCLAP_CHECK(!thinnedBy(oom, 0, density, d, false));
            JCLAP_CHECK(!thinnedBy(oom, 12, density, d, false));
         }
      // C08, A14: the pad takes a pushed chord with the comp, not on the beat after it.
      auto song = songOf("x.....x.......x.", {{"C", nullptr}, {"F", nullptr}});
      for (Section &sec : song->sections)
         for (Pattern &t : sec.takes)
            t.at(14).push = t.at(30).push = 1;
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.pad = true;
      s.padChannel = 5;
      e.setSettings(s);
      e.reset();
      const auto all = runWith(e, 24000LL * 8, 0.0, [](int64_t, Block &) {});
      std::set<int> padAtPush;
      for (const Timed &t : all)
         if (t.on() && (t.m.status & 15) == 5 && std::abs(t.time - 84000) < 1440)
            padAtPush.insert(t.m.data1 % 12);
      JCLAP_CHECK(padAtPush.count(5) && padAtPush.count(9));
   }
   {
      // C10, D4: 4THS keeps an add9's and a maj9's 9th (Eno's "1/2" chord is an add9).
      std::vector<double> amb = defaultValues();
      for (const auto &[id, x] : styleValues(23, 0))
         amb[id] = x;
      VoiceSettings vs = voiceSettings([&](clap_id id) { return amb[id]; });
      JCLAP_CHECK_EQ(static_cast<int>(vs.shape), static_cast<int>(kQuartal));
      for (const char *name : {"Eadd9", "Cadd9", "Amaj9", "Fmaj9", "Dadd9"}) {
         const Chord c = chord(name);
         const Voicing v = voice(c, nullptr, vs, -1, nullptr);
         bool nine = false;
         for (int k = 0; k < v.count; ++k)
            nine = nine || ((v.notes[static_cast<size_t>(k)] - c.root) % 12 + 12) % 12 == 2;
         JCLAP_CHECK(nine);
      }
   }
   {
      // D7: BLEND brings in another genre's rhythm, not its melody line or a left hand.
      for (int g = 0; g < genreCount(); g += 3)
         for (int g2 : {0, 15, 12}) {
            Recipe r = recipeOf(g, 0, 7);
            r.genre2 = g2;
            r.blend = 1.0;
            Song song;
            generateSong(r, song);
            Recipe plainR = recipeOf(g, 0, 7);
            Song own;
            generateSong(plainR, own);
            for (int i = 0; i < kSections; ++i) {
               const Pattern &t = song.sections[static_cast<size_t>(i)].takes[0];
               const Pattern &o = own.sections[static_cast<size_t>(i)].takes[0];
               if (!hasLine(o))
                  JCLAP_CHECK(!hasLine(t));
            }
         }
   }
   {
      // D8: a new ENERGY or BLEND keeps the chords as edited and their band hits.
      Recipe r = recipeOf(0, 0, 9);
      Song song;
      generateSong(r, song);
      song.sections[kVerse].chords[0] = chord("F#m7");
      song.sections[kVerse].marks[0] = kMarkHold;
      r.energy = 0.9;
      generateSong(r, song, true);
      JCLAP_CHECK(chordName(song.sections[kVerse].chords[0], 4) == "F#m7");
      JCLAP_CHECK_EQ(static_cast<int>(song.sections[kVerse].marks[0]), static_cast<int>(kMarkHold));
      generateSong(r, song);
      JCLAP_CHECK(chordName(song.sections[kVerse].chords[0], 4) != "F#m7");
   }

   // ---- the seventh review's fixes (0.8.0) ----
   {
      // B01, D01: a rootless 7#5 keeps its #5 (no natural fifth); D06: OCTAVES keeps an altered fifth
      // and an altered ninth.
      VoiceSettings q;
      q.shape = kRootless;
      q.voices = 4;
      q.lo = 48;
      q.hi = 76;
      q.center = 62;
      for (const Voicing &v : candidates(chord("C7#5"), q)) {
         bool sharp5 = false;
         for (int i = 0; i < v.count; ++i) {
            JCLAP_CHECK(v.notes[static_cast<size_t>(i)] % 12 != 7);
            sharp5 = sharp5 || v.notes[static_cast<size_t>(i)] % 12 == 8;
         }
         JCLAP_CHECK(sharp5);
      }
      q.shape = kOctaves;
      for (int voices : {4, 5}) {
         q.voices = voices;
         for (const auto &[name, pc] : std::vector<std::pair<const char *, int>>{{"Cm7b5", 6}, {"C7b9", 1}, {"C7#9", 3}, {"C7#5", 8}}) {
            const Voicing v = voice(chord(name), nullptr, q);
            JCLAP_CHECK(!v.empty());
            bool has = false;
            for (int i = 0; i < v.count; ++i)
               has = has || v.notes[static_cast<size_t>(i)] % 12 == pc;
            JCLAP_CHECK(has);
         }
      }
   }
   {
      // E02: a new key moves the chords kept (ENERGY or BLEND after a key change), as it moves a
      // locked section's.
      Recipe r = recipeOf(0, 0, 9);
      Song song;
      generateSong(r, song);
      song.sections[kVerse].chords[0] = chord("F#m7");
      r.key = (r.key + 2) % 12;
      generateSong(r, song, true);
      JCLAP_CHECK(song.sections[kVerse].chords[0] == chord("G#m7"));
   }
   {
      // E01: a jump back out of a fill plays the take where it lands, not the rest of the fill.
      auto song = songOf("x...............", {{"C", nullptr}, {"F", nullptr}});
      for (Section &sec : song->sections)
         sec.fill = fromRhythm("x.x.x.x.x.x.x.x.");
      auto engine = std::make_unique<Engine>();
      Engine &e = *engine;
      e.setSong(*song);
      Settings s = plain();
      s.autoFillBars = 4;
      e.setSettings(s);
      e.reset();
      bool jumped = false;
      int64_t jumpAt = -1;
      const auto all = runWith(e, 24000 * 24, 0.0, [&](int64_t at, Block &b) {
         if (!jumped && b.beats >= 13.0) {
            jumped = true;
            jumpAt = at;
            b.beats = 0.0;
         }
      });
      JCLAP_CHECK(jumped);
      std::set<int64_t> onsets;
      for (const Timed &t : all)
         if (t.on() && t.time >= jumpAt && t.time < jumpAt + 24000 * 7 / 2) // bar 1, its downbeat
            onsets.insert((t.time - jumpAt) / 2400);
      JCLAP_CHECK_EQ(static_cast<int>(onsets.size()), 1);
   }
   {
      // B03: a figure played as written isn't tied over: its next downbeat is struck. E08: a band hit
      // on a chord the take pushes into is that push, held: not struck again on the beat.
      for (bool fixed : {true, false}) {
         auto song = songOf("x.............x.", {{"C", nullptr}, {"F", nullptr}});
         for (Section &sec : song->sections) {
            for (Pattern &t : sec.takes)
               t.at(14).push = 1;
            sec.marks[2] = fixed ? kMarkNone : kMarkHold;
         }
         auto engine = std::make_unique<Engine>();
         Engine &e = *engine;
         e.setSong(*song);
         Settings s = plain();
         s.fixed = fixed;
         s.section = kVerse;
         e.setSettings(s);
         e.reset();
         const auto all = runWith(e, 24000 * 8, 0.0, [](int64_t, Block &) {});
         JCLAP_CHECK(startingAt(all, 84000) == (std::set<int>{0, 5, 9}));
         if (fixed)
            JCLAP_CHECK(!startingAt(all, 96000).empty());
         else
            JCLAP_CHECK(startingAt(all, 96000).empty());
      }
   }
   {
      // E10: a 6/8 song saved before 0.8.0 on the 3/4 grid loads on the 6/8 one.
      for (int g = 0; g < genreCount(); ++g)
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st) {
            if (!(genreAt(g).styles[static_cast<size_t>(st)].mods & kSixEight))
               continue;
            const Recipe r = recipeOf(g, st, 5);
            Song song;
            generateSong(r, song);
            for (Section &sec : song.sections) {
               for (Pattern &t : sec.takes)
                  t.rate = kRate34;
               sec.fill.rate = sec.fillDown.rate = sec.ending.rate = kRate34;
            }
            completeSong(r, song, std::vector<uint8_t>{3, 0, 0, 0});
            for (const Section &sec : song.sections) {
               for (const Pattern &t : sec.takes)
                  JCLAP_CHECK(t.rate == kRate68);
               JCLAP_CHECK(sec.fill.rate == kRate68 && sec.ending.rate == kRate68);
            }
         }
   }
   {
      // D07: the answer leaves a line's notes and a left hand's as written. D10: a hit a mutation adds
      // is no louder than the take's own.
      const Pattern line = fromRhythm("1.3.45..54.3.41.|1.3.45..54.3.41.");
      const Pattern palm = fromRhythm("b.x.f3b5f...b3f5|b.x.f3b5f...b3f5");
      for (uint32_t seed = 1; seed <= 60; ++seed)
         for (const Pattern *base : {&line, &palm}) {
            Pattern p = *base;
            Rng rng(seed);
            applyMutation(p, "answer", rng);
            for (int k = 0; k < base->steps; ++k)
               if (base->at(k).on() && (base->at(k).part == kPartLine || leftHand(base->at(k).part)))
                  JCLAP_CHECK(p.at(k) == base->at(k));
         }
      JCLAP_CHECK(!mutationFits("answer", 0, line));
      for (const char *tag : {"busier", "push it", "answer", "ghosts", "shift"})
         for (uint32_t seed = 1; seed <= 30; ++seed) {
            Pattern p = fromRhythm("x.......x.......|x.......x.......");
            for (int k = 0; k < p.steps; ++k)
               if (p.at(k).on())
                  p.at(k).vel = 41;
            Rng rng(seed);
            applyMutation(p, tag, rng);
            for (int k = 0; k < p.steps; ++k)
               if (p.at(k).on())
                  JCLAP_CHECK(p.at(k).vel <= 41);
         }
   }
   {
      // A06: WEIRD gives the blues no suspended approach chords; C06: an ambient fill pushes nothing;
      // A09: funk's fill is a pickup, not pushed over the next One.
      for (int g = 0; g < genreCount(); ++g) {
         const std::string name = genreAt(g).name;
         for (int st = 0; st < static_cast<int>(genreAt(g).styles.size()); ++st)
            for (uint32_t seed = 1; seed <= 12; ++seed) {
               Recipe r = recipeOf(g, st, seed);
               r.weird = 1.0;
               Song song;
               generateSong(r, song);
               for (const Section &sec : song.sections) {
                  if (name == "Blues")
                     for (int k = 0; k < sec.progBars * 2; ++k)
                        JCLAP_CHECK(sec.chords[static_cast<size_t>(k)].quality != kSus);
                  if (name == "Ambient" || name == "Funk")
                     for (int k = 0; k < sec.fill.steps; ++k)
                        JCLAP_CHECK(!sec.fill.at(k).push);
               }
            }
      }
   }
   {
      // A10: a tritone sub falling a half step is spelled flat (Ab13 to G in E minor, not G#13).
      const Chord to = chord("Gmaj9");
      JCLAP_CHECK(chordName(chord("Ab13"), 4, kMinor, &to) == "Ab13");
   }

   roundEight();
   roundNine();
   roundTen();
   roundEleven();
   roundTwelve();
   roundThirteen();
   roundFourteen();
   roundFourteenB();
   roundFourteenC();
   roundFifteenC();
   roundFifteenD();
   roundFifteenE();
   roundSixteenD();
   roundSixteenC();
   roundSixteenE();
   roundSeventeenD();
   roundSeventeenC();
   roundSeventeenE();
   roundEighteenC();
   roundEighteenD();
   roundEighteenE();
   roundNineteenC();
   roundNineteenD();
   roundNineteenE();
   songStructure();
   return jclap::core::testResult();
}
