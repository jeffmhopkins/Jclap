#pragma once

// JSynth's presets, as JVerb's: a factory bank compiled in, and the user's own saved as small text
// files.
//
// A factory preset is written as what it changes from the defaults, so the bank stays readable, and
// loading it sets every other param to its default, so nothing from the previous patch (a route, say)
// carries over. A user preset saves every param. Loading one is a normal edit of every param that
// changes, so the host sees each and can undo it.
//
// A user preset file is plain text: a "JSynth preset 1" line, then one "<param id> <value>" per line.
// Ids it doesn't know are skipped, and params it doesn't list load at their defaults.

#include <jclap/ui/params.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "params.h"

namespace jsynth {

struct Preset {
   std::string name;
   std::string category;
   std::vector<std::pair<clap_id, double>> values;
   BusNames buses{};
};

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      using Values = std::vector<std::pair<clap_id, double>>;
      // Part params by offset: a(oscParam(0, kOscWave), 2) is part A's.
      const auto a = [](clap_id offset, double v) { return std::pair<clap_id, double>{partId(0, offset), v}; };
      const auto b = [](clap_id offset, double v) { return std::pair<clap_id, double>{partId(1, offset), v}; };
      const auto g = [](clap_id id, double v) { return std::pair<clap_id, double>{id, v}; };
      const auto route = [](int part, int r, int source, clap_id dest, double amount) {
         return Values{{partId(part, routeParam(r, kRouteSource)), source},
                       {partId(part, routeParam(r, kRouteDestination)), destinationIndex(dest)},
                       {partId(part, routeParam(r, kRouteAmount)), amount}};
      };
      const auto join = [](std::initializer_list<Values> parts) {
         Values all;
         for (const Values &p : parts)
            all.insert(all.end(), p.begin(), p.end());
         return all;
      };
      // Envelope `e` of `part`, in milliseconds and a sustain level.
      const auto env = [](int part, int e, double attack, double decay, double sustain, double release) {
         return Values{{partId(part, envParam(e, kEnvAttack)), timeValue(attack)},
                       {partId(part, envParam(e, kEnvDecay)), timeValue(decay)},
                       {partId(part, envParam(e, kEnvSustain)), sustain},
                       {partId(part, envParam(e, kEnvRelease)), timeValue(release)}};
      };
      // Oscillator `o` of `part`: its wave, its pitch as a ratio of the note, and its level.
      const auto op = [](int part, int o, int wave, double ratio, double level) {
         return Values{{partId(part, oscParam(o, kOscWave)), wave},
                       {partId(part, oscParam(o, kOscPitchMode)), 1},
                       {partId(part, oscParam(o, kOscRatio)), ratio},
                       {partId(part, mixParam(kMixLevel, o)), level}};
      };
      // Filter 1 of `part` wide open, so the voice is its oscillators alone.
      const auto open = [](int part) {
         return Values{{partId(part, filterParam(0, kFilterCutoff)), 1.0}, {partId(part, filterParam(0, kFilterResonance)), 0.0}};
      };
      // The routes of `part`, from the first: they replace the three it starts with, so the preset
      // has these and no others.
      struct Link {
         int source;
         clap_id dest;
         double amount;
      };
      const auto routes = [](int part, std::initializer_list<Link> links) {
         Values v;
         int r = 0;
         for (const Link &l : links) {
            v.push_back({partId(part, routeParam(r, kRouteSource)), l.source});
            v.push_back({partId(part, routeParam(r, kRouteDestination)), destinationIndex(l.dest)});
            v.push_back({partId(part, routeParam(r, kRouteAmount)), l.amount});
            ++r;
         }
         for (; r < 3; ++r)
            v.push_back({partId(part, routeParam(r, kRouteSource)), kSourceOff});
         return v;
      };
      // Oscillator `o` of `part` pitched by the note: its wave, coarse and fine tuning, and its level.
      const auto osc = [](int part, int o, int wave, double coarse, double fine, double level) {
         return Values{{partId(part, oscParam(o, kOscWave)), wave},
                       {partId(part, oscParam(o, kOscPitchMode)), 0},
                       {partId(part, oscParam(o, kOscCoarse)), coarse},
                       {partId(part, oscParam(o, kOscFine)), fine},
                       {partId(part, mixParam(kMixLevel, o)), level}};
      };
      // Filter `f` of `part`: its type, cutoff in Hz, resonance and key tracking.
      const auto filter = [](int part, int f, int type, double hz, double res, double keyTrack) {
         return Values{{partId(part, filterParam(f, kFilterType)), type},
                       {partId(part, filterParam(f, kFilterCutoff)), cutoffValue(hz)},
                       {partId(part, filterParam(f, kFilterResonance)), res},
                       {partId(part, filterParam(f, kFilterKeyTrack)), keyTrack}};
      };
      // LFO `l` of `part` (2 is LFO 1 and 3 is LFO 2; 0 and 1 are the wingbeat and the wind): its shape
      // and rate in Hz.
      const auto lfo = [](int part, int l, int shape, double hz) {
         return Values{{partId(part, lfoParam(l, kLfoShape)), shape}, {partId(part, lfoParam(l, kLfoRate)), lfoValue(hz)}};
      };
      // The same, synced to the host's tempo at a division (kDivisionNames) and running Free, so it
      // sits on the host's bar rather than starting with each note.
      const auto lfoSync = [](int part, int l, int shape, int division) {
         return Values{{partId(part, lfoParam(l, kLfoShape)), shape},
                       {partId(part, lfoParam(l, kLfoSync)), 1},
                       {partId(part, lfoParam(l, kLfoDivision)), division},
                       {partId(part, lfoParam(l, kLfoTrigger)), 1}};
      };
      // One voice that slides between held notes over `glide` ms, and restarts from a released one.
      const auto mono = [](int part, double glide) {
         return Values{{partId(part, kVoiceMode), 2}, {partId(part, kGlideTime), glide > 0 ? timeValue(glide) : 0.0},
                       {partId(part, kGlideMode), 1}};
      };
      // Envelope `e` of `part` in Shape mode: a shape by its name, one pass of it in `ms`, and how
      // often it plays (kRepeatNames).
      const auto shape = [](int part, int e, const char *name, double ms, int repeat) {
         return Values{{partId(part, envParam(e, kEnvMode)), kModeShape},
                       {partId(part, shapeParam(e, kShapeIndex)), shapeIndex(name)},
                       {partId(part, shapeParam(e, kShapeTime)), shapeValue(ms)},
                       {partId(part, shapeParam(e, kShapeRepeat)), repeat}};
      };
      // The same, a pass a tempo length (kShapeDivisionNames).
      const auto shapeSync = [](int part, int e, const char *name, int division, int repeat) {
         return Values{{partId(part, envParam(e, kEnvMode)), kModeShape},
                       {partId(part, shapeParam(e, kShapeIndex)), shapeIndex(name)},
                       {partId(part, shapeParam(e, kShapeSync)), 1},
                       {partId(part, shapeParam(e, kShapeDivision)), division},
                       {partId(part, shapeParam(e, kShapeRepeat)), repeat}};
      };
      // A shape's warp and bend.
      const auto bow = [](int part, int e, double warp, double bend) {
         return Values{{partId(part, shapeParam(e, kShapeWarp)), warp}, {partId(part, shapeParam(e, kShapeBend)), bend}};
      };
      constexpr int kOnce = 0, kForever = kRepeatForever;
      constexpr int kHalfBar = 3, kBar = 4, kBars2 = 5, kBars4 = 7, kBars8 = 9, kBars16 = 11;
      constexpr int kScatter = 1, kHunt = 2, kFlee = 3;
      constexpr int kSine = 0, kTriangle = 1, kSaw = 2, kSquare = 3, kPulse = 4, kHalfSine = 5, kAltSine = 8, kRandom = 14;
      constexpr int kRatio = 1;
      constexpr int kLowPass = 0, kHighPass = 1, kBandPass = 2, kLadder = 5, kComb = 6;
      constexpr int kLfoSine = 0, kLfoTriangle = 1, kLfoSquare = 3, kLfoHold = 4, kLfoSmooth = 5;
      constexpr int k16th = 2, k8thDotted = 7;
      const int env2 = kSourceEnv1 + 1, env3 = kSourceEnv1 + 2, lfo1 = kSourceLfo1, lfo2 = kSourceLfo1 + 1;
      const int velocity = kSourceVelocity, press = kSourceVelocity + 1, slide = kSourceVelocity + 3;
      const int release = kSourceVelocity + 4, wheel = kSourceModWheel, key = kSourceKey;
      const clap_id cutoff = filterParam(0, kFilterCutoff), resonance = filterParam(0, kFilterResonance);
      const clap_id cutoff2 = filterParam(1, kFilterCutoff);
      const clap_id fm21 = fmCell(1, 0), fm31 = fmCell(2, 0), fm32 = fmCell(2, 1), fb1 = fmCell(0, 0), fb2 = fmCell(1, 1);
      // A route's own depth, for a route that moves another (the mod wheel deepening a vibrato, say).
      const auto depth = [](int r) { return routeParam(r, kRouteAmount); };
      std::vector<Preset> p;

      p.push_back({"Init", "Init", {}});

      // The bank is built around the flock. Nearly every patch is a held sound that the creatures
      // play: each note's creature flies its own path, and where it heads, how it turns, how fast it
      // flies and how near its neighbours are move that note's filter, shape, mix and FM, so a chord
      // never sits still and no two notes move alike. Envelopes in Shape mode loop under them and
      // reach back into the flock (its speed, its wander, the wind) and into the depth of the flock's
      // own routes, so the modulation itself breathes. Drones and Textures run from soft, spacey
      // ambience to heavily modulated industrial noise; a few plainer patches follow, flocked too.
      // Most answer Press and the mod wheel. The menu runs in the order of kCategoryOrder below.

      // The mod buses.
      // Bus `b` of `part`: how it mixes (kBusModeNames), bends (kBusCurveNames) and which way round
      // it sends (kBusPolarityNames), its onset after the note and its lag in ms, and Global.
      const auto busSet = [](int part, int b, int mode, int curve = 0, int polarity = 0, double delay = 0.0,
                             double fade = 0.0, double lag = 0.0, int global = 0) {
         return Values{{partId(part, busParam(b, kBusMode)), mode},
                       {partId(part, busParam(b, kBusCurve)), curve},
                       {partId(part, busParam(b, kBusPolarity)), polarity},
                       {partId(part, busParam(b, kBusDelay)), delay > 0 ? timeValue(delay) : 0.0},
                       {partId(part, busParam(b, kBusFade)), fade > 0 ? timeValue(fade) : 0.0},
                       {partId(part, busParam(b, kBusLag)), lag > 0 ? timeValue(lag) : 0.0},
                       {partId(part, busParam(b, kBusGlobal)), global}};
      };
      // A bus's inputs are the routes onto its Base.
      const auto busIn = [](int b) { return busParam(b, kBusOffset); };
      const int bus1 = kSourceBus1, bus2 = kSourceBus1 + 1, bus3 = kSourceBus1 + 2, bus4 = kSourceBus1 + 3;
      constexpr int kSum = kBusSum, kMul = kBusMultiply, kMin = kBusMin, kMax = kBusMax;
      constexpr int kLinear = 0, kExpo = 1, kLog = 2, kS = 3, kAbs = 4;
      constexpr int kAsIs = 0, kToUni = 1, kToBi = 2;
      // A bus that only smooths what is routed onto it, over `lag` ms.
      const auto lagBus = [&](int part, int bus, double lag) { return busSet(part, bus, kSum, kLinear, kAsIs, 0, 0, lag); };
      // The same, turned bipolar: Heading (0 down, 1 up) comes out -1 diving to +1 climbing, so a
      // route from it moves its destination both ways from where the knob sits.
      const auto biBus = [&](int part, int bus, double lag) { return busSet(part, bus, kSum, kLinear, kToBi, 0, 0, lag); };
      // How sharply a creature turns, either way: the absolute of Turn.
      const auto absBus = [&](int part, int bus, double lag) { return busSet(part, bus, kSum, kAbs, kAsIs, 0, 0, lag); };
      // The buses' names, as {part, bus, name}.
      struct BusName {
         int part, bus;
         const char *name;
      };
      const auto names = [](std::initializer_list<BusName> list) {
         BusNames n{};
         for (const BusName &x : list)
            n[x.part][x.bus] = x.name;
         return n;
      };

      // The flock.
      // How the creatures of `part` fly: top speed, restlessness, how hard the note pulls its creature
      // and how near counts as a neighbour.
      const auto fly = [](int part, double speed, double wander, double lure, double range = 0.35) {
         return Values{{partId(part, flockParam(kSpeed)), speed},
                       {partId(part, flockParam(kWander)), wander},
                       {partId(part, flockParam(kLure)), lure},
                       {partId(part, flockParam(kRange)), range}};
      };
      // How they keep together: cohesion, alignment and separation.
      const auto school = [](int part, double cohesion, double alignment, double separation) {
         return Values{{partId(part, flockParam(kCohesion)), cohesion},
                       {partId(part, flockParam(kAlignment)), alignment},
                       {partId(part, flockParam(kSeparation)), separation}};
      };
      // What flying near another does to the sound: Neighbour FM, Tune Pull and Bite.
      const auto between = [](int part, double neighbourFm, double tunePull, double bite = 0.0) {
         return Values{{partId(part, flockParam(kNeighbourFm)), neighbourFm},
                       {partId(part, flockParam(kTunePull)), tunePull},
                       {partId(part, flockParam(kBite)), bite}};
      };
      // The ink the panel draws: how long it lasts and how far it blooms.
      const auto ink = [](int part, double tail, double spread) {
         return Values{{partId(part, flockParam(kTailLength)), tail}, {partId(part, flockParam(kTailSpread)), spread}};
      };
      // How hard the wingbeat bobs each creature and the wind pushes them all, and the wind's rate.
      const auto air = [](int part, double wingbeat, double wind, double windHz = 0.2) {
         return Values{{partId(part, lfoParam(0, kLfoPush)), wingbeat},
                       {partId(part, lfoParam(1, kLfoPush)), wind},
                       {partId(part, lfoParam(1, kLfoRate)), lfoValue(windHz)}};
      };
      // The sky: its size (a bigger sky flies slower and wider) and its walls (kWallNames).
      const auto sky = [&](double size, int walls = 0) { return Values{g(kSkySize, size), g(kSkyWalls, walls)}; };
      constexpr int kWrap = 1;

      // Sources and destinations by name.
      const int heading = kSourceHeading, nearest = kSourceNearest, speed = kSourceSpeed, turn = kSourceTurn;
      const int random = kSourceRandom, macro1 = kSourceMacro1;
      const auto shapeOf = [](int o) { return oscParam(o, kOscShape); };
      const auto fineOf = [](int o) { return oscParam(o, kOscFine); };
      const auto ratioOf = [](int o) { return oscParam(o, kOscRatio); };
      const auto coarseOf = [](int o) { return oscParam(o, kOscCoarse); };
      const auto level = [](int o) { return mixParam(kMixLevel, o); };
      const auto panOf = [](int o) { return mixParam(kMixPan, o); };
      const clap_id res2 = filterParam(1, kFilterResonance), noise = mixParam(kNoiseLevel), color = mixParam(kNoiseColor);
      const clap_id drive = tailParam(kDriveAmount), balance = tailParam(kFilterBalance);
      const clap_id fSpeed = flockParam(kSpeed), fWander = flockParam(kWander), fCohesion = flockParam(kCohesion);
      const clap_id fRange = flockParam(kRange), fPull = flockParam(kTunePull), fNfm = flockParam(kNeighbourFm);
      const clap_id fLure = flockParam(kLure);
      const clap_id windPush = lfoParam(1, kLfoPush), wingPush = lfoParam(0, kLfoPush);
      const clap_id lfo1Rate = lfoParam(2, kLfoRate), lfo2Rate = lfoParam(3, kLfoRate);
      const clap_id fm12 = fmCell(0, 1), fm13 = fmCell(0, 2), fb3 = fmCell(2, 2);
      // Each envelope's Shape Time, Warp, Bend and Level as destinations.
      const auto tm = [](int e) { return shapeParam(e, kShapeTime); };
      const auto wp = [](int e) { return shapeParam(e, kShapeWarp); };
      const auto bd = [](int e) { return shapeParam(e, kShapeBend); };
      const auto lv = [](int e) { return shapeParam(e, kShapeLevel); };
      // A part param of either part.
      const auto pv = [](int part, clap_id offset, double v) { return std::pair<clap_id, double>{partId(part, offset), v}; };
      // Two band passes in parallel at a vowel's first two formants (ah is 750 and 1150 Hz, ee 300
      // and 2300, oo 330 and 700), fixed in pitch as a mouth is, whatever note sings through them.
      const auto vox = [&](int part, double f1, double f2, double res) {
         return join({{pv(part, tailParam(kFilterRouting), 1), pv(part, filterParam(0, kFilterSlope), 0), pv(part, filterParam(1, kFilterSlope), 0)},
                      filter(part, 0, kBandPass, f1, res, 0.0), filter(part, 1, kBandPass, f2, res, 0.0)});
      };
      // Both filters side by side, each oscillator into the one given (0 for F1, 1 for F2).
      const auto parallel = [&](int part) { return Values{pv(part, tailParam(kFilterRouting), 1)}; };
      const auto into = [&](int part, int o, int f) { return Values{pv(part, oscParam(o, kOscFilter), f + 1)}; };
      // Part B at the note's own pitch (or `transpose` from it), flying with A rather than hunting it.
      const auto own = [&](int transpose, double lvl) {
         return Values{b(kPartTranspose, transpose), b(flockParam(kBehaviour), 0), b(kPartLevel, lvl)};
      };
      constexpr int kAbsSine = 6, kQuarter = 7, kAltHalf = 9, kCamel = 10, kWhite = 12, kPink = 13;
      constexpr int kNotch = 3, kPeak = 4, kCombOdd = 7;
      constexpr int kHard = 1, kTube = 2;
      constexpr int kLfoSaw = 2;
      constexpr int kLureCentre = 6, kLureRandom = 5;
      // The room on the main output: the reverb's mix, size, decay in seconds and damping.
      const auto room = [&](double mix, double size, double decay, double damp) {
         return Values{g(kReverbOn, 1), g(kReverbMix, mix), g(kReverbSize, size), g(kReverbDecay, decay), g(kReverbDamp, damp)};
      };
      // Echoes in time with the host, at a division (kDelaySyncNames), their mix and feedback.
      const auto echo = [&](int sync, double mix, double feedback) {
         return Values{g(kDelayOn, 1), g(kDelaySync, sync), g(kDelayMix, mix), g(kDelayFeedback, feedback)};
      };
      constexpr int k8thDot = 7, kQuarterDot = 10;

      // ---- Ambient Drones: soft and spacey, the flock moving every note its own way ----

      // Saws and a sub under a slow, wide flock: each creature's climb opens its own filter, its turns
      // swing it across the stereo field and its speed brings up the sub. A 9 s Breath loop makes the
      // flock restless and deepens the climb as it peaks.
      p.push_back({"Slow Murmuration", "Ambient Drones",
                   join({osc(0, 0, kSaw, 0, -7, 0.8), osc(0, 1, kSaw, 0, 7, 0.8), osc(0, 2, kSine, -12, 0, 0.0),
                         {a(panOf(0), -0.5), a(panOf(1), 0.5), a(kSkyPan, 0.6), a(kDrift, 0.2)},
                         filter(0, 0, kLowPass, 520, 0.25, 0.3), env(0, 0, 2500, 1000, 1.0, 6000),
                         fly(0, 0.18, 0.5, 0.35, 0.4), school(0, 0.6, 0.7, 0.4), between(0, 0, 0.4), ink(0, 0.8, 0.7), sky(0.75),
                         shape(0, 1, "Breath", 9000, kForever), biBus(0, 0, 300),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.22}, {turn, kPartPan, 0.35}, {speed, level(2), 0.6},
                                    {env2, fWander, 0.4}, {env2, depth(1), 0.15}, {nearest, resonance, 0.25}, {press, cutoff, 0.15},
                                    {wheel, cutoff, 0.25}})}),
                   names({{0, 0, "Climb"}})});
      // A choir in two formants: each creature's climb opens its vowel from "oo" toward "ah", its turns
      // brighten the second formant, and Tune Pull draws the chord into just intervals. Flying fast
      // deepens the vibrato, and a slow Twin Hills loop lets the vowels swing wider, then settle.
      p.push_back({"Aurora Choir", "Ambient Drones",
                   join({osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kPulse, 0, 5, 0.7), {a(shapeOf(1), 0.35), a(noise, 0.03)},
                         vox(0, 400, 900, 0.45), env(0, 0, 1800, 1000, 1.0, 5000), lfo(0, 2, kLfoSine, 5.2),
                         fly(0, 0.22, 0.4, 0.4), school(0, 0.7, 0.8, 0.4), between(0, 0, 0.8), sky(0.6), {a(kSkyPan, 0.5)},
                         shape(0, 1, "Twin Hills", 14000, kForever), biBus(0, 0, 250), absBus(0, 1, 200),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.09}, {bus1, cutoff2, 0.05}, {turn, busIn(1), 1.0},
                                    {bus2, cutoff2, 0.08}, {lfo1, kPartFine, 0.015}, {speed, depth(5), 0.025}, {env2, depth(1), 0.06},
                                    {press, cutoff2, 0.05}, {wheel, depth(5), 0.02}})}),
                   names({{0, 0, "Climb"}, {0, 1, "Sharp Turns"}})});
      // FM glass that floats: the faster each creature flies the brighter its FM, its climb swells a sub
      // an octave down, and its turns pull its carrier a few cents either way so the chord beats. A
      // 20 s Arch loop speeds the whole flock up and lets it rest.
      p.push_back({"Weightless", "Ambient Drones",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSine, 0.5, 0.0),
                         {a(fm21, 0.12), a(kSkyPan, 0.5), a(kDrift, 0.1)}, open(0), env(0, 0, 2000, 1000, 1.0, 6000),
                         fly(0, 0.2, 0.35, 0.4), air(0, 0.05, 0.3), sky(0.7), shape(0, 1, "Arch", 20000, kForever), lagBus(0, 0, 400),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, fm21, 0.35}, {heading, level(2), 0.55}, {turn, fineOf(0), 0.05},
                                    {env2, fSpeed, 0.4}, {press, fm21, 0.2}, {wheel, fb2, 0.3}})}),
                   names({{0, 0, "Flight"}})});
      // Breath through a resonant band pass at the note: a 17 s Surge loop strengthens the wind, the
      // gusts carry every creature at once, and how fast each is carried brings up its breath and rings
      // its band; its climb swings the band up and down.
      p.push_back({"Tidal Breath", "Ambient Drones",
                   join({osc(0, 0, kSine, 0, 0, 0.35), {a(noise, 0.6), a(color, -0.6), a(filterParam(0, kFilterSlope), 0), a(kSkyPan, 0.6)},
                         filter(0, 0, kBandPass, 261.6, 0.55, 1.0), filter(0, 1, kLowPass, 3000, 0.0, 0.0),
                         env(0, 0, 1500, 1000, 1.0, 4000), fly(0, 0.25, 0.3, 0.3, 0.4), air(0, 0.1, 0.5, 0.12), sky(0.7),
                         shape(0, 1, "Surge", 17000, kForever), lagBus(0, 0, 200), biBus(0, 1, 300),
                         routes(0, {{env2, windPush, 0.45}, {speed, busIn(0), 1.0}, {bus1, noise, 0.35}, {bus1, resonance, 0.2},
                                    {heading, busIn(1), 1.0}, {bus2, cutoff, 0.08}, {press, noise, 0.2}, {wheel, cutoff2, 0.2}})}),
                   names({{0, 0, "Carried"}, {0, 1, "Climb"}})});
      // A ring of light: a triangle and air through a comb tuned to the note. Each creature's climb
      // lengthens its ring, its turns shade the ring a few cents off the note, and how near its
      // neighbours are thickens the air. A 25 s Shimmer loop brightens the whole halo.
      p.push_back({"Halo", "Ambient Drones",
                   join({osc(0, 0, kTriangle, 0, 0, 0.5), {a(noise, 0.25), a(color, 0.3), a(kSkyPan, 0.5)},
                         filter(0, 0, kComb, 261.6, 0.6, 1.0), filter(0, 1, kLowPass, 2500, 0.1, 0.4), env(0, 0, 1200, 1000, 1.0, 5000),
                         fly(0, 0.2, 0.45, 0.4), between(0, 0, 0.5), sky(0.65), shape(0, 1, "Shimmer", 25000, kForever), biBus(0, 0, 300),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, resonance, 0.3}, {bus1, cutoff2, 0.2}, {turn, cutoff, 0.006},
                                    {nearest, noise, 0.3}, {env2, cutoff2, 0.15}, {press, resonance, 0.15}, {wheel, cutoff2, 0.25}})}),
                   names({{0, 0, "Climb"}})});
      // Three-operator glass: each creature's climb rings its ×3 partial, crowding feeds the modulator
      // back on itself, and a 31 s Wander drifts the ×3 off true and back, so the glass tints from pure
      // to bell-like.
      p.push_back({"Horizon Glass", "Ambient Drones",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 0.5, 0.0), op(0, 2, kSine, 3.0, 0.0),
                         {a(fm31, 0.08), a(fm21, 0.1), a(kSkyPan, 0.5)}, open(0), env(0, 0, 2500, 1000, 1.0, 6000),
                         fly(0, 0.18, 0.4, 0.45), between(0, 0, 0.6), sky(0.7), shape(0, 2, "Wander", 31000, kForever), lagBus(0, 0, 250),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, fm31, 0.3}, {nearest, fb2, 0.35}, {env3, ratioOf(2), 0.02},
                                    {speed, fm21, 0.15}, {press, fm31, 0.2}, {wheel, fb2, 0.3}})}),
                   names({{0, 0, "Climb"}})});
      // Sine drawbars that drift: each creature's climb draws out the octave, its turns either way the
      // twelfth, and Tune Pull keeps the chord pure. Its speed sets the depth of a slow chorus.
      p.push_back({"Floating Organ", "Ambient Drones",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.15), op(0, 2, kSine, 3.0, 0.05), open(0),
                         {a(kSkyPan, 0.4)}, env(0, 0, 900, 1000, 1.0, 3500), lfo(0, 2, kLfoSine, 0.7), fly(0, 0.22, 0.35, 0.45),
                         between(0, 0, 0.9), sky(0.6), absBus(0, 0, 200), busSet(0, 1, kMul, kLinear, kAsIs, 0, 0, 100),
                         routes(0, {{heading, level(1), 0.55}, {turn, busIn(0), 1.0}, {bus1, level(2), 0.45}, {lfo1, busIn(1), 1.0},
                                    {speed, busIn(1), 1.0}, {bus2, fineOf(1), 0.06}, {press, level(2), 0.2}, {wheel, fineOf(2), 0.05}})}),
                   names({{0, 0, "Sharp Turns"}, {0, 1, "Chorus x Speed"}})});
      // Saws in a moving band: a low pass that each creature's climb opens and a high pass its speed
      // raises, so a fast creature thins to a bright band. A 30 s Wander loop widens the flock's range
      // and pulls it back, so creatures fall in and out of each other's company.
      p.push_back({"Nebula", "Ambient Drones",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 0.9), osc(0, 2, kSquare, -12, 0, 0.3),
                         {a(panOf(0), -0.6), a(panOf(1), 0.6), a(kSkyPan, 0.7)}, filter(0, 0, kLowPass, 900, 0.3, 0.3),
                         filter(0, 1, kHighPass, 120, 0.0, 0.0), env(0, 0, 3000, 1000, 1.0, 7000), fly(0, 0.25, 0.5, 0.35, 0.3),
                         school(0, 0.6, 0.5, 0.4), sky(0.8), shape(0, 1, "Wander", 30000, kForever), biBus(0, 0, 300),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.18}, {speed, cutoff2, 0.25}, {env2, fRange, 0.35},
                                    {nearest, resonance, 0.3}, {press, cutoff, 0.15}, {wheel, cutoff2, 0.2}})}),
                   names({{0, 0, "Climb"}})});
      // The slowest of them: a pulse and a saw on a Slow S fade-in, and creatures in a huge sky whose
      // climbs, lagged over two seconds, open the filter so slowly it reads as weather. A 40 s Brownian
      // walk sets how fast they fly, and the amp itself deepens the flock's hold on the filter as the
      // chord blooms.
      p.push_back({"Deep Field", "Ambient Drones",
                   join({osc(0, 0, kPulse, 0, -3, 0.8), osc(0, 1, kSaw, 0, 3, 0.8), osc(0, 2, kSine, -12, 0, 0.4),
                         {a(shapeOf(0), 0.3), a(kSkyPan, 0.7), a(envParam(0, kEnvRelease), timeValue(8000))},
                         filter(0, 0, kLowPass, 400, 0.2, 0.3), shape(0, 0, "Slow S", 8000, kOnce), fly(0, 0.15, 0.4, 0.3, 0.45),
                         sky(1.0), shape(0, 2, "Brownian", 40000, kForever), biBus(0, 0, 2000),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.25}, {env3, fSpeed, 0.5}, {kSourceEnv1, depth(1), 0.1},
                                    {turn, shapeOf(0), 0.2}, {press, cutoff, 0.15}, {wheel, cutoff, 0.2}})}),
                   names({{0, 0, "Slow Climb"}})});
      // Pulse waves whose widths the creatures bend as they turn, one each way, under a filter their
      // climb opens; LFO 1 at an odd 0.13 Hz breathes the second width, and speed brightens the voice.
      p.push_back({"Lumen", "Ambient Drones",
                   join({osc(0, 0, kPulse, 0, -5, 0.8), osc(0, 1, kPulse, 0, 5, 0.8),
                         {a(shapeOf(0), 0.3), a(shapeOf(1), 0.3), a(panOf(0), -0.5), a(panOf(1), 0.5), a(kSkyPan, 0.5)},
                         filter(0, 0, kLowPass, 1200, 0.2, 0.4), env(0, 0, 1500, 1000, 1.0, 5000), lfo(0, 2, kLfoSine, 0.13),
                         fly(0, 0.25, 0.4, 0.45), sky(0.6), biBus(0, 0, 250),
                         routes(0, {{turn, shapeOf(0), 0.25}, {turn, shapeOf(1), -0.25}, {heading, busIn(0), 1.0}, {bus1, cutoff, 0.15},
                                    {lfo1, shapeOf(1), 0.1}, {speed, cutoff, 0.1}, {press, cutoff, 0.15}, {wheel, resonance, 0.3}})}),
                   names({{0, 0, "Climb"}})});
      // A string machine flown by the flock: each creature's speed is its vibrato depth, its climb its
      // brightness, and its turns pull its two saws apart. Tune Pull keeps the section together.
      p.push_back({"Ether Strings", "Ambient Drones",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 0.9), osc(0, 2, kSaw, 12, 0, 0.35),
                         {a(panOf(0), -0.4), a(panOf(1), 0.4), a(kSkyPan, 0.4)}, filter(0, 0, kLowPass, 1600, 0.15, 0.5),
                         env(0, 0, 600, 1000, 1.0, 2500), lfo(0, 2, kLfoSine, 5.5), fly(0, 0.3, 0.3, 0.5), between(0, 0, 0.5), sky(0.6),
                         busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 100), biBus(0, 1, 200),
                         routes(0, {{lfo1, busIn(0), 1.0}, {speed, busIn(0), 1.0}, {bus1, kPartFine, 0.05}, {heading, busIn(1), 1.0},
                                    {bus2, cutoff, 0.14}, {turn, fineOf(1), 0.06}, {press, cutoff, 0.15}, {wheel, depth(2), 0.03}})}),
                   names({{0, 0, "Vibrato x Speed"}, {0, 1, "Climb"}})});
      // High and airy: an alt-sine and its far partials with a breath of white noise, the creatures
      // scattered across a wrapping sky. Each one's speed lifts its air, its climb its top partial, its
      // turns pan its fourth harmonic, and a 12 s Shimmer loop sparkles the lot.
      p.push_back({"Snow Light", "Ambient Drones",
                   join({echo(kQuarterDot, 0.18, 0.5), op(0, 0, kAltSine, 1.0, 0.7), op(0, 1, kSine, 4.0, 0.0), op(0, 2, kSine, 6.01, 0.0),
                         {a(noise, 0.03), a(color, 0.6), a(flockParam(kBehaviour), kScatter), a(kSkyPan, 0.9)}, open(0),
                         filter(0, 1, kHighPass, 300, 0.0, 0.0), env(0, 0, 1500, 1000, 1.0, 5000), fly(0, 0.3, 0.6, 0.25),
                         sky(0.8, kWrap), shape(0, 1, "Shimmer", 12000, kForever),
                         routes(0, {{speed, noise, 0.1}, {heading, level(2), 0.3}, {env2, level(1), 0.35}, {turn, panOf(1), 0.5},
                                    {nearest, fineOf(2), 0.1}, {press, level(1), 0.2}, {wheel, noise, 0.1}})})});
      // Envelopes that move each other, under the flock: a 12 s Breath on Env 2 opens the filter and
      // quickens Env 3's wobble as it fills; each creature's climb leans its breath early or late, and
      // its speed sets how deep its wobble runs.
      p.push_back({"Breathing Planet", "Ambient Drones",
                   join({osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kSaw, 0, 5, 0.9), {a(panOf(0), -0.3), a(panOf(1), 0.3), a(kSkyPan, 0.5)},
                         filter(0, 0, kLowPass, 700, 0.3, 0.3), env(0, 0, 2000, 1000, 1.0, 6000),
                         shape(0, 1, "Breath", 12000, kForever), shape(0, 2, "Wobble 8", 6000, kForever),
                         {a(shapeParam(2, kShapeLevel), 0.2)}, fly(0, 0.22, 0.4, 0.4), sky(0.7),
                         routes(0, {{env2, cutoff, 0.2}, {env2, tm(2), -0.08}, {env3, cutoff, 0.08}, {heading, wp(1), 0.7},
                                    {speed, lv(2), 1.0}, {turn, kPartPan, 0.3}, {press, cutoff, 0.15}, {wheel, resonance, 0.3}})})});
      // Soft sines that ring each other: Neighbour FM makes a gathered flock shimmer and a scattered one
      // fall pure. A 22 s Arch loop pulls the creatures together and lets them drift apart, so the
      // shimmer comes and goes; each one's climb lifts it into its octave.
      p.push_back({"Cirrus", "Ambient Drones",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.5), open(0), {a(kSkyPan, 0.6)},
                         env(0, 0, 1500, 1000, 1.0, 5000), fly(0, 0.25, 0.4, 0.4, 0.4), school(0, 0.3, 0.6, 0.4),
                         between(0, 0.35, 0.5), sky(0.65), shape(0, 1, "Arch", 22000, kForever),
                         routes(0, {{env2, fCohesion, 0.6}, {env2, fNfm, 0.25}, {heading, level(1), 0.3}, {turn, fineOf(1), 0.04},
                                    {press, fNfm, 0.2}, {wheel, fNfm, 0.3}})})});

      // ---- Dark Drones: low, slow and ominous ----

      // A sub and a growl in a low ladder: each creature's climb opens the ladder, how near its
      // neighbours are drives it, and a 23 s Surge loop sends the flock racing, so the growl rings
      // when it does.
      p.push_back({"Abyssal", "Dark Drones",
                   join({osc(0, 0, kSine, -12, 0, 0.8), osc(0, 1, kSaw, 0, -4, 0.7), osc(0, 2, kSaw, 0, 4, 0.6),
                         {a(tailParam(kDriveType), kTube), a(drive, 0.2), a(kSkyPan, 0.3)}, filter(0, 0, kLadder, 160, 0.45, 0.2),
                         env(0, 0, 2000, 1000, 1.0, 5000), fly(0, 0.15, 0.35, 0.5), sky(0.7), shape(0, 1, "Surge", 23000, kForever),
                         biBus(0, 0, 400), lagBus(0, 1, 300),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.2}, {nearest, drive, 0.35}, {env2, fSpeed, 0.5},
                                    {speed, busIn(1), 1.0}, {bus2, resonance, 0.25}, {press, drive, 0.3}, {wheel, cutoff, 0.25}})}),
                   names({{0, 0, "Climb"}, {0, 1, "Flight"}})});
      // A low 1:2 FM growl: each creature's speed turns up its modulator's feedback, its turns slide the
      // modulator a little off its ratio so the growl beats and rolls, and its climb deepens the FM.
      p.push_back({"Leviathan", "Dark Drones",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSine, 0.5, 0.4),
                         {a(fm21, 0.15), a(fb2, 0.1), a(kDrift, 0.1), a(kSkyPan, 0.3)}, filter(0, 0, kLowPass, 900, 0.1, 0.3),
                         env(0, 0, 1500, 1000, 1.0, 4000), fly(0, 0.2, 0.4, 0.45), sky(0.7), shape(0, 2, "Gusts", 19000, kForever),
                         lagBus(0, 0, 300), biBus(0, 1, 500),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, fb2, 0.45}, {turn, ratioOf(1), 0.004}, {heading, busIn(1), 1.0},
                                    {bus2, fm21, 0.2}, {env3, cutoff, 0.2}, {press, fm21, 0.25}, {wheel, cutoff, 0.2}})}),
                   names({{0, 0, "Flight"}, {0, 1, "Climb"}})});
      // Dark air in a stone room: pink noise and a low saw through a comb tuned an octave down. Each
      // creature's turns colour the noise, its climb lengthens the ring, and a 33 s Plateau loop lets
      // the cave breathe out.
      p.push_back({"Cavern", "Dark Drones",
                   join({osc(0, 0, kSaw, 0, 0, 0.2), {a(noise, 0.55), a(color, -0.7), a(kSkyPan, 0.5)},
                         filter(0, 0, kComb, 130.8, 0.55, 1.0), filter(0, 1, kLowPass, 1200, 0.1, 0.3), env(0, 0, 2500, 1000, 1.0, 6000),
                         fly(0, 0.2, 0.4, 0.4), sky(0.75), shape(0, 1, "Plateau", 33000, kForever), biBus(0, 0, 300),
                         routes(0, {{turn, color, 0.5}, {heading, busIn(0), 1.0}, {bus1, resonance, 0.25}, {env2, cutoff2, 0.2},
                                    {nearest, noise, 0.2}, {press, resonance, 0.15}, {wheel, cutoff2, 0.25}})}),
                   names({{0, 0, "Climb"}})});
      // A pulse and a saw an octave apart in a resonant ladder that never opens far: each creature's
      // climb slides its pulse width, crowding rings the resonance, and a Brownian walk slowly tilts the
      // cutoff. Monolithic, but never still.
      p.push_back({"Black Monolith", "Dark Drones",
                   join({osc(0, 0, kPulse, -12, 0, 0.8), osc(0, 1, kSaw, 0, 0, 0.7), {a(shapeOf(0), 0.2)},
                         filter(0, 0, kLadder, 220, 0.55, 0.3), {a(filterParam(0, kFilterDrive), 0.2)},
                         env(0, 0, 1800, 1000, 1.0, 5000), fly(0, 0.18, 0.35, 0.5), sky(0.7), shape(0, 1, "Brownian", 27000, kForever),
                         biBus(0, 0, 300),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, shapeOf(0), 0.25}, {nearest, resonance, 0.3}, {env2, cutoff, 0.15},
                                    {speed, cutoff, 0.1}, {press, cutoff, 0.15}, {wheel, resonance, 0.2}})}),
                   names({{0, 0, "Climb"}})});
      // Low saws on a slow tide: a 41 s Gusts shape strengthens the wind, and each gust carries the
      // whole flock one way, so the chord leans together: the creatures' climbs open the filter, their
      // turns pan them, and how fast they are carried rings the resonance.
      p.push_back({"Night Tide", "Dark Drones",
                   join({osc(0, 0, kSaw, 0, -8, 0.9), osc(0, 1, kSaw, 0, 8, 0.9), osc(0, 2, kSine, -12, 0, 0.4),
                         {a(panOf(0), -0.4), a(panOf(1), 0.4), a(kSkyPan, 0.6)}, filter(0, 0, kLowPass, 450, 0.25, 0.3),
                         env(0, 0, 3000, 1000, 1.0, 7000), fly(0, 0.15, 0.25, 0.35), air(0, 0.05, 0.6, 0.08), sky(0.8),
                         shape(0, 1, "Gusts", 41000, kForever), biBus(0, 0, 500),
                         routes(0, {{env2, windPush, 0.4}, {heading, busIn(0), 1.0}, {bus1, cutoff, 0.18}, {turn, kPartPan, 0.3},
                                    {speed, resonance, 0.25}, {press, cutoff, 0.15}, {wheel, cutoff, 0.25}})}),
                   names({{0, 0, "Climb"}})});
      // A low, dark choir in two formants near "oo": each creature's climb opens its vowel toward "oh",
      // its speed adds breath, and Tune Pull holds the chord in just intervals. A 29 s Bow loop deepens
      // how far the vowels move.
      p.push_back({"Obsidian Choir", "Dark Drones",
                   join({osc(0, 0, kSaw, 0, -3, 1.0), osc(0, 1, kPulse, 0, 4, 0.6), {a(shapeOf(1), 0.3), a(noise, 0.03), a(kSkyPan, 0.4)},
                         vox(0, 330, 700, 0.5), env(0, 0, 2500, 1000, 1.0, 6000), fly(0, 0.18, 0.35, 0.45), between(0, 0, 0.85),
                         sky(0.7), shape(0, 1, "Bow", 29000, kForever), biBus(0, 0, 300),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.06}, {bus1, cutoff2, 0.08}, {speed, noise, 0.1},
                                    {env2, depth(1), 0.06}, {env2, depth(2), 0.08}, {press, cutoff2, 0.05}, {wheel, cutoff, 0.05}})}),
                   names({{0, 0, "Climb"}})});
      // A pad heard from under water: saws through a steep, key-tracked low pass, chorused by each
      // creature's turns. Its climb lifts it toward the surface; a 25 s Brownian walk deepens the
      // flock's hold on the filter and lets it go.
      p.push_back({"Submerged", "Dark Drones",
                   join({osc(0, 0, kSaw, 0, -9, 0.9), osc(0, 1, kSaw, 0, 9, 0.9), {a(panOf(0), -0.4), a(panOf(1), 0.4), a(kSkyPan, 0.5)},
                         filter(0, 0, kLowPass, 350, 0.35, 0.6), {a(filterParam(0, kFilterSlope), 2)}, env(0, 0, 2500, 1000, 1.0, 6000),
                         fly(0, 0.2, 0.4, 0.4), sky(0.7), shape(0, 1, "Brownian", 25000, kForever), biBus(0, 0, 800),
                         routes(0, {{turn, fineOf(0), 0.06}, {turn, fineOf(1), -0.06}, {heading, busIn(0), 1.0}, {bus1, cutoff, 0.15},
                                    {env2, depth(3), 0.15}, {speed, resonance, 0.2}, {press, cutoff, 0.15}, {wheel, cutoff, 0.2}})}),
                   names({{0, 0, "Surfacing"}})});
      // A chord that keeps falling in on itself: each creature's dive pulls its pitch a few cents flat
      // and darkens it, Tune Pull keeps trying to straighten the chord, and a 20 s Collapse loop pulls
      // the flock tight, where Neighbour FM grinds.
      p.push_back({"Event Horizon", "Dark Drones",
                   join({osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kSaw, 0, 4, 0.9), osc(0, 2, kSine, -12, 0, 0.4), {a(kSkyPan, 0.5)},
                         filter(0, 0, kLowPass, 800, 0.3, 0.3), env(0, 0, 2000, 1000, 1.0, 6000), fly(0, 0.22, 0.35, 0.4, 0.4),
                         school(0, 0.4, 0.6, 0.3), between(0, 0.2, 0.6), sky(0.7), shape(0, 1, "Collapse", 20000, kForever),
                         biBus(0, 0, 400),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, kPartFine, 0.04}, {bus1, cutoff, 0.15}, {env2, fCohesion, 0.5},
                                    {nearest, fNfm, 0.3}, {press, cutoff, 0.15}, {wheel, fNfm, 0.3}})}),
                   names({{0, 0, "Climb"}})});
      // Creatures falling together: a Late Swell on Env 2 raises Cohesion and the range over 16 s, so a
      // held chord gathers; the nearer each creature's neighbours, the more FM and resonance it gains,
      // until the drone tightens into a ringing knot. Let go and it scatters.
      p.push_back({"Gravity Well", "Dark Drones",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), osc(0, 2, kSaw, 0, 7, 0.4), {a(kSkyPan, 0.5)},
                         filter(0, 0, kLowPass, 700, 0.25, 0.4), env(0, 0, 1500, 1000, 1.0, 4000), fly(0, 0.25, 0.35, 0.4, 0.25),
                         school(0, 0.2, 0.5, 0.3), between(0, 0.1, 0.4), sky(0.7), shape(0, 1, "Late Swell", 16000, kOnce),
                         lagBus(0, 0, 300),
                         routes(0, {{env2, fCohesion, 0.7}, {env2, fRange, 0.4}, {nearest, busIn(0), 1.0}, {bus1, fm21, 0.4},
                                    {bus1, resonance, 0.35}, {bus1, cutoff, 0.12}, {turn, kPartPan, 0.3}, {press, fm21, 0.2},
                                    {wheel, fCohesion, 0.5}})}),
                   names({{0, 0, "Gathering"}})});
      // Plates grinding: three saws a few cents apart at the bottom, each creature's speed pushing tube
      // drive and its turns slowly re-tuning the middle saw. A 37 s Wander bends the ladder; the mod
      // wheel drags the filter down.
      p.push_back({"Tectonic", "Dark Drones",
                   join({osc(0, 0, kSaw, 0, -9, 0.9), osc(0, 1, kSaw, 0, 9, 0.9), osc(0, 2, kSaw, -12, 3, 0.7),
                         {a(tailParam(kDriveType), kTube), a(drive, 0.25), a(kSkyPan, 0.4)}, filter(0, 0, kLadder, 300, 0.35, 0.3),
                         env(0, 0, 3000, 1000, 1.0, 6000), fly(0, 0.17, 0.3, 0.45), sky(0.75), shape(0, 1, "Wander", 37000, kForever),
                         lagBus(0, 0, 500),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, drive, 0.4}, {turn, fineOf(1), 0.08}, {env2, cutoff, 0.2},
                                    {heading, resonance, 0.2}, {press, drive, 0.25}, {wheel, cutoff, -0.25}})}),
                   names({{0, 0, "Flight"}})});

      // ---- Textures: patterns, grains and particles, each creature playing its own ----

      // Half-sine sparks on a Sparkle amp, in a fast, scattered flock: the faster a creature flies the
      // denser its sparks, its climb lifts their pitch a little, and crowding brightens them.
      p.push_back({"Firefly Field", "Textures",
                   join({echo(k8thDot, 0.25, 0.45), op(0, 0, kHalfSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), open(0),
                         {a(fm21, 0.1), a(kPartTranspose, 12), a(flockParam(kBehaviour), kScatter), a(kSkyPan, 0.9),
                          a(envParam(0, kEnvRelease), timeValue(600))},
                         shape(0, 0, "Sparkle", 2500, kForever), fly(0, 0.55, 0.7, 0.3), sky(0.8, kWrap), ink(0, 0.3, 0.8),
                         lagBus(0, 0, 150),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, tm(0), -0.1}, {heading, kPartFine, 0.05}, {nearest, fm21, 0.25},
                                    {press, tm(0), -0.05}, {wheel, fm21, 0.3}})}),
                   names({{0, 0, "Flight"}})});
      // FM glass struck by Sparkle, each creature its own rainfall: how near its neighbours are deepens
      // the FM, its turns pan it, its speed quickens the drops, and a 20 s Gusts loop brings squalls,
      // flying the flock faster as it rains harder.
      p.push_back({"Rain on Glass", "Textures",
                   join({echo(k8thDot, 0.22, 0.4), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), open(0),
                         {a(fm21, 0.2), a(kPartTranspose, 12), a(kSkyPan, 0.8), a(envParam(0, kEnvRelease), timeValue(900))},
                         shape(0, 0, "Sparkle", 1600, kForever), shape(0, 1, "Gusts", 20000, kForever), fly(0, 0.4, 0.5, 0.35), sky(0.7),
                         routes(0, {{nearest, fm21, 0.35}, {turn, kPartPan, 0.5}, {speed, tm(0), -0.08}, {env2, tm(0), -0.1},
                                    {env2, fSpeed, 0.3}, {press, fm21, 0.2}, {wheel, tm(0), -0.1}})})});
      // High FM chirps on a Stutter amp: the wingbeat bobs each creature, its turns bend its chirp, its
      // speed sets how fast it stutters and its climb how bright. A scattered, restless swarm.
      p.push_back({"Insect Choir", "Textures",
                   join({echo(k8thDot, 0.15, 0.3), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), open(0),
                         {a(fm21, 0.25), a(kPartTranspose, 24), a(flockParam(kBehaviour), kScatter), a(kSkyPan, 0.9),
                          a(envParam(0, kEnvRelease), timeValue(300))},
                         shape(0, 0, "Stutter", 700, kForever), fly(0, 0.6, 0.8, 0.3), air(0, 0.6, 0.2), sky(0.7), ink(0, 0.25, 0.6),
                         routes(0, {{turn, ratioOf(1), 0.05}, {speed, tm(0), -0.12}, {heading, fm21, 0.2}, {nearest, kPartFine, 0.05},
                                    {press, fm21, 0.2}, {wheel, tm(0), -0.1}})})});
      // A grain cloud: Saw Train as the amp, its rate set by each creature's speed (S-curved and
      // smoothed), its pitch smeared by how near its neighbour is, its colour by its climb. A 15 s Bloom
      // loop makes the flock restless, and the cloud thickens.
      p.push_back({"Murmur Grains", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), osc(0, 2, kSaw, 0, 0, 0.3),
                         {a(fm21, 0.2), a(kSkyPan, 0.9), a(envParam(0, kEnvRelease), timeValue(800))}, filter(0, 0, kLowPass, 2500, 0.2, 0.4),
                         shape(0, 0, "Saw Train", 900, kForever), shape(0, 1, "Bloom", 15000, kForever), fly(0, 0.45, 0.7, 0.35), sky(0.7),
                         busSet(0, 0, kSum, kS, kAsIs, 0, 0, 120),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, tm(0), -0.12}, {nearest, fineOf(2), 0.15}, {nearest, fm21, 0.2},
                                    {heading, cutoff, 0.15}, {env2, fWander, 0.4}, {turn, kPartPan, 0.4}, {press, fWander, 0.3},
                                    {wheel, fm21, 0.3}})}),
                   names({{0, 0, "Grain Rate"}})});
      // Pink noise in a comb tuned to the note, crackling on a Flicker amp: each creature's climb bends
      // the comb up and down, its speed whitens the noise, and a hot creature (fast and crowded at once)
      // rings longest.
      p.push_back({"Embers", "Textures",
                   join({osc(0, 0, kSaw, 0, 0, 0.1), {a(noise, 0.5), a(color, -0.4), a(kSkyPan, 0.7), a(envParam(0, kEnvRelease), timeValue(1500))},
                         filter(0, 0, kComb, 261.6, 0.72, 1.0), filter(0, 1, kLowPass, 3500, 0.0, 0.2),
                         shape(0, 0, "Flicker", 2200, kForever), fly(0, 0.35, 0.5, 0.4), sky(0.7), biBus(0, 0, 200),
                         busSet(0, 1, kMul, kLinear, kAsIs, 0, 0, 100),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.03}, {speed, color, 0.5}, {speed, busIn(1), 1.0},
                                    {nearest, busIn(1), 1.0}, {bus2, resonance, 0.45}, {press, resonance, 0.2}, {wheel, cutoff2, 0.2}})}),
                   names({{0, 0, "Climb"}, {0, 1, "Heat"}})});
      // A wind harp: noise through a ringing comb at the note in a strong, gusting wind, and how fast
      // each creature is carried sets how loud its string sings; its climb brightens the bow and its
      // turns lean the string a few cents off.
      p.push_back({"Aeolian Harp", "Textures",
                   join({{a(noise, 0.6), a(kSkyPan, 0.7)}, filter(0, 0, kComb, 261.6, 0.75, 1.0), filter(0, 1, kLowPass, 2200, 0.1, 0.3),
                         {a(oscParam(0, kOscWave), kSine), a(level(0), 0.1)}, env(0, 0, 800, 1000, 1.0, 3000),
                         fly(0, 0.3, 0.3, 0.3), air(0, 0.05, 0.8, 0.15), sky(0.7), shape(0, 1, "Gusts", 13000, kForever), lagBus(0, 0, 250),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, resonance, 0.25}, {bus1, kPartLevel, 0.1}, {heading, cutoff2, 0.2},
                                    {turn, cutoff, 0.003}, {env2, windPush, 0.3}, {press, resonance, 0.15}, {wheel, cutoff2, 0.2}})}),
                   names({{0, 0, "Carried"}})});
      // A looping Bell as the amp in a wind: FM tines, each struck as often as its creature turns
      // (sharper turns, quicker strikes) and brighter the higher it flies.
      p.push_back({"Chime Flock", "Textures",
                   join({echo(kQuarterDot, 0.2, 0.4), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 2.01, 0.2), open(0),
                         {a(fm21, 0.25), a(kPartTranspose, 12), a(kSkyPan, 0.8), a(envParam(0, kEnvRelease), timeValue(2500))},
                         shape(0, 0, "Bell", 1800, kForever), fly(0, 0.4, 0.5, 0.35), air(0, 0.1, 0.6), sky(0.7), absBus(0, 0, 80),
                         routes(0, {{turn, busIn(0), 1.0}, {bus1, tm(0), -0.15}, {heading, fm21, 0.25}, {speed, fineOf(2), 0.05},
                                    {press, fm21, 0.2}, {wheel, tm(0), -0.08}})}),
                   names({{0, 0, "Sharp Turns"}})});
      // Drops into a still pool: sines that leap and fall on each Sparkle strike (Env 3 plays the same
      // contour onto the pitch), each creature's climb setting how far its drops fall and its speed how
      // often they land.
      p.push_back({"Water Drops", "Textures",
                   join({echo(kQuarterDot, 0.25, 0.5), osc(0, 0, kSine, 12, 0, 1.0), op(0, 1, kSine, 2.0, 0.0), open(0),
                         {a(fm21, 0.08), a(kSkyPan, 0.9), a(envParam(0, kEnvRelease), timeValue(400))},
                         shape(0, 0, "Sparkle", 3000, kForever), shape(0, 2, "Sparkle", 3000, kForever), fly(0, 0.4, 0.6, 0.35),
                         sky(0.75), ink(0, 0.4, 0.9),
                         routes(0, {{env3, coarseOf(0), 0.08}, {heading, depth(0), 0.08}, {speed, tm(0), -0.08}, {speed, tm(2), -0.08},
                                    {turn, kPartPan, 0.5}, {press, fm21, 0.2}, {wheel, depth(0), 0.06}})})});
      // Radio static: white noise and a buzzing pulse through a band pass that jumps on Random 16, its
      // steps quicker the faster each creature flies; its climb tunes the band, its turns the buzz.
      p.push_back({"Static Swarm", "Textures",
                   join({osc(0, 0, kPulse, 0, 0, 0.3), {a(shapeOf(0), 0.1), a(noise, 1.0), a(color, -0.2), a(kSkyPan, 0.8)},
                         filter(0, 0, kBandPass, 600, 0.6, 0.3), env(0, 0, 300, 1000, 1.0, 800), shape(0, 1, "Random 16", 3000, kForever),
                         fly(0, 0.45, 0.6, 0.35), sky(0.7),
                         routes(0, {{env2, cutoff, 0.2}, {speed, tm(1), -0.1}, {heading, cutoff, 0.1}, {turn, shapeOf(0), 0.2},
                                    {nearest, resonance, 0.3}, {press, noise, 0.2}, {wheel, tm(1), -0.1}})})});
      // Sparse Steps as the amp of high sines: each creature a bird keying its own call, quicker the
      // faster it flies, its pitch leaning with its climb and its overtone with its turns.
      p.push_back({"Morse Birds", "Textures",
                   join({echo(k8thDot, 0.2, 0.35), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), open(0),
                         {a(fm21, 0.05), a(kPartTranspose, 24), a(flockParam(kBehaviour), kScatter), a(kSkyPan, 0.9),
                          a(envParam(0, kEnvRelease), timeValue(200))},
                         shape(0, 0, "Sparse Steps", 2500, kForever), fly(0, 0.5, 0.6, 0.35), sky(0.8, kWrap), biBus(0, 0, 50),
                         routes(0, {{speed, tm(0), -0.12}, {heading, busIn(0), 1.0}, {bus1, kPartFine, 0.1}, {turn, ratioOf(1), 0.03},
                                    {nearest, fm21, 0.25}, {press, fm21, 0.2}, {wheel, tm(0), -0.1}})}),
                   names({{0, 0, "Climb"}})});
      // Resonant pings bouncing: a ladder on the edge of ringing, struck by a looping Bounce on its
      // cutoff; each creature's speed sets how fast it bounces, crowding tightens the ring and its climb
      // lifts the pitch of the ping.
      p.push_back({"Bubble Chamber", "Textures",
                   join({filter(0, 0, kLadder, 400, 0.8, 0.5), {a(kSkyPan, 0.8)}, env(0, 0, 600, 1000, 1.0, 2500),
                         shape(0, 1, "Bounce", 2500, kForever), fly(0, 0.4, 0.5, 0.4), sky(0.7),
                         routes(0, {{env2, cutoff, 0.35}, {speed, tm(1), -0.1}, {nearest, resonance, 0.15}, {heading, cutoff, 0.1},
                                    {turn, kPartPan, 0.4}, {press, cutoff, 0.15}, {wheel, resonance, 0.1}})})});
      // A tape loop warping: saws whose wow is each creature's climb and whose flutter (LFO 1) runs
      // faster and deeper the faster it flies; its turns tilt the tone, and a Flicker loop crackles in
      // the hiss.
      p.push_back({"Tape Murmur", "Textures",
                   join({osc(0, 0, kSaw, 0, -3, 1.0), osc(0, 1, kTriangle, 12, 0, 0.3), {a(color, -0.5), a(kSkyPan, 0.4)},
                         filter(0, 0, kLowPass, 2000, 0.1, 0.4), env(0, 0, 400, 1000, 1.0, 1500), lfo(0, 2, kLfoSine, 6.0),
                         shape(0, 1, "Flicker", 4000, kForever), fly(0, 0.3, 0.4, 0.45), sky(0.6), biBus(0, 0, 600),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, kPartFine, 0.04}, {lfo1, kPartFine, 0.01}, {speed, lfo1Rate, 0.15},
                                    {speed, depth(2), 0.02}, {env2, noise, 0.12}, {turn, cutoff, 0.1}, {press, depth(1), 0.05},
                                    {wheel, noise, 0.2}})}),
                   names({{0, 0, "Wow"}})});
      // Ice forming: a high comb rung by crackling white noise, each creature's crowding lengthening its
      // ring and its climb tuning it a few cents up and down; its speed glints the air above.
      p.push_back({"Frost", "Textures",
                   join({{a(oscParam(0, kOscWave), kSine), a(level(0), 0.0), a(noise, 0.1), a(color, 0.2), a(kSkyPan, 0.8)},
                         filter(0, 0, kComb, 523.3, 0.85, 1.0), filter(0, 1, kLowPass, 6000, 0.0, 0.3), env(0, 0, 1000, 1000, 1.0, 4000),
                         shape(0, 1, "Flicker", 3000, kForever), fly(0, 0.35, 0.5, 0.4), sky(0.7),
                         routes(0, {{env2, noise, 0.4}, {nearest, resonance, 0.25}, {heading, cutoff, 0.004}, {speed, color, 0.4},
                                    {turn, kPartPan, 0.5}, {press, resonance, 0.1}, {wheel, cutoff2, -0.2}})})});
      // Gate 8 in time with the host as the amp of plucky saws, each creature's crowding pushing its
      // steps late (Warp) so a tight flock swings; its climb opens each step.
      p.push_back({"Clockwork Swarm", "Textures",
                   join({osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kSaw, 0, 4, 0.8), {a(kSkyPan, 0.6)},
                         filter(0, 0, kLowPass, 900, 0.35, 0.4), shapeSync(0, 0, "Gate 8", kBar, kForever),
                         {a(envParam(0, kEnvRelease), timeValue(400))}, fly(0, 0.4, 0.5, 0.4), sky(0.7),
                         routes(0, {{nearest, wp(0), 0.5}, {heading, cutoff, 0.2}, {speed, resonance, 0.2}, {turn, kPartPan, 0.4},
                                    {press, cutoff, 0.15}, {wheel, cutoff, 0.2}})})});

      // ---- Industrial: heavily modulated drones and textures, metal, grind and machinery ----

      // A foundry floor: saws and noise through a comb, hard-clipped. Each creature's speed drives it,
      // its climb bends the comb, and a Saw Train loop hammers the noise, struck faster the more
      // sharply the creature turns.
      p.push_back({"Foundry", "Industrial",
                   join({osc(0, 0, kSaw, 0, -5, 0.8), osc(0, 1, kSaw, 0, 5, 0.7), {a(noise, 0.3), a(color, 0.2), a(kSkyPan, 0.6)},
                         {a(tailParam(kDriveType), kHard), a(drive, 0.3)}, filter(0, 0, kComb, 130.8, 0.5, 1.0),
                         filter(0, 1, kLowPass, 3000, 0.2, 0.3), env(0, 0, 300, 1000, 1.0, 1500), shape(0, 1, "Saw Train", 1200, kForever),
                         fly(0, 0.4, 0.5, 0.4), sky(0.6), absBus(0, 0, 100), biBus(0, 1, 200),
                         routes(0, {{speed, drive, 0.4}, {heading, busIn(1), 1.0}, {bus2, cutoff, 0.05}, {env2, noise, 0.45},
                                    {turn, busIn(0), 1.0}, {bus1, tm(1), -0.15}, {nearest, resonance, 0.25}, {press, drive, 0.3},
                                    {wheel, cutoff2, 0.25}})}),
                   names({{0, 0, "Sharp Turns"}, {0, 1, "Climb"}})});
      // A turbine spinning up: a Slow S on Env 2 raises the flock's speed over 8 s, pushing the FM
      // modulator's ratio up as it goes, and each creature's speed pushes it further, a whine climbing out of the hum; its turns
      // add feedback, its climb opens the filter. Let go and it winds down.
      p.push_back({"Turbine", "Industrial",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 0.5, 0.5),
                         {a(fm21, 0.2), a(fb2, 0.1), a(kSkyPan, 0.5)}, filter(0, 0, kLowPass, 2500, 0.2, 0.3),
                         env(0, 0, 800, 1000, 1.0, 3000), shape(0, 1, "Slow S", 8000, kOnce), fly(0, 0.3, 0.3, 0.5), sky(0.6),
                         lagBus(0, 0, 300),
                         routes(0, {{env2, fSpeed, 0.6}, {env2, ratioOf(1), 0.12}, {env2, fm21, 0.25}, {speed, busIn(0), 1.0}, {bus1, ratioOf(1), 0.08},
                                    {turn, fb2, 0.3}, {heading, cutoff, 0.15}, {press, fSpeed, 0.3}, {wheel, ratioOf(1), 0.1}})}),
                   names({{0, 0, "Spin"}})});
      // Hard sync, ground by the flock: Osc 2 synced to Osc 1, each creature's climb sweeping the synced
      // pitch, its turns its drive, its crowding the resonance; a Brownian walk keeps the grind uneven.
      p.push_back({"Rust Belt", "Industrial",
                   join({osc(0, 0, kSaw, 0, 0, 0.3), osc(0, 1, kSaw, 7, 0, 1.0), {a(oscParam(1, kOscSync), 1), a(kSkyPan, 0.5)},
                         {a(tailParam(kDriveType), kHard), a(drive, 0.3)}, filter(0, 0, kLadder, 1500, 0.3, 0.3),
                         env(0, 0, 400, 1000, 1.0, 2000), shape(0, 1, "Brownian", 15000, kForever), fly(0, 0.35, 0.5, 0.4), sky(0.6),
                         biBus(0, 0, 200), absBus(0, 1, 100),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, coarseOf(1), 0.12}, {env2, coarseOf(1), 0.08}, {turn, busIn(1), 1.0},
                                    {bus2, drive, 0.4}, {nearest, resonance, 0.35}, {press, coarseOf(1), 0.1}, {wheel, cutoff, 0.2}})}),
                   names({{0, 0, "Climb"}, {0, 1, "Sharp Turns"}})});
      // A machine breathing: a looping Breath on Env 2 opens a resonant ladder and lets in hiss, each
      // creature's speed setting how fast it breathes, its crowding how hoarse and its climb how hard it
      // drives.
      p.push_back({"Iron Lung", "Industrial",
                   join({osc(0, 0, kSaw, 0, -6, 0.8), osc(0, 1, kPulse, -12, 0, 0.6), {a(shapeOf(1), 0.3), a(noise, 0.1), a(color, -0.2)},
                         {a(tailParam(kDriveType), kTube), a(drive, 0.35), a(kSkyPan, 0.5)}, filter(0, 0, kLadder, 300, 0.5, 0.3),
                         env(0, 0, 500, 1000, 1.0, 2500), shape(0, 1, "Breath", 3500, kForever), fly(0, 0.35, 0.45, 0.4), sky(0.6),
                         lagBus(0, 0, 150),
                         routes(0, {{env2, cutoff, 0.3}, {env2, noise, 0.3}, {speed, busIn(0), 1.0}, {bus1, tm(1), -0.12},
                                    {nearest, resonance, 0.3}, {heading, drive, 0.3}, {turn, kPartPan, 0.3}, {press, drive, 0.3},
                                    {wheel, tm(1), -0.1}})}),
                   names({{0, 0, "Flight"}})});
      // Struck scrap: inharmonic FM (×3.73 and ×7.11) on a looping Collapse as the amp, each strike lower
      // than the last; each creature's turns knock its ratios off true so no two hits ring alike, its
      // speed sets how fast the pile collapses and its crowding how hard it clangs.
      p.push_back({"Scrapyard", "Industrial",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.73, 0.0), op(0, 2, kSine, 7.11, 0.15), open(0),
                         {a(fm21, 0.3), a(fm31, 0.1), a(flockParam(kBehaviour), kScatter), a(kSkyPan, 0.8),
                          a(envParam(0, kEnvRelease), timeValue(1500))},
                         shape(0, 0, "Collapse", 2500, kForever), fly(0, 0.45, 0.6, 0.35), sky(0.7),
                         routes(0, {{turn, ratioOf(1), 0.03}, {turn, ratioOf(2), -0.03}, {speed, tm(0), -0.1}, {nearest, fm21, 0.3},
                                    {heading, fm31, 0.2}, {press, fm21, 0.2}, {wheel, tm(0), -0.1}})})});
      // Mains hum and a transformer's buzz: a narrow pulse and its octave, tube-driven, with a
      // sample-and-hold crackling the filter at a rate each creature's speed sets; its climb widens the
      // pulse, its turns ring the resonance.
      p.push_back({"Power Station", "Industrial",
                   join({osc(0, 0, kPulse, 0, 0, 0.8), osc(0, 1, kPulse, -12, 0, 0.6), {a(shapeOf(0), 0.08), a(shapeOf(1), 0.15)},
                         {a(tailParam(kDriveType), kTube), a(drive, 0.5), a(kSkyPan, 0.5)}, filter(0, 0, kLowPass, 600, 0.4, 0.3),
                         env(0, 0, 200, 1000, 1.0, 1000), lfo(0, 2, kLfoHold, 6.0), fly(0, 0.35, 0.5, 0.4), sky(0.6),
                         routes(0, {{lfo1, cutoff, 0.12}, {speed, lfo1Rate, 0.25}, {heading, shapeOf(0), 0.2}, {heading, cutoff, 0.1},
                                    {nearest, drive, 0.3}, {turn, resonance, 0.2}, {press, drive, 0.3}, {wheel, depth(0), 0.1}})})});
      // Exponential FM with heavy feedback: each creature's crowding grinds the FM deeper, its turns
      // pitch the modulator sharp and flat, its climb sweeps a band pass over the noise of it.
      p.push_back({"Grinder", "Industrial",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(kFmBase + kFmType, 1), a(fm21, 0.2), a(fb2, 0.35)},
                         {a(tailParam(kDriveType), kHard), a(drive, 0.2), a(kSkyPan, 0.5), a(filterParam(0, kFilterSlope), 0)},
                         filter(0, 0, kBandPass, 1200, 0.3, 0.3), env(0, 0, 400, 1000, 1.0, 2000), fly(0, 0.4, 0.5, 0.35, 0.4), sky(0.6),
                         biBus(0, 0, 200),
                         routes(0, {{nearest, fm21, 0.35}, {turn, ratioOf(1), 0.03}, {heading, busIn(0), 1.0}, {bus1, cutoff, 0.2},
                                    {speed, fb2, 0.3}, {press, fm21, 0.2}, {wheel, drive, 0.4}})}),
                   names({{0, 0, "Climb"}})});
      // Steam under pressure: hiss in an odd-harmonic comb tuned to the note, rising in gusts; each
      // creature's climb tightens the comb, its speed whitens the hiss, its turns pan the leak.
      p.push_back({"Pressure Vessel", "Industrial",
                   join({osc(0, 0, kSaw, 0, 0, 0.15), {a(noise, 0.5), a(kSkyPan, 0.6)}, filter(0, 0, kCombOdd, 261.6, 0.65, 1.0),
                         filter(0, 1, kLowPass, 4000, 0.0, 0.0), env(0, 0, 600, 1000, 1.0, 2500), shape(0, 1, "Gusts", 9000, kForever),
                         fly(0, 0.35, 0.5, 0.4), sky(0.6),
                         routes(0, {{env2, noise, 0.4}, {env2, resonance, 0.2}, {heading, resonance, 0.15}, {speed, color, 0.6},
                                    {turn, kPartPan, 0.5}, {nearest, cutoff2, 0.2}, {press, noise, 0.2}, {wheel, resonance, 0.1}})})});
      // A conveyor in time with the host: Gate 8 as the amp of hard-driven saws, each creature's speed
      // dragging its steps late, an Arp Up climbing the filter every two bars and the creature's climb
      // opening it further.
      p.push_back({"Conveyor", "Industrial",
                   join({echo(4, 0.15, 0.3), osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kSquare, -12, 0, 0.5), {a(tailParam(kDriveType), kHard), a(drive, 0.3)},
                         {a(kSkyPan, 0.5), a(envParam(0, kEnvRelease), timeValue(300))}, filter(0, 0, kLadder, 500, 0.45, 0.3),
                         shapeSync(0, 0, "Gate 8", kBar, kForever), shapeSync(0, 1, "Arp Up", kBars2, kForever), fly(0, 0.4, 0.5, 0.4),
                         sky(0.6),
                         routes(0, {{speed, wp(0), 0.5}, {heading, cutoff, 0.2}, {env2, cutoff, 0.2}, {nearest, resonance, 0.25},
                                    {turn, kPartPan, 0.3}, {press, drive, 0.3}, {wheel, cutoff, 0.2}})})});
      // A dying transmission: FM whose modulator steps on Random 16, quicker the faster each creature
      // flies, through a comb its climb retunes; crowding scrambles it with Neighbour FM.
      p.push_back({"Signal Decay", "Industrial",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.3), {a(fm21, 0.15), a(kSkyPan, 0.7)},
                         filter(0, 0, kCombOdd, 261.6, 0.4, 1.0), shape(0, 1, "Random 16", 4000, kForever), between(0, 0.15, 0),
                         env(0, 0, 300, 1000, 1.0, 1500), fly(0, 0.4, 0.6, 0.35), sky(0.7),
                         routes(0, {{env2, ratioOf(1), 0.08}, {env2, fm21, 0.2}, {speed, tm(1), -0.1}, {heading, cutoff, 0.05},
                                    {nearest, fNfm, 0.4}, {turn, kPartPan, 0.4}, {press, fm21, 0.2}, {wheel, tm(1), -0.1}})})});
      // Engines idling: detuned saws throbbing on LFO 1, whose rate is each creature's speed (an engine
      // revving) and whose depth is how near its neighbours are; a 26 s Wander leans on the drive.
      p.push_back({"Engine Room", "Industrial",
                   join({osc(0, 0, kSaw, 0, -6, 0.9), osc(0, 1, kSaw, 0, 6, 0.9), osc(0, 2, kSquare, -12, 0, 0.4),
                         {a(tailParam(kDriveType), kTube), a(drive, 0.3), a(kSkyPan, 0.5)}, filter(0, 0, kLowPass, 500, 0.3, 0.3),
                         env(0, 0, 500, 1000, 1.0, 2000), lfo(0, 2, kLfoSine, 6.0), shape(0, 1, "Wander", 26000, kForever),
                         fly(0, 0.35, 0.45, 0.4), sky(0.6), busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 30),
                         routes(0, {{lfo1, busIn(0), 1.0}, {nearest, busIn(0), 1.0}, {bus1, kPartLevel, 0.1}, {bus1, cutoff, 0.05},
                                    {speed, lfo1Rate, 0.3}, {env2, drive, 0.4}, {heading, cutoff, 0.15}, {press, lfo1Rate, 0.15},
                                    {wheel, drive, 0.3}})}),
                   names({{0, 0, "Throb x Crowd"}})});
      // Shortwave between stations: noise and a whistle through a comb whose tuning a sample-and-hold
      // throws about, at a rate set by each creature's speed; its climb sweeps a band pass over it all
      // and its turns bend the whistle.
      p.push_back({"Dead Radio", "Industrial",
                   join({osc(0, 0, kSine, 12, 0, 0.2), {a(noise, 1.0), a(color, -0.2), a(kSkyPan, 0.7), a(filterParam(1, kFilterSlope), 0)},
                         filter(0, 0, kComb, 261.6, 0.65, 0.5), filter(0, 1, kBandPass, 1500, 0.3, 0.0), env(0, 0, 300, 1000, 1.0, 1200),
                         lfo(0, 2, kLfoHold, 5.0), fly(0, 0.4, 0.6, 0.35), sky(0.7),
                         routes(0, {{lfo1, cutoff, 0.1}, {speed, lfo1Rate, 0.3}, {heading, cutoff2, 0.2}, {turn, fineOf(0), 0.3},
                                    {nearest, resonance, 0.25}, {press, noise, 0.2}, {wheel, depth(0), 0.1}})})});
      // A furnace roaring: a resonant ladder on the edge of screaming, tube-driven, a Surge loop raising
      // the drive, each creature's climb opening the ladder and crowding pushing the resonance into song.
      p.push_back({"Furnace", "Industrial",
                   join({osc(0, 0, kSaw, 0, -5, 0.9), osc(0, 1, kSaw, 0, 5, 0.9), {a(noise, 0.1), a(color, -0.5), a(kSkyPan, 0.5)},
                         {a(tailParam(kDriveType), kTube), a(drive, 0.3)}, filter(0, 0, kLadder, 350, 0.6, 0.3),
                         {a(filterParam(0, kFilterDrive), 0.3)}, env(0, 0, 600, 1000, 1.0, 2500), shape(0, 1, "Surge", 11000, kForever),
                         fly(0, 0.35, 0.45, 0.4), sky(0.6),
                         routes(0, {{env2, drive, 0.4}, {heading, cutoff, 0.2}, {nearest, resonance, 0.3}, {speed, noise, 0.2},
                                    {turn, kPartPan, 0.3}, {press, cutoff, 0.15}, {wheel, resonance, 0.1}})})});
      // Layer: a driven saw drone that flees, and B's hunters a fifth up, square saw trains that quicken
      // the faster they chase; when one catches its prey it bites, ring-modulating it.
      p.push_back({"Machine Swarm", "Industrial",
                   join({{g(kPlayMode, 1.0)}, osc(0, 0, kSaw, 0, -5, 0.9), osc(0, 1, kSaw, 0, 5, 0.8),
                         {a(tailParam(kDriveType), kHard), a(drive, 0.25), a(flockParam(kBehaviour), kFlee), a(kSkyPan, 0.5)},
                         filter(0, 0, kLadder, 600, 0.35, 0.3), env(0, 0, 400, 1000, 1.0, 2000), fly(0, 0.35, 0.4, 0.5),
                         routes(0, {{heading, cutoff, 0.2}, {nearest, resonance, 0.2}, {turn, kPartPan, 0.3}, {press, drive, 0.3},
                                    {wheel, cutoff, 0.2}}),
                         osc(1, 0, kSquare, 0, 0, 0.6), {b(kPartTranspose, 7), b(kSkyPan, 0.7), b(kPartLevel, -4)},
                         filter(1, 0, kLowPass, 1500, 0.3, 0.3), shape(1, 0, "Saw Train", 700, kForever),
                         {b(envParam(0, kEnvRelease), timeValue(500))}, fly(1, 0.55, 0.6, 0.3), between(1, 0, 0, 0.8),
                         routes(1, {{speed, tm(0), -0.12}, {nearest, cutoff, 0.2}, {heading, resonance, 0.3}, {press, fSpeed, 0.3},
                                    {wheel, flockParam(kBite), 0.2}})})});

      // ---- Landscapes: both parts at once, two flocks in one sky ----

      // Press crossfades a calm sky into a storm: part A is FM glass drifting in a slow shoal, part B a
      // driven comb of noise and saws whose hunters bite. Each part's creatures play its own sound:
      // A's climb rings its glass, B's speed drives its grind and its turns hammer its comb.
      p.push_back({"Glass and Iron", "Landscapes",
                   join({{g(kPlayMode, 5.0), g(kCrossfadeSource, 1.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 0.5, 0.3), open(0),
                         {a(fm21, 0.1), a(kSkyPan, 0.6)}, env(0, 0, 1500, 1000, 1.0, 5000), fly(0, 0.2, 0.4, 0.4), between(0, 0, 0.6),
                         lagBus(0, 0, 250),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, fm21, 0.3}, {turn, fineOf(0), 0.05}, {speed, level(2), 0.4},
                                    {wheel, fm21, 0.2}}),
                         osc(1, 0, kSaw, 0, -6, 0.6), osc(1, 1, kSaw, 0, 6, 0.6),
                         {b(kPartTranspose, 0), b(noise, 0.4), b(color, 0.2), b(tailParam(kDriveType), kHard), b(drive, 0.35),
                          b(kSkyPan, 0.6), b(kPartLevel, -3)},
                         filter(1, 0, kComb, 130.8, 0.6, 1.0), filter(1, 1, kLowPass, 3500, 0.2, 0.3), env(1, 0, 600, 1000, 1.0, 2500),
                         shape(1, 1, "Saw Train", 1100, kForever), fly(1, 0.5, 0.6, 0.35), between(1, 0, 0, 0.6), absBus(1, 0, 100),
                         routes(1, {{speed, drive, 0.4}, {env2, noise, 0.4}, {turn, busIn(0), 1.0}, {bus1, tm(1), -0.15},
                                    {heading, cutoff, 0.04}, {nearest, resonance, 0.25}, {wheel, cutoff2, 0.2}})}),
                   names({{0, 0, "Climb"}, {1, 0, "Sharp Turns"}})});
      // Layer: a soft saw drone in a slow shoal, and high FM bells (part B, an octave up) that hunt it.
      // When a bell catches a creature of the drone it bites, ring-modulating it; the bells' speed
      // brightens them and their turns shift their ratio.
      p.push_back({"Predator Weather", "Landscapes",
                   join({{g(kPlayMode, 1.0)}, osc(0, 0, kSaw, 0, -6, 0.9), osc(0, 1, kSaw, 0, 6, 0.9),
                         {a(panOf(0), -0.4), a(panOf(1), 0.4), a(kSkyPan, 0.5)}, filter(0, 0, kLowPass, 700, 0.25, 0.3),
                         env(0, 0, 2000, 1000, 1.0, 5000), fly(0, 0.2, 0.4, 0.4), biBus(0, 0, 300),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.18}, {turn, kPartPan, 0.3}, {nearest, resonance, 0.25},
                                    {press, cutoff, 0.15}, {wheel, cutoff, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 3.5, 0.0), open(1),
                         {b(kPartTranspose, 12), b(fm21, 0.15), b(kSkyPan, 0.8), b(kPartLevel, -8)},
                         env(1, 0, 600, 1000, 1.0, 3000), fly(1, 0.5, 0.5, 0.3), between(1, 0, 0, 0.9),
                         routes(1, {{speed, fm21, 0.3}, {turn, ratioOf(1), 0.02}, {heading, kPartLevel, 0.06}, {press, fSpeed, 0.3},
                                    {wheel, flockParam(kBite), 0.1}})}),
                   names({{0, 0, "Climb"}})});
      // Layer: a low drone and, an octave and a fifth up, a scattered flock of birds keying Sparse Steps.
      // The drone's creatures open its filter as they climb; the birds sing quicker as they fly faster
      // and their calls lean with their turns.
      p.push_back({"Dawn Chorus", "Landscapes",
                   join({echo(k8thDot, 0.18, 0.4), {g(kPlayMode, 1.0)}, osc(0, 0, kSaw, 0, -5, 0.8), osc(0, 1, kTriangle, 0, 5, 0.8), osc(0, 2, kSine, -12, 0, 0.4),
                         {a(kSkyPan, 0.4)}, filter(0, 0, kLowPass, 600, 0.2, 0.3), env(0, 0, 2500, 1000, 1.0, 5000),
                         fly(0, 0.18, 0.35, 0.45), biBus(0, 0, 400),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.15}, {speed, level(2), 0.3}, {press, cutoff, 0.15},
                                    {wheel, cutoff, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.0), open(1),
                         {b(kPartTranspose, 19), b(fm21, 0.05), b(flockParam(kBehaviour), kScatter), b(kSkyPan, 0.9), b(kPartLevel, -6),
                          b(envParam(0, kEnvRelease), timeValue(200))},
                         shape(1, 0, "Sparse Steps", 2200, kForever), fly(1, 0.5, 0.7, 0.3),
                         routes(1, {{speed, tm(0), -0.12}, {turn, kPartFine, 0.08}, {nearest, fm21, 0.2}, {heading, ratioOf(1), 0.02},
                                    {wheel, tm(0), -0.1}})}),
                   names({{0, 0, "Climb"}})});
      // Velocity picks the weather: soft notes play a breathing pad whose creatures open its filter,
      // hard ones a grinding sync drone whose climb sweeps the synced pitch and whose speed drives it.
      p.push_back({"Storm Front", "Landscapes",
                   join({{g(kPlayMode, 4.0), g(kVelocitySplit, 90.0)}, osc(0, 0, kSaw, 0, -7, 0.9), osc(0, 1, kSaw, 0, 7, 0.9),
                         {a(panOf(0), -0.4), a(panOf(1), 0.4), a(kSkyPan, 0.5), a(kPartLevel, -9)}, filter(0, 0, kLowPass, 800, 0.2, 0.3),
                         env(0, 0, 1500, 1000, 1.0, 4000), shape(0, 1, "Breath", 8000, kForever), fly(0, 0.22, 0.4, 0.4),
                         routes(0, {{env2, cutoff, 0.12}, {heading, cutoff, 0.15}, {turn, kPartPan, 0.3}, {press, cutoff, 0.15},
                                    {wheel, cutoff, 0.2}}),
                         osc(1, 0, kSaw, 0, 0, 0.3), osc(1, 1, kSaw, 7, 0, 1.0),
                         {b(kPartTranspose, 0), b(oscParam(1, kOscSync), 1), b(tailParam(kDriveType), kHard), b(drive, 0.35),
                          b(kSkyPan, 0.6), b(kPartLevel, 0)},
                         filter(1, 0, kLadder, 1200, 0.35, 0.3), env(1, 0, 300, 1000, 1.0, 2500), fly(1, 0.45, 0.5, 0.35),
                         biBus(1, 0, 150),
                         routes(1, {{heading, busIn(0), 1.0}, {bus1, coarseOf(1), 0.12}, {speed, drive, 0.35}, {nearest, resonance, 0.3},
                                    {press, coarseOf(1), 0.08}, {wheel, cutoff, 0.2}})}),
                   names({{1, 0, "Climb"}})});
      // Split at C3: below, a dark ladder drone whose creatures open it as they climb; above, a wind
      // harp in a strong gusting wind, each string singing as loud as its creature is carried.
      p.push_back({"Low Tide, High Wind", "Landscapes",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0)},
                         osc(0, 0, kSine, 0, 0, 0.1), {a(noise, 0.6), a(kSkyPan, 0.7)}, filter(0, 0, kComb, 261.6, 0.75, 1.0),
                         filter(0, 1, kLowPass, 2200, 0.1, 0.3), env(0, 0, 800, 1000, 1.0, 3000), fly(0, 0.3, 0.3, 0.3),
                         air(0, 0.05, 0.8, 0.15), lagBus(0, 0, 250),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, resonance, 0.25}, {bus1, kPartLevel, 0.1}, {heading, cutoff2, 0.2},
                                    {press, resonance, 0.15}, {wheel, cutoff2, 0.2}}),
                         osc(1, 0, kSaw, 0, -6, 0.9), osc(1, 1, kSaw, 0, 6, 0.9), osc(1, 2, kSine, -12, 0, 0.5),
                         {b(kPartTranspose, 0), b(flockParam(kBehaviour), 0), b(kSkyPan, 0.4)}, filter(1, 0, kLadder, 200, 0.45, 0.3),
                         env(1, 0, 1500, 1000, 1.0, 4000), fly(1, 0.18, 0.35, 0.5), biBus(1, 0, 400),
                         routes(1, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.18}, {nearest, resonance, 0.2}, {press, cutoff, 0.15},
                                    {wheel, cutoff, 0.2}})}),
                   names({{0, 0, "Carried"}, {1, 0, "Climb"}})});
      // Layer: two flocks that ring each other. A flees and B hunts it a fifth up, both with Neighbour
      // FM, so each species shimmers when it gathers; A's climb opens its filter, B's speed its FM.
      p.push_back({"Two Flocks", "Landscapes",
                   join({{g(kPlayMode, 1.0)}, osc(0, 0, kSaw, 0, -4, 0.8), op(0, 1, kSine, 1.0, 0.5),
                         {a(flockParam(kBehaviour), kFlee), a(kSkyPan, 0.6)}, filter(0, 0, kLowPass, 900, 0.25, 0.3),
                         env(0, 0, 1200, 1000, 1.0, 4000), fly(0, 0.3, 0.4, 0.4), between(0, 0.3, 0.5), biBus(0, 0, 300),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.15}, {nearest, fNfm, 0.3}, {turn, kPartPan, 0.3},
                                    {press, cutoff, 0.15}, {wheel, fNfm, 0.3}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.4), open(1),
                         {b(kPartTranspose, 7), b(fm21, 0.1), b(kSkyPan, 0.6), b(kPartLevel, -6)},
                         env(1, 0, 1200, 1000, 1.0, 4000), fly(1, 0.45, 0.5, 0.35), between(1, 0.3, 0.5),
                         routes(1, {{speed, fm21, 0.3}, {heading, level(1), 0.3}, {turn, fineOf(1), 0.05}, {press, fm21, 0.2},
                                    {wheel, fNfm, 0.3}})}),
                   names({{0, 0, "Climb"}})});
      // Layer: a hive. A buzzing pulse drone whose widths the creatures bend as they turn, and part B's
      // workers an octave up, chirping on a Stutter amp quicker the faster they fly.
      p.push_back({"Hive", "Landscapes",
                   join({{g(kPlayMode, 1.0)}, osc(0, 0, kPulse, 0, -4, 0.8), osc(0, 1, kPulse, 0, 4, 0.8),
                         {a(shapeOf(0), 0.15), a(shapeOf(1), 0.2), a(kSkyPan, 0.5)}, filter(0, 0, kLowPass, 900, 0.3, 0.3),
                         env(0, 0, 1000, 1000, 1.0, 3000), fly(0, 0.3, 0.5, 0.4),
                         routes(0, {{turn, shapeOf(0), 0.15}, {turn, shapeOf(1), -0.15}, {heading, cutoff, 0.15}, {nearest, resonance, 0.25},
                                    {press, cutoff, 0.15}, {wheel, cutoff, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.0), open(1),
                         {b(kPartTranspose, 12), b(fm21, 0.3), b(flockParam(kBehaviour), kScatter), b(kSkyPan, 0.9), b(kPartLevel, -8),
                          b(envParam(0, kEnvRelease), timeValue(300))},
                         shape(1, 0, "Stutter", 800, kForever), fly(1, 0.6, 0.8, 0.3), air(1, 0.6, 0.2),
                         routes(1, {{speed, tm(0), -0.12}, {turn, ratioOf(1), 0.04}, {heading, fm21, 0.2}, {wheel, tm(0), -0.1}})})});
      // Layer: a slow migration. A low sine-and-saw drone and, two octaves up, a thin alt-sine line
      // flying with it; both pulled into just intervals, the lower climbing open, the upper chorused by
      // its turns. Arch loops of 34 s and 21 s raise each
      // flock's speed, out of step with each other.
      p.push_back({"Migration", "Landscapes",
                   join({{g(kPlayMode, 1.0)}, osc(0, 0, kSaw, 0, -5, 0.7), osc(0, 1, kSine, -12, 0, 0.6), {a(kSkyPan, 0.4)},
                         filter(0, 0, kLowPass, 500, 0.2, 0.3), env(0, 0, 3000, 1000, 1.0, 6000), fly(0, 0.18, 0.35, 0.45),
                         between(0, 0, 0.7), shape(0, 1, "Arch", 34000, kForever), biBus(0, 0, 500),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.18}, {env2, fSpeed, 0.4}, {press, cutoff, 0.15},
                                    {wheel, cutoff, 0.2}}),
                         op(1, 0, kAltSine, 1.0, 1.0), own(24, -10), {b(kSkyPan, 0.8)}, open(1), env(1, 0, 3000, 1000, 1.0, 6000),
                         fly(1, 0.25, 0.4, 0.35), between(1, 0, 0.7), shape(1, 1, "Arch", 21000, kForever),
                         routes(1, {{turn, kPartFine, 0.04}, {heading, kPartLevel, 0.08}, {env2, fSpeed, 0.4}, {wheel, kPartLevel, 0.1}})}),
                   names({{0, 0, "Climb"}})});

      // ---- Pads: the classic shapes, flown by the flock ----

      // A warm analog pad: two saws a few cents apart, each creature's climb opening its filter and its
      // turns panning it, its speed adding a touch of resonance.
      p.push_back({"Flock Pad", "Pads",
                   join({osc(0, 0, kSaw, 0, -9, 0.9), osc(0, 1, kSaw, 0, 9, 0.9), {a(panOf(0), -0.3), a(panOf(1), 0.3), a(kSkyPan, 0.4)},
                         filter(0, 0, kLowPass, 900, 0.2, 0.3), env(0, 0, 500, 1000, 1.0, 2000), env(0, 1, 800, 2000, 0.5, 2000),
                         fly(0, 0.3, 0.4, 0.45), biBus(0, 0, 200),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff, 0.12}, {env2, cutoff, 0.12}, {turn, kPartPan, 0.25},
                                    {speed, resonance, 0.2}, {press, cutoff, 0.15}, {wheel, cutoff, 0.25}})}),
                   names({{0, 0, "Climb"}})});
      // FM glass: a ×3 modulator over a carrier and a sub an octave down, each creature's climb
      // brightening it, how near its neighbours are adding feedback and its turns a slow beat.
      p.push_back({"Glass Flock", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 0.5, 0.4), open(0),
                         {a(fm21, 0.15), a(kSkyPan, 0.5)}, env(0, 0, 600, 1000, 1.0, 2500), fly(0, 0.3, 0.4, 0.45), lagBus(0, 0, 200),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, fm21, 0.25}, {nearest, fb2, 0.25}, {turn, fineOf(2), 0.04},
                                    {press, fm21, 0.15}, {wheel, fm21, 0.25}})}),
                   names({{0, 0, "Climb"}})});
      // Pulse-width strings: LFO 1 and each creature's turns move the widths, its climb opens the
      // filter, and its speed is a slow vibrato's depth.
      p.push_back({"PWM Strings", "Pads",
                   join({osc(0, 0, kPulse, 0, -6, 0.9), osc(0, 1, kPulse, 0, 6, 0.9),
                         {a(shapeOf(0), 0.35), a(shapeOf(1), 0.35), a(panOf(0), -0.4), a(panOf(1), 0.4), a(kSkyPan, 0.4)},
                         filter(0, 0, kLowPass, 2000, 0.15, 0.4), env(0, 0, 400, 1000, 1.0, 1800), lfo(0, 2, kLfoSine, 0.6),
                         lfo(0, 3, kLfoSine, 5.0), fly(0, 0.3, 0.4, 0.45), busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 100),
                         routes(0, {{lfo1, shapeOf(0), 0.12}, {turn, shapeOf(1), 0.15}, {heading, cutoff, 0.12}, {kSourceLfo1 + 1, busIn(0), 1.0},
                                    {speed, busIn(0), 1.0}, {bus1, kPartFine, 0.03}, {press, cutoff, 0.15}, {wheel, cutoff, 0.2}})}),
                   names({{0, 0, "Vibrato x Speed"}})});
      // A choir on "ah": each creature's climb leans its vowel toward "eh", its turns brighten the
      // second formant, and Tune Pull holds the chord.
      p.push_back({"Choir Flock", "Pads",
                   join({osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kPulse, 0, 4, 0.7), {a(shapeOf(1), 0.3), a(noise, 0.02), a(kSkyPan, 0.4)},
                         vox(0, 750, 1150, 0.45), env(0, 0, 500, 1000, 1.0, 2000), lfo(0, 2, kLfoSine, 5.0), fly(0, 0.25, 0.35, 0.45),
                         between(0, 0, 0.7), biBus(0, 0, 200),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, cutoff2, 0.07}, {turn, cutoff2, 0.04}, {lfo1, kPartFine, 0.015},
                                    {speed, cutoff, 0.04}, {press, cutoff2, 0.05}, {wheel, depth(3), 0.02}})}),
                   names({{0, 0, "Climb"}})});
      // Brass that swells: two saws, the filter opening a little after the note speaks, each creature's
      // climb brightening it and its speed adding vibrato.
      p.push_back({"Flock Brass", "Pads",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 0.9), {a(kSkyPan, 0.4), a(envParam(1, kEnvVelocity), 0.5)},
                         filter(0, 0, kLowPass, 450, 0.15, 0.4), env(0, 0, 60, 500, 0.9, 300), env(0, 1, 90, 600, 0.45, 300),
                         lfo(0, 2, kLfoSine, 5.0), fly(0, 0.3, 0.4, 0.5), busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 100),
                         routes(0, {{env2, cutoff, 0.35}, {heading, cutoff, 0.12}, {lfo1, busIn(0), 1.0}, {speed, busIn(0), 1.0},
                                    {bus1, kPartFine, 0.04}, {press, cutoff, 0.2}, {wheel, cutoff, 0.15}})}),
                   names({{0, 0, "Vibrato x Speed"}})});
      // Three saws spread wide: each creature's turns pull them apart and back, so the unison breathes
      // wider and narrower note by note, and its climb opens the filter.
      p.push_back({"Wide Unison", "Pads",
                   join({osc(0, 0, kSaw, 0, -10, 0.8), osc(0, 1, kSaw, 0, 0, 0.8), osc(0, 2, kSaw, 0, 10, 0.8),
                         {a(panOf(0), -0.7), a(panOf(2), 0.7), a(kSkyPan, 0.3)}, filter(0, 0, kLowPass, 1500, 0.15, 0.4),
                         env(0, 0, 300, 1000, 1.0, 1500), fly(0, 0.3, 0.4, 0.45), absBus(0, 0, 200),
                         routes(0, {{turn, busIn(0), 1.0}, {bus1, fineOf(0), -0.1}, {bus1, fineOf(2), 0.1}, {heading, cutoff, 0.12},
                                    {press, cutoff, 0.15}, {wheel, cutoff, 0.2}})}),
                   names({{0, 0, "Sharp Turns"}})});
      // Soft sines that ring each other when they gather (Neighbour FM) and settle into just intervals
      // (Tune Pull); each creature's climb lifts an octave into its voice.
      p.push_back({"Dream Pad", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.4), op(0, 2, kTriangle, 1.0, 0.3), open(0),
                         {a(oscParam(2, kOscFine), 6), a(kSkyPan, 0.5)}, env(0, 0, 800, 1000, 1.0, 3000), fly(0, 0.25, 0.4, 0.4, 0.4),
                         school(0, 0.6, 0.6, 0.4), between(0, 0.25, 0.7),
                         routes(0, {{heading, level(1), 0.3}, {nearest, fNfm, 0.2}, {turn, fineOf(2), 0.04}, {press, fNfm, 0.2},
                                    {wheel, fNfm, 0.3}})})});
      // A slow filter sweep whose rate is each creature's speed: LFO 1 sweeps the cutoff, faster the
      // faster the creature flies, deeper the higher it climbs.
      p.push_back({"Sweep Pad", "Pads",
                   join({osc(0, 0, kSaw, 0, -6, 0.9), osc(0, 1, kSquare, 0, 6, 0.6), {a(kSkyPan, 0.4)},
                         filter(0, 0, kLowPass, 700, 0.35, 0.3), env(0, 0, 600, 1000, 1.0, 2500), lfo(0, 2, kLfoTriangle, 0.15),
                         fly(0, 0.3, 0.4, 0.45),
                         routes(0, {{lfo1, cutoff, 0.15}, {speed, lfo1Rate, 0.3}, {heading, depth(0), 0.1}, {turn, kPartPan, 0.3},
                                    {press, cutoff, 0.15}, {wheel, resonance, 0.3}})})});

      // ---- Keys and Bells: played sounds, each note shaded by its creature ----

      // A DX electric piano: a 1:1 pair for the body and a ×14 tine, both harder the harder you play.
      // Each creature's climb brightens its note's bark, and its speed is the depth of a tremolo.
      p.push_back({"Flock Tines", "Keys and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 14.0, 0.0), open(0),
                         {a(fm21, 0.15), a(kSkyPan, 0.4)}, env(0, 0, 1, 5000, 0.0, 400), env(0, 1, 0.5, 1500, 0.2, 400),
                         env(0, 2, 0.5, 60, 0.0, 40), lfo(0, 2, kLfoSine, 4.5), fly(0, 0.35, 0.4, 0.45),
                         busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 50),
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {heading, fm21, 0.12},
                                    {lfo1, busIn(0), 1.0}, {speed, busIn(0), 1.0}, {bus1, kPartLevel, 0.05}, {key, envParam(0, kEnvDecay), -0.08},
                                    {wheel, depth(6), 0.05}})}),
                   names({{0, 0, "Tremolo x Speed"}})});
      // A vibraphone: ×4 FM with a tremolo whose depth is how near each creature's neighbours are, so a
      // gathered chord shimmers; its turns pan it.
      p.push_back({"Flock Vibes", "Keys and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.0), open(0), {a(fm21, 0.15), a(kSkyPan, 0.5)},
                         env(0, 0, 0.5, 3000, 0.0, 1200), env(0, 2, 0.5, 300, 0.0, 200), lfo(0, 2, kLfoSine, 5.5),
                         fly(0, 0.35, 0.4, 0.45), busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 50),
                         routes(0, {{env3, fm21, 0.2}, {velocity, fm21, 0.15}, {lfo1, busIn(0), 1.0}, {nearest, busIn(0), 1.0},
                                    {bus1, kPartLevel, 0.06}, {turn, kPartPan, 0.3}, {wheel, depth(4), 0.05}})}),
                   names({{0, 0, "Tremolo x Crowd"}})});
      // Tubular bells: a ×3.5 modulator over a long ring and a carrier an octave up; each creature's
      // turns knock the modulator off its ratio, so a held bell's colour wavers.
      p.push_back({"Flock Bells", "Keys and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 2.01, 0.25), open(0),
                         {a(fm21, 0.3), a(kSkyPan, 0.6)}, env(0, 0, 0.5, 6000, 0.0, 3000), env(0, 2, 0.5, 1500, 0.0, 1500),
                         fly(0, 0.35, 0.45, 0.4),
                         routes(0, {{env3, fm21, 0.25}, {velocity, fm21, 0.2}, {turn, ratioOf(1), 0.015}, {heading, fineOf(2), 0.03},
                                    {wheel, fm21, 0.2}})})});
      // A kalimba: a ×3 FM blip with an inharmonic ×5.95 partial; each creature's climb brightens the
      // blip and its turns pan it.
      p.push_back({"Kalimba Swarm", "Keys and Bells",
                   join({echo(k8thDot, 0.2, 0.35), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 5.95, 0.06), open(0),
                         {a(fm21, 0.1), a(kSkyPan, 0.7)}, env(0, 0, 0.5, 1100, 0.0, 600), env(0, 2, 0.5, 60, 0.0, 50),
                         fly(0, 0.4, 0.5, 0.4),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.1}, {heading, fm21, 0.1}, {turn, kPartPan, 0.3},
                                    {wheel, level(2), 0.2}})})});
      // A drawbar organ whose rotary speaker runs at each creature's speed, so a chord's notes spin at
      // their own rates; a key click, and the wheel speeds them all.
      p.push_back({"Rotor Organ", "Keys and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 0.5, 0.8), op(0, 2, kSine, 1.5, 0.7), open(0),
                         {a(kVelocityToAmp, 0.0), a(color, 0.5), a(tailParam(kDriveType), kTube), a(drive, 0.25),
                          a(lfoParam(2, kLfoTrigger), 1)},
                         lfo(0, 2, kLfoSine, 0.8), env(0, 0, 3, 100, 1.0, 40), env(0, 2, 0.5, 25, 0.0, 10), fly(0, 0.35, 0.4, 0.45),
                         routes(0, {{env3, noise, 0.2}, {lfo1, kPartPan, 0.15}, {lfo1, kPartFine, 0.03}, {speed, lfo1Rate, 0.25},
                                    {wheel, lfo1Rate, 0.27}, {press, drive, 0.3}})})});
      // A plucked saw and pulse through a resonant filter that each creature's climb tunes, its turns
      // panning it; the wheel lengthens the pluck.
      p.push_back({"Flock Pluck", "Keys and Bells",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 7, 0.6), {a(kSkyPan, 0.6)},
                         filter(0, 0, kLowPass, 500, 0.45, 0.3), env(0, 0, 1, 800, 0.1, 300), env(0, 1, 0.5, 260, 0.0, 200),
                         fly(0, 0.4, 0.5, 0.4),
                         routes(0, {{env2, cutoff, 0.5}, {velocity, cutoff, 0.2}, {heading, cutoff, 0.1}, {turn, kPartPan, 0.35},
                                    {speed, resonance, 0.15}, {wheel, envParam(1, kEnvDecay), 0.15}})})});
      // A music box: a high triangle with a faint ×3, its tines ringing out; each creature's climb leans
      // its note a few cents and its speed adds a tremble.
      p.push_back({"Music Box Flock", "Keys and Bells",
                   join({echo(kQuarterDot, 0.2, 0.4), op(0, 0, kTriangle, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.1), op(0, 2, kSine, 4.0, 0.0), open(0),
                         {a(kPartTranspose, 12), a(fm31, 0.08), a(kSkyPan, 0.6)}, env(0, 0, 0.5, 2500, 0.0, 1200),
                         env(0, 2, 0.5, 80, 0.0, 50), lfo(0, 2, kLfoSine, 6.0), fly(0, 0.35, 0.45, 0.4),
                         busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 50),
                         routes(0, {{env3, fm31, 0.2}, {heading, kPartFine, 0.03}, {lfo1, busIn(0), 1.0}, {speed, busIn(0), 1.0},
                                    {bus1, kPartLevel, 0.04}, {wheel, fm31, 0.2}})}),
                   names({{0, 0, "Tremble x Speed"}})});

      // ---- Bass and Leads: one creature each, playing its own tone ----

      // A sine sub with a quiet octave; the creature's turns add a little feedback, so it growls only
      // as it banks, and its climb lifts the octave.
      p.push_back({"Sub Flight", "Bass and Leads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.12), {a(fb1, 0.08), a(kVelocityToAmp, 0.4)}, mono(0, 0),
                         env(0, 0, 2, 300, 1.0, 80), open(0), fly(0, 0.4, 0.4, 0.5), absBus(0, 0, 80),
                         routes(0, {{turn, busIn(0), 1.0}, {bus1, fb1, 0.2}, {heading, level(1), 0.2}, {wheel, fb1, 0.3}})}),
                   names({{0, 0, "Sharp Turns"}})});
      // A saw into a resonant, driven ladder with glide between held notes: the creature's climb opens
      // it, its speed raises the resonance, and harder notes accent the envelope.
      p.push_back({"Acid Flight", "Bass and Leads",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), filter(0, 0, kLadder, 220, 0.65, 0.3), {a(filterParam(0, kFilterDrive), 0.35)},
                         mono(0, 70), env(0, 0, 1, 300, 0.8, 60), env(0, 1, 1, 220, 0.0, 80), {a(envParam(1, kEnvVelocity), 0.5)},
                         fly(0, 0.45, 0.5, 0.45), biBus(0, 0, 100),
                         routes(0, {{env2, cutoff, 0.45}, {velocity, cutoff, 0.12}, {heading, busIn(0), 1.0}, {bus1, cutoff, 0.12},
                                    {speed, resonance, 0.2}, {press, cutoff, 0.2}, {wheel, resonance, 0.25}})}),
                   names({{0, 0, "Climb"}})});
      // Two saws over a sub, their detune pulled wider and narrower by the creature's turns; its climb
      // opens the filter, and drive holds it together.
      p.push_back({"Reese Flight", "Bass and Leads",
                   join({osc(0, 0, kSaw, 0, -11, 1.0), osc(0, 1, kSaw, 0, 11, 1.0), osc(0, 2, kSine, -12, 0, 0.6),
                         filter(0, 0, kLowPass, 500, 0.1, 0.4), {a(drive, 0.35)}, mono(0, 40), env(0, 0, 3, 400, 1.0, 120),
                         fly(0, 0.4, 0.4, 0.5),
                         routes(0, {{turn, fineOf(1), 0.06}, {heading, cutoff, 0.15}, {speed, drive, 0.2}, {press, cutoff, 0.2},
                                    {wheel, cutoff, 0.35}})})});
      // A DX bass: 1:1 FM with feedback on the modulator, its depth dying away fast; the creature's
      // speed adds feedback grit and its climb holds the FM up.
      p.push_back({"FM Flight Bass", "Bass and Leads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fm21, 0.22), a(fb2, 0.3)}, mono(0, 0),
                         env(0, 0, 1, 400, 0.9, 70), env(0, 2, 0.5, 250, 0.15, 100), open(0), fly(0, 0.4, 0.4, 0.5),
                         routes(0, {{env3, fm21, 0.35}, {velocity, fm21, 0.2}, {speed, fb2, 0.25}, {heading, fm21, 0.12},
                                    {wheel, fb2, 0.3}})})});
      // Two detuned saws: the creature's speed is the depth of its vibrato, its climb opens the filter
      // and its turns lean the pitch a few cents.
      p.push_back({"Flight Lead", "Bass and Leads",
                   join({echo(kQuarterDot, 0.2, 0.35), room(0.18, 0.6, 2.0, 0.5), osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 0.85), filter(0, 0, kLowPass, 2800, 0.2, 0.5),
                         lfo(0, 2, kLfoSine, 5.5), mono(0, 45), env(0, 0, 3, 300, 1.0, 180), env(0, 1, 2, 400, 0.5, 200),
                         fly(0, 0.45, 0.5, 0.45), busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 80),
                         routes(0, {{env2, cutoff, 0.15}, {heading, cutoff, 0.12}, {lfo1, busIn(0), 1.0}, {speed, busIn(0), 1.0},
                                    {bus1, kPartFine, 0.04}, {turn, kPartFine, 0.02}, {press, cutoff, 0.25}, {wheel, depth(4), 0.03}})}),
                   names({{0, 0, "Vibrato x Speed"}})});
      // A theremin: Press lifts the creature up the sky; flying upward bends it sharp, diving flat,
      // through a slow lag, and the wingbeat, deepened by its speed, is its vibrato.
      p.push_back({"Sky Theremin", "Bass and Leads",
                   join({echo(kQuarterDot, 0.18, 0.4), room(0.25, 0.7, 3.0, 0.5), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), open(0), {a(fm21, 0.06), a(flockParam(kLureY), 2)},
                         mono(0, 120), env(0, 0, 120, 300, 1.0, 400), fly(0, 0.4, 0.3, 0.6), air(0, 0.5, 0.1), biBus(0, 0, 250),
                         lfo(0, 2, kLfoSine, 5.5), busSet(0, 1, kMul, kLinear, kAsIs, 0, 0, 80),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, kPartFine, 0.08}, {lfo1, busIn(1), 1.0}, {speed, busIn(1), 1.0},
                                    {bus2, kPartFine, 0.05}, {press, fm21, 0.2}, {wheel, fm21, 0.2}})}),
                   names({{0, 0, "Climb"}, {0, 1, "Vibrato x Speed"}})});
      // Hard sync: Osc 1 is silent and only resets Osc 2, whose pitch the creature's climb sweeps; its
      // turns open the filter either way.
      p.push_back({"Sync Flight", "Bass and Leads",
                   join({echo(k8thDot, 0.15, 0.3), room(0.15, 0.5, 1.6, 0.5), osc(0, 0, kSine, 0, 0, 0.0), osc(0, 1, kSaw, 7, 0, 1.0), {a(oscParam(1, kOscSync), 1)},
                         filter(0, 0, kLowPass, 5000, 0.1, 0.3), mono(0, 40), env(0, 0, 2, 300, 1.0, 200), env(0, 1, 1, 700, 0.25, 300),
                         fly(0, 0.45, 0.5, 0.45), absBus(0, 0, 80),
                         routes(0, {{env2, coarseOf(1), 0.12}, {heading, coarseOf(1), 0.1}, {turn, busIn(0), 1.0}, {bus1, cutoff, 0.1},
                                    {press, coarseOf(1), 0.1}, {wheel, coarseOf(1), 0.12}})}),
                   names({{0, 0, "Sharp Turns"}})});
      // A growling bass: FM with a feedback loop the creature's turns pull open, tube drive, and a ladder
      // its climb opens; Press leans on the growl.
      p.push_back({"Growl Flight", "Bass and Leads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 0.5, 0.5), {a(fm21, 0.2), a(fb2, 0.15)},
                         filter(0, 0, kLadder, 900, 0.3, 0.3), {a(tailParam(kDriveType), kTube), a(drive, 0.2)}, mono(0, 20),
                         env(0, 0, 2, 400, 1.0, 80), fly(0, 0.45, 0.5, 0.45), absBus(0, 0, 80),
                         routes(0, {{turn, busIn(0), 1.0}, {bus1, fb2, 0.4}, {heading, cutoff, 0.15}, {press, fb2, 0.4},
                                    {press, drive, 0.3}, {velocity, fm21, 0.2}, {wheel, fm21, 0.3}})}),
                   names({{0, 0, "Sharp Turns"}})});

      // Each category's room, for every preset in it that doesn't set its own: long, soft halls for
      // the ambient and dark drones, a tight, bright room for the industrial ones.
      struct Space {
         const char *category;
         double mix, size, decay, damp;
      };
      static const Space spaces[] = {{"Ambient Drones", 0.42, 0.9, 7.0, 0.45}, {"Dark Drones", 0.36, 0.85, 6.0, 0.65},
                                     {"Textures", 0.32, 0.7, 3.5, 0.5},        {"Industrial", 0.22, 0.45, 2.2, 0.3},
                                     {"Landscapes", 0.36, 0.85, 5.5, 0.5},     {"Pads", 0.3, 0.75, 4.0, 0.5},
                                     {"Keys and Bells", 0.22, 0.6, 2.2, 0.5}};
      for (Preset &preset : p)
         for (const Space &s : spaces)
            if (preset.category == s.category &&
                std::none_of(preset.values.begin(), preset.values.end(), [](const auto &v) { return v.first == kReverbOn; })) {
               const Values r = room(s.mix, s.size, s.decay, s.damp);
               preset.values.insert(preset.values.end(), r.begin(), r.end());
            }

      // Each preset's level, matched by measurement so stepping through the bank doesn't jump: the
      // loudest 400 ms of a held three-note chord at velocity 0.8 (one note for the mono and bass
      // patches) sits near -20 dBFS, a little lower for the plucks and lower again for the hits,
      // which are heard by their peaks. A trim raises or lowers every part's Level together, keeping a
      // layer's balance, and spills into the Master Level past the parts' +6 dB.
            static const std::pair<const char *, double> trims[] = {
         {"Slow Murmuration", -3.5},
         {"Aurora Choir", 7.0},
         {"Weightless", -3.0},
         {"Tidal Breath", 7.5},
         {"Halo", -6.5},
         {"Horizon Glass", -2.5},
         {"Floating Organ", -4.0},
         {"Nebula", -1.5},
         {"Deep Field", -5.0},
         {"Lumen", -4.0},
         {"Ether Strings", -3.5},
         {"Snow Light", 2.5},
         {"Breathing Planet", -3.5},
         {"Cirrus", -4.5},
         {"Abyssal", -2.0},
         {"Leviathan", -3.5},
         {"Cavern", -2.5},
         {"Black Monolith", 0.0},
         {"Night Tide", -7.0},
         {"Obsidian Choir", 6.5},
         {"Submerged", -7.5},
         {"Event Horizon", -5.0},
         {"Gravity Well", -4.5},
         {"Tectonic", -0.5},
         {"Firefly Field", 12.0},
         {"Rain on Glass", 9.0},
         {"Insect Choir", 1.0},
         {"Murmur Grains", 3.0},
         {"Embers", 6.5},
         {"Aeolian Harp", -7.5},
         {"Chime Flock", 5.0},
         {"Water Drops", 7.5},
         {"Static Swarm", 13.0},
         {"Morse Birds", 3.0},
         {"Bubble Chamber", -2.5},
         {"Tape Murmur", -1.0},
         {"Frost", 4.0},
         {"Clockwork Swarm", -2.0},
         {"Foundry", 3.5},
         {"Turbine", -3.5},
         {"Rust Belt", -1.5},
         {"Iron Lung", -0.5},
         {"Scrapyard", -1.5},
         {"Power Station", 2.5},
         {"Grinder", 2.5},
         {"Pressure Vessel", 0.0},
         {"Conveyor", 1.0},
         {"Signal Decay", 0.0},
         {"Engine Room", 0.5},
         {"Dead Radio", 7.0},
         {"Furnace", -2.5},
         {"Machine Swarm", -1.5},
         {"Glass and Iron", -2.0},
         {"Predator Weather", -3.5},
         {"Dawn Chorus", -4.0},
         {"Storm Front", 0.0},
         {"Low Tide, High Wind", -8.0},
         {"Two Flocks", -3.0},
         {"Hive", -4.0},
         {"Migration", -1.0},
         {"Flock Pad", -3.0},
         {"Glass Flock", -2.5},
         {"PWM Strings", -5.5},
         {"Choir Flock", 8.5},
         {"Flock Brass", -2.5},
         {"Wide Unison", -3.0},
         {"Dream Pad", -4.0},
         {"Sweep Pad", -5.5},
         {"Flock Tines", -2.0},
         {"Flock Vibes", 0.0},
         {"Flock Bells", -1.5},
         {"Kalimba Swarm", 3.0},
         {"Rotor Organ", -2.5},
         {"Flock Pluck", 4.0},
         {"Music Box Flock", 2.0},
         {"Sub Flight", 1.5},
         {"Acid Flight", 4.5},
         {"Reese Flight", 6.0},
         {"FM Flight Bass", 2.5},
         {"Flight Lead", 1.0},
         {"Sky Theremin", 1.0},
         {"Sync Flight", 3.5},
         {"Growl Flight", 5.0},
      };
      for (const auto &[name, trim] : trims)
         for (Preset &preset : p) {
            if (preset.name != name)
               continue;
            double level[kPartCount] = {}, master = -6.0;
            for (const auto &[id, v] : preset.values) {
               for (int part = 0; part < kPartCount; ++part)
                  if (id == partId(part, kPartLevel))
                     level[part] = v;
               if (id == kMasterLevel)
                  master = v;
            }
            const double spill = std::max(0.0, std::max(level[0], level[1]) + trim - 6.0);
            for (int part = 0; part < kPartCount; ++part)
               preset.values.push_back({partId(part, kPartLevel), level[part] + trim - spill});
            preset.values.push_back({kMasterLevel, master + spill});
         }
      // An envelope with a time past what Normal shows opens on Long, so its corners aren't pinned at
      // the end of the editor. The range is only the editor's view: the sound is the same either way.
      for (Preset &preset : p)
         for (int part = 0; part < kPartCount; ++part)
            for (int e = 0; e < kEnvCount; ++e) {
               double longest = 0.0;
               for (EnvParam stage : {kEnvAttack, kEnvDecay, kEnvRelease})
                  for (const auto &[id, v] : preset.values)
                     if (id == partId(part, envParam(e, stage)))
                        longest = std::max(longest, timeMs(v));
               if (longest > envRangeMs(kEnvNormal) + 0.5)
                  preset.values.push_back({partId(part, envParam(e, kEnvRange)), kEnvLong});
            }
      // The menu's order: the plain patches to start from, then the showcases, the flock first.
      static const char *const kCategoryOrder[] = {"Init", "Ambient Drones", "Dark Drones", "Textures", "Industrial", "Landscapes", "Pads", "Keys and Bells", "Bass and Leads"};
      const auto rank = [](const Preset &x) {
         const auto *end = std::end(kCategoryOrder);
         return std::find_if(std::begin(kCategoryOrder), end, [&](const char *c) { return x.category == c; }) - std::begin(kCategoryOrder);
      };
      std::stable_sort(p.begin(), p.end(), [&](const Preset &x, const Preset &y) { return rank(x) < rank(y); });
      return p;
   }();
   return presets;
}

// Every param of `p`, in kParams() order: its own values, and every other param at its default.
inline std::vector<double> presetValues(const Preset &p) {
   std::vector<double> v(paramCount());
   for (size_t i = 0; i < paramCount(); ++i)
      v[i] = kParams()[i].def.defaultValue;
   for (const auto &[id, value] : p.values) {
      const int i = paramIndex(id);
      if (i >= 0 && std::isfinite(value))
         v[i] = std::clamp(value, kParams()[i].def.min, kParams()[i].def.max);
   }
   return v;
}

// The folder user presets live in: the OS's per-user application data folder.
inline std::filesystem::path userPresetFolder() {
#if defined(_WIN32)
   const char *appData = std::getenv("APPDATA");
   return std::filesystem::path(appData ? appData : ".") / "JSynth" / "Presets";
#elif defined(__APPLE__)
   const char *home = std::getenv("HOME");
   return std::filesystem::path(home ? home : ".") / "Library" / "Application Support" / "JSynth" / "Presets";
#else
   const char *config = std::getenv("XDG_CONFIG_HOME");
   const char *home = std::getenv("HOME");
   const std::filesystem::path base = config && *config ? std::filesystem::path(config)
                                                        : std::filesystem::path(home ? home : ".") / ".config";
   return base / "JSynth" / "Presets";
#endif
}

// Format 2 adds Release as a route source; a format 1 preset's route sources are moved to match.
constexpr const char *kPresetHeader = "JSynth preset 2";
constexpr const char *kPresetHeaderBeforeRelease = "JSynth preset 1";
constexpr const char *kPresetExtension = ".jspreset";

// Reads a user preset; false if the file isn't one.
inline bool readPreset(const std::filesystem::path &file, Preset &out) {
   std::ifstream in(file);
   std::string line;
   if (!in || !std::getline(in, line) || (line != kPresetHeader && line != kPresetHeaderBeforeRelease))
      return false;
   const bool old = line == kPresetHeaderBeforeRelease;
   out = {file.stem().string(), "User", {}};
   while (std::getline(in, line)) {
      std::istringstream fields(line);
      // A bus's name: "bus <part> <bus> <name>". An older JSynth skips these lines.
      if (line.rfind("bus ", 0) == 0) {
         std::string word;
         int part = -1, bus = -1;
         if (fields >> word >> part >> bus && part >= 0 && part < kPartCount && bus >= 0 && bus < kBusCount) {
            std::string name;
            std::getline(fields >> std::ws, name);
            out.buses[part][bus] = name.substr(0, kBusNameMax);
         }
         continue;
      }
      unsigned id = 0;
      double v = 0.0;
      if (fields >> id >> v && findParam(id) && std::isfinite(v))
         out.values.emplace_back(static_cast<clap_id>(id), old ? migrateBeforeRelease(id, v) : v);
   }
   return true;
}

// Saves every param, so a preset sounds the same even if a later version changes a default; false if
// the file can't be written. It is written beside its final name and then renamed over it, so a
// failed save never leaves a half-written preset behind.
inline bool writePreset(const std::filesystem::path &file, const jclap::ui::ParamAccess &access,
                        const BusNames &buses = {}) {
   std::error_code ec;
   std::filesystem::create_directories(file.parent_path(), ec);
   std::filesystem::path temp = file;
   temp += ".tmp";
   {
      std::ofstream out(temp);
      if (!out)
         return false;
      out << kPresetHeader << "\n";
      char line[64];
      for (const Param &p : kParams()) {
         std::snprintf(line, sizeof(line), "%u %.17g\n", static_cast<unsigned>(p.def.id), access.paramValue(p.def.id));
         out << line;
      }
      for (int part = 0; part < kPartCount; ++part)
         for (int bus = 0; bus < kBusCount; ++bus)
            if (!buses[part][bus].empty())
               out << "bus " << part << " " << bus << " " << buses[part][bus] << "\n";
      out.flush();
      if (!out) {
         out.close();
         std::filesystem::remove(temp, ec);
         return false;
      }
   }
   std::filesystem::rename(temp, file, ec);
   if (ec) {
      std::filesystem::remove(temp, ec);
      return false;
   }
   return true;
}

// The user's presets in `folder`, sorted by name.
inline std::vector<Preset> userPresets(const std::filesystem::path &folder = userPresetFolder()) {
   std::vector<Preset> found;
   std::error_code ec;
   for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
      Preset p;
      if (it->path().extension() == kPresetExtension && readPreset(it->path(), p))
         found.push_back(std::move(p));
   }
   std::sort(found.begin(), found.end(), [](const Preset &x, const Preset &y) { return x.name < y.name; });
   return found;
}

// A name made safe for a file name: letters, digits, spaces, dashes and underscores only.
inline std::string presetFileName(const std::string &name) {
   std::string safe;
   for (char ch : name)
      if (std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '-' || ch == '_')
         safe += ch;
   while (!safe.empty() && safe.back() == ' ')
      safe.pop_back();
   return safe.empty() ? "Untitled" : safe;
}

} // namespace jsynth
