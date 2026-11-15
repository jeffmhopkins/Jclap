// JArp through CLAP: held notes in, the arp out on each finger's channel, expression passed through
// and re-keyed, state with the pattern, param text and the factory presets.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

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

   // ---- state ----
   {
      TestHost h(clap_entry, kId);
      h.setParam(kLength, 12);
      h.setParam(kRowsBy, 1);
      h.process(64);
      const Buffer saved = save(h);
      TestHost g(clap_entry, kId);
      JCLAP_CHECK(load(g, saved));
      double v = 0;
      auto *params = static_cast<const clap_plugin_params_t *>(g.plugin()->get_extension(g.plugin(), CLAP_EXT_PARAMS));
      params->get_value(g.plugin(), kLength, &v);
      JCLAP_CHECK_EQ(v, 12.0);
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
      formatParam(kLength, 16, text, sizeof(text));
      JCLAP_CHECK(!std::strcmp(text, "16 steps"));
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
         const Pattern *pattern = factoryPattern(p.name);
         JCLAP_CHECK(pattern != nullptr);
         if (pattern)
            JCLAP_CHECK(pattern->count > 0);
      }
   }

   // ---- dice ----
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
      const Pattern pedal = *factoryPattern("Pedal Climb");
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
