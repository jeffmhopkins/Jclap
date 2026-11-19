#pragma once

// ORB's DSP renderer (orb/render: events.py, foley.py, natural.py, mix.py), ported to C++ for the
// roles a drum kit wants: the one-shot hits, not the scenes (birdsong, rain, traffic, a siren...).
// Each role is the same physical model, drawing the same random numbers, as the original.

#include <map>
#include <string>
#include <vector>

#include "gen/orb/dsp.h"

namespace jkit::orb {

// ORB's 47 roles, in its order (a role's index seeds its noise).
const std::vector<std::string> &roleNames();
int roleIndex(const std::string &name);
// Whether this port renders the role (the kit's one-shots).
bool roleRendered(int role);

struct Event {
   double t = 0.0, dur = 0.5;
   int role = 0;
   double apex = 0.0; // t + the role's apex fraction of dur
   double gainDb = -6.0, pan = 0.0;
   int seed = 0;
   // The kit's variations (not ORB's): the sound's pitch and its decay time, as factors.
   double pitch = 1.0, decay = 1.0;
};

// One event as stereo (left, right) at 48 kHz, with its reverb tail when `tail`
// (events.render_event).
void renderEvent(const Event &ev, int seed, bool tail, Vec &left, Vec &right);

// What the planner writes: a cue `duration` long with up to 8 timed events (grammar.SpecEvent).
struct SpecEvent {
   int role = 0;
   double t = 0.0, dur = 0.5, gainDb = -6.0, pan = 0.0;
   int seed = 0; // 0..255
   double pitch = 1.0, decay = 1.0; // as Event's
};
struct Spec {
   double duration = 1.0;
   std::vector<SpecEvent> events;
};

// The whole cue, mixed and normalised as render_spec does with no bed: (left, right) at 48 kHz.
void renderSpec(const Spec &spec, Vec &left, Vec &right);

} // namespace jkit::orb
