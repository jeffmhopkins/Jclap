#pragma once

// The players: who plays the song. A player doesn't change what the groove is, only how it is
// played: how loose the timing and dynamics are, how busy, how many ghost notes, how big the fills,
// and where the backbeat sits against the grid. Drummers play the kit; a percussionist plays its own
// part on a second channel, for a second JKit set to the Percussion kit.

#include <vector>

#include "kit.h"
#include "styles.h"

namespace jdrummer {

struct Player {
   const char *name;
   const char *blurb;
   const char *genres;
   double loose, busy, ghosts, fills;
   // Milliseconds the backbeat (snare, clap) sits behind (+) or ahead of (-) the grid, and the hats.
   int backbeatFeel, hatFeel;
};

constexpr int kDrummers = 6;
constexpr int kPercussionists = 6;

inline const Player kDrummerList[kDrummers] = {
   {"Session Pro", "tight, tasteful, fills at phrase ends", "pop, rock, R&B", 0.2, 0.5, 0.4, 0.5, 2, 0},
   {"Pocket Funk", "ghost notes everywhere, hats lay back", "funk, soul, hip-hop", 0.55, 0.7, 0.9, 0.4, 6, 5},
   {"Stadium Rock", "big accents, crashes, tom fills", "rock, metal, punk", 0.3, 0.6, 0.2, 0.9, 0, -2},
   {"Brushes Jazz", "loose and swung, ride and comping", "jazz, bossa, lounge", 0.75, 0.55, 0.6, 0.5, 4, 3},
   {"Break Chopper", "chopped breaks, rolls, shuffled hats", "breaks, DnB, jungle", 0.45, 0.9, 0.55, 0.7, -2, 0},
   {"Machine", "dead on the grid, no humanize", "house, techno, trap", 0.0, 0.5, 0.1, 0.3, 0, 0},
};

// Index 0 of the Percussionist param is no percussionist; 1..6 are these.
inline const Player kPercussionistList[kPercussionists] = {
   {"Conguero", "tumbao: slaps, open tones, heel-toe", "Afro-Cuban, salsa, Latin house", 0.4, 0.7, 0.6, 0.5, 0, 0},
   {"Djembe Circle", "call and response, rolling bass tones", "West African, afrobeat", 0.5, 0.8, 0.4, 0.7, 0, 0},
   {"Bateria", "surdo, caixa and tamborim driving", "samba, carnival", 0.35, 0.9, 0.3, 0.8, 0, 0},
   {"Hand Drummer", "cajon and frame drum, unplugged", "acoustic, flamenco, folk", 0.6, 0.5, 0.7, 0.4, 3, 0},
   {"Tabla & Darbuka", "ornate rolls, odd meters", "Indian, Middle Eastern", 0.5, 0.85, 0.5, 0.6, 0, 0},
   {"Shaker & Tamb", "the top layer over any drummer", "any groove", 0.5, 0.9, 0.3, 0.1, 0, 2},
};

// Each percussionist's own grooves, on the Percussion kit's pads.
inline const std::vector<Groove> &percussionGrooves(int percussionist) {
   static const std::vector<Groove> g[kPercussionists] = {
      {{{{HIGH_CONGA, "..x...x...x....."}, {CONGA_SLAP, "....X.......X..."}, {LOW_CONGA, "......x.......x."},
         {LOW_BONGO, "xoxoxoxoxoxoxoxo"}}},
       {{{LOW_CONGA, "......x.x.....x."}, {HIGH_CONGA, "x.x...x...x.x..."}, {CONGA_SLAP, "...X.......X...."},
         {PALMAS, "x..x...x..x.x..."}}}},
      {{{{DJEMBE_BASS, "X.....X...X....."}, {DJEMBE_TONE, "..x.x...x.x.x..."}, {DJEMBE_SLAP, "....X..X....X..X"}}},
       {{{DJEMBE_BASS, "X.......X..x...."}, {DJEMBE_TONE, "x.x..x.x.x..x.x."}, {DJEMBE_SLAP, "...X......X...X."}}}},
      {{{{CAJON_BASS, "....X.......X..."}, {SHAKER, "XoxoXoxoXoxoXoxo"}, {HIGH_BONGO, "x.xx.x.xx.x.x.x."},
         {PALMAS, "..x...x...x...x."}}},
       {{{CAJON_BASS, "X..xX..xX..xX..x"}, {SHAKER, "xoxoxoxoxoxoxoxo"}, {HIGH_BONGO, "x..x..x...x..x.."}}}},
      {{{{CAJON_BASS, "X......X..X....."}, {CAJON_SLAP, "....X.......X..."}, {FRAME_DRUM, "..g...g...g...g."},
         {SHAKER, "x.x.x.x.x.x.x.x."}}},
       {{{CAJON_BASS, "X.....x...X....."}, {CAJON_SLAP, "....X..g....X..g"}, {PALMAS, "..x...x...x...x."}}}},
      {{{{DARBUKA_DUM, "X.....X...X....."}, {DARBUKA_TEK, "..x.x..x.xx.x.xx"}, {TABLA, "x..x..x.x..x..x."}}},
       {{{DARBUKA_DUM, "X.......X......."}, {DARBUKA_TEK, "..x...x.....x..."}, {TABLA, "XoxoXoxoXoxoXoxo"}}}},
      {{{{SHAKER, "oXoxoXoxoXoxoXox"}, {FRAME_DRUM, "X.......X......."}, {PALMAS, "....x.......x..."}}},
       {{{SHAKER, "xoxoxoxoxoxoxoxo"}, {HIGH_BONGO, "..x...x...x...x."}}}},
   };
   return g[percussionist < 0 ? 0 : percussionist >= kPercussionists ? kPercussionists - 1 : percussionist];
}

} // namespace jdrummer
