#pragma once

// A preset browser for a panel's header: arrows (or the wheel) step through every preset, the name
// opens a menu of them by category, and SAVE names the current settings and writes them to the
// user's preset folder. The factory bank comes first, then the user's presets under "User". The star
// beside the name marks the current preset a favorite, and the menu lists favorites first.
//
//    auto &bar = add<PresetBar>({380, 12, 340, 28}, factoryPresets(), format, params);
//    bar.currentName = [&] { return host.presetName(); };
//    bar.setCurrentName = [&](const std::string &n) { host.setPresetName(n); };
//
// It is drawn for a height of about 28 and scales with it.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <visage/ui.h>

#include "jclap/ui/params.h"
#include "jclap/ui/presets.h"
#include "jclap/ui/theme.h"

namespace visage {
class TextEditor;
}

namespace jclap::ui {

class PresetBar : public visage::Frame {
public:
   // `factory` must outlive the bar (it is usually a static bank).
   PresetBar(const std::vector<Preset> &factory, PresetFormat format, ParamAccess &params);
   ~PresetBar() override;

   // The name of the preset last loaded or saved, which the plugin keeps with its state. Without
   // these the bar keeps the name itself.
   std::function<std::string()> currentName;
   std::function<void(const std::string &)> setCurrentName;
   // Applies a preset. By default each param the preset lists that differs is set as one edit,
   // with any param it leaves out at `defaults` (when given). A plugin that coalesces a whole
   // preset's edits, or restarts something after a load, sets its own.
   std::function<void(const Preset &)> onLoad;
   std::vector<double> defaults;

   void setTheme(const Theme &theme);
   void setAccent(unsigned color);

   // Loads the preset `dir` places after (or before) the current one, wrapping around.
   void step(int dir);
   // Loads a preset by name; false if there is none.
   bool load(const std::string &name);
   // Re-reads the user's preset folder. Done when the bar is made, when the menu opens and after a
   // save, never on an arrow click, so stepping through presets doesn't wait on the disk.
   void rescan();
   // Every preset in menu order: the factory bank, then the user's.
   int total() const;
   const Preset &at(int index) const;
   // Stars the current preset, or unstars it. False if the current name isn't a preset.
   bool toggleFavorite();
   const PresetFavorites &favorites() const { return favorites_; }

   void draw(visage::Canvas &canvas) override;
   void mouseMove(const visage::MouseEvent &e) override;
   void mouseExit(const visage::MouseEvent &e) override;
   void mouseDown(const visage::MouseEvent &e) override;
   bool mouseWheel(const visage::MouseEvent &e) override;

private:
   enum class Zone { None, Prev, Name, Star, Next, Save };

   Zone zoneAt(float x) const;
   void setHover(Zone z);
   std::string name() const;
   void rename(const std::string &name);
   int indexOf(const std::string &name) const;
   void loadIndex(int index);
   void showMenu();
   void confirmDelete();
   void startSave();
   void finishSave(bool apply);
   std::string uniqueName(const std::string &wanted);

   const std::vector<Preset> &factory_;
   PresetFormat format_;
   ParamAccess &params_;
   PresetFavorites favorites_;
   const Theme *theme_ = &defaultTheme();
   unsigned accent_ = 0;
   std::vector<Preset> user_;
   int currentIndex_ = -1;
   std::string ownName_ = "Init";
   std::unique_ptr<visage::TextEditor> editor_;
   Zone hover_ = Zone::None;
};

// A five-pointed star centred on (cx, cy), filled or as an outline, in the canvas's current color.
// Preset bars draw it beside the name to mark a favorite.
void drawStar(visage::Canvas &canvas, float cx, float cy, float radius, bool filled);

// A die showing five, `size` across and centred on (cx, cy), in the canvas's current color. A plugin
// with dice draws it at the left of its preset bar, opposite the star, to open its dice card.
void drawDie(visage::Canvas &canvas, float cx, float cy, float size);

// Applies `preset` through `params`, one gesture per param that changes. Params the preset leaves
// out go to `defaults` (one per id) when it is given.
void applyPreset(const Preset &preset, ParamAccess &params, const std::vector<double> &defaults = {});

} // namespace jclap::ui
