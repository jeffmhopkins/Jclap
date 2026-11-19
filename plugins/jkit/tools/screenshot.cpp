// Renders the panel to a PNG without a host or a model: jkit-screenshot out.png [page [width height]]
// (page 0 Generate, 1 Kit, 2 Generate on first run, 3 Generate with ORB, 4 the engine picker)
// Page 0 is Generate, 1 is Kit, 2 is Generate on first run, while the model downloads, 3 is Generate with ORB. The sounds are made up here (decaying noise and sines), so the
// pictures are the same on every machine.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "editor.h"
#include "params.h"

namespace {

using namespace jkit;

// A made-up drum: a pitched sine with a falling pitch plus noise, under an exponential decay.
SamplePtr fake(const char *name, int role, uint32_t seed, double seconds, double hz, double noise, double decay) {
   auto s = std::make_shared<Sample>();
   s->name = name;
   s->role = role;
   s->seed = seed;
   s->prompt = "dusty lo-fi snare, tape saturation, short room tail, 90s boom bap";
   const size_t n = static_cast<size_t>(seconds * kSampleRate);
   s->left.resize(n);
   s->right.resize(n);
   uint32_t r = seed * 2654435761u + 1;
   double phase = 0;
   for (size_t i = 0; i < n; ++i) {
      const double t = i / kSampleRate;
      r = r * 1664525u + 1013904223u;
      const double white = (r >> 8) / 8388608.0 - 1.0;
      phase += 2 * 3.14159265 * hz * (1.0 + 2.0 * std::exp(-t * 40)) / kSampleRate;
      const double env = std::exp(-t * decay) * (1.0 - std::exp(-t * 2000));
      const double v = (std::sin(phase) * (1 - noise) + white * noise) * env * 0.8;
      s->left[i] = static_cast<float>(v);
      s->right[i] = static_cast<float>(v * 0.95);
   }
   return s;
}

class DemoHost : public EditorHost {
public:
   // First run: the model downloading, nothing generated yet.
   bool firstRun = false;

   DemoHost(bool first, bool orb = false) : firstRun(first) {
      gen_.engine = orb ? 1 : 0;
      for (clap_id i = 0; i < kParamCount; ++i)
         values_[i] = param(i).defaultValue;
      struct P {
         const char *name;
         int role;
         double secs, hz, noise, decay;
      };
      const P pads[14] = {{"Kick", kKick, 0.6, 55, 0.05, 7},    {"Kick 2", kKick, 0.5, 62, 0.1, 9},
                          {"Snare", kSnare, 0.5, 190, 0.6, 9},  {"Rim", kPerc, 0.2, 800, 0.3, 30},
                          {"Clap", kClap, 0.4, 900, 0.9, 10},   {"Closed Hat", kHat, 0.15, 6000, 0.95, 35},
                          {"Open Hat", kHat, 0.6, 6000, 0.95, 6}, {"Ride", kHat, 1.2, 4000, 0.8, 3},
                          {"Low Tom", kTom, 0.7, 90, 0.1, 6},  {"Hi Tom", kTom, 0.6, 150, 0.1, 7},
                          {"Shaker", kPerc, 0.25, 7000, 0.95, 18}, {"Conga", kPerc, 0.4, 260, 0.15, 12},
                          {"Vinyl Riser", kFx, 2.0, 300, 0.7, 0.8}, {"Zap", kFx, 0.3, 1200, 0.2, 14}};
      for (int i = 0; i < 14; ++i)
         pads_[i] = fake(pads[i].name, pads[i].role, 40100 + i * 7, pads[i].secs, pads[i].hz, pads[i].noise, pads[i].decay);
      const double sends[14][2] = {{.05, 0}, {0, 0}, {.35, .2}, {.2, 0}, {.45, .3}, {.05, .1}, {.15, .25},
                                   {.3, 0},  {.25, 0}, {.25, 0}, {.1, .35}, {.2, .1}, {.6, .5}, {.15, .4}};
      const double pans[14] = {0, 0, 0.05, -0.25, 0.15, 0.45, 0.5, -0.55, -0.4, 0.35, 0.7, -0.6, -0.2, 0.6};
      for (int i = 0; i < 14; ++i) {
         values_[padParam(i, kReverbSend)] = sends[i][0];
         values_[padParam(i, kDelaySend)] = sends[i][1];
         values_[padParam(i, kPan)] = pans[i];
      }
      // Pad 3, the snare, as the mockup has it.
      values_[padParam(2, kLevel)] = -3.0;
      values_[padParam(2, kPitch)] = 2.0;
      values_[padParam(2, kDecay)] = decayValue(640);
      values_[padParam(2, kStart)] = 0.04;
      values_[padParam(2, kEnd)] = 0.76;
      // PAD TONE: a high pass, a resonant low pass, a bell and a low shelf.
      const auto point = [&](int i, int type, double hz, double db, double q) {
         values_[padParam(2, pointParam(i, kPointType))] = type;
         values_[padParam(2, pointParam(i, kPointFreq))] = toneHzValue(hz);
         values_[padParam(2, pointParam(i, kPointGain))] = db;
         values_[padParam(2, pointParam(i, kPointQ))] = toneQValue(q);
      };
      point(0, kToneHp12, 90, 0, 0.7071);
      point(1, kToneLp24, 9000, 0, 1.6);
      point(2, kToneBell, 2400, 4.0, 1.2);
      point(3, kToneLowShelf, 160, -3.0, 0.7071);
      values_[padParam(2, kFilterEnv)] = 0.25;
      values_[padParam(2, kFilterDecay)] = filterDecayValue(120);
      values_[padParam(2, kVelFilter)] = 0.4;
      values_[padParam(2, kDrive)] = 0.2;
      values_[kMasterLow] = 2.0;
      values_[kMasterMid] = -2.5;
      values_[kMasterMidFreq] = midFreqValue(400);
      values_[kMasterHigh] = 1.5;
      values_[kBoom] = 0.35;
      values_[kWidth] = 0.8;
      selectedPad = 2;
      targetPad = 2;
      selectedVariant = 1;
      gen_.prompt = "dusty lo-fi snare, tape saturation, short room tail, 90s boom bap";
      gen_.role = kSnare;
      if (orb) {
         gen_.prompt = "a heavy door slams shut, then glass shattering";
         gen_.strange = 0.6;
      }
      gen_.seed = 40217;
      gen_.seconds = 1.2;
      gen_.variety = 0.35;
      for (int i = 0; i < 4; ++i)
         variants_.push_back({fake(orb ? "Hit" : "Snare", kSnare, 40217 + i, 0.8 + 0.07 * i, 170 + 20 * i, 0.55 + 0.05 * i, 7 + i), i == 1});
      const int64_t now = static_cast<int64_t>(std::time(nullptr));
      recent_ = {{"909 kick, long boom, slight click", kKick, 11, now - 120},
                 {"crushed shaker loop hit, 8-bit", kHat, 12, now - 360},
                 {"reverse cymbal swell into silence", kFx, 13, now - 660}};
      if (firstRun) {
         variants_.clear();
         recent_.clear();
         selectedVariant = 0;
      }
      treatment_.start = 0.02;
      treatment_.end = 0.8;
      treatment_.fadeMs = 120;
      treatment_.fadeInMs = 40;
   }

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id] : 0.0; }
   void beginEdit(clap_id) override {}
   void setParam(clap_id id, double v) override {
      if (id < kParamCount)
         values_[id] = v;
   }
   void endEdit(clap_id) override {}

   SamplePtr padSample(int pad) const override { return pad >= 0 && pad < kPads ? pads_[pad] : nullptr; }
   void setPadSample(int pad, SamplePtr s) override { pads_[pad] = std::move(s); }
   void swapPads(int, int) override {}
   void playPad(int, float) override {}
   void stopPad(int) override {}
   // Pads 5 and 6 (the clap and the closed hat) are sounding.
   float padPosition(int pad) const override { return pad == 4 ? 0.42f : pad == 5 ? 0.18f : -1.0f; }
   float padLevel(int pad) const override { return pad == 4 ? 0.8f : pad == 5 ? 0.5f : 0.0f; }
   uint32_t padHits() const override { return 0; }
   int lastHitPad() const override { return 0; }
   void meters(float &kit, float &fx, float &out) override {
      kit = 0.5f;
      fx = 0.12f;
      out = 0.6f;
   }

   GenSettings &gen() override { return gen_; }
   Treatment &treatment() override { return treatment_; }
   std::vector<Variant> &variants() override { return variants_; }
   void generateOne(const std::string &, int, double, const std::string &) override {}
   SamplePtr takeKitSound() override { return nullptr; }
   std::vector<Recent> &recent() override { return recent_; }
   EngineStatus engineStatus(int engine) override {
      const int was = gen_.engine;
      gen_.engine = engine;
      EngineStatus e = this->engine();
      gen_.engine = was;
      if (engine == 2) {
         e.state = EngineStatus::State::Missing;
         e.downloadBytes = 4893000000ull;
         e.diskBytes = 0;
      }
      return e;
   }
   EngineStatus engine() override {
      EngineStatus e;
      if (gen_.engine == 1) {
         e.state = EngineStatus::State::Ready;
         e.downloadBytes = e.diskBytes = 20795318ull;
         e.folder = "~/Library/Application Support/JClap/models/orb-planner";
         return e;
      }
      e.state = firstRun ? EngineStatus::State::Downloading : EngineStatus::State::Ready;
      e.received = 1053000000ull;
      e.total = 2239099934ull;
      e.downloadBytes = 2239099934ull;
      e.diskBytes = firstRun ? 1053000000ull : 2139697237ull;
      e.folder = "~/Library/Application Support/JClap/models/stable-audio-open-small";
      return e;
   }
   void installEngine() override {}
   void removeEngine() override {}
   void cancelEngine() override {}
   void generate(int) override {}
   bool generating() const override { return firstRun; }
   double generateProgress() const override { return -1.0; }
   bool pollGeneration(std::string &) override { return false; }
   double lastRunSeconds() const override { return firstRun ? 0.0 : 3.6; }
   void audition(SamplePtr) override {}
   float auditionPosition() const override { return firstRun ? -1.0f : 0.34f; }
   std::string kitName() const override { return "Dusty Tape Kit"; }
   void setKitName(const std::string &) override {}

private:
   std::vector<double> values_ = std::vector<double>(kParamCount);
   SamplePtr pads_[kPads];
   GenSettings gen_;
   Treatment treatment_;
   std::vector<Variant> variants_;
   std::vector<Recent> recent_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [page [width height]]\n", argv[0]);
      return 1;
   }
   const int page = argc > 2 ? std::atoi(argv[2]) : 0;
   const int width = argc > 4 ? std::atoi(argv[3]) : kEditorWidth;
   const int height = argc > 4 ? std::atoi(argv[4]) : kEditorHeight;
   DemoHost host(page == 2, page == 3);
   // Page 1 is the Kit tab (the panel's page 0); 0, 2, 3, 4 and 5 the Generate tab (its page 1), 4
   // with the engine picker open and 5 with GENERATE KIT's card.
   return renderScreenshot(host, page == 1 ? 0 : 1, width, height, argv[1], page == 4 ? 1 : page == 5 ? 2 : 0) ? 0 : 1;
}
