// JArp's engine on its own: rows mapped onto held notes, timing and swing, note lengths, ratchets,
// pressure, latch and the saved pattern.

#include <jclap/core/test_host.h>

#include <vector>

#include "engine.h"

using namespace jarp;

namespace {

// At 120 BPM and 48 kHz a 1/16 step is 6000 samples.
constexpr int kStepSamples = 6000;
constexpr uint32_t kBlock = 256;

struct Timed {
   int64_t time;
   MidiOut m;
   bool on() const { return (m.status & 0xF0) == 0x90; }
   int channel() const { return m.status & 0x0F; }
};

struct In {
   int64_t time;
   enum Kind { On, Off, Pressure } kind;
   int channel, key, value;
};

// Runs the engine for `samples` from beat `from`, with `in` played at their times, and returns
// everything it sent, timed from the start.
std::vector<Timed> run(Engine &e, int64_t samples, std::vector<In> in, bool playing = true, double from = 0.0) {
   std::vector<Timed> all;
   OutBuffer out;
   for (int64_t at = 0; at < samples; at += kBlock) {
      Block b;
      b.frames = kBlock;
      b.sampleRate = 48000.0;
      b.hasTransport = true;
      b.playing = playing;
      b.tempo = 120.0;
      b.beats = from + at * 2.0 / 48000.0;
      out.clear();
      e.beginBlock(b, out);
      for (const In &i : in)
         if (i.time >= at && i.time < at + kBlock) {
            const uint32_t t = static_cast<uint32_t>(i.time - at);
            if (i.kind == In::On)
               e.noteOn(t, i.channel, i.key, i.value, out);
            else if (i.kind == In::Off)
               e.noteOff(t, i.channel, i.key, out);
            else {
               e.advance(t, out);
               e.setPressure(i.channel, -1, i.value / 127.0);
            }
         }
      e.advance(kBlock, out);
      for (size_t k = 0; k < out.count; ++k)
         if (at + out.items[k].time < samples)
            all.push_back({at + out.items[k].time, out.items[k]});
   }
   return all;
}

std::vector<Timed> ons(const std::vector<Timed> &all) {
   std::vector<Timed> r;
   for (const Timed &t : all)
      if (t.on())
         r.push_back(t);
   return r;
}

std::vector<In> chord(std::initializer_list<int> keys, int64_t at = 0, int velocity = 100) {
   std::vector<In> in;
   int channel = 1;
   for (int k : keys)
      in.push_back({at, In::On, channel++, k, velocity});
   return in;
}

Pattern pattern(std::initializer_list<Note> notes) {
   Pattern p;
   for (const Note &n : notes)
      p.add(n.row, n.step, n.length);
   return p;
}

} // namespace

int main() {
   // ---- rows ----
   {
      // Pitch: row 1 is the lowest held note; beyond the notes held, Rest leaves a row empty and
      // Wrap starts again from the bottom.
      Settings s;
      const int held[] = {48, 52, 55};
      int key[kRows], target[kRows];
      mapRows(held, 3, s, key, target);
      JCLAP_CHECK_EQ(key[0], 0);
      JCLAP_CHECK_EQ(key[2], 2);
      JCLAP_CHECK_EQ(key[3], -1);
      s.wrap = true;
      mapRows(held, 3, s, key, target);
      JCLAP_CHECK_EQ(key[3], 0);
      JCLAP_CHECK_EQ(key[7], 1);
   }
   {
      // Degree: a Cmaj7 in C major gives 1st C, 3rd E, 5th G, 7th B; the 2nd (D) is nearest C and E
      // equally, and both are in the scale, so the lower wins.
      Settings s;
      s.degree = true;
      const int held[] = {48, 52, 55, 59};
      int key[kRows], target[kRows];
      mapRows(held, 4, s, key, target);
      JCLAP_CHECK_EQ(key[0], 0);
      JCLAP_CHECK_EQ(key[2], 1);
      JCLAP_CHECK_EQ(key[4], 2);
      JCLAP_CHECK_EQ(key[6], 3);
      JCLAP_CHECK_EQ(target[1], 50);
      JCLAP_CHECK_EQ(key[1], 0);
      // The 8th (C4) is nearest B3.
      JCLAP_CHECK_EQ(key[7], 3);
   }
   {
      // Cm7 held with the scale on C major: the 4th wants F3, Eb3 and G3 are equally near, and G is in
      // the scale, so G wins.
      Settings s;
      s.degree = true;
      const int held[] = {48, 51, 55, 58};
      int key[kRows], target[kRows];
      mapRows(held, 4, s, key, target);
      JCLAP_CHECK_EQ(target[3], 53);
      JCLAP_CHECK_EQ(key[3], 2);
      // In C minor the 3rd is Eb itself.
      s.scale = 1;
      mapRows(held, 4, s, key, target);
      JCLAP_CHECK_EQ(target[2], 51);
      JCLAP_CHECK_EQ(key[2], 1);
   }

   // ---- timing ----
   {
      // A 7th chord, rows 1 and 3 alternating: C on step 1, G on step 2, each on its own finger's
      // channel and key, at the finger's velocity.
      Engine e;
      Settings s;
      s.pressVelocity = 0.0;
      e.setSettings(s);
      e.setPattern(pattern({{0, 0, 4}, {2, 1, 4}, {0, 2, 4}, {2, 3, 4}}));
      const auto out = run(e, 4 * kStepSamples + 100, chord({60, 64, 67, 70}));
      const auto on = ons(out);
      JCLAP_CHECK_EQ(on.size(), 4u);
      JCLAP_CHECK_EQ(on[0].time, 0);
      JCLAP_CHECK_EQ(on[0].m.data1, 60);
      JCLAP_CHECK_EQ(on[0].channel(), 1);
      JCLAP_CHECK_EQ(on[0].m.data2, 100);
      JCLAP_CHECK_EQ(on[1].time, kStepSamples);
      JCLAP_CHECK_EQ(on[1].m.data1, 67);
      JCLAP_CHECK_EQ(on[1].channel(), 3);
      JCLAP_CHECK_EQ(on[3].time, 3 * kStepSamples);
   }
   {
      // The pattern loops at its length; a note longer than a step holds, and a quarter-step note
      // is a quarter of one.
      Engine e;
      Settings s;
      s.length = 2;
      e.setSettings(s);
      e.setPattern(pattern({{0, 0, 1}, {1, 1, 4}}));
      const auto out = run(e, 4 * kStepSamples - 10, chord({60, 64}));
      const auto on = ons(out);
      JCLAP_CHECK_EQ(on.size(), 4u);
      JCLAP_CHECK_EQ(on[2].time, 2 * kStepSamples);
      JCLAP_CHECK_EQ(out[1].on(), false);
      JCLAP_CHECK_EQ(out[1].time, kStepSamples / 4);
   }
   {
      // A held bass for the whole pattern sounds once, under the rest.
      Engine e;
      Settings s;
      s.length = 4;
      e.setSettings(s);
      e.setPattern(pattern({{0, 0, 16}, {1, 0, 2}, {1, 1, 2}, {1, 2, 2}, {1, 3, 2}}));
      const auto out = run(e, 4 * kStepSamples - 10, chord({48, 60}));
      int bass = 0;
      for (const Timed &t : ons(out))
         bass += t.m.data1 == 48;
      JCLAP_CHECK_EQ(bass, 1);
      JCLAP_CHECK_EQ(ons(out).size(), 5u);
   }
   {
      // Swing at 75% puts the second of each pair of steps three quarters of the way through it.
      Engine e;
      Settings s;
      s.swing = 0.75;
      e.setSettings(s);
      e.setPattern(pattern({{0, 0, 4}, {0, 1, 4}, {0, 2, 4}}));
      const auto on = ons(run(e, 3 * kStepSamples, chord({60})));
      JCLAP_CHECK_EQ(on.size(), 3u);
      JCLAP_CHECK_EQ(on[1].time, kStepSamples * 3 / 2);
      JCLAP_CHECK_EQ(on[2].time, 2 * kStepSamples);
   }
   {
      // Back-to-back notes on one key: the first ends before the second starts, at the same time.
      Engine e;
      e.setPattern(pattern({{0, 0, 4}, {0, 1, 4}}));
      const auto out = run(e, 2 * kStepSamples - 10, chord({60}));
      JCLAP_CHECK_EQ(out.size(), 3u);
      JCLAP_CHECK(out[0].on());
      JCLAP_CHECK(!out[1].on());
      JCLAP_CHECK_EQ(out[1].time, kStepSamples);
      JCLAP_CHECK(out[2].on());
      JCLAP_CHECK_EQ(out[2].time, kStepSamples);
   }
   {
      // The transport stopped: the arp runs on its own clock.
      Engine e;
      e.setPattern(pattern({{0, 0, 4}, {0, 1, 4}}));
      const auto on = ons(run(e, 2 * kStepSamples, chord({60}), false));
      JCLAP_CHECK_EQ(on.size(), 2u);
      JCLAP_CHECK_EQ(on[1].time, kStepSamples);
   }
   {
      // Restart Free follows the song position: a chord pressed part way through step 2 waits for
      // step 3.
      Engine e;
      Settings s;
      s.restart = Restart::Free;
      e.setSettings(s);
      e.setPattern(pattern({{0, 0, 4}, {0, 1, 4}, {0, 2, 4}, {0, 3, 4}}));
      const auto on = ons(run(e, 3 * kStepSamples, chord({60}, kStepSamples + 1000)));
      JCLAP_CHECK_EQ(on.size(), 1u);
      JCLAP_CHECK_EQ(on[0].time, 2 * kStepSamples);
   }

   // ---- lanes ----
   {
      // Octave, ratchet and velocity, per step.
      Engine e;
      Settings s;
      s.pressVelocity = 0.0;
      e.setSettings(s);
      Pattern p = pattern({{0, 0, 4}, {0, 1, 4}});
      p.lanes[0].octave = 1;
      p.lanes[0].velocity = -20;
      p.lanes[1].ratchet = 3;
      e.setPattern(p);
      const auto on = ons(run(e, 2 * kStepSamples - 10, chord({60})));
      JCLAP_CHECK_EQ(on.size(), 4u);
      JCLAP_CHECK_EQ(on[0].m.data1, 72);
      JCLAP_CHECK_EQ(on[0].m.data2, 80);
      JCLAP_CHECK_EQ(on[1].time, kStepSamples);
      JCLAP_CHECK_EQ(on[2].time, kStepSamples + kStepSamples / 3);
      JCLAP_CHECK_EQ(on[3].time, kStepSamples + 2 * kStepSamples / 3);
   }
   {
      // Pressure: a P step adds hits up to Max Hits as the finger presses, and velocity follows
      // pressure by Pressure to Velocity.
      Engine e;
      Settings s;
      s.pressRatchet = 1.0;
      s.maxHits = 4;
      s.pressVelocity = 1.0;
      e.setSettings(s);
      Pattern p = pattern({{0, 0, 4}});
      p.lanes[0].pressure = 1;
      p.lanes[0].velocity = 10;
      e.setPattern(p);
      std::vector<In> in = {{0, In::Pressure, 1, -1, 0}, {0, In::On, 1, 60, 100}};
      auto on = ons(run(e, kStepSamples - 10, in));
      JCLAP_CHECK_EQ(on.size(), 1u);
      JCLAP_CHECK_EQ(on[0].m.data2, 10);

      Engine hard;
      hard.setSettings(s);
      hard.setPattern(p);
      in = {{0, In::Pressure, 1, -1, 127}, {0, In::On, 1, 60, 100}};
      on = ons(run(hard, kStepSamples - 10, in));
      JCLAP_CHECK_EQ(on.size(), 4u);
      JCLAP_CHECK_EQ(on[0].m.data2, 127);
      Frame f;
      hard.fillFrame(f);
      JCLAP_CHECK_EQ(f.hits[0], 4);
      JCLAP_CHECK(f.rows[0].pressure > 0.99f);
   }
   {
      // The curve: soft reaches further on a light touch than hard.
      JCLAP_CHECK(curvePressure(0.3, -1.0) > curvePressure(0.3, 0.0));
      JCLAP_CHECK(curvePressure(0.3, 1.0) < curvePressure(0.3, 0.0));
      JCLAP_CHECK_EQ(curvePressure(1.0, 1.0), 1.0);
   }

   // ---- fingers ----
   {
      // Letting go ends that finger's note at once; the rest play on, rows remapped.
      Engine e;
      e.setPattern(pattern({{0, 0, 16}, {1, 0, 16}}));
      std::vector<In> in = chord({60, 64});
      in.push_back({1000, In::Off, 1, 60, 0});
      const auto out = run(e, kStepSamples, in);
      JCLAP_CHECK_EQ(out.size(), 3u);
      JCLAP_CHECK(!out[2].on());
      JCLAP_CHECK_EQ(out[2].time, 1000);
      JCLAP_CHECK_EQ(out[2].m.data1, 60);
      Frame f;
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.rows[0].key, 64);
      JCLAP_CHECK_EQ(f.rows[1].key, -1);
   }
   {
      // Latch keeps the chord after letting go, until a new one is played.
      Engine e;
      Settings s;
      s.latch = true;
      e.setSettings(s);
      e.setPattern(pattern({{0, 0, 4}, {0, 1, 4}, {0, 2, 4}, {0, 3, 4}}));
      std::vector<In> in = chord({60});
      in.push_back({100, In::Off, 1, 60, 0});
      in.push_back({2 * kStepSamples - 500, In::On, 2, 62, 100});
      const auto on = ons(run(e, 4 * kStepSamples - 10, in));
      JCLAP_CHECK_EQ(on.size(), 5u);
      JCLAP_CHECK_EQ(on[1].m.data1, 60);
      // The new chord starts the pattern over (Restart Note).
      JCLAP_CHECK_EQ(on[2].m.data1, 62);
      JCLAP_CHECK_EQ(on[2].time, 2 * kStepSamples - 500);
      JCLAP_CHECK_EQ(on[3].time, 3 * kStepSamples - 500);
   }
   {
      // Expression goes to every note sounding from its finger.
      Engine e;
      Pattern p = pattern({{0, 0, 4}});
      p.lanes[0].octave = 1;
      e.setPattern(p);
      run(e, 1000, chord({60, 64}));
      int sounding = 0, key = -1;
      e.forEachSounding(1, 60, [&](int k) {
         ++sounding;
         key = k;
      });
      JCLAP_CHECK_EQ(sounding, 1);
      JCLAP_CHECK_EQ(key, 72);
   }

   // ---- the saved pattern ----
   {
      Pattern p = pattern({{0, 0, 64}, {3, 2, 1}, {7, 15, 4}});
      p.lanes[4] = {-2, 5, 1, -33};
      const std::vector<uint8_t> saved = p.save();
      Pattern q;
      JCLAP_CHECK(q.load(saved.data(), saved.size()));
      JCLAP_CHECK(q == p);
      JCLAP_CHECK(!q.load(saved.data(), 5));
      // A note can't overlap the next in its row.
      Pattern r = pattern({{0, 4, 4}});
      JCLAP_CHECK(r.add(0, 0, 64));
      JCLAP_CHECK_EQ(r.notes[1].length, 16);
      JCLAP_CHECK(!r.add(0, 2, 4));
   }

   return jclap::core::testResult();
}
