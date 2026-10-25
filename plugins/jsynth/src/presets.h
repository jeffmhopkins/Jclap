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
      constexpr int kScatter = 1, kHunt = 2, kFlee = 3;
      constexpr int kSine = 0, kSaw = 2, kPulse = 4, kHalfSine = 5, kAltSine = 8;
      constexpr int kRatio = 1;
      const int env2 = kSourceEnv1 + 1, env3 = kSourceEnv1 + 2, lfo1 = kSourceLfo1, lfo2 = kSourceLfo1 + 1;
      const int strike = kSourceStrike, press = kSourceStrike + 1, slide = kSourceStrike + 3;
      const clap_id cutoff = filterParam(0, kFilterCutoff), resonance = filterParam(0, kFilterResonance);
      const clap_id fm21 = fmCell(1, 0), fm31 = fmCell(2, 0), fm32 = fmCell(2, 1), fb1 = fmCell(0, 0), fb2 = fmCell(1, 1);
      std::vector<Preset> p;

      p.push_back({"Init", "Init", {}});

      // The bank runs from the fastest envelopes to the slowest swells, a category at a time. Within
      // each, from a plain patch with nothing modulating it to ones the flock plays as much as you do.

      // ---- Hits: gone in a fraction of a second ----
      p.push_back({"Wood Block", "Hits",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.76, 0.0), {a(fm21, 0.35)}, env(0, 0, 0.5, 70, 0.0, 60),
                         open(0), routes(0, {})})});
      p.push_back({"Glass Tick", "Hits",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 7.0, 0.0), {a(fm21, 0.45)}, env(0, 0, 0.5, 140, 0.0, 120),
                         open(0), routes(0, {})})});
      p.push_back({"Marimba", "Hits",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.3), op(0, 2, kSine, 10.0, 0.0), {a(fm31, 0.05)},
                         env(0, 0, 0.5, 420, 0.0, 300), env(0, 2, 0.5, 35, 0.0, 30), open(0),
                         routes(0, {{env3, fm31, 0.4}, {strike, fm31, 0.15}})})});
      p.push_back({"Steel Drum", "Hits",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 2.0, 0.0),
                         {a(oscParam(1, kOscFine), 4), a(fm21, 0.2), a(fm31, 0.1)}, env(0, 0, 1, 520, 0.0, 350),
                         env(0, 2, 0.5, 140, 0.0, 100), open(0), routes(0, {{env3, fm21, 0.3}, {strike, fm31, 0.3}})})});
      p.push_back({"Log Drum", "Hits",
                   join({op(0, 0, kSine, 1.0, 1.0), {a(fb1, 0.15)}, env(0, 0, 0.5, 300, 0.0, 220), env(0, 2, 0.5, 45, 0.0, 40),
                         open(0),
                         // The hit drops in pitch, further the harder it is struck.
                         routes(0, {{env3, oscParam(0, kOscRatio), 0.1}, {strike, routeParam(0, kRouteAmount), 0.1}})})});
      p.push_back({"Fireflies", "Hits",
                   {a(flockParam(kBehaviour), kScatter), a(oscParam(0, kOscWave), kHalfSine), a(envParam(0, kEnvAttack), timeValue(1)),
                    a(envParam(0, kEnvDecay), timeValue(220)), a(envParam(0, kEnvSustain), 0.0),
                    a(flockParam(kSpeed), 0.8), a(flockParam(kWander), 0.7), a(flockParam(kTailLength), 0.15),
                    a(flockParam(kTailSpread), 0.2), a(lfoParam(0, kLfoRate), lfoValue(9.0)), a(kSkyPan, 0.9)}});
      p.push_back({"Metal Rain", "Hits",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.73, 0.0), op(0, 2, kSine, 7.11, 0.0),
                         {a(fm21, 0.35), a(fm32, 0.3), a(flockParam(kBehaviour), kScatter), a(flockParam(kWander), 0.6),
                          a(kSkyPan, 0.7)},
                         env(0, 0, 0.5, 240, 0.0, 500), env(0, 2, 0.5, 90, 0.0, 80), open(0),
                         routes(0, {{env3, fm21, 0.3},
                                    {kSourceRandom, oscParam(1, kOscRatio), 0.04},
                                    {kSourceRandom, oscParam(2, kOscRatio), -0.05},
                                    {strike, fm32, 0.3}})})});
      p.push_back({"Hunter Clicks", "Hits",
                   join({{g(kPlayMode, 1.0)},
                         op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0),
                         {a(fm21, 0.25), a(flockParam(kBehaviour), kFlee), a(flockParam(kSpeed), 0.7), a(kSkyPan, 0.6)},
                         env(0, 0, 0.5, 130, 0.0, 120), env(0, 2, 0.5, 50, 0.0, 40), open(0),
                         routes(0, {{env3, fm21, 0.4}, {kSourceCaught, fm21, 0.4}, {kSourceSpeed, oscParam(1, kOscRatio), 0.05}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 1.41, 0.0),
                         {b(kPartTranspose, 12), b(fm21, 0.4), b(flockParam(kSpeed), 0.85), b(flockParam(kBite), 0.7),
                          b(flockParam(kNeighbourFm), 0.4), b(kSkyPan, 0.6), b(kPartLevel, -4)},
                         env(1, 0, 0.5, 70, 0.0, 60), env(1, 2, 0.5, 30, 0.0, 30), open(1),
                         routes(1, {{env3, fm21, 0.4}, {kSourceNearest, fm21, 0.3}})})});

      // ---- Plucks: struck, ringing out over a second or two ----
      p.push_back({"Sine Pluck", "Plucks", join({op(0, 0, kSine, 1.0, 1.0), env(0, 0, 1, 900, 0.0, 500), open(0), routes(0, {})})});
      p.push_back({"Nylon", "Plucks",
                   join({{a(cutoff, 0.3), a(resonance, 0.05), a(filterParam(0, kFilterKeyTrack), 0.6)},
                         env(0, 0, 1, 1800, 0.0, 400), env(0, 1, 0.5, 350, 0.0, 300), routes(0, {{env2, cutoff, 0.4}})})});
      p.push_back({"Harp", "Plucks",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fm21, 0.08)}, env(0, 0, 1, 2400, 0.0, 1500),
                         env(0, 2, 0.5, 500, 0.0, 400), open(0), routes(0, {{env3, fm21, 0.3}})})});
      p.push_back({"Koto", "Plucks",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), {a(fm21, 0.15), a(fb1, 0.1)},
                         env(0, 0, 0.5, 1300, 0.0, 600), env(0, 2, 0.5, 180, 0.0, 150), open(0),
                         routes(0, {{env3, fm21, 0.4}, {strike, fm21, 0.2}})})});
      p.push_back({"Synth Pluck", "Plucks",
                   join({{a(oscParam(1, kOscWave), kPulse), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), 7),
                          a(mixParam(kMixLevel, 1), 0.6), a(cutoff, 0.3), a(resonance, 0.45)},
                         env(0, 0, 1, 800, 0.1, 300), env(0, 1, 0.5, 260, 0.0, 200),
                         routes(0, {{env2, cutoff, 0.5}, {strike, cutoff, 0.2}, {slide, resonance, 0.3}})})});
      p.push_back({"Bell Pluck", "Plucks",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.5, 0.0), op(0, 2, kSine, 7.0, 0.0),
                         {a(fm21, 0.25), a(fm32, 0.25), a(lfoParam(2, kLfoRate), lfoValue(0.7))},
                         env(0, 0, 0.5, 2600, 0.0, 1500), env(0, 2, 0.5, 600, 0.0, 500), open(0),
                         routes(0, {{env3, fm21, 0.3}, {press, fm32, 0.4}, {lfo1, oscParam(1, kOscFine), 0.03}})})});
      p.push_back({"Starling Bells", "Plucks",
                   join({{a(oscParam(0, kOscWave), kSine), a(oscParam(1, kOscWave), kSine), a(oscParam(1, kOscPitchMode), kRatio),
                          a(oscParam(1, kOscRatio), 3.5), a(fmCell(1, 0), 0.4), a(envParam(0, kEnvAttack), timeValue(2)),
                          a(envParam(0, kEnvDecay), timeValue(1800)), a(envParam(0, kEnvSustain), 0.0),
                          a(envParam(0, kEnvRelease), timeValue(1500)), a(flockParam(kNeighbourFm), 0.7),
                          a(flockParam(kTunePull), 0.4), a(flockParam(kCohesion), 0.7), a(flockParam(kTailLength), 0.7),
                          a(kSkyPan, 0.6)},
                         route(0, 6, kSourceCrowd, fmCell(1, 0), 0.3)})});
      p.push_back({"Dripping Ink", "Plucks",
                   join({op(0, 0, kAltSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0),
                         {a(fm21, 0.15), a(cutoff, 0.55), a(resonance, 0.3), a(flockParam(kTailLength), 1.0),
                          a(flockParam(kTailSpread), 0.9), a(flockParam(kWake), 0.8), a(kSkyPan, 0.5),
                          a(lfoParam(2, kLfoRate), lfoValue(0.8))},
                         env(0, 0, 0.5, 1400, 0.0, 1500), env(0, 2, 0.5, 400, 0.0, 400),
                         routes(0, {{env3, fm21, 0.3},
                                    {kSourceWake, cutoff, 0.35},
                                    {kSourceSpeed, fm21, 0.2},
                                    {kSourceHeading, resonance, 0.2},
                                    {lfo1, oscParam(1, kOscRatio), 0.02}})})});

      // ---- Keys: played like a keyboard, organs held, pianos and mallets dying away ----
      const Values organ = join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.5), op(0, 2, kSine, 3.0, 0.3),
                                 env(0, 0, 5, 100, 1.0, 60), open(0), {a(kPartLevel, -4)}});
      p.push_back({"Sine Organ", "Keys", join({organ, routes(0, {})})});
      p.push_back({"Tonewheel", "Keys",
                   join({organ, {a(fb1, 0.12), a(lfoParam(2, kLfoRate), lfoValue(6.5))}, routes(0, {{lfo1, kPartFine, 0.04}})})});
      p.push_back({"Celesta", "Keys",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 7.0, 0.0), {a(fm21, 0.2)}, env(0, 0, 0.5, 2500, 0.0, 600),
                         env(0, 2, 0.5, 200, 0.0, 200), open(0), routes(0, {{env3, fm21, 0.2}})})});
      p.push_back({"Clav", "Keys",
                   join({{a(kPartLevel, -5)}, {a(oscParam(0, kOscWave), kPulse), a(oscParam(0, kOscShape), 0.25), a(cutoff, 0.55), a(resonance, 0.35)},
                         env(0, 0, 0.5, 1200, 0.3, 60), env(0, 1, 0.5, 180, 0.0, 100),
                         routes(0, {{env2, cutoff, 0.35}, {strike, cutoff, 0.25}})})});
      const Values vibes = join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.0), {a(fm21, 0.15)},
                                 env(0, 0, 0.5, 3000, 0.0, 1200), env(0, 2, 0.5, 300, 0.0, 200), open(0)});
      p.push_back({"Vibes", "Keys",
                   join({vibes, {a(lfoParam(2, kLfoRate), lfoValue(5.5))}, routes(0, {{env3, fm21, 0.2}, {lfo1, kPartLevel, 0.05}})})});
      // A DX-style electric piano: a 1:1 pair for the body and a high ratio for the tine's bark.
      const Values tines = join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), op(0, 2, kSine, 14.0, 0.0),
                                 {a(fm21, 0.15)}, env(0, 0, 1, 5000, 0.0, 400), env(0, 1, 0.5, 1500, 0.2, 400),
                                 env(0, 2, 0.5, 60, 0.0, 40), open(0)});
      p.push_back({"Tine Piano", "Keys",
                   join({tines, routes(0, {{env2, fm21, 0.3}, {env3, fm31, 0.35}, {strike, fm21, 0.2}})})});
      // Pressure pushes the bark, and CC 74 sets how far the tremolo pans.
      p.push_back({"Suitcase Pressure", "Keys",
                   join({tines, {a(lfoParam(2, kLfoRate), lfoValue(4.5))},
                         routes(0, {{env2, fm21, 0.3},
                                    {env3, fm31, 0.35},
                                    {strike, fm21, 0.2},
                                    {press, fm21, 0.35},
                                    {lfo1, kPartPan, 0.0},
                                    {slide, routeParam(4, kRouteAmount), 0.6}})})});
      p.push_back({"Mallet Flock", "Keys",
                   join({{g(kPlayMode, 1.0)}, vibes, {a(flockParam(kNeighbourFm), 0.3), a(kSkyPan, 0.7)},
                         routes(0, {{env3, fm21, 0.2}, {kSourceCrowd, fm21, 0.2}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 4.0, 0.0),
                         {b(kPartTranspose, 12), b(flockParam(kBehaviour), 0), b(fm21, 0.2), b(flockParam(kNeighbourFm), 0.35),
                          b(flockParam(kTunePull), 0.5), b(kSkyPan, 0.7), b(kPartLevel, -6)},
                         env(1, 0, 0.5, 1200, 0.0, 800), env(1, 2, 0.5, 40, 0.0, 40), open(1),
                         routes(1, {{env3, fm21, 0.35}, {kSourceHeading, fm21, 0.2}})})});

      // ---- Bass and leads: held, speaking at once ----
      p.push_back({"Sine Bass", "Bass and Leads",
                   join({op(0, 0, kSine, 1.0, 1.0), {a(kPartTranspose, -12), a(fb1, 0.12)}, env(0, 0, 2, 200, 0.9, 80), open(0),
                         routes(0, {})})});
      p.push_back({"FM Bass", "Bass and Leads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(kPartTranspose, -12), a(fm21, 0.2), a(fb2, 0.2)},
                         env(0, 0, 1, 300, 1.0, 60), env(0, 2, 0.5, 220, 0.15, 100), open(0), routes(0, {{env3, fm21, 0.4}})})});
      p.push_back({"Saw Lead", "Bass and Leads",
                   join({{a(kPartLevel, -4)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), 8),
                          a(mixParam(kMixLevel, 1), 0.7), a(cutoff, 0.75), a(resonance, 0.25), a(lfoParam(2, kLfoRate), lfoValue(5.0)),
                          a(lfoParam(2, kLfoDelay), timeValue(350))},
                         env(0, 0, 5, 300, 1.0, 200), routes(0, {{lfo1, kPartFine, 0.05}})})});
      p.push_back({"Reed", "Bass and Leads",
                   join({{a(kPartLevel, -5)}, {a(oscParam(0, kOscWave), kPulse), a(oscParam(0, kOscShape), 0.35), a(cutoff, 0.5), a(resonance, 0.1)},
                         env(0, 0, 30, 300, 1.0, 120), env(0, 1, 30, 200, 0.4, 150),
                         routes(0, {{env2, cutoff, 0.25}, {press, cutoff, 0.3}})})});
      p.push_back({"East Coast Brass", "Bass and Leads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.0), {a(fb2, 0.3), a(fm21, 0.1)},
                         env(0, 0, 40, 400, 0.9, 200), env(0, 2, 60, 300, 0.6, 200), open(0),
                         routes(0, {{env3, fm21, 0.4}, {strike, fm21, 0.2}})})});
      // Pressure opens the FM and brings in vibrato; CC 74 adds grit through the modulator's feedback.
      p.push_back({"Pressure Lead", "Bass and Leads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), {a(fm21, 0.1), a(fb2, 0.1), a(lfoParam(2, kLfoRate), lfoValue(5.5))},
                         env(0, 0, 10, 300, 1.0, 250), open(0),
                         routes(0, {{press, fm21, 0.55}, {slide, fb2, 0.4}, {lfo1, kPartFine, 0.0}, {press, routeParam(2, kRouteAmount), 0.06}})})});
      p.push_back({"Shark Bass", "Bass and Leads",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0), b(flockParam(kBehaviour), kHunt), b(oscParam(0, kOscWave), kSaw),
                          b(oscParam(1, kOscWave), kPulse), b(mixParam(kMixLevel, 1), 0.7), b(filterParam(0, kFilterCutoff), 0.35),
                          b(filterParam(0, kFilterResonance), 0.5), b(flockParam(kSpeed), 0.85), b(flockParam(kBite), 0.8)},
                         route(1, 6, kSourceSpeed, filterParam(0, kFilterCutoff), 0.45)})});
      p.push_back({"Predator and Prey", "Bass and Leads",
                   join({{g(kPlayMode, 1.0), a(flockParam(kBehaviour), kFlee), b(flockParam(kBehaviour), kHunt),
                          b(oscParam(0, kOscWave), kPulse), b(kPartTranspose, -12), b(flockParam(kBite), 0.9),
                          a(flockParam(kSpeed), 0.6), b(flockParam(kSpeed), 0.7)},
                         route(0, 6, kSourceCaught, filterParam(0, kFilterCutoff), 0.6)})});

      // ---- Pads: rising in under a second, held ----
      p.push_back({"Sine Pad", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.0, 0.7),
                         {a(oscParam(1, kOscFine), 7), a(mixParam(kMixPan, 0), -0.4), a(mixParam(kMixPan, 1), 0.4)},
                         env(0, 0, 400, 1000, 1.0, 1500), open(0), routes(0, {})})});
      p.push_back({"Warm Analog", "Pads",
                   join({{a(kPartLevel, -4)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), 9),
                          a(mixParam(kMixLevel, 1), 0.9), a(mixParam(kMixPan, 0), -0.3), a(mixParam(kMixPan, 1), 0.3),
                          a(cutoff, 0.42), a(resonance, 0.15)},
                         env(0, 0, 500, 1000, 1.0, 2000), env(0, 1, 800, 2000, 0.5, 2000), routes(0, {{env2, cutoff, 0.15}})})});
      p.push_back({"Glass Pad", "Pads",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 3.0, 0.0), op(0, 2, kSine, 0.5, 0.4),
                         {a(fm21, 0.2), a(lfoParam(2, kLfoRate), lfoValue(0.3))}, env(0, 0, 600, 1000, 1.0, 2500), open(0),
                         routes(0, {{lfo1, fm21, 0.15}})})});
      p.push_back({"Wind Choir", "Pads",
                   join({{a(oscParam(0, kOscWave), kSaw), a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0),
                          a(oscParam(1, kOscFine), 11), a(mixParam(kMixLevel, 1), 0.9), a(mixParam(kNoiseLevel), 0.35),
                          a(filterParam(0, kFilterType), 2), a(filterParam(0, kFilterCutoff), 0.5),
                          a(filterParam(0, kFilterResonance), 0.4), a(kPartLevel, 6.0), a(lfoParam(1, kLfoPush), 0.9),
                          a(lfoParam(1, kLfoRate), lfoValue(0.1)), a(envParam(0, kEnvAttack), timeValue(600))},
                         route(0, 6, kSourceWind, filterParam(0, kFilterCutoff), 0.4),
                         route(0, 7, slide, filterParam(0, kFilterResonance), 0.3)})});
      p.push_back({"String Machine", "Pads",
                   join({{a(kPartLevel, -4)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), -8),
                          a(mixParam(kMixLevel, 1), 0.9), a(oscParam(2, kOscWave), kSaw), a(oscParam(2, kOscPitchMode), 0),
                          a(oscParam(2, kOscCoarse), 12), a(mixParam(kMixLevel, 2), 0.4), a(mixParam(kMixPan, 0), -0.4),
                          a(mixParam(kMixPan, 1), 0.4), a(cutoff, 0.55), a(resonance, 0.1),
                          a(lfoParam(2, kLfoRate), lfoValue(6.0)), a(lfoParam(3, kLfoRate), lfoValue(0.4))},
                         env(0, 0, 300, 1000, 1.0, 1200),
                         routes(0, {{lfo1, oscParam(1, kOscFine), 0.04}, {lfo2, oscParam(2, kOscFine), 0.05}, {press, cutoff, 0.25}})})});
      p.push_back({"Murmuration Pad", "Pads",
                   join({{a(oscParam(0, kOscWave), kSaw), a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscFine), 9),
                          a(mixParam(kMixLevel, 1), 0.8), a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5),
                          a(envParam(0, kEnvAttack), timeValue(900)), a(envParam(0, kEnvRelease), timeValue(3000)),
                          a(filterParam(0, kFilterCutoff), 0.45), a(flockParam(kAlignment), 0.8),
                          a(flockParam(kSpeed), 0.25), a(flockParam(kTailLength), 0.9), a(flockParam(kTailSpread), 0.8),
                          a(kSkyPan, 0.8), a(kDrift, 0.15)},
                         route(0, 6, kSourceHeading, kPartPan, 0.6)})});
      p.push_back({"Pressure Front", "Pads",
                   join({{a(oscParam(1, kOscWave), kSine), a(oscParam(1, kOscPitchMode), kRatio), a(oscParam(1, kOscRatio), 2.0),
                          a(flockParam(kNeighbourFm), 0.5), a(lfoParam(2, kLfoRate), lfoValue(6.0))},
                         route(0, 6, press, fmCell(1, 0), 0.6), route(0, 7, kSourceLfo1, kPartFine, 0.15)})});
      p.push_back({"Ink Ribbons", "Pads",
                   join({{a(oscParam(0, kOscWave), kAltSine), a(fmCell(0, 0), 0.25), a(flockParam(kTailLength), 1.0),
                          a(flockParam(kTailSpread), 0.9), a(flockParam(kWake), 0.8), a(flockParam(kLure), 0.35)},
                         route(0, 6, kSourceWake, filterParam(0, kFilterCutoff), 0.5),
                         route(0, 7, kSourceWake, fmCell(1, 0), 0.4)})});

      // ---- Swells: seconds to arrive ----
      p.push_back({"Slow Sine", "Swells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.3), env(0, 0, 3000, 1000, 1.0, 4000), open(0),
                         routes(0, {})})});
      p.push_back({"Bowed Glass", "Swells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 2.0, 0.0), env(0, 0, 2500, 1000, 1.0, 3000),
                         env(0, 2, 4000, 1000, 1.0, 3000), open(0), routes(0, {{env3, fm21, 0.35}})})});
      p.push_back({"Tidal Strings", "Swells",
                   join({{a(kPartLevel, -6)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), -7),
                          a(mixParam(kMixLevel, 1), 0.9), a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5),
                          a(cutoff, 0.3), a(resonance, 0.1), a(lfoParam(3, kLfoRate), lfoValue(0.15))},
                         env(0, 0, 3500, 1000, 1.0, 4000), env(0, 1, 5000, 1000, 1.0, 4000),
                         routes(0, {{env2, cutoff, 0.35}, {lfo2, kPartPan, 0.3}})})});
      // The vibrato only arrives once the note has bloomed.
      p.push_back({"Dawn Chorus", "Swells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 4.0, 0.0), op(0, 2, kSine, 1.0, 0.4),
                         {a(oscParam(2, kOscFine), 5), a(lfoParam(2, kLfoRate), lfoValue(5.0)), a(lfoParam(2, kLfoDelay), timeValue(2500))},
                         env(0, 0, 2000, 1000, 1.0, 3000), env(0, 2, 3000, 1000, 0.8, 3000), open(0),
                         routes(0, {{env3, fm21, 0.3}, {lfo1, kPartFine, 0.04}, {press, fm21, 0.25}})})});
      p.push_back({"Deep Current", "Swells",
                   join({{a(kPartLevel, 6)}, op(0, 1, kSine, 0.5, 0.6),
                         {a(kPartTranspose, -12), a(filterParam(0, kFilterType), 5), a(cutoff, 0.25), a(resonance, 0.5),
                          a(tailParam(kDriveType), 2), a(kDrift, 0.2), a(lfoParam(3, kLfoRate), lfoValue(0.07))},
                         env(0, 0, 2500, 1000, 1.0, 4000), env(0, 1, 6000, 1000, 1.0, 4000),
                         routes(0, {{env2, cutoff, 0.4}, {lfo2, resonance, 0.2}, {press, cutoff, 0.25}, {slide, tailParam(kDriveAmount), 0.4}})})});
      p.push_back({"Aurora", "Swells",
                   join({op(0, 0, kSine, 1.0, 1.0), op(0, 1, kSine, 1.5, 0.0), op(0, 2, kSine, 3.01, 0.0),
                         {a(fm21, 0.15), a(cutoff, 0.7), a(resonance, 0.2), a(lfoParam(2, kLfoRate), lfoValue(0.1)),
                          a(flockParam(kSpeed), 0.25), a(flockParam(kAlignment), 0.8), a(flockParam(kTunePull), 0.3), a(kSkyPan, 0.8)},
                         env(0, 0, 4000, 1000, 1.0, 5000), env(0, 2, 5000, 1000, 1.0, 4000),
                         routes(0, {{env3, fm32, 0.35},
                                    {kSourceHeading, fm21, 0.3},
                                    {lfo1, oscParam(2, kOscRatio), 0.01},
                                    {kSourceSkyY, cutoff, 0.25},
                                    {press, fm21, 0.2}})})});
      p.push_back({"Storm Front", "Swells",
                   join({{a(kPartLevel, 5)}, {a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0), a(oscParam(1, kOscFine), -12),
                          a(mixParam(kMixLevel, 1), 0.8), a(mixParam(kNoiseLevel), 0.4), a(mixParam(kNoiseColor), 0.3),
                          a(filterParam(0, kFilterType), 6), a(cutoff, 0.45), a(resonance, 0.5), a(filterParam(1, kFilterCutoff), 0.7),
                          a(tailParam(kDriveType), 2), a(lfoParam(1, kLfoPush), 0.8), a(lfoParam(1, kLfoRate), lfoValue(0.15)),
                          a(flockParam(kBehaviour), kScatter), a(kSkyPan, 0.6), a(kDrift, 0.2)},
                         env(0, 0, 2500, 1000, 1.0, 4000), env(0, 1, 3000, 1000, 1.0, 3000),
                         routes(0, {{kSourceWind, cutoff, 0.25},
                                    {kSourceWake, resonance, 0.3},
                                    {kSourceSpeed, filterParam(1, kFilterCutoff), 0.3},
                                    {env2, mixParam(kNoiseLevel), 0.3},
                                    {press, tailParam(kDriveAmount), 0.4}})})});
      p.push_back({"Murmuration Storm", "Swells",
                   join({{a(kPartLevel, -5)}, {g(kPlayMode, 1.0), a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscPitchMode), 0),
                          a(oscParam(1, kOscFine), 9), a(mixParam(kMixLevel, 1), 0.9), a(cutoff, 0.4), a(resonance, 0.2),
                          a(flockParam(kNeighbourFm), 0.5), a(flockParam(kTunePull), 0.5), a(kSkyPan, 0.8)},
                         env(0, 0, 3000, 1000, 1.0, 4000), env(0, 1, 3000, 1000, 1.0, 4000),
                         routes(0, {{env2, cutoff, 0.3},
                                    {kSourceCrowd, cutoff, 0.3},
                                    {kSourceNearest, kPartPan, 0.3},
                                    {kSourceSpeed, fm21, 0.2},
                                    {press, routeParam(1, kRouteAmount), 0.3}}),
                         op(1, 0, kSine, 1.0, 1.0), op(1, 1, kSine, 2.0, 0.0),
                         {b(fm21, 0.2), b(flockParam(kBite), 0.4), b(flockParam(kNeighbourFm), 0.3), b(kSkyPan, 0.6), b(kPartLevel, -8)},
                         env(1, 0, 3000, 1000, 1.0, 4000), env(1, 2, 4000, 1000, 1.0, 3000), open(1),
                         routes(1, {{kSourceCaught, fm21, 0.4}, {env3, fm21, 0.3}, {kSourceHeading, oscParam(1, kOscFine), 0.1}})})});
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

constexpr const char *kPresetHeader = "JSynth preset 1";
constexpr const char *kPresetExtension = ".jspreset";

// Reads a user preset; false if the file isn't one.
inline bool readPreset(const std::filesystem::path &file, Preset &out) {
   std::ifstream in(file);
   std::string line;
   if (!in || !std::getline(in, line) || line != kPresetHeader)
      return false;
   out = {file.stem().string(), "User", {}};
   while (std::getline(in, line)) {
      std::istringstream fields(line);
      unsigned id = 0;
      double v = 0.0;
      if (fields >> id >> v && findParam(id) && std::isfinite(v))
         out.values.emplace_back(static_cast<clap_id>(id), v);
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
