// Renders JDrummer's panel offscreen to a PNG, for the README and visual checks.
//
//    jdrummer-screenshot out.png [generate|edit|play] [width height] [players] [browser] [notes]
//                        [custom] [addpad]
//
// The panel shows the House "Classic Four" song with a conguero, playing the chorus a few steps in,
// following the host. `players`, `browser`, `notes` and `addpad` open those overlays; `custom`
// shows the notes card's CUSTOM map with one note learned and one learning.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "editor.h"
#include "params.h"
#include "recipe.h"

namespace {

using namespace jdrummer;

class DemoHost : public EditorHost {
public:
   DemoHost() {
      for (clap_id i = 0; i < kParamCount; ++i)
         values_[i] = kParams[i].defaultValue;
      for (const auto &[id, v] : styleValues(0, 0))
         values_[id] = v;
      values_[kPercussionist] = 1; // the conguero
      values_[kSeed] = 4021;
      values_[kSectionEnergy + kBreak] = 0.08; // a near-silent break
      generateSong(recipeFrom([this](clap_id id) { return values_[id]; }), song_);
      song_.sections[kVerse].locked = 1;
      song_.sections[kChorus].generation = 2;
      frame_.running = frame_.following = frame_.hostPlaying = true;
      frame_.tempo = 124.0;
      frame_.bar = 5;
      frame_.beat = 2;
      frame_.section = kChorus;
      frame_.variation = 1;
      frame_.queued = kBridge;
      frame_.step = 9;
      for (int pad : {0, 6, 3})
         frame_.hits[static_cast<size_t>(pad)] = 1;
      mutateSection = kChorus;
      editSection = kChorus;
      editVar = 0;
      editPad = 2;
      // EDIT picks the snare's lane, or the highest backbeat pad the chorus plays if not the snare.
      Pattern &p = song_.sections[kChorus].vars[0];
      for (int pad = 0; pad < kPads; ++pad)
         if (p.uses(pad) && pad != 0 && editPad == 2 && !p.uses(2))
            editPad = pad;
   }

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id] : 0.0; }
   void beginEdit(clap_id) override {}
   void setParam(clap_id id, double v) override {
      if (id < kParamCount)
         values_[id] = v;
   }
   void endEdit(clap_id) override {}

   const Frame &frame() override { return frame_; }
   const Song &song() const override { return song_; }
   void setSong(const Song &s) override { song_ = s; }
   void request(int) override {}
   void armLearn(bool) override {}
   int takeLearned() override { return -1; }
   std::string presetName() const override { return "Classic Four"; }
   void setPresetName(const std::string &) override {}

   Song song_;
   double values_[kParamCount] = {};
   Frame frame_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [generate|edit|play] [width height] [players] [browser] [notes] [custom] [addpad]\n",
                   argv[0]);
      return 2;
   }
   DemoHost host;
   int page = 0, width = kEditorWidth, height = kEditorHeight, numbers = 0;
   for (int i = 2; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "generate")
         page = 0;
      else if (a == "edit")
         page = 1;
      else if (a == "play")
         page = 2;
      else if (a == "players")
         host.playersOpen = true;
      else if (a == "browser")
         host.browserOpen = true;
      else if (a == "notes")
         host.notesOpen = true;
      else if (a == "addpad")
         host.addPadOpen = true;
      else if (a == "custom") {
         host.notesOpen = true;
         host.values_[kNoteMap] = 1;
         host.values_[kLearnNote + kActChorus] = 60;
         host.learnAction = kActFill;
      } else if (!a.empty() && a[0] >= '0' && a[0] <= '9')
         (numbers++ == 0 ? width : height) = std::atoi(a.c_str());
   }
   // EDIT shows a few of the hits' own values.
   if (page == 1) {
      host.frame_.variation = 0; // so the playhead shows on the pattern being edited
      Pattern &p = host.song_.sections[kChorus].vars[0];
      const int pad = host.editPad;
      int n = 0;
      for (int st = 0; st < p.steps; ++st) {
         Hit &h = p.at(pad, st);
         if (!h.on())
            continue;
         if (n == 1)
            h.nudge = 8;
         if (n == 2)
            h.ratchet = 2;
         ++n;
      }
      Hit &chance = p.at(6, 15);
      chance = Hit{};
      chance.vel = kSoft;
      chance.chance = 50;
   }
   if (!renderScreenshot(host, page, width, height, argv[1])) {
      std::fprintf(stderr, "render failed\n");
      return 1;
   }
   return 0;
}
