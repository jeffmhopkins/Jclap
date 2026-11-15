// JArp's engine on its own: rows mapped onto held notes, timing and swing, note lengths, ratchets,
// pressure, latch and the saved pattern.

#include <jclap/core/test_host.h>

#include <memory>
#include <vector>

#include "engine.h"

using namespace jarp;

// Engines are big (a bank of eight 128-step patterns), so each test keeps its own on the heap:
// dozens of them in one frame would overflow a 1 MB stack.

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

// Sets slot 1, with the pattern's length taken from the settings' old LENGTH where a test gives one.
void setPattern(Engine &e, Pattern p, int length = 8) {
   p.length = static_cast<uint8_t>(length);
   e.setPattern(0, p);
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
      // Octave: the same, an octave up each time round.
      s.octaves = true;
      int octave[kRows];
      mapRows(held, 3, s, key, target, octave);
      JCLAP_CHECK_EQ(key[3], 0);
      JCLAP_CHECK_EQ(octave[3], 1);
      JCLAP_CHECK_EQ(octave[7], 2);
      JCLAP_CHECK_EQ(octave[2], 0);
   }
   {
      // Octave rows play: rows 1 to 6 over a triad climb two octaves.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.wrap = s.octaves = true;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {3, 1, 4}, {5, 2, 4}}), 3);
      const auto on = ons(run(e, 3 * kStepSamples - 300, chord({48, 52, 55})));
      JCLAP_CHECK_EQ(on.size(), 3u);
      JCLAP_CHECK_EQ(on[0].m.data1, 48);
      JCLAP_CHECK_EQ(on[1].m.data1, 60);
      JCLAP_CHECK_EQ(on[2].m.data1, 67);
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

   {
      // Held Degree: only a degree that's held plays (in any octave), so a C and G held play the 1st
      // and 5th rows and leave the 3rd and 7th silent; the 8th is C an octave up, so the C plays it.
      Settings s;
      s.degree = s.strict = true;
      const int held[] = {48, 55};
      int key[kRows], target[kRows];
      mapRows(held, 2, s, key, target);
      JCLAP_CHECK_EQ(key[0], 0);
      JCLAP_CHECK_EQ(key[2], -1);
      JCLAP_CHECK_EQ(key[4], 1);
      JCLAP_CHECK_EQ(key[6], -1);
      JCLAP_CHECK_EQ(key[7], 0);
      JCLAP_CHECK_EQ(target[2], 52);
   }
   {
      // Spread: a pattern up to row 3 plays bottom, middle and top of any chord, 3 notes or 10.
      Settings s;
      s.spread = true;
      int key[kRows], target[kRows];
      const int three[] = {48, 52, 55};
      mapRows(three, 3, s, key, target, nullptr, 2);
      JCLAP_CHECK_EQ(key[0], 0);
      JCLAP_CHECK_EQ(key[1], 1);
      JCLAP_CHECK_EQ(key[2], 2);
      JCLAP_CHECK_EQ(key[3], -1);
      const int ten[] = {36, 40, 43, 48, 52, 55, 60, 64, 67, 72};
      mapRows(ten, 10, s, key, target, nullptr, 2);
      JCLAP_CHECK_EQ(key[0], 0);
      JCLAP_CHECK_EQ(key[1], 5);
      JCLAP_CHECK_EQ(key[2], 9);
      // Fewer notes than rows: rows share them, and one held note plays every row.
      const int two[] = {48, 60};
      mapRows(two, 2, s, key, target, nullptr, 4);
      JCLAP_CHECK_EQ(key[0], 0);
      JCLAP_CHECK_EQ(key[1], 0);
      JCLAP_CHECK_EQ(key[3], 1);
      JCLAP_CHECK_EQ(key[4], 1);
      mapRows(two, 1, s, key, target, nullptr, 4);
      JCLAP_CHECK_EQ(key[2], 0);
      JCLAP_CHECK_EQ(key[4], 0);

      // Playing: rows 1 to 3 on one step with two notes held play each held note once.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {1, 0, 4}, {2, 0, 4}}), 2);
      const auto on = ons(run(e, kStepSamples - 100, chord({60, 67})));
      JCLAP_CHECK_EQ(static_cast<int>(on.size()), 2);
   }

   {
      // Range: an octave shift may take a note at most `range` octaves above the lowest note held;
      // past that it drops back by octaves. A held note itself is never moved.
      JCLAP_CHECK_EQ(foldOctaves(48, 7, 48, 2), 72);
      JCLAP_CHECK_EQ(foldOctaves(55, 3, 48, 2), 67);
      JCLAP_CHECK_EQ(foldOctaves(55, 1, 48, 2), 67);
      JCLAP_CHECK_EQ(foldOctaves(52, 2, 48, 2), 64);
      JCLAP_CHECK_EQ(foldOctaves(84, 0, 48, 1), 84);
      JCLAP_CHECK_EQ(foldOctaves(60, -5, 48, 1), 36);
      JCLAP_CHECK_EQ(foldOctaves(48, 4, 48, 4), 96);

      // Playing: OCT rows with one note held climb an octave a row, and fold back at the range.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.wrap = s.octaves = true;
      s.range = 2;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {1, 1, 4}, {2, 2, 4}, {7, 3, 4}}), 4);
      const auto on = ons(run(e, 4 * kStepSamples - 300, chord({48})));
      JCLAP_CHECK_EQ(static_cast<int>(on.size()), 4);
      if (on.size() == 4) {
         JCLAP_CHECK_EQ(on[0].m.data1, 48);
         JCLAP_CHECK_EQ(on[1].m.data1, 60);
         JCLAP_CHECK_EQ(on[2].m.data1, 72);
         JCLAP_CHECK_EQ(on[3].m.data1, 72);
      }
   }

   // ---- timing ----
   {
      // A 7th chord, rows 1 and 3 alternating: C on step 1, G on step 2, each on its own finger's
      // channel and key, at the finger's velocity.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.pressVelocity = 0.0;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {2, 1, 4}, {0, 2, 4}, {2, 3, 4}}));
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
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 1}, {1, 1, 4}}), 2);
      const auto out = run(e, 4 * kStepSamples - 10, chord({60, 64}));
      const auto on = ons(out);
      JCLAP_CHECK_EQ(on.size(), 4u);
      JCLAP_CHECK_EQ(on[2].time, 2 * kStepSamples);
      JCLAP_CHECK_EQ(out[1].on(), false);
      JCLAP_CHECK_EQ(out[1].time, kStepSamples / 4);
   }
   {
      // A held bass for the whole pattern sounds once, under the rest.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 16}, {1, 0, 2}, {1, 1, 2}, {1, 2, 2}, {1, 3, 2}}), 4);
      const auto out = run(e, 4 * kStepSamples - 10, chord({48, 60}));
      int bass = 0;
      for (const Timed &t : ons(out))
         bass += t.m.data1 == 48;
      JCLAP_CHECK_EQ(bass, 1);
      JCLAP_CHECK_EQ(ons(out).size(), 5u);
   }
   {
      // Swing at 75% puts the second of each pair of steps three quarters of the way through it.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.swing = 0.75;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {0, 1, 4}, {0, 2, 4}}));
      const auto on = ons(run(e, 3 * kStepSamples, chord({60})));
      JCLAP_CHECK_EQ(on.size(), 3u);
      JCLAP_CHECK_EQ(on[1].time, kStepSamples * 3 / 2);
      JCLAP_CHECK_EQ(on[2].time, 2 * kStepSamples);
   }
   {
      // Back-to-back notes on one key: the first ends before the second starts, at the same time.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      setPattern(e, pattern({{0, 0, 4}, {0, 1, 4}}));
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
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      setPattern(e, pattern({{0, 0, 4}, {0, 1, 4}}));
      const auto on = ons(run(e, 2 * kStepSamples, chord({60}), false));
      JCLAP_CHECK_EQ(on.size(), 2u);
      JCLAP_CHECK_EQ(on[1].time, kStepSamples);
   }
   {
      // Restart Song follows the song position: a chord pressed part way through step 2 waits for
      // step 3.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.restart = Restart::Song;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {0, 1, 4}, {0, 2, 4}, {0, 3, 4}}));
      const auto on = ons(run(e, 3 * kStepSamples, chord({60}, kStepSamples + 1000)));
      JCLAP_CHECK_EQ(on.size(), 1u);
      JCLAP_CHECK_EQ(on[0].time, 2 * kStepSamples);
   }

   // ---- lanes ----
   {
      // Octave, ratchet and velocity, per step.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.pressVelocity = 0.0;
      e.setSettings(s);
      Pattern p = pattern({{0, 0, 4}, {0, 1, 4}});
      p.lanes[0].octave = 1;
      p.lanes[0].velocity = -20;
      p.lanes[1].ratchet = 3;
      setPattern(e, p);
      const auto on = ons(run(e, 2 * kStepSamples - 10, chord({60})));
      JCLAP_CHECK_EQ(on.size(), 4u);
      JCLAP_CHECK_EQ(on[0].m.data1, 72);
      JCLAP_CHECK_EQ(on[0].m.data2, 80);
      JCLAP_CHECK_EQ(on[1].time, kStepSamples);
      JCLAP_CHECK_EQ(on[2].time, kStepSamples + kStepSamples / 3);
      JCLAP_CHECK_EQ(on[3].time, kStepSamples + 2 * kStepSamples / 3);
   }
   {
      // A note's own lane values override its step's, for that note only: two notes on one step,
      // the upper with its own octave, ratchet and velocity, the lower on the step's.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.pressVelocity = 0.0;
      e.setSettings(s);
      Pattern p = pattern({{0, 0, 4}, {1, 0, 4}});
      p.lanes[0].velocity = -20;
      Note &n = p.notes[p.noteStarting(1, 0)];
      n.own = kOwnOctave | kOwnRatchet | kOwnVelocity;
      n.lane = {1, 2, 0, 10};
      setPattern(e, p);
      const auto on = ons(run(e, kStepSamples - 10, chord({60, 64})));
      JCLAP_CHECK_EQ(on.size(), 3u);
      int low = 0, high = 0;
      for (const auto &o : on) {
         if (o.m.data1 == 60) {
            ++low;
            JCLAP_CHECK_EQ(o.m.data2, 80);
         }
         else {
            ++high;
            JCLAP_CHECK_EQ(o.m.data1, 76);
            JCLAP_CHECK_EQ(o.m.data2, 110);
         }
      }
      JCLAP_CHECK_EQ(low, 1);
      JCLAP_CHECK_EQ(high, 2);
      // Only the bits it owns count: its step's octave still applies to what it doesn't own.
      Note plain = n;
      plain.own = kOwnVelocity;
      const Lane l = plain.over(Lane{-1, 3, 1, 0});
      JCLAP_CHECK_EQ(l.octave, -1);
      JCLAP_CHECK_EQ(l.ratchet, 3);
      JCLAP_CHECK_EQ(l.velocity, 10);
   }
   {
      // Pressure: a P step adds hits up to Max Hits as the finger presses, and velocity follows
      // pressure by Pressure to Velocity.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.pressRatchet = 1.0;
      s.maxHits = 4;
      s.pressVelocity = 1.0;
      e.setSettings(s);
      Pattern p = pattern({{0, 0, 4}});
      p.lanes[0].pressure = 1;
      p.lanes[0].velocity = 10;
      setPattern(e, p);
      std::vector<In> in = {{0, In::Pressure, 1, -1, 0}, {0, In::On, 1, 60, 100}};
      auto on = ons(run(e, kStepSamples - 10, in));
      JCLAP_CHECK_EQ(on.size(), 1u);
      JCLAP_CHECK_EQ(on[0].m.data2, 10);

      auto hardOwner = std::make_unique<Engine>();
      Engine &hard = *hardOwner;
      hard.setSettings(s);
      setPattern(hard, p);
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
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      setPattern(e, pattern({{0, 0, 16}, {1, 0, 16}}));
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
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.latch = true;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {0, 1, 4}, {0, 2, 4}, {0, 3, 4}}));
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
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Pattern p = pattern({{0, 0, 4}});
      p.lanes[0].octave = 1;
      setPattern(e, p);
      run(e, 1000, chord({60, 64}));
      int sounding = 0, key = -1;
      e.forEachSounding(1, 60, [&](int k) {
         ++sounding;
         key = k;
      });
      JCLAP_CHECK_EQ(sounding, 1);
      JCLAP_CHECK_EQ(key, 72);
   }

   // ---- chains ----
   {
      // Advance Chord: each new chord, after letting go of every note, plays the next pattern with
      // notes in it, skipping empty ones and going round. The first chord plays the one selected.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.advance = Advance::Chord;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}}), 2);
      Pattern b = pattern({{1, 0, 4}});
      b.length = 2;
      e.setPattern(1, b);
      Pattern d = pattern({{2, 0, 4}});
      d.length = 2;
      e.setPattern(3, d);
      std::vector<In> in;
      for (int k = 0; k < 4; ++k) {
         const int64_t at = k * 2 * kStepSamples;
         for (int key : {60, 64, 67})
            in.push_back({at, In::On, 1, key, 100});
         for (int key : {60, 64, 67})
            in.push_back({at + kStepSamples, In::Off, 1, key, 0});
      }
      const auto on = ons(run(e, 8 * kStepSamples, in));
      JCLAP_CHECK_EQ(on.size(), 4u);
      JCLAP_CHECK_EQ(on[0].m.data1, 60); // A: row 1
      JCLAP_CHECK_EQ(on[1].m.data1, 64); // B: row 2
      JCLAP_CHECK_EQ(on[2].m.data1, 67); // D: row 3 (C is empty)
      JCLAP_CHECK_EQ(on[3].m.data1, 60); // round to A
   }
   {
      // Advance Chord: a slot picked between chords is the one the next chord plays.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.advance = Advance::Chord;
      e.setSettings(s);
      for (int k = 0; k < 3; ++k) {
         Pattern p = pattern({{static_cast<uint8_t>(k), 0, 4}});
         p.length = 2;
         e.setPattern(k, p);
      }
      std::vector<In> first = chord({60, 64, 67});
      for (int key : {60, 64, 67})
         first.push_back({kStepSamples, In::Off, 0, key, 0});
      for (In &i : first)
         if (i.kind == In::Off)
            i.channel = 1 + (i.key == 64) + 2 * (i.key == 67);
      run(e, 2 * kStepSamples, first);
      s.slot = 2;
      e.setSettings(s);
      const auto on = ons(run(e, kStepSamples, chord({60, 64, 67}), true, 1.0));
      JCLAP_CHECK_EQ(on.size(), 1u);
      if (!on.empty())
         JCLAP_CHECK_EQ(on[0].m.data1, 67);
   }
   {
      // Advance Loop: a new chord plays the chain from the slot selected, not where it stopped.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.advance = Advance::Loop;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}}), 4);
      Pattern b = pattern({{1, 0, 4}});
      b.length = 4;
      e.setPattern(1, b);
      std::vector<In> in = chord({60, 64});
      in.push_back({6 * kStepSamples, In::Off, 1, 60, 0});
      in.push_back({6 * kStepSamples, In::Off, 2, 64, 0});
      in.push_back({7 * kStepSamples, In::On, 1, 60, 100});
      in.push_back({7 * kStepSamples, In::On, 2, 64, 100});
      const auto on = ons(run(e, 8 * kStepSamples - 300, in));
      JCLAP_CHECK_EQ(on.size(), 3u);
      if (on.size() == 3) {
         JCLAP_CHECK_EQ(on[1].m.data1, 64); // the chain moved on to B
         JCLAP_CHECK_EQ(on[2].m.data1, 60); // and starts again from A
      }
   }
   {
      // A tempo ramp isn't a jump: with the tempo rising 0.1 BPM a block for a second, a note on
      // every step plays once per step, not retriggered every block.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      e.setSettings(Settings{});
      Pattern p;
      for (int k = 0; k < 8; ++k)
         p.add(0, k, 4);
      setPattern(e, p, 8);
      OutBuffer out;
      double beats = 0.0, tempo = 120.0;
      int count = 0;
      for (int at = 0; at < 48000; at += kBlock) {
         Block bl;
         bl.frames = kBlock;
         bl.sampleRate = 48000.0;
         bl.hasTransport = bl.playing = true;
         bl.tempo = tempo;
         bl.beats = beats;
         out.clear();
         e.beginBlock(bl, out);
         if (at == 0)
            e.noteOn(0, 1, 60, 100, out);
         e.advance(kBlock, out);
         for (size_t k = 0; k < out.count; ++k)
            count += (out.items[k].status & 0xF0) == 0x90;
         const double next = tempo + 0.1;
         beats += kBlock * (tempo + next) / 2.0 / 60.0 / 48000.0;
         tempo = next;
      }
      JCLAP_CHECK(count >= 8 && count <= 11);
   }
   {
      // Advance Loop: each time a pattern ends the next starts, at its own length.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.advance = Advance::Loop;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {0, 1, 4}}), 2);
      Pattern b = pattern({{1, 0, 4}, {1, 1, 4}, {1, 2, 4}});
      b.length = 3;
      e.setPattern(1, b);
      const auto on = ons(run(e, 7 * kStepSamples - 300, chord({60, 64})));
      JCLAP_CHECK_EQ(on.size(), 7u);
      JCLAP_CHECK_EQ(on[1].m.data1, 60);
      JCLAP_CHECK_EQ(on[2].m.data1, 64);
      JCLAP_CHECK_EQ(on[2].time, 2 * kStepSamples);
      JCLAP_CHECK_EQ(on[4].m.data1, 64);
      JCLAP_CHECK_EQ(on[5].m.data1, 60);
      JCLAP_CHECK_EQ(on[5].time, 5 * kStepSamples);
      Frame f;
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.slot, 0);
   }
   {
      // Restart Song with Advance Loop: the chain is sequenced along the song. A (2 steps) then B
      // (3 steps) make a 5-step cycle, so a chord pressed during step 6 starts at step 7: B's step 1.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.advance = Advance::Loop;
      s.restart = Restart::Song;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {0, 1, 4}}), 2);
      Pattern b = pattern({{1, 0, 4}, {1, 1, 4}, {1, 2, 4}});
      b.length = 3;
      e.setPattern(1, b);
      const auto on = ons(run(e, 11 * kStepSamples - 300, chord({60, 64}, 6 * kStepSamples + 1000)));
      JCLAP_CHECK_EQ(on.size(), 4u);
      JCLAP_CHECK_EQ(on[0].time, 7 * kStepSamples);
      JCLAP_CHECK_EQ(on[0].m.data1, 64); // B
      JCLAP_CHECK_EQ(on[2].m.data1, 64); // B's last step
      JCLAP_CHECK_EQ(on[3].m.data1, 60); // A again at step 10
      JCLAP_CHECK_EQ(on[3].time, 10 * kStepSamples);
      Frame f;
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.slot, 0);
   }
   {
      // Restart Song with Advance Chord: the next chord's pattern picks up where the song is in it.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.advance = Advance::Chord;
      s.restart = Restart::Song;
      e.setSettings(s);
      setPattern(e, pattern({{0, 0, 4}, {0, 1, 4}, {0, 2, 4}, {0, 3, 4}}), 4);
      Pattern b = pattern({{1, 0, 4}, {1, 1, 4}, {1, 2, 4}, {2, 3, 4}});
      b.length = 4;
      e.setPattern(1, b);
      std::vector<In> in = chord({60, 64, 67});
      int channel = 1;
      for (int key : {60, 64, 67})
         in.push_back({2 * kStepSamples - 100, In::Off, channel++, key, 0});
      for (const In &n : chord({60, 64, 67}, 6 * kStepSamples + 1000))
         in.push_back(n);
      const auto on = ons(run(e, 8 * kStepSamples - 300, in));
      JCLAP_CHECK_EQ(on.size(), 3u);
      JCLAP_CHECK_EQ(on[2].time, 7 * kStepSamples);
      JCLAP_CHECK_EQ(on[2].m.data1, 67); // B's step 4 (7 mod 4 = 3), row 3
   }
   {
      // Advance Off: the selected pattern plays, and picking another switches at the next step.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      setPattern(e, pattern({{0, 0, 4}, {0, 1, 4}}), 2);
      Pattern b = pattern({{1, 0, 4}, {1, 1, 4}});
      b.length = 2;
      e.setPattern(1, b);
      auto on = ons(run(e, 2 * kStepSamples - 10, chord({60, 64})));
      JCLAP_CHECK_EQ(on.size(), 2u);
      JCLAP_CHECK_EQ(on[1].m.data1, 60);
      Settings s;
      s.slot = 1;
      e.setSettings(s);
      Frame f;
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.slot, 1);
   }
   {
      // The bank saves and loads every slot, each with its length; a version 1 pattern keeps the
      // length it is given.
      Bank bank;
      bank.slots[0] = pattern({{0, 0, 4}});
      bank.slots[5] = pattern({{3, 2, 8}});
      bank.slots[5].length = 12;
      const std::vector<uint8_t> saved = bank.save();
      Bank back;
      JCLAP_CHECK(back.load(saved.data(), saved.size()));
      JCLAP_CHECK(back == bank);
      JCLAP_CHECK_EQ(back.next(0), 5);
      JCLAP_CHECK_EQ(back.next(5), 0);
      std::vector<uint8_t> v1 = {1, 0};
      for (int k = 0; k < 16; ++k) // version 1 had 16 steps
         v1.insert(v1.end(), {0, 1, 0, 0});
      Pattern old;
      old.length = 5;
      JCLAP_CHECK(old.load(v1.data(), v1.size()));
      JCLAP_CHECK_EQ(old.length, 5);
      JCLAP_CHECK_EQ(old.count, 0);
   }

   // ---- long patterns ----
   {
      // Up to 128 steps: a 40-step pattern plays its last step, then loops.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Pattern p = pattern({{0, 0, 4}, {1, 39, 4}});
      setPattern(e, p, 40);
      const auto on = ons(run(e, 41 * kStepSamples - 300, chord({60, 64})));
      JCLAP_CHECK_EQ(on.size(), 3u);
      JCLAP_CHECK_EQ(on[1].m.data1, 64);
      JCLAP_CHECK_EQ(on[1].time, 39 * kStepSamples);
      JCLAP_CHECK_EQ(on[2].time, 40 * kStepSamples);
   }
   {
      // A long note (more than 255 quarters) and late lanes save and load (version 3); a version 2
      // pattern, from before, still loads.
      Pattern p = pattern({{0, 0, 4 * 100}, {2, 127, 4}});
      p.length = 128;
      p.lanes[120] = {1, 3, 1, 20};
      const std::vector<uint8_t> saved = p.save();
      Pattern q;
      JCLAP_CHECK(q.load(saved.data(), saved.size()));
      JCLAP_CHECK(q == p);
      JCLAP_CHECK_EQ(q.notes[q.noteStarting(0, 0)].length, 400);
      std::vector<uint8_t> v2 = {2, 12, 1, 3, 4, 8};
      for (int k = 0; k < 16; ++k)
         v2.insert(v2.end(), {0, static_cast<uint8_t>(k == 4 ? 2 : 1), 0, 0});
      Pattern r;
      JCLAP_CHECK(r.load(v2.data(), v2.size()));
      JCLAP_CHECK_EQ(r.length, 12);
      JCLAP_CHECK_EQ(r.count, 1);
      JCLAP_CHECK_EQ(r.notes[0].step, 4);
      JCLAP_CHECK_EQ(r.lanes[4].ratchet, 2);
   }
   {
      // Restart Song with a long Loop chain: 32 + 32 steps, entered at step 40, plays the second
      // pattern's step 9.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      Settings s;
      s.advance = Advance::Loop;
      s.restart = Restart::Song;
      e.setSettings(s);
      Pattern a = pattern({{0, 8, 4}});
      Pattern b = pattern({{1, 8, 4}});
      setPattern(e, a, 32);
      b.length = 32;
      e.setPattern(1, b);
      const auto on = ons(run(e, 41 * kStepSamples - 300, chord({60, 64}, 39 * kStepSamples + 500)));
      JCLAP_CHECK_EQ(on.size(), 1u);
      JCLAP_CHECK_EQ(on[0].m.data1, 64);
      JCLAP_CHECK_EQ(on[0].time, 40 * kStepSamples);
   }

   {
      // Chance: a step at 0% never plays, at 100% always, and at 50% about half the times round. A
      // note's own chance overrides its step's.
      auto eOwner = std::make_unique<Engine>();
      Engine &e = *eOwner;
      e.setSettings(Settings{});
      Pattern p = pattern({{0, 0, 4}, {0, 1, 4}, {0, 2, 4}, {1, 2, 4}});
      p.lanes[0].chance = 0;
      p.lanes[2].chance = 50;
      Note &n = p.notes[p.noteStarting(1, 2)];
      n.own = kOwnChance;
      n.lane.chance = 100;
      setPattern(e, p, 4);
      const auto on = ons(run(e, 400 * kStepSamples - 300, chord({60, 64})));
      int first = 0, second = 0, third = 0, upper = 0;
      for (const auto &o : on) {
         const int64_t step = (o.time + kStepSamples / 2) / kStepSamples % 4;
         if (o.m.data1 == 64)
            ++upper;
         else if (step == 0)
            ++first;
         else if (step == 1)
            ++second;
         else if (step == 2)
            ++third;
      }
      JCLAP_CHECK_EQ(first, 0);
      JCLAP_CHECK_EQ(second, 100);
      JCLAP_CHECK_EQ(upper, 100);
      JCLAP_CHECK(third > 30 && third < 70);
   }

   // ---- moving notes ----
   {
      // One note moves with its length and its own values.
      Pattern p = pattern({{0, 0, 8}, {2, 3, 4}});
      p.notes[0].own = kOwnChance;
      p.notes[0].lane.chance = 40;
      JCLAP_CHECK(p.move({0}, 1, 2));
      JCLAP_CHECK_EQ(p.count, 2);
      const int i = p.noteStarting(1, 2);
      JCLAP_CHECK(i >= 0);
      JCLAP_CHECK_EQ(p.notes[i].length, 8);
      JCLAP_CHECK_EQ(p.notes[i].own, kOwnChance);
      JCLAP_CHECK_EQ(p.laneOf(i).chance, 40);
      JCLAP_CHECK_EQ(p.noteStarting(0, 0), -1);

      // Several move together, keeping their shape; one moving onto where another of them was is
      // fine.
      Pattern q = pattern({{0, 0, 4}, {0, 1, 4}, {1, 2, 4}});
      JCLAP_CHECK(q.move({0, 1, 2}, 0, 1));
      JCLAP_CHECK(q.noteStarting(0, 1) >= 0 && q.noteStarting(0, 2) >= 0 && q.noteStarting(1, 3) >= 0);
      JCLAP_CHECK_EQ(q.noteStarting(0, 0), -1);
      JCLAP_CHECK_EQ(q.notes[q.noteStarting(0, 1)].length, 4);

      // Blocked, leaving the pattern as it was: off the grid, past the pattern's length, or onto a
      // note that stays.
      const Pattern before = q;
      JCLAP_CHECK(!q.move({q.noteStarting(1, 3)}, 7, 0));
      JCLAP_CHECK(!q.move({q.noteStarting(0, 1)}, 0, -2));
      JCLAP_CHECK(!q.move({q.noteStarting(0, 1)}, 0, 7));
      JCLAP_CHECK(!q.move({q.noteStarting(1, 3)}, -1, -1));
      JCLAP_CHECK(q == before);

      // A long note moved in front of another is shortened to fit.
      Pattern r = pattern({{0, 0, 16}, {1, 4, 4}});
      JCLAP_CHECK(r.move({r.noteStarting(0, 0)}, 1, 0));
      JCLAP_CHECK_EQ(r.notes[r.noteStarting(1, 0)].length, 16);
      Pattern t = pattern({{0, 0, 16}, {1, 3, 4}});
      JCLAP_CHECK(t.move({t.noteStarting(0, 0)}, 1, 0));
      JCLAP_CHECK_EQ(t.notes[t.noteStarting(1, 0)].length, 12);
   }

   // ---- the saved pattern ----
   {
      // Version 5 saves chance, for steps and a note's own; a version 4 pattern loads at 100%.
      Pattern c = pattern({{0, 0, 4}, {1, 1, 4}});
      c.lanes[1].chance = 35;
      c.notes[0].own = kOwnChance;
      c.notes[0].lane.chance = 60;
      const std::vector<uint8_t> five = c.save();
      Pattern d;
      JCLAP_CHECK(d.load(five.data(), five.size()));
      JCLAP_CHECK(d == c);
      JCLAP_CHECK_EQ(d.lanes[1].chance, 35);
      JCLAP_CHECK_EQ(d.laneOf(d.noteStarting(0, 0)).chance, 60);
      const std::vector<uint8_t> v4 = {4, 8, 1, 0, 0, 0, 4, 0, 8, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0};
      Pattern f;
      JCLAP_CHECK(f.load(v4.data(), v4.size()));
      JCLAP_CHECK_EQ(f.count, 1);
      JCLAP_CHECK_EQ(f.notes[0].own, kOwnVelocity);
      JCLAP_CHECK_EQ(f.notes[0].lane.chance, 100);
      JCLAP_CHECK_EQ(f.lanes[0].chance, 100);
      JCLAP_CHECK_EQ(f.lanes[0].ratchet, 1);
   }
   {
      // A note's own lane saves and loads (version 4); a version 3 pattern, from before, still loads.
      Pattern p = pattern({{0, 0, 4}, {2, 3, 8}});
      Note &n = p.notes[p.noteStarting(2, 3)];
      n.own = kOwnPressure | kOwnVelocity;
      n.lane = {0, 1, 1, -30};
      const std::vector<uint8_t> saved = p.save();
      Pattern q;
      JCLAP_CHECK(q.load(saved.data(), saved.size()));
      JCLAP_CHECK(q == p);
      const Note &m = q.notes[q.noteStarting(2, 3)];
      JCLAP_CHECK_EQ(m.own, kOwnPressure | kOwnVelocity);
      JCLAP_CHECK_EQ(m.lane.velocity, -30);
      JCLAP_CHECK_EQ(q.notes[q.noteStarting(0, 0)].own, 0);
      JCLAP_CHECK(!q.load(saved.data(), saved.size() - 6));
      const std::vector<uint8_t> v3 = {3, 8, 1, 0, 1, 2, 4, 0, 3, 0, 1, 0, 0, 0, 1, 0, 0, 0, 2, 0, 5};
      Pattern r;
      JCLAP_CHECK(r.load(v3.data(), v3.size()));
      JCLAP_CHECK_EQ(r.count, 1);
      JCLAP_CHECK_EQ(r.notes[0].own, 0);
      JCLAP_CHECK_EQ(r.lanes[2].ratchet, 2);
      JCLAP_CHECK_EQ(r.lanes[2].velocity, 5);
   }
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
