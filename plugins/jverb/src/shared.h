#pragma once

// What the audio thread and the panel pass each other. The audio thread publishes an OrbitFrame
// through a TripleBuffer a few hundred times a second; the panel reads the newest one each frame it
// draws. The panel sends OrbitCommands back through a queue when someone drags a body. Neither side
// ever waits for the other.

#include <cstdint>

#include "params.h"

namespace jverb {

struct OrbitFrame {
   // Bumped when the sim restarts (Reset, Retrig, a state load), so the panel can clear its gas.
   uint32_t resets = 0;
   int count = 3;
   float x[kMaxBodies] = {}, y[kMaxBodies] = {}, vx[kMaxBodies] = {}, vy[kMaxBodies] = {};
   float mass[kMaxBodies] = {};
   float delayMs[kMaxBodies] = {};
   // The dry input split into low, mid and high: an envelope per band (about 0..1), and a counter
   // that goes up on each transient with how hard it hit (0..1).
   float band[3] = {};
   uint32_t hits[3] = {};
   float hitStrength[3] = {};
   // How loud the tank is, and the settings the drawing follows.
   float late = 0.0f;
   float decay = 4.0f;
   bool running = true, frozen = false;
};

struct OrbitCommand {
   enum class Kind : uint8_t {
      Place,    // move body `body` to (x, y)
      Velocity, // set its velocity to (x, y)
      Spawn,    // where the next body added should appear
      Reset,    // back to the start positions
   } kind;
   int8_t body;
   float x, y;
};

} // namespace jverb
