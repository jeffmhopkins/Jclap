// Renders JComp's panel offscreen to a PNG, for the README and visual checks.
//
//    jcomp-screenshot out.png [generate|chords|edit|play] [width height] [players] [browser] [notes] [custom]
//
// The panel shows the Funk "Pocket Stabs" song, playing the chorus a few steps in, following the
// host. `players`, `browser` and `notes` open those overlays; `custom` shows the notes card's CUSTOM
// map with one note learned and one learning.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "editor.h"
#include "harmony.h"
#include "params.h"
#include "recipe.h"

namespace {

using namespace jcomp;

class DemoHost : public EditorHost {
public:
   DemoHost() {
      for (clap_id i = 0; i < kParamCount; ++i)
         values_[i] = kParams[i].defaultValue;
      for (const auto &[id, v] : styleValues(0, 0))
         values_[id] = v;
      values_[kSeed] = 4021;
      generateSong(recipeFrom([this](clap_id id) { return values_[id]; }), song_);
      song_.sections[kVerse].locked = 1;
      song_.sections[kChorus].generation = 2;
      frame_.running = frame_.following = frame_.hostPlaying = true;
      frame_.tempo = 104.0;
      frame_.bar = 1;
      frame_.beat = 2;
      frame_.section = kChorus;
      frame_.take = 0;
      frame_.queued = kBridge;
      frame_.step = 9;
      const Section &sec = song_.sections[kChorus];
      frame_.chord = sec.chordAt(1, false);
      frame_.next = sec.chordAt(2 % std::max<int>(1, sec.progBars), false);
      VoiceSettings vs = voiceSettings([this](clap_id id) { return values_[id]; });
      const Voicing v = voice(frame_.chord, nullptr, vs, -1, nullptr);
      for (int i = 0; i < v.count; ++i)
         frame_.voicing[static_cast<size_t>(i)] = v.notes[static_cast<size_t>(i)];
      frame_.voices = v.count;
      mutateSection = kChorus;
      chordSection = kChorus;
      chordSlot = 2;
      editSection = kChorus;
      editTake = 0;
      // EDIT picks the take's second hit.
      const Pattern &p = sec.takes[0];
      int n = 0;
      for (int st = 0; st < p.steps; ++st)
         if (p.at(st).on() && n++ == 1) {
            editStep = st;
            break;
         }
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
   std::string presetName() const override { return "Pocket Stabs"; }
   void setPresetName(const std::string &) override {}

   Song song_;
   double values_[kParamCount] = {};
   Frame frame_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [generate|chords|edit|play] [width height] [players] [browser] [notes] [custom]\n",
                   argv[0]);
      return 2;
   }
   DemoHost host;
   int page = 0, width = kEditorWidth, height = kEditorHeight, numbers = 0;
   for (int i = 2; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "generate")
         page = 0;
      else if (a == "chords")
         page = 1;
      else if (a == "edit")
         page = 2;
      else if (a == "play")
         page = 3;
      else if (a == "players")
         host.playersOpen = true;
      else if (a == "browser")
         host.browserOpen = true;
      else if (a == "notes")
         host.notesOpen = true;
      else if (a == "custom") {
         host.notesOpen = true;
         host.values_[kNoteMap] = 1;
         host.values_[kLearnNote + kActChorus] = 60;
         host.learnAction = kActFill;
      } else if (!a.empty() && a[0] >= '0' && a[0] <= '9')
         (numbers++ == 0 ? width : height) = std::atoi(a.c_str());
   }
   if (!renderScreenshot(host, page, width, height, argv[1])) {
      std::fprintf(stderr, "render failed\n");
      return 1;
   }
   return 0;
}
