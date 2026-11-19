// JDrummer's library, generator and engine on their own: every groove well formed, songs made the
// same from the same seed, sparse sections, mutations, and the engine's timing, sections, notes and
// dice.

#include <jclap/core/test_host.h>

#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine.h"
#include "recipe.h"

using namespace jdrummer;

namespace {

constexpr uint32_t kBlock = 256;
// At 120 BPM and 48 kHz a beat is 24000 samples, a 1/16 step 6000.
constexpr int kStep = 6000;

struct Timed {
   int64_t time;
   MidiOut m;
   bool on() const { return (m.status & 0xF0) == 0x90; }
   int pad() const { return m.data1 - kFirstNote; }
};

// Runs the engine for `samples`, following a playing host from beat `from` (or free when `host` is
// false), with note-ons `notes` played at their sample times.
std::vector<Timed> run(Engine &e, int64_t samples, bool host = true, double from = 0.0,
                       std::vector<std::pair<int64_t, int>> notes = {}) {
   std::vector<Timed> all;
   auto out = std::make_unique<OutBuffer>();
   for (int64_t at = 0; at < samples; at += kBlock) {
      for (const auto &[t, key] : notes)
         if (t >= at && t < at + kBlock) {
            e.noteOn(0, key, 100);
            e.noteOff(0, key);
         }
      Block b;
      b.frames = kBlock;
      b.sampleRate = 48000.0;
      b.tempo = 120.0;
      b.hasTransport = host;
      b.playing = host;
      b.beats = from + at * 2.0 / 48000.0;
      out->clear();
      e.process(b, *out);
      for (size_t i = 0; i < out->count; ++i)
         all.push_back({at + out->items[i].time, out->items[i]});
   }
   return all;
}

Song oneHitSong() {
   Song s;
   for (Section &sec : s.sections) {
      sec.vars[0].at(K, 0).vel = 100;
      sec.fill = sec.vars[0];
      sec.fill.at(SN, 12).vel = 100;
   }
   return s;
}

Settings plain() {
   Settings st;
   st.vary = 0.0;
   st.ghosts = 0.0;
   st.autoFillBars = 0;
   st.fillInto = false;
   return st;
}

std::unique_ptr<Engine> engineWith(const Song &song, const Settings &st) {
   auto e = std::make_unique<Engine>();
   e->setSettings(st);
   e->setSong(song);
   e->reset();
   return e;
}

int countOn(const std::vector<Timed> &v, int pad = -1) {
   int n = 0;
   for (const Timed &t : v)
      n += t.on() && (pad < 0 || t.pad() == pad);
   return n;
}

void testLibrary() {
   JCLAP_CHECK_EQ(genreCount(), 34);
   JCLAP_CHECK(styleCount() >= 200);
   std::set<std::string> names;
   for (const FactoryEntry &e : factoryEntries())
      JCLAP_CHECK(names.insert(e.name).second);
   for (int g = 0; g < genreCount(); ++g) {
      const Genre &genre = genreAt(g);
      JCLAP_CHECK(!genre.grooves.empty());
      JCLAP_CHECK(genre.styles.size() >= 4);
      JCLAP_CHECK(genre.styles.size() <= 16); // the Style param's range
      for (const Groove &gr : genre.grooves) {
         // Every lane of a groove the same length.
         const int n = laneLength(gr.lanes.front().hits);
         for (const Lane &l : gr.lanes) {
            JCLAP_CHECK_EQ(laneLength(l.hits), n);
            if (laneLength(l.hits) != n)
               std::fprintf(stderr, "  %s: lane for pad %d is %d steps, not %d\n", genre.name, l.pad,
                            laneLength(l.hits), n);
            for (const char *c = l.hits; *c; ++c)
               JCLAP_CHECK(std::strchr("Xxog.234?", *c) != nullptr);
         }
      }
      for (const Style &s : genre.styles)
         JCLAP_CHECK(s.groove >= 0 && s.groove < static_cast<int>(genre.grooves.size()));
   }
}

void testEveryStyleMakesASong() {
   for (const FactoryEntry &e : factoryEntries()) {
      Recipe r;
      r.genre = e.genre;
      r.style = e.style;
      r.seed = 7;
      auto song = std::make_unique<Song>();
      generateSong(r, *song);
      for (int s = 0; s < kSections; ++s) {
         const Section &sec = song->sections[static_cast<size_t>(s)];
         JCLAP_CHECK(!sec.vars[0].empty());
         JCLAP_CHECK(!sec.fill.empty());
      }
      // The chorus is busier than the intro.
      JCLAP_CHECK(song->sections[kChorus].vars[0].count() * 4 >= song->sections[kIntro].vars[0].count() * 3);
   }
}

void testSameSeedSameSong() {
   Recipe r;
   r.genre = 15;
   r.player = 1;
   r.percussionist = 1;
   r.seed = 1234;
   auto a = std::make_unique<Song>(), b = std::make_unique<Song>(), c = std::make_unique<Song>();
   generateSong(r, *a);
   generateSong(r, *b);
   JCLAP_CHECK(*a == *b);
   JCLAP_CHECK(!a->sections[kVerse].perc.empty());
   r.seed = 1235;
   generateSong(r, *c);
   JCLAP_CHECK(*a != *c);
   // A locked section survives a new roll.
   a->sections[kVerse].locked = 1;
   const Section verse = a->sections[kVerse];
   generateSong(r, *a);
   JCLAP_CHECK(a->sections[kVerse] == verse);
   JCLAP_CHECK(a->sections[kChorus] == c->sections[kChorus]);
}

void testSparse() {
   Recipe r;
   r.genre = 14; // Rock
   r.sections[kIntro] = 0.0;
   r.sections[kVerse] = 0.06;
   r.sections[kPre] = 0.15;
   auto song = std::make_unique<Song>();
   generateSong(r, *song);
   JCLAP_CHECK(song->sections[kIntro].vars[0].empty());
   const Pattern &verse = song->sections[kVerse].vars[0];
   JCLAP_CHECK(!verse.empty());
   JCLAP_CHECK(verse.count() <= 2);
   const Pattern &pre = song->sections[kPre].vars[0];
   for (int pad = 0; pad < kPads; ++pad)
      for (int s = 0; s < pre.steps; ++s)
         if (pre.at(pad, s).on())
            JCLAP_CHECK(s % 4 == 0 || pre.at(pad, s).vel >= kAccent * 0.8);
   JCLAP_CHECK(pre.count() < song->sections[kChorus].vars[0].count());
}

void testMutations() {
   Recipe r;
   r.genre = 0;
   auto song = std::make_unique<Song>();
   generateSong(r, *song);
   const Pattern &parent = song->sections[kChorus].vars[0];
   Rng rng(5);
   std::vector<int> used;
   std::set<std::string> tags;
   for (int i = 0; i < 5; ++i) {
      std::string tag;
      const Pattern child = mutate(parent, kDrumKit, 0.4, rng, tag, &used);
      JCLAP_CHECK(child != parent);
      tags.insert(tag.substr(0, tag.find(',')));
   }
   JCLAP_CHECK_EQ(tags.size(), 5u);
}

void testSongSaves() {
   Recipe r;
   r.genre = 7;
   r.percussionist = 3;
   auto a = std::make_unique<Song>(), b = std::make_unique<Song>();
   generateSong(r, *a);
   a->sections[2].locked = 1;
   a->sections[4].generation = 3;
   const std::vector<uint8_t> bytes = a->save();
   JCLAP_CHECK(b->load(bytes.data(), bytes.size()));
   JCLAP_CHECK(*a == *b);
   JCLAP_CHECK(!b->load(bytes.data(), bytes.size() / 2));
}

void testHostTiming() {
   auto e = engineWith(oneHitSong(), plain());
   const auto out = run(*e, 4 * 24000 * 2);
   // A kick on each bar's first step, two bars.
   JCLAP_CHECK_EQ(countOn(out, K), 2);
   for (const Timed &t : out)
      if (t.on())
         JCLAP_CHECK(std::abs(static_cast<int>(t.time % (4 * 24000))) <= 1);
}

void testSwingAndNudge() {
   Song s;
   for (Section &sec : s.sections) {
      sec.vars[0].at(CH, 1).vel = 90;
      sec.vars[0].at(SN, 4).vel = 90;
      sec.vars[0].at(SN, 4).nudge = 10;
   }
   Settings st = plain();
   st.swing = 0.75;
   auto e = engineWith(s, st);
   const auto out = run(*e, 24000 * 2);
   for (const Timed &t : out)
      if (t.on() && t.pad() == CH)
         JCLAP_CHECK(std::abs(t.time - (kStep + kStep / 2)) <= 2); // half a step late
      else if (t.on() && t.pad() == SN)
         JCLAP_CHECK(std::abs(t.time - (4 * kStep + 480)) <= 2); // 10 ms late
}

void testRatchetAndChance() {
   Song s;
   for (Section &sec : s.sections) {
      sec.vars[0].at(CH, 0).vel = 90;
      sec.vars[0].at(CH, 0).ratchet = 3;
      sec.vars[0].at(PC, 2).vel = 90;
      sec.vars[0].at(PC, 2).chance = 50;
   }
   Settings st = plain();
   st.sameDiceBars = 0;
   auto e = engineWith(s, st);
   const auto out = run(*e, 4 * 24000 * 32);
   JCLAP_CHECK_EQ(countOn(out, CH), 3 * 32);
   const int perc = countOn(out, PC);
   JCLAP_CHECK(perc > 6 && perc < 26);
}

void testSameDice() {
   Song s;
   for (Section &sec : s.sections)
      for (int i = 0; i < 16; ++i) {
         sec.vars[0].at(CH, i).vel = 90;
         sec.vars[0].at(CH, i).chance = 50;
      }
   Settings st = plain();
   st.sameDiceBars = 1;
   auto e = engineWith(s, st);
   const auto out = run(*e, 4 * 24000 * 4);
   // Every bar plays the same hits.
   std::vector<std::set<int64_t>> bars(4);
   for (const Timed &t : out)
      if (t.on())
         bars[static_cast<size_t>(t.time / 96000)].insert(t.time % 96000);
   JCLAP_CHECK(bars[0] == bars[1] && bars[1] == bars[2] && bars[2] == bars[3]);
   JCLAP_CHECK(!bars[0].empty() && bars[0].size() < 16);
}

void testSectionNoteSwitchesAtBar() {
   Song s;
   for (int i = 0; i < kSections; ++i)
      s.sections[static_cast<size_t>(i)].vars[0].at(i, 0).vel = 100; // section i plays pad i
   auto e = engineWith(s, plain());
   // Chorus (F0 = 17) played in bar 1: bar 2 on is the chorus.
   const auto out = run(*e, 96000 * 3, true, 0.0, {{30000, 17}});
   std::vector<int> pads;
   for (const Timed &t : out)
      if (t.on())
         pads.push_back(t.pad());
   JCLAP_CHECK(pads.size() == 3u);
   JCLAP_CHECK(pads.size() == 3u && pads[0] == kIntro && pads[1] == kChorus && pads[2] == kChorus);
   JCLAP_CHECK_EQ(e->section(), kChorus);
}

void testFillIntoSwitch() {
   auto e = engineWith(oneHitSong(), [] {
      Settings st = plain();
      st.fillInto = true;
      return st;
   }());
   // Verse (D0 = 14) asked for in bar 1: its end plays the fill's snare.
   const auto out = run(*e, 96000 * 2, true, 0.0, {{10000, 14}});
   JCLAP_CHECK_EQ(countOn(out, SN), 1);
}

void testAutoFillAndSongMode() {
   Song s = oneHitSong();
   for (Section &sec : s.sections)
      sec.bars = 2;
   Settings st = plain();
   st.song = true;
   st.fillInto = true;
   auto e = engineWith(s, st);
   run(*e, 96000 * 5);
   // Two bars each: bar 5 is the third section.
   JCLAP_CHECK_EQ(e->section(), kPre);
   st.song = false;
   st.fillInto = false;
   st.autoFillBars = 4;
   auto e2 = engineWith(oneHitSong(), st);
   const auto out = run(*e2, 96000 * 8);
   JCLAP_CHECK_EQ(countOn(out, SN), 2);
}

void testMutesAndStop() {
   Song s;
   for (Section &sec : s.sections) {
      sec.vars[0].at(K, 0).vel = 100;
      sec.vars[0].at(CH, 2).vel = 100;
   }
   auto e = engineWith(s, plain());
   // Mute Hats (C#1 = 25) held for the whole run.
   e->noteOn(0, 25, 100);
   auto out = run(*e, 96000);
   JCLAP_CHECK_EQ(countOn(out, CH), 0);
   JCLAP_CHECK_EQ(countOn(out, K), 1);
   e->noteOff(0, 25);
   // Stop (F1 = 29) silences until a section note.
   e->noteOn(0, 29, 100);
   out = run(*e, 96000, true, 4.0);
   JCLAP_CHECK_EQ(countOn(out), 0);
   e->noteOn(0, 12, 100);
   out = run(*e, 96000, true, 8.0);
   JCLAP_CHECK(countOn(out) > 0);
}

void testFreeRun() {
   Settings st = plain();
   st.play = true;
   st.freeTempo = 60.0;
   auto e = engineWith(oneHitSong(), st);
   const auto out = run(*e, 48000 * 8, false);
   // At 60 BPM a bar is four seconds: two kicks in eight.
   JCLAP_CHECK_EQ(countOn(out, K), 2);
   st.play = false;
   e->setSettings(st);
   JCLAP_CHECK_EQ(countOn(run(*e, 48000 * 8, false)), 0);
}

void testCustomNotes() {
   Settings st = plain();
   st.noteMap = 1;
   for (int a = 0; a < kActionCount; ++a)
      st.learn[static_cast<size_t>(a)] = 60 + a;
   auto e = engineWith(oneHitSong(), st);
   JCLAP_CHECK(!e->noteOn(0, 12, 100)); // the keyboard map's intro isn't one now
   JCLAP_CHECK(e->noteOn(0, 63, 100));  // Chorus
   JCLAP_CHECK_EQ(e->section(), kChorus);
   e->learn(true);
   JCLAP_CHECK(e->noteOn(3, 70, 90));
   JCLAP_CHECK_EQ(e->learned(), 70);
}

void testEveryNoteHasAnOff() {
   Recipe r;
   r.genre = 11; // Trap: rolls
   r.percussionist = 2;
   auto song = std::make_unique<Song>();
   generateSong(r, *song);
   Settings st;
   st.ghosts = 0.5;
   st.vary = 0.5;
   auto e = engineWith(*song, st);
   const auto out = run(*e, 96000 * 6);
   int balance[16][128] = {};
   for (const Timed &t : out) {
      const int ch = t.m.status & 15;
      balance[ch][t.m.data1] += t.on() ? 1 : -1;
   }
   const auto tail = run(*e, 96000, false); // the host stops: what's sounding stops
   for (const Timed &t : tail)
      balance[t.m.status & 15][t.m.data1] += t.on() ? 1 : -1;
   for (auto &ch : balance)
      for (int v : ch)
         JCLAP_CHECK_EQ(v, 0);
   // In time order.
   for (size_t i = 1; i < out.size(); ++i)
      JCLAP_CHECK(out[i].time >= out[i - 1].time);
}

} // namespace

int main() {
   testLibrary();
   testEveryStyleMakesASong();
   testSameSeedSameSong();
   testSparse();
   testMutations();
   testSongSaves();
   testHostTiming();
   testSwingAndNudge();
   testRatchetAndChance();
   testSameDice();
   testSectionNoteSwitchesAtBar();
   testFillIntoSwitch();
   testAutoFillAndSongMode();
   testMutesAndStop();
   testFreeRun();
   testCustomNotes();
   testEveryNoteHasAnOff();
   return jclap::core::testResult();
}
