// Plays a phrase through the engine offline and records it: the audio, and with a video path, the
// panel as it plays, as an MP4 with the audio (it needs ffmpeg on the PATH).
//    jsynth-record out.mp4|out.wav [preset] [phrase]
// The preset is a factory preset's name (Init when left out). The phrase is "cm7-swell" (the default):
// a Cm7 held for 15 seconds while the pressure on every note swells up and back down; "four-bar", a
// chord a bar; "drone", a low chord built up over half a minute; or "landscape", the drone with its
// low notes struck hard and pressure leaning all the way in, for patches on both parts.

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "editor.h"
#include "engine.h"
#include "params.h"
#include "presets.h"

namespace {

using namespace jsynth;

constexpr double kRate = 48000.0;
constexpr int kFps = 30;
constexpr int kBlock = 64;

// The engine, and what the panel needs from it, offline: the params, the held notes, the flock.
class RecordHost : public EditorHost {
public:
   explicit RecordHost(const Preset &preset)
       : values_(new std::atomic<double>[paramCount()]), mods_(new std::atomic<double>[paramCount()]),
         name_(preset.name), buses_(preset.buses) {
      const std::vector<double> v = presetValues(preset);
      for (size_t i = 0; i < paramCount(); ++i) {
         values_[i].store(v[i]);
         mods_[i].store(0.0);
      }
      engine_.prepare(kRate, {values_.get(), mods_.get()});
   }

   double paramValue(clap_id id) const override {
      const int i = paramIndex(id);
      return i < 0 ? 0.0 : values_[i].load();
   }
   double paramModulated(clap_id id) const override { return paramValue(id); }
   void beginEdit(clap_id) override {}
   void setParam(clap_id id, double v) override {
      const int i = paramIndex(id);
      if (i >= 0)
         values_[i].store(v);
   }
   void endEdit(clap_id) override {}
   void noteOn(int, float) override {}
   void noteExpression(int, float, float, float) override {}
   void noteOff(int) override {}
   std::string presetName() const override { return name_; }
   void setPresetName(const std::string &) override {}
   BusNames busNames() const override { return buses_; }
   void notes(std::vector<Note> &out) const override { out = held_; }
   bool flockFrame(FlockFrame &out) const override {
      engine_.flock().show(out);
      engine_.showEnvelopes(out);
      for (int part = 0; part < kPartCount; ++part) {
         for (int e = 0; e < kEnvCount; ++e)
            out.env[part][e] = engine_.envLevel(part, e);
         for (int l = 0; l < kLfoCount; ++l)
            out.lfo[part][l] = engine_.lfoLevel(part, l);
      }
      return true;
   }

   Engine &engine() { return engine_; }
   std::vector<Note> &held() { return held_; }

private:
   std::unique_ptr<std::atomic<double>[]> values_, mods_;
   std::string name_;
   BusNames buses_;
   Engine engine_;
   std::vector<Note> held_;
};

NoteRef ref(int key, int id) {
   NoteRef r;
   r.key = static_cast<int16_t>(key);
   r.channel = 0;
   r.port = 0;
   r.noteId = id;
   r.host = true;
   return r;
}

// A phrase: notes, each struck and let go on its own sample, with its pressure over time.
struct Phrase {
   struct Note {
      int key;
      double on, off; // seconds
      float velocity;
   };
   std::vector<Note> notes;
   double length = 0;
   // Each held note's pressure at `t` seconds.
   float (*pressure)(const Phrase &, const Note &, double t) = nullptr;

   // A Cm7 (C3 G3 Bb3 Eb4) struck at 0.3 s and held for 15 s while every note's pressure swells from
   // nothing to full and back, then released into its tail.
   static Phrase cm7Swell() {
      Phrase p;
      for (int key : {48, 55, 58, 63})
         p.notes.push_back({key, 0.3, 15.3, 0.7f});
      p.length = 17.8;
      p.pressure = [](const Phrase &, const Note &n, double t) {
         const double s = std::sin(3.14159265358979 * (t - n.on) / (n.off - n.on));
         return static_cast<float>(s * s);
      };
      return p;
   }

   // A drone: an open fifth low down (C2 G2) held for half a minute, Eb3 and Bb3 joining at 6 s, D4
   // at 13 s and G4 at 19 s, all let go together at 28 s into their tails. Pressure rises and falls
   // slowly on every note, a little apart.
   static Phrase drone() {
      Phrase p;
      const std::pair<int, double> keys[] = {{36, 0.3}, {43, 0.3}, {51, 6.0}, {58, 6.0}, {62, 13.0}, {67, 19.0}};
      for (const auto &[key, on] : keys)
         p.notes.push_back({key, on, 28.0, 0.7f});
      p.length = 36.0;
      p.pressure = [](const Phrase &, const Note &n, double t) {
         const double s = std::sin(3.14159265358979 * (t - n.on) / (n.off - n.on) + 0.3 * (n.key % 5));
         return static_cast<float>(0.35 * s * s);
      };
      return p;
   }

   // The drone for two-part patches: its two low notes struck hard (past a velocity split) and
   // pressure that leans all the way in and out, so a Press crossfade goes from one part to the other.
   static Phrase landscape() {
      Phrase p = drone();
      p.notes[0].velocity = p.notes[1].velocity = 0.9f;
      p.pressure = [](const Phrase &, const Note &n, double t) {
         const double s = std::sin(3.14159265358979 * (t - n.on) / (n.off - n.on));
         return static_cast<float>(0.9 * s * s);
      };
      return p;
   }

   // Four bars at 84 BPM, a chord a bar: Cm9, Abmaj7, Fm9, G7(b9). Each is strummed upward, its
   // notes 45 ms apart and a little harder toward the top, and held into the next; pressure swells
   // through each bar.
   static Phrase fourBar() {
      Phrase p;
      const double bar = 4.0 * 60.0 / 84.0, start = 0.3, strum = 0.045;
      const std::vector<std::vector<int>> chords = {
          {48, 55, 58, 62, 63}, {44, 51, 55, 60, 63}, {41, 48, 51, 55, 56}, {43, 53, 59, 62, 68}};
      for (size_t c = 0; c < chords.size(); ++c) {
         const double at = start + bar * static_cast<double>(c);
         const double off = c + 1 < chords.size() ? at + bar + 0.03 : at + bar * 1.5;
         for (size_t i = 0; i < chords[c].size(); ++i)
            p.notes.push_back({chords[c][i], at + strum * static_cast<double>(i), off,
                               0.55f + 0.06f * static_cast<float>(i)});
      }
      p.length = start + bar * 4.5 + 2.5;
      p.pressure = [](const Phrase &, const Note &n, double t) {
         const double s = std::sin(3.14159265358979 * std::min(1.0, (t - n.on) / (n.off - n.on)));
         return static_cast<float>(0.15 + 0.7 * s * s);
      };
      return p;
   }
};

// Renders `samples` more of the phrase into `out` (interleaved stereo), starting at sample `at`.
void play(RecordHost &host, const Phrase &phrase, int64_t at, int samples, std::vector<float> &out) {
   Engine &e = host.engine();
   float l[kBlock], r[kBlock];
   float *main[2] = {l, r};
   float *const *ports[3] = {main, nullptr, nullptr};
   const auto sampleOf = [](double s) { return static_cast<int64_t>(s * kRate); };
   for (int done = 0; done < samples;) {
      const int64_t now = at + done;
      int n = std::min(kBlock, samples - done);
      // Land every note on and off on its sample.
      for (const Phrase::Note &note : phrase.notes)
         for (int64_t edge : {sampleOf(note.on), sampleOf(note.off)})
            if (now < edge && now + n > edge)
               n = static_cast<int>(edge - now);
      // Each note is its own (a chord can hold a key the next one strikes again).
      const auto id = [&](const Phrase::Note &note) { return static_cast<int>(&note - phrase.notes.data()); };
      for (const Phrase::Note &note : phrase.notes) {
         if (now == sampleOf(note.off)) {
            e.noteOff(ref(note.key, id(note)), 0.3f);
            auto &held = host.held();
            for (size_t i = 0; i < held.size(); ++i)
               if (held[i].note == note.key) {
                  held.erase(held.begin() + static_cast<long>(i));
                  break;
               }
         }
      }
      for (const Phrase::Note &note : phrase.notes)
         if (now == sampleOf(note.on)) {
            e.noteOn(ref(note.key, id(note)), note.velocity);
            host.held().push_back({note.key, 0.0f, 0.0f, 0.5f, note.velocity, 0.0f});
         }
      // Pressure, as an MPE controller sends it: a fresh value each block.
      const double t = static_cast<double>(now) / kRate;
      for (const Phrase::Note &note : phrase.notes)
         if (t >= note.on && t < note.off) {
            const float p = phrase.pressure(phrase, note, t);
            e.expression(ref(note.key, id(note)), CLAP_NOTE_EXPRESSION_PRESSURE, p);
            for (EditorHost::Note &h : host.held())
               if (h.note == note.key)
                  h.pressure = p;
         }
      std::fill(l, l + n, 0.0f);
      std::fill(r, r + n, 0.0f);
      e.render(ports, 0, n);
      e.clearEnded();
      for (int i = 0; i < n; ++i) {
         out.push_back(l[i]);
         out.push_back(r[i]);
      }
      done += n;
   }
}

bool writeWav(const char *path, const std::vector<float> &stereo) {
   FILE *f = std::fopen(path, "wb");
   if (!f)
      return false;
   const uint32_t frames = static_cast<uint32_t>(stereo.size() / 2), rate = static_cast<uint32_t>(kRate);
   const uint32_t bytes = frames * 4;
   const auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
   const auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
   std::fwrite("RIFF", 1, 4, f);
   u32(36 + bytes);
   std::fwrite("WAVEfmt ", 1, 8, f);
   u32(16);
   u16(1);
   u16(2);
   u32(rate);
   u32(rate * 4);
   u16(4);
   u16(16);
   std::fwrite("data", 1, 4, f);
   u32(bytes);
   for (float s : stereo) {
      const int16_t v = static_cast<int16_t>(std::lround(std::max(-1.0f, std::min(1.0f, s)) * 32767.0f));
      std::fwrite(&v, 2, 1, f);
   }
   return std::fclose(f) == 0;
}

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.mp4|out.wav [preset]\n", argv[0]);
      return 1;
   }
   const std::string out = argv[1];
   const std::string presetName = argc > 2 ? argv[2] : "Init";
   const Preset *preset = nullptr;
   for (const Preset &p : factoryPresets())
      if (p.name == presetName)
         preset = &p;
   if (!preset) {
      std::fprintf(stderr, "no factory preset called %s\n", presetName.c_str());
      return 1;
   }
   RecordHost host(*preset);
   const std::string phraseName = argc > 3 ? argv[3] : "cm7-swell";
   if (phraseName != "cm7-swell" && phraseName != "four-bar" && phraseName != "drone" && phraseName != "landscape") {
      std::fprintf(stderr, "no phrase called %s (cm7-swell, four-bar, drone or landscape)\n", phraseName.c_str());
      return 1;
   }
   const Phrase phrase = phraseName == "four-bar"    ? Phrase::fourBar()
                         : phraseName == "drone"     ? Phrase::drone()
                         : phraseName == "landscape" ? Phrase::landscape()
                                                     : Phrase::cm7Swell();
   std::vector<float> audio;
   const int64_t total = static_cast<int64_t>(phrase.length * kRate);
   audio.reserve(static_cast<size_t>(total) * 2);

   const bool video = out.size() > 4 && out.compare(out.size() - 4, 4, ".mp4") == 0;
   if (!video) {
      play(host, phrase, 0, static_cast<int>(total), audio);
   }
   else {
      const int width = 1648, height = 940;
      const std::string silent = out + ".video.mp4";
      const std::string encode = "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s " + std::to_string(width) + "x" +
                                 std::to_string(height) + " -r " + std::to_string(kFps) +
                                 " -i - -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p \"" + silent + "\"";
      FILE *pipe = popen(encode.c_str(), "w");
      if (!pipe)
         return 1;
      const int frames = static_cast<int>(phrase.length * kFps);
      int64_t at = 0;
      ScreenshotView view;
      const bool ok = renderFrames(
          host, view, width, height, frames, kFps,
          [&](int frame) {
             const int64_t until = static_cast<int64_t>(std::llround((frame + 1) * kRate / kFps));
             play(host, phrase, at, static_cast<int>(until - at), audio);
             at = until;
          },
          [&](const uint8_t *rgba) { std::fwrite(rgba, 1, static_cast<size_t>(width) * height * 4, pipe); });
      if (pclose(pipe) != 0 || !ok)
         return 1;
      const std::string wav = out + ".wav";
      if (!writeWav(wav.c_str(), audio))
         return 1;
      const std::string mux = "ffmpeg -y -loglevel error -i \"" + silent + "\" -i \"" + wav +
                              "\" -c:v copy -c:a aac -b:a 256k -shortest -movflags +faststart \"" + out + "\"";
      const int status = std::system(mux.c_str());
      std::remove(silent.c_str());
      std::remove(wav.c_str());
      if (status != 0)
         return 1;
   }
   float peak = 0;
   for (float s : audio)
      peak = std::max(peak, std::fabs(s));
   std::printf("%s: %.1f s, peak %.3f (%.1f dBFS)\n", presetName.c_str(), audio.size() / 2 / kRate, peak,
               20.0 * std::log10(std::max(peak, 1e-9f)));
   if (!video && !writeWav(out.c_str(), audio))
      return 1;
   return 0;
}
