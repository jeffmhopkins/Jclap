// The generator's parts that need no model: hashing, the tokenizer, the sampler's schedule and
// noise, the random prompts, and the studio's bookkeeping. With JKIT_TEST_MODEL set to a folder
// holding the model files and JKIT_TEST_ORT to ONNX Runtime's library, it also generates a sound.

#include <jclap/core/test_host.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include "gen/fetch.h"
#include "gen/ort.h"
#include "gen/sao.h"
#include "gen/sha256.h"
#include "gen/studio.h"
#include "gen/tokenizer.h"
#include "prompts.h"

using namespace jkit;
using namespace jkit::gen;

namespace {

std::string sha(const std::string &s) {
   Sha256 h;
   h.update(s.data(), s.size());
   return h.hex();
}

void testSha256() {
   JCLAP_CHECK(sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
   JCLAP_CHECK(sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
   // Fed in odd pieces, across block edges, it agrees with one go.
   std::string big(1000, 'x');
   for (size_t i = 0; i < big.size(); ++i)
      big[i] = static_cast<char>('a' + i % 26);
   Sha256 pieces;
   for (size_t i = 0; i < big.size(); i += 37)
      pieces.update(big.data() + i, std::min<size_t>(37, big.size() - i));
   JCLAP_CHECK(pieces.hex() == sha(big));
}

void testTokenizer() {
   // A tiny Unigram vocabulary in tokenizer.json's shape.
   const std::string json = R"({"model":{"type":"Unigram","unk_id":2,"vocab":[["<pad>",0.0],["</s>",0.0],["<unk>",0.0],)"
                            R"(["▁",-2.0],["▁kick",-3.0],["▁ki",-6.0],["ck",-6.0],["▁drum",-3.5],)"
                            R"(["s",-4.0],["d",-5.0],["r",-5.0],["u",-5.0],["m",-5.0],["k",-5.0],["i",-5.0],["c",-5.0]]}})";
   Tokenizer t;
   JCLAP_CHECK(t.load(json));
   const std::vector<int64_t> ids = t.encode("kick drums");
   // "▁kick" beats "▁ki"+"ck"; "▁drum" + "s"; then </s>.
   const std::vector<int64_t> want = {4, 7, 8, 1};
   JCLAP_CHECK(ids == want);
   // Unknown characters become one <unk> per run.
   const std::vector<int64_t> unk = t.encode("kick ZZZ");
   JCLAP_CHECK(unk.size() == 4 && unk[0] == 4 && unk[1] == 3 && unk[2] == Tokenizer::kUnk && unk[3] == 1);
   // Cut to the length, still ending with </s>.
   const std::vector<int64_t> cut = t.encode("kick kick kick kick kick", 3);
   JCLAP_CHECK(cut.size() == 3 && cut.back() == 1);
}

void testSampler() {
   const std::vector<float> s = StableAudio::schedule(8);
   JCLAP_CHECK_EQ(s.size(), 9u);
   JCLAP_CHECK(s.front() == 1.0f && s.back() == 0.0f);
   for (size_t i = 1; i < s.size(); ++i)
      JCLAP_CHECK(s[i] < s[i - 1]);
   // Noise is the same for a seed, different across seeds, and roughly unit normal.
   Noise a(42), b(42), c(43);
   double sum = 0, sq = 0;
   bool same = true, differs = false;
   for (int i = 0; i < 20000; ++i) {
      const float x = a.normal(), y = b.normal(), z = c.normal();
      same = same && x == y;
      differs = differs || x != z;
      sum += x;
      sq += x * x;
   }
   JCLAP_CHECK(same);
   JCLAP_CHECK(differs);
   JCLAP_CHECK(std::fabs(sum / 20000) < 0.05);
   JCLAP_CHECK(std::fabs(sq / 20000 - 1.0) < 0.05);
}

void testPrompts() {
   for (int r = 0; r < kRoleCount; ++r)
      for (double strange : {0.0, 0.25, 0.5, 0.75, 1.0}) {
         std::set<std::string> seen;
         for (uint64_t seed = 0; seed < 40; ++seed) {
            const std::string p = randomPrompt(static_cast<Role>(r), strange, seed);
            JCLAP_CHECK(!p.empty() && p.size() < 220);
            // The same seed makes the same prompt.
            JCLAP_CHECK(p == randomPrompt(static_cast<Role>(r), strange, seed));
            seen.insert(p);
         }
         // Plenty of different ones.
         JCLAP_CHECK(seen.size() >= 35);
      }
   // The word bank is big: every role has many plain and strange sounds.
   for (int r = 0; r < kRoleCount; ++r) {
      const auto &w = prompts::roleWords(static_cast<Role>(r));
      JCLAP_CHECK(w.sounds.size() >= 25 && w.traits.size() >= 25 && w.oddities.size() >= 15);
   }
   // Strange 0 never reaches for the odd sounds; strange 1 mostly does.
   int odd0 = 0, odd1 = 0;
   for (uint64_t seed = 0; seed < 200; ++seed) {
      const std::string a = randomPrompt(kKick, 0.0, seed), b = randomPrompt(kKick, 1.0, seed);
      for (const char *o : prompts::roleWords(kKick).oddities) {
         odd0 += a.find(o) != std::string::npos;
         odd1 += b.find(o) != std::string::npos;
      }
   }
   JCLAP_CHECK_EQ(odd0, 0);
   JCLAP_CHECK(odd1 > 120);
}

void testStudio() {
   const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jkit-test-folder";
   std::error_code ec;
   std::filesystem::remove_all(dir, ec);
   std::filesystem::create_directories(dir);
#if defined(_WIN32)
   _putenv_s("JKIT_FOLDER", dir.string().c_str());
#else
   setenv("JKIT_FOLDER", dir.c_str(), 1);
#endif
   JCLAP_CHECK(Studio::folder() == dir);
   JCLAP_CHECK(Studio::downloadBytes() > 2000000000ull && Studio::downloadBytes() < 2500000000ull);
   JCLAP_CHECK_EQ(Studio::diskBytes(), 0u);
   {
      Studio::Handle a, b; // two instances share one studio
      JCLAP_CHECK(&*a == &*b);
      JCLAP_CHECK(a->status().state == Studio::State::Missing);
      // Files on disk count; removing deletes them.
      std::filesystem::create_directories(Studio::modelFolder());
      std::ofstream(Studio::modelFolder() / "partial.part") << std::string(1000, 'x');
      JCLAP_CHECK_EQ(Studio::diskBytes(), 1000u);
      a->remove();
      for (int i = 0; i < 200 && std::filesystem::exists(Studio::modelFolder()); ++i)
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      JCLAP_CHECK(!std::filesystem::exists(Studio::modelFolder()));
      JCLAP_CHECK_EQ(Studio::diskBytes(), 0u);
      JCLAP_CHECK(a->status().state == Studio::State::Missing);
   }
   std::filesystem::remove_all(dir, ec);
   JCLAP_CHECK(!runtimePackagePath().empty());
}

// Only with the model at hand (not in CI): one short sound, which should be audible and finite.
void testGenerate() {
   const char *model = std::getenv("JKIT_TEST_MODEL"), *ort = std::getenv("JKIT_TEST_ORT");
   if (!model || !ort)
      return;
   Runtime runtime;
   std::string error;
   JCLAP_CHECK(runtime.load(ort, error));
   StableAudio sao;
   JCLAP_CHECK(sao.load(runtime, model, 4, error));
   Request r;
   r.prompt = "punchy 909 kick drum";
   r.seconds = 1.0;
   r.variants = 2;
   std::vector<Sound> out;
   std::atomic<bool> cancel{false};
   JCLAP_CHECK(sao.generate(r, out, {}, cancel, error));
   JCLAP_CHECK_EQ(out.size(), 2u);
   for (const Sound &s : out) {
      float peak = 0;
      for (float x : s.left) {
         JCLAP_CHECK(std::isfinite(x));
         peak = std::max(peak, std::fabs(x));
      }
      JCLAP_CHECK(peak > 0.01f);
      JCLAP_CHECK(s.left.size() >= 44100);
   }
   std::printf("generated %zu sounds\n", out.size());
}

} // namespace

int main() {
   testSha256();
   testTokenizer();
   testSampler();
   testPrompts();
   testStudio();
   testGenerate();
   return jclap::core::testResult();
}
