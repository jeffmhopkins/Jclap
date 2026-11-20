// Drives J2D2 the way a host does, and checks its engine directly.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
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

   // The droid voice follows the key: it speaks at once on the note-on and is silent within 10 ms
   // of the note-off, however much of its phrase was left.
   {
      Settings s = defaults();
      s.v[kMorph] = 0.0;
      s.v[kSpaceMix] = 0.0;
      Engine e;
      e.prepare(48000.0);
      e.seed(4);
      auto peakOf = [&](int frames) {
         std::vector<float> l(frames), r(frames);
         e.process(l.data(), r.data(), frames, s);
         double p = 0;
         for (int i = 0; i < frames; ++i)
            p = std::max(p, static_cast<double>(std::max(std::fabs(l[i]), std::fabs(r[i]))));
         return p;
      };
      e.noteOn(60, -1, -1, 0.9, s);
      const double onset = peakOf(480); // the first 10 ms
      peakOf(4320);
      JCLAP_CHECK(e.view(0).event != kEvNone || e.view(0).active);
      e.noteOff(60, -1, -1);
      peakOf(480);
      const double after = peakOf(4800);
      std::printf("droid: %.3f peak in the first 10 ms, %.6f from 10 ms after the note-off\n", onset, after);
      JCLAP_CHECK(onset > 0.01);
      JCLAP_CHECK(after < 1e-4);
   }

   // PER KEY SYLLABLE: one press speaks one syllable, LENGTH long; PHRASE speaks several, and never
   // runs past LENGTH.
   for (double perKey : {0.0, 1.0}) {
      for (double length : {0.08, 0.5, 1.5}) {
         Settings s = defaults();
         s.v[kMorph] = 0.0;
         s.v[kTalk] = 0.0;
         s.v[kPerKey] = perKey;
         s.v[kLength] = length;
         Engine e;
         e.prepare(48000.0);
         e.seed(11);
         e.noteOn(60, -1, -1, 0.9, s);
         std::vector<float> l(48), r(48);
         int sounds = 0, last = kEvNone, lastLoud = 0;
         for (int b = 0; b < 4000; ++b) {
            e.process(l.data(), r.data(), 48, s);
            const int k = e.view(0).event;
            sounds += k != kEvNone && last == kEvNone;
            if (k != kEvNone)
               lastLoud = b + 1;
            last = k;
         }
         const double heard = lastLoud * 48 / 48000.0;
         std::printf("%s %.2f s: %d sounds over %.2f s\n", perKey > 0.5 ? "phrase" : "syllable", length, sounds, heard);
         if (perKey < 0.5)
            JCLAP_CHECK(sounds == 1 && heard > 0.6 * length && heard < 1.5 * length + 0.01);
         else
            JCLAP_CHECK(heard < 1.1 * length + 0.01 && (length < 1.0 || sounds > 2));
      }
   }

   // At the ribbon's middle, a soft note only hums and a hard one speaks too (SPEAK ABOVE).
   for (double vel : {0.3, 0.9}) {
      Settings s = defaults();
      s.v[kMorph] = 0.5;
      s.v[kTalk] = 0.0;
      s.v[kPop] = 0.0;
      Engine e;
      e.prepare(48000.0);
      e.seed(6);
      e.noteOn(55, -1, -1, vel, s);
      std::vector<float> l(480), r(480);
      int heard = 0;
      for (int b = 0; b < 100; ++b) {
         e.process(l.data(), r.data(), 480, s);
         heard += e.view(0).event != kEvNone;
      }
      std::printf("ribbon 50%%, velocity %.1f: droid in %d of 100 blocks\n", vel, heard);
      JCLAP_CHECK(vel < 0.5 ? heard == 0 : heard > 0);
   }

   // Pitch bend moves the droid: +12 semitones puts a syllable an octave up.
   {
      double mean[2] = {0, 0};
      for (int k = 0; k < 2; ++k) {
         Settings s = defaults();
         s.v[kLength] = 0.4;
         Engine e;
         e.prepare(48000.0);
         e.seed(8);
         e.noteOn(60, -1, -1, 0.9, s);
         e.bend(60, -1, -1, k ? 12.0 : 0.0);
         std::vector<float> l(48), r(48);
         int n = 0;
         for (int b = 0; b < 300; ++b) {
            e.process(l.data(), r.data(), 48, s);
            const VoiceView v = e.view(0);
            if (v.event != kEvNone && b > 20) {
               mean[k] += v.eventPitch;
               ++n;
            }
         }
         mean[k] /= std::max(1, n);
      }
      std::printf("droid pitch %.1f, bent +12: %.1f\n", mean[0], mean[1]);
      JCLAP_CHECK(std::fabs(mean[1] - mean[0] - 12.0) < 1.0);
   }

   // The SUB moving to a new lowest note, and back, doesn't click: a sine-only drone keeps a smooth
   // waveform (its worst second difference stays tiny) through a lower note, its release and a steal.
   {
      Settings s = defaults();
      for (int k = 0; k < kLayerCount; ++k)
         s.v[kLayer0 + k] = k == kLayerSub ? 0.5 : 0.0;
      s.v[kEnv1Amount] = s.v[kEnv2Amount] = s.v[kLfo1Amount] = s.v[kLfo2Amount] = 0.0;
      s.v[kBreath] = 0.0;
      s.v[kMorph] = 1.0;
      s.v[kSpaceMix] = 0.0;
      s.v[kMasterLevel] = -12.0;
      s.v[kVoices] = 2;
      s.v[kAttack] = 0.3;
      s.v[kRelease] = 0.6;
      s.v[kSustain] = 0.6;
      Engine e;
      e.prepare(48000.0);
      e.seed(2);
      struct Ev {
         int t, key;
         bool on;
      };
      const Ev evs[] = {{0, 48, true},     {48000, 43, true},   {96000, 43, false}, {120000, 55, true},
                        {144000, 60, true}, {170000, 48, false}, {190000, 55, false}, {200000, 60, false}};
      std::vector<float> l(64), r(64);
      double worst = 0, prev[2] = {0, 0};
      size_t k = 0;
      for (int t = 0; t < 48000 * 6; t += 64) {
         for (; k < std::size(evs) && evs[k].t <= t; ++k)
            evs[k].on ? (void)e.noteOn(evs[k].key, -1, -1, 0.8, s) : e.noteOff(evs[k].key, -1, -1);
         e.process(l.data(), r.data(), 64, s);
         for (int i = 0; i < 64; ++i) {
            worst = std::max(worst, std::fabs(l[i] - 2 * prev[0] + prev[1]));
            prev[1] = prev[0];
            prev[0] = l[i];
         }
      }
      std::printf("sub-only drone: worst second difference %.2e\n", worst);
      JCLAP_CHECK(worst < 2e-4);
   }

   // DEGRADE still speaks, and changes the sound; SPACE's delay repeats a short sound after it ends.
   {
      double rms[2] = {0, 0};
      for (int k = 0; k < 2; ++k) {
         Settings s = defaults();
         s.v[kDegrade] = k ? 1.0 : 0.0;
         s.v[kSpaceMix] = 0.0;
         Engine e;
         e.prepare(48000.0);
         e.seed(12);
         e.noteOn(60, -1, -1, 0.9, s);
         std::vector<float> l(4800), r(4800);
         double sum = 0;
         bool finite = true;
         for (int b = 0; b < 5; ++b) {
            e.process(l.data(), r.data(), 4800, s);
            for (float x : l) {
               finite = finite && std::isfinite(x);
               sum += x * x;
            }
         }
         rms[k] = std::sqrt(sum / 24000);
         JCLAP_CHECK(finite);
      }
      std::printf("degrade 0: rms %.3f, degrade 1: rms %.3f\n", rms[0], rms[1]);
      JCLAP_CHECK(rms[1] > 0.2 * rms[0] && rms[0] > 0.01);

      Settings s = defaults();
      s.v[kLength] = 0.05;
      s.v[kSpaceMix] = 0.0;
      s.v[kDelayOn] = 1.0;
      s.v[kDelayMix] = 0.8;
      s.v[kDelayFeedback] = 0.3;
      s.v[kDelayTime] = std::log(300.0 / 10.0) / std::log(200.0); // 300 ms
      Engine e;
      e.prepare(48000.0);
      e.seed(13);
      e.noteOn(60, -1, -1, 0.9, s);
      std::vector<float> l(4800), r(4800);
      double echo = 0;
      for (int b = 0; b < 8; ++b) {
         e.process(l.data(), r.data(), 4800, s);
         if (b >= 3 && b < 5) // 0.3 to 0.5 s: the first repeat, after the 50 ms syllable is over
            for (float x : l)
               echo = std::max(echo, static_cast<double>(std::fabs(x)));
      }
      std::printf("delay: first repeat peak %.3f\n", echo);
      JCLAP_CHECK(echo > 0.02);
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
