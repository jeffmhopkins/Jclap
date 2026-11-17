// JArp through CLAP: held notes in, the arp out on each finger's channel, expression passed through
// and re-keyed, state with the pattern, param text and the factory presets.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "editor.h"
#include "params.h"
#include "presets.h"
#include "randomize.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace jarp;

namespace {

constexpr const char *kId = "com.jeffmhopkins.jclap.jarp";

struct Buffer {
   std::vector<char> data;
   size_t read = 0;
   clap_ostream_t out() {
      return {this, [](const clap_ostream_t *s, const void *p, uint64_t n) -> int64_t {
                 auto *b = static_cast<Buffer *>(s->ctx);
                 b->data.insert(b->data.end(), static_cast<const char *>(p), static_cast<const char *>(p) + n);
                 return static_cast<int64_t>(n);
              }};
   }
   clap_istream_t in() {
      return {this, [](const clap_istream_t *s, void *p, uint64_t n) -> int64_t {
                 auto *b = static_cast<Buffer *>(s->ctx);
                 const size_t k = std::min<size_t>(n, b->data.size() - b->read);
                 std::memcpy(p, b->data.data() + b->read, k);
                 b->read += k;
                 return static_cast<int64_t>(k);
              }};
   }
};

const clap_plugin_state_t *stateOf(jclap::core::TestHost &h) {
   return static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
}

Buffer save(jclap::core::TestHost &h) {
   Buffer b;
   const clap_ostream_t out = b.out();
   JCLAP_CHECK(stateOf(h)->save(h.plugin(), &out));
   return b;
}

bool load(jclap::core::TestHost &h, Buffer b) {
   b.read = 0;
   const clap_istream_t in = b.in();
   return stateOf(h)->load(h.plugin(), &in);
}

std::vector<jclap::core::TestMidi> play(jclap::core::TestHost &h, int blocks) {
   std::vector<jclap::core::TestMidi> all;
   for (int k = 0; k < blocks; ++k)
      for (jclap::core::TestMidi m : h.process(256).midi) {
         m.time += k * 256;
         all.push_back(m);
      }
   return all;
}

std::vector<jclap::core::TestMidi> noteOns(const std::vector<jclap::core::TestMidi> &midi) {
   std::vector<jclap::core::TestMidi> r;
   for (const auto &m : midi)
      if ((m.status & 0xF0) == 0x90 && m.data2 > 0)
         r.push_back(m);
   return r;
}

} // namespace

int main() {
   using jclap::core::TestHost;

   // ---- notes ----
   {
      // A C major 7th on MPE channels 2-5 (MIDI dialect): the default pattern climbs it, each note on
      // its finger's own channel; the held notes themselves don't pass.
      TestHost h(clap_entry, kId);
      h.setTransport(120.0, 0.0, true);
      const int keys[] = {48, 52, 55, 59};
      for (int f = 0; f < 4; ++f)
         h.midi(static_cast<uint8_t>(0x91 + f), static_cast<uint8_t>(keys[f]), 100);
      const auto on = noteOns(play(h, 100)); // 25600 samples: steps at 0, 6000, ... 24000
      JCLAP_CHECK_EQ(on.size(), 5u);
      JCLAP_CHECK_EQ(on[0].data1, 48);
      JCLAP_CHECK_EQ(on[0].status, 0x91);
      JCLAP_CHECK_EQ(on[1].data1, 52);
      JCLAP_CHECK_EQ(on[1].status, 0x92);
      JCLAP_CHECK_EQ(on[3].data1, 59);
      JCLAP_CHECK_EQ(on[3].status, 0x94);
      JCLAP_CHECK_EQ(on[1].time, 6000u);

      // Pressure, slide and bend pass straight through on their channel; poly aftertouch is re-keyed
      // to the note sounding from that finger.
      h.midi(0xD3, 90, 0);
      h.midi(0xB3, 74, 30);
      h.midi(0xE3, 0, 80);
      auto out = h.process(256).midi;
      int passed = 0;
      for (const auto &m : out)
         passed += m.status == 0xD3 || (m.status == 0xB3 && m.data1 == 74) || m.status == 0xE3;
      JCLAP_CHECK_EQ(passed, 3);

      // Letting go ends that finger's notes at once: G3 is playing on step 5.
      h.midi(0x83, 55, 0);
      out = h.process(256).midi;
      bool ended = false;
      for (const auto &m : out)
         ended = ended || ((m.status & 0xF0) == 0x80 && (m.status & 0x0F) == 3 && m.data1 == 55 && m.time == 0);
      JCLAP_CHECK(ended);
   }
   {
      // A held key's CLAP tuning goes to each new arp note it starts, not just the one sounding.
      TestHost h(clap_entry, kId);
      h.setTransport(120.0, 0.0, true);
      h.note(CLAP_EVENT_NOTE_ON, 60, 0.8, 1, 0);
      h.process(256);
      h.noteExpression(CLAP_NOTE_EXPRESSION_TUNING, 60, 2.0, 1, 0);
      h.process(256);
      int carried = 0; // The default pattern comes back to a lone key a few steps on.
      for (int k = 0; k < 400; ++k) {
         const auto out = h.process(256);
         for (const auto &m : noteOns(out.midi))
            for (const auto &x : out.expressions)
               carried += x.expression_id == CLAP_NOTE_EXPRESSION_TUNING && x.key == m.data1 && x.header.time == m.time &&
                          std::abs(x.value - 2.0) < 1e-9;
      }
      JCLAP_CHECK(carried >= 1);
   }
   {
      // CLAP notes and note expressions: pressure for a held key goes to the arp note playing from
      // it, an octave up on a +1 step.
      TestHost h(clap_entry, kId);
      h.setTransport(120.0, 0.0, true);
      h.note(CLAP_EVENT_NOTE_ON, 60, 0.8, 1, 0);
      auto first = h.process(256);
      JCLAP_CHECK_EQ(noteOns(first.midi).size(), 1u);
      h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 60, 0.5, 1, 0);
      const auto out = h.process(256);
      JCLAP_CHECK_EQ(out.expressions.size(), 1u);
      if (!out.expressions.empty()) {
         JCLAP_CHECK_EQ(out.expressions[0].key, 60);
         JCLAP_CHECK_EQ(out.expressions[0].value, 0.5);
      }
   }
   {
      // No transport: the arp runs on its own clock.
      TestHost h(clap_entry, kId);
      h.midi(0x90, 60, 100);
      h.midi(0x90, 64, 100);
      JCLAP_CHECK(noteOns(play(h, 50)).size() >= 2);
   }

   // ---- sustain pedal ----
   {
      // Held by the arp (the default), the pedal doesn't reach the synth and keeps a let-go key
      // playing; with PASS SUSTAIN on it goes straight through and the arp ignores it.
      const auto pedals = [](const std::vector<jclap::core::TestMidi> &midi) {
         int n = 0;
         for (const auto &m : midi)
            n += (m.status & 0xF0) == 0xB0 && m.data1 == 64;
         return n;
      };
      for (int pass = 0; pass < 2; ++pass) {
         TestHost h(clap_entry, kId);
         h.setParam(kPassSustain, pass);
         h.setTransport(120.0, 0.0, true);
         h.midi(0x90, 60, 100);
         h.midi(0xB0, 64, 127);
         auto first = play(h, 4);
         h.midi(0x80, 60, 0);
         auto later = play(h, 200);
         JCLAP_CHECK_EQ(pedals(first), pass);
         if (pass)
            JCLAP_CHECK(noteOns(later).empty());
         else
            JCLAP_CHECK(!noteOns(later).empty());
      }
   }

   // ---- state ----
   {
      TestHost h(clap_entry, kId);
      h.setParam(kAdvance, 2);
      h.setParam(kRowsBy, 1);
      h.process(64);
      const Buffer saved = save(h);
      TestHost g(clap_entry, kId);
      JCLAP_CHECK(load(g, saved));
      double v = 0;
      auto *params = static_cast<const clap_plugin_params_t *>(g.plugin()->get_extension(g.plugin(), CLAP_EXT_PARAMS));
      params->get_value(g.plugin(), kAdvance, &v);
      JCLAP_CHECK_EQ(v, 2.0);
      params->get_value(g.plugin(), kRowsBy, &v);
      JCLAP_CHECK_EQ(v, 1.0);
      // The same pattern plays from the copy.
      h.setTransport(120.0, 0.0, true);
      g.setTransport(120.0, 0.0, true);
      h.midi(0x90, 60, 100);
      g.midi(0x90, 60, 100);
      const auto a = noteOns(play(h, 40)), b = noteOns(play(g, 40));
      JCLAP_CHECK_EQ(a.size(), b.size());
      Buffer bad = saved;
      bad.data[0] = 'X';
      JCLAP_CHECK(!load(g, bad));
   }

   {
      // The preset's name and blurb come back with the state; a state from before blurbs gets a
      // factory preset's own.
      TestHost h(clap_entry, kId);
      auto *host = h.object<jarp::EditorHost>();
      JCLAP_CHECK(host != nullptr);
      if (host) {
         JCLAP_CHECK(host->presetBlurb() == factoryBlurb("Init"));
         host->setPresetName("Funk Bass");
         host->setPresetBlurb("My own words");
         Buffer saved = save(h);
         TestHost g(clap_entry, kId);
         JCLAP_CHECK(load(g, saved));
         auto *copy = g.object<jarp::EditorHost>();
         JCLAP_CHECK(copy && copy->presetName() == "Funk Bass" && copy->presetBlurb() == "My own words");
         saved.data.resize(saved.data.size() - 4 - std::strlen("My own words"));
         saved.read = 0;
         TestHost old(clap_entry, kId);
         JCLAP_CHECK(load(old, saved));
         auto *oldHost = old.object<jarp::EditorHost>();
         JCLAP_CHECK(oldHost && oldHost->presetBlurb() == factoryBlurb("Funk Bass"));
      }
   }

   {
      // A version 1 state (one pattern, a Length param) loads as slot 1 with that length, and the
      // params after Length land where they are now.
      Buffer old;
      const auto put = [&](const void *p, size_t n) {
         old.data.insert(old.data.end(), static_cast<const char *>(p), static_cast<const char *>(p) + n);
      };
      put("JARP", 4);
      const uint32_t header[2] = {1, 13};
      put(header, sizeof(header));
      const double values[13] = {2, 0.5, 12, 1, 1, 1, 0, 0, 0, 0.5, 0, 0.6, 4};
      put(values, sizeof(values));
      std::vector<uint8_t> pattern = {1, 1, 0, 0, 4};
      for (int k = 0; k < 16; ++k) // version 1 had 16 steps
         pattern.insert(pattern.end(), {0, 1, 0, 0});
      const uint32_t size = static_cast<uint32_t>(pattern.size());
      put(&size, 4);
      put(pattern.data(), size);
      const uint32_t nameLength = 0;
      put(&nameLength, 4);
      TestHost g(clap_entry, kId);
      JCLAP_CHECK(load(g, old));
      auto *params = static_cast<const clap_plugin_params_t *>(g.plugin()->get_extension(g.plugin(), CLAP_EXT_PARAMS));
      double v = -1;
      params->get_value(g.plugin(), kLatch, &v);
      JCLAP_CHECK_EQ(v, 1.0);
      params->get_value(g.plugin(), kRowsBy, &v);
      JCLAP_CHECK_EQ(v, 1.0);
      params->get_value(g.plugin(), kAdvance, &v);
      JCLAP_CHECK_EQ(v, 0.0);
      // Slot 1's one note plays, once per 12 steps.
      g.setTransport(120.0, 0.0, true);
      g.midi(0x90, 60, 100);
      JCLAP_CHECK_EQ(noteOns(play(g, 400)).size(), 2u); // 102400 samples: steps 0 and 12
   }

   {
      // Edits from the panel while the host isn't processing are never lost: after a hundred of them
      // (more than any queue would hold), the last is what plays.
      TestHost h(clap_entry, kId);
      auto *editor = h.object<jarp::EditorHost>();
      JCLAP_CHECK(editor != nullptr);
      for (int k = 0; k < 100; ++k) {
         jarp::Pattern p;
         p.length = 8;
         p.add(0, k == 99 ? 1 : 2 + k % 6, 4);
         editor->setSlot(0, p);
      }
      h.setTransport(120.0, 0.0, true);
      h.midi(0x90, 60, 100);
      const auto on = noteOns(play(h, 25)); // 6400 samples: steps 0 and 1 (6000 per step)
      JCLAP_CHECK_EQ(on.size(), 1u);
      if (!on.empty())
         JCLAP_CHECK_EQ(on[0].time, 6000u);
   }

   {
      // All Notes Off (a host's panic) lets go of latched fingers too: nothing plays after it.
      TestHost h(clap_entry, kId);
      h.setParam(kLatch, 1);
      h.setTransport(120.0, 0.0, true);
      h.midi(0x91, 60, 100);
      play(h, 2);
      h.midi(0x81, 60, 0);
      play(h, 2);
      h.midi(0xB1, 123, 0);
      JCLAP_CHECK(noteOns(play(h, 100)).empty());
   }

   // ---- params ----
   {
      char text[64];
      formatParam(kRate, 2, text, sizeof(text));
      JCLAP_CHECK(!std::strcmp(text, "1/16"));
      formatParam(kSwing, 0.54, text, sizeof(text));
      JCLAP_CHECK(!std::strcmp(text, "54%"));
      formatParam(kCurve, 0.0, text, sizeof(text));
      JCLAP_CHECK(!std::strcmp(text, "Linear"));
      formatParam(kCurve, -0.4, text, sizeof(text));
      JCLAP_CHECK(!std::strcmp(text, "Soft 40%"));
      formatParam(kSlot, 3, text, sizeof(text));
      JCLAP_CHECK(!std::strcmp(text, "3"));
      formatParam(kAdvance, 1, text, sizeof(text));
      JCLAP_CHECK(!std::strcmp(text, "Chord"));
      for (clap_id id = 0; id < kParamCount; ++id) {
         // Every value reads back as itself.
         const double values[] = {kParams[id].min, kParams[id].defaultValue, kParams[id].max};
         for (double value : values) {
            formatParam(id, value, text, sizeof(text));
            double back = -999;
            JCLAP_CHECK(parseParam(id, text, back));
            JCLAP_CHECK(std::abs(back - value) < 0.006);
         }
      }
   }

   // ---- presets ----
   {
      std::set<std::string> names;
      for (const Preset &p : factoryPresets()) {
         JCLAP_CHECK(names.insert(p.name).second);
         JCLAP_CHECK_EQ(p.values.size(), static_cast<size_t>(kParamCount));
         const Bank *bank = factoryBank(p.name);
         JCLAP_CHECK(bank != nullptr);
         if (bank)
            JCLAP_CHECK(bank->slots[0].count > 0);
         // A chained preset has more than one pattern to chain.
         for (const auto &[id, v] : p.values)
            if (bank && id == kAdvance && std::lround(v) != 0)
               JCLAP_CHECK(bank->next(0) != 0);
      }
      JCLAP_CHECK_EQ(factoryCategories().size(), 51u);
      // The library uses LENGTH by HELD and BOUNCE: Up Down is the held notes up and back for any
      // chord, and a held pattern has a step for each row it could reach.
      {
         int byHeld = 0, bounce = 0;
         for (const library::Entry &e : factoryLibrary())
            for (const Pattern &p : e.patterns) {
               byHeld += p.byHeld;
               bounce += p.bounce;
               if (std::string(e.name) == "Up Down")
                  JCLAP_CHECK(p.byHeld && p.bounce && p.stepsFor(3) == 3 && p.cycleFor(3) == 4);
            }
         JCLAP_CHECK(byHeld >= 5);
         JCLAP_CHECK(bounce >= 3);
      }
      // Every factory preset says what it is, in a line that fits the header, and no pattern skips a
      // row: the rows each uses run on without a gap, and all of a preset's from row 1, so every note
      // held plays and the voicing gives the shape.
      for (const library::Entry &e : factoryLibrary()) {
         JCLAP_CHECK(e.blurb && std::strlen(e.blurb) > 0 && std::strlen(e.blurb) <= 66);
         bool all[kRows] = {};
         for (const Pattern &p : e.patterns) {
            bool used[kRows] = {};
            for (int i = 0; i < p.count; ++i)
               used[p.notes[i].row] = all[p.notes[i].row] = true;
            int lo = kRows, hi = -1;
            for (int r = 0; r < kRows; ++r)
               if (used[r])
                  lo = std::min(lo, r), hi = std::max(hi, r);
            for (int r = lo; r <= hi; ++r)
               if (!used[r]) {
                  std::printf("%s skips row %d\n", e.name, r + 1);
                  JCLAP_CHECK(used[r]);
               }
         }
         JCLAP_CHECK(all[0]);
         // The Drum Kit presets are four bars: 64 sixteenths, or 48 triplet eighths, and keep their
         // rows, so letting go of one drum's key drops only that drum.
         if (std::string(e.category) == "Drum Kit") {
            bool keeps = false;
            for (const auto &[id, x] : e.params)
               keeps = keeps || (id == kKeepRows && x == 1);
            JCLAP_CHECK(keeps);
         }
         // The Berlin School presets take one, two or three notes held: a fixed-length pattern uses
         // rows 1 to 3 at most, its octaves coming from the Octave lane.
         if (std::string(e.category).rfind("Berlin School", 0) == 0)
            for (const Pattern &p : e.patterns)
               for (int i = 0; i < p.count; ++i)
                  if (!p.byHeld && p.notes[i].row > 2) {
                     std::printf("%s needs more than three notes held\n", e.name);
                     JCLAP_CHECK(false);
                     break;
                  }
         if (std::string(e.category) == "Drum Kit")
            for (const Pattern &p : e.patterns)
               if (!p.empty() && p.length != 64 && p.length != 48) {
                  std::printf("%s is %d steps\n", e.name, p.length);
                  JCLAP_CHECK(false);
               }
         for (int r = 1; r < kRows; ++r)
            if (all[r] && !all[r - 1]) {
               std::printf("%s skips row %d\n", e.name, r);
               JCLAP_CHECK(all[r - 1]);
            }
      }
      // The bass grooves play from two held notes, or three where the line walks a chord: no row past
      // the third, and the blurb says what to hold (a triad, or the 1-5-6 shuffle, for three).
      {
         int bass = 0;
         for (const library::Entry &e : factoryLibrary()) {
            if (std::string(e.category).rfind("Bass: ", 0) != 0)
               continue;
            ++bass;
            int top = 0;
            for (const Pattern &p : e.patterns)
               for (int i = 0; i < p.count; ++i)
                  top = std::max(top, p.notes[i].row + 1);
            const std::string blurb = e.blurb;
            const bool three = blurb.rfind("Hold a triad:", 0) == 0 || blurb.rfind("Hold root, 5th and 6th:", 0) == 0;
            const bool two = blurb.rfind("Hold root and fifth:", 0) == 0 || blurb.rfind("Hold root and b7:", 0) == 0;
            if (top > 3 || (three && top != 3) || (two && top != 2) || (!two && !three)) {
               std::printf("%s plays %d rows: %s\n", e.name, top, e.blurb);
               JCLAP_CHECK(false);
            }
         }
         JCLAP_CHECK(bass >= 80);
      }
      // A bass line's ^ is an octave up from its register and v an octave down, in the Octave lane.
      {
         const Pattern b = library::bass("1 2^ 2v 1s^");
         JCLAP_CHECK_EQ(b.length, 4);
         JCLAP_CHECK_EQ(b.lanes[0].octave, -1);
         JCLAP_CHECK_EQ(b.lanes[1].octave, 0);
         JCLAP_CHECK_EQ(b.lanes[2].octave, -2);
         JCLAP_CHECK_EQ(b.lanes[3].octave, 0);
         JCLAP_CHECK_EQ(b.notes[b.noteStarting(0, 3)].length, 2);
      }
      // The library's step lines: a tie, a chord, a short note, a note held to the end, and lanes
      // that repeat.
      const Pattern t = library::Pat("1 _ c . 2s 3!").vel("5 .").oct(". 1");
      JCLAP_CHECK_EQ(t.length, 6);
      JCLAP_CHECK_EQ(t.count, 7);
      JCLAP_CHECK_EQ(t.notes[t.noteStarting(0, 0)].length, 8);
      JCLAP_CHECK(t.noteStarting(3, 2) >= 0);
      JCLAP_CHECK_EQ(t.notes[t.noteStarting(1, 4)].length, 2);
      JCLAP_CHECK_EQ(t.notes[t.noteStarting(2, 5)].length, 4);
      JCLAP_CHECK_EQ(t.lanes[4].velocity, 5);
      JCLAP_CHECK_EQ(t.lanes[5].octave, 1);
      JCLAP_CHECK_EQ(t.lanes[5].velocity, 0);
      // The chord (c) on step 3 starts its own row 1, after the tied one.
      JCLAP_CHECK_EQ(t.notes[t.noteStarting(0, 2)].length, 4);
   }

   // ---- dice ----
   {
      // Rebuilding a pattern (as Mutate and Randomize do) keeps each note's own lane values.
      jarp::Pattern p;
      p.add(0, 0, 4);
      p.add(2, 3, 4);
      p.notes[1].own = jarp::kOwnVelocity;
      p.notes[1].lane.velocity = -20;
      std::vector<jarp::Note> notes(p.notes.begin(), p.notes.begin() + p.count);
      jarp::dice_detail::rebuild(p, notes);
      const int i = p.noteStarting(2, 3);
      JCLAP_CHECK(i >= 0);
      if (i >= 0) {
         JCLAP_CHECK_EQ(p.notes[i].own, jarp::kOwnVelocity);
         JCLAP_CHECK_EQ(p.notes[i].lane.velocity, -20);
      }
   }
   {
      DiceSettings d;
      Pattern base;
      const Pattern r = randomizePattern(base, d, 16, 1234);
      JCLAP_CHECK(r.count > 0);
      for (int i = 0; i < r.count; ++i) {
         JCLAP_CHECK(r.notes[i].row < d.rows);
         JCLAP_CHECK(r.notes[i].length <= d.length);
      }
      // Nothing ticked leaves the pattern as it is.
      DiceSettings none;
      for (bool &on : none.on)
         on = false;
      const Pattern pedal = factoryBank("Pedal Climb")->slots[0];
      JCLAP_CHECK(randomizePattern(pedal, none, 16, 99) == pedal);
      JCLAP_CHECK(mutatePattern(pedal, none, 16, 99) == pedal);
      // Mutate keeps most of the pattern.
      const Pattern m = mutatePattern(pedal, d, 16, 7);
      JCLAP_CHECK(m.count > pedal.count / 2);
      DiceHistory history;
      history.push(pedal);
      JCLAP_CHECK(history.canUndo());
      JCLAP_CHECK(history.undo(m) == pedal);
      JCLAP_CHECK(history.canRedo());
   }

   return jclap::core::testResult();
}
