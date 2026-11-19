#pragma once

// JKit's panel. Kept behind this interface so plugin.cpp never includes Visage, and so the
// screenshot tool can draw it with made-up sounds and no model.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

#include "kit.h"
#include "params.h"

namespace jkit {

constexpr int kEditorWidth = 1080, kEditorHeight = 660;

// The Generate tab's settings. They're kept with the plugin's state, not as params: nothing about
// them changes the sound a host plays.
struct GenSettings {
   int engine = 0; // gen::Engine: 0 Stable Audio Open Small, 1 ORB, 2 Stable Audio Open 1.0
   std::string prompt;
   int role = kSnare;
   double seconds = 1.0;  // LENGTH
   int steps = 8;         // STEPS
   double guidance = 1.0; // GUIDANCE
   double variety = 1.0;  // VARIETY: how far apart the variants are
   double strange = 0.3;  // the random prompt's STRANGE slider
   uint32_t seed = 1;
   bool seedLocked = false;
   int variants = 4; // 1, 2, 4 or 8
   // SOUND or KIT: one sound (GENERATE makes variants) or a whole kit (GENERATE fills the pads), and
   // the kind of kit (kitgen.h's KitLayout).
   bool kitMode = false;
   int kitLayout = 0;
};

struct Recent {
   std::string prompt;
   int role = kPerc;
   uint32_t seed = 0;
   int64_t when = 0; // seconds since the epoch
};

struct Variant {
   SamplePtr sample;
   bool starred = false;
};

// The model's download and its state, for the MODEL card.
struct EngineStatus {
   enum class State { Missing, Downloading, Installed, Loading, Ready, Failed } state = State::Missing;
   uint64_t received = 0, total = 0; // while downloading
   uint64_t downloadBytes = 0;       // all of it
   uint64_t diskBytes = 0;           // on disk now
   std::string error;
   std::string folder; // where it lives, to show
};

class EditorHost : public jclap::ui::ParamAccess {
public:
   // ---- the kit ----
   virtual SamplePtr padSample(int pad) const = 0;
   // Puts a sound on a pad (null empties it).
   virtual void setPadSample(int pad, SamplePtr sample) = 0;
   // Swaps two pads: their sounds and every pad param.
   virtual void swapPads(int a, int b) = 0;
   // Plays a pad as its note would, or stops it.
   virtual void playPad(int pad, float velocity) = 0;
   virtual void stopPad(int pad) = 0;
   // Where a pad's sound is playing, 0..1 of the whole sample, or -1 while silent; and how loud it
   // is now, 0..1.
   virtual float padPosition(int pad) const = 0;
   virtual float padLevel(int pad) const = 0;
   // How many pads have been played so far (over MIDI or from the panel), and which was the last.
   virtual uint32_t padHits() const = 0;
   virtual int lastHitPad() const = 0;
   // The loudest dry kit, FX return and output samples since the last call, for the OUTPUT bars.
   virtual void meters(float &kit, float &fx, float &out) = 0;

   // ---- generating ----
   virtual GenSettings &gen() = 0;
   virtual Treatment &treatment() = 0;
   virtual std::vector<Variant> &variants() = 0;
   virtual std::vector<Recent> &recent() = 0;
   virtual EngineStatus engine() = 0;
   // Any engine's state, size and disk use (0 Stable Audio Open Small, 1 ORB, 2 Stable Audio Open 1.0),
   // for the engine picker. Walks its folder: call it when the picker opens, not every frame.
   virtual EngineStatus engineStatus(int engine) = 0;
   virtual void installEngine() = 0;
   virtual void removeEngine() = 0;
   virtual void cancelEngine() = 0;
   // Starts generating from gen(): `around` >= 0 makes variations of that variant instead (VARY).
   virtual void generate(int around = -1) = 0;
   virtual bool generating() const = 0;
   // 0..1, or -1 while it waits for the model to download or load.
   virtual double generateProgress() const = 0;
   // Picks up a finished generation; true when the variants changed. The panel calls it on its timer.
   virtual bool pollGeneration(std::string &error) = 0;
   // GENERATE KIT: starts one sound for one pad, with gen()'s engine, steps and guidance but this
   // prompt and length, and no variants. pollGeneration() leaves the variants alone for it and
   // takeKitSound() hands the sound over (named and with the role and prompt), once.
   virtual void generateOne(const std::string &prompt, int role, double seconds, const std::string &name) = 0;
   virtual SamplePtr takeKitSound() = 0;
   // Seconds the last generation took.
   virtual double lastRunSeconds() const = 0;
   // Plays a sound by itself, as the pads would at no pitch, or stops it (null).
   virtual void audition(SamplePtr sample) = 0;
   // Where the audition is, 0..1, or -1.
   virtual float auditionPosition() const = 0;

   // ---- the kit's name, kept with the state ----
   virtual std::string kitName() const = 0;
   virtual void setKitName(const std::string &name) = 0;

   // What the panel was showing, so it opens the same way again.
   int page = 0;           // 0 Kit, 1 Generate
   int selectedPad = 0;    // the Kit tab's
   int targetPad = 0;      // SEND TO PAD's
   int selectedVariant = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size, showing tab `page`, and saves it as a PNG.
// `overlay` 1 opens the engine picker over it, 2 the kit card (GENERATE in KIT).
bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath, int overlay = 0);

// A kit file's sounds sit beside it, with this extension in place of the preset's.
constexpr const char *kKitSoundsExtension = ".jkitsounds";

} // namespace jkit
