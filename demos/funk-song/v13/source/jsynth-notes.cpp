// Local tool: renders a note log (seconds status data1 data2, as jarp-video writes) through a JSynth
// factory preset to a mono-summed stereo WAV.  jsynth-notes notes.txt out.wav "Preset" seconds [transpose]
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "engine.h"
#include "params.h"
#include "presets.h"
using namespace jsynth;
struct Ev { double t; int st, d1, d2; };
int main(int argc, char **argv) {
   if (argc < 5) return 2;
   const double kRate = 48000.0;
   const Preset *preset = nullptr;
   for (const Preset &p : factoryPresets()) if (p.name == argv[3]) preset = &p;
   if (!preset) { std::fprintf(stderr, "no preset %s\n", argv[3]); return 1; }
   const double seconds = std::atof(argv[4]);
   const int transpose = argc > 5 ? std::atoi(argv[5]) : 0;
   std::vector<std::pair<double, double>> sweep; // (seconds, cutoff offset) on filter A
   if (argc > 6) { FILE *sf = std::fopen(argv[6], "r"); double a, b; while (sf && std::fscanf(sf, "%lf %lf", &a, &b) == 2) sweep.push_back({a, b}); if (sf) std::fclose(sf); }
   std::unique_ptr<std::atomic<double>[]> values(new std::atomic<double>[paramCount()]), mods(new std::atomic<double>[paramCount()]);
   const std::vector<double> v = presetValues(*preset);
   for (size_t i = 0; i < paramCount(); ++i) { values[i].store(v[i]); mods[i].store(0.0); }
   auto engine = std::make_unique<Engine>();
   engine->prepare(kRate, {values.get(), mods.get()});
   std::vector<Ev> evs;
   FILE *in = std::fopen(argv[1], "r");
   Ev e;
   while (in && std::fscanf(in, "%lf %d %d %d", &e.t, &e.st, &e.d1, &e.d2) == 4) evs.push_back(e);
   if (in) std::fclose(in);
   std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &b) { return a.t < b.t; });
   std::map<std::pair<int, int>, std::vector<int>> live; // (channel,key) -> note ids
   std::map<int, std::pair<int, int>> idKey;              // id -> (channel,key)
   int nextId = 1;
   const int64_t total = static_cast<int64_t>(seconds * kRate);
   std::vector<float> out;
   out.reserve(static_cast<size_t>(total) * 2);
   size_t k = 0;
   const int kBlock = 64;
   float l[kBlock], r[kBlock];
   float *mainp[2] = {l, r};
   float *const *ports[3] = {mainp, nullptr, nullptr};
   auto ref = [&](int key, int id) { NoteRef n; n.key = static_cast<int16_t>(key); n.channel = 0; n.port = 0; n.noteId = id; n.host = true; return n; };
   for (int64_t s = 0; s < total;) {
      int n = static_cast<int>(std::min<int64_t>(kBlock, total - s));
      while (k < evs.size() && static_cast<int64_t>(evs[k].t * kRate) <= s) {
         const Ev &x = evs[k++];
         const int type = x.st & 0xF0, ch = x.st & 0x0F;
         const int key = std::clamp(x.d1 + transpose, 0, 127);
         if (type == 0x90 && x.d2 > 0) {
            const int id = nextId++;
            live[{ch, x.d1}].push_back(id);
            idKey[id] = {ch, key};
            engine->noteOn(ref(key, id), x.d2 / 127.0f);
         } else if (type == 0x80 || (type == 0x90 && x.d2 == 0)) {
            auto &vv = live[{ch, x.d1}];
            if (!vv.empty()) { const int id = vv.front(); vv.erase(vv.begin()); engine->noteOff(ref(key, id), 0.4f); idKey.erase(id); }
         } else if (type == 0xD0) {
            for (auto &[id, ck] : idKey) if (ck.first == ch) engine->expression(ref(ck.second, id), CLAP_NOTE_EXPRESSION_PRESSURE, x.d1 / 127.0f);
         }
      }
      if (k < evs.size()) {
         const int64_t nx = static_cast<int64_t>(evs[k].t * kRate);
         if (nx > s && nx < s + n) n = static_cast<int>(nx - s);
      }
      if (!sweep.empty()) {
         const double t = s / kRate;
         double o = sweep.front().second;
         for (size_t q = 0; q + 1 < sweep.size(); ++q)
            if (t >= sweep[q].first && t < sweep[q + 1].first) { const double u = (t - sweep[q].first) / (sweep[q + 1].first - sweep[q].first); o = sweep[q].second + u * (sweep[q + 1].second - sweep[q].second); }
         if (t >= sweep.back().first) o = sweep.back().second;
         const clap_id c = filterParam(0, kFilterCutoff);
         values[c].store(std::clamp(v[c] + o, 0.0, 1.0));
      }
      std::fill(l, l + n, 0.0f); std::fill(r, r + n, 0.0f);
      engine->render(ports, 0, n);
      engine->clearEnded();
      for (int i = 0; i < n; ++i) { out.push_back(l[i]); out.push_back(r[i]); }
      s += n;
   }
   FILE *f = std::fopen(argv[2], "wb");
   const uint32_t frames = static_cast<uint32_t>(out.size() / 2), rate = 48000, bytes = frames * 8;
   auto u32 = [&](uint32_t x) { std::fwrite(&x, 4, 1, f); };
   auto u16 = [&](uint16_t x) { std::fwrite(&x, 2, 1, f); };
   std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(3); u16(2); u32(rate); u32(rate * 8); u16(8); u16(32);
   std::fwrite("data", 1, 4, f); u32(bytes); std::fwrite(out.data(), 4, out.size(), f); std::fclose(f);
   return 0;
}
