// Drives J2D2 the way a host does, and checks its engine directly.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "editor.h"
#include "engine.h"
#include "params.h"
#include "presets.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace j2d2;

namespace {

// A byte buffer the plugin's state extension writes to and reads from.
struct Buffer {
   std::vector<uint8_t> bytes;
   size_t pos = 0;
   clap_ostream_t out{this, [](const clap_ostream_t *s, const void *data, uint64_t size) -> int64_t {
                         auto *b = static_cast<Buffer *>(s->ctx);
                         b->bytes.insert(b->bytes.end(), static_cast<const uint8_t *>(data),
                                         static_cast<const uint8_t *>(data) + size);
                         return static_cast<int64_t>(size);
                      }};
   clap_istream_t in{this, [](const clap_istream_t *s, void *data, uint64_t size) -> int64_t {
                        auto *b = static_cast<Buffer *>(s->ctx);
                        const size_t n = std::min<size_t>(size, b->bytes.size() - b->pos);
                        std::memcpy(data, b->bytes.data() + b->pos, n);
                        b->pos += n;
                        return static_cast<int64_t>(n);
                     }};
};

// RMS and peak of `blocks` blocks of 512 frames.
struct Level {
   double rms = 0, peak = 0;
   bool finite = true;
};
Level run(jclap::core::TestHost &h, int blocks) {
   std::vector<float> l(512), r(512);
   float *out[2] = {l.data(), r.data()};
   double sum = 0;
   Level lv;
   for (int b = 0; b < blocks; ++b) {
      h.process(512, nullptr, out);
      for (int i = 0; i < 512; ++i) {
         lv.finite = lv.finite && std::isfinite(l[i]) && std::isfinite(r[i]);
         sum += 0.5 * (l[i] * l[i] + r[i] * r[i]);
         lv.peak = std::max(lv.peak, static_cast<double>(std::max(std::fabs(l[i]), std::fabs(r[i]))));
      }
   }
   lv.rms = std::sqrt(sum / (blocks * 512.0));
   return lv;
}

Settings defaults() {
   Settings s;
   for (int i = 0; i < kParamCount; ++i)
      s.v[i] = kParams[i].defaultValue;
   return s;
}

// How many droid sounds a phrase in mood (e, f) has per second, how many rise, and its median pitch.
struct PhraseStats {
   double perSecond = 0, rising = 0;
};
PhraseStats phraseStats(double e, double f) {
   const double sr = 48000.0;
   int events = 0, up = 0;
   double seconds = 0;
   for (int seed = 0; seed < 12; ++seed) {
      Speaker sp;
      sp.prepare(sr);
      sp.seed(100 + seed);
      const double mix[kSyllableCount] = {1, 1, 1, 1, 1, 1, 1, 1};
      sp.phrase(moodFor(e, f), 2.0, 0.0, mix);
      int last = kEvNone, n = 0;
      double start = 0, prevPitch = 0;
      while (sp.busy() && n < sr * 10) {
         sp.tick();
         const int k = sp.kind();
         if (k != last && k != kEvNone && k < kEvRise) {
            ++events;
            start = sp.pitch();
         }
         if (k == kEvNone && last != kEvNone && last < kEvRise)
            up += prevPitch > start;
         if (k != kEvNone)
            prevPitch = sp.pitch();
         last = k;
         ++n;
      }
      seconds += n / sr;
   }
   return {events / seconds, static_cast<double>(up) / std::max(1, events)};
}

} // namespace

int main() {
   // ---- the engine ----

   // Mood shapes how the droid talks: excited is faster and climbs, sad is slower and sinks.
   {
      const PhraseStats excited = phraseStats(0.95, 0.85), sad = phraseStats(-0.85, -0.65);
      std::printf("excited %.1f/s %.0f%% up, sad %.1f/s %.0f%% up\n", excited.perSecond, 100 * excited.rising, sad.perSecond,
                  100 * sad.rising);
      JCLAP_CHECK(excited.perSecond > 1.5 * sad.perSecond);
      JCLAP_CHECK(excited.rising > sad.rising + 0.25);
   }

   // Every layer of the drone sounds on its own at about the same level, and none is silent.
   {
      for (int layer = 0; layer < kLayerCount; ++layer) {
         Drone d;
         d.prepare(48000.0);
         d.start(mtof(50), 7);
         DroneControls c;
         for (int k = 0; k < kLayerCount; ++k)
            c.level[k] = k == layer;
         double sum = 0;
         const int n = 48000 * 6;
         for (int i = 0; i < n; ++i) {
            float l, r;
            d.tick(c, true, l, r);
            sum += 0.5 * (l * l + r * r);
         }
         const double db = 10 * std::log10(sum / n);
         std::printf("layer %s %.1f dB\n", kLayerNames[layer], db);
         JCLAP_CHECK(db > -27.0 && db < -14.0);
      }
   }

   // ATONAL is bounded: at 1 each METAL mode is a free bar's, within a quarter octave of its tuned place.
   {
      for (int i = 0; i < 4; ++i)
         JCLAP_CHECK(std::fabs(std::log2(kMetalBar[i] / kMetalPure[i])) < 0.2);
   }

   // Pure droid has no drone; pure drone has no phrases, and the drone sounds.
   {
      Settings s = defaults();
      Engine e;
      e.prepare(48000.0);
      e.seed(3);
      s.v[kMorph] = 1.0;
      s.v[kAttack] = 0.01;
      e.noteOn(50, -1, -1, 0.8, s);
      std::vector<float> l(4800), r(4800);
      for (int b = 0; b < 10; ++b)
         e.process(l.data(), r.data(), 4800, s);
      const VoiceView v = e.view(0);
      JCLAP_CHECK(v.active && v.event == kEvNone && v.drone > 0.3);
   }

   // Pressure on a held drone pops bleeps through; with no pressure, none come.
   {
      for (double press : {0.0, 1.0}) {
         Settings s = defaults();
         s.v[kMorph] = 0.7;
         s.v[kTalk] = 0.0;
         Engine e;
         e.prepare(48000.0);
         e.seed(9);
         e.noteOn(50, -1, -1, 0.8, s);
         std::vector<float> l(480), r(480);
         // Let the note's own phrase finish first.
         for (int b = 0; b < 600; ++b)
            e.process(l.data(), r.data(), 480, s);
         e.pressure(50, -1, -1, press);
         int pops = 0, last = kEvNone;
         for (int b = 0; b < 1000; ++b) {
            e.process(l.data(), r.data(), 480, s);
            const int k = e.view(0).event;
            pops += k != kEvNone && last == kEvNone;
            last = k;
         }
         std::printf("pressure %.0f: %d pops in 10 s\n", press, pops);
         JCLAP_CHECK(press > 0.5 ? pops > 15 : pops == 0);
      }
   }

   // ---- the plugin ----
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.j2d2");

   // Silent until a note.
   JCLAP_CHECK(run(h, 8).peak == 0.0);

   // A note speaks (droid end), then goes quiet once released.
   h.note(CLAP_EVENT_NOTE_ON, 60, 0.9);
   const Level talking = run(h, 100);
   JCLAP_CHECK(talking.finite && talking.rms > 0.01 && talking.peak < 1.0);
   h.note(CLAP_EVENT_NOTE_OFF, 60, 0.0);
   run(h, 900);
   JCLAP_CHECK(run(h, 20).peak < 1e-4);

   // Every factory preset makes sound from a held chord with pressure, never clips, never goes NaN.
   for (const Preset &p : factoryPresets()) {
      JCLAP_CHECK_EQ(p.values.size(), static_cast<size_t>(kParamCount));
      for (const auto &[id, v] : p.values) {
         JCLAP_CHECK(v >= kParams[id].min && v <= kParams[id].max);
         h.setParam(id, v);
      }
      for (int key : {50, 57, 64})
         h.note(CLAP_EVENT_NOTE_ON, static_cast<int16_t>(key), 0.8);
      h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 50, 0.6);
      const Level lv = run(h, 500);
      std::printf("%-18s rms %.1f dB peak %.2f\n", p.name.c_str(), 20 * std::log10(lv.rms + 1e-9), lv.peak);
      JCLAP_CHECK(lv.finite && lv.rms > 0.02 && lv.peak <= 1.0);
      h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0);
      run(h, 4);
   }

   // MPE over MIDI: a note on channel 2 of a zone, with pressure, sounds.
   h.midi(0xB0, 101, 0);
   h.midi(0xB0, 100, 6);
   h.midi(0xB0, 6, 15);
   h.midi(0x91, 62, 100);
   h.midi(0xD1, 90, 0);
   JCLAP_CHECK(run(h, 60).rms > 0.005);
   h.midi(0x81, 62, 0);
   h.midi(0xB0, 120, 0);
   run(h, 4);

   // Every param's text reads back to the same text.
   for (clap_id id = 0; id < kParamCount; ++id) {
      for (double f : {0.0, 0.13, 0.5, 0.77, 1.0}) {
         const double v = kParams[id].min + f * (kParams[id].max - kParams[id].min);
         double back = 0;
         JCLAP_CHECK(h.textToValue(id, h.paramText(id, v).c_str(), back));
         JCLAP_CHECK(h.paramText(id, back) == h.paramText(id, v));
      }
   }

   // State round-trips, preset name included, and a short state leaves the rest at defaults.
   {
      h.setParam(kMorph, 0.42);
      h.setParam(kEnergy, -0.3);
      h.process(4);
      auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
      Buffer b;
      JCLAP_CHECK(state->save(h.plugin(), &b.out));
      h.setParam(kMorph, 0.9);
      h.process(4);
      JCLAP_CHECK(state->load(h.plugin(), &b.in));
      JCLAP_CHECK(std::fabs(h.paramValue(kMorph) - 0.42) < 1e-9);
      JCLAP_CHECK(std::fabs(h.paramValue(kEnergy) + 0.3) < 1e-9);
      Buffer junk;
      junk.bytes = {'X', 'X', 'X', 'X'};
      JCLAP_CHECK(!state->load(h.plugin(), &junk.in));
   }

   return jclap::core::testResult();
}
