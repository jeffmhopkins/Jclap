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

      // The bank is mostly showcases: the flock playing the sound, Press shaped by the mod buses,
      // drones and textures on Shape envelopes, patches that move on their own or move each other,
      // and the Monsta variations. Essentials comes first, a few plain patches by instrument family to
      // start from. Most answer the mod wheel and Press, so they play from any keyboard. The menu
      // runs in the order of kCategoryOrder below, whatever order the patches are written in.

      // The mod buses, for the showcases and the Monsta patches.
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
      constexpr int kSum = kBusSum, kMul = kBusMultiply, kMin = kBusMin, kMax = kBusMax;
      constexpr int kLinear = 0, kExpo = 1, kLog = 2, kS = 3, kAbs = 4;
      constexpr int kAsIs = 0, kToUni = 1, kToBi = 2;
      constexpr int kAll = 1;
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
      // What places a lure (kLureSourceNames).
      constexpr int kLurePress = 2, kLureSlide = 3;
      const int heading = kSourceHeading, nearest = kSourceNearest, speed = kSourceSpeed, turn = kSourceTurn;
      const int random = kSourceRandom;

      // ---- Essentials: a few plain patches to start from ----
      // Bass.
      p.push_back({"Sub Bass", "Essentials",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.12), {a(fb1, 0.1), a(kVelocityToAmp, 0.4)},
                         mono(0, 0), env(0, 0, 2, 300, 1.0, 80), open(0), routes(0, {{wheel, fb1, 0.3}})})});
      p.push_back({"Analog Bass", "Essentials",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSquare, -12, 0, 0.6), filter(0, 0, kLadder, 260, 0.25, 0.4),
                         mono(0, 25), env(0, 0, 1, 300, 0.85, 90), env(0, 1, 1, 260, 0.15, 120),
                         {a(envParam(1, kEnvVelocity), 0.5)}, routes(0, {{env2, cutoff, 0.35}, {wheel, cutoff, 0.3}})})});
      // A DX bass: a 1:1 pair, the modulator's feedback for the growl, its depth dying away fast.
      p.push_back({"FM Bass", "Essentials",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fm21, 0.22), a(fb2, 0.3)}, mono(0, 0),
                         env(0, 0, 1, 400, 0.9, 70), env(0, 2, 0.5, 250, 0.15, 100), open(0),
                         routes(0, {{env3, fm21, 0.35}, {velocity, fm21, 0.2}, {wheel, fb2, 0.3}})})});
      // Slide notes into each other for the glide; harder notes accent the filter.
      p.push_back({"Acid Bass", "Essentials",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), filter(0, 0, kLadder, 220, 0.72, 0.3), {a(filterParam(0, kFilterDrive), 0.35)},
                         mono(0, 70), env(0, 0, 1, 300, 0.8, 60), env(0, 1, 1, 220, 0.0, 80), {a(envParam(1, kEnvVelocity), 0.5)},
                         routes(0, {{env2, cutoff, 0.45}, {velocity, cutoff, 0.12}, {wheel, resonance, 0.25}, {slide, cutoff, 0.3}})})});
      p.push_back({"Reese Bass", "Essentials",
                   join({osc(0, 0, kSaw, 0, -11, 1.0), osc(0, 1, kSaw, 0, 11, 1.0), osc(0, 2, kSine, -12, 0, 0.6),
                         filter(0, 0, kLowPass, 500, 0.1, 0.4), {a(tailParam(kDriveAmount), 0.35)}, lfo(0, 3, kLfoTriangle, 0.25),
                         mono(0, 40), env(0, 0, 3, 400, 1.0, 120),
                         routes(0, {{lfo2, oscParam(1, kOscFine), 0.04}, {wheel, cutoff, 0.35}, {press, cutoff, 0.2}})})});

      // Leads.
      // The vibrato waits 350 ms, and the mod wheel deepens it.
      p.push_back({"Saw Lead", "Essentials",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 0.85), filter(0, 0, kLowPass, 2800, 0.2, 0.5),
                         lfo(0, 2, kLfoSine, 5.5), {a(lfoParam(2, kLfoDelay), timeValue(350))}, mono(0, 45),
                         env(0, 0, 3, 300, 1.0, 180), env(0, 1, 2, 400, 0.5, 200),
                         routes(0, {{env2, cutoff, 0.15}, {press, cutoff, 0.25}, {lfo1, kPartFine, 0.03}, {wheel, depth(2), 0.04},
                                    {velocity, cutoff, 0.1}})})});
      // Osc 1 is silent and only resets Osc 2, whose pitch Env 2, the wheel and Press sweep.
      p.push_back({"Sync Lead", "Essentials",
                   join({osc(0, 0, kSine, 0, 0, 0.0), osc(0, 1, kSaw, 7, 0, 1.0), {a(oscParam(1, kOscSync), 1), a(kPartLevel, -2)},
                         filter(0, 0, kLowPass, 6000, 0.1, 0.3), lfo(0, 2, kLfoSine, 5.5), {a(lfoParam(2, kLfoDelay), timeValue(400))},
                         mono(0, 40), env(0, 0, 2, 300, 1.0, 200), env(0, 1, 1, 700, 0.25, 300),
                         routes(0, {{env2, oscParam(1, kOscCoarse), 0.12}, {wheel, oscParam(1, kOscCoarse), 0.12},
                                    {press, oscParam(1, kOscCoarse), 0.1}, {lfo1, kPartFine, 0.02}})})});
      // Pressure opens the FM and brings in vibrato; Slide (CC 74) adds grit through the modulator's feedback.
      p.push_back({"Pressure Lead", "Essentials",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), {a(fm21, 0.1), a(fb2, 0.1), a(lfoParam(2, kLfoRate), lfoValue(5.5))},
                         mono(0, 40), env(0, 0, 10, 300, 1.0, 250), open(0),
                         routes(0, {{press, fm21, 0.55}, {slide, fb2, 0.4}, {lfo1, kPartFine, 0.0}, {press, depth(2), 0.06},
                                    {wheel, depth(2), 0.04}})})});

      // Keys.
      // A DX-style electric piano: a 1:1 pair for the body and a ×14 modulator for the tine's bark,
      // both harder the harder you play. Higher notes die away sooner.
      const Values tines = join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 14.0, 0.0),
                                 {a(fm21, 0.15)}, env(0, 0, 1, 5000, 0.0, 400), env(0, 1, 0.5, 1500, 0.2, 400),
                                 env(0, 2, 0.5, 60, 0.0, 40), open(0)});
      p.push_back({"Tine Piano", "Essentials",
                   join({tines, routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2},
                                           {key, envParam(0, kEnvDecay), -0.08}})})});
      // A part param of either part, for the patches built for A or B alike.
      const auto pv = [](int part, clap_id offset, double v) { return std::pair<clap_id, double>{partId(part, offset), v}; };
      // A struck string: two saws beating a few cents apart and a quiet octave, through a low pass the
      // hammer throws open (wider the harder you play) and that closes as the note rings, so the tone
      // darkens long before it dies. A thump of noise for the hammer. Its routes start Env 2 on the
      // cutoff, Env 3 on the noise, and Key on the decay (higher notes die sooner) and the pan (low
      // notes left, high right, as a player hears it).
      const auto piano = [&](int part) {
         return join({osc(part, 0, kSaw, 0, -3, 1.0), osc(part, 1, kSaw, 0, 3, 0.8), osc(part, 2, kTriangle, 12, 0, 0.25),
                      filter(part, 0, kLowPass, 300, 0.0, 0.9),
                      {pv(part, filterParam(0, kFilterSlope), 0), pv(part, envParam(1, kEnvVelocity), 0.8), pv(part, envParam(2, kEnvVelocity), 0.7),
                       pv(part, mixParam(kNoiseColor), 0.2), pv(part, kVelocityToAmp, 0.85), pv(part, kPolyphony, 16)},
                      env(part, 0, 1, 6500, 0.0, 450), env(part, 1, 0.5, 1300, 0.12, 450), env(part, 2, 0.5, 35, 0.0, 30)});
      };
      // Layer: the struck strings, and under them a pure FM body that rings out the fundamental and
      // a faintly stretched second partial, as a big soundboard does. The mod wheel opens the lid.
      p.push_back({"Concert Grand", "Essentials",
                   join({{g(kPlayMode, 1.0)}, piano(0),
                         routes(0, {{env2, cutoff, 0.42}, {env3, mixParam(kNoiseLevel), 0.22}, {key, envParam(0, kEnvDecay), -0.1},
                                    {key, kPartPan, 0.25}, {velocity, cutoff, 0.08}, {wheel, cutoff, 0.12}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.003, 0.25), op(1, 2, kSine, 1.0, 0.0),
                         {b(kPartTranspose, 0), b(flockParam(kBehaviour), 0), b(fm31, 0.05), b(kPartLevel, -9), b(kPolyphony, 16)},
                         env(1, 0, 2, 4500, 0.0, 450), env(1, 2, 0.5, 250, 0.0, 100), open(1),
                         routes(1, {{env3, fm31, 0.25}, {velocity, fm31, 0.15}, {key, envParam(0, kEnvDecay), -0.1}, {key, kPartPan, 0.25}})})});
      // The reed piano with Press on the drive: lean in and it snarls. The wheel adds tremolo.
      p.push_back({"Wurli Drive", "Essentials",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kHalfSine, 1.0, 0.0), {a(fm21, 0.12), a(tailParam(kDriveAmount), 0.3)},
                         filter(0, 0, kLowPass, 3500, 0.0, 0.5), lfo(0, 2, kLfoSine, 5.5), env(0, 0, 1, 2200, 0.15, 250),
                         env(0, 2, 0.5, 350, 0.1, 200),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.35}, {press, tailParam(kDriveAmount), 0.5}, {press, fm21, 0.15},
                                    {lfo1, kPartLevel, 0.0}, {wheel, depth(4), 0.07}})})});
      p.push_back({"Clav", "Essentials",
                   join({{a(kPartLevel, -5)}, {a(oscParam(0, kOscWave), kPulse), a(oscParam(0, kOscShape), 0.25), a(cutoff, 0.55), a(resonance, 0.35)},
                         env(0, 0, 0.5, 1200, 0.3, 60), env(0, 1, 0.5, 180, 0.0, 100),
                         routes(0, {{env2, cutoff, 0.35}, {velocity, cutoff, 0.25}, {wheel, resonance, 0.3}})})});
      // Drawbars 16', 8' and 5 1/3', a key click, and a rotary speaker the mod wheel speeds up.
      p.push_back({"Tonewheel Organ", "Essentials",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 0.5, 0.8), op(0, 2, kSine, 1.5, 0.7),
                         {a(kVelocityToAmp, 0.0), a(mixParam(kNoiseColor), 0.5), a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.25),
                          a(kPartLevel, -6), a(lfoParam(2, kLfoTrigger), 1)},
                         lfo(0, 2, kLfoSine, 0.8), env(0, 0, 3, 100, 1.0, 40), env(0, 2, 0.5, 25, 0.0, 10), open(0),
                         routes(0, {{env3, mixParam(kNoiseLevel), 0.2}, {lfo1, kPartPan, 0.15}, {lfo1, kPartFine, 0.03},
                                    {wheel, lfoParam(2, kLfoRate), 0.27}})})});

      // Mallets and Bells.
      p.push_back({"Vibraphone", "Essentials",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.0), {a(fm21, 0.15)}, lfo(0, 2, kLfoSine, 5.5),
                         env(0, 0, 0.5, 3000, 0.0, 1200), env(0, 2, 0.5, 300, 0.0, 200), open(0),
                         routes(0, {{env3, fm21, 0.2}, {velocity, fm21, 0.15}, {lfo1, kPartLevel, 0.04}, {wheel, depth(2), 0.05}})})});
      p.push_back({"Tubular Bells", "Essentials",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 2.01, 0.25), {a(fm21, 0.3)},
                         env(0, 0, 0.5, 6000, 0.0, 3000), env(0, 2, 0.5, 1500, 0.0, 1500), open(0),
                         routes(0, {{env3, fm21, 0.25}, {velocity, fm21, 0.2}})})});

      // Plucks.
      p.push_back({"Synth Pluck", "Essentials",
                   join({{a(oscParam(1, kOscWave), kPulse), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), 7),
                          a(mixParam(kMixLevel, 1), 0.6), a(cutoff, 0.3), a(resonance, 0.45)},
                         env(0, 0, 1, 800, 0.1, 300), env(0, 1, 0.5, 260, 0.0, 200),
                         routes(0, {{env2, cutoff, 0.5}, {velocity, cutoff, 0.2}, {slide, resonance, 0.3}, {wheel, envParam(1, kEnvDecay), 0.15}})})});
      p.push_back({"Nylon Guitar", "Essentials",
                   join({{a(cutoff, 0.3), a(resonance, 0.05), a(filterParam(0, kFilterKeyTrack), 0.6)},
                         env(0, 0, 1, 1800, 0.0, 400), env(0, 1, 0.5, 350, 0.0, 300), routes(0, {{env2, cutoff, 0.4}, {velocity, cutoff, 0.1}})})});
      p.push_back({"Kalimba", "Essentials",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 5.95, 0.06), {a(fm21, 0.1)},
                         env(0, 0, 0.5, 1100, 0.0, 600), env(0, 2, 0.5, 60, 0.0, 50), open(0),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.1}})})});

      // Brass.
      // The filter opens a little after the note speaks, harder when struck harder, and the pitch
      // scoops up into the note.
      p.push_back({"Synth Brass", "Essentials",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 0.9), filter(0, 0, kLowPass, 450, 0.15, 0.4),
                         lfo(0, 2, kLfoSine, 5.0), {a(lfoParam(2, kLfoDelay), timeValue(300)), a(envParam(1, kEnvVelocity), 0.5)},
                         env(0, 0, 35, 500, 0.85, 250), env(0, 1, 70, 600, 0.45, 300), env(0, 2, 0.5, 70, 0.0, 50),
                         routes(0, {{env2, cutoff, 0.38}, {env3, kPartFine, -0.12}, {press, cutoff, 0.2}, {lfo1, kPartFine, 0.0},
                                    {wheel, depth(3), 0.04}})})});

      // Strings.
      p.push_back({"String Machine", "Essentials",
                   join({{a(kPartLevel, -4)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), -8),
                          a(mixParam(kMixLevel, 1), 0.9), a(oscParam(2, kOscWave), kSaw), a(oscParam(2, kOscPitchMode), 0),
                          a(oscParam(2, kOscCoarse), 12), a(mixParam(kMixLevel, 2), 0.4), a(mixParam(kMixPan, 0), -0.4),
                          a(mixParam(kMixPan, 1), 0.4), a(cutoff, 0.55), a(resonance, 0.1),
                          a(lfoParam(2, kLfoRate), lfoValue(6.0)), a(lfoParam(3, kLfoRate), lfoValue(0.4))},
                         env(0, 0, 300, 1000, 1.0, 1200),
                         routes(0, {{lfo1, oscParam(1, kOscFine), 0.04}, {lfo2, oscParam(2, kOscFine), 0.05}, {press, cutoff, 0.25},
                                    {wheel, cutoff, 0.2}})})});
      p.push_back({"Solo Cello", "Essentials",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 0, 0.5), {a(oscParam(1, kOscShape), 0.35)},
                         filter(0, 0, kLowPass, 1400, 0.2, 0.5), filter(0, 1, kHighPass, 90, 0.0, 0.0), lfo(0, 2, kLfoSine, 5.2),
                         {a(lfoParam(2, kLfoDelay), timeValue(350))}, mono(0, 80), env(0, 0, 110, 300, 1.0, 300),
                         routes(0, {{lfo1, kPartFine, 0.03}, {press, cutoff, 0.25}, {press, depth(0), 0.03}, {wheel, depth(0), 0.04}})})});

      // Pads.
      p.push_back({"Warm Analog", "Essentials",
                   join({{a(kPartLevel, -4)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), 9),
                          a(mixParam(kMixLevel, 1), 0.9), a(mixParam(kMixPan, 0), -0.3), a(mixParam(kMixPan, 1), 0.3),
                          a(cutoff, 0.42), a(resonance, 0.15)},
                         env(0, 0, 500, 1000, 1.0, 2000), env(0, 1, 800, 2000, 0.5, 2000),
                         routes(0, {{env2, cutoff, 0.15}, {wheel, cutoff, 0.25}, {press, cutoff, 0.15}})})});
      p.push_back({"Glass Pad", "Essentials",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 0.5, 0.4),
                         {a(fm21, 0.2), a(lfoParam(2, kLfoRate), lfoValue(0.3))}, env(0, 0, 600, 1000, 1.0, 2500), open(0),
                         routes(0, {{lfo1, fm21, 0.15}, {wheel, fm21, 0.25}})})});

      // Voices.
      // Two band passes in parallel at a vowel's first two formants (ah is 750 and 1150 Hz, ee 300
      // and 2300, oo 330 and 700), fixed in pitch as a mouth is, whatever note sings through them.
      const auto vox = [&](int part, double f1, double f2, double res) {
         return join({{pv(part, tailParam(kFilterRouting), 1), pv(part, filterParam(0, kFilterSlope), 0), pv(part, filterParam(1, kFilterSlope), 0)},
                      filter(part, 0, kBandPass, f1, res, 0.0), filter(part, 1, kBandPass, f2, res, 0.0)});
      };
      // A talk box lead: Press slides the mouth from "ah" to "ee", the wheel brings in vibrato.
      p.push_back({"Talk Box", "Essentials",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 5, 0.6), {a(oscParam(1, kOscShape), 0.3), a(mixParam(kNoiseLevel), 0.03)},
                         vox(0, 750, 1150, 0.3), {a(tailParam(kDriveAmount), 0.2)}, lfo(0, 2, kLfoSine, 5.5),
                         {a(lfoParam(2, kLfoDelay), timeValue(400))}, mono(0, 60), env(0, 0, 8, 300, 1.0, 150),
                         routes(0, {{press, cutoff, -0.13}, {press, cutoff2, 0.1}, {lfo1, kPartFine, 0.0}, {wheel, depth(2), 0.04},
                                    {press, depth(2), 0.02}, {velocity, cutoff2, 0.04}})})});

      // ---- Expressive: patches played with Press (channel or poly aftertouch, or MPE pressure) ----
      // Each does something with Press beyond opening a filter: it bows, blows, bends, gates, bells
      // into a pad or speeds up a rotor. Played without pressure, each is still a usable sound.

      // Press is the bow: it swells the note, brightens it and lets the vibrato in.
      p.push_back({"Solo Violin", "Expressive",
                   join({osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kSaw, 0, 4, 0.7), {a(mixParam(kNoiseLevel), 0.03), a(kPartLevel, -4)},
                         filter(0, 0, kLowPass, 2000, 0.15, 0.5), filter(0, 1, kHighPass, 180, 0.0, 0.0), lfo(0, 2, kLfoSine, 5.6),
                         {a(lfoParam(2, kLfoDelay), timeValue(300))}, mono(0, 40), env(0, 0, 120, 1000, 1.0, 250),
                         routes(0, {{press, kPartLevel, 0.14}, {press, cutoff, 0.2}, {lfo1, kPartFine, 0.012}, {press, depth(2), 0.03},
                                    {wheel, depth(2), 0.03}, {velocity, cutoff, 0.08}})})});
      // Press is the breath: louder, brighter and, at the top, growling through the drive.
      p.push_back({"Breath Sax", "Expressive",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 3, 0.5), {a(oscParam(1, kOscShape), 0.35), a(mixParam(kNoiseLevel), 0.04)},
                         filter(0, 0, kLadder, 700, 0.2, 0.5), {a(tailParam(kDriveAmount), 0.1), a(kPartLevel, -3)}, lfo(0, 2, kLfoSine, 5.0),
                         {a(lfoParam(2, kLfoDelay), timeValue(400))}, mono(0, 30), env(0, 0, 30, 1000, 1.0, 150),
                         routes(0, {{press, kPartLevel, 0.12}, {press, cutoff, 0.3}, {press, tailParam(kDriveAmount), 0.35},
                                    {lfo1, kPartFine, 0.0}, {wheel, depth(3), 0.04}, {velocity, cutoff, 0.1}})})});
      // Press is the bellows. Two reeds a little apart for the musette; the wheel pulls them further.
      p.push_back({"Accordion", "Expressive",
                   join({osc(0, 0, kPulse, 0, 0, 1.0), osc(0, 1, kPulse, 0, 13, 0.8), osc(0, 2, kSquare, -12, 0, 0.4),
                         {a(oscParam(0, kOscShape), 0.3), a(oscParam(1, kOscShape), 0.4), a(kVelocityToAmp, 0.2), a(kPartLevel, -5)},
                         filter(0, 0, kLowPass, 2500, 0.1, 0.4), env(0, 0, 40, 1000, 1.0, 120),
                         routes(0, {{press, kPartLevel, 0.12}, {press, cutoff, 0.15}, {wheel, oscParam(1, kOscFine), 0.05}})})});
      // Press bends the note up a semitone, as a guitarist bends a string; drive and a delayed vibrato.
      p.push_back({"Bending Lead", "Expressive",
                   join({osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kSquare, 0, 4, 0.6), filter(0, 0, kLadder, 1800, 0.3, 0.5),
                         {a(tailParam(kDriveAmount), 0.45), a(kPartLevel, -4)}, lfo(0, 2, kLfoSine, 5.5),
                         {a(lfoParam(2, kLfoDelay), timeValue(500))}, mono(0, 30), env(0, 0, 2, 300, 1.0, 200),
                         routes(0, {{press, kPartFine, 0.5}, {press, cutoff, 0.1}, {lfo1, kPartFine, 0.0}, {wheel, depth(2), 0.04},
                                    {velocity, cutoff, 0.1}})})});
      // A bass that growls as you lean on it: Press drives the FM feedback, the drive and the filter.
      p.push_back({"Pressure Growl", "Expressive",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 0.5, 0.5), {a(fm21, 0.2), a(fb2, 0.15)},
                         filter(0, 0, kLadder, 900, 0.3, 0.3), {a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.2)},
                         mono(0, 20), env(0, 0, 2, 400, 1.0, 80),
                         routes(0, {{press, fb2, 0.5}, {press, tailParam(kDriveAmount), 0.4}, {press, resonance, 0.3},
                                    {velocity, fm21, 0.2}, {wheel, fm21, 0.3}})})});
      // A bell that blooms: strike it and it rings away; press and a pad grows out of it.
      p.push_back({"Bloom Bells", "Expressive",
                   join({op(0, 0, kSine, 1.0, 0.0), osc(0, 1, kSaw, 0, 7, 0.0), op(0, 2, kSine, 3.5, 0.0), {a(fm31, 0.2)},
                         filter(0, 0, kLowPass, 2200, 0.1, 0.4), {a(kSkyPan, 0.5)}, env(0, 0, 1, 1000, 1.0, 1800),
                         env(0, 2, 0.5, 2800, 0.0, 1200),
                         routes(0, {{env3, mixParam(kMixLevel, 0), 1.0}, {env3, fm31, 0.2}, {velocity, fm31, 0.1},
                                    {press, mixParam(kMixLevel, 1), 0.6}, {press, cutoff, -0.1}, {wheel, cutoff, 0.2}})})});
      // A struck wire: noise through a comb tuned to the note. Press tightens it, so the ring sustains
      // and sharpens toward a bowed metal tone.
      p.push_back({"Tension Wire", "Expressive",
                   join({osc(0, 0, kSaw, 0, 0, 0.15), {a(mixParam(kNoiseLevel), 0.5), a(mixParam(kNoiseColor), 0.4), a(kDrift, 0.1)},
                         filter(0, 0, kComb, 261.6, 0.75, 1.0), filter(0, 1, kLadder, 3000, 0.1, 0.3), env(0, 0, 5, 1500, 0.4, 800),
                         routes(0, {{press, resonance, 0.2}, {press, cutoff2, 0.15}, {wheel, mixParam(kNoiseColor), 0.5},
                                    {velocity, cutoff2, 0.1}})})});
      // The tonewheel organ with Press on the rotor: lean in and it spins up to fast, and the drive
      // bites. The wheel does the same for a plain keyboard.
      p.push_back({"Squeeze Organ", "Expressive",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 0.5, 0.8), op(0, 2, kSine, 2.0, 0.6),
                         {a(kVelocityToAmp, 0.0), a(mixParam(kNoiseColor), 0.5), a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.2),
                          a(kPartLevel, -6), a(lfoParam(2, kLfoTrigger), 1)},
                         lfo(0, 2, kLfoSine, 0.8), env(0, 0, 3, 100, 1.0, 40), env(0, 2, 0.5, 25, 0.0, 10), open(0),
                         routes(0, {{env3, mixParam(kNoiseLevel), 0.2}, {lfo1, kPartPan, 0.15}, {lfo1, kPartFine, 0.03},
                                    {press, lfoParam(2, kLfoRate), 0.27}, {wheel, lfoParam(2, kLfoRate), 0.27},
                                    {press, tailParam(kDriveAmount), 0.35}})})});
      // A pad that wilts under pressure: it sags a fifth of a semitone flat and darkens as you press.
      p.push_back({"Wilting Pad", "Expressive",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 1.0), {a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4)},
                         filter(0, 0, kLowPass, 1500, 0.25, 0.3), lfo(0, 3, kLfoSmooth, 0.2), env(0, 0, 500, 1000, 1.0, 2000),
                         {a(kPartLevel, -3)},
                         routes(0, {{press, kPartFine, -0.1}, {press, cutoff, -0.12}, {press, resonance, 0.2}, {lfo2, cutoff, 0.05},
                                    {wheel, cutoff, 0.2}})})});
      // A pad that flutters: Press is the tremolo, deeper and faster the harder you press.
      p.push_back({"Flutter Pad", "Expressive",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kTriangle, 12, 6, 0.6), filter(0, 0, kLowPass, 1800, 0.15, 0.3),
                         lfo(0, 2, kLfoSine, 1.0), env(0, 0, 400, 1000, 1.0, 1800),
                         routes(0, {{lfo1, kPartLevel, 0.0}, {press, depth(0), 0.12}, {press, lfoParam(2, kLfoRate), 0.3},
                                    {lfo1, kPartPan, 0.0}, {press, depth(3), 0.4}, {wheel, cutoff, 0.2}})})});
      // FM whose modulator slides off its ratio under Press, from a pure tone to a clanging bell.
      p.push_back({"Pressure Metal", "Expressive",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fm21, 0.25), a(kSkyPan, 0.4)},
                         env(0, 0, 5, 2000, 0.6, 800), open(0),
                         routes(0, {{press, oscParam(1, kOscRatio), 0.15}, {press, fm21, 0.2}, {wheel, fb2, 0.3}, {velocity, fm21, 0.1}})})});
      // A low pass and a resonant high pass side by side: Press crossfades between them, Slide moves
      // the pulse width, the wheel the resonance.
      p.push_back({"Morph Pad", "Expressive",
                   join({osc(0, 0, kPulse, 0, -5, 1.0), osc(0, 1, kSaw, 0, 5, 0.8), {a(tailParam(kFilterRouting), 1), a(tailParam(kFilterBalance), -0.6)},
                         filter(0, 0, kLowPass, 700, 0.2, 0.4), filter(0, 1, kHighPass, 1400, 0.45, 0.4),
                         env(0, 0, 400, 1000, 1.0, 1800),
                         routes(0, {{press, tailParam(kFilterBalance), 0.6}, {slide, oscParam(0, kOscShape), 0.4},
                                    {wheel, filterParam(1, kFilterResonance), 0.3}, {wheel, resonance, 0.3}})})});
      // A pad that only stutters when you lean on it: Press deepens a sixteenth-note gate at the host's tempo.
      p.push_back({"Press Gate Pad", "Expressive",
                   join({osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0), filter(0, 0, kLowPass, 1600, 0.2, 0.3),
                         {a(lfoParam(2, kLfoPolarity), 1)}, lfoSync(0, 2, kLfoSquare, k16th), env(0, 0, 300, 1000, 1.0, 1200),
                         routes(0, {{lfo1, kPartLevel, 0.0}, {press, depth(0), -0.3}, {press, cutoff, 0.15}, {wheel, cutoff, 0.2}})})});
      // Brass that swells with Press: louder, brighter and a little sharper, as a player pushes air.
      p.push_back({"Swell Brass", "Expressive",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 0.9), filter(0, 0, kLowPass, 380, 0.15, 0.4),
                         lfo(0, 2, kLfoSine, 5.0), {a(lfoParam(2, kLfoDelay), timeValue(300)), a(envParam(1, kEnvVelocity), 0.5), a(kPartLevel, -3)},
                         env(0, 0, 60, 500, 0.9, 300), env(0, 1, 90, 600, 0.4, 300), env(0, 2, 0.5, 70, 0.0, 50),
                         routes(0, {{env2, cutoff, 0.3}, {env3, kPartFine, -0.12}, {press, cutoff, 0.3}, {press, kPartLevel, 0.1},
                                    {press, kPartFine, 0.02}, {lfo1, kPartFine, 0.0}, {wheel, depth(5), 0.04}})})});

      // ---- Monsta: variations on MicroMonsta 2 factory programs ----
      // Each takes the idea of one of the MicroMonsta 2's own programs (its name in brackets) and
      // plays it on JSynth's parts. Its aftertouch routes become Press, and its mod knob the wheel.
      // Where the program lets one source deepen another's modulation (aftertouch deepening an LFO's
      // wobble, say), that is a Multiply bus here, named for what it does.

      // [Foggy EP] Soft keys behind a velocity-hungry filter, a long foggy release, Press warbling one
      // oscillator sharp, and the wheel giving each note a different shape.
      p.push_back({"Foggy EP", "Monsta",
                   join({osc(0, 0, kPulse, 0, -4, 0.7), osc(0, 1, kTriangle, 0, 4, 0.9), osc(0, 2, kSine, 12, 0, 0.2),
                         {a(oscParam(0, kOscShape), 0.3), a(envParam(1, kEnvVelocity), 0.8), a(tailParam(kDriveAmount), 0.25), a(kSkyPan, 0.5)},
                         filter(0, 0, kLowPass, 550, 0.15, 0.66), env(0, 0, 3, 2500, 0.5, 1400), env(0, 1, 3, 900, 0.25, 1200),
                         env(0, 2, 0.5, 90, 0.0, 50),
                         busSet(0, 0, kMul),
                         routes(0, {{env2, cutoff, 0.35}, {env3, kPartFine, -0.03}, {press, oscParam(1, kOscFine), 0.05},
                                    {kSourceRandom, busIn(0), 1.0}, {wheel, busIn(0), 1.0}, {bus1, oscParam(0, kOscShape), 0.25}})}),
                   names({{0, 0, "Shape per Note"}})});
      // [Genetic] A plucky lead whose filter snaps shut; Press brings in a pulse two octaves up.
      p.push_back({"Genetic Lead", "Monsta",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 24, 0, 0.0), osc(0, 2, kSquare, -12, 0, 0.3),
                         {a(oscParam(1, kOscShape), 0.2), a(kPartLevel, -2)}, filter(0, 0, kLadder, 260, 0.35, 0.33), mono(0, 60),
                         env(0, 0, 1, 1000, 1.0, 200), env(0, 1, 0.5, 400, 0.5, 300), lfo(0, 3, kLfoTriangle, 0.3),
                         routes(0, {{env2, cutoff, 0.45}, {press, mixParam(kMixLevel, 1), 0.7}, {lfo2, oscParam(1, kOscShape), 0.2},
                                    {wheel, cutoff, 0.2}, {velocity, cutoff, 0.1}})})});
      // [MstaVoic] Voices that waver apart under Press, one sharp and one flat, while the wheel lets
      // the vowel drift.
      p.push_back({"Monsta Voices", "Monsta",
                   join({osc(0, 0, kSaw, 0, -3, 1.0), osc(0, 1, kPulse, 0, 3, 0.8), {a(mixParam(kNoiseLevel), 0.03), a(kPartLevel, 3)},
                         vox(0, 650, 1100, 0.5), lfo(0, 2, kLfoSine, 6.5), lfo(0, 3, kLfoSmooth, 0.25), env(0, 0, 600, 1000, 1.0, 2500),
                         busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 40), busSet(0, 1, kMul),
                         routes(0, {{lfo1, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, oscParam(0, kOscFine), 0.05},
                                    {bus1, oscParam(1, kOscFine), -0.08}, {lfo2, busIn(1), 1.0}, {wheel, busIn(1), 1.0}, {bus2, cutoff2, 0.1},
                                    {press, cutoff, 0.05}})}),
                   names({{0, 0, "Waver"}, {0, 1, "Vowel Drift"}})});
      // [Die-50] A resonant filter that sweeps open and sinks over seconds, pulse widths shimmering
      // fast and slowing as it sinks. Press wobbles the filter and lets the pitch sag.
      p.push_back({"Fifty Pad", "Monsta",
                   join({osc(0, 0, kPulse, 0, -5, 1.0), osc(0, 1, kPulse, 12, 5, 0.35), {a(envParam(1, kEnvVelocity), 0.3)},
                         filter(0, 0, kLadder, 280, 0.6, 0.3), lfo(0, 2, kLfoTriangle, 7.0), lfo(0, 3, kLfoSine, 5.0),
                         env(0, 0, 400, 1000, 1.0, 2500), env(0, 1, 1, 6000, 0.2, 4000), busSet(0, 0, kMul),
                         routes(0, {{env2, cutoff, 0.45}, {lfo1, oscParam(0, kOscShape), 0.3}, {lfo1, oscParam(1, kOscShape), 0.3},
                                    {env2, lfoParam(2, kLfoRate), -0.15}, {lfo2, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, cutoff, 0.1},
                                    {press, kPartFine, -0.03}, {wheel, lfoParam(3, kLfoRate), 0.2}})}),
                   names({{0, 0, "Pressed Wobble"}})});
      // [Kingdome] A driven three-oscillator pad. Press raises the second oscillator up to a fifth and
      // sets the filter wobbling.
      p.push_back({"Kingdome", "Monsta",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 8, 0.9), osc(0, 2, kSaw, -12, 0, 0.8),
                         {a(oscParam(1, kOscShape), 0.4), a(tailParam(kDriveAmount), 0.5), a(envParam(1, kEnvVelocity), 0.8), a(kPartLevel, -4)},
                         filter(0, 0, kLowPass, 650, 0.35, 0.0), lfo(0, 3, kLfoSine, 4.0), env(0, 0, 250, 1000, 0.75, 2000),
                         env(0, 1, 300, 1500, 0.0, 1500),
                         busSet(0, 0, kMul, kS),
                         routes(0, {{env2, cutoff, 0.35}, {press, oscParam(1, kOscCoarse), 0.073}, {lfo2, busIn(0), 1.0}, {press, busIn(0), 1.0},
                                    {bus1, cutoff, 0.15}, {wheel, cutoff, -0.15}})}),
                   names({{0, 0, "Pressed Wobble"}})});
      // [Therapy] Strings in reverse: Press hushes them, and the harder you press the more the shape
      // shimmers. The wheel darkens them.
      p.push_back({"Therapy Strings", "Monsta",
                   join({osc(0, 0, kPulse, 0, -14, 1.0), osc(0, 1, kPulse, 0, 14, 1.0), osc(0, 2, kPulse, 24, 0, 0.4),
                         {a(mixParam(kMixPan, 0), -0.8), a(mixParam(kMixPan, 1), 0.8), a(kDrift, 0.3)},
                         filter(0, 0, kLowPass, 5000, 0.2, 0.35), lfo(0, 2, kLfoSine, 4.0), env(0, 0, 300, 1000, 1.0, 1500),
                         busSet(0, 0, kMul),
                         routes(0, {{press, kPartLevel, -0.3}, {lfo1, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, oscParam(2, kOscShape), 0.4},
                                    {wheel, cutoff, -0.3}, {wheel, oscParam(0, kOscShape), -0.3}, {wheel, oscParam(1, kOscShape), -0.3}})}),
                   names({{0, 0, "Shimmer"}})});
      // [Nitemare] A low, slowly blooming drone; a third oscillator dives in at the start, and Press
      // brings in its pulsing shriek, only when pressed hard.
      p.push_back({"Nightmare", "Monsta",
                   join({osc(0, 0, kSaw, -12, 0, 1.0), osc(0, 1, kSquare, 24, -13, 0.35), osc(0, 2, kSaw, 12, 0, 0.0), {a(fm21, 0.1)},
                         {a(kDrift, 0.3), a(tailParam(kDriveAmount), 0.15), a(kSkyPan, 0.5)}, filter(0, 0, kLowPass, 2200, 0.05, 0.0),
                         lfo(0, 2, kLfoSine, 0.05), lfo(0, 3, kLfoSquare, 6.0), env(0, 0, 1500, 1000, 1.0, 5000),
                         env(0, 2, 0.5, 2500, 0.3, 5000),
                         busSet(0, 0, kSum, kExpo), busSet(0, 1, kMul),
                         routes(0, {{lfo1, oscParam(0, kOscShape), 0.3}, {env3, oscParam(2, kOscCoarse), 0.3}, {press, busIn(0), 1.0},
                                    {lfo2, busIn(1), 1.0}, {bus1, busIn(1), 1.0}, {bus2, mixParam(kMixLevel, 2), 0.4}, {wheel, resonance, 0.4},
                                    {lfo1, cutoff, 0.1}})}),
                   names({{0, 0, "Hard Press"}, {0, 1, "Shriek"}})});
      // [MG Quak] A squelchy, driven mono bass with a long glide; Press sets the filter quacking.
      p.push_back({"Quack Bass", "Monsta",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSaw, -12, 10, 0.5), filter(0, 0, kLadder, 200, 0.7, 0.55),
                         {a(tailParam(kDriveAmount), 0.5), a(kPartLevel, -2)}, lfo(0, 2, kLfoSine, 6.0), mono(0, 110),
                         env(0, 0, 1, 300, 0.7, 60), env(0, 1, 1, 450, 0.0, 100),
                         busSet(0, 0, kMul),
                         routes(0, {{env2, cutoff, 0.5}, {lfo1, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, cutoff, 0.2}, {wheel, resonance, 0.2},
                                    {velocity, cutoff, 0.1}})}),
                   names({{0, 0, "Quack"}})});
      // [Gristle] A thick, wide lead of pulse waves whose widths swing on an LFO; Press makes the
      // resonance flutter, the wheel slows the swing and darkens it.
      p.push_back({"Gristle Lead", "Monsta",
                   join({osc(0, 0, kPulse, 0, -20, 1.0), osc(0, 1, kPulse, -12, 20, 1.0), {a(mixParam(kMixPan, 0), -0.7), a(mixParam(kMixPan, 1), 0.7)},
                         filter(0, 0, kLadder, 2500, 0.25, 0.4), {a(tailParam(kDriveAmount), 0.35), a(kPartLevel, -3)},
                         lfo(0, 2, kLfoSine, 2.5), lfo(0, 3, kLfoSine, 7.0), mono(0, 80), env(0, 0, 7, 1000, 0.8, 120),
                         busSet(0, 0, kMul),
                         routes(0, {{lfo1, oscParam(0, kOscShape), 0.4}, {lfo1, oscParam(1, kOscShape), 0.4}, {lfo2, busIn(0), 1.0},
                                    {press, busIn(0), 1.0}, {bus1, resonance, 0.35}, {wheel, lfoParam(2, kLfoRate), -0.15}, {wheel, cutoff, -0.2}})}),
                   names({{0, 0, "Flutter"}})});
      // [Cumulous] A wide pad under a resonant, fully tracking filter. Press speeds up its drift and
      // narrows the pulse; the wheel sweeps it across the stereo field.
      p.push_back({"Cumulus", "Monsta",
                   join({osc(0, 0, kPulse, 0, -15, 1.0), osc(0, 1, kPulse, 7, 15, 0.8), {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5)},
                         filter(0, 0, kLowPass, 1800, 0.55, 1.0), lfo(0, 2, kLfoTriangle, 0.25), lfo(0, 3, kLfoSmooth, 0.4),
                         env(0, 0, 1200, 1000, 0.95, 3000), busSet(0, 0, kMul),
                         routes(0, {{lfo1, oscParam(1, kOscShape), 0.15}, {press, lfoParam(2, kLfoRate), 0.25}, {press, oscParam(0, kOscShape), -0.25},
                                    {lfo2, cutoff, 0.08}, {lfo1, busIn(0), 1.0}, {wheel, busIn(0), 1.0}, {bus1, kPartPan, 0.6}})}),
                   names({{0, 0, "Stereo Sweep"}})});
      // [OldHills] Plucky keys over a held body: each note draws its own pulse width, a slow LFO turns
      // it, and the pitch leans up as the note settles. The wheel adds resonance and width.
      p.push_back({"Old Hills", "Monsta",
                   join({osc(0, 0, kSaw, 0, -4, 0.25), osc(0, 1, kPulse, 0, 4, 1.0), {a(oscParam(1, kOscShape), 0.35)},
                         filter(0, 0, kLowPass, 650, 0.2, 1.0), lfo(0, 3, kLfoTriangle, 0.3), env(0, 0, 2, 1000, 1.0, 400),
                         env(0, 1, 2, 700, 0.3, 400), env(0, 2, 500, 900, 0.3, 400),
                         routes(0, {{env2, cutoff, 0.35}, {kSourceRandom, oscParam(1, kOscShape), 0.25}, {lfo2, oscParam(1, kOscShape), 0.15},
                                    {env3, kPartFine, 0.02}, {press, cutoff, 0.15}, {wheel, resonance, 0.3}, {wheel, oscParam(1, kOscShape), 0.2}})})});
      // [Sunshine] A bright string pad under a resonant, fully tracking filter, its pulse width turning
      // slowly and drawn anew for each note. The wheel adds a fifth above.
      p.push_back({"Sunshine Strings", "Monsta",
                   join({osc(0, 0, kSaw, 0, -6, 0.6), osc(0, 1, kPulse, 0, 6, 1.0), osc(0, 2, kSaw, 7, -10, 0.0),
                         {a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4)}, filter(0, 0, kLowPass, 3500, 0.45, 1.0),
                         lfo(0, 3, kLfoSine, 0.15), env(0, 0, 300, 1000, 1.0, 1800), env(0, 2, 1500, 3000, 0.5, 1800),
                         routes(0, {{lfo2, oscParam(1, kOscShape), 0.35}, {kSourceRandom, oscParam(1, kOscShape), 0.25}, {env3, kPartFine, 0.02},
                                    {press, cutoff, 0.1}, {wheel, mixParam(kMixLevel, 2), 0.6}})})});
      // [WornArp] Short plucks from a worn machine: each note chirps up as it starts, lands a little
      // off pitch and somewhere else in the stereo field.
      p.push_back({"Worn Arp", "Monsta",
                   join({osc(0, 0, kSaw, 0, 5, 1.0), osc(0, 1, kPulse, 0, -12, 0.3), {a(kDrift, 0.3), a(tailParam(kDriveAmount), 0.15)},
                         filter(0, 0, kLowPass, 1300, 0.5, 1.0), lfo(0, 3, kLfoSine, 0.1), env(0, 0, 1, 500, 0.6, 250),
                         env(0, 1, 0.5, 150, 0.0, 120), env(0, 2, 0.5, 60, 0.0, 50),
                         routes(0, {{env2, cutoff, 0.4}, {env3, oscParam(1, kOscCoarse), 0.12}, {kSourceRandom, kPartPan, 0.5},
                                    {kSourceRandom, kPartFine, 0.02}, {lfo2, oscParam(1, kOscShape), 0.4}, {press, cutoff, 0.15},
                                    {wheel, envParam(0, kEnvSustain), -0.4}})})});
      // [Waxxy] A driven, resonant pad-pluck whose filter lands somewhere new on each note; higher notes
      // are narrower and less breathy.
      p.push_back({"Waxxy", "Monsta",
                   join({osc(0, 0, kSaw, 0, -4, 0.9), osc(0, 1, kPulse, 0, 4, 0.9), {a(mixParam(kNoiseLevel), 0.3), a(tailParam(kDriveAmount), 0.3)},
                         filter(0, 0, kLadder, 500, 0.55, 0.0), env(0, 0, 1, 1000, 1.0, 300),
                         env(0, 1, 1, 220, 0.3, 250),
                         routes(0, {{env2, cutoff, 0.45}, {kSourceRandom, cutoff, 0.2}, {key, oscParam(1, kOscShape), 0.4},
                                    {key, mixParam(kNoiseLevel), -0.3}, {press, resonance, 0.2}, {wheel, cutoff, 0.2}})})});
      // [Reptile] A slow, crawling drone: three oscillators whose widths and tuning wander on slow LFOs,
      // one drawn anew each note, and a pan that swings out as it arrives. Press drives it.
      p.push_back({"Reptile", "Monsta",
                   join({osc(0, 0, kSaw, 0, 10, 1.0), osc(0, 1, kPulse, 0, -30, 0.7), osc(0, 2, kPulse, 0, 30, 0.6),
                         {a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.25), a(kPartLevel, -4)}, filter(0, 0, kLowPass, 3500, 0.1, 0.3),
                         lfo(0, 2, kLfoSmooth, 0.08), lfo(0, 3, kLfoSine, 0.12), env(0, 0, 800, 1500, 0.9, 2500), env(0, 2, 200, 1000, 1.0, 800),
                         routes(0, {{lfo1, oscParam(1, kOscFine), 0.03}, {lfo2, oscParam(1, kOscShape), 0.4}, {lfo1, oscParam(0, kOscShape), 0.2},
                                    {kSourceRandom, oscParam(2, kOscShape), 0.5}, {env3, kPartPan, 0.3}, {press, tailParam(kDriveAmount), 0.3},
                                    {wheel, oscParam(1, kOscShape), 0.2}})})});
      // [Crystal] Octaves of saw and pulse under a dark, resonant filter that each note sweeps open and
      // lets close over seconds; slow LFOs turn the widths.
      p.push_back({"Crystal Strings", "Monsta",
                   join({osc(0, 0, kPulse, -12, 0, 1.0), osc(0, 1, kSaw, 12, -12, 0.8), osc(0, 2, kPulse, 0, 0, 0.5),
                         filter(0, 0, kLadder, 150, 0.5, 1.0), {a(tailParam(kDriveAmount), 0.2)}, lfo(0, 2, kLfoSine, 0.35),
                         lfo(0, 3, kLfoTriangle, 0.15), env(0, 0, 1, 1000, 1.0, 3000), env(0, 1, 0.5, 3000, 0.0, 3000),
                         busSet(0, 0, kMul),
                         routes(0, {{env2, cutoff, 0.55}, {lfo2, oscParam(0, kOscShape), 0.3}, {lfo2, oscParam(2, kOscShape), -0.3},
                                    {lfo1, busIn(0), 1.0}, {wheel, busIn(0), 1.0}, {bus1, kPartFine, 0.04}, {press, cutoff, 0.15}})}),
                   names({{0, 0, "Wheel Vibrato"}})});
      // [Softy] A drone that takes seconds to arrive, its filter breathing on a slow LFO; the wheel
      // brings in a resonance that flutters.
      p.push_back({"Softy Drone", "Monsta",
                   join({osc(0, 0, kSaw, -12, 0, 1.0), osc(0, 1, kSaw, 0, -12, 0.8), osc(0, 2, kPulse, 0, 30, 0.5),
                         filter(0, 0, kLowPass, 1200, 0.15, 0.0), lfo(0, 2, kLfoSine, 0.25), lfo(0, 3, kLfoSmooth, 0.3),
                         env(0, 0, 3000, 1000, 1.0, 3000), {a(kPartLevel, -3)},
                         busSet(0, 0, kMul),
                         routes(0, {{lfo1, cutoff, 0.25}, {lfo2, busIn(0), 1.0}, {wheel, busIn(0), 1.0}, {bus1, resonance, 0.4}, {press, cutoff, 0.1}})}),
                   names({{0, 0, "Flutter"}})});
      // [Ceasefre] A wide pad whose widths turn as Env 2 swells over seconds; Press brings in a third
      // oscillator a fifth up, pulsing. The wheel takes out the octave.
      p.push_back({"Ceasefire", "Monsta",
                   join({osc(0, 0, kPulse, 12, 0, 0.5), osc(0, 1, kPulse, 0, 0, 1.0), osc(0, 2, kSaw, 7, 0, 0.0),
                         {a(mixParam(kMixPan, 0), -0.7), a(mixParam(kMixPan, 1), 0.7), a(kSkyPan, 0.5)},
                         filter(0, 0, kLowPass, 1500, 0.45, 0.3), lfo(0, 2, kLfoSine, 3.0), env(0, 0, 1000, 1000, 1.0, 3000),
                         env(0, 1, 2500, 3000, 0.0, 3000),
                         busSet(0, 0, kMul, kLinear, kToUni),
                         routes(0, {{env2, oscParam(0, kOscShape), 0.35}, {env2, oscParam(1, kOscShape), -0.3}, {lfo1, busIn(0), 1.0},
                                    {press, busIn(0), 1.0}, {bus1, mixParam(kMixLevel, 2), 0.5}, {press, mixParam(kMixLevel, 2), 0.2},
                                    {wheel, mixParam(kMixLevel, 0), -0.4}})}),
                   names({{0, 0, "Pulsing Fifth"}})});
      // [ChamberS] Slow chamber strings whose widths and levels move on an LFO; Press is the vibrato.
      p.push_back({"Chamber Strings", "Monsta",
                   join({osc(0, 0, kSaw, 0, -9, 1.0), osc(0, 1, kPulse, 0, 9, 1.0), osc(0, 2, kSaw, -12, 0, 0.4),
                         {a(mixParam(kMixPan, 0), -0.7), a(mixParam(kMixPan, 1), 0.7), a(tailParam(kDriveAmount), 0.2)},
                         filter(0, 0, kLowPass, 1400, 0.35, 0.3), lfo(0, 2, kLfoSine, 5.5), lfo(0, 3, kLfoSine, 0.3),
                         env(0, 0, 500, 1000, 1.0, 1500),
                         busSet(0, 0, kMul, kLinear, kAsIs, 400, 700),
                         routes(0, {{lfo2, oscParam(1, kOscShape), 0.4}, {lfo2, mixParam(kMixLevel, 1), -0.2}, {lfo1, busIn(0), 1.0},
                                    {press, busIn(0), 1.0}, {bus1, kPartFine, 0.03}, {wheel, oscParam(0, kOscShape), 0.3}, {wheel, cutoff, 0.15}})}),
                   names({{0, 0, "Late Vibrato"}})});
      // [FreshAir] Wide saws under a filter resonant enough to whistle at the note; Press sets the
      // whistle wobbling, the wheel adds breath.
      p.push_back({"Fresh Air", "Monsta",
                   join({osc(0, 0, kSaw, 0, -25, 1.0), osc(0, 1, kSaw, 0, 25, 1.0), {a(mixParam(kMixPan, 0), -0.6), a(mixParam(kMixPan, 1), 0.6)},
                         filter(0, 0, kLadder, 1050, 0.85, 1.0), lfo(0, 2, kLfoSine, 6.0), env(0, 0, 300, 1000, 1.0, 1000),
                         busSet(0, 0, kMul),
                         routes(0, {{lfo1, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, cutoff, -0.08}, {wheel, mixParam(kNoiseLevel), 0.2}})}),
                   names({{0, 0, "Whistle Wobble"}})});
      // [ClangHrt] A struck, clanging string: an inharmonic FM strike over a driven saw body, the
      // filter closing as it rings. Press brings the clang back.
      p.push_back({"Clang Heart", "Monsta",
                   join({op(0, 0, kSine, 1.0, 0.6), osc(0, 1, kSaw, 0, 0, 1.0), op(0, 2, kSine, 3.5, 0.0), {a(fm31, 0.15)},
                         {a(tailParam(kDriveAmount), 0.3), a(kVelocityToAmp, 0.6), a(envParam(1, kEnvVelocity), 0.4)},
                         filter(0, 0, kLowPass, 900, 0.2, 1.0), env(0, 0, 1, 1500, 0.45, 800), env(0, 1, 0.5, 800, 0.0, 600),
                         env(0, 2, 0.5, 600, 0.0, 700),
                         routes(0, {{env2, cutoff, 0.4}, {env3, fm31, 0.35}, {press, fm31, 0.2}, {wheel, cutoff, 0.3}})})});
      // [SpaceGod] A howling, resonant swell that rises in pitch and opens over seconds; Press throws
      // its top oscillator into a wild warble.
      p.push_back({"Space God", "Monsta",
                   join({osc(0, 0, kSaw, -2, 0, 1.0), osc(0, 1, kSaw, 0, 20, 0.6), osc(0, 2, kSaw, 24, 0, 0.3),
                         {a(mixParam(kNoiseLevel), 0.1), a(kSkyPan, 0.8), a(kPartLevel, -3)}, filter(0, 0, kLadder, 900, 0.8, 1.0),
                         lfo(0, 2, kLfoSine, 5.0), env(0, 0, 1500, 1000, 1.0, 4000), env(0, 1, 3000, 1000, 1.0, 4000),
                         busSet(0, 0, kMul),
                         routes(0, {{env2, cutoff, 0.3}, {env2, kPartFine, 0.03}, {lfo1, busIn(0), 1.0}, {press, busIn(0), 1.0},
                                    {bus1, oscParam(2, kOscFine), 0.15}, {wheel, resonance, -0.3}})}),
                   names({{0, 0, "Warble"}})});
      // [Opera2.0] A pad that sours as you press, one oscillator sinking flat; the wheel pulses in an
      // octave above.
      p.push_back({"Opera Pad", "Monsta",
                   join({osc(0, 0, kPulse, 0, 0, 0.5), osc(0, 1, kSaw, 12, 0, 1.0), osc(0, 2, kPulse, 12, 4, 0.0),
                         {a(oscParam(0, kOscShape), 0.3), a(mixParam(kNoiseLevel), 0.03), a(tailParam(kDriveAmount), 0.2),
                          a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5)},
                         filter(0, 0, kLowPass, 2000, 0.45, 1.0), lfo(0, 2, kLfoSine, 0.6), env(0, 0, 300, 1000, 1.0, 1200),
                         busSet(0, 0, kMul),
                         routes(0, {{press, oscParam(1, kOscFine), -0.1}, {lfo1, busIn(0), 1.0}, {wheel, busIn(0), 1.0},
                                    {bus1, mixParam(kMixLevel, 2), 0.5}, {wheel, cutoff, 0.1}})}),
                   names({{0, 0, "Octave Pulse"}})});
      // [IceBells] A filter resonant enough to ring, pinged by each note: the harder you strike, the
      // brighter the ping. A slow LFO shimmers it.
      p.push_back({"Ice Bells", "Monsta",
                   join({osc(0, 0, kSaw, 0, -10, 0.5), osc(0, 1, kSaw, 0, 10, 0.5), {a(mixParam(kNoiseLevel), 0.05), a(kSkyPan, 0.6)},
                         filter(0, 0, kLadder, 520, 0.92, 1.0), {a(envParam(1, kEnvVelocity), 1.0)}, lfo(0, 2, kLfoSine, 0.5),
                         env(0, 0, 1, 2500, 0.4, 1500), env(0, 1, 0.5, 40, 0.0, 40),
                         routes(0, {{env2, cutoff, 0.3}, {lfo1, cutoff, 0.03}, {press, resonance, 0.06}, {wheel, cutoff, 0.15}})})});
      // [Oldfield] A lead whose second voice swells in under the first while the vibrato speeds up,
      // as on a seventies record. The wheel speeds it further, Press deepens it.
      p.push_back({"Oldfield Lead", "Monsta",
                   join({osc(0, 0, kPulse, 0, 0, 0.0), osc(0, 1, kPulse, 12, 0, 0.6), osc(0, 2, kSaw, 12, 5, 0.3),
                         {a(oscParam(0, kOscShape), 0.35), a(oscParam(1, kOscShape), 0.35), a(tailParam(kDriveAmount), 0.25)},
                         filter(0, 0, kLowPass, 3500, 0.3, 0.0), lfo(0, 2, kLfoSine, 4.0), mono(0, 30),
                         env(0, 0, 30, 1000, 1.0, 400), env(0, 1, 800, 4000, 0.4, 600),
                         busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 80),
                         routes(0, {{env2, mixParam(kMixLevel, 0), 0.9}, {lfo1, kPartFine, 0.02}, {env2, lfoParam(2, kLfoRate), 0.15},
                                    {lfo1, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, kPartFine, 0.02}, {wheel, lfoParam(2, kLfoRate), 0.1}})}),
                   names({{0, 0, "Pressed Vibrato"}})});
      // [PinkHaze] A hazy, wide pad that glides between chords; Press pumps the filter down on a slow
      // LFO, and the wheel fades it back.
      p.push_back({"Pink Haze", "Monsta",
                   join({osc(0, 0, kSaw, 0, -20, 0.8), osc(0, 1, kPulse, 12, 20, 0.8), osc(0, 2, kPulse, 12, -15, 0.7),
                         {a(mixParam(kMixPan, 0), -0.6), a(mixParam(kMixPan, 1), 0.6), a(kGlideTime, timeValue(150)), a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 5000, 0.4, 0.5), lfo(0, 2, kLfoSine, 0.3), env(0, 0, 600, 1000, 1.0, 1500),
                         busSet(0, 0, kMul),
                         routes(0, {{lfo1, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, cutoff, -0.2}, {wheel, kPartLevel, -0.2}})}),
                   names({{0, 0, "Pump"}})});

      // ---- Motion: patches that keep moving on their own ----
      // Two unsynced LFOs and Env 3 wandering on the FM (a 9 s random walk), so no two bars sound the
      // same; the wheel deepens all of it.
      p.push_back({"Shifting Glass", "Motion",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSine, 3.5, 0.0), {a(fm21, 0.15), a(fm32, 0.1)},
                         {a(kSkyPan, 0.5)}, lfo(0, 2, kLfoSmooth, 0.27), lfo(0, 3, kLfoTriangle, 0.11),
                         env(0, 0, 800, 1000, 1.0, 2500), shape(0, 2, "Wander", 9000, kForever), open(0),
                         routes(0, {{env3, fm21, 0.25}, {lfo1, fm32, 0.25}, {lfo2, oscParam(2, kOscRatio), 0.02},
                                    {lfo2, kPartPan, 0.3}, {wheel, depth(0), 0.15}, {wheel, depth(1), 0.15}, {press, fm21, 0.25}})})});
      // Each note gets its own wave (Random) and its own LFO speed, so a chord shimmers apart.
      p.push_back({"Random Garden", "Motion",
                   join({{a(oscParam(0, kOscWave), 14), a(oscParam(1, kOscWave), 14), a(oscParam(1, kOscPitchMode), kRatio),
                          a(oscParam(1, kOscRatio), 2.0), a(fm21, 0.15), a(kSkyPan, 0.7), a(kDrift, 0.3)},
                         filter(0, 0, kLowPass, 2400, 0.25, 0.4), lfo(0, 2, kLfoSine, 0.4), lfo(0, 3, kLfoSmooth, 2.0),
                         env(0, 0, 400, 1000, 0.8, 2000), env(0, 1, 600, 3000, 0.3, 2000),
                         routes(0, {{kSourceRandom, lfoParam(2, kLfoRate), 0.2}, {lfo1, cutoff, 0.15}, {lfo2, fm21, 0.15},
                                    {env2, cutoff, 0.2}, {kSourceRandom, kPartPan, 0.5}, {wheel, depth(2), 0.2}, {press, fm21, 0.3}})})});
      // A Pluck shape looping as the amp makes every held note pulse on its own clock, faster higher up;
      // Env 3 plays the same shape bent down, so each pulse starts bright.
      p.push_back({"Pulsar", "Motion",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), {a(fm21, 0.2), a(kSkyPan, 0.7)},
                         shape(0, 0, "Pluck", 300, kForever), {a(envParam(0, kEnvRelease), timeValue(600))},
                         shape(0, 2, "Pluck", 300, kForever), bow(0, 2, 0.0, -0.7), open(0),
                         routes(0, {{key, shapeParam(0, kShapeTime), -0.05}, {key, shapeParam(2, kShapeTime), -0.05}, {env3, fm21, 0.3},
                                    {kSourceRandom, shapeParam(0, kShapeTime), 0.03}, {kSourceRandom, shapeParam(2, kShapeTime), 0.03},
                                    {wheel, fm21, 0.3}, {press, shapeParam(0, kShapeTime), -0.05},
                                    {press, shapeParam(2, kShapeTime), -0.05}})})});
      // A sixteenth-note sequence from LFOs alone: S&H on the FM, a dotted-eighth square on the filter.
      p.push_back({"Clockwork", "Motion",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSquare, 0.5, 0.35), {a(fm21, 0.15)},
                         filter(0, 0, kLadder, 1400, 0.4, 0.4), {a(lfoParam(3, kLfoPolarity), 1)}, lfoSync(0, 2, kLfoHold, k16th),
                         lfoSync(0, 3, kLfoSquare, k8thDotted), env(0, 0, 3, 500, 1.0, 300),
                         routes(0, {{lfo1, fm21, 0.3}, {lfo2, cutoff, 0.3}, {lfo2, kPartLevel, -0.15}, {wheel, resonance, 0.3},
                                    {press, depth(0), 0.2}})})});
      // Every expressive dimension does something: Press brightens and adds vibrato, Slide (CC 74) moves
      // the vowel, Glide bends it, and the release velocity sets how long it lingers.
      p.push_back({"MPE Voices", "Motion",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 1.0), {a(tailParam(kFilterRouting), 1), a(kPartLevel, 4)},
                         filter(0, 0, kBandPass, 700, 0.4, 0.3), filter(0, 1, kBandPass, 1200, 0.4, 0.3),
                         {a(filterParam(0, kFilterSlope), 0), a(filterParam(1, kFilterSlope), 0)}, lfo(0, 2, kLfoSine, 5.3),
                         env(0, 0, 150, 1000, 1.0, 600),
                         routes(0, {{press, cutoff, 0.2}, {press, cutoff2, 0.25}, {slide, cutoff2, -0.3}, {lfo1, kPartFine, 0.0},
                                    {press, depth(3), 0.05}, {release, envParam(0, kEnvRelease), 0.25},
                                    {wheel, depth(3), 0.04}})})});
      // Both parts: A's saw drone breathes on a slow LFO, B's FM bells an octave up echo on their own,
      // an Echoes shape looping as their amp, each note's stretched by its Random.
      p.push_back({"Tide Machine", "Motion",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0), {a(mixParam(kMixPan, 0), -0.6), a(mixParam(kMixPan, 1), 0.6)},
                         filter(0, 0, kLadder, 500, 0.35, 0.3), lfo(0, 2, kLfoSine, 0.09), lfo(0, 3, kLfoSmooth, 0.31),
                         env(0, 0, 1500, 1000, 1.0, 3000), {a(kPartLevel, -3)},
                         routes(0, {{lfo1, cutoff, 0.3}, {lfo2, resonance, 0.15}, {wheel, cutoff, 0.25}, {press, cutoff, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 3.5, 0.0),
                         {b(kPartTranspose, 12), b(fm21, 0.2), b(kSkyPan, 0.8), b(kPartLevel, -9)},
                         shape(1, 0, "Echoes", 2600, kForever), {b(envParam(0, kEnvRelease), timeValue(1500))},
                         env(1, 2, 0.5, 300, 0.0, 300), open(1), lfo(1, 2, kLfoTriangle, 0.05),
                         routes(1, {{env3, fm21, 0.3}, {kSourceRandom, shapeParam(0, kShapeTime), 0.06}, {lfo1, kPartLevel, 0.1},
                                    {wheel, fm21, 0.25}})})});

      // ---- Drones: Shape-mode envelopes, after Zadar, that keep a held chord moving for minutes ----
      // Saws and a square sub through a ladder: the amp swells in on a slow S, then two long drifting
      // shapes (23 s and 37 s, so they never line up) move the cutoff, resonance and pan.
      p.push_back({"Basalt", "Drones",
                   join({osc(0, 0, kSaw, 0, -7, 0.8), osc(0, 1, kSaw, -12, 6, 0.8), osc(0, 2, kSquare, -24, 0, 0.35),
                         {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5), a(tailParam(kDriveType), 2),
                          a(tailParam(kDriveAmount), 0.25), a(kDrift, 0.2), a(kSkyPan, 0.4), a(kPartLevel, -4)},
                         filter(0, 0, kLadder, 380, 0.35, 0.3), shape(0, 0, "Slow S", 6000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(6000))}, shape(0, 1, "Wander", 23000, kForever),
                         shape(0, 2, "Gusts", 37000, kForever),
                         routes(0, {{env2, cutoff, 0.28}, {env3, resonance, 0.25}, {env3, kPartPan, 0.3}, {press, cutoff, 0.2},
                                    {wheel, tailParam(kDriveAmount), 0.5}})})});
      // Three sine drawbars that breathe: the 2nd and 3rd partials swell in and out on their own Breath
      // shapes, one reversed, so the organ's colour turns over every few seconds.
      p.push_back({"Breathing Organ", "Drones",
                   join({op(0, 0, kSine, 1.0, 0.9), op(0, 1, kSine, 2.0, 0.15), op(0, 2, kSine, 3.0, 0.1),
                         {a(fmCell(0, 0), 0.08), a(kDrift, 0.15), a(kSkyPan, 0.5), a(kPartLevel, -3)}, open(0),
                         shape(0, 0, "Lift", 2500, kOnce), {a(envParam(0, kEnvRelease), timeValue(3000))},
                         shape(0, 1, "Breath", 9000, kForever), shape(0, 2, "Breath", 13000, kForever),
                         {a(shapeParam(2, kShapeReverse), 1)}, lfo(0, 2, kLfoSine, 5.5),
                         routes(0, {{env2, mixParam(kMixLevel, 1), 0.6}, {env3, mixParam(kMixLevel, 2), 0.5}, {lfo1, kPartFine, 0.01},
                                    {wheel, depth(2), 0.05}, {press, fmCell(0, 0), 0.25}})})});
      // Pink noise through a comb tuned to the note, under a vault: one shape shimmers the comb's ring,
      // another rolls the air's colour over 17 seconds.
      p.push_back({"Cathedral Air", "Drones",
                   join({op(0, 0, kSine, 1.0, 0.15), {a(mixParam(kNoiseLevel), 0.6), a(mixParam(kNoiseColor), -0.4), a(kSkyPan, 0.6),
                          a(kDrift, 0.1)},
                         filter(0, 0, kComb, 261.6, 0.8, 1.0), filter(0, 1, kLowPass, 2500, 0.1, 0.3),
                         shape(0, 0, "Bloom", 5000, kOnce), {a(envParam(0, kEnvRelease), timeValue(7000))},
                         shape(0, 1, "Shimmer", 11000, kForever), shape(0, 2, "Twin Hills", 17000, kForever),
                         routes(0, {{env2, resonance, 0.15}, {env3, cutoff2, 0.3}, {env3, mixParam(kNoiseColor), 0.4},
                                    {wheel, cutoff2, 0.3}, {press, resonance, 0.1}})})});
      // A sub and a 1:2 FM growl that surges up every 14 s, while a 29 s random walk turns its feedback.
      p.push_back({"Undertow", "Drones",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), {a(fm21, 0.08), a(fb2, 0.1), a(kSkyPan, 0.3)},
                         filter(0, 0, kLowPass, 1400, 0.2, 0.4), shape(0, 0, "Slow S", 4000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(5000))}, shape(0, 1, "Surge", 14000, kForever),
                         shape(0, 2, "Brownian", 29000, kForever),
                         routes(0, {{env2, fm21, 0.35}, {env2, cutoff, 0.15}, {env3, fb2, 0.3}, {env3, oscParam(1, kOscFine), 0.04},
                                    {press, fm21, 0.25}, {wheel, cutoff, 0.3}})})});
      // A chain: the amp blooms in over 12 s, and 70% of the way there the filter starts its own 30 s
      // arch, opening and closing once. Hold a chord and wait.
      p.push_back({"Long Bloom", "Drones",
                   join({osc(0, 0, kSaw, 0, -9, 0.9), osc(0, 1, kSaw, 0, 9, 0.9), osc(0, 2, kPulse, 12, 0, 0.3),
                         {a(mixParam(kMixPan, 0), -0.6), a(mixParam(kMixPan, 1), 0.6), a(oscParam(2, kOscShape), 0.3),
                          a(kSkyPan, 0.5), a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 280, 0.3, 0.3), shape(0, 0, "Bloom", 12000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(8000))}, shape(0, 1, "Arch", 30000, kOnce),
                         {a(shapeParam(1, kShapePhase), 0.7), a(chainParam(kChain), 1)}, shape(0, 2, "Wander", 19000, kForever),
                         routes(0, {{env2, cutoff, 0.5}, {env2, resonance, 0.2}, {env3, kPartPan, 0.25}, {env3, oscParam(2, kOscShape), 0.2},
                                    {press, cutoff, 0.15}, {wheel, cutoff, 0.25}})})});
      // A looped chain: Env 2 and Env 3, each a 10 s arch, start each other halfway through, so a
      // filtered saw and a glassy FM tone cross-fade forever.
      p.push_back({"Glacier", "Drones",
                   join({osc(0, 0, kSaw, 0, -5, 0.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 3.5, 0.0),
                         {a(fmCell(2, 1), 0.25), a(kSkyPan, 0.6), a(kDrift, 0.15), a(kPartLevel, 1)},
                         filter(0, 0, kLowPass, 1200, 0.2, 0.4), shape(0, 0, "Lift", 3000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(5000))}, shape(0, 1, "Arch", 10000, kOnce),
                         shape(0, 2, "Arch", 10000, kOnce),
                         {a(shapeParam(1, kShapePhase), 0.5), a(shapeParam(2, kShapePhase), 0.5), a(chainParam(kChain), 5)},
                         routes(0, {{env2, mixParam(kMixLevel, 0), 0.9}, {env3, mixParam(kMixLevel, 1), 0.8}, {env3, kPartPan, 0.2},
                                    {press, cutoff, 0.2}, {wheel, fmCell(2, 1), 0.3}})})});
      // Detuned saws in a comb with tube drive, a hum that flickers: a fast random flicker on the
      // comb's ring and a 41 s wander on the drive.
      p.push_back({"Iron Hum", "Drones",
                   join({osc(0, 0, kSaw, 0, -4, 0.8), osc(0, 1, kSaw, -12, 4, 0.7),
                         {a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.3), a(kSkyPan, 0.4), a(kPartLevel, -6)},
                         filter(0, 0, kComb, 261.6, 0.6, 1.0), filter(0, 1, kLowPass, 1800, 0.15, 0.3),
                         shape(0, 0, "Slow S", 3000, kOnce), {a(envParam(0, kEnvRelease), timeValue(4000))},
                         shape(0, 1, "Flicker", 6000, kForever), shape(0, 2, "Wander", 41000, kForever),
                         routes(0, {{env2, resonance, 0.12}, {env3, tailParam(kDriveAmount), 0.4}, {env3, cutoff2, 0.25},
                                    {press, cutoff2, 0.25}, {wheel, resonance, 0.2}})})});
      // In time with the host: four filter waves to every eight bars, and resonance that wobbles
      // down over sixteen.
      p.push_back({"Tidal Pulse", "Drones",
                   join({osc(0, 0, kSaw, 0, -8, 0.9), osc(0, 1, kSaw, 0, 8, 0.9), osc(0, 2, kSaw, -12, 0, 0.5),
                         {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5), a(kSkyPan, 0.4), a(kPartLevel, -4)},
                         filter(0, 0, kLadder, 450, 0.3, 0.3), shape(0, 0, "Slow S", 4000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(4000))}, shapeSync(0, 1, "Wobble 4", kBars8, kForever),
                         shapeSync(0, 2, "Fading Wobble", kBars16, kForever),
                         routes(0, {{env2, cutoff, 0.3}, {env3, resonance, 0.35}, {press, cutoff, 0.2}, {wheel, cutoff, 0.2}})})});

      // ---- Textures: shapes as the amp, so each held note plays a pattern of its own ----
      // FM glass that sparkles: the amp is the Sparkle shape, looping, and each note's Random
      // stretches its own, so a chord rains rather than strums.
      p.push_back({"Rain Glass", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 7.02, 0.0),
                         {a(fm21, 0.25), a(fm32, 0.15), a(kSkyPan, 0.9), a(kPartLevel, 2)}, open(0),
                         shape(0, 0, "Sparkle", 5000, kForever), {a(envParam(0, kEnvRelease), timeValue(2500))},
                         shape(0, 2, "Wander", 13000, kForever),
                         routes(0, {{kSourceRandom, shapeParam(0, kShapeTime), 0.06}, {key, shapeParam(0, kShapeTime), -0.04},
                                    {env3, fm21, 0.25}, {kSourceRandom, kPartPan, 0.6}, {press, fm32, 0.3}, {wheel, fm21, 0.3}})})});
      // A dropped ball, over and over: higher notes bounce faster, and the filter bounces with it.
      p.push_back({"Bouncing Light", "Textures",
                   join({op(0, 0, kTriangle, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), {a(fm21, 0.15), a(kSkyPan, 0.8)},
                         filter(0, 0, kLowPass, 900, 0.3, 0.5), shape(0, 0, "Bounce", 2400, kForever),
                         {a(envParam(0, kEnvRelease), timeValue(1500))}, shape(0, 1, "Bounce", 2400, kForever),
                         routes(0, {{key, shapeParam(0, kShapeTime), -0.08}, {key, shapeParam(1, kShapeTime), -0.08},
                                    {kSourceRandom, shapeParam(0, kShapeTime), 0.03}, {kSourceRandom, shapeParam(1, kShapeTime), 0.03},
                                    {env2, cutoff, 0.35}, {press, fm21, 0.3}, {wheel, resonance, 0.3}})})});
      // Each note echoes itself on the beat, six taps dying away, and darkens over four bars.
      p.push_back({"Echo Chamber", "Textures",
                   join({osc(0, 0, kSaw, 0, -6, 0.8), osc(0, 1, kPulse, 0, 6, 0.6), {a(kSkyPan, 0.6), a(kPartLevel, 1)},
                         filter(0, 0, kLowPass, 3200, 0.25, 0.5), shapeSync(0, 0, "Echoes", kBar, kForever),
                         {a(envParam(0, kEnvRelease), timeValue(1200))}, shapeSync(0, 1, "Ramp Down", kBars4, kForever),
                         routes(0, {{env2, cutoff, 0.3}, {kSourceRandom, kPartPan, 0.5}, {press, cutoff, 0.2},
                                    {wheel, resonance, 0.3}})})});
      // Gates that come faster through every bar, over a filter stepping at random every eighth.
      p.push_back({"Stutter Fields", "Textures",
                   join({osc(0, 0, kSaw, 0, -10, 0.9), osc(0, 1, kSaw, 0, 10, 0.9), osc(0, 2, kSquare, -12, 0, 0.4),
                         {a(mixParam(kMixPan, 0), -0.6), a(mixParam(kMixPan, 1), 0.6), a(kPartLevel, -3)},
                         filter(0, 0, kLadder, 1100, 0.35, 0.4), shapeSync(0, 0, "Stutter", kBar, kForever),
                         {a(envParam(0, kEnvRelease), timeValue(400)), a(shapeParam(0, kShapeRetrigger), 1)},
                         shapeSync(0, 1, "Random 16", kBars2, kForever), shapeSync(0, 2, "Ramp Up", kBars4, kForever),
                         routes(0, {{env2, cutoff, 0.3}, {env3, resonance, 0.3}, {press, cutoff, 0.2}, {wheel, depth(0), 0.2}})})});
      // A faraway signal: a band-passed tone keyed in sparse steps, with static, drifting in pitch.
      p.push_back({"Number Station", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSquare, 2.0, 0.5),
                         {a(mixParam(kNoiseLevel), 0.15), a(kDrift, 0.3), a(kSkyPan, 0.5), a(kPartLevel, 6)},
                         filter(0, 0, kBandPass, 900, 0.2, 0.6), {a(filterParam(0, kFilterSlope), 0)}, shapeSync(0, 0, "Sparse Steps", kBars2, kForever),
                         {a(envParam(0, kEnvRelease), timeValue(800))}, shape(0, 1, "Flicker", 3000, kForever),
                         shape(0, 2, "Brownian", 17000, kForever),
                         routes(0, {{env2, kPartFine, 0.03}, {env3, cutoff, 0.15}, {env3, mixParam(kNoiseLevel), 0.2},
                                    {press, cutoff, 0.2}, {wheel, mixParam(kNoiseLevel), 0.4}})})});
      // A filter climbing a stair each beat and a second voice stepping in at random: one held note
      // walks a line.
      p.push_back({"Stairwell", "Textures",
                   join({osc(0, 0, kSaw, 0, 0, 0.9), osc(0, 1, kSquare, 7, 0, 0.0), osc(0, 2, kSaw, -12, 0, 0.5),
                         {a(kSkyPan, 0.5), a(kPartLevel, -2)}, filter(0, 0, kLadder, 500, 0.45, 0.4),
                         shape(0, 0, "Lift", 800, kOnce), {a(envParam(0, kEnvRelease), timeValue(900))},
                         shapeSync(0, 1, "Arp Up", kBar, kForever), shapeSync(0, 2, "Random 16", kBars2, kForever),
                         routes(0, {{env2, cutoff, 0.35}, {env3, mixParam(kMixLevel, 1), 0.6}, {press, resonance, 0.25},
                                    {wheel, cutoff, 0.2}})})});
      // A looped chain of three: the amp breathes in and out, the filter strikes and dies, the FM
      // bounces, and round again, each starting part way through the one before.
      p.push_back({"Breathing Machine", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.5, 0.0), osc(0, 2, kSaw, -12, 0, 0.5),
                         {a(fm21, 0.1), a(kSkyPan, 0.6), a(kPartLevel, -1)}, filter(0, 0, kLowPass, 700, 0.35, 0.4),
                         shape(0, 0, "Breath", 3000, kOnce), {a(envParam(0, kEnvRelease), timeValue(1500))},
                         shape(0, 1, "Comet", 2000, kOnce), shape(0, 2, "Bounce", 3000, kOnce),
                         {a(shapeParam(0, kShapePhase), 0.5), a(shapeParam(1, kShapePhase), 0.6), a(shapeParam(2, kShapePhase), 0.6),
                          a(chainParam(kChain), 6)},
                         routes(0, {{env2, cutoff, 0.45}, {env3, fm21, 0.35}, {key, shapeParam(0, kShapeTime), -0.03},
                                    {press, cutoff, 0.2}, {wheel, fm21, 0.25}})})});
      // A slow heartbeat under the note, two beats a bar, and a 20 s wander through the dark.
      p.push_back({"Night Pulse", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), {a(fm21, 0.12), a(mixParam(kNoiseLevel), 0.05),
                          a(kSkyPan, 0.3), a(kPartLevel, 3)},
                         filter(0, 0, kLowPass, 600, 0.2, 0.4), shapeSync(0, 0, "Heartbeat", kBar, kForever),
                         {a(envParam(0, kEnvRelease), timeValue(1000))}, shape(0, 1, "Wander", 20000, kForever),
                         routes(0, {{env2, cutoff, 0.3}, {env2, fm21, 0.2}, {press, fm21, 0.3}, {wheel, cutoff, 0.3}})})});
      // Bells struck again and again in a loop of their own, each tap of the Collapse shape a step
      // lower, and the FM fading with it.
      p.push_back({"Falling Bells", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 5.02, 0.0),
                         {a(fm21, 0.3), a(fm31, 0.1), a(kSkyPan, 0.8)}, open(0),
                         shape(0, 0, "Collapse", 4000, kForever), {a(envParam(0, kEnvRelease), timeValue(2500))},
                         shape(0, 1, "Bell", 4000, kForever), bow(0, 1, 0.0, -0.4),
                         routes(0, {{env2, fm21, 0.35}, {kSourceRandom, shapeParam(0, kShapeTime), 0.05},
                                    {kSourceRandom, shapeParam(1, kShapeTime), 0.05}, {kSourceRandom, kPartPan, 0.5},
                                    {press, fm31, 0.3}, {wheel, fm21, 0.2}})})});

      // ---- Interplay: envelopes that move each other ----
      const clap_id time2 = shapeParam(1, kShapeTime), warp2 = shapeParam(1, kShapeWarp), bend2 = shapeParam(1, kShapeBend);
      const clap_id warp3 = shapeParam(2, kShapeWarp);
      // An arch on the filter that will not keep time: a 25 s wander stretches and squeezes each
      // pass, from under 3 s to over 12, and leans it early or late.
      p.push_back({"Tug of War", "Interplay",
                   join({osc(0, 0, kSaw, 0, -7, 0.9), osc(0, 1, kSaw, 0, 7, 0.9), osc(0, 2, kSaw, -12, 0, 0.4),
                         {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5), a(kSkyPan, 0.4), a(kDrift, 0.15),
                          a(kPartLevel, -4)},
                         filter(0, 0, kLadder, 300, 0.4, 0.3), shape(0, 0, "Bloom", 4000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(5000))}, shape(0, 1, "Arch", 6000, kForever),
                         shape(0, 2, "Wander", 25000, kForever),
                         routes(0, {{env2, cutoff, 0.4}, {env3, time2, 0.12}, {env3, warp2, 0.3}, {env3, kPartPan, 0.2},
                                    {press, cutoff, 0.15}, {wheel, resonance, 0.3}})})});
      // A gated pad that speeds up: a 40 s ramp shortens the gate's pass five times over, so the
      // chord starts as slow pulses and ends as a shiver.
      p.push_back({"Quickening", "Interplay",
                   join({osc(0, 0, kSaw, 0, -5, 0.8), osc(0, 1, kPulse, 0, 5, 0.7), osc(0, 2, kSaw, 12, 0, 0.3),
                         {a(oscParam(1, kOscShape), 0.35), a(kSkyPan, 0.5), a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 350, 0.3, 0.4), shape(0, 0, "Lift", 3000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(4000))}, shape(0, 1, "Gate 8", 4000, kForever),
                         shape(0, 2, "Ramp Up", 40000, kOnce),
                         routes(0, {{env2, cutoff, 0.45}, {env3, time2, -0.12}, {env3, resonance, 0.25},
                                    {press, cutoff, 0.2}, {wheel, time2, -0.1}})})});
      // A breath that slows itself at the top: its own level lengthens its pass, so it lingers when
      // open and falls back quickly. Noise and the filter breathe with it.
      p.push_back({"Lingering Breath", "Interplay",
                   join({osc(0, 0, kSaw, 0, -4, 0.7), osc(0, 1, kTriangle, 12, 3, 0.5),
                         {a(mixParam(kNoiseLevel), 0.0), a(mixParam(kNoiseColor), 0.2), a(kSkyPan, 0.5), a(kDrift, 0.2),
                          a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 450, 0.25, 0.3), shape(0, 0, "Slow S", 3500, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(5000))}, shape(0, 1, "Breath", 5000, kForever),
                         shape(0, 2, "Gusts", 31000, kForever),
                         routes(0, {{env2, time2, 0.1}, {env2, cutoff, 0.4}, {env2, mixParam(kNoiseLevel), 0.35},
                                    {env3, warp2, 0.4}, {press, cutoff, 0.15}, {wheel, resonance, 0.3}})})});
      // Two shapes in orbit: each one warps or bends the other, so the FM's colour and the stereo
      // place never line up the same way twice.
      p.push_back({"Mutual Orbit", "Interplay",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSine, 3.01, 0.0),
                         {a(fm21, 0.05), a(fm31, 0.04), a(kSkyPan, 0.7), a(kDrift, 0.1)}, open(0),
                         shape(0, 0, "Lift", 3000, kOnce), {a(envParam(0, kEnvRelease), timeValue(5000))},
                         shape(0, 1, "Twin Hills", 11000, kForever), shape(0, 2, "Wobble 4", 17000, kForever),
                         routes(0, {{env2, warp3, 0.4}, {env3, bend2, 0.4}, {env2, fm21, 0.25}, {env3, fm31, 0.2},
                                    {env3, kPartPan, 0.5}, {press, fm21, 0.2}, {wheel, fm31, 0.2}})})});
      // Random steps in time with the host, tilted by a slow arch: as it rises the steps climb and
      // crowd the top of the filter, as it falls they sink.
      p.push_back({"Tilting Steps", "Interplay",
                   join({osc(0, 0, kSquare, 0, -6, 0.6), osc(0, 1, kSaw, 0, 6, 0.8), osc(0, 2, kSine, -12, 0, 0.5),
                         {a(kSkyPan, 0.5), a(kPartLevel, -5)},
                         filter(0, 0, kLowPass, 400, 0.45, 0.4), shape(0, 0, "Bloom", 2000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(3000))}, shapeSync(0, 1, "Random 16", kBars2, kForever),
                         shape(0, 2, "Arch", 20000, kForever),
                         routes(0, {{env2, cutoff, 0.35}, {env3, bend2, 0.6}, {env3, resonance, 0.2}, {env2, kPartPan, 0.25},
                                    {press, cutoff, 0.2}, {wheel, bend2, 0.5}})})});
      // A flicker the amp drives: as the note blooms over 8 s, the flicker on the noise and the comb
      // gets faster and faster, and slows again in the release.
      p.push_back({"Gathering Storm", "Interplay",
                   join({osc(0, 0, kSaw, 0, -10, 0.6), osc(0, 1, kSaw, -12, 10, 0.6),
                         {a(mixParam(kNoiseLevel), 0.4), a(mixParam(kNoiseColor), -0.2), a(kSkyPan, 0.6),
                          a(kPartLevel, -5)},
                         filter(0, 0, kComb, 261.6, 0.5, 1.0), filter(0, 1, kLowPass, 900, 0.2, 0.3),
                         shape(0, 0, "Slow S", 8000, kOnce), {a(envParam(0, kEnvRelease), timeValue(6000))},
                         shape(0, 1, "Flicker", 8000, kForever), shape(0, 2, "Wander", 23000, kForever),
                         routes(0, {{kSourceEnv1, time2, -0.14}, {env2, cutoff2, 0.35}, {env2, resonance, 0.15},
                                    {env3, mixParam(kNoiseColor), 0.4}, {press, cutoff2, 0.2}, {wheel, resonance, 0.25}})})});

      // ---- Landscapes: both parts, on Shape envelopes that move each other ----
      // Each envelope's Time, Warp and Bend as destinations.
      const auto tm = [](int e) { return shapeParam(e, kShapeTime); };
      const auto wp = [](int e) { return shapeParam(e, kShapeWarp); };
      const auto bd = [](int e) { return shapeParam(e, kShapeBend); };
      const int macro1 = kSourceMacro1, macro2 = kSourceMacro1 + 1;
      // Part B at the note's own pitch (or `transpose` from it), flying with A rather than hunting it.
      const auto own = [&](int transpose, double level) {
         return Values{b(kPartTranspose, transpose), b(flockParam(kBehaviour), 0), b(kPartLevel, level)};
      };
      // Layer: two tides. A's saws open on a 13 s arch, B's FM glass swells on its own 21 s one, and a
      // wander on each part leans its arch early or late, so they meet and part. Macro 1 slows both
      // tides, Macro 2 adds the FM's edge and the saws' drive.
      p.push_back({"Two Tides", "Landscapes",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, 0, -8, 0.9), osc(0, 1, kSaw, 0, 8, 0.9),
                         {a(mixParam(kMixPan, 0), -0.6), a(mixParam(kMixPan, 1), 0.6), a(kSkyPan, 0.4), a(kDrift, 0.15),
                          a(tailParam(kDriveType), 2), a(kPartLevel, -4)},
                         filter(0, 0, kLadder, 320, 0.3, 0.3), shape(0, 0, "Bloom", 5000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(6000))}, shape(0, 1, "Arch", 13000, kForever),
                         shape(0, 2, "Wander", 29000, kForever),
                         routes(0, {{env2, cutoff, 0.42}, {env3, wp(1), 0.45}, {env3, kPartPan, 0.2}, {macro1, tm(1), 0.1},
                                    {macro2, tailParam(kDriveAmount), 0.5}, {press, cutoff, 0.15}, {wheel, resonance, 0.3}}),
                         op(1, 0, kSine, 1.0, 0.8), op(1, 1, kSine, 2.0, 0.0), op(1, 2, kSine, 5.0, 0.0),
                         {b(fm21, 0.06), b(fm32, 0.04), b(kSkyPan, 0.8)}, own(12, -22), open(1),
                         shape(1, 0, "Lift", 4000, kOnce), {b(envParam(0, kEnvRelease), timeValue(7000))},
                         shape(1, 1, "Arch", 21000, kForever), shape(1, 2, "Gusts", 37000, kForever),
                         routes(1, {{env2, kPartLevel, 0.25}, {env2, fm21, 0.2}, {env3, wp(1), -0.45}, {env3, fm32, 0.15},
                                    {macro1, tm(1), 0.1}, {macro2, fm21, 0.25}, {wheel, fm32, 0.2}})})});
      // Layer: embers. Pink noise through a comb tuned to the note crackles on Sparkle, its pace
      // pulled by a wander; under it a tube-driven sub swells and sinks on a slow surge.
      p.push_back({"Ember Field", "Landscapes",
                   join({{g(kPlayMode, 1.0)},
                         op(0, 0, kSine, 1.0, 0.0), {a(mixParam(kNoiseLevel), 0.7), a(mixParam(kNoiseColor), -0.5), a(kSkyPan, 0.9),
                          a(kPartLevel, -2)},
                         filter(0, 0, kComb, 261.6, 0.85, 1.0), filter(0, 1, kHighPass, 300, 0.1, 0.2),
                         shape(0, 0, "Sparkle", 7000, kForever), {a(envParam(0, kEnvRelease), timeValue(4000))},
                         shape(0, 1, "Shimmer", 9000, kForever), shape(0, 2, "Wander", 19000, kForever),
                         routes(0, {{env2, resonance, 0.12}, {env3, tm(0), 0.09}, {env3, mixParam(kNoiseColor), 0.4},
                                    {kSourceRandom, kPartPan, 0.6}, {macro1, mixParam(kNoiseLevel), 0.3}, {press, resonance, 0.1}}),
                         osc(1, 0, kSine, 0, 0, 1.0), osc(1, 1, kTriangle, 0, 4, 0.5), own(-12, -6),
                         {b(tailParam(kDriveType), 2), b(tailParam(kDriveAmount), 0.3), b(kSkyPan, 0.2)},
                         filter(1, 0, kLadder, 500, 0.2, 0.2), shape(1, 0, "Slow S", 6000, kOnce),
                         {b(envParam(0, kEnvRelease), timeValue(6000))}, shape(1, 1, "Surge", 16000, kForever),
                         shape(1, 2, "Brownian", 33000, kForever),
                         routes(1, {{env2, cutoff, 0.3}, {env2, tailParam(kDriveAmount), 0.3}, {env3, bd(1), 0.5},
                                    {macro1, tailParam(kDriveAmount), 0.4}, {wheel, cutoff, 0.3}})})});
      // Layer: two bells that ring on their own clocks, A every 3 s and B every 4.7 s, so the pair
      // phase against each other; a wander on each part speeds or slows its clock. Macro 1 makes
      // both ring faster.
      p.push_back({"Glass Conversation", "Landscapes",
                   join({{g(kPlayMode, 1.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), {a(fm21, 0.18), a(kSkyPan, 0.7)}, open(0),
                         shape(0, 0, "Bell", 3000, kForever), {a(envParam(0, kEnvRelease), timeValue(3000))},
                         shape(0, 1, "Long Tail", 3000, kForever), shape(0, 2, "Wander", 23000, kForever),
                         {a(shapeParam(1, kShapeRetrigger), 1)},
                         routes(0, {{env2, fm21, 0.3}, {env3, tm(0), 0.07}, {env3, tm(1), 0.07}, {kSourceRandom, kPartPan, 0.5},
                                    {macro1, tm(0), -0.12}, {macro1, tm(1), -0.12}, {press, fm21, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 5.02, 0.0), {b(fm21, 0.12), b(kSkyPan, 0.7)}, own(12, -3),
                         open(1), shape(1, 0, "Bell", 4700, kForever), {b(envParam(0, kEnvRelease), timeValue(3000))},
                         shape(1, 1, "Long Tail", 4700, kForever), shape(1, 2, "Gusts", 31000, kForever),
                         routes(1, {{env2, fm21, 0.3}, {env3, tm(0), -0.07}, {env3, tm(1), -0.07}, {kSourceRandom, kPartPan, 0.5},
                                    {macro1, tm(0), -0.12}, {macro1, tm(1), -0.12}, {wheel, fm21, 0.2}})})});
      // Split at C3: weather in two layers. Below, saws surge on the filter while a random walk
      // bends each surge; above, airy band-passed noise and glass shimmer in on a chain, the filter's
      // arch starting halfway through the amp's.
      p.push_back({"Low and High Weather", "Landscapes",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0)},
                         op(0, 0, kTriangle, 1.0, 0.9), op(0, 1, kSine, 4.0, 0.3),
                         {a(mixParam(kNoiseLevel), 0.7), a(mixParam(kNoiseColor), 0.3), a(kSkyPan, 0.9), a(kPartLevel, 6)},
                         filter(0, 0, kBandPass, 1200, 0.25, 1.0), shape(0, 0, "Arch", 8000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(4000))}, shape(0, 1, "Arch", 10000, kOnce),
                         shape(0, 2, "Shimmer", 7000, kOnce),
                         {a(shapeParam(1, kShapePhase), 0.5), a(shapeParam(2, kShapePhase), 0.5), a(shapeParam(0, kShapePhase), 0.2),
                          a(chainParam(kChain), 6)},
                         routes(0, {{env2, cutoff, 0.35}, {env3, mixParam(kMixLevel, 1), 0.4}, {env3, kPartPan, 0.4},
                                    {press, cutoff, 0.2}, {wheel, mixParam(kNoiseLevel), 0.3}}),
                         osc(1, 0, kSaw, 0, -6, 0.9), osc(1, 1, kSaw, -12, 6, 0.8), own(0, -3),
                         {b(tailParam(kDriveType), 2), b(tailParam(kDriveAmount), 0.25), b(kSkyPan, 0.3)},
                         filter(1, 0, kLowPass, 260, 0.35, 0.3), shape(1, 0, "Slow S", 3000, kOnce),
                         {b(envParam(0, kEnvRelease), timeValue(5000))}, shape(1, 1, "Surge", 12000, kForever),
                         shape(1, 2, "Brownian", 27000, kForever),
                         routes(1, {{env2, cutoff, 0.4}, {env3, bd(1), 0.5}, {env3, wp(1), 0.3}, {press, cutoff, 0.2},
                                    {wheel, resonance, 0.3}})})});
      // Expression: Press turns calm to unrest. Light, a sine choir breathes; lean in and a pulse
      // swarm takes over, its stutter quickening the harder you press.
      p.push_back({"Calm to Unrest", "Landscapes",
                   join({{g(kPlayMode, 5.0), g(kCrossfadeSource, 1.0)},
                         op(0, 0, kSine, 1.0, 0.7), op(0, 1, kAltSine, 2.0, 0.25), op(0, 2, kSine, 3.0, 0.15),
                         {a(kSkyPan, 0.5), a(kDrift, 0.2), a(kPartLevel, -1)}, open(0),
                         shape(0, 0, "Lift", 3000, kOnce), {a(envParam(0, kEnvRelease), timeValue(4000))},
                         shape(0, 1, "Breath", 8000, kForever), shape(0, 2, "Breath", 11000, kForever),
                         {a(shapeParam(2, kShapeReverse), 1)},
                         routes(0, {{env2, mixParam(kMixLevel, 1), 0.4}, {env3, mixParam(kMixLevel, 2), 0.4}, {env2, tm(2), 0.06}}),
                         osc(1, 0, kPulse, 0, -9, 0.8), osc(1, 1, kSaw, 0, 9, 0.8), own(0, -3),
                         {b(oscParam(0, kOscShape), 0.4), b(tailParam(kDriveType), 1), b(tailParam(kDriveAmount), 0.3), b(kSkyPan, 0.7)},
                         filter(1, 0, kLowPass, 900, 0.4, 0.4), shape(1, 0, "Stutter", 2000, kForever),
                         {b(envParam(0, kEnvRelease), timeValue(1500))}, shape(1, 1, "Random 8", 4000, kForever),
                         shape(1, 2, "Gusts", 17000, kForever),
                         routes(1, {{press, tm(0), -0.12}, {env2, cutoff, 0.35}, {env3, tm(1), 0.08}, {env3, kPartPan, 0.4},
                                    {wheel, resonance, 0.3}})})});
      // Expression: Slide moves between two worlds. Slide down, a looped chain of arches cross-fades
      // a saw and FM glass; slide up, random steps in time with the host tick through a ringing filter.
      p.push_back({"Between Worlds", "Landscapes",
                   join({{g(kPlayMode, 5.0), g(kCrossfadeSource, 0.0)},
                         osc(0, 0, kSaw, 0, -5, 0.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 3.5, 0.0),
                         {a(fmCell(2, 1), 0.25), a(kSkyPan, 0.6), a(kDrift, 0.15)},
                         filter(0, 0, kLowPass, 1200, 0.2, 0.4), shape(0, 0, "Lift", 3000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(5000))}, shape(0, 1, "Arch", 9000, kOnce),
                         shape(0, 2, "Arch", 13000, kOnce),
                         {a(shapeParam(1, kShapePhase), 0.5), a(shapeParam(2, kShapePhase), 0.5), a(chainParam(kChain), 5)},
                         routes(0, {{env2, mixParam(kMixLevel, 0), 0.9}, {env3, mixParam(kMixLevel, 1), 0.8}, {env3, kPartPan, 0.2},
                                    {wheel, fmCell(2, 1), 0.3}}),
                         osc(1, 0, kSquare, 0, -4, 0.6), osc(1, 1, kSaw, 0, 4, 0.8), own(0, -4),
                         {b(kSkyPan, 0.5)}, filter(1, 0, kLowPass, 450, 0.55, 0.4), shape(1, 0, "Bloom", 2000, kOnce),
                         {b(envParam(0, kEnvRelease), timeValue(3000))}, shapeSync(1, 1, "Random 16", kBars2, kForever),
                         shape(1, 2, "Twin Hills", 15000, kForever),
                         routes(1, {{env2, cutoff, 0.35}, {env3, bd(1), 0.5}, {env3, resonance, 0.15}, {env2, kPartPan, 0.3},
                                    {wheel, cutoff, 0.2}})})});
      // Velocity: soft notes rain glass, hard ones strike a comet that roars down over 8 s. The
      // rain's sparkle speeds up while the note is young and settles as Env 1's tail fades.
      p.push_back({"Glass and Comets", "Landscapes",
                   join({{g(kPlayMode, 4.0), g(kVelocitySplit, 100.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 7.0, 0.0), {a(fm21, 0.12), a(kSkyPan, 0.8), a(kPartLevel, 6)}, open(0),
                         shape(0, 0, "Sparkle", 5000, kForever), {a(envParam(0, kEnvRelease), timeValue(3500))},
                         shape(0, 1, "Fade", 20000, kOnce), shape(0, 2, "Wander", 13000, kForever),
                         routes(0, {{env2, tm(0), -0.1}, {env3, fm21, 0.25}, {kSourceRandom, kPartPan, 0.6},
                                    {kSourceRandom, tm(0), 0.05}, {press, fm21, 0.2}}),
                         osc(1, 0, kSaw, 0, -10, 0.9), osc(1, 1, kSaw, -12, 10, 0.9), own(0, -2),
                         {b(mixParam(kNoiseLevel), 0.3), b(tailParam(kDriveType), 2), b(tailParam(kDriveAmount), 0.4),
                          b(kSkyPan, 0.5)},
                         filter(1, 0, kLadder, 300, 0.45, 0.3), shape(1, 0, "Comet", 8000, kOnce),
                         {b(envParam(0, kEnvRelease), timeValue(3000))}, shape(1, 1, "Collapse", 8000, kOnce),
                         shape(1, 2, "Tremble", 3000, kForever),
                         routes(1, {{env2, cutoff, 0.5}, {env3, resonance, 0.2}, {velocity, tm(0), 0.06}, {velocity, tm(1), 0.06},
                                    {wheel, cutoff, 0.2}})})});
      // Layer, with B hunting: a pad, and a ring-modded flicker that bites whenever one of B's
      // creatures catches one of A's. The hunters fly faster on the gusts.
      p.push_back({"Hunter's Drone", "Landscapes",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, 0, -7, 0.9), osc(0, 1, kSaw, 0, 7, 0.9), {a(kSkyPan, 0.6), a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 700, 0.2, 0.3), shape(0, 0, "Slow S", 4000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(5000)), a(flockParam(kBite), 0.6)},
                         shape(0, 1, "Twin Hills", 17000, kForever), shape(0, 2, "Wander", 23000, kForever),
                         routes(0, {{env2, cutoff, 0.3}, {env3, wp(1), 0.4}, {kSourceNearest, cutoff, 0.15}, {press, cutoff, 0.2}}),
                         op(1, 0, kSine, 1.0, 0.8), op(1, 1, kSine, 2.5, 0.2), {b(fm21, 0.1), b(kSkyPan, 0.9), b(kPartLevel, -5),
                          b(kPartTranspose, 12)},
                         open(1), shape(1, 0, "Flicker", 6000, kForever), {b(envParam(0, kEnvRelease), timeValue(3000))},
                         shape(1, 1, "Gusts", 13000, kForever), shape(1, 2, "Brownian", 21000, kForever),
                         routes(1, {{env2, flockParam(kSpeed), 0.4}, {env3, tm(0), 0.1}, {kSourceSpeed, fm21, 0.3},
                                    {wheel, flockParam(kSpeed), 0.3}})})});
      // Layer: a choir that turns. B's voices hand over on a looped chain of arches, the root giving
      // way to the fifth and the octave in turn, over A's dark pad that breathes on its own slower
      // arch. Macro 1 slows the turning.
      p.push_back({"Turning Choir", "Landscapes",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, -12, -6, 0.8), osc(0, 1, kTriangle, 0, 6, 0.8), {a(kSkyPan, 0.3), a(kPartLevel, -5)},
                         filter(0, 0, kLowPass, 500, 0.15, 0.3), shape(0, 0, "Bloom", 6000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(6000))}, shape(0, 1, "Plateau", 19000, kForever),
                         routes(0, {{env2, cutoff, 0.3}, {press, cutoff, 0.15}, {wheel, resonance, 0.3}}),
                         op(1, 0, kAltSine, 1.0, 0.6), op(1, 1, kAltSine, 1.5, 0.0), op(1, 2, kAltSine, 2.0, 0.0),
                         {b(kSkyPan, 0.8), b(kDrift, 0.25), b(flockParam(kTunePull), 0.4)}, own(12, -2), open(1),
                         shape(1, 0, "Bloom", 5000, kOnce), {b(envParam(0, kEnvRelease), timeValue(6000))},
                         shape(1, 1, "Arch", 7000, kOnce), shape(1, 2, "Arch", 9000, kOnce),
                         {b(shapeParam(1, kShapePhase), 0.6), b(shapeParam(2, kShapePhase), 0.6), b(chainParam(kChain), 5)},
                         routes(1, {{env2, mixParam(kMixLevel, 0), -0.45}, {env2, mixParam(kMixLevel, 1), 0.7},
                                    {env3, mixParam(kMixLevel, 2), 0.6}, {env3, mixParam(kMixLevel, 0), -0.3}, {macro1, tm(1), 0.08},
                                    {macro1, tm(2), 0.08}, {wheel, kPartLevel, 0.1}})})});
      // Layer, in time with the host: a heartbeat on B's amp, two beats a bar, under A's filter
      // that climbs random steps; a 16-bar wave bends the heartbeat's shape from soft to hard.
      p.push_back({"Clockwork Garden", "Landscapes",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, 0, -5, 0.8), osc(0, 1, kSquare, 12, 5, 0.3), {a(kSkyPan, 0.5), a(kPartLevel, -5)},
                         filter(0, 0, kLowPass, 500, 0.5, 0.4), shape(0, 0, "Bloom", 2000, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(3000))}, shapeSync(0, 1, "Random 8", kBar, kForever),
                         shapeSync(0, 2, "Rising Wobble", kBars8, kForever),
                         routes(0, {{env2, cutoff, 0.35}, {env3, wp(1), 0.5}, {env2, kPartPan, 0.25}, {wheel, resonance, 0.3}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.0), {b(fm21, 0.15), b(kSkyPan, 0.4)}, own(-12, -3),
                         open(1), shapeSync(1, 0, "Heartbeat", kHalfBar, kForever), {b(envParam(0, kEnvRelease), timeValue(1500))},
                         shapeSync(1, 1, "Arch", kBars16, kForever),
                         routes(1, {{env2, bd(0), 0.5}, {env2, fm21, 0.2}, {press, fm21, 0.2}})})});
      // Layer: a low engine and its exhaust. A's comb hums and its drive wanders; B's Random wave
      // spits a saw train, slower on low notes and faster up high.
      p.push_back({"Distant Engine", "Landscapes",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, 0, -4, 0.8), osc(0, 1, kSaw, -12, 4, 0.7),
                         {a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.3), a(kSkyPan, 0.3), a(kPartLevel, -6)},
                         filter(0, 0, kComb, 261.6, 0.6, 1.0), filter(0, 1, kLowPass, 1500, 0.15, 0.3),
                         shape(0, 0, "Slow S", 3000, kOnce), {a(envParam(0, kEnvRelease), timeValue(4000))},
                         shape(0, 1, "Wander", 31000, kForever), shape(0, 2, "Flicker", 9000, kForever),
                         routes(0, {{env2, tailParam(kDriveAmount), 0.4}, {env3, resonance, 0.1}, {env2, tm(2), 0.08},
                                    {press, cutoff2, 0.25}, {wheel, resonance, 0.2}}),
                         osc(1, 0, kRandom, 0, 0, 0.9), own(0, 6), {b(kSkyPan, 0.7)},
                         filter(1, 0, kBandPass, 520, 0.3, 1.0), shape(1, 0, "Saw Train", 2500, kForever),
                         {b(envParam(0, kEnvRelease), timeValue(2000))}, shape(1, 1, "Gusts", 11000, kForever),
                         routes(1, {{key, tm(0), -0.12}, {env2, cutoff, 0.3}, {env2, wp(0), 0.4}, {kSourceRandom, kPartPan, 0.5},
                                    {wheel, cutoff, 0.2}})})});
      // Layer: an afterglow. A plays a soft pad; B, a fifth up, swells in late while the chord is
      // held, and its long shimmer lingers after, faster the longer A has bloomed.
      p.push_back({"Afterglow", "Landscapes",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kTriangle, 0, -6, 0.9), osc(0, 1, kSaw, 0, 6, 0.4), {a(kSkyPan, 0.4), a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 900, 0.1, 0.3), shape(0, 0, "Lift", 2500, kOnce),
                         {a(envParam(0, kEnvRelease), timeValue(4000))}, shape(0, 1, "Bow", 12000, kForever),
                         routes(0, {{env2, cutoff, 0.25}, {press, cutoff, 0.2}, {wheel, resonance, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 3.0, 0.0), {b(fm21, 0.08), b(kSkyPan, 0.9)}, own(7, -16),
                         open(1), shape(1, 0, "Late Swell", 9000, kOnce), {b(envParam(0, kEnvRelease), timeValue(9000))},
                         shape(1, 1, "Shimmer", 6000, kForever), shape(1, 2, "Ramp Up", 20000, kOnce),
                         routes(1, {{env2, kPartLevel, 0.15}, {env2, fm21, 0.2}, {env3, tm(1), -0.1}, {kSourceEnv1, bd(1), 0.4},
                                    {wheel, fm21, 0.2}})})});

      // ---- Essentials, both parts at once ----
      // Layer: the tine piano, and slow strings under it.
      p.push_back({"Piano and Strings", "Essentials",
                   join({{g(kPlayMode, 1.0)}, tines,
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2}}),
                         osc(1, 0, kSaw, 0, -9, 1.0), osc(1, 1, kSaw, 0, 9, 1.0),
                         {b(kPartTranspose, 0), b(mixParam(kMixPan, 0), -0.5), b(mixParam(kMixPan, 1), 0.5), b(kPartLevel, -9),
                          b(flockParam(kBehaviour), 0)},
                         filter(1, 0, kLowPass, 1800, 0.05, 0.5), lfo(1, 2, kLfoSine, 5.0), env(1, 0, 450, 1000, 1.0, 1200),
                         routes(1, {{lfo1, kPartFine, 0.015}, {wheel, kPartLevel, 0.1}, {press, cutoff, 0.2}})})});
      // Split at C3: a bass below, the reed piano above.
      p.push_back({"Bass and Keys Split", "Essentials",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kHalfSine, 1.0, 0.0), {a(fm21, 0.12), a(tailParam(kDriveAmount), 0.35)},
                         filter(0, 0, kLowPass, 4000, 0.0, 0.5), env(0, 0, 1, 2500, 0.15, 250), env(0, 2, 0.5, 400, 0.1, 200),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.3}}),
                         osc(1, 0, kSaw, 0, 0, 1.0), osc(1, 1, kSquare, -12, 0, 0.6),
                         {b(kPartTranspose, 0), b(envParam(1, kEnvVelocity), 0.5), b(flockParam(kBehaviour), 0)},
                         filter(1, 0, kLadder, 260, 0.25, 0.4), mono(1, 25), env(1, 0, 1, 300, 0.85, 90), env(1, 1, 1, 260, 0.15, 120),
                         routes(1, {{env2, cutoff, 0.35}, {wheel, cutoff, 0.3}})})});

      // ---- Flock: the creatures play the sound, from one species to both, hunting each other ----
      p.push_back({"Fireflies", "Flock",
                   {a(flockParam(kBehaviour), kScatter), a(oscParam(0, kOscWave), kHalfSine), a(envParam(0, kEnvAttack), timeValue(1)),
                    a(envParam(0, kEnvDecay), timeValue(220)), a(envParam(0, kEnvSustain), 0.0),
                    a(flockParam(kSpeed), 0.8), a(flockParam(kWander), 0.7), a(flockParam(kTailLength), 0.15),
                    a(flockParam(kTailSpread), 0.2), a(lfoParam(0, kLfoRate), lfoValue(9.0)), a(kSkyPan, 0.9)}});
      p.push_back({"Metal Rain", "Flock",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.73, 0.0), op(0, 2, kSine, 7.11, 0.0),
                         {a(fm21, 0.35), a(fm32, 0.3), a(flockParam(kBehaviour), kScatter), a(flockParam(kWander), 0.6),
                          a(kSkyPan, 0.7)},
                         env(0, 0, 0.5, 240, 0.0, 500), env(0, 2, 0.5, 90, 0.0, 80), open(0),
                         routes(0, {{env3, fm21, 0.3},
                                    {kSourceRandom, oscParam(1, kOscRatio), 0.04},
                                    {kSourceRandom, oscParam(2, kOscRatio), -0.05},
                                    {velocity, fm32, 0.3}})})});
      p.push_back({"Starling Bells", "Flock",
                   join({{a(oscParam(0, kOscWave), kSine), a(oscParam(1, kOscWave), kSine), a(oscParam(1, kOscPitchMode), kRatio),
                          a(oscParam(1, kOscRatio), 3.5), a(fmCell(1, 0), 0.4), a(envParam(0, kEnvAttack), timeValue(2)),
                          a(envParam(0, kEnvDecay), timeValue(1800)), a(envParam(0, kEnvSustain), 0.0),
                          a(envParam(0, kEnvRelease), timeValue(1500)), a(flockParam(kNeighbourFm), 0.7),
                          a(flockParam(kTunePull), 0.4), a(flockParam(kCohesion), 0.7), a(flockParam(kTailLength), 0.7),
                          a(kSkyPan, 0.6)},
                         route(0, 6, kSourceNearest, fmCell(1, 0), 0.3)})});
      p.push_back({"Dripping Ink", "Flock",
                   join({op(0, 0, kAltSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0),
                         {a(fm21, 0.15), a(cutoff, 0.55), a(resonance, 0.3), a(flockParam(kTailLength), 1.0),
                          a(flockParam(kTailSpread), 0.9), a(kSkyPan, 0.5),
                          a(lfoParam(2, kLfoRate), lfoValue(0.8))},
                         env(0, 0, 0.5, 1400, 0.0, 1500), env(0, 2, 0.5, 400, 0.0, 400),
                         routes(0, {{env3, fm21, 0.3},
                                    {kSourceTurn, cutoff, 0.35},
                                    {kSourceSpeed, fm21, 0.2},
                                    {kSourceHeading, resonance, 0.2},
                                    {lfo1, oscParam(1, kOscRatio), 0.02}})})});
      p.push_back({"Ink Ribbons", "Flock",
                   join({{a(oscParam(0, kOscWave), kAltSine), a(fmCell(0, 0), 0.25), a(flockParam(kTailLength), 1.0),
                          a(flockParam(kTailSpread), 0.9), a(flockParam(kLure), 0.35)},
                         route(0, 6, kSourceTurn, filterParam(0, kFilterCutoff), 0.5),
                         route(0, 7, kSourceTurn, fmCell(1, 0), 0.4)})});
      p.push_back({"Murmuration Pad", "Flock",
                   join({{a(oscParam(0, kOscWave), kSaw), a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscFine), 9),
                          a(mixParam(kMixLevel, 1), 0.8), a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5),
                          a(envParam(0, kEnvAttack), timeValue(900)), a(envParam(0, kEnvRelease), timeValue(3000)),
                          a(filterParam(0, kFilterCutoff), 0.45), a(flockParam(kAlignment), 0.8),
                          a(flockParam(kSpeed), 0.25), a(flockParam(kTailLength), 0.9), a(flockParam(kTailSpread), 0.8),
                          a(kSkyPan, 0.8), a(kDrift, 0.15)},
                         route(0, 6, kSourceHeading, kPartPan, 0.6)})});
      p.push_back({"Pressure Front", "Flock",
                   join({{a(oscParam(1, kOscWave), kSine), a(oscParam(1, kOscPitchMode), kRatio), a(oscParam(1, kOscRatio), 2.0),
                          a(flockParam(kNeighbourFm), 0.5), a(lfoParam(2, kLfoRate), lfoValue(6.0))},
                         route(0, 6, press, fmCell(1, 0), 0.6), route(0, 7, kSourceLfo1, kPartFine, 0.15)})});
      p.push_back({"Hunter Clicks", "Flock",
                   join({{g(kPlayMode, 1.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0),
                         {a(fm21, 0.25), a(flockParam(kBehaviour), kFlee), a(flockParam(kSpeed), 0.7), a(kSkyPan, 0.6)},
                         env(0, 0, 0.5, 130, 0.0, 120), env(0, 2, 0.5, 50, 0.0, 40), open(0),
                         routes(0, {{env3, fm21, 0.4}, {kSourceNearest, fm21, 0.4}, {kSourceSpeed, oscParam(1, kOscRatio), 0.05}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 1.41, 0.0),
                         {b(kPartTranspose, 12), b(fm21, 0.4), b(flockParam(kSpeed), 0.85), b(flockParam(kBite), 0.7),
                          b(flockParam(kNeighbourFm), 0.4), b(kSkyPan, 0.6), b(kPartLevel, -4)},
                         env(1, 0, 0.5, 70, 0.0, 60), env(1, 2, 0.5, 30, 0.0, 30), open(1),
                         routes(1, {{env3, fm21, 0.4}, {kSourceNearest, fm21, 0.3}})})});
      const Values vibes = join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.0), {a(fm21, 0.15)},
                                 env(0, 0, 0.5, 3000, 0.0, 1200), env(0, 2, 0.5, 300, 0.0, 200), open(0)});
      p.push_back({"Mallet Flock", "Flock",
                   join({{g(kPlayMode, 1.0)}, vibes, {a(flockParam(kNeighbourFm), 0.3), a(kSkyPan, 0.7)},
                         routes(0, {{env3, fm21, 0.2}, {kSourceNearest, fm21, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 4.0, 0.0),
                         {b(kPartTranspose, 12), b(flockParam(kBehaviour), 0), b(fm21, 0.2), b(flockParam(kNeighbourFm), 0.35),
                          b(flockParam(kTunePull), 0.5), b(kSkyPan, 0.7), b(kPartLevel, -6)},
                         env(1, 0, 0.5, 1200, 0.0, 800), env(1, 2, 0.5, 40, 0.0, 40), open(1),
                         routes(1, {{env3, fm21, 0.35}, {kSourceHeading, fm21, 0.2}})})});
      p.push_back({"Shark Bass", "Flock",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0), b(flockParam(kBehaviour), kHunt), b(oscParam(0, kOscWave), kSaw),
                          b(oscParam(1, kOscWave), kPulse), b(mixParam(kMixLevel, 1), 0.7), b(filterParam(0, kFilterCutoff), 0.35),
                          b(filterParam(0, kFilterResonance), 0.5), b(flockParam(kSpeed), 0.85), b(flockParam(kBite), 0.8)},
                         route(1, 6, kSourceSpeed, filterParam(0, kFilterCutoff), 0.45)})});
      p.push_back({"Predator and Prey", "Flock",
                   join({{g(kPlayMode, 1.0), a(flockParam(kBehaviour), kFlee), b(flockParam(kBehaviour), kHunt),
                          b(oscParam(0, kOscWave), kPulse), b(kPartTranspose, -12), b(flockParam(kBite), 0.9),
                          a(flockParam(kSpeed), 0.6), b(flockParam(kSpeed), 0.7)},
                         route(0, 6, kSourceNearest, filterParam(0, kFilterCutoff), 0.6)})});
      // Both species at full tilt: the shoal's FM opens with pressure and with how fast and near it
      // flies, and the hunters' bite rings whatever they catch.
      p.push_back({"Murmuration Riot", "Flock",
                   join({{g(kPlayMode, 1.0), a(kPartLevel, -3)},
                         op(0, 0, kSaw, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSine, 3.01, 0.0),
                         {a(fm21, 0.25), a(fb2, 0.2), a(fm32, 0.2), a(cutoff, 0.5), a(resonance, 0.35),
                          a(flockParam(kNeighbourFm), 0.45), a(flockParam(kTunePull), 0.3), a(flockParam(kSpeed), 0.6),
                          a(flockParam(kWander), 0.5), a(kSkyPan, 0.8), a(kDrift, 0.1),
                          a(lfoParam(2, kLfoRate), lfoValue(5.5))},
                         env(0, 0, 120, 800, 0.85, 1200), env(0, 2, 200, 1200, 0.3, 800),
                         routes(0, {{env3, fm21, 0.35},
                                    {press, fm21, 0.5},
                                    {press, cutoff, 0.3},
                                    {kSourceTurn, cutoff, 0.25},
                                    {kSourceSpeed, fm32, 0.3},
                                    {kSourceNearest, resonance, 0.25},
                                    {lfo1, kPartFine, 0.0},
                                    {press, routeParam(6, kRouteAmount), 0.08}}),
                         op(1, 0, kPulse, 1.0, 1.0), op(1, 1, kSine, 1.0, 0.0),
                         {b(oscParam(0, kOscShape), 0.3), b(fm21, 0.3), b(filterParam(0, kFilterType), 5), b(cutoff, 0.35),
                          b(resonance, 0.5), b(tailParam(kDriveType), 2), b(tailParam(kDriveAmount), 0.3),
                          b(flockParam(kBite), 0.6), b(flockParam(kNeighbourFm), 0.3), b(flockParam(kSpeed), 0.75),
                          b(kSkyPan, 0.6), b(kPartLevel, -10)},
                         env(1, 0, 80, 800, 0.9, 800),
                         routes(1, {{kSourceNearest, cutoff, 0.4}, {kSourceSpeed, fm21, 0.3}, {press, fm21, 0.3}})})});
      p.push_back({"Murmuration Storm", "Flock",
                   join({{a(kPartLevel, -5)}, {g(kPlayMode, 1.0), a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0),
                          a(oscParam(1, kOscFine), 9), a(mixParam(kMixLevel, 1), 0.9), a(cutoff, 0.4), a(resonance, 0.2),
                          a(flockParam(kNeighbourFm), 0.5), a(flockParam(kTunePull), 0.5), a(kSkyPan, 0.8)},
                         env(0, 0, 3000, 1000, 1.0, 4000), env(0, 1, 3000, 1000, 1.0, 4000),
                         routes(0, {{env2, cutoff, 0.3},
                                    {kSourceNearest, cutoff, 0.3},
                                    {kSourceNearest, kPartPan, 0.3},
                                    {kSourceSpeed, fm21, 0.2},
                                    {press, routeParam(1, kRouteAmount), 0.3}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.0),
                         {b(fm21, 0.2), b(flockParam(kBite), 0.4), b(flockParam(kNeighbourFm), 0.3), b(kSkyPan, 0.6), b(kPartLevel, -8)},
                         env(1, 0, 3000, 1000, 1.0, 4000), env(1, 2, 4000, 1000, 1.0, 3000), open(1),
                         routes(1, {{kSourceNearest, fm21, 0.4}, {env3, fm21, 0.3}, {kSourceHeading, oscParam(1, kOscFine), 0.1}})})});

      // ---- Flock, with the buses: what the creatures do becomes the sound ----
      // A choir that sings louder and brighter as its creatures gather: how near each one's
      // neighbour is, eased on an S-curve and slowed, opens it and draws the voices into pure
      // intervals. Press lifts a creature up the sky (Lure Y), and a vibrato comes in only while
      // one is pressed and flying upward.
      p.push_back({"Gathering Voices", "Flock",
                   join({op(0, 0, kAltSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSaw, 1.0, 0.25),
                         {a(fm21, 0.12), a(oscParam(2, kOscFine), 7), a(kSkyPan, 0.7), a(kDrift, 0.1),
                          a(flockParam(kCohesion), 0.8), a(flockParam(kAlignment), 0.7), a(flockParam(kNeighbourFm), 0.35),
                          a(flockParam(kTunePull), 0.7), a(flockParam(kTailLength), 0.8), a(flockParam(kLureY), kLurePress)},
                         filter(0, 0, kLowPass, 900, 0.2, 0.5), env(0, 0, 700, 1000, 1.0, 2500), lfo(0, 2, kLfoSine, 5.0),
                         busSet(0, 0, kSum, kS, kAsIs, 0, 0, 400), busSet(0, 1, kMul, kLinear, kAsIs, 0, 0, 80),
                         routes(0, {{nearest, busIn(0), 1.0}, {bus1, cutoff, 0.4}, {bus1, fm21, 0.2}, {bus1, kPartLevel, 0.06},
                                    {lfo1, busIn(1), 1.0}, {press, busIn(1), 1.0}, {heading, busIn(1), 1.0}, {bus2, kPartFine, 0.25},
                                    {wheel, cutoff, 0.2}})}),
                   names({{0, 0, "Gathering"}, {0, 1, "Press Climb Vibrato"}})});
      // Predator and prey, both heard. A's glass plucks flee; B's growling saws hunt them and bite. The
      // faster or nearer a hunter is (the greater of the two) the more its filter opens, and the
      // nearer the prey's pursuer, the more it rings and the wider it scatters.
      p.push_back({"Hunting Grounds", "Flock",
                   join({{g(kPlayMode, 1.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0),
                         {a(fm21, 0.2), a(flockParam(kBehaviour), kFlee), a(flockParam(kSpeed), 0.65), a(flockParam(kWander), 0.4),
                          a(kSkyPan, 0.8), a(kPartLevel, -4)},
                         env(0, 0, 1, 900, 0.0, 700), env(0, 2, 0.5, 200, 0.0, 150), open(0),
                         busSet(0, 0, kSum, kExpo, kAsIs, 0, 0, 120),
                         routes(0, {{env3, fm21, 0.3}, {nearest, busIn(0), 1.0}, {bus1, fm21, 0.35}, {bus1, flockParam(kWander), 0.4},
                                    {random, kPartPan, 0.3}}),
                         osc(1, 0, kSaw, -12, -6, 1.0), osc(1, 1, kSaw, -12, 6, 0.8), filter(1, 0, kLadder, 200, 0.45, 0.3),
                         {b(flockParam(kBehaviour), kHunt), b(flockParam(kSpeed), 0.8), b(flockParam(kBite), 0.75), b(kSkyPan, 0.5),
                          b(tailParam(kDriveType), 2), b(tailParam(kDriveAmount), 0.3), b(kPartLevel, -8)},
                         env(1, 0, 40, 1000, 0.9, 600), busSet(1, 0, kMax, kLinear, kAsIs, 0, 0, 60),
                         routes(1, {{speed, busIn(0), 1.0}, {nearest, busIn(0), 0.8}, {bus1, cutoff, 0.45}, {bus1, tailParam(kDriveAmount), 0.3},
                                    {press, flockParam(kSpeed), 0.3}, {wheel, resonance, 0.3}})}),
                   names({{0, 0, "Pursued"}, {1, 0, "Hunt Heat"}})});
      // A flock that arpeggiates: each note runs Arp Up as its amp, and the faster its creature flies
      // the faster the steps run. Its turning pans it, and the whole part leans its timbre with the
      // newest creature's heading (a Global bus, slowed).
      p.push_back({"Flight Steps", "Flock",
                   join({osc(0, 0, kPulse, 0, 0, 1.0), osc(0, 1, kSaw, 12, 5, 0.4), {a(oscParam(0, kOscShape), 0.3), a(kSkyPan, 0.6),
                          a(flockParam(kSpeed), 0.5), a(flockParam(kWander), 0.6), a(flockParam(kBehaviour), kScatter)},
                         filter(0, 0, kLowPass, 1400, 0.35, 0.4), shape(0, 0, "Arp Up", 1200, kForever),
                         {a(envParam(0, kEnvRelease), timeValue(400))},
                         busSet(0, 0, kSum, kS, kAsIs, 0, 0, 250), busSet(0, 1, kSum, kLinear, kAsIs, 0, 0, 900, kAll),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, tm(0), -0.12}, {turn, kPartPan, 0.35}, {heading, busIn(1), 1.0},
                                    {bus2, oscParam(0, kOscShape), 0.4}, {bus2, cutoff, 0.25}, {press, resonance, 0.3},
                                    {wheel, tm(0), -0.1}})}),
                   names({{0, 0, "Wing Speed"}, {0, 1, "Flock Heading"}})});
      // A theremin played by its creature: Press lifts it up the sky, and a creature flying upward
      // bends sharp, diving flat, through a slow lag; the wingbeat bobs it into a natural vibrato.
      p.push_back({"Creature Theremin", "Flock",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), {a(fm21, 0.06), a(fb1, 0.05)}, mono(0, 180),
                         {a(flockParam(kLureY), kLurePress), a(flockParam(kLure), 0.8), a(flockParam(kSpeed), 0.55),
                          a(lfoParam(0, kLfoPush), 0.5), a(lfoParam(0, kLfoRate), lfoValue(5.5)), a(kSkyPan, 0.5)},
                         env(0, 0, 120, 1000, 1.0, 900), open(0),
                         busSet(0, 0, kSum, kLinear, kToBi, 0, 0, 300),
                         routes(0, {{heading, busIn(0), 1.0}, {bus1, kPartFine, 0.18}, {press, fm21, 0.25}, {speed, kPartLevel, 0.05},
                                    {wheel, fb1, 0.3}})}),
                   names({{0, 0, "Climb and Dive"}})});
      // Glass that scatters: each creature keeps clear of the rest, and the more crowded it is the
      // duller and quieter it rings (Base less Nearest, on an exponential curve); alone, it opens up and shimmers.
      p.push_back({"Scatter Glass", "Flock",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.01, 0.0), op(0, 2, kSine, 7.0, 0.0),
                         {a(fm21, 0.25), a(fm32, 0.15), a(flockParam(kBehaviour), kScatter), a(flockParam(kSeparation), 0.8),
                          a(flockParam(kWander), 0.5), a(flockParam(kTailLength), 0.6), a(kSkyPan, 0.9)},
                         env(0, 0, 2, 2400, 0.3, 2000), open(0), lfo(0, 2, kLfoSine, 0.7),
                         busSet(0, 0, kSum, kExpo, kAsIs, 0, 0, 200),
                         {a(busParam(0, kBusOffset), 1.0)},
                         routes(0, {{nearest, busIn(0), -1.0}, {bus1, fm21, 0.3}, {bus1, fm32, 0.25}, {bus1, kPartLevel, 0.05},
                                    {lfo1, oscParam(1, kOscRatio), 0.01}, {press, fm32, 0.3}, {wheel, flockParam(kSeparation), 0.3}})}),
                   names({{0, 0, "Alone"}})});
      // A shoal in a current: the wind pushes it hard, and how fast each creature is carried rings a
      // comb tuned to its note, slowed so it swells rather than chatters.
      p.push_back({"Shoal Current", "Flock",
                   join({osc(0, 0, kSaw, 0, -5, 0.8), osc(0, 1, kTriangle, 0, 5, 0.8),
                         {a(mixParam(kNoiseLevel), 0.25), a(mixParam(kNoiseColor), -0.3), a(flockParam(kAlignment), 0.85),
                          a(flockParam(kCohesion), 0.6), a(lfoParam(1, kLfoPush), 0.9), a(lfoParam(1, kLfoRate), lfoValue(0.12)),
                          a(flockParam(kTailLength), 0.9), a(flockParam(kTailSpread), 0.8), a(kSkyPan, 0.7), a(kPartLevel, -3)},
                         filter(0, 0, kComb, 261.6, 0.55, 1.0), filter(0, 1, kLowPass, 2200, 0.1, 0.3),
                         env(0, 0, 900, 1000, 1.0, 3000), busSet(0, 0, kSum, kS, kAsIs, 0, 0, 700),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, resonance, 0.35}, {bus1, cutoff2, 0.25}, {turn, kPartPan, 0.3},
                                    {press, mixParam(kNoiseLevel), 0.4}, {wheel, lfoParam(1, kLfoPush), 0.1}})}),
                   names({{0, 0, "Carried"}})});
      // A low drone that the hunters ring. A's saws hold the floor; B's sines, an octave and a fifth
      // up, hunt them, and their bite only shows where a slow gust and the hunter's nearness agree
      // (the lesser of the two).
      p.push_back({"Circling Hawks", "Flock",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, -12, -4, 0.8), osc(0, 1, kSaw, 0, 4, 0.6), filter(0, 0, kLadder, 300, 0.25, 0.3),
                         {a(flockParam(kSpeed), 0.3), a(flockParam(kCohesion), 0.8), a(kSkyPan, 0.3), a(kPartLevel, -5)},
                         env(0, 0, 1500, 1000, 1.0, 4000), shape(0, 1, "Gusts", 19000, kForever),
                         routes(0, {{env2, cutoff, 0.25}, {nearest, cutoff, 0.15}, {press, cutoff, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.0),
                         {b(kPartTranspose, 19), b(fm21, 0.1), b(flockParam(kBehaviour), kHunt), b(flockParam(kSpeed), 0.6),
                          b(flockParam(kBite), 0.6), b(flockParam(kNeighbourFm), 0.3), b(kSkyPan, 0.9), b(kPartLevel, -18)},
                         env(1, 0, 2500, 1000, 1.0, 4000), open(1), shape(1, 1, "Gusts", 13000, kForever),
                         busSet(1, 0, kMin, kLinear, kAsIs, 0, 0, 300),
                         routes(1, {{env2, busIn(0), 1.0}, {nearest, busIn(0), 1.0}, {bus1, flockParam(kBite), 0.4}, {bus1, fm21, 0.3},
                                    {bus1, kPartLevel, 0.08}, {wheel, flockParam(kSpeed), 0.3}})}),
                   names({{1, 0, "Gust and Near"}})});
      // A gate that swings with the flock: Gate 8 in time with the host plays the amp, and the more
      // crowded a creature, the later each step lands (Warp), so a tight flock grooves.
      p.push_back({"Crowd Swing", "Flock",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 1.0), {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5),
                          a(flockParam(kCohesion), 0.75), a(kSkyPan, 0.6), a(kPartLevel, -2)},
                         filter(0, 0, kLowPass, 1300, 0.3, 0.4), shapeSync(0, 0, "Gate 8", kBar, kForever),
                         {a(envParam(0, kEnvRelease), timeValue(300))}, busSet(0, 0, kSum, kS, kAsIs, 0, 0, 500),
                         routes(0, {{nearest, busIn(0), 1.0}, {bus1, wp(0), 0.5}, {bus1, cutoff, 0.2}, {turn, kPartPan, 0.3},
                                    {press, bd(0), 0.6}, {wheel, resonance, 0.3}})}),
                   names({{0, 0, "Crowding"}})});
      // Strings that bow harder as their creatures turn: how sharply each one turns, either way
      // (Absolute), roughens its bow with noise and drive; flying together they pull in tune.
      p.push_back({"Turning Strings", "Flock",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 0.9), osc(0, 2, kSaw, -12, 0, 0.4),
                         {a(mixParam(kNoiseLevel), 0.05), a(mixParam(kNoiseColor), 0.3), a(flockParam(kAlignment), 0.8),
                          a(flockParam(kTunePull), 0.6), a(flockParam(kWander), 0.4), a(kSkyPan, 0.7), a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 1600, 0.15, 0.4), env(0, 0, 450, 1000, 1.0, 1400), lfo(0, 2, kLfoSine, 5.2),
                         {a(lfoParam(2, kLfoDelay), timeValue(600))},
                         busSet(0, 0, kSum, kAbs, kAsIs, 0, 0, 150),
                         routes(0, {{turn, busIn(0), 1.0}, {bus1, mixParam(kNoiseLevel), 0.35}, {bus1, tailParam(kDriveAmount), 0.3},
                                    {bus1, cutoff, 0.15}, {lfo1, kPartFine, 0.04}, {press, cutoff, 0.25}, {wheel, depth(4), 0.04}})}),
                   names({{0, 0, "Turn Pressure"}})});
      // Two flocks in conversation: each part's creatures ring the other's (Neighbour FM), velocity
      // places A's lure and Slide lifts B's, and each part's chatter comes in a second or two into
      // the chord.
      p.push_back({"Two Flocks Talking", "Flock",
                   join({{g(kPlayMode, 1.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kTriangle, 2.0, 0.0),
                         {a(fm21, 0.15), a(flockParam(kNeighbourFm), 0.5), a(flockParam(kLureX), 1), a(kSkyPan, 0.9),
                          a(kPartLevel, -5)},
                         env(0, 0, 300, 1000, 1.0, 2000), open(0), busSet(0, 0, kMul, kLinear, kAsIs, 1200, 1500),
                         routes(0, {{nearest, busIn(0), 1.0}, {speed, busIn(0), 1.0}, {bus1, fm21, 0.6}, {turn, kPartPan, 0.3},
                                    {press, fm21, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 1.5, 0.0),
                         {b(kPartTranspose, 7), b(fm21, 0.12), b(flockParam(kBehaviour), 0), b(flockParam(kNeighbourFm), 0.5),
                          b(flockParam(kLureY), kLureSlide), b(kSkyPan, 0.9), b(kPartLevel, -9)},
                         env(1, 0, 600, 1000, 1.0, 2500), open(1), busSet(1, 0, kMul, kLinear, kAsIs, 2200, 2000),
                         routes(1, {{nearest, busIn(0), 1.0}, {speed, busIn(0), 1.0}, {bus1, fm21, 0.6}, {turn, kPartPan, 0.3},
                                    {slide, fm21, 0.2}})}),
                   names({{0, 0, "A Chatter"}, {1, 0, "B Chatter"}})});
      // A tremolo the wingbeat plays: how fast each creature flies sets how deep LFO 1's tremolo goes
      // (the two multiplied), so a resting flock holds still and a flying one shivers.
      p.push_back({"Wingbeat Tremolo", "Flock",
                   join({osc(0, 0, kTriangle, 0, -4, 1.0), osc(0, 1, kPulse, 0, 4, 0.5), {a(oscParam(1, kOscShape), 0.4),
                          a(flockParam(kSpeed), 0.6), a(flockParam(kWander), 0.6), a(lfoParam(0, kLfoPush), 0.6), a(kSkyPan, 0.8),
                          a(kPartLevel, -1)},
                         filter(0, 0, kLowPass, 2000, 0.15, 0.4), env(0, 0, 150, 1000, 1.0, 1500), lfo(0, 2, kLfoSine, 6.5),
                         busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 40),
                         routes(0, {{lfo1, busIn(0), 1.0}, {speed, busIn(0), 1.0}, {bus1, kPartLevel, 0.12}, {bus1, kPartPan, 0.2},
                                    {speed, lfoParam(2, kLfoRate), 0.1}, {press, flockParam(kSpeed), 0.4}, {wheel, cutoff, 0.25}})}),
                   names({{0, 0, "Flutter"}})});
      // Bells that panic. Part B is a silent hunter (heard only through its bite on A); the bells
      // flee it, and the faster they run the brighter and more out of tune they ring.
      p.push_back({"Panic Bells", "Flock",
                   join({{g(kPlayMode, 1.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 1.003, 0.4),
                         {a(fm21, 0.18), a(flockParam(kBehaviour), kFlee), a(flockParam(kSpeed), 0.75), a(kSkyPan, 0.9)},
                         env(0, 0, 1, 2600, 0.0, 1800), open(0), busSet(0, 0, kSum, kExpo, kAsIs, 0, 0, 150),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, fm21, 0.35}, {bus1, oscParam(2, kOscRatio), 0.02},
                                    {velocity, fm21, 0.2}, {nearest, kPartPan, 0.3}}),
                         op(1, 0, kSine, 1.0, 1.0), {b(flockParam(kBehaviour), kHunt), b(flockParam(kSpeed), 0.9),
                          b(flockParam(kBite), 0.35), b(kPartLevel, -60)},
                         env(1, 0, 1, 1000, 1.0, 1000), open(1), routes(1, {{press, flockParam(kSpeed), 0.3}})}),
                   names({{0, 0, "Panic"}})});
      // A drone flock: three detuned saws per creature, held in tune by Tune Pull; the flock's middle
      // (how near the neighbours are) and its climb blend into one slow colour for the whole part.
      p.push_back({"Flock Organ", "Flock",
                   join({osc(0, 0, kSaw, 0, -9, 0.7), osc(0, 1, kSaw, 0, 9, 0.7), osc(0, 2, kSquare, -12, 0, 0.4),
                         {a(flockParam(kTunePull), 0.8), a(flockParam(kCohesion), 0.7), a(flockParam(kSpeed), 0.3),
                          a(kDrift, 0.2), a(kSkyPan, 0.6), a(kPartLevel, -5)},
                         filter(0, 0, kLowPass, 700, 0.25, 0.3), env(0, 0, 1500, 1000, 1.0, 3500),
                         busSet(0, 0, kSum, kLinear, kAsIs, 0, 0, 1500, kAll),
                         routes(0, {{nearest, busIn(0), 0.6}, {heading, busIn(0), 0.6}, {bus1, cutoff, 0.35}, {bus1, resonance, 0.2},
                                    {turn, kPartPan, 0.3}, {press, flockParam(kTunePull), -0.4}, {wheel, cutoff, 0.25}})}),
                   names({{0, 0, "Flock Colour"}})});
      // Ink that sings: long tails, each creature's speed and turning blended into one bus that
      // sweeps a resonant band pass, and the lure follows Press up the sky.
      p.push_back({"Ink Song", "Flock",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 12, 0, 0.3), {a(oscParam(1, kOscShape), 0.25),
                          a(flockParam(kTailLength), 1.0), a(flockParam(kTailSpread), 0.9), a(flockParam(kLureY), kLurePress),
                          a(flockParam(kWander), 0.4), a(kSkyPan, 0.7)},
                         filter(0, 0, kBandPass, 700, 0.6, 0.5), env(0, 0, 200, 1000, 1.0, 1800),
                         busSet(0, 0, kSum, kS, kAsIs, 0, 0, 120),
                         routes(0, {{speed, busIn(0), 0.6}, {turn, busIn(0), 0.5}, {bus1, cutoff, 0.4}, {heading, resonance, 0.2},
                                    {press, flockParam(kSpeed), 0.3}, {wheel, resonance, 0.25}})}),
                   names({{0, 0, "Flight Line"}})});

      // ---- Pressure: Press, shaped by the buses ----
      // A vibrato you lean into: LFO 1 times Press, fading in over a second after a short delay, so
      // a quick note stays plain and a held, pressed one sings.
      p.push_back({"Lean-In Vibrato", "Pressure",
                   join({osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kSaw, 0, 4, 0.8), filter(0, 0, kLowPass, 1100, 0.2, 0.5),
                         mono(0, 60), env(0, 0, 40, 1000, 1.0, 400), lfo(0, 2, kLfoSine, 5.5), {a(kSkyPan, 0.4)},
                         busSet(0, 0, kMul, kLinear, kAsIs, 350, 900), busSet(0, 1, kSum, kExpo, kAsIs, 0, 0, 30),
                         routes(0, {{lfo1, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, kPartFine, 0.1}, {press, busIn(1), 1.0},
                                    {bus2, cutoff, 0.35}, {bus2, kPartLevel, 0.05}, {press, lfoParam(2, kLfoRate), 0.08},
                                    {wheel, depth(2), 0.05}})}),
                   names({{0, 0, "Leaned Vibrato"}, {0, 1, "Breath"}})});
      // A reed you blow: Press on an exponential curve is the breath, so it speaks softly and then
      // roars; a breathy noise rides on top, and a pressed note bends a touch sharp.
      p.push_back({"Blown Reed", "Pressure",
                   join({osc(0, 0, kPulse, 0, 0, 1.0), osc(0, 1, kSaw, 0, 3, 0.4), {a(oscParam(0, kOscShape), 0.35),
                          a(mixParam(kNoiseLevel), 0.0), a(mixParam(kNoiseColor), 0.5), a(kPartLevel, -14), a(kSkyPan, 0.3)},
                         filter(0, 0, kLowPass, 420, 0.25, 0.6), mono(0, 50), env(0, 0, 30, 1000, 1.0, 200),
                         busSet(0, 0, kSum, kExpo, kAsIs, 0, 0, 25),
                         routes(0, {{press, busIn(0), 1.0}, {velocity, busIn(0), 0.25}, {bus1, kPartLevel, 0.22}, {bus1, cutoff, 0.4},
                                    {bus1, mixParam(kNoiseLevel), 0.25}, {bus1, kPartFine, 0.02}, {bus1, oscParam(0, kOscShape), -0.15},
                                    {wheel, resonance, 0.3}})}),
                   names({{0, 0, "Breath"}})});
      // A pad whose bloom you hold back: Press slows Env 2's Bloom, and the filter follows whichever
      // is higher, the bloom or your finger, so pressing can always open it further.
      p.push_back({"Held-Back Bloom", "Pressure",
                   join({osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0), osc(0, 2, kTriangle, 12, 0, 0.3),
                         {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5), a(kSkyPan, 0.6), a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 300, 0.25, 0.3), env(0, 0, 800, 1000, 1.0, 2500), shape(0, 1, "Bloom", 6000, kOnce),
                         busSet(0, 0, kMax, kLinear, kAsIs, 0, 0, 60),
                         routes(0, {{env2, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, cutoff, 0.45}, {press, tm(1), 0.12},
                                    {bus1, resonance, 0.15}, {wheel, bd(1), 0.6}})}),
                   names({{0, 0, "Bloom or Press"}})});
      // An organ whose rotor you speed with Press, on a lag of over a second like a real rotor's
      // inertia; let go and it winds down.
      p.push_back({"Pressure Rotor", "Pressure",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.5), op(0, 2, kSine, 3.0, 0.35),
                         {a(fb1, 0.06), a(kSkyPan, 0.4), a(kPartLevel, -3)}, open(0), env(0, 0, 5, 1000, 1.0, 120),
                         lfo(0, 2, kLfoSine, 0.8), lfo(0, 3, kLfoTriangle, 0.9),
                         busSet(0, 0, kSum, kS, kAsIs, 0, 0, 1400),
                         routes(0, {{press, busIn(0), 1.0}, {bus1, lfoParam(2, kLfoRate), 0.3}, {bus1, lfoParam(3, kLfoRate), 0.28},
                                    {lfo1, kPartPan, 0.3}, {lfo2, kPartLevel, 0.03}, {lfo1, kPartFine, 0.01},
                                    {bus1, tailParam(kDriveAmount), 0.3}, {wheel, fb1, 0.3}})}),
                   names({{0, 0, "Rotor"}})});
      // Glass FM that a light touch already brightens: Press on a Log curve, quick at first, opens the
      // index; the release velocity rings a shimmer as you let go.
      p.push_back({"Touch Glass", "Pressure",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSine, 5.0, 0.0),
                         {a(fm21, 0.08), a(fm32, 0.05), a(kSkyPan, 0.6)}, env(0, 0, 10, 3000, 0.6, 2500), open(0),
                         lfo(0, 2, kLfoTriangle, 3.0), busSet(0, 0, kSum, kLog, kAsIs, 0, 0, 40),
                         routes(0, {{press, busIn(0), 1.0}, {bus1, fm21, 0.35}, {bus1, fm32, 0.2}, {release, fm32, 0.3},
                                    {lfo1, oscParam(2, kOscRatio), 0.005}, {wheel, fm32, 0.3}})}),
                   names({{0, 0, "Light Touch"}})});
      // Bowed metal: noise and a comb tuned to the note. The bow is the lesser of Press and Env 2's
      // swell, so a pressed note still has to grow into its scrape, and a light one never does.
      p.push_back({"Bowed Iron", "Pressure",
                   join({op(0, 0, kSaw, 1.0, 0.25), {a(mixParam(kNoiseLevel), 0.5), a(mixParam(kNoiseColor), 0.2), a(kSkyPan, 0.5),
                          a(kPartLevel, -6)},
                         filter(0, 0, kComb, 261.6, 0.75, 1.0), filter(0, 1, kHighPass, 120, 0.0, 0.0),
                         env(0, 0, 200, 1000, 1.0, 2500), env(0, 1, 1800, 1000, 1.0, 2000),
                         busSet(0, 0, kMin, kLinear, kAsIs, 0, 0, 60),
                         routes(0, {{press, busIn(0), 1.0}, {env2, busIn(0), 1.0}, {bus1, resonance, 0.22}, {bus1, mixParam(kNoiseLevel), 0.4},
                                    {bus1, kPartLevel, 0.08}, {velocity, resonance, 0.1}, {wheel, filterParam(1, kFilterCutoff), 0.3}})}),
                   names({{0, 0, "Bow"}})});
      // Press that sinks the pitch, only at the top of its travel (Exponential), like a tape slowing
      // under a thumb; the lag keeps it from wobbling.
      p.push_back({"Thumb on the Tape", "Pressure",
                   join({osc(0, 0, kTriangle, 0, 0, 1.0), osc(0, 1, kSaw, 0, 6, 0.5), osc(0, 2, kSine, -12, 0, 0.4),
                         {a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.2), a(kDrift, 0.3), a(kSkyPan, 0.4)},
                         filter(0, 0, kLowPass, 1800, 0.1, 0.4), env(0, 0, 20, 1500, 0.8, 900),
                         lfo(0, 2, kLfoSmooth, 0.4), busSet(0, 0, kSum, kExpo, kAsIs, 0, 0, 160),
                         routes(0, {{press, busIn(0), 1.0}, {bus1, kPartFine, -0.25}, {bus1, cutoff, -0.2}, {lfo1, kPartFine, 0.02},
                                    {wheel, tailParam(kDriveAmount), 0.4}})}),
                   names({{0, 0, "Tape Drag"}})});
      // A pad with a gate you press in: Gate 8 synced on Env 2, times Press, takes the level away in
      // steps, so pressing turns a held chord into a rhythm.
      p.push_back({"Pressed Rhythm", "Pressure",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 1.0), {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5),
                          a(kSkyPan, 0.5), a(kPartLevel, -2)},
                         filter(0, 0, kLowPass, 1500, 0.25, 0.3), env(0, 0, 250, 1000, 1.0, 1200), shapeSync(0, 1, "Gate 8", kBar, kForever),
                         {a(shapeParam(1, kShapeReverse), 1)}, busSet(0, 0, kMul, kLinear, kAsIs, 0, 0, 5),
                         routes(0, {{env2, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, kPartLevel, -0.2}, {press, cutoff, 0.2},
                                    {bus1, resonance, 0.15}, {wheel, wp(1), 0.5}})}),
                   names({{0, 0, "Pressed Gate"}})});
      // Press makes the creatures restless: faster, wandering further and ringing their neighbours,
      // and how near they crowd sets the FM.
      p.push_back({"Restless Swarm", "Pressure",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSaw, 1.0, 0.3),
                         {a(fm21, 0.1), a(oscParam(2, kOscFine), 9), a(flockParam(kSpeed), 0.3), a(flockParam(kWander), 0.2),
                          a(flockParam(kTailLength), 0.8), a(kSkyPan, 0.8)},
                         filter(0, 0, kLowPass, 1600, 0.2, 0.4), env(0, 0, 400, 1000, 1.0, 2000),
                         busSet(0, 0, kSum, kS, kAsIs, 0, 0, 200),
                         routes(0, {{press, busIn(0), 1.0}, {bus1, flockParam(kSpeed), 0.5}, {bus1, flockParam(kWander), 0.6},
                                    {bus1, flockParam(kNeighbourFm), 0.6}, {nearest, fm21, 0.3}, {speed, cutoff, 0.2},
                                    {wheel, flockParam(kSeparation), 0.4}})}),
                   names({{0, 0, "Unrest"}})});
      // Hold a chord and lean in: after two seconds held, and only as hard as you press, a choir an
      // octave up fades in over three more (a Global bus, so a note added later doesn't reset it).
      p.push_back({"Patient Choir", "Pressure",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kTriangle, 0, 6, 0.8), filter(0, 0, kLowPass, 900, 0.15, 0.4),
                         {a(kSkyPan, 0.4), a(kPartLevel, -4)}, env(0, 0, 500, 1000, 1.0, 2500),
                         routes(0, {{press, cutoff, 0.2}, {wheel, cutoff, 0.2}}),
                         op(1, 0, kAltSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.0),
                         {b(kPartTranspose, 12), b(fm21, 0.1), b(flockParam(kBehaviour), 0), b(flockParam(kTunePull), 0.6),
                          b(kSkyPan, 0.8), b(kPartLevel, -40)},
                         filter(1, 0, kLowPass, 2400, 0.15, 0.4), env(1, 0, 800, 1000, 1.0, 3000), lfo(1, 2, kLfoSine, 5.0),
                         busSet(1, 0, kSum, kLog, kAsIs, 2000, 3000, 120, kAll),
                         routes(1, {{press, busIn(0), 1.0}, {bus1, kPartLevel, 0.6}, {lfo1, kPartFine, 0.03},
                                    {bus1, fm21, 0.1}})}),
                   names({{1, 0, "Patience"}})});
      // A vowel you squeeze: Press sweeps a resonant band pass from "oo" toward "ah" on an S-curve,
      // slowed a little, over a buzzing pulse.
      p.push_back({"Squeezed Vowel", "Pressure",
                   join({osc(0, 0, kPulse, 0, 0, 1.0), osc(0, 1, kSaw, 0, 5, 0.6), {a(oscParam(0, kOscShape), 0.2),
                          a(tailParam(kFilterRouting), 1), a(kSkyPan, 0.4), a(kPartLevel, -1)},
                         filter(0, 0, kBandPass, 350, 0.55, 0.3), filter(0, 1, kBandPass, 900, 0.5, 0.3), mono(0, 70),
                         env(0, 0, 60, 1000, 1.0, 300), lfo(0, 2, kLfoSine, 5.3), busSet(0, 0, kSum, kS, kAsIs, 0, 0, 90),
                         routes(0, {{press, busIn(0), 1.0}, {bus1, cutoff, 0.2}, {bus1, cutoff2, 0.15}, {lfo1, kPartFine, 0.03},
                                    {bus1, kPartLevel, 0.04}, {wheel, depth(3), 0.04}})}),
                   names({{0, 0, "Vowel"}})});
      // A split for both hands under Press: the bass below middle C drives harder, the lead above
      // grows a delayed vibrato and a brighter edge.
      p.push_back({"Pressure Split", "Pressure",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 60.0)},
                         osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kSquare, 12, 5, 0.3), filter(0, 0, kLowPass, 1600, 0.25, 0.4),
                         mono(0, 50), env(0, 0, 20, 1000, 1.0, 400), lfo(0, 2, kLfoSine, 5.6), {a(kSkyPan, 0.5), a(kPartLevel, -4)},
                         busSet(0, 0, kMul, kLinear, kAsIs, 300, 700),
                         routes(0, {{lfo1, busIn(0), 1.0}, {press, busIn(0), 1.0}, {bus1, kPartFine, 0.08}, {press, cutoff, 0.3},
                                    {wheel, cutoff, 0.2}}),
                         osc(1, 0, kSaw, 0, 0, 1.0), osc(1, 1, kSine, -12, 0, 0.7), filter(1, 0, kLadder, 300, 0.35, 0.3),
                         {b(kPartTranspose, 0), b(flockParam(kBehaviour), 0), b(tailParam(kDriveType), 2), b(kPartLevel, -2)},
                         mono(1, 0), env(1, 0, 2, 600, 0.9, 120), busSet(1, 0, kSum, kExpo, kAsIs, 0, 0, 30),
                         routes(1, {{press, busIn(0), 1.0}, {bus1, tailParam(kDriveAmount), 0.6}, {bus1, cutoff, 0.3},
                                    {wheel, resonance, 0.3}})}),
                   names({{0, 0, "Lead Vibrato"}, {1, 0, "Bass Drive"}})});
      // A wah under the finger: Press sweeps a resonant low pass on an S-curve with a short lag,
      // over a clavinet-ish pulse.
      p.push_back({"Finger Wah", "Pressure",
                   join({osc(0, 0, kPulse, 0, 0, 1.0), osc(0, 1, kSaw, 12, 0, 0.3), {a(oscParam(0, kOscShape), 0.15), a(kSkyPan, 0.4)},
                         filter(0, 0, kLowPass, 350, 0.65, 0.4), env(0, 0, 2, 900, 0.6, 250),
                         busSet(0, 0, kSum, kS, kAsIs, 0, 0, 50),
                         routes(0, {{press, busIn(0), 1.0}, {velocity, busIn(0), 0.2}, {bus1, cutoff, 0.5}, {bus1, resonance, 0.1},
                                    {wheel, resonance, 0.2}})}),
                   names({{0, 0, "Wah"}})});
      // A pluck you can hold: let it ring and it dies away as a pluck; press and it sustains, the
      // harder the longer and louder.
      p.push_back({"Holdable Pluck", "Pressure",
                   join({osc(0, 0, kSaw, 0, -3, 1.0), osc(0, 1, kSquare, 0, 3, 0.4), filter(0, 0, kLowPass, 600, 0.3, 0.5),
                         {a(kSkyPan, 0.5)}, env(0, 0, 1, 700, 0.0, 400), env(0, 1, 1, 400, 0.0, 300),
                         busSet(0, 0, kSum, kLog, kAsIs, 0, 0, 60),
                         routes(0, {{env2, cutoff, 0.4}, {press, busIn(0), 1.0}, {bus1, envParam(0, kEnvSustain), 0.8},
                                    {bus1, envParam(1, kEnvSustain), 0.3}, {velocity, cutoff, 0.15}, {wheel, resonance, 0.3}})}),
                   names({{0, 0, "Hold"}})});
      // Tides you push: a looped Surge breathes the filter, and Press slows the tide while lifting it,
      // so leaning in stretches each swell.
      p.push_back({"Pushing the Tide", "Pressure",
                   join({osc(0, 0, kSaw, 0, -6, 0.9), osc(0, 1, kSaw, -12, 6, 0.8), {a(mixParam(kNoiseLevel), 0.15),
                          a(mixParam(kNoiseColor), -0.5), a(kSkyPan, 0.5), a(kPartLevel, -3)},
                         filter(0, 0, kLadder, 350, 0.35, 0.3), env(0, 0, 1200, 1000, 1.0, 3500), shape(0, 1, "Surge", 7000, kForever),
                         busSet(0, 0, kSum, kLinear, kAsIs, 0, 0, 500),
                         routes(0, {{press, busIn(0), 1.0}, {env2, cutoff, 0.35}, {bus1, tm(1), 0.12}, {bus1, bd(1), 0.6},
                                    {env2, mixParam(kNoiseLevel), 0.3}, {wheel, resonance, 0.3}})}),
                   names({{0, 0, "Push"}})});
      // A bass you dig into: Press bends the filter's resonance up and a sub in, and the drive only
      // bites where Press and the note's velocity agree.
      p.push_back({"Dig-In Bass", "Pressure",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSine, -12, 0, 0.3), filter(0, 0, kLadder, 260, 0.3, 0.3),
                         {a(tailParam(kDriveType), 1)}, mono(0, 30), env(0, 0, 2, 400, 0.8, 120), env(0, 1, 1, 260, 0.2, 120),
                         busSet(0, 0, kMin, kLinear, kAsIs, 0, 0, 20),
                         routes(0, {{env2, cutoff, 0.35}, {press, resonance, 0.35}, {press, mixParam(kMixLevel, 1), 0.5},
                                    {press, busIn(0), 1.0}, {velocity, busIn(0), 1.0}, {bus1, tailParam(kDriveAmount), 0.6},
                                    {wheel, cutoff, 0.2}})}),
                   names({{0, 0, "Bite"}})});

      // ---- Drones, with the buses ----
      // A drone that catches fire the longer it is held: three seconds in, its drive and FM fade up
      // over eight more, a Global bus so the whole chord ignites as one.
      p.push_back({"Slow Ignition", "Drones",
                   join({osc(0, 0, kSaw, 0, -7, 0.8), osc(0, 1, kSaw, -12, 7, 0.8), op(0, 2, kSine, 2.0, 0.0),
                         {a(fmCell(2, 0), 0.0), a(tailParam(kDriveType), 2), a(kDrift, 0.2), a(kSkyPan, 0.4), a(kPartLevel, -4)},
                         filter(0, 0, kLadder, 260, 0.3, 0.3), env(0, 0, 3000, 1000, 1.0, 5000), shape(0, 1, "Wander", 21000, kForever),
                         busSet(0, 0, kSum, kS, kAsIs, 3000, 8000, 0, kAll), {a(busParam(0, kBusOffset), 1.0)},
                         routes(0, {{bus1, tailParam(kDriveAmount), 0.45}, {bus1, fm31, 0.2}, {bus1, cutoff, 0.3}, {env2, cutoff, 0.2},
                                    {press, resonance, 0.25}, {wheel, busIn(0), -1.0}})}),
                   names({{0, 0, "Ignition"}})});
      // A singing bowl: a comb rung by noise, a shimmer moving its ring, and the creatures pulling the
      // chord's notes into just intervals as they fly together.
      p.push_back({"Singing Bowls", "Drones",
                   join({op(0, 0, kSine, 1.0, 0.5), op(0, 1, kSine, 2.76, 0.12), {a(mixParam(kNoiseLevel), 0.35),
                          a(mixParam(kNoiseColor), -0.6), a(flockParam(kTunePull), 0.9), a(flockParam(kCohesion), 0.7), a(kSkyPan, 0.7)},
                         filter(0, 0, kComb, 261.6, 0.85, 1.0), env(0, 0, 2500, 1000, 1.0, 6000),
                         shape(0, 1, "Shimmer", 9000, kForever), shape(0, 2, "Breath", 15000, kForever),
                         busSet(0, 0, kSum, kLinear, kAsIs, 0, 0, 600),
                         routes(0, {{env2, busIn(0), 0.5}, {nearest, busIn(0), 0.5}, {bus1, resonance, 0.12}, {env3, mixParam(kMixLevel, 1), 0.3},
                                    {env3, kPartPan, 0.25}, {press, mixParam(kNoiseLevel), 0.3}, {wheel, resonance, 0.1}})}),
                   names({{0, 0, "Ring"}})});
      // Clouds that move as one: a Brownian walk on Env 3 sets the colour of every note at once (a
      // Global bus), so the chord drifts together instead of each note on its own.
      p.push_back({"One Sky", "Drones",
                   join({osc(0, 0, kSaw, 0, -8, 0.9), osc(0, 1, kPulse, 0, 8, 0.7), osc(0, 2, kSine, 12, 0, 0.2),
                         {a(oscParam(1, kOscShape), 0.3), a(kDrift, 0.15), a(kSkyPan, 0.6), a(kPartLevel, -4)},
                         filter(0, 0, kLowPass, 600, 0.3, 0.3), env(0, 0, 2000, 1000, 1.0, 5000), shape(0, 2, "Brownian", 27000, kForever),
                         busSet(0, 0, kSum, kS, kToBi, 0, 0, 2000, kAll),
                         routes(0, {{env3, busIn(0), 1.0}, {bus1, cutoff, 0.3}, {bus1, oscParam(1, kOscShape), 0.3}, {bus1, kPartPan, 0.3},
                                    {press, cutoff, 0.2}, {wheel, resonance, 0.3}})}),
                   names({{0, 0, "Weather"}})});
      // Beating that breathes: two slow LFOs multiplied make a level that swells only where both are
      // up, so the drone's pulse comes and goes on its own.
      p.push_back({"Twin Pulse", "Drones",
                   join({osc(0, 0, kSaw, 0, -3, 0.8), osc(0, 1, kSaw, 7, 3, 0.6), osc(0, 2, kSaw, -12, 0, 0.6),
                         {a(lfoParam(2, kLfoPolarity), 1), a(lfoParam(3, kLfoPolarity), 1), a(kSkyPan, 0.5), a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 800, 0.3, 0.3), env(0, 0, 1500, 1000, 1.0, 4000),
                         lfo(0, 2, kLfoSine, 0.23), lfo(0, 3, kLfoTriangle, 0.37), busSet(0, 0, kMul, kS, kAsIs, 0, 0, 200),
                         routes(0, {{lfo1, busIn(0), 1.0}, {lfo2, busIn(0), 1.0}, {bus1, kPartLevel, 0.1}, {bus1, cutoff, 0.35},
                                    {bus1, mixParam(kMixLevel, 1), 0.4}, {press, resonance, 0.3}, {wheel, lfoParam(2, kLfoRate), 0.2}})}),
                   names({{0, 0, "Both Up"}})});
      // A deep drone that only opens where two slow shapes both rise: the filter follows the lesser of
      // a 17 s Surge and a 23 s Plateau, so it opens rarely and briefly.
      p.push_back({"Rare Light", "Drones",
                   join({osc(0, 0, kSaw, -12, -5, 0.9), osc(0, 1, kSaw, 0, 5, 0.7), osc(0, 2, kSquare, 7, 0, 0.25),
                         {a(kDrift, 0.2), a(kSkyPan, 0.4), a(kPartLevel, -4)},
                         filter(0, 0, kLadder, 220, 0.4, 0.3), env(0, 0, 2500, 1000, 1.0, 6000),
                         shape(0, 1, "Surge", 17000, kForever), shape(0, 2, "Plateau", 23000, kForever),
                         busSet(0, 0, kMin, kExpo, kAsIs, 0, 0, 400),
                         routes(0, {{env2, busIn(0), 1.0}, {env3, busIn(0), 1.0}, {bus1, cutoff, 0.5}, {bus1, mixParam(kMixLevel, 2), 0.5},
                                    {env2, kPartPan, 0.2}, {press, cutoff, 0.2}, {wheel, busIn(0), 0.5}})}),
                   names({{0, 0, "Both Rising"}})});
      // A drone of rust: grit comes from the greater of a per-note random and a flickering shape, so
      // each note of the chord crackles in its own way.
      p.push_back({"Rust", "Drones",
                   join({osc(0, 0, kSaw, 0, -6, 0.8), osc(0, 1, kSquare, -12, 6, 0.6), {a(mixParam(kNoiseLevel), 0.1),
                          a(tailParam(kDriveType), 1), a(kDrift, 0.25), a(kSkyPan, 0.5), a(kPartLevel, -6)},
                         filter(0, 0, kLowPass, 500, 0.4, 0.3), env(0, 0, 1800, 1000, 1.0, 4500), shape(0, 1, "Flicker", 6000, kForever),
                         busSet(0, 0, kMax, kLinear, kToUni, 0, 0, 80),
                         routes(0, {{random, busIn(0), 0.7}, {env2, busIn(0), 1.0}, {bus1, tailParam(kDriveAmount), 0.5},
                                    {bus1, mixParam(kNoiseLevel), 0.25}, {bus1, cutoff, 0.15}, {press, cutoff, 0.25}, {wheel, resonance, 0.3}})}),
                   names({{0, 0, "Grit"}})});
      // Layer: a horizon. A holds the low fifth; B's high sines arrive four seconds into the chord and
      // fade in over six (a Global bus), breathing slowly once they're there.
      p.push_back({"Far Horizon", "Drones",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, 0, -6, 0.8), osc(0, 1, kSaw, 7, 6, 0.6), filter(0, 0, kLowPass, 500, 0.2, 0.3),
                         {a(kSkyPan, 0.3), a(kDrift, 0.15), a(kPartLevel, -5)}, env(0, 0, 2500, 1000, 1.0, 5000),
                         shape(0, 1, "Arch", 19000, kForever), routes(0, {{env2, cutoff, 0.3}, {press, cutoff, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 3.0, 0.0),
                         {b(kPartTranspose, 24), b(fm21, 0.06), b(flockParam(kBehaviour), 0), b(flockParam(kTunePull), 0.5),
                          b(kSkyPan, 0.9), b(kPartLevel, -40)},
                         env(1, 0, 1000, 1000, 1.0, 6000), open(1), shape(1, 1, "Breath", 11000, kForever),
                         busSet(1, 0, kSum, kS, kAsIs, 4000, 6000, 0, kAll), {b(busParam(0, kBusOffset), 1.0)},
                         routes(1, {{bus1, kPartLevel, 0.5}, {env2, fm21, 0.15}, {env2, kPartLevel, 0.06}, {wheel, fm21, 0.2}})}),
                   names({{1, 0, "Arrival"}})});
      // A choir of creatures that drone: Nearest and Heading blended, slowed, set each voice's
      // vowel, and Tune Pull holds the cluster in just tuning.
      p.push_back({"Drone Swarm", "Drones",
                   join({osc(0, 0, kPulse, 0, -5, 0.9), osc(0, 1, kSaw, 0, 5, 0.6), {a(oscParam(0, kOscShape), 0.3),
                          a(tailParam(kFilterRouting), 1), a(flockParam(kTunePull), 0.8), a(flockParam(kSpeed), 0.25),
                          a(flockParam(kTailLength), 0.9), a(kSkyPan, 0.8), a(kPartLevel, -1)},
                         filter(0, 0, kBandPass, 500, 0.5, 0.3), filter(0, 1, kBandPass, 1300, 0.45, 0.3),
                         env(0, 0, 2000, 1000, 1.0, 5000), busSet(0, 0, kSum, kS, kAsIs, 0, 0, 900),
                         routes(0, {{nearest, busIn(0), 0.6}, {heading, busIn(0), 0.6}, {bus1, cutoff, 0.2}, {bus1, cutoff2, -0.15},
                                    {turn, kPartPan, 0.3}, {press, flockParam(kSpeed), 0.3}, {wheel, resonance, 0.2}})}),
                   names({{0, 0, "Vowel"}})});

      // ---- Textures, with the buses ----
      // Rain on a window: each note's amp is Sparkle, looping, its pace set per note at random, its
      // brightness and pan by the drops; Press brings a downpour.
      p.push_back({"Window Rain", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 5.03, 0.0), {a(fm21, 0.2), a(mixParam(kNoiseLevel), 0.15),
                          a(mixParam(kNoiseColor), 0.4), a(flockParam(kBehaviour), kScatter), a(kSkyPan, 0.9)},
                         open(0), shape(0, 0, "Sparkle", 3000, kForever), {a(envParam(0, kEnvRelease), timeValue(1500))},
                         shape(0, 2, "Flicker", 2000, kForever), busSet(0, 0, kSum, kLinear, kAsIs, 0, 0, 0),
                         routes(0, {{random, tm(0), 0.08}, {env3, fm21, 0.3}, {random, kPartPan, 0.5}, {press, busIn(0), 1.0},
                                    {bus1, tm(0), -0.1}, {bus1, mixParam(kNoiseLevel), 0.3}, {wheel, fm21, 0.2}})}),
                   names({{0, 0, "Downpour"}})});
      // Woven steps: Random 16 on Env 2 in time with the host, held through a short lag so each step
      // glides, moving pitch and filter; Press thickens the weave.
      p.push_back({"Gliding Weave", "Textures",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kTriangle, 12, 0, 0.5), {a(kSkyPan, 0.6), a(kPartLevel, -2)},
                         filter(0, 0, kLowPass, 1200, 0.4, 0.4), env(0, 0, 100, 1000, 1.0, 1200),
                         shapeSync(0, 1, "Random 16", kBars2, kForever), busSet(0, 0, kSum, kLinear, kToBi, 0, 0, 70),
                         routes(0, {{env2, busIn(0), 1.0}, {bus1, cutoff, 0.3}, {bus1, oscParam(1, kOscFine), 0.15},
                                    {bus1, kPartPan, 0.3}, {press, resonance, 0.3}, {wheel, mixParam(kMixLevel, 1), 0.4}})}),
                   names({{0, 0, "Gliding Steps"}})});
      // A swarm of grains: Saw Train on the amp, its rate set by each creature's speed and its pitch
      // smeared by how near its neighbour is.
      p.push_back({"Grain Swarm", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), op(0, 2, kSaw, 1.0, 0.3),
                         {a(fm21, 0.2), a(flockParam(kSpeed), 0.6), a(flockParam(kWander), 0.7), a(flockParam(kTailLength), 0.4),
                          a(kSkyPan, 0.9)},
                         filter(0, 0, kLowPass, 2500, 0.2, 0.4), shape(0, 0, "Saw Train", 900, kForever),
                         {a(envParam(0, kEnvRelease), timeValue(800))}, busSet(0, 0, kSum, kS, kAsIs, 0, 0, 120),
                         routes(0, {{speed, busIn(0), 1.0}, {bus1, tm(0), -0.12}, {nearest, oscParam(2, kOscFine), 0.15},
                                    {nearest, fm21, 0.2}, {turn, kPartPan, 0.4}, {press, flockParam(kWander), 0.3}, {wheel, fm21, 0.3}})}),
                   names({{0, 0, "Grain Rate"}})});
      // Wind chimes: bells that ring on their own on a looping Bell shape, the wind pushing the
      // creatures hard; how fast the wind carries each sets how often it strikes.
      p.push_back({"Wind Chimes", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 5.4, 0.15),
                         {a(fm21, 0.12), a(lfoParam(1, kLfoPush), 1.0), a(lfoParam(1, kLfoRate), lfoValue(0.25)),
                          a(flockParam(kBehaviour), kScatter), a(flockParam(kWander), 0.5), a(kSkyPan, 0.9), a(kPartLevel, -2)},
                         open(0), shape(0, 0, "Bell", 2600, kForever), {a(envParam(0, kEnvRelease), timeValue(2500))},
                         busSet(0, 0, kSum, kS, kAsIs, 0, 0, 300),
                         routes(0, {{speed, busIn(0), 1.0}, {random, busIn(0), 0.3}, {bus1, tm(0), -0.12}, {bus1, fm21, 0.2},
                                    {random, kPartPan, 0.4}, {press, lfoParam(1, kLfoPush), 0.3}, {wheel, fm21, 0.25}})}),
                   names({{0, 0, "Wind"}})});
      // Insects on a summer night: high FM chirps on Stutter, the wingbeat bobbing each creature, and
      // its turning bending the chirp.
      p.push_back({"Night Insects", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.48, 0.0), {a(kPartTranspose, 24), a(fm21, 0.3),
                          a(lfoParam(0, kLfoPush), 0.8), a(lfoParam(0, kLfoRate), lfoValue(11.0)), a(flockParam(kBehaviour), kScatter),
                          a(flockParam(kSpeed), 0.7), a(kSkyPan, 1.0), a(kPartLevel, -8)},
                         open(0), shape(0, 0, "Stutter", 700, kForever), {a(envParam(0, kEnvRelease), timeValue(300))},
                         busSet(0, 0, kSum, kLinear, kAsIs, 0, 0, 20),
                         routes(0, {{turn, busIn(0), 1.0}, {bus1, kPartFine, 0.3}, {bus1, fm21, 0.2}, {random, tm(0), 0.1},
                                    {speed, kPartLevel, 0.08}, {press, fm21, 0.3}, {wheel, tm(0), -0.1}})}),
                   names({{0, 0, "Chirp Bend"}})});
      // A clock shop: each note a clock on its own Heartbeat, its pace set by its key (low clocks
      // slow, high ones quick), its tick brightened by Press.
      p.push_back({"Clock Shop", "Textures",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.2, 0.0), {a(fm21, 0.3), a(kSkyPan, 0.8)},
                         open(0), shape(0, 0, "Heartbeat", 1100, kForever), {a(envParam(0, kEnvRelease), timeValue(500))},
                         env(0, 2, 0.5, 40, 0.0, 30), busSet(0, 0, kSum, kLinear, kAsIs, 0, 0, 0),
                         routes(0, {{key, busIn(0), 1.0}, {bus1, tm(0), -0.15}, {bus1, kPartPan, 0.4}, {env3, fm21, 0.3},
                                    {press, fm21, 0.3}, {wheel, bd(0), 0.6}})}),
                   names({{0, 0, "Clock Pace"}})});
      // Ghosts in the tape: a slow random wow and a faster flutter, summed and lagged into one wobble
      // on pitch and filter, deeper with Press.
      p.push_back({"Tape Ghosts", "Textures",
                   join({osc(0, 0, kTriangle, 0, 0, 1.0), osc(0, 1, kSine, 12, 0, 0.3), {a(mixParam(kNoiseLevel), 0.06),
                          a(mixParam(kNoiseColor), -0.2), a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.2), a(kSkyPan, 0.4)},
                         filter(0, 0, kLowPass, 1500, 0.15, 0.4), env(0, 0, 300, 1000, 1.0, 2500),
                         lfo(0, 2, kLfoSmooth, 0.35), lfo(0, 3, kLfoSine, 7.0), busSet(0, 0, kSum, kLinear, kAsIs, 0, 0, 60),
                         busSet(0, 1, kMul, kLinear, kAsIs, 0, 0, 20),
                         routes(0, {{lfo1, busIn(0), 0.8}, {lfo2, busIn(0), 0.15}, {bus1, busIn(1), 1.0}, {press, busIn(1), 1.0},
                                    {bus1, kPartFine, 0.03}, {bus2, kPartFine, 0.08}, {bus1, cutoff, 0.05}, {wheel, mixParam(kNoiseLevel), 0.3}})}),
                   names({{0, 0, "Wow and Flutter"}, {0, 1, "Pressed Wobble"}})});
      // A field of embers: noise through a comb, the crackle on Sparkle, and the hottest embers (the
      // greater of Press and a slow Gusts shape) ringing brightest.
      p.push_back({"Ember Glow", "Textures",
                   join({op(0, 0, kSine, 1.0, 0.0), {a(mixParam(kNoiseLevel), 0.7), a(mixParam(kNoiseColor), -0.3),
                          a(flockParam(kBehaviour), kScatter), a(kSkyPan, 0.9), a(kPartLevel, -2)},
                         filter(0, 0, kComb, 261.6, 0.7, 1.0), filter(0, 1, kLowPass, 3000, 0.1, 0.3),
                         shape(0, 0, "Sparkle", 5000, kForever), {a(envParam(0, kEnvRelease), timeValue(3000))},
                         shape(0, 1, "Gusts", 12000, kForever), busSet(0, 0, kMax, kLinear, kAsIs, 0, 0, 100),
                         routes(0, {{press, busIn(0), 1.0}, {env2, busIn(0), 0.8}, {bus1, resonance, 0.2}, {bus1, cutoff2, 0.3},
                                    {random, kPartPan, 0.5}, {random, tm(0), 0.08}, {wheel, mixParam(kNoiseColor), 0.4}})}),
                   names({{0, 0, "Heat"}})});

      // Each preset's level, matched by measurement so stepping through the bank doesn't jump: the
      // loudest 400 ms of a held three-note chord at velocity 0.8 (one note for the mono and bass
      // patches) sits near -20 dBFS, a little lower for the plucks and lower again for the hits,
      // which are heard by their peaks. A trim raises or lowers every part's Level together, keeping a
      // layer's balance, and spills into the Master Level past the parts' +6 dB.
      static const std::pair<const char *, double> trims[] = {
         {"Sub Bass", 2.0},
         {"Analog Bass", 4.0},
         {"FM Bass", 5.0},
         {"Acid Bass", 7.0},
         {"Reese Bass", 5.5},
         {"Saw Lead", 0.5},
         {"Sync Lead", 7.0},
         {"Pressure Lead", 2.5},
         {"Tine Piano", -1.0},
         {"Clav", 4.0},
         {"Tonewheel Organ", 4.0},
         {"Vibraphone", -2.0},
         {"Tubular Bells", -2.5},
         {"Synth Pluck", 0.5},
         {"Nylon Guitar", 3.0},
         {"Kalimba", 2.0},
         {"Synth Brass", -2.0},
         {"Solo Cello", 8.0},
         {"Warm Analog", 0.5},
         {"Glass Pad", -2.5},
         {"Shifting Glass", -3.5},
         {"Random Garden", 2.5},
         {"Pulsar", 6.0},
         {"Clockwork", -2.0},
         {"MPE Voices", 2.5},
         {"Tide Machine", -0.5},
         {"Basalt", 3.0},
         {"Breathing Organ", -1.5},
         {"Cathedral Air", -5.0},
         {"Undertow", -2.0},
         {"Long Bloom", -1.0},
         {"Glacier", -2.0},
         {"Iron Hum", 1.5},
         {"Tidal Pulse", 0.5},
         {"Rain Glass", 0.5},
         {"Bouncing Light", -2.5},
         {"Echo Chamber", 4.5},
         {"Stutter Fields", 0.5},
         {"Number Station", 2.0},
         {"Stairwell", -1.5},
         {"Breathing Machine", -0.5},
         {"Night Pulse", 0.5},
         {"Falling Bells", -3.5},
         {"Tug of War", 1.0},
         {"Quickening", -3.5},
         {"Lingering Breath", 2.5},
         {"Mutual Orbit", -3.5},
         {"Tilting Steps", -0.5},
         {"Gathering Storm", 1.0},
         {"Ember Field", 2.5},
         {"Glass Conversation", 0.5},
         {"Low and High Weather", -0.5},
         {"Calm to Unrest", -1.0},
         {"Between Worlds", -2.5},
         {"Glass and Comets", 2.0},
         {"Turning Choir", 1.5},
         {"Clockwork Garden", 1.0},
         {"Distant Engine", 1.0},
         {"Piano and Strings", -1.0},
         {"Bass and Keys Split", 4.5},
         {"Fireflies", 5.0},
         {"Metal Rain", 4.0},
         {"Starling Bells", -0.5},
         {"Dripping Ink", 4.0},
         {"Ink Ribbons", 3.5},
         {"Murmuration Pad", -0.5},
         {"Pressure Front", 2.0},
         {"Hunter Clicks", 3.5},
         {"Mallet Flock", -2.0},
         {"Shark Bass", 2.0},
         {"Predator and Prey", -0.5},
         {"Murmuration Riot", 4.5},
         {"Murmuration Storm", 0.5},
         {"Concert Grand", 0.5},
         {"Wurli Drive", 4.5},
         {"Talk Box", 18.0},
         {"Solo Violin", 5.5},
         {"Breath Sax", 7.5},
         {"Accordion", -7.5},
         {"Bending Lead", 10.5},
         {"Pressure Growl", 5.0},
         {"Bloom Bells", 1.0},
         {"Tension Wire", 8.0},
         {"Squeeze Organ", 2.0},
         {"Wilting Pad", -0.5},
         {"Flutter Pad", -7.0},
         {"Pressure Metal", 2.0},
         {"Morph Pad", -4.5},
         {"Press Gate Pad", -3.5},
         {"Swell Brass", -2.5},
         {"Foggy EP", 1.5},
         {"Genetic Lead", 6.0},
         {"Monsta Voices", 5.0},
         {"Fifty Pad", -5.0},
         {"Kingdome", 7.0},
         {"Therapy Strings", -7.5},
         {"Nightmare", 0.5},
         {"Quack Bass", 9.0},
         {"Gristle Lead", 10.0},
         {"Cumulus", -7.0},
         {"Old Hills", -4.0},
         {"Sunshine Strings", -5.5},
         {"Worn Arp", 2.5},
         {"Waxxy", -1.0},
         {"Reptile", 3.5},
         {"Crystal Strings", -3.5},
         {"Softy Drone", 0.0},
         {"Ceasefire", -6.5},
         {"Chamber Strings", -2.0},
         {"Fresh Air", -4.5},
         {"Clang Heart", 4.0},
         {"Space God", -1.0},
         {"Opera Pad", 0.5},
         {"Ice Bells", -1.0},
         {"Oldfield Lead", 4.5},
         {"Pink Haze", -2.5},
         {"Gathering Voices", -4.0},
         {"Hunting Grounds", 7.0},
         {"Flight Steps", -3.0},
         {"Creature Theremin", 1.0},
         {"Scatter Glass", -2.0},
         {"Shoal Current", -6.0},
         {"Circling Hawks", 5.0},
         {"Crowd Swing", -1.5},
         {"Turning Strings", 2.5},
         {"Two Flocks Talking", 0.0},
         {"Wingbeat Tremolo", -2.0},
         {"Panic Bells", 2.0},
         {"Flock Organ", 2.5},
         {"Ink Song", 14.5},
         {"Lean-In Vibrato", 1.0},
         {"Blown Reed", 7.5},
         {"Held-Back Bloom", -1.0},
         {"Pressure Rotor", 1.5},
         {"Touch Glass", -2.5},
         {"Bowed Iron", -3.0},
         {"Thumb on the Tape", 0.5},
         {"Pressed Rhythm", -0.5},
         {"Restless Swarm", -4.0},
         {"Patient Choir", 1.0},
         {"Squeezed Vowel", 13.5},
         {"Pressure Split", 7.5},
         {"Finger Wah", -0.5},
         {"Holdable Pluck", 1.5},
         {"Pushing the Tide", 0.0},
         {"Dig-In Bass", 4.5},
         {"Slow Ignition", 4.5},
         {"Singing Bowls", -10.0},
         {"One Sky", -0.5},
         {"Twin Pulse", -5.5},
         {"Rare Light", 4.5},
         {"Rust", 7.0},
         {"Far Horizon", 2.0},
         {"Drone Swarm", 11.0},
         {"Window Rain", 6.5},
         {"Gliding Weave", -1.5},
         {"Grain Swarm", 2.0},
         {"Wind Chimes", 5.5},
         {"Night Insects", 7.0},
         {"Clock Shop", 9.0},
         {"Tape Ghosts", 0.5},
         {"Ember Glow", 7.5},
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
      static const char *const kCategoryOrder[] = {"Init",   "Essentials", "Flock",      "Pressure",   "Expressive", "Drones",
                                                   "Textures", "Motion",   "Interplay", "Landscapes", "Monsta"};
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
