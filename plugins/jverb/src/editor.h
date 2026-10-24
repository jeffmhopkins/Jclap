#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <memory>
#include <string>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

#include "shared.h"

namespace jverb {

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   // Copies the newest orbit frame into `out`; false if nothing new arrived since the last call.
   virtual bool readFrame(OrbitFrame &out) = 0;
   // Drags and clicks on the orbit view, for the audio thread.
   virtual void send(const OrbitCommand &command) = 0;
   // The loudest output samples since the last call, per channel, for the meters.
   virtual void outputPeaks(float &left, float &right) = 0;
   // The name of the preset last loaded or saved, kept with the plugin's state.
   virtual std::string presetName() const = 0;
   virtual void setPresetName(const std::string &name) = 0;
   // Sets every param at once (`values` holds kParamCount of them), as loading a preset does. Each
   // param that changes is one edit for the host. The plugin coalesces these, so clicking quickly
   // through presets sends the host the latest values instead of a backlog that could overflow.
   virtual void setParams(const double *values) {
      for (clap_id id = 0; id < kParamCount; ++id) {
         if (paramValue(id) == values[id])
            continue;
         beginEdit(id);
         setParam(id, values[id]);
         endEdit(id);
      }
   }
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG. The view is run for
// `seconds` of frames first (pulled from the host), so the trails and gas have built up.
// `spacePage` picks the left card's page: 0 SPACE, 1 SHAPE, 2 ORBIT.
bool renderScreenshot(EditorHost &host, double seconds, int width, int height, const char *pngPath,
                      int spacePage = 0);

} // namespace jverb
