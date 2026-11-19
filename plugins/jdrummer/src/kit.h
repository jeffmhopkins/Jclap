#pragma once

// JKit's 16 pads, as JDrummer names them: the same four layouts JKit makes (KIT in its Generate
// tab), so a lane reads as the sound JKit has on that pad. Pad 1 is note 36 (C2), pad 16 note 51.

namespace jdrummer {

enum Kit { kDrumKit, kPercussionKit, kFxKit, kAnimalKit, kKitCount };
inline const char *const kKitNames[kKitCount] = {"Drum Kit", "Percussion", "FX", "Animals"};

// What a pad does in a groove; the panel colours it, and energy brings roles in in this order.
enum Role { kKick, kSnare, kHat, kTom, kPerc, kFx, kRoleCount };

// The drum kit's pads, by index, as the grooves name them.
constexpr int K = 0; // Kick
constexpr int RIM = 1; // Rim
constexpr int SN = 2; // Snare
constexpr int CL = 3; // Clap
constexpr int SN2 = 4; // Snare 2
constexpr int LT = 5; // Low Tom
constexpr int CH = 6; // Closed Hat
constexpr int MT = 7; // Mid Tom
constexpr int PH = 8; // Pedal Hat
constexpr int HT = 9; // High Tom
constexpr int OH = 10; // Open Hat
constexpr int PC = 11; // Perc (ride, bell, cowbell)
constexpr int SH = 12; // Shaker
constexpr int CR = 13; // Crash
constexpr int K2 = 14; // Kick 2 (808)
constexpr int FX = 15; // FX

// The percussion kit's pads, for the hand percussion grooves and the percussionist.
constexpr int CAJON_BASS = 0;
constexpr int DJEMBE_SLAP = 1;
constexpr int CAJON_SLAP = 2;
constexpr int PALMAS = 3;
constexpr int DJEMBE_BASS = 4;
constexpr int LOW_CONGA = 5;
constexpr int SHAKER = 6;
constexpr int HIGH_CONGA = 7;
constexpr int CONGA_SLAP = 8;
constexpr int LOW_BONGO = 9;
constexpr int HIGH_BONGO = 10;
constexpr int DJEMBE_TONE = 11;
constexpr int DARBUKA_DUM = 12;
constexpr int DARBUKA_TEK = 13;
constexpr int TABLA = 14;
constexpr int FRAME_DRUM = 15;

struct PadInfo {
   const char *name;
   Role role;
};

inline const PadInfo kPadInfo[kKitCount][16] = {
   {{"Kick", kKick}, {"Rim", kPerc}, {"Snare", kSnare}, {"Clap", kSnare}, {"Snare 2", kSnare}, {"Low Tom", kTom},
    {"Closed Hat", kHat}, {"Mid Tom", kTom}, {"Pedal Hat", kHat}, {"High Tom", kTom}, {"Open Hat", kHat},
    {"Perc", kPerc}, {"Shaker", kPerc}, {"Crash", kHat}, {"Kick 2", kKick}, {"FX", kFx}},
   {{"Cajon Bass", kKick}, {"Djembe Slap", kPerc}, {"Cajon Slap", kSnare}, {"Palmas", kSnare},
    {"Djembe Bass", kKick}, {"Low Conga", kTom}, {"Shaker", kHat}, {"High Conga", kTom}, {"Conga Slap", kPerc},
    {"Low Bongo", kTom}, {"High Bongo", kPerc}, {"Djembe Tone", kTom}, {"Darbuka Dum", kKick},
    {"Darbuka Tek", kPerc}, {"Tabla", kPerc}, {"Frame Drum", kTom}},
   {{"Sub Boom", kKick}, {"Blip", kPerc}, {"Zap", kSnare}, {"Glitch", kSnare}, {"Noise Hit", kSnare},
    {"Bloop", kTom}, {"Tick", kHat}, {"Boing", kTom}, {"Chirp", kPerc}, {"Pew", kTom}, {"Sweep", kHat},
    {"Coin", kPerc}, {"Wub", kPerc}, {"Riser", kFx}, {"Impact", kKick}, {"Reverse", kFx}},
   {{"Bullfrog", kKick}, {"Woodpecker", kPerc}, {"Dog", kSnare}, {"Duck", kSnare}, {"Cat", kSnare},
    {"Cow", kTom}, {"Cricket", kHat}, {"Goat", kTom}, {"Bird", kPerc}, {"Sheep", kTom}, {"Rattlesnake", kHat},
    {"Monkey", kPerc}, {"Horse", kPerc}, {"Elephant", kFx}, {"Lion", kKick}, {"Wolf", kFx}},
};

inline const char *padName(int kit, int pad) {
   return kPadInfo[kit < 0 || kit >= kKitCount ? 0 : kit][pad & 15].name;
}
inline Role padRole(int kit, int pad) { return kPadInfo[kit < 0 || kit >= kKitCount ? 0 : kit][pad & 15].role; }

// The hats a held MUTE HATS silences, and the kicks MUTE KICK does, on every kit.
inline bool isHatPad(int pad) { return pad == CH || pad == PH || pad == OH; }
inline bool isKickPad(int pad) { return pad == K || pad == K2; }

} // namespace jdrummer
