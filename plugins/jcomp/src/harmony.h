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
inline const char *pitchName(int pc, int key = 0) { return (flatKey(key) ? kFlatNames : kSharpNames)[((pc % 12) + 12) % 12]; }

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

inline std::string chordName(const Chord &c, int key = 0) {
   if (c.none())
      return "N.C.";
   std::string n = std::string(pitchName(c.root, key)) + qualityOf(c).suffix;
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
inline std::vector<int> chordTones(const Chord &c) {
   std::vector<int> t;
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
inline Chord nameChord(const std::vector<int> &notes, bool slash = true) {
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

enum Shape { kClose, kDrop2, kDrop24, kShell, kQuartal, kSpread, kShapeCount };
inline const char *const kShapeNames[kShapeCount] = {"Close", "Drop 2", "Drop 2+4", "Shell", "Quartal", "Spread"};

struct VoiceSettings {
   int shape = kDrop2;
   int voices = 4;         // notes in the upper part, 3..5
   int lo = 48, hi = 74;   // the window the voicing stays in (C3..D5)
   int center = 62;        // where its middle is pulled
   int floor = 43;         // nothing under this (G2), to keep clear of the bass
   int topMove = 4;        // the most the top voice should move between chords, in semitones
   int maxSpan = 19;       // bottom to top, in semitones
   double lead = 0.85;     // 0 a block in the window's middle, 1 the least movement
   bool noRubs = true;     // no half steps between neighbouring voices below C4
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

// The lowest a note can sit with the interval above it before it turns to mud (C4 = 60): the low
// interval limits of the arranging texts (see the README's sources). Index: the interval, 1..11.
inline int lowIntervalLimit(int semitones) {
   // Hoffmann 2019 (the Berklee table, Pease & Pullig 2001): the lowest note each interval stays clear from.
   static const int limit[12] = {0, 52, 51, 48, 46, 46, 46, 34, 43, 41, 41, 41};
   return semitones >= 1 && semitones <= 11 ? limit[semitones] : 0;
}

// The tones (as intervals over the root) a voicing of `n` notes takes, by the quality's priority; a
// doubled tone (when there are more notes than tones) is the root, then the fifth.
inline std::vector<int> pickTones(const Chord &c, int n) {
   const Quality &q = qualityOf(c);
   std::vector<int> t;
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

namespace detail {

inline void addCandidate(std::vector<Voicing> &out, std::vector<int> v, const VoiceSettings &vs, int bassNote) {
   std::sort(v.begin(), v.end());
   if (v.empty())
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
inline std::vector<std::vector<int>> closeStacks(const std::vector<int> &pcs, int lo, int hi) {
   std::vector<std::vector<int>> out;
   std::vector<int> sorted = pcs;
   std::sort(sorted.begin(), sorted.end());
   sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
   const int n = static_cast<int>(sorted.size());
   for (int r = 0; r < n; ++r)
      for (int base = lo - 24; base <= hi; ++base) {
         if (((base % 12) + 12) % 12 != sorted[static_cast<size_t>(r)])
            continue;
         std::vector<int> v{base};
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
inline std::vector<Voicing> candidates(const Chord &c, const VoiceSettings &vs, int shapeOverride = -1) {
   std::vector<Voicing> out;
   if (c.none())
      return out;
   const int shape = shapeOverride >= 0 ? shapeOverride : vs.shape;
   const Quality &q = qualityOf(c);
   const int n = std::clamp(vs.voices, 2, 5);
   const int bassPc = c.bass >= 0 ? c.bass : c.root;
   auto pcsOf = [&](const std::vector<int> &iv) {
      std::vector<int> p;
      for (int i : iv)
         p.push_back((c.root + i) % 12);
      return p;
   };
   switch (shape) {
   case kShell: {
      // Root (or the slash bass) under the guide tones: 1-3-7, 1-7-3 (Bud Powell's shells), or
      // 1-3-5 / 1-5-3 for a triad; with more notes, the colour tones on top.
      std::vector<int> guide;
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
            std::vector<int> v{b};
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
      for (int b = vs.lo - 12; b <= vs.hi; ++b) {
         if (!(allowed >> (((b % 12) + 12) % 12) & 1))
            continue;
         for (int last : {5, 4}) {
            std::vector<int> v{b};
            for (int k = 1; k < n; ++k)
               v.push_back(v.back() + (k == n - 1 && n >= 4 ? last : 5));
            bool ok = true;
            for (int x : v)
               ok = ok && (allowed >> (((x % 12) + 12) % 12) & 1);
            // It must say what the chord is: the third (or the sus fourth) among its notes.
            bool third = false;
            for (int x : v) {
               const int iv = ((x - c.root) % 12 + 12) % 12;
               third = third || iv == 3 || iv == 4 || (iv == 5 && q.tones[1] == 5);
            }
            if (ok && third)
               detail::addCandidate(out, v, vs, -1);
         }
      }
      if (out.empty())
         return candidates(c, vs, kDrop2);
      break;
   }
   case kSpread: {
      // Two hands: the bass note (or root) low, an open fifth or tenth over it, and a close upper
      // structure of the guide and colour tones.
      const std::vector<int> upper = pickTones(c, std::max(3, n - 1));
      for (int b = vs.floor; b < vs.floor + 12; ++b) {
         if (b % 12 != bassPc)
            continue;
         for (const auto &st : detail::closeStacks(pcsOf(upper), vs.lo, vs.hi)) {
            if (st.front() < b + 9)
               continue;
            std::vector<int> v{b};
            v.insert(v.end(), st.begin(), st.end());
            detail::addCandidate(out, v, vs, b);
         }
      }
      break;
   }
   default: {
      const std::vector<int> tones = pickTones(c, n);
      std::vector<int> pcs = pcsOf(tones);
      // A doubled tone: voice the distinct notes, then add the double where DOUBLE puts it.
      std::vector<int> distinct = pcs;
      std::sort(distinct.begin(), distinct.end());
      distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
      const bool doubled = distinct.size() < pcs.size();
      for (auto st : detail::closeStacks(distinct, vs.lo, vs.hi)) {
         if (shape == kDrop2 && st.size() >= 3)
            st[st.size() - 2] -= 12;
         else if (shape == kDrop24 && st.size() >= 4) {
            st[st.size() - 2] -= 12;
            st[st.size() - 4] -= 12;
         }
         std::sort(st.begin(), st.end());
         if (doubled) {
            // The double is the root (or fifth): an octave from one already there.
            const int dpc = pcs.back() % 12;
            for (int x : std::vector<int>(st)) {
               if (((x % 12) + 12) % 12 != dpc)
                  continue;
               for (int d : {x + 12, x - 12}) {
                  std::vector<int> v = st;
                  v.push_back(d);
                  std::sort(v.begin(), v.end());
                  if (vs.doubleMiddle && (v.back() == d))
                     continue;
                  detail::addCandidate(out, v, vs, -1);
               }
            }
         } else
            detail::addCandidate(out, st, vs, -1);
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
      if (iv <= 1 && lo < 60)
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
inline std::vector<Voicing> rankShapes(const Chord &c, const Voicing *prev, const VoiceSettings &vs) {
   std::vector<Voicing> all = candidates(c, vs);
   std::vector<std::pair<double, size_t>> order;
   for (size_t i = 0; i < all.size(); ++i)
      order.push_back({cost(all[i], prev, vs), i});
   std::stable_sort(order.begin(), order.end());
   std::vector<Voicing> out;
   for (const auto &[k, i] : order)
      out.push_back(all[i]);
   return out;
}

// The voicing a chord gets after `prev` (and the chord it voiced): the best, or the n-th best when
// a hit asks for its own.
inline Voicing voice(const Chord &c, const Voicing *prev, const VoiceSettings &vs, int pick = -1,
                     const Chord *prevChord = nullptr) {
   if (c.none())
      return Voicing{};
   if (vs.sameShape && prevChord && *prevChord == c && prev && !prev->empty() && pick < 0)
      return *prev;
   const std::vector<Voicing> r = rankShapes(c, prev, vs);
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
   const std::vector<Voicing> r = rankShapes(c, prev, wide);
   for (const Voicing &v : r)
      if (v.top() == topNote)
         return v;
   for (const Voicing &v : r)
      if (v.top() % 12 == topNote % 12)
         return v;
   return r.empty() ? Voicing{} : r.front();
}

// ---- passing runs ----

// A run of `length` single notes into `target`: stepping through the key's scale from the side the
// line comes from (below when it rises to the target, above when it falls), and with `halfStep` the
// last note a half step under the target when it comes from below.
inline std::vector<int> passingRun(int from, int target, int length, int key, int scale, bool halfStep) {
   std::vector<int> run;
   if (length <= 0)
      return run;
   const bool up = from < 0 || from <= target;
   int n = target;
   for (int k = 0; k < length; ++k) {
      do
         n += up ? -1 : 1;
      while (!inScale(n, key, scale) && std::abs(n - target) < 12);
      run.insert(run.begin(), n);
   }
   if (up && halfStep)
      run.back() = target - 1;
   return run;
}

} // namespace jcomp
