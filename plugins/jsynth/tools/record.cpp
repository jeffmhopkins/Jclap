// Plays a phrase through the engine offline and records it: the audio, and with a video path, the
// panel as it plays, as an MP4 with the audio (it needs ffmpeg on the PATH).
//    jsynth-record out.mp4|out.wav [preset] [phrase]
// The preset is a factory preset's name (Init when left out). The phrase is "cm7-swell" (the default):
// a Cm7 held for 15 seconds while the pressure on every note swells up and back down.

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
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
         name_(preset.name) {
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
   void notes(std::vector<Note> &out) const override { out = held_; }
   bool flockFrame(FlockFrame &out) const override {
      engine_.flock().show(out);
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
   Engine engine_;
   std::vector<Note> held_;
};

NoteRef ref(int key) {
   NoteRef r;
   r.key = static_cast<int16_t>(key);
   r.channel = 0;
   r.port = 0;
   r.noteId = key;
   r.host = true;
   return r;
}

// The phrase: a Cm7 (C3 G3 Bb3 Eb4) struck at 0.3 s and held for 15 s while every note's pressure
// swells from nothing to full and back, then released into its tail.
struct Phrase {
   static constexpr double kStart = 0.3, kHold = 15.0, kTail = 2.5;
   const int keys[4] = {48, 55, 58, 63};
   double length() const { return kStart + kHold + kTail; }
   float pressure(double t) const {
      if (t < kStart || t > kStart + kHold)
         return 0.0f;
      const double s = std::sin(3.14159265358979 * (t - kStart) / kHold);
      return static_cast<float>(s * s);
   }
};

// Renders `samples` more of the phrase into `out` (interleaved stereo), starting at sample `at`.
void play(RecordHost &host, const Phrase &phrase, int64_t at, int samples, std::vector<float> &out) {
   Engine &e = host.engine();
   float l[kBlock], r[kBlock];
   float *main[2] = {l, r};
   float *const *ports[3] = {main, nullptr, nullptr};
   const int64_t on = static_cast<int64_t>(Phrase::kStart * kRate);
   const int64_t off = static_cast<int64_t>((Phrase::kStart + Phrase::kHold) * kRate);
   for (int done = 0; done < samples;) {
      const int64_t now = at + done;
      int n = std::min(kBlock, samples - done);
      // Land the note on and off on their samples.
      if (now < on && now + n > on)
         n = static_cast<int>(on - now);
      if (now < off && now + n > off)
         n = static_cast<int>(off - now);
      if (now == on) {
         for (int k : phrase.keys)
            e.noteOn(ref(k), 0.7f);
         host.held().clear();
         for (int k : phrase.keys)
            host.held().push_back({k, 0.0f, 0.0f, 0.5f, 0.7f, 0.0f});
      }
      if (now == off) {
         for (int k : phrase.keys)
            e.noteOff(ref(k), 0.3f);
         host.held().clear();
      }
      // Pressure, as an MPE controller sends it: a fresh value each block.
      const float p = phrase.pressure(static_cast<double>(now) / kRate);
      if (now >= on && now < off) {
         for (int k : phrase.keys)
            e.expression(ref(k), CLAP_NOTE_EXPRESSION_PRESSURE, p);
         for (EditorHost::Note &h : host.held())
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
   const Phrase phrase;
   std::vector<float> audio;
   const int64_t total = static_cast<int64_t>(phrase.length() * kRate);
   audio.reserve(static_cast<size_t>(total) * 2);

   const bool video = out.size() > 4 && out.compare(out.size() - 4, 4, ".mp4") == 0;
   if (!video) {
      play(host, phrase, 0, static_cast<int>(total), audio);
   }
   else {
      const int width = 1200, height = 780;
      const std::string silent = out + ".video.mp4";
      const std::string encode = "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s " + std::to_string(width) + "x" +
                                 std::to_string(height) + " -r " + std::to_string(kFps) +
                                 " -i - -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p \"" + silent + "\"";
      FILE *pipe = popen(encode.c_str(), "w");
      if (!pipe)
         return 1;
      const int frames = static_cast<int>(phrase.length() * kFps);
      int64_t at = 0;
      ScreenshotView view;
      view.leftTab = 1;  // FM: the grid the pressure is moving
      view.rightTab = 0; // ENV
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
