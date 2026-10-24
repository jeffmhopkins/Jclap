// Drives the synth the way a host does: CLAP notes and expressions, MPE MIDI, the panel's notes,
// and checks the pitch, voices and note-end reports that come back.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <clap/ext/state.h>

#include <cmath>
#include <cstring>

#include <vector>

#include <jclap/ui/keyboard.h>
#include <jclap/ui/note_grid.h>

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

   // The amplitude of one frequency, by correlating with a sine and cosine at it.
   double magnitude(double hz) const {
      double re = 0.0, im = 0.0;
      for (size_t i = 0; i < left.size(); ++i) {
         const double phase = 2.0 * 3.141592653589793 * hz * i / 48000.0;
         re += left[i] * std::cos(phase);
         im += left[i] * std::sin(phase);
      }
      return left.empty() ? 0.0 : 2.0 * std::sqrt(re * re + im * im) / left.size();
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

// A byte buffer the plugin's state extension writes to and reads from.
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
   h.setParam(kEnv2ToFilter, 0.0);
   h.setParam(kPressureAmount, 0.0);
   h.setParam(kTimbreAmount, 0.0);
   h.setParam(kEnv1Sustain, 1.0);
   h.setParam(kEnv1Attack, 1.0);
   h.setParam(kEnv1Release, 50.0);
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
   h.setParam(kEnv2ToFilter, 3.0);
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

   // ---- host automation and modulation ----
   h.setParam(kResonance, 0.0);
   h.setParam(kCutoff, 1.0);
   h.setParam(kEnv2ToFilter, 0.0);
   h.setParam(kWave, 0.0);
   h.setParam(kFilterMode, 0.0);
   h.setParam(kSlope, 1.0);
   // Every param can be automated and modulated, and all but bend range and the loop switches per
   // voice.
   auto *params = static_cast<const clap_plugin_params_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_PARAMS));
   for (uint32_t i = 0; i < kParamCount; ++i) {
      clap_param_info_t info{};
      JCLAP_CHECK(params->get_info(h.plugin(), i, &info));
      JCLAP_CHECK((info.flags & CLAP_PARAM_IS_AUTOMATABLE) && (info.flags & CLAP_PARAM_IS_MODULATABLE));
      JCLAP_CHECK(i == kBendRange || i == kEnv1Loop || i == kEnv2Loop ||
                  (info.flags & CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID));
   }
   // A global modulation moves every voice, and the knobs show it.
   h.modulate(kTune, 12.0);
   h.note(CLAP_EVENT_NOTE_ON, 57, 1.0, 1);
   run(4800);
   run(48000);
   JCLAP_CHECK(near(run.frequency(), 440.0, 0.5));
   JCLAP_CHECK_EQ(panel.paramModulated(kTune), 12.0);
   JCLAP_CHECK_EQ(panel.paramValue(kTune), 0.0);
   h.modulate(kTune, 0.0);
   h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
   run(64);
   // A per-voice modulation moves only the note it names, and a new note starts without it.
   h.note(CLAP_EVENT_NOTE_ON, 57, 1.0, 2);
   h.modulateNote(kTune, 12.0, 2);
   run(4800);
   run(48000);
   JCLAP_CHECK(near(run.frequency(), 440.0, 0.5));
   h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
   h.note(CLAP_EVENT_NOTE_ON, 57, 1.0, 3);
   run(4800);
   run(48000);
   JCLAP_CHECK(near(run.frequency(), 220.0, 0.5));
   h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
   run(64);

   // Env 1 to VCA: at full the VCA follows the envelope down to sustain 0; at 0 it just follows
   // the gate, so the note holds at full level.
   h.setParam(kEnv1Decay, 20.0);
   h.setParam(kEnv1Sustain, 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 57, 1.0);
   run(9600);
   run(4800);
   JCLAP_CHECK(run.rms() < 1e-3);
   h.setParam(kEnv1ToVca, 0.0);
   run(4800);
   run(4800);
   JCLAP_CHECK(run.rms() > 0.05);
   // and closes on release.
   h.note(CLAP_EVENT_NOTE_OFF, 57, 0.0);
   run(4800);
   run(4800);
   JCLAP_CHECK(run.rms() < 1e-3);
   JCLAP_CHECK(h.paramText(kEnv1ToVca, 0.5) == "50%");

   // Env 2 to pitch: held at sustain 1, +12 st doubles the frequency.
   h.setParam(kEnv1ToVca, 1.0);
   h.setParam(kEnv1Sustain, 1.0);
   h.setParam(kEnv2Sustain, 1.0);
   h.setParam(kEnv2Attack, 1.0);
   h.setParam(kEnv2ToFilter, 0.0);
   h.setParam(kEnv2ToPitch, 12.0);
   h.note(CLAP_EVENT_NOTE_ON, 57, 1.0);
   run(4800);
   run(48000);
   JCLAP_CHECK(near(run.frequency(), 440.0, 0.5));
   h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
   h.setParam(kEnv2ToPitch, 0.0);
   run(64);
   // A looping Env 1 on the VCA pulses the note: loud at each peak, quiet between.
   h.setParam(kEnv1Loop, 1.0);
   h.setParam(kEnv1Attack, 1.0);
   h.setParam(kEnv1Decay, 50.0);
   h.setParam(kEnv1Sustain, 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 57, 1.0);
   run(48000);
   int pulses = 0;
   bool loud = false;
   for (size_t i = 0; i + 480 <= run.left.size(); i += 480) {
      float peak = 0.0f;
      for (size_t j = i; j < i + 480; ++j)
         peak = std::max(peak, std::fabs(run.left[j]));
      pulses += !loud && peak > 0.05f;
      loud = peak > 0.05f;
   }
   JCLAP_CHECK(pulses >= 10);
   h.note(CLAP_EVENT_NOTE_OFF, 57, 0.0);
   run(19200);
   JCLAP_CHECK(run.rms() < 1e-3);
   // It is one shared cycle: notes started at different times show a single playhead, and with no
   // note held it keeps running.
   {
      std::vector<EditorHost::EnvState> states;
      panel.envelopes(0, states);
      JCLAP_CHECK_EQ(states.size(), 1u);
      h.note(CLAP_EVENT_NOTE_ON, 60, 1.0);
      run(700);
      h.note(CLAP_EVENT_NOTE_ON, 64, 1.0);
      run(300);
      panel.envelopes(0, states);
      JCLAP_CHECK_EQ(states.size(), 1u);
      h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
      run(64);
   }
   // Switching the loop on restarts the cycle from its attack.
   h.setParam(kEnv1Loop, 0.0);
   run(64);
   h.setParam(kEnv1Attack, 200.0);
   h.setParam(kEnv1Loop, 1.0);
   run(480);
   {
      std::vector<EditorHost::EnvState> states;
      panel.envelopes(0, states);
      JCLAP_CHECK(states.size() == 1 && states[0].stage == 0 && states[0].level < 0.2f);
   }
   h.setParam(kEnv1Attack, 1.0);
   h.setParam(kEnv1Loop, 0.0);
   JCLAP_CHECK(h.paramText(kEnv1Loop, 1.0) == "On");
   JCLAP_CHECK(h.paramText(kEnv2ToPitch, -7.0) == "-7.0 st");

   // The panel sees every sounding voice's envelopes: two notes, two playheads each, and a
   // released note's release start.
   {
      h.setParam(kEnv1Attack, 1.0);
      h.setParam(kEnv1Release, 500.0);
      h.setParam(kEnv1Sustain, 1.0);
      h.note(CLAP_EVENT_NOTE_ON, 60, 1.0);
      h.note(CLAP_EVENT_NOTE_ON, 64, 1.0);
      run(4800);
      std::vector<EditorHost::EnvState> states;
      panel.envelopes(0, states);
      JCLAP_CHECK_EQ(states.size(), 2u);
      panel.envelopes(1, states);
      JCLAP_CHECK_EQ(states.size(), 2u);
      h.note(CLAP_EVENT_NOTE_OFF, 64, 0.0);
      run(480);
      panel.envelopes(0, states);
      int releasing = 0;
      for (const auto &e : states)
         releasing += e.stage == 3 && e.releaseFrom > e.level && e.level > 0.0f;
      JCLAP_CHECK_EQ(releasing, 1);
      h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
      run(64);
      panel.envelopes(0, states);
      JCLAP_CHECK(states.empty());
      h.setParam(kEnv1Release, 50.0);
   }

   // Key track: a high sine through a low cutoff comes through louder once the cutoff follows it.
   {
      const double cutoff = h.paramValue(kCutoff);
      h.setParam(kCutoff, cutoffValue(200.0));
      const auto level = [&] {
         h.note(CLAP_EVENT_NOTE_ON, 81, 1.0);
         run(4800);
         run(4800);
         const double rms = run.rms();
         h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
         run(64);
         return rms;
      };
      const double fixed = level();
      h.setParam(kKeyTrack, 1.0);
      const double tracked = level();
      JCLAP_CHECK(tracked > 4.0 * fixed);
      JCLAP_CHECK(h.paramText(kKeyTrack, 0.5) == "50%");
      h.setParam(kCutoff, cutoff);
      h.setParam(kKeyTrack, 0.0);
   }

   // Velocity: on the VCA by its own amount (all of it by default), and on the filter only when asked.
   {
      const auto level = [&](double velocity) {
         h.note(CLAP_EVENT_NOTE_ON, 81, velocity);
         run(4800);
         run(4800);
         const double rms = run.rms();
         h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
         run(64);
         return rms;
      };
      JCLAP_CHECK_EQ(kParams[kVelocityToVca].defaultValue, 1.0);
      const double hard = level(1.0);
      JCLAP_CHECK(near(level(0.25) / hard, 0.25, 0.02));
      h.setParam(kVelocityToVca, 0.5);
      JCLAP_CHECK(near(level(0.25) / hard, 0.625, 0.02));
      h.setParam(kVelocityToVca, 0.0);
      JCLAP_CHECK(near(level(0.25) / hard, 1.0, 0.02));
      // A high sine through a low cutoff, as for key track: harder notes open it further.
      const double cutoff = h.paramValue(kCutoff);
      h.setParam(kCutoff, cutoffValue(200.0));
      const double closed = level(1.0);
      h.setParam(kVelocityToFilter, 4.0);
      JCLAP_CHECK(level(1.0) > 4.0 * closed);
      JCLAP_CHECK(level(0.01) < 1.5 * closed);
      JCLAP_CHECK(h.paramText(kVelocityToFilter, 1.5) == "1.5 oct");
      JCLAP_CHECK(h.paramText(kVelocityToVca, 0.5) == "50%");
      h.setParam(kCutoff, cutoff);
      h.setParam(kVelocityToFilter, 0.0);
      h.setParam(kVelocityToVca, 1.0);
   }

   // LFO to pulse width: a slow square swings a 30% pulse to 75% and then to 5%, which changes how
   // much second harmonic there is; with no depth both halves sound the same.
   {
      h.setParam(kWave, 4.0);
      h.setParam(kPulseWidth, 0.3);
      h.setParam(kLfoShape, 3.0);
      h.setParam(kLfoRate, lfoValue(2.0));
      const auto halves = [&](double &first, double &second) {
         h.note(CLAP_EVENT_NOTE_ON, 57, 1.0);
         run(1000);
         run(8000); // most of the first half
         first = run.magnitude(440.0);
         run(4000);
         run(8000); // most of the second
         second = run.magnitude(440.0);
         h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
         run(64);
      };
      double first = 0.0, second = 0.0;
      halves(first, second);
      JCLAP_CHECK(near(first, second, 0.1 * first));
      h.setParam(kLfoToPulseWidth, 1.0);
      halves(first, second);
      JCLAP_CHECK(first > 2.0 * second);
      JCLAP_CHECK(h.paramText(kLfoToPulseWidth, 0.4) == "40%");
      h.setParam(kLfoToPulseWidth, 0.0);
      h.setParam(kLfoShape, kParams[kLfoShape].defaultValue);
      h.setParam(kLfoRate, kParams[kLfoRate].defaultValue);
      h.setParam(kPulseWidth, kParams[kPulseWidth].defaultValue);
      h.setParam(kWave, 0.0);
   }

   // Dual: a second oscillator detuned from the first. A few cents changes the sound but not the
   // pitch; at the top of the knob the second is an octave up.
   {
      h.note(CLAP_EVENT_NOTE_ON, 57, 1.0);
      run(4800);
      run(4800);
      const std::vector<float> single = run.left;
      h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
      run(64);
      h.setParam(kDual, 1.0);
      h.setParam(kDetune, detuneValue(0.1));
      h.note(CLAP_EVENT_NOTE_ON, 57, 1.0);
      run(4800);
      run(48000);
      JCLAP_CHECK(near(run.frequency(), 220.0, 1.5));
      JCLAP_CHECK(run.rms() > 0.03);
      JCLAP_CHECK(std::vector<float>(run.left.begin(), run.left.begin() + 4800) != single);
      h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
      run(64);
      h.setParam(kDetune, 1.0);
      h.setParam(kWave, 0.0); // sine
      h.note(CLAP_EVENT_NOTE_ON, 57, 1.0);
      run(4800);
      run(48000);
      // Two sines, at the note and an octave up, about equally loud.
      const double at220 = run.magnitude(220.0), at440 = run.magnitude(440.0);
      JCLAP_CHECK(at220 > 0.01 && near(at440 / at220, 1.0, 0.2));
      h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
      run(64);
      h.setParam(kWave, kParams[kWave].defaultValue);
      h.setParam(kDual, 0.0);
      JCLAP_CHECK(near(detuneSemitones(1.0), 12.0, 1e-9) && near(detuneSemitones(-1.0), -12.0, 1e-9));
      JCLAP_CHECK(h.paramText(kDetune, detuneValue(0.1)) == "+10 ct");
      JCLAP_CHECK(h.paramText(kDetune, -1.0) == "-12.00 st");
      double d = 0.0;
      JCLAP_CHECK(h.textToValue(kDetune, "-7 ct", d) && near(detuneSemitones(d), -0.07, 1e-9));
      JCLAP_CHECK(h.textToValue(kDetune, "7", d) && near(detuneSemitones(d), 7.0, 1e-9));
   }

   // The LFO: a slow square at full tremolo depth turns the note on and off, and the panel sees it.
   h.setParam(kEnv1ToVca, 1.0);
   h.setParam(kEnv1Sustain, 1.0);
   h.setParam(kLfoShape, 3.0);
   h.setParam(kLfoRate, lfoValue(2.0));
   h.setParam(kLfoToVca, 1.0);
   h.note(CLAP_EVENT_NOTE_ON, 57, 1.0);
   run(6000); // the first quarter cycle, high
   const double high = run.rms();
   float lfo = 0.0f;
   JCLAP_CHECK(panel.lfo(lfo) && lfo == 1.0f);
   run(6000);
   run(11000); // most of the second half, low
   JCLAP_CHECK(high > 0.05 && run.rms() < 0.01);
   JCLAP_CHECK(panel.lfo(lfo) && lfo == -1.0f);
   h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
   run(64);
   JCLAP_CHECK(!panel.lfo(lfo));
   h.setParam(kLfoToVca, 0.0);
   JCLAP_CHECK(h.paramText(kLfoRate, lfoValue(2.0)) == "2.00 Hz");
   JCLAP_CHECK(h.paramText(kLfoRate, 0.0) == "0.050 Hz");
   JCLAP_CHECK(h.paramText(kLfoShape, 4.0) == "Random");

   // Param text.
   JCLAP_CHECK(h.paramText(kCutoff, cutoffValue(1000.0)) == "1.00 kHz");
   JCLAP_CHECK(h.paramText(kCutoff, cutoffValue(250.0)) == "250 Hz");
   double v = 0;
   JCLAP_CHECK(h.textToValue(kCutoff, "2.5 kHz", v) && near(cutoffHz(v), 2500.0, 1e-6));
   JCLAP_CHECK(h.paramText(kWave, 4.0) == "Pulse");
   JCLAP_CHECK(h.paramText(kSlope, 3.0) == "48 dB");
   JCLAP_CHECK(h.paramText(kEnv2ToFilter, -1.5) == "-1.5 oct");
   // Every param's text reads back as the same value, as hosts (and clap-validator) check.
   for (clap_id id = 0; id < kParamCount; ++id) {
      for (double value : {kParams[id].min, kParams[id].defaultValue, kParams[id].max}) {
         double back = -999;
         JCLAP_CHECK(h.textToValue(id, h.paramText(id, value).c_str(), back));
         JCLAP_CHECK(h.paramText(id, back) == h.paramText(id, value));
      }
   }

   // The panel's notes are its own: playing and letting go of a key the host also holds leaves the
   // host's note sounding, and the host hears no NOTE_END for a note it never sent.
   {
      run.noteEnds.clear();
      h.note(CLAP_EVENT_NOTE_ON, 60, 1.0, 77);
      run(480);
      panel.noteOn(60, 1.0f);
      run(480);
      panel.noteOff(60);
      run(48000);
      std::vector<EditorHost::Voice> held;
      panel.voices(held);
      JCLAP_CHECK(held.size() == 1 && held[0].note == 60);
      JCLAP_CHECK(run.noteEnds.empty());
      h.note(CLAP_EVENT_NOTE_OFF, 60, 0.0, 77);
      run(48000);
      JCLAP_CHECK(run.noteEnds.size() == 1 && run.noteEnds[0].note_id == 77);
   }

   // A looping envelope keeps time at other sample rates: at 96 kHz a 200 ms attack is still
   // rising 150 ms in.
   {
      jclap::core::TestHost h96(clap_entry, "com.jeffmhopkins.jclap.basic-synth", 96000);
      Run run96{h96, {}, {}, {}};
      h96.setParam(kEnv1Attack, 200.0);
      h96.setParam(kEnv1Loop, 1.0);
      run96(64);
      run96(14400);
      std::vector<EditorHost::EnvState> states;
      h96.object<EditorHost>()->envelopes(0, states);
      JCLAP_CHECK(states.size() == 1 && states[0].stage == 0);
   }

   // State: a round trip, and a state with fewer params (an older version) loads them and leaves
   // the rest at their defaults.
   {
      auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
      h.setParam(kResonance, 0.8);
      h.setParam(kTimbreAmount, 1.0);
      run(64);
      Buffer saved;
      JCLAP_CHECK(state->save(h.plugin(), &saved.out));
      JCLAP_CHECK_EQ(saved.bytes.size(), 12u + 8u * kParamCount);
      h.setParam(kResonance, 0.1);
      run(64);
      JCLAP_CHECK(state->load(h.plugin(), &saved.in));
      JCLAP_CHECK(near(h.paramValue(kResonance), 0.8, 1e-12));

      Buffer older;
      older.bytes = saved.bytes;
      const uint32_t count = kTimbreAmount;
      std::memcpy(older.bytes.data() + 8, &count, 4);
      older.bytes.resize(12 + 8 * count);
      JCLAP_CHECK(state->load(h.plugin(), &older.in));
      JCLAP_CHECK(near(h.paramValue(kResonance), 0.8, 1e-12));
      JCLAP_CHECK(near(h.paramValue(kTimbreAmount), kParams[kTimbreAmount].defaultValue, 1e-12));

      Buffer cut;
      cut.bytes.assign(saved.bytes.begin(), saved.bytes.end() - 4);
      JCLAP_CHECK(!state->load(h.plugin(), &cut.in));
   }

   // A NaN from the host is ignored rather than silencing the synth.
   {
      h.setParam(kCutoff, std::nan(""));
      run(64);
      JCLAP_CHECK(std::isfinite(h.paramValue(kCutoff)));
      h.modulate(kCutoff, std::nan(""));
      h.note(CLAP_EVENT_NOTE_ON, 60, 1.0);
      run(4800);
      JCLAP_CHECK(run.rms() > 0.01);
      h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
      run(64);
   }

   // The panel's keyboard and grid move by octaves and stop at the ends of the MIDI range.
   {
      jclap::ui::PianoKeyboard keys(36, 84);
      JCLAP_CHECK(keys.shiftOctaves(1) && keys.lowNote() == 48 && keys.highNote() == 96);
      JCLAP_CHECK(keys.shiftOctaves(5) && keys.highNote() <= 127 && keys.highNote() - keys.lowNote() == 48);
      JCLAP_CHECK(!keys.shiftOctaves(1));
      jclap::ui::NoteGrid grid(16, 5, 40, 5);
      JCLAP_CHECK(grid.highNote() == 40 + 15 + 4 * 5);
      grid.setRowInterval(12);
      JCLAP_CHECK(grid.highNote() == 40 + 15 + 4 * 12);
      JCLAP_CHECK(grid.shiftOctaves(-5) && grid.lowNote() == 4);
      JCLAP_CHECK(jclap::ui::noteName(60) == "C4" && jclap::ui::noteName(37) == "C#2" && jclap::ui::noteName(0) == "C-1");
   }

   return jclap::core::testResult();
}
