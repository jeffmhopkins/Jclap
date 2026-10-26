#pragma once

// The flock: every sounding note is a creature, and each part's creatures fly as boids. The engine
// owns one and steps it on the audio thread, so what you hear and what the panel draws come from the
// same sim; the panel only flies its own copy while the plugin isn't processing.
//
// Fixed capacity and no allocation: slots 0..kVoiceSlots-1 belong to the engine's voices (a voice's
// creature lives in the voice's slot), and the rest are resting creatures that keep a species alive
// while it plays nothing. Randomness is our own xorshift, reseeded with the flock, so a render is
// the same every time from the same start.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "dsp.h"
#include "params.h"

namespace jsynth {

// Voices for both parts together: each part sounds at most its Polyphony (16), and the spare slots
// let a stolen voice fade out in its own slot while the new note starts in another.
constexpr int kVoiceSlots = 40;
constexpr int kIdlePerPart = 6;
constexpr int kCreatureSlots = kVoiceSlots + kIdlePerPart * kPartCount;
// The sky is kSkyAspect wide and 1 high.
constexpr float kSkyAspect = 4.0f / 3.0f;
// The neighbours whose sound a creature hears (Neighbour FM).
constexpr int kNeighbours = 3;

// Where a held note's lure sits: low notes left and high right (bend moves it), more Slide higher up.
// What places a lure along one axis (kLureSourceNames).
enum LureSource { kLurePitch, kLureStrike, kLurePress, kLureSlide, kLureLift, kLureRandom, kLureCentre };

// A note's place along one axis, 0..1, by `source`: its pitch with bend over four octaves from C2, a
// touch dimension (strike, press, glide, slide, lift, each 0..1), its own random place (-1..1), or
// the middle.
inline float lureAxis(int source, float note, float bend, const float touch[5], float random) {
   switch (source) {
   case kLurePitch: return std::clamp((note + bend - 36.0f) / 48.0f, 0.0f, 1.0f);
   case kLureStrike: return std::clamp(touch[0], 0.0f, 1.0f);
   case kLurePress: return std::clamp(touch[1], 0.0f, 1.0f);
   case kLureSlide: return std::clamp(touch[3], 0.0f, 1.0f);
   case kLureLift: return std::clamp(touch[4], 0.0f, 1.0f);
   case kLureRandom: return std::clamp(0.5f + 0.5f * random, 0.0f, 1.0f);
   default: return 0.5f;
   }
}

// Where a lure sits in the sky for its places along X and Y (0..1, Y upward), clear of the edges.
inline void lureAt(float ax, float ay, float &x, float &y) {
   x = (0.07f + 0.86f * ax) * kSkyAspect;
   y = 0.1f + 0.8f * (1.0f - ay);
}

// How a creature flies: its species' flock params, moved by its own routes.
struct FlockParams {
   int behaviour = 0;
   float cohesion = 0.5f, alignment = 0.5f, separation = 0.4f, speed = 0.4f, wander = 0.2f, lure = 0.6f,
         range = 0.35f;
   // Wingbeat and wind push (0..1).
   float wingPush = 0.2f, windPush = 0.3f;

   // From a part's param values; `v(offset)` reads one.
   template <typename Get>
   void read(Get v) {
      behaviour = static_cast<int>(v(flockParam(kBehaviour)) + 0.5);
      cohesion = static_cast<float>(v(flockParam(kCohesion)));
      alignment = static_cast<float>(v(flockParam(kAlignment)));
      separation = static_cast<float>(v(flockParam(kSeparation)));
      speed = static_cast<float>(v(flockParam(kSpeed)));
      wander = static_cast<float>(v(flockParam(kWander)));
      lure = static_cast<float>(v(flockParam(kLure)));
      range = static_cast<float>(v(flockParam(kRange)));
      wingPush = static_cast<float>(v(lfoParam(0, kLfoPush)));
      windPush = static_cast<float>(v(lfoParam(1, kLfoPush)));
   }
};

// A creature's own modulation sources, each continuous, so a route from one never steps: Speed,
// Nearest and Heading 0..1, Turn -1..1. Caught (0..1, for Bite) and x (0..1, for X Pan) aren't
// route sources but glide the same way.
struct CreatureSources {
   float speed = 0, nearest = 0, heading = 0, turn = 0, caught = 0, x = 0;
};

struct Creature {
   bool alive = false;
   // Bumped each time the slot gets a new creature, so the panel can tell a new one from an old.
   uint32_t id = 0;
   int part = 0;
   // Held (its note is down), or flying off as it fades; resting creatures are never held.
   bool held = false;
   bool resting = false;
   float x = 0, y = 0, vx = 0, vy = 0;
   // How bright and present it is, 0..1: up while held, down after.
   float energy = 0;
   float lureX = 0, lureY = 0, press = 0;
   // -1..1, this creature's wingbeat now (its own LFO's value).
   float wing = 0, wingPhase = 0;
   float wanderAngle = 0, wanderTurn = 0;
   // Whether the engine runs it (the panel's own flock doesn't), and if so whether its voice still
   // sounds and how loud (its amp envelope): let go, it glows as its sound does and goes when it ends.
   bool tracked = false, sounding = false;
   float level = 0;
   // Its own roaming around the lure: two slow orbits at rates and phases of its own, so no two
   // creatures trace the same path and none repeats.
   float roamRate[2] = {}, roamPhase[2] = {};
   FlockParams params;
   CreatureSources sources;
   // Where they were a step ago, so a voice can glide between steps.
   CreatureSources lastSources;
   // The same-species creatures it hears, and how much (0..1), and the hunter holding it.
   int neighbour[kNeighbours] = {-1, -1, -1};
   float neighbourWeight[kNeighbours] = {};
   int hunter = -1;
};

struct FlockFrame;

class Flock {
public:
   // The flock's step: fixed, so it flies the same at any sample rate and block size.
   static constexpr float kStep = 1.0f / 500.0f;

   Creature creatures[kCreatureSlots];
   // The sky's settings: Size (0..1) and Walls (Soft, Wrap, Open).
   float size = 0.5f;
   int walls = 0;
   // Each species' wind gust now (-1..1).
   float gust[kPartCount] = {};

   void reset(uint32_t seed) {
      rng_.seed(seed);
      time_ = 0;
      for (Creature &c : creatures)
         c = Creature{};
      nextId_ = 1;
      for (int part = 0; part < kPartCount; ++part)
         for (int i = 0; i < kIdlePerPart; ++i) {
            Creature &c = creatures[kVoiceSlots + part * kIdlePerPart + i];
            hatch(c, part, (0.15f + 0.3f * rng_.unipolar() + 0.4f * part) * kSkyAspect, 0.25f + 0.5f * rng_.unipolar());
            c.resting = true;
         }
   }

   // A new creature in voice slot `slot`, at its lure.
   void hatch(int slot, int part, float lureX, float lureY) {
      Creature &c = creatures[slot];
      hatch(c, part, lureX, lureY);
      c.held = true;
      c.lureX = lureX;
      c.lureY = lureY;
      c.energy = std::max(c.energy, 0.2f);
   }
   // Its note is up: it flies off as it fades.
   void release(int slot) { creatures[slot].held = false; }
   // Its voice has ended: it fades out and goes.
   void silence(int slot) { creatures[slot].sounding = false; }

   // Copies the live creatures into `f`, for the panel.
   void show(FlockFrame &f) const;

   // Whether `part` has any held creature, so its resting ones fade out.
   bool playing(int part) const {
      for (int i = 0; i < kVoiceSlots; ++i)
         if (creatures[i].alive && creatures[i].part == part && creatures[i].held)
            return true;
      return false;
   }

   // One step of kStep seconds.
   void step() {
      const float dt = kStep;
      time_ += dt;
      for (Creature &c : creatures)
         c.lastSources = c.sources;
      bool plays[kPartCount];
      for (int p = 0; p < kPartCount; ++p)
         plays[p] = playing(p);
      for (Creature &c : creatures) {
         if (!c.alive)
            continue;
         float target = 0.0f, rate = 1.2f;
         if (c.held)
            target = 0.55f + 0.45f * c.press, rate = 5.0f;
         else if (c.sounding)
            target = std::max(0.04f, 0.55f * c.level), rate = 5.0f; // its release
         else if (c.resting)
            target = plays[c.part] ? 0.0f : 0.3f;
         else if (c.tracked)
            rate = 6.0f; // its voice has ended
         c.energy += (target - c.energy) * std::min(1.0f, dt * rate);
         // A released creature flies off as it fades, then goes; a resting one stays, dimmed. A
         // resting creature beats its own wings.
         if (!c.held && !c.resting && c.energy < 0.01f)
            c.alive = false;
         if (c.resting) {
            c.wingPhase += dt * 4.0f;
            c.wing = dsp::Sine::at(c.wingPhase);
         }
      }
      fly(dt);
      couple(dt);
   }

private:
   static constexpr float kPi = dsp::kPi;
   static constexpr float kCatch = 0.03f;
   // A turn rate (radians a second) that reads as a hard turn on the Turn source.
   static constexpr float kTurnFull = 4.0f;

   void hatch(Creature &c, int part, float x, float y) {
      const float wing = c.wing;
      c = Creature{};
      c.alive = true;
      c.id = nextId_++;
      c.part = part;
      c.x = x + 0.03f * rng_.bipolar();
      c.y = y + 0.03f * rng_.bipolar();
      c.vx = 0.1f * rng_.bipolar();
      c.vy = 0.1f * rng_.bipolar();
      c.wanderAngle = kPi * rng_.bipolar();
      for (int i = 0; i < 2; ++i) {
         c.roamRate[i] = (0.6f + 0.5f * rng_.unipolar()) * (i == 0 ? 1.0f : 1.618f) * (rng_.bipolar() < 0 ? -1.0f : 1.0f);
         c.roamPhase[i] = 2.0f * kPi * rng_.unipolar();
      }
      c.wing = wing;
   }

   // A bigger sky makes everything in it relatively smaller and slower.
   float scale() const { return 1.4f - 0.8f * size; }
   float radius(const FlockParams &p) const { return (0.08f + 0.32f * p.range) * scale(); }
   float maxSpeed(const FlockParams &p, float press) const {
      return (0.08f + 0.42f * p.speed) * (1.0f + 0.8f * press) * scale();
   }

   void fly(float dt) {
      const float windAngle = 0.4f + 0.9f * std::sin(time_ * 0.07f);
      for (Creature &b : creatures) {
         if (!b.alive)
            continue;
         const FlockParams &sp = b.params;
         const float R = radius(sp);
         float cx = 0, cy = 0, ax = 0, ay = 0, sx = 0, sy = 0, hx = 0, hy = 0, wsum = 0, near = 0;
         float prey = 1e9f;
         for (const Creature &o : creatures) {
            if (&o == &b || !o.alive || o.energy < 0.02f)
               continue;
            const float dx = o.x - b.x, dy = o.y - b.y, d = std::sqrt(dx * dx + dy * dy) + 1e-5f;
            if (o.part == b.part) {
               if (d < R) {
                  // A smooth kernel, so a neighbour drifting in or out of range fades in or out.
                  const float k = 1.0f - d / R, w = k * k * o.energy;
                  cx += o.x * w, cy += o.y * w, ax += o.vx * w, ay += o.vy * w, wsum += w;
                  near = std::max(near, k * std::min(1.0f, 2.0f * o.energy));
               }
               if (d < 0.4f * R) {
                  const float k = 1.0f - d / (0.4f * R);
                  sx -= dx / d * k, sy -= dy / d * k;
               }
            }
            else {
               // Hunters chase the nearest of the other species; everyone shies from a hunter, and
               // Flee runs from the other species whatever it does.
               if (sp.behaviour == 2 && d < prey && d < 2.5f * R && o.energy > 0.1f) {
                  prey = d;
                  hx = dx / d, hy = dy / d;
               }
               const float scare = sp.behaviour == 3 ? 1.5f : o.params.behaviour == 2 ? 0.6f : 0.0f;
               if (scare > 0.0f && d < R) {
                  sx -= dx / d * scare * (1.0f - d / R) * o.energy;
                  sy -= dy / d * scare * (1.0f - d / R) * o.energy;
               }
            }
         }
         float accX = 0, accY = 0;
         if (wsum > 1e-4f) {
            const float pull = sp.behaviour == 1 ? -1.0f : 1.0f; // Scatter pushes away from the middle
            const float blend = std::min(1.0f, wsum * 4.0f);
            accX += ((cx / wsum - b.x) * sp.cohesion * 2.5f * pull + (ax / wsum - b.vx) * sp.alignment * 2.0f) * blend;
            accY += ((cy / wsum - b.y) * sp.cohesion * 2.5f * pull + (ay / wsum - b.vy) * sp.alignment * 2.0f) * blend;
         }
         accX += sx * sp.separation * 1.6f + hx * 0.9f;
         accY += sy * sp.separation * 1.6f + hy * 0.9f;
         if (b.held) {
            // The finger pulls toward a point that roams around the lure, on a leash: loose near it,
            // so the flock and the wander shape the path there, and firm when the creature strays.
            const float reach = (0.05f + 0.14f * sp.wander) * scale();
            const float tx = b.lureX + reach * (std::sin(time_ * b.roamRate[0] + b.roamPhase[0]) +
                                                0.5f * std::sin(time_ * b.roamRate[1] * 1.3f + b.roamPhase[1]));
            const float ty = b.lureY + reach * 0.8f * (std::cos(time_ * b.roamRate[1] + b.roamPhase[1]) +
                                                       0.5f * std::sin(time_ * b.roamRate[0] * 0.7f + b.roamPhase[0]));
            const float dx = tx - b.x, dy = ty - b.y;
            const float leash = 0.35f + 0.65f * std::min(1.0f, std::sqrt(dx * dx + dy * dy) / (0.2f * scale()));
            accX += dx * sp.lure * 6.0f * leash - b.vx * sp.lure * 0.6f;
            accY += dy * sp.lure * 6.0f * leash - b.vy * sp.lure * 0.6f;
         }
         // Wander: the heading drifts, and how fast it turns drifts too, so a creature sometimes
         // sweeps a long curve and sometimes jinks.
         b.wanderTurn += (rng_.bipolar() * 6.0f - b.wanderTurn * 0.8f) * dt;
         b.wanderAngle += (b.wanderTurn + rng_.bipolar() * 4.0f) * dt;
         accX += std::cos(b.wanderAngle) * sp.wander * 1.1f;
         accY += std::sin(b.wanderAngle) * sp.wander * 1.1f;
         accX += std::cos(windAngle) * gust[b.part] * sp.windPush * 0.35f;
         accY += std::sin(windAngle) * gust[b.part] * sp.windPush * 0.35f;
         // The wingbeat bobs each creature across its path.
         const float v = std::sqrt(b.vx * b.vx + b.vy * b.vy) + 1e-5f;
         accX += -b.vy / v * b.wing * sp.wingPush * 0.8f;
         accY += b.vx / v * b.wing * sp.wingPush * 0.8f;
         // Walls: soft ones turn creatures back near the edge; open ones only well past it.
         const float m = walls == 2 ? -0.08f : 0.06f, k = walls == 2 ? 4.0f : 14.0f;
         if (walls != 1) {
            if (b.x < m)
               accX += (m - b.x) * k;
            if (b.x > kSkyAspect - m)
               accX -= (b.x - kSkyAspect + m) * k;
            if (b.y < m)
               accY += (m - b.y) * k;
            if (b.y > 1.0f - m)
               accY -= (b.y - 1.0f + m) * k;
         }
         const float wasX = b.vx, wasY = b.vy;
         b.vx += accX * dt;
         b.vy += accY * dt;
         const float top = maxSpeed(sp, b.press), speed = std::sqrt(b.vx * b.vx + b.vy * b.vy) + 1e-6f;
         const float clamped = std::clamp(speed, 0.2f * top, top);
         b.vx *= clamped / speed;
         b.vy *= clamped / speed;
         b.x += b.vx * dt;
         b.y += b.vy * dt;
         if (walls == 1) {
            b.x -= kSkyAspect * std::floor(b.x / kSkyAspect);
            b.y -= std::floor(b.y);
         }
         else {
            // Even open walls keep creatures somewhere the panel can reach.
            b.x = std::clamp(b.x, -0.5f, kSkyAspect + 0.5f);
            b.y = std::clamp(b.y, -0.5f, 1.5f);
         }
         CreatureSources &s = b.sources;
         s.speed = clamped / maxSpeed(sp, 1.0f);
         s.nearest += (near - s.nearest) * std::min(1.0f, dt * 20.0f);
         // How much it flies upward: smooth all the way round, unlike an angle.
         s.heading = 0.5f - 0.5f * b.vy / (clamped + 1e-6f);
         // How fast its heading turns, in radians a second: + clockwise on screen (a right turn, as y
         // runs down), - a left one. kTurnFull is a hard turn; tanh keeps a sharper one in range, and
         // the smoothing (about 170 ms) keeps the wander's and the wingbeat's jitter out of it.
         const float was = std::sqrt(wasX * wasX + wasY * wasY) + 1e-6f;
         const float sine = (wasX * b.vy - wasY * b.vx) / (was * clamped);
         const float turn = std::tanh(std::asin(std::clamp(sine, -1.0f, 1.0f)) / dt / kTurnFull);
         s.turn += (turn - s.turn) * std::min(1.0f, dt * 6.0f);
         s.x = std::clamp(b.x / kSkyAspect, 0.0f, 1.0f);
      }
   }

   // Who hears whom: each creature's three nearest of its own kind, weighted smoothly by distance
   // and their energy, and the hunter holding it, if one is.
   void couple(float dt) {
      for (int i = 0; i < kCreatureSlots; ++i) {
         Creature &b = creatures[i];
         for (int k = 0; k < kNeighbours; ++k)
            b.neighbour[k] = -1, b.neighbourWeight[k] = 0;
         b.hunter = -1;
         if (!b.alive)
            continue;
         const float R = radius(b.params);
         float bestHunter = kCatch * 2.0f;
         for (int j = 0; j < kCreatureSlots; ++j) {
            const Creature &o = creatures[j];
            if (j == i || !o.alive || o.energy < 0.02f)
               continue;
            const float dx = o.x - b.x, dy = o.y - b.y, d = std::sqrt(dx * dx + dy * dy);
            if (o.part == b.part) {
               if (d >= R)
                  continue;
               const float k = 1.0f - d / R, w = k * k * std::min(1.0f, 1.5f * o.energy);
               // Keep the three strongest.
               int weakest = 0;
               for (int n = 1; n < kNeighbours; ++n)
                  if (b.neighbourWeight[n] < b.neighbourWeight[weakest])
                     weakest = n;
               if (w > b.neighbourWeight[weakest]) {
                  b.neighbour[weakest] = j;
                  b.neighbourWeight[weakest] = w;
               }
            }
            else if (o.params.behaviour == 2 && o.energy > 0.1f && b.energy > 0.1f && d < bestHunter) {
               bestHunter = d;
               b.hunter = j;
            }
         }
         // Caught rises quickly while a hunter is on it and lets go slowly, so Bite fades.
         const float target = b.hunter >= 0 ? 1.0f - 0.5f * bestHunter / (kCatch * 2.0f) : 0.0f;
         const float rate = target > b.sources.caught ? 50.0f : 2.0f;
         b.sources.caught += (target - b.sources.caught) * std::min(1.0f, dt * rate);
      }
   }

   dsp::Random rng_;
   float time_ = 0;
   uint32_t nextId_ = 1;
};

// What the engine shows the panel, a few hundred times a second, through a TripleBuffer.
struct FlockFrame {
   uint32_t frame = 0;
   struct Shown {
      uint32_t id;
      int8_t part;
      bool held, resting;
      float x, y, vx, vy, energy, press, wing, lureX, lureY;
      CreatureSources sources;
      // Its slot, its strongest neighbour's and its hunter's (-1 for none).
      int16_t slot, neighbour, hunter;
   };
   int count = 0;
   Shown creatures[kCreatureSlots];
   // Each part's newest note's envelopes and LFOs, for the chips' lights.
   float env[kPartCount][kEnvCount] = {};
   float lfo[kPartCount][kLfoCount] = {};
   // Where every sounding voice's envelopes are, for the dots riding the envelope curves. A stage is
   // the curve's: 0 attack, 1 decay, 2 sustain, 3 release, -1 not running.
   struct Playhead {
      int8_t part;
      int8_t stage[kEnvCount];
      float level[kEnvCount], from[kEnvCount];
   };
   int playheadCount = 0;
   Playhead playheads[kVoiceSlots];
};

inline void Flock::show(FlockFrame &f) const {
   f.frame++;
   f.count = 0;
   for (int i = 0; i < kCreatureSlots; ++i) {
      const Creature &c = creatures[i];
      if (!c.alive)
         continue;
      FlockFrame::Shown &s = f.creatures[f.count++];
      s.id = c.id;
      s.part = static_cast<int8_t>(c.part);
      s.held = c.held;
      s.resting = c.resting;
      s.x = c.x, s.y = c.y, s.vx = c.vx, s.vy = c.vy;
      s.energy = c.energy, s.press = c.press, s.wing = c.wing;
      s.lureX = c.lureX, s.lureY = c.lureY;
      s.sources = c.sources;
      s.slot = static_cast<int16_t>(i);
      int best = -1;
      for (int n = 0; n < kNeighbours; ++n)
         if (c.neighbour[n] >= 0 && (best < 0 || c.neighbourWeight[n] > c.neighbourWeight[best]))
            best = n;
      s.neighbour = static_cast<int16_t>(best >= 0 ? c.neighbour[best] : -1);
      s.hunter = static_cast<int16_t>(c.hunter);
   }
}

} // namespace jsynth
