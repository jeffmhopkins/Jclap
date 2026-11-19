#pragma once

// The style library: 34 genres in eight families, each with its own grooves and the named styles
// built on them. A groove is a few lanes written as text, one character a step:
//
//    X accent   x hit   o soft   g ghost   . rest
//    2 3 4      a hit rolled into that many (a ratchet)
//    ?          a hit left to chance (half the time)
//
// A style is a groove, a tempo and a handful of twists (MODS) the generator applies to it, so a few
// hand-written grooves give a genre many characters. The generator (generator.h) then shapes every
// style into a song: which lanes come in at which energy, the four variations, the fills.

#include <vector>

#include "kit.h"
#include "shared.h"

namespace jdrummer {

// Twists a style applies to its groove.
enum Mod : unsigned {
   kShuffle = 1 << 0,     // more swing
   kBusyHats = 1 << 1,    // hats in sixteenths
   kSparse = 1 << 2,      // fewer hits
   kOpenHats = 1 << 3,    // open hats on the off-beats
   kRollingPerc = 1 << 4, // a rolling perc line, some left to chance
   kHalfTime = 1 << 5,    // the backbeat on 3 only
   kDoubleKick = 1 << 6,  // kick pickups
   kGhosty = 1 << 7,      // ghost notes round the snare
   kToms = 1 << 8,        // a tom figure in the groove
   kRimShot = 1 << 9,     // the backbeat on the rim
   kClapStack = 1 << 10,  // a clap on the snare
   kShakerLine = 1 << 11, // a shaker in sixteenths
   kBroken = 1 << 12,     // kicks pulled off the grid
   kRatchetHats = 1 << 13, // rolls on the hats
   kSwing8 = 1 << 14       // swung in eighths, not sixteenths (a shuffle, a triplet feel)
};

// The kind of fill a genre plays at a phrase's end.
//    TOMS round the kit, SNARE a roll, HATS a hat roll, DROP a build or the band falling out, HANDS
//    congas (toms on a kit), FX sweeps; DJEMBE the djembe's call, DARBUKA a roll that lands on a doum,
//    TIHAI a phrase played three times that lands on the next bar's one, CAJON a cajon break.
enum FillKind { kFillToms, kFillSnare, kFillHats, kFillDrop, kFillHands, kFillFx, kFillDjembe, kFillDarbuka,
                kFillTihai, kFillCajon };

struct Lane {
   int pad;
   const char *hits;
};

struct Groove {
   std::vector<Lane> lanes;
   uint8_t rate = kRate16; // the step: 12/8 grooves are sixteenth triplets, two to an eighth
};

struct Style {
   const char *name;
   const char *tag;
   int bpm;
   int groove;
   unsigned mods;
};

struct Genre {
   const char *name;
   const char *family;
   Kit kit;
   double swing;   // 0.5 straight .. 0.75
   FillKind fill;
   int player;     // the drummer it suits (players.h)
   int percussionist; // a percussionist it suits, 0 for none
   std::vector<Groove> grooves;
   std::vector<Style> styles;
   // The tradition a hand-percussion genre belongs to (kit.h): stray hits and mutations stay in it.
   unsigned tradition = kTradAny;
};

inline const char *const kFamilies[] = {"Four on the Floor", "Breaks", "Hip-Hop", "Live Kit",
                                        "Latin & Afro", "Reggae & Dub", "Hand Percussion", "FX & Odd"};
constexpr int kFamilyCount = 8;

// Drummers (players.h): 0 Session Pro, 1 Pocket Funk, 2 Stadium Rock, 3 Brushes Jazz, 4 Break
// Chopper, 5 Machine. Percussionists: 1 Conguero, 2 Djembe Circle, 3 Bateria, 4 Hand Drummer,
// 5 Tabla & Darbuka, 6 Shaker & Tamb.
inline const std::vector<Genre> &genres() {
   static const std::vector<Genre> g = {
      // ---- Four on the Floor ----
      {"House", kFamilies[0], kDrumKit, 0.5, kFillDrop, 5, 0,
       {{{{K, "X...X...X...X..."}, {CL, "....X.......X..."}, {OH, "..x...x...x...x."}, {CH, "o.o.o.o.o.o.o.o."},
          {SH, "gogogogogogogogo"}}},
        {{{K, "X...X...X...X..."}, {CL, "....x.......x..."}, {CH, "..o...o...o.o.o."}, {SH, "gxgogxgogxgogxgo"},
          {RIM, "......x.......x."}}},
        {{{K, "X...X...X...X..x"}, {CL, "....X.......X..."}, {CH, "xoxoxoxoxoxoxoxo"}, {OH, "..x...x...x...x."},
          {RIM, "x..x..x...x..x.."}}},
        {{{K, "X...X...X...X..."}, {SN, "....x.......x..g"}, {CH, "x.xox.xox.xox.xo"}, {OH, "..x.......x....."},
          {RIM, "...x..x....x..x."}}},
        // 4 piano anthem: snare and clap together, open hats lifting every off-beat, a ride on top
        {{{K, "X...X...X...X..."}, {SN, "....X.......X..."}, {CL, "....X.......X..."}, {OH, "..X...X...X...X."},
          {CH, "x.o.x.o.x.o.x.o."}, {RD, "x.x.x.x.x.x.x.x."}}},
        // 5 rolling shaker: the shaker carries the groove, accented, over a soft rim
        {{{K, "X...X...X...X..."}, {SH, "oxXxoxXxoxXxoxXx"}, {CL, "....x.......x..."}, {RIM, "..o..o....o..o.."},
          {OH, "..........x....."}}}},
       {{"Classic Four", "Chicago", 124, 0, 0},
        {"Deep Shuffle", "Deep", 120, 1, kShuffle},
        {"Warehouse Jack", "Jackin", 126, 2, 0},
        {"Garage Swing", "NY garage", 122, 3, kShuffle},
        {"Afro Tribal", "Afro house", 122, 1, kRollingPerc | kToms},
        {"Piano Anthem", "Italo", 125, 4, 0},
        {"Filter Disco", "French", 124, 0, kBusyHats},
        {"Rolling Shaker", "Deep", 118, 5, 0},
        {"Tech Stab", "Tech house", 126, 2, kSparse},
        {"Lo-fi Kitchen", "Lo-fi house", 115, 1, kShuffle | kGhosty},
        {"Acid Pulse", "Acid", 127, 0, kRatchetHats},
        {"Sunday Soul", "Soulful", 121, 3, kShakerLine | kGhosty}}},
      {"Techno", kFamilies[0], kDrumKit, 0.5, kFillHats, 5, 0,
       {{{{K, "X...X...X...X..."}, {CH, "..x...x...x...x."}, {RIM, "...x.....x....x."}, {CL, "....o.......o..."},
          {SH, ".o.o.o.o.o.o.o.o"}}},
        {{{K, "X...X...X...X..."}, {K2, "..o...o...o...o."}, {CH, "o.x.o.x.o.x.o.x."}, {RIM, "......x..x......"}}},
        {{{K, "X...X...X...X..."}, {RIM, "x..x..x..x..x.x."}, {OH, "..x...x...x...x."}, {CL, "............x..."}}},
        {{{K, "X.....x...X....."}, {SN, "....x.......x..."}, {CH, "x.xxx.xxx.xxx.xx"}, {RIM, "..x....x..x....x"}}}},
       {{"Berlin Driver", "Berlin", 132, 0, 0},
        {"Detroit Pulse", "Detroit", 128, 0, kShakerLine | kClapStack},
        {"Rumble Room", "Hard", 135, 1, 0},
        {"Hypnotic Loop", "Hypnotic", 130, 2, 0},
        {"Minimal Tool", "Minimal", 128, 2, kSparse},
        {"Industrial", "Industrial", 138, 1, kRatchetHats | kDoubleKick},
        {"Broken Techno", "Broken", 130, 3, 0},
        {"Dub Techno", "Dub", 122, 0, kSparse | kRimShot},
        {"Acid Techno", "Acid", 138, 0, kBusyHats},
        {"Peak Time", "Peak", 134, 1, kOpenHats}}},
      {"Disco", kFamilies[0], kDrumKit, 0.5, kFillToms, 0, 6,
       {{{{K, "X...X...X...X..."}, {SN, "....X.......X..."}, {OH, "..X...X...X...X."}, {CH, "xo.oxo.oxo.oxo.o"},
          {SH, "gxgxgxgxgxgxgxgx"}}},
        {{{K, "X...X...X...X..."}, {SN, "....X.......X..g"}, {CH, "x.x.x.x.x.x.x.x."}, {OH, "......x.......x."},
          {MT, "..........x....."}}},
        {{{K, "X...X...X...X..."}, {CL, "....X.......X..."}, {OH, "..x...x...x...x."}, {RIM, "x..x..x.x..x..x."},
          {SH, "gogogogogogogogo"}}}},
       {{"Studio Floor", "Classic", 118, 0, 0},
        {"Philly Strings", "Philly", 112, 1, kGhosty},
        {"Nu Disco", "Nu disco", 120, 2, 0},
        {"Italo Night", "Italo", 122, 2, kBusyHats},
        {"Funky Disco", "Funk", 114, 1, kGhosty | kShakerLine},
        {"Boogie", "Boogie", 110, 1, kShuffle | kSwing8},
        {"Hi-NRG", "Hi-NRG", 128, 0, kBusyHats | kDoubleKick},
        {"Space Disco", "Cosmic", 116, 2, kToms}}},
      {"Trance", kFamilies[0], kDrumKit, 0.5, kFillSnare, 5, 0,
       {{{{K, "X...X...X...X..."}, {CL, "....X.......X..."}, {OH, "..x...x...x...x."}, {CH, "oo.ooo.ooo.ooo.o"}}},
        {{{K, "X...X...X...X..."}, {CH, "..x...x...x...x."}, {RD, "x.x.x.x.x.x.x.x."}, {CL, "....x.......x..."}}},
        {{{K, "X...X...X...X..."}, {SN, "....X.......X..."}, {OH, "..x...x...x...x."}, {SH, "gogogogogogogogo"},
          {CL, "....x.......x..."}}}},
       {{"Uplifting", "Uplifting", 138, 2, 0},
        {"Progressive", "Prog", 132, 0, kSparse},
        {"Psytrance", "Psy", 145, 1, 0},
        {"Goa", "Goa", 142, 1, kRatchetHats},
        {"Hard Trance", "Hard", 145, 0, kDoubleKick},
        {"Balearic", "Balearic", 126, 2, kShakerLine | kSparse},
        {"Tech Trance", "Tech", 136, 1, kBusyHats}}},
      {"Minimal", kFamilies[0], kDrumKit, 0.5, kFillHats, 5, 0,
       {{{{K, "X...X...X...X..."}, {RIM, "...x..x....x..x."}, {CH, "..o...o...o...o."}, {CL, "......o.....o..."}}},
        {{{K, "X...X...X...X..."}, {CL, "............x..."}, {SH, "g.g.gog.g.g.gog."}, {RIM, "..x.....x..x...."}}},
        {{{K, "X..xX...X..xX..."}, {RIM, "x...x.x...x.x..x"}, {CH, "..o.o.o...o.o.o."}}},
        // 3 swing tool: a shuffled hat and a clap that drags, the off-beat hats swung in eighths
        {{{K, "X...X...X...X..."}, {CH, "..x...x...x...x."}, {PH, "x.o.x.o.x.o.x.o."}, {CL, "....x.......x..g"},
          {RIM, ".......x.......x"}}}},
       {{"Click Tool", "Minimal", 125, 0, 0},
        {"Microhouse", "Micro", 124, 2, kGhosty},
        {"Romanian", "Rominimal", 126, 2, kShuffle | kRollingPerc},
        {"Glitchy Minimal", "Glitch", 122, 1, kRatchetHats},
        {"Deep Minimal", "Deep", 120, 1, kSparse},
        {"Swing Tool", "Swing", 124, 3, kShuffle | kSwing8}}},

      // ---- Breaks ----
      {"Breakbeat", kFamilies[1], kDrumKit, 0.5, kFillSnare, 4, 0,
       {{{{K, "X.x.......X....."}, {SN, "....X..g.g..X..g"}, {CH, "x.x.x.x.x.x.x.x."}, {OH, "..............x."}}},
        {{{K, "X.........X..x.."}, {SN, "....X.......X..."}, {CH, "xoxoxoxoxoxoxoxo"}}},
        {{{K, "X..x..x...X....."}, {SN, "....X.......X..."}, {CL, "....X.......X..."}, {OH, "..x...x...x...x."},
          {RIM, "x..x..x..x..x..."}}}},
       {{"Nu Skool", "Nu skool", 132, 1, 0},
        {"Big Beat", "Big beat", 125, 2, 0},
        {"Funky Breaks", "Funky", 120, 0, kGhosty},
        {"Florida Breaks", "Florida", 130, 1, kDoubleKick},
        {"Breakbeat Hardcore", "Hardcore", 140, 0, kRatchetHats},
        {"Chopped", "Chopped", 128, 0, kBroken},
        {"Electro Breaks", "Electro", 126, 2, kSparse},
        {"Rolling Breaks", "Rolling", 134, 1, kShakerLine}}},
      {"Drum & Bass", kFamilies[1], kDrumKit, 0.5, kFillSnare, 4, 0,
       {{{{K, "X.........X....."}, {SN, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.x."}, {SH, "gogogogogogogogo"}}},
        {{{K, "X.........X..x.."}, {SN, "....X..g....X..g"}, {CH, "xoxoxoxoxoxoxoxo"}}},
        {{{K, "X......x..X....."}, {SN, "....X.......X..."}, {OH, "..x.......x....."}, {CH, "x.x.x.x.x.x.x.x."},
          {RIM, "......x.......x."}}},
        {{{K, "X.........X.x..."}, {SN, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.x."}, {FX, "...........x...."}}}},
       {{"Two Step", "Two step", 174, 0, 0},
        {"Liquid", "Liquid", 172, 2, kGhosty},
        {"Rollers", "Roller", 174, 1, 0},
        {"Neurofunk", "Neuro", 174, 3, kRatchetHats},
        {"Jump Up", "Jump up", 175, 0, kDoubleKick},
        {"Halftime DnB", "Halftime", 170, 0, kHalfTime},
        {"Atmospheric", "Atmos", 170, 2, kSparse | kShakerLine},
        {"Techstep", "Techstep", 172, 3, kRimShot}}},
      {"Jungle", kFamilies[1], kDrumKit, 0.5, kFillSnare, 4, 0,
       {{{{K, "X.x.......X.......x.......X....."},
          {SN, "....X..g.g..X..g....X..g.g..X..."},
          {CH, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x."},
          {OH, "..............x...............x."}}},
        {{{K, "X.........X....."}, {SN, "....X..X.X..X.X."}, {CH, "xoxoxoxoxoxoxoxo"}, {OH, "..x............."}}},
        {{{K, "X.....x...X....."}, {SN, "....X.......X..g"}, {RIM, "..x..x..x..x..x."}, {CH, "x.x.x.x.x.x.x.x."}}}},
       {{"Chopped Break", "Classic", 165, 0, 0},
        {"Ragga Jungle", "Ragga", 160, 2, 0},
        {"Darkside", "Dark", 168, 1, 0},
        {"Intelligent", "Atmos", 162, 0, kShakerLine | kSparse},
        {"Jungle Roll", "Roll", 170, 1, kRatchetHats},
        {"Old Skool", "Old skool", 158, 2, kGhosty}}},
      {"UK Garage", kFamilies[1], kDrumKit, 0.66, kFillSnare, 1, 0,
       {{{{K, "X.........X....."}, {SN, "....X.......X..."}, {CH, "..x...x...x...x."}, {SH, "gogogogogogogogo"},
          {RIM, "...x.....x......"}}},
        {{{K, "X...X...X...X..."}, {SN, "....X.......X..."}, {CH, "x.xox.xox.xox.xo"}, {OH, "..x.......x....."}}},
        {{{K, "X.....x..x......"}, {CL, "....X.......X..."}, {CH, "xoxoxoxoxoxoxoxo"}, {RIM, "......x.......x."}}}},
       {{"Two Step", "2-step", 132, 0, 0},
        {"Speed Garage", "Speed", 135, 1, kOpenHats},
        {"Four Four Garage", "4x4", 130, 1, 0},
        {"Bassline", "Bassline", 138, 1, kDoubleKick},
        {"Future Garage", "Future", 130, 0, kSparse | kGhosty},
        {"Grime", "Grime", 140, 2, kHalfTime | kSparse},
        {"Shuffled", "Shuffle", 132, 0, kShuffle | kRollingPerc}}},
      {"Electro", kFamilies[1], kDrumKit, 0.5, kFillToms, 5, 0,
       {{{{K, "X......x..X....."}, {CL, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.x."}, {RIM, "......x.......x."}}},
        {{{K, "X.....x...x....."}, {CL, "....X.......X..."}, {CH, "xoxoxoxoxoxoxoxo"}, {HT, "...........x...."}}}},
       {{"Detroit Electro", "Detroit", 128, 0, 0},
        {"Miami Bass", "Miami", 130, 0, kDoubleKick},
        {"Electro Funk", "Funk", 112, 1, kGhosty | kShakerLine},
        {"Robot", "Robo", 124, 1, kRatchetHats}}},

      // ---- Hip-Hop ----
      {"Boom Bap", kFamilies[2], kDrumKit, 0.58, kFillSnare, 1, 0,
       {{{{K, "X.........X.x..."}, {SN, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X......x..X....."}, {SN, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.xx"}, {OH, "..............x."}}},
        {{{K, "X.x.......X..x.."}, {SN, "....X..g....X..."}, {CH, "xox.xox.xox.xox."}}}},
       {{"Golden Era", "90s", 92, 0, 0},
        {"Dusty", "Dusty", 88, 1, kGhosty},
        {"East Coast", "East", 94, 2, 0},
        {"Jazzy", "Jazz hop", 86, 0, kShuffle | kSwing8 | kRimShot},
        {"Hard Knock", "Hard", 95, 1, kClapStack},
        {"Head Nod", "Nod", 90, 2, kShakerLine},
        {"Underground", "Under", 84, 0, kSparse},
        {"Backpack", "Backpack", 92, 2, kToms}}},
      {"Trap", kFamilies[2], kDrumKit, 0.5, kFillHats, 5, 0,
       {{{{K, "X......x..X....."}, {CL, "........X......."}, {CH, "x.x.x.x.x.x3x.x."}, {K2, "X......x........"}}},
        {{{K, "X.....x...x..x.."}, {CL, "........X......."}, {CH, "x.x.2.x.x.x.3.x."}, {OH, "...............x"}}},
        {{{K, "X..x......X....."}, {CL, "........X......."}, {CH, "x.xxx.xxx.x2x.x3"}, {K2, "X..x......X....."}}}},
       {{"Atlanta", "ATL", 140, 0, 0},
        {"Rolling 808", "808", 142, 1, 0},
        {"Rage", "Rage", 150, 2, kRatchetHats},
        {"Cloud", "Cloud", 130, 0, kSparse | kShakerLine},
        {"Hard Trap", "Hard", 145, 1, kDoubleKick},
        {"Melodic Trap", "Melodic", 138, 0, kOpenHats},
        {"Phonk", "Phonk", 135, 2, kRollingPerc},
        {"Plugg", "Plugg", 145, 0, kSparse}}},
      {"Lo-fi", kFamilies[2], kDrumKit, 0.6, kFillSnare, 1, 0,
       {{{{K, "X......x..X....."}, {SN, "....X.......X..."}, {CH, "x.xox.xox.xox.xo"}}},
        {{{K, "X.........x.x..."}, {RIM, "....x.......x..."}, {SH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X..x......X..x.."}, {SN, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.x."}, {OH, "......x........."},
          {RD, "x.......x......."}}}},
       {{"Study Beat", "Chill", 80, 0, 0},
        {"Rainy Day", "Rain", 76, 1, 0},
        {"Tape Hiss", "Tape", 84, 2, kGhosty},
        {"Jazzhop", "Jazz", 82, 0, kRimShot | kShuffle},
        {"Bedroom", "Bedroom", 78, 1, kSparse},
        {"Sunset Loop", "Sunset", 86, 2, kShakerLine}}},
      {"Drill", kFamilies[2], kDrumKit, 0.54, kFillHats, 5, 0,
       {{{{K, "X.....x...x....."}, {SN, "........X..x...."}, {CH, "x..x..x.x2.x..x."}}},
        {{{K, "X.......x.x....."}, {SN, "........X.....x."}, {CH, "x.x.x.xx3.x.x.x."}, {RIM, "...x......x....."}}}},
       {{"UK Drill", "UK", 142, 0, 0},
        {"NY Drill", "NY", 140, 1, 0},
        {"Sliding Drill", "Slide", 144, 0, kRatchetHats},
        {"Dark Drill", "Dark", 138, 1, kSparse},
        {"Afro Drill", "Afro", 140, 0, kRollingPerc}}},

      // ---- Live Kit ----
      {"Rock", kFamilies[3], kDrumKit, 0.5, kFillToms, 2, 0,
       {{{{K, "X.......X.X....."}, {SN, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X.x.....X.x....."}, {SN, "....X.......X..."}, {OH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X...X...X...X..."}, {SN, "....X.......X..."}, {CH, "XoxoXoxoXoxoXoxo"}}},
        {{{K, "X...X...X...X..."}, {SN, "..X...X...X...X."}, {CH, "x.x.x.x.x.x.x.x."}}}},
       {{"Straight Eight", "Classic", 110, 0, 0},
        {"Arena", "Arena", 120, 1, kClapStack},
        {"Indie Floor", "Indie", 124, 2, 0},
        {"Punk", "Punk", 170, 3, 0},
        {"Garage Rock", "Garage", 130, 1, kGhosty},
        {"Grunge", "Grunge", 100, 0, kOpenHats},
        {"Pop Rock", "Pop", 116, 0, kShakerLine},
        {"Ballad", "Ballad", 72, 0, kSparse},
        {"Tom Groove", "Tribal", 105, 0, kToms},
        {"Surf", "Surf", 150, 3, kToms}}},
      {"Funk", kFamilies[3], kDrumKit, 0.54, kFillSnare, 1, 0,
       {{{{K, "X..x..x...X..x.."}, {SN, "....X..g.g..X..g"}, {CH, "xoxoxoxoxoxoxoxo"}}},
        {{{K, "X.x....x..x....."}, {SN, "....X.g..g.gX..."}, {CH, "x.x.x.x.x.x.x.x."}, {OH, "..............x."}}},
        {{{K, "X....x....X.x..."}, {SN, "....X..g.g.gX..g"}, {RD, "x..x..x...x.x..."}, {CH, "x.x.x.x.x.x.x.x."}}},
        // 3 second line: the New Orleans street beat, the bass drum on 1, the and of 2 and 4, the snare
        // rolling through
        {{{K, "X.....x.X.....x."}, {SN, "x.gx..x.g.xgX.g."}, {RD, "x...x...x...x..."}, {CH, "....x.......x..."}}},
        // 4 go-go: the pocket, the cowbell (on the ride's bell) and the open hat on the and of 4
        {{{K, "X.....x...X....."}, {SN, "....X..g....X..."}, {CH, "x.x.x.x.x.x.x..."}, {OH, "..............x."},
          {RD, "x..x..x...x.x..."}}}},
       {{"Tight Pocket", "Pocket", 100, 0, 0},
        {"Second Line", "New Orleans", 96, 3, kSwing8},
        {"Go-Go", "Go-go", 95, 4, kShuffle},
        {"Soul Strut", "Strut", 104, 1, 0},
        {"Space Funk", "P-funk", 98, 0, kClapStack},
        {"Linear", "Linear", 102, 1, kBroken},
        {"Disco Funk", "Disco", 112, 0, kOpenHats},
        {"Slow Jam", "Slow", 88, 1, kSparse | kGhosty}}},
      {"Soul & R&B", kFamilies[3], kDrumKit, 0.56, kFillToms, 0, 6,
       {{{{K, "X.......X......."}, {SN, "....X.......X..."}, {CL, "....X.......X..."}, {SH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X......x..x....."}, {SN, "....X.......X..g"}, {CH, "x.xox.xox.xox.xo"}, {RIM, "...g......g....."}}},
        {{{K, "X..x......X..x.."}, {CL, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.x."}, {SH, "gogogogogogogogo"}}},
        // 3 Motown: the snare and the tambourine (on the shaker) on all four beats
        {{{K, "X.......X.x....."}, {SN, "x...X...x...X..."}, {SH, "X.o.X.o.X.o.X.o."}, {CH, "x.x.x.x.x.x.x.x."}}}},
       {{"Motown Stomp", "Motown", 104, 3, 0},
        {"Neo Soul", "Neo", 86, 1, kShuffle},
        {"Slow Jam 90s", "90s", 90, 2, 0},
        {"Southern Soul", "Southern", 96, 0, kGhosty},
        {"Modern R&B", "Modern", 96, 2, kRatchetHats},
        {"Gospel Groove", "Gospel", 100, 1, kClapStack | kToms},
        {"Quiet Storm", "Storm", 74, 1, kSparse}}},
      {"Metal", kFamilies[3], kDrumKit, 0.5, kFillToms, 2, 0,
       {{{{K, "X.X.X.X.X.X.X.X."}, {SN, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X...X...X...X..."}, {SN, "..X...X...X...X."}, {CH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X.x.X.x.X.x.X.x."}, {SN, ".x.x.x.x.x.x.x.x"}, {CH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X..xX.....X.XX.."}, {SN, "....X.......X..."}, {RD, "X...x...X...x..."}}}},
       {{"Double Kick", "Classic", 160, 0, 0},
        {"Thrash", "Thrash", 190, 1, 0},
        {"Blast", "Blast", 200, 2, 0},
        {"Groove Metal", "Groove", 110, 3, 0},
        {"Djent", "Djent", 120, 3, kBroken},
        {"Doom", "Doom", 70, 0, kHalfTime | kSparse},
        {"Metalcore", "Core", 170, 0, kOpenHats}}},
      {"Jazz", kFamilies[3], kDrumKit, 0.66, kFillSnare, 3, 0,
       {{{{RD, "x...x.x.x...x.x."}, {PH, "....x.......x..."}, {SN, "......g.....g.g."}, {K, "g...g...g...g..."}}},
        {{{SH, "xoxoxoxoxoxoxoxo"}, {PH, "....x.......x..."}, {K, "g.......g......."}}},
        {{{K, "X..x..x...x....."}, {SN, "....X..g.g..X..."}, {CH, "xoxoxoxoxoxoxoxo"}, {RD, "x..............."}}},
        // 3 cool jazz: the ride, the hat on 2 and 4, a cross-stick on 4, the bass drum feathered
        {{{RD, "x...x.x.x...x.x."}, {PH, "....x.......x..."}, {RIM, "............x..."}, {K, "g...g...g...g..."}}}},
       {{"Medium Swing", "Swing", 140, 0, kSwing8},
        {"Brushes Ballad", "Ballad", 70, 1, kSwing8},
        {"Up Tempo", "Bebop", 220, 0, kSparse | kSwing8},
        {"Fusion", "Fusion", 110, 2, 0},
        {"Cool Jazz", "Cool", 120, 3, kSwing8},
        {"Hard Bop", "Hard bop", 170, 0, kGhosty | kSwing8},
        {"Acid Jazz", "Acid jazz", 104, 2, kShakerLine}}},

      // ---- Latin & Afro ----
      {"Afrobeat", kFamilies[4], kDrumKit, 0.52, kFillToms, 1, 2,
       {{{{K, "X.....x...X....."}, {SN, "....x..g..x...g."}, {CH, "x.xxx.xxx.xxx.xx"}, {RD, "X.x.xx.x.x.xx.x."},
          {SH, "gogogogogogogogo"}}},
        {{{K, "X..x....X.x....."}, {SN, "....X.......X..g"}, {OH, "..x...x...x...x."}, {RD, "x.x..x.x.x..x.x."}}},
        {{{K, "X..x..x...x..x.."}, {CL, "....X.......X..."}, {SH, "xoxoxoxoxoxoxoxo"}, {RIM, "..x..x....x..x.."}}},
        // 3 highlife: the bell pattern on the ride's bell, a rim backbeat, the hats in eighths
        {{{K, "X.......X.....x."}, {RIM, "....x.......x..."}, {CH, "x.x.x.x.x.x.x.x."}, {RD, "x.x.x.xx.x.x.x.x"}}}},
       {{"Lagos Groove", "Lagos", 110, 0, 0},
        {"Afro Funk", "Funk", 104, 1, kGhosty},
        {"Afropop", "Afropop", 102, 2, 0},
        {"Highlife", "Highlife", 120, 3, kShakerLine},
        {"Afro Soul", "Soul", 96, 1, kSparse},
        {"Juju", "Juju", 112, 0, kRollingPerc | kToms}}},
      {"Amapiano", kFamilies[4], kDrumKit, 0.54, kFillDrop, 5, 6,
       {{{{K, "X.......X......."}, {K2, "...x..x....x..x."}, {SH, "gogogogogogogogo"}, {CL, "....x.......x..."},
          {RIM, "..x..x....x..x.."}}},
        {{{K, "X...X...X...X..."}, {K2, "......x.x.....x."}, {SH, "xoxoxoxoxoxoxoxo"}, {RIM, "...x..x....x..x."}}}},
       {{"Log Drum", "Classic", 113, 0, 0},
        {"Private School", "Soft", 110, 0, kSparse | kShakerLine},
        {"Deep Piano", "Deep", 112, 1, 0},
        {"Bacardi", "Bacardi", 115, 1, kDoubleKick},
        {"Quantum", "Quantum", 113, 0, kRatchetHats}}},
      {"Samba", kFamilies[4], kDrumKit, 0.5, kFillHands, 0, 3,
       {{{{K, "x..Xx..Xx..Xx..X"}, {SH, "XoxoXoxoXoxoXoxo"}, {RIM, "x.xx.x.xx.x.x.x."}, {CL, "..x...x...x...x."}}},
        {{{K, "X..xX..xX..xX..x"}, {SN, "....X.......X..."}, {CH, "xoxoxoxoxoxoxoxo"}, {RIM, "x..x..x...x..x.."}}}},
       {{"Batucada", "Carnival", 104, 0, 0},
        {"Samba Funk", "Funk", 100, 1, 0},
        {"Partido Alto", "Partido", 96, 1, kGhosty},
        {"Samba Reggae", "Reggae", 100, 0, kSparse},
        {"Pagode", "Pagode", 92, 1, kShakerLine}}},
      {"Bossa Nova", kFamilies[4], kDrumKit, 0.5, kFillSnare, 3, 1,
       {{{{K, "X..xX..xX..xX..x"}, {RIM, "x..x..x...x..x.."}, {CH, "xoxoxoxoxoxoxoxo"}}},
        {{{K, "X.......X......."}, {RIM, "..x..x...x..x..."}, {SH, "xoxoxoxoxoxoxoxo"}}}},
       {{"Classic Bossa", "Bossa", 130, 0, 0},
        {"Brushes Bossa", "Brushes", 120, 1, kSparse},
        {"Samba Jazz", "Samba jazz", 150, 0, kGhosty},
        {"Lounge", "Lounge", 110, 1, kShakerLine}}},
      {"Reggaeton", kFamilies[4], kDrumKit, 0.5, kFillSnare, 5, 0,
       {{{{K, "X...X...X...X..."}, {SN, "...X..X....X..X."}, {CH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X...X...X...X..."}, {SN, "...x..x....x..x."}, {CL, "...X..X....X..X."}, {OH, "..x...x...x...x."}}},
        {{{K, "X..xX...X..xX..."}, {SN, "...X..X....X..X."}, {RIM, "x..x..x...x..x.."}}}},
       {{"Dembow", "Classic", 95, 0, 0},
        {"Perreo", "Perreo", 98, 2, 0},
        {"Moombahton", "Moomba", 108, 1, 0},
        {"Dancehall Dembow", "Dance", 100, 0, kRollingPerc},
        {"Latin Trap", "Trap", 90, 1, kRatchetHats | kHalfTime},
        {"Romantic", "Romantic", 90, 0, kSparse}}},

      // ---- Reggae & Dub ----
      {"Reggae", kFamilies[5], kDrumKit, 0.6, kFillToms, 0, 0,
       {{{{K, "........X......."}, {RIM, "........X......."}, {CH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X...X...X...X..."}, {SN, "........X......."}, {CH, "x.x.x.x.x.x.x.x."}, {OH, "..x...x...x...x."}}},
        {{{K, "X...X...X...X..."}, {RIM, "....x.......x..."}, {CH, "xoxoxoxoxoxoxoxo"}}},
        // 3 rocksteady: the kick on 1 and 3, the snare on 2 and 4, the hats swung in eighths
        {{{K, "X.......X......."}, {SN, "....X.......X..."}, {CH, "x.x.x.x.x.x.x.x."}, {RIM, "..x.......x....."}}}},
       {{"One Drop", "One drop", 75, 0, kSwing8},
        {"Rockers", "Rockers", 80, 1, 0},
        {"Steppers", "Steppers", 82, 2, 0},
        {"Roots", "Roots", 72, 0, kGhosty | kToms | kSwing8},
        {"Lovers Rock", "Lovers", 76, 0, kShakerLine | kSwing8},
        {"Ska", "Ska", 150, 1, kOpenHats | kSwing8},
        {"Rocksteady", "Rocksteady", 100, 3, kShuffle | kSwing8},
        {"Dancehall", "Dancehall", 96, 2, kRollingPerc}}},
      {"Dub", kFamilies[5], kDrumKit, 0.6, kFillFx, 0, 0,
       {{{{K, "........X......."}, {RIM, "........X......."}, {CH, "x.x.x.x.x.x.x.x."}, {FX, "..............x."}}},
        {{{K, "X...X...X...X..."}, {SN, "........X......."}, {CH, ".x.x.x.x.x.x.x.x"}, {OH, "......x.......x."}}}},
       {{"Dub Plate", "Roots", 72, 0, kSwing8},
        {"Steppers Dub", "Steppers", 78, 1, 0},
        {"Dubwise", "Dubwise", 70, 0, kSparse | kSwing8},
        {"Digi Dub", "Digital", 80, 1, kRatchetHats},
        {"Dub Siren", "Siren", 74, 0, kRollingPerc}}},

      // ---- Hand Percussion (the Afro-Latin and Percussion kits) ----
      {"Afro-Cuban", kFamilies[6], kAfroLatinKit, 0.5, kFillHands, 0, 0,
       {// 0 salsa: son clave 3-2, the conga tumbao (heel-tip, slap on 2, open tones on 4 and 4-and), bongo bell
        {{{CLAVE, "X.....x.....x.......x...x......."}, {CONGA, "g.g...g.g.g.x.x.g.g...g.g.g.x.x."}, {AL_CONGA_SLAP, "....X...............X..........."}, {CAMPANA, "X...x.x.X...x.x.X...x.x.X...x.x."}}},
        // 1 martillo: the bongo's eighths with the open hembra on 4, guiro
        {{{AL_HIGH_BONGO, "x.o.x.o.x.o...o.x.o.x.o.x.o...o."}, {AL_LOW_BONGO, "............X...............X..."}, {GUIRO, "X.xxx.xxX.xxx.xxX.xxx.xxX.xxx.xx"}}},
        // 2 guaguanco: rumba clave 3-2, the palitos, conga and tumba answering
        {{{CLAVE, "X.....x.......x.....x...x......."}, {BELL, "x.x.xx.x.xx.x.x.x.x.xx.x.xx.x.x."}, {CONGA, "......x.x.....X.......x.x......."}, {TUMBA, "..........x...............x...Xx"}, {AL_CONGA_SLAP, "....X.......g.......X.......g..."}}},
        // 3 cha-cha: cowbell on the beats, guiro long-short-short, the tumbao's cha-cha on 4, clave 2-3
        {{{CAMPANA, "X...x...X...x...X...x...X...x..."}, {GUIRO, "X.xxx.xxX.xxx.xxX.xxx.xxX.xxx.xx"}, {CONGA, "g.g...g.g.g.x.x.g.g...g.g.g.x.x."}, {AL_CONGA_SLAP, "....X...............X..........."}, {CLAVE, "....x...x.......x.....x.....x..."}}},
        // 4 mambo: the mambo bell (cascara 2-3) on the campana, clave 2-3, a driving tumbao
        {{{CAMPANA, "X...x.x...x...x.X...x...x.x...x."}, {CLAVE, "....x...x.......x.....x.....x..."}, {CONGA, "g.g...g.g.g.x.x.g.g...g.g.g.x.x."}, {AL_CONGA_SLAP, "....X...............X..........."}, {TUMBA, "...............x...............x"}}},
        // 5 Latin house: congas over a shekere in sixteenths
        {{{CONGA, "x..x..x...x.x..."}, {TUMBA, ".......x......X."}, {AL_CONGA_SLAP, "....X........X.."}, {SHEKERE, "xoxoxoxoxoxoxoxo"}}},
        // 6 son: clave 2-3, martillo, the tumba's two open tones, guiro
        {{{CLAVE, "....x...x.......x.....x.....x..."}, {AL_HIGH_BONGO, "x.o.x.o.x.o...o.x.o.x.o.x.o...o."}, {AL_LOW_BONGO, "............X...............X..."}, {TUMBA, "............x.x.............x.x."}, {GUIRO, "X.xx..xxX.xx..xxX.xx..xxX.xx..xx"}}}},
       {{"Tumbao", "Salsa", 180, 0, 0},
        {"Martillo", "Bongo", 160, 1, 0},
        {"Guaguanco", "Rumba", 190, 2, 0},
        {"Son Clave", "Son", 150, 6, 0},
        {"Cha-cha", "Cha-cha", 118, 3, 0},
        {"Mambo", "Mambo", 200, 4, 0},
        {"Latin House Hands", "House", 124, 5, kShakerLine}},
       kTradCuban},
      {"West African", kFamilies[6], kAfroLatinKit, 0.5, kFillDjembe, 0, 0,
       {// 0 djembe circle: the first djembe accompaniment over the three dunun and their bell
        {{{AL_DJEMBE_BASS, "X.......X......."}, {AL_DJEMBE_TONE, "..xx......xx...."}, {AL_DJEMBE_SLAP, "....X.x.....X.x."}, {KENKENI, "..x...x...x...x."}, {BELL, "x.x.x.x.x.x.x.x."}, {SANGBAN, "X.....x.X.....x."}, {DUNDUNBA, "X.........x....."}}},
        // 1 kuku: bass on 1 and 3, tones after, slaps on 2-and and 4-and
        {{{AL_DJEMBE_BASS, "X.......X......."}, {AL_DJEMBE_TONE, "..xx......xx...."}, {AL_DJEMBE_SLAP, "......X.......X."}, {KENKENI, "x.x.x.x.x.x.x.x."}, {SANGBAN, "X..x..x...x.x..."}, {DUNDUNBA, "X.....x...x....."}, {BELL, "x...x...x...x..."}}},
        // 2 djole: a busier slap figure over the sangban
        {{{AL_DJEMBE_BASS, "X.......X......."}, {AL_DJEMBE_TONE, "..xx......xx...."}, {AL_DJEMBE_SLAP, "....X.xx....X.xx"}, {SANGBAN, "x.x...x.x.x...x."}, {DUNDUNBA, "X.....x.x.....x."}, {BELL, "x.x.x.x.x.x.x.x."}}},
        // 3 soli (12/8): djembe, kenkeni on the dotted beats, the standard bell
        {{{AL_DJEMBE_BASS, "X...........X..........."}, {AL_DJEMBE_TONE, "....x.x.........x.x....."}, {AL_DJEMBE_SLAP, "........X...........X..."}, {KENKENI, "x...o...o...x...o...o..."}, {BELL, "x...x...x.x...x...x...x."}, {SANGBAN, "......x.....X.........x."}, {DUNDUNBA, "X...............x.x....."}}, kRate16T},
        // 4 dununba (12/8): the dundunba leads, the bell on the four dotted beats
        {{{DUNDUNBA, "X...x.......X...x......."}, {SANGBAN, "......x...........x.X..."}, {KENKENI, "x...x...x...x...x...x..."}, {BELL, "x.....x.....x.....x....."}, {AL_DJEMBE_BASS, "X...........X..........."}, {AL_DJEMBE_SLAP, "........X.x.........X.x."}}, kRate16T},
        // 5 lamban (12/8, fast)
        {{{AL_DJEMBE_SLAP, "....X...........X......."}, {AL_DJEMBE_TONE, "........x.x.........x.x."}, {AL_DJEMBE_BASS, "X...........X..........."}, {KENKENI, "..x.x...x.x...x.x...x.x."}, {BELL, "x...x...x.x...x...x...x."}, {SANGBAN, "x.......x...x.......x..."}}, kRate16T}},
       {{"Djembe Circle", "Circle", 110, 0, 0},
        {"Kuku", "Kuku", 124, 1, 0},
        {"Dununba", "Dununba", 104, 4, 0},
        {"Lamban", "Lamban", 138, 5, 0},
        {"Soli", "Soli", 116, 3, 0},
        {"Djole", "Djole", 128, 2, 0},
        {"Village Night", "Night", 96, 3, kSparse}},
       kTradWestAfrican},
      {"Middle Eastern", kFamilies[6], kPercussionKit, 0.5, kFillDarbuka, 0, 0,
       {// 0 maqsum: D T . T D . T .
        {{{DARBUKA_DUM, "X.......X......."}, {DARBUKA_TEK, "..x...x...g.x.g."}, {FRAME_DRUM, "o.......o......."}}},
        // 1 saidi: D T . D D . T .
        {{{DARBUKA_DUM, "X.....X.X......."}, {DARBUKA_TEK, "..x.g.....g.x.g."}, {FRAME_DRUM, "o.......o......."}}},
        // 2 baladi: D D . T D . T .
        {{{DARBUKA_DUM, "X.X.....X......."}, {DARBUKA_TEK, "....g.x...g.x.g."}, {FRAME_DRUM, "o.......o......."}}},
        // 3 malfuf (2/4): D . . T . . T .
        {{{DARBUKA_DUM, "X.......X......."}, {DARBUKA_TEK, "...x..x....x..x."}}},
        // 4 ayoub (2/4): D . D T
        {{{DARBUKA_DUM, "X...x...X...x..."}, {DARBUKA_TEK, "..g...x...g...x."}}},
        // 5 chiftetelli (8/4): D . . T . T . . D . D . T . . .
        {{{DARBUKA_DUM, "X...............X...x..........."}, {DARBUKA_TEK, "..g...x...x...g.........x...g..."}, {FRAME_DRUM, "o...............o..............."}}},
        // 6 masmoudi kabir (8/4): D D . T D . T .
        {{{DARBUKA_DUM, "X...X...........X..............."}, {DARBUKA_TEK, "........g...x.......g...x...g..."}, {FRAME_DRUM, "o...............o..............."}}}},
       {{"Maqsum", "Maqsum", 100, 0, 0},
        {"Saidi", "Saidi", 104, 1, 0},
        {"Ayoub", "Ayoub", 120, 4, 0},
        {"Malfuf", "Malfuf", 130, 3, 0},
        {"Baladi", "Baladi", 100, 2, 0},
        {"Chiftetelli", "Chifte", 90, 5, 0},
        {"Masmoudi", "Masmoudi", 96, 6, 0},
        {"Desert Night", "Desert", 84, 5, kSparse}},
       kTradMiddleEastern},
      {"Indian", kFamilies[6], kPercussionKit, 0.5, kFillTihai, 0, 0,
       {// 0 teentaal (16 matras, an eighth each): dha dhin dhin dha | dha dhin dhin dha | dha tin tin ta | ta dhin dhin dha; no bayan in the khali
        {{{TABLA, "X.x.x.x.x.x.x.x.x.o.o.o.o.x.x.x."}, {FRAME_DRUM, "X.x.x.x.x.x.x.x...........x.x.x."}}},
        // 1 keherwa (8 matras): dha ge na ti | na ka dhi na
        {{{TABLA, "X...x.x.x...x.x."}, {FRAME_DRUM, "X.x.......g.x..."}}},
        // 2 dadra (6 matras): dha dhin na | dha tin na
        {{{TABLA, "X.x.x.x.x.x."}, {FRAME_DRUM, "X.x...x....."}}},
        // 3 bhangra chaal: the dhol's bass (on the darbuka doum) on the beats, its stick (tek) on the swung offbeats
        {{{DARBUKA_DUM, "X.....x.....X.....x...o."}, {DARBUKA_TEK, "..g.x...g.x...g.x...g..."}}, kRate16T},
        // 4 filmi: a dholak keherwa with a shaker
        {{{FRAME_DRUM, "X.....x.X.....x."}, {TABLA, "..o.x..o..o.x..o"}, {SHAKER, "o.o.o.o.o.o.o.o."}}}},
       {{"Teentaal", "Teentaal", 95, 0, 0},
        {"Keherwa", "Keherwa", 100, 1, 0},
        {"Dadra", "Dadra", 90, 2, 0},
        {"Bhangra", "Bhangra", 100, 3, 0},
        {"Filmi", "Filmi", 105, 4, 0},
        {"Raga Night", "Raga", 70, 0, kSparse}},
       kTradIndian},

      // ---- FX & Odd ----
      {"Glitch", kFamilies[7], kFxKit, 0.5, kFillFx, 5, 0,
       {{{{0, "X..x....X.x....."}, {2, "....X.......X..."}, {6, "x.xxx.x2x.xx3.x."}, {3, "...x.......x..x."},
          {1, "......x.......x."}}},
        {{{0, "X.......X..x...."}, {4, "....X.......X..."}, {6, "oooooooooooooooo"}, {5, "..x...x.x.....x."},
          {9, "...........x...."}}},
        {{{0, "X.....x...X....."}, {2, "....x..x....x..."}, {6, "2.x.3.x.4.x.2.x."}, {11, "x..........x...."},
          {8, ".x....x......x.."}}}},
       {{"IDM", "IDM", 120, 0, 0},
        {"Glitch Hop", "Glitch hop", 105, 2, 0},
        {"Clicks and Cuts", "Clicks", 128, 1, 0},
        {"Bitcrushed", "Bit", 140, 0, kRatchetHats},
        {"Video Game", "Chip", 150, 2, kBusyHats},
        {"Broken Robot", "Robot", 95, 1, kBroken}}},
      {"Animal Grooves", kFamilies[7], kAnimalKit, 0.5, kFillFx, 0, 0,
       {{{{0, "X...X...X...X..."}, {2, "....X.......X..."}, {6, "x.x.x.x.x.x.x.x."}, {3, "......x.......x."},
          {1, "x.x.x..........."}}},
        {{{0, "X.....x...X....."}, {4, "....X.......X..."}, {6, "xoxoxoxoxoxoxoxo"}, {8, "..x.....x.x....."},
          {10, "..............x."}}}},
       {{"Barnyard Stomp", "Farm", 110, 0, 0},
        {"Jungle Night", "Jungle", 100, 1, 0},
        {"Pond Life", "Pond", 90, 1, kSparse},
        {"Dog Park", "Park", 120, 0, kGhosty},
        {"Safari", "Safari", 105, 1, kRollingPerc | kGhosty}}},
      {"Odd Meters", kFamilies[7], kDrumKit, 0.5, kFillToms, 0, 0,
       {{{{K, "X.....X.X....."}, {SN, "....X.......X."}, {CH, "x.x.x.x.x.x.x."}}},
        {{{K, "X.......X...X......."}, {SN, "....X.......X...X..."}, {CH, "x.x.x.x.x.x.x.x.x.x."}}},
        {{{K, "X..........."}, {SN, "....x...x..."}, {CH, "x.x.x.x.x.x."}}},
        {{{K, "X...X...X...X....."}, {SN, "........X.....X..."}, {CH, "x.x.x.x.x.x.x.x.x."}}}},
       {{"Seven Eight", "7/8", 120, 0, 0},
        {"Five Four", "5/4", 110, 1, 0},
        {"Waltz", "3/4", 90, 2, 0},
        {"Nine Eight", "9/8", 130, 3, 0},
        {"Prog Odd", "Prog", 140, 0, kGhosty | kToms},
        {"Math Rock", "Math", 150, 1, kBroken}}},
      {"Halftime", kFamilies[7], kDrumKit, 0.5, kFillSnare, 4, 0,
       {{{{K, "X.........x....."}, {SN, "........X......."}, {CH, "x.x.x.x.x.x.x.x."}}},
        {{{K, "X.........X....."}, {SN, "........X......."}, {CH, "x...x...x...x2x."}, {FX, "..............x."}}},
        {{{K, "X......x.x......"}, {CL, "........X......."}, {OH, "..x.......x....."}, {RIM, "...x..x.......x."}}}},
       {{"Dubstep", "Dubstep", 140, 1, 0},
        {"Halftime Roller", "DnB", 170, 0, 0},
        {"Future Bass", "Future", 150, 2, kOpenHats},
        {"Downtempo", "Downtempo", 90, 0, kShakerLine},
        {"Trip Hop", "Trip hop", 88, 2, kGhosty | kSparse},
        {"Riddim", "Riddim", 140, 1, kRatchetHats},
        {"Chillstep", "Chill", 140, 0, kSparse}}},
   };
   return g;
}

inline int genreCount() { return static_cast<int>(genres().size()); }

inline const Genre &genreAt(int i) {
   const auto &g = genres();
   return g[static_cast<size_t>(i < 0 ? 0 : i >= static_cast<int>(g.size()) ? static_cast<int>(g.size()) - 1 : i)];
}

inline int styleCount() {
   int n = 0;
   for (const Genre &g : genres())
      n += static_cast<int>(g.styles.size());
   return n;
}

// The twelve genres the GENERATE tab shows as tiles; the rest are a click away in the browser.
inline const int kTileGenres[12] = {0, 1, 2, 10, 11, 6, 5, 14, 15, 24, 19, 26};

} // namespace jdrummer
