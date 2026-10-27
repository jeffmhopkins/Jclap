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
      // A host takes a block's events in time order, NOTE_ENDs included.
      for (int i = 1; i < e.endedCount(); ++i)
         JCLAP_CHECK(e.ended(i - 1).sample <= e.ended(i).sample);
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

   // Six notes let go a few samples apart, the first-played last, end inside one tick in the
   // order they were let go, whichever voice slot each is in.
   {
      Params p;
      auto e = std::make_unique<Engine>();
      e->prepare(kRate, {p.values.get(), p.mods.get()});
      std::vector<Event> ev;
      for (int i = 0; i < 6; ++i)
         ev.push_back({100, Event::On, 48 + 5 * i, 1.0f - 0.15f * i});
      for (int i = 0; i < 6; ++i)
         ev.push_back({24000 + 3 * (5 - i), Event::Off, 48 + 5 * i, 0.0f});
      std::sort(ev.begin(), ev.end(), [](const Event &a, const Event &b) { return a.at < b.at; });
      int ends = 0;
      render(*e, 240000, 4096, ev, &ends);
      JCLAP_CHECK(ends == 6);
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

   // Per-note host modulation moves only the note it names: Level down 60 dB on one of two notes.
   {
      // On the heap: an engine is big, and Windows gives a test 1 MB of stack.
      Params p;
      JCLAP_CHECK(kParams()[paramIndex(partId(0, kPartLevel))].def.flags & CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID);
      const auto level = [&](int key) {
         auto f = std::make_unique<Engine>();
         f->prepare(kRate, {p.values.get(), p.mods.get()});
         f->setBlockSample(0);
         f->noteOn(ref(60), 0.8f);
         f->noteOn(ref(64), 0.8f);
         f->voiceMod(ref(key), paramIndex(partId(0, kPartLevel)), -66.0f);
         return peak(render(*f, 24000, 256, {}));
      };
      const float none = [&] {
         auto f = std::make_unique<Engine>();
         f->prepare(kRate, {p.values.get(), p.mods.get()});
         f->setBlockSample(0);
         f->noteOn(ref(60), 0.8f);
         return peak(render(*f, 24000, 256, {}));
      }();
      // Silencing 64 leaves about 60 alone; and a mod on a note not playing does nothing.
      JCLAP_CHECK(std::fabs(level(64) - none) < 0.02f * none + 1e-3f);
      JCLAP_CHECK(level(61) > none * 1.2f);
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

   // Wave Random draws a wave for each note: the same key, played eight times from the same phase,
   // takes more than one shape (by its crest factor, peak over RMS).
   {
      Params p;
      p.setPart(0, oscParam(0, kOscWave), dsp::kRandom);
      p.setPart(0, oscParam(0, kOscRetrigger), 1);
      p.setPart(0, filterParam(0, kFilterCutoff), 1.0);
      p.setPart(0, kDrift, 0.0);
      for (int r = 0; r < kRouteCount; ++r)
         p.setPart(0, routeParam(r, kRouteSource), kSourceOff);
      std::vector<Event> ev;
      for (int n = 0; n < 8; ++n) {
         ev.push_back({n * 12000, Event::On, 57, 1.0f});
         ev.push_back({n * 12000 + 6000, Event::Off, 57, 0.0f});
      }
      auto e = std::make_unique<Engine>();
      e->prepare(kRate, {p.values.get(), p.mods.get()});
      const auto out = render(*e, 8 * 12000, 512, ev);
      std::vector<int> crests;
      for (int n = 0; n < 8; ++n) {
         double sum = 0, top = 0;
         for (size_t i = (n * 12000 + 3000) * 2; i < static_cast<size_t>(n * 12000 + 5000) * 2; i += 2) {
            sum += out[i] * out[i];
            top = std::max(top, static_cast<double>(std::fabs(out[i])));
         }
         crests.push_back(static_cast<int>(10.0 * top / std::sqrt(sum / 2000.0) + 0.5));
      }
      std::sort(crests.begin(), crests.end());
      const auto kinds = std::unique(crests.begin(), crests.end()) - crests.begin();
      std::printf("random wave: %d shapes in 8 notes\n", static_cast<int>(kinds));
      JCLAP_CHECK(finite(out) && kinds >= 3);
   }

   // Each oscillator picks its filter: in serial, F2 skips a closed Filter 1; in parallel, Balance
   // all the way to Filter 1 silences what goes only into Filter 2.
   {
      const auto level = [](int routing, int dest, double balance) {
         Params p;
         p.setPart(0, filterParam(0, kFilterCutoff), 0.0);
         p.setPart(0, tailParam(kFilterRouting), routing);
         p.setPart(0, tailParam(kFilterBalance), balance);
         p.setPart(0, oscParam(0, kOscFilter), dest);
         for (int r = 0; r < kRouteCount; ++r)
            p.setPart(0, routeParam(r, kRouteSource), kSourceOff);
         // On the heap: MSVC gives each test's Engine its own room in main's frame.
         auto e = std::make_unique<Engine>();
         e->prepare(kRate, {p.values.get(), p.mods.get()});
         const auto out = render(*e, 24000, 512, {{0, Event::On, 57, 1.0f}});
         double sum = 0;
         for (size_t i = 19200; i < out.size(); ++i)
            sum += out[i] * out[i];
         return std::sqrt(sum / (out.size() - 19200));
      };
      const double serialF1 = level(0, 1, 0.0), serialF2 = level(0, 2, 0.0), serialBoth = level(0, 0, 0.0);
      const double parallelF1 = level(1, 1, -1.0), parallelF2 = level(1, 2, -1.0);
      std::printf("filter picks: serial F1 %.4f F2 %.4f both %.4f, parallel at F1 %.4f %.4f\n", serialF1, serialF2,
                  serialBoth, parallelF1, parallelF2);
      JCLAP_CHECK(serialF2 > 10.0 * serialF1);
      JCLAP_CHECK(std::fabs(serialBoth - serialF1) < 1e-6);
      JCLAP_CHECK(parallelF2 < 1e-6 && parallelF1 < serialF2);
   }

   // The panel sees what a route does to the newest voice: Press on FB 2 moves that cell's value
   // while the key is pressed, and nothing else; no voice, nothing to show.
   {
      Params p;
      for (int r = 0; r < kRouteCount; ++r)
         p.setPart(0, routeParam(r, kRouteSource), kSourceOff);
      p.setPart(0, routeParam(0, kRouteSource), kSourceVelocity + 1);
      p.setPart(0, routeParam(0, kRouteDestination), destinationIndex(fmCell(1, 1)));
      p.setPart(0, routeParam(0, kRouteAmount), 0.5);
      auto e = std::make_unique<Engine>();
      e->prepare(kRate, {p.values.get(), p.mods.get()});
      std::vector<float> value(kDestSlots);
      std::unique_ptr<bool[]> moved(new bool[kDestSlots]);
      JCLAP_CHECK(!e->destinationsNow(0, value.data(), moved.get()));
      render(*e, 12000, 512, {{0, Event::On, 60, 1.0f}, {100, Event::Pressure, 60, 1.0f}});
      JCLAP_CHECK(e->destinationsNow(0, value.data(), moved.get()));
      const int fb = destinationIndex(fmCell(1, 1));
      int others = 0;
      for (int d = 0; d < static_cast<int>(destinations().size()); ++d)
         others += d != fb && moved[d];
      const double base = kParams()[paramIndex(partId(0, fmCell(1, 1)))].def.defaultValue;
      std::printf("FB 2 with Press: %.3f from %.3f\n", value[fb], base);
      JCLAP_CHECK(moved[fb] && value[fb] > base + 0.1);
      JCLAP_CHECK(others == 0);
      JCLAP_CHECK(destinations().size() <= static_cast<size_t>(kDestSlots));
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

   // A creature lives as long as its voice: through a long release after its note is let go, and
   // gone soon after the voice ends.
   {
      Params p;
      p.setPart(0, envParam(0, kEnvRelease), timeValue(3000));
      Engine e;
      e.prepare(kRate, {p.values.get(), p.mods.get()});
      const auto played = [&] {
         int n = 0;
         for (int i = 0; i < kVoiceSlots; ++i)
            n += e.flock().creatures[i].alive;
         return n;
      };
      render(e, 96000, 256, {{100, Event::On, 60, 0.8f}, {4800, Event::Off, 60, 0.0f}});
      JCLAP_CHECK_EQ(e.activeVoices(), 1);
      JCLAP_CHECK_EQ(played(), 1);
      render(e, 48000 * 12, 256, {});
      JCLAP_CHECK_EQ(e.activeVoices(), 0);
      JCLAP_CHECK_EQ(played(), 0);
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

   // Synced LFOs that aren't retriggered sit on the host's bar: playback started a bar in hears
   // what playback from the top hears there, S&H values included, for the part's (Global) LFO and
   // a held note's (Free) one alike.
   {
      const auto levels = [](int trigger, int shape, double fromBeat, int blocks) {
         Params p;
         p.setPart(0, lfoParam(2, kLfoSync), 1);
         p.setPart(0, lfoParam(2, kLfoDivision), 2); // 1/16
         p.setPart(0, lfoParam(2, kLfoShape), shape);
         p.setPart(0, lfoParam(2, kLfoTrigger), trigger);
         p.setPart(0, lfoParam(2, kLfoPhase), 90.0);
         auto e = std::make_unique<Engine>();
         e->prepare(kRate, {p.values.get(), p.mods.get()});
         std::vector<float> l(256), r(256);
         float *ch[2] = {l.data(), r.data()};
         float *const *ports[3] = {ch, ch, ch};
         std::vector<float> out;
         for (int b = 0; b < blocks; ++b) {
            const double beats = fromBeat + b * 256 * 128.0 / 60.0 / kRate;
            e->transport(128.0, true, &beats);
            if (b == 0 && trigger != 2) {
               e->setBlockSample(0);
               e->noteOn(ref(60), 0.8f);
            }
            e->render(ports, 0, 256);
            e->clearEnded();
            out.push_back(e->lfoLevel(0, 2));
         }
         return out;
      };
      // 64 beats at 128 BPM is 5625 blocks of 256 exactly, so the later start falls on a block.
      constexpr int kSkip = 5625;
      for (int trigger : {1, 2})
         for (int shape : {dsp::Lfo::kSine, dsp::Lfo::kSampleHold, dsp::Lfo::kSmooth}) {
            const auto top = levels(trigger, shape, 0.0, kSkip + 400);
            const auto in = levels(trigger, shape, 64.0, 400);
            float worst = 0;
            for (int b = 1; b < 400; ++b)
               worst = std::max(worst, std::fabs(top[kSkip + b] - in[b]));
            JCLAP_CHECK(worst < 1e-3f);
         }
      // And it is on the beat: a synced Global sine at 1/16 with 90 degrees of Phase is at its top
      // on each sixteenth, so at its bottom halfway between, where this playback starts.
      {
         const auto at = levels(2, dsp::Lfo::kSine, 1.125, 1);
         JCLAP_CHECK(std::fabs(at[0] + 1.0f) < 0.05f);
      }
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
