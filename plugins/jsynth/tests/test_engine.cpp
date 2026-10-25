// The engine on its own, without the plugin around it: that it sounds, in tune and finite; that the
// output doesn't depend on the host's block size or on how long it has run; that each host note
// gets exactly one NOTE_END; and what it costs.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "engine.h"
#include "presets.h"

using namespace jsynth;

namespace {

constexpr double kRate = 48000.0;

// The params the engine reads, at their defaults, settable by id.
struct Params {
   std::unique_ptr<std::atomic<double>[]> values{new std::atomic<double>[paramCount()]};
   std::unique_ptr<std::atomic<double>[]> mods{new std::atomic<double>[paramCount()]};
   Params() {
      for (size_t i = 0; i < paramCount(); ++i) {
         values[i].store(kParams()[i].def.defaultValue);
         mods[i].store(0.0);
      }
   }
   void set(clap_id id, double v) { values[paramIndex(id)].store(v); }
   void setPart(int part, clap_id offset, double v) { set(partId(part, offset), v); }
};

// Something the test plays at a sample: a note on or off, or a pressure change.
struct Event {
   int at;
   enum Kind { On, Off, Pressure } kind;
   int key;
   float value;
};

NoteRef ref(int key) {
   NoteRef r;
   r.key = static_cast<int16_t>(key);
   r.channel = 0;
   r.port = 0;
   r.noteId = key;
   r.host = true;
   return r;
}

// Renders `frames` samples in host blocks of `block`, applying `events` on their samples, and
// returns the main output's left and right interleaved. Counts NOTE_ENDs into `ends` if given.
std::vector<float> render(Engine &e, int frames, int block, const std::vector<Event> &events, int *ends = nullptr) {
   std::vector<float> out;
   out.reserve(static_cast<size_t>(frames) * 2);
   std::vector<float> l(block), r(block), al(block), ar(block), bl(block), br(block);
   float *mainCh[2] = {l.data(), r.data()}, *aCh[2] = {al.data(), ar.data()}, *bCh[2] = {bl.data(), br.data()};
   float *const *ports[3] = {mainCh, aCh, bCh};
   size_t next = 0;
   for (int start = 0; start < frames; start += block) {
      const int n = std::min(block, frames - start);
      std::fill(l.begin(), l.end(), 0.0f);
      std::fill(r.begin(), r.end(), 0.0f);
      int at = 0;
      while (next < events.size() && events[next].at < start + n) {
         const Event &ev = events[next++];
         const int t = std::max(0, ev.at - start);
         if (t > at) {
            e.render(ports, at, t - at);
            at = t;
         }
         e.setBlockSample(at);
         if (ev.kind == Event::On)
            e.noteOn(ref(ev.key), ev.value);
         else if (ev.kind == Event::Off)
            e.noteOff(ref(ev.key), 0.0f);
         else
            e.expression(ref(ev.key), CLAP_NOTE_EXPRESSION_PRESSURE, ev.value);
      }
      if (at < n)
         e.render(ports, at, n - at);
      if (ends)
         *ends += e.endedCount();
      e.clearEnded();
      for (int i = 0; i < n; ++i) {
         out.push_back(l[i]);
         out.push_back(r[i]);
      }
   }
   return out;
}

bool finite(const std::vector<float> &x) {
   for (float v : x)
      if (!std::isfinite(v))
         return false;
   return true;
}
float peak(const std::vector<float> &x) {
   float p = 0;
   for (float v : x)
      p = std::max(p, std::fabs(v));
   return p;
}

// A chord with movement: notes on, pressure sweeps, notes off.
std::vector<Event> phrase() {
   std::vector<Event> ev;
   const int keys[] = {48, 55, 60, 64, 67, 71};
   for (int i = 0; i < 6; ++i)
      ev.push_back({1000 + i * 1777, Event::On, keys[i], 0.5f + 0.08f * i});
   for (int t = 0; t < 40; ++t)
      ev.push_back({12000 + t * 301, Event::Pressure, keys[t % 6], 0.5f + 0.5f * std::sin(t * 0.7f)});
   for (int i = 0; i < 6; ++i)
      ev.push_back({30000 + i * 913, Event::Off, keys[i], 0.0f});
   std::sort(ev.begin(), ev.end(), [](const Event &a, const Event &b) { return a.at < b.at; });
   return ev;
}

} // namespace

int main() {
   // Silence with nothing playing.
   {
      Params p;
      Engine e;
      e.prepare(kRate, {p.values.get(), p.mods.get()});
      const auto out = render(e, 24000, 256, {});
      JCLAP_CHECK(peak(out) == 0.0f);
   }

   // A note sounds, stays finite and under full scale, and ends with exactly one NOTE_END.
   {
      Params p;
      p.set(kPlayMode, 1); // Layer: one note, a voice in each part
      Engine e;
      e.prepare(kRate, {p.values.get(), p.mods.get()});
      int ends = 0;
      const auto out = render(e, 96000, 256, {{100, Event::On, 60, 0.8f}, {24000, Event::Off, 60, 0.0f}}, &ends);
      JCLAP_CHECK(finite(out));
      JCLAP_CHECK(peak(out) > 0.01f);
      JCLAP_CHECK(peak(out) <= 1.0f);
      JCLAP_CHECK(ends == 1);
      JCLAP_CHECK(e.activeVoices() == 0);
   }

   // In tune: a lone sine at A4 crosses zero 440 times a second.
   {
      Params p;
      p.setPart(0, oscParam(0, kOscWave), 0);
      p.setPart(0, filterParam(0, kFilterCutoff), 1.0);
      p.setPart(0, kDrift, 0.0);
      for (int r = 0; r < kRouteCount; ++r)
         p.setPart(0, routeParam(r, kRouteSource), kSourceOff);
      p.set(kQuality, 0);
      Engine e;
      e.prepare(kRate, {p.values.get(), p.mods.get()});
      const auto out = render(e, 72000, 512, {{0, Event::On, 69, 1.0f}});
      int crossings = 0;
      for (size_t i = 48000; i < 48000 + 96000; i += 2)
         crossings += (out[i] <= 0.0f) != (out[i + 2] <= 0.0f);
      const double hz = crossings / 2.0;
      std::printf("A4 measures %.2f Hz\n", hz);
      JCLAP_CHECK(std::fabs(hz - 440.0) < 2.0);
   }

   // The host's block size changes nothing: the control grid is the engine's own.
   {
      Params p;
      p.set(kPlayMode, 1);
      std::vector<float> outs[3];
      const int blocks[3] = {512, 37, 64};
      for (int b = 0; b < 3; ++b) {
         Engine e;
         e.prepare(kRate, {p.values.get(), p.mods.get()});
         outs[b] = render(e, 48000, blocks[b], phrase());
      }
      JCLAP_CHECK(outs[0] == outs[1]);
      JCLAP_CHECK(outs[0] == outs[2]);
      JCLAP_CHECK(peak(outs[0]) > 0.01f);
   }

   // A reset starts everything (the flock, every random sequence) over: the same notes again give
   // the same sound.
   {
      Params p;
      p.set(kPlayMode, 1);
      Engine e;
      e.prepare(kRate, {p.values.get(), p.mods.get()});
      const auto first = render(e, 48000, 256, phrase());
      e.reset(1);
      const auto again = render(e, 48000, 256, phrase());
      JCLAP_CHECK(first == again);
   }

   // Every factory preset plays finite and under full scale, at every quality.
   {
      int i = 0;
      for (int quality = 0; quality < 3; ++quality) {
         Params p;
         p.set(kQuality, quality);
         // Push the dangerous corners: full feedback, full resonance, hard drive.
         for (int part = 0; part < kPartCount; ++part) {
            for (int o = 0; o < kOscCount; ++o)
               p.setPart(part, fmCell(o, o), 1.0);
            p.setPart(part, fmCell(1, 0), 1.0);
            p.setPart(part, filterParam(0, kFilterResonance), 1.0);
            p.setPart(part, filterParam(1, kFilterResonance), 1.0);
            p.setPart(part, filterParam(0, kFilterType), (quality * 3 + i++) % 8);
            p.setPart(part, tailParam(kDriveAmount), 1.0);
            p.setPart(part, kFmBase + kFmType, quality == 1);
            p.setPart(part, flockParam(kNeighbourFm), 1.0);
            p.setPart(part, flockParam(kBite), 1.0);
         }
         p.set(kPlayMode, 1);
         Engine e;
         e.prepare(kRate, {p.values.get(), p.mods.get()});
         const auto out = render(e, 48000, 128, phrase());
         JCLAP_CHECK(finite(out));
         JCLAP_CHECK(peak(out) <= 1.0f);
      }
   }

   // Legato: A, then B over it, B up, A up. A is ended once, when it is let go, and B once.
   {
      Params p;
      p.setPart(0, kVoiceMode, 2);
      Engine e;
      e.prepare(kRate, {p.values.get(), p.mods.get()});
      int ends = 0;
      render(e, 96000, 256, {{100, Event::On, 60, 0.8f}, {5000, Event::On, 64, 0.8f}, {10000, Event::Off, 64, 0.0f},
                             {15000, Event::Off, 60, 0.0f}}, &ends);
      JCLAP_CHECK_EQ(ends, 2);
      JCLAP_CHECK_EQ(e.activeVoices(), 0);
   }

   // Mono: a note choked away isn't fallen back to later.
   {
      Params p;
      p.setPart(0, kVoiceMode, 1);
      Engine e;
      e.prepare(kRate, {p.values.get(), p.mods.get()});
      render(e, 4800, 256, {{100, Event::On, 60, 0.8f}});
      NoteRef all = ref(-1);
      all.noteId = -1, all.channel = -1, all.port = -1;
      e.choke(all);
      render(e, 96000, 256, {{100, Event::On, 64, 0.8f}, {5000, Event::Off, 64, 0.0f}});
      JCLAP_CHECK_EQ(e.activeVoices(), 0);
   }

   // Every factory preset sounds, finite and under full scale.
   for (const Preset &preset : factoryPresets()) {
      Params p;
      const std::vector<double> v = presetValues(preset);
      for (size_t i = 0; i < paramCount(); ++i)
         p.values[i].store(v[i]);
      Engine e;
      e.prepare(kRate, {p.values.get(), p.mods.get()});
      const auto out = render(e, 96000, 256, phrase());
      const bool ok = finite(out) && peak(out) <= 1.0f && peak(out) > 1e-4f; // a swell has barely begun by the end
      if (!ok)
         std::printf("preset %s: peak %g\n", preset.name.c_str(), peak(out));
      JCLAP_CHECK(ok);
   }

   // What it costs: 32 voices (16 notes in each part) at 2x for two seconds.
   {
      Params p;
      p.set(kPlayMode, 1);
      p.set(kQuality, 1);
      for (int part = 0; part < kPartCount; ++part) {
         p.setPart(part, kPolyphony, 16);
         p.setPart(part, oscParam(1, kOscWave), 2);
         p.setPart(part, mixParam(kMixLevel, 1), 0.7);
         p.setPart(part, fmCell(1, 0), 0.4);
         p.setPart(part, filterParam(0, kFilterSlope), 3);
      }
      Engine e;
      e.prepare(kRate, {p.values.get(), p.mods.get()});
      std::vector<Event> notes;
      for (int k = 0; k < 16; ++k)
         notes.push_back({k, Event::On, 40 + k * 2, 0.7f});
      const auto t0 = std::chrono::steady_clock::now();
      const auto out = render(e, 96000, 256, notes);
      const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      std::printf("32 voices at 2x: %.1f%% of a core (%d voices)\n", seconds / 2.0 * 100.0, e.activeVoices());
      JCLAP_CHECK(finite(out));
      JCLAP_CHECK(e.activeVoices() == 32);
   }

   return jclap::core::testResult();
}
