#pragma once

// JBreath's parameters. Ids are indexes into kParams, so keep them in order; once released, only
// append so saved projects keep working.

#include <jclap/core/helpers.h>

#include "fx.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jbreath {

enum ParamId : clap_id {
   // The instrument, and how it's blown.
   kModel,
   kBreath,
   kEmbouchure,
   kNoise,
   kEdge,
   // How a note starts and ends.
   kKick,
   kAttack,
   kRelease,
   // The bore.
   kTune,
   kFine,
   kBright,
   kGlide,
   // Vibrato.
   kVibRate,
   kVibDepth,
   kVibPitch,
   kVibDelay,
   // Voice and expression.
   kVoiceMode,
   kBreathSource,
   kCurve,
   kSlide,
   kBendRange,
   kLevel,
   // Added after the first build: an input to blow from (a CV, an LFO, automation).
   kBreathIn,
   // Modulation (added with MAP): two LFOs, the Swell envelope, four buses and a pool of routes.
   kLfo1Rate,
   kLfo1Shape,
   kLfo1Sync,
   kLfo2Rate,
   kLfo2Shape,
   kLfo2Sync,
   kSwellShape,
   kSwellTime,
   kSwellLoop,
   kBusBase,
   kRouteBase = kBusBase + 4 * 5,
   // The rest of the exciter (added with the full panel): the reed's or lips' stiffness, how far
   // the valve stands open, and a growl in the throat.
   kStiffness = kRouteBase + 32 * 3,
   kAperture,
   kGrowl,
   // How the player tongues and where their vibrato comes from, and how human they are.
   kTongueType,
   kVibType,
   kHumanize,
   // HYBRID: a second instrument blown with the same breath, layered with the first.
   kHybridModel,
   kHybridMix,
   kHybridOctave,
   kHybridDetune,
   kHybridBreath,
   // The rest of the bore: its body, losses, holes and walls.
   kLength,
   kLoss,
   kHoleCutoff,
   kCone,
   kRadiate,
   kBrass,
   kBarrel,
   kWall,
   kDetune,
   kHoles,
   kTube,
   // The voice card: stereo spread, air around the instrument, the room, master and limiter.
   kSpread,
   kAir,
   kRoom,
   kRoomSize,
   kRoomDecay,
   kRoomDamp,
   kRoomPreDelay,
   kMaster,
   kLimiter,
   // The AIR view's look. Kept with the project, not with presets.
   kLook,
   kPalette,
   kTurbulence,
   kRise,
   kSwirl,
   kTail,
   kLine,
   kGlow,
   // Added after the first release: flutter tongue on any style, note pan, unison, the HYBRID
   // tab's subtractive voice's filter, and where the AIR view's loops sit and how big they are.
   kFlutter,
   kFlutterRate,
   kPan,
   kUnison,
   kUnisonDetune,
   kHybridCutoff,
   kHybridResonance,
   kHybridFilterBreath,
   kLoopX,
   kLoopY,
   kLoopSize,
   kLoopSizeMin,
   // Added after Release 24: the HYBRID voice's own envelope, so it can be a pad under the
   // instrument: attack, decay, sustain and release, how much its level still follows the breath,
   // and how far the envelope opens its lowpass.
   kHybridAttack,
   kHybridDecay,
   kHybridSustain,
   kHybridRelease,
   kHybridFollow,
   kHybridFilterEnv,
   // The FX: the reverb (the Room params) switched on and off, and a delay.
   kReverbOn,
   kDelayOn,
   kDelayMix,
   kDelayTime,
   kDelaySync,
   kDelayFeedback,
   kDelayTone,
   kDelaySpread,
   // A mute in the bell (or over the end of any instrument): which kind, and how far it's in. Its
   // amount is a route destination, so slide or pressure can work a plunger.
   kMute,
   kMuteAmount,
   kParamCount
};
// The AIR view's look: the panel's own, kept with the project but not with presets.
inline bool isLookParam(clap_id id) { return (id >= kLook && id <= kGlow) || (id >= kLoopX && id <= kLoopSizeMin); }
// The routes' params end where the panel's later params begin.
constexpr clap_id kRouteEnd = kStiffness;
// Presets cover every param but the look's (isLookParam), which loading one leaves as it is.
constexpr clap_id kPresetParamCount = kParamCount;

constexpr int kLfoCount = 2, kBusCount = 4, kRouteCount = 32;
constexpr int kBusStride = 5, kRouteStride = 3;
enum BusParam : clap_id { kBusAmount, kBusMode, kBusCurve, kBusPolarity, kBusLag };
enum RouteParam : clap_id { kRouteSource, kRouteDestination, kRouteAmount };
constexpr clap_id busParam(int bus, BusParam p) { return kBusBase + bus * kBusStride + p; }
constexpr clap_id routeParam(int route, RouteParam p) { return kRouteBase + route * kRouteStride + p; }
constexpr clap_id lfoParam(int lfo, int p) { return (lfo ? kLfo2Rate : kLfo1Rate) + p; }

// What a route can come from. The header's five are the player's hands (MPE), the next six are
// read off each voice's own breath loop and sound, then the generators and the buses.
enum ModSource {
   kSrcOff,
   kSrcStrike,   // the note's velocity
   kSrcPress,    // its pressure
   kSrcGlide,    // its pitch bend, -1..1 over the bend range
   kSrcSlide,    // its slide (CC 74)
   kSrcLift,     // its release velocity
   kSrcLoop,     // how big its breath loop is: the breath blowing
   kSrcKnot,     // how knotted: the brightness of its sound
   kSrcFold,     // how far past where the instrument overblows
   kSrcRegister, // how high it plays in the instrument's range
   kSrcVapor,    // how much breath noise rides on it
   kSrcDraft,    // a slow draught of air: a wandering -1..1 of its own
   kSrcSwell,
   kSrcLfo1,
   kSrcLfo2,
   kSrcKey,      // the note, -1..1 around middle C
   kSrcRandom,   // a fresh -1..1 for each note
   kSrcBus1,
   kModSourceCount = kSrcBus1 + kBusCount
};
inline const char *const kModSourceNames[kModSourceCount] = {
   "Off", "Strike", "Press", "Glide", "Slide", "Lift", "Loop", "Knot", "Fold", "Register", "Vapor", "Draft",
   "Swell", "LFO 1", "LFO 2", "Key", "Random", "Bus 1", "Bus 2", "Bus 3", "Bus 4"};
inline const unsigned kModSourceColors[kModSourceCount] = {
   0xff6b7280, 0xfff5d76e, 0xffff5fc8, 0xffa78bfa, 0xff7ee6a0, 0xff6fb3a0, 0xffffb547, 0xff5cc8ff, 0xff9b7bff,
   0xffff7a6b, 0xffc9d3e6, 0xff5fd0b0, 0xff7fd3ff, 0xffffa64d, 0xffffd166, 0xffb0b8c8, 0xffe0e0e0,
   0xff8fe8a8, 0xff8fe8a8, 0xff8fe8a8, 0xff8fe8a8};
// Sources that swing both ways (-1..1); the rest run 0..1.
inline bool bipolarSource(int s) {
   return s == kSrcGlide || s == kSrcDraft || s == kSrcLfo1 || s == kSrcLfo2 || s == kSrcKey || s == kSrcRandom ||
          s >= kSrcBus1;
}

// What a route can move: these params, by index. A bus's Amount knob is where its inputs go.
inline constexpr clap_id kModDestinations[] = {
   kBreath, kEmbouchure, kNoise, kEdge, kKick, kAttack, kRelease, kTune, kFine, kGlide, kVibRate, kVibDepth,
   kVibPitch, kVibDelay, kCurve, kLevel, kLfo1Rate, kLfo2Rate, kSwellTime, kStiffness, kAperture, kGrowl,
   kHumanize, kHybridMix, kHybridDetune, kLoss, kHoleCutoff, kCone, kBrass, kDetune, kAir,
   busParam(0, kBusAmount), busParam(1, kBusAmount), busParam(2, kBusAmount), busParam(3, kBusAmount),
   // Added after the first release, after the buses so saved routes keep their destinations.
   kFlutter, kFlutterRate, kPan, kUnisonDetune, kHybridCutoff, kHybridResonance, kHybridFilterBreath,
   // Added after Release 24.
   kMuteAmount};
constexpr int kModDestCount = static_cast<int>(sizeof(kModDestinations) / sizeof(kModDestinations[0]));
constexpr int kFirstBusDest = 31;
constexpr bool isBusDest(int d) { return d >= kFirstBusDest && d < kFirstBusDest + kBusCount; }
static_assert(kModDestinations[kFirstBusDest] == busParam(0, kBusAmount), "the buses' destinations moved");
constexpr int destinationOf(clap_id id) {
   for (int i = 0; i < kModDestCount; ++i)
      if (kModDestinations[i] == id)
         return i;
   return -1;
}
// A destination's index, worked out when compiling.
template <clap_id Id>
constexpr int kDest = destinationOf(Id);

enum LfoShape { kLfoSine, kLfoTriangle, kLfoSaw, kLfoSquare, kLfoSampleHold, kLfoSmooth };
inline const char *const kLfoShapeNames[] = {"Sine", "Triangle", "Saw", "Square", "S&H", "Smooth"};
inline const char *const kLfoSyncNames[] = {"Note", "Free"};
// The Swell's contours, each over its Time: a rise and fall, a rise to stay, a fall, two humps, a
// quick bloom that settles, and a slow breath in and out.
enum SwellShape { kSwellArch, kSwellRise, kSwellFall, kSwellWave, kSwellBloom, kSwellTide };
inline const char *const kSwellShapeNames[] = {"Arch", "Rise", "Fall", "Waves", "Bloom", "Tide"};
inline const char *const kSwellLoopNames[] = {"Once", "Loop"};
enum BusMode { kBusSum, kBusMultiply, kBusMin, kBusMax };
inline const char *const kBusModeNames[] = {"Sum", "Multiply", "Min", "Max"};
inline const char *const kBusCurveNames[] = {"Linear", "Exp", "Log", "S-Curve", "Absolute"};
inline const char *const kBusPolarityNames[] = {"As Is", "Unipolar", "Bipolar", "Invert"};


constexpr auto kGlobalMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kMod = kGlobalMod | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_MODULATABLE_PER_KEY |
                      CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL | CLAP_PARAM_IS_MODULATABLE_PER_PORT;
constexpr auto kStepped = kMod | CLAP_PARAM_IS_STEPPED;
// The instrument, voice mode and breath source are the whole plugin's: no per-voice meaning.
constexpr auto kGlobalChoice = kGlobalMod | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

// The look's params are the panel's, not the sound's: not for the host to automate.
constexpr auto kLookFlags = static_cast<clap_param_info_flags>(CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM);
constexpr auto kLookKnob = static_cast<clap_param_info_flags>(0);

enum VoiceMode { kPoly, kMono, kLegato };
// How a note is tongued: a soft "da", a hard "ta", every note tongued even when slurred
// ("ta-ka"), a rolled R through the note, or no tongue at all (a breath attack).
enum TongueType { kTongueSoft, kTongueHard, kTongueDouble, kTongueFlutter, kTongueBreath };
inline const char *const kTongueTypeNames[] = {"Soft", "Hard", "Double", "Flutter", "Breath"};
// Where the vibrato comes from: the diaphragm (breath), the jaw (embouchure, so pitch and colour),
// both, or the lips (pitch alone).
enum VibType { kVibBreath, kVibJaw, kVibBoth, kVibLip };
inline const char *const kVibTypeNames[] = {"Breath", "Jaw", "Breath+Jaw", "Lip"};
// The HYBRID tab's voice: a plain oscillator, filtered, blown with the same breath.
inline const char *const kHybridWaveNames[] = {"Sine", "Triangle", "Saw", "Square", "Pulse"};
// What places a loop in the AIR view, along each axis, and what sets its size.
enum LoopSource { kLoopPitch, kLoopStrike, kLoopPress, kLoopSlide, kLoopBreath, kLoopRandom, kLoopCentre };
inline const char *const kLoopSourceNames[] = {"Pitch", "Strike", "Press", "Slide", "Breath", "Random", "Centre"};
inline const char *const kHolesNames[] = {"Closed", "Lattice 3", "Lattice 5", "Lattice 7", "Lattice 9", "Open"};
inline const char *const kTubeNames[] = {"Grenadilla", "Boxwood", "Maple", "Bamboo", "Brass", "Silver", "Resin", "Glass"};
inline const char *const kLimiterNames[] = {"Off", "Soft", "Hard"};
inline const char *const kOffOnNames[] = {"Off", "On"};
inline const char *const kMuteNames[] = {"Open", "Straight", "Cup", "Harmon", "Plunger", "Bucket"};
inline const char *const kLookNames[] = {"Smoke", "Ink"};
inline const char *const kPaletteNames[] = {"Dusk", "Ember", "Frost", "Moss", "Mono"};
inline const char *const kHybridOctaveNames[] = {"-2 oct", "-1 oct", "0 oct", "+1 oct", "+2 oct"};
enum BreathSource { kSourceAuto, kSourcePressure, kSourceVelocity, kSourceInput, kSourceMix };

namespace detail {
inline std::array<jclap::core::ParamDef, kParamCount> buildParams() {
   std::array<jclap::core::ParamDef, kParamCount> p{};
   const jclap::core::ParamDef fixed[] = {
   {kModel, "Instrument", "Exciter", 0.0, 5.0, 1.0, kGlobalChoice},
   {kBreath, "Breath", "Exciter", 0.25, 1.5, 1.0, kMod},
   {kEmbouchure, "Embouchure", "Exciter", 0.0, 1.0, 0.5, kMod},
   {kNoise, "Noise", "Exciter", 0.0, 2.0, 1.0, kMod},
   {kEdge, "Bite", "Exciter", 0.0, 2.0, 1.0, kMod},
   {kKick, "Kick", "Articulation", 0.0, 1.0, 0.25, kMod},
   {kAttack, "Attack", "Articulation", 1.0, 400.0, 25.0, kMod},
   {kRelease, "Release", "Articulation", 5.0, 2000.0, 90.0, kMod},
   {kTune, "Transpose", "Voice", -24.0, 24.0, 0.0, kStepped},
   {kFine, "Tune", "Bore", -100.0, 100.0, 0.0, kMod},
   {kBright, "Tone", "Room", -12.0, 12.0, 0.0, kGlobalMod},
   {kGlide, "Glide", "Bore", 0.0, 1000.0, 0.0, kMod},
   {kVibRate, "Vibrato Rate", "Vibrato", 2.0, 9.0, 5.1, kMod},
   {kVibDepth, "Vibrato Depth", "Vibrato", 0.0, 0.3, 0.05, kMod},
   {kVibPitch, "Vibrato Pitch", "Vibrato", 0.0, 50.0, 0.0, kMod},
   {kVibDelay, "Vibrato Delay", "Vibrato", 0.0, 2000.0, 350.0, kMod},
   {kVoiceMode, "Voice Mode", "Voice", 0.0, 2.0, 0.0, kGlobalChoice},
   {kBreathSource, "Breath Source", "Voice", 0.0, 4.0, 0.0, kGlobalChoice},
   {kCurve, "Press Curve", "Voice", 0.3, 3.0, 1.0, kMod},
   {kSlide, "Slide", "Voice", 0.0, 1.0, 0.5, kMod},
   {kBendRange, "MPE Bend Range", "Voice", 1.0, 96.0, 48.0, kGlobalMod | CLAP_PARAM_IS_STEPPED},
   {kLevel, "Level", "Output", -60.0, 12.0, 0.0, kMod},
   {kBreathIn, "Breath In", "Voice", 0.0, 1.0, 0.0, kMod},
      {kLfo1Rate, "LFO 1 Rate", "LFO", 0.02, 20.0, 1.0, kMod},
      {kLfo1Shape, "LFO 1 Shape", "LFO", 0.0, 5.0, 0.0, kGlobalChoice},
      {kLfo1Sync, "LFO 1 Sync", "LFO", 0.0, 1.0, 0.0, kGlobalChoice},
      {kLfo2Rate, "LFO 2 Rate", "LFO", 0.02, 20.0, 0.25, kMod},
      {kLfo2Shape, "LFO 2 Shape", "LFO", 0.0, 5.0, 5.0, kGlobalChoice},
      {kLfo2Sync, "LFO 2 Sync", "LFO", 0.0, 1.0, 1.0, kGlobalChoice},
      {kSwellShape, "Swell Shape", "Swell", 0.0, 5.0, 0.0, kGlobalChoice},
      {kSwellTime, "Swell Time", "Swell", 0.05, 30.0, 3.0, kMod},
      {kSwellLoop, "Swell Loop", "Swell", 0.0, 1.0, 0.0, kGlobalChoice},
      {kStiffness, "Stiffness", "Exciter", 0.0, 1.0, 0.5, kMod},
      {kAperture, "Aperture", "Exciter", 0.0, 1.0, 0.5, kMod},
      {kGrowl, "Growl", "Exciter", 0.0, 1.0, 0.0, kMod},
      {kTongueType, "Tongue", "Articulation", 0.0, 4.0, 1.0, kGlobalChoice},
      {kVibType, "Vibrato Type", "Vibrato", 0.0, 3.0, 0.0, kGlobalChoice},
      {kHumanize, "Humanize", "Articulation", 0.0, 1.0, 0.0, kMod},
      {kHybridModel, "Hybrid Wave", "Hybrid", 0.0, 4.0, 2.0, kGlobalChoice},
      {kHybridMix, "Hybrid Mix", "Hybrid", 0.0, 1.0, 0.0, kMod},
      {kHybridOctave, "Hybrid Octave", "Hybrid", -2.0, 2.0, 0.0, kGlobalMod | CLAP_PARAM_IS_STEPPED},
      {kHybridDetune, "Hybrid Detune", "Hybrid", -50.0, 50.0, 0.0, kMod},
      {kHybridBreath, "Hybrid Breath", "Hybrid", 0.5, 1.5, 1.0, kMod},
      {kLength, "Length", "Bore", 0.0, 1.0, 0.65, kGlobalMod},
      {kLoss, "Loss", "Bore", 0.0, 0.5, 0.04, kMod},
      {kHoleCutoff, "Hole Cutoff", "Bore", 500.0, 4000.0, 1300.0, kMod},
      {kCone, "Cone", "Bore", 0.0, 1.0, 0.5, kMod},
      {kRadiate, "Radiate", "Bore", 0.0, 1.0, 0.66, kGlobalMod},
      {kBrass, "Brass", "Bore", 0.0, 1.0, 0.5, kMod},
      {kBarrel, "Barrel", "Bore", 0.0, 1.0, 0.4, kGlobalMod},
      {kWall, "Wall", "Bore", 0.0, 1.0, 0.5, kGlobalMod},
      {kDetune, "Detune", "Bore", 0.0, 25.0, 0.0, kMod},
      {kHoles, "Holes", "Bore", 0.0, 5.0, 3.0, kGlobalChoice},
      {kTube, "Tube", "Bore", 0.0, 7.0, 0.0, kGlobalChoice},
      {kSpread, "Spread", "Output", -1.0, 1.0, 0.5, kGlobalMod},
      {kAir, "Air", "Output", 0.0, 1.0, 0.0, kMod},
      {kRoom, "Room", "Room", 0.0, 1.0, 0.15, kGlobalMod},
      {kRoomSize, "Room Size", "Room", 0.0, 1.0, 0.5, kGlobalMod},
      {kRoomDecay, "Room Decay", "Room", 0.2, 10.0, 1.6, kGlobalMod},
      {kRoomDamp, "Room Damping", "Room", 0.0, 1.0, 0.5, kGlobalMod},
      {kRoomPreDelay, "Room Pre-Delay", "Room", 0.0, 100.0, 12.0, kGlobalMod},
      {kMaster, "Master", "Output", -36.0, 6.0, 0.0, kGlobalMod},
      {kLimiter, "Limiter", "Output", 0.0, 2.0, 1.0, kGlobalChoice},
      {kLook, "Look", "View", 0.0, 1.0, 0.0, kLookFlags},
      {kPalette, "Palette", "View", 0.0, 4.0, 0.0, kLookFlags},
      {kTurbulence, "Turbulence", "View", 0.0, 1.0, 0.42, kLookKnob},
      {kRise, "Rise", "View", 0.0, 1.0, 0.55, kLookKnob},
      {kSwirl, "Swirl", "View", 0.0, 1.0, 0.38, kLookKnob},
      {kTail, "Tail", "View", 0.5, 8.0, 4.0, kLookKnob},
      {kLine, "Line", "View", 0.0, 1.0, 0.4, kLookKnob},
      {kGlow, "Glow", "View", 0.0, 1.0, 0.5, kLookKnob},
      {kFlutter, "Flutter", "Articulation", 0.0, 1.0, 0.0, kMod},
      {kFlutterRate, "Flutter Rate", "Articulation", 10.0, 40.0, 24.0, kMod},
      {kPan, "Pan", "Output", -1.0, 1.0, 0.0, kMod},
      {kUnison, "Unison", "Voice", 1.0, 4.0, 1.0, kGlobalMod | CLAP_PARAM_IS_STEPPED},
      {kUnisonDetune, "Unison Detune", "Voice", 0.0, 50.0, 12.0, kMod},
      {kHybridCutoff, "Hybrid Cutoff", "Hybrid", 80.0, 12000.0, 1800.0, kMod},
      {kHybridResonance, "Hybrid Resonance", "Hybrid", 0.0, 1.0, 0.2, kMod},
      {kHybridFilterBreath, "Hybrid Filter Breath", "Hybrid", 0.0, 1.0, 0.6, kMod},
      {kLoopX, "Loop X", "View", 0.0, 6.0, 0.0, kLookFlags},
      {kLoopY, "Loop Y", "View", 0.0, 6.0, 4.0, kLookFlags},
      {kLoopSize, "Loop Size", "View", 0.0, 6.0, 4.0, kLookFlags},
      {kLoopSizeMin, "Loop Size Min", "View", 0.1, 1.0, 0.25, kLookKnob},
      // Times are 0..1 knobs, shown in ms on a log scale (hybridEnvMs()).
      {kHybridAttack, "Hybrid Attack", "Hybrid", 0.0, 1.0, 0.18, kMod},
      {kHybridDecay, "Hybrid Decay", "Hybrid", 0.0, 1.0, 0.6, kMod},
      {kHybridSustain, "Hybrid Sustain", "Hybrid", 0.0, 1.0, 1.0, kMod},
      {kHybridRelease, "Hybrid Release", "Hybrid", 0.0, 1.0, 0.45, kMod},
      {kHybridFollow, "Hybrid Follow", "Hybrid", 0.0, 1.0, 1.0, kMod},
      {kHybridFilterEnv, "Hybrid Filter Env", "Hybrid", -1.0, 1.0, 0.0, kMod},
      {kReverbOn, "Reverb", "Reverb", 0.0, 1.0, 1.0, kGlobalChoice},
      {kDelayOn, "Delay", "Delay", 0.0, 1.0, 0.0, kGlobalChoice},
      {kDelayMix, "Delay Mix", "Delay", 0.0, 1.0, 0.3, kGlobalMod},
      // 0..1 to 10 ms .. 2 s on a log scale (delayTimeMs()); 0.684 is 375 ms.
      {kDelayTime, "Delay Time", "Delay", 0.0, 1.0, 0.684, kGlobalMod},
      {kDelaySync, "Delay Sync", "Delay", 0.0, 13.0, 0.0, kGlobalChoice},
      {kDelayFeedback, "Delay Feedback", "Delay", 0.0, 0.95, 0.4, kGlobalMod},
      {kDelayTone, "Delay Tone", "Delay", 0.0, 1.0, 0.6, kGlobalMod},
      {kDelaySpread, "Delay Spread", "Delay", 0.0, 1.0, 0.5, kGlobalMod},
      {kMute, "Mute", "Bore", 0.0, 5.0, 0.0, kGlobalChoice},
      {kMuteAmount, "Mute Amount", "Bore", 0.0, 1.0, 1.0, kMod},
   };
   for (const auto &d : fixed)
      p[d.id] = d;
   // Generated names live as long as the program.
   static std::vector<std::string> names;
   names.reserve(kBusCount * kBusStride + kRouteCount * kRouteStride);
   const auto name = [](std::string n) { names.push_back(std::move(n)); return names.back().c_str(); };
   constexpr auto kNone = static_cast<clap_param_info_flags>(0);
   for (int b = 0; b < kBusCount; ++b) {
      const std::string n = "Bus " + std::to_string(b + 1);
      p[busParam(b, kBusAmount)] = {busParam(b, kBusAmount), name(n + " Amount"), "Bus", -1.0, 1.0, 0.0, kMod};
      p[busParam(b, kBusMode)] = {busParam(b, kBusMode), name(n + " Mode"), "Bus", 0.0, 3.0, 0.0, kGlobalChoice};
      p[busParam(b, kBusCurve)] = {busParam(b, kBusCurve), name(n + " Curve"), "Bus", 0.0, 4.0, 0.0, kGlobalChoice};
      p[busParam(b, kBusPolarity)] = {busParam(b, kBusPolarity), name(n + " Polarity"), "Bus", 0.0, 3.0, 0.0, kGlobalChoice};
      p[busParam(b, kBusLag)] = {busParam(b, kBusLag), name(n + " Lag"), "Bus", 0.0, 2000.0, 0.0, kMod};
   }
   for (int r = 0; r < kRouteCount; ++r) {
      const std::string n = "Route " + std::to_string(r + 1);
      // Which source and destination aren't things to automate; the amount is.
      p[routeParam(r, kRouteSource)] = {routeParam(r, kRouteSource), name(n + " Source"), "Routes", 0.0,
                                        static_cast<double>(kModSourceCount - 1), 0.0, kNone | CLAP_PARAM_IS_STEPPED};
      p[routeParam(r, kRouteDestination)] = {routeParam(r, kRouteDestination), name(n + " Destination"), "Routes", 0.0,
                                             static_cast<double>(kModDestCount - 1), 0.0, kNone | CLAP_PARAM_IS_STEPPED};
      p[routeParam(r, kRouteAmount)] = {routeParam(r, kRouteAmount), name(n + " Amount"), "Routes", -1.0, 1.0, 0.0, kGlobalMod};
   }
   return p;
}
} // namespace detail

inline const std::array<jclap::core::ParamDef, kParamCount> kParams = detail::buildParams();

inline const char *const kModelNames[] = {"Flute", "Clarinet", "Sax", "Oboe", "Trumpet", "Alien"};
inline const char *const kVoiceModeNames[] = {"Poly", "Mono", "Legato"};
inline const char *const kBreathSourceNames[] = {"Auto", "Pressure", "Velocity", "Input", "Mix"};

inline const char *const *choiceNames(clap_id id, int &count) {
   switch (id) {
   case kModel:
      count = 6;
      return kModelNames;
   case kVoiceMode:
      count = 3;
      return kVoiceModeNames;
   case kBreathSource:
      count = 5;
      return kBreathSourceNames;
   case kLfo1Shape:
   case kLfo2Shape:
      count = 6;
      return kLfoShapeNames;
   case kLfo1Sync:
   case kLfo2Sync:
      count = 2;
      return kLfoSyncNames;
   case kSwellShape:
      count = 6;
      return kSwellShapeNames;
   case kSwellLoop:
      count = 2;
      return kSwellLoopNames;
   case kHybridModel:
      count = 5;
      return kHybridWaveNames;
   case kLoopX:
   case kLoopY:
   case kLoopSize:
      count = 7;
      return kLoopSourceNames;
   case kTongueType:
      count = 5;
      return kTongueTypeNames;
   case kVibType:
      count = 4;
      return kVibTypeNames;
   case kHoles:
      count = 6;
      return kHolesNames;
   case kTube:
      count = 8;
      return kTubeNames;
   case kLimiter:
      count = 3;
      return kLimiterNames;
   case kReverbOn:
   case kDelayOn:
      count = 2;
      return kOffOnNames;
   case kDelaySync:
      count = kDelaySyncCount;
      return kDelaySyncNames;
   case kMute:
      count = 6;
      return kMuteNames;
   case kLook:
      count = 2;
      return kLookNames;
   case kPalette:
      count = 5;
      return kPaletteNames;
   default:
      if (id >= kBusBase && id < kRouteBase) {
         switch ((id - kBusBase) % kBusStride) {
         case kBusMode:
            count = 4;
            return kBusModeNames;
         case kBusCurve:
            count = 5;
            return kBusCurveNames;
         case kBusPolarity:
            count = 4;
            return kBusPolarityNames;
         default:
            break;
         }
      }
      if (id >= kRouteBase && id < kRouteEnd && (id - kRouteBase) % kRouteStride == kRouteSource) {
         count = kModSourceCount;
         return kModSourceNames;
      }
      count = 0;
      return nullptr;
   }
}

inline bool isPercent(clap_id id) {
   switch (id) {
   case kBreath:
   case kEmbouchure:
   case kNoise:
   case kEdge:
   case kKick:
   case kVibDepth:
   case kSlide:
   case kBreathIn:
   case kAperture:
   case kGrowl:
   case kHumanize:
   case kHybridMix:
   case kHybridBreath:
   case kLoss:
   case kCone:
   case kRadiate:
   case kBrass:
   case kBarrel:
   case kWall:
   case kSpread:
   case kAir:
   case kRoom:
   case kRoomSize:
   case kRoomDamp:
   case kTurbulence:
   case kRise:
   case kSwirl:
   case kLine:
   case kGlow:
   case kFlutter:
   case kPan:
   case kHybridResonance:
   case kHybridFilterBreath:
   case kLoopSizeMin:
   case kHybridSustain:
   case kHybridFollow:
   case kHybridFilterEnv:
   case kMuteAmount:
   case kDelayMix:
   case kDelayFeedback:
   case kDelayTone:
   case kDelaySpread:
      return true;
   default:
      if (id >= kBusBase && id < kRouteBase)
         return (id - kBusBase) % kBusStride == kBusAmount;
      if (id >= kRouteBase && id < kRouteEnd)
         return (id - kRouteBase) % kRouteStride == kRouteAmount;
      return false;
   }
}

// The Length knob's body, in metres: 0.15 to 1.5, evenly in octaves.
// The HYBRID envelope's times: a 0..1 knob to 1 ms .. 10 s on a log scale.
inline double hybridEnvMs(double v) { return std::pow(10.0, 4.0 * std::clamp(v, 0.0, 1.0)); }
inline double bodyLength(double v) { return 0.15 * std::pow(10.0, std::clamp(v, 0.0, 1.0)); }

// What the Wall knob's value is like, for the panel.
inline const char *wallName(double v) {
   static const char *const walls[] = {"Metal", "Resin", "Wood", "Cane", "Bone"};
   return walls[std::clamp(static_cast<int>(v * 4.999), 0, 4)];
}

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      const int i = static_cast<int>(value + 0.5);
      std::snprintf(out, size, "%s", names[i < 0 ? 0 : i >= count ? count - 1 : i]);
      return;
   }
   if (isPercent(id)) {
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      return;
   }
   if (id >= kRouteBase && id < kRouteEnd && (id - kRouteBase) % kRouteStride == kRouteDestination) {
      const int d = std::clamp(static_cast<int>(value + 0.5), 0, kModDestCount - 1);
      std::snprintf(out, size, "%s", kParams[kModDestinations[d]].name);
      return;
   }
   if (id >= kBusBase && id < kRouteBase && (id - kBusBase) % kBusStride == kBusLag) {
      std::snprintf(out, size, "%.0f ms", value);
      return;
   }
   switch (id) {
   case kLfo1Rate:
   case kLfo2Rate:
      std::snprintf(out, size, value < 1.0 ? "%.2f Hz" : "%.1f Hz", value);
      break;
   case kSwellTime:
      std::snprintf(out, size, value < 10.0 ? "%.2f s" : "%.1f s", value);
      break;
   case kAttack:
   case kRelease:
   case kGlide:
   case kVibDelay:
   case kRoomPreDelay:
      std::snprintf(out, size, "%.0f ms", value);
      break;
   case kStiffness:
      std::snprintf(out, size, "%.2f", value);
      break;
   case kHybridOctave:
      std::snprintf(out, size, "%+d oct", static_cast<int>(std::lround(value)));
      break;
   case kHybridDetune:
   case kDetune:
      std::snprintf(out, size, "%+.0f ct", value);
      break;
   case kLength:
      std::snprintf(out, size, "%.2f m", bodyLength(value));
      break;
   case kDelayTime:
      std::snprintf(out, size, delayTimeMs(value) < 1000.0 ? "%.0f ms" : "%.2f s",
                    delayTimeMs(value) < 1000.0 ? delayTimeMs(value) : delayTimeMs(value) / 1000.0);
      break;
   case kHybridAttack:
   case kHybridDecay:
   case kHybridRelease: {
      const double ms = hybridEnvMs(value);
      if (ms < 1000.0)
         std::snprintf(out, size, ms < 10.0 ? "%.1f ms" : "%.0f ms", ms);
      else
         std::snprintf(out, size, "%.2f s", ms / 1000.0);
      break;
   }
   case kHoleCutoff:
      std::snprintf(out, size, "%.2f kHz", value / 1000.0);
      break;
   case kHybridCutoff:
      std::snprintf(out, size, value < 1000.0 ? "%.0f Hz" : "%.2f kHz", value < 1000.0 ? value : value / 1000.0);
      break;
   case kFlutterRate:
      std::snprintf(out, size, "%.1f Hz", value);
      break;
   case kUnison:
      std::snprintf(out, size, "%d", static_cast<int>(std::lround(value)));
      break;
   case kUnisonDetune:
      std::snprintf(out, size, "%.0f ct", value);
      break;
   case kRoomDecay:
   case kTail:
      std::snprintf(out, size, "%.1f s", value);
      break;
   case kMaster:
      std::snprintf(out, size, "%.1f dB", value);
      break;
   case kTune:
      std::snprintf(out, size, "%+d st", static_cast<int>(std::lround(value)));
      break;
   case kBendRange:
      std::snprintf(out, size, "%d st", static_cast<int>(std::lround(value)));
      break;
   case kFine:
      std::snprintf(out, size, "%+.0f ct", value);
      break;
   case kVibPitch:
      std::snprintf(out, size, "%.0f ct", value);
      break;
   case kBright:
      std::snprintf(out, size, "%+.1f dB", value);
      break;
   case kLevel:
      std::snprintf(out, size, "%.1f dB", value);
      break;
   case kVibRate:
      std::snprintf(out, size, "%.2f Hz", value);
      break;
   case kCurve:
      std::snprintf(out, size, "%.2f", value);
      break;
   default:
      std::snprintf(out, size, "%g", value);
      break;
   }
}

// The reverse of formatParam, also accepting plain numbers in the same unit. Hosts check that the
// two round-trip.
inline bool parseParam(clap_id id, const char *text, double &value) {
   while (*text == ' ')
      ++text;
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      for (int i = 0; i < count; ++i)
         if (!std::strcmp(text, names[i])) {
            value = i;
            return true;
         }
   }
   if (id >= kRouteBase && id < kRouteEnd && (id - kRouteBase) % kRouteStride == kRouteDestination)
      for (int d = 0; d < kModDestCount; ++d)
         if (!std::strcmp(text, kParams[kModDestinations[d]].name)) {
            value = d;
            return true;
         }
   double v = 0.0;
   if (!jclap::core::parseNumber(text, v, nullptr))
      return false;
   if (isPercent(id))
      value = v / 100.0;
   else if (id == kHoleCutoff)
      value = v < 50.0 ? v * 1000.0 : v;
   else if (id == kHybridCutoff)
      value = std::strstr(text, "k") ? v * 1000.0 : v;
   else if (id == kLength)
      value = std::clamp(std::log10(std::max(v, 0.15) / 0.15), 0.0, 1.0);
   else if (id == kDelayTime) {
      const double ms = std::strstr(text, "ms") ? v : std::strstr(text, "s") ? v * 1000.0 : v;
      value = std::clamp(std::log(std::max(ms, 10.0) / 10.0) / std::log(200.0), 0.0, 1.0);
   }
   else if (id == kHybridAttack || id == kHybridDecay || id == kHybridRelease) {
      const double ms = std::strstr(text, "ms") ? v : std::strstr(text, "s") ? v * 1000.0 : v;
      value = std::clamp(std::log10(std::max(ms, 1.0)) / 4.0, 0.0, 1.0);
   }
   else
      value = (kParams[id].flags & CLAP_PARAM_IS_STEPPED) ? std::round(v) : v;
   return true;
}

} // namespace jbreath
