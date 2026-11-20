// JComp's harmony, library, generator and engine on their own: chord names, voicings that keep to
// their window and the low-interval limits, every style's progressions and rhythms, songs made the
// same from the same seed, mutations, and the engine playing chords with the host, from held notes
// and from section notes.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstdio>
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
Song songOf(const char *rhythm, std::vector<std::pair<const char *, const char *>> bars) {
   Song song;
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
   return song;
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

} // namespace

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
                  if (b - a < 12 && b - a > 0)
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
      // A passing run steps from one note to the next in the scale.
      const Ints r = passingRun(67, 72, 3, 0, kMajor, false);
      JCLAP_CHECK_EQ(static_cast<int>(r.size()), 3);
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
            Song song;
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
      Song song;
      const Recipe r = recipeOf(0, 0, 42);
      generateSong(r, song);
      Engine e;
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
      Song song;
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
         Engine e;
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
         Song song;
         const Recipe r = recipeOf(g, st, 7);
         generateSong(r, song);
         std::vector<double> v = defaultValues();
         for (const auto &[id, x] : styleValues(g, st))
            v[id] = x;
         Engine e;
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
      Song song;
      generateSong(recipeOf(0, 0, 42), song);
      Engine e;
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
      Song song;
      generateSong(recipeOf(0, 0, 42), song);
      Engine e;
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
            Song song;
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
            Song song;
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
            Song song;
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
            const Ints run = passingRun(from, 67, len, 0, kDorian, false, chordTones(chord("Abmaj7")));
            for (int n : run)
               JCLAP_CHECK(n % 12 != 9);
         }
      // Country boom-chick: the root on 1, the fifth on 3, and the chord on 2 and 4 without the
      // left hand's low note under it.
      {
         const int g = genreNamed("Country"), st = styleNamed(g, "Boom-Chick");
         Song song;
         const Recipe r = recipeOf(g, st, 3);
         generateSong(r, song);
         Engine e;
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
         Song song;
         const Recipe r = recipeOf(g, st, 5);
         generateSong(r, song);
         Engine e;
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
      Engine e;
      e.setSong(songOf("x---------------", {{"C", "G"}}));
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
      Engine e;
      e.setSong(songOf("x.....x.........", {{"C", "G"}}));
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
            Song song;
            const Recipe r = recipeOf(g, st, 11);
            generateSong(r, song);
            std::vector<double> v = defaultValues();
            for (const auto &[id, x] : styleValues(g, st))
               v[id] = x;
            Engine e;
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
      Engine e;
      e.setSong(songOf("x...............|..x.............", {{"C", nullptr}}));
      e.setSettings(plain());
      e.reset();
      const std::vector<Timed> out = runWith(e, 24000 * 4, 4.0, [](int64_t, Block &) {});
      JCLAP_CHECK(startingAt(out, 0).empty());
      JCLAP_CHECK(!startingAt(out, 12000).empty());
   }
   {
      // DENSITY adds nothing to a figure played as written, nor a chord on the beat to an offbeat take.
      const auto count = [](const char *rhythm, bool fixed, double density, bool beats) {
         Engine e;
         e.setSong(songOf(rhythm, {{"C", nullptr}}));
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
      JCLAP_CHECK(count("x..x..x...x..x..", false, 1.0, false) > count("x..x..x...x..x..", false, 0.5, false));
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
                  const Ints r = passingRun(from, target, len, 0, kMajor, half, chordTones(chord("C")));
                  for (size_t k = 1; k < r.size(); ++k)
                     JCLAP_CHECK(std::abs(r[k] - r[k - 1]) <= 3 || std::abs(from - target) > 3 * len + 3);
               }
   }
   {
      // LATCH keeps the chord as its keys are let go one at a time, and after.
      Engine e;
      e.setSong(songOf("x...x...x...x...", {{"F", nullptr}}));
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
         Engine e;
         e.setSong(songOf("x...x...x...x...", {{"C", nullptr}}));
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
               JCLAP_CHECK(t.time <= std::max<int64_t>(48000, when) + 300);
      }
   }
   {
      // PLAY ONLY WHILE A CHORD IS HELD works with the progression's chords too.
      Engine e;
      e.setSong(songOf("x...x...x...x...", {{"C", nullptr}}));
      Settings s = plain();
      s.heldOnly = true;
      e.setSettings(s);
      e.reset();
      JCLAP_CHECK(run(e, 24000 * 2).empty());
      JCLAP_CHECK(!run(e, 24000 * 2, {72}).empty());
   }
   {
      // A host loop: every pass starts with its downbeat chord, held its length.
      Engine e;
      e.setSong(songOf("x---............", {{"C", nullptr}}));
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
      Engine e;
      e.setSong(songOf("x---------------", {{"C", nullptr}}));
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
      Engine e;
      Song song = songOf("x...............", {{"C", nullptr}});
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
      Engine e;
      e.setSong(songOf("1...5...8...3...", {{"C", nullptr}}));
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
      Engine e;
      e.setSong(songOf("x.x.x.x.x.x.x.x.", {{"C", nullptr}}));
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
      // The slip note: a grace note a whole step under an inner note of the chord, just before it.
      Engine e;
      e.setSong(songOf("X...X...X...X...", {{"C", nullptr}}));
      Settings s = plain();
      s.slip = true;
      e.setSettings(s);
      e.reset();
      const std::vector<Timed> out = run(e, 24000 * 8);
      int slips = 0;
      for (const Timed &t : out)
         if (t.on() && t.time % 12000 > 10000) {
            for (const Timed &u : out)
               slips += u.on() && u.m.data1 == t.m.data1 + 2 && u.time > t.time && u.time - t.time < 2400;
         }
      JCLAP_CHECK(slips >= 4);
   }
   {
      // Nothing allocated while playing: held chords, runs, the bass coming in.
      Engine e;
      Song song;
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

   return jclap::core::testResult();
}
