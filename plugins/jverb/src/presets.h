#pragma once

// JVerb's presets: a factory bank compiled in, and the user's own saved as small text files.
//
// A factory preset is written as what it changes from the defaults, so the bank stays readable,
// then filled out with every other param at its default, so it holds the whole sound and nothing
// from the previous one (a body's routing, say) carries over. A user preset saves every param.
// Loading one is a normal edit of every param (the host sees each change, and can undo it)
// followed by a restart of the orbits from the new masses and velocities.
//
// A user preset file is plain text: a "JVerb preset 1" line, then one "<param id> <value>" per line.

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

namespace jverb {

struct Preset {
   std::string name;
   std::string category;
   std::vector<std::pair<clap_id, double>> values;
};

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      // Body params by role: body(b, kBody1Target, Target::Echo)...
      const auto body = [](int b, clap_id role, double v) { return std::pair<clap_id, double>{bodyParam(b, role), v}; };
      const auto target = [&](int b, Target t, double amount) {
         return std::vector<std::pair<clap_id, double>>{body(b, kBody1Target, static_cast<double>(t)),
                                                       body(b, kBody1Amount, amount)};
      };
      const auto with = [](std::vector<std::pair<clap_id, double>> a, const std::vector<std::pair<clap_id, double>> &b) {
         a.insert(a.end(), b.begin(), b.end());
         return a;
      };
      constexpr double kInto = 0, kLoopSlot = 1, kAfter = 2;
      constexpr double kPreSlot = 0, kInLoop = 1, kPostSlot = 2;
      constexpr double kTape = 0, kTube = 1, kFuzz = 2, kFold = 3, kCrush = 4;
      std::vector<Preset> p;

      // The bank runs from the classic spaces (rooms, plates, halls) through what JVerb adds to them
      // (echoes, shimmer, orbits, drive) to washes that ring for most of a minute and the extremes.
      // The classics keep Motion low and the bodies routed nowhere, so they sound like real spaces;
      // the later categories each show off one part of the engine.

      // Rooms: natural spaces under about a second and a half.
      p.push_back({"Init", "Rooms", {}});
      p.push_back({"Vocal Booth", "Rooms",
                   {{kDecay, 0.3}, {kSize, 0.25}, {kMotion, 0.1}, {kDiffusion, 0.6}, {kEarlyLate, 0.3},
                    {kPredelay, 0}, {kDampHigh, 9000}, {kLowCut, 120}, {kSmear, 0.1}, {kMix, 0.2}}});
      p.push_back({"Studio Room", "Rooms",
                   {{kDecay, 0.55}, {kSize, 0.4}, {kMotion, 0.15}, {kDiffusion, 0.7}, {kEarlyLate, 0.45},
                    {kPredelay, 2}, {kDampHigh, 8000}, {kBassMult, 0.9}, {kSmear, 0.15}, {kMix, 0.22}}});
      p.push_back({"Wood Room", "Rooms",
                   {{kDecay, 0.75}, {kSize, 0.35}, {kMotion, 0.15}, {kDiffusion, 0.75}, {kEarlyLate, 0.5},
                    {kPredelay, 3}, {kDampHigh, 5500}, {kBassMult, 0.85}, {kSmear, 0.15}, {kMix, 0.25}}});
      p.push_back({"Live Room", "Rooms",
                   {{kDecay, 1.1}, {kSize, 0.6}, {kMotion, 0.2}, {kDiffusion, 0.8}, {kEarlyLate, 0.55},
                    {kPredelay, 6}, {kDampHigh, 7500}, {kBassMult, 0.9}, {kLowCut, 70}, {kMix, 0.25}}});
      p.push_back({"Drum Reactor", "Rooms",
                   with({{kReact, 1}, {kDecay, 1.2}, {kSize, 0.6}, {kMotion, 0.3}, {kEarlyLate, 0.6}, {kDuck, 0.4},
                         {kLowCut, 90}, {kMix, 0.3}},
                        target(0, Target::Damping, 0.16))});
      p.push_back({"Gated Snare", "Rooms",
                   {{kDecay, 0.4}, {kSize, 1.2}, {kMotion, 0.1}, {kDiffusion, 1}, {kEarlyLate, 0.85}, {kPredelay, 0},
                    {kDampHigh, 10000}, {kBassMult, 0.5}, {kLowCut, 160}, {kSmear, 0.5}, {kMix, 0.35}}});

      // Plates: dense from the first moment, bright, no early reflections to speak of.
      p.push_back({"Vocal Plate", "Plates",
                   {{kDecay, 1.8}, {kSize, 0.55}, {kMotion, 0.3}, {kDiffusion, 0.95}, {kEarlyLate, 1},
                    {kPredelay, 25}, {kDampHigh, 11000}, {kBassMult, 0.8}, {kLowCut, 180}, {kDuck, 0.3},
                    {kMix, 0.25}}});
      p.push_back({"Snare Plate", "Plates",
                   {{kDecay, 1.3}, {kSize, 0.5}, {kMotion, 0.25}, {kDiffusion, 1}, {kEarlyLate, 1}, {kPredelay, 5},
                    {kDampHigh, 14000}, {kBassMult, 0.7}, {kLowCut, 200}, {kMix, 0.3}}});
      p.push_back({"Figure-Eight Plate", "Plates",
                   {{kDecay, 2.4}, {kSize, 0.6}, {kMotion, 0.35}, {kDiffusion, 0.95}, {kEarlyLate, 0.95},
                    {kPredelay, 10}, {kDampHigh, 12500}, {kLowCut, 120}, {kMix, 0.3}}});
      p.push_back({"Gold Plate", "Plates",
                   {{kDecay, 3.2}, {kSize, 0.7}, {kMotion, 0.5}, {kDiffusion, 1}, {kEarlyLate, 1}, {kPredelay, 12},
                    {kDampHigh, 9500}, {kBassMult, 0.9}, {kLowCut, 100}, {kSmear, 0.45}, {kWidth, 1.2},
                    {kMix, 0.3}}});
      p.push_back({"Dark Plate", "Plates",
                   {{kDecay, 2.6}, {kSize, 0.65}, {kMotion, 0.35}, {kDiffusion, 0.95}, {kEarlyLate, 1},
                    {kPredelay, 8}, {kDampHigh, 3800}, {kBassMult, 1.1}, {kLowCut, 90}, {kMix, 0.3}}});
      p.push_back({"Orbit Plate", "Plates",
                   with(with({{kDecay, 2.2}, {kSize, 0.6}, {kBodies, 4}, {kMotion, 0.4}, {kDiffusion, 0.9},
                              {kEarlyLate, 1}, {kPredelay, 10}, {kDampHigh, 11000}, {kLowCut, 120}, {kOrbitPan, 1},
                              {kMix, 0.3}},
                             target(0, Target::Width, 0.3)),
                        target(2, Target::Damping, 0.2))});

      // Halls and chambers: early reflections first, then a long, darker tail with a lift in the bass.
      p.push_back({"Echo Chamber", "Halls",
                   {{kDecay, 1.6}, {kSize, 0.8}, {kMotion, 0.2}, {kDiffusion, 0.8}, {kEarlyLate, 0.6},
                    {kPredelay, 8}, {kDampHigh, 6000}, {kBassMult, 1}, {kMix, 0.28}}});
      p.push_back({"Stone Chamber", "Halls",
                   {{kDecay, 2.2}, {kSize, 1}, {kMotion, 0.2}, {kDiffusion, 0.7}, {kEarlyLate, 0.5}, {kPredelay, 12},
                    {kDampHigh, 5000}, {kBassMult, 1.3}, {kMix, 0.3}}});
      p.push_back({"Vocal Hall", "Halls",
                   {{kDecay, 2.8}, {kSize, 1.3}, {kMotion, 0.4}, {kDiffusion, 0.8}, {kPredelay, 35}, {kDampHigh, 8000},
                    {kLowCut, 150}, {kDuck, 0.35}, {kMix, 0.28}}});
      p.push_back({"Concert Hall", "Halls",
                   {{kDecay, 2.6}, {kSize, 1.6}, {kMotion, 0.3}, {kDiffusion, 0.85}, {kEarlyLate, 0.8},
                    {kPredelay, 28}, {kDampHigh, 6500}, {kBassMult, 1.35}, {kBassXover, 300}, {kMix, 0.3}}});
      p.push_back({"Strings Hall", "Halls",
                   {{kDecay, 3.4}, {kSize, 1.9}, {kMotion, 0.5}, {kDiffusion, 0.85}, {kEarlyLate, 0.85},
                    {kAttack, 0.15}, {kPredelay, 40}, {kDampHigh, 5500}, {kBassMult, 1.3}, {kMix, 0.32}}});
      p.push_back({"Grand Hall", "Halls",
                   {{kDecay, 4.5}, {kSize, 2.4}, {kMotion, 0.4}, {kDiffusion, 0.85}, {kEarlyLate, 0.85},
                    {kPredelay, 45}, {kDampHigh, 5800}, {kBassMult, 1.4}, {kLowCut, 60}, {kMix, 0.35}}});
      p.push_back({"Cathedral Drift", "Halls",
                   {{kDecay, 13}, {kSize, 2.6}, {kGravity, 0.7}, {kMotion, 0.5}, {kDiffusion, 0.85}, {kPredelay, 45},
                    {kDampHigh, 5200}, {kBassMult, 1.5}, {kEarlyLate, 0.85}, {kMix, 0.4}}});

      // Echoes: the echo in each of its slots, from slapback to a tail that returns once a bar.
      p.push_back({"Swirling Slapback", "Echoes",
                   with({{kEchoOn, 1}, {kEchoWhere, kInto}, {kEchoSync, 0}, {kEchoTime, 140}, {kEchoFeedback, 0.25},
                         {kEchoPingPong, 0}, {kEchoTone, 0.2}, {kEchoMix, 0.7}, {kDecay, 1.4}, {kSize, 0.7},
                         {kMix, 0.3}},
                        target(0, Target::Damping, 0.2))});
      p.push_back({"Ping-Pong Space", "Echoes",
                   {{kEchoOn, 1}, {kEchoWhere, kAfter}, {kEchoSync, 7}, {kEchoFeedback, 0.5}, {kEchoPingPong, 1},
                    {kEchoMix, 0.5}, {kDecay, 2.5}, {kMix, 0.4}}});
      p.push_back({"Triplet Rain", "Echoes",
                   {{kEchoOn, 1}, {kEchoWhere, kAfter}, {kEchoSync, 2}, {kEchoFeedback, 0.55}, {kEchoPingPong, 1},
                    {kEchoTone, 0.3}, {kEchoMix, 0.45}, {kDecay, 1.8}, {kSize, 0.55}, {kDiffusion, 0.95},
                    {kEarlyLate, 1}, {kDampHigh, 12000}, {kLowCut, 150}, {kMix, 0.35}}});
      p.push_back({"Dotted Echo Hall", "Echoes",
                   {{kEchoOn, 1}, {kEchoWhere, kInto}, {kEchoSync, 6}, {kEchoFeedback, 0.45}, {kEchoMix, 0.55},
                    {kDecay, 3}, {kSize, 1.4}, {kMix, 0.35}}});
      p.push_back({"Dub Chamber", "Echoes",
                   {{kEchoOn, 1}, {kEchoWhere, kInto}, {kEchoSync, 7}, {kEchoFeedback, 0.7}, {kEchoPingPong, 0},
                    {kEchoTone, -0.7}, {kEchoMix, 0.55}, {kDriveOn, 1}, {kDriveWhere, kPreSlot}, {kDriveType, kTape},
                    {kDrive, 6}, {kDecay, 2}, {kSize, 0.9}, {kEarlyLate, 0.6}, {kDampHigh, 4500}, {kLowCut, 90},
                    {kMix, 0.4}}});
      p.push_back({"Tape Loop Orbit", "Echoes",
                   with({{kEchoOn, 1}, {kEchoWhere, kLoopSlot}, {kEchoSync, 12}, {kEchoFeedback, 0.35},
                         {kEchoTone, -0.5}, {kEchoMix, 0.8}, {kDriveOn, 1}, {kDriveWhere, kInLoop},
                         {kDriveType, kTape}, {kDrive, 8}, {kFeedback, 0.65}, {kDecay, 6}, {kSize, 1.6}, {kMix, 0.45}},
                        target(2, Target::Tilt, 0.1))});
      p.push_back({"Infinite Return", "Echoes",
                   {{kEchoOn, 1}, {kEchoWhere, kLoopSlot}, {kEchoSync, 10}, {kEchoFeedback, 0.7}, {kEchoMix, 0.7},
                    {kFeedback, 0.7}, {kShimmer, 0.2}, {kDecay, 10}, {kSize, 2}, {kMix, 0.45}}});

      // Shimmer and swell: pitch in the loop, slow blooms and backwards starts.
      p.push_back({"Swell Machine", "Shimmer",
                   {{kDecay, 6}, {kSize, 1.8}, {kAttack, 0.85}, {kDiffusion, 0.9}, {kMotion, 0.5}, {kMix, 0.45}}});
      p.push_back({"Reverse Bloom", "Shimmer",
                   {{kDecay, 4}, {kSize, 1.4}, {kAttack, -0.8}, {kDiffusion, 0.9}, {kEarlyLate, 1}, {kMotion, 0.4},
                    {kDampHigh, 8000}, {kMix, 0.4}}});
      p.push_back({"Shimmer Halo", "Shimmer",
                   {{kDecay, 9}, {kSize, 2.2}, {kShimmer, 0.45}, {kFeedback, 0.85}, {kShimmerPitch, 12}, {kTilt, 0.2},
                    {kAttack, 0.3}, {kDiffusion, 0.85}, {kMix, 0.42}}});
      p.push_back({"Fifth Cathedral", "Shimmer",
                   {{kDecay, 10}, {kSize, 2.4}, {kShimmer, 0.35}, {kShimmerPitch, 7}, {kFeedback, 0.8},
                    {kAttack, 0.25}, {kDiffusion, 0.9}, {kEarlyLate, 0.9}, {kPredelay, 40}, {kDampHigh, 6500},
                    {kMix, 0.42}}});
      p.push_back({"Ice Crystals", "Shimmer",
                   {{kDecay, 8}, {kSize, 1.6}, {kShimmer, 0.3}, {kShimmerPitch, 24}, {kFeedback, 0.7},
                    {kDiffusion, 0.95}, {kEarlyLate, 1}, {kDampHigh, 12000}, {kLowCut, 300}, {kTilt, 0.3},
                    {kWidth, 1.4}, {kMix, 0.4}}});
      p.push_back({"Octave Under", "Shimmer",
                   {{kDecay, 10}, {kSize, 2.4}, {kShimmer, 0.4}, {kShimmerPitch, -12}, {kFeedback, 0.8},
                    {kDampHigh, 4000}, {kMix, 0.4}}});
      p.push_back({"Choir Cloud", "Shimmer",
                   {{kDecay, 12}, {kSize, 2.8}, {kShimmer, 0.3}, {kShimmerPitch, 12}, {kFeedback, 0.75},
                    {kAttack, 0.6}, {kMotion, 0.8}, {kDiffusion, 0.95}, {kEarlyLate, 1}, {kDampHigh, 6000},
                    {kOrbitSwirl, 1}, {kMix, 0.45}}});

      // Orbits: the bodies front and centre, moving the tail around the stereo field, its tone and its
      // length.
      p.push_back({"Small Orbit", "Orbits",
                   {{kDecay, 1}, {kSize, 0.45}, {kBodies, 4}, {kMotion, 0.25}, {kDiffusion, 0.8}, {kEarlyLate, 0.55},
                    {kPredelay, 4}, {kDampHigh, 9500}, {kMix, 0.25}}});
      p.push_back({"Binary Star", "Orbits",
                   with({{kDecay, 3.2}, {kBodies, 2}, {kMotion, 0.6}, {kSize, 1.2}, {kMix, 0.35},
                         body(0, kBody1Mass, 1.4), body(1, kBody1Mass, 1.1)},
                        target(1, Target::Width, 0.24))});
      p.push_back({"Heavy Moon", "Orbits",
                   {{kBodies, 3}, {kMotion, 0.6}, {kDecay, 5}, {kSize, 1.5}, body(0, kBody1Mass, 2.8),
                    body(1, kBody1Mass, 0.3), body(2, kBody1Mass, 0.4), {kMix, 0.4}}});
      p.push_back({"Synced Orbit", "Orbits",
                   with({{kClock, 1}, {kSyncBars, 2}, {kMotion, 0.6}, {kDecay, 3.5}, {kSize, 1.3}, {kMix, 0.4}},
                        target(2, Target::Tilt, 0.16))});
      p.push_back({"Comet Pass", "Orbits",
                   with({{kDecay, 4}, {kSize, 1.4}, {kMotion, 0.5}, {kOrbitTone, 1.2}, {kMix, 0.38},
                         body(0, kBody1Mass, 2), body(1, kBody1Mass, 1.6), body(2, kBody1Mass, 0.15),
                         body(2, kBody1Velocity, 1.4)},
                        target(2, Target::Damping, 0.6))});
      p.push_back({"Tidal Breath", "Orbits",
                   with(with({{kDecay, 5}, {kSize, 1.8}, {kBodies, 2}, {kGravity, 0.5}, {kMotion, 0.5},
                              {kDiffusion, 0.85}, {kEarlyLate, 0.9}, {kMix, 0.4}},
                             target(0, Target::Decay, 0.35)),
                        target(1, Target::EarlyLate, -0.3))});
      p.push_back({"Six Moons", "Orbits",
                   {{kBodies, 6}, {kDecay, 5}, {kSize, 1.6}, {kMotion, 0.5}, {kOrbitPan, 1}, {kOrbitTone, 1},
                    {kOrbitSwirl, 0.9}, {kDiffusion, 0.85}, {kMix, 0.4}}});
      p.push_back({"Pulsar", "Orbits",
                   with(with({{kClock, 1}, {kSyncBars, 1}, {kGravity, 2}, {kBodies, 4}, {kDecay, 3}, {kSize, 1.2},
                              {kOrbitPan, 1.2}, {kMotion, 0.4}, {kMix, 0.38}},
                             target(1, Target::Width, 0.5)),
                        target(3, Target::Tilt, -0.4))});
      p.push_back({"Slow Ballet", "Orbits",
                   {{kDecay, 8}, {kSize, 2}, {kGravity, 0.35}, {kMotion, 0.6}, {kDiffusion, 0.85}, {kSmear, 0.6},
                    {kMix, 0.4}}});
      p.push_back({"Chaos Engine", "Orbits",
                   with(with({{kBodies, 6}, {kGravity, 2.5}, {kMotion, 0.8}, {kReact, 0.8}, {kDecay, 4},
                              {kFeedback, 0.3}, {kMix, 0.45}},
                             target(0, Target::Damping, 0.24)),
                        target(3, Target::Feedback, 0.24))});

      // Texture: the drive at the input, in the loop and on the tail, and a lo-fi radio.
      p.push_back({"Hot Input", "Texture",
                   {{kDriveOn, 1}, {kDriveWhere, kPreSlot}, {kDriveType, kTape}, {kDrive, 15}, {kDriveTone, 0.3},
                    {kDecay, 1.5}, {kSize, 0.6}, {kMix, 0.35}}});
      p.push_back({"Lo-Fi Radio", "Texture",
                   {{kDriveOn, 1}, {kDriveWhere, kPostSlot}, {kDriveType, kCrush}, {kDrive, 6}, {kDriveBias, 0.35},
                    {kDriveMix, 0.8}, {kLowCut, 450}, {kHighCut, 4000}, {kDecay, 1.6}, {kSize, 0.7},
                    {kMotion, 0.6}, {kMix, 0.4}}});
      p.push_back({"Burning Loop", "Texture",
                   {{kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kTube}, {kDrive, 18}, {kFeedback, 0.55},
                    {kTilt, -0.3}, {kDecay, 4}, {kMix, 0.4}}});
      p.push_back({"Crushed Satellites", "Texture",
                   {{kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kCrush}, {kDriveBias, 0.5}, {kDrive, 14},
                    {kFeedback, 0.4}, {kBodies, 5}, {kDecay, 4}, {kMix, 0.4}}});
      p.push_back({"Tape Ghosts", "Texture",
                   {{kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kTape}, {kDrive, 12}, {kFeedback, 0.6},
                    {kEchoOn, 1}, {kEchoWhere, kLoopSlot}, {kEchoSync, 10}, {kEchoFeedback, 0.3}, {kEchoTone, -0.6},
                    {kEchoMix, 0.6}, {kMotion, 0.95}, {kTilt, -0.5}, {kDecay, 6}, {kSize, 1.5}, {kDampHigh, 4500},
                    {kMix, 0.42}}});
      p.push_back({"Fuzz Cloud", "Texture",
                   {{kDriveOn, 1}, {kDriveWhere, kPostSlot}, {kDriveType, kFuzz}, {kDrive, 24}, {kDriveMix, 0.6},
                    {kDriveTrim, -6}, {kDecay, 7}, {kSize, 2}, {kMix, 0.45}}});
      p.push_back({"Folded Space", "Texture",
                   {{kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kFold}, {kDrive, 20}, {kShimmer, 0.25},
                    {kFeedback, 0.8}, {kDecay, 7}, {kSize, 1.8}, {kMix, 0.4}}});

      // Washes: big sizes, high diffusion, slow attacks and long decays, for Supermassive-style pads
      // that keep blooming long after the note.
      p.push_back({"Nebula", "Washes",
                   {{kDecay, 22}, {kSize, 3}, {kMotion, 0.55}, {kDiffusion, 0.9}, {kAttack, 0.35}, {kEarlyLate, 0.95},
                    {kPredelay, 30}, {kDampHigh, 6500}, {kBassMult, 1.1}, {kSmear, 0.5}, {kWidth, 1.3}, {kMix, 0.5}}});
      p.push_back({"Aurora", "Washes",
                   with({{kDecay, 25}, {kSize, 3.5}, {kMotion, 0.6}, {kDiffusion, 0.95}, {kAttack, 0.45},
                         {kEarlyLate, 1}, {kPredelay, 30}, {kDampHigh, 7500}, {kShimmer, 0.15}, {kShimmerPitch, 12},
                         {kFeedback, 0.4}, {kTilt, 0.2}, {kOrbitTone, 1.2}, {kWidth, 1.4}, {kMix, 0.5}},
                        target(1, Target::Shimmer, 0.35))});
      p.push_back({"Deep Field", "Washes",
                   {{kDecay, 30}, {kSize, 3.4}, {kGravity, 0.5}, {kBodies, 6}, {kMotion, 0.6}, {kAttack, 0.4},
                    {kDiffusion, 0.9}, {kEarlyLate, 0.95}, {kDampHigh, 6000}, {kBassMult, 1.1}, {kPredelay, 60},
                    {kSmear, 0.45}, {kMix, 0.5}}});
      p.push_back({"Galaxy Shimmer", "Washes",
                   {{kDecay, 35}, {kSize, 3.5}, {kMotion, 0.5}, {kDiffusion, 0.95}, {kAttack, 0.5}, {kEarlyLate, 1},
                    {kPredelay, 40}, {kDampHigh, 7500}, {kShimmer, 0.25}, {kShimmerPitch, 12}, {kFeedback, 0.5},
                    {kTilt, 0.1}, {kSmear, 0.5}, {kWidth, 1.4}, {kMix, 0.55}}});
      p.push_back({"Glacier", "Washes",
                   {{kDecay, 40}, {kSize, 7}, {kGravity, 0.3}, {kMotion, 0.5}, {kDiffusion, 1}, {kAttack, 0.7},
                    {kEarlyLate, 1}, {kPredelay, 20}, {kDampHigh, 4500}, {kBassMult, 1.2}, {kLowCut, 80},
                    {kSmear, 0.7}, {kWidth, 1.3}, {kMix, 0.55}}});
      p.push_back({"Event Horizon", "Washes",
                   {{kDecay, 45}, {kSize, 4}, {kGravity, 0.4}, {kMotion, 0.7}, {kDiffusion, 1}, {kAttack, 0.6},
                    {kEarlyLate, 1}, {kPredelay, 80}, {kDampHigh, 4500}, {kBassMult, 1.3}, {kLowCut, 80},
                    {kSmear, 0.6}, {kWidth, 1.4}, {kDuck, 0.2}, {kMix, 0.6}}});
      p.push_back({"Dark Matter", "Washes",
                   {{kDecay, 50}, {kSize, 4}, {kGravity, 0.3}, {kMotion, 0.6}, {kDiffusion, 0.95}, {kAttack, 0.45},
                    {kEarlyLate, 1}, {kDampHigh, 2500}, {kBassMult, 1.6}, {kShimmer, 0.2}, {kShimmerPitch, -12},
                    {kFeedback, 0.4}, {kSmear, 0.5}, {kMix, 0.6}}});
      p.push_back({"Frozen Ring", "Washes",
                   {{kDecay, 60}, {kSize, 3}, {kMotion, 0.4}, {kDiffusion, 0.9}, {kEarlyLate, 1}, {kDampHigh, 8000},
                    {kSmear, 0.4}, {kDuck, 0.3}, {kMix, 0.6}}});

      // The extremes: the far ends of the ranges, for sounds that stop being rooms. Feedback past 100%
      // sustains and grows into the safety stage, so these hold or bloom rather than die away.
      p.push_back({"Singing Tank", "Extremes",
                   {{kDecay, 20}, {kSize, 0.3}, {kDiffusion, 0}, {kFeedback, 0.9}, {kDampHigh, 20000}, {kMotion, 0},
                    {kEarlyLate, 1}, {kMix, 0.5}}});
      p.push_back({"Steel Cathedral", "Extremes",
                   {{kDecay, 15}, {kSize, 1.2}, {kDiffusion, 0}, {kShimmer, 0.3}, {kShimmerPitch, 7}, {kFeedback, 1},
                    {kDampHigh, 20000}, {kMotion, 0.1}, {kEarlyLate, 1}, {kTilt, -0.2}, {kMix, 0.45}}});
      p.push_back({"Whale Song", "Extremes",
                   {{kDecay, 30}, {kSize, 4}, {kGravity, 0.4}, {kMotion, 1.6}, {kFeedback, 0.95}, {kShimmer, 0.3},
                    {kShimmerPitch, -5}, {kDampHigh, 3000}, {kBassMult, 1.6}, {kOrbitSwirl, 1.6}, {kDiffusion, 0.9},
                    {kEarlyLate, 1}, {kAttack, 0.4}, {kMix, 0.6}}});
      p.push_back({"Sub Collapse", "Extremes",
                   {{kDecay, 20}, {kSize, 3}, {kShimmer, 0.35}, {kShimmerPitch, -24}, {kFeedback, 1.15},
                    {kTilt, -0.6}, {kDampHigh, 2200}, {kBassMult, 1.5}, {kMotion, 0.6}, {kEarlyLate, 1},
                    {kMix, 0.5}}});
      p.push_back({"Feedback Bloom", "Extremes",
                   {{kFeedback, 1.45}, {kTilt, 0.3}, {kShimmer, 0.12}, {kShimmerPitch, 7}, {kDampHigh, 12000},
                    {kDecay, 6}, {kSize, 1.5}, {kMix, 0.5}}});
      p.push_back({"Screaming Loop", "Extremes",
                   {{kFeedback, 1.3}, {kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kTube}, {kDrive, 24},
                    {kTilt, 0.2}, {kDecay, 5}, {kSize, 0.8}, {kMix, 0.45}}});
      p.push_back({"Glitch Hold", "Extremes",
                   {{kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kCrush}, {kDrive, 14}, {kDriveBias, 0.6},
                    {kFeedback, 1.1}, {kEchoOn, 1}, {kEchoWhere, kLoopSlot}, {kEchoSync, 1}, {kEchoFeedback, 0.8},
                    {kEchoMix, 0.7}, {kEchoPingPong, 1}, {kDecay, 4}, {kSize, 0.8}, {kMix, 0.45}}});
      p.push_back({"Orbit Riot", "Extremes",
                   with(with(with({{kBodies, 6}, {kGravity, 2.5}, {kReact, 1}, {kMotion, 0.9}, {kDecay, 5},
                                   {kFeedback, 0.6}, {kMix, 0.5}},
                                  target(0, Target::Feedback, 0.6)),
                             with(target(1, Target::Damping, -0.6), target(2, Target::Tilt, 0.5))),
                        with(with(target(3, Target::Width, 0.6), target(4, Target::Shimmer, 0.5)),
                             target(5, Target::Decay, 0.5)))});
      p.push_back({"Inside Out", "Extremes",
                   {{kOrbitPan, 2}, {kOrbitSwirl, 2}, {kOrbitTone, 2}, {kWidth, 2}, {kDecay, 6}, {kSize, 2},
                    {kMotion, 1.2}, {kMix, 0.55}}});
      p.push_back({"Backwards Universe", "Extremes",
                   {{kAttack, -1}, {kDecay, 20}, {kSize, 6}, {kDiffusion, 1}, {kEarlyLate, 1}, {kShimmer, 0.25},
                    {kShimmerPitch, -12}, {kFeedback, 0.9}, {kMotion, 1.3}, {kDampHigh, 5000}, {kWidth, 1.6},
                    {kMix, 0.55}}});
      p.push_back({"Echo Runaway", "Extremes",
                   {{kEchoOn, 1}, {kEchoWhere, kLoopSlot}, {kEchoSync, 9}, {kEchoFeedback, 1.05}, {kEchoTone, -0.4},
                    {kEchoMix, 1}, {kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kTape}, {kDrive, 10},
                    {kFeedback, 0.8}, {kDecay, 5}, {kMix, 0.5}}});

      // Every preset lists every setting, the rest at their defaults, so a preset is the whole sound.
      for (Preset &preset : p) {
         std::vector<bool> given(kParamCount, false);
         for (const auto &[id, v] : preset.values)
            given[id] = true;
         for (clap_id id = 0; id < kParamCount; ++id)
            if (!given[id])
               preset.values.push_back({id, kParams[id].defaultValue});
      }

      return p;
   }();
   return presets;
}

// Applies a preset through `host` (anything with setParams(const double *)): every param to its
// default, then the preset's values.
template <class Host>
void applyPreset(const Preset &preset, Host &host) {
   double values[kParamCount];
   for (clap_id id = 0; id < kParamCount; ++id)
      values[id] = kParams[id].defaultValue;
   for (const auto &[id, v] : preset.values)
      if (id < kParamCount)
         values[id] = std::clamp(v, kParams[id].min, kParams[id].max);
   host.setParams(values);
}

// Where user presets live: per platform, under the user's own settings folder.
inline std::filesystem::path userPresetFolder() {
#if defined(_WIN32)
   const char *base = std::getenv("APPDATA");
   return std::filesystem::path(base ? base : ".") / "JVerb" / "Presets";
#elif defined(__APPLE__)
   const char *home = std::getenv("HOME");
   return std::filesystem::path(home ? home : ".") / "Library" / "Application Support" / "JVerb" / "Presets";
#else
   const char *config = std::getenv("XDG_CONFIG_HOME");
   const char *home = std::getenv("HOME");
   const std::filesystem::path base = config && *config ? std::filesystem::path(config)
                                                        : std::filesystem::path(home ? home : ".") / ".config";
   return base / "JVerb" / "Presets";
#endif
}

// Version 2 from 0.7.0. Version 1 files come from 0.4.x (69 params, the old body targets) or from
// 0.5.0 and 0.6.0 (more params, today's targets); which one shows in how many params they list.
constexpr const char *kPresetHeader = "JVerb preset 2";
constexpr const char *kPresetHeaderPrefix = "JVerb preset ";
constexpr const char *kPresetExtension = ".jvpreset";

// Reads a user preset; false if the file isn't one.
inline bool readPreset(const std::filesystem::path &file, Preset &out) {
   std::ifstream in(file);
   std::string line;
   if (!in || !std::getline(in, line) || line.rfind(kPresetHeaderPrefix, 0) != 0)
      return false;
   const int version = std::atoi(line.c_str() + std::strlen(kPresetHeaderPrefix));
   if (version < 1 || version > 2)
      return false;
   out = {file.stem().string(), "User", {}};
   bool newer = false;
   while (std::getline(in, line)) {
      std::istringstream fields(line);
      unsigned id = 0;
      double v = 0.0;
      if (fields >> id >> v) {
         newer = newer || id >= kV1ParamCount;
         if (id < kParamCount && std::isfinite(v))
            out.values.emplace_back(static_cast<clap_id>(id), v);
      }
   }
   // A 0.4.x file: its body targets meant something else then.
   if (version == 1 && !newer)
      for (auto &[id, v] : out.values)
         if (bodyOf(id) >= 0 && bodyRole(id) == kBody1Target)
            v = targetFromV1(v);
   return true;
}

// Saves every param, so a preset sounds the same even if a later version changes a default; returns
// false if the file can't be written. The
// file is written beside its final name and then renamed over it, so a failed save never leaves a
// half-written preset behind.
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
      for (clap_id id = 0; id < kParamCount; ++id) {
         std::snprintf(line, sizeof(line), "%u %.9g\n", static_cast<unsigned>(id), access.paramValue(id));
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

// The user's presets, sorted by name.
inline std::vector<Preset> userPresets() {
   std::vector<Preset> found;
   std::error_code ec;
   const auto folder = userPresetFolder();
   for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
      Preset p;
      if (it->path().extension() == kPresetExtension && readPreset(it->path(), p))
         found.push_back(std::move(p));
   }
   std::sort(found.begin(), found.end(), [](const Preset &a, const Preset &b) { return a.name < b.name; });
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

} // namespace jverb
