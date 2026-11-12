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
      constexpr int kOnce = 0, kForever = kRepeatForever;
      constexpr int kBar = 4, kBars2 = 5;
      constexpr int kScatter = 1, kFlee = 3;
      constexpr int kSine = 0, kTriangle = 1, kSaw = 2, kSquare = 3, kPulse = 4, kHalfSine = 5;
      constexpr int kLowPass = 0, kHighPass = 1, kBandPass = 2, kLadder = 5, kComb = 6;
      constexpr int kLfoSine = 0, kLfoTriangle = 1;
      const int env2 = kSourceEnv1 + 1, env3 = kSourceEnv1 + 2, lfo1 = kSourceLfo1;
      const int velocity = kSourceVelocity, press = kSourceVelocity + 1, wheel = kSourceModWheel, key = kSourceKey;
      const clap_id cutoff = filterParam(0, kFilterCutoff), resonance = filterParam(0, kFilterResonance);
      const clap_id cutoff2 = filterParam(1, kFilterCutoff);
      const clap_id fm21 = fmCell(1, 0), fm31 = fmCell(2, 0), fb2 = fmCell(1, 1);
      // A route's own depth, for a route that moves another (the mod wheel deepening a vibrato, say).
      const auto depth = [](int r) { return routeParam(r, kRouteAmount); };
      std::vector<Preset> p;

      p.push_back({"Init", "Init", {}});

      // The bank comes in two halves. The flock's showcases come first in this file: held sounds the
      // creatures play, each note's creature moving that note's filter, shape, mix and FM as it heads,
      // turns, speeds up and gathers, with Shape envelopes looping under them and reaching back into
      // the flock. They end up together in the Flock category. Then the playable bank: keys to effects,
      // laid out as the big commercial banks are and voiced to play plainly from any keyboard. The
      // menu runs in the order of kCategoryOrder below.

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
      const int bus1 = kSourceBus1, bus2 = kSourceBus1 + 1;
      constexpr int kSum = kBusSum, kMul = kBusMultiply;
      constexpr int kLinear = 0, kAbs = 4;
      constexpr int kAsIs = 0, kToBi = 2;
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
      const auto shapeOf = [](int o) { return oscParam(o, kOscShape); };
      const auto fineOf = [](int o) { return oscParam(o, kOscFine); };
      const auto ratioOf = [](int o) { return oscParam(o, kOscRatio); };
      const auto coarseOf = [](int o) { return oscParam(o, kOscCoarse); };
      const auto level = [](int o) { return mixParam(kMixLevel, o); };
      const auto panOf = [](int o) { return mixParam(kMixPan, o); };
      const clap_id noise = mixParam(kNoiseLevel), color = mixParam(kNoiseColor);
      const clap_id drive = tailParam(kDriveAmount);
      const clap_id fSpeed = flockParam(kSpeed), fWander = flockParam(kWander), fCohesion = flockParam(kCohesion);
      const clap_id fRange = flockParam(kRange), fNfm = flockParam(kNeighbourFm);
      const clap_id windPush = lfoParam(1, kLfoPush);
      const clap_id lfo1Rate = lfoParam(2, kLfoRate);
      // Each envelope's Shape Time, Warp and Level as destinations.
      const auto tm = [](int e) { return shapeParam(e, kShapeTime); };
      const auto wp = [](int e) { return shapeParam(e, kShapeWarp); };
      const auto lv = [](int e) { return shapeParam(e, kShapeLevel); };
      // A part param of either part.
      const auto pv = [](int part, clap_id offset, double v) { return std::pair<clap_id, double>{partId(part, offset), v}; };
      // Two band passes in parallel at a vowel's first two formants (ah is 750 and 1150 Hz, ee 300
      // and 2300, oo 330 and 700), fixed in pitch as a mouth is, whatever note sings through them.
      const auto vox = [&](int part, double f1, double f2, double res) {
         return join({{pv(part, tailParam(kFilterRouting), 1), pv(part, filterParam(0, kFilterSlope), 0), pv(part, filterParam(1, kFilterSlope), 0)},
                      filter(part, 0, kBandPass, f1, res, 0.0), filter(part, 1, kBandPass, f2, res, 0.0)});
      };
      constexpr int kHard = 1, kTube = 2;
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

      // ---- Industrial: heavily modulated drones and textures, metal, grind and machinery ----

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

      // ---- Two more played by the flock: a bell and a theremin ----

      // Tubular bells: a ×3.5 modulator over a long ring and a carrier an octave up; each creature's
      // turns knock the modulator off its ratio, so a held bell's colour wavers.
      p.push_back({"Flock Bells", "Keys and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 2.01, 0.25), open(0),
                         {a(fm21, 0.3), a(kSkyPan, 0.6)}, env(0, 0, 0.5, 6000, 0.0, 3000), env(0, 2, 0.5, 1500, 0.0, 1500),
                         fly(0, 0.35, 0.45, 0.4),
                         routes(0, {{env3, fm21, 0.25}, {velocity, fm21, 0.2}, {turn, ratioOf(1), 0.015}, {heading, fineOf(2), 0.03},
                                    {wheel, fm21, 0.2}})})});

      // A theremin: Press lifts the creature up the sky; flying upward bends it sharp, diving flat,
      // through a slow lag, and the wingbeat, deepened by its speed, is its vibrato.
      p.push_back({"Sky Theremin", "Bass and Leads",
                   join({echo(kQuarterDot, 0.18, 0.4), room(0.25, 0.7, 3.0, 0.5), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), open(0), {a(fm21, 0.06), a(flockParam(kLureY), 2)},
                         mono(0, 120), env(0, 0, 120, 300, 1.0, 400), fly(0, 0.4, 0.3, 0.6), air(0, 0.5, 0.1), biBus(0, 0, 250),
                         lfo(0, 2, kLfoSine, 5.5), busSet(0, 1, kMul, kLinear, kAsIs, 0, 0, 80),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, kPartFine, 0.08}, {lfo1, busIn(1), 1.0}, {speed, busIn(1), 1.0},
                                    {bus2, kPartFine, 0.05}, {press, fm21, 0.2}, {wheel, fm21, 0.2}})}),
                   names({{0, 0, "Climb"}, {0, 1, "Vibrato x Speed"}})});

      // ---- The playable bank: the sounds a session reaches for first ----
      //
      // Laid out the way the big commercial banks are (keys, organs, mallets, plucks, bass, leads,
      // pads, strings and brass, sequences, percussion and effects), each one voiced from scratch on
      // JSynth's own engine. They play plainly from any keyboard: velocity shapes the attack and tone,
      // the mod wheel does the obvious thing for the sound (vibrato, rotary speed, brightness), and
      // Press and Slide add to that rather than being needed. The flock leaves them alone.

      const int lfo2 = kSourceLfo1 + 1, slide = kSourceVelocity + 3;
      const clap_id res2 = filterParam(1, kFilterResonance);
      const clap_id ampAttack = envParam(0, kEnvAttack), ampDecay = envParam(0, kEnvDecay), ampRelease = envParam(0, kEnvRelease);
      constexpr int kLfoSmooth = 5, kPeak = 4;
      constexpr int kBeat = 2, kHalfBar = 3, kGlobalLfo = 2;
      constexpr int k8th = 6, kQuarter = 9;
      // Envelope `e`'s strike: how much velocity scales it.
      const auto envVel = [&](int part, int e, double v) { return Values{pv(part, envParam(e, kEnvVelocity), v)}; };
      // How much velocity sets the part's loudness (all of it by default).
      const auto velAmp = [&](int part, double v) { return pv(part, kVelocityToAmp, v); };
      // Filter `f`'s slope (0 is 12 dB, 3 is 48 dB).
      const auto slope = [&](int part, int f, int s) { return Values{pv(part, filterParam(f, kFilterSlope), s)}; };
      // A vibrato or tremolo LFO: LFO 1 or 2 (2 or 3) at `hz`, faded in over `delayMs` after the note.
      const auto vib = [&](int part, int l, double hz, double delayMs) {
         return join({lfo(part, l, kLfoSine, hz), {pv(part, lfoParam(l, kLfoDelay), delayMs > 0 ? timeValue(delayMs) : 0.0)}});
      };
      // An LFO in time with the host at a division (kDivisionNames).
      const auto lfoSync = [&](int part, int l, int shapeIndex, int division) {
         return Values{pv(part, lfoParam(l, kLfoShape), shapeIndex), pv(part, lfoParam(l, kLfoSync), 1),
                       pv(part, lfoParam(l, kLfoDivision), division)};
      };
      // Oscillator `o` at a fixed pitch in Hz, whatever the note.
      const auto fixedOsc = [&](int part, int o, int wave, double hz, double lvl) {
         return Values{pv(part, oscParam(o, kOscWave), wave), pv(part, oscParam(o, kOscPitchMode), 2),
                       pv(part, oscParam(o, kOscFixed), fixedValue(hz)), pv(part, mixParam(kMixLevel, o), lvl)};
      };
      // Which filter oscillator `o` (or the noise, at kOscCount) goes into: 0 both, 1 Filter 1, 2 Filter 2.
      const auto into = [&](int part, int o, int f) {
         return Values{pv(part, o < kOscCount ? oscParam(o, kOscFilter) : mixParam(kNoiseFilter), f)};
      };
      const auto parallel = [&](int part) { return Values{pv(part, tailParam(kFilterRouting), 1)}; };
      // The amp's decay and release curves (+ fast then slow, as a struck or plucked thing dies away).
      const auto ampCurves = [&](int part, double decay, double release) {
         return Values{pv(part, envParam(0, kEnvDecayCurve), decay), pv(part, envParam(0, kEnvReleaseCurve), release)};
      };
      const auto poly = [&](int part, int voices) { return pv(part, kPolyphony, voices); };

      // ---- Keys ----

      // A tine electric piano: two operators for the body, a ×14 one for the tine's ping, both harder
      // the harder you play, and a decay that shortens up the keyboard. The wheel brings in tremolo.
      p.push_back({"Tine Piano", "Keys",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 14.0, 0.0), open(0),
                         {a(fm21, 0.16), velAmp(0, 0.7), a(kDrift, 0.04), poly(0, 12)}, env(0, 0, 1, 4500, 0.0, 380), ampCurves(0, 0.6, 0.3),
                         env(0, 1, 0.5, 1800, 0.25, 400), envVel(0, 1, 0.8), env(0, 2, 0.5, 90, 0.0, 60), envVel(0, 2, 0.9),
                         vib(0, 2, 4.6, 0),
                         routes(0, {{env2, fm21, 0.26}, {env3, fm31, 0.32}, {velocity, fm21, 0.12}, {key, ampDecay, -0.12},
                                    {lfo1, kPartLevel, 0.0}, {wheel, depth(4), 0.06}, {press, fm21, 0.12}})})});
      // A suitcase piano: a rounder bark, a little tube on hard notes, and a stereo tremolo that pans
      // across the field. The wheel sets how deep the tremolo pans.
      p.push_back({"Suitcase", "Keys",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kTriangle, 1.0, 0.18), open(0),
                         {pv(0, fineOf(2), 4), a(fm21, 0.12), velAmp(0, 0.7), a(kDrift, 0.05), a(tailParam(kDriveType), kTube), poly(0, 12)},
                         env(0, 0, 1, 5000, 0.0, 420), ampCurves(0, 0.6, 0.3), env(0, 1, 0.5, 2200, 0.3, 400), envVel(0, 1, 1.0),
                         lfo(0, 2, kLfoSine, 4.2), {a(lfoParam(2, kLfoTrigger), kGlobalLfo)},
                         routes(0, {{env2, fm21, 0.3}, {velocity, drive, 0.35}, {key, ampDecay, -0.12}, {lfo1, kPartPan, 0.2},
                                    {wheel, depth(3), 0.55}, {press, drive, 0.3}})})});
      // A reed piano: a triangle barked by a 1:1 modulator and pushed into tube drive by the strike,
      // through a soft low pass. Bright and nasal when you dig in, mellow when you don't.
      p.push_back({"Reed Piano", "Keys",
                   join({op(0, 0, kTriangle, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 2.0, 0.12),
                         {a(fm21, 0.18), velAmp(0, 0.6), a(tailParam(kDriveType), kTube), a(drive, 0.18), poly(0, 12)},
                         filter(0, 0, kLowPass, 2600, 0.1, 0.5), env(0, 0, 1, 2600, 0.0, 260), ampCurves(0, 0.5, 0.3),
                         env(0, 1, 0.5, 900, 0.2, 300), envVel(0, 1, 1.0), vib(0, 2, 5.5, 0),
                         routes(0, {{env2, fm21, 0.3}, {velocity, drive, 0.3}, {key, ampDecay, -0.1}, {velocity, cutoff, 0.12},
                                    {lfo1, kPartLevel, 0.0}, {wheel, depth(4), 0.07}, {press, cutoff, 0.15}})})});
      // A funky clavinet: a narrow pulse and a saw through a high pass and a snapping low pass, short
      // and percussive, quiet when you let go. Velocity opens the snap; Slide moves the pickup.
      p.push_back({"Clav Funk", "Keys",
                   join({osc(0, 0, kPulse, 0, 0, 1.0), osc(0, 1, kSaw, 12, 3, 0.35), {a(shapeOf(0), 0.3), velAmp(0, 0.7), poly(0, 12)},
                         filter(0, 0, kHighPass, 220, 0.1, 0.0), filter(0, 1, kLowPass, 1100, 0.35, 0.6), slope(0, 1, 0),
                         env(0, 0, 0.5, 900, 0.25, 50), ampCurves(0, 0.4, 0.0), env(0, 1, 0.5, 260, 0.15, 60), envVel(0, 1, 0.6),
                         routes(0, {{env2, cutoff2, 0.38}, {velocity, cutoff2, 0.15}, {slide, shapeOf(0), 0.35}, {wheel, res2, 0.35},
                                    {press, cutoff2, 0.15}})})});
      // A felt upright: a triangle and its octave with a soft hammer of noise, a low pass that opens
      // with the strike and closes as the note dies, and a gentle detune between the strings.
      p.push_back({"Felt Upright", "Keys",
                   join({osc(0, 0, kTriangle, 0, -2, 1.0), osc(0, 1, kSine, 12, 2, 0.35), op(0, 2, kSine, 3.0, 0.0),
                         {a(fm31, 0.1), a(noise, 0.0), a(color, -0.4), velAmp(0, 0.75), a(kDrift, 0.04), poly(0, 16)},
                         filter(0, 0, kLowPass, 900, 0.05, 0.7), slope(0, 0, 0), env(0, 0, 2, 3800, 0.0, 450), ampCurves(0, 0.7, 0.4),
                         env(0, 1, 0.5, 1500, 0.1, 400), envVel(0, 1, 1.0), env(0, 2, 0.5, 30, 0.0, 20), envVel(0, 2, 1.0),
                         routes(0, {{env2, cutoff, 0.28}, {env3, noise, 0.25}, {env3, fm31, 0.2}, {key, ampDecay, -0.14},
                                    {wheel, cutoff, 0.2}, {press, cutoff, 0.12}})})});
      // A glassy FM piano: a ×7 bell partial over the body that rings on hard notes, an octave of
      // sine underneath, and echoes a dotted eighth behind.
      p.push_back({"Crystal Keys", "Keys",
                   join({echo(k8thDot, 0.16, 0.3), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 7.0, 0.0), op(0, 2, kSine, 2.0, 0.25), open(0),
                         {a(fm21, 0.1), velAmp(0, 0.7), poly(0, 12)}, env(0, 0, 0.5, 4000, 0.0, 600), ampCurves(0, 0.6, 0.3),
                         env(0, 1, 0.5, 700, 0.1, 400), envVel(0, 1, 1.0),
                         routes(0, {{env2, fm21, 0.25}, {velocity, fm21, 0.1}, {key, ampDecay, -0.1}, {wheel, fm21, 0.15},
                                    {press, level(2), 0.25}})})});
      // A harpsichord: a narrow pulse and a saw an octave up, plucked bright through a high pass, with
      // the quick damped release of a jack falling back. It barely follows velocity, as the real one.
      p.push_back({"Harpsichord", "Keys",
                   join({osc(0, 0, kPulse, 0, 0, 0.9), osc(0, 1, kSaw, 12, 2, 0.5), {a(shapeOf(0), 0.2), velAmp(0, 0.2), poly(0, 16)},
                         filter(0, 0, kHighPass, 300, 0.0, 0.3), filter(0, 1, kLowPass, 2600, 0.15, 0.7), slope(0, 1, 0),
                         env(0, 0, 0.5, 2400, 0.0, 140), ampCurves(0, 0.7, 0.5), env(0, 1, 0.5, 600, 0.2, 100),
                         routes(0, {{env2, cutoff2, 0.25}, {key, ampDecay, -0.12}, {wheel, cutoff2, -0.25}, {press, cutoff2, 0.12}})})});
      // A worn tape of an electric piano: the tines, a slow wow, hiss and a darker top, a little
      // soft drive. The wheel deepens the wow.
      p.push_back({"Lo-Fi Keys", "Keys",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 14.0, 0.0),
                         {a(fm21, 0.14), velAmp(0, 0.6), a(noise, 0.015), a(color, 0.2), a(drive, 0.25), poly(0, 12)},
                         filter(0, 0, kLowPass, 2400, 0.1, 0.0), env(0, 0, 1, 4000, 0.0, 400), ampCurves(0, 0.6, 0.3),
                         env(0, 1, 0.5, 1500, 0.2, 400), envVel(0, 1, 0.8), env(0, 2, 0.5, 70, 0.0, 50), envVel(0, 2, 0.8),
                         lfo(0, 2, kLfoSmooth, 0.6), {a(lfoParam(2, kLfoTrigger), kGlobalLfo)},
                         routes(0, {{env2, fm21, 0.22}, {env3, fm31, 0.25}, {lfo1, kPartFine, 0.07}, {key, ampDecay, -0.1},
                                    {wheel, depth(2), 0.08}, {press, cutoff, 0.15}})})});

      // ---- Organs ----

      // A jazz tonewheel organ: 16', 8' and 2⅔' drawbars, a key click and a decaying third-harmonic
      // percussion, through a tube and a rotary speaker shared by every note. The wheel speeds the
      // rotor from chorale to tremolo.
      p.push_back({"Jazz Drawbars", "Organs",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 0.5, 0.75), op(0, 2, kSine, 3.0, 0.2), open(0),
                         {velAmp(0, 0.0), a(color, 0.6), a(tailParam(kDriveType), kTube), a(drive, 0.22), poly(0, 16)},
                         env(0, 0, 3, 100, 1.0, 45), env(0, 1, 0.5, 14, 0.0, 10), env(0, 2, 0.5, 350, 0.0, 60),
                         lfo(0, 2, kLfoSine, 0.8), {a(lfoParam(2, kLfoTrigger), kGlobalLfo)},
                         routes(0, {{env2, noise, 0.12}, {env3, level(2), 0.5}, {lfo1, kPartPan, 0.25}, {lfo1, kPartFine, 0.03},
                                    {wheel, lfo1Rate, 0.27}, {press, drive, 0.35}})})});
      // A full gospel registration: every drawbar out, driven hard, the rotor already spinning fast.
      // The wheel slows it down; Press pushes the drive.
      p.push_back({"Gospel Organ", "Organs",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.7), op(0, 2, kSine, 0.5, 0.8), open(0),
                         {a(fm21, 0.05), velAmp(0, 0.0), a(color, 0.6), a(tailParam(kDriveType), kTube), a(drive, 0.4), poly(0, 16)},
                         env(0, 0, 3, 100, 1.0, 50), env(0, 1, 0.5, 14, 0.0, 10), lfo(0, 2, kLfoSine, 6.4),
                         {a(lfoParam(2, kLfoTrigger), kGlobalLfo)},
                         routes(0, {{env2, noise, 0.12}, {lfo1, kPartPan, 0.3}, {lfo1, kPartFine, 0.04}, {wheel, lfo1Rate, -0.27},
                                    {press, drive, 0.3}})})});
      // A pipe organ in a stone church: a square principal, an octave and a sub, a breath of wind,
      // slow to speak and slower to leave the room.
      p.push_back({"Cathedral Pipes", "Organs",
                   join({room(0.4, 0.95, 7.0, 0.45), osc(0, 0, kSquare, 0, 0, 0.55), op(0, 1, kSine, 2.0, 0.55), op(0, 2, kSine, 0.5, 0.75),
                         {velAmp(0, 0.0), a(noise, 0.015), a(color, -0.3), a(kDrift, 0.03), poly(0, 16)},
                         filter(0, 0, kLowPass, 2200, 0.05, 0.6), env(0, 0, 90, 100, 1.0, 500),
                         routes(0, {{wheel, level(1), 0.4}, {wheel, cutoff, 0.15}, {press, cutoff, 0.15}})})});
      // A 60s combo organ: a square and a bright octave saw with a quick vibrato on every note. The
      // wheel deepens the vibrato.
      p.push_back({"Combo Organ", "Organs",
                   join({osc(0, 0, kSquare, 0, 0, 0.8), op(0, 1, kSaw, 2.0, 0.35), {velAmp(0, 0.0), poly(0, 16)},
                         filter(0, 0, kLowPass, 3200, 0.1, 0.5), env(0, 0, 2, 100, 1.0, 40), vib(0, 2, 6.5, 0),
                         routes(0, {{lfo1, kPartFine, 0.04}, {wheel, depth(0), 0.06}, {press, cutoff, 0.2}})})});
      // A harmonium: two saws a musette apart and a pulse below through a reedy peak, breathing in
      // with the bellows. Press is the bellows: lean in and it swells.
      p.push_back({"Harmonium", "Organs",
                   join({osc(0, 0, kSaw, 0, -6, 0.8), osc(0, 1, kSaw, 0, 6, 0.8), osc(0, 2, kPulse, -12, 0, 0.35),
                         {a(shapeOf(2), 0.5), velAmp(0, 0.2), a(kDrift, 0.05), poly(0, 16)}, filter(0, 0, kLowPass, 1700, 0.1, 0.5),
                         filter(0, 1, kPeak, 1300, 0.35, 0.0), env(0, 0, 60, 100, 1.0, 180),
                         routes(0, {{press, kPartLevel, 0.08}, {press, cutoff, 0.15}, {wheel, cutoff, 0.2}})})});

      // ---- Bells and Mallets ----

      // A glockenspiel: a bright struck bar, an octave up, with its inharmonic ×2.76 overtone fading
      // first and a ping from the mallet.
      p.push_back({"Glockenspiel", "Bells and Mallets",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.76, 0.3), op(0, 2, kSine, 5.4, 0.12), open(0),
                         {a(kPartTranspose, 12), velAmp(0, 0.8), poly(0, 16)}, env(0, 0, 0.5, 3000, 0.0, 2000), ampCurves(0, 0.6, 0.4),
                         env(0, 1, 0.5, 500, 0.0, 300), envVel(0, 1, 0.6),
                         routes(0, {{env2, level(1), 0.25}, {env2, level(2), 0.2}, {key, ampDecay, -0.08}, {wheel, level(2), 0.2}})})});
      // A marimba: a warm wooden bar, its strong fourth partial knocked out by the mallet and gone in
      // a moment, a soft woody click, and a decay that shortens up the range.
      p.push_back({"Marimba", "Bells and Mallets",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.0), op(0, 2, kSine, 10.0, 0.0), open(0),
                         {a(fm31, 0.0), velAmp(0, 0.8), poly(0, 16)}, env(0, 0, 0.5, 1100, 0.0, 600), ampCurves(0, 0.6, 0.4),
                         env(0, 1, 0.5, 120, 0.0, 80), envVel(0, 1, 0.8), env(0, 2, 0.5, 15, 0.0, 10), envVel(0, 2, 1.0),
                         routes(0, {{env2, level(1), 0.35}, {env3, fm31, 0.28}, {key, ampDecay, -0.14}, {wheel, ampDecay, 0.2}})})});
      // A vibraphone: a long metal bar with its ×4 overtone, a soft mallet, and the motor's tremolo,
      // which the wheel turns up.
      p.push_back({"Vibraphone", "Bells and Mallets",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.18), op(0, 2, kSine, 10.0, 0.0), open(0),
                         {velAmp(0, 0.8), poly(0, 16)}, env(0, 0, 0.5, 5000, 0.0, 1400), ampCurves(0, 0.5, 0.4),
                         env(0, 1, 0.5, 900, 0.0, 400), env(0, 2, 0.5, 20, 0.0, 10), envVel(0, 2, 1.0), lfo(0, 2, kLfoSine, 5.2),
                         {a(lfoParam(2, kLfoTrigger), kGlobalLfo)},
                         routes(0, {{env2, level(1), 0.15}, {env3, fm31, 0.2}, {lfo1, kPartLevel, 0.025}, {wheel, depth(2), 0.06},
                                    {key, ampDecay, -0.08}})})});
      // Tubular bells: a ×3.5 modulator that rings and slowly fades out of a long carrier, with an
      // octave partial a hair sharp. Hit it hard for the clang.
      p.push_back({"Tubular Bells", "Bells and Mallets",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 2.01, 0.25), open(0),
                         {a(fm21, 0.18), velAmp(0, 0.8), poly(0, 12)}, env(0, 0, 0.5, 7000, 0.0, 3500), ampCurves(0, 0.6, 0.4),
                         env(0, 1, 0.5, 2500, 0.0, 2000), envVel(0, 1, 1.0),
                         routes(0, {{env2, fm21, 0.3}, {velocity, fm21, 0.1}, {wheel, fm21, 0.15}})})});
      // A kalimba: a soft thumb on a metal tine, a quick ×3 blip and a faint inharmonic overtone,
      // with echoes behind.
      p.push_back({"Kalimba", "Bells and Mallets",
                   join({echo(k8thDot, 0.18, 0.3), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 5.95, 0.06), open(0),
                         {a(fm21, 0.06), velAmp(0, 0.8), poly(0, 12)}, env(0, 0, 0.5, 1300, 0.0, 700), ampCurves(0, 0.6, 0.4),
                         env(0, 1, 0.5, 60, 0.0, 40), envVel(0, 1, 1.0),
                         routes(0, {{env2, fm21, 0.3}, {key, ampDecay, -0.1}, {wheel, level(2), 0.2}})})});
      // A steel pan: a ringing note with its octave and twelfth, a bright metallic attack that settles
      // quickly, and a slight pitch lift as it's struck.
      p.push_back({"Steel Pan", "Bells and Mallets",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.35), op(0, 2, kSine, 3.0, 0.0), open(0),
                         {a(fm31, 0.12), a(fm21, 0.08), velAmp(0, 0.8), poly(0, 12)}, env(0, 0, 1, 1600, 0.0, 600), ampCurves(0, 0.5, 0.4),
                         env(0, 1, 0.5, 180, 0.0, 100), envVel(0, 1, 1.0), env(0, 2, 0.5, 40, 0.0, 30),
                         routes(0, {{env2, fm31, 0.28}, {env3, kPartFine, -0.06}, {key, ampDecay, -0.1}, {wheel, fm21, 0.2}})})});
      // A music box: a high triangle with a faint ×3 tine and a long ringing tail, a little uneven.
      p.push_back({"Music Box", "Bells and Mallets",
                   join({echo(kQuarterDot, 0.15, 0.3), op(0, 0, kTriangle, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.08), op(0, 2, kSine, 4.0, 0.0),
                         open(0), {a(kPartTranspose, 12), a(fm31, 0.06), a(kDrift, 0.06), velAmp(0, 0.6), poly(0, 16)},
                         env(0, 0, 0.5, 2600, 0.0, 1500), ampCurves(0, 0.6, 0.4), env(0, 2, 0.5, 80, 0.0, 50),
                         routes(0, {{env3, fm31, 0.2}, {key, ampDecay, -0.08}, {wheel, fm31, 0.2}})})});
      // A crystal bell: an inharmonic ×1.41 FM ring, high glints at ×7.13, and a slow shimmer between
      // two carriers a few cents apart, in a big room.
      p.push_back({"Crystal Bell", "Bells and Mallets",
                   join({room(0.35, 0.85, 5.0, 0.4), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.41, 0.0), op(0, 2, kSine, 7.13, 0.06),
                         open(0), {a(fm21, 0.16), a(fm31, 0.04), velAmp(0, 0.8), poly(0, 12)}, env(0, 0, 0.5, 6000, 0.0, 3000),
                         ampCurves(0, 0.6, 0.4), env(0, 1, 0.5, 3000, 0.0, 2000), envVel(0, 1, 1.0), lfo(0, 2, kLfoSine, 0.35),
                         routes(0, {{env2, fm21, 0.25}, {lfo1, ratioOf(1), 0.0015}, {wheel, fm31, 0.2}, {press, fm21, 0.12}})})});

      // ---- Plucks ----

      // The classic synth pluck: two saws a few cents apart, a resonant filter that snaps shut, and
      // echoes a dotted eighth behind. The wheel lengthens it.
      p.push_back({"Classic Pluck", "Plucks",
                   join({echo(k8thDot, 0.2, 0.35), osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0),
                         {a(panOf(0), -0.4), a(panOf(1), 0.4), velAmp(0, 0.6), poly(0, 12)}, filter(0, 0, kLowPass, 280, 0.3, 0.4),
                         env(0, 0, 0.5, 700, 0.0, 300), ampCurves(0, 0.5, 0.3), env(0, 1, 0.5, 240, 0.0, 200), envVel(0, 1, 0.5),
                         routes(0, {{env2, cutoff, 0.5}, {velocity, cutoff, 0.1}, {wheel, envParam(1, kEnvDecay), 0.15},
                                    {wheel, ampDecay, 0.15}, {press, cutoff, 0.15}})})});
      // A hollow square pluck, round and woody, its filter snapping with a little resonance.
      p.push_back({"Square Pluck", "Plucks",
                   join({osc(0, 0, kSquare, 0, 0, 1.0), osc(0, 1, kSine, -12, 0, 0.3), {velAmp(0, 0.6), poly(0, 12)},
                         filter(0, 0, kLowPass, 400, 0.35, 0.5), env(0, 0, 0.5, 600, 0.0, 250), ampCurves(0, 0.5, 0.3),
                         env(0, 1, 0.5, 180, 0.0, 150), envVel(0, 1, 0.5),
                         routes(0, {{env2, cutoff, 0.45}, {velocity, cutoff, 0.1}, {wheel, shapeOf(0), -0.3}, {press, cutoff, 0.15}})})});
      // A nylon string, plucked: a short burst of noise and saw rings a comb tuned to the note, the
      // way a string rings, then a low pass softens it. Harder notes pluck brighter.
      p.push_back({"Nylon String", "Plucks",
                   join({osc(0, 0, kSaw, 0, 0, 0.0), {a(noise, 0.0), a(color, -0.3), velAmp(0, 0.7), poly(0, 12)},
                         filter(0, 0, kComb, 261.63, 0.985, 1.0), filter(0, 1, kLowPass, 2200, 0.0, 0.6), slope(0, 1, 0),
                         env(0, 0, 0.5, 3500, 0.0, 250), ampCurves(0, 0.4, 0.3), env(0, 2, 0.5, 9, 0.0, 5), envVel(0, 2, 0.7),
                         routes(0, {{env3, noise, 0.9}, {env3, level(0), 0.5}, {velocity, cutoff2, 0.15}, {wheel, cutoff2, 0.2},
                                    {press, cutoff2, 0.15}})})});
      // A concert harp: the same plucked comb, softer, its burst a triangle, ringing long into a hall.
      p.push_back({"Harp", "Plucks",
                   join({room(0.3, 0.8, 3.5, 0.5), osc(0, 0, kTriangle, 0, 0, 0.0), {a(noise, 0.0), a(color, -0.7), velAmp(0, 0.7), poly(0, 16)},
                         filter(0, 0, kComb, 261.63, 0.99, 1.0), filter(0, 1, kLowPass, 1500, 0.0, 0.7), slope(0, 1, 0),
                         env(0, 0, 0.5, 5000, 0.0, 600), ampCurves(0, 0.4, 0.3), env(0, 2, 0.5, 14, 0.0, 8), envVel(0, 2, 0.5),
                         routes(0, {{env3, noise, 0.5}, {env3, level(0), 0.9}, {velocity, cutoff2, 0.15}, {wheel, cutoff2, 0.2}})})});
      // String-section pizzicato: two saws a few cents apart through a quick low pass, short and
      // bouncing.
      p.push_back({"Pizzicato", "Plucks",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 0.9), {a(panOf(0), -0.3), a(panOf(1), 0.3), velAmp(0, 0.8), poly(0, 16)},
                         filter(0, 0, kLowPass, 700, 0.1, 0.5), filter(0, 1, kHighPass, 150, 0.0, 0.0), env(0, 0, 1, 340, 0.0, 250),
                         ampCurves(0, 0.6, 0.4), env(0, 1, 0.5, 150, 0.0, 150), envVel(0, 1, 0.6),
                         routes(0, {{env2, cutoff, 0.3}, {key, ampDecay, -0.08}, {wheel, ampDecay, 0.15}})})});
      // A bell pluck: a ×2 FM ping that falls away fast into a pure tone, with a faint fourth partial.
      p.push_back({"Bell Pluck", "Plucks",
                   join({echo(k8thDot, 0.18, 0.35), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSine, 4.0, 0.1), open(0),
                         {a(fm21, 0.05), velAmp(0, 0.7), poly(0, 12)}, env(0, 0, 0.5, 900, 0.0, 400), ampCurves(0, 0.6, 0.4),
                         env(0, 1, 0.5, 200, 0.0, 150), envVel(0, 1, 1.0),
                         routes(0, {{env2, fm21, 0.35}, {wheel, fm21, 0.15}, {press, level(2), 0.25}})})});
      // A mallet pluck: a triangle and its octave through a resonant filter that pings shut, like a
      // soft synth marimba.
      p.push_back({"Mallet Pluck", "Plucks",
                   join({osc(0, 0, kTriangle, 0, 0, 1.0), osc(0, 1, kSine, 12, 0, 0.3), {velAmp(0, 0.7), poly(0, 12)},
                         filter(0, 0, kLowPass, 500, 0.55, 0.8), slope(0, 0, 0), env(0, 0, 0.5, 500, 0.0, 300), ampCurves(0, 0.6, 0.4),
                         env(0, 1, 0.5, 90, 0.0, 80), envVel(0, 1, 0.7),
                         routes(0, {{env2, cutoff, 0.35}, {wheel, resonance, 0.25}, {press, cutoff, 0.15}})})});
      // A house chord stab: saws and a square an octave down through a resonant low pass that bites
      // and closes, short enough to play on the offbeat.
      p.push_back({"Chord Stab", "Plucks",
                   join({osc(0, 0, kSaw, 0, -10, 1.0), osc(0, 1, kSaw, 0, 10, 1.0), osc(0, 2, kSquare, -12, 0, 0.4),
                         {a(panOf(0), -0.3), a(panOf(1), 0.3), velAmp(0, 0.6), poly(0, 16)}, filter(0, 0, kLowPass, 450, 0.35, 0.3),
                         env(0, 0, 0.5, 420, 0.0, 180), ampCurves(0, 0.5, 0.3), env(0, 1, 0.5, 200, 0.0, 150), envVel(0, 1, 0.5),
                         routes(0, {{env2, cutoff, 0.42}, {wheel, cutoff, 0.2}, {press, resonance, 0.2}})})});

      // ---- Bass ----

      // A clean sub: a sine with a touch of triangle and soft drive, so it still speaks on small
      // speakers. The wheel adds grit.
      p.push_back({"Sub Bass", "Bass",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kTriangle, 1.0, 0.15), open(0), mono(0, 0),
                         {velAmp(0, 0.3), a(drive, 0.18)}, env(0, 0, 3, 300, 1.0, 70),
                         routes(0, {{wheel, drive, 0.35}, {press, level(1), 0.3}})})});
      // An analog mono bass: a saw and a square an octave down into a ladder that snaps open with the
      // strike, a little drift between them. Harder notes accent the filter.
      p.push_back({"Analog Bass", "Bass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSquare, -12, 0, 0.6), filter(0, 0, kLadder, 260, 0.25, 0.3),
                         {a(kDrift, 0.08), velAmp(0, 0.4)}, mono(0, 25), env(0, 0, 1, 500, 0.85, 60), env(0, 1, 0.5, 280, 0.15, 80),
                         envVel(0, 1, 0.5),
                         routes(0, {{env2, cutoff, 0.38}, {wheel, cutoff, 0.25}, {press, cutoff, 0.2}})})});
      // A fat ladder bass: three saws (one an octave down) pushed into the ladder's own drive, with a
      // punchy, short filter envelope.
      p.push_back({"Ladder Punch", "Bass",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 1.0), osc(0, 2, kSaw, -12, 0, 0.7), filter(0, 0, kLadder, 170, 0.32, 0.3),
                         {a(filterParam(0, kFilterDrive), 0.35), a(kDrift, 0.05), velAmp(0, 0.4)}, mono(0, 0),
                         env(0, 0, 1, 400, 0.7, 60), env(0, 1, 0.5, 200, 0.0, 80), envVel(0, 1, 0.4),
                         routes(0, {{env2, cutoff, 0.45}, {wheel, cutoff, 0.25}, {press, resonance, 0.2}})})});
      // Acid: a saw into a squelching, driven ladder that slides between held notes; harder notes
      // accent the filter. The wheel turns up the squelch and Press opens it.
      p.push_back({"Acid Line", "Bass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), filter(0, 0, kLadder, 240, 0.72, 0.3), {a(filterParam(0, kFilterDrive), 0.4), velAmp(0, 0.5)},
                         mono(0, 60), env(0, 0, 1, 300, 0.8, 50), env(0, 1, 0.5, 190, 0.0, 80), envVel(0, 1, 0.7),
                         routes(0, {{env2, cutoff, 0.42}, {wheel, resonance, 0.22}, {press, cutoff, 0.25}})})});
      // A DX bass: a 1:1 pair with feedback, its bite dying away fast, and a sine sub an octave down.
      p.push_back({"FM Bass", "Bass",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 0.5, 0.35), open(0),
                         {a(fm21, 0.18), a(fb2, 0.35), velAmp(0, 0.4)}, mono(0, 0), env(0, 0, 1, 400, 0.9, 70),
                         env(0, 2, 0.5, 220, 0.12, 100), envVel(0, 2, 0.7),
                         routes(0, {{env3, fm21, 0.4}, {wheel, fb2, 0.3}, {press, fm21, 0.15}})})});
      // A Reese: two saws a dozen cents apart, beating slowly over a sine sub, held together by drive.
      // A slow LFO breathes the detune; the wheel opens it up.
      p.push_back({"Reese", "Bass",
                   join({osc(0, 0, kSaw, 0, -12, 1.0), osc(0, 1, kSaw, 0, 12, 1.0), osc(0, 2, kSine, -12, 0, 0.6),
                         filter(0, 0, kLowPass, 520, 0.12, 0.4), {a(drive, 0.3), velAmp(0, 0.3)}, mono(0, 40), env(0, 0, 3, 400, 1.0, 120),
                         lfo(0, 2, kLfoSine, 0.25),
                         routes(0, {{lfo1, fineOf(1), 0.04}, {wheel, cutoff, 0.3}, {press, cutoff, 0.2}})})});
      // A rubbery square bass: a square and a sub through a resonant low pass that bounces shut, with
      // a slight pitch blip.
      p.push_back({"Rubber Bass", "Bass",
                   join({osc(0, 0, kSquare, 0, 0, 1.0), osc(0, 1, kSine, -12, 0, 0.5), filter(0, 0, kLowPass, 300, 0.45, 0.3),
                         {velAmp(0, 0.4)}, mono(0, 0), env(0, 0, 1, 280, 0.5, 60), env(0, 1, 0.5, 150, 0.0, 80), envVel(0, 1, 0.5),
                         env(0, 2, 0.5, 25, 0.0, 20),
                         routes(0, {{env2, cutoff, 0.4}, {env3, kPartFine, 0.08}, {wheel, shapeOf(0), -0.3}, {press, cutoff, 0.2}})})});
      // A wobble bass: saws and a sub in a driven low pass that an LFO in time with the host opens on
      // every eighth. The wheel deepens the wobble.
      p.push_back({"Wobble Bass", "Bass",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, -12, 6, 0.8), osc(0, 2, kSine, -12, 0, 0.6),
                         filter(0, 0, kLowPass, 180, 0.4, 0.2), {a(drive, 0.35), velAmp(0, 0.3)}, mono(0, 30), env(0, 0, 2, 400, 1.0, 80),
                         lfoSync(0, 2, kLfoSine, 5), {a(lfoParam(2, kLfoPolarity), 1)},
                         routes(0, {{lfo1, cutoff, 0.3}, {wheel, depth(0), 0.12}, {press, resonance, 0.2}})})});
      // A short plucked bass for busy lines: a saw and a sub, the filter and the amp both closing fast.
      p.push_back({"Pluck Bass", "Bass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSine, -12, 0, 0.6), filter(0, 0, kLowPass, 220, 0.2, 0.3),
                         {velAmp(0, 0.5)}, mono(0, 0), env(0, 0, 0.5, 260, 0.0, 60), ampCurves(0, 0.5, 0.3), env(0, 1, 0.5, 120, 0.0, 60),
                         envVel(0, 1, 0.5),
                         routes(0, {{env2, cutoff, 0.42}, {wheel, ampDecay, 0.15}, {press, cutoff, 0.2}})})});
      // A growling bass: a saw ring-barked by a 1:1 modulator that an LFO in time with the host pulls
      // open every quarter, through tube drive and a ladder.
      p.push_back({"Growler", "Bass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 0.5, 0.5), {a(fm21, 0.12)},
                         filter(0, 0, kLadder, 800, 0.3, 0.3), {a(tailParam(kDriveType), kTube), a(drive, 0.3), velAmp(0, 0.3)}, mono(0, 20),
                         env(0, 0, 2, 400, 1.0, 80), lfoSync(0, 2, kLfoTriangle, 8), {a(lfoParam(2, kLfoPolarity), 1)},
                         routes(0, {{lfo1, fm21, 0.3}, {lfo1, cutoff, 0.12}, {wheel, fm21, 0.25}, {press, drive, 0.3}})})});
      // A fingered upright: a round triangle and its octave, a thump in the filter, and a decay that
      // lets each note ring and fall.
      p.push_back({"Finger Bass", "Bass",
                   join({osc(0, 0, kTriangle, 0, 0, 1.0), osc(0, 1, kSine, 12, 0, 0.25), filter(0, 0, kLowPass, 700, 0.1, 0.5),
                         {velAmp(0, 0.6)}, mono(0, 0), env(0, 0, 4, 1300, 0.0, 120), ampCurves(0, 0.5, 0.3), env(0, 1, 0.5, 90, 0.0, 60),
                         envVel(0, 1, 0.6),
                         routes(0, {{env2, cutoff, 0.25}, {wheel, cutoff, 0.2}, {press, ampDecay, 0.2}})})});
      // An octave bass: a saw and a pulse an octave up, opening together, for disco and synth-pop lines.
      p.push_back({"Octave Bass", "Bass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 12, 0, 0.45), {a(shapeOf(1), 0.4), velAmp(0, 0.5)},
                         filter(0, 0, kLowPass, 400, 0.25, 0.3), mono(0, 0), env(0, 0, 1, 350, 0.6, 60), env(0, 1, 0.5, 200, 0.1, 80),
                         envVel(0, 1, 0.5),
                         routes(0, {{env2, cutoff, 0.38}, {wheel, level(1), 0.4}, {press, cutoff, 0.2}})})});

      // ---- Leads ----

      // A singing saw lead: two saws and a quiet octave, legato with a glide, a vibrato that waits a
      // moment, and echoes. The wheel deepens the vibrato; Press opens the filter.
      p.push_back({"Saw Lead", "Leads",
                   join({echo(k8thDot, 0.18, 0.35), osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kSaw, 0, 5, 0.9), osc(0, 2, kSaw, 12, 0, 0.2),
                         filter(0, 0, kLowPass, 2800, 0.18, 0.5), {a(kDrift, 0.05), velAmp(0, 0.4)}, mono(0, 50),
                         env(0, 0, 3, 300, 1.0, 200), env(0, 1, 2, 400, 0.4, 200), vib(0, 2, 5.6, 450),
                         routes(0, {{env2, cutoff, 0.12}, {lfo1, kPartFine, 0.04}, {wheel, depth(1), 0.06}, {press, cutoff, 0.25}})})});
      // A hollow square lead with a sub, a short glide and the same waiting vibrato.
      p.push_back({"Square Lead", "Leads",
                   join({echo(k8thDot, 0.16, 0.3), osc(0, 0, kSquare, 0, 0, 1.0), osc(0, 1, kSquare, -12, 3, 0.4),
                         filter(0, 0, kLowPass, 2400, 0.15, 0.5), {velAmp(0, 0.4)}, mono(0, 30), env(0, 0, 2, 300, 1.0, 180),
                         vib(0, 2, 5.4, 400),
                         routes(0, {{lfo1, kPartFine, 0.04}, {wheel, depth(0), 0.06}, {press, cutoff, 0.25}, {slide, shapeOf(0), -0.35}})})});
      // A sync lead: Osc 1 only keeps time, and the synced saw's pitch sweeps down from a scream as
      // each note starts. The wheel and Press push the sweep higher.
      p.push_back({"Sync Lead", "Leads",
                   join({echo(k8thDot, 0.15, 0.3), osc(0, 0, kSine, 0, 0, 0.0), osc(0, 1, kSaw, 7, 0, 1.0), {a(oscParam(1, kOscSync), 1)},
                         filter(0, 0, kLowPass, 5000, 0.1, 0.3), {velAmp(0, 0.4)}, mono(0, 40), env(0, 0, 2, 300, 1.0, 200),
                         env(0, 1, 0.5, 600, 0.2, 300), vib(0, 2, 5.6, 450),
                         routes(0, {{env2, coarseOf(1), 0.14}, {lfo1, kPartFine, 0.035}, {wheel, coarseOf(1), 0.12},
                                    {press, coarseOf(1), 0.1}})})});
      // A whistle: a pure sine with a breath of air, a lazy glide and a gentle vibrato.
      p.push_back({"Whistle", "Leads",
                   join({echo(kQuarterDot, 0.2, 0.35), op(0, 0, kSine, 1.0, 1.0), open(0), {a(noise, 0.035), a(color, 0.3), velAmp(0, 0.4)},
                         filter(0, 1, kBandPass, 261.63, 0.3, 1.0), into(0, 0, 1), into(0, kOscCount, 2), parallel(0),
                         mono(0, 90), env(0, 0, 40, 300, 1.0, 220), vib(0, 2, 5.0, 300),
                         routes(0, {{lfo1, kPartFine, 0.04}, {wheel, depth(0), 0.05}, {press, noise, 0.08}})})});
      // A wide unison lead: three saws spread across the field, a touch of drive, a quick glide.
      p.push_back({"Unison Lead", "Leads",
                   join({echo(k8thDot, 0.18, 0.35), osc(0, 0, kSaw, 0, 0, 0.9), osc(0, 1, kSaw, 0, -11, 0.9), osc(0, 2, kSaw, 0, 11, 0.9),
                         {a(panOf(1), -0.6), a(panOf(2), 0.6), a(drive, 0.18), velAmp(0, 0.4)}, filter(0, 0, kLowPass, 3500, 0.15, 0.4),
                         mono(0, 35), env(0, 0, 2, 300, 1.0, 200), vib(0, 2, 5.6, 500),
                         routes(0, {{lfo1, kPartFine, 0.035}, {wheel, depth(0), 0.06}, {press, cutoff, 0.25}})})});
      // A chiptune lead: a thin pulse, no filter, a fast vibrato that comes in after the note.
      p.push_back({"Chip Lead", "Leads",
                   join({echo(k8th, 0.15, 0.25), osc(0, 0, kPulse, 0, 0, 1.0), {a(shapeOf(0), 0.3), velAmp(0, 0.2)}, open(0), mono(0, 0),
                         env(0, 0, 1, 100, 1.0, 60), vib(0, 2, 7.0, 250),
                         routes(0, {{lfo1, kPartFine, 0.05}, {wheel, shapeOf(0), 0.4}, {slide, shapeOf(0), 0.4}})})});
      // A screaming lead: a saw and a square into a hard-driven, resonant ladder. Press opens it up and
      // makes it scream; the wheel adds vibrato.
      p.push_back({"Screamer", "Leads",
                   join({echo(k8thDot, 0.16, 0.35), osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kSquare, 0, 4, 0.7),
                         filter(0, 0, kLadder, 1300, 0.55, 0.4), {a(tailParam(kDriveType), kHard), a(drive, 0.45), velAmp(0, 0.3)},
                         mono(0, 40), env(0, 0, 2, 300, 1.0, 200), env(0, 1, 2, 500, 0.3, 200), vib(0, 2, 5.8, 400),
                         routes(0, {{env2, cutoff, 0.15}, {lfo1, kPartFine, 0.0}, {wheel, depth(1), 0.06}, {press, cutoff, 0.25},
                                    {press, resonance, 0.2}})})});
      // A breathy flute: a sine with a little second harmonic, breath through a band pass at the note,
      // and a vibrato that blooms. Press blows harder.
      p.push_back({"Breathy Flute", "Leads",
                   join({room(0.25, 0.7, 2.5, 0.5), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.12), op(0, 2, kSine, 3.0, 0.04),
                         {a(noise, 0.06), a(color, 0.2), velAmp(0, 0.4)}, filter(0, 0, kLowPass, 4000, 0.0, 0.5),
                         filter(0, 1, kBandPass, 261.63, 0.25, 1.0), into(0, kOscCount, 2), into(0, 0, 1), into(0, 1, 1), into(0, 2, 1),
                         parallel(0), mono(0, 40), env(0, 0, 60, 300, 1.0, 180), env(0, 2, 0.5, 120, 0.0, 80), vib(0, 2, 5.0, 350),
                         routes(0, {{env3, noise, 0.12}, {lfo1, kPartFine, 0.03}, {wheel, depth(1), 0.05}, {press, level(1), 0.25},
                                    {press, noise, 0.06}})})});
      // A lead built to be played with an expressive controller: Press opens it and adds vibrato, Slide
      // narrows the pulse, and a wide bend lets you slide between notes. From a keyboard the wheel
      // does the vibrato.
      p.push_back({"Expressive Lead", "Leads",
                   join({echo(k8thDot, 0.16, 0.3), osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kPulse, 0, 4, 0.7), {a(shapeOf(1), 0.6)},
                         filter(0, 0, kLowPass, 1200, 0.2, 0.5), {velAmp(0, 0.4)}, mono(0, 30), env(0, 0, 8, 300, 1.0, 220),
                         vib(0, 2, 5.6, 0),
                         routes(0, {{press, cutoff, 0.35}, {lfo1, kPartFine, 0.0}, {press, depth(1), 0.05}, {slide, shapeOf(1), -0.4},
                                    {wheel, depth(1), 0.05}})})});

      // ---- Pads ----

      // A warm analog pad: two saws and a sub pulse, a low pass that opens slowly after the chord and
      // breathes a little, and drift between the voices.
      p.push_back({"Warm Analog Pad", "Pads",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 1.0), osc(0, 2, kPulse, -12, 0, 0.3),
                         {a(panOf(0), -0.45), a(panOf(1), 0.45), a(kDrift, 0.12), velAmp(0, 0.4), poly(0, 12)},
                         filter(0, 0, kLowPass, 900, 0.15, 0.4), env(0, 0, 600, 1000, 1.0, 1800), env(0, 1, 1500, 3000, 0.6, 2000),
                         lfo(0, 2, kLfoSine, 0.25),
                         routes(0, {{env2, cutoff, 0.15}, {lfo1, cutoff, 0.04}, {wheel, cutoff, 0.25}, {press, cutoff, 0.2}})})});
      // A string machine: three saws, an octave on top, through an ensemble of two slow and quick
      // pitch wobbles, panned wide.
      p.push_back({"String Machine", "Pads",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 1.0), osc(0, 2, kSaw, 12, 0, 0.35),
                         {a(panOf(0), -0.5), a(panOf(1), 0.5), velAmp(0, 0.4), poly(0, 12)}, filter(0, 0, kLowPass, 3200, 0.1, 0.3),
                         filter(0, 1, kHighPass, 180, 0.0, 0.0), slope(0, 1, 0), env(0, 0, 350, 1000, 1.0, 1500),
                         lfo(0, 2, kLfoSine, 0.6), lfo(0, 3, kLfoSine, 5.5),
                         routes(0, {{lfo1, fineOf(0), 0.04}, {lfo2, fineOf(1), 0.02}, {wheel, cutoff, -0.2}, {press, cutoff, 0.15}})})});
      // A glass pad: soft FM with a ×3 modulator that slowly comes and goes, and a second carrier a few
      // cents off for a slow beat. Press brightens it.
      p.push_back({"Glass Pad", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 1.0, 0.7), open(0),
                         {pv(0, fineOf(2), 6), a(panOf(2), 0.5), a(panOf(0), -0.3), a(fm21, 0.14), velAmp(0, 0.4), poly(0, 12)},
                         env(0, 0, 900, 1000, 1.0, 2500), lfo(0, 2, kLfoSine, 0.15),
                         routes(0, {{lfo1, fm21, 0.07}, {press, fm21, 0.15}, {wheel, fm21, 0.15}})})});
      // A choir singing "ah": a saw and a pulse through two fixed formants, with a slow vibrato and a
      // breath. The wheel leans the vowel toward "oh".
      p.push_back({"Choir Aah", "Pads",
                   join({osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kPulse, 0, 5, 0.7), {a(shapeOf(1), 0.35), a(noise, 0.03), velAmp(0, 0.4),
                         a(kDrift, 0.08), poly(0, 12)},
                         vox(0, 750, 1150, 0.45), env(0, 0, 500, 1000, 1.0, 1500), vib(0, 2, 5.0, 300),
                         routes(0, {{lfo1, kPartFine, 0.025}, {wheel, cutoff, -0.05}, {wheel, cutoff2, -0.06}, {press, cutoff2, 0.04}})})});
      // A pulse-width pad: two pulses whose widths two slow LFOs sweep in opposite ways.
      p.push_back({"PWM Pad", "Pads",
                   join({osc(0, 0, kPulse, 0, -5, 1.0), osc(0, 1, kPulse, 0, 5, 1.0),
                         {a(shapeOf(0), 0.5), a(shapeOf(1), 0.5), a(panOf(0), -0.45), a(panOf(1), 0.45), velAmp(0, 0.4), poly(0, 12)},
                         filter(0, 0, kLowPass, 1600, 0.15, 0.4), env(0, 0, 400, 1000, 1.0, 1500), lfo(0, 2, kLfoSine, 0.4),
                         lfo(0, 3, kLfoTriangle, 0.53),
                         routes(0, {{lfo1, shapeOf(0), 0.35}, {lfo2, shapeOf(1), -0.35}, {wheel, cutoff, 0.25}, {press, cutoff, 0.15}})})});
      // A sweeping pad: saws in a resonant low pass that sweeps up for four seconds after the chord and
      // settles half way.
      p.push_back({"Sweep Pad", "Pads",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 1.0), {a(panOf(0), -0.4), a(panOf(1), 0.4), velAmp(0, 0.4),
                         a(kDrift, 0.08), poly(0, 12)},
                         filter(0, 0, kLowPass, 300, 0.4, 0.3), env(0, 0, 500, 1000, 1.0, 2000), env(0, 1, 4000, 4000, 0.45, 3000),
                         routes(0, {{env2, cutoff, 0.45}, {wheel, resonance, 0.3}, {press, cutoff, 0.15}})})});
      // A dark pad: saws an octave down and a sine sub in a steep, low filter, moving slowly.
      p.push_back({"Dark Pad", "Pads",
                   join({osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, -12, 8, 0.8), osc(0, 2, kSine, -12, 0, 0.5),
                         {a(panOf(0), -0.4), a(panOf(1), 0.4), a(kDrift, 0.1), velAmp(0, 0.4), poly(0, 12)}, slope(0, 0, 2),
                         filter(0, 0, kLowPass, 450, 0.2, 0.3), env(0, 0, 800, 1000, 1.0, 2500), lfo(0, 2, kLfoSmooth, 0.12),
                         routes(0, {{lfo1, cutoff, 0.06}, {wheel, cutoff, 0.3}, {press, cutoff, 0.2}})})});
      // An airy pad: a soft triangle and its octave, washed in high, filtered air.
      p.push_back({"Air Pad", "Pads",
                   join({osc(0, 0, kTriangle, 0, -4, 0.8), osc(0, 1, kSine, 12, 4, 0.4), {a(noise, 0.12), a(color, 0.5), velAmp(0, 0.4),
                         a(kDrift, 0.06), poly(0, 12)},
                         filter(0, 0, kLowPass, 3000, 0.0, 0.3), filter(0, 1, kHighPass, 2500, 0.2, 0.3), into(0, 0, 1), into(0, 1, 1),
                         into(0, kOscCount, 2), parallel(0), env(0, 0, 900, 1000, 1.0, 2500), lfo(0, 2, kLfoSmooth, 0.2),
                         routes(0, {{lfo1, noise, 0.05}, {lfo1, cutoff2, 0.08}, {wheel, noise, 0.15}, {press, noise, 0.1}})})});
      // An evolving pad: a saw and a pulse whose filter and width two long looping Shape envelopes
      // keep moving, out of step with each other, so a held chord never repeats itself.
      p.push_back({"Evolving Pad", "Pads",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kPulse, 0, 6, 0.8), osc(0, 2, kSine, 12, 0, 0.15),
                         {a(panOf(0), -0.4), a(panOf(1), 0.4), velAmp(0, 0.4), a(kDrift, 0.08), poly(0, 12)},
                         filter(0, 0, kLowPass, 700, 0.25, 0.4), env(0, 0, 1200, 1000, 1.0, 3000),
                         shape(0, 1, "Twin Hills", 9000, kForever), shape(0, 2, "Wander", 13000, kForever),
                         routes(0, {{env2, cutoff, 0.22}, {env3, shapeOf(1), 0.35}, {env3, level(2), 0.25}, {wheel, cutoff, 0.2},
                                    {press, cutoff, 0.15}})})});
      // A bright unison pad for trance and pop: three saws spread across the field, cut below and
      // open above.
      p.push_back({"Bright Unison", "Pads",
                   join({osc(0, 0, kSaw, 0, 0, 0.8), osc(0, 1, kSaw, 0, -14, 0.8), osc(0, 2, kSaw, 0, 14, 0.8),
                         {a(panOf(1), -0.7), a(panOf(2), 0.7), velAmp(0, 0.4), poly(0, 12)}, filter(0, 0, kLowPass, 5000, 0.1, 0.2),
                         filter(0, 1, kHighPass, 220, 0.0, 0.0), slope(0, 1, 0), env(0, 0, 150, 1000, 1.0, 1200),
                         routes(0, {{wheel, cutoff, -0.3}, {press, cutoff, 0.1}})})});
      // A soft sine pad: a sine, a quiet octave and a triangle a few cents off, slow to bloom.
      p.push_back({"Soft Sine Pad", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.25), op(0, 2, kTriangle, 1.0, 0.5), open(0),
                         {pv(0, fineOf(2), 5), a(panOf(2), 0.5), a(panOf(0), -0.3), velAmp(0, 0.3), a(kDrift, 0.06), poly(0, 12)},
                         env(0, 0, 1200, 1000, 1.0, 2500),
                         routes(0, {{wheel, level(1), 0.4}, {press, level(1), 0.3}})})});
      // An "ooh" pad whose vowel slowly opens toward "ah" and back on an LFO.
      p.push_back({"Vowel Pad", "Pads",
                   join({osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kSaw, 0, 5, 0.8), {a(panOf(0), -0.4), a(panOf(1), 0.4), a(noise, 0.02),
                         velAmp(0, 0.4), poly(0, 12)},
                         vox(0, 400, 800, 0.45), env(0, 0, 700, 1000, 1.0, 2000), lfo(0, 2, kLfoSine, 0.09),
                         {a(lfoParam(2, kLfoPolarity), 1)},
                         routes(0, {{lfo1, cutoff, 0.09}, {lfo1, cutoff2, 0.06}, {wheel, cutoff2, 0.08}, {press, cutoff, 0.05}})})});

      // ---- Strings and Brass ----

      // An ensemble of strings: saws in a gentle ensemble, a bowed attack that's quicker on harder
      // notes, and a vibrato that comes in late.
      p.push_back({"Ensemble Strings", "Strings and Brass",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 1.0), osc(0, 2, kSaw, 0, 0, 0.5),
                         {a(panOf(0), -0.5), a(panOf(1), 0.5), velAmp(0, 0.5), a(kDrift, 0.06), poly(0, 16)},
                         filter(0, 0, kLowPass, 2300, 0.1, 0.4), filter(0, 1, kHighPass, 200, 0.0, 0.0), slope(0, 1, 0),
                         env(0, 0, 280, 1000, 1.0, 900), vib(0, 2, 5.2, 600), lfo(0, 3, kLfoSine, 0.7),
                         routes(0, {{lfo1, kPartFine, 0.025}, {lfo2, fineOf(2), 0.03}, {velocity, ampAttack, -0.12}, {wheel, cutoff, 0.2},
                                    {press, cutoff, 0.15}})})});
      // Slow strings: the same section further back, swelling in over a second and a half.
      p.push_back({"Slow Strings", "Strings and Brass",
                   join({osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0), osc(0, 2, kSaw, -12, 0, 0.35),
                         {a(panOf(0), -0.55), a(panOf(1), 0.55), velAmp(0, 0.3), a(kDrift, 0.08), poly(0, 16)},
                         filter(0, 0, kLowPass, 1500, 0.1, 0.4), filter(0, 1, kHighPass, 160, 0.0, 0.0), slope(0, 1, 0),
                         env(0, 0, 1500, 1000, 1.0, 2000), vib(0, 2, 5.0, 900), lfo(0, 3, kLfoSine, 0.5),
                         routes(0, {{lfo1, kPartFine, 0.02}, {lfo2, fineOf(2), 0.03}, {wheel, cutoff, 0.25}, {press, cutoff, 0.15}})})});
      // A solo cello: one bowed voice, legato, its body a peak in the low mids, a vibrato that blooms,
      // and the bow's hiss. Press leans on the bow.
      p.push_back({"Solo Cello", "Strings and Brass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 3, 0.4), {a(shapeOf(1), 0.6), a(noise, 0.02), a(color, 0.3),
                         velAmp(0, 0.4)},
                         filter(0, 0, kLowPass, 1800, 0.1, 0.5), filter(0, 1, kPeak, 550, 0.35, 0.0), mono(0, 70),
                         {a(kPartTranspose, -12)}, env(0, 0, 120, 300, 1.0, 300), vib(0, 2, 5.4, 350),
                         routes(0, {{lfo1, kPartFine, 0.035}, {wheel, depth(0), 0.04}, {press, cutoff, 0.25}, {press, noise, 0.04}})})});
      // Synth brass: two saws through a low pass that blooms a beat after the note speaks, a slight
      // scoop up into pitch, and deeper with velocity.
      p.push_back({"Synth Brass", "Strings and Brass",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 1.0), {a(panOf(0), -0.3), a(panOf(1), 0.3), velAmp(0, 0.5),
                         a(kDrift, 0.05), poly(0, 12)},
                         filter(0, 0, kLowPass, 500, 0.15, 0.5), env(0, 0, 25, 300, 0.9, 250), env(0, 1, 70, 600, 0.45, 300),
                         envVel(0, 1, 0.5), env(0, 2, 0.5, 70, 0.0, 30),
                         routes(0, {{env2, cutoff, 0.35}, {env3, kPartFine, -0.08}, {wheel, cutoff, 0.2}, {press, cutoff, 0.2}})})});
      // Brass stabs: brighter and shorter, a punch of filter for hits on the beat.
      p.push_back({"Brass Stab", "Strings and Brass",
                   join({osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0), osc(0, 2, kSaw, -12, 0, 0.4),
                         {a(panOf(0), -0.35), a(panOf(1), 0.35), velAmp(0, 0.6), poly(0, 12)}, filter(0, 0, kLowPass, 700, 0.15, 0.5),
                         env(0, 0, 6, 450, 0.3, 160), env(0, 1, 20, 300, 0.25, 160), envVel(0, 1, 0.5), env(0, 2, 0.5, 50, 0.0, 30),
                         routes(0, {{env2, cutoff, 0.35}, {env3, kPartFine, -0.06}, {wheel, cutoff, 0.2}, {press, cutoff, 0.2}})})});
      // Soft horns: a saw and a triangle, mellow and slow to speak, like a French horn section.
      p.push_back({"Soft Horns", "Strings and Brass",
                   join({osc(0, 0, kSaw, 0, -4, 0.8), osc(0, 1, kTriangle, 0, 4, 0.9), {velAmp(0, 0.5), a(kDrift, 0.06), poly(0, 12)},
                         filter(0, 0, kLowPass, 450, 0.05, 0.6), env(0, 0, 120, 300, 1.0, 400), env(0, 1, 200, 800, 0.5, 400),
                         envVel(0, 1, 0.6), vib(0, 2, 5.0, 700),
                         routes(0, {{env2, cutoff, 0.18}, {lfo1, kPartFine, 0.015}, {wheel, cutoff, 0.2}, {press, cutoff, 0.2}})})});
      // A solo trumpet: one bright legato voice that blooms with the breath, a scoop into each note and
      // a late vibrato. Press is the breath.
      p.push_back({"Solo Trumpet", "Strings and Brass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 2, 0.3), {a(shapeOf(1), 0.5), velAmp(0, 0.5)},
                         filter(0, 0, kLowPass, 900, 0.12, 0.6), filter(0, 1, kPeak, 1400, 0.3, 0.0), mono(0, 30),
                         env(0, 0, 25, 300, 1.0, 180), env(0, 1, 50, 500, 0.6, 200), envVel(0, 1, 0.6), env(0, 2, 0.5, 60, 0.0, 30),
                         vib(0, 2, 5.6, 500),
                         routes(0, {{env2, cutoff, 0.25}, {env3, kPartFine, -0.07}, {lfo1, kPartFine, 0.03}, {press, cutoff, 0.25},
                                    {wheel, depth(2), 0.04}})})});
      // Tremolo strings: the ensemble bowed fast, a quick swell on every stroke.
      p.push_back({"Tremolo Strings", "Strings and Brass",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 1.0), {a(panOf(0), -0.5), a(panOf(1), 0.5), velAmp(0, 0.5),
                         poly(0, 16)},
                         filter(0, 0, kLowPass, 2000, 0.1, 0.4), filter(0, 1, kHighPass, 200, 0.0, 0.0), slope(0, 1, 0),
                         env(0, 0, 150, 1000, 1.0, 700), lfo(0, 2, kLfoTriangle, 7.5), {a(lfoParam(2, kLfoPolarity), 1)},
                         routes(0, {{lfo1, kPartLevel, -0.08}, {lfo1, cutoff, 0.03}, {wheel, lfo1Rate, 0.08}, {press, cutoff, 0.15}})})});

      // ---- Arps and Sequences: hold a chord, and the patch plays it in time with the host ----

      // A trance gate: bright unison saws chopped into sixteenths in time with the host.
      p.push_back({"Trance Gate", "Arps and Sequences",
                   join({echo(k8thDot, 0.15, 0.3), osc(0, 0, kSaw, 0, 0, 0.8), osc(0, 1, kSaw, 0, -12, 0.8), osc(0, 2, kSaw, 0, 12, 0.8),
                         {a(panOf(1), -0.6), a(panOf(2), 0.6), velAmp(0, 0.3), poly(0, 12)}, filter(0, 0, kLowPass, 4000, 0.15, 0.2),
                         shapeSync(0, 0, "Gate 8", kHalfBar, kForever), {a(ampRelease, timeValue(150))},
                         routes(0, {{wheel, cutoff, -0.3}, {press, cutoff, 0.1}})})});
      // An eighth-note pulse: a plucked pulse retriggered every eighth, its filter stepping to a new
      // place each time.
      p.push_back({"Pulse Sequence", "Arps and Sequences",
                   join({echo(k8thDot, 0.2, 0.35), osc(0, 0, kPulse, 0, 0, 1.0), osc(0, 1, kSaw, 0, 7, 0.5), {a(shapeOf(0), 0.4), velAmp(0, 0.3),
                         poly(0, 12)},
                         filter(0, 0, kLowPass, 500, 0.4, 0.4), shapeSync(0, 0, "Gate 8", kBar, kForever), {a(ampRelease, timeValue(200))},
                         shapeSync(0, 1, "Random 16", kBars2, kForever),
                         routes(0, {{env2, cutoff, 0.3}, {kSourceEnv1, cutoff, 0.15}, {wheel, resonance, 0.25}, {press, cutoff, 0.15}})})});
      // A sample-and-hold filter: a saw into a resonant ladder whose cutoff jumps to a new random step
      // every sixteenth.
      p.push_back({"Random Filter", "Arps and Sequences",
                   join({osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kSaw, 0, 5, 0.8), {velAmp(0, 0.3), poly(0, 12)},
                         filter(0, 0, kLadder, 350, 0.6, 0.3), env(0, 0, 5, 1000, 1.0, 300), shapeSync(0, 1, "Random 16", kBar, kForever),
                         routes(0, {{env2, cutoff, 0.35}, {wheel, resonance, 0.25}, {press, cutoff, 0.2}})})});
      // An octave arpeggio: each held note leaps through four octaves an eighth at a time, struck as
      // sixteenths.
      p.push_back({"Octave Arp", "Arps and Sequences",
                   join({echo(k8thDot, 0.18, 0.35), osc(0, 0, kSaw, -24, 0, 1.0), osc(0, 1, kSquare, -24, 6, 0.5), {velAmp(0, 0.3), poly(0, 12)},
                         filter(0, 0, kLowPass, 1400, 0.3, 0.6), shapeSync(0, 0, "Gate 8", kHalfBar, kForever),
                         {a(ampRelease, timeValue(120))}, shapeSync(0, 1, "Arp Up", kHalfBar, kForever),
                         routes(0, {{env2, coarseOf(0), 0.5}, {env2, coarseOf(1), 0.5}, {wheel, cutoff, 0.25}, {press, resonance, 0.2}})})});
      // A dotted delay pluck: quarter-note plucks fed into dotted-eighth echoes, so a held chord plays
      // a rolling rhythm.
      p.push_back({"Echo Pluck", "Arps and Sequences",
                   join({echo(k8thDot, 0.35, 0.45), osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSquare, 0, 6, 0.6), {velAmp(0, 0.3), poly(0, 12)},
                         filter(0, 0, kLowPass, 600, 0.35, 0.4), shapeSync(0, 0, "Gate 4", kBar, kForever), {a(ampRelease, timeValue(200))},
                         shapeSync(0, 1, "Pluck", kBeat, kForever),
                         routes(0, {{env2, cutoff, 0.35}, {wheel, resonance, 0.25}, {press, cutoff, 0.15}})})});
      // A chopped pad: a warm pad gated in a syncopated pattern of eighths, with the filter stepping.
      p.push_back({"Pad Chopper", "Arps and Sequences",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 1.0), {a(panOf(0), -0.45), a(panOf(1), 0.45), velAmp(0, 0.3),
                         poly(0, 12)},
                         filter(0, 0, kLowPass, 1200, 0.2, 0.3), shapeSync(0, 0, "Gate 4", kHalfBar, kForever), {a(ampRelease, timeValue(150))},
                         shapeSync(0, 1, "Arp Up", kBars2, kForever),
                         routes(0, {{env2, cutoff, 0.2}, {wheel, cutoff, 0.25}, {press, cutoff, 0.15}})})});
      // Computer bleeps: an FM blip whose modulator jumps to a new ratio every sixteenth.
      p.push_back({"Data Bleeps", "Arps and Sequences",
                   join({echo(k8th, 0.2, 0.35), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), open(0), {a(fm21, 0.25), velAmp(0, 0.3), poly(0, 12)},
                         {a(kPartTranspose, 12)}, shapeSync(0, 0, "Gate 8", kHalfBar, kForever), {a(ampRelease, timeValue(80))},
                         shapeSync(0, 1, "Random 16", kBar, kForever),
                         routes(0, {{env2, ratioOf(1), 0.25}, {wheel, fm21, 0.2}, {press, fm21, 0.15}})})});
      // A wobbling arp: a resonant saw opened and closed eight times a bar by a synced Wobble 8.
      p.push_back({"Wobble Arp", "Arps and Sequences",
                   join({osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kSaw, -12, 5, 0.6), {velAmp(0, 0.3), poly(0, 12)},
                         filter(0, 0, kLowPass, 250, 0.45, 0.3), env(0, 0, 5, 1000, 1.0, 300), shapeSync(0, 1, "Wobble 8", kBar, kForever),
                         routes(0, {{env2, cutoff, 0.4}, {wheel, resonance, 0.25}, {press, cutoff, 0.15}})})});

      // ---- Percussion: play them from C2 up ----

      // A kick drum: a sine whose pitch drops fast from a knock, a little drive, tuned by the key.
      p.push_back({"Analog Kick", "Percussion",
                   join({op(0, 0, kSine, 1.0, 1.0), open(0), {a(drive, 0.25), velAmp(0, 0.6), poly(0, 4)},
                         env(0, 0, 0.5, 420, 0.0, 80), ampCurves(0, 0.4, 0.3), env(0, 2, 0.5, 45, 0.0, 30),
                         routes(0, {{env3, coarseOf(0), 0.3}, {wheel, ampDecay, 0.15}})})});
      // A snare: a pitched body and a burst of bright noise, each through its own filter.
      p.push_back({"Snare", "Percussion",
                   join({osc(0, 0, kTriangle, 12, 0, 0.8), {a(noise, 0.9), a(color, 0.5), velAmp(0, 0.7), poly(0, 4)}, parallel(0),
                         into(0, 0, 1), into(0, kOscCount, 2), filter(0, 0, kLowPass, 3000, 0.0, 0.0), filter(0, 1, kHighPass, 1400, 0.1, 0.0),
                         slope(0, 1, 0), env(0, 0, 0.5, 240, 0.0, 120), ampCurves(0, 0.6, 0.4), env(0, 2, 0.5, 30, 0.0, 20),
                         env(0, 1, 0.5, 90, 0.0, 60),
                         routes(0, {{env3, coarseOf(0), 0.05}, {wheel, cutoff2, 0.15}})})});
      // A closed hi-hat: high, thin, metallic noise, gone in a moment.
      p.push_back({"Closed Hat", "Percussion",
                   join({fixedOsc(0, 0, kSquare, 540, 0.15), fixedOsc(0, 1, kSquare, 800, 0.15), {a(noise, 1.0), a(color, 1.0), velAmp(0, 0.7), poly(0, 4)},
                         filter(0, 0, kHighPass, 7000, 0.2, 0.0), env(0, 0, 0.5, 60, 0.0, 40), ampCurves(0, 0.6, 0.4),
                         routes(0, {{wheel, ampDecay, 0.2}})})});
      // An open hi-hat: the same, left to ring.
      p.push_back({"Open Hat", "Percussion",
                   join({fixedOsc(0, 0, kSquare, 540, 0.15), fixedOsc(0, 1, kSquare, 800, 0.15), {a(noise, 1.0), a(color, 1.0), velAmp(0, 0.7), poly(0, 4)},
                         filter(0, 0, kHighPass, 6500, 0.2, 0.0), env(0, 0, 0.5, 600, 0.0, 200), ampCurves(0, 0.5, 0.4),
                         routes(0, {{wheel, ampDecay, -0.2}})})});
      // A tom: a sine and a triangle that drop in pitch as they ring, tuned by the key.
      p.push_back({"Tom", "Percussion",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kTriangle, 1.5, 0.2), open(0), {a(noise, 0.06), velAmp(0, 0.7), poly(0, 4)},
                         env(0, 0, 0.5, 520, 0.0, 120), ampCurves(0, 0.5, 0.4), env(0, 2, 0.5, 160, 0.0, 60), env(0, 1, 0.5, 20, 0.0, 10),
                         routes(0, {{env3, coarseOf(0), 0.06}, {env3, coarseOf(1), 0.06}, {env2, noise, 0.3}, {wheel, ampDecay, 0.15}})})});
      // A hand clap: a band of noise struck several times in quick succession.
      p.push_back({"Hand Clap", "Percussion",
                   join({osc(0, 0, kSine, 0, 0, 0.0), {a(noise, 1.0), a(color, 0.3), velAmp(0, 0.7), poly(0, 4)},
                         filter(0, 0, kLowPass, 3800, 0.1, 0.0), slope(0, 0, 0), filter(0, 1, kHighPass, 750, 0.15, 0.0),
                         shape(0, 0, "Saw Train", 90, kOnce), {a(ampRelease, timeValue(120))},
                         routes(0, {{wheel, cutoff, 0.15}})})});
      // A cowbell: two square tones at fixed pitches through a band pass, short and bright.
      p.push_back({"Cowbell", "Percussion",
                   join({fixedOsc(0, 0, kSquare, 562, 0.7), fixedOsc(0, 1, kSquare, 845, 0.6), {velAmp(0, 0.7), poly(0, 4)},
                         filter(0, 0, kBandPass, 900, 0.3, 0.0), slope(0, 0, 0), env(0, 0, 0.5, 320, 0.0, 80), ampCurves(0, 0.7, 0.4),
                         routes(0, {{wheel, cutoff, 0.15}})})});
      // A rim shot: a high sine knock and a click of noise, very short.
      p.push_back({"Rim Shot", "Percussion",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kTriangle, 2.3, 0.4), open(0), {a(kPartTranspose, 24), a(noise, 0.3), a(color, 0.8),
                         velAmp(0, 0.7), poly(0, 4)},
                         env(0, 0, 0.5, 70, 0.0, 40), ampCurves(0, 0.6, 0.4),
                         routes(0, {{wheel, ampDecay, 0.15}})})});

      // ---- FX ----

      // A riser: noise and a saw climbing through a high pass that opens over four seconds.
      p.push_back({"Riser", "FX",
                   join({room(0.3, 0.8, 4.0, 0.4), osc(0, 0, kSaw, 0, 0, 0.4), {a(noise, 0.8), a(color, 0.6), velAmp(0, 0.3)},
                         filter(0, 0, kHighPass, 150, 0.35, 0.0), env(0, 0, 3500, 1000, 1.0, 1500), env(0, 1, 4000, 1000, 1.0, 1500),
                         routes(0, {{env2, cutoff, 0.6}, {env2, coarseOf(0), 0.125}, {wheel, resonance, 0.3}})})});
      // A downlifter: the same falling away, high to low.
      p.push_back({"Downlifter", "FX",
                   join({room(0.3, 0.8, 4.0, 0.4), osc(0, 0, kSaw, 12, 0, 0.4), {a(noise, 0.8), a(color, 0.4), velAmp(0, 0.3)},
                         filter(0, 0, kLowPass, 250, 0.3, 0.0), env(0, 0, 5, 3500, 0.0, 800), ampCurves(0, -0.3, 0.0),
                         env(0, 1, 0.5, 3500, 0.0, 800),
                         routes(0, {{env2, cutoff, 0.6}, {env2, coarseOf(0), 0.2}, {wheel, resonance, 0.3}})})});
      // A laser: a sine and a saw diving three octaves in a blink, into echoes.
      p.push_back({"Laser", "FX",
                   join({echo(k8th, 0.3, 0.45), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSaw, 1.0, 0.3), open(0), {a(fm21, 0.1), velAmp(0, 0.5)},
                         {a(kPartTranspose, 12)}, env(0, 0, 0.5, 250, 0.0, 80), env(0, 2, 0.5, 180, 0.0, 60),
                         routes(0, {{env3, coarseOf(0), 0.4}, {env3, ratioOf(1), 0.1}, {wheel, fm21, 0.3}})})});
      // Wind: pink noise through a resonant band pass that drifts on its own, swelling and dying.
      p.push_back({"Wind", "FX",
                   join({room(0.35, 0.85, 5.0, 0.5), osc(0, 0, kSine, 0, 0, 0.0), {a(noise, 1.0), a(color, -0.4), velAmp(0, 0.3)},
                         filter(0, 0, kBandPass, 700, 0.55, 0.5), slope(0, 0, 0), env(0, 0, 1500, 1000, 1.0, 2500),
                         lfo(0, 2, kLfoSmooth, 0.15), lfo(0, 3, kLfoSmooth, 0.08),
                         routes(0, {{lfo1, cutoff, 0.18}, {lfo2, kPartLevel, 0.1}, {wheel, resonance, 0.3}, {press, cutoff, 0.2}})})});
      // Surf: noise rolling in and out on a slow looping Breath, its brightness rising with each wave.
      p.push_back({"Ocean Surf", "FX",
                   join({room(0.3, 0.85, 4.0, 0.5), osc(0, 0, kSine, 0, 0, 0.0), {a(noise, 1.0), a(color, -0.2), velAmp(0, 0.3)},
                         filter(0, 0, kLowPass, 400, 0.05, 0.0), env(0, 0, 500, 1000, 1.0, 3000),
                         shape(0, 1, "Breath", 7000, kForever),
                         routes(0, {{env2, cutoff, 0.35}, {env2, kPartLevel, 0.15}, {wheel, cutoff, 0.2}})})});
      // A UFO: a sine whose pitch wobbles fast, ring-barked, drifting overhead.
      p.push_back({"UFO", "FX",
                   join({echo(kQuarterDot, 0.25, 0.4), op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.5, 0.0), open(0), {a(fm21, 0.2), velAmp(0, 0.3)},
                         env(0, 0, 400, 1000, 1.0, 1500), lfo(0, 2, kLfoSine, 7.0), lfo(0, 3, kLfoSine, 0.2),
                         routes(0, {{lfo1, coarseOf(0), 0.02}, {lfo2, lfo1Rate, 0.12}, {lfo2, fm21, 0.15}, {wheel, lfo1Rate, 0.2}})})});
      // A siren: a square swinging up and down a fifth, slowly.
      p.push_back({"Siren", "FX",
                   join({echo(kQuarter, 0.2, 0.3), osc(0, 0, kSquare, 0, 0, 0.6), osc(0, 1, kSaw, 0, 8, 0.4), {velAmp(0, 0.3)},
                         filter(0, 0, kLowPass, 2500, 0.2, 0.5), env(0, 0, 200, 1000, 1.0, 800), lfo(0, 2, kLfoTriangle, 0.45),
                         {a(lfoParam(2, kLfoPolarity), 1)},
                         routes(0, {{lfo1, coarseOf(0), 0.075}, {lfo1, coarseOf(1), 0.075}, {wheel, lfo1Rate, 0.25}})})});
      // Thunder: deep, driven noise that cracks and rolls away.
      p.push_back({"Thunder", "FX",
                   join({room(0.4, 0.95, 7.0, 0.6), osc(0, 0, kSine, 0, 0, 0.0), {a(noise, 1.0), a(color, -1.0), a(tailParam(kDriveType), kTube),
                         a(drive, 0.4), velAmp(0, 0.5)},
                         filter(0, 0, kLowPass, 220, 0.2, 0.0), shape(0, 0, "Collapse", 5000, kOnce), {a(ampRelease, timeValue(2000))},
                         lfo(0, 2, kLfoSmooth, 3.0),
                         routes(0, {{lfo1, cutoff, 0.1}, {kSourceEnv1, cutoff, 0.15}, {wheel, cutoff, 0.2}})})});

      // Each category's room, for every preset in it that doesn't set its own: long, soft halls for
      // the flock's ambient and dark drones, a tight, bright room for its industrial ones, small rooms
      // for the keys, plucks and leads, and none for the bass, which stays dry and centred.
      struct Space {
         const char *category;
         double mix, size, decay, damp;
      };
      static const Space spaces[] = {{"Ambient Drones", 0.42, 0.9, 7.0, 0.45}, {"Dark Drones", 0.36, 0.85, 6.0, 0.65},
                                     {"Textures", 0.32, 0.7, 3.5, 0.5},        {"Industrial", 0.22, 0.45, 2.2, 0.3},
                                     {"Landscapes", 0.36, 0.85, 5.5, 0.5},     {"Keys and Bells", 0.22, 0.6, 2.2, 0.5},
                                     {"Keys", 0.16, 0.55, 1.8, 0.5},           {"Organs", 0.2, 0.65, 2.2, 0.5},
                                     {"Bells and Mallets", 0.25, 0.7, 3.0, 0.45}, {"Plucks", 0.2, 0.6, 2.0, 0.5},
                                     {"Leads", 0.16, 0.55, 1.8, 0.5},          {"Pads", 0.32, 0.8, 4.5, 0.5},
                                     {"Strings and Brass", 0.26, 0.75, 3.0, 0.5}, {"Arps and Sequences", 0.2, 0.6, 2.2, 0.5},
                                     {"Percussion", 0.1, 0.4, 1.0, 0.5},       {"FX", 0.3, 0.8, 4.0, 0.5}};
      for (Preset &preset : p)
         for (const Space &s : spaces)
            if (preset.category == s.category &&
                std::none_of(preset.values.begin(), preset.values.end(), [](const auto &v) { return v.first == kReverbOn; })) {
               const Values r = room(s.mix, s.size, s.decay, s.damp);
               preset.values.insert(preset.values.end(), r.begin(), r.end());
            }
      // The flock's own showcases, each in the room it was voiced in, sit together at the end.
      for (Preset &preset : p)
         for (const char *flock : {"Ambient Drones", "Dark Drones", "Textures", "Industrial", "Landscapes", "Keys and Bells", "Bass and Leads"})
            if (preset.category == flock)
               preset.category = "Flock";

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
         {"Breathing Planet", -3.5},
         {"Cirrus", -4.5},
         {"Abyssal", -2.0},
         {"Obsidian Choir", 6.5},
         {"Gravity Well", -4.5},
         {"Firefly Field", 12.0},
         {"Rain on Glass", 9.0},
         {"Embers", 6.5},
         {"Aeolian Harp", -7.5},
         {"Turbine", -3.5},
         {"Scrapyard", -1.5},
         {"Engine Room", 0.5},
         {"Glass and Iron", -2.0},
         {"Predator Weather", -3.5},
         {"Two Flocks", -3.0},
         {"Flock Bells", -1.5},
         {"Sky Theremin", 1.0},
         {"Tine Piano", -1.5},
         {"Suitcase", 2.5},
         {"Reed Piano", 4.5},
         {"Clav Funk", -1.0},
         {"Felt Upright", 0.5},
         {"Crystal Keys", -1.0},
         {"Harpsichord", 2.5},
         {"Lo-Fi Keys", 2.5},
         {"Jazz Drawbars", -2.5},
         {"Gospel Organ", -1.5},
         {"Cathedral Pipes", -6.0},
         {"Combo Organ", -5.0},
         {"Harmonium", -9.0},
         {"Glockenspiel", -1.0},
         {"Marimba", 4.0},
         {"Vibraphone", -2.0},
         {"Tubular Bells", -2.0},
         {"Kalimba", 3.0},
         {"Steel Pan", 1.0},
         {"Music Box", 2.0},
         {"Crystal Bell", -1.5},
         {"Classic Pluck", -1.0},
         {"Square Pluck", 2.5},
         {"Nylon String", 14.5},
         {"Harp", 10.5},
         {"Pizzicato", 0.0},
         {"Bell Pluck", 3.0},
         {"Mallet Pluck", 3.0},
         {"Chord Stab", 1.5},
         {"Sub Bass", 2.5},
         {"Analog Bass", 2.5},
         {"Ladder Punch", 4.5},
         {"Acid Line", 5.5},
         {"FM Bass", 3.0},
         {"Reese", 3.5},
         {"Rubber Bass", 3.0},
         {"Wobble Bass", 3.0},
         {"Pluck Bass", 14.0},
         {"Growler", 4.0},
         {"Finger Bass", 8.0},
         {"Octave Bass", 7.5},
         {"Saw Lead", -0.5},
         {"Square Lead", -2.0},
         {"Sync Lead", 3.5},
         {"Whistle", 1.5},
         {"Unison Lead", 3.0},
         {"Chip Lead", -0.5},
         {"Screamer", 4.5},
         {"Breathy Flute", 0.5},
         {"Expressive Lead", -1.0},
         {"Warm Analog Pad", -6.0},
         {"String Machine", -5.5},
         {"Glass Pad", -7.5},
         {"Choir Aah", 5.0},
         {"PWM Pad", -9.5},
         {"Sweep Pad", -6.5},
         {"Dark Pad", -5.5},
         {"Air Pad", -1.0},
         {"Evolving Pad", -7.0},
         {"Bright Unison", -4.5},
         {"Soft Sine Pad", -6.5},
         {"Vowel Pad", 5.5},
         {"Ensemble Strings", -5.0},
         {"Slow Strings", -5.0},
         {"Solo Cello", -3.0},
         {"Synth Brass", -4.0},
         {"Brass Stab", -1.0},
         {"Soft Horns", -4.0},
         {"Solo Trumpet", -0.5},
         {"Tremolo Strings", -2.5},
         {"Trance Gate", -1.5},
         {"Pulse Sequence", -3.5},
         {"Random Filter", -5.0},
         {"Octave Arp", -3.0},
         {"Echo Pluck", -4.5},
         {"Pad Chopper", -3.0},
         {"Data Bleeps", -0.5},
         {"Wobble Arp", -5.0},
         {"Analog Kick", 17.0},
         {"Snare", 9.0},
         {"Closed Hat", 10.5},
         {"Open Hat", 9.5},
         {"Tom", 10.5},
         {"Hand Clap", 13.5},
         {"Cowbell", 12.0},
         {"Rim Shot", 11.0},
         {"Riser", 1.0},
         {"Downlifter", 1.5},
         {"Laser", 5.5},
         {"Wind", 7.0},
         {"Ocean Surf", -14.5},
         {"UFO", -4.5},
         {"Siren", -3.5},
         {"Thunder", 2.0},
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
      // The menu's order: the playable bank by kind of sound, then the flock's showcases.
      static const char *const kCategoryOrder[] = {"Init",  "Keys", "Organs", "Bells and Mallets", "Plucks", "Bass", "Leads", "Pads",
                                                   "Strings and Brass", "Arps and Sequences", "Percussion", "FX", "Flock"};
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
