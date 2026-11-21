#pragma once

// The song JComp plays, and what the audio thread and the panel pass each other.
//
// A song is seven sections (intro, verse, pre, chorus, bridge, break, outro) in a key. Each section
// has its chords (a progression of up to twelve bars, two chords a bar at most), four takes of its
// comping rhythm, a fill, how many bars it runs and the energy it was made with. A take is a grid
// of hits: one chord (or part of one) per step, up to four bars of sixteenths. Which notes a hit
// plays isn't stored: the engine voices each chord as it comes, led from the chord before (see
// harmony.h), so a take plays over any chords. A hit can still ask for a voicing of its own.
//
// The panel owns the song and sends the whole of it whenever it changes; the audio thread publishes
// a Frame every block for the panel to draw. Neither side waits for the other.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace jcomp {

// A vector of at most N, kept in place: what the audio thread uses where it would otherwise grow a
// std::vector (voicing a chord, naming held keys, a passing run), so it never allocates. Past N it
// drops what doesn't fit.
template <typename T, size_t N>
struct FixedVec {
   std::array<T, N> items{};
   size_t n = 0;
   FixedVec() = default;
   FixedVec(std::initializer_list<T> list) {
      for (const T &x : list)
         push_back(x);
   }
   template <typename It>
   FixedVec(It first, It last) {
      for (; first != last; ++first)
         push_back(*first);
   }
   T *begin() { return items.data(); }
   T *end() { return items.data() + n; }
   const T *begin() const { return items.data(); }
   const T *end() const { return items.data() + n; }
   size_t size() const { return n; }
   bool empty() const { return n == 0; }
   bool full() const { return n >= N; }
   void clear() { n = 0; }
   T &operator[](size_t i) { return items[i]; }
   const T &operator[](size_t i) const { return items[i]; }
   T &front() { return items[0]; }
   const T &front() const { return items[0]; }
   T &back() { return items[n - 1]; }
   const T &back() const { return items[n - 1]; }
   void push_back(const T &x) {
      if (n < N)
         items[n++] = x;
   }
   void pop_back() {
      if (n)
         --n;
   }
   T *insert(T *pos, const T &x) {
      const size_t at = static_cast<size_t>(pos - begin());
      if (n >= N)
         return pos;
      for (size_t i = n; i > at; --i)
         items[i] = items[i - 1];
      items[at] = x;
      ++n;
      return begin() + at;
   }
   template <typename It>
   void insert(T *pos, It first, It last) {
      size_t at = static_cast<size_t>(pos - begin());
      for (; first != last; ++first)
         insert(begin() + at++, *first);
   }
   T *erase(T *first, T *last) {
      const size_t a = static_cast<size_t>(first - begin()), b = static_cast<size_t>(last - begin());
      for (size_t i = b; i < n; ++i)
         items[a + i - b] = items[i];
      n -= b - a;
      return begin() + a;
   }
   T *erase(T *pos) { return erase(pos, pos + 1); }
   bool operator==(const FixedVec &o) const {
      if (n != o.n)
         return false;
      for (size_t i = 0; i < n; ++i)
         if (!(items[i] == o.items[i]))
            return false;
      return true;
   }
};

constexpr int kSections = 7;
constexpr int kTakes = 4;
// How many chord qualities there are (harmony.h's table; a test holds the two together).
constexpr int kQualityLimit = 29;
constexpr int kMaxSteps = 64;   // four bars of sixteenths
constexpr int kMaxProgBars = 12; // a progression loops after up to twelve bars (a 12-bar blues)
constexpr int kSlots = kMaxProgBars * 2;
constexpr int kMaxBars = 64;
constexpr int kMaxVoices = 6;
constexpr int kMaxNudge = 40; // ms either way

// Hit velocities: what the panel and the generator call accented, normal, soft and ghosted.
constexpr int kAccent = 112, kNormal = 92, kSoft = 70, kGhost = 44;

enum SectionId { kIntro, kVerse, kPre, kChorus, kBridge, kBreak, kOutro };
inline const char *const kSectionNames[kSections] = {"Intro", "Verse", "Pre", "Chorus", "Bridge", "Break", "Outro"};
inline const char *const kSectionCaps[kSections] = {"INTRO", "VERSE", "PRE", "CHORUS", "BRIDGE", "BREAK", "OUTRO"};
// Each section's place in a song's arc, before ENERGY moves them all.
inline const double kArcEnergy[kSections] = {0.3, 0.5, 0.65, 0.9, 0.55, 0.3, 0.4};
inline const int kSectionBars[kSections] = {4, 8, 4, 8, 8, 4, 8};

// How long a hit's notes sound: as the take wrote it (its length), cut short, half, held to the next
// chord, or with the lower notes let go so only the top keeps singing.
enum Articulation : uint8_t { kArtAsWritten, kArtChop, kArtHalf, kArtRing, kArtTopRings };
inline const char *const kArtNames[] = {"As written", "Chop", "Half", "Ring", "Top rings"};
// Which of the chord a hit plays: all of it, the top note, the top two, or a bass note (a pianist's
// left hand, a guitarist's thumb): the root, or the fifth the bass alternates with.
enum Part : uint8_t { kPartChord, kPartTop, kPartUpper, kPartBass, kPartFifth, kPartLine };
inline const char *const kPartNames[] = {"Chord", "Top note", "Top two", "Bass", "Bass 5th", "Line"};
inline bool leftHand(uint8_t part) { return part == kPartBass || part == kPartFifth; }
// A hit that sounds the chord (all of it, its top or its upper two), not a left hand's note or a line's.
inline bool chordPart(uint8_t part) { return part == kPartChord || part == kPartUpper || part == kPartTop; }

// One chord stab on one step. A velocity of 0 is no hit.
struct Hit {
   uint8_t vel = 0;
   uint8_t len = 1;      // in steps
   uint8_t chance = 100; // percent of the times round it plays
   int8_t nudge = 0;     // ms early (-) or late (+)
   uint8_t art = kArtAsWritten;
   uint8_t part = kPartChord;
   uint8_t run = 0;      // a passing run leads into it, when the chord changes here
   uint8_t push = 0;     // an anticipation: it plays the chord that comes an eighth later
   int8_t voicing = -1;  // -1 led by the engine, else the n-th shape (see harmony.h rankShapes)
   uint8_t degree = 1;   // a line note's degree of the chord, 1..9 (kPartLine)
   bool on() const { return vel > 0; }
   bool operator==(const Hit &o) const {
      return vel == o.vel && (!vel || (len == o.len && chance == o.chance && nudge == o.nudge && art == o.art &&
                                       part == o.part && run == o.run && push == o.push && voicing == o.voicing &&
                                       degree == o.degree));
   }
   bool operator!=(const Hit &o) const { return !(*this == o); }
};

// Step lengths: sixteenths, eighth triplets (12/8 and the shuffle: three to a beat, twelve a bar), or
// sixteenths in a bar of three beats (3/4, and 6/8 counted in its eighths: twelve a bar).
// kRate34 and kRate68: a bar of three quarter beats in sixteenths (12 steps); 3/4 counts them in
// three quarters, 6/8 in two dotted quarters (six eighths, accents on 1 and 4).
enum Rate : uint8_t { kRate16, kRate8T, kRate34, kRate68 };
inline const char *const kRateNames[] = {"1/16", "1/8T", "1/16 in 3", "1/16 in 6/8"};
inline const int kRateTicks[] = {12, 16, 12, 12};
constexpr int kTicksPerBeat = 48;
constexpr int kBeatsPerBar = 4; // a 4/4 bar's (a song in 3/4 has three: Song::beats)
constexpr int kTicksPerBar = kTicksPerBeat * kBeatsPerBar;

struct Pattern {
   uint8_t steps = 16;
   uint8_t rate = kRate16;
   std::array<Hit, kMaxSteps> hits{};

   Hit &at(int step) { return hits[static_cast<size_t>(step)]; }
   const Hit &at(int step) const { return hits[static_cast<size_t>(step)]; }
   int count() const {
      int n = 0;
      for (int s = 0; s < steps; ++s)
         n += hits[static_cast<size_t>(s)].on();
      return n;
   }
   bool empty() const { return count() == 0; }
   int stepsPerBar() const { return rate == kRate16 ? 16 : 12; }
   // A beat's steps: a quarter, a triplet's three, or 6/8's dotted quarter.
   int perBeat() const { return rate == kRate8T ? 3 : rate == kRate68 ? 6 : 4; }
   bool inThree() const { return rate == kRate34 || rate == kRate68; }
   int beats() const { return inThree() ? 3 : 4; } // quarter beats a bar
   int stepTicks() const { return kRateTicks[std::min<int>(rate, kRate68)]; }
   int bars() const { return std::max(1, (steps + stepsPerBar() - 1) / stepsPerBar()); }
   void clear() { hits.fill(Hit{}); }
   bool operator==(const Pattern &o) const { return steps == o.steps && rate == o.rate && hits == o.hits; }
   bool operator!=(const Pattern &o) const { return !(*this == o); }
};

inline int patternTicks(const Pattern &p) { return p.steps * p.stepTicks(); }
// A bar's strong beats, where a left hand plays and a take's anchors fall: 1 and 3 in four, 1 in three, 1 and 4 in 6/8.
inline int strongSteps(const Pattern &p) { return p.rate == kRate34 ? 12 : p.rate == kRate68 ? 6 : p.perBeat() * 2; }
// Where a bar's second chord comes in (its second slot): half way, but on beat 3 in a waltz.
inline int halfBar(const Pattern &p) { return p.rate == kRate34 ? 8 : p.stepsPerBar() / 2; }

// A chord: a root (0 = C), a quality (an index into harmony.h's kQualities) and a bass note for a
// slash chord (-1 the root). A root of -1 is no chord (N.C.): the comp rests.
struct Chord {
   int8_t root = -1;
   uint8_t quality = 0;
   int8_t bass = -1;
   bool none() const { return root < 0; }
   bool operator==(const Chord &o) const { return root == o.root && (root < 0 || (quality == o.quality && bass == o.bass)); }
   bool operator!=(const Chord &o) const { return !(*this == o); }
};

// A band hit on a chord (written over it in a chart): the take plays on with the chord's first
// stroke accented, or the chord is struck once and held to the next (hold), or struck once and cut
// short (a shot), the take resting meanwhile.
enum Mark : uint8_t { kMarkNone, kMarkAccent, kMarkHold, kMarkShot };
inline const char *const kMarkNames[] = {"Comp", "Accent", "Hold", "Shot"};

struct Section {
   std::array<Pattern, kTakes> takes{};
   Pattern fill;
   // A fill down into a quieter section (or the intro's held chord setting up the verse), and the
   // song's ending (the outro's, its last bar or two): empty where there is none.
   Pattern fillDown, ending;
   // The progression: two slots a bar (the bar's first and second half). A second half with
   // `split` off for that bar holds the first half's chord.
   std::array<Chord, kSlots> chords{};
   uint16_t split = 0; // bit b: bar b changes chord half way
   std::array<uint8_t, kSlots> marks{}; // a band hit on each chord (Mark), by slot
   uint8_t progBars = 4;
   uint8_t bars = 8;
   uint8_t energy = 50; // 0..100, as made
   uint8_t locked = 0;
   uint32_t seed = 1;
   uint16_t generation = 0;

   // The chord at a step (16ths from the section's start) of a progression.
   const Chord &chordAt(int bar, bool secondHalf) const {
      const int b = ((bar % std::max<int>(1, progBars)) + std::max<int>(1, progBars)) % std::max<int>(1, progBars);
      const bool second = secondHalf && (split >> b & 1);
      return chords[static_cast<size_t>(b * 2 + (second ? 1 : 0))];
   }
   bool operator==(const Section &o) const {
      return takes == o.takes && fill == o.fill && fillDown == o.fillDown && ending == o.ending && chords == o.chords &&
             split == o.split && marks == o.marks && progBars == o.progBars &&
             bars == o.bars && energy == o.energy && locked == o.locked && seed == o.seed && generation == o.generation;
   }
   bool operator!=(const Section &o) const { return !(*this == o); }
};

struct Song {
   std::array<Section, kSections> sections{};
   uint8_t key = 4; // E
   uint8_t beats = 4; // a bar's beats: 4, or 3 (a waltz, a 6/8)
   bool operator==(const Song &o) const { return sections == o.sections && key == o.key && beats == o.beats; }
   bool operator!=(const Song &o) const { return !(*this == o); }

   int totalBars() const {
      int n = 0;
      for (const Section &s : sections)
         n += s.bars;
      return n;
   }

   // Saved as bytes: a version, the key, then each section's fields, its chords and its patterns
   // (four takes and the fill), each pattern as its steps, rate and every hit (10 bytes; 9 before version 2) of its grid.
   // Version 3 adds the bar's beats after the key, and after each section's fill its fill down, its
   // ending and its chords' band hits.
   static constexpr uint32_t kVersion = 4; // 2: a hit's line degree; 3: 3/4, fills down, endings, band hits; 4: 6/8

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
         for (const Hit &h : p.hits) {
            u8(h.vel);
            u8(h.len);
            u8(h.chance);
            u8(static_cast<uint8_t>(h.nudge));
            u8(h.art);
            u8(h.part);
            u8(h.run);
            u8(h.push);
            u8(static_cast<uint8_t>(h.voicing));
            u8(h.degree);
         }
      };
      u32(kVersion);
      u32(kSections);
      u8(key);
      u8(beats);
      for (const Section &s : sections) {
         u8(s.bars);
         u8(s.energy);
         u8(s.locked);
         u8(s.progBars);
         u32(s.split);
         u32(s.seed);
         u32(s.generation);
         for (const Chord &c : s.chords) {
            u8(static_cast<uint8_t>(c.root));
            u8(c.quality);
            u8(static_cast<uint8_t>(c.bass));
         }
         for (const Pattern &p : s.takes)
            pattern(p);
         pattern(s.fill);
         pattern(s.fillDown);
         pattern(s.ending);
         for (uint8_t m : s.marks)
            u8(m);
      }
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
      uint32_t version = 0;
      auto pattern = [&](Pattern &p) {
         p.steps = static_cast<uint8_t>(std::clamp<int>(u8(), 1, kMaxSteps));
         p.rate = static_cast<uint8_t>(std::min<int>(u8(), version >= 3 ? kRate68 : kRate8T));
         for (Hit &h : p.hits) {
            h.vel = static_cast<uint8_t>(std::min<int>(u8(), 127));
            h.len = static_cast<uint8_t>(std::clamp<int>(u8(), 1, kMaxSteps));
            h.chance = static_cast<uint8_t>(std::min<int>(u8(), 100));
            h.nudge = static_cast<int8_t>(std::clamp<int>(static_cast<int8_t>(u8()), -kMaxNudge, kMaxNudge));
            h.art = static_cast<uint8_t>(std::min<int>(u8(), kArtTopRings));
            h.part = static_cast<uint8_t>(std::min<int>(u8(), kPartLine));
            h.run = u8() ? 1 : 0;
            h.push = u8() ? 1 : 0;
            h.voicing = static_cast<int8_t>(std::clamp<int>(static_cast<int8_t>(u8()), -1, 7));
            h.degree = version >= 2 ? static_cast<uint8_t>(std::clamp<int>(u8(), 1, 9)) : 1;
         }
      };
      version = u32();
      const uint32_t count = u32();
      if (!ok || version < 1 || version > kVersion || count != kSections)
         return false;
      Song loaded;
      loaded.key = static_cast<uint8_t>(u8() % 12);
      loaded.beats = version >= 3 ? (u8() == 3 ? 3 : 4) : 4;
      for (Section &s : loaded.sections) {
         s.bars = static_cast<uint8_t>(std::clamp<int>(u8(), 1, kMaxBars));
         s.energy = static_cast<uint8_t>(std::min<int>(u8(), 100));
         s.locked = u8() ? 1 : 0;
         s.progBars = static_cast<uint8_t>(std::clamp<int>(u8(), 1, kMaxProgBars));
         s.split = static_cast<uint16_t>(u32() & 0xfff);
         s.seed = u32();
         s.generation = static_cast<uint16_t>(std::min<uint32_t>(u32(), 65535));
         for (Chord &c : s.chords) {
            const int r = static_cast<int8_t>(u8());
            c.root = static_cast<int8_t>(r < 0 || r > 11 ? -1 : r);
            c.quality = static_cast<uint8_t>(std::min<int>(u8(), kQualityLimit - 1));
            const int b = static_cast<int8_t>(u8());
            c.bass = static_cast<int8_t>(b < 0 || b > 11 ? -1 : b);
         }
         for (Pattern &p : s.takes)
            pattern(p);
         pattern(s.fill);
         if (version >= 3) {
            pattern(s.fillDown);
            pattern(s.ending);
            for (uint8_t &m : s.marks)
               m = static_cast<uint8_t>(std::min<int>(u8(), kMarkShot));
         }
         // Every pattern written is on the song's meter: a 16-step grid in a waltz's bars is damage.
         auto meter = [&](const Pattern &p) {
            if (!p.empty() && p.inThree() != (loaded.beats == 3))
               ok = false;
         };
         for (const Pattern &p : s.takes)
            meter(p);
         meter(s.fill);
         meter(s.fillDown);
         meter(s.ending);
      }
      if (!ok)
         return false;
      *this = loaded;
      return true;
   }
};

// The pattern a section's take number names in EDIT: a take (0..3), the fill (4), the fill down (5)
// or the ending (6).
inline Pattern &patternOf(Section &s, int take) {
   return take == 6 ? s.ending : take == 5 ? s.fillDown : take >= kTakes ? s.fill : s.takes[static_cast<size_t>(std::max(0, take))];
}
inline const Pattern &patternOf(const Section &s, int take) { return patternOf(const_cast<Section &>(s), take); }

// What the panel shows of the engine, published every block.
struct Frame {
   bool running = false;   // playing, from the host or on its own
   bool following = false; // following the host's transport (else running free)
   bool hostPlaying = false;
   double tempo = 120.0;
   int bar = 0;  // bars since the section started (from 0)
   int beat = 0;
   int section = kChorus;
   int take = 0;
   bool fill = false;
   bool fillDown = false;              // the fill playing is the fill down
   bool ending = false, ended = false; // the song's ending playing; the song over
   int beats = 4;                      // a bar's beats
   int queued = -1;
   int queuedTake = -1;
   int step = -1; // the step playing in the pattern playing, and its length
   int steps = 16;
   // The chord sounding (or about to), the next one in the progression, and the notes out now.
   Chord chord, next;
   std::array<uint8_t, kMaxVoices> voicing{};
   int voices = 0;
   uint32_t hits = 0; // a count of chords played, so the panel can light them
   // What is held on the chord keys, as named, and the notes themselves.
   Chord held;
   std::array<uint8_t, 8> heldNotes{};
   int heldCount = 0;
   // The bass coming in, for RHYTHM FROM THE BASS: its last few notes (key, step in the bar).
   uint32_t bassHits = 0;
   int lastBass = -1;
   bool stopped = false, hold = false, runsOff = false;
};

} // namespace jcomp
