#pragma once

// JComp's harmony: chord qualities, keys and scales, chord names, naming a chord from held notes,
// and voicing: which notes of a chord a player takes, how they are stacked (close, drop 2, drop 2
// and 4, shells, fourths, spread across two hands), and which voicing follows best from the last.
//
// Voice leading follows the arranger's rules: keep the voices moving as little as they can (common
// tones held, the rest by step), keep the top voice's line smooth (it is the line a listener hears),
// stay in a register window round a centre, and keep intervals out of the mud below their low
// interval limits. Each rule is a cost; the voicing with the least wins. No UI here.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "shared.h"

namespace jcomp {

// The audio thread voices chords, so none of this allocates: fixed-size lists in place of vectors.
using Ints = FixedVec<int, 24>;      // tones, intervals, a run
using Stack = FixedVec<int, 8>;      // one stack of notes
using Stacks = FixedVec<Stack, 128>; // the close stacks of a set of tones

// ---- chord qualities ----

struct Quality {
   const char *suffix;        // as written after the root: "m7", "7#9"
   std::vector<int> tones;    // the chord, as intervals over the root
   // The tones a player keeps, most important first: the third and seventh (the guide tones), then
   // the colour (9th, 13th, the altered tone), then the root and fifth, which go first when there
   // are fewer fingers than tones.
   std::vector<int> priority;
   bool seventh;              // has a seventh (or sixth): shells are 3-7, not 1-3-5
};

inline const std::vector<Quality> &qualities() {
   static const std::vector<Quality> q = {
      {"", {0, 4, 7}, {4, 0, 7}, false},
      {"m", {0, 3, 7}, {3, 0, 7}, false},
      {"7", {0, 4, 7, 10}, {4, 10, 0, 7}, true},
      {"maj7", {0, 4, 7, 11}, {4, 11, 0, 7}, true},
      {"m7", {0, 3, 7, 10}, {3, 10, 0, 7}, true},
      {"9", {0, 4, 7, 10, 14}, {4, 10, 14, 7, 0}, true},
      {"m9", {0, 3, 7, 10, 14}, {3, 10, 14, 7, 0}, true},
      {"maj9", {0, 4, 7, 11, 14}, {4, 11, 14, 7, 0}, true},
      {"13", {0, 4, 7, 10, 14, 21}, {4, 10, 14, 21, 0, 7}, true},
      {"7#9", {0, 4, 7, 10, 15}, {4, 10, 15, 7, 0}, true},
      {"7b9", {0, 4, 7, 10, 13}, {4, 10, 13, 7, 0}, true},
      {"6", {0, 4, 7, 9}, {4, 9, 0, 7}, true},
      {"m6", {0, 3, 7, 9}, {3, 9, 0, 7}, true},
      {"6/9", {0, 4, 7, 9, 14}, {4, 9, 14, 7, 0}, true},
      {"sus", {0, 5, 7}, {5, 0, 7}, false},
      {"7sus", {0, 5, 7, 10}, {5, 10, 0, 7}, true},
      {"dim", {0, 3, 6}, {3, 6, 0}, false},
      {"dim7", {0, 3, 6, 9}, {3, 9, 6, 0}, true},
      {"m7b5", {0, 3, 6, 10}, {3, 10, 6, 0}, true},
      {"aug", {0, 4, 8}, {4, 8, 0}, false},
      {"add9", {0, 4, 7, 14}, {4, 14, 0, 7}, false},
      {"m11", {0, 3, 7, 10, 14, 17}, {3, 10, 17, 14, 7, 0}, true},
      {"11", {0, 7, 10, 14, 17}, {10, 14, 17, 7, 0}, true},
      {"m(maj7)", {0, 3, 7, 11}, {3, 11, 0, 7}, true},
      {"7#11", {0, 4, 7, 10, 18}, {4, 10, 18, 7, 0}, true},
      {"maj7#11", {0, 4, 7, 11, 18}, {4, 11, 18, 7, 0}, true},
      {"madd9", {0, 3, 7, 14}, {3, 14, 0, 7}, false},
      {"13sus", {0, 5, 7, 10, 14, 21}, {5, 10, 21, 14, 0, 7}, true},
   };
   return q;
}
inline int qualityCount() { return static_cast<int>(qualities().size()); }
inline const Quality &qualityOf(const Chord &c) {
   return qualities()[static_cast<size_t>(std::clamp<int>(c.quality, 0, qualityCount() - 1))];
}
// Quality indexes the generator and tests name.
enum QualityId {
   kMaj, kMin, kDom7, kMaj7, kMin7, kDom9, kMin9, kMaj9, kDom13, kSharp9, kFlat9, kSix, kMin6, kSixNine, kSus,
   kSus7, kDim, kDim7, kHalfDim, kAug, kAdd9, kMin11, kEleven, kMinMaj7, kSharp11, kMaj7Sharp11, kMinAdd9, kSus13
};

// ---- keys and scales ----

inline const char *const kSharpNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline const char *const kFlatNames[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
// Keys written with flats (F, Bb, Eb, Ab, Db, Gb as majors).
inline bool flatKey(int key) { return key == 5 || key == 10 || key == 3 || key == 8 || key == 1 || key == 6; }
// A note's name in a key, spelled by its degree: the key's own notes by their letters (G# in E, Bb
// in F), and the chromatic ones as the flat third, sixth, seventh and second and the sharp fourth
// (Eb, Ab, Bb, Db and F# in C), as the borrowed and secondary chords are written. A spelling that
// would need a double accidental or an E#, B#, Cb or Fb falls back to the key's sharps or flats.
inline const char *pitchName(int pc, int key = 0) {
   pc = ((pc % 12) + 12) % 12;
   key = ((key % 12) + 12) % 12;
   const char *const *names = flatKey(key) ? kFlatNames : kSharpNames;
   static const int letterPc[7] = {0, 2, 4, 5, 7, 9, 11}; // C D E F G A B
   static const int stepOf[12] = {0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6}; // the degree's letter, from the key's
   static const char *const spelled[7][3] = {{"Cb", "C", "C#"}, {"Db", "D", "D#"}, {"Eb", "E", "E#"}, {"Fb", "F", "F#"},
                                             {"Gb", "G", "G#"}, {"Ab", "A", "A#"}, {"Bb", "B", "B#"}};
   const char keyLetter = names[key][0];
   const int keyStep = static_cast<int>(std::strchr("CDEFGAB", keyLetter) - "CDEFGAB");
   const int letter = (keyStep + stepOf[(pc - key + 12) % 12]) % 7;
   int acc = pc - letterPc[letter];
   if (acc > 6)
      acc -= 12;
   if (acc < -6)
      acc += 12;
   if (acc < -1 || acc > 1)
      return names[pc];
   const char *n = spelled[letter][acc + 1];
   if (!std::strcmp(n, "E#") || !std::strcmp(n, "B#") || !std::strcmp(n, "Cb") || !std::strcmp(n, "Fb"))
      return names[pc];
   return n;
}

enum Scale { kMajor, kMinor, kDorian, kMixolydian, kBlues, kScaleCount };
inline const char *const kScaleNames[kScaleCount] = {"Major", "Minor", "Dorian", "Mixolydian", "Blues"};
inline const std::vector<int> &scaleSteps(int scale) {
   static const std::vector<int> s[kScaleCount] = {
      {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 10},
      {0, 3, 5, 6, 7, 10}};
   return s[std::clamp(scale, 0, kScaleCount - 1)];
}
inline bool inScale(int pc, int key, int scale) {
   const int d = ((pc - key) % 12 + 12) % 12;
   for (int s : scaleSteps(scale))
      if (s == d)
         return true;
   return false;
}

// ---- names ----

// The flat fifth of a key, spelled as one (Bb in E, Gb in C): the root of a tritone sub, or of a
// major chord a tritone away, which is written flat; a diminished chord there is the sharp fourth.
inline const char *flatFifthName(int key) {
   key = ((key % 12) + 12) % 12;
   const char *const *names = flatKey(key) ? kFlatNames : kSharpNames;
   static const char *const flats[7] = {"Cb", "Db", "Eb", "Fb", "Gb", "Ab", "Bb"};
   static const char *const naturals[7] = {"C", "D", "E", "F", "G", "A", "B"};
   static const int letterPc[7] = {0, 2, 4, 5, 7, 9, 11};
   const int keyStep = static_cast<int>(std::strchr("CDEFGAB", names[key][0]) - "CDEFGAB");
   const int letter = (keyStep + 4) % 7;
   const int pc = (key + 6) % 12;
   if (letterPc[letter] == pc)
      return naturals[letter];
   if ((letterPc[letter] + 11) % 12 == pc && letter != 0 && letter != 3)
      return flats[letter];
   return names[pc];
}

inline std::string chordName(const Chord &c, int key = 0) {
   if (c.none())
      return "N.C.";
   const bool flatFifth = ((c.root - key) % 12 + 12) % 12 == 6 && c.quality != 16 && c.quality != 17 && c.quality != 18;
   std::string n = std::string(flatFifth ? flatFifthName(key) : pitchName(c.root, key)) + qualityOf(c).suffix;
   if (c.bass >= 0 && c.bass != c.root)
      n += std::string("/") + pitchName(c.bass, key);
   return n;
}

// "E9", "Bbmaj7", "C/G", "N.C.": the root, a quality's suffix, a slash bass. False if not a chord.
inline bool parseChord(const std::string &text, Chord &out) {
   if (text == "N.C." || text == "NC" || text == "-") {
      out = Chord{};
      return true;
   }
   if (text.empty())
      return false;
   static const char letters[] = "C D EF G A B";
   auto pitch = [&](const std::string &s, size_t &at) -> int {
      if (at >= s.size())
         return -1;
      const char *p = std::strchr(letters, s[at]);
      if (!p || *p == ' ')
         return -1;
      int pc = static_cast<int>(p - letters);
      ++at;
      if (at < s.size() && s[at] == '#') {
         ++pc;
         ++at;
      } else if (at < s.size() && s[at] == 'b') {
         --pc;
         ++at;
      }
      return (pc + 12) % 12;
   };
   size_t at = 0;
   const int root = pitch(text, at);
   if (root < 0)
      return false;
   std::string rest = text.substr(at);
   int bass = -1;
   const size_t slash = rest.find('/');
   // "6/9" is a quality, not a slash chord.
   if (slash != std::string::npos && rest.compare(slash, 2, "/9") != 0) {
      size_t b = slash + 1;
      bass = pitch(rest, b);
      if (bass < 0 || b != rest.size())
         return false;
      rest = rest.substr(0, slash);
   }
   for (int q = 0; q < qualityCount(); ++q)
      if (rest == qualities()[static_cast<size_t>(q)].suffix) {
         out.root = static_cast<int8_t>(root);
         out.quality = static_cast<uint8_t>(q);
         out.bass = static_cast<int8_t>(bass == root ? -1 : bass);
         return true;
      }
   return false;
}
inline Chord chord(const char *text, int transpose = 0) {
   Chord c;
   parseChord(text, c);
   if (!c.none()) {
      c.root = static_cast<int8_t>((c.root + transpose + 120) % 12);
      if (c.bass >= 0)
         c.bass = static_cast<int8_t>((c.bass + transpose + 120) % 12);
   }
   return c;
}

// The chord's tones as pitch classes (0..11), for runs and for naming held notes.
inline Ints chordTones(const Chord &c) {
   Ints t;
   if (c.none())
      return t;
   for (int i : qualityOf(c).tones)
      t.push_back((c.root + i) % 12);
   return t;
}

// ---- naming what is held ----

// The chord a set of held notes makes. Each pitch class is tried as the root against every quality:
// a tone of the quality that is held scores, a held note it doesn't explain costs more, a missing
// fifth costs little (players leave it out), and the lowest note as the root breaks ties. With
// `slash`, a lowest note that isn't the root becomes the bass (C/G). One or two notes name a root
// (a power chord or a bare third reads as major or minor).
template <typename Notes>
inline Chord nameChord(const Notes &notes, bool slash = true) {
   if (notes.empty())
      return Chord{};
   int lowest = 128;
   unsigned held = 0;
   for (int n : notes) {
      lowest = std::min(lowest, n);
      held |= 1u << (n % 12);
   }
   const int bassPc = lowest % 12;
   int pcs = 0;
   for (int i = 0; i < 12; ++i)
      pcs += held >> i & 1;
   if (pcs <= 2) {
      // A bare fifth or octave is the low note's power chord (read as major), a bare third major or
      // minor over the lower note, anything else the low note's chord.
      Chord c;
      c.root = static_cast<int8_t>(bassPc);
      for (int pc = 0; pc < 12; ++pc)
         if ((held >> pc & 1) && pc != bassPc) {
            const int iv = (pc - bassPc + 12) % 12;
            if (iv == 3)
               c.quality = kMin;
            else if (iv == 5) // a fourth: the upper note's chord, over its fifth
               c.root = static_cast<int8_t>(pc);
         }
      c.bass = static_cast<int8_t>(slash && c.root != bassPc ? bassPc : -1);
      return c;
   }
   Chord best;
   double bestScore = -1e9;
   for (int root = 0; root < 12; ++root) {
      if (!(held >> root & 1) && pcs < 4)
         continue; // a rootless voicing needs four notes to be named by its missing root
      for (int q = 0; q < qualityCount(); ++q) {
         const Quality &qu = qualities()[static_cast<size_t>(q)];
         unsigned want = 0;
         for (int i : qu.tones)
            want |= 1u << ((root + i) % 12);
         double score = 0;
         for (int pc = 0; pc < 12; ++pc) {
            const bool h = held >> pc & 1, w = want >> pc & 1;
            if (h && w)
               score += 2.0;
            else if (h)
               score -= 3.0;
            else if (w) {
               const int iv = (pc - root + 12) % 12;
               score -= iv == 7 ? 0.4 : iv == 0 ? 1.2 : 1.6;
            }
         }
         if (root == bassPc)
            score += 0.75;
         // Simpler names first: fewer tones break ties.
         score -= 0.05 * static_cast<double>(qu.tones.size());
         if (score > bestScore) {
            bestScore = score;
            best.root = static_cast<int8_t>(root);
            best.quality = static_cast<uint8_t>(q);
         }
      }
   }
   best.bass = static_cast<int8_t>(slash && bassPc != best.root ? bassPc : -1);
   return best;
}

// ---- voicing ----

// Rootless: Levine's A and B voicings (3-5-7-9 and 7-9-3-5, the 13th for the 5th on a dominant), the
// root left to the bass. Octaves: a close chord with its bottom note doubled on top (the montuno's
// right hand).
enum Shape { kClose, kDrop2, kDrop24, kShell, kQuartal, kSpread, kRootless, kOctaves, kGrip, kShapeCount };
inline const char *const kShapeNames[kShapeCount] = {"Close", "Drop 2", "Drop 2+4", "Shell", "Quartal", "Spread",
                                                     "Rootless", "Octaves", "Grip"};

struct VoiceSettings {
   int shape = kDrop2;
   int voices = 4;         // notes in the upper part, 3..5
   int lo = 48, hi = 74;   // the window the voicing stays in (C3..D5)
   int center = 62;        // where its middle is pulled
   int floor = 43;         // nothing under this (G2), to keep clear of the bass
   int topMove = 4;        // the most the top voice should move between chords, in semitones
   int maxSpan = 19;       // bottom to top, in semitones
   double lead = 0.85;     // 0 a block in the window's middle, 1 the least movement
   bool noRubs = true;     // no half steps between neighbouring voices low down (below E3, the m2's limit)
   bool doubleMiddle = true; // a doubled note goes in the middle, never on top
   bool sameShape = true;  // a chord repeated keeps its voicing
};

struct Voicing {
   std::array<uint8_t, kMaxVoices> notes{};
   int count = 0;
   bool empty() const { return count == 0; }
   int top() const { return count ? notes[static_cast<size_t>(count - 1)] : -1; }
   int bottom() const { return count ? notes[0] : -1; }
   bool operator==(const Voicing &o) const {
      if (count != o.count)
         return false;
      for (int i = 0; i < count; ++i)
         if (notes[static_cast<size_t>(i)] != o.notes[static_cast<size_t>(i)])
            return false;
      return true;
   }
};

using Voicings = FixedVec<Voicing, 256>;

// The lowest a note can sit with the interval above it before it turns to mud (C4 = 60): the low
// interval limits of the arranging texts (see the README's sources). Index: the interval, 1..11.
inline int lowIntervalLimit(int semitones) {
   // Hoffmann 2019 (the Berklee table, Pease & Pullig 2001): the lowest note each interval stays clear from.
   static const int limit[12] = {0, 52, 51, 48, 46, 46, 46, 34, 43, 41, 41, 41};
   return semitones >= 1 && semitones <= 11 ? limit[semitones] : 0;
}

// The tones (as intervals over the root) a voicing of `n` notes takes, by the quality's priority; a
// doubled tone (when there are more notes than tones) is the root, then the fifth.
inline Ints pickTones(const Chord &c, int n) {
   const Quality &q = qualityOf(c);
   Ints t;
   for (int i : q.priority)
      if (static_cast<int>(t.size()) < n)
         t.push_back(i);
   // The fifth goes before the root in a seventh chord of four notes when there is a colour tone
   // (3, 7, 9, 5: the classic rootless voicing); pickTones already orders it so.
   const int base = static_cast<int>(t.size());
   for (int k = 0; static_cast<int>(t.size()) < n; ++k)
      t.push_back(k % 2 == 0 ? 12 : (q.tones.size() > 2 ? q.tones[2] : 7) + 12 * (1 + k / 2));
   (void)base;
   return t;
}

// The tones over a bass that already sounds the root: the priority list without the root, then the
// fifth, so the upper part never doubles the bass (and never puts the root a half step over a major
// seventh), the root coming back only when there is nothing else to play.
inline Ints upperTones(const Chord &c, int n) {
   const Quality &q = qualityOf(c);
   Ints t;
   for (int i : q.priority)
      if (i % 12 != 0 && static_cast<int>(t.size()) < n)
         t.push_back(i);
   for (int extra : {7, 12, 19})
      if (static_cast<int>(t.size()) < n && std::find(t.begin(), t.end(), extra) == t.end() &&
          !(extra == 7 && std::find(q.tones.begin(), q.tones.end(), 7) == q.tones.end() && q.tones.size() > 2 &&
            (q.tones[2] == 6 || q.tones[2] == 8)))
         t.push_back(extra);
   return t;
}

namespace detail {

template <typename Out, typename V>
inline void addCandidate(Out &out, V v, const VoiceSettings &vs, int bassNote) {
   std::sort(v.begin(), v.end());
   if (v.empty() || v.size() > static_cast<size_t>(kMaxVoices))
      return;
   const int span = v.back() - v.front();
   const int lowest = bassNote >= 0 ? v[1 % v.size()] : v.front();
   if (lowest < vs.lo || v.back() > vs.hi || v.front() < vs.floor)
      return;
   if (span > vs.maxSpan + (bassNote >= 0 ? 24 : 0))
      return;
   for (size_t i = 1; i < v.size(); ++i)
      if (v[i] == v[i - 1])
         return;
   Voicing w;
   w.count = static_cast<int>(std::min<size_t>(v.size(), kMaxVoices));
   for (int i = 0; i < w.count; ++i)
      w.notes[static_cast<size_t>(i)] = static_cast<uint8_t>(std::clamp(v[static_cast<size_t>(i)], 0, 127));
   for (const Voicing &o : out)
      if (o == w)
         return;
   out.push_back(w);
}

// The close-position stacks of a set of pitch classes: each tone at the bottom in turn, the others
// stacked above it within an octave, at every octave in reach.
template <typename P>
inline Stacks closeStacks(const P &pcs, int lo, int hi) {
   Stacks out;
   Ints sorted(pcs.begin(), pcs.end());
   std::sort(sorted.begin(), sorted.end());
   sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
   const int n = static_cast<int>(sorted.size());
   for (int r = 0; r < n; ++r)
      for (int base = lo - 24; base <= hi; ++base) {
         if (((base % 12) + 12) % 12 != sorted[static_cast<size_t>(r)])
            continue;
         Stack v{base};
         for (int k = 1; k < n; ++k) {
            int note = v.back() + 1;
            while (((note % 12) + 12) % 12 != sorted[static_cast<size_t>((r + k) % n)])
               ++note;
            v.push_back(note);
         }
         out.push_back(v);
      }
   return out;
}

} // namespace detail

// Every voicing of a chord the settings allow, in no order.
inline Voicings candidates(const Chord &c, const VoiceSettings &vs, int shapeOverride = -1) {
   Voicings out;
   if (c.none())
      return out;
   const int shape = shapeOverride >= 0 ? shapeOverride : vs.shape;
   const Quality &q = qualityOf(c);
   const int n = std::clamp(vs.voices, 2, 5);
   const int bassPc = c.bass >= 0 ? c.bass : c.root;
   auto pcsOf = [&](const auto &iv) {
      Ints p;
      for (int i : iv)
         p.push_back((c.root + i) % 12);
      return p;
   };
   switch (shape) {
   case kShell: {
      // Root (or the slash bass) under the guide tones: 1-3-7, 1-7-3 (Bud Powell's shells), or
      // 1-3-5 / 1-5-3 for a triad; with more notes, the colour tones on top.
      Ints guide;
      if (q.seventh) {
         for (int i : q.priority)
            if (guide.size() < static_cast<size_t>(std::max(2, n - 1)))
               guide.push_back(i);
      } else
         guide = {q.tones[1], q.tones[2]};
      for (int b = vs.floor; b < vs.floor + 12; ++b) {
         if (b % 12 != bassPc)
            continue;
         for (const auto &st : detail::closeStacks(pcsOf(guide), b + 3, b + 26)) {
            if (st.front() <= b + 2 || st.front() > b + 16)
               continue;
            Stack v{b};
            v.insert(v.end(), st.begin(), st.end());
            detail::addCandidate(out, v, vs, b);
         }
      }
      break;
   }
   case kQuartal: {
      // Fourths stacked on a chord tone or colour tone (with a major third on top, "So What"), kept
      // when every note is the chord's or one of its tensions.
      unsigned allowed = 0;
      for (int i : q.tones)
         allowed |= 1u << ((c.root + i) % 12);
      for (int t : {2, 9}) // the 9th and 13th sit well in fourths over most chords
         if (q.seventh || q.tones.size() > 3)
            allowed |= 1u << ((c.root + t) % 12);
      if (q.tones.size() > 1 && q.tones[1] == 3)
         allowed |= 1u << ((c.root + 5) % 12); // the 11th over a minor chord
      // A stack of fourths is as wide as it is tall: five notes span 19 or 20 semitones, whatever SPREAD.
      VoiceSettings qs = vs;
      qs.maxSpan = std::max(vs.maxSpan, 5 * (n - 1));
      for (int b = vs.lo - 12; b <= vs.hi; ++b) {
         if (!(allowed >> (((b % 12) + 12) % 12) & 1))
            continue;
         for (int last : {5, 4}) {
            Stack v{b};
            for (int k = 1; k < n; ++k)
               v.push_back(v.back() + (k == n - 1 && n >= 4 ? last : 5));
            bool ok = true, root = false;
            for (int x : v) {
               ok = ok && (allowed >> (((x % 12) + 12) % 12) & 1);
               root = root || ((x - bassPc) % 12 + 12) % 12 == 0;
            }
            // And its root (or bass) among them: without it every chord of a diatonic progression
            // finds the same stack of fourths, and the changes are never heard.
            ok = ok && root;
            // It must say what the chord is: the third (or the sus fourth) among its notes.
            bool third = false;
            for (int x : v) {
               const int iv = ((x - c.root) % 12 + 12) % 12;
               third = third || iv == 3 || iv == 4 || (iv == 5 && q.tones[1] == 5);
            }
            if (ok && third)
               detail::addCandidate(out, v, qs, -1);
         }
      }
      if (out.empty())
         return candidates(c, vs, kSpread);
      break;
   }
   case kSpread: {
      // Two hands: the bass note (or root) low, an open fifth or tenth over it, and a close upper
      // structure of the guide and colour tones.
      // Over its own root the upper part leaves the root out (the bass has it); over a slash bass
      // it is the chord.
      const Ints upper = bassPc == c.root ? upperTones(c, std::max(3, n - 1)) : pickTones(c, std::max(3, n - 1));
      for (int b = vs.floor; b < vs.floor + 12; ++b) {
         if (b % 12 != bassPc)
            continue;
         for (const auto &st : detail::closeStacks(pcsOf(upper), vs.lo, vs.hi)) {
            if (st.front() < b + 9)
               continue;
            Stack v{b};
            v.insert(v.end(), st.begin(), st.end());
            detail::addCandidate(out, v, vs, b);
         }
      }
      break;
   }
   case kRootless: {
      // Levine's A and B forms: the third, fifth (the 13th on a dominant), seventh and ninth stacked
      // close from the third or from the seventh. A chord without a seventh is voiced close.
      Ints iv;
      const std::vector<int> &t = q.tones;
      const auto has = [&](int i) { return std::find(t.begin(), t.end(), i) != t.end(); };
      const int third = has(4) ? 4 : has(3) ? 3 : has(5) ? 5 : -1;
      const int seventh = has(10) ? 10 : has(11) ? 11 : has(9) ? 9 : -1;
      if (!q.seventh || third < 0 || seventh < 0 || c.quality == kDim7 || c.quality == kHalfDim)
         return candidates(c, vs, kClose);
      const bool dom = third == 4 && seventh == 10;
      int colour = has(15) ? 15 : has(13) ? 13 : 14;              // the altered 9th, or the 9th
      // The 13th on a dominant; the b13 with an altered ninth (Levine's 3-b13-7-b9 for the V of a minor ii-V).
      int fifth = dom ? (has(15) || has(13) ? 8 : 9) : (has(6) ? 6 : has(8) ? 8 : 7);
      if (has(18))
         fifth = 18;                                                // the #11 in its place
      if (seventh == 9)
         colour = 14, fifth = 7;                                    // a sixth chord: 3-5-6-9
      iv = {third, fifth, seventh, colour};
      if (n == 3)
         iv = {third, seventh, colour};
      if (n == 5) {
         // A fifth note the four don't already hold: the 11th of a minor eleventh, else the fifth, else the root.
         Ints pcs4;
         for (int i : iv)
            pcs4.push_back(i % 12);
         const auto held = [&](int i) { return std::find(pcs4.begin(), pcs4.end(), i % 12) != pcs4.end(); };
         iv.push_back(has(17) && !held(17) ? 17 : !held(7) && !has(6) && !has(8) ? 7 : 0);
      }
      Ints pcs = pcsOf(iv);
      const int low3 = (c.root + third) % 12, low7 = (c.root + seventh) % 12;
      for (const auto &st : detail::closeStacks(pcs, vs.lo, vs.hi)) {
         const int bottom = ((st.front() % 12) + 12) % 12;
         if (bottom == low3 || bottom == low7)
            detail::addCandidate(out, st, vs, -1);
      }
      break;
   }
   case kOctaves: {
      // A close chord of n-1 notes, its bottom note doubled an octave up on top.
      const Ints tones = pickTones(c, std::max(2, n - 1));
      Ints pcs = pcsOf(tones);
      std::sort(pcs.begin(), pcs.end());
      pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
      // The octave is on the root, the third or the fifth: the line a montuno's thumbs carry (any
      // tone, for a chord with no third).
      const size_t had = out.size();
      for (int pass = 0; pass < 2 && out.size() == had; ++pass)
         for (auto st : detail::closeStacks(pcs, vs.lo, vs.hi)) {
            if (st.back() - st.front() >= 12)
               continue;
            const int outer = ((st.front() - c.root) % 12 + 12) % 12;
            if (pass == 0 && outer != 0 && outer != 3 && outer != 4 && outer != 7)
               continue;
            st.push_back(st.front() + 12);
            detail::addCandidate(out, st, vs, -1);
         }
      break;
   }
   case kGrip: {
      // A guitar's grip: the bass string, the fifth and the octave over it, then the chord's third
      // and colour stacked close above (E2 B2 E3 G#3 B3; three notes, the power chord). Over a slash
      // bass, the chord close above it.
      Ints upper;
      for (int i : q.priority)
         if (i % 12 != 0 && i != 7 && static_cast<int>(upper.size()) < n - 3)
            upper.push_back(i);
      for (int extra : {7, 12})
         if (static_cast<int>(upper.size()) < n - 3)
            upper.push_back(extra);
      const bool slash = bassPc != c.root;
      int fifthIv = 7; // the chord's fifth (a diminished or augmented one, if that's what it has)
      if (std::find(q.tones.begin(), q.tones.end(), 7) == q.tones.end())
         for (int i : {6, 8})
            if (std::find(q.tones.begin(), q.tones.end(), i) != q.tones.end())
               fifthIv = i;
      for (int b = vs.floor; b < vs.floor + 24; ++b) {
         if (b % 12 != bassPc || (!slash && b < lowIntervalLimit(fifthIv) - 2))
            continue;
         if (slash) {
            for (const auto &st : detail::closeStacks(pcsOf(pickTones(c, std::max(2, n - 1))), b + 3, b + 20)) {
               if (st.front() <= b + 2 || st.front() > b + 9)
                  continue;
               Stack v{b};
               v.insert(v.end(), st.begin(), st.end());
               detail::addCandidate(out, v, vs, b);
            }
            continue;
         }
         Stack v{b, b + fifthIv, b + 12};
         if (!upper.empty())
            for (const auto &st : detail::closeStacks(pcsOf(upper), b + 13, b + 25)) {
               if (st.front() <= b + 12 || st.front() > b + 23)
                  continue;
               Stack w = v;
               w.insert(w.end(), st.begin(), st.end());
               detail::addCandidate(out, w, vs, b);
            }
         else
            detail::addCandidate(out, v, vs, b);
      }
      break;
   }
   default: {
      const Ints tones = pickTones(c, n);
      Ints pcs = pcsOf(tones);
      // A doubled tone: voice the distinct notes, then add the doubles where DOUBLE puts them.
      Ints distinct = pcs;
      std::sort(distinct.begin(), distinct.end());
      distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
      Ints doubles; // the pitch classes played twice, in the order pickTones added them
      {
         Ints seen;
         for (int pc : pcs) {
            if (std::find(seen.begin(), seen.end(), pc) != seen.end())
               doubles.push_back(pc);
            seen.push_back(pc);
         }
      }
      // Dropping the second and fourth voices by an octave widens a stack by an octave: drop 2+4
      // is allowed that much more than SPREAD.
      VoiceSettings ds = vs;
      if (shape == kDrop24)
         ds.maxSpan = vs.maxSpan + 12;
      for (auto st : detail::closeStacks(distinct, vs.lo, vs.hi)) {
         if (shape == kDrop2 && st.size() >= 3)
            st[st.size() - 2] -= 12;
         else if (shape == kDrop24 && st.size() >= 4) {
            st[st.size() - 2] -= 12;
            st[st.size() - 4] -= 12;
         }
         std::sort(st.begin(), st.end());
         // Each double an octave from a note of its pitch class already there, never on top with DOUBLE.
         FixedVec<Stack, 32> grown{st};
         for (int dpc : doubles) {
            FixedVec<Stack, 32> next;
            for (const auto &g : grown)
               for (int x : g) {
                  if (((x % 12) + 12) % 12 != dpc)
                     continue;
                  for (int d : {x + 12, x - 12}) {
                     if (std::find(g.begin(), g.end(), d) != g.end() || (vs.doubleMiddle && d > g.back()))
                        continue;
                     Stack v = g;
                     v.push_back(d);
                     std::sort(v.begin(), v.end());
                     next.push_back(v);
                  }
               }
            grown = next;
         }
         for (const auto &v : grown)
            detail::addCandidate(out, v, ds, -1);
      }
      // A slash chord keeps its bass at the bottom: the voicings that have it there, else each with
      // the bass note added under it.
      if (c.bass >= 0 && c.bass != c.root && !out.empty()) {
         Voicings withBass;
         for (const Voicing &v : out)
            if (v.bottom() % 12 == c.bass)
               withBass.push_back(v);
         if (withBass.empty())
            for (const Voicing &v : out) {
               int b = v.bottom() - 1;
               while (b >= 0 && b % 12 != c.bass)
                  --b;
               if (b < vs.floor)
                  continue;
               Stack w{b};
               for (int i = 0; i < v.count; ++i)
                  w.push_back(v.notes[static_cast<size_t>(i)]);
               detail::addCandidate(withBass, w, vs, b);
            }
         if (!withBass.empty())
            out = withBass;
      }
      break;
   }
   }
   // Nothing fits the window (a narrow one, a big shape): widen it and try again.
   if (out.empty() && vs.hi - vs.lo < 60) {
      VoiceSettings wide = vs;
      wide.lo = std::max(vs.floor, vs.lo - 6);
      wide.hi = vs.hi + 6;
      wide.maxSpan = vs.maxSpan + 7;
      return candidates(c, wide, shapeOverride);
   }
   return out;
}

// How far one voicing moves to another: each voice to the nearest of the other's.
inline int movement(const Voicing &a, const Voicing &b) {
   if (a.empty() || b.empty())
      return 0;
   int m = 0;
   if (a.count == b.count) {
      for (int i = 0; i < a.count; ++i)
         m += std::abs(a.notes[static_cast<size_t>(i)] - b.notes[static_cast<size_t>(i)]);
      return m;
   }
   for (int i = 0; i < b.count; ++i) {
      int best = 99;
      for (int j = 0; j < a.count; ++j)
         best = std::min(best, std::abs(b.notes[static_cast<size_t>(i)] - a.notes[static_cast<size_t>(j)]));
      m += best;
   }
   return m;
}

// The breaks a voicing makes in the rules, for the cost and for the panel to name.
struct Breaks {
   bool topJump = false, rub = false, mud = false;
};
inline Breaks breaksOf(const Voicing &v, const Voicing *prev, const VoiceSettings &vs) {
   Breaks b;
   if (prev && !prev->empty())
      b.topJump = std::abs(v.top() - prev->top()) > vs.topMove;
   for (int i = 1; i < v.count; ++i) {
      const int lo = v.notes[static_cast<size_t>(i - 1)], iv = v.notes[static_cast<size_t>(i)] - lo;
      if (iv <= 1 && lo < lowIntervalLimit(1))
         b.rub = true;
      if (iv < 12 && lo < lowIntervalLimit(iv))
         b.mud = true;
   }
   return b;
}

inline double cost(const Voicing &v, const Voicing *prev, const VoiceSettings &vs) {
   double mean = 0;
   for (int i = 0; i < v.count; ++i)
      mean += v.notes[static_cast<size_t>(i)];
   mean /= std::max(1, v.count);
   const double centre = std::abs(mean - vs.center);
   double c = 0;
   if (prev && !prev->empty()) {
      const double move = movement(*prev, v) + 2.0 * std::abs(v.top() - prev->top());
      c += vs.lead * move + (1.0 - vs.lead) * 3.0 * centre + 0.35 * centre;
      const int jump = std::abs(v.top() - prev->top());
      if (jump > vs.topMove)
         c += 10.0 + 2.0 * (jump - vs.topMove);
   } else
      c += centre;
   const Breaks b = breaksOf(v, prev, vs);
   if (b.rub && vs.noRubs)
      c += 20.0;
   if (b.mud)
      c += 14.0;
   return c;
}

// The voicings of a chord after `prev`, best first.
inline Voicings rankShapes(const Chord &c, const Voicing *prev, const VoiceSettings &vs) {
   Voicings all = candidates(c, vs);
   // Mud below the low interval limits, and a low half step with NO RUBS, are out whenever a voicing
   // without them exists (a guitar's shell or grip is a guitar's, and keeps its low fifths and sevenths).
   if (vs.shape != kShell && vs.shape != kGrip) {
      Voicings clean;
      for (const Voicing &v : all) {
         const Breaks b = breaksOf(v, nullptr, vs);
         if (!b.mud && !(b.rub && vs.noRubs))
            clean.push_back(v);
      }
      if (!clean.empty())
         all = clean;
   }
   // By cost, ties kept in order (an insertion sort: no allocation).
   FixedVec<double, 256> costs;
   for (const Voicing &v : all)
      costs.push_back(cost(v, prev, vs));
   for (size_t i = 1; i < all.size(); ++i) {
      const Voicing v = all[i];
      const double k = costs[i];
      size_t j = i;
      while (j > 0 && costs[j - 1] > k) {
         all[j] = all[j - 1];
         costs[j] = costs[j - 1];
         --j;
      }
      all[j] = v;
      costs[j] = k;
   }
   return all;
}

// The voicing a chord gets after `prev` (and the chord it voiced): the best, or the n-th best when
// a hit asks for its own.
inline Voicing voice(const Chord &c, const Voicing *prev, const VoiceSettings &vs, int pick = -1,
                     const Chord *prevChord = nullptr) {
   if (c.none())
      return Voicing{};
   if (vs.sameShape && prevChord && *prevChord == c && prev && !prev->empty() && pick < 0)
      return *prev;
   const Voicings r = rankShapes(c, prev, vs);
   if (r.empty())
      return Voicing{};
   return r[static_cast<size_t>(std::clamp(pick, 0, static_cast<int>(r.size()) - 1))];
}

// A voicing that keeps a given top note (MY TOP NOTE): the best with that note on top, else the
// best with its pitch class on top, else the best.
inline Voicing voiceUnder(const Chord &c, int topNote, const Voicing *prev, const VoiceSettings &vs) {
   VoiceSettings wide = vs;
   wide.hi = std::max(vs.hi, topNote);
   wide.lo = std::min(vs.lo, topNote - wide.maxSpan);
   const Voicings r = rankShapes(c, prev, wide);
   for (const Voicing &v : r)
      if (v.top() == topNote)
         return v;
   for (const Voicing &v : r)
      if (v.top() % 12 == topNote % 12)
         return v;
   return r.empty() ? Voicing{} : r.front();
}

// The bass note a left hand plays under a chord: its bass (or root) at or above `floor`, or with
// `fifth` the note it alternates with (the fifth, the flat fifth of a diminished chord; the root
// under a slash chord), the nearer of the two above and below that root.
inline int bassNote(const Chord &c, int floor, bool fifth = false) {
   const int pc = c.bass >= 0 ? c.bass : c.root;
   int n = std::max(0, floor);
   while (n % 12 != ((pc % 12) + 12) % 12)
      ++n;
   if (!fifth)
      return n;
   const std::vector<int> &t = qualityOf(c).tones;
   const int iv = t.size() > 2 && t[2] >= 6 && t[2] <= 8 ? t[2] : 7;
   const int alt = c.bass >= 0 && c.bass != c.root ? (c.root - pc + 12) % 12 : iv;
   const int up = n + alt;
   return up <= floor + 12 ? up : up - 12;
}

// ---- passing runs ----

// A run of `length` single notes into `target`: stepping through a scale from the side the line
// comes from (below when it rises to the target, above when it falls), and with `halfStep` its last
// note a half step under the target when it comes from below, the scale walked down from there (so
// the line is all steps: B C C# into D, never a leap before the half step).
//
// The scale is the key's while the chord it leads into is in the key. A chord from outside it (the
// Ab of a borrowed Abmaj7, the C of a borrowed iv, the F# of a D7, a chord of a key change) gets its
// own: its tones, with a whole step over each where the gap to the next tone leaves room, so a run
// into Am in E walks B C D E, not the key's D#.
inline Ints passingRun(int from, int target, int length, int key, int scale, bool halfStep, const Ints &chordPcs = {}) {
   Ints run;
   if (length <= 0)
      return run;
   unsigned allowed = 0, chord = 0;
   for (int pc = 0; pc < 12; ++pc)
      if (inScale(pc, key, scale))
         allowed |= 1u << pc;
   bool outside = false;
   for (int pc : chordPcs) {
      const int p = ((pc % 12) + 12) % 12;
      chord |= 1u << p;
      outside = outside || !(allowed >> p & 1);
   }
   if (outside) {
      Ints t;
      for (int pc = 0; pc < 12; ++pc)
         if (chord >> pc & 1)
            t.push_back(pc);
      allowed = chord;
      for (size_t k = 0; k < t.size(); ++k) {
         const int a = t[k], b = t[(k + 1) % t.size()];
         const int gap = ((b - a) % 12 + 12) % 12;
         if (gap >= 3 || (t.size() == 1))
            allowed |= 1u << ((a + 2) % 12);
      }
   }
   const auto ok = [&](int n) { return (allowed >> (((n % 12) + 12) % 12) & 1) != 0; };
   const bool up = from < 0 || from <= target;
   int n = target;
   int left = length;
   if (up && halfStep) {
      n = target - 1;
      run.insert(run.begin(), n);
      --left;
   }
   for (int k = 0; k < left; ++k) {
      const int start = n;
      do
         n += up ? -1 : 1;
      while (!ok(n) && std::abs(n - start) < 3);
      run.insert(run.begin(), n);
   }
   return run;
}

// A line note's interval over a chord's root, for degree `d` (1..9: 8 the root's octave, 9 the
// ninth above it): the chord's own tone where it has one (its third, its fifth, its seventh, an
// altered ninth), else the key's scale, else the plain interval.
inline int lineInterval(const Chord &c, int d, int key, int scale) {
   const std::vector<int> &t = qualityOf(c).tones;
   const auto has = [&](int i) { return std::find(t.begin(), t.end(), i) != t.end(); };
   const auto scaled = [&](int a, int b, int fallback) {
      if (inScale(c.root + a, key, scale))
         return a;
      if (inScale(c.root + b, key, scale))
         return b;
      return fallback;
   };
   switch (std::clamp(d, 1, 9)) {
   case 1: return 0;
   case 2: return has(13) ? 1 : has(15) ? 3 : scaled(2, 1, 2);
   case 3: return has(3) ? 3 : has(4) ? 4 : has(5) ? 5 : scaled(4, 3, 4);
   case 4: return has(18) ? 6 : has(17) || has(5) ? 5 : scaled(5, 6, 5);
   case 5: return has(6) && !has(7) ? 6 : has(8) && !has(7) ? 8 : 7;
   case 6: return has(9) || has(21) ? 9 : has(8) ? 8 : scaled(9, 8, 9);
   case 7: return has(10) ? 10 : has(11) ? 11 : has(9) && has(6) ? 9 : scaled(10, 11, 10);
   case 8: return 12;
   default: return has(13) ? 13 : has(15) ? 15 : 14;
   }
}

} // namespace jcomp
