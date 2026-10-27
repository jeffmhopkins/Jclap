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
      constexpr int kBar = 4, kBars2 = 5, kBars4 = 7, kBars8 = 9, kBars16 = 11;
      constexpr int kScatter = 1, kHunt = 2, kFlee = 3;
      constexpr int kSine = 0, kTriangle = 1, kSaw = 2, kSquare = 3, kPulse = 4, kHalfSine = 5, kAltSine = 8;
      constexpr int kRatio = 1;
      constexpr int kLowPass = 0, kHighPass = 1, kBandPass = 2, kNotch = 3, kLadder = 5, kComb = 6;
      constexpr int kLfoSine = 0, kLfoTriangle = 1, kLfoSaw = 2, kLfoSquare = 3, kLfoHold = 4, kLfoSmooth = 5;
      constexpr int k16th = 2, k8th = 5, k8thDotted = 7, kHalf = 11;
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

      // The bank runs by instrument family, as a keyboard player looks for a sound. Within each, from
      // a plain patch to ones with more going on. Most answer the mod wheel (vibrato, brightness or
      // motion) and Press (channel or MPE pressure), so they play from any keyboard. The last three
      // categories are the showcases: patches that move on their own, both parts at once, and the
      // flock playing the sound.

      // ---- Bass ----
      p.push_back({"Sub Bass", "Bass",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.12), {a(fb1, 0.1), a(kVelocityToAmp, 0.4)},
                         mono(0, 0), env(0, 0, 2, 300, 1.0, 80), open(0), routes(0, {{wheel, fb1, 0.3}})})});
      p.push_back({"Analog Bass", "Bass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSquare, -12, 0, 0.6), filter(0, 0, kLadder, 260, 0.25, 0.4),
                         mono(0, 25), env(0, 0, 1, 300, 0.85, 90), env(0, 1, 1, 260, 0.15, 120),
                         {a(envParam(1, kEnvVelocity), 0.5)}, routes(0, {{env2, cutoff, 0.35}, {wheel, cutoff, 0.3}})})});
      // A DX bass: a 1:1 pair, the modulator's feedback for the growl, its depth dying away fast.
      p.push_back({"FM Bass", "Bass",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fm21, 0.22), a(fb2, 0.3)}, mono(0, 0),
                         env(0, 0, 1, 400, 0.9, 70), env(0, 2, 0.5, 250, 0.15, 100), open(0),
                         routes(0, {{env3, fm21, 0.35}, {velocity, fm21, 0.2}, {wheel, fb2, 0.3}})})});
      p.push_back({"Funk Bass", "Bass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 6, 0.6), {a(oscParam(1, kOscShape), 0.3)},
                         filter(0, 0, kLowPass, 150, 0.3, 0.3), mono(0, 0), env(0, 0, 1, 500, 0.5, 90),
                         env(0, 1, 1, 180, 0.0, 100), {a(envParam(1, kEnvVelocity), 0.6)},
                         routes(0, {{env2, cutoff, 0.5}, {wheel, cutoff, 0.2}})})});
      // Slide notes into each other for the glide; harder notes accent the filter.
      p.push_back({"Acid Bass", "Bass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), filter(0, 0, kLadder, 220, 0.72, 0.3), {a(filterParam(0, kFilterDrive), 0.35)},
                         mono(0, 70), env(0, 0, 1, 300, 0.8, 60), env(0, 1, 1, 220, 0.0, 80), {a(envParam(1, kEnvVelocity), 0.5)},
                         routes(0, {{env2, cutoff, 0.45}, {velocity, cutoff, 0.12}, {wheel, resonance, 0.25}, {slide, cutoff, 0.3}})})});
      p.push_back({"Reese Bass", "Bass",
                   join({osc(0, 0, kSaw, 0, -11, 1.0), osc(0, 1, kSaw, 0, 11, 1.0), osc(0, 2, kSine, -12, 0, 0.6),
                         filter(0, 0, kLowPass, 500, 0.1, 0.4), {a(tailParam(kDriveAmount), 0.35)}, lfo(0, 3, kLfoTriangle, 0.25),
                         mono(0, 40), env(0, 0, 3, 400, 1.0, 120),
                         routes(0, {{lfo2, oscParam(1, kOscFine), 0.04}, {wheel, cutoff, 0.35}, {press, cutoff, 0.2}})})});
      // An eighth-note wobble at the host's tempo; the mod wheel deepens it.
      p.push_back({"Wobble Bass", "Bass",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSquare, -12, 0, 0.7), filter(0, 0, kLadder, 150, 0.45, 0.3),
                         {a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.4), a(lfoParam(2, kLfoPolarity), 1)},
                         lfoSync(0, 2, kLfoSine, k8th), mono(0, 30), env(0, 0, 2, 300, 1.0, 100),
                         routes(0, {{lfo1, cutoff, 0.3}, {wheel, depth(0), 0.1}, {wheel, tailParam(kDriveAmount), 0.3}})})});

      // ---- Leads ----
      // The vibrato waits 350 ms, and the mod wheel deepens it.
      p.push_back({"Saw Lead", "Leads",
                   join({osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 0.85), filter(0, 0, kLowPass, 2800, 0.2, 0.5),
                         lfo(0, 2, kLfoSine, 5.5), {a(lfoParam(2, kLfoDelay), timeValue(350))}, mono(0, 45),
                         env(0, 0, 3, 300, 1.0, 180), env(0, 1, 2, 400, 0.5, 200),
                         routes(0, {{env2, cutoff, 0.15}, {press, cutoff, 0.25}, {lfo1, kPartFine, 0.03}, {wheel, depth(2), 0.04},
                                    {velocity, cutoff, 0.1}})})});
      p.push_back({"Square Lead", "Leads",
                   join({osc(0, 0, kSquare, 0, 0, 1.0), osc(0, 1, kSquare, -12, 4, 0.5), filter(0, 0, kLowPass, 2200, 0.15, 0.5),
                         {a(filterParam(0, kFilterSlope), 0)}, lfo(0, 2, kLfoSine, 5.2), lfo(0, 3, kLfoTriangle, 0.35), mono(0, 60),
                         env(0, 0, 3, 300, 1.0, 160),
                         routes(0, {{lfo2, oscParam(0, kOscShape), 0.15}, {lfo1, kPartFine, 0.0}, {wheel, depth(1), 0.04},
                                    {press, cutoff, 0.25}})})});
      // Osc 1 is silent and only resets Osc 2, whose pitch Env 2, the wheel and Press sweep.
      p.push_back({"Sync Lead", "Leads",
                   join({osc(0, 0, kSine, 0, 0, 0.0), osc(0, 1, kSaw, 7, 0, 1.0), {a(oscParam(1, kOscSync), 1), a(kPartLevel, -2)},
                         filter(0, 0, kLowPass, 6000, 0.1, 0.3), lfo(0, 2, kLfoSine, 5.5), {a(lfoParam(2, kLfoDelay), timeValue(400))},
                         mono(0, 40), env(0, 0, 2, 300, 1.0, 200), env(0, 1, 1, 700, 0.25, 300),
                         routes(0, {{env2, oscParam(1, kOscCoarse), 0.12}, {wheel, oscParam(1, kOscCoarse), 0.12},
                                    {press, oscParam(1, kOscCoarse), 0.1}, {lfo1, kPartFine, 0.02}})})});
      // A breath of noise at the start and under Press.
      p.push_back({"Soft Flute", "Leads",
                   join({osc(0, 0, kSine, 0, 0, 1.0), osc(0, 1, kTriangle, 12, 0, 0.15), {a(mixParam(kNoiseLevel), 0.05), a(mixParam(kNoiseColor), 0.3)},
                         filter(0, 0, kLowPass, 3500, 0.1, 0.5), {a(filterParam(0, kFilterSlope), 0)}, lfo(0, 2, kLfoSine, 5.0),
                         {a(lfoParam(2, kLfoDelay), timeValue(500))}, mono(0, 30), env(0, 0, 70, 300, 0.9, 200),
                         env(0, 2, 1, 120, 0.0, 50),
                         routes(0, {{env3, mixParam(kNoiseLevel), 0.12}, {press, mixParam(kNoiseLevel), 0.12}, {press, cutoff, 0.2},
                                    {lfo1, kPartFine, 0.02}, {wheel, depth(3), 0.03}})})});
      p.push_back({"FM Lead", "Leads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fm21, 0.3), a(fb2, 0.25)}, lfo(0, 2, kLfoSine, 5.5),
                         mono(0, 50), env(0, 0, 4, 300, 1.0, 200), env(0, 2, 10, 500, 0.5, 200), open(0),
                         routes(0, {{env3, fm21, 0.2}, {press, fm21, 0.35}, {velocity, fm21, 0.15}, {lfo1, kPartFine, 0.0},
                                    {wheel, depth(3), 0.04}})})});
      // Pressure opens the FM and brings in vibrato; Slide (CC 74) adds grit through the modulator's feedback.
      p.push_back({"Pressure Lead", "Leads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), {a(fm21, 0.1), a(fb2, 0.1), a(lfoParam(2, kLfoRate), lfoValue(5.5))},
                         mono(0, 40), env(0, 0, 10, 300, 1.0, 250), open(0),
                         routes(0, {{press, fm21, 0.55}, {slide, fb2, 0.4}, {lfo1, kPartFine, 0.0}, {press, depth(2), 0.06},
                                    {wheel, depth(2), 0.04}})})});
      p.push_back({"Overdrive Lead", "Leads",
                   join({osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kSaw, 0, 5, 1.0), osc(0, 2, kSquare, -12, 0, 0.4),
                         {a(tailParam(kDriveAmount), 0.6), a(kPartLevel, -3)}, filter(0, 0, kLadder, 1800, 0.45, 0.5),
                         lfo(0, 2, kLfoSine, 5.8), mono(0, 35), env(0, 0, 2, 300, 1.0, 200),
                         routes(0, {{press, tailParam(kDriveAmount), 0.3}, {wheel, cutoff, 0.25}, {lfo1, kPartFine, 0.0},
                                    {press, depth(2), 0.05}, {velocity, cutoff, 0.1}})})});

      // ---- Keys ----
      // A DX-style electric piano: a 1:1 pair for the body and a ×14 modulator for the tine's bark,
      // both harder the harder you play. Higher notes die away sooner.
      const Values tines = join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 14.0, 0.0),
                                 {a(fm21, 0.15)}, env(0, 0, 1, 5000, 0.0, 400), env(0, 1, 0.5, 1500, 0.2, 400),
                                 env(0, 2, 0.5, 60, 0.0, 40), open(0)});
      p.push_back({"Tine Piano", "Keys",
                   join({tines, routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2},
                                           {key, envParam(0, kEnvDecay), -0.08}})})});
      // The piano through a stereo tremolo; the wheel widens it, and Press pushes the bark.
      p.push_back({"Suitcase EP", "Keys",
                   join({tines, lfo(0, 2, kLfoSine, 4.5), {a(lfoParam(2, kLfoTrigger), 1)},
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {press, fm21, 0.35},
                                    {lfo1, kPartPan, 0.3}, {wheel, depth(4), 0.35}, {key, envParam(0, kEnvDecay), -0.08}})})});
      // A reed piano: a half-sine modulator that barks when struck hard, through a little drive.
      p.push_back({"Reed Piano", "Keys",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kHalfSine, 1.0, 0.0), {a(fm21, 0.12), a(tailParam(kDriveAmount), 0.35)},
                         filter(0, 0, kLowPass, 4000, 0.0, 0.5), lfo(0, 2, kLfoSine, 5.5), env(0, 0, 1, 2500, 0.15, 250),
                         env(0, 2, 0.5, 400, 0.1, 200),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.3}, {lfo1, kPartLevel, 0.0}, {wheel, depth(2), 0.06}})})});
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
      // The tine piano on either part.
      const auto tinesOn = [&](int part) {
         return join({op(part, 0, kSine, 1.0, 1.0), op(part, 1, kSine, 1.0, 0.0), op(part, 2, kSine, 14.0, 0.0), {pv(part, fm21, 0.15)},
                      env(part, 0, 1, 5000, 0.0, 400), env(part, 1, 0.5, 1500, 0.2, 400), env(part, 2, 0.5, 60, 0.0, 40), open(part)});
      };
      // Layer: the struck strings, and under them a pure FM body that rings out the fundamental and
      // a faintly stretched second partial, as a big soundboard does. The mod wheel opens the lid.
      p.push_back({"Concert Grand", "Keys",
                   join({{g(kPlayMode, 1.0)}, piano(0),
                         routes(0, {{env2, cutoff, 0.42}, {env3, mixParam(kNoiseLevel), 0.22}, {key, envParam(0, kEnvDecay), -0.1},
                                    {key, kPartPan, 0.25}, {velocity, cutoff, 0.08}, {wheel, cutoff, 0.12}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.003, 0.25), op(1, 2, kSine, 1.0, 0.0),
                         {b(kPartTranspose, 0), b(flockParam(kBehaviour), 0), b(fm31, 0.05), b(kPartLevel, -9), b(kPolyphony, 16)},
                         env(1, 0, 2, 4500, 0.0, 450), env(1, 2, 0.5, 250, 0.0, 100), open(1),
                         routes(1, {{env3, fm31, 0.25}, {velocity, fm31, 0.15}, {key, envParam(0, kEnvDecay), -0.1}, {key, kPartPan, 0.25}})})});
      // Soft hammers: a triangle and a sine through a dark filter, more thump than ring, and a long
      // release like the damper lifting late. Press warms it.
      p.push_back({"Felt Piano", "Keys",
                   join({osc(0, 0, kTriangle, 0, -3, 1.0), osc(0, 1, kSine, 0, 3, 0.8), osc(0, 2, kSaw, 12, 0, 0.1),
                         filter(0, 0, kLowPass, 220, 0.05, 0.8),
                         {a(filterParam(0, kFilterSlope), 0), a(envParam(1, kEnvVelocity), 0.6), a(mixParam(kNoiseColor), -0.5),
                          a(kVelocityToAmp, 0.7), a(kPolyphony, 16)},
                         env(0, 0, 3, 4500, 0.0, 900), env(0, 1, 2, 900, 0.1, 600), env(0, 2, 0.5, 60, 0.0, 40),
                         routes(0, {{env2, cutoff, 0.3}, {env3, mixParam(kNoiseLevel), 0.3}, {key, envParam(0, kEnvDecay), -0.1},
                                    {press, cutoff, 0.12}, {wheel, cutoff, 0.15}})})});
      // Layer: the piano twice, a little out of tune with itself and panned apart, as a bar upright.
      p.push_back({"Honky Tonk", "Keys",
                   join({{g(kPlayMode, 1.0)}, piano(0), {a(kPartFine, -9), a(kPartPan, -0.5)},
                         routes(0, {{env2, cutoff, 0.45}, {env3, mixParam(kNoiseLevel), 0.2}, {key, envParam(0, kEnvDecay), -0.12},
                                    {wheel, cutoff, 0.12}}),
                         piano(1), {b(kPartTranspose, 0), b(flockParam(kBehaviour), 0), b(kPartFine, 9), b(kPartPan, 0.5)},
                         routes(1, {{env2, cutoff, 0.45}, {key, envParam(0, kEnvDecay), -0.12}, {wheel, cutoff, 0.12}})})});
      // The tine piano struck harder: the bark comes in sooner and lasts longer, with a touch of drive.
      p.push_back({"Dyno Tines", "Keys",
                   join({tinesOn(0), env(0, 2, 0.5, 140, 0.0, 60), {a(tailParam(kDriveAmount), 0.2)},
                         routes(0, {{env2, fm21, 0.35}, {env3, fm31, 0.5}, {velocity, fm21, 0.3}, {velocity, fm31, 0.35},
                                    {key, envParam(0, kEnvDecay), -0.08}, {press, fm21, 0.2}, {wheel, tailParam(kDriveAmount), 0.4}})})});
      // A swept notch over the tines, for a phaser; the wheel speeds it up and Press deepens it.
      p.push_back({"Phaser EP", "Keys",
                   join({tinesOn(0), filter(0, 0, kNotch, 900, 0.3, 0.0), lfo(0, 2, kLfoTriangle, 0.4), {a(lfoParam(2, kLfoTrigger), 2)},
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2},
                                    {lfo1, cutoff, 0.22}, {wheel, lfoParam(2, kLfoRate), 0.25}, {press, resonance, 0.45},
                                    {key, envParam(0, kEnvDecay), -0.08}})})});
      // Press is the tremolo depth, from none to a deep throb; the wheel sets its speed.
      p.push_back({"Tremolo Tines", "Keys",
                   join({tinesOn(0), lfo(0, 2, kLfoSine, 4.0), {a(lfoParam(2, kLfoTrigger), 2)},
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {lfo1, kPartLevel, 0.0},
                                    {lfo1, kPartPan, 0.0}, {press, depth(3), 0.12}, {press, depth(4), 0.5},
                                    {wheel, lfoParam(2, kLfoRate), 0.2}, {key, envParam(0, kEnvDecay), -0.08}})})});
      // An old tape of the piano: dull, a little warped and wobbling; the wheel warps it further.
      p.push_back({"Lo-Fi EP", "Keys",
                   join({tinesOn(0), filter(0, 0, kLowPass, 1700, 0.15, 0.3),
                         {a(kDrift, 0.35), a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.3), a(lfoParam(3, kLfoTrigger), 2)},
                         lfo(0, 3, kLfoSmooth, 0.8),
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.25}, {velocity, fm21, 0.2}, {lfo2, kPartFine, 0.025},
                                    {wheel, depth(3), 0.05}, {wheel, kDrift, 0.4}, {press, cutoff, 0.15}})})});
      // The tine modulator at x7 and slower to fade, so the bark rings on as glass.
      p.push_back({"Glass EP", "Keys",
                   join({tinesOn(0), op(0, 2, kSine, 7.0, 0.0), env(0, 2, 0.5, 900, 0.1, 600), {a(kSkyPan, 0.4)},
                         routes(0, {{env2, fm21, 0.25}, {env3, fm31, 0.3}, {velocity, fm31, 0.2}, {press, fm31, 0.2},
                                    {wheel, fm21, 0.25}, {key, envParam(0, kEnvDecay), -0.08}})})});
      // The reed piano with Press on the drive: lean in and it snarls. The wheel adds tremolo.
      p.push_back({"Wurli Drive", "Keys",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kHalfSine, 1.0, 0.0), {a(fm21, 0.12), a(tailParam(kDriveAmount), 0.3)},
                         filter(0, 0, kLowPass, 3500, 0.0, 0.5), lfo(0, 2, kLfoSine, 5.5), env(0, 0, 1, 2200, 0.15, 250),
                         env(0, 2, 0.5, 350, 0.1, 200),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.35}, {press, tailParam(kDriveAmount), 0.5}, {press, fm21, 0.15},
                                    {lfo1, kPartLevel, 0.0}, {wheel, depth(4), 0.07}})})});
      p.push_back({"Clav", "Keys",
                   join({{a(kPartLevel, -5)}, {a(oscParam(0, kOscWave), kPulse), a(oscParam(0, kOscShape), 0.25), a(cutoff, 0.55), a(resonance, 0.35)},
                         env(0, 0, 0.5, 1200, 0.3, 60), env(0, 1, 0.5, 180, 0.0, 100),
                         routes(0, {{env2, cutoff, 0.35}, {velocity, cutoff, 0.25}, {wheel, resonance, 0.3}})})});
      p.push_back({"Harpsichord", "Keys",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 12, 3, 0.5), {a(oscParam(1, kOscShape), 0.2), a(kVelocityToAmp, 0.4)},
                         filter(0, 0, kLowPass, 5000, 0.1, 0.6), filter(0, 1, kHighPass, 250, 0.0, 0.0),
                         env(0, 0, 0.5, 1800, 0.0, 180), env(0, 1, 0.5, 500, 0.3, 200), routes(0, {{env2, cutoff, 0.15}})})});
      // Drawbars 16', 8' and 5 1/3', a key click, and a rotary speaker the mod wheel speeds up.
      p.push_back({"Tonewheel Organ", "Keys",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 0.5, 0.8), op(0, 2, kSine, 1.5, 0.7),
                         {a(kVelocityToAmp, 0.0), a(mixParam(kNoiseColor), 0.5), a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.25),
                          a(kPartLevel, -6), a(lfoParam(2, kLfoTrigger), 1)},
                         lfo(0, 2, kLfoSine, 0.8), env(0, 0, 3, 100, 1.0, 40), env(0, 2, 0.5, 25, 0.0, 10), open(0),
                         routes(0, {{env3, mixParam(kNoiseLevel), 0.2}, {lfo1, kPartPan, 0.15}, {lfo1, kPartFine, 0.03},
                                    {wheel, lfoParam(2, kLfoRate), 0.27}})})});
      p.push_back({"Church Organ", "Keys",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.55), op(0, 2, kSine, 0.5, 0.6),
                         {a(fb1, 0.1), a(kVelocityToAmp, 0.0), a(kPartLevel, -5)}, env(0, 0, 80, 100, 1.0, 600), open(0),
                         routes(0, {{wheel, fb1, 0.25}})})});

      // ---- Mallets and Bells ----
      p.push_back({"Marimba", "Mallets and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.3), op(0, 2, kSine, 10.0, 0.0), {a(fm31, 0.05)},
                         env(0, 0, 0.5, 420, 0.0, 300), env(0, 2, 0.5, 35, 0.0, 30), open(0),
                         routes(0, {{env3, fm31, 0.4}, {velocity, fm31, 0.15}})})});
      p.push_back({"Vibraphone", "Mallets and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.0), {a(fm21, 0.15)}, lfo(0, 2, kLfoSine, 5.5),
                         env(0, 0, 0.5, 3000, 0.0, 1200), env(0, 2, 0.5, 300, 0.0, 200), open(0),
                         routes(0, {{env3, fm21, 0.2}, {velocity, fm21, 0.15}, {lfo1, kPartLevel, 0.04}, {wheel, depth(2), 0.05}})})});
      p.push_back({"Celesta", "Mallets and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 7.0, 0.0), {a(fm21, 0.2)}, env(0, 0, 0.5, 2500, 0.0, 600),
                         env(0, 2, 0.5, 200, 0.0, 200), open(0), routes(0, {{env3, fm21, 0.2}, {velocity, fm21, 0.15}})})});
      p.push_back({"Tubular Bells", "Mallets and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 2.01, 0.25), {a(fm21, 0.3)},
                         env(0, 0, 0.5, 6000, 0.0, 3000), env(0, 2, 0.5, 1500, 0.0, 1500), open(0),
                         routes(0, {{env3, fm21, 0.25}, {velocity, fm21, 0.2}})})});
      p.push_back({"Crystal Bells", "Mallets and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.41, 0.0), op(0, 2, kSine, 7.0, 0.0), {a(fm21, 0.2), a(fm31, 0.08)},
                         lfo(0, 2, kLfoSine, 0.8), env(0, 0, 0.5, 4000, 0.0, 2500), env(0, 2, 0.5, 600, 0.0, 500), open(0),
                         routes(0, {{env3, fm31, 0.3}, {velocity, fm21, 0.2}, {lfo1, oscParam(1, kOscFine), 0.03}, {wheel, fm21, 0.2}})})});
      p.push_back({"Steel Drum", "Mallets and Bells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 2.0, 0.0),
                         {a(oscParam(1, kOscFine), 4), a(fm21, 0.2), a(fm31, 0.1)}, env(0, 0, 1, 520, 0.0, 350),
                         env(0, 2, 0.5, 140, 0.0, 100), open(0), routes(0, {{env3, fm21, 0.3}, {velocity, fm31, 0.3}})})});

      // ---- Plucks ----
      p.push_back({"Synth Pluck", "Plucks",
                   join({{a(oscParam(1, kOscWave), kPulse), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), 7),
                          a(mixParam(kMixLevel, 1), 0.6), a(cutoff, 0.3), a(resonance, 0.45)},
                         env(0, 0, 1, 800, 0.1, 300), env(0, 1, 0.5, 260, 0.0, 200),
                         routes(0, {{env2, cutoff, 0.5}, {velocity, cutoff, 0.2}, {slide, resonance, 0.3}, {wheel, envParam(1, kEnvDecay), 0.15}})})});
      // The mod wheel lets it ring longer.
      p.push_back({"Bright Pluck", "Plucks",
                   join({osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0), osc(0, 2, kSquare, 12, 0, 0.3),
                         {a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4), a(envParam(1, kEnvVelocity), 0.4)},
                         filter(0, 0, kLowPass, 400, 0.25, 0.5), env(0, 0, 1, 600, 0.0, 400), env(0, 1, 0.5, 220, 0.0, 250),
                         routes(0, {{env2, cutoff, 0.5}, {wheel, envParam(0, kEnvDecay), 0.15}, {wheel, envParam(1, kEnvDecay), 0.15}})})});
      p.push_back({"Nylon Guitar", "Plucks",
                   join({{a(cutoff, 0.3), a(resonance, 0.05), a(filterParam(0, kFilterKeyTrack), 0.6)},
                         env(0, 0, 1, 1800, 0.0, 400), env(0, 1, 0.5, 350, 0.0, 300), routes(0, {{env2, cutoff, 0.4}, {velocity, cutoff, 0.1}})})});
      p.push_back({"Harp", "Plucks",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fm21, 0.08)}, env(0, 0, 1, 2400, 0.0, 1500),
                         env(0, 2, 0.5, 500, 0.0, 400), open(0), routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.1}})})});
      p.push_back({"Koto", "Plucks",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), {a(fm21, 0.15), a(fb1, 0.1)},
                         env(0, 0, 0.5, 1300, 0.0, 600), env(0, 2, 0.5, 180, 0.0, 150), open(0),
                         routes(0, {{env3, fm21, 0.4}, {velocity, fm21, 0.2}})})});
      p.push_back({"Kalimba", "Plucks",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 5.95, 0.06), {a(fm21, 0.1)},
                         env(0, 0, 0.5, 1100, 0.0, 600), env(0, 2, 0.5, 60, 0.0, 50), open(0),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.1}})})});
      p.push_back({"Pizzicato", "Plucks",
                   join({osc(0, 0, kSaw, 0, -4, 1.0), osc(0, 1, kSaw, 0, 4, 0.8), filter(0, 0, kLowPass, 1800, 0.05, 0.6),
                         env(0, 0, 2, 300, 0.0, 250), env(0, 1, 1, 120, 0.0, 100),
                         routes(0, {{env2, cutoff, 0.2}, {velocity, cutoff, 0.15}})})});
      p.push_back({"Bell Pluck", "Plucks",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 7.0, 0.0),
                         {a(fm21, 0.25), a(fm32, 0.25), a(lfoParam(2, kLfoRate), lfoValue(0.7))},
                         env(0, 0, 0.5, 2600, 0.0, 1500), env(0, 2, 0.5, 600, 0.0, 500), open(0),
                         routes(0, {{env3, fm21, 0.3}, {press, fm32, 0.4}, {lfo1, oscParam(1, kOscFine), 0.03}})})});

      // ---- Brass ----
      // The filter opens a little after the note speaks, harder when struck harder, and the pitch
      // scoops up into the note.
      p.push_back({"Synth Brass", "Brass",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 0.9), filter(0, 0, kLowPass, 450, 0.15, 0.4),
                         lfo(0, 2, kLfoSine, 5.0), {a(lfoParam(2, kLfoDelay), timeValue(300)), a(envParam(1, kEnvVelocity), 0.5)},
                         env(0, 0, 35, 500, 0.85, 250), env(0, 1, 70, 600, 0.45, 300), env(0, 2, 0.5, 70, 0.0, 50),
                         routes(0, {{env2, cutoff, 0.38}, {env3, kPartFine, -0.12}, {press, cutoff, 0.2}, {lfo1, kPartFine, 0.0},
                                    {wheel, depth(3), 0.04}})})});
      p.push_back({"Soft Horns", "Brass",
                   join({osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kTriangle, 0, 5, 0.8), filter(0, 0, kLowPass, 600, 0.1, 0.4),
                         {a(filterParam(0, kFilterSlope), 0)}, lfo(0, 2, kLfoSine, 5.0), {a(lfoParam(2, kLfoDelay), timeValue(400))},
                         env(0, 0, 140, 600, 0.9, 450), env(0, 1, 200, 800, 0.5, 500),
                         routes(0, {{env2, cutoff, 0.25}, {press, cutoff, 0.2}, {lfo1, kPartFine, 0.0}, {wheel, depth(2), 0.04}})})});
      p.push_back({"East Coast Brass", "Brass",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fb2, 0.3), a(fm21, 0.1)}, lfo(0, 2, kLfoSine, 5.2),
                         env(0, 0, 40, 400, 0.9, 200), env(0, 2, 60, 300, 0.6, 200), open(0),
                         routes(0, {{env3, fm21, 0.4}, {velocity, fm21, 0.2}, {press, fm21, 0.2}, {lfo1, kPartFine, 0.0},
                                    {wheel, depth(3), 0.04}})})});
      p.push_back({"Brass Stabs", "Brass",
                   join({osc(0, 0, kSaw, 0, -9, 1.0), osc(0, 1, kSaw, 0, 9, 1.0), osc(0, 2, kSaw, 12, 0, 0.35),
                         {a(tailParam(kDriveAmount), 0.2), a(envParam(1, kEnvVelocity), 0.7)}, filter(0, 0, kLowPass, 700, 0.2, 0.4),
                         env(0, 0, 4, 350, 0.5, 150), env(0, 1, 4, 250, 0.2, 200),
                         routes(0, {{env2, cutoff, 0.45}, {wheel, cutoff, 0.2}})})});

      // ---- Strings ----
      p.push_back({"String Machine", "Strings",
                   join({{a(kPartLevel, -4)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), -8),
                          a(mixParam(kMixLevel, 1), 0.9), a(oscParam(2, kOscWave), kSaw), a(oscParam(2, kOscPitchMode), 0),
                          a(oscParam(2, kOscCoarse), 12), a(mixParam(kMixLevel, 2), 0.4), a(mixParam(kMixPan, 0), -0.4),
                          a(mixParam(kMixPan, 1), 0.4), a(cutoff, 0.55), a(resonance, 0.1),
                          a(lfoParam(2, kLfoRate), lfoValue(6.0)), a(lfoParam(3, kLfoRate), lfoValue(0.4))},
                         env(0, 0, 300, 1000, 1.0, 1200),
                         routes(0, {{lfo1, oscParam(1, kOscFine), 0.04}, {lfo2, oscParam(2, kOscFine), 0.05}, {press, cutoff, 0.25},
                                    {wheel, cutoff, 0.2}})})});
      // Two saws drifting against each other for the ensemble; a harder note bows in faster.
      const Values ensemble = join({osc(0, 0, kSaw, 0, -9, 1.0), osc(0, 1, kSaw, 0, 9, 1.0), osc(0, 2, kSaw, 12, -4, 0.3),
                                    {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5), a(lfoParam(2, kLfoDelay), timeValue(400))},
                                    lfo(0, 2, kLfoSine, 5.2), lfo(0, 3, kLfoTriangle, 0.6)});
      const auto ensembleRoutes = [&](double vibrato) {
         return routes(0, {{lfo1, kPartFine, vibrato}, {lfo2, oscParam(0, kOscFine), 0.04}, {lfo2, oscParam(1, kOscFine), -0.04},
                           {press, cutoff, 0.2}, {wheel, depth(0), 0.03}, {velocity, envParam(0, kEnvAttack), -0.1}, {env2, cutoff, 0.15}});
      };
      p.push_back({"Ensemble Strings", "Strings",
                   join({ensemble, filter(0, 0, kLowPass, 2500, 0.05, 0.5), env(0, 0, 250, 1000, 1.0, 800),
                         env(0, 1, 400, 1000, 1.0, 800), ensembleRoutes(0.015)})});
      p.push_back({"Slow Strings", "Strings",
                   join({ensemble, filter(0, 0, kLowPass, 1500, 0.05, 0.5), env(0, 0, 1200, 1000, 1.0, 2500),
                         env(0, 1, 1800, 1000, 1.0, 2500), ensembleRoutes(0.015)})});
      p.push_back({"Solo Cello", "Strings",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 0, 0.5), {a(oscParam(1, kOscShape), 0.35)},
                         filter(0, 0, kLowPass, 1400, 0.2, 0.5), filter(0, 1, kHighPass, 90, 0.0, 0.0), lfo(0, 2, kLfoSine, 5.2),
                         {a(lfoParam(2, kLfoDelay), timeValue(350))}, mono(0, 80), env(0, 0, 110, 300, 1.0, 300),
                         routes(0, {{lfo1, kPartFine, 0.03}, {press, cutoff, 0.25}, {press, depth(0), 0.03}, {wheel, depth(0), 0.04}})})});

      // ---- Pads ----
      p.push_back({"Sine Pad", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.7),
                         {a(oscParam(1, kOscFine), 7), a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4)},
                         env(0, 0, 400, 1000, 1.0, 1500), open(0), routes(0, {})})});
      p.push_back({"Warm Analog", "Pads",
                   join({{a(kPartLevel, -4)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), 9),
                          a(mixParam(kMixLevel, 1), 0.9), a(mixParam(kMixPan, 0), -0.3), a(mixParam(kMixPan, 1), 0.3),
                          a(cutoff, 0.42), a(resonance, 0.15)},
                         env(0, 0, 500, 1000, 1.0, 2000), env(0, 1, 800, 2000, 0.5, 2000),
                         routes(0, {{env2, cutoff, 0.15}, {wheel, cutoff, 0.25}, {press, cutoff, 0.15}})})});
      // Two square waves whose widths drift on unrelated LFOs.
      p.push_back({"PWM Pad", "Pads",
                   join({osc(0, 0, kSquare, 0, -6, 1.0), osc(0, 1, kSquare, 0, 6, 1.0),
                         {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5)}, lfo(0, 2, kLfoTriangle, 0.35),
                         lfo(0, 3, kLfoTriangle, 0.23), filter(0, 0, kLowPass, 1800, 0.1, 0.3), env(0, 0, 500, 1000, 1.0, 1800),
                         env(0, 1, 800, 2000, 0.6, 1500),
                         routes(0, {{lfo1, oscParam(0, kOscShape), 0.25}, {lfo2, oscParam(1, kOscShape), 0.25}, {env2, cutoff, 0.15},
                                    {wheel, cutoff, 0.25}, {press, cutoff, 0.15}})})});
      p.push_back({"Glass Pad", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 0.5, 0.4),
                         {a(fm21, 0.2), a(lfoParam(2, kLfoRate), lfoValue(0.3))}, env(0, 0, 600, 1000, 1.0, 2500), open(0),
                         routes(0, {{lfo1, fm21, 0.15}, {wheel, fm21, 0.25}})})});
      // Two band passes in parallel make an "ah"; a slow LFO moves the vowel, and the wheel darkens it toward "oo".
      p.push_back({"Choir Pad", "Pads",
                   join({osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0), {a(mixParam(kNoiseLevel), 0.08), a(kPartLevel, 4)},
                         {a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4), a(tailParam(kFilterRouting), 1)},
                         filter(0, 0, kBandPass, 750, 0.45, 0.0), filter(0, 1, kBandPass, 1150, 0.45, 0.0),
                         {a(filterParam(0, kFilterSlope), 0), a(filterParam(1, kFilterSlope), 0), a(lfoParam(2, kLfoDelay), timeValue(600))},
                         lfo(0, 2, kLfoSine, 5.0), lfo(0, 3, kLfoSmooth, 0.15), env(0, 0, 700, 1000, 1.0, 2000),
                         routes(0, {{lfo1, kPartFine, 0.015}, {lfo2, cutoff, 0.05}, {lfo2, cutoff2, 0.04}, {wheel, cutoff2, -0.15},
                                    {wheel, cutoff, -0.05}})})});
      p.push_back({"Bowed Glass", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), env(0, 0, 2500, 1000, 1.0, 3000),
                         env(0, 2, 4000, 1000, 1.0, 3000), open(0), routes(0, {{env3, fm21, 0.35}, {press, fm21, 0.2}})})});
      // The vibrato only arrives once the note has bloomed.
      p.push_back({"Dawn Chorus", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.0), op(0, 2, kSine, 1.0, 0.4),
                         {a(oscParam(2, kOscFine), 5), a(lfoParam(2, kLfoRate), lfoValue(5.0)), a(lfoParam(2, kLfoDelay), timeValue(2500))},
                         env(0, 0, 2000, 1000, 1.0, 3000), env(0, 2, 3000, 1000, 0.8, 3000), open(0),
                         routes(0, {{env3, fm21, 0.3}, {lfo1, kPartFine, 0.04}, {press, fm21, 0.25}})})});
      p.push_back({"Deep Current", "Pads",
                   join({{a(kPartLevel, -2)}, op(0, 1, kSine, 0.5, 0.6),
                         {a(kPartTranspose, -12), a(filterParam(0, kFilterType), 5), a(cutoff, 0.25), a(resonance, 0.5),
                          a(tailParam(kDriveType), 2), a(kDrift, 0.2), a(lfoParam(3, kLfoRate), lfoValue(0.07))},
                         env(0, 0, 2500, 1000, 1.0, 4000), env(0, 1, 6000, 1000, 1.0, 4000),
                         routes(0, {{env2, cutoff, 0.4}, {lfo2, resonance, 0.2}, {press, cutoff, 0.25}, {slide, tailParam(kDriveAmount), 0.4}})})});

      // ---- Rhythmic: these move in time with the host's tempo ----
      // A sixteenth-note gate; the mod wheel softens it back toward a pad.
      p.push_back({"Trance Gate", "Rhythmic",
                   join({osc(0, 0, kSaw, 0, -10, 1.0), osc(0, 1, kSaw, 0, 10, 1.0), osc(0, 2, kSaw, -12, 0, 0.5),
                         {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5), a(lfoParam(2, kLfoPolarity), 1)},
                         filter(0, 0, kLowPass, 2000, 0.2, 0.3), lfoSync(0, 2, kLfoSquare, k16th), env(0, 0, 10, 500, 1.0, 300),
                         routes(0, {{lfo1, kPartLevel, -0.5}, {lfo1, cutoff, -0.1}, {wheel, depth(0), 0.25}, {press, cutoff, 0.2}})})});
      // Each sixteenth a falling saw snaps the filter open and shut, so one held note plays a line.
      p.push_back({"Sixteenth Pulse", "Rhythmic",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, -12, 0, 0.5), filter(0, 0, kLadder, 1600, 0.4, 0.4),
                         {a(lfoParam(2, kLfoPolarity), 1)}, lfoSync(0, 2, kLfoSaw, k16th), env(0, 0, 3, 500, 1.0, 200),
                         routes(0, {{lfo1, cutoff, -0.45}, {lfo1, kPartLevel, -0.25}, {wheel, resonance, 0.3}, {press, cutoff, 0.15}})})});
      p.push_back({"Sample and Hold", "Rhythmic",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSquare, 0, 5, 0.7), filter(0, 0, kLadder, 900, 0.6, 0.4),
                         lfoSync(0, 2, kLfoHold, k16th), env(0, 0, 5, 500, 1.0, 400),
                         routes(0, {{lfo1, cutoff, 0.25}, {wheel, resonance, 0.25}, {press, depth(0), 0.15}})})});
      // Chords chopped on dotted eighths, the filter breathing on the half note.
      p.push_back({"Dotted Chords", "Rhythmic",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 0.9), filter(0, 0, kLowPass, 1200, 0.25, 0.4),
                         {a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4), a(lfoParam(3, kLfoPolarity), 1)},
                         lfoSync(0, 3, kLfoSquare, k8thDotted), lfoSync(0, 2, kLfoSine, kHalf), env(0, 0, 5, 500, 1.0, 300),
                         routes(0, {{lfo2, kPartLevel, -0.45}, {lfo1, cutoff, 0.15}, {wheel, cutoff, 0.2}, {press, cutoff, 0.15}})})});

      // ---- Atmospheres ----
      p.push_back({"Wind Choir", "Atmospheres",
                   join({{a(oscParam(0, kOscWave), kSaw), a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0),
                          a(oscParam(1, kOscFine), 11), a(mixParam(kMixLevel, 1), 0.9), a(mixParam(kNoiseLevel), 0.35),
                          a(filterParam(0, kFilterType), 2), a(filterParam(0, kFilterCutoff), 0.5),
                          a(filterParam(0, kFilterResonance), 0.4), a(kPartLevel, 6.0), a(lfoParam(1, kLfoPush), 0.9),
                          a(lfoParam(1, kLfoRate), lfoValue(0.1)), a(envParam(0, kEnvAttack), timeValue(600)),
                          a(lfoParam(3, kLfoRate), lfoValue(0.12))},
                         route(0, 6, lfo2, filterParam(0, kFilterCutoff), 0.2),
                         route(0, 7, slide, filterParam(0, kFilterResonance), 0.3)})});
      // Pink noise through a comb tuned to the note, so the noise sings it; slow random LFOs move it.
      p.push_back({"Deep Space", "Atmospheres",
                   join({op(0, 0, kSine, 1.0, 0.2), {a(mixParam(kNoiseLevel), 0.5), a(mixParam(kNoiseColor), -0.5), a(kSkyPan, 0.5),
                          a(kDrift, 0.2)},
                         filter(0, 0, kComb, 261.6, 0.85, 1.0), filter(0, 1, kLadder, 1500, 0.2, 0.3), lfo(0, 2, kLfoSmooth, 0.13),
                         lfo(0, 3, kLfoSmooth, 0.08), env(0, 0, 2000, 1000, 1.0, 4000), shape(0, 1, "Wander", 21000, kForever),
                         shape(0, 2, "Gusts", 33000, kForever),
                         routes(0, {{lfo1, cutoff2, 0.2}, {lfo2, resonance, 0.1}, {wheel, cutoff2, 0.3}, {press, resonance, 0.12},
                                    {env2, cutoff2, 0.15}, {env3, mixParam(kNoiseColor), 0.3}})})});
      p.push_back({"Aurora", "Atmospheres",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.5, 0.0), op(0, 2, kSine, 3.01, 0.0),
                         {a(fm21, 0.15), a(cutoff, 0.7), a(resonance, 0.2), a(lfoParam(2, kLfoRate), lfoValue(0.1)),
                          a(flockParam(kSpeed), 0.25), a(flockParam(kAlignment), 0.8), a(flockParam(kTunePull), 0.3), a(kSkyPan, 0.8)},
                         env(0, 0, 4000, 1000, 1.0, 5000), env(0, 2, 5000, 1000, 1.0, 4000),
                         routes(0, {{env3, fm32, 0.35},
                                    {kSourceHeading, fm21, 0.3},
                                    {lfo1, oscParam(2, kOscRatio), 0.01},
                                    {slide, cutoff, 0.25},
                                    {press, fm21, 0.2}})})});
      p.push_back({"Storm Front", "Atmospheres",
                   join({{a(kPartLevel, 5)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), -12),
                          a(mixParam(kMixLevel, 1), 0.8), a(mixParam(kNoiseLevel), 0.4), a(mixParam(kNoiseColor), 0.3),
                          a(filterParam(0, kFilterType), 6), a(cutoff, 0.45), a(resonance, 0.5), a(filterParam(1, kFilterCutoff), 0.7),
                          a(tailParam(kDriveType), 2), a(lfoParam(1, kLfoPush), 0.8), a(lfoParam(1, kLfoRate), lfoValue(0.15)),
                          a(flockParam(kBehaviour), kScatter), a(kSkyPan, 0.6), a(kDrift, 0.2)},
                         env(0, 0, 2500, 1000, 1.0, 4000), shape(0, 1, "Gusts", 15000, kForever),
                         routes(0, {{lfo2, cutoff, 0.25},
                                    {kSourceTurn, resonance, 0.3},
                                    {kSourceSpeed, filterParam(1, kFilterCutoff), 0.3},
                                    {env2, mixParam(kNoiseLevel), 0.35},
                                    {press, tailParam(kDriveAmount), 0.4}})})});

      // ---- Voices: formants, throats and whispers ----
      // Two band passes in parallel at a vowel's first two formants (ah is 750 and 1150 Hz, ee 300
      // and 2300, oo 330 and 700), fixed in pitch as a mouth is, whatever note sings through them.
      const auto vox = [&](int part, double f1, double f2, double res) {
         return join({{pv(part, tailParam(kFilterRouting), 1), pv(part, filterParam(0, kFilterSlope), 0), pv(part, filterParam(1, kFilterSlope), 0)},
                      filter(part, 0, kBandPass, f1, res, 0.0), filter(part, 1, kBandPass, f2, res, 0.0)});
      };
      // A talk box lead: Press slides the mouth from "ah" to "ee", the wheel brings in vibrato.
      p.push_back({"Talk Box", "Voices",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 5, 0.6), {a(oscParam(1, kOscShape), 0.3), a(mixParam(kNoiseLevel), 0.03)},
                         vox(0, 750, 1150, 0.3), {a(tailParam(kDriveAmount), 0.2)}, lfo(0, 2, kLfoSine, 5.5),
                         {a(lfoParam(2, kLfoDelay), timeValue(400))}, mono(0, 60), env(0, 0, 8, 300, 1.0, 150),
                         routes(0, {{press, cutoff, -0.13}, {press, cutoff2, 0.1}, {lfo1, kPartFine, 0.0}, {wheel, depth(2), 0.04},
                                    {press, depth(2), 0.02}, {velocity, cutoff2, 0.04}})})});
      // A new vowel every eighth note at the host's tempo, from sample-and-hold LFOs on both formants.
      p.push_back({"Robot Chant", "Voices",
                   join({osc(0, 0, kSquare, 0, 0, 1.0), osc(0, 1, kSaw, -12, 0, 0.5), vox(0, 600, 1400, 0.65),
                         {a(tailParam(kDriveType), 1), a(tailParam(kDriveAmount), 0.25)}, lfoSync(0, 2, kLfoHold, k8th),
                         lfoSync(0, 3, kLfoHold, k8thDotted), env(0, 0, 3, 200, 0.9, 150),
                         routes(0, {{lfo1, cutoff, 0.12}, {lfo2, cutoff2, 0.14}, {press, resonance, 0.25}, {wheel, depth(0), 0.1},
                                    {wheel, depth(1), 0.1}})})});
      // Every note its own mouth: a random vowel and its own speed of babbling, so a chord is a crowd.
      // Press makes them more animated.
      p.push_back({"Babble Choir", "Voices",
                   join({osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 1.0), vox(0, 600, 1300, 0.55),
                         {a(mixParam(kMixPan, 0), -0.3), a(mixParam(kMixPan, 1), 0.3), a(kDrift, 0.2)},
                         lfo(0, 2, kLfoSmooth, 2.5), lfo(0, 3, kLfoSmooth, 1.7), env(0, 0, 300, 1000, 1.0, 1500),
                         routes(0, {{lfo1, cutoff, 0.1}, {lfo2, cutoff2, 0.1}, {kSourceRandom, lfoParam(2, kLfoRate), 0.15},
                                    {kSourceRandom, cutoff, 0.1}, {kSourceRandom, cutoff2, -0.08}, {kSourceRandom, kPartPan, 0.5},
                                    {press, depth(0), 0.1}, {press, depth(1), 0.1}, {wheel, lfoParam(3, kLfoRate), 0.2}})})});
      // Overtone singing: a buzzing drone with one very narrow band pass riding its harmonics. Press
      // (or the wheel) sweeps it up the series, so the whistle above the drone plays a tune.
      p.push_back({"Throat Singer", "Voices",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSaw, 0, 4, 0.6), {a(tailParam(kFilterRouting), 1), a(kPartTranspose, -12)},
                         filter(0, 0, kBandPass, 450, 0.2, 0.0), filter(0, 1, kBandPass, 1050, 0.93, 1.0),
                         {a(filterParam(0, kFilterSlope), 0), a(filterParam(1, kFilterSlope), 1), a(tailParam(kFilterBalance), -0.2)},
                         lfo(0, 3, kLfoSmooth, 0.3), mono(0, 150), env(0, 0, 200, 1000, 1.0, 600),
                         routes(0, {{press, cutoff2, 0.15}, {wheel, cutoff2, 0.15}, {lfo2, cutoff2, 0.0}, {slide, depth(2), 0.04},
                                    {press, cutoff, 0.06}})})});
      // A whisper: noise through the vowel. Press brings the voice in under the breath.
      p.push_back({"Whisper Ghosts", "Voices",
                   join({osc(0, 0, kSaw, 0, -5, 0.0), osc(0, 1, kSaw, 0, 5, 0.0), {a(mixParam(kNoiseLevel), 1.0), a(mixParam(kNoiseColor), 0.2)},
                         vox(0, 700, 1200, 0.45), {a(kVelocityToAmp, 0.5)}, {a(kSkyPan, 0.7), a(kDrift, 0.2)}, lfo(0, 3, kLfoSmooth, 0.2),
                         env(0, 0, 400, 1000, 1.0, 2000),
                         routes(0, {{press, mixParam(kMixLevel, 0), 0.9}, {press, mixParam(kMixLevel, 1), 0.9},
                                    {press, mixParam(kNoiseLevel), -0.5}, {lfo2, cutoff, 0.08}, {lfo2, cutoff2, -0.06},
                                    {wheel, cutoff2, 0.1}})})});
      // An FM throat singing through formants that follow the creatures: which way each flies sets
      // its vowel, how fast its growl. They pull each other into tune as they flock.
      p.push_back({"Alien Choir", "Voices",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.5, 0.0), osc(0, 2, kSaw, 0, 6, 0.5), {a(fm21, 0.25)},
                         vox(0, 500, 1600, 0.55),
                         {a(flockParam(kTunePull), 0.4), a(flockParam(kNeighbourFm), 0.3), a(kSkyPan, 0.8), a(kPartLevel, 3)},
                         env(0, 0, 800, 1000, 1.0, 2500),
                         routes(0, {{kSourceHeading, cutoff, 0.15}, {kSourceHeading, cutoff2, -0.12}, {kSourceSpeed, fm21, 0.2},
                                    {press, fm21, 0.25}, {wheel, oscParam(1, kOscRatio), 0.05}})})});
      // A giant, two octaves down, with a mouth that opens as you press and a slow, sliding pitch.
      p.push_back({"Yawning Giant", "Voices",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSaw, -12, 6, 0.7), vox(0, 300, 800, 0.2),
                         {a(kPartTranspose, -12), a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.3)}, mono(0, 350),
                         env(0, 0, 150, 1000, 1.0, 500),
                         routes(0, {{press, cutoff, 0.2}, {press, cutoff2, 0.12}, {press, kPartLevel, 0.08}, {wheel, cutoff, 0.15}})})});
      // A pluck that talks: each note says "yow" as Env 2 opens the vowel from "ee" to "ah".
      p.push_back({"Talking Pluck", "Voices",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSquare, 0, 6, 0.5), vox(0, 330, 2100, 0.4),
                         {a(envParam(1, kEnvVelocity), 0.4)}, env(0, 0, 1, 700, 0.25, 250), env(0, 1, 1, 260, 0.0, 200),
                         routes(0, {{env2, cutoff, 0.2}, {env2, cutoff2, -0.22}, {velocity, cutoff, 0.05}, {press, resonance, 0.2},
                                    {wheel, cutoff2, 0.08}})})});
      // Monks an octave down on "oh"; the wheel and Press open it to "ah".
      p.push_back({"Monks", "Voices",
                   join({osc(0, 0, kSaw, 0, -5, 1.0), osc(0, 1, kSaw, 0, 5, 1.0), vox(0, 420, 800, 0.5),
                         {a(kPartTranspose, -12), a(mixParam(kNoiseLevel), 0.04), a(mixParam(kMixPan, 0), -0.3), a(mixParam(kMixPan, 1), 0.3),
                          a(lfoParam(2, kLfoDelay), timeValue(800))},
                         lfo(0, 2, kLfoSine, 4.8), env(0, 0, 600, 1000, 1.0, 1800),
                         routes(0, {{lfo1, kPartFine, 0.012}, {wheel, cutoff, 0.12}, {wheel, cutoff2, 0.14}, {press, cutoff, 0.1},
                                    {press, cutoff2, 0.12}})})});

      // ---- Drums and FX ----
      // The drums ignore the key: Key Track off pitches them from middle C, so every key is the same drum.
      p.push_back({"Kick", "Drums and FX",
                   join({op(0, 0, kSine, 0.25, 1.0), {a(oscParam(0, kOscKeyTrack), 0), a(fb1, 0.05), a(kPartLevel, 3)},
                         env(0, 0, 0.5, 400, 0.0, 100), env(0, 2, 0.5, 50, 0.0, 30), open(0),
                         routes(0, {{env3, oscParam(0, kOscRatio), 0.33}, {velocity, depth(0), 0.05}})})});
      p.push_back({"Snare", "Drums and FX",
                   join({op(0, 0, kSine, 0.7, 0.6), {a(oscParam(0, kOscKeyTrack), 0), a(mixParam(kNoiseLevel), 0.8), a(mixParam(kNoiseColor), 0.6)},
                         filter(0, 0, kLowPass, 8000, 0.0, 0.0),
                         env(0, 0, 0.5, 180, 0.0, 150), env(0, 2, 0.5, 40, 0.0, 30),
                         routes(0, {{env3, oscParam(0, kOscRatio), 0.1}, {velocity, envParam(0, kEnvDecay), 0.05}})})});
      // The mod wheel opens the hat.
      p.push_back({"Hi-Hat", "Drums and FX",
                   join({{a(mixParam(kMixLevel, 0), 0.0), a(mixParam(kNoiseLevel), 1.0), a(mixParam(kNoiseColor), 1.0)},
                         filter(0, 0, kHighPass, 7000, 0.2, 0.0), env(0, 0, 0.5, 70, 0.0, 60),
                         routes(0, {{velocity, envParam(0, kEnvDecay), 0.1}, {wheel, envParam(0, kEnvDecay), 0.25},
                                    {wheel, envParam(0, kEnvRelease), 0.25}})})});
      p.push_back({"Wood Block", "Drums and FX",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.76, 0.0), {a(fm21, 0.35)}, env(0, 0, 0.5, 70, 0.0, 60),
                         open(0), routes(0, {})})});
      p.push_back({"Log Drum", "Drums and FX",
                   join({op(0, 0, kSine, 1.0, 1.0), {a(fb1, 0.15)}, env(0, 0, 0.5, 300, 0.0, 220), env(0, 2, 0.5, 45, 0.0, 40),
                         open(0),
                         // The hit drops in pitch, further the harder it is struck.
                         routes(0, {{env3, oscParam(0, kOscRatio), 0.1}, {velocity, depth(0), 0.1}})})});
      p.push_back({"Laser Zap", "Drums and FX",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.5, 0.0), {a(fm21, 0.25), a(fb1, 0.2)},
                         env(0, 0, 0.5, 400, 0.0, 200), env(0, 2, 0.5, 250, 0.0, 100), open(0),
                         routes(0, {{env3, oscParam(0, kOscRatio), 0.5}, {env3, oscParam(1, kOscRatio), 0.5}, {velocity, fm21, 0.2}})})});
      // Hold a note: it rises an octave and opens over four seconds.
      p.push_back({"Riser", "Drums and FX",
                   join({osc(0, 0, kSaw, 0, -12, 1.0), osc(0, 1, kSaw, 0, 12, 1.0), {a(mixParam(kNoiseLevel), 0.4), a(kSkyPan, 0.6)},
                         {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5)}, filter(0, 0, kLowPass, 200, 0.35, 0.0),
                         env(0, 0, 3000, 100, 1.0, 1500), env(0, 1, 4000, 10, 1.0, 1000), env(0, 2, 4000, 10, 1.0, 1000),
                         routes(0, {{env2, cutoff, 0.6}, {env3, oscParam(0, kOscCoarse), 0.125}, {env3, oscParam(1, kOscCoarse), 0.125}})})});

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

      // [Foggy EP] Soft keys behind a velocity-hungry filter, a long foggy release, Press warbling one
      // oscillator sharp, and the wheel giving each note a different shape.
      p.push_back({"Foggy EP", "Monsta",
                   join({osc(0, 0, kPulse, 0, -4, 0.7), osc(0, 1, kTriangle, 0, 4, 0.9), osc(0, 2, kSine, 12, 0, 0.2),
                         {a(oscParam(0, kOscShape), 0.3), a(envParam(1, kEnvVelocity), 0.8), a(tailParam(kDriveAmount), 0.25), a(kSkyPan, 0.5)},
                         filter(0, 0, kLowPass, 550, 0.15, 0.66), env(0, 0, 3, 2500, 0.5, 1400), env(0, 1, 3, 900, 0.25, 1200),
                         env(0, 2, 0.5, 90, 0.0, 50),
                         routes(0, {{env2, cutoff, 0.35}, {env3, kPartFine, -0.03}, {press, oscParam(1, kOscFine), 0.05},
                                    {kSourceRandom, oscParam(0, kOscShape), 0.0}, {wheel, depth(3), 0.25}})})});
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
                         routes(0, {{lfo1, oscParam(0, kOscFine), 0.0}, {lfo1, oscParam(1, kOscFine), 0.0}, {press, depth(0), 0.05},
                                    {press, depth(1), -0.08}, {lfo2, cutoff2, 0.0}, {wheel, depth(4), 0.1}, {press, cutoff, 0.05}})})});
      // [Die-50] A resonant filter that sweeps open and sinks over seconds, pulse widths shimmering
      // fast and slowing as it sinks. Press wobbles the filter and lets the pitch sag.
      p.push_back({"Fifty Pad", "Monsta",
                   join({osc(0, 0, kPulse, 0, -5, 1.0), osc(0, 1, kPulse, 12, 5, 0.35), {a(envParam(1, kEnvVelocity), 0.3)},
                         filter(0, 0, kLadder, 280, 0.6, 0.3), lfo(0, 2, kLfoTriangle, 7.0), lfo(0, 3, kLfoSine, 5.0),
                         env(0, 0, 400, 1000, 1.0, 2500), env(0, 1, 1, 6000, 0.2, 4000),
                         routes(0, {{env2, cutoff, 0.45}, {lfo1, oscParam(0, kOscShape), 0.3}, {lfo1, oscParam(1, kOscShape), 0.3},
                                    {env2, lfoParam(2, kLfoRate), -0.15}, {lfo2, cutoff, 0.0}, {press, depth(4), 0.1},
                                    {press, kPartFine, -0.03}, {wheel, lfoParam(3, kLfoRate), 0.2}})})});
      // [Kingdome] A driven three-oscillator pad. Press raises the second oscillator up to a fifth and
      // sets the filter wobbling.
      p.push_back({"Kingdome", "Monsta",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kPulse, 0, 8, 0.9), osc(0, 2, kSaw, -12, 0, 0.8),
                         {a(oscParam(1, kOscShape), 0.4), a(tailParam(kDriveAmount), 0.5), a(envParam(1, kEnvVelocity), 0.8), a(kPartLevel, -4)},
                         filter(0, 0, kLowPass, 650, 0.35, 0.0), lfo(0, 3, kLfoSine, 4.0), env(0, 0, 250, 1000, 0.75, 2000),
                         env(0, 1, 300, 1500, 0.0, 1500),
                         routes(0, {{env2, cutoff, 0.35}, {press, oscParam(1, kOscCoarse), 0.073}, {lfo2, cutoff, 0.0}, {press, depth(2), 0.15},
                                    {wheel, cutoff, -0.15}})})});
      // [Therapy] Strings in reverse: Press hushes them, and the harder you press the more the shape
      // shimmers. The wheel darkens them.
      p.push_back({"Therapy Strings", "Monsta",
                   join({osc(0, 0, kPulse, 0, -14, 1.0), osc(0, 1, kPulse, 0, 14, 1.0), osc(0, 2, kPulse, 24, 0, 0.4),
                         {a(mixParam(kMixPan, 0), -0.8), a(mixParam(kMixPan, 1), 0.8), a(kDrift, 0.3)},
                         filter(0, 0, kLowPass, 5000, 0.2, 0.35), lfo(0, 2, kLfoSine, 4.0), env(0, 0, 300, 1000, 1.0, 1500),
                         routes(0, {{press, kPartLevel, -0.3}, {lfo1, oscParam(2, kOscShape), 0.0}, {press, depth(1), 0.4},
                                    {wheel, cutoff, -0.3}, {wheel, oscParam(0, kOscShape), -0.3}, {wheel, oscParam(1, kOscShape), -0.3}})})});
      // [Nitemare] A low, slowly blooming drone; a third oscillator dives in at the start, and Press
      // brings in its pulsing shriek.
      p.push_back({"Nightmare", "Monsta",
                   join({osc(0, 0, kSaw, -12, 0, 1.0), osc(0, 1, kSquare, 24, -13, 0.35), osc(0, 2, kSaw, 12, 0, 0.0), {a(fm21, 0.1)},
                         {a(kDrift, 0.3), a(tailParam(kDriveAmount), 0.15), a(kSkyPan, 0.5)}, filter(0, 0, kLowPass, 2200, 0.05, 0.0),
                         lfo(0, 2, kLfoSine, 0.05), lfo(0, 3, kLfoSquare, 6.0), env(0, 0, 1500, 1000, 1.0, 5000),
                         env(0, 2, 0.5, 2500, 0.3, 5000),
                         routes(0, {{lfo1, oscParam(0, kOscShape), 0.3}, {env3, oscParam(2, kOscCoarse), 0.3}, {lfo2, mixParam(kMixLevel, 2), 0.0},
                                    {press, depth(2), 0.4}, {wheel, resonance, 0.4}, {lfo1, cutoff, 0.1}})})});
      // [MG Quak] A squelchy, driven mono bass with a long glide; Press sets the filter quacking.
      p.push_back({"Quack Bass", "Monsta",
                   join({osc(0, 0, kSaw, 0, 0, 1.0), osc(0, 1, kSaw, -12, 10, 0.5), filter(0, 0, kLadder, 200, 0.7, 0.55),
                         {a(tailParam(kDriveAmount), 0.5), a(kPartLevel, -2)}, lfo(0, 2, kLfoSine, 6.0), mono(0, 110),
                         env(0, 0, 1, 300, 0.7, 60), env(0, 1, 1, 450, 0.0, 100),
                         routes(0, {{env2, cutoff, 0.5}, {lfo1, cutoff, 0.0}, {press, depth(1), 0.2}, {wheel, resonance, 0.2},
                                    {velocity, cutoff, 0.1}})})});
      // [Gristle] A thick, wide lead of pulse waves whose widths swing on an LFO; Press makes the
      // resonance flutter, the wheel slows the swing and darkens it.
      p.push_back({"Gristle Lead", "Monsta",
                   join({osc(0, 0, kPulse, 0, -20, 1.0), osc(0, 1, kPulse, -12, 20, 1.0), {a(mixParam(kMixPan, 0), -0.7), a(mixParam(kMixPan, 1), 0.7)},
                         filter(0, 0, kLadder, 2500, 0.25, 0.4), {a(tailParam(kDriveAmount), 0.35), a(kPartLevel, -3)},
                         lfo(0, 2, kLfoSine, 2.5), lfo(0, 3, kLfoSine, 7.0), mono(0, 80), env(0, 0, 7, 1000, 0.8, 120),
                         routes(0, {{lfo1, oscParam(0, kOscShape), 0.4}, {lfo1, oscParam(1, kOscShape), 0.4}, {lfo2, resonance, 0.0},
                                    {press, depth(2), 0.35}, {wheel, lfoParam(2, kLfoRate), -0.15}, {wheel, cutoff, -0.2}})})});
      // [Cumulous] A wide pad under a resonant, fully tracking filter. Press speeds up its drift and
      // narrows the pulse; the wheel sweeps it across the stereo field.
      p.push_back({"Cumulus", "Monsta",
                   join({osc(0, 0, kPulse, 0, -15, 1.0), osc(0, 1, kPulse, 7, 15, 0.8), {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5)},
                         filter(0, 0, kLowPass, 1800, 0.55, 1.0), lfo(0, 2, kLfoTriangle, 0.25), lfo(0, 3, kLfoSmooth, 0.4),
                         env(0, 0, 1200, 1000, 0.95, 3000),
                         routes(0, {{lfo1, oscParam(1, kOscShape), 0.15}, {press, lfoParam(2, kLfoRate), 0.25}, {press, oscParam(0, kOscShape), -0.25},
                                    {lfo2, cutoff, 0.08}, {lfo1, kPartPan, 0.0}, {wheel, depth(4), 0.6}})})});
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
                         routes(0, {{env2, cutoff, 0.55}, {lfo2, oscParam(0, kOscShape), 0.3}, {lfo2, oscParam(2, kOscShape), -0.3},
                                    {lfo1, kPartFine, 0.0}, {wheel, depth(3), 0.04}, {press, cutoff, 0.15}})})});
      // [Softy] A drone that takes seconds to arrive, its filter breathing on a slow LFO; the wheel
      // brings in a resonance that flutters.
      p.push_back({"Softy Drone", "Monsta",
                   join({osc(0, 0, kSaw, -12, 0, 1.0), osc(0, 1, kSaw, 0, -12, 0.8), osc(0, 2, kPulse, 0, 30, 0.5),
                         filter(0, 0, kLowPass, 1200, 0.15, 0.0), lfo(0, 2, kLfoSine, 0.25), lfo(0, 3, kLfoSmooth, 0.3),
                         env(0, 0, 3000, 1000, 1.0, 3000), {a(kPartLevel, -3)},
                         routes(0, {{lfo1, cutoff, 0.25}, {lfo2, resonance, 0.0}, {wheel, depth(1), 0.4}, {press, cutoff, 0.1}})})});
      // [Ceasefre] A wide pad whose widths turn as Env 2 swells over seconds; Press brings in a third
      // oscillator a fifth up, pulsing. The wheel takes out the octave.
      p.push_back({"Ceasefire", "Monsta",
                   join({osc(0, 0, kPulse, 12, 0, 0.5), osc(0, 1, kPulse, 0, 0, 1.0), osc(0, 2, kSaw, 7, 0, 0.0),
                         {a(mixParam(kMixPan, 0), -0.7), a(mixParam(kMixPan, 1), 0.7), a(kSkyPan, 0.5)},
                         filter(0, 0, kLowPass, 1500, 0.45, 0.3), lfo(0, 2, kLfoSine, 3.0), env(0, 0, 1000, 1000, 1.0, 3000),
                         env(0, 1, 2500, 3000, 0.0, 3000),
                         routes(0, {{env2, oscParam(0, kOscShape), 0.35}, {env2, oscParam(1, kOscShape), -0.3}, {lfo1, mixParam(kMixLevel, 2), 0.0},
                                    {press, depth(2), 0.5}, {press, mixParam(kMixLevel, 2), 0.2}, {wheel, mixParam(kMixLevel, 0), -0.4}})})});
      // [ChamberS] Slow chamber strings whose widths and levels move on an LFO; Press is the vibrato.
      p.push_back({"Chamber Strings", "Monsta",
                   join({osc(0, 0, kSaw, 0, -9, 1.0), osc(0, 1, kPulse, 0, 9, 1.0), osc(0, 2, kSaw, -12, 0, 0.4),
                         {a(mixParam(kMixPan, 0), -0.7), a(mixParam(kMixPan, 1), 0.7), a(tailParam(kDriveAmount), 0.2)},
                         filter(0, 0, kLowPass, 1400, 0.35, 0.3), lfo(0, 2, kLfoSine, 5.5), lfo(0, 3, kLfoSine, 0.3),
                         env(0, 0, 500, 1000, 1.0, 1500),
                         routes(0, {{lfo2, oscParam(1, kOscShape), 0.4}, {lfo2, mixParam(kMixLevel, 1), -0.2}, {lfo1, kPartFine, 0.0},
                                    {press, depth(2), 0.03}, {wheel, oscParam(0, kOscShape), 0.3}, {wheel, cutoff, 0.15}})})});
      // [FreshAir] Wide saws under a filter resonant enough to whistle at the note; Press sets the
      // whistle wobbling, the wheel adds breath.
      p.push_back({"Fresh Air", "Monsta",
                   join({osc(0, 0, kSaw, 0, -25, 1.0), osc(0, 1, kSaw, 0, 25, 1.0), {a(mixParam(kMixPan, 0), -0.6), a(mixParam(kMixPan, 1), 0.6)},
                         filter(0, 0, kLadder, 1050, 0.85, 1.0), lfo(0, 2, kLfoSine, 6.0), env(0, 0, 300, 1000, 1.0, 1000),
                         routes(0, {{lfo1, cutoff, 0.0}, {press, depth(0), -0.08}, {wheel, mixParam(kNoiseLevel), 0.2}})})});
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
                         routes(0, {{env2, cutoff, 0.3}, {env2, kPartFine, 0.03}, {lfo1, oscParam(2, kOscFine), 0.0}, {press, depth(2), 0.15},
                                    {wheel, resonance, -0.3}})})});
      // [Opera2.0] A pad that sours as you press, one oscillator sinking flat; the wheel pulses in an
      // octave above.
      p.push_back({"Opera Pad", "Monsta",
                   join({osc(0, 0, kPulse, 0, 0, 0.5), osc(0, 1, kSaw, 12, 0, 1.0), osc(0, 2, kPulse, 12, 4, 0.0),
                         {a(oscParam(0, kOscShape), 0.3), a(mixParam(kNoiseLevel), 0.03), a(tailParam(kDriveAmount), 0.2),
                          a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5)},
                         filter(0, 0, kLowPass, 2000, 0.45, 1.0), lfo(0, 2, kLfoSine, 0.6), env(0, 0, 300, 1000, 1.0, 1200),
                         routes(0, {{press, oscParam(1, kOscFine), -0.1}, {lfo1, mixParam(kMixLevel, 2), 0.0}, {wheel, depth(1), 0.5},
                                    {wheel, cutoff, 0.1}})})});
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
                         routes(0, {{env2, mixParam(kMixLevel, 0), 0.9}, {lfo1, kPartFine, 0.02}, {env2, lfoParam(2, kLfoRate), 0.15},
                                    {press, depth(1), 0.02}, {wheel, lfoParam(2, kLfoRate), 0.1}})})});
      // [PinkHaze] A hazy, wide pad that glides between chords; Press pumps the filter down on a slow
      // LFO, and the wheel fades it back.
      p.push_back({"Pink Haze", "Monsta",
                   join({osc(0, 0, kSaw, 0, -20, 0.8), osc(0, 1, kPulse, 12, 20, 0.8), osc(0, 2, kPulse, 12, -15, 0.7),
                         {a(mixParam(kMixPan, 0), -0.6), a(mixParam(kMixPan, 1), 0.6), a(kGlideTime, timeValue(150)), a(kPartLevel, -3)},
                         filter(0, 0, kLowPass, 5000, 0.4, 0.5), lfo(0, 2, kLfoSine, 0.3), env(0, 0, 600, 1000, 1.0, 1500),
                         routes(0, {{lfo1, cutoff, 0.0}, {press, depth(0), -0.2}, {wheel, kPartLevel, -0.2}})})});

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

      // ---- Layers and Splits: both parts at once ----
      // Layer: the tine piano, and slow strings under it.
      p.push_back({"Piano and Strings", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, tines,
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2}}),
                         osc(1, 0, kSaw, 0, -9, 1.0), osc(1, 1, kSaw, 0, 9, 1.0),
                         {b(kPartTranspose, 0), b(mixParam(kMixPan, 0), -0.5), b(mixParam(kMixPan, 1), 0.5), b(kPartLevel, -9),
                          b(flockParam(kBehaviour), 0)},
                         filter(1, 0, kLowPass, 1800, 0.05, 0.5), lfo(1, 2, kLfoSine, 5.0), env(1, 0, 450, 1000, 1.0, 1200),
                         routes(1, {{lfo1, kPartFine, 0.015}, {wheel, kPartLevel, 0.1}, {press, cutoff, 0.2}})})});
      // Layer: an FM bell for the strike over a warm pad that swells in behind it.
      p.push_back({"Bell Pad", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 7.0, 0.0), {a(fm21, 0.25), a(fm31, 0.05)},
                         env(0, 0, 0.5, 3000, 0.0, 2000), env(0, 2, 0.5, 500, 0.0, 400), open(0),
                         routes(0, {{env3, fm21, 0.25}, {velocity, fm21, 0.2}}),
                         osc(1, 0, kSaw, 0, -7, 1.0), osc(1, 1, kSaw, 0, 7, 1.0),
                         {b(kPartTranspose, 0), b(mixParam(kMixPan, 0), -0.5), b(mixParam(kMixPan, 1), 0.5), b(kPartLevel, -8),
                          b(flockParam(kBehaviour), 0)},
                         filter(1, 0, kLowPass, 1200, 0.15, 0.3), lfo(1, 3, kLfoTriangle, 0.2), env(1, 0, 900, 1000, 1.0, 2500),
                         routes(1, {{wheel, cutoff, 0.3}, {lfo2, cutoff, 0.08}, {press, cutoff, 0.2}})})});
      // Split at C3: a bass below, the reed piano above.
      p.push_back({"Bass and Keys Split", "Layers and Splits",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kHalfSine, 1.0, 0.0), {a(fm21, 0.12), a(tailParam(kDriveAmount), 0.35)},
                         filter(0, 0, kLowPass, 4000, 0.0, 0.5), env(0, 0, 1, 2500, 0.15, 250), env(0, 2, 0.5, 400, 0.1, 200),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.3}}),
                         osc(1, 0, kSaw, 0, 0, 1.0), osc(1, 1, kSquare, -12, 0, 0.6),
                         {b(kPartTranspose, 0), b(envParam(1, kEnvVelocity), 0.5), b(flockParam(kBehaviour), 0)},
                         filter(1, 0, kLadder, 260, 0.25, 0.4), mono(1, 25), env(1, 0, 1, 300, 0.85, 90), env(1, 1, 1, 260, 0.15, 120),
                         routes(1, {{env2, cutoff, 0.35}, {wheel, cutoff, 0.3}})})});
      // Split at C3: an acid bass below, a saw lead above.
      p.push_back({"Bass and Lead Split", "Layers and Splits",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0)},
                         osc(0, 0, kSaw, 0, -6, 1.0), osc(0, 1, kSaw, 0, 6, 0.85), filter(0, 0, kLowPass, 2800, 0.2, 0.5),
                         lfo(0, 2, kLfoSine, 5.5), {a(lfoParam(2, kLfoDelay), timeValue(350))}, mono(0, 45),
                         env(0, 0, 3, 300, 1.0, 180), env(0, 1, 2, 400, 0.5, 200),
                         routes(0, {{env2, cutoff, 0.15}, {press, cutoff, 0.25}, {lfo1, kPartFine, 0.03}, {wheel, depth(2), 0.04}}),
                         osc(1, 0, kSaw, 0, 0, 1.0), filter(1, 0, kLadder, 220, 0.72, 0.3),
                         {b(kPartTranspose, 0), b(filterParam(0, kFilterDrive), 0.35), b(envParam(1, kEnvVelocity), 0.5),
                          b(flockParam(kBehaviour), 0)},
                         mono(1, 70), env(1, 0, 1, 300, 0.8, 60), env(1, 1, 1, 220, 0.0, 80),
                         routes(1, {{env2, cutoff, 0.45}, {velocity, cutoff, 0.12}, {wheel, resonance, 0.25}})})});
      // Velocity: play softly for a pad, dig in for a brass stab.
      p.push_back({"Velocity Stab", "Layers and Splits",
                   join({{g(kPlayMode, 4.0), g(kVelocitySplit, 96.0)},
                         osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0), {a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4)},
                         filter(0, 0, kLowPass, 900, 0.1, 0.3), env(0, 0, 300, 1000, 1.0, 1200),
                         routes(0, {{wheel, cutoff, 0.25}, {press, cutoff, 0.2}}),
                         osc(1, 0, kSaw, 0, -9, 1.0), osc(1, 1, kSaw, 0, 9, 1.0), osc(1, 2, kSaw, 12, 0, 0.35),
                         {b(kPartTranspose, 0), b(tailParam(kDriveAmount), 0.25), b(envParam(1, kEnvVelocity), 0.7), b(kPartLevel, 2)},
                         filter(1, 0, kLowPass, 700, 0.2, 0.4), env(1, 0, 4, 350, 0.5, 200), env(1, 1, 4, 250, 0.2, 200),
                         routes(1, {{env2, cutoff, 0.45}, {wheel, cutoff, 0.2}})})});
      // Expression: Press crossfades each note from a breathy flute (A) to bright brass (B).
      p.push_back({"Breath to Brass", "Layers and Splits",
                   join({{g(kPlayMode, 5.0), g(kCrossfadeSource, 1.0)},
                         osc(0, 0, kSine, 0, 0, 1.0), osc(0, 1, kTriangle, 12, 0, 0.15), {a(mixParam(kNoiseLevel), 0.06), a(kPartLevel, 2)},
                         filter(0, 0, kLowPass, 3000, 0.1, 0.5), lfo(0, 2, kLfoSine, 5.0), {a(lfoParam(2, kLfoDelay), timeValue(500))},
                         env(0, 0, 70, 300, 0.9, 300), routes(0, {{lfo1, kPartFine, 0.02}, {wheel, depth(0), 0.03}}),
                         osc(1, 0, kSaw, 0, -7, 1.0), osc(1, 1, kSaw, 0, 7, 0.9),
                         {b(kPartTranspose, 0), b(tailParam(kDriveAmount), 0.3)}, filter(1, 0, kLowPass, 900, 0.2, 0.4),
                         lfo(1, 2, kLfoSine, 5.0), env(1, 0, 40, 500, 0.9, 300),
                         routes(1, {{press, cutoff, 0.35}, {lfo1, kPartFine, 0.02}, {wheel, depth(1), 0.03}})})});
      // Layer: a warm pad, and an octave-up ring of sine bells that shimmer against each other.
      p.push_back({"Octave Shimmer", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)},
                         osc(0, 0, kSaw, 0, -7, 1.0), osc(0, 1, kSaw, 0, 7, 1.0), {a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5)},
                         filter(0, 0, kLowPass, 1100, 0.15, 0.3), env(0, 0, 600, 1000, 1.0, 2500), {a(kPartLevel, -3)},
                         routes(0, {{wheel, cutoff, 0.25}, {press, cutoff, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.0),
                         {b(kPartTranspose, 12), b(fm21, 0.1), b(flockParam(kBehaviour), 0), b(flockParam(kNeighbourFm), 0.35),
                          b(flockParam(kTunePull), 0.5), b(kSkyPan, 0.8), b(kPartLevel, -8)},
                         lfo(1, 2, kLfoSine, 3.0), env(1, 0, 1200, 1000, 1.0, 3000), open(1),
                         routes(1, {{lfo1, kPartLevel, 0.04}, {kSourceNearest, fm21, 0.2}, {wheel, fm21, 0.25}})})});
      // Part B as a plain second sound at the note's own pitch, not hunting A.
      const auto partB = [&](double level) {
         return Values{b(kPartTranspose, 0), b(flockParam(kBehaviour), 0), b(kPartLevel, level)};
      };
      // A warm pad of two saws, on either part.
      const auto warmPad = [&](int part, double hz, double attack, double release) {
         return join({osc(part, 0, kSaw, 0, -8, 1.0), osc(part, 1, kSaw, 0, 8, 1.0),
                      {pv(part, mixParam(kMixPan, 0), -0.5), pv(part, mixParam(kMixPan, 1), 0.5)},
                      filter(part, 0, kLowPass, hz, 0.1, 0.3), env(part, 0, attack, 1000, 1.0, release)});
      };
      // Layer: the piano, with a pad under it that Press brings in. Play it plain and it's a piano;
      // lean into a chord and the strings rise behind it.
      p.push_back({"Lean-In Piano", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, piano(0),
                         routes(0, {{env2, cutoff, 0.42}, {env3, mixParam(kNoiseLevel), 0.22}, {key, envParam(0, kEnvDecay), -0.1},
                                    {key, kPartPan, 0.25}, {wheel, cutoff, 0.12}}),
                         warmPad(1, 1500, 400, 1500), partB(-36),
                         routes(1, {{press, kPartLevel, 0.4}, {press, cutoff, 0.15}, {wheel, kPartLevel, 0.15}})})});
      // Layer: the tine piano, and strings that Press (or the wheel) swells in behind it.
      p.push_back({"EP and Strings", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, tinesOn(0),
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2},
                                    {key, envParam(0, kEnvDecay), -0.08}}),
                         warmPad(1, 2200, 500, 1500), osc(1, 2, kSaw, 12, -4, 0.3), lfo(1, 2, kLfoSine, 5.2),
                         {b(lfoParam(2, kLfoDelay), timeValue(400))}, partB(-34),
                         routes(1, {{press, kPartLevel, 0.38}, {wheel, kPartLevel, 0.15}, {lfo1, kPartFine, 0.015}, {press, cutoff, 0.1}})})});
      // Layer: the tine piano, and an FM bell an octave up that rings on after it.
      p.push_back({"EP and Bells", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, tinesOn(0),
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2},
                                    {key, envParam(0, kEnvDecay), -0.08}, {press, fm21, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 3.5, 0.0), {b(fm21, 0.2), b(kSkyPan, 0.5)}, partB(-11),
                         {b(kPartTranspose, 12)}, env(1, 0, 0.5, 3500, 0.0, 2000), env(1, 2, 0.5, 700, 0.0, 500), open(1),
                         routes(1, {{env3, fm21, 0.2}, {velocity, kPartLevel, 0.1}, {wheel, kPartLevel, 0.12}, {press, fm21, 0.15}})})});
      // Layer: the tine piano twice, the second a few cents sharp, a hair late and panned away, for a
      // wide chorus without a chorus. The wheel spreads them further.
      p.push_back({"Chorus EP", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, tinesOn(0), {a(kPartPan, -0.6), a(kPartFine, -4)},
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2},
                                    {key, envParam(0, kEnvDecay), -0.08}, {wheel, kPartFine, -0.03}}),
                         tinesOn(1), partB(0), {b(kPartPan, 0.6), b(kPartFine, 4), b(envParam(0, kEnvDelay), timeValue(14))},
                         routes(1, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2},
                                    {key, envParam(0, kEnvDecay), -0.08}, {wheel, kPartFine, 0.03}})})});
      // Layer: the tine piano and the reed piano at once, one each side; Press leans on the reed's bark.
      p.push_back({"Tine and Reed", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, tinesOn(0), {a(kPartPan, -0.45)},
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {velocity, fm31, 0.2},
                                    {key, envParam(0, kEnvDecay), -0.08}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kHalfSine, 1.0, 0.0), {b(fm21, 0.12), b(tailParam(kDriveAmount), 0.3)},
                         filter(1, 0, kLowPass, 3500, 0.0, 0.5), env(1, 0, 1, 2500, 0.15, 250), env(1, 2, 0.5, 400, 0.1, 200),
                         partB(-3), {b(kPartPan, 0.45)},
                         routes(1, {{env3, fm21, 0.3}, {velocity, fm21, 0.3}, {press, tailParam(kDriveAmount), 0.4}, {wheel, kPartLevel, 0.08}})})});
      // Velocity: a mellow tine piano with no bark, until a hard note switches to the driven reed.
      p.push_back({"Velocity Bark EP", "Layers and Splits",
                   join({{g(kPlayMode, 4.0), g(kVelocitySplit, 92.0)}, tinesOn(0),
                         routes(0, {{env2, fm21, 0.2}, {velocity, fm21, 0.1}, {key, envParam(0, kEnvDecay), -0.08}, {press, fm21, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kHalfSine, 1.0, 0.0), {b(fm21, 0.25), b(tailParam(kDriveAmount), 0.5)},
                         filter(1, 0, kLowPass, 4500, 0.0, 0.5), env(1, 0, 1, 2200, 0.15, 250), env(1, 2, 0.5, 400, 0.15, 200), partB(5),
                         routes(1, {{env3, fm21, 0.35}, {velocity, fm21, 0.2}, {press, tailParam(kDriveAmount), 0.3}})})});
      // Split at C3: a round FM bass below, the suitcase piano above.
      p.push_back({"Bass and EP Split", "Layers and Splits",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0)}, tinesOn(0), lfo(0, 2, kLfoSine, 4.5), {a(lfoParam(2, kLfoTrigger), 1)},
                         routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm21, 0.2}, {press, fm21, 0.35}, {lfo1, kPartPan, 0.3},
                                    {wheel, depth(4), 0.35}, {key, envParam(0, kEnvDecay), -0.08}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 1.0, 0.0), {b(fm21, 0.18), b(fb2, 0.2)}, partB(-2), mono(1, 0),
                         env(1, 0, 1, 500, 0.8, 80), env(1, 2, 0.5, 200, 0.1, 100), open(1),
                         routes(1, {{env3, fm21, 0.3}, {velocity, fm21, 0.15}, {press, fb2, 0.3}})})});
      // Layer: a formant choir, and a ring of FM bells an octave up for the attack.
      p.push_back({"Choir and Bells", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0), vox(0, 750, 1150, 0.45),
                         {a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4), a(lfoParam(2, kLfoDelay), timeValue(600)), a(kPartLevel, 4)},
                         lfo(0, 2, kLfoSine, 5.0), env(0, 0, 700, 1000, 1.0, 2000),
                         routes(0, {{lfo1, kPartFine, 0.015}, {wheel, cutoff2, -0.12}, {press, cutoff, 0.06}, {press, cutoff2, 0.08}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 3.5, 0.0), op(1, 2, kSine, 7.0, 0.0), {b(fm21, 0.2), b(fm31, 0.05)},
                         partB(-6), {b(kPartTranspose, 12), b(kSkyPan, 0.5)}, env(1, 0, 0.5, 3000, 0.0, 2000), env(1, 2, 0.5, 500, 0.0, 400), open(1),
                         routes(1, {{env3, fm21, 0.25}, {velocity, fm21, 0.15}})})});
      // Layer: the tonewheel organ, and a choir that Press (or the wheel) brings in over it.
      p.push_back({"Organ and Choir", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 0.5, 0.8), op(0, 2, kSine, 1.5, 0.7),
                         {a(kVelocityToAmp, 0.0), a(mixParam(kNoiseColor), 0.5), a(tailParam(kDriveType), 2), a(tailParam(kDriveAmount), 0.2),
                          a(kPartLevel, -12), a(lfoParam(2, kLfoTrigger), 1)},
                         lfo(0, 2, kLfoSine, 0.8), env(0, 0, 3, 100, 1.0, 40), env(0, 2, 0.5, 25, 0.0, 10), open(0),
                         routes(0, {{env3, mixParam(kNoiseLevel), 0.2}, {lfo1, kPartPan, 0.15}, {lfo1, kPartFine, 0.03}}),
                         osc(1, 0, kSaw, 0, -8, 1.0), osc(1, 1, kSaw, 0, 8, 1.0), vox(1, 420, 900, 0.35), partB(-22),
                         {b(mixParam(kMixPan, 0), -0.4), b(mixParam(kMixPan, 1), 0.4)}, env(1, 0, 600, 1000, 1.0, 1500),
                         routes(1, {{press, kPartLevel, 0.3}, {wheel, kPartLevel, 0.12}, {press, cutoff, 0.08}, {press, cutoff2, 0.1}})})});
      // Layer: a plucked harp, and slow strings under it.
      p.push_back({"Harp and Strings", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fm21, 0.08)},
                         env(0, 0, 1, 2400, 0.0, 1500), env(0, 2, 0.5, 500, 0.0, 400), open(0),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.1}}),
                         warmPad(1, 1800, 900, 2000), osc(1, 2, kSaw, 12, -4, 0.25), lfo(1, 2, kLfoSine, 5.0), partB(-10),
                         {b(lfoParam(2, kLfoDelay), timeValue(600))},
                         routes(1, {{lfo1, kPartFine, 0.015}, {press, cutoff, 0.2}, {wheel, kPartLevel, 0.12}})})});
      // Layer: a bright pluck, and a sine sub an octave under it that holds on as long as the key.
      p.push_back({"Sub and Pluck", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, osc(0, 0, kSaw, 0, -8, 1.0), osc(0, 1, kSaw, 0, 8, 1.0),
                         {a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4), a(envParam(1, kEnvVelocity), 0.4)},
                         filter(0, 0, kLowPass, 400, 0.3, 0.5), env(0, 0, 1, 600, 0.0, 400), env(0, 1, 0.5, 220, 0.0, 250),
                         routes(0, {{env2, cutoff, 0.5}, {press, resonance, 0.3}, {wheel, envParam(1, kEnvDecay), 0.15}}),
                         op(1, 0, kSine, 1.0, 1.0), partB(-4), {b(kPartTranspose, -12)}, env(1, 0, 3, 1000, 1.0, 200), open(1),
                         routes(1, {{press, kPartLevel, 0.06}})})});
      // Expression: Press crossfades each note from a clean pad (A) to a snarling, resonant one (B).
      p.push_back({"Pad to Growl", "Layers and Splits",
                   join({{g(kPlayMode, 5.0), g(kCrossfadeSource, 1.0)}, warmPad(0, 1300, 400, 1500),
                         routes(0, {{wheel, cutoff, 0.2}}),
                         osc(1, 0, kSaw, 0, -10, 1.0), osc(1, 1, kSquare, -12, 10, 0.8), filter(1, 0, kLadder, 900, 0.65, 0.3),
                         {b(tailParam(kDriveType), 2), b(tailParam(kDriveAmount), 0.6)}, lfo(1, 2, kLfoTriangle, 3.0), partB(5),
                         env(1, 0, 400, 1000, 1.0, 1500),
                         routes(1, {{press, cutoff, 0.25}, {lfo1, cutoff, 0.06}, {wheel, cutoff, 0.2}})})});
      // Expression: Slide (CC 74) crossfades from a whisper (A) to the sung vowel (B), and Press opens
      // the mouth. Without MPE the two sit half and half.
      p.push_back({"Whisper to Voice", "Layers and Splits",
                   join({{g(kPlayMode, 5.0), g(kCrossfadeSource, 0.0)}, osc(0, 0, kSaw, 0, 0, 0.0),
                         {a(mixParam(kNoiseLevel), 0.7), a(mixParam(kNoiseColor), 0.2), a(kPartLevel, 3)}, vox(0, 600, 1300, 0.5),
                         env(0, 0, 300, 1000, 1.0, 1500), routes(0, {{press, cutoff, 0.1}, {press, cutoff2, 0.1}}),
                         osc(1, 0, kSaw, 0, -6, 1.0), osc(1, 1, kSaw, 0, 6, 1.0), vox(1, 600, 1300, 0.55), partB(2),
                         lfo(1, 2, kLfoSine, 5.2), {b(lfoParam(2, kLfoDelay), timeValue(500))}, env(1, 0, 300, 1000, 1.0, 1500),
                         routes(1, {{press, cutoff, 0.1}, {press, cutoff2, 0.1}, {lfo1, kPartFine, 0.015}, {wheel, cutoff2, -0.12}})})});
      // Layer: ensemble strings, and brass on top that Press (or the wheel) pushes in.
      p.push_back({"Brass over Strings", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, warmPad(0, 2400, 250, 900), osc(0, 2, kSaw, 12, -4, 0.3), lfo(0, 2, kLfoSine, 5.2),
                         {a(lfoParam(2, kLfoDelay), timeValue(400)), a(kPartLevel, -6)},
                         routes(0, {{lfo1, kPartFine, 0.015}, {press, cutoff, 0.1}}),
                         osc(1, 0, kSaw, 0, -7, 1.0), osc(1, 1, kSaw, 0, 7, 0.9), filter(1, 0, kLowPass, 700, 0.15, 0.4), partB(-18),
                         {b(envParam(1, kEnvVelocity), 0.5)}, env(1, 0, 35, 500, 0.85, 250), env(1, 1, 70, 600, 0.45, 300),
                         routes(1, {{press, kPartLevel, 0.3}, {wheel, kPartLevel, 0.12}, {env2, cutoff, 0.35}, {press, cutoff, 0.15}})})});
      // Layer: a kalimba, and a soft pad under it that slowly fills in.
      p.push_back({"Kalimba Pad", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 5.95, 0.06), {a(fm21, 0.1)},
                         env(0, 0, 0.5, 1100, 0.0, 600), env(0, 2, 0.5, 60, 0.0, 50), open(0),
                         routes(0, {{env3, fm21, 0.3}, {velocity, fm21, 0.1}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.0), op(1, 2, kSine, 1.0, 0.5), partB(-9),
                         {b(oscParam(2, kOscFine), 7), b(kSkyPan, 0.6)}, env(1, 0, 1500, 1000, 1.0, 2500), env(1, 2, 2000, 1000, 1.0, 2500),
                         open(1), routes(1, {{env3, fm21, 0.15}, {press, fm21, 0.2}, {wheel, kPartLevel, 0.12}})})});
      // Velocity: the felt piano when played gently, a bright struck bell piano when played hard.
      p.push_back({"Felt and Glass", "Layers and Splits",
                   join({{g(kPlayMode, 4.0), g(kVelocitySplit, 90.0)},
                         osc(0, 0, kTriangle, 0, -3, 1.0), osc(0, 1, kSine, 0, 3, 0.8), osc(0, 2, kSaw, 12, 0, 0.1),
                         filter(0, 0, kLowPass, 220, 0.05, 0.8),
                         {a(filterParam(0, kFilterSlope), 0), a(envParam(1, kEnvVelocity), 0.6), a(mixParam(kNoiseColor), -0.5), a(kVelocityToAmp, 0.7)},
                         env(0, 0, 3, 4500, 0.0, 900), env(0, 1, 2, 900, 0.1, 600), env(0, 2, 0.5, 60, 0.0, 40),
                         routes(0, {{env2, cutoff, 0.3}, {env3, mixParam(kNoiseLevel), 0.3}, {key, envParam(0, kEnvDecay), -0.1}}),
                         tinesOn(1), op(1, 2, kSine, 7.0, 0.0), env(1, 2, 0.5, 900, 0.1, 600), partB(-5),
                         routes(1, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {velocity, fm31, 0.2}, {key, envParam(0, kEnvDecay), -0.08}})})});
      // Split at C3: a solo cello below, a breathy flute above.
      p.push_back({"Cello and Flute Split", "Layers and Splits",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0)},
                         osc(0, 0, kSine, 0, 0, 1.0), osc(0, 1, kTriangle, 12, 0, 0.15), {a(mixParam(kNoiseLevel), 0.05), a(mixParam(kNoiseColor), 0.3)},
                         filter(0, 0, kLowPass, 3500, 0.1, 0.5), {a(filterParam(0, kFilterSlope), 0)}, lfo(0, 2, kLfoSine, 5.0),
                         {a(lfoParam(2, kLfoDelay), timeValue(500))}, mono(0, 30), env(0, 0, 70, 300, 0.9, 200), env(0, 2, 1, 120, 0.0, 50),
                         routes(0, {{env3, mixParam(kNoiseLevel), 0.12}, {press, mixParam(kNoiseLevel), 0.12}, {press, cutoff, 0.2},
                                    {lfo1, kPartFine, 0.02}, {wheel, depth(3), 0.03}}),
                         osc(1, 0, kSaw, 0, 0, 1.0), osc(1, 1, kPulse, 0, 0, 0.5), {b(oscParam(1, kOscShape), 0.35)},
                         filter(1, 0, kLowPass, 1400, 0.2, 0.5), filter(1, 1, kHighPass, 90, 0.0, 0.0), lfo(1, 2, kLfoSine, 5.2),
                         {b(lfoParam(2, kLfoDelay), timeValue(350))}, mono(1, 80), env(1, 0, 110, 300, 1.0, 300), partB(6),
                         routes(1, {{lfo1, kPartFine, 0.03}, {press, cutoff, 0.25}, {press, depth(0), 0.03}, {wheel, depth(0), 0.04}})})});
      // Layer: a glassy FM pad, and a pure sine sub an octave down.
      p.push_back({"Glass and Sub", "Layers and Splits",
                   join({{g(kPlayMode, 1.0)}, op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 0.5, 0.4),
                         {a(fm21, 0.2), a(lfoParam(2, kLfoRate), lfoValue(0.3)), a(kSkyPan, 0.5)}, env(0, 0, 600, 1000, 1.0, 2500), open(0),
                         routes(0, {{lfo1, fm21, 0.15}, {wheel, fm21, 0.25}, {press, fm21, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), partB(-5), {b(kPartTranspose, -12)}, env(1, 0, 300, 1000, 1.0, 2000), open(1),
                         routes(1, {{press, kPartLevel, 0.05}})})});

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

      // Each preset's level, matched by measurement so stepping through the bank doesn't jump: the
      // loudest 400 ms of a held three-note chord at velocity 0.8 (one note for the mono and bass
      // patches) sits near -20 dBFS, a little lower for the plucks and lower again for the hits,
      // which are heard by their peaks. A trim raises or lowers every part's Level together, keeping a
      // layer's balance, and spills into the Master Level past the parts' +6 dB.
      static const std::pair<const char *, double> trims[] = {
         {"Sub Bass", 2.0},
         {"Analog Bass", 4.0},
         {"FM Bass", 5.0},
         {"Funk Bass", 6.5},
         {"Acid Bass", 7.0},
         {"Reese Bass", 5.5},
         {"Wobble Bass", 5.0},
         {"Saw Lead", 0.5},
         {"Square Lead", -1.0},
         {"Sync Lead", 7.0},
         {"Soft Flute", 4.0},
         {"FM Lead", 2.0},
         {"Pressure Lead", 2.5},
         {"Overdrive Lead", 11.0},
         {"Tine Piano", -1.0},
         {"Suitcase EP", -1.0},
         {"Reed Piano", 4.5},
         {"Clav", 4.0},
         {"Harpsichord", 2.0},
         {"Tonewheel Organ", 4.0},
         {"Church Organ", -1.0},
         {"Marimba", 5.5},
         {"Vibraphone", -2.0},
         {"Celesta", -0.5},
         {"Tubular Bells", -2.5},
         {"Crystal Bells", -1.5},
         {"Steel Drum", 5.0},
         {"Synth Pluck", 0.5},
         {"Bright Pluck", 2.5},
         {"Nylon Guitar", 3.0},
         {"Koto", 1.0},
         {"Kalimba", 2.0},
         {"Pizzicato", 4.5},
         {"Bell Pluck", -1.0},
         {"Synth Brass", -2.0},
         {"Soft Horns", -3.0},
         {"East Coast Brass", -2.0},
         {"Brass Stabs", 4.5},
         {"Ensemble Strings", -4.0},
         {"Slow Strings", -4.0},
         {"Solo Cello", 8.0},
         {"Sine Pad", -5.5},
         {"Warm Analog", 0.5},
         {"PWM Pad", -8.5},
         {"Glass Pad", -2.5},
         {"Choir Pad", 5.5},
         {"Bowed Glass", -3.5},
         {"Dawn Chorus", -3.5},
         {"Deep Current", -1.5},
         {"Trance Gate", -2.0},
         {"Sixteenth Pulse", 5.0},
         {"Sample and Hold", -5.5},
         {"Dotted Chords", -1.0},
         {"Wind Choir", 9.0},
         {"Deep Space", -2.5},
         {"Aurora", -2.0},
         {"Storm Front", -4.0},
         {"Kick", -5.5},
         {"Snare", 3.0},
         {"Hi-Hat", 7.5},
         {"Wood Block", 7.0},
         {"Log Drum", 3.5},
         {"Laser Zap", 2.0},
         {"Riser", -4.0},
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
         {"Piano and Strings", -1.0},
         {"Bell Pad", 1.0},
         {"Bass and Keys Split", 4.5},
         {"Bass and Lead Split", 0.5},
         {"Velocity Stab", 3.0},
         {"Breath to Brass", -3.0},
         {"Octave Shimmer", -2.0},
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
         {"Felt Piano", -3.0},
         {"Honky Tonk", -4.0},
         {"Dyno Tines", 1.5},
         {"Phaser EP", 0.0},
         {"Tremolo Tines", -0.5},
         {"Lo-Fi EP", 0.5},
         {"Glass EP", 0.0},
         {"Wurli Drive", 4.5},
         {"Talk Box", 18.0},
         {"Robot Chant", 10.0},
         {"Babble Choir", 8.0},
         {"Throat Singer", 17.5},
         {"Whisper Ghosts", 9.0},
         {"Alien Choir", 8.0},
         {"Yawning Giant", 18.0},
         {"Talking Pluck", 13.0},
         {"Monks", 9.0},
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
         {"Lean-In Piano", -1.0},
         {"EP and Strings", -0.5},
         {"EP and Bells", -2.5},
         {"Chorus EP", -3.5},
         {"Tine and Reed", -1.5},
         {"Velocity Bark EP", 2.0},
         {"Bass and EP Split", 2.0},
         {"Choir and Bells", 3.5},
         {"Organ and Choir", 7.0},
         {"Harp and Strings", 2.0},
         {"Sub and Pluck", 0.0},
         {"Pad to Growl", -3.0},
         {"Whisper to Voice", 9.5},
         {"Brass over Strings", 0.0},
         {"Kalimba Pad", 2.0},
         {"Felt and Glass", 1.0},
         {"Cello and Flute Split", 4.0},
         {"Glass and Sub", -4.5},
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
inline bool writePreset(const std::filesystem::path &file, const jclap::ui::ParamAccess &access) {
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
