// Renders the panel to a PNG without a host:
//    jbreath-screenshot out.png [width height [frames [player-tab [routes [armed-source [exciter-tab
//       [voice-tab [air [settings...]]]]]]]]]
// Each setting is preset=Name (a factory preset, underscores for spaces), Param_Name=value, or the
// word dice, which opens the dice card. A four-note chord is playing, with breath loops drawn from
// synthetic tones in the instrument's colour of sound.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "lyrics.h"
#include "editor.h"
#include "params.h"
#include "presets.h"

namespace {

using namespace jbreath;

class DemoHost : public EditorHost {
public:
   DemoHost() {
      for (int i = 0; i < kParamCount; ++i)
         values_[i] = kParams[i].defaultValue;
      values_[kBreath] = 1.1;
      values_[kEmbouchure] = 0.45;
      values_[kVibDepth] = 0.03;
      values_[kVibPitch] = 6.0;
      values_[kKick] = 0.35;
      values_[kNoise] = 0.8;
      values_[kLfo1Rate] = 0.6;
      values_[kSwellTime] = 6.0;
      // A few routes, as a player would map them.
      const struct {
         int source;
         clap_id dest;
         double amount;
      } routes[] = {{kSrcPress, kBreath, 0.3}, {kSrcSlide, kEmbouchure, 0.6}, {kSrcStrike, kKick, 0.5},
                    {kSrcVapor, kNoise, 0.3},  {kSrcFold, kEdge, -0.25},     {kSrcLfo1, kFine, 0.1},
                    {kSrcSwell, kVibDepth, 0.4}};
      int r = 0;
      for (const auto &route : routes) {
         values_[routeParam(r, kRouteSource)] = route.source;
         values_[routeParam(r, kRouteDestination)] = destinationOf(route.dest);
         values_[routeParam(r, kRouteAmount)] = route.amount;
         ++r;
      }
   }
   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id] : 0.0; }
   double paramModulated(clap_id id) const override { return id == kEmbouchure ? 0.58 : paramValue(id); }
   void beginEdit(clap_id) override {}
   void setParam(clap_id id, double v) override {
      if (id < kParamCount)
         values_[id] = v;
   }
   void endEdit(clap_id) override {}
   std::string presetName() const override { return preset_; }
   std::string preset_ = "Wooden Reed";

   // preset=Name or Param_Name=value; false if it names nothing.
   bool apply(std::string setting) {
      for (char &c : setting)
         if (c == '_')
            c = ' ';
      const size_t eq = setting.find('=');
      if (eq == std::string::npos)
         return false;
      const std::string key = setting.substr(0, eq), value = setting.substr(eq + 1);
      if (key == "lyrics") {
         lyrics_ = value;
         return true;
      }
      if (key == "preset") {
         for (const auto &p : factoryPresets())
            if (p.name == value) {
               for (const auto &[id, v] : p.values)
                  values_[id] = v;
               preset_ = p.name;
               return true;
            }
         return false;
      }
      for (clap_id id = 0; id < kParamCount; ++id)
         if (key == kParams[id].name) {
            double v = 0.0;
            if (!parseParam(id, value.c_str(), v))
               return false;
            values_[id] = v;
            return true;
         }
      return false;
   }
   void setPresetName(const std::string &) override {}
   std::string lyrics() const override { return lyrics_; }
   void setLyrics(const std::string &text) override { lyrics_ = text; }
   void sungWords(std::string &text, std::vector<std::pair<int, int>> &syllables) const override {
      syllables.clear();
      speech::Book book;
      if (values_[kAlienSource] > 0.5 && !lyrics_.empty()) {
         speech::readLyrics(lyrics_, book);
         text = lyrics_;
      } else {
         uint32_t sounds = 0, vowels = 0;
         for (int f = 0; f < 8; ++f)
            sounds |= values_[kAlienSounds + f] > 0.5 ? 1u << f : 0u;
         for (int f = 0; f < 5; ++f)
            vowels |= values_[kAlienVowels + f] > 0.5 ? 1u << f : 0u;
         speech::alienWords(static_cast<uint32_t>(std::lround(values_[kAlienSeed])), sounds, vowels,
                            static_cast<int>(std::lround(values_[kAlienOrder])), book, &text);
      }
      for (int i = 0; i < book.count; ++i)
         syllables.emplace_back(book.begin[i], book.end[i]);
   }
   std::string lyrics_;

   // D3 and A3 held, F#4 swelling, E5 let go and fading.
   static constexpr int kNotes[4] = {50, 57, 66, 76};
   void voices(std::vector<Voice> &out) const override {
      out.assign(kVoiceCount, Voice{});
      const float breath[4] = {0.62f, 0.7f, 0.85f, 0.3f}, pressure[4] = {0.5f, 0.62f, 0.85f, 0.2f};
      const float timbre[4] = {0.5f, 0.42f, 0.66f, 0.5f}, level[4] = {0.08f, 0.1f, 0.14f, 0.03f};
      for (int i = 0; i < 4; ++i) {
         const float hz = 440.0f * std::pow(2.0f, (kNotes[i] - 69) / 12.0f);
         out[i] = {kNotes[i], hz, breath[i], pressure[i], timbre[i], level[i]};
         out[i].releasing = i == 3;
         // On the Alien, the first two singing one syllable, the others the next ones on.
         if (static_cast<int>(std::lround(values_[kModel])) == 5) // the Alien
            out[i].saying = i < 2 ? 2 : i + 1;
         float *src = out[i].sources.data();
         src[kSrcStrike] = 0.7f;
         src[kSrcPress] = pressure[i];
         src[kSrcGlide] = 0.1f;
         src[kSrcSlide] = timbre[i];
         src[kSrcLoop] = std::min(1.0f, breath[i]);
         src[kSrcKnot] = 0.45f;
         src[kSrcFold] = 0.2f;
         src[kSrcRegister] = (kNotes[i] - 43) / 60.0f;
         src[kSrcVapor] = 0.35f;
         src[kSrcDraft] = -0.3f;
         src[kSrcSwell] = 0.62f;
         src[kSrcLfo1] = 0.4f;
         src[kSrcLfo2] = -0.2f;
         out[i].offsets[destinationOf(kFine)] = 0.02f;
         out[i].offsets[destinationOf(kEmbouchure)] = 0.1f;
      }
   }
   // A clarinet-like tone (strong odd harmonics, a little even) at each voice's pitch.
   void readVoice(int slot, float *out, size_t count) override {
      if (slot >= 4) {
         for (size_t i = 0; i < count; ++i)
            out[i] = 0.0f;
         return;
      }
      const double hz = 440.0 * std::pow(2.0, (kNotes[slot] - 69) / 12.0);
      const double amp[4] = {0.3, 0.35, 0.45, 0.15};
      // Harmonics 1..7 for each instrument: a flute's near-sine with its 2nd, the clarinet's odd
      // ones, the sax's and oboe's full rows, the trumpet's bright brass.
      static const double recipes[5][7] = {{1, 0.45, 0.12, 0.05, 0.02, 0, 0},
                                                      {1, 0.12, 0.55, 0.02, 0.25, 0.01, 0.12},
                                                      {1, 0.7, 0.5, 0.35, 0.25, 0.15, 0.1},
                                                      {0.6, 1, 0.7, 0.4, 0.3, 0.2, 0.15},
                                                      {1, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3}};
      const double *h = recipes[std::clamp(static_cast<int>(values_[kModel] + 0.5), 0, 4)];
      for (size_t i = 0; i < count; ++i) {
         const double ph = 2.0 * 3.141592653589793 * hz * static_cast<double>(i) / 48000.0;
         double x = 0.0;
         for (int k = 0; k < 7; ++k)
            x += h[k] * std::sin((k + 1) * ph + 0.7 * k);
         out[i] = static_cast<float>(amp[slot] * x);
      }
   }
   float outputPeak(int channel) override { return channel ? 0.38f : 0.42f; }
   double values_[kParamCount] = {};
   double sampleRate() const override { return 48000.0; }
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height [frames [player-tab [routes [armed [exciter-tab [voice-tab [air [settings... | dice | panel | nopanel]]]]]]]]]\n", argv[0]);
      return 1;
   }
   if (!std::strcmp(argv[1], "--bench")) {
      // jbreath-screenshot --bench [frames]: the panel's cost a frame, in PLAY and AIR.
      const int frames = argc > 2 ? std::atoi(argv[2]) : 240;
      for (bool air : {false, true}) {
         DemoHost bench;
         const PanelBenchmark b = benchmarkPanel(bench, 1648, 940, frames, air);
         std::printf("%s: step %.2f ms (smoke solved inline; the plugin solves it on its own thread), draw %.2f ms a frame (smoke grid %d cells)\n", air ? "AIR " : "PLAY", b.stepMs, b.drawMs,
                     b.cells);
      }
      return 0;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 1648;
   const int height = argc > 3 ? std::atoi(argv[3]) : 940;
   const int frames = argc > 4 ? std::atoi(argv[4]) : 150;
   ScreenshotOptions options;
   options.playerTab = argc > 5 ? std::atoi(argv[5]) : 0;
   options.routes = argc > 6 && std::atoi(argv[6]) != 0;
   options.armed = argc > 7 ? std::atoi(argv[7]) : 0;
   options.exciterTab = argc > 8 ? std::atoi(argv[8]) : 0;
   options.voiceTab = argc > 9 ? std::atoi(argv[9]) : 0;
   options.air = argc > 10 && std::atoi(argv[10]) != 0;
   DemoHost host;
   for (int i = 11; i < argc; ++i)
      if (!std::strcmp(argv[i], "dice"))
         options.dice = true; // the dice card open
      else if (!std::strcmp(argv[i], "nopanel"))
         host.airPanelHidden = true; // the AIR view's controls tucked away (as they start)
      else if (!std::strcmp(argv[i], "panel"))
         host.airPanelHidden = false; // the AIR view's controls open
      else if (!host.apply(argv[i])) {
         std::fprintf(stderr, "unknown setting: %s\n", argv[i]);
         return 1;
      }
   return renderScreenshot(host, width, height, frames, argv[1], options) ? 0 : 1;
}
