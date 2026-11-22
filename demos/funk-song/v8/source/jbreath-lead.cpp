// Hosts the JBreath plugin (the real CLAP, through JClap's in-process test host): loads a factory
// preset and plays a note log (seconds status data1 data2) through it to a stereo float WAV.
//    jbreath-lead "Preset" notes.txt out.wav seconds ["Param Name"=value ...] [tempo=bpm] [lyrics=words]
#include <jclap/core/test_host.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "editor.h"
#include "params.h"
#include "presets.h"

extern "C" const clap_plugin_entry_t clap_entry;
using namespace jbreath;

struct Ev {
   double t;
   int st, d1, d2;
};

int main(int argc, char **argv) {
   if (argc < 5)
      return 2;
   const Preset *preset = nullptr;
   for (const Preset &p : factoryPresets())
      if (p.name == argv[1])
         preset = &p;
   if (!preset) {
      std::fprintf(stderr, "no preset %s\n", argv[1]);
      return 1;
   }
   const double sr = 48000.0, seconds = std::atof(argv[4]);
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.jbreath", sr);
   for (const auto &[id, v] : preset->values)
      h.setParam(id, v);
   // Extra params by name ("Unison=2"), and "tempo=100" for a playing transport (Alien Pace follows it).
   // "@seconds:Param Name=value": a param change at that time (automation).
   std::vector<std::pair<double, std::pair<clap_id, double>>> autos;
   for (int i = 5; i < argc; ++i)
      if (argv[i][0] == '@') {
         const char *colon = std::strchr(argv[i], ':'), *eq = std::strchr(argv[i], '=');
         const std::string name(colon + 1, static_cast<size_t>(eq - colon - 1));
         for (clap_id id = 0; id < kParamCount; ++id)
            if (name == kParams[id].name)
               autos.push_back({std::atof(argv[i] + 1), {id, std::atof(eq + 1)}});
      }
   for (int i = 5; i < argc; ++i)
      if (argv[i][0] == '@')
         continue;
      else if (const char *eq = std::strchr(argv[i], '=')) {
         const std::string name(argv[i], static_cast<size_t>(eq - argv[i]));
         if (name == "lyrics") { // words for the Alien to sing, a syllable a note
            h.object<EditorHost>()->setLyrics(eq + 1);
            continue;
         }
         if (name == "tempo") {
            h.setTransport(std::atof(eq + 1), 0.0, true);
            continue;
         }
         bool found = false;
         for (clap_id id = 0; id < kParamCount; ++id)
            if (name == kParams[id].name) {
               h.setParam(id, std::atof(eq + 1));
               found = true;
            }
         if (!found)
            std::fprintf(stderr, "no param %s\n", name.c_str());
      }
   {
      h.process(64);
      std::string text;
      std::vector<std::pair<int, int>> syl;
      h.object<EditorHost>()->sungWords(text, syl);
      std::fprintf(stderr, "sings: %s (%zu syllables)\n", text.substr(0, 80).c_str(), syl.size());
   }
   std::vector<Ev> evs;
   FILE *in = std::fopen(argv[2], "r");
   Ev e;
   while (in && std::fscanf(in, "%lf %d %d %d", &e.t, &e.st, &e.d1, &e.d2) == 4)
      evs.push_back(e);
   if (in)
      std::fclose(in);
   std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &b) { return a.t < b.t; });
   const int64_t total = static_cast<int64_t>(seconds * sr);
   const uint32_t kBlock = 64;
   std::vector<float> L, R, l(kBlock), r(kBlock);
   float *out[2] = {l.data(), r.data()};
   size_t k = 0;
   for (int64_t at = 0; at < total; at += kBlock) {
      for (const auto &[t, pv] : autos)
         if (static_cast<int64_t>(t * sr) >= at && static_cast<int64_t>(t * sr) < at + kBlock)
            h.setParam(pv.first, pv.second);
      while (k < evs.size() && static_cast<int64_t>(evs[k].t * sr) < at + kBlock) {
         const Ev &x = evs[k++];
         const uint32_t off = static_cast<uint32_t>(std::max<int64_t>(0, static_cast<int64_t>(x.t * sr) - at));
         h.midi(static_cast<uint8_t>(x.st), static_cast<uint8_t>(x.d1), static_cast<uint8_t>(x.d2), off);
      }
      h.process(kBlock, nullptr, out);
      L.insert(L.end(), l.begin(), l.end());
      R.insert(R.end(), r.begin(), r.end());
   }
   std::ofstream f(argv[3], std::ios::binary);
   const uint32_t n = static_cast<uint32_t>(L.size()), data = n * 8;
   auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
   auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
   f.write("RIFF", 4); u32(36 + data); f.write("WAVEfmt ", 8); u32(16); u16(3); u16(2); u32(48000);
   u32(48000 * 8); u16(8); u16(32); f.write("data", 4); u32(data);
   for (uint32_t i = 0; i < n; ++i) {
      f.write(reinterpret_cast<const char *>(&L[i]), 4);
      f.write(reinterpret_cast<const char *>(&R[i]), 4);
   }
   return 0;
}
