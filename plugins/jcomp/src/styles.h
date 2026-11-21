#pragma once

// The style library: 25 genres in seven families, each with its comping rhythms, its progressions and
// the named styles built on them. Every rhythm, voicing note and progression here comes from the
// research behind JComp (plugins/jcomp/README.md, "Where the content comes from"): recordings,
// players' own words, method books and musicology. The numbers in each genre's `source` are that
// bibliography's. A pattern the sources describe but don't print exactly is our reading of it, and
// the README says which those are.
//
// A rhythm is one to four bars written as text, one character a step: sixteen to a 4/4 bar, or twelve
// for the triplet grid (12/8, the shuffle). A rhythm marked three beats a bar (a waltz's, a 6/8's) has
// twelve sixteenths to its bar. A '|' between bars is only for reading.
//
//    X accented chord   x chord   o soft chord   g ghosted chop (short and quiet)
//    t the top note     u the top two notes     b the bass note (a left hand, a thumb, a low string)
//    f the fifth the bass alternates with
//    1..9 a single line note, by its degree over the chord's root (1 the root, 3 the chord's third,
//         7 its seventh, 8 the root an octave up, 9 the ninth up there), from the chord's own tones
//         or the key's scale: a riff, a tenor guitar's line, a finger's figure
//    -  holds the step before   .  rest
//
// A progression is written in C (or C minor) and moved to the song's key: bars split by '|', two
// chords in a bar split by a space, each a chord name harmony.h's parseChord reads.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "harmony.h"
#include "shared.h"

namespace jcomp {

// Twists a style applies to its rhythms.
enum Mod : unsigned {
   kPushes = 1 << 0,    // anticipations: a hit an eighth before a chord change plays the new chord
   kSparse = 1 << 1,    // fewer hits
   kBusy = 1 << 2,      // more hits, ghosted chops between
   kLongs = 1 << 3,     // chords held longer
   kShorts = 1 << 4,    // everything chopped short
   kRunsIn = 1 << 5,    // passing runs into the changes
   kTopLine = 1 << 6,   // some hits only the top note (a guitar's line, a pianist's right hand)
   kLeftHand = 1 << 7,  // a bass note on the strong beats under the chords
   kRich = 1 << 8,      // extensions: 9ths, 11ths, 13ths
   kPlain = 1 << 9,     // triads and sevenths only
   kShuffle = 1 << 10,  // swung
   kHalfTime = 1 << 11, // hits thinned to half time
   kTriplet = 1 << 12,  // the triplet grid (12/8): uses the genre's twelve-step rhythms
   // A figure that is the style (a montuno on the clave, the bossa batida, Freddie Green's four):
   // DENSITY, the mutations, WEIRD's strays and the energy leave its onsets alone, its fill is the
   // figure itself, and no run breaks into it.
   kFixed = 1 << 13,
   kInterlock = 1 << 14, // two of its rhythms at once, interlocking (a tenor line over the chord chops)
   kSlip = 1 << 15,      // Cramer's slip note: a whole step slid into a chord tone, in place of runs
   kWaltz = 1 << 16,     // three beats a bar (3/4): the genre's three-beat rhythms
   kSixEight = 1 << 17,  // with kWaltz: the same bar in 6/8, two dotted-quarter beats (its six-beat rhythms)
};

// The progression a section is played over: its role in the song. The break plays the verse's.
enum ProgRole { kRoleIntro, kRoleVerse, kRolePre, kRoleChorus, kRoleBridge, kRoleOutro, kRoleCount };
inline int roleOf(int section) {
   static const int roles[kSections] = {kRoleIntro, kRoleVerse, kRolePre, kRoleChorus, kRoleBridge, kRoleVerse, kRoleOutro};
   return roles[std::clamp(section, 0, kSections - 1)];
}

struct Rhythm {
   const char *hits;
   double energy; // the energy it suits, 0..1
   int beats = 4; // a bar's beats: 4, 3 (twelve sixteenths a bar) or 6 (6/8: the same bar, in two dotted quarters)
};

struct ProgSet {
   int scale; // harmony.h's scale the set is in
   const char *roles[kRoleCount]; // intro, verse, pre, chorus, bridge, outro
};

struct Style {
   const char *name;
   const char *tag;
   int bpm;
   unsigned mods;
   int progSet = 0;     // which of the genre's progression sets
   unsigned rhythms = 0; // which of the genre's rhythms (bits), 0 all
   int player = -1;     // -1 the genre's
   // A voicing the style needs whatever the player (Freddie Green's low shells): -1 the player's.
   int shape = -1, voices = 0, lo = 0, hi = 0, center = 0;
   // Its own swing, and on eighths (1) or sixteenths (0): 0 and -1 the genre's.
   double swing = 0.0;
   int swingFeel = -1;
};

struct Genre {
   const char *name;
   const char *family;
   int player;      // the player it suits (players.h)
   double swing;    // 0.5 straight .. 0.75
   int swingFeel;   // 0 sixteenths, 1 eighths
   std::vector<Rhythm> rhythms;
   std::vector<Rhythm> fills;
   std::vector<ProgSet> progs;
   std::vector<Style> styles;
   const char *source; // where it comes from: the README bibliography's numbers
   // Every anticipation taken: the changes always come on the and of 4 (son, salsa, swing comping).
   bool pushAll = false;
};

inline const char *const kFamilies[] = {"Funk & Soul", "Church & Blues", "Jazz & Latin", "Pop & Rock", "Dance",
                                        "Caribbean & Africa", "Ambient"};
constexpr int kFamilyCount = 7;

// The steps a rhythm has (a '|' isn't one), and whether it is on the triplet grid.
inline int rhythmSteps(const char *hits) {
   int n = 0;
   for (const char *c = hits; *c; ++c)
      n += *c != '|';
   return n;
}
// (Its first bar says, where a '|' marks one: four bars of triplets are 48 steps, as three of sixteenths.)
inline bool rhythmTriplet(const char *hits) {
   if (const char *bar = std::strchr(hits, '|'))
      return bar - hits == 12;
   return rhythmSteps(hits) % 12 == 0 && rhythmSteps(hits) % 16 != 0;
}
// The grid a rhythm is on, and the one a style plays on.
inline uint8_t rhythmRate(const Rhythm &r) { return r.beats == 6 ? kRate68 : r.beats == 3 ? kRate34 : rhythmTriplet(r.hits) ? kRate8T : kRate16; }
inline uint8_t styleRate(unsigned mods) { return mods & kSixEight ? kRate68 : mods & kWaltz ? kRate34 : mods & kTriplet ? kRate8T : kRate16; }
// A style's swing: its own, or its genre's (more with kShuffle); and whether it swings eighths.
inline double styleSwingOf(const Genre &g, const Style &s) {
   const double sw = s.swing > 0.0 ? s.swing : g.swing + (s.mods & kShuffle ? 0.08 : 0.0);
   return std::clamp(sw, 0.5, 0.75);
}
inline int styleSwingFeelOf(const Genre &g, const Style &s) { return s.swingFeel >= 0 ? s.swingFeel : g.swingFeel; }
// A swing comper's style (swung eighths, not a sixteenth-note groove's lilt): its fills and its energy
// add offbeat stabs, not a pulse of chords on the beats.
inline bool swungEighths(const Genre &g, const Style &s) { return styleSwingFeelOf(g, s) == 1 && styleSwingOf(g, s) > 0.55; }

// The players, by index into players.h's list.
enum : int {
   kPocketKeys, kClavChopper, kChurchOrgan, kNeoSoulKeys, kJazzComper, kPopPianist,
   kRhythmGuitar, kSkankGuitar, kStrummer, kMontuno, kHouseStabs, kPadPlayer,
   kAmbientDrifter
};

inline const std::vector<Genre> &genres() {
   static const std::vector<Genre> g = {
      // ---- Funk & Soul ----
      {"Funk", kFamilies[0], kPocketKeys, 0.53, 0,
       // Stabs on the one; the offbeat clav counter-part; a two-bar clav riff on the "Superstition"
       // rhythm (eighths, the last a sixteenth early, the resolution on the last sixteenth: Hein),
       // its line from the minor pentatonic: the root, its octave, the b7, the octave (Hein's first
       // half-bar), then down through the 5th and 4th to the b3 (our reading); the chicken-scratch motor
       // (Stewart: muted, "occasional chords are allowed to ring out").
       {{"X.....x...x.X...", 0.35}, {"..x..x.x..x..x.x", 0.55}, {"1.8.7.8.5.43...X|1.8.7.8.5.4.3..X", 0.7}, {"X.gxg.x.gXg.x.gx", 0.9}},
       {{"X.gxg.x.gXg.xxxx", 0.8}},
       // Brown's static dominant vamp, the bridge a step up ("take it to the bridge"); Fela's Dorian i7.
       {{kMixolydian, {"C7#9|C7#9", "C9|C9|C9|C9", "F9|F9|C9|C9", "C9|F9|C9|F9", "D9|D9|D9|D9", "C9|C9|C7#9|C7#9"}},
        {kDorian, {"Cm9|Cm9", "Cm9|F9|Cm9|F9", "F9|F9|F9|F9", "Cm9|Cm9|F9|F9", "Dm9|Dm9|G9|G9", "Cm9|Cm9|Cm9|Cm9"}}},
       {{"Pocket Stabs", "Pocket", 100, 0, 0, 0b0001},
        {"Clav Riff", "Clav", 100, kTopLine, 1, 0b0100, kClavChopper},
        {"Dorian Groove", "Dorian", 108, kRich, 1, 0b0011},
        {"Chicken Scratch", "Scratch", 112, kBusy | kShorts, 0, 0b1000, kRhythmGuitar}},
       "5, 6, 13"},
      {"Soul", kFamilies[0], kChurchOrgan, 0.54, 0,
       // The backbeat on 2 and 4 (Motown's and Memphis's guitar and keys); pumping eighths; the
       // gospel 3-3-2 push.
       // The 60s R&B piano's pushed offbeats and its answer (MMA's R&B groove: the piano's and the organ's
       // parts, both played on piano here, each bar with its own chords: no bar left to one upbeat).
       {{"....X.......X...", 0.3}, {"x-x-X-x-x-x-X-x-", 0.6}, {"x--x--x-x--x--x-", 0.8},
        {"..x.x.x.x.x...x.|x.x.x...x...x.x.", 0.5}, {"....x.x.......x.|x.x.x...x...x...", 0.65}},
       {{"x-x-X-x-x-x-xxxx", 0.7}},
       {{kMajor, {"C|C7|F|Fm", "C|Am|F|G", "F|G|F|G", "C|F|C|F", "Dm7|G7|C|C", "C|C7|F Fm|C"}}},
       {{"Memphis Backbeat", "Backbeat", 96, kPlain, 0, 0b001, -1, kDrop2, 4, 55, 76, 64},
        {"Pumping Eighths", "Eighths", 112, 0, 0, 0b010, kPopPianist},
        {"Gospel Push", "Push", 104, kRunsIn, 0, 0b100},
        {"Sixties R&B", "60s R&B", 108, 0, 0, 0b11000, kPopPianist}},
       "1, 23, 61"},
      {"Motown", kFamilies[0], kPopPianist, 0.5, 0,
       // The backbeat ("never blew a backbeat": Van Dyke on Messina), quarter-note chords leaning on 2
       // and 4 with Benjamin's quarter-note snare, the
       // eighth-note pump with left-hand bass notes, and Hunter's bluesy, boogie-leaning piano: the
       // left hand rocking root and fifth on the beats, the right hand's chords on the swung ands.
       {{"....X.......X...", 0.3}, {"x...X...x...X...", 0.55}, {"x.x.x.x.x.x.x.x.", 0.75}, {"b.x.f.X.b.x.f.X.", 0.85}},
       {{"x.x.x.x.x.x.xxxx", 0.8}},
       // I-vi-ii-V, I-IV-I-V, IV-V-I; the outro turns round home (I-vi-ii-V-I).
       {{kMajor, {"C|Am|Dm7|G7", "C|Am|Dm|G", "C|F|C|G", "F|G|C|C", "F|F|G|G", "C|Am|Dm7 G7|C"}}},
       {{"Backbeat Piano", "Backbeat", 120, kLeftHand, 0, 0b0011},
        {"Straight Eighths", "Eighths", 128, kLeftHand, 0, 0b0100},
        {"Boogie Figure", "Boogie", 116, 0, 0, 0b1000, -1, -1, 0, 0, 0, 0, 0.62, 1}},
       "23, 52"},
      {"Disco", kFamilies[0], kRhythmGuitar, 0.5, 0,
       // Offbeat eighths over four on the floor; the 3-3-2 Rhodes figure; Rodgers' "chucking":
       // sixteenths, muted and ringing, on three-string inversions, the bar struck from its downbeat
       // ("Good Times" and "Le Freak" start on the one) with the motor between.
       {{"..x...x...x...x.", 0.4}, {"x..x..x...x..x..", 0.6}, {"XxgxgXgxxxgxgXgx", 0.9}},
       {{"XxgxgXgxgxgxgxxX", 0.9}},
       {{kDorian, {"Cm7|F13", "Cm7|F13|Cm7|F13", "Cm7|F7sus", "Cm7|F7sus|Cm7|F7sus", "Ebmaj7|Dm7|Cm7|Bb", "Cm7|F13|Cm7|Cm7"}}},
       {{"Chic Chucking", "Chucking", 116, kBusy, 0, 0b100},
        {"Offbeat Lift", "Lift", 122, kShorts, 0, 0b001},
        {"Rhodes Figure", "Rhodes", 112, kRich, 0, 0b010, kPocketKeys}},
       "40"},
      {"Neo-Soul", kFamilies[0], kNeoSoulKeys, 0.58, 0,
       // Long chords behind the beat with an anticipation; soft re-attacks; a sparse push. The feel
       // is the timing (Danielsen on D'Angelo's "Left and Right"): keys late, never on the hats.
       {{"x--.--x---.x--..", 0.4}, {"x--o--x-o-x--o--", 0.6}, {"x.....x-.x....x.", 0.7}},
       {{"x--o--x-o-x.xoxx", 0.7}},
       // A Dorian m9 vamp; IVmaj7-iii7-vi7 and ii7-V7sus in the relative major; m9s planed. Read as
       // minor: the vamp's Dorian IV13 is then a borrowed colour, and the relative major's iii keeps
       // its own 9th out.
       {{kMinor, {"Cm9|Dbm9 Cm9", "Cm9|F13|Cm9|F13", "Fm9|Bb13sus", "Abmaj7|Gm7|Cm7|Fm7 Bb13sus", "Abmaj9|Abmaj9|Fm9|Bb13sus", "Cm9|Dbm9 Cm9|Cm9|Cm9"}}},
       {{"Behind The Beat", "Lazy", 84, kLongs | kRich, 0, 0b001},
        {"Breathing Chords", "Breath", 76, kRich, 0, 0b010},
        {"Dilla Push", "Push", 90, kPushes | kRich, 0, 0b100}},
       "15"},
      {"90s R&B", kFamilies[0], kNeoSoulKeys, 0.5, 0,
       {{"x--------.x-----", 0.35}, {"x..x..x.....x...", 0.6}, {"x.x...x.x...x.x.", 0.8}},
       {{"x..x..x...x.xxxx", 0.7}},
       // IVmaj7-iii7-vi7; bVImaj7-bVII-I; ii7-V7sus-Imaj9.
       {{kMajor, {"Fmaj7|Em7|Am7|Am7", "Fmaj7|Em7|Am7|Am7", "Abmaj7|Bb|C|C", "Dm7|G13sus|Cmaj9|Cmaj9", "Abmaj7|Bb|Fmaj7|G13sus", "Fmaj7|Em7|Dm7 G13sus|Cmaj9"}}},
       {{"Slow Jam", "Slow Jam", 68, kLongs | kRich, 0, 0b001, kPadPlayer},
        {"New Jack Swing", "NJS", 100, kShorts, 0, 0b110, kPocketKeys, -1, 0, 0, 0, 0, 0.64},
        {"Quiet Storm", "Storm", 78, kRich, 0, 0b011}},
       "(none found: from the idiom)"},
      {"Lo-fi", kFamilies[0], kNeoSoulKeys, 0.57, 0,
       {{"x---------x-----", 0.35}, {"x-----.x--------", 0.5}, {"x..x..x...x.....", 0.7}},
       {{"x..x..x...x.x.xx", 0.6}},
       {{kMajor, {"Fmaj7 Em7|Am7|Dm7 G7|Cmaj7", "Fmaj7 Em7|Am7|Dm7 G7|Cmaj7", "Dm7|G7", "Fmaj7 Em7|Am7|Dm7 G7|Cmaj9", "Dm9|G13|Em7|Am9", "Fmaj7 Em7|Am7|Dm7 G7|Cmaj7"}},
        {kMinor, {"Cm9|Fm9", "Cm9|Fm9", "Abmaj7|G7#9", "Cm9|Fm9|Bb13|Ebmaj7", "Abmaj7|Abmaj7|G7#9|G7#9", "Cm9|Fm9|Cm9|Cm9"}}},
       {{"Study Loop", "Loop", 80, kLongs | kRich, 0, 0b001},
        {"Rainy Chop", "Chop", 74, kRich, 1, 0b100},
        {"Dusty Push", "Push", 86, kPushes | kRich, 0, 0b010}},
       "15, 17"},

      // ---- Church & Blues ----
      {"Gospel", kFamilies[1], kChurchOrgan, 0.56, 0,
       // A ballad comp with pickups before 3 and 1; the uptempo comp; the 12/8 shout ballad; the shout
       // (praise break): very fast and swung, the left hand on the beats, the chords on the swung
       // upbeats with the claps, the upbeats of 2 and 4 hardest; the 12/8 ballad's quiet chords on the
       // beats and its peak, the triplets leaning on 2 and 4.
       {{"x---x-x-----x-x-", 0.4}, {"x.xX.xx.x.xX.xx.", 0.85}, {"X.xx.xX.xx.x", 0.6}, {"b.xb.Xb.xb.X", 0.9},
        {"x--x--x--x--", 0.35}, {"X.xX.xx.xX.x", 0.85},
        // The spiritual's four-bar phrase: halves, the fourth bar in quarters turning it round.
        {"x-------x-------|x-------x-------|x-------x-------|x---x---x---x---", 0.45}},
       {{"x.xX.xx.x.xXxxxx", 0.85}, {"X.xx.xX.xxxx", 0.7}},
       // I-vi-ii-V; the walk-up IV-#IVo7-I/5-VI7-ii-V; ii-V vamps; I-I7/3-IV-iv-I.
       // The shout's own vamp: I-vi-IV-I, the blues shuffle's harmony sped up (PianoGroove).
       {{kMajor, {"C|Am7|Dm7|G7", "C|Am7|Dm7|G7", "F F#dim7|C/G A7|Dm7 G7|C", "F F#dim7|C/G A7|Dm7|G7", "Dm7|G7|Dm7|G7", "C C7/E|F Fm|C|C"}},
        {kMajor, {"C|Am|F|C", "C|Am|F|C", "F|C|F|G7", "C|Am|F|C", "Am|F|Am|G", "C|Am|F|C"}}},
       {{"Sunday Ballad", "Ballad", 66, kTriplet | kRunsIn | kRich, 0, 0b110100},
        {"Shout", "Shout", 160, kTriplet | kRunsIn, 1, 0b1000},
        {"Praise Walk-Up", "Walk-Up", 84, kRunsIn | kLeftHand | kRich, 0, 0b1000011, kPopPianist}},
       "31, 32, 44, 53, 61"},
      {"Blues", kFamilies[1], kPopPianist, 0.5, 0,
       // On the triplet grid: backbeat chords, the Chicago lift, the long-short shuffle pulse (2 and
       // 4 leaning against the drummer); the slow blues' chord on the one with the backbeat, and its
       // repeated triplet chords at the peak.
       // The four-bar phrase, its fourth bar in quarters (the turnaround), and the slow blues' quarters
       // with a pickup into 4 (MMA).
       {{"...x.....x..", 0.3}, {"x..X.xx..X.x", 0.6}, {"x.xX.xx.xX.x", 0.85}, {"x..X.....X..", 0.45}, {"X.xX.xX.xX.x", 0.75},
        {"x-----x-----|x-----x-----|x-----x-----|x--x--x--x--", 0.4}, {"x--x--x--x--|x--x----xx--", 0.55}},
       {{"x.xx.xx.xxxx", 0.8}},
       // The 12-bar and its quick change; the I-I7/3-IV-#IVo7-I/5-V7 turnaround.
       {{kBlues, {"C7 C7/E|F7 F#dim7|C7/G|G7", "C7|C7|C7|C7|F7|F7|C7|C7|G7|F7|C7|G7", "C7 C7/E|F7 F#dim7|C7/G|G7", "C7|C7|C7|C7|F7|F7|C7|C7|G7|F7|C7|G7", "F7|F7|C7|C7", "C7 C7/E|F7 F#dim7|C7/G G7|C7"}},
        {kBlues, {"C7 C7/E|F7 F#dim7|C7/G|G7", "C7|F7|C7|C7|F7|F7|C7|C7|G7|F7|C7|G7", "C7 C7/E|F7 F#dim7|C7/G|G7", "C7|F7|C7|C7|F7|F7|C7|C7|G7|F7|C7|G7", "F7|F7|C7|C7", "C7 C7/E|F7 F#dim7|C7/G G7|C7"}}},
       {{"Slow Blues", "Slow", 72, kTriplet | kLongs, 0, 0b1011001},
        {"Chicago Shuffle", "Shuffle", 112, kTriplet | kLeftHand, 0, 0b100110},
        {"Quick Change", "Quick", 100, kTriplet, 1}},
       "61 (otherwise from the idiom)"},

      // ---- Jazz & Latin ----
      {"Swing Jazz", kFamilies[2], kJazzComper, 0.64, 1,
       // The Charleston (1, the and of 2) and its reverse (the and of 1, 3); Garland's offbeats, an eighth ahead of the next beat;
       // Freddie Green's four to the bar, one or two notes, even quarters (the chunk at one level: a
       // backbeat accent on 2 and 4 is the drummer's hi-hat, not his).
       // Held comps (MMA's basic swing piano: 1 held, then the and of 3 held over the bar; Impro-Visor's
       // chord held to the and of 4); Freddie Green's four, each stroke most of a beat long ("chunk"), four
       // to the bar throughout (the two-feel is the bass's, not his); MMA's
       // LongSS2 (1, 2, the and of 3, 4).
       {{"x.....x.........", 0.35}, {"..x.....x.......", 0.45}, {"......x.......x.", 0.6}, {"x--.x--.x--.x--.", 0.8},
        {"x-------..x-----|x---------------", 0.3}, {"x--.....x--.....", 0.1}, {"x-------------..", 0.25},
        {"x-------....o---", 0.4}, {"x...x.....x.x...", 0.55},
        // The jazz waltz, in three: MMA's piano (1, its and, 2, 3), Impro-Visor's half and quarter,
        // the "2 and 3" comp that leaves the one to the bass, MMA's guitar.
        {"x.x.x...x...|x.x.x...x...|x...x...x...|x.x.x...x...", 0.6, 3}, {"x-------x---", 0.35, 3},
        {"....x...x---", 0.5, 3}, {"........x...|........x...|x...........|x...x.......", 0.4, 3}},
       {{"x.....x...x...x.", 0.6}},
       // Rhythm changes' A; iii-VI7-ii-V; the III7-VI7-II7-V7 bridge.
       {{kMajor, {"Em7 A7|Dm7 G7|Em7 A7|Dm7 G7", "C6 Am7|Dm7 G7|C6 Am7|Dm7 G7", "Dm7|G7|Cmaj7|Cmaj7", "Dm7 G7|Cmaj7|Dm7 G7|Cmaj7 A7", "E7|E7|A7|A7|D7|D7|G7|G7", "Em7 A7|Dm7 G7|Cmaj7|Cmaj7"}},
        // A waltz's: a chord a bar ("Someday My Prince Will Come"'s I-III7+5-IV-VI7+5-ii-VI7+5-ii-V).
        {kMajor, {"Dm7|G7|Cmaj7|Cmaj7", "Cmaj7|E7#5|Fmaj7|A7#5|Dm7|A7#5|Dm7|G7", "Em7|A7|Dm7|G7", "Cmaj7|Am7|Dm7|G7|Em7|A7|Dm7|G7", "Fmaj7|Fm7|Em7|A7|D7|D7|Dm7|G7", "Dm7|G7|Cmaj7|Cmaj7"}}},
       {{"Charleston Comp", "Charleston", 160, kSparse, 0, 0b0011},
        {"Garland Pushes", "Garland", 140, kPushes, 0, 0b100000100},
        {"Four To The Bar", "Freddie", 180, kPlain | kShorts | kFixed, 0, 0b001000, kRhythmGuitar, kShell, 3, 40, 64, 52},
        {"Held Comp", "Held", 132, 0, 0, 0b11010000},
        {"Jazz Waltz", "Waltz", 150, kWaltz, 1, 0}},
       "2, 19, 20, 21, 54, 55, 57, 61, 62", true},
      {"Bossa Nova", kFamilies[2], kJazzComper, 0.5, 1,
       // Brazilian music writes bossa in 2/4 in sixteenths, so at a 4/4 tempo each 2/4 bar is a 4/4
       // bar in eighths. The batida: the thumb on the 2/4 beats (1 and 3 here), the fingers on both
       // eighths of the first beat and the second sixteenth of the second (1, 2 and the and of 3
       // here: Garcia, via Wikipedia); the 3-3-4-3-3 cycle in eighths over two bars; the same cycle
       // with the thumb under it.
       // Samba: MMA's four bars of piano, and Impro-Visor's offbeat eighths (in 2/2: a 2/4 samba at
       // 100 is 200 here, MMA's "double the tempo").
       {{"x...x...f.x.....", 0.6}, {"x.....x.....x...|....x.....x.....", 0.7}, {"x.....x.f...x...|b...x...f.x.....", 0.8},
        {"x.....x.x...x...|..x.....x...x...|x.....x...x...x.|..x.x.......x...", 0.75}, {"..x...x...x...x.", 0.55}},
       {{"x...x...f.x...x.", 0.6}},
       // Imaj7-II7(#11) and the "Ipanema" turn (Imaj7-II7-ii7-bII7); ii-V chains for the bridge.
       {{kMajor, {"Cmaj7|Cmaj7|C6/9|C6/9", "Cmaj7|Cmaj7|D7#11|D7#11|Dm7|Db7#11|Cmaj7|Db7#11", "Dm7|G7|Cmaj7|Cmaj7", "Cmaj7|D7#11|Dm7|Db7#11", "Em7|A7b9|Dm7|G7|Fm7|Bb7|Cmaj7|Cmaj7", "Cmaj7|Cmaj7|C6/9|C6/9"}}},
       // The guitar's voicing whoever plays: the thumb's bass under the fingers' three-note chord.
       {{"Batida", "Batida", 130, kFixed, 0, 0b001, -1, kSpread, 4, 50, 74, 62},
        {"Two-Bar Bossa", "Two Bar", 138, kFixed, 0, 0b100, -1, kSpread, 4, 50, 74, 62},
        {"Quiet Bossa", "Quiet", 120, kSparse | kRich | kFixed, 0, 0b101, -1, kSpread, 4, 50, 74, 62},
        {"Samba", "Samba", 200, kPushes, 0, 0b11000}},
       "11, 12, 60, 61, 62"},
      {"Salsa", kFamilies[2], kMontuno, 0.5, 0,
       // A clave bar is a 4/4 bar in eighths (the clave's strokes on 1, the and of 2, 4 | 2, 3). Chords
       // on the 3-2 clave's strokes; the montuno on the 2-3 onbeat/offbeat motif (the two-side 1, 2,
       // the ands of 2, 3 and 4; the three-side all offbeats) and on 3-2 (the bars swapped, the and
       // of 4 the pickup): Moore's examples via Wikipedia's Guajeo, read note by note from its MIDI;
       // chords on the 2-3 clave's strokes.
       {{"x.....x.....x...|....x...x.......", 0.2}, {"x...x.x...x...x.|..x...x...x...x.", 0.6}, {"..x...x...x...x.|x...x.x...x...x.", 0.6},
        {"....x...x.......|x.....x.....x...", 0.2}},
       {{"x...x.x...x.x.x.", 0.8}},
       // The son montuno I-IV-V-IV; ii-V-I-IV; a minor i-iv-V7. Changes anticipated on the "and" of 4.
       {{kMajor, {"C|F|G7|F", "C|F|G7|F", "Dm7|G7|C|C", "Dm7|G7|C|F", "Am|Dm|E7|E7", "C|F|G7|C"}},
        {kMinor, {"Cm|Fm|G7|G7", "Cm|Fm|G7|Fm", "Ab|G7", "Cm|Fm|G7|Cm", "Fm|Bb7|Eb|G7", "Cm|Fm|G7|Cm"}}},
       {{"Montuno 2-3", "2-3", 180, kPushes | kFixed, 0, 0b1010},
        {"Montuno 3-2", "3-2", 180, kPushes | kFixed, 0, 0b101},
        {"Minor Montuno", "Minor", 170, kPushes | kFixed, 1, 0b1010}},
       "2, 7, 8, 9, 10, 56, 58", true},
      {"Latin Jazz", kFamilies[2], kJazzComper, 0.5, 0,
       // The chachachá's quarters; the guajeos (as salsa); the quarters with the "cha-cha-chá" cell
       // that names it (4, its and, and the next 1).
       // The bolero's tresillo (1, the and of 2, 4: Impro-Visor's rhumba) and MMA's four bars; the
       // mambo's broken chord (the chord, then single inner notes).
       {{"x...x...x...x...", 0.5}, {"x...x.x...x...x.|..x...x...x...x.", 0.8}, {"..x...x...x...x.|x...x.x...x...x.", 0.8},
        {"x...x...x...x.x.", 0.65}, {"x-----x-----x...", 0.4},
        {"x.......x...x...|x...x...x.......|x.........x...x.|....x.....x...x.", 0.55}, {"x.3.5.x.x.3.5.x.", 0.6}},
       {{"x...x...x.x.x.xx", 0.6}},
       // The i-IV7 vamp ("Oye Como Va"); a minor ii-V. The mambo plays the 2-3 guajeo (in clave, as
       // MMA's mambo plays its broken chord over Clave2 Clave3), the broken chord when quiet.
       {{kDorian, {"Cm7|F9", "Cm7|F9", "Ab7|G7", "Cm7|F9|Cm7|F9", "Dm7b5|G7b9|Cm7|Cm7", "Cm7|F9|Cm7|Cm7"}},
        // The bolero's song form: a minor i-iv-V7-i with its secondary dominants ("Besame Mucho").
        {kMinor, {"Fm7|G7|Cm|Cm", "Cm|Fm7|G7|Cm|C7|Fm7|D7|G7", "Ab7|G7", "Fm7|Bb7|Ebmaj7|Abmaj7|Dm7b5|G7b9|Cm|Cm", "Ebmaj7|Abmaj7|Dm7b5|G7b9", "Fm7|G7b9|Cm|Cm"}}},
       {{"Cha-Cha", "Cha-Cha", 118, kFixed, 0, 0b1001},
        {"Latin Jazz Guajeo", "Guajeo", 190, kPushes | kRich | kFixed, 0, 0b010, kMontuno},
        {"Bolero", "Bolero", 80, kRich, 1, 0b110000},
        {"Mambo", "Mambo", 180, kPushes | kFixed, 0, 0b1000010}},
       "2, 7, 10, 56, 61, 62", true},

      // ---- Pop & Rock ----
      {"Pop", kFamilies[3], kPopPianist, 0.5, 0,
       // The driving eighths lean on the backbeat, the ands softer.
       // MMA's 8-beat four-bar phrase: a different push each bar, the fourth leaving the one out.
       {{"x---------------", 0.2}, {"x-----x-----x---", 0.45}, {"x-o-X-o-x-o-X-o-", 0.65}, {"X--x--x-X--x--x-", 0.8},
        {"x.....x...x...x.|x.........x.....|x.........x...x.|..........x...x.", 0.6}},
       {{"x-x-x-x-x-x-x.xx", 0.7}},
       // Weighted by de Clercq & Temperley's rock corpus: I, IV, V and bVII; IV-I and I-IV the
       // commonest moves; IV-V-I, V-IV-I and bVII-IV-I the commonest endings.
       {{kMajor, {"C|G|Am|F", "C|G|Am|F", "F|G|F|G", "F|C|G|Am", "Ab|Bb|C|C", "F|G|C|C"}},
        {kMajor, {"F|F|C|C", "Am|F|C|G", "Dm|F|G|G", "C|G|Am|F", "Dm|Em|F|G", "Bb|F|C|C"}}},
       {{"Driving Eighths", "Eighths", 118, 0, 0, 0b0100},
        {"Dotted Pulse", "Dotted", 104, 0, 1, 0b11010},
        {"Pad Intro", "Pad", 96, kLongs, 0, 0b0001, kPadPlayer}},
       "1, 61"},
      {"Rock", kFamilies[3], kStrummer, 0.5, 0,
       // MMA's four-bar half-note phrase, its last bar in quarters; the 50s piano's pounding triplets and
       // its dotted pickups (MMA), played over the 50s changes.
       {{"X---------------", 0.25}, {"X---X---X---X---", 0.6}, {"x-o-X-o-x-o-X-o-", 0.75}, {"X..x..x.X..x..x.", 0.85},
        {"x---....x---....|x---....x---....|x---....x---....|x---x---x---x---", 0.5},
        {"xxxXxxxxxXxx|xxxXxxxxxXxx|xxxXxxxxxXxx|x..x..x..x..", 0.8}, {"x..xx...x..xx...|x..x.......xx...", 0.7}},
       {{"x-x-x-x-x.x.xxxx", 0.8}},
       // I-bVII-IV-I (Mixolydian), bVI-bVII-I, the minor i-bVI-bIII-bVII.
       {{kMixolydian, {"C|Bb|F|C", "C|Bb|F|C", "F|G|F|G", "Ab|Bb|C|C", "Ab|Bb|F|F", "Bb|F|C|C"}},
        {kMinor, {"Cm|Ab|Eb|Bb", "Cm|Ab|Eb|Bb", "Ab|Bb", "Cm|Ab|Eb|Bb", "Fm|Ab|Bb|Bb", "Ab|Bb|Cm|Cm"}},
        // The 50s: I-vi-IV-V (the doo-wop changes), I-vi-ii-V, the IV-I and II7-V7 of a rock'n'roll bridge.
        {kMajor, {"C|Am|F|G", "C|Am|F|G7", "F|F|C|C", "C|Am|Dm7|G7", "F|F|C|C|D7|D7|G7|G7", "C|Am|F G7|C"}}},
       // The rock guitar's power chord (root, fifth, octave): Walser.
       {{"Eighth Pump", "Pump", 128, kPlain, 0, 0b0100, kPopPianist},
        {"Quarter Pound", "Pound", 140, kPlain, 0, 0b10011, -1, kGrip, 3, 40, 64, 50},
        {"Minor Rock", "Minor", 116, kPlain, 1, 0b1101, -1, kGrip, 3, 40, 64, 50},
        {"Rock'n'Roll Triplets", "Triplets", 140, kTriplet | kLeftHand | kPlain, 2, 0, kPopPianist},
        {"Fifties Pump", "Fifties", 150, kLeftHand | kPlain, 2, 0b1000000, kPopPianist}},
       "1, 61"},
      {"Piano Ballad", kFamilies[3], kPopPianist, 0.5, 0,
       // Half notes; broken eighths (the left hand's root, fifth, root, fifth under the right hand's
       // chord, struck with the root on 1 and 3 and held); 12/8: every triplet, leaning on 2 and 4
       // (the doo-wop and soul ballad), and quiet on the beats.
       // MMA's: the quarter-note block pulse, the ballad figure (1, the and of 2, 3 held), the right
       // hand's arpeggio up the chord and back, the four-bar phrase whose third bar drops its one; in
       // 6/8 (the same twelve sixteenths, in two dotted quarters): the dotted quarters, the six eighths leaning on 1 and 4,
       // the left hand rocking under them.
       {{"x-------x-------", 0.35}, {"x.f.b.f.x.f.b.f.", 0.65}, {"xxxXxxxxxXxx", 0.6}, {"x--x--x--x--", 0.35},
        {"x...x...x...x...", 0.5}, {"x.....x.x-------|x...x...x-------", 0.55}, {"b...3.5.8...5.3.|b.....5.8...5.3.", 0.45},
        {"x---x---x---x---|x---x---x---x---|....x---x---x---|x---x---x---x---", 0.5},
        {"x-----x-----", 0.35, 6}, {"X.x.x.X.x.x.", 0.6, 6}, {"b.x.x.f.x.x.", 0.5, 6}},
       {{"x-------x.x.x.xx", 0.5}, {"xxxXxxxxxXXX", 0.6}},
       // I-V/7-vi-IV over a falling bass; I-iii-IV-V; IV-V-iii-vi-ii-V-I; the last chorus again, ending home.
       {{kMajor, {"C|G/B|Am|F", "C|G/B|Am|F", "C|Em|F|G", "F|G|Em|Am|Dm|G|C|C", "Ab|Bb|C|C", "F|G|Em|Am|Dm|G|C|C"}}},
       {{"Half Notes", "Halves", 72, kLongs, 0, 0b001, kPopPianist},
        {"Broken Chords", "Arpeggio", 68, kLongs, 0, 0b010},
        {"12/8 Ballad", "12/8", 64, kTriplet},
        {"Ballad Block", "Block", 76, 0, 0, 0b10110000},
        {"Right-Hand Arpeggio", "RH Arp", 70, 0, 0, 0b1000000},
        {"6/8 Ballad", "6/8", 84, kWaltz | kSixEight, 0, 0}},
       "1, 61, 62"},
      {"Country", kFamilies[3], kPopPianist, 0.53, 1,
       // Boom-chick (the bass on 1 and 3, the root then the fifth, the chord on 2 and 4); Cramer's
       // Nashville sound, its slip notes into the chords; the two-step's two-beat: the bass string on
       // 1 and 3 (root, then fifth), a down-up strum on 2.
       // Western swing's four on the guitar, the fourth bar with its pickups (MMA); the country waltz's
       // oom-pah-pah, the bass on 1 (root, then fifth) and the chord on 2 and 3, and its busier strum.
       {{"b...x...f...x...", 0.45}, {"x---x-x-x---x-x-", 0.6}, {"b...x-x.f...x-x.", 0.8},
        {"x...X...x...X...|x...X...x...X...|x...X...x...X...|x.g.X...x.g.X...", 0.6},
        {"b...x...x...|f...x...x...", 0.5, 3}, {"b...x.x.x...|f...x.x.x...", 0.75, 3}},
       {{"b...x...f.x.xxxx", 0.6}},
       // I-IV-I-V; I-II7-V7-I; I-I7-IV-iv-I. A waltz's: a chord a bar.
       {{kMajor, {"C|F|C|G7", "C|F|C|G7", "F|F|C|C", "C|D7|G7|C", "F|C|D7|G7", "C C7|F Fm|C|C"}},
        {kMajor, {"C|G7|C|C", "C|C|F|C|G7|G7|C|C", "F|F|C|C", "F|C|G7|C", "F|F|C|C|D7|D7|G7|G7", "C|G7|C|C"}}},
       {{"Boom-Chick", "Boom-Chick", 112, kShorts, 0, 0b001},
        {"Nashville Slip", "Slip", 84, kSlip, 0, 0b010},
        {"Two-Step", "Two-Step", 132, 0, 0, 0b100, kStrummer},
        {"Western Swing", "Western", 168, 0, 0, 0b1000, kRhythmGuitar, kShell, 3, 45, 67, 55, 0.6, 1},
        {"Country Waltz", "Waltz", 104, kWaltz, 1, 0}},
       "18, 33, 59, 61"},

      // ---- Dance ----
      {"House", kFamilies[4], kHouseStabs, 0.54, 0,
       // Offbeat stabs; the 3-3-2 M1 piano riff; the two-bar riff anticipating its second bar.
       {{"X...............", 0.2}, {"..X...X...X...X.", 0.55}, {"x..x..x...x..x..", 0.75}, {"x..x..x.x..x..x.|x..x..x...x..x..", 0.9}},
       {{"x..x..x...x.xxxx", 0.8}},
       // i-bVII-bVI-bVII; the major lift I-iii-IV, IV-V-vi; i-iv in the breakdown.
       {{kMinor, {"Cm|Fm", "Cm|Bb|Ab|Bb", "Ab|Bb|Ab|Bb", "Cm|Bb|Ab|Bb", "Cm|Fm|Cm|Fm", "Cm|Fm|Cm|Cm"}},
        {kMajor, {"C|Em|F|F", "C|Em|F|F", "F|G|Am|Am", "F|G|Am|Am", "Am|Dm|Am|Dm", "C|Em|F|C"}}},
       {{"Piano Riff", "M1 Piano", 124, kPushes, 0, 0b1100},
        {"Offbeat Stabs", "Offbeat", 126, kShorts, 0, 0b0010},
        {"Major Lift", "Lift", 122, kPushes, 1, 0b0100}},
       "36, 37"},
      {"Deep House", kFamilies[4], kHouseStabs, 0.55, 0,
       // Heard's "Can You Feel It": long chords held, "an organ played like strings"; the offbeat
       // stabs with a sixteenth push; the stab shifted a sixteenth.
       {{"x---------------", 0.35}, {"..x...x...x..x..", 0.6}, {"...x..x...x..x..", 0.75}},
       {{"..x...x...x.xxx.", 0.6}},
       // i9-iv9; i(add9)-bVImaj7-bVII(add9) (the "Can You Feel It" changes: Hooktheory, Mixed In Key); m9 planed a step.
       {{kMinor, {"Cm9|Fm9", "Cm9|Fm9|Cm9|Fm9", "Abmaj7|Bbadd9", "Cmadd9|Abmaj7|Bbadd9|Bbadd9", "Dm9|Dm9|Cm9|Cm9", "Cm9|Fm9|Cm9|Cm9"}}},
       {{"Held Like Strings", "Strings", 118, kLongs | kRich, 0, 0b001, kPadPlayer},
        {"Chicago Stabs", "Stabs", 122, kRich | kShorts, 0, 0b010},
        {"Jersey Push", "Push", 124, kRich | kPushes, 0, 0b100}},
       "24, 25"},

      // ---- Caribbean & Africa ----
      {"Reggae", kFamilies[5], kSkankGuitar, 0.54, 1,
       // The skank on 2 and 4 (or every offbeat, counted double); the organ bubble (the left hand's
       // low single notes on the upbeats, the right hand's chop on 2 and 4: Jenson, Berklee); the
       // double chop: the skank on 2 and 4 and a strum on its and (Guitar World's Marley lesson, which
       // also keeps a light swing).
       {{"....x.......x...", 0.4}, {"..x...x...x...x.", 0.6}, {"..b.x.b...b.x.b.", 0.75}, {"....x.x.....x.x.", 0.85}},
       {{"....x..x..x.x.xx", 0.6}},
       {{kMajor, {"C|F", "C|F|C|F", "Am|F|G|G", "C|G|Am|F", "Am|G|Am|G", "C|F|C|C"}},
        {kMinor, {"Cm|Fm", "Cm|Fm|Cm|Fm", "Ab|Bb", "Cm|Bb|Cm|Bb", "Fm|Bb|Cm|Cm", "Cm|Fm|Cm|Cm"}}},
       {{"One Drop Skank", "One Drop", 74, kShorts, 0, 0b0001},
        {"Organ Bubble", "Bubble", 72, kShorts | kPlain, 0, 0b0100, kChurchOrgan, kClose, 4, 55, 76, 64},
        {"Rockers", "Rockers", 80, kShorts, 1, 0b1001}},
       "35, 42, 51"},
      {"Ska", kFamilies[5], kSkankGuitar, 0.5, 0,
       // The offbeat chop that named it (Britannica); the offbeat doubled a sixteenth later; early ska's
       // shuffle.
       {{"..x...x...x...x.", 0.6}, {"..xx..xx..xx..xx", 0.85}, {"..x..x..x..x", 0.6}},
       {{"..x...x...x.x.xx", 0.7}, {"..x..x..xxxx", 0.7}},
       {{kMajor, {"C|F|G|G", "C|Am|F|G", "F|G", "C|F|G|C", "Dm7|G7|Cmaj7|Cmaj7", "C|F|G|C"}}},
       {{"Upstroke", "Upstroke", 132, kShorts, 0, 0b011},
        {"Shuffle Ska", "Shuffle", 120, kShorts | kTriplet},
        {"Ska Jazz", "Jazz", 144, kShorts | kRich, 0, 0b001, kJazzComper}},
       "22"},
      {"Afrobeat", kFamilies[5], kRhythmGuitar, 0.5, 0,
       // Fela's "Alagbon Close" (Stewart, Example 6): the tenor guitar's single-note line and the
       // rhythm guitar's chords interlocking; chords on the bass's strokes. The parts play
       // "without variation" (Veal), so the fill is the groove. The tenor's line moves in the Dorian
       // mode round the root, the b3, the 4th and the 5th (our reading of the example's shape).
       {{"x.....x..x.x..x.", 0.35}, {"1.3.45..54.3.41.", 0.65}, {".x.xx.x.x.x.x.x.", 0.8}},
       {{".x.xx.x.x.x.x.x.", 0.8}},
       // A static minor-seventh (Dorian) vamp; "tonal shifts were rare".
       {{kDorian, {"Cm7|Cm7", "Cm9|Cm9|Cm9|Cm9", "Cm9|F7", "Cm9|F9|Cm9|F9", "Cm11|Cm11", "Cm7|Cm7"}}},
       {{"Tenor Line", "Tenor", 112, 0, 0, 0b010},
        {"Chord Chop", "Chop", 118, kShorts, 0, 0b100},
        {"Interlock", "Interlock", 104, kInterlock, 0, 0b110}},
       "13, 14"},
      {"Highlife", kFamilies[5], kStrummer, 0.52, 0,
       // Palm-wine two-finger interlock (the thumb on eighths, a finger on sixteenths between: the
       // Koo Nimo article: the thumb alternating root and fifth, the finger a figure on the top
       // strings); the dance-band 3-3-2; the guitar on the 12/8 bell.
       {{"b.x.f3b5f.x.b3f5", 0.75}, {"x..x..x.x..x..x.", 0.6}, {"x.x.xx.x.x.x", 0.7}},
       {{"b.x.f3b5f.x.xxxx", 0.7}, {"x.x.xx.xxxxx", 0.7}},
       // Dagomba patterns "emphasize two chords, the tonic major and the dominant seventh".
       {{kMajor, {"C|G7", "C|G7|C|G7", "F|G7", "C|F|C|G7", "F|C|G7|C", "C|G7|C|C"}}},
       {{"Palm-Wine", "Palm-Wine", 108, 0, 0, 0b001},
        {"Dance Band", "Dance Band", 120, 0, 0, 0b010},
        {"Bell Twelve", "12/8", 112, kTriplet | kFixed}},
       "7, 26, 27, 28"},

      // ---- Ambient ----
      {"Ambient", kFamilies[6], kAmbientDrifter, 0.5, 0,
       // Long chords: a bar held, two bars held, a slow half-note swell, a chord re-struck late in
       // the bar like a loop coming round again (Eno's tape loops of unequal length drift in and
       // out of phase: Music for Airports).
       {{"x---------------|----------------", 0.2}, {"x---------------", 0.4}, {"o-------x-------", 0.55},
        {"x---------o-----", 0.65}},
       {{"x-------o---o---", 0.5}},
       // Ambiguous, slow harmony: sus and add9 chords that leave the third out or blur it, a key
       // that sits between a minor and its relative major ("1/2": an add9 with no third, at home in
       // either Ab major or F minor), and borrowed major sevenths.
       // Each chord lasts two bars, so a two-bar hold sounds it whole.
       {{kMajor, {"Csus|Csus|Cadd9|Cadd9", "Cadd9|Cadd9|Fmaj7|Fmaj7|Am9|Am9|Fmaj7|Fmaj7", "Dm9|Dm9|Gsus|Gsus",
                  "Fmaj7|Fmaj7|Cadd9|Cadd9|Am9|Am9|G7sus|G7sus", "Abmaj7|Abmaj7|Fmaj7|Fmaj7", "Fmaj7|Fmaj7|Cadd9|Cadd9"}},
        {kMinor, {"Cm9|Cm9|Cm9|Cm9", "Cm9|Cm9|Abmaj7|Abmaj7|Ebmaj7|Ebmaj7|Bbsus|Bbsus", "Abmaj7|Abmaj7|Bbsus|Bbsus",
                  "Abmaj7|Abmaj7|Ebmaj7|Ebmaj7|Cm9|Cm9|Bbsus|Bbsus", "Fm9|Fm9|Abmaj7|Abmaj7", "Abmaj7|Abmaj7|Cm9|Cm9"}}},
       {{"Long Tones", "Tones", 70, kLongs | kRich, 0, 0b0011},
        {"Mirror Drift", "Drift", 64, kLongs | kRich, 1, 0b0001},
        {"Slow Swells", "Swells", 76, kLongs, 1, 0b0110}},
       "47, 48, 49, 50"},

      // ---- added later: kept after the first 24 so a saved genre number keeps its genre ----
      {"Calypso", kFamilies[5], kStrummer, 0.5, 0,
       // MMA's calypso: 1, the and of 2 and 4 (its "C12'3"), the same every bar; a busier strum, and
       // the bar's one and four alone, quiet.
       {{"x.....x.....x...", 0.5}, {"x.....x.x...x.x.", 0.75}, {"x...........x...", 0.3}},
       {{"x.....x...x.x.xx", 0.7}},
       // I-V7, I-IV-V7-I.
       {{kMajor, {"C|G7|C|C", "C|F|G7|C", "F|C|G7|C", "C|F|G7|C|C|F|G7|C", "F|F|C|C|D7|D7|G7|G7", "C|G7|C|C"}}},
       {{"Calypso Strum", "Strum", 110, 0, 0, 0b011},
        {"Calypso Piano", "Piano", 112, kLeftHand, 0, 0b101, kPopPianist}},
       "61"},
   };
   return g;
}
inline int genreCount() { return static_cast<int>(genres().size()); }
inline const Genre &genreAt(int i) {
   return genres()[static_cast<size_t>(std::clamp(i, 0, genreCount() - 1))];
}
inline const Style &styleAt(const Genre &g, int s) {
   return g.styles[static_cast<size_t>(std::clamp(s, 0, static_cast<int>(g.styles.size()) - 1))];
}

} // namespace jcomp
