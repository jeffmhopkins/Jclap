// JDrummer through CLAP: drums out on channel 10 with the host and on its own PLAY, section notes
// taken and other notes passed on, state with the song, and param text.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "params.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace jdrummer;

namespace {

constexpr const char *kId = "com.jeffmhopkins.jclap.jdrummer";

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

using Midi = std::vector<jclap::core::TestMidi>;

Midi play(jclap::core::TestHost &h, int blocks) {
   Midi all;
   for (int k = 0; k < blocks; ++k)
      for (jclap::core::TestMidi m : h.process(256).midi) {
         m.time += static_cast<uint32_t>(k * 256);
         all.push_back(m);
      }
   return all;
}

Midi noteOns(const Midi &midi) {
   Midi r;
   for (const auto &m : midi)
      if ((m.status & 0xF0) == 0x90 && m.data2 > 0)
         r.push_back(m);
   return r;
}

bool same(const Midi &a, const Midi &b) {
   if (a.size() != b.size())
      return false;
   for (size_t i = 0; i < a.size(); ++i)
      if (a[i].status != b[i].status || a[i].data1 != b[i].data1 || a[i].data2 != b[i].data2 || a[i].time != b[i].time)
         return false;
   return true;
}

} // namespace

int main() {
   using jclap::core::TestHost;

   // ---- with the host ----
   {
      // A couple of bars at 120 BPM: JKit's notes on channel 10, each with its note-off.
      TestHost h(clap_entry, kId);
      h.setTransport(120.0, 0.0, true);
      const Midi out = play(h, 400);
      const Midi on = noteOns(out);
      JCLAP_CHECK(on.size() > 8);
      int offs = 0;
      for (const auto &m : out) {
         JCLAP_CHECK_EQ(m.status & 0x0F, 9);
         JCLAP_CHECK(m.data1 >= kFirstNote && m.data1 < kFirstNote + kPads);
         offs += (m.status & 0xF0) == 0x80 || ((m.status & 0xF0) == 0x90 && m.data2 == 0);
      }
      JCLAP_CHECK(offs >= static_cast<int>(on.size()) - 4);
      // The first hit is on the downbeat.
      JCLAP_CHECK_EQ(on.front().time, 0u);
   }
   {
      // Stopped, nothing plays; PLAY runs it free.
      TestHost h(clap_entry, kId);
      h.setTransport(120.0, 0.0, false);
      JCLAP_CHECK(noteOns(play(h, 200)).empty());
      h.setParam(kPlay, 1.0);
      JCLAP_CHECK(!noteOns(play(h, 200)).empty());
      // HOST only follows the host, so a stopped host stops it again.
      h.setParam(kSync, 1.0);
      play(h, 4);
      JCLAP_CHECK(noteOns(play(h, 200)).empty());
   }

   // ---- notes in ----
   {
      // A section's note (Intro is C0 on the keyboard map) is taken; any other note goes on to JKit.
      TestHost h(clap_entry, kId);
      h.setTransport(120.0, 0.0, true);
      h.midi(0x90, 12, 100);
      h.midi(0x90, 60, 100);
      const Midi out = h.process(256).midi;
      bool section = false, passed = false;
      for (const auto &m : out) {
         section = section || m.data1 == 12;
         passed = passed || (m.status == 0x90 && m.data1 == 60);
      }
      JCLAP_CHECK(!section);
      JCLAP_CHECK(passed);
      // Unless PASS NOTES is off.
      h.setParam(kPassNotes, 0.0);
      h.midi(0x90, 62, 100);
      bool dropped = true;
      for (const auto &m : h.process(256).midi)
         dropped = dropped && m.data1 != 62;
      JCLAP_CHECK(dropped);
   }

   // ---- state ----
   {
      // A genre, a style and a seed saved and loaded play the same song.
      TestHost a(clap_entry, kId);
      a.setParam(kGenre, 6.0);
      a.setParam(kStyle, 2.0);
      a.setParam(kSeed, 777.0);
      a.process(256);
      Buffer b;
      const clap_ostream_t out = b.out();
      JCLAP_CHECK(stateOf(a)->save(a.plugin(), &out));
      TestHost c(clap_entry, kId);
      b.read = 0;
      const clap_istream_t in = b.in();
      JCLAP_CHECK(stateOf(c)->load(c.plugin(), &in));
      a.setTransport(170.0, 0.0, true);
      c.setTransport(170.0, 0.0, true);
      const Midi x = play(a, 300), y = play(c, 300);
      JCLAP_CHECK(!x.empty());
      JCLAP_CHECK(same(x, y));
   }

   // ---- param text ----
   {
      TestHost h(clap_entry, kId);
      JCLAP_CHECK(h.paramText(kGenre, 6.0) == std::string("Drum & Bass"));
      JCLAP_CHECK(h.paramText(kSwitchAt, 1.0) == std::string("Next Bar"));
      JCLAP_CHECK(h.paramText(kChannel, 10.0) == std::string("10"));
      double v = 0;
      JCLAP_CHECK(h.textToValue(kLearnNote, "C4", v));
      JCLAP_CHECK_EQ(static_cast<int>(v), 60);
   }

   return jclap::core::testResult();
}
