// Renders the panel to a PNG without a host:
//
//    monolith-screenshot out.png [orthography [width height]] [listen]
//
// The demo slab is the default script after a few bars of reading, with one glyph worn down and the
// voice partway through cell 3. With `listen`, it is in LISTEN instead: three bars have been played
// in and carved, and the fourth is being carved, its last note still held.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "editor.h"
#include "engine.h"
#include "params.h"

namespace {

using namespace monolith;

class DemoHost : public EditorHost {
public:
   DemoHost(int orthography, bool listen) : orthography_(orthography), listen_(listen) {
      EngineParams p;
      p.length = 12;
      p.drift = 0.0;
      p.orthography = static_cast<Orthography>(orthography);
      p.mode = listen ? Mode::Listen : Mode::Speak;
      engine_.setParams(p);
      if (listen) {
         playIn();
         return;
      }
      Command erode;
      erode.kind = Command::Kind::Load;
      erode.cell = 5;
      erode.glyph = engine_.cells()[5].glyph;
      erode.word = engine_.cells()[5].word;
      erode.eroded = 2;
      engine_.apply(erode);
      // Play from the top until the voice is a quarter of the way into the fourth glyph.
      std::vector<NoteOut> out;
      Block b;
      b.hasTransport = true;
      b.playing = true;
      b.frames = 480;
      for (int i = 0; i < 1300; ++i) {
         b.beats = i * (480.0 / 48000.0) * 2.0;
         engine_.process(b, nullptr, 0, out);
         out.clear();
         if (b.beats >= 3 * 4.0 + 1.0)
            break;
      }
      engine_.fillFrame(frame_);
   }
   double paramValue(clap_id id) const override {
      if (id == kMode)
         return listen_ ? 1 : 0;
      if (id == kLength)
         return 12;
      if (id == kDrift)
         return 0;
      if (id == kOrthography)
         return orthography_;
      return id < kParamCount ? kParams[id].defaultValue : 0.0;
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   const SlabFrame &frame() override { return frame_; }
   void command(const Command &) override {}
   std::string presetName() const override { return "Init"; }
   void setPresetName(const std::string &) override {}

private:
   // Plays a phrase in, a note at a time (beat, pitch, velocity, length in beats; a length of 0 is
   // still held), stopping partway into the fourth bar.
   void playIn() {
      struct Played {
         double beat;
         uint8_t pitch, velocity;
         double length;
      };
      const Played phrase[] = {
         {0.0, 69, 100, 0.5},  {0.5, 72, 85, 0.5},   {1.0, 76, 120, 1.0},  {2.0, 79, 100, 1.5},
         {4.0, 76, 120, 0.75}, {4.75, 74, 85, 0.25}, {5.0, 72, 100, 1.0},  {6.0, 69, 85, 0.5},  {6.5, 67, 100, 1.5},
         {8.0, 65, 100, 1.0},  {9.0, 69, 120, 0.5},  {9.5, 72, 100, 0.5},  {10.0, 76, 85, 2.0},
         {12.0, 81, 120, 0.5}, {12.75, 76, 100, 0.25}, {13.25, 84, 110, 0.0}};
      const double perFrame = 2.0 / 48000.0;
      std::vector<NoteOut> out;
      Block b;
      b.hasTransport = true;
      b.playing = true;
      b.frames = 480;
      for (int i = 0;; ++i) {
         b.beats = i * 480 * perFrame;
         const double end = b.beats + 480 * perFrame;
         if (b.beats >= 13.9)
            break;
         std::vector<NoteIn> in;
         for (const Played &n : phrase) {
            const auto at = [&](double beat, bool on) {
               if (beat >= b.beats && beat < end)
                  in.push_back({static_cast<uint32_t>((beat - b.beats) / perFrame), n.pitch,
                                static_cast<uint8_t>(on ? n.velocity : 0), on});
            };
            at(n.beat, true);
            if (n.length > 0.0)
               at(n.beat + n.length, false);
         }
         std::stable_sort(in.begin(), in.end(), [](const NoteIn &a, const NoteIn &c) { return a.time < c.time; });
         engine_.process(b, in.data(), static_cast<int>(in.size()), out);
         out.clear();
      }
      engine_.fillFrame(frame_);
   }

   int orthography_;
   bool listen_;
   Engine engine_;
   SlabFrame frame_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [orthography [width height]] [listen]\n", argv[0]);
      return 1;
   }
   bool listen = false;
   if (std::strcmp(argv[argc - 1], "listen") == 0) {
      listen = true;
      --argc;
   }
   const int orthography = argc > 2 ? std::atoi(argv[2]) : 0;
   const int width = argc > 4 ? std::atoi(argv[3]) : 1200;
   const int height = argc > 4 ? std::atoi(argv[4]) : 680;
   DemoHost host(orthography, listen);
   return renderScreenshot(host, width, height, -1, argv[1]) ? 0 : 1;
}
