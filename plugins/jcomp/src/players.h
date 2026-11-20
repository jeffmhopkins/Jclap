#pragma once

// The players: who plays the song. A player doesn't change the chords, only how they are played:
// how busy the rhythm is, how far behind the beat it sits, how rich the voicings are, how often a
// run leads into a change, how long the chords ring, and the instrument it is voicing for (which
// sets the shape, the number of notes and the register). Each is an archetype from the research
// behind the genres (see the README's sources), not a person.

#include "harmony.h"

namespace jcomp {

enum Instrument { kEPiano, kClav, kOrgan, kPiano, kGuitar, kSynth, kPad, kInstrumentCount };
inline const char *const kInstrumentNames[kInstrumentCount] = {"E-Piano", "Clav", "Organ", "Piano", "Guitar", "Synth", "Pad"};
// The kind of player, for its icon: keys, organ, guitar.
enum Kind { kKeys, kOrganKind, kGuitarKind };

struct Player {
   const char *name;
   const char *blurb;
   const char *genres;
   int instrument;
   int kind;
   // How it voices: shape, notes, the register window and its centre.
   int shape, voices, lo, hi, center;
   int onsets;         // 0 together, 1 rolled, 2 strummed down, 3 strummed both ways
   // How it plays, 0..1: how busy, how rich the chords (extensions), how often a run, how long the
   // chords ring, how loose the timing; and how far behind the beat it sits, in ms.
   double busy, rich, runs, ring, loose;
   int laidBack;
};

constexpr int kPlayers = 13;

inline const Player kPlayerList[kPlayers] = {
   {"Pocket Keys", "tight stabs on the bass, 9ths and 13ths", "funk, soul, disco", kEPiano, kKeys,
    kDrop2, 4, 50, 76, 62, 0, 0.55, 0.7, 0.25, 0.4, 0.3, 6},
   {"Clav Chopper", "busy sixteenth chops, muted and percussive", "funk, 70s soul", kClav, kKeys,
    kClose, 3, 52, 79, 66, 0, 0.9, 0.4, 0.1, 0.1, 0.25, 2},
   {"Church Organ", "held chords, walk-ups into the changes", "gospel, soul, blues", kOrgan, kOrganKind,
    kSpread, 4, 48, 79, 64, 0, 0.45, 0.75, 0.55, 0.9, 0.35, 4},
   {"Neo-Soul Keys", "lazy, behind the beat, rich and open", "neo-soul, R&B, lo-fi", kEPiano, kKeys,
    kDrop24, 5, 48, 79, 62, 1, 0.4, 0.95, 0.3, 0.75, 0.55, 18},
   {"Jazz Comper", "shells and rootless voicings, sparse pushes", "swing, bossa, Latin jazz", kPiano, kKeys,
    kDrop2, 4, 50, 72, 60, 0, 0.35, 0.85, 0.2, 0.35, 0.5, 4},
   {"Pop Pianist", "eighth-note pulse, open triads", "pop, ballad, rock", kPiano, kKeys,
    kSpread, 4, 48, 76, 62, 0, 0.5, 0.25, 0.15, 0.6, 0.25, 0},
   {"Rhythm Guitar", "sixteenth scratch, three-note grips", "funk, disco, afrobeat", kGuitar, kGuitarKind,
    kClose, 3, 55, 76, 66, 2, 0.85, 0.5, 0.05, 0.15, 0.3, 0},
   {"Skank Guitar", "off-beat chops, short and high", "reggae, ska", kGuitar, kGuitarKind,
    kClose, 3, 57, 79, 67, 2, 0.4, 0.2, 0.0, 0.05, 0.2, 0},
   {"Strummer", "down-up strums across six strings", "rock, country, pop", kGuitar, kGuitarKind,
    kSpread, 5, 40, 76, 58, 3, 0.65, 0.2, 0.05, 0.5, 0.3, 0},
   {"Montuno", "Latin piano figures in octaves", "salsa, Latin", kPiano, kKeys,
    kClose, 4, 55, 84, 68, 0, 0.8, 0.3, 0.0, 0.15, 0.15, 0},
   {"House Stabs", "one chord shape, rhythmic and on the grid", "house, deep house, disco", kSynth, kKeys,
    kClose, 4, 52, 76, 63, 0, 0.6, 0.65, 0.0, 0.2, 0.0, 0},
   {"Pad Player", "whole bars and slow swells", "ballad, ambient, intros", kPad, kKeys,
    kDrop24, 5, 45, 79, 60, 1, 0.05, 0.7, 0.0, 1.0, 0.2, 0},
   // Eno's ambient (Music for Airports' liner notes keep "doubt and uncertainty"; its "1/2" an
   // add9 chord with no third) and Budd's "soft pedal" piano: slow, sustained, open and ambiguous.
   {"Ambient Drifter", "long held chords, wide, open and barely moving", "ambient, film, intros", kPad, kKeys,
    kQuartal, 5, 43, 86, 62, 1, 0.0, 0.8, 0.0, 1.0, 0.45, 0},
};

} // namespace jcomp
