// Drives JBreath the way a host does: CLAP notes and expressions, MPE MIDI and a breath controller,
// the mono modes, voice stealing, state and params, at several sample rates.

#include <jclap/core/test_host.h>

#include <clap/ext/state.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "editor.h"
#include "params.h"
#include "presets.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace jbreath;

namespace {

constexpr uint32_t kBlock = 512;
constexpr const char *kId = "com.jeffmhopkins.jclap.jbreath";

struct Run {
   jclap::core::TestHost &host;
   double rate = 48000.0;
   std::vector<float> left, right;
   std::vector<clap_event_note_t> noteEnds;

   // Runs `frames` frames in blocks, keeping both channels and every note end.
   void operator()(uint32_t frames) {
      left.clear();
      right.clear();
      float outL[kBlock], outR[kBlock];
      float *out[2] = {outL, outR};
      while (frames > 0) {
         const uint32_t n = std::min(frames, kBlock);
         const auto result = host.process(n, nullptr, out);
         noteEnds.insert(noteEnds.end(), result.noteEnds.begin(), result.noteEnds.end());
         left.insert(left.end(), outL, outL + n);
         right.insert(right.end(), outR, outR + n);
         frames -= n;
      }
   }
   void seconds(double s) { (*this)(static_cast<uint32_t>(s * rate)); }

   double rms() const {
      double sum = 0.0;
      for (float s : left)
         sum += s * s;
      return left.empty() ? 0.0 : std::sqrt(sum / left.size());
   }
   bool finite() const {
      return std::all_of(left.begin(), left.end(), [](float v) { return std::isfinite(v); });
   }

   // The pitch near `hz`, by normalised autocorrelation over the last 0.2 s.
   double pitch(double hz) const {
      const size_t n = std::min(left.size(), static_cast<size_t>(0.2 * rate));
      const float *x = left.data() + left.size() - n;
      const int lo = static_cast<int>(rate / (hz * 1.3)), hi = static_cast<int>(rate / (hz / 1.3));
      const int m = static_cast<int>(n) - hi - 2;
      if (m <= 0)
         return 0.0;
      std::vector<double> r(hi + 2, 0.0);
      int best = lo;
      for (int L = lo - 1; L <= hi + 1; ++L) {
         double s = 0, e0 = 0, e1 = 0;
         for (int i = 0; i < m; ++i) {
            s += x[i] * x[i + L];
            e0 += x[i] * x[i];
            e1 += x[i + L] * x[i + L];
         }
         r[L] = s / std::sqrt(e0 * e1 + 1e-20);
         if (L >= lo && L <= hi && r[L] > r[best])
            best = L;
      }
      const double a = r[best - 1], b = r[best], c = r[best + 1], den = a - 2 * b + c;
      return rate / (best + (den != 0 ? 0.5 * (a - c) / den : 0.0));
   }
};

double cents(double got, double want) { return got > 0 ? 1200.0 * std::log2(got / want) : 9999.0; }
double hzOf(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

int sounding(EditorHost &panel) {
   std::vector<EditorHost::Voice> v;
   panel.voices(v);
   return static_cast<int>(std::count_if(v.begin(), v.end(), [](const EditorHost::Voice &x) { return x.note >= 0; }));
}

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

// The voices' own sound: no room, and every note in the middle (the output stage has its own test).
void dry(jclap::core::TestHost &h) {
   h.setParam(kRoom, 0.0);
   h.setParam(kSpread, 0.0);
}

double energy(const std::vector<float> &x) {
   double e = 0.0;
   for (float v : x)
      e += v * v;
   return e;
}

// Every instrument plays a held note in tune and at a sensible level, at this rate.
void playsInTune(double rate) {
   jclap::core::TestHost h(clap_entry, kId, rate);
   Run run{h, rate};
   dry(h);
   for (int m = 0; m < 5; ++m) {
      h.setParam(kModel, m);
      h.setParam(kVibDepth, 0.0);
      const int key = m == 0 ? 72 : m == 2 ? 55 : 64;
      h.note(CLAP_EVENT_NOTE_ON, key, 0.75, 100 + m);
      run.seconds(0.6);
      const double c = cents(run.pitch(hzOf(key)), hzOf(key));
      const double db = 20.0 * std::log10(run.rms() + 1e-12);
      std::printf("%.0f Hz, %s: %+.1f cents, %.1f dB RMS\n", rate, kModelNames[m], c, db);
      JCLAP_CHECK(std::fabs(c) < 10.0);
      JCLAP_CHECK(db > -32.0 && db < -10.0);
      JCLAP_CHECK(run.finite());
      JCLAP_CHECK(run.left == run.right);
      h.note(CLAP_EVENT_NOTE_OFF, key, 0.0, 100 + m);
      run.seconds(1.0);
      JCLAP_CHECK(run.left.back() == 0.0f);
   }
}

} // namespace

int main() {
   jclap::core::TestHost h(clap_entry, kId);
   EditorHost &panel = *h.object<EditorHost>();
   Run run{h};
   dry(h);

   // Silent until a note arrives.
   run(4800);
   JCLAP_CHECK_EQ(run.rms(), 0.0);

   // The default clarinet, blown by velocity until pressure arrives.
   h.setParam(kVibDepth, 0.0);
   h.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 7);
   run.seconds(0.6);
   JCLAP_CHECK(run.rms() > 0.02);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(62)), hzOf(62))) < 6.0);
   JCLAP_CHECK_EQ(sounding(panel), 1);

   // A CLAP tuning expression bends the bore: up two semitones.
   h.noteExpression(CLAP_NOTE_EXPRESSION_TUNING, -1, 2.0, 7);
   run.seconds(0.5);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(64)), hzOf(64))) < 10.0);
   h.noteExpression(CLAP_NOTE_EXPRESSION_TUNING, -1, 0.0, 7);

   // Pressure takes over from velocity: blown softly, the note is quieter.
   run.seconds(0.3);
   const double loud = run.rms();
   h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, -1, 0.3, 7);
   run.seconds(1.0);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < loud * 0.9);

   // Let go: the note dies away and the host hears it ended.
   h.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 7);
   run.seconds(1.0);
   JCLAP_CHECK(run.left.back() == 0.0f);
   JCLAP_CHECK(run.noteEnds.size() == 1 && run.noteEnds[0].note_id == 7);
   JCLAP_CHECK_EQ(sounding(panel), 0);
   run.noteEnds.clear();

   // Breath from pressure only: silent until the controller blows.
   h.setParam(kBreathSource, kSourcePressure);
   h.midi(0x91, 60, 100);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < 1e-3);
   // A breath controller (CC 2) on the note's channel blows it.
   h.midi(0xB1, 2, 100);
   run.seconds(0.5);
   JCLAP_CHECK(run.rms() > 0.02);
   // MPE: the note channel's bend, at the default 48-semitone range, up one semitone.
   const int bend = 8192 + 8192 / 48;
   h.midi(0xE1, bend & 0x7f, bend >> 7);
   run.seconds(0.5);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(61)), hzOf(61))) < 10.0);
   h.midi(0x81, 60, 0);
   h.midi(0xE1, 0, 64);
   h.midi(0xB1, 2, 0);
   run.seconds(1.0);
   JCLAP_CHECK(run.left.back() == 0.0f);

   // Breath from the Breath In parameter (a CV in the host): silent until it rises, then it blows.
   h.setParam(kBreathSource, kSourceInput);
   h.midi(0x91, 60, 100);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < 1e-3);
   h.setParam(kBreathIn, 0.8);
   run.seconds(0.5);
   JCLAP_CHECK(run.rms() > 0.02);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(60)), hzOf(60))) < 10.0);
   h.setParam(kBreathIn, 0.0);
   run.seconds(0.5);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < 1e-3);
   h.midi(0x81, 60, 0);
   run.seconds(1.0);
   JCLAP_CHECK(run.left.back() == 0.0f);

   // Mix: Breath In times each note's own velocity, or its pressure once that arrives. No input is
   // silent whatever the note does.
   h.setParam(kBreathSource, kSourceMix);
   h.setParam(kBreathIn, 1.0);
   h.midi(0x91, 60, 100);
   run.seconds(0.4);
   JCLAP_CHECK(run.rms() > 0.02);
   h.setParam(kBreathIn, 0.0);
   run.seconds(0.5);
   run.seconds(0.3);
   JCLAP_CHECK(run.rms() < 1e-3);
   h.setParam(kBreathIn, 1.0);
   h.midi(0xD1, 90, 0);
   run.seconds(0.6);
   JCLAP_CHECK(run.rms() > 0.02);
   h.midi(0x81, 60, 0);
   h.midi(0xD1, 0, 0);
   h.setParam(kBreathIn, 0.0);
   run.seconds(1.0);
   JCLAP_CHECK(run.left.back() == 0.0f);
   h.setParam(kBreathSource, kSourceAuto);

   // A route: LFO 1 (a 4 Hz sine) onto Fine at a quarter of its range (+-50 cents) bends the
   // pitch up and down; with the route's amount at zero the pitch holds still.
   {
      jclap::core::TestHost mh(clap_entry, kId);
      Run mr{mh};
      mh.setParam(kVibDepth, 0.0);
      mh.setParam(kLfo1Rate, 4.0);
      mh.setParam(routeParam(0, kRouteSource), kSrcLfo1);
      mh.setParam(routeParam(0, kRouteDestination), destinationOf(kFine));
      mh.setParam(routeParam(0, kRouteAmount), 0.25);
      mh.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 1);
      mr.seconds(0.5);
      double lo = 1e9, hi = -1e9;
      for (int i = 0; i < 8; ++i) {
         mr.seconds(0.03125);
         // A short window: the pitch over the last 30 ms.
         const double c = cents(mr.pitch(hzOf(62)), hzOf(62));
         lo = std::min(lo, c);
         hi = std::max(hi, c);
      }
      std::printf("LFO 1 onto Fine: %.0f..%.0f cents\n", lo, hi);
      JCLAP_CHECK(hi - lo > 40.0 && hi - lo < 130.0);
      mh.setParam(routeParam(0, kRouteAmount), 0.0);
      mr.seconds(0.3);
      lo = 1e9, hi = -1e9;
      for (int i = 0; i < 8; ++i) {
         mr.seconds(0.03125);
         const double c = cents(mr.pitch(hzOf(62)), hzOf(62));
         lo = std::min(lo, c);
         hi = std::max(hi, c);
      }
      JCLAP_CHECK(hi - lo < 10.0);

      // Press through Bus 1 onto Breath: a note with no pressure plays from its velocity, and
      // pressure (through the bus) makes it louder.
      mh.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 1);
      mr.seconds(1.0);
      mh.setParam(kBreathSource, kSourceVelocity);
      mh.setParam(routeParam(1, kRouteSource), kSrcPress);
      mh.setParam(routeParam(1, kRouteDestination), kFirstBusDest);
      mh.setParam(routeParam(1, kRouteAmount), 1.0);
      mh.setParam(routeParam(2, kRouteSource), kSrcBus1);
      mh.setParam(routeParam(2, kRouteDestination), destinationOf(kBreath));
      mh.setParam(routeParam(2, kRouteAmount), 0.3);
      mh.setParam(kBreath, 0.8);
      mh.midi(0x91, 60, 70);
      mr.seconds(0.6);
      const double quiet = mr.rms();
      mh.midi(0xD1, 127, 0);
      mr.seconds(0.6);
      const double loud = mr.rms();
      std::printf("Press through Bus 1 onto Breath: %.3f -> %.3f RMS\n", quiet, loud);
      JCLAP_CHECK(quiet > 0.01 && loud > quiet * 1.15);
   }

   run.noteEnds.clear();
   // Eight voices; a ninth note steals one, and the host hears that note ended.
   for (int i = 0; i < 8; ++i)
      h.note(CLAP_EVENT_NOTE_ON, 50 + 2 * i, 0.6, 200 + i);
   run.seconds(0.2);
   JCLAP_CHECK_EQ(sounding(panel), 8);
   JCLAP_CHECK(run.finite());
   h.note(CLAP_EVENT_NOTE_ON, 80, 0.6, 300);
   run.seconds(0.1);
   JCLAP_CHECK_EQ(run.noteEnds.size(), 1u);
   JCLAP_CHECK_EQ(sounding(panel), 8);
   // A choke naming nothing cuts everything at once.
   h.note(CLAP_EVENT_NOTE_CHOKE, -1, 0.0, -1, -1);
   run(256);
   JCLAP_CHECK_EQ(sounding(panel), 0);
   JCLAP_CHECK_EQ(run.noteEnds.size(), 9u);
   // Only the body's short ring (the tube below the holes) is left, and it's gone in 60 ms.
   run.seconds(0.06);
   run.seconds(0.1);
   JCLAP_CHECK(run.rms() == 0.0);
   run.noteEnds.clear();

   // Legato: one voice; a second key slurs to it, letting go goes back to the first.
   h.setParam(kVoiceMode, kLegato);
   h.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 1);
   run.seconds(0.5);
   h.note(CLAP_EVENT_NOTE_ON, 65, 0.8, 2);
   run.seconds(0.5);
   JCLAP_CHECK_EQ(sounding(panel), 1);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(65)), hzOf(65))) < 8.0);
   // The slur never stopped the air: no gap where the note changed.
   h.note(CLAP_EVENT_NOTE_OFF, 65, 0.0, 2);
   run.seconds(0.5);
   JCLAP_CHECK_EQ(sounding(panel), 1);
   JCLAP_CHECK(std::fabs(cents(run.pitch(hzOf(62)), hzOf(62))) < 8.0);
   h.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 1);
   run.seconds(1.0);
   JCLAP_CHECK_EQ(sounding(panel), 0);
   JCLAP_CHECK(run.left.back() == 0.0f);
   run.noteEnds.clear();

   // Mono: the tongue stops the air between notes, so the level dips at the change.
   h.setParam(kVoiceMode, kMono);
   h.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 1);
   run.seconds(0.5);
   h.note(CLAP_EVENT_NOTE_ON, 64, 0.8, 2);
   run(static_cast<uint32_t>(0.03 * 48000));
   const double gap = run.rms();
   run.seconds(0.5);
   JCLAP_CHECK(gap < run.rms() * 0.7);
   JCLAP_CHECK_EQ(sounding(panel), 1);
   h.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 1);
   h.note(CLAP_EVENT_NOTE_OFF, 64, 0.0, 2);
   run.seconds(1.0);
   JCLAP_CHECK_EQ(sounding(panel), 0);
   h.setParam(kVoiceMode, kPoly);

   // The panel's per-voice samples follow the voice.
   h.note(CLAP_EVENT_NOTE_ON, 62, 0.8, 9);
   run.seconds(0.3);
   std::vector<float> loop(512);
   panel.readVoice(0, loop.data(), loop.size());
   JCLAP_CHECK(std::any_of(loop.begin(), loop.end(), [](float v) { return std::fabs(v) > 1e-3f; }));
   JCLAP_CHECK(panel.outputPeak(0) > 0.0f && panel.outputPeak(1) > 0.0f);
   h.note(CLAP_EVENT_NOTE_OFF, 62, 0.0, 9);
   run.seconds(1.0);

   // Every param prints and reads back as itself.
   for (const auto &p : kParams) {
      for (double t : {0.0, 0.37, 1.0}) {
         double v = p.min + t * (p.max - p.min);
         if (p.flags & CLAP_PARAM_IS_STEPPED)
            v = std::round(v);
         char text[64];
         formatParam(p.id, v, text, sizeof(text));
         double back = -1e9;
         JCLAP_CHECK(parseParam(p.id, text, back));
         char again[64];
         formatParam(p.id, back, again, sizeof(again));
         if (std::strcmp(text, again))
            std::printf("param %s: %s -> %s\n", p.name, text, again);
         JCLAP_CHECK(!std::strcmp(text, again));
      }
   }

   // Factory presets: every value in range, names unique.
   const auto &presets = factoryPresets();
   JCLAP_CHECK(presets.size() >= 10);
   for (const auto &p : presets) {
      JCLAP_CHECK_EQ(p.values.size(), static_cast<size_t>(kPresetParamCount));
      for (const auto &[id, v] : p.values)
         JCLAP_CHECK(v >= kParams[id].min && v <= kParams[id].max);
   }
   for (size_t i = 0; i < presets.size(); ++i)
      for (size_t j = i + 1; j < presets.size(); ++j)
         JCLAP_CHECK(presets[i].name != presets[j].name);

   // Every factory preset plays a low and a middle note: sounding, finite, at a usable level, and in
   // tune (a note past the instrument's range plays a whole octave away, so pitch class only).
   for (const auto &p : presets) {
      jclap::core::TestHost ph(clap_entry, kId);
      Run pr{ph};
      for (const auto &[id, v] : p.values)
         ph.setParam(id, v);
      double tune = 0.0;
      for (const auto &[id, v] : p.values)
         tune += id == kTune ? std::round(v) : id == kFine ? v / 100.0 : 0.0;
      for (int key : {50, 62}) {
         ph.note(CLAP_EVENT_NOTE_ON, key, 0.75, key);
         pr.seconds(1.2);
         const double db = 20.0 * std::log10(pr.rms() + 1e-12);
         const double hz = hzOf(key + tune);
         double best = 9999.0;
         for (double oct : {-2.0, -1.0, 0.0, 1.0, 2.0})
            best = std::min(best, std::fabs(cents(pr.pitch(hz * std::pow(2.0, oct)), hz * std::pow(2.0, oct))));
         if (db < -40.0 || db > -6.0 || best > 35.0)
            std::printf("preset %s, key %d: %.1f dB, %.0f cents\n", p.name.c_str(), key, db, best);
         JCLAP_CHECK(pr.finite());
         JCLAP_CHECK(db > -40.0 && db < -6.0);
         JCLAP_CHECK(best < 35.0);
         ph.note(CLAP_EVENT_NOTE_OFF, key, 0.0, key);
         pr.seconds(2.5);
      }
   }

   // The output stage: Spread puts a low note left of the middle and a high one right; the Room rings
   // on after the voices stop, then empties and is silent.
   {
      jclap::core::TestHost oh(clap_entry, kId);
      EditorHost &op = *oh.object<EditorHost>();
      Run o{oh};
      oh.setParam(kSpread, 1.0);
      oh.setParam(kRoom, 0.0);
      oh.note(CLAP_EVENT_NOTE_ON, 45, 0.8, 1);
      o.seconds(0.5);
      JCLAP_CHECK(energy(o.left) > 2.0 * energy(o.right));
      oh.note(CLAP_EVENT_NOTE_OFF, 45, 0.0, 1);
      o.seconds(1.0);
      JCLAP_CHECK(o.left.back() == 0.0f);
      oh.setParam(kRoom, 0.6);
      oh.setParam(kRoomDecay, 1.0);
      oh.note(CLAP_EVENT_NOTE_ON, 84, 0.8, 2);
      o.seconds(0.5);
      JCLAP_CHECK(energy(o.right) > 2.0 * energy(o.left));
      oh.note(CLAP_EVENT_NOTE_OFF, 84, 0.0, 2);
      o.seconds(0.4);
      JCLAP_CHECK_EQ(sounding(op), 0);
      o.seconds(0.2);
      JCLAP_CHECK(o.rms() > 1e-4);
      o.seconds(2.0);
      JCLAP_CHECK(o.finite());
      JCLAP_CHECK(o.left.back() == 0.0f && o.right.back() == 0.0f);
   }

   // Every new control at both ends, on every instrument: still sounding, finite, not too loud, and
   // at the note's pitch (or an octave from it, where a setting overblows it).
   {
      struct Extreme {
         clap_id id;
         double value;
      };
      const Extreme extremes[] = {
         {kStiffness, 0.0}, {kStiffness, 1.0}, {kAperture, 0.0}, {kAperture, 1.0}, {kGrowl, 1.0},
         {kLoss, 0.0}, {kLoss, 0.25}, {kHoleCutoff, 700.0}, {kHoleCutoff, 4000.0}, {kCone, 0.0}, {kCone, 1.0},
         {kBrass, 1.0}, {kRadiate, 0.0}, {kRadiate, 1.0}, {kBarrel, 1.0}, {kWall, 1.0}, {kLength, 1.0},
         {kHoles, 5.0}, {kTube, 7.0}, {kDetune, 25.0}, {kHumanize, 1.0}, {kAir, 1.0},
         {kTongueType, kTongueFlutter}, {kTongueType, kTongueBreath}, {kTongueType, kTongueSoft},
         {kVibType, kVibJaw}, {kVibType, kVibLip}, {kHybridMix, 0.5}, {kHybridMix, 1.0}, {kLimiter, 2.0},
      };
      for (int m = 0; m < 5; ++m)
         for (const Extreme &x : extremes) {
            jclap::core::TestHost eh(clap_entry, kId);
            Run e{eh};
            dry(eh);
            eh.setParam(kModel, m);
            eh.setParam(kVibDepth, 0.0);
            eh.setParam(x.id, x.value);
            const int key = m == 4 ? 62 : 60;
            eh.note(CLAP_EVENT_NOTE_ON, key, 0.75, 1);
            e.seconds(0.8);
            const double db = 20.0 * std::log10(e.rms() + 1e-12);
            double best = 9999.0;
            for (double oct : {-1.0, 0.0, 1.0})
               best = std::min(best, std::fabs(cents(e.pitch(hzOf(key + 12 * oct)), hzOf(key + 12 * oct))));
            const bool ok = e.finite() && db > -40.0 && db < -3.0 && best < 40.0;
            if (!ok)
               std::printf("%s with %s at %g: %.1f dB, %.0f cents\n", kModelNames[m], kParams[x.id].name, x.value, db,
                           best);
            JCLAP_CHECK(ok);
         }
   }

   // State round trip, with the preset name.
   {
      h.setParam(kModel, 4);
      h.setParam(kEdge, 1.7);
      run(64);
      panel.setPresetName("Brass Plume");
      auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
      Buffer b;
      JCLAP_CHECK(state->save(h.plugin(), &b.out));
      jclap::core::TestHost h2(clap_entry, kId);
      JCLAP_CHECK(state->load(h2.plugin(), &b.in));
      EditorHost &panel2 = *h2.object<EditorHost>();
      JCLAP_CHECK_EQ(panel2.paramValue(kModel), 4.0);
      JCLAP_CHECK_EQ(panel2.paramValue(kEdge), 1.7);
      JCLAP_CHECK(panel2.presetName() == "Brass Plume");
      // Someone else's state is refused.
      Buffer bad;
      bad.bytes = {'J', 'B', 'S', 'Y', 1, 0, 0, 0};
      JCLAP_CHECK(!state->load(h2.plugin(), &bad.in));
   }

   // Every instrument, in tune at the common rates (oversampled 2x at 44.1 and 48 kHz, not at 96).
   playsInTune(44100.0);
   playsInTune(48000.0);
   playsInTune(96000.0);
   playsInTune(192000.0);

   return jclap::core::testResult();
}
