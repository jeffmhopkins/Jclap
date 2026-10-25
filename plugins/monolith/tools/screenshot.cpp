// Renders the panel to a PNG without a host: monolith-screenshot out.png [orthography [width height]]
//
// The demo slab is the default script after a few bars of reading, with one glyph worn down and the
// voice partway through cell 3.

#include <cstdio>
#include <cstdlib>

#include "editor.h"
#include "engine.h"
#include "params.h"

namespace {

using namespace monolith;

class DemoHost : public EditorHost {
public:
   explicit DemoHost(int orthography) : orthography_(orthography) {
      EngineParams p;
      p.length = 12;
      p.drift = 0.0;
      p.orthography = static_cast<Orthography>(orthography);
      engine_.setParams(p);
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
   std::string presetName() const override { return "Phrygian Engine"; }
   void setPresetName(const std::string &) override {}

private:
   int orthography_;
   Engine engine_;
   SlabFrame frame_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [orthography [width height]]\n", argv[0]);
      return 1;
   }
   const int orthography = argc > 2 ? std::atoi(argv[2]) : 0;
   const int width = argc > 4 ? std::atoi(argv[3]) : 1200;
   const int height = argc > 4 ? std::atoi(argv[4]) : 680;
   DemoHost host(orthography);
   return renderScreenshot(host, width, height, -1, argv[1]) ? 0 : 1;
}
