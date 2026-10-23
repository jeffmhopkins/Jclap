// Drives the synth the way a host does: CLAP notes and expressions, MPE MIDI, the panel's notes,
// and checks the pitch, voices and note-end reports that come back.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "editor.h"
#include "params.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace basic_synth;

namespace {

constexpr uint32_t kBlock = 1024;

struct Run {
   jclap::core::TestHost &host;
   std::vector<float> left, right;
   std::vector<clap_event_note_t> noteEnds;

   // Runs `frames` frames in blocks, keeping the left channel and every note end.
   void operator()(uint32_t frames) {
      left.clear();
      right.clear();
      float silentIn[kBlock] = {}, outL[kBlock], outR[kBlock];
      float *in[2] = {silentIn, silentIn}, *out[2] = {outL, outR};
      while (frames > 0) {
         const uint32_t n = std::min(frames, kBlock);
         const auto result = host.process(n, in, out);
         noteEnds.insert(noteEnds.end(), result.noteEnds.begin(), result.noteEnds.end());
         left.insert(left.end(), outL, outL + n);
         right.insert(right.end(), outR, outR + n);
         frames -= n;
      }
   }

   double rms() const {
      double sum = 0.0;
      for (float s : left)
         sum += s * s;
      return left.empty() ? 0.0 : std::sqrt(sum / left.size());
   }

   // Frequency from rising zero crossings, for a sine.
   double frequency() const {
      int first = -1, last = -1, count = 0;
      for (size_t i = 1; i < left.size(); ++i) {
         if (left[i - 1] <= 0.0f && left[i] > 0.0f) {
            if (first < 0)
               first = static_cast<int>(i);
            last = static_cast<int>(i);
            ++count;
         }
      }
      return count > 1 ? (count - 1) * 48000.0 / (last - first) : 0.0;
   }
};

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

} // namespace

int main() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.basic-synth");
   EditorHost &panel = *h.object<EditorHost>();
   Run run{h, {}, {}, {}};
   std::vector<EditorHost::Voice> voices;

   // Silent until a note arrives.
   run(4800);
   JCLAP_CHECK_EQ(run.rms(), 0.0);

   // A plain sine with the filter wide open, so pitch is easy to measure.
   h.setParam(kWave, 0.0);
   h.setParam(kCutoff, 1.0);
   h.setParam(kEnvAmount, 0.0);
   h.setParam(kPressureAmount, 0.0);
   h.setParam(kTimbreAmount, 0.0);
   h.setParam(kSustain, 1.0);
   h.setParam(kAttack, 1.0);
   h.setParam(kRelease, 50.0);
   h.note(CLAP_EVENT_NOTE_ON, 69, 1.0, 7);
   run(4800);
   run(48000);
   JCLAP_CHECK(run.rms() > 0.05);
   JCLAP_CHECK(near(run.frequency(), 440.0, 0.5));
   JCLAP_CHECK(run.left == run.right);

   // A CLAP tuning expression on that note id bends it up an octave.
   h.noteExpression(CLAP_NOTE_EXPRESSION_TUNING, -1, 12.0, 7);
   run(4800);
   run(48000);
   JCLAP_CHECK(near(run.frequency(), 880.0, 1.0));
   panel.voices(voices);
   JCLAP_CHECK(voices.size() == 1 && voices[0].note == 69 && voices[0].bend == 12.0f);
   // Pressure and brightness show on the panel too.
   h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 69, 0.75);
   h.noteExpression(CLAP_NOTE_EXPRESSION_BRIGHTNESS, 69, 0.25);
   run(64);
   panel.voices(voices);
   JCLAP_CHECK(voices.size() == 1 && voices[0].pressure == 0.75f && voices[0].timbre == 0.25f);

   // Tune transposes.
   h.setParam(kTune, -12.0);
   run(4800);
   run(48000);
   JCLAP_CHECK(near(run.frequency(), 440.0, 0.5));
   h.setParam(kTune, 0.0);

   // Note off releases; once silent, the host hears that note id ended.
   run.noteEnds.clear();
   h.note(CLAP_EVENT_NOTE_OFF, 69, 0.0, 7);
   run(9600);
   JCLAP_CHECK(run.left.back() == 0.0f);
   JCLAP_CHECK(run.noteEnds.size() == 1 && run.noteEnds[0].note_id == 7 && run.noteEnds[0].key == 69);
   panel.voices(voices);
   JCLAP_CHECK(voices.empty());

   // Eight voices: a ninth note steals the oldest, which is reported ended.
   run.noteEnds.clear();
   for (int i = 0; i < 9; ++i)
      h.note(CLAP_EVENT_NOTE_ON, static_cast<int16_t>(48 + i), 0.8, 100 + i);
   run(256);
   panel.voices(voices);
   JCLAP_CHECK_EQ(voices.size(), 8u);
   JCLAP_CHECK(run.noteEnds.size() == 1 && run.noteEnds[0].note_id == 100);
   // Choke cuts them all at once, by wildcard.
   h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
   run(256);
   panel.voices(voices);
   JCLAP_CHECK(voices.empty());
   JCLAP_CHECK_EQ(run.noteEnds.size(), 9u);
   run(4800);
   JCLAP_CHECK_EQ(run.rms(), 0.0);

   // MPE over MIDI: each note on its own channel, bent by that channel alone.
   h.setParam(kBendRange, 48.0);
   h.midi(0x91, 60, 100); // channel 2
   h.midi(0x92, 67, 100); // channel 3
   h.midi(0xE1, 0, 96);   // channel 2 bend: (96 << 7 - 8192) / 8192 = +0.5, so +24 st
   h.midi(0xD1, 64, 0);   // channel 2 pressure
   h.midi(0xB2, 74, 127); // channel 3 timbre
   run(512);
   panel.voices(voices);
   JCLAP_CHECK_EQ(voices.size(), 2u);
   for (const auto &v : voices) {
      if (v.note == 60) {
         JCLAP_CHECK(near(v.bend, 24.0, 1e-4) && near(v.pressure, 64 / 127.0, 1e-4) && v.timbre == 0.5f);
      } else {
         JCLAP_CHECK(v.note == 67 && v.bend == 0.0f && v.timbre == 1.0f);
      }
   }
   // The manager channel (1) bends every note by up to 2 semitones.
   h.midi(0xE0, 0, 96);
   run(64);
   panel.voices(voices);
   for (const auto &v : voices)
      JCLAP_CHECK(near(v.bend, v.note == 60 ? 25.0 : 1.0, 1e-4));
   // Note off on the right channel releases only that note.
   h.midi(0x82, 67, 0);
   run(64);
   panel.voices(voices);
   JCLAP_CHECK(voices.size() == 1 && voices[0].note == 60);
   h.midi(0xB0, 123, 0); // all notes off
   h.midi(0xE0, 0, 64);
   h.midi(0xE1, 0, 64);
   run(9600);
   panel.voices(voices);
   JCLAP_CHECK(voices.empty());

   // Plain MIDI on channel 1 plays at the right pitch.
   h.midi(0x90, 57, 100);
   run(4800);
   run(48000);
   JCLAP_CHECK(near(run.frequency(), 220.0, 0.5));
   h.midi(0x90, 57, 0); // note on at velocity 0 is a note off
   run(9600);
   JCLAP_CHECK(run.left.back() == 0.0f);

   // The panel's keyboard and grid play it, with their own expression.
   panel.noteOn(64, 1.0f);
   panel.noteExpression(64, -1.5f, 0.4f, 0.6f);
   run(256);
   JCLAP_CHECK(run.rms() > 0.01);
   panel.voices(voices);
   JCLAP_CHECK(voices.size() == 1 && voices[0].note == 64 && voices[0].bend == -1.5f && voices[0].pressure == 0.4f);
   panel.noteOff(64);
   run(9600);
   panel.voices(voices);
   JCLAP_CHECK(voices.empty());
   float scope[64];
   panel.readScope(scope, 64);

   // Every wave, filter mode and slope makes a sound and stays finite, with resonance up.
   h.setParam(kResonance, 1.0);
   h.setParam(kCutoff, 0.5);
   h.setParam(kEnvAmount, 3.0);
   for (int wave = 0; wave < 5; ++wave)
      for (int mode = 0; mode < 3; ++mode)
         for (int slope = 0; slope < 4; ++slope) {
            h.setParam(kWave, wave);
            h.setParam(kFilterMode, mode);
            h.setParam(kSlope, slope);
            h.note(CLAP_EVENT_NOTE_ON, 45, 1.0);
            run(2048);
            const double level = run.rms();
            // Quiet is fine (a low sine through a steep band pass far above it is nearly silent).
            JCLAP_CHECK(std::isfinite(level) && level > 1e-6 && level < 4.0);
            h.note(CLAP_EVENT_NOTE_CHOKE, 45, 0.0);
            run(64);
         }

   // Param text.
   JCLAP_CHECK(h.paramText(kCutoff, cutoffValue(1000.0)) == "1.00 kHz");
   JCLAP_CHECK(h.paramText(kCutoff, cutoffValue(250.0)) == "250 Hz");
   double v = 0;
   JCLAP_CHECK(h.textToValue(kCutoff, "2.5 kHz", v) && near(cutoffHz(v), 2500.0, 1e-6));
   JCLAP_CHECK(h.paramText(kWave, 4.0) == "Pulse");
   JCLAP_CHECK(h.paramText(kSlope, 3.0) == "48 dB");
   JCLAP_CHECK(h.paramText(kEnvAmount, -1.5) == "-1.5 oct");
   // Every param's text reads back as the same value, as hosts (and clap-validator) check.
   for (clap_id id = 0; id < kParamCount; ++id) {
      for (double value : {kParams[id].min, kParams[id].defaultValue, kParams[id].max}) {
         double back = -999;
         JCLAP_CHECK(h.textToValue(id, h.paramText(id, value).c_str(), back));
         JCLAP_CHECK(h.paramText(id, back) == h.paramText(id, value));
      }
   }

   return jclap::core::testResult();
}
