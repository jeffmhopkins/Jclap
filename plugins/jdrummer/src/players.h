#pragma once

// The players: who plays the song. A player doesn't change what the groove is, only how it is
// played: how loose the timing and dynamics are, how busy, how many ghost notes, how big the fills,
// and where the backbeat sits against the grid. Drummers play the kit; a percussionist plays its own
// part on a second channel, for a second JKit set to the percussionist's kit (Afro-Latin or
// Percussion).

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
   {"Pocket Funk", "ghost notes everywhere, hats lay back", "funk, soul, hip-hop", 0.4, 0.7, 0.9, 0.4, 5, 3},
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

// The kit each percussionist plays (a second JKit set to it), the tradition its own twists stay in,
// and the fill it plays. The conguero and the djembe player play JKit's Afro-Latin kit, the rest its
// Percussion kit.
inline int percussionistKit(int who) { return who == 0 || who == 1 ? kAfroLatinKit : kPercussionKit; }
inline unsigned percussionistTradition(int who) {
   static const unsigned t[kPercussionists] = {kTradCuban, kTradWestAfrican, kTradBrazilian, kTradFlamenco,
                                               kTradMiddleEastern | kTradIndian,
                                               kTradCuban | kTradBrazilian | kTradFlamenco};
   return t[who < 0 ? 0 : who >= kPercussionists ? kPercussionists - 1 : who];
}
inline FillKind percussionistFill(int who) {
   static const FillKind f[kPercussionists] = {kFillHands, kFillDjembe, kFillHands, kFillCajon, kFillDarbuka, kFillHats};
   return f[who < 0 ? 0 : who >= kPercussionists ? kPercussionists - 1 : who];
}

// Each percussionist's own grooves, on its kit's pads: in 4/4, and in 3/4, 7/8 or 12/8 for songs in
// those meters (the generator picks one that fits the drummer's).
inline const std::vector<Groove> &percussionGrooves(int percussionist) {
   static const std::vector<Groove> g[kPercussionists] = {
      // Conguero: the tumbao on conga and tumba, the bongo's martillo, a 12/8 bembe under a bell.
      {{{{CONGA, "g.g...g.g.g.x..."}, {AL_CONGA_SLAP, "....X..........."}, {TUMBA, "..............x."}}},
       {{{AL_HIGH_BONGO, "x.o.x.o.x.o...o."}, {AL_LOW_BONGO, "............X..."}, {CONGA, "............x.x."}}},
       {{{CONGA, "o.......x.x.o.......x.x."}, {AL_CONGA_SLAP, "....X...........X......."}, {BELL, "x...x...x.x...x...x...x."}}, kRate16T}},
      // Djembe Circle: the djembe's accompaniments, with the kenkeni, and one in 12/8.
      {{{{AL_DJEMBE_BASS, "X.......X......."}, {AL_DJEMBE_TONE, "..xx......xx...."}, {AL_DJEMBE_SLAP, "....X.x.....X.x."}}},
       {{{AL_DJEMBE_BASS, "X.......X......."}, {AL_DJEMBE_TONE, "..xx......xx...."}, {AL_DJEMBE_SLAP, "......X.......X."},
         {KENKENI, "x.x.x.x.x.x.x.x."}}},
       {{{AL_DJEMBE_BASS, "X...........X..........."}, {AL_DJEMBE_TONE, "....x.x.........x.x....."}, {AL_DJEMBE_SLAP, "........X.x.........X.x."}}, kRate16T}},
      // Bateria: the surdo (on the cajon bass) on 2 and 4, the ganza and the tamborim (on the bongo).
      {{{{CAJON_BASS, "....X.......X..."}, {SHAKER, "XoxoXoxoXoxoXoxo"}, {HIGH_BONGO, "XxxxXxxxXxxxXxxx"}}},
       {{{CAJON_BASS, "o..xX..xo..xX..x"}, {SHAKER, "xoxoxoxoxoxoxoxo"}, {HIGH_BONGO, "x.xx.x.xx.x.x.x."}}},
       // 12/8: the surdo on beats 2 and 4, the ganza in triplets, the tamborim on the bell's figure
       {{{CAJON_BASS, "......X...........X....."}, {SHAKER, "XooXooXooXooXooXooXooXoo"},
         {HIGH_BONGO, "x.x.x.x.x...x.x.x.x...x."}}, kRate16T}},
      // Hand Drummer: the cajon, the frame drum and palmas, in 4/4, 3/4 and 12/8.
      {{{{CAJON_BASS, "X......X..X....."}, {CAJON_SLAP, "....X.......X..."}, {FRAME_DRUM, "..g...g...g...g."},
         {SHAKER, "x.x.x.x.x.x.x.x."}}},
       {{{CAJON_BASS, "X.....x...X....."}, {CAJON_SLAP, "....X..g....X..g"}, {PALMAS, "..x...x...x...x."}}},
       {{{CAJON_BASS, "X..........."}, {CAJON_SLAP, "....x...x..."}, {SHAKER, "o.o.o.o.o.o."}}},
       {{{CAJON_BASS, "X.......X....."}, {CAJON_SLAP, "....x.......x."}}},           // 7/8
       {{{CAJON_BASS, "X.......X........."}, {CAJON_SLAP, "....x.......x....."}}},   // 9/8
       {{{CAJON_BASS, "X...........X......."}, {CAJON_SLAP, "....x...........x..."}}}, // 5/4
       {{{CAJON_BASS, "X...........X..........."}, {CAJON_SLAP, "..g.g.X.g.g...g.g.X.g.g."}}, kRate16T}},
      // Tabla & Darbuka: maqsum on the darbuka, keherwa on the tabla, dadra in 3/4 and a 7/8.
      {{{{DARBUKA_DUM, "X.......X......."}, {DARBUKA_TEK, "..x...x...g.x.g."}, {TABLA, "x..x..x.x..x..x."}}},
       {{{TABLA, "X...x.x.x...x.x."}, {FRAME_DRUM, "X.x.......g.x..."}}},
       {{{TABLA, "X.x.x.x.x.x."}, {FRAME_DRUM, "X.x...x....."}}},
       {{{DARBUKA_DUM, "X.......X....."}, {DARBUKA_TEK, "....x.......x."}, {TABLA, "..g...g...g..."}}},
       // 9/8 karsilama (2+2+2+3) and a 5/4
       {{{DARBUKA_DUM, "X.......X........."}, {DARBUKA_TEK, "....x.......x.x..."}, {TABLA, "..g...g...g......."}}},
       {{{DARBUKA_DUM, "X...........X......."}, {DARBUKA_TEK, "....x...x.......x..."}, {TABLA, "..g...g...g...g...g."}}},
       // 12/8: a 6/8 on the darbuka, the tabla's soft strokes between
       {{{DARBUKA_DUM, "X...........X..........."}, {DARBUKA_TEK, "......x...x.......x...x."},
         {TABLA, "..g.g.....g...g.g.....g."}}, kRate16T}},
      // Shaker & Tamb: the top layer, in sixteenths, in 3/4 and in 12/8.
      {{{{SHAKER, "oXoxoXoxoXoxoXox"}, {FRAME_DRUM, "X.......X......."}, {PALMAS, "....x.......x..."}}},
       {{{SHAKER, "xoxoxoxoxoxoxoxo"}, {HIGH_BONGO, "..x...x...x...x."}}},
       {{{SHAKER, "Xoxoxoxoxoxo"}}},
       {{{SHAKER, "X.o.o.X.o.o.X.o.o.X.o.o."}}, kRate16T},
       {{{SHAKER, "XoxoXoxoXoxoXoxoXo"}}},   // 9/8
       {{{SHAKER, "XoxoXoxoXoxoXoxoXoxo"}}}}, // 5/4
   };
   return g[percussionist < 0 ? 0 : percussionist >= kPercussionists ? kPercussionists - 1 : percussionist];
}

} // namespace jdrummer
