// Monolith through CLAP: notes out in time with the host, pass-through, state with the slab, param
// text and the factory presets.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "params.h"
#include "presets.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace monolith;

namespace {

constexpr const char *kId = "com.jeffmhopkins.jclap.monolith";

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

// The MIDI sent over `blocks` blocks of 256, with times counted from the first.
std::vector<jclap::core::TestMidi> play(jclap::core::TestHost &h, int blocks) {
   std::vector<jclap::core::TestMidi> all;
   for (int k = 0; k < blocks; ++k)
      for (jclap::core::TestMidi m : h.process(256).midi) {
         m.time += k * 256;
         all.push_back(m);
      }
   return all;
}

bool same(const std::vector<jclap::core::TestMidi> &a, const std::vector<jclap::core::TestMidi> &b) {
   if (a.size() != b.size())
      return false;
   for (size_t i = 0; i < a.size(); ++i)
      if (a[i].status != b[i].status || a[i].data1 != b[i].data1 || a[i].data2 != b[i].data2 || a[i].time != b[i].time)
         return false;
   return true;
}

int noteOns(const std::vector<jclap::core::TestMidi> &midi) {
   int n = 0;
   for (const auto &m : midi)
      n += (m.status & 0xF0) == 0x90 && m.data2 > 0;
   return n;
}

} // namespace

int main() {
   // Playing, it reads notes out on the chosen channel; every note-on gets its note-off once the
   // host stops.
   {
      jclap::core::TestHost h(clap_entry, kId);
      h.setParam(kChannel, 3);
      h.setTransport(120.0, 0.0, true);
      const auto bar = play(h, 375); // 2 s: one bar at 120 BPM
      JCLAP_CHECK(noteOns(bar) >= 1);
      for (const auto &m : bar)
         JCLAP_CHECK_EQ(m.status & 0x0F, 2);
      for (size_t i = 1; i < bar.size(); ++i)
         JCLAP_CHECK(bar[i].time >= bar[i - 1].time);
      h.setTransport(120.0, 4.0, false);
      auto all = bar;
      for (const auto &m : play(h, 2))
         all.push_back(m);
      int balance = 0;
      for (const auto &m : all)
         balance += (m.status & 0xF0) == 0x90 && m.data2 > 0 ? 1 : -1;
      JCLAP_CHECK_EQ(balance, 0);
      // Stopped, it is quiet.
      JCLAP_CHECK(play(h, 50).empty());
   }

   // Played notes pass through, unless key follow is using them.
   {
      jclap::core::TestHost h(clap_entry, kId);
      h.setTransport(120.0, 0.0, false);
      h.midi(0x90, 60, 100);
      auto out = h.process();
      JCLAP_CHECK_EQ(out.midi.size(), 1u);
      h.setParam(kFollow, 1);
      h.midi(0x80, 60, 0);
      h.midi(0x90, 62, 100);
      out = h.process();
      JCLAP_CHECK_EQ(out.midi.size(), 0u);
   }

   // The slab is saved with the project: one that drifted comes back as it was, not as the seed
   // would write it afresh.
   {
      jclap::core::TestHost a(clap_entry, kId);
      a.setParam(kSeed, 5);
      a.setParam(kLength, 2);
      a.setParam(kDrift, 1);
      a.setTransport(120.0, 0.0, true);
      play(a, 375 * 4);
      a.setParam(kLock, 1);
      a.setTransport(120.0, 0.0, false);
      play(a, 1);
      const Buffer state = save(a);

      jclap::core::TestHost b(clap_entry, kId);
      b.setTransport(120.0, 0.0, false);
      JCLAP_CHECK(load(b, state));
      // Saved again straight away, before a block has run, it's the same state.
      JCLAP_CHECK(save(b).data == state.data);
      play(b, 1);
      JCLAP_CHECK(save(b).data == state.data);
      JCLAP_CHECK_EQ(b.paramValue(kSeed), 5.0);

      // Both read the same notes from the top.
      a.setTransport(120.0, 0.0, true);
      b.setTransport(120.0, 0.0, true);
      const auto fromA = play(a, 750), fromB = play(b, 750);
      JCLAP_CHECK(same(fromA, fromB));

      // A fresh instance with the same params has the undrifted slab.
      jclap::core::TestHost c(clap_entry, kId);
      c.setParam(kSeed, 5);
      c.setParam(kLength, 2);
      c.setParam(kLock, 1);
      c.setParam(kDrift, 1);
      c.setTransport(120.0, 0.0, true);
      JCLAP_CHECK(!same(play(c, 750), fromA));

      // Garbage is refused.
      Buffer bad = state;
      bad.data[0] = 'X';
      JCLAP_CHECK(!load(b, bad));
      Buffer cut = state;
      cut.data.resize(40);
      JCLAP_CHECK(!load(b, cut));
   }

   // Every param's text reads back as the value it shows.
   {
      jclap::core::TestHost h(clap_entry, kId);
      for (clap_id id = 0; id < kParamCount; ++id)
         for (double v : {kParams[id].min, kParams[id].defaultValue, kParams[id].max}) {
            const std::string t = h.paramText(id, v);
            double back = -1.0;
            JCLAP_CHECK(h.textToValue(id, t.c_str(), back));
            JCLAP_CHECK(std::fabs(back - v) < 0.01);
         }
      double v = 0;
      JCLAP_CHECK(h.textToValue(kOrthography, "Halo", v));
      JCLAP_CHECK_EQ(v, 3.0);
      JCLAP_CHECK(h.textToValue(kChannel, "Ch 7", v));
      JCLAP_CHECK_EQ(v, 7.0);
      JCLAP_CHECK(!h.textToValue(kGrammar, "nan", v));
   }

   // The factory bank: whole presets, in range, uniquely named, using every script.
   {
      const auto &bank = factoryPresets();
      JCLAP_CHECK(bank.size() >= 12u);
      std::set<std::string> names, categories;
      std::set<int> scripts;
      for (const Preset &p : bank) {
         JCLAP_CHECK(names.insert(p.name).second);
         categories.insert(p.category);
         JCLAP_CHECK_EQ(p.values.size(), static_cast<size_t>(kParamCount));
         for (const auto &[id, v] : p.values) {
            JCLAP_CHECK(id < kParamCount);
            JCLAP_CHECK(v >= kParams[id].min && v <= kParams[id].max);
            if (id == kOrthography)
               scripts.insert(static_cast<int>(v));
         }
      }
      JCLAP_CHECK_EQ(categories.size(), 3u);
      JCLAP_CHECK_EQ(scripts.size(), 5u);
      JCLAP_CHECK(bank.front().name == "Init");
   }

   return jclap::core::testResult();
}
