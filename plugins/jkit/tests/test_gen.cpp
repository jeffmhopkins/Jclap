// The generator's parts that need no model: hashing, the tokenizer, the sampler's schedule and
// noise, the random prompts, the studio's bookkeeping, and ORB's renderer and planner grammar. With
// JKIT_TEST_MODEL set to a folder holding the model files and JKIT_TEST_ORT to ONNX Runtime's library,
// it also generates a sound; with JKIT_TEST_ORB set to a folder holding planner.safetensors, it plans
// and plays one with ORB.

#include <jclap/core/test_host.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <thread>

#include "gen/fetch.h"
#include "gen/orb/planner.h"
#include "gen/orb/render.h"
#include "gen/orb_engine.h"
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
   const Engine sao = Engine::SaoSmall, orb = Engine::Orb;
   JCLAP_CHECK(Studio::downloadBytes(sao) > 2000000000ull && Studio::downloadBytes(sao) < 2500000000ull);
   JCLAP_CHECK_EQ(Studio::downloadBytes(orb), 20795318u);
   // 1.0: its files and the runtime (not there yet in the test folder).
   JCLAP_CHECK(Studio::downloadBytes(Engine::SaoOpen1) > 4800000000ull && Studio::downloadBytes(Engine::SaoOpen1) < 4950000000ull);
   JCLAP_CHECK(Studio::modelFolder(Engine::SaoOpen1) != Studio::modelFolder(sao));
   JCLAP_CHECK(Studio::modelFolder(sao) != Studio::modelFolder(orb));
   JCLAP_CHECK_EQ(Studio::diskBytes(sao), 0u);
   JCLAP_CHECK_EQ(Studio::diskBytes(orb), 0u);
   {
      Studio::Handle a, b; // two instances share one studio
      JCLAP_CHECK(&*a == &*b);
      JCLAP_CHECK(a->status(sao).state == Studio::State::Missing);
      JCLAP_CHECK(a->status(orb).state == Studio::State::Missing);
      // Files on disk count, each engine its own; removing one deletes only its files.
      std::filesystem::create_directories(Studio::modelFolder(sao));
      std::ofstream(Studio::modelFolder(sao) / "partial.part") << std::string(1000, 'x');
      std::filesystem::create_directories(Studio::modelFolder(orb));
      std::ofstream(Studio::modelFolder(orb) / "partial.part") << std::string(300, 'x');
      JCLAP_CHECK_EQ(Studio::diskBytes(sao), 1000u);
      JCLAP_CHECK_EQ(Studio::diskBytes(orb), 300u);
      a->remove(sao);
      for (int i = 0; i < 200 && std::filesystem::exists(Studio::modelFolder(sao)); ++i)
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      JCLAP_CHECK(!std::filesystem::exists(Studio::modelFolder(sao)));
      JCLAP_CHECK_EQ(Studio::diskBytes(sao), 0u);
      JCLAP_CHECK_EQ(Studio::diskBytes(orb), 300u);
      JCLAP_CHECK(a->status(sao).state == Studio::State::Missing);
      // The runtime goes with the last Stable Audio: removing Small keeps it while 1.0 is there.
      std::filesystem::create_directories(Studio::modelFolder(Engine::SaoOpen1));
      std::filesystem::create_directories(Studio::folder() / "runtime");
      std::ofstream(Studio::folder() / "runtime" / "lib.bin") << std::string(50, 'x');
      a->remove(sao);
      for (int i = 0; i < 200 && a->status(sao).state != Studio::State::Missing; ++i)
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      JCLAP_CHECK(std::filesystem::exists(Studio::folder() / "runtime"));
      a->remove(Engine::SaoOpen1);
      for (int i = 0; i < 200 && std::filesystem::exists(Studio::folder() / "runtime"); ++i)
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      JCLAP_CHECK(!std::filesystem::exists(Studio::folder() / "runtime"));
      a->remove(orb);
      for (int i = 0; i < 200 && std::filesystem::exists(Studio::modelFolder(orb)); ++i)
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      JCLAP_CHECK_EQ(Studio::diskBytes(orb), 0u);
      // A job for an engine that isn't downloaded fails (and asks for the download).
      Request r;
      r.engine = orb;
      const int job = a->submit(r);
      std::vector<Sound> got;
      std::string error;
      bool done = false;
      for (int i = 0; i < 500 && !done; ++i) {
         a->cancel(); // stop the download it starts: the test has no network to spare
         done = a->take(job, got, error);
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      JCLAP_CHECK(done);
      JCLAP_CHECK(!error.empty());
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

// 1.0's noise levels: k-diffusion's polyexponential schedule from 500 to 0.3, then 0.
void testSigmas() {
   const std::vector<double> s = StableAudio::sigmas(50);
   JCLAP_CHECK_EQ(s.size(), 51u);
   JCLAP_CHECK(std::fabs(s[0] - 500.0) < 1e-9);
   JCLAP_CHECK(std::fabs(s[49] - 0.3) < 1e-12);
   JCLAP_CHECK(s[50] == 0.0);
   for (size_t i = 1; i < 50; ++i)
      JCLAP_CHECK(s[i] < s[i - 1] && std::fabs(std::log(s[i - 1] / s[i]) - std::log(500.0 / 0.3) / 49) < 1e-9);
}

// Only with 1.0 at hand (not in CI): JKIT_TEST_SAO1 is its folder. Writes the sound's raw floats to
// JKIT_TEST_SAO1_OUT if set, for comparing against the Python sampler.
void testGenerateOpen1() {
   const char *model = std::getenv("JKIT_TEST_SAO1"), *ort = std::getenv("JKIT_TEST_ORT");
   if (!model || !ort)
      return;
   Runtime runtime;
   std::string error;
   JCLAP_CHECK(runtime.load(ort, error));
   StableAudio sao(StableAudio::Model::Open1);
   JCLAP_CHECK(sao.load(runtime, model, 4, error));
   Request r;
   r.engine = Engine::SaoOpen1;
   r.prompt = "a deep punchy kick drum";
   r.seconds = 1.5;
   r.steps = 50;
   r.guidance = 7.0;
   r.seed = 1;
   std::vector<Sound> out;
   std::atomic<bool> cancel{false};
   const auto t0 = std::chrono::steady_clock::now();
   JCLAP_CHECK(sao.generate(r, out, {}, cancel, error));
   std::printf("1.0: %s in %.1f s\n", error.c_str(),
               std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
   JCLAP_CHECK_EQ(out.size(), 1u);
   if (out.empty())
      return;
   JCLAP_CHECK_EQ(out[0].left.size(), 66150u);
   if (const char *file = std::getenv("JKIT_TEST_SAO1_OUT")) {
      std::ofstream f(file, std::ios::binary);
      f.write(reinterpret_cast<const char *>(out[0].left.data()), static_cast<std::streamsize>(out[0].left.size() * 4));
      f.write(reinterpret_cast<const char *>(out[0].right.data()), static_cast<std::streamsize>(out[0].right.size() * 4));
   }
}

// ORB's renderer against numbers from the original Python (orb 0.4.x, numpy 2): one event each.
void testOrbRender() {
   using namespace jkit::orb;
   JCLAP_CHECK_EQ(roleNames().size(), 47u);
   int rendered = 0;
   for (int r = 0; r < 47; ++r)
      rendered += roleRendered(r) ? 1 : 0;
   JCLAP_CHECK_EQ(rendered, 27);
   JCLAP_CHECK(!roleRendered(roleIndex("thunder")));
   struct Gold {
      const char *role;
      int seed;
      double dur;
      size_t n;
      double energy, a, b;
   };
   const Gold gold[] = {
      {"impact", 3, 0.5, 83999, 165.65059211271887, -0.029628231314558259, 6.9866826112229837e-05},
      {"glass_break", 4, 1.0, 119999, 57.496618523974696, -0.037742769109628767, -0.0010416014181510564},
      {"gunshot", 5, 1.0, 263999, 39.031864567525872, -0.0085650138115130718, -0.00016226512055388562},
      {"church_bell", 2, 1.5, 311999, 92.873927363237101, 0.019682722355746002, -0.00099715536077203053},
      {"whoosh", 9, 0.8, 98399, 114.85246065386615, -0.0024367485726295175, -0.0060200321594987786},
   };
   const double apex[] = {0.0, 0.0, 0.0, 0.0, 0.05};
   for (size_t g = 0; g < std::size(gold); ++g) {
      Event ev;
      ev.role = roleIndex(gold[g].role);
      ev.dur = gold[g].dur;
      ev.apex = apex[g] * ev.dur;
      Vec l, r;
      renderEvent(ev, gold[g].seed, true, l, r);
      JCLAP_CHECK_EQ(l.size(), gold[g].n);
      if (l.size() != gold[g].n)
         continue;
      double e = 0;
      for (double x : l)
         e += x * x;
      JCLAP_CHECK(std::fabs(e - gold[g].energy) < 1e-6 * gold[g].energy);
      JCLAP_CHECK(std::fabs(l[gold[g].n / 7] - gold[g].a) < 1e-7);
      JCLAP_CHECK(std::fabs(r[gold[g].n / 3] - gold[g].b) < 1e-7);
   }
   // Every role this port renders makes a finite, audible sound for a few seeds.
   for (int role = 0; role < 47; ++role) {
      if (!roleRendered(role))
         continue;
      for (int seed : {1, 2, 3}) {
         Spec spec;
         spec.duration = 0.6;
         spec.events.push_back({role, 0.0, 0.6, -6.0, 0.0, seed});
         Vec l, r;
         renderSpec(spec, l, r);
         double peak = 0;
         bool finite = true;
         for (size_t i = 0; i < l.size(); ++i) {
            finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
            peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
         }
         JCLAP_CHECK(finite);
         JCLAP_CHECK(peak > 0.85 && peak < 0.9); // normalised to 0.89
         JCLAP_CHECK_EQ(l.size(), 28800u);
      }
   }
}

void testOrbPlan() {
   using namespace jkit::orb;
   // The grammar: a fixed length spells its digits, no bed, only the given roles.
   PlanOptions o;
   o.durationCs = 1234;
   o.roles = {roleIndex("bell"), roleIndex("knock")};
   Grammar g(o);
   const auto one = [&](int expect) {
      const std::vector<int> a = g.allowed();
      JCLAP_CHECK(a.size() == 1 && a[0] == expect);
      g.advance(expect);
   };
   one(tok::SPEC);
   one(tok::DUR);
   for (int d : {0, 1, 2, 3, 4})
      one(tok::DIGIT + d);
   one(tok::TEX);
   one(tok::NOBED);
   one(tok::MOOD);
   JCLAP_CHECK_EQ(g.allowed().size(), 10u);
   g.advance(tok::MOODVAL);
   one(tok::EVENTS);
   one(tok::EVENT); // no bed: at least one event
   JCLAP_CHECK(g.allowed() == (std::vector<int>{tok::ROLEVAL + roleIndex("bell"), tok::ROLEVAL + roleIndex("knock")}));
   g.advance(tok::ROLEVAL + roleIndex("knock"));
   one(tok::T);
   // t <= 12.32 s: the first digit can be 0 or 1.
   JCLAP_CHECK_EQ(g.allowed().size(), 2u);

   // Decoding a hand-written plan, then fitting it to a pad.
   std::vector<int> ids = {tok::SPEC, tok::DUR};
   for (int d : {0, 0, 2, 0, 0})
      ids.push_back(tok::DIGIT + d);
   ids.insert(ids.end(), {tok::TEX, tok::NOBED, tok::MOOD, tok::MOODVAL, tok::EVENTS});
   const auto event = [&](int role, int tCs, int dCs) {
      ids.insert(ids.end(), {tok::EVENT, tok::ROLEVAL + role, tok::T});
      for (int div : {1000, 100, 10, 1})
         ids.push_back(tok::DIGIT + tCs / div % 10);
      ids.push_back(tok::D);
      for (int div : {1000, 100, 10, 1})
         ids.push_back(tok::DIGIT + dCs / div % 10);
      ids.insert(ids.end(), {tok::G, tok::GAIN + 12, tok::P, tok::PAN + 4, tok::S, tok::HEX + 1, tok::HEX + 15});
   };
   event(roleIndex("impact"), 10, 70);
   event(roleIndex("bell"), 50, 150);
   ids.insert(ids.end(), {tok::END_EVENTS, tok::END_SPEC, tok::EOS});
   Spec spec;
   JCLAP_CHECK(Planner::decode(ids, spec));
   JCLAP_CHECK_EQ(spec.events.size(), 2u);
   JCLAP_CHECK(std::fabs(spec.duration - 2.0) < 1e-9);
   JCLAP_CHECK(std::fabs(spec.events[1].t - 0.5) < 1e-9 && std::fabs(spec.events[1].dur - 1.5) < 1e-9);
   JCLAP_CHECK(spec.events[0].gainDb == -6.0 && spec.events[0].pan == 0.0 && spec.events[0].seed == 31);
   ids.pop_back();
   ids.pop_back();
   JCLAP_CHECK(!Planner::decode(ids, spec));

   Spec planned;
   planned.duration = 2.0;
   planned.events = {{roleIndex("impact"), 0.1, 0.7}, {roleIndex("bell"), 0.5, 1.5}};
   Spec fitted = OrbEngine::fit(planned, 0.3);
   JCLAP_CHECK(std::fabs(fitted.duration - 0.3) < 1e-9);
   JCLAP_CHECK_EQ(fitted.events.size(), 1u); // the bell would start after the end
   JCLAP_CHECK(fitted.events[0].t == 0.0);
   fitted = OrbEngine::fit(planned, 5.0);
   JCLAP_CHECK(std::fabs(fitted.duration - 1.9) < 1e-9);
   JCLAP_CHECK_EQ(fitted.events.size(), 2u);
   JCLAP_CHECK(std::fabs(fitted.events[1].t - 0.4) < 1e-9);

   // Each variant's seed changes the sound; the same seed makes the same one.
   const Sound a = OrbEngine::render(fitted, 7), b = OrbEngine::render(fitted, 7), c = OrbEngine::render(fitted, 8);
   JCLAP_CHECK(a.left == b.left);
   JCLAP_CHECK(a.left != c.left);
   JCLAP_CHECK_EQ(a.left.size(), static_cast<size_t>(std::llround(1.9 * 48000 * 441.0 / 480.0)));

   // Resampling keeps a tone's pitch: 1 kHz at 48 kHz is 44.1 samples a cycle at 44.1 kHz.
   std::vector<double> sine(48000);
   for (size_t i = 0; i < sine.size(); ++i)
      sine[i] = std::sin(2.0 * 3.141592653589793 * 1000.0 * static_cast<double>(i) / 48000.0);
   const std::vector<float> s = resample48To441(sine);
   JCLAP_CHECK_EQ(s.size(), 44100u);
   double err = 0;
   for (size_t i = 2000; i < 42000; ++i)
      err = std::max(err, std::fabs(s[i] - std::sin(2.0 * 3.141592653589793 * 1000.0 * static_cast<double>(i) / 44100.0)));
   JCLAP_CHECK(err < 1e-3);

   // The other variants' pitch and decay: a bell (parametric) and a door (played faster or slower)
   // come out different from the plain take, and the plain take is unchanged by asking twice.
   for (const char *name : {"bell", "door_slam"}) {
      Spec one;
      one.duration = 1.0;
      SpecEvent ev;
      ev.role = roleIndex(name);
      ev.dur = 0.8;
      one.events.push_back(ev);
      const Sound plain = OrbEngine::render(one, 7), again = OrbEngine::render(one, 7);
      const Sound varied = OrbEngine::render(one, 7, true);
      JCLAP_CHECK(plain.left == again.left);
      JCLAP_CHECK(plain.left.size() == varied.left.size() && plain.left != varied.left);
   }

   // With the weights at hand (not in CI): a prompt becomes a plan and a sound.
   const char *weights = std::getenv("JKIT_TEST_ORB");
   if (!weights)
      return;
   OrbEngine engine;
   std::string error;
   JCLAP_CHECK(engine.load(weights, error));
   Request r;
   r.engine = Engine::Orb;
   r.prompt = "a deep punchy kick drum";
   r.seconds = 1.0;
   r.variants = 3;
   std::vector<Sound> out;
   std::atomic<bool> cancel{false};
   JCLAP_CHECK(engine.generate(r, out, [](double) {}, cancel, error));
   JCLAP_CHECK_EQ(out.size(), 3u);
   for (size_t a = 0; a < out.size(); ++a)
      for (size_t b = a + 1; b < out.size(); ++b)
         JCLAP_CHECK(out[a].left != out[b].left);
   // A sampled plan is still a plan, and differs between seeds somewhere in a few tries.
   std::set<std::vector<int>> plans;
   for (uint64_t seed = 1; seed <= 6; ++seed) {
      Spec sampled;
      JCLAP_CHECK(engine.plan("metal clang", 1.5, sampled, error, nullptr, 0.8, seed));
      std::vector<int> roles;
      for (const SpecEvent &e : sampled.events)
         roles.push_back(e.role);
      plans.insert(roles);
   }
   JCLAP_CHECK(plans.size() > 1);
   Spec kick;
   JCLAP_CHECK(engine.plan(r.prompt, 1.0, kick, error));
   JCLAP_CHECK(!kick.events.empty() && kick.events[0].role == roleIndex("punch"));
   std::printf("ORB planned %zu events for the kick\n", kick.events.size());
}

} // namespace

int main() {
   testSha256();
   testTokenizer();
   testSampler();
   testPrompts();
   testStudio();
   testGenerate();
   testSigmas();
   testGenerateOpen1();
   testOrbRender();
   testOrbPlan();
   return jclap::core::testResult();
}
