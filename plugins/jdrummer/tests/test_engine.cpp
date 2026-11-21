// JDrummer's library, generator and engine on their own: every groove well formed, songs made the
// same from the same seed, sparse sections, mutations, and the engine's timing, sections, notes and
// dice.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstdio>
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
            JCLAP_CHECK(s % 4 == 0 || pre.at(pad, s).vel >= kAccent * 0.65);
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
      sec.vars[0].at(RD, 2).vel = 90;
      sec.vars[0].at(RD, 2).chance = 50;
   }
   Settings st = plain();
   st.sameDiceBars = 0;
   auto e = engineWith(s, st);
   const auto out = run(*e, 4 * 24000 * 32);
   JCLAP_CHECK_EQ(countOn(out, CH), 3 * 32);
   const int perc = countOn(out, RD);
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

// Every twist a style names changes its groove, whatever the seed, and no two styles of a genre are
// the same groove, twists and swing.
void testStylesDiffer() {
   for (int gi = 0; gi < genreCount(); ++gi) {
      const Genre &g = genreAt(gi);
      for (const Style &st : g.styles) {
         const unsigned mods = st.mods & ~(kShuffle | kSwing8 | kStraight);
         if (!mods)
            continue;
         for (uint32_t seed = 1; seed <= 40; ++seed) {
            Rng rng(seed);
            const Pattern plainGroove = fromGroove(g.grooves[static_cast<size_t>(st.groove)]);
            Pattern p = plainGroove;
            applyMods(p, mods, g.kit, rng, g.tradition);
            JCLAP_CHECK(p != plainGroove);
            if (p == plainGroove) {
               std::fprintf(stderr, "  %s / %s: its twists did nothing (seed %u)\n", g.name, st.name, seed);
               break;
            }
         }
      }
      for (size_t a = 0; a < g.styles.size(); ++a)
         for (size_t b = a + 1; b < g.styles.size(); ++b) {
            const Style &x = g.styles[a], &y = g.styles[b];
            Rng r1(3), r2(3);
            Pattern p = fromGroove(g.grooves[static_cast<size_t>(x.groove)]);
            Pattern q = fromGroove(g.grooves[static_cast<size_t>(y.groove)]);
            applyMods(p, x.mods, g.kit, r1, g.tradition);
            applyMods(q, y.mods, g.kit, r2, g.tradition);
            const bool same = p == q && styleSwing(g, x) == styleSwing(g, y) && styleSwingFeel(x) == styleSwingFeel(y);
            JCLAP_CHECK(!same);
            if (same)
               std::fprintf(stderr, "  %s: %s and %s are the same\n", g.name, x.name, y.name);
         }
   }
}

int genreIndex(const char *name) {
   for (int i = 0; i < genreCount(); ++i)
      if (!std::strcmp(genreAt(i).name, name))
         return i;
   return -1;
}

std::vector<int> stepsOf(const Groove &g, int pad) {
   std::vector<int> v;
   for (const Lane &l : g.lanes)
      if (l.pad == pad)
         for (int s = 0; l.hits[s]; ++s)
            if (l.hits[s] != '.')
               v.push_back(s);
   return v;
}

// The hand percussion grooves against their references: the son and rumba claves, the conga tumbao,
// the Middle Eastern rhythms' doum and tek, the tabla's thekas and 12/8 in sixteenth triplets.
void testReferenceGrooves() {
   const Genre &cuba = genreAt(genreIndex("Afro-Cuban"));
   JCLAP_CHECK_EQ(cuba.kit, kAfroLatinKit);
   JCLAP_CHECK(stepsOf(cuba.grooves[0], CLAVE) == (std::vector<int>{0, 6, 12, 20, 24}));   // son 3-2
   JCLAP_CHECK(stepsOf(cuba.grooves[2], CLAVE) == (std::vector<int>{0, 6, 14, 20, 24}));   // rumba 3-2
   JCLAP_CHECK(stepsOf(cuba.grooves[6], CLAVE) == (std::vector<int>{4, 8, 16, 22, 28}));   // son 2-3
   JCLAP_CHECK(stepsOf(cuba.grooves[0], AL_CONGA_SLAP) == (std::vector<int>{4, 20}));      // the slap on 2
   const std::vector<int> open = stepsOf(cuba.grooves[0], CONGA);
   JCLAP_CHECK(std::find(open.begin(), open.end(), 12) != open.end() && std::find(open.begin(), open.end(), 14) != open.end());
   const Genre &me = genreAt(genreIndex("Middle Eastern"));
   JCLAP_CHECK(stepsOf(me.grooves[0], DARBUKA_DUM) == (std::vector<int>{0, 8}));            // maqsum
   const std::vector<int> tek = stepsOf(me.grooves[0], DARBUKA_TEK);
   for (int t : {2, 6, 12})
      JCLAP_CHECK(std::find(tek.begin(), tek.end(), t) != tek.end());
   JCLAP_CHECK(stepsOf(me.grooves[1], DARBUKA_DUM) == (std::vector<int>{0, 6, 8}));         // saidi
   for (const Groove &gr : me.grooves)
      JCLAP_CHECK(stepsOf(gr, SHAKER).empty()); // no shaker carpet under a darbuka
   const Genre &india = genreAt(genreIndex("Indian"));
   JCLAP_CHECK_EQ(laneLength(india.grooves[0].lanes[0].hits), 32); // teentaal: 16 matras
   JCLAP_CHECK_EQ(laneLength(india.grooves[2].lanes[0].hits), 12); // dadra: 6 matras
   // Teentaal's khali: the bayan (the frame drum) on matra 9's dha, then none in matras 10 to 13.
   for (int st : stepsOf(india.grooves[0], FRAME_DRUM))
      JCLAP_CHECK(st < 18 || st >= 26);
   JCLAP_CHECK(!stepsOf(india.grooves[0], FRAME_DRUM).empty() && stepsOf(india.grooves[0], FRAME_DRUM)[8] == 16);
   const Genre &wa = genreAt(genreIndex("West African"));
   JCLAP_CHECK_EQ(wa.kit, kAfroLatinKit);
   int twelve = 0;
   for (const Groove &gr : wa.grooves)
      if (gr.rate == kRate16T) {
         ++twelve;
         JCLAP_CHECK_EQ(laneLength(gr.lanes[0].hits), 24);
         // The standard bell: x.x.xx.x.x.x in eighths, two steps each.
         const std::vector<int> bell = stepsOf(gr, BELL);
         if (!bell.empty() && bell.size() == 7)
            JCLAP_CHECK(bell == (std::vector<int>{0, 4, 8, 10, 14, 18, 22}));
      }
   JCLAP_CHECK(twelve >= 2); // soli and dununba (lamban is 4/4)
   // A 12/8 groove makes a 12/8 song.
   Recipe r;
   r.genre = genreIndex("West African");
   r.style = 4; // Soli
   auto song = std::make_unique<Song>();
   generateSong(r, *song);
   JCLAP_CHECK_EQ(song->sections[kVerse].vars[0].rate, kRate16T);
   JCLAP_CHECK_EQ(song->sections[kVerse].fill.rate, kRate16T);
}

// A hand genre only ever reaches for its own tradition's drums: its grooves' pads, or pads of its
// tradition, even very weird.
void testHandTraditions() {
   for (int gi = 0; gi < genreCount(); ++gi) {
      const Genre &g = genreAt(gi);
      if (g.tradition == kTradAny)
         continue;
      std::set<int> own;
      for (const Groove &gr : g.grooves)
         for (const Lane &l : gr.lanes)
            own.insert(l.pad);
      for (int st = 0; st < static_cast<int>(g.styles.size()); ++st)
         for (uint32_t seed = 1; seed <= 4; ++seed) {
            Recipe r;
            r.genre = gi;
            r.style = st;
            r.seed = seed;
            r.weird = 0.9;
            auto song = std::make_unique<Song>();
            generateSong(r, *song);
            for (const Section &sec : song->sections)
               for (int v = 0; v <= kVariations; ++v) {
                  const Pattern &p = v < kVariations ? sec.vars[static_cast<size_t>(v)] : sec.fill;
                  for (int pad = 0; pad < kPads; ++pad)
                     if (p.uses(pad)) {
                        const bool ok = own.count(pad) || (padTradition(g.kit, pad) & g.tradition);
                        JCLAP_CHECK(ok);
                        if (!ok)
                           std::fprintf(stderr, "  %s / %s: %s is not of its tradition\n", g.name,
                                        g.styles[static_cast<size_t>(st)].name, padName(g.kit, pad));
                     }
               }
         }
   }
}

// A song builds: across the library the chorus plays clearly more than the verse, and louder.
void testEnergyArc() {
   double verse = 0, chorus = 0, verseVel = 0, chorusVel = 0;
   for (int gi = 0; gi < genreCount(); ++gi) {
      const Genre &g = genreAt(gi);
      double gv = 0, gc = 0;
      for (int st = 0; st < static_cast<int>(g.styles.size()) * 3; ++st) {
         Recipe r;
         r.genre = gi;
         r.style = st / 3;
         r.player = g.player;
         r.seed = static_cast<uint32_t>(11 + st % 3);
         auto song = std::make_unique<Song>();
         generateSong(r, *song);
         // The main groove (the variations each add or take a twist of their own).
         for (int v = 0; v < 1; ++v)
            for (int pad = 0; pad < kPads; ++pad)
               for (int s = 0; s < kMaxSteps; ++s) {
                  const Hit &a = song->sections[kVerse].vars[static_cast<size_t>(v)].at(pad, s);
                  const Hit &b = song->sections[kChorus].vars[static_cast<size_t>(v)].at(pad, s);
                  if (a.on() && s < song->sections[kVerse].vars[static_cast<size_t>(v)].steps) {
                     gv += 1;
                     verseVel += a.vel;
                  }
                  if (b.on() && s < song->sections[kChorus].vars[static_cast<size_t>(v)].steps) {
                     gc += 1;
                     chorusVel += b.vel;
                  }
               }
      }
      JCLAP_CHECK(gc >= gv);
      if (gc < gv)
         std::fprintf(stderr, "  %s: chorus %g hits, verse %g\n", g.name, gc, gv);
      verse += gv;
      chorus += gc;
   }
   JCLAP_CHECK(chorus >= verse * 1.2);
   JCLAP_CHECK(chorusVel / chorus > verseVel / verse * 1.1);
}

// The percussionist has variations and a fill of its own, on its own kit, in the drummer's meter.
void testPercussionist() {
   for (int who = 1; who <= kPercussionists; ++who) {
      Recipe r;
      r.genre = 0;
      r.percussionist = who;
      r.seed = 5;
      auto song = std::make_unique<Song>();
      generateSong(r, *song);
      const Section &sec = song->sections[kChorus];
      JCLAP_CHECK(!sec.perc.empty());
      JCLAP_CHECK(!sec.percFill.empty());
      JCLAP_CHECK(sec.percFill != sec.perc);
      int differ = 0;
      for (const Pattern &p : sec.percVars)
         differ += !p.empty() && p != sec.perc;
      JCLAP_CHECK(differ >= 2);
      const int kit = percussionistKit(who - 1);
      JCLAP_CHECK(kit == (who <= 2 ? kAfroLatinKit : kPercussionKit));
   }
   // A waltz gets a percussion part in three, a 12/8 one in 12/8.
   Recipe r;
   r.genre = genreIndex("Odd Meters");
   r.style = 2; // Waltz
   r.percussionist = 4;
   auto song = std::make_unique<Song>();
   generateSong(r, *song);
   JCLAP_CHECK_EQ(song->sections[kVerse].vars[0].steps, 12);
   JCLAP_CHECK_EQ(song->sections[kVerse].perc.steps, 12);
   r.genre = genreIndex("West African");
   r.style = 4; // Soli, 12/8
   r.percussionist = 2;
   generateSong(r, *song);
   JCLAP_CHECK_EQ(song->sections[kVerse].perc.rate, kRate16T);
   JCLAP_CHECK_EQ(song->sections[kVerse].perc.steps, 24);
}

// A song saved before the percussion had variations still loads, its variations the main part.
void testOldSongLoads() {
   Recipe r;
   r.percussionist = 1;
   auto a = std::make_unique<Song>(), b = std::make_unique<Song>();
   generateSong(r, *a);
   std::vector<uint8_t> v1;
   auto u8 = [&](uint8_t x) { v1.push_back(x); };
   auto u32 = [&](uint32_t x) {
      for (int i = 0; i < 4; ++i)
         v1.push_back(static_cast<uint8_t>(x >> (8 * i)));
   };
   auto pattern = [&](const Pattern &p) {
      u8(p.steps);
      u8(p.rate);
      for (int pad = 0; pad < kPads; ++pad)
         for (int s = 0; s < kMaxSteps; ++s) {
            const Hit &h = p.at(pad, s);
            u8(h.vel);
            u8(h.chance);
            u8(h.ratchet);
            u8(static_cast<uint8_t>(h.nudge));
         }
   };
   u32(1);
   u32(kSections);
   for (const Section &s : a->sections) {
      u8(s.bars);
      u8(s.energy);
      u8(s.crash);
      u8(s.locked);
      u32(s.seed);
      u32(s.generation);
      for (const Pattern &p : s.vars)
         pattern(p);
      pattern(s.fill);
      pattern(s.perc);
   }
   JCLAP_CHECK(b->load(v1.data(), v1.size()));
   for (int i = 0; i < kSections; ++i) {
      const Section &x = a->sections[static_cast<size_t>(i)], &y = b->sections[static_cast<size_t>(i)];
      JCLAP_CHECK(x.vars == y.vars && x.fill == y.fill && x.perc == y.perc);
      JCLAP_CHECK(y.percVars[0] == y.perc && y.percFill == y.perc);
      JCLAP_CHECK_EQ(y.meter, kTicksPerBar);
   }
   // Version 1 wrote every percussion part on the Percussion kit, the conguero's too.
   JCLAP_CHECK_EQ(a->percKit, kAfroLatinKit);
   JCLAP_CHECK_EQ(b->percKit, kPercussionKit);
   // And version 2 keeps the kit and the meter.
   a->sections[kVerse].meter = 168;
   auto c = std::make_unique<Song>();
   const std::vector<uint8_t> v2 = a->save();
   JCLAP_CHECK(c->load(v2.data(), v2.size()));
   JCLAP_CHECK(*c == *a);
}

// Swung in eighths: the off-beat eighth lands at SWING of the beat, the sixteenths either side move
// with it.
void testEighthSwing() {
   Song s;
   for (Section &sec : s.sections) {
      sec.vars[0].at(CH, 1).vel = 90;
      sec.vars[0].at(SN, 2).vel = 90;
      sec.vars[0].at(K, 3).vel = 90;
   }
   Settings st = plain();
   st.swing = 2.0 / 3.0;
   st.swingFeel = 1;
   auto e = engineWith(s, st);
   const auto out = run(*e, 24000 * 2);
   for (const Timed &t : out) {
      if (!t.on())
         continue;
      if (t.pad() == SN)
         JCLAP_CHECK(std::abs(t.time - 16000) <= 2); // the triplet's third
      else if (t.pad() == CH)
         JCLAP_CHECK(std::abs(t.time - 8000) <= 2); // halfway to it
      else if (t.pad() == K)
         JCLAP_CHECK(std::abs(t.time - 20000) <= 2); // halfway from it to the next beat
   }
   JCLAP_CHECK_EQ(countOn(out, SN), 1);
}

// A fill ends on a crash on the next bar's one.
void testCrashAfterFill() {
   Song s = oneHitSong();
   Settings st = plain();
   st.autoFillBars = 4;
   auto e = engineWith(s, st);
   const auto out = run(*e, 96000 * 8);
   int crashes = 0;
   for (const Timed &t : out)
      if (t.on() && t.pad() == CR) {
         ++crashes;
         JCLAP_CHECK(t.time % 96000 <= 2);
      }
   JCLAP_CHECK_EQ(crashes, 1); // after bar 4's fill (bar 8's is cut off)
   // Not on the hand kits.
   st.kit = kAfroLatinKit;
   auto e2 = engineWith(s, st);
   JCLAP_CHECK_EQ(countOn(run(*e2, 96000 * 8), CR), 0);
}

// The percussionist plays its fill with the drummer's.
void testPercussionFill() {
   Song s;
   for (Section &sec : s.sections) {
      sec.vars[0].at(K, 0).vel = 100;
      sec.fill = sec.vars[0];
      sec.perc.at(5, 0).vel = 100;
      sec.percFill.at(7, 0).vel = 100;
   }
   Settings st = plain();
   st.autoFillBars = 4;
   auto e = engineWith(s, st);
   const auto out = run(*e, 96000 * 4);
   int main = 0, fill = 0;
   for (const Timed &t : out)
      if (t.on() && (t.m.status & 15) == st.percChannel) {
         main += t.pad() == 5;
         fill += t.pad() == 7;
      }
   JCLAP_CHECK_EQ(main, 3);
   JCLAP_CHECK_EQ(fill, 1);
}

// A fill written at the end of a two-bar pattern plays its last bar whatever bar it falls in, and a
// fill cut off by a stop doesn't come back on the restart.
void testFillOnLongPattern() {
   Song s;
   for (Section &sec : s.sections) {
      sec.vars[0].steps = 32;
      sec.vars[0].at(K, 0).vel = 100;
      sec.fill = sec.vars[0];
      sec.fill.at(SN, 28).vel = 100; // bar 2's beat 4
   }
   Settings st = plain();
   st.autoFillBars = 1; // every bar, so the even bars too
   auto e = engineWith(s, st);
   JCLAP_CHECK_EQ(countOn(run(*e, 96000 * 4), SN), 4);
   // Stopped during a fill, then started again: the first bar is the groove.
   Song one = oneHitSong();
   st.autoFillBars = 2;
   auto e2 = engineWith(one, st);
   run(*e2, 96000 + 48000); // half way into bar 2's fill
   Block b;
   b.frames = kBlock;
   b.sampleRate = 48000.0;
   b.tempo = 120.0;
   b.hasTransport = true;
   b.playing = false;
   auto out = std::make_unique<OutBuffer>();
   e2->process(b, *out);
   JCLAP_CHECK_EQ(countOn(run(*e2, 96000, true, 40.0), SN), 0);
}

// A 7/8 section's bars are 7/8 long: the fill ends and the crash lands on its bar lines.
void testOddMeter() {
   Song s;
   for (Section &sec : s.sections) {
      sec.meter = 168;
      sec.vars[0].steps = 14;
      sec.vars[0].at(K, 0).vel = 100;
      sec.fill = sec.vars[0];
      sec.fill.at(SN, 12).vel = 100;
   }
   Settings st = plain();
   st.autoFillBars = 2;
   auto e = engineWith(s, st);
   const auto out = run(*e, 84000 * 4);
   const int64_t bar = 84000; // 3.5 beats at 120
   int crashes = 0, kicks = 0, snares = 0;
   for (const Timed &t : out) {
      if (!t.on())
         continue;
      if (t.pad() == CR) {
         ++crashes;
         JCLAP_CHECK(t.time % bar <= 2);
      }
      if (t.pad() == K) {
         ++kicks;
         JCLAP_CHECK(t.time % bar <= 2);
      }
      if (t.pad() == SN) {
         ++snares;
         JCLAP_CHECK(t.time % bar >= 72000 - 2 && t.time % bar <= 72000 + 2);
      }
   }
   JCLAP_CHECK_EQ(kicks, 5); // the fifth bar's one is in the last block
   JCLAP_CHECK_EQ(snares, 2);
   JCLAP_CHECK_EQ(crashes, 2); // out of each fill
   // The generator writes the meter: 7/8 for a 7/8 groove, 4/4 for a two-bar one.
   Recipe r;
   r.genre = genreIndex("Odd Meters");
   for (int i = 0; i < static_cast<int>(genreAt(r.genre).styles.size()); ++i) {
      r.style = i;
      const Section sec = makeSection(r, kVerse, 7);
      const int ticks = patternTicks(fromGroove(genreAt(r.genre).grooves[static_cast<size_t>(styleOf(r).groove)]));
      JCLAP_CHECK_EQ(static_cast<int>(sec.meter), ticks % kTicksPerBar == 0 ? kTicksPerBar : ticks);
   }
   r.genre = genreIndex("Afro-Cuban");
   r.style = 0;
   JCLAP_CHECK_EQ(static_cast<int>(makeSection(r, kVerse, 7).meter), kTicksPerBar);
}

// The fixes the generator review asked for, each as it was found.
void testGeneratorEdges() {
   // A shaker player's voices aren't the darbuka's.
   const Voices v = voicesFor(kPercussionKit, kTradAny);
   JCLAP_CHECK(v.kick != DARBUKA_DUM && v.shaker == SHAKER);
   JCLAP_CHECK(percussionistTradition(5) != kTradAny);
   // One-step and two-step patterns go through every twist without a crash.
   for (int steps : {1, 2})
      for (int kit : {kDrumKit, kPercussionKit, kAfroLatinKit}) {
         Pattern p;
         p.steps = static_cast<uint8_t>(steps);
         p.at(voicesFor(kit).kick, 0).vel = 100;
         for (int k = 0; k < 40; ++k) {
            Rng rng(static_cast<uint64_t>(k + 1));
            std::string tag;
            (void)mutate(p, kit, 1.0, rng, tag);
         }
      }
   // Every percussionist plays in 12/8 under a 12/8 drummer.
   Pattern twelve;
   twelve.rate = kRate16T;
   twelve.steps = 24;
   for (int who = 0; who < kPercussionists; ++who) {
      Rng rng(3);
      JCLAP_CHECK_EQ(percussionPart(who, twelve, rng).rate, kRate16T);
   }
   // The tabla player plays the tabla over a taal.
   Recipe r;
   r.genre = genreIndex("Indian");
   r.percussionist = 5;
   for (uint32_t seed = 1; seed < 6; ++seed) {
      const Section sec = makeSection(r, kChorus, seed);
      JCLAP_CHECK(!sec.perc.uses(DARBUKA_TEK));
   }
   // A higher ENERGY keeps the chorus above the pre.
   Recipe hot;
   hot.energy = 1.0;
   JCLAP_CHECK(sectionEnergy(hot, kChorus) > sectionEnergy(hot, kPre));
   // A quiet section's variations don't bring back what its energy took out.
   Recipe rock;
   rock.genre = genreIndex("Rock");
   for (uint32_t seed = 1; seed < 8; ++seed) {
      const Section sec = makeSection(rock, kIntro, seed);
      for (const Pattern &p : sec.vars)
         for (int t : {HT, MT, LT})
            JCLAP_CHECK(sectionEnergy(rock, kIntro) >= threshold(kDrumKit, t, p) || !p.uses(t));
   }
}

// VARY never drops the kick on a beat, in 12/8 too; with VELOCITY SETS ENERGY off, the velocity a
// section note was played at no longer matters.
void testVaryAndEnergy() {
   Song s;
   for (Section &sec : s.sections) {
      sec.vars[0].rate = kRate16T;
      sec.vars[0].steps = 24;
      for (int st = 0; st < 24; st += 6)
         sec.vars[0].at(K, st).vel = 100;
   }
   Settings st = plain();
   st.vary = 1.0;
   auto e = engineWith(s, st);
   JCLAP_CHECK_EQ(countOn(run(*e, 96000 * 8), K), 32);
   st.vary = 0.0;
   st.velEnergy = true;
   auto e2 = engineWith(oneHitSong(), st);
   e2->noteOn(0, st.noteFor(kVerse), 1); // a soft section note
   st.velEnergy = false;
   e2->setSettings(st);
   for (const Timed &t : run(*e2, 96000))
      if (t.on() && t.pad() == K)
         JCLAP_CHECK_EQ(static_cast<int>(t.m.data2), 100);
}

} // namespace

void testSongStructure() {
   // SONG mode walks the structure: Pop is I V P C V P C B C C O, then round again.
   Song s = oneHitSong();
   for (Section &sec : s.sections)
      sec.bars = 1;
   s.setForm(2);
   JCLAP_CHECK_EQ(s.entries(), 11);
   JCLAP_CHECK_EQ(s.totalBars(), 11);
   Settings st = plain();
   st.song = true;
   st.section = kIntro;
   auto e = engineWith(s, st);
   const int pop[] = {kIntro, kVerse, kPre, kChorus, kVerse, kPre, kChorus, kBridge, kChorus, kChorus, kOutro, kIntro, kVerse};
   for (int bar = 0; bar < 12; ++bar) {
      run(*e, 96000, true, bar * 4.0);
      JCLAP_CHECK_EQ(e->section(), pop[bar + 1]);
   }
   // A section picked by hand goes on from its next entry: the chorus, then the second verse.
   auto e2 = engineWith(s, st);
   e2->request(kActChorus);
   run(*e2, 96000, true, 0.0);
   JCLAP_CHECK_EQ(e2->section(), kVerse);
   run(*e2, 96000, true, 4.0);
   JCLAP_CHECK_EQ(e2->section(), kPre);
   // A section the structure leaves out plays, and the song goes on from where it was.
   auto e3 = engineWith(s, st);
   e3->request(kActBreak);
   run(*e3, 96000, true, 0.0);
   JCLAP_CHECK_EQ(e3->section(), kVerse);
   // A custom structure saves and loads; templates are named; no structure is once through.
   Song c = s;
   c.setForm(kCustomForm, {kIntro, kChorus, kChorus, kOutro});
   auto d = std::make_unique<Song>();
   JCLAP_CHECK(d->load(c.save().data(), c.save().size()));
   JCLAP_CHECK(*d == c);
   JCLAP_CHECK_EQ(d->entries(), 4);
   JCLAP_CHECK_EQ(d->entry(2), kChorus);
   JCLAP_CHECK_EQ(static_cast<int>(d->form), static_cast<int>(kCustomForm));
   Song once;
   JCLAP_CHECK_EQ(once.entries(), kSections);
   JCLAP_CHECK_EQ(once.entry(6), kOutro);
   JCLAP_CHECK_EQ(once.find(kChorus, 4), 3);
   // Changing the structure while it plays keeps the place where the section is still in it.
   auto e4 = engineWith(s, st);
   run(*e4, 96000 * 3, true, 0.0); // into the chorus, entry 3
   Song v = s;
   v.setForm(1); // I V C V C B C O: the chorus's next entry from 3 is 4, then the bridge
   e4->setSong(v);
   run(*e4, 96000, true, 12.0);
   JCLAP_CHECK_EQ(e4->section(), kBridge);
}

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
   testStylesDiffer();
   testReferenceGrooves();
   testHandTraditions();
   testEnergyArc();
   testPercussionist();
   testOldSongLoads();
   testEighthSwing();
   testCrashAfterFill();
   testPercussionFill();
   testFillOnLongPattern();
   testOddMeter();
   testGeneratorEdges();
   testVaryAndEnergy();
   testSongStructure();
   return jclap::core::testResult();
}
