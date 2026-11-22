// Hosts the JDrummer plugin (the real CLAP, through JClap's in-process test host): rolls a Funk song
// with the Pocket Funk drummer, then plays an arrangement by automating the Section param on a
// playing transport, and prints every note it sends as a log: seconds status data1 data2.
//    jdrummer-funk "Style" bpm seed arrangement.txt > drums.txt
// arrangement.txt: lines of "bar section" (section 0..6: intro verse pre chorus bridge break outro),
// and a last line "bar -1" for the end.
#include <jclap/core/test_host.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "editor.h"
#include "params.h"
#include "recipe.h"

extern "C" const clap_plugin_entry_t clap_entry;
using namespace jdrummer;

int main(int argc, char **argv) {
   if (argc < 5)
      return 2;
   const std::string styleName = argv[1];
   const double bpm = std::atof(argv[2]);
   const uint32_t seed = static_cast<uint32_t>(std::atoi(argv[3]));
   std::vector<std::pair<int, int>> arr;
   FILE *f = std::fopen(argv[4], "r");
   int b, s;
   while (f && std::fscanf(f, "%d %d", &b, &s) == 2)
      arr.push_back({b, s});
   if (f)
      std::fclose(f);

   int genre = -1, style = -1;
   for (int g = 0; g < genreCount(); ++g)
      if (std::string(genreAt(g).name) == "Funk")
         for (size_t i = 0; i < genreAt(g).styles.size(); ++i)
            if (genreAt(g).styles[i].name == styleName)
               genre = g, style = static_cast<int>(i);
   if (genre < 0) {
      std::fprintf(stderr, "no Funk style %s\n", styleName.c_str());
      return 1;
   }

   const double sr = 48000.0;
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.jdrummer", sr);
   auto *host = h.object<EditorHost>();
   // The style, as picking it on the panel does, then the rest of the recipe.
   for (auto [id, v] : styleValues(genre, style))
      host->setParam(id, v);
   host->setParam(kPlayer, 1); // Pocket Funk
   host->setParam(kPercussionist, 0);
   host->setParam(kEnergy, 0.6);
   host->setParam(kWeird, 0.12);
   host->setParam(kSeed, seed);
   host->setParam(kGhosts, 0.35);
   host->setParam(kVary, 0.3);
   host->setParam(kSwitchAt, 1); // next bar
   host->setParam(kSongMode, 0);
   host->setParam(kSection, arr.front().second);
   Song song;
   generateSong(recipeFrom([&](clap_id id) { return host->paramValue(id); }), song);
   host->setSong(song);
   std::fprintf(stderr, "genre %d style %d (%s), swing %.2f\n", genre, style, styleName.c_str(), host->paramValue(kSwing));

   const int endBar = arr.back().first;
   const double beatSamples = sr * 60.0 / bpm;
   const int64_t total = static_cast<int64_t>(std::llround((endBar * 4) * beatSamples));
   const uint32_t kBlock = 64;
   h.setTransport(bpm, 0.0, true);
   size_t next = 1;
   for (int64_t at = 0; at < total; at += kBlock) {
      const double beat = at / beatSamples;
      // Each change goes in just after the bar before it starts, so the switch lands on its bar
      // line and JDrummer's fill leads into it.
      while (next < arr.size() && arr[next].second >= 0 && beat >= (arr[next].first - 1) * 4 + 0.05) {
         h.setParam(kSection, arr[next].second);
         ++next;
      }
      const auto out = h.process(kBlock);
      for (const auto &m : out.midi) {
         const int type = m.status & 0xF0;
         if (type != 0x90 && type != 0x80)
            continue;
         std::printf("%.6f %d %d %d\n", (at + m.time) / sr, m.status, m.data1, m.data2);
      }
   }
   return 0;
}
