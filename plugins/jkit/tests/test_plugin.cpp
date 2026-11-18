// Drives JKit the way a host does: sounds on pads, notes in, audio out, state saved and loaded.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "editor.h"
#include "kit.h"
#include "params.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace jkit;

namespace {

constexpr const char *kId = "com.jeffmhopkins.jclap.jkit";

struct Buffer {
   std::vector<uint8_t> bytes;
   size_t pos = 0;
   clap_ostream_t out{this, [](const clap_ostream_t *s, const void *data, uint64_t size) -> int64_t {
                         auto *b = static_cast<Buffer *>(s->ctx);
                         b->bytes.insert(b->bytes.end(), static_cast<const uint8_t *>(data),
                                         static_cast<const uint8_t *>(data) + size);
                         return static_cast<int64_t>(size);
                      }};
   clap_istream_t in{this, [](const clap_istream_t *s, void *data, uint64_t size) -> int64_t {
                        auto *b = static_cast<Buffer *>(s->ctx);
                        const size_t n = std::min<size_t>(size, b->bytes.size() - b->pos);
                        std::memcpy(data, b->bytes.data() + b->pos, n);
                        b->pos += n;
                        return static_cast<int64_t>(n);
                     }};
};

// A 0.2 s sine burst at 220 Hz with a quick decay.
SamplePtr tone(double seconds = 0.2, int role = kKick) {
   auto s = std::make_shared<Sample>();
   s->name = "Test";
   s->prompt = "a test tone";
   s->role = role;
   s->seed = 7;
   const size_t n = static_cast<size_t>(seconds * kSampleRate);
   for (size_t i = 0; i < n; ++i) {
      const double t = i / kSampleRate;
      const float v = static_cast<float>(0.8 * std::sin(2 * 3.14159265 * 220 * t) * std::exp(-t * 5));
      s->left.push_back(v);
      s->right.push_back(v);
   }
   return s;
}

// Runs `blocks` blocks of 256 and returns the left channel.
std::vector<float> run(jclap::core::TestHost &h, int blocks) {
   std::vector<float> all;
   std::vector<float> l(256), r(256);
   float *out[2] = {l.data(), r.data()};
   for (int b = 0; b < blocks; ++b) {
      h.process(256, nullptr, out);
      all.insert(all.end(), l.begin(), l.end());
   }
   return all;
}

float peak(const std::vector<float> &v, size_t from = 0, size_t to = SIZE_MAX) {
   float p = 0;
   for (size_t i = from; i < std::min(to, v.size()); ++i)
      p = std::max(p, std::fabs(v[i]));
   return p;
}

// The last sample louder than -60 dB.
size_t lastSound(const std::vector<float> &v) {
   size_t last = 0;
   for (size_t i = 0; i < v.size(); ++i)
      if (std::fabs(v[i]) > 0.001f)
         last = i;
   return last;
}

void testParamText() {
   jclap::core::TestHost h(clap_entry, kId);
   for (clap_id id = 0; id < kParamCount; ++id) {
      const auto &d = param(id);
      for (double f : {0.0, 0.13, 0.5, 0.77, 1.0}) {
         double v = d.min + (d.max - d.min) * f;
         if (d.flags & CLAP_PARAM_IS_STEPPED)
            v = std::round(v);
         const std::string a = h.paramText(id, v);
         double back = 0;
         JCLAP_CHECK(h.textToValue(id, a.c_str(), back));
         const std::string b = h.paramText(id, back);
         if (a != b)
            std::printf("param %u: %s -> %g -> %s\n", id, a.c_str(), back, b.c_str());
         JCLAP_CHECK(a == b);
      }
   }
   JCLAP_CHECK(h.paramText(padParam(0, kDecay), 1.0) == "Full");
   JCLAP_CHECK(h.paramText(padParam(0, kLowCut), 0.0) == "Off");
   JCLAP_CHECK(h.paramText(kVolume, -60.0) == "-inf dB");
}

void testPlaying() {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   JCLAP_CHECK(host != nullptr);
   // Empty: silence.
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(peak(run(h, 10)) < 1e-6f);
   host->setPadSample(0, tone());
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> a = run(h, 60);
   JCLAP_CHECK(peak(a) > 0.3f);
   // 0.2 s at 48 kHz is 9600 samples (and the master's 30 Hz low cut rings on a little).
   const size_t full = lastSound(a);
   JCLAP_CHECK(full > 9000 && full < 11500);
   // Note 37 is pad 2, which is empty.
   h.note(CLAP_EVENT_NOTE_ON, 37, 1.0);
   JCLAP_CHECK(peak(run(h, 10)) < 1e-6f);
   // MIDI note-ons play too.
   h.midi(0x90, 36, 100);
   JCLAP_CHECK(peak(run(h, 10)) > 0.3f);
   run(h, 60);

   // An octave up plays it in half the time.
   h.setParam(padParam(0, kPitch), 12.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> up = run(h, 60);
   JCLAP_CHECK(lastSound(up) > full * 0.4 && lastSound(up) < full * 0.6);
   h.setParam(padParam(0, kPitch), 0.0);

   // End at half.
   h.setParam(padParam(0, kEnd), 0.5);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> half = run(h, 60);
   JCLAP_CHECK(lastSound(half) > full * 0.4 && lastSound(half) < full * 0.6);
   h.setParam(padParam(0, kEnd), 1.0);

   // A short decay cuts it short.
   h.setParam(padParam(0, kDecay), decayValue(20.0));
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(lastSound(run(h, 60)) < 2000);
   h.setParam(padParam(0, kDecay), 1.0);

   // Velocity: soft is quieter.
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float loud = peak(run(h, 60));
   h.note(CLAP_EVENT_NOTE_ON, 36, 0.2);
   const float soft = peak(run(h, 60));
   JCLAP_CHECK(soft < loud * 0.6f && soft > 0.0f);

   // Level at -inf is silent.
   h.setParam(padParam(0, kLevel), -60.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(peak(run(h, 20)) < 1e-6f);
   h.setParam(padParam(0, kLevel), 0.0);

   // Gate: the note's end stops it; one shot plays on.
   h.setParam(padParam(0, kPlayMode), 1.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   run(h, 4);
   h.note(CLAP_EVENT_NOTE_OFF, 36, 0.0);
   const std::vector<float> gated = run(h, 40);
   JCLAP_CHECK(lastSound(gated) < 3000);
   h.setParam(padParam(0, kPlayMode), 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   run(h, 4);
   h.note(CLAP_EVENT_NOTE_OFF, 36, 0.0);
   JCLAP_CHECK(lastSound(run(h, 40)) > 6000);

   // Reverse starts quiet (the tail) and ends loud.
   h.setParam(padParam(0, kReverse), 1.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> rev = run(h, 40);
   JCLAP_CHECK(peak(rev, 0, 1000) < peak(rev, 8000, 9600));
   h.setParam(padParam(0, kReverse), 0.0);
   run(h, 20);
}

void testChokeAndSends() {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   host->setPadSample(0, tone(0.5));
   host->setPadSample(1, tone(0.5));
   h.setParam(padParam(0, kChoke), 1.0);
   h.setParam(padParam(1, kChoke), 1.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   run(h, 2);
   JCLAP_CHECK(host->padPosition(0) >= 0.0f);
   h.note(CLAP_EVENT_NOTE_ON, 37, 1.0);
   run(h, 4);
   // Pad 1 was cut off by pad 2 in the same group.
   JCLAP_CHECK(host->padPosition(0) < 0.0f);
   JCLAP_CHECK(host->padPosition(1) >= 0.0f);
   run(h, 120);

   // A reverb send rings on after the sound ends.
   h.setParam(padParam(0, kReverbSend), 1.0);
   h.setParam(kReverbReturn, 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const std::vector<float> wet = run(h, 200);
   JCLAP_CHECK(lastSound(wet) > 30000);
   h.setParam(padParam(0, kReverbSend), 0.0);
   run(h, 600);
   // Delay too.
   h.setParam(padParam(0, kDelaySend), 1.0);
   h.setParam(kDelayReturn, 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(lastSound(run(h, 200)) > 30000);
}

void testMaster() {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   host->setPadSample(0, tone());
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float base = peak(run(h, 60));
   h.setParam(kVolume, -12.0);
   run(h, 2);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   const float quiet = peak(run(h, 60));
   JCLAP_CHECK(std::fabs(quiet / base - 0.251f) < 0.03f);
   h.setParam(kVolume, 0.0);
   // Boom adds low end: a 220 Hz tone is near its edge, a 60 Hz one isn't.
   h.setParam(kBoom, 1.0);
   run(h, 2);
   h.note(CLAP_EVENT_NOTE_ON, 36, 1.0);
   JCLAP_CHECK(peak(run(h, 60)) > base * 1.05f);
}

void testState() {
   Buffer state;
   {
      jclap::core::TestHost h(clap_entry, kId);
      auto *host = h.object<EditorHost>();
      host->setPadSample(5, tone(0.3, kHat));
      host->setParam(padParam(5, kPitch), -5.0);
      host->setParam(kBoom, 0.4);
      host->gen().prompt = "a saved prompt";
      host->gen().role = kClap;
      host->setKitName("My Kit");
      auto *st = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
      JCLAP_CHECK(st->save(h.plugin(), &state.out));
   }
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   auto *st = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
   JCLAP_CHECK(st->load(h.plugin(), &state.in));
   const SamplePtr s = host->padSample(5);
   JCLAP_CHECK(s && s->frames() == static_cast<size_t>(0.3 * kSampleRate) && s->role == kHat && s->seed == 7);
   if (s) {
      const SamplePtr t = tone(0.3);
      float err = 0;
      for (size_t i = 0; i < s->frames(); ++i)
         err = std::max(err, std::fabs(s->left[i] - t->left[i]));
      JCLAP_CHECK(err < 1e-4f);
   }
   JCLAP_CHECK(!host->padSample(0));
   JCLAP_CHECK_EQ(h.paramValue(padParam(5, kPitch)), -5.0);
   JCLAP_CHECK_EQ(h.paramValue(kBoom), 0.4);
   JCLAP_CHECK(host->gen().prompt == "a saved prompt" && host->gen().role == kClap);
   JCLAP_CHECK(host->kitName() == "My Kit");
   // A truncated state is refused.
   Buffer bad;
   bad.bytes.assign(state.bytes.begin(), state.bytes.begin() + state.bytes.size() / 2);
   JCLAP_CHECK(!st->load(h.plugin(), &bad.in));

   // Swapping pads moves the sounds and their params.
   host->setParam(padParam(5, kLevel), -6.0);
   host->swapPads(5, 9);
   JCLAP_CHECK(!host->padSample(5) && host->padSample(9));
   JCLAP_CHECK_EQ(host->paramValue(padParam(9, kLevel)), -6.0);
   JCLAP_CHECK_EQ(host->paramValue(padParam(5, kLevel)), 0.0);
   JCLAP_CHECK_EQ(host->paramValue(padParam(9, kPitch)), -5.0);
}

void testTreatment() {
   Sample s;
   // 0.1 s of silence, then a tone at half scale, then silence.
   const size_t pre = 4410;
   for (size_t i = 0; i < 4410 * 4; ++i) {
      const float v = i >= pre && i < pre * 3 ? 0.5f * static_cast<float>(std::sin(i * 0.05)) : 0.0f;
      s.left.push_back(v);
      s.right.push_back(v);
   }
   Treatment t;
   t.normalize = false;
   t.trimSilence = true;
   Sample out = treat(s, t);
   // The silence is gone, with 2 ms kept before the start.
   JCLAP_CHECK(out.frames() > pre * 2 && out.frames() < pre * 2 + 200);
   t.normalize = true;
   out = treat(s, t);
   JCLAP_CHECK(std::fabs(peakOf(out) - 0.966f) < 0.01f);
   // Start and end as fractions.
   t.trimSilence = false;
   t.start = 0.25;
   t.end = 0.5;
   out = treat(s, t);
   JCLAP_CHECK_EQ(out.frames(), pre);
   // A fade ends at zero.
   t.fadeMs = 20;
   out = treat(s, t);
   JCLAP_CHECK(std::fabs(out.left.back()) < 1e-3f);
   // Reverse.
   t = Treatment{};
   t.normalize = t.trimSilence = false;
   t.reverse = true;
   out = treat(s, t);
   JCLAP_CHECK(out.left.front() == s.left.back() && out.left.back() == s.left.front());

   // A kit's sounds survive writing and reading.
   SamplePtr pads[16];
   pads[3] = tone(0.1, kSnare);
   const std::string bytes = writeKit(pads);
   SamplePtr back[16];
   JCLAP_CHECK(readKit(bytes, back));
   JCLAP_CHECK(back[3] && back[3]->frames() == pads[3]->frames() && back[3]->role == kSnare && !back[0]);
   JCLAP_CHECK(!readKit(bytes.substr(0, bytes.size() - 10), back));
}

} // namespace

int main() {
   testParamText();
   testPlaying();
   testChokeAndSends();
   testMaster();
   testState();
   testTreatment();
   return jclap::core::testResult();
}
