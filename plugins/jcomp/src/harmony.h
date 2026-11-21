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
      {"13", {0, 4, 7, 10, 14, 21}, {4, 10, 21, 14, 0, 7}, true},
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
      // The augmented dominant (a chart's 7+5: the waltz's III7 and VI7), its #5 the b13 a minor
      // target's dominant leans on.
      {"7#5", {0, 4, 8, 10}, {4, 10, 8, 0}, true},
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
   kSus7, kDim, kDim7, kHalfDim, kAug, kAdd9, kMin11, kEleven, kMinMaj7, kSharp11, kMaj7Sharp11, kMinAdd9, kSus13, kSharp5
};

// ---- keys and scales ----

inline const char *const kSharpNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline const char *const kFlatNames[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
enum Scale { kMajor, kMinor, kDorian, kMixolydian, kBlues, kScaleCount };
// Keys written with flats (F, Bb, Eb, Ab, Db, Gb as majors). A mode is written as its relative
// major is (F# minor with A's sharps, G minor with Bb's flats, D Mixolydian with G's sharp).
inline bool flatKey(int key, int scale = kMajor) {
   static const int toMajor[kScaleCount] = {0, 3, 10, 5, 0};
   key = (((key + toMajor[std::clamp(scale, 0, kScaleCount - 1)]) % 12) + 12) % 12;
   return key == 5 || key == 10 || key == 3 || key == 8 || key == 1 || key == 6;
}
// A note's name in a key, spelled by its degree: the key's own notes by their letters (G# in E, Bb
// in F), and the chromatic ones as the flat third, sixth, seventh and second and the sharp fourth
// (Eb, Ab, Bb, Db and F# in C), as the borrowed and secondary chords are written. A spelling that
// would need a double accidental, a Cb (but in Gb's signature) or Fb, or an E# or B# (but as a sharp
// key's leading tone) falls back to the key's sharps or flats.
inline const char *pitchName(int pc, int key = 0, int scale = kMajor) {
   pc = ((pc % 12) + 12) % 12;
   key = ((key % 12) + 12) % 12;
   const char *const *names = flatKey(key, scale) ? kFlatNames : kSharpNames;
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
   // A sharp key's leading tone keeps its letter (E# in F# minor, B# in C# minor): vii°7 is E#dim7.
   // So does its raised fourth (B major's E#, the #iv°7 and V/V's third: C#/E#).
   // (And a sharp minor key's major third, the picardy third and V/iv's: C#7/E# in C# minor.)
   const int deg = (pc - key + 12) % 12;
   const bool leading = (deg == 11 || deg == 6 || deg == 4) && names == kSharpNames;
   if ((!std::strcmp(n, "E#") || !std::strcmp(n, "B#")) && !leading)
      return names[pc];
   // Cb only where the key signature has it: Gb major's six flats (and its modes: Eb minor, Ab
   // Dorian, Db Mixolydian), whose IV is Cb.
   static const int toMajor[kScaleCount] = {0, 3, 10, 5, 0};
   const int major = (key + toMajor[std::clamp(scale, 0, kScaleCount - 1)]) % 12;
   // (Gb blues too: its IV is Cb7 beside Gb7 and Db7, not a B7 in another signature.)
   if (!std::strcmp(n, "Cb") && major == 6 && pc == 11)
      return n;
   if (!std::strcmp(n, "Cb") || !std::strcmp(n, "Fb"))
      return names[pc];
   return n;
}

inline const char *const kScaleNames[kScaleCount] = {"Major", "Minor", "Dorian", "Mixolydian", "Blues"};
// The scales' steps, as constant tables (no static to build on the audio thread); -1 ends one.
inline constexpr int kScaleSteps[kScaleCount][8] = {
   {0, 2, 4, 5, 7, 9, 11, -1}, {0, 2, 3, 5, 7, 8, 10, -1}, {0, 2, 3, 5, 7, 9, 10, -1}, {0, 2, 4, 5, 7, 9, 10, -1},
   {0, 3, 5, 6, 7, 10, -1, -1}};
inline bool inScale(int pc, int key, int scale) {
   const int d = ((pc - key) % 12 + 12) % 12;
   for (int s : kScaleSteps[std::clamp(scale, 0, kScaleCount - 1)])
      if (s == d)
         return true;
   return false;
}

// ---- names ----

// The flat fifth of a key, spelled as one (Bb in E, Gb in C): the root of a tritone sub, or of a
// major chord a tritone away, which is written flat; a diminished chord there is the sharp fourth.
inline const char *flatFifthName(int key, int scale = kMajor) {
   key = ((key % 12) + 12) % 12;
   const char *const *names = flatKey(key, scale) ? kFlatNames : kSharpNames;
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

// A note spelled on a letter: `name` (a pitch's name, "Bb", "C#") moved `letters` up, then given
// the accidental that makes it `pc`. Empty where that would take a double accidental.
inline std::string onLetter(const std::string &name, int letters, int pc) {
   static const char letterNames[] = "CDEFGAB";
   static const int letterPc[7] = {0, 2, 4, 5, 7, 9, 11};
   const char *at = name.empty() ? nullptr : std::strchr(letterNames, name[0]);
   if (!at)
      return {};
   const int letter = (static_cast<int>(at - letterNames) + letters) % 7;
   int acc = ((((pc % 12) + 12) % 12) - letterPc[letter] + 12) % 12;
   if (acc > 6)
      acc -= 12;
   if (acc < -1 || acc > 1)
      return {};
   return std::string(1, letterNames[letter]) + (acc > 0 ? "#" : acc < 0 ? "b" : "");
}

// A chord's name in a key (and its scale, for the key's sharps or flats). `next`, the chord after it:
// a dominant a half step above it is its flat II (Eb9 into D, Db9 into C), spelled as one.
inline std::string chordName(const Chord &c, int key = 0, int scale = kMajor, const Chord *next = nullptr) {
   if (c.none())
      return "N.C.";
   const std::vector<int> &tones = qualityOf(c).tones;
   const auto hasTone = [&](int i) { return std::find(tones.begin(), tones.end(), i) != tones.end(); };
   // A major or dominant chord a tritone from the key (a tritone sub, a bV) is written flat; a minor
   // or diminished one there is the sharp fourth's: C#m7 in G is the ii of the V/vi (Levine's
   // C#m7-F#7-Bm), never Dbm7.
   const bool flatFifth = ((c.root - key) % 12 + 12) % 12 == 6 && !hasTone(3);
   std::string root = flatFifth ? flatFifthName(key, scale) : pitchName(c.root, key, scale);
   // A passing diminished chord a half step under a scale step is that step's lower neighbour
   // raised: C#°7 and D#°7 between C, Dm and Em (Levine), not Db°7 and Eb°7. (Not on a step of
   // the key's own scale: A minor's C and F are its b3 and b6, Cdim7 and Fdim7.)
   const int deg = ((c.root - key) % 12 + 12) % 12;
   if ((c.quality == kDim || c.quality == kDim7 || c.quality == kHalfDim) && (deg == 1 || deg == 3 || deg == 8) &&
       !inScale(c.root, key, scale == kBlues ? kMixolydian : scale))
      if (const std::string up = onLetter(pitchName(c.root - 1, key, scale), 0, c.root); !up.empty())
         root = up;
   if (next && !next->none() && (c.root - next->root + 12) % 12 == 1 &&
       (c.quality == kDom7 || c.quality == kDom9 || c.quality == kDom13 || c.quality == kSharp9 ||
        c.quality == kFlat9 || c.quality == kEleven || c.quality == kSharp11 || c.quality == kSus13 ||
        c.quality == kSus7)) {
      // As the next chord's flat second: its letter and one up, flattened.
      const std::string to = pitchName(next->root, key, scale);
      static const char letters[] = "CDEFGAB";
      const char *at = std::strchr(letters, to[0]);
      if (at && to.size() == 1) {
         const char up = letters[(at - letters + 1) % 7];
         if (up != 'C' && up != 'F')
            root = std::string(1, up) + "b";
      }
   }
   // (Nor a root whose own third or fifth would take a double accidental: Gb major's IV°7 is
   // Bdim7, not Cbdim7's Ebb; C# minor's picardy V/iv is F, not E#'s G##.)
   for (const int i : {hasTone(4) ? 4 : hasTone(3) ? 3 : -1, hasTone(6) ? 6 : hasTone(8) ? 8 : hasTone(7) ? 7 : -1})
      if (i >= 0 && onLetter(root, i <= 4 ? 2 : 4, c.root + i).empty()) {
         root = (flatKey(key, scale) ? kFlatNames : kSharpNames)[((c.root % 12) + 12) % 12];
         break;
      }
   std::string n = root + qualityOf(c).suffix;
   // A slash bass that is one of the chord's tones takes that tone's letter (E's third is G#: E/G#,
   // not E/Ab); another bass is spelled in the key.
   if (c.bass >= 0 && c.bass != c.root) {
      const int iv = ((c.bass - c.root) % 12 + 12) % 12;
      int letters = -1;
      for (int t : qualityOf(c).tones)
         if (t % 12 == iv)
            letters = iv == 3 || iv == 4 ? 2 : iv == 6 || iv == 7 || iv == 8 ? 4 : iv == 10 || iv == 11 ? 6 : iv == 1 || iv == 2 ? 1 : iv == 5 ? 3 : iv == 9 ? 5 : -1;
      std::string b = letters >= 0 ? onLetter(root, letters, c.bass) : std::string{};
      // (Nor a spelling the key never writes: a Cb or Fb under a chord written in sharps. But a
      // sharp root's own major third is E# or B#, whatever the key: C#7/E# in A, G#7/B# in E.)
      const bool sharpThird = letters == 2 && iv == 4 && root.size() > 1 && root[1] == '#' && (b == "E#" || b == "B#");
      if (b.empty() ||
          (!sharpThird && (b == "Cb" || b == "Fb" || b == "E#" || b == "B#") && b != pitchName(c.bass, key, scale)))
         b = pitchName(c.bass, key, scale);
      n += "/" + b;
   }
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
   // "6/9" is a quality, not a slash chord: a bass comes after it (C6/9/E).
   const size_t sixNine = rest.find("6/9");
   const size_t slash = rest.find('/', sixNine == std::string::npos ? 0 : sixNine + 3);
   if (slash != std::string::npos) {
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
// (a power chord or a bare third reads as major or minor). `key` (0..11, or -1 for none) settles a
// bare tritone: the dominant whose root is in the key (Bb-E in C is C7, not Gb7).
template <typename Notes>
inline Chord nameChord(const Notes &notes, bool slash = true, int key = -1, int scale = kMajor) {
   if (notes.empty())
      return Chord{};
   // The blues scale is the melody's: its b3 and b5 are blue notes over dominant chords (I7 IV7
   // V7), so the harmony is read in the key's Mixolydian.
   const int asked = scale;
   if (scale == kBlues)
      scale = kMixolydian;
   int lowest = 128;
   unsigned held = 0;
   for (int n : notes) {
      lowest = std::min(lowest, n);
      held |= 1u << (n % 12);
   }
   const int bassPc = lowest % 12;
   // The bass held only as the lowest note (F/G's G): a slash bass, not a tension of the chord over it.
   int bassCount = 0;
   for (int n : notes)
      bassCount += n % 12 == bassPc;
   const unsigned bassBit = 1u << bassPc;
   int pcs = 0;
   for (int i = 0; i < 12; ++i)
      pcs += held >> i & 1;
   if (pcs <= 2) {
      // A bare fifth or octave is the low note's power chord (read as major), a bare third major or
      // minor over the lower note, anything else the low note's chord.
      Chord c;
      c.root = static_cast<int8_t>(bassPc);
      // (A bare fifth or octave in a key: the key's own third over it, A5 in A minor is Am.)
      const bool power = held == bassBit || held == (bassBit | 1u << ((bassPc + 7) % 12));
      // (Not a minor key's V: its dominant is major in practice, its leading tone raised. A mode's v
      // stays minor: Dorian's and Mixolydian's minor v is the mode.)
      if (power && key >= 0 && inScale(bassPc, key, scale) && !inScale((bassPc + 4) % 12, key, scale) &&
          inScale((bassPc + 3) % 12, key, scale) && !((bassPc - key + 12) % 12 == 7 && (asked == kMinor || asked == kBlues)))
         c.quality = kMin;
      for (int pc = 0; pc < 12; ++pc)
         if ((held >> pc & 1) && pc != bassPc) {
            const int iv = (pc - bassPc + 12) % 12;
            if (iv == 3)
               c.quality = kMin;
            else if (iv == 2) // a whole step: the low note's add9
               c.quality = kAdd9;
            else if (iv == 5) // a fourth: the upper note's chord, over its fifth
               c.root = static_cast<int8_t>(pc);
            else if (iv == 6) { // a tritone: a dominant's 3rd and 7th, the shell a major third over its root
               c.root = static_cast<int8_t>((bassPc + 8) % 12);
               // (Or the other dominant it belongs to, its 7th in the bass, when that one's root is
               // in the key and this one's isn't.)
               if (key >= 0) {
                  // (In the key's own scale: B and F in A minor are G7's, not C#7's.)
                  const auto inKey = [&](int pc) { return inScale(pc, key, scale); };
                  if (!inKey(c.root) && inKey((bassPc + 2) % 12))
                     c.root = static_cast<int8_t>((bassPc + 2) % 12);
               }
               c.quality = kDom7;
               c.bass = -1;
               return c;
            }
            else if (iv == 8) // a minor sixth: the upper note's major chord, over its third (C/E)
               c.root = static_cast<int8_t>(pc);
            else if (iv == 9) { // a major sixth: the upper note's minor chord, over its third (Am/C)
               c.root = static_cast<int8_t>(pc);
               c.quality = kMin;
            } else if (iv == 10) // a seventh over the low note: its 7th chord (a shell)
               c.quality = kDom7;
            else if (iv == 11)
               c.quality = kMaj7;
         }
      c.bass = static_cast<int8_t>(slash && c.root != bassPc ? bassPc : -1);
      return c;
   }
   Chord best;
   double bestScore = -1e9;
   bool bestRootless = false;
   // Root, 3rd and 7th over the lowest note (C E Bb) is that note's dominant shell, the most common
   // way to hold one, never the rootless voicing of the chord a tritone away (Gb7#11).
   const bool bassShell = (held >> ((bassPc + 4) % 12) & 1) && (held >> ((bassPc + 10) % 12) & 1);
   // Whether the notes are some chord held whole (every tone, its root among them, nothing else).
   // And whether one such chord has its own third in the bass (C7#9/E, Cm11/Eb).
   bool wholeChord = false, thirdBass = false;
   for (int root = 0; root < 12; ++root)
      if (held >> root & 1)
         for (const Quality &qu : qualities()) {
            unsigned want = 0;
            for (int i : qu.tones)
               want |= 1u << ((root + i) % 12);
            if (want == held && qu.tones.size() >= 3) {
               wholeChord = true;
               const int iv = (bassPc - root + 12) % 12;
               const bool has = std::find(qu.tones.begin(), qu.tones.end(), iv) != qu.tones.end();
               thirdBass = thirdBass || ((iv == 3 || iv == 4) && has);
            }
         }
   // A dominant whose root is held with its 3rd and 7th (C7's C E Bb, over its E): that chord, not the
   // rootless one a tritone away (Gb7#11) whose root nobody played.
   bool rootExplains = false;
   for (int root = 0; root < 12; ++root)
      if ((held >> root & 1) && (held >> ((root + 4) % 12) & 1) && (held >> ((root + 10) % 12) & 1))
         rootExplains = true;
   // Whether the bass, as the root, has a chord with a third holding every note (C Eb F Bb: Cm11).
   bool bassExplains = false;
   // A bass a fifth or more under the rest: the left hand's root, under the chord (C under E B F#
   // is Cmaj7#11, not a rootless D13 with the C left out).
   int nextUp = 128;
   for (int n : notes)
      if (n > lowest)
         nextUp = std::min(nextUp, n);
   const bool apartBass = nextUp < 128 && nextUp - lowest >= 7;
   for (const Quality &qu : qualities()) {
      unsigned want = 0;
      for (int i : qu.tones)
         want |= 1u << ((bassPc + i) % 12);
      const bool third = std::find(qu.tones.begin(), qu.tones.end(), 3) != qu.tones.end() ||
                         std::find(qu.tones.begin(), qu.tones.end(), 4) != qu.tones.end();
      if (third && (held & ~want) == 0 && ((held >> ((bassPc + 3) % 12) & 1) || (held >> ((bassPc + 4) % 12) & 1))) {
         bassExplains = true;
         break;
      }
   }
   // (Counted up from the bass, so a tie is read the same way in every key.)
   for (int k = 0; k < 12; ++k) {
      const int root = (bassPc + k) % 12;
      // A rootless voicing needs four notes to be named by its missing root, or a dominant's
      // tritone (its 3rd and 7th) and a colour: the left hand's rootless voicings.
      const bool rootless = !(held >> root & 1);
      const bool tritone = (held >> ((root + 4) % 12) & 1) && (held >> ((root + 10) % 12) & 1);
      // (Three notes: the tritone and a colour that isn't the fifth. A tritone and a fifth is a
      // diminished triad, vii°6's D F B, not a G7 with a root nobody played.)
      const unsigned shell = (1u << ((root + 4) % 12)) | (1u << ((root + 10) % 12)) | (1u << ((root + 7) % 12));
      if (rootless && pcs < 4 && (!tritone || (held & ~shell) == 0))
         continue;
      for (int q = 0; q < qualityCount(); ++q) {
         const Quality &qu = qualities()[static_cast<size_t>(q)];
         unsigned want = 0;
         for (int i : qu.tones)
            want |= 1u << ((root + i) % 12);
         double score = 0;
         // (A chord with a third over the bass: F/G, Bb/C. A sus triad over a bass is that bass's own
         // chord without its fifth, C6/9's C E A D, not Asus/C.)
         const bool hasThird = std::find(qu.tones.begin(), qu.tones.end(), 3) != qu.tones.end() ||
                               std::find(qu.tones.begin(), qu.tones.end(), 4) != qu.tones.end();
         const bool overBass = slash && !bassShell && root != bassPc && bassCount == 1 && !(want & bassBit) &&
                               (want & ~bassBit) == (held & ~bassBit) && hasThird;
         for (int pc = 0; pc < 12; ++pc) {
            const bool h = held >> pc & 1, w = want >> pc & 1;
            if (overBass && pc == bassPc)
               continue; // scored below, as the slash bass under a chord held exactly
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
         // A chord that is exactly what's held beats a richer one it would have to add a note to
         // (Cm/Eb, not an Eb6 with a Bb nobody played).
         if (want == held)
            score += root == bassPc ? 1.0 : 0.5;
         // (Not where the notes are a whole chord, root held, over its own third: C7#9/E is C7#9, not a
         // Cm7 over the E that makes the chord major.)
         if (overBass && !(wholeChord && thirdBass))
            score += 2.5;
         // A dominant's rootless voicing (3-13-7-9, 7-9-3-5: Levine's left hand), every held note
         // its own, is that dominant (G13 from F A B E, not an Fmaj7#11 with an avoid note).
         const bool dom = std::find(qu.tones.begin(), qu.tones.end(), 4) != qu.tones.end() &&
                          std::find(qu.tones.begin(), qu.tones.end(), 10) != qu.tones.end();
         const bool rootlessDom = rootless && tritone && dom && !bassShell && !rootExplains && !(apartBass && bassExplains);
         // (Not where the notes are a chord held whole, root and all: Cm7b5's Gb Bb C Eb is that
         // chord inverted, whatever the bass, not an Ab9 nobody's root is under.)
         if (rootlessDom && (held & ~want) == 0 && !wholeChord) {
            score += 2.5;
            // The stock left-hand forms (3-7-9, 7-3-13: Levine) are the unaltered dominant's: the same
            // notes as the tritone sub's 7#5 or 7#9, which they are far more often (C9's E Bb D, not
            // Gb7#5 with a Gb nobody played).
            const auto hasQ = [&](int i) { return std::find(qu.tones.begin(), qu.tones.end(), i) != qu.tones.end(); };
            if (!hasQ(8) && !hasQ(13) && !hasQ(15) && !hasQ(6) && !hasQ(18))
               score += 1.8;
         }
         // Of two whole readings, the one with a third (Cm11, not an F13sus over its Eb; Gm11, not a
         // C13sus with no C under it).
         if (want == held && root != bassPc && qu.tones.size() >= 5 &&
             std::find(qu.tones.begin(), qu.tones.end(), 3) == qu.tones.end() &&
             std::find(qu.tones.begin(), qu.tones.end(), 4) == qu.tones.end())
            score -= 0.3;
         // A sus chord over another root (F7sus over C) is that root's own chord with a third when one
         // holds every note: Cm11 without its fifth, not F7sus/C.
         if (want == held && root != bassPc && !hasThird && bassExplains)
            score -= 2.5;
         // (Nor a rootless chord other than a dominant: Ab6/9 over C for C Eb F Bb.)
         if (rootless && !dom && bassExplains)
            score -= 1.0;
         // A bass a fifth or more apart whose own chord holds every note is that chord's root
         // (Db under D F Ab is Db7b9's, not a D diminished over its b9).
         if (apartBass && bassExplains && root == bassPc && (held & ~want) == 0)
            score += 3.0;
         // So is a bass whose own chord of four tones or more holds four notes or more, its third
         // among them: E A D G B, the So What stack, is Em11 (Levine), not a Gadd9 over its E;
         // G Bb C D F is Gm11, not a Bbadd9/G. (Three notes are too few to say: G C F is Csus/G.)
         else if (root == bassPc && pcs >= 4 && qu.tones.size() >= 4 && (held & ~want) == 0 &&
                  std::any_of(qu.tones.begin(), qu.tones.end(),
                              [&](int i) { return (i == 3 || i == 4) && (held >> ((root + i) % 12) & 1); }))
            score += 2.0;
         // Simpler names first: fewer tones break ties.
         score -= 0.05 * static_cast<double>(qu.tones.size());
         if (score > bestScore) {
            bestScore = score;
            best.root = static_cast<int8_t>(root);
            best.quality = static_cast<uint8_t>(q);
            bestRootless = rootlessDom;
         }
      }
   }
   best.bass = static_cast<int8_t>(slash && bassPc != best.root && !bestRootless ? bassPc : -1);
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
   int key = -1, scale = 0; // the key the chords are in (-1 none): an added tension stays in it
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
   // A 13sus in three notes is its 4, b7 and 9: its 4, b7 and 13 are the ii9's own notes a fourth
   // over (A13sus's D G F# is Em9's G D F#), and the change to it would not be heard.
   if (c.quality == kSus13 && n == 3)
      return Ints{5, 10, 14};
   for (int i : q.priority)
      if (static_cast<int>(t.size()) < n)
         t.push_back(i);
   // The fifth goes before the root in a seventh chord of four notes when there is a colour tone
   // (3, 7, 9, 5: the classic rootless voicing); pickTones already orders it so.
   const int base = static_cast<int>(t.size());
   // (Over a major seventh the doubled note is the 9th, not the root a half step over the 7th.)
   const bool maj7 = std::find(q.tones.begin(), q.tones.end(), 11) != q.tones.end();
   // (And a major seventh in three notes is its 3, 5 and 7, not the root a half step over the 7th.)
   // (Not over a slash bass: Cmaj7/E without its C is an E minor triad.)
   if (maj7 && n == 3 && (c.bass < 0 || c.bass == c.root)) {
      const auto root = std::find(t.begin(), t.end(), 0);
      if (root != t.end() && std::find(t.begin(), t.end(), 7) == t.end() &&
          std::find(q.tones.begin(), q.tones.end(), 7) != q.tones.end())
         *root = 7;
   }
   // Over a slash bass the root stays: C9/E in three notes is E Bb C, not E Bb D (Em7b5). It takes
   // the place of the bass's own tone (the bass sounds it), else of the last tone that is neither
   // altered nor a guide tone (an altered C7b9/E keeps its Bb: without it, no dominant).
   if (c.bass >= 0 && c.bass != c.root && !t.empty() && std::find(t.begin(), t.end(), 0) == t.end() &&
       std::find(t.begin(), t.end(), 12) == t.end()) {
      bool placed = false;
      for (size_t k = t.size(); k-- > 0 && !placed;)
         if ((c.root + t[k]) % 12 == c.bass) {
            t[k] = 0;
            placed = true;
         }
      for (size_t k = t.size(); k-- > 0 && !placed;) {
         const int iv = t[k];
         const bool altered = iv == 13 || iv == 15 || iv == 6 || iv == 8 || iv == 18 || iv == 20;
         const bool guide = iv % 12 == 3 || iv % 12 == 4 || iv % 12 == 10 || iv % 12 == 11;
         if (!altered && !guide) {
            t[k] = 0;
            placed = true;
         }
      }
   }
   const bool has9 = std::find(t.begin(), t.end(), 14) != t.end();
   for (int k = 0; static_cast<int>(t.size()) < n; ++k)
      t.push_back(k % 2 == 0 ? (maj7 && !has9 && k == 0 ? 14 : 12) : (q.tones.size() > 2 ? q.tones[2] : 7) + 12 * (1 + k / 2));
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
   const bool maj7 = std::find(q.tones.begin(), q.tones.end(), 11) != q.tones.end();
   for (int extra : {7, maj7 ? 14 : 12, 19})
      if (static_cast<int>(t.size()) < n && std::find(t.begin(), t.end(), extra) == t.end() &&
          !((extra == 7 || extra == 19) && std::find(q.tones.begin(), q.tones.end(), 18) != q.tones.end()) &&
          !((extra == 7 || extra == 19) && std::find(q.tones.begin(), q.tones.end(), 7) == q.tones.end() && q.tones.size() > 2 &&
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

// The open chords of a guitar in standard tuning (E2 A2 D3 G3 B3 E4): C, A, G, E, D, Am, Em, Dm, A7,
// D7, E7, G7, C7, B7, Am7, Em7, Cmaj7, Dmaj7, Fmaj7, Amaj7, Emaj7, Cadd9, Dsus4, Asus4, Esus4.
// (No sus2: there is no such quality to match them.)
inline constexpr int kOpenGrips[][6] = {
      {48, 52, 55, 60, 64, 0},  {45, 52, 57, 61, 64, 0},  {43, 47, 50, 55, 59, 67}, {40, 47, 52, 56, 59, 64},
      {50, 57, 62, 66, 0, 0},   {45, 52, 57, 60, 64, 0},  {40, 47, 52, 55, 59, 64}, {50, 57, 62, 65, 0, 0},
      {45, 52, 55, 61, 64, 0},  {50, 57, 60, 66, 0, 0},   {40, 47, 50, 56, 59, 64}, {43, 47, 50, 55, 59, 65},
      {48, 52, 58, 60, 64, 0},  {47, 51, 57, 59, 66, 0},  {45, 52, 55, 60, 64, 0},  {40, 47, 50, 55, 59, 64},
      {48, 52, 55, 59, 64, 0},  {50, 57, 61, 66, 0, 0},   {53, 57, 60, 64, 0, 0},   {45, 52, 56, 61, 64, 0},
      {40, 47, 51, 56, 59, 64}, {48, 52, 55, 62, 64, 0},  {50, 57, 62, 67, 0, 0},
      {45, 52, 57, 62, 64, 0},  {40, 47, 52, 57, 59, 64},
};
// An open shape cut to `n` neighbouring strings, the strum unbroken: its lowest `n` (the bass string
// kept), or with `top` its highest (a strummer's four-string G7 is xx0001; 320xx1, two strings
// muted inside the strum, is no grip).
inline Ints gripCut(const int (&shape)[6], int n, bool top = false) {
   Ints all;
   for (int x : shape)
      if (x > 0)
         all.push_back(x);
   if (static_cast<int>(all.size()) > n)
      all.erase(top ? all.begin() : all.begin() + n, top ? all.end() - n : all.end());
   return all;
}
// Whether a grip is one of the open chords as a hand plays it at that many strings (not a thin
// subset of one: G3 B3 E4 is three strings of an Em7, not an open chord).
inline bool openGrip(const Voicing &v) {
   for (const auto &shape : kOpenGrips)
      for (bool top : {false, true}) {
         if (top && v.count < 4)
            continue;
         const Ints cut = gripCut(shape, v.count, top);
         bool same = static_cast<int>(cut.size()) == v.count;
         for (int i = 0; i < v.count && same; ++i)
            same = cut[static_cast<size_t>(i)] == v.notes[static_cast<size_t>(i)];
         if (same)
            return true;
      }
   return false;
}

} // namespace detail

// The breaks a voicing makes in the rules, for the cost and for the panel to name.
struct Breaks {
   bool topJump = false, rub = false, mud = false;
};

// Every voicing of a chord the settings allow, in no order.
inline double gripFrets(const Voicing &v); // (below: how a hand frets a grip)
inline double clashes(const Voicing &v, const Chord &c); // (below: a voicing's rubs)
inline Breaks breaksOf(const Voicing &v, const Voicing *prev, const VoiceSettings &vs); // (below: the rules it breaks)
inline Voicings candidates(const Chord &c, const VoiceSettings &vs, int shapeOverride = -1, bool widen = true) {
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
   // Over a slash bass the hands hold the chord's root and its other tones by priority, never the
   // bass's pitch class again: C/G without its C is an E minor triad over G.
   const auto overSlash = [&](int k) {
      Ints t{0};
      for (int i : q.priority)
         if (static_cast<int>(t.size()) < k && i % 12 != 0 && (c.root + i) % 12 != bassPc)
            t.push_back(i);
      return t;
   };
   const bool slashed = bassPc != c.root;
   switch (shape) {
   case kShell: {
      // Root (or the slash bass) under the guide tones: 1-3-7, 1-7-3 (Bud Powell's shells), or
      // 1-3-5 / 1-5-3 for a triad; with more notes, the colour tones on top (an add9's 9th first:
      // it is named for it).
      Ints guide;
      if (slashed)
         guide = overSlash(std::max(2, n - 1));
      else {
         // (Not the root again: the bass has it, and over a major seventh B under a C rubs.)
         for (int i : q.priority)
            if (guide.size() < static_cast<size_t>(std::max(2, n - 1)) && i % 12 != 0)
               guide.push_back(i);
         // Three notes hold the root and two tones: an altered chord's altered tone is one of them.
         // An altered fifth takes the third's place (the b7 keeps it a seventh chord: m7b5's R-b5-b7,
         // the guitar's half-diminished shell, not a Cm7's); an altered ninth the seventh's (the 3rd
         // under it keeps a #9 from being heard as a minor third).
         const auto inQ = [&](int i) { return std::find(q.tones.begin(), q.tones.end(), i) != q.tones.end(); };
         if (n == 3 && guide.size() == 2 && inQ(10)) {
            const int fifth = inQ(6) && !inQ(7) ? 6 : inQ(8) ? 8 : inQ(18) ? 18 : -1;
            const int ninth = inQ(4) ? (inQ(13) ? 13 : inQ(15) ? 15 : -1) : -1;
            if (fifth >= 0)
               guide = {10, fifth};
            else if (ninth >= 0)
               guide = {q.tones[1], ninth};
         }
      }
      for (int b = vs.floor; b < std::max(vs.floor + 12, vs.lo); ++b) { // up to the window, wherever it is
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
      // Three notes are one hand's (a guitarist's root on string 6 or 5 and two guide tones over it):
      // within a tenth and a third of the root where any shell is, not a root and a 13th apart.
      if (n == 3) {
         Voicings near;
         for (const Voicing &v : out)
            if (v.top() - v.bottom() <= 16)
               near.push_back(v);
         if (!near.empty())
            return near;
      }
      break;
   }
   case kQuartal: {
      // Fourths stacked on a chord tone or colour tone (with a major third on top, "So What"), kept
      // when every note is the chord's or one of its tensions, and the stack says what the chord is:
      // its root, its third (or sus fourth), its seventh (or sixth) and an altered tone where it has
      // one. Without them every chord of a diatonic progression finds the same stack and the changes
      // are never heard. The altered tone always stays (a 7b9 voiced without its b9 is another
      // chord): a chord no stack holds it in takes another shape. A stack that can't hold the rest
      // puts the root under it. The seventh never goes (a 7b9 without it is no dominant, a Cmaj7/G
      // without it a C6/G): a chord no stack keeps it in takes SPREAD.
      // Over a slash bass the bass is the stack's lowest note and the chord's own root is over it
      // (C/G is never an E minor over G): a slash chord no stack fits that way takes SPREAD too.
      unsigned own = 0, allowed = 0;
      for (int i : q.tones)
         own |= 1u << ((c.root + i) % 12);
      allowed = own;
      const auto hasIv = [&](int i) { return std::find(q.tones.begin(), q.tones.end(), i) != q.tones.end(); };
      const bool altered9 = hasIv(13) || hasIv(15), altered5 = hasIv(8) || hasIv(6) || hasIv(18);
      // The 9th and 13th sit well in fourths over a chord with a seventh, but not against its b9/#9,
      // nor the 13th against a b13 or a diminished fifth.
      // (A sus or a plain triad takes the 9th too: the sus's add9, Eno's voicing, a stack that keeps
      // its size.)
      // An added tension (one the chord doesn't name) is the key's: a vi chord's 13th in E major
      // isn't A#. (A chord from outside the key takes its own.)
      // (The blues scale's harmony is the key's Mixolydian: I7 IV7 V7 take their 9ths and 13ths.)
      const int keyScale = vs.scale == kBlues ? kMixolydian : vs.scale;
      const auto keyHas = [&](int iv) {
         return vs.key < 0 || !inScale(c.root % 12, vs.key, keyScale) || inScale((c.root + iv) % 12, vs.key, keyScale) ||
                hasIv(iv) || hasIv(iv + 12);
      };
      if ((q.seventh || q.tones.size() > 3 || (q.tones.size() == 3 && !hasIv(6) && !hasIv(8))) && !altered9 && keyHas(2))
         allowed |= 1u << ((c.root + 2) % 12);
      // (A #11 is the lydian dominant's, whose scale has the 13th.)
      if (q.seventh && !altered9 && !hasIv(8) && !hasIv(6) && !hasIv(9) && keyHas(9))
         allowed |= 1u << ((c.root + 9) % 12);
      if (q.tones.size() > 1 && q.tones[1] == 3 && !altered5 && keyHas(5))
         allowed |= 1u << ((c.root + 5) % 12); // the 11th over a minor chord
      // A secondary dominant takes the key's tensions: the b9 and b13 where the key has them and not
      // the natural ones (V7 of vi in E: G#7's A and E, not A# and E#).
      if (hasIv(4) && hasIv(10) && !altered9 && !altered5) {
         if (!keyHas(2) && keyHas(1))
            allowed |= 1u << ((c.root + 1) % 12);
         if (!keyHas(9) && keyHas(8))
            allowed |= 1u << ((c.root + 8) % 12);
      }
      int seventh = -1;
      for (int i : {10, 11, 9})
         if (q.seventh && seventh < 0 && hasIv(i))
            seventh = i;
      int alt = -1;
      for (int i : {13, 15, 18, 8})
         if (alt < 0 && hasIv(i) && !(i == 8 && !hasIv(4)))
            alt = i % 12;
      // (A half-diminished's or a diminished's b5 is as much the chord as an altered tone.)
      if (alt < 0 && hasIv(6) && !hasIv(7))
         alt = 6;
      // (A minor-major seventh's major seventh over its minor third leaves few stacks: its first pass
      // goes on to the last level too.)
      const bool fewStacks = seventh == 11 && q.tones.size() > 1 && q.tones[1] == 3;
      // The colour the chord is named for, after its third and seventh: an 11th chord's 11th, a 13th
      // chord's 13th, else a 9th chord's (or an add9's) 9th. The levels go on down until a stack
      // holds it with the guide tones; it never takes their place (an m9 without its minor third is
      // a 9sus, a 9th without its seventh an add9).
      // (An 11th chord with no third has its 11th in the third's place: C11's colour is its 9th.)
      const int own3 = hasIv(3) ? 3 : hasIv(4) ? 4 : hasIv(5) || hasIv(17) ? 5 : q.tones.size() > 1 ? q.tones[1] % 12 : -1;
      const int colour = hasIv(17) && own3 != 5 ? 5 : hasIv(21) ? 9 : hasIv(14) ? 2 : -1;
      const bool wantColour = colour >= 0;
      const auto holds = [&](const Voicing &v, int iv) {
         for (int i = 0; i < v.count; ++i)
            if (((v.notes[static_cast<size_t>(i)] - c.root) % 12 + 12) % 12 == iv)
               return true;
         return false;
      };
      const auto colourFrom = [&](int from) {
         for (size_t k = static_cast<size_t>(from); k < out.size(); ++k)
            if (holds(out[k], colour))
               return true;
         return false;
      };
      // A stack of fourths is as wide as it is tall: five notes span 19 to 21 semitones, whatever SPREAD.
      VoiceSettings qs = vs;
      qs.maxSpan = std::max(vs.maxSpan, 5 * (n - 1) + 1);
      // Pure fourths first; then one of them a tritone (Bb E A D over C7) or a major third (the
      // "So What" top, or lower down). Where no stack holds root, third and seventh together, the
      // root (or slash bass) goes under a stack of the rest (the left hand's root under the right
      // hand's fourths).
      // A chord with fewer tones than voices (a triad, a sus, an add9) stacks fewer fourths rather
      // than leaving the shape; one a note or two short is always a choice too (the sparse set of
      // stacks otherwise leaves the top leaping from chord to chord).
      // (A chord with as many tones as voices gets a stack its own size where one exists: a five-note
      // m9 isn't left at three.)
      const bool fewer = static_cast<int>(q.tones.size()) < n;
      // (Every size with the seventh, never one without: a five-note Eb7 stack with no Db is an
      // Eb6/9, and a four-note one that keeps it fits.)
      // (A second pass goes on to the last level where nothing fitted, and on down the sizes for a
      // chord's colour, where no stack with the third and seventh held it.)
      for (int pass = 0; pass < 2 && (out.empty() || (pass == 1 && wantColour && !colourFrom(0))); ++pass)
      for (int size = n; size >= 3 && (out.empty() || (fewer && size >= n - 2) || (pass == 1 && !colourFrom(0))); --size)
      // A level less strict comes in while there are fewer than three stacks to choose from (one
      // stack a chord leaves the top leaping from chord to chord), down to the root (or slash bass)
      // under a stack of the rest; the last level (plain stacks with both odd intervals) comes in for
      // a seventh chord only in the second pass.
      for (int strict = 3, found = static_cast<int>(out.size());
           strict >= (pass == 0 && seventh >= 0 && !fewStacks ? 1 : 0) &&
           (static_cast<int>(out.size()) < found + 3 || (wantColour && !colourFrom(found)));
           --strict)
         // (Last, a tritone and a major third both: a dominant's 7-3-13-9, B E# A D over C#7, where
         // the key leaves no stack with one odd interval. Judged at each size: a five-note stack that
         // clashes doesn't keep a minor key's V7 from its clean four.)
         for (int odd = 0; odd < 3 && (static_cast<int>(out.size()) < found + 3 || (wantColour && !colourFrom(found))) &&
                           !(odd == 2 && (static_cast<int>(out.size()) > found || strict > 1));
              ++odd)
            for (int b = vs.lo - 12; b <= vs.hi; ++b) {
               if (!(allowed >> (((b % 12) + 12) % 12) & 1))
                  continue;
               const int m = strict == 1 ? size - 1 : size;
               const int ways = odd == 0 ? 1 : odd == 1 ? 2 * (m - 1) : std::max(0, (m - 1) * (m - 2));
               for (int ex = 0; ex < ways; ++ex) {
                  Stack v{b};
                  int tri = -1, maj = -1;
                  if (odd == 2) {
                     tri = ex / (m - 2);
                     maj = ex % (m - 2);
                     maj += maj >= tri;
                  }
                  for (int k = 1; k < m; ++k)
                     v.push_back(v.back() + (odd == 1 && k - 1 == ex / 2 ? (ex % 2 ? 6 : 4)
                                             : k - 1 == tri            ? 6
                                             : k - 1 == maj            ? 4
                                                                       : 5));
                  bool ok = m >= 2, low = false; // `low`: the stack holds the root, or the slash bass
                  for (int x : v) {
                     ok = ok && (allowed >> (((x % 12) + 12) % 12) & 1);
                     low = low || ((x - bassPc) % 12 + 12) % 12 == 0;
                  }
                  if (strict == 1) {
                     // (Under a stack that hasn't got it: one with it at the bottom is a plain stack's,
                     // and one with it higher up would sound it twice.)
                     if (low)
                        continue;
                     int under = b - 3;
                     while (((under - bassPc) % 12 + 12) % 12)
                        --under;
                     if (under < vs.floor)
                        under += 12;
                     if (under >= b)
                        continue;
                     v.insert(v.begin(), under);
                  } else if (((v.front() - bassPc) % 12 + 12) % 12) {
                     // (A stack over a slash bass sits on it; one over the root holds the root
                     // anywhere in it.)
                     if (slashed || !low)
                        continue;
                  }
                  bool root = false, third = false, sev = seventh < 0, al = alt < 0;
                  for (int x : v) {
                     const int iv = ((x - c.root) % 12 + 12) % 12;
                     root = root || iv == 0; // the chord's own (over a slash bass, among the notes above it)
                     third = third || iv == own3; // its own (a 7#9's 3rd, not the #9)
                     sev = sev || iv == seventh;
                     al = al || iv == alt;
                  }
                  if (ok && root && third && sev && al)
                     detail::addCandidate(out, v, qs, strict == 1 ? v.front() : -1);
               }
            }
      // Every stack has the third and seventh; of them, the ones that hold most of the chord's colour
      // (an 11th or 13th chord's 9th after its 11th or 13th).
      if (wantColour && !out.empty()) {
         const auto says = [&](const Voicing &v) {
            return (holds(v, colour) ? 2 : 0) + (colour != 2 && hasIv(14) && holds(v, 2) ? 1 : 0);
         };
         int most = 0;
         for (const Voicing &v : out)
            most = std::max(most, says(v));
         Voicings best;
         for (const Voicing &v : out)
            if (says(v) == most)
               best.push_back(v);
         out = best;
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
      Ints upper = bassPc == c.root ? upperTones(c, std::max(3, n - 1)) : pickTones(c, std::max(3, n - 1));
      // Over a slash bass the root stays among them (the I of a I/5 is the arrival), in place of the
      // fifth, else of the last colour.
      if (bassPc != c.root && std::none_of(upper.begin(), upper.end(), [](int i) { return i % 12 == 0; })) {
         auto fifth = std::find_if(upper.begin(), upper.end(), [](int i) { return i % 12 == 7; });
         if (fifth != upper.end())
            *fifth = 0;
         else if (upper.size() > 2)
            upper.back() = 0;
      }
      const bool sharp9 = bassPc == c.root && std::find(q.tones.begin(), q.tones.end(), 15) != q.tones.end() &&
                          std::find(q.tones.begin(), q.tones.end(), 4) != q.tones.end();
      // The bass in either of its low octaves (never under FLOOR, and low: under the window's
      // bottom fifth), at least a fifth under the right hand: every root gets a choice of voicings,
      // so the top voice can lead whatever the key.
      for (int b = vs.floor; b < vs.floor + 24; ++b) {
         if (b % 12 != bassPc || (b >= vs.floor + 12 && b >= vs.lo + 7))
            continue;
         for (const auto &st : detail::closeStacks(pcsOf(upper), vs.lo, vs.hi)) {
            // (A 7#9's upper part may start on its 3rd a third over the root, so the #9 can go on
            // top, the Hendrix chord's 1-3-7-#9, not a half step under the 3rd.)
            const bool hendrix = sharp9 && ((st.front() - c.root) % 12 + 12) % 12 == 4 && st.front() >= b + 4;
            if (st.front() < b + 7 && !hendrix)
               continue;
            Stack v{b};
            v.insert(v.end(), st.begin(), st.end());
            // (A triad's upper part at five voices: its lowest note doubled an octave up, over the
            // stack, the octave-doubled triad of an organist's right hand; its pitch classes alone
            // are one short.)
            if (static_cast<int>(v.size()) < n && st.size() >= 3 && st.front() + 12 > st.back() && st.front() + 12 <= vs.hi)
               v.push_back(st.front() + 12);
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
      int fifth = dom ? (has(8) || has(15) || has(13) ? 8 : 9) : (has(6) ? 6 : has(8) ? 8 : 7); // a 7#5 keeps its #5
      if (has(18))
         fifth = 18;                                                // the #11 in its place
      if (third == 3 && has(17))
         fifth = 17;                                                // a minor 11th's 11 in place of its fifth (an m11, not an m9)
      if (third == 5 && has(21))
         fifth = 21;                                                // a 13sus's 13 in place of its fifth
      if (seventh == 9)
         colour = 14, fifth = 7;                                    // a sixth chord: 3-5-6-9
      iv = {third, fifth, seventh, colour};
      // (At three notes an altered fifth, a #5 or #11, is the colour: a 7#11 isn't a plain 9th. So is
      // an m11's 11th, a 13sus's 13th and a dominant 13th's: without them they're an m9, a 9sus and a
      // 9th. Levine's three-note 13th is 3-13-7 or 7-3-13.)
      if (n == 3)
         iv = {third, seventh,
               fifth == 18 || fifth == 17 || fifth == 21 || (fifth == 9 && has(21)) || ((fifth == 8 || fifth == 6) && colour == 14)
                  ? fifth
                  : colour};
      if (n == 5) {
         // A fifth note the four don't already hold: the 11th of a minor chord, else the fifth, else
         // the 13th of a major or dominant one, else the root (never a half step over a major
         // seventh: then it stays at four).
         Ints pcs4;
         for (int i : iv)
            pcs4.push_back(i % 12);
         const auto held = [&](int i) { return std::find(pcs4.begin(), pcs4.end(), i % 12) != pcs4.end(); };
         // (Nor the fifth under a b13 put in its place, an altered 7b9's F Ab B D Eb, nor the root
         // a half step under it: an altered dominant stays at four.)
         const bool sharp5 = has(6) || has(8) || has(18) || fifth == 8 || fifth == 6;
         const int extra = (has(17) || (third == 3 && !has(6))) && !held(17) ? 17
                           : !held(7) && !sharp5                           ? 7
                           : third == 4 && !held(9) && !has(8) && !sharp5   ? 9
                           : seventh != 11 && fifth != 8 && fifth != 6      ? 0
                                                                            : -1;
         if (extra >= 0)
            iv.push_back(extra);
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
      // A close chord of n-1 notes, its bottom note doubled an octave up on top: the triad (root,
      // third, fifth, so the line can move through all three), with a seventh inside for four.
      Ints tones;
      for (size_t i = 0; i < q.tones.size() && tones.size() < 3; ++i)
         tones.push_back(q.tones[i]);
      // (A chord with no third holds its fourth in the third's place: C11 is C F G, not a C7.)
      const auto inQ = [&](int i) { return std::find(q.tones.begin(), q.tones.end(), i) != q.tones.end(); };
      if (!inQ(3) && !inQ(4) && (inQ(17) || inQ(5)) && tones.size() >= 2 && tones[1] != 5 && tones[1] != 17)
         tones = {0, 5, 7};
      // A seventh chord at three notes under the octave keeps its seventh in place of a perfect fifth
      // (A9 is heard as a dominant, not an A triad). An altered fifth (m7b5's b5, 7#5's #5) is the
      // chord, so the seventh goes in place of the root instead: the bass has it.
      if (q.seventh && n - 1 <= 3 && tones.size() >= 3)
         for (int i : {10, 11, 9})
            if (std::find(q.tones.begin(), q.tones.end(), i) != q.tones.end() && tones[2] % 12 != i % 12) {
               // (A major seventh takes the root's place, never the fifth's: B under a doubled C rubs.)
               // (Over a slash bass the root stays: Cmaj7/E without its C is E minor.)
               tones[tones[2] == 7 && (i != 11 || (c.bass >= 0 && c.bass != c.root)) ? 2 : 0] = i;
               break;
            }
      // An altered colour (b9, #9, #11) names the chord too: it goes in place of the root at three
      // notes (the montuno's b9 over the bass's root), else in place of a perfect fifth.
      for (int alt : {13, 15, 18})
         if (std::find(q.tones.begin(), q.tones.end(), alt) != q.tones.end()) {
            for (int want : {n - 1 <= 3 ? 0 : 7, 7, 0}) {
               auto it = std::find(tones.begin(), tones.end(), want);
               if (it != tones.end()) {
                  *it = alt;
                  break;
               }
            }
            break;
         }
      for (int i : {10, 11, 9})
         if (static_cast<int>(tones.size()) < n - 1 && q.seventh &&
             std::find(q.tones.begin(), q.tones.end(), i) != q.tones.end() &&
             std::find(tones.begin(), tones.end(), i) == tones.end()) {
            const auto root = std::find(tones.begin(), tones.end(), 0);
            if (i == 11 && root != tones.end())
               *root = i; // (in the root's place, as above)
            else
               tones.push_back(i);
         }
      if (tones.size() < 2)
         tones = pickTones(c, std::max(2, n - 1));
      Ints pcs = pcsOf(tones);
      std::sort(pcs.begin(), pcs.end());
      pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
      // The octave is on the root, the third or the fifth: the line a montuno's thumbs carry (any
      // tone, for a chord with no third).
      // (But a 7#9's: within the octave its #9 is a half step under the doubled third on top, a
      // cluster, wherever the octave is but on the #9 itself. There it is the Hendrix chord's #9 on
      // top, a major seventh over the 3rd: Eb E Bb Eb.)
      const bool sharp9 = inQ(15) && inQ(4);
      const size_t had = out.size();
      for (int pass = 0; pass < 2 && out.size() == had; ++pass)
         for (auto st : detail::closeStacks(pcs, vs.lo, vs.hi)) {
            if (st.back() - st.front() >= 12)
               continue;
            // (The chord's own third and fifth: a 7#9's #9 isn't its third.)
            const int outer = ((st.front() - c.root) % 12 + 12) % 12;
            const int own3 = q.tones.size() > 1 ? q.tones[1] % 12 : -1, own5 = q.tones.size() > 2 ? q.tones[2] % 12 : 7;
            if (pass == 0 && outer != 0 && outer != own3 && outer != own5 && outer != 7 && !(sharp9 && outer == 3))
               continue;
            st.push_back(st.front() + 12);
            detail::addCandidate(out, st, vs, -1);
         }
      // (Those only, where one sits clear of the low register's rubs: low down, the clashing ones.)
      if (sharp9) {
         Voicings clean;
         for (const Voicing &v : out) {
            const Breaks k = breaksOf(v, nullptr, vs);
            if (clashes(v, c) <= 0.0 && !k.mud && !(k.rub && vs.noRubs))
               clean.push_back(v);
         }
         if (!clean.empty())
            out = clean;
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
      // The tones that say what the chord is, beyond its root and fifth: its third, its seventh, then
      // its colour or altered tone, by priority.
      Ints colour;
      for (int i : q.priority)
         if (i % 12 != 0 && i != 7 && (i == q.tones[1] || q.seventh || q.tones.size() > 3))
            colour.push_back(i);
      const bool slash = bassPc != c.root;
      Voicings reach; // the close grips, for a chord the usual ones can't fret
      Voicings thin;  // the same with the fifth in place of the colour, where none of them frets
      int fifthIv = 7; // the chord's fifth (a diminished or augmented one, if that's what it has)
      if (std::find(q.tones.begin(), q.tones.end(), 7) == q.tones.end())
         for (int i : {6, 8})
            if (std::find(q.tones.begin(), q.tones.end(), i) != q.tones.end())
               fifthIv = i;
      // The open chords a guitarist plays first, standard tuning (E2 A2 D3 G3 B3 E4): G's 320003,
      // D's xx0232, C7's x32310 and the rest. Each is offered where it's this chord, cut to the
      // voices on its lowest strings (a five-string G is 32000x). Where that cut loses a tone that
      // names the chord, its top strings instead, over the root or fifth (G7 at four strings is
      // xx0001, D G B F: 3200 has lost its F, and the bass player has the G).
      if (!slash) {
         Ints chordPcs;
         for (int i : q.tones)
            chordPcs.push_back((c.root + i) % 12);
         for (const auto &shape : detail::kOpenGrips)
            for (bool top : {false, true}) {
               const Ints all = detail::gripCut(shape, n, top);
               const int low = all.empty() ? -1 : (all.front() - c.root + 120) % 12;
               if (all.size() < 4 || (low != 0 && !(top && low == 7 && fifthIv == 7)))
                  continue;
               // Every note the chord's, and every tone that names it there (its third, seventh and
               // colour: an open E is no E7#9 or Eadd9); only a perfect fifth may go.
               const auto whole = [&](const Ints &notes) {
                  bool fits = true, named = true;
                  for (int x : notes)
                     fits = fits && std::find(chordPcs.begin(), chordPcs.end(), x % 12) != chordPcs.end();
                  for (int i : q.tones)
                     if (i % 12 != 7)
                        named = named && std::any_of(notes.begin(), notes.end(), [&](int x) { return (x - c.root + 120) % 12 == i % 12; });
                  return fits && named;
               };
               if (top && whole(detail::gripCut(shape, n)))
                  continue; // (the bass string's cut has it all: G at four is 3200)
               if (whole(all)) {
                  Stack v;
                  for (int x : all)
                     v.push_back(x);
                  detail::addCandidate(out, v, vs, v.front());
               }
            }
      }
      for (int b = vs.floor; b < std::max(vs.floor + 24, vs.lo); ++b) {
         if (b % 12 != bassPc || (!slash && b < lowIntervalLimit(fifthIv) - 2))
            continue;
         if (slash) {
            for (const auto &st : detail::closeStacks(pcsOf(overSlash(std::max(2, n - 1))), b + 3, b + 20)) {
               if (st.front() <= b + 2 || st.front() > b + 9)
                  continue;
               Stack v{b};
               v.insert(v.end(), st.begin(), st.end());
               detail::addCandidate(out, v, vs, b);
            }
            continue;
         }
         // The grips a guitarist reaches for when the fifth-and-octave one can't be fretted (Cmaj7's
         // x32000, Fadd9's xx3213): the root under the chord's tones stacked close, the tones that
         // name it first and the fifth where there's a string left (a 7#9 at four strings is the
         // root, 3rd, 7th and #9, never an A7's x0202x with the #9 left out); gripFrets picks the ones
         // a hand can make.
         // (Only where none of the usual grips can be: an E major is the open E.)
         if (n >= 4 && !colour.empty() && (q.seventh || q.tones.size() > 3)) {
            Ints tones(colour.begin(), colour.begin() + std::min<int>(n - 1, static_cast<int>(colour.size())));
            if (static_cast<int>(tones.size()) < n - 1)
               tones.push_back(fifthIv);
            for (const auto &st : detail::closeStacks(pcsOf(tones), b + 3, b + 22)) {
               if (st.front() <= b + 2 || st.front() > b + 12)
                  continue;
               Stack w{b};
               w.insert(w.end(), st.begin(), st.end());
               detail::addCandidate(reach, w, vs, b);
            }
            if (static_cast<int>(colour.size()) > n - 2) {
               Ints less(colour.begin(), colour.begin() + (n - 2));
               less.push_back(fifthIv);
               for (const auto &st : detail::closeStacks(pcsOf(less), b + 3, b + 22)) {
                  if (st.front() <= b + 2 || st.front() > b + 12)
                     continue;
                  Stack w{b};
                  w.insert(w.end(), st.begin(), st.end());
                  detail::addCandidate(thin, w, vs, b);
               }
            }
         }
         // A chord whose colour won't fit over the fifth and octave (a 7th at four strings, a 7#9, a
         // 13th) is gripped as a guitarist does: the root, then the third, seventh and colour close
         // above, the fifth and octave dropped (E G# D G, the Hendrix chord).
         if (n >= 4 && (q.seventh || q.tones.size() > 3) && static_cast<int>(colour.size()) > n - 3) {
            Ints tones(colour.begin(), colour.begin() + std::min<int>(n - 1, static_cast<int>(colour.size())));
            for (const auto &st : detail::closeStacks(pcsOf(tones), b + 3, b + 20)) {
               if (st.front() <= b + 2 || st.front() > b + 10)
                  continue;
               Stack w{b};
               w.insert(w.end(), st.begin(), st.end());
               detail::addCandidate(out, w, vs, b);
               // Short of strings (a 9th's 3, 7 and 9 at five): the fifth on top too, C9's x32333.
               if (static_cast<int>(w.size()) < n) {
                  const bool altered5 = std::any_of(q.tones.begin(), q.tones.end(), [](int i) { return i == 6 || i == 8 || i == 18; });
                  int top = w.back() + 1;
                  while (((top - (altered5 ? b : b + 7)) % 12 + 12) % 12)
                     ++top;
                  w.push_back(top);
                  detail::addCandidate(out, w, vs, b);
               }
            }
            continue;
         }
         // A triad's open grip as well: the root, then the third, fifth, octave and third close above
         // it (C's x32010, G's 32000x): gripFrets keeps it where the fingers can make it.
         if (n >= 4 && q.tones.size() == 3 && !q.seventh && (q.tones[1] == 3 || q.tones[1] == 4)) {
            const int third = q.tones[1];
            const int shape[5] = {0, third, fifthIv, 12, 12 + third};
            Stack open;
            for (int i = 0; i < std::min(n, 5); ++i)
               open.push_back(b + shape[i]);
            detail::addCandidate(out, open, vs, b);
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
      // (The close grips a hand can make come in beside the rest, so a seventh at four strings has its
      // E-form 1312xx as well as a three-string shell: the cost of a missing voice picks. Where no
      // grip can be fretted, all of them.)
      // (Where no grip a hand can make holds the tones that name the chord, the ones with its fifth
      // in place of its colour too: B13 at four strings keeps its 3rd and 7th, x2424x, rather
      // than a string fewer and a bare fifth. Never beside one that names it: an open A7 is no A7#9.)
      const auto names = [&](const Voicing &v) {
         for (size_t k = 0; k < colour.size() && k < 3; ++k) {
            bool held = false;
            for (int i = 0; i < v.count; ++i)
               held = held || (v.notes[static_cast<size_t>(i)] - c.root + 120) % 12 == colour[k] % 12;
            if (!held)
               return false;
         }
         return true;
      };
      const auto frets = [](const Voicing &v) { return gripFrets(v) >= 0.0; };
      const auto playable = [&](const Voicings &from) {
         return std::any_of(from.begin(), from.end(), [&](const Voicing &v) { return frets(v) && names(v); });
      };
      if (!playable(out) && !playable(reach))
         for (const Voicing &v : thin)
            if (std::find(reach.begin(), reach.end(), v) == reach.end())
               reach.push_back(v);
      if (std::none_of(out.begin(), out.end(), frets)) {
         for (const Voicing &v : reach)
            out.push_back(v);
      } else
         for (const Voicing &v : reach)
            if (frets(v) && std::find(out.begin(), out.end(), v) == out.end())
               out.push_back(v);
      // A chord no grip fits (a slash chord with no room, a diminished chord over a low FLOOR) is
      // still played: close, over its bass.
      if (out.empty())
         return candidates(c, vs, kClose);
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
      // DOUBLE is a rule on the voicing, not on how it was built: (D F# A)+D and (F# A D)+D are the
      // same notes. A doubled top is kept only where nothing else fits.
      Voicings doubledTop;
      const size_t had = out.size();
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
                     if (std::find(g.begin(), g.end(), d) != g.end())
                        continue;
                     Stack v = g;
                     v.push_back(d);
                     std::sort(v.begin(), v.end());
                     next.push_back(v);
                  }
               }
            grown = next;
         }
         for (const auto &v : grown) {
            bool topTwice = false;
            for (size_t i = 0; i + 1 < v.size(); ++i)
               topTwice = topTwice || ((v[i] - v.back()) % 12 + 12) % 12 == 0;
            if (vs.doubleMiddle && !doubles.empty() && topTwice)
               detail::addCandidate(doubledTop, v, ds, -1);
            else
               detail::addCandidate(out, v, ds, -1);
         }
      }
      // DOUBLE is a cost in the ranking (a doubled top +6), not a filter: where the line needs it
      // (a triad at four voices, its top held), the doubled top is still a choice.
      (void)had;
      for (const Voicing &v : doubledTop)
         out.push_back(v);
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
               // (Still n notes in all: the bass's pitch class isn't doubled above it, and a doubled
               // tone gives way; C/E at four is E3 C4 G4 C5, not E3 C4 E4 G4 C5.)
               Stack w{b};
               for (int i = 0; i < v.count; ++i)
                  if (v.notes[static_cast<size_t>(i)] % 12 != c.bass)
                     w.push_back(v.notes[static_cast<size_t>(i)]);
               for (int k = static_cast<int>(w.size()) - 1; k > 0 && static_cast<int>(w.size()) > n; --k)
                  for (int j = 1; j < k; ++j)
                     if (w[static_cast<size_t>(j)] % 12 == w[static_cast<size_t>(k)] % 12) {
                        w.erase(w.begin() + k);
                        break;
                     }
               if (w.size() < 3)
                  continue;
               detail::addCandidate(withBass, w, vs, b);
            }
         // No room under them (FLOOR): the bass at its lowest over FLOOR, the voicing's notes above it
         // (as above, never its pitch class again, n notes in all; a note at or under the bass goes up
         // an octave rather than out: Cm7b5/Gb keeps its Eb, Gb3 C4 Eb4 Bb4, not Gb3 Bb3 Gb4 C5).
         // Too many, and a doubled tone gives way, then the one that matters least (the fifth before
         // the root, which stays over a slash bass).
         if (withBass.empty()) {
            int b = std::max(0, vs.floor);
            while (b % 12 != c.bass)
               ++b;
            const auto rank = [&](int x) {
               const int iv = ((x - c.root) % 12 + 12) % 12;
               if (iv == 0)
                  return -1;
               for (size_t k = 0; k < q.priority.size(); ++k)
                  if (q.priority[k] % 12 == iv)
                     return static_cast<int>(k);
               return 99;
            };
            for (const Voicing &v : out) {
               Stack w{b};
               for (int i = 0; i < v.count; ++i) {
                  int x = v.notes[static_cast<size_t>(i)];
                  if (x % 12 == c.bass)
                     continue;
                  while (x <= b + 2)
                     x += 12;
                  if (x <= vs.hi && std::find(w.begin(), w.end(), x) == w.end())
                     w.push_back(x);
               }
               std::sort(w.begin() + 1, w.end());
               for (int k = static_cast<int>(w.size()) - 1; k > 0 && static_cast<int>(w.size()) > n; --k)
                  for (int j = 1; j < k; ++j)
                     if (w[static_cast<size_t>(j)] % 12 == w[static_cast<size_t>(k)] % 12) {
                        w.erase(w.begin() + k);
                        break;
                     }
               while (static_cast<int>(w.size()) > n) {
                  size_t least = 1;
                  for (size_t k = 2; k < w.size(); ++k)
                     if (rank(w[k]) >= rank(w[least]))
                        least = k;
                  w.erase(w.begin() + static_cast<long>(least));
               }
               if (w.size() >= 3)
                  detail::addCandidate(withBass, w, ds, b);
            }
         }
         if (!withBass.empty())
            out = withBass;
      }
      break;
   }
   }
   // Nothing fits the window (a narrow one, a big shape): a voice fewer, then a wider window.
   if (out.empty() && vs.voices > 3) {
      VoiceSettings fewer = vs;
      fewer.voices = vs.voices - 1;
      Voicings r = candidates(c, fewer, shapeOverride, false);
      if (!r.empty())
         return r;
   }
   if (out.empty() && widen && vs.hi - vs.lo < 60) {
      VoiceSettings wide = vs;
      wide.lo = std::max(vs.floor, vs.lo - 6);
      wide.hi = vs.hi + 6;
      wide.maxSpan = vs.maxSpan + 7;
      return candidates(c, wide, shapeOverride);
   }
   // A 7#9 whose every stack puts the #9 under the 3rd (a cluster, not the Hendrix chord) looks a
   // little lower and wider for one with the #9 on top.
   if (!out.empty() && widen && vs.hi - vs.lo < 60) {
      const Quality &q = qualityOf(c);
      const auto has = [&](int i) { return std::find(q.tones.begin(), q.tones.end(), i) != q.tones.end(); };
      if (has(15) && has(4) &&
          std::all_of(out.begin(), out.end(), [&](const Voicing &v) { return clashes(v, c) > 0.0; })) {
         VoiceSettings wide = vs;
         wide.lo = std::max(vs.floor, vs.lo - 6);
         wide.hi = vs.hi; // (lower only: HIGHEST is the window's top)
         wide.maxSpan = vs.maxSpan + 7;
         for (const Voicing &v : candidates(c, wide, shapeOverride, false))
            if (clashes(v, c) <= 0.0)
               out.push_back(v);
      }
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
   // Of different sizes: the voices that start and the voices that stop, both, halved (counting only
   // the new voicing's notes would make a thinner voicing always the cheaper).
   const auto oneWay = [](const Voicing &x, const Voicing &y) {
      int sum = 0;
      for (int i = 0; i < x.count; ++i) {
         int best = 99;
         for (int j = 0; j < y.count; ++j)
            best = std::min(best, std::abs(x.notes[static_cast<size_t>(i)] - y.notes[static_cast<size_t>(j)]));
         sum += best;
      }
      return sum;
   };
   m = (oneWay(b, a) + oneWay(a, b) + 1) / 2;
   return m;
}

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
   // SPREAD's bass is the left hand's: the window's middle is where the right hand's part sits, and
   // the hole between the hands costs past a tenth.
   const int from = vs.shape == kSpread && v.count > 2 ? 1 : 0;
   double mean = 0;
   for (int i = from; i < v.count; ++i)
      mean += v.notes[static_cast<size_t>(i)];
   mean /= std::max(1, v.count - from);
   const double centre = std::abs(mean - vs.center);
   double c = 0;
   if (from)
      c += 1.5 * std::max(0, v.notes[1] - v.notes[0] - 16);
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
   // (A guitar's grip keeps its low thirds and fifths: the open G's B2 is the open G.)
   if (b.mud && vs.shape != kGrip)
      c += 14.0;
   return c;
}

// What a voicing costs for the chord it voices: a minor ninth between two of its notes (a major
// seventh under the root an octave up, a 9th under the b3), which turns the colour into a cluster,
// except the b9 over the root a 7b9 is made of; and a 7#9's #9 under its 3rd (the Hendrix chord has
// it on top).
inline double clashes(const Voicing &v, const Chord &c) {
   const Quality &q = qualityOf(c);
   const auto has = [&](int i) { return std::find(q.tones.begin(), q.tones.end(), i) != q.tones.end(); };
   const auto iv = [&](int n) { return ((n - c.root) % 12 + 12) % 12; };
   double k = 0.0;
   for (int i = 0; i < v.count; ++i)
      for (int j = i + 1; j < v.count; ++j) {
         const int lo = v.notes[static_cast<size_t>(i)], hi = v.notes[static_cast<size_t>(j)];
         if (hi - lo >= 13 && (hi - lo) % 12 == 1 && !(has(13) && iv(lo) == 0))
            k += 20.0; // as much as the maj7-under-root rub: a clean voicing nearby wins
         // A major seventh with the root a half step (or a minor 9th) over it: the rub the
         // voicing texts rule out (Pease & Pullig; Levine).
         if (has(11) && iv(lo) == 11 && iv(hi) == 0)
            k += hi - lo == 1 ? 24.0 : 20.0;
      }
   if (has(15) && has(4)) {
      int third = -1, sharp9 = -1;
      for (int i = 0; i < v.count; ++i) {
         const int n = v.notes[static_cast<size_t>(i)];
         if (iv(n) == 4)
            third = std::max(third, n);
         if (iv(n) == 3)
            sharp9 = std::max(sharp9, n);
      }
      if (third >= 0 && sharp9 >= 0 && sharp9 < third)
         k += 9.0;
   }
   return k;
}

// The voicings of a chord after `prev`, best first.
// A grip's fingering on a guitar in standard tuning: its notes on neighbouring strings, low to high
// (no string skipped inside the strum), the fretted ones within four frets and four fingers. What it
// costs: the position up the neck (the open chords first), or -1 if no hand can grip it.
inline double gripFrets(const Voicing &v) {
   static constexpr int kOpen[6] = {40, 45, 50, 55, 59, 64};
   // (An open chord, cut to neighbouring strings, is fretted as written.)
   if (detail::openGrip(v))
      return 0.0;
   double best = -1.0;
   for (int first = 0; first + v.count <= 6; ++first) {
      int lo = 99, hi = -1;
      int fret[6] = {};
      bool ok = true;
      for (int i = 0; i < v.count && ok; ++i) {
         fret[i] = v.notes[static_cast<size_t>(i)] - kOpen[first + i];
         ok = fret[i] >= 0 && fret[i] <= 15;
         if (ok && fret[i] > 0) {
            lo = std::min(lo, fret[i]);
            hi = std::max(hi, fret[i]);
         }
      }
      if (!ok || (hi >= 0 && hi - lo > 3))
         continue;
      // The fingers it takes: the index across the lowest fret (a barre, where no open string rings
      // inside it; else a finger for each run of neighbouring strings there), then one for each run
      // of neighbouring strings at a higher fret (a finger laid flat across them). A diminished
      // triad's 89a87x takes five: no hand.
      if (hi >= 0) {
         int firstLo = -1, lastLo = -1;
         for (int i = 0; i < v.count; ++i)
            if (fret[i] == lo) {
               firstLo = firstLo < 0 ? i : firstLo;
               lastLo = i;
            }
         bool barre = true;
         for (int i = firstLo; i <= lastLo; ++i)
            barre = barre && fret[i] != 0;
         int fingers = barre ? 1 : 0;
         for (int i = 0; i < v.count; ++i)
            if (fret[i] > 0 && (fret[i] > lo || !barre) && (i == 0 || fret[i - 1] != fret[i]))
               ++fingers;
         if (fingers > 4)
            continue;
      }
      const double cost = hi < 0 ? 0.0 : lo;
      if (best < 0.0 || cost < best)
         best = cost;
   }
   return best;
}

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
   // Three voices are a guitar's three strings (a chop) or a light right hand: no half-step cluster
   // (an m9's b7-9-b3) when a plainer three will do.
   if (vs.voices == 3) {
      Voicings open;
      for (const Voicing &v : all) {
         bool cluster = false;
         for (int i = 1; i < v.count; ++i)
            cluster = cluster || v.notes[static_cast<size_t>(i)] - v.notes[static_cast<size_t>(i - 1)] == 1;
         if (!cluster)
            open.push_back(v);
      }
      if (!open.empty())
         all = open;
   }
   // A grip is one a hand can make, low on the neck: the open E is the open E every time it comes
   // round, not wherever the last chord left the hand.
   const bool grip = vs.shape == kGrip;
   if (grip) {
      Voicings playable;
      for (const Voicing &v : all)
         if (gripFrets(v) >= 0.0)
            playable.push_back(v);
      if (!playable.empty())
         all = playable;
      // None a hand can make (a 13th, a 13sus or an m11 at five strings): a string fewer, as a
      // guitarist leaves one out, rather than a cluster no hand can fret.
      else if (vs.voices > 3) {
         VoiceSettings fewer = vs;
         --fewer.voices;
         const Voicings less = rankShapes(c, prev, fewer);
         if (!less.empty() && gripFrets(less[0]) >= 0.0)
            return less;
      }
   }
   // By cost, ties kept in order (an insertion sort: no allocation).
   FixedVec<double, 256> costs;
   // DOUBLE IN THE MIDDLE: the top note's pitch sounded again under it costs (the octaves and the
   // fourths are made of their doublings; a left hand's bass may have it).
   // (Nor a guitar's grip: its open chords double the top, C's x32010 and G's 32000x.)
   const bool middle = vs.doubleMiddle && vs.shape != kOctaves && vs.shape != kQuartal && vs.shape != kGrip;
   const int from = vs.shape == kSpread || vs.shape == kGrip || vs.shape == kShell ? 1 : 0;
   const auto doubledTop = [&](const Voicing &v) {
      for (int i = from; i + 1 < v.count; ++i)
         if (v.notes[static_cast<size_t>(i)] % 12 == v.top() % 12)
            return true;
      return false;
   };
   // A voice short of what the candidates can give costs (a full voicing over a thin one that moves
   // a little less).
   int fullest = 0;
   for (const Voicing &v : all)
      fullest = std::max<int>(fullest, std::min<int>(v.count, vs.voices + (vs.shape == kSpread || vs.shape == kShell ? 1 : 0)));
   for (const Voicing &v : all) {
      const double frets = grip ? gripFrets(v) : -1.0;
      // (An open chord is the guitarist's first choice: D's xx0232 over a barre on five strings.)
      const bool open = grip && detail::openGrip(v);
      // (Fourths more: VOICES 5 of fourths is a five-note stack, not the three-note one that sits
      // nearer the middle.)
      // (And each voice more short costs more than the last: VOICES 5 never settles for three notes
      // where four sit a little higher.)
      const double short1 = open ? 0.5 : vs.shape == kQuartal ? 4.0 : 1.5;
      const int shortBy = std::max(0, fullest - v.count);
      const double shortCost = vs.shape == kQuartal ? short1 * shortBy * (shortBy + 1) / 2.0 : short1 * shortBy;
      const double top = (middle && doubledTop(v) ? 6.0 : 0.0) + shortCost -
                         (open ? 2.0 : 0.0);
      costs.push_back(top + (frets >= 0.0 ? 1.5 * frets + cost(v, nullptr, vs) * 0.3 + clashes(v, c) : cost(v, prev, vs) + clashes(v, c)));
   }
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
   const Voicings r = rankShapes(c, prev, vs);
   if (r.empty())
      return Voicing{};
   // SAME SHAPE: a repeated chord keeps its voicing, while the shape, voices and window still allow
   // it (a change of any of them voices it afresh).
   if (vs.sameShape && prevChord && *prevChord == c && prev && !prev->empty() && pick < 0)
      for (const Voicing &v : r)
         if (v == *prev)
            return v;
   // A new chord is heard as one: not the very notes of the chord before (a ii11 into a V13sus can
   // share every pitch), when another voicing nearby will do. A chord only changing colour (C7 to
   // C9, over the same root) is the same chord, and stays where it is rather than jumping away.
   if (pick < 0 && prevChord && *prevChord != c && prev && r.size() > 1 &&
       (prevChord->root != c.root || prevChord->bass != c.bass) && r[0] == *prev)
      for (size_t i = 1; i < r.size(); ++i)
         // (And no rub the best hasn't got: a minor ninth isn't the price of a fresh voicing.)
         if (std::abs(r[i].top() - prev->top()) <= std::max(2, vs.topMove) &&
             std::abs(r[i].bottom() - prev->bottom()) <= 7 && clashes(r[i], c) <= clashes(r[0], c))
            return r[i];
   return r[static_cast<size_t>(std::clamp(pick, 0, static_cast<int>(r.size()) - 1))];
}

// The OCTAVES shape's line, as a montuno's thumbs move it through a chord: the `step`-th stroke on
// it has the fifth, third, root, third (the chord's own: a sus fourth, a diminished fifth) at the
// bottom, doubled on top. Each stroke takes the octave nearest the last stroke's bottom, so the line
// moves by a step or a third across a change too; a chord's first stroke goes back to the window's
// middle only when the line has strayed an octave from it.
inline Voicing octaveLine(const Chord &c, const Voicing *prev, const VoiceSettings &vs, int step) {
   const auto &q = qualityOf(c).tones;
   const int third = q.size() > 1 ? q[1] % 12 : 4, fifth = q.size() > 2 ? q[2] % 12 : 7;
   const int want[4] = {fifth, third, 0, third};
   const int degree = want[((step % 4) + 4) % 4];
   // The strokes after the first may dip up to a sixth under the window (not into the bass), rather than leap a sixth
   // up for a root the window's floor cuts off. A new chord's first stroke follows the line too (the
   // octaves carry the voice leading across the change: E's G# to A's A, not an octave's leap back
   // to the middle), unless that strays more than an octave from the window's middle.
   const bool follow = prev && !prev->empty();
   VoiceSettings wide = vs;
   if (follow)
      wide.lo = vs.lo - 4; // (a third under the window at most: never down into the bass's register)
   const Voicings r = rankShapes(c, prev, wide);
   // The degree wanted, else (a seventh chord's stack with no fifth, a maj7's with no root) another
   // of the chord's tones, so the line still moves: the seventh, then the others.
   int prefs[5] = {degree, q.size() > 3 ? q[3] % 12 : fifth, fifth, 0, third};
   const Voicing *leap = nullptr;
   for (int pass = 0; pass < 5; ++pass) {
      const int want1 = prefs[pass];
      const Voicing *best = nullptr;
      double bestD = 1e9;
      for (const Voicing &v : r) {
         if (v.count < 2 || ((v.notes[0] - c.root) % 12 + 12) % 12 != want1)
            continue;
         if (v.notes[0] < vs.lo && !(follow && std::abs(v.notes[0] - prev->notes[0]) <= 5 && v.notes[0] >= vs.lo - 4))
            continue;
         // (Not the stroke before again: the line moves. In the first pass too, within a chord, when
         // the degree wanted is the one just struck.)
         if ((pass > 0 || step % 4 != 0) && follow && v.notes[0] == prev->notes[0])
            continue;
         double mean = 0.0;
         for (int i = 0; i < v.count; ++i)
            mean += v.notes[static_cast<size_t>(i)];
         const double off = std::abs(mean / v.count - vs.center);
         // Near the stroke before, pulled back up into the window, and a chord's first stroke back
         // toward the middle when the line has strayed an octave from it.
         // And the stroke after in reach: a dip under the window the next degree can't follow by a
         // step or a third (E3, then A's C#3 under the floor) is a sixth's leap one stroke later.
         const int v0 = v.notes[0], nextPc = (c.root + want[((step + 1) % 4 + 4) % 4]) % 12;
         int ahead = 12;
         for (int x = vs.lo - 4; x <= vs.hi; ++x)
            if (x % 12 == nextPc && (x >= vs.lo || std::abs(x - v0) <= 5) && x + 12 <= vs.hi) // (its octave fits too)
               ahead = std::min(ahead, std::abs(x - v0));
         double d = follow ? std::abs(v0 - prev->notes[0]) + 0.05 * off + 0.5 * std::max(0, vs.lo - v0) +
                                 0.6 * std::max(0, ahead - 5) + (step == 0 && off > 12 ? 100.0 : 0.0)
                           : off;
         if (d < bestD - 1e-9) {
            bestD = d;
            best = &v;
         }
      }
      // (A degree a sixth's leap away gives way to another of the chord's, in reach by a step or a
      // third: E's G#3 goes to A's A3, not up to E4.)
      if (best && (!follow || std::abs(best->notes[0] - prev->notes[0]) <= 5))
         return *best;
      if (best && !leap)
         leap = best;
   }
   if (leap)
      return *leap;
   return r.empty() ? Voicing{} : r[0];
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
// The left hand's note: the bass (or root) at or above FLOOR, or its alternate (the fifth, or a
// slash chord's root). A guitar's thumb takes the alternate below when it would ring more than an
// octave over the floor (A2 then E2); a pianist's left hand keeps the root lowest (1-5-8), but not
// up into the chord: where the fifth above would reach the right hand's lowest note (`under`), it's
// the fifth below (E3 then B2 under B3 E4 G#4, the boom-chick's alternate bass).
inline int bassNote(const Chord &c, int floor, bool fifth = false, bool guitar = true, int under = 128) {
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
   if (guitar)
      return up <= floor + 12 ? up : up - 12;
   // (Under a slash chord the slash bass stays the lowest note: the alternate goes above it, or the
   // bass is struck again, never the root under it: B/D#'s D# is the line, B2 under it undoes it.)
   if (c.bass >= 0 && c.bass != c.root)
      return up > under - 3 ? n : up;
   return up > under - 3 && up - 12 >= floor ? up - 12 : up; // (FLOOR holds: nothing under it)
}

// ---- passing runs ----

// A chord's own scale, as a mask of pitch classes: the scale a player runs over it when it isn't
// the key's. Its tones, and between them the usual steps: the 9th (b9 if the chord has it), the 11th
// (#11 with one), the 13th (b13 on a minor chord or a dominant where the key has it, or with a b9;
// a dominant's b9 where the key has it and not the 9th), and a seventh
// for a chord without one (the key's, else b7). Diminished chords run the whole-half scale,
// half-diminished the locrian with a natural 9th, augmented the whole-tone scale.
inline unsigned chordScale(const Chord &c, int key, int scale) {
   if (c.none())
      return 0;
   unsigned t = 0;
   for (int i : qualityOf(c).tones)
      t |= 1u << (i % 12);
   const auto has = [&](int i) { return (t >> i & 1) != 0; };
   const auto key_ = [&](int i) { return inScale(c.root + i, key, scale); };
   unsigned m = 0;
   const auto add = [&](int i) { m |= 1u << (i % 12); };
   if (has(3) && has(6) && !has(10)) {
      for (int i : {0, 2, 3, 5, 6, 8, 9, 11})
         add(i);
   } else if (has(3) && has(6)) {
      for (int i : {0, 2, 3, 5, 6, 8, 10})
         add(i);
   } else if (has(4) && has(8) && !has(7)) {
      for (int i : {0, 2, 4, 6, 8, 10})
         add(i);
   } else {
      add(0);
      // A dominant takes the key's 9th and 13th where the key has only the flat ones: a minor key's
      // V7 (E7 in A minor) runs the harmonic minor's F and C, not F# and C# against the tonic's
      // third (Levine: the minor ii-V's V7 is b9 b13). V7/vi in a major key is the same chord.
      const bool dom7 = has(4) && has(10) && !has(2) && !has(9);
      const bool flatNine = has(1) || (dom7 && key_(1) && !key_(2));
      add(flatNine ? 1 : 2);
      if (has(3) && has(4))
         add(3); // a #9
      if (has(4))
         add(4);
      else if (has(3))
         add(3);
      else
         add(4); // a sus chord's run passes through the third it suspends (no minor-third leap)
      add(has(6) ? 6 : 5);
      add(7);
      const bool minor = has(3) && !has(4);
      add(has(9) ? 9 : (flatNine || ((minor || dom7) && key_(8) && !key_(9))) ? 8 : 9);
      add(has(10) ? 10 : has(11) ? 11 : key_(11) ? 11 : 10);
   }
   unsigned pcs = t; // the chord's own tones, whatever the scale says
   for (int i = 0; i < 12; ++i)
      if (m >> i & 1)
         pcs |= 1u << i;
   unsigned out = 0;
   for (int i = 0; i < 12; ++i)
      if (pcs >> i & 1)
         out |= 1u << ((c.root + i) % 12);
   return out;
}

// A passing run of `length` notes into `target` (a MIDI note), from the line's last note `from`
// (-1 for none), each a scale step from the next and all between the two, so it carries the line on
// from where it is (a run never jumps away first, nor strikes the note sounding again). Fewer notes
// when the gap holds fewer; with `halfStep` its last note is a half step under the target when it
// comes from below.
//
// The scale is the key's while both chords are in it. Over a chord from outside the key (the one
// left, a passing diminished chord or a chord of a key change) it's that chord's own scale, with the
// tones of the chord it leads to; into a chord from outside the key, the chord's own scale (a
// secondary dominant's mixolydian, a borrowed Abmaj7's lydian-less major), so a run into D7 in C
// walks E F# G, never G#, and a run into Am in E walks B C D, not the key's D#.
inline Ints passingRun(int from, int target, int length, int key, int scale, bool halfStep, const Chord &to = Chord{},
                       const Chord &leaving = Chord{}) {
   Ints run;
   if (length <= 0)
      return run;
   unsigned keyMask = 0;
   for (int pc = 0; pc < 12; ++pc)
      if (inScale(pc, key, scale))
         keyMask |= 1u << pc;
   const auto inKey = [&](const Chord &c) {
      for (int pc : chordTones(c))
         if (!(keyMask >> pc & 1))
            return false;
      return true;
   };
   unsigned allowed = keyMask;
   if (!leaving.none() && !inKey(leaving)) {
      allowed = chordScale(leaving, key, scale);
      for (int pc : chordTones(to))
         allowed |= 1u << pc;
   } else if (!to.none() && !inKey(to)) {
      allowed = chordScale(to, key, scale);
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
   // All between the line's note and the target.
   if (from >= 0) {
      Ints kept;
      for (int x : run)
         if (up ? (x > from && x < target) : (x < from && x > target))
            kept.push_back(x);
      run = kept;
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
