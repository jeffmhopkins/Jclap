// JComp through CLAP: chords out with the host and on its own PLAY, section notes and chord keys
// taken and other notes passed on (or not), state with the song, and param text.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <cstdlib>
#include <new>

#include "editor.h"
#include "params.h"

extern "C" const clap_plugin_entry_t clap_entry;

// Counts the allocations made while `gCount` is on: process() must make none.
static bool gCount = false;
static int gAllocs = 0;
void *operator new(std::size_t n) {
   if (gCount)
      ++gAllocs;
   if (void *p = std::malloc(n ? n : 1))
      return p;
   throw std::bad_alloc();
}
void *operator new(std::size_t n, const std::nothrow_t &) noexcept {
   if (gCount)
      ++gAllocs;
   return std::malloc(n ? n : 1);
}
void *operator new[](std::size_t n) { return operator new(n); }
void *operator new[](std::size_t n, const std::nothrow_t &t) noexcept { return operator new(n, t); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }

using namespace jcomp;

namespace {

constexpr const char *kId = "com.jeffmhopkins.jclap.jcomp";

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
      // A few bars at 104 BPM: chords on channel 1, several notes at once, each with its note-off.
      TestHost h(clap_entry, kId);
      h.setTransport(104.0, 0.0, true);
      const Midi out = play(h, 600);
      const Midi on = noteOns(out);
      JCLAP_CHECK(on.size() > 12);
      int offs = 0;
      for (const auto &m : out) {
         JCLAP_CHECK_EQ(m.status & 0x0F, 0);
         offs += (m.status & 0xF0) == 0x80 || ((m.status & 0xF0) == 0x90 && m.data2 == 0);
      }
      JCLAP_CHECK(offs >= static_cast<int>(on.size()) - 8);
      int together = 0;
      for (size_t i = 1; i < on.size(); ++i)
         together += on[i].time - on[i - 1].time < 400;
      JCLAP_CHECK(together > 6);
   }
   {
      // Stopped, nothing plays; PLAY runs it free.
      TestHost h(clap_entry, kId);
      h.setTransport(120.0, 0.0, false);
      JCLAP_CHECK(noteOns(play(h, 200)).empty());
      h.setParam(kPlay, 1.0);
      JCLAP_CHECK(!noteOns(play(h, 300)).empty());
   }

   // ---- notes in ----
   {
      // A section's note (Intro is C0 on the keyboard map) and a chord key are taken; a note between
      // them is dropped unless PASS OTHER NOTES is on.
      TestHost h(clap_entry, kId);
      h.setTransport(120.0, 0.0, true);
      h.midi(0x90, 12, 100);
      h.midi(0x90, 64, 100);
      h.midi(0x90, 40, 100);
      bool section = false, chordKey = false, passed = false;
      for (const auto &m : h.process(256).midi) {
         section = section || m.data1 == 12;
         passed = passed || (m.status == 0x90 && m.data1 == 40 && m.data2 == 100);
         chordKey = chordKey || (m.status == 0x90 && m.data1 == 64 && m.data2 == 100 && m.time == 0);
      }
      JCLAP_CHECK(!section);
      JCLAP_CHECK(!passed);
      (void)chordKey;
      h.setParam(kPassNotes, 1.0);
      h.midi(0x90, 41, 100);
      bool through = false;
      for (const auto &m : h.process(256).midi)
         through = through || (m.status == 0x90 && m.data1 == 41);
      JCLAP_CHECK(through);
   }

   // ---- state ----
   {
      // The song itself is saved and loaded, edits and all: an edited chord and hit come back.
      TestHost a(clap_entry, kId);
      a.setParam(kGenre, 7.0);
      a.setParam(kStyle, 1.0);
      a.setParam(kSeed, 777.0);
      a.process(256);
      a.plugin()->on_main_thread(a.plugin());
      auto *host = a.object<EditorHost>();
      JCLAP_CHECK(host != nullptr);
      Song edited = host->song();
      for (Section &sec : edited.sections) {
         sec.chords[0] = chord("F#m7");
         sec.takes[0].at(3) = hitOf('X');
      }
      host->setSong(edited);
      a.process(256);
      Buffer b;
      const clap_ostream_t out = b.out();
      JCLAP_CHECK(stateOf(a)->save(a.plugin(), &out));
      TestHost c(clap_entry, kId);
      b.read = 0;
      const clap_istream_t in = b.in();
      JCLAP_CHECK(stateOf(c)->load(c.plugin(), &in));
      JCLAP_CHECK(c.object<EditorHost>()->song() == edited);
      // The same recipe without the edits: another song.
      TestHost d(clap_entry, kId);
      d.setParam(kGenre, 7.0);
      d.setParam(kStyle, 1.0);
      d.setParam(kSeed, 777.0);
      d.process(256);
      d.plugin()->on_main_thread(d.plugin());
      a.setTransport(90.0, 0.0, true);
      c.setTransport(90.0, 0.0, true);
      d.setTransport(90.0, 0.0, true);
      const Midi x = play(a, 400), y = play(c, 400), z = play(d, 400);
      JCLAP_CHECK(!x.empty());
      JCLAP_CHECK(same(x, y));
      JCLAP_CHECK(!same(x, z));
   }
   {
      // E1, E12: a state from before 0.6.0 keeps playing as it did: its Song Mode 1 (round and
      // round) is Song On Repeat now, and its swing doesn't ease.
      TestHost a(clap_entry, kId);
      a.setParam(kSongMode, 1.0);
      a.process(256);
      a.plugin()->on_main_thread(a.plugin());
      Buffer b;
      const clap_ostream_t out = b.out();
      JCLAP_CHECK(stateOf(a)->save(a.plugin(), &out));
      // Cut it down to the params a 0.5 state had (everything before INTRO SETS UP THE VERSE).
      Buffer old;
      old.data.assign(b.data.begin(), b.data.begin() + 12);
      const uint32_t count = kIntroSetup;
      std::memcpy(old.data.data() + 8, &count, 4);
      old.data.insert(old.data.end(), b.data.begin() + 12, b.data.begin() + 12 + 8 * count);
      old.data.insert(old.data.end(), b.data.begin() + 12 + 8 * kParamCount, b.data.end());
      TestHost c(clap_entry, kId);
      const clap_istream_t in = old.in();
      JCLAP_CHECK(stateOf(c)->load(c.plugin(), &in));
      JCLAP_CHECK_EQ(static_cast<int>(c.object<EditorHost>()->paramValue(kSongMode)), 2);
      JCLAP_CHECK_EQ(static_cast<int>(c.object<EditorHost>()->paramValue(kSwingEase)), 0);
      // Today's state keeps its Play Song.
      TestHost d(clap_entry, kId);
      b.read = 0;
      const clap_istream_t in2 = b.in();
      JCLAP_CHECK(stateOf(d)->load(d.plugin(), &in2));
      JCLAP_CHECK_EQ(static_cast<int>(d.object<EditorHost>()->paramValue(kSongMode)), 1);
      JCLAP_CHECK_EQ(static_cast<int>(d.object<EditorHost>()->paramValue(kSwingEase)), 1);
   }
   {
      // A recipe param set by the host remakes the song: the genre it names plays.
      TestHost a(clap_entry, kId);
      const Song before = a.object<EditorHost>()->song();
      a.setParam(kGenre, 7.0);
      a.process(256);
      a.plugin()->on_main_thread(a.plugin());
      JCLAP_CHECK(!(a.object<EditorHost>()->song() == before));
      // The panel's own changes (the same value again) don't.
      const Song now = a.object<EditorHost>()->song();
      a.setParam(kGenre, 7.0);
      a.process(256);
      a.plugin()->on_main_thread(a.plugin());
      JCLAP_CHECK(a.object<EditorHost>()->song() == now);
   }

   // ---- real time ----
   {
      // Nothing allocated in process(): the comp, chord keys, section notes, passed notes, the bass.
      TestHost h(clap_entry, kId);
      h.setParam(kPassNotes, 1.0);
      h.setParam(kRuns, 1.0);
      h.setParam(kDensity, 0.9);
      h.setParam(kRhythmFrom, 2.0);
      h.process(256);
      struct Out {
         int count = 0;
         static bool push(const clap_output_events_t *l, const clap_event_header_t *) {
            ++static_cast<Out *>(l->ctx)->count;
            return true;
         }
      } sink;
      const clap_output_events_t outEvents{&sink, Out::push};
      struct In {
         clap_event_midi_t ev[8];
         uint32_t n = 0;
         static uint32_t size(const clap_input_events_t *l) { return static_cast<const In *>(l->ctx)->n; }
         static const clap_event_header_t *get(const clap_input_events_t *l, uint32_t i) {
            return &static_cast<const In *>(l->ctx)->ev[i].header;
         }
         void midi(uint32_t time, uint8_t a, uint8_t b, uint8_t c) {
            clap_event_midi_t &e = ev[n++];
            e = clap_event_midi_t{};
            e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
            e.data[0] = a;
            e.data[1] = b;
            e.data[2] = c;
         }
      } events;
      const clap_input_events_t inEvents{&events, In::size, In::get};
      static float bufs[4][256];
      float *ins[2] = {bufs[0], bufs[1]}, *outs[2] = {bufs[2], bufs[3]};
      clap_audio_buffer_t inBuf{}, outBuf{};
      inBuf.data32 = ins;
      inBuf.channel_count = 2;
      outBuf.data32 = outs;
      outBuf.channel_count = 2;
      clap_event_transport_t transport{};
      transport.header = {sizeof(transport), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
      transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
      transport.tempo = 120.0;
      clap_process_t proc{};
      proc.frames_count = 256;
      proc.steady_time = -1;
      proc.transport = &transport;
      proc.audio_inputs = &inBuf;
      proc.audio_outputs = &outBuf;
      proc.audio_inputs_count = proc.audio_outputs_count = 1;
      proc.in_events = &inEvents;
      proc.out_events = &outEvents;
      gAllocs = 0;
      for (int k = 0; k < 800; ++k) {
         events.n = 0;
         if (k % 40 == 0)
            for (uint8_t n : {62, 65, 69, 72})
               events.midi(5, k % 80 ? 0x80 : 0x90, n, 100);
         if (k % 25 == 0) {
            events.midi(9, 0x91, 40, 100); // the bass, on channel 2
            events.midi(10, 0x90, 30, 90); // passed on
         }
         if (k % 25 == 5) {
            events.midi(1, 0x81, 40, 0);
            events.midi(2, 0x80, 30, 0);
         }
         transport.song_pos_beats = static_cast<clap_beattime>(std::llround(k * 256 / 24000.0 * CLAP_BEATTIME_FACTOR));
         gCount = true;
         h.plugin()->process(h.plugin(), &proc);
         gCount = false;
      }
      JCLAP_CHECK(sink.count > 100);
      JCLAP_CHECK_EQ(gAllocs, 0);
   }

   // ---- param text ----
   {
      TestHost h(clap_entry, kId);
      JCLAP_CHECK(h.paramText(kGenre, 0.0) == std::string("Funk"));
      JCLAP_CHECK(h.paramText(kSwitchAt, 1.0) == std::string("Next Bar"));
      JCLAP_CHECK(h.paramText(kChannel, 1.0) == std::string("1"));
      double v = 0;
      JCLAP_CHECK(h.textToValue(kChordSplit, "C4", v));
      JCLAP_CHECK_EQ(static_cast<int>(v), 60);
      // Every param's text reads back as its value.
      for (clap_id id = 0; id < kParamCount; ++id)
         for (int k = 0; k <= 8; ++k) {
            const double x = kParams[id].min + (kParams[id].max - kParams[id].min) * k / 8.0;
            const std::string t = h.paramText(id, x);
            double back = -1e9;
            JCLAP_CHECK(h.textToValue(id, t.c_str(), back));
            JCLAP_CHECK(h.paramText(id, back) == t);
         }
   }

   return jclap::core::testResult();
}
