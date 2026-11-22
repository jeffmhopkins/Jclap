// Hosts the JKit plugin (the real CLAP, through JClap's in-process test host).
//
//    jkit-funk make kit.state "style" seed takes   generates a WHOLE KIT (drum kit layout) the way the
//                                                   panel's GENERATE KIT does: each pad's prompt from
//                                                   kitPrompt(), its sounds treated and put on the pad
//                                                   as takes, the hats in their choke group; saves the
//                                                   plugin state and each pad's first take as a WAV
//    jkit-funk render kit.state notes.txt out.wav seconds [param=value ...]
//                                                   loads that state and plays a note log
//                                                   (seconds status data1 data2) through it
#include <jclap/core/test_host.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "editor.h"
#include "kit.h"
#include "kitgen.h"
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

void writeWav(const std::string &path, const std::vector<float> &l, const std::vector<float> &r, int rate) {
   std::ofstream f(path, std::ios::binary);
   const uint32_t n = static_cast<uint32_t>(l.size()), data = n * 2 * 4;
   auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
   auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
   f.write("RIFF", 4); u32(36 + data); f.write("WAVEfmt ", 8); u32(16); u16(3); u16(2); u32(rate);
   u32(rate * 8); u16(8); u16(32); f.write("data", 4); u32(data);
   for (uint32_t i = 0; i < n; ++i) {
      f.write(reinterpret_cast<const char *>(&l[i]), 4);
      f.write(reinterpret_cast<const char *>(&r[i]), 4);
   }
}

const clap_plugin_state_t *stateOf(jclap::core::TestHost &h) {
   return static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
}

int make(const std::string &path, const std::string &style, uint64_t seed, int takes) {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   host->gen().engine = 0; // Stable Audio Open Small, the default and the one for real drums
   const double strange = 0.2;
   const std::string dir = path.substr(0, path.find_last_of('/') + 1);
   const int *order = kitOrder(kDrumKitLayout);
   for (int i = 0; i < kKitSlots; ++i) {
      const int pad = order[i];
      const KitSlot &slot = kitSlot(pad, kDrumKitLayout);
      const std::string prompt = kitPrompt(pad, style, kDrumKitLayout, strange, seed);
      std::fprintf(stderr, "pad %2d %-10s %s\n", pad + 1, slot.name, prompt.c_str());
      host->generateOne(prompt, slot.role, slot.seconds, slot.name, takes);
      std::string error;
      std::vector<SamplePtr> sounds;
      const auto t0 = std::chrono::steady_clock::now();
      while (true) {
         host->pollGeneration(error);
         if (!host->generating()) {
            sounds = host->takeKitSounds();
            break;
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (sounds.empty()) {
         std::fprintf(stderr, "  failed: %s\n", error.c_str());
         return 1;
      }
      std::fprintf(stderr, "  %zu take(s) in %.1f s\n", sounds.size(),
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
      PadSounds p;
      for (const SamplePtr &sound : sounds) {
         auto s = std::make_shared<Sample>(treat(*sound, host->treatment()));
         s->name = slot.name;
         p.takes.push_back(Take{std::move(s)});
      }
      writeWav(dir + "pad" + std::to_string(pad + 1) + "-" + slot.name + ".wav", p.takes[0].sample->left,
               p.takes[0].sample->right, 44100);
      host->setPadSounds(pad, std::move(p));
      host->setParam(padParam(pad, kStart), 0.0);
      host->setParam(padParam(pad, kEnd), 1.0);
      if (slot.choke > 0)
         host->setParam(padParam(pad, kChoke), slot.choke);
   }
   host->setKitName("Funk Kit");
   Buffer b;
   if (!stateOf(h)->save(h.plugin(), &b.out))
      return 1;
   std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(b.bytes.data()), b.bytes.size());
   return 0;
}

// Generates `count` sounds for one pad of a drum kit (prompt from kitPrompt, or `prompt` as given),
// treated as GENERATE KIT treats them, as WAVs: out/<pad>-<n>.wav. For auditioning before assembling.
int candidates(int pad, const std::string &prompt, int count, const std::string &out, double seconds) {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   host->gen().engine = 0;
   const KitSlot &slot = kitSlot(pad, kDrumKitLayout);
   int n = 0;
   while (n < count) {
      host->generateOne(prompt, slot.role, seconds > 0 ? seconds : slot.seconds, slot.name, std::min(4, count - n));
      std::string error;
      std::vector<SamplePtr> sounds;
      while (true) {
         host->pollGeneration(error);
         if (!host->generating()) {
            sounds = host->takeKitSounds();
            break;
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (sounds.empty()) {
         std::fprintf(stderr, "failed: %s\n", error.c_str());
         return 1;
      }
      for (const SamplePtr &sound : sounds) {
         const Sample s = treat(*sound, host->treatment());
         writeWav(out + "/" + std::to_string(pad + 1) + "-" + std::to_string(++n) + ".wav", s.left, s.right, 44100);
      }
   }
   return 0;
}

SamplePtr readWav(const std::string &path, const std::string &name, int role) {
   std::ifstream f(path, std::ios::binary);
   std::string d((std::istreambuf_iterator<char>(f)), {});
   const size_t i = d.find("data");
   auto s = std::make_shared<Sample>();
   s->name = name;
   s->prompt = path;
   s->role = role;
   const float *x = reinterpret_cast<const float *>(d.data() + i + 8);
   const size_t n = (d.size() - i - 8) / 8;
   for (size_t k = 0; k < n; ++k) {
      s->left.push_back(x[2 * k]);
      s->right.push_back(x[2 * k + 1]);
   }
   return s;
}

// Builds a kit from chosen sounds: lines of "pad file[,file...] [param=value ...]" (pad 1..16,
// params are pad fields by index: 0 level dB, 2 pitch, 3 decay, 6 attack, 7 velocity...).
int assemble(const std::string &path, const std::string &list) {
   jclap::core::TestHost h(clap_entry, kId);
   auto *host = h.object<EditorHost>();
   std::ifstream f(list);
   std::string line;
   while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#')
         continue;
      std::istringstream ls(line);
      int pad;
      std::string files;
      ls >> pad >> files;
      --pad;
      const KitSlot &slot = kitSlot(pad, kDrumKitLayout);
      PadSounds p;
      std::istringstream fs(files);
      std::string file;
      while (std::getline(fs, file, ','))
         p.takes.push_back(Take{readWav(file, slot.name, slot.role)});
      host->setPadSounds(pad, std::move(p));
      if (slot.choke > 0)
         host->setParam(padParam(pad, kChoke), slot.choke);
      std::string kv;
      while (ls >> kv) {
         const size_t eq = kv.find('=');
         host->setParam(padParam(pad, static_cast<PadParam>(std::stoi(kv.substr(0, eq)))), std::stod(kv.substr(eq + 1)));
      }
   }
   host->setKitName("Funk Kit");
   Buffer b;
   if (!stateOf(h)->save(h.plugin(), &b.out))
      return 1;
   std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(b.bytes.data()), b.bytes.size());
   return 0;
}

struct Ev {
   double t;
   int st, d1, d2;
};

int render(const std::string &statePath, const std::string &notes, const std::string &wav, double seconds,
           const std::vector<std::pair<clap_id, double>> &params) {
   const double sr = 48000.0;
   jclap::core::TestHost h(clap_entry, kId, sr);
   Buffer b;
   std::ifstream f(statePath, std::ios::binary);
   b.bytes.assign(std::istreambuf_iterator<char>(f), {});
   if (!stateOf(h)->load(h.plugin(), &b.in)) {
      std::fprintf(stderr, "state didn't load\n");
      return 1;
   }
   for (auto [id, v] : params)
      h.setParam(id, v);
   std::vector<Ev> evs;
   FILE *in = std::fopen(notes.c_str(), "r");
   Ev e;
   while (in && std::fscanf(in, "%lf %d %d %d", &e.t, &e.st, &e.d1, &e.d2) == 4)
      evs.push_back(e);
   if (in)
      std::fclose(in);
   std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &c) { return a.t < c.t; });
   const int64_t total = static_cast<int64_t>(seconds * sr);
   const uint32_t kBlock = 64;
   std::vector<float> L, R, l(kBlock), r(kBlock);
   float *out[2] = {l.data(), r.data()};
   size_t k = 0;
   for (int64_t at = 0; at < total; at += kBlock) {
      while (k < evs.size() && static_cast<int64_t>(evs[k].t * sr) < at + kBlock) {
         const Ev &x = evs[k++];
         const uint32_t off = static_cast<uint32_t>(std::max<int64_t>(0, static_cast<int64_t>(x.t * sr) - at));
         h.midi(static_cast<uint8_t>(x.st), static_cast<uint8_t>(x.d1), static_cast<uint8_t>(x.d2), off);
      }
      h.process(kBlock, nullptr, out);
      L.insert(L.end(), l.begin(), l.end());
      R.insert(R.end(), r.begin(), r.end());
   }
   writeWav(wav, L, R, static_cast<int>(sr));
   return 0;
}
} // namespace

int main(int argc, char **argv) {
   if (argc >= 6 && !std::strcmp(argv[1], "make"))
      return make(argv[2], argv[3], std::strtoull(argv[4], nullptr, 10), std::atoi(argv[5]));
   if (argc >= 6 && !std::strcmp(argv[1], "render")) {
      std::vector<std::pair<clap_id, double>> params;
      for (int i = 6; i < argc; ++i) {
         const char *eq = std::strchr(argv[i], '=');
         if (eq)
            params.push_back({static_cast<clap_id>(std::atoi(argv[i])), std::atof(eq + 1)});
      }
      return render(argv[2], argv[3], argv[4], std::atof(argv[5]), params);
   }
   if (argc >= 6 && !std::strcmp(argv[1], "candidates"))
      return candidates(std::atoi(argv[2]) - 1, argv[3], std::atoi(argv[4]), argv[5], argc > 6 ? std::atof(argv[6]) : 0);
   if (argc >= 4 && !std::strcmp(argv[1], "assemble"))
      return assemble(argv[2], argv[3]);
   std::fprintf(stderr, "jkit-funk make|render ...\n");
   return 2;
}
