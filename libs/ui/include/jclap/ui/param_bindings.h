#pragma once

// Connects widgets to CLAP params through a ParamAccess. Edits go out as gestures, a timer pulls
// host changes (automation, modulation, state loads) back into the widgets, and right-click menus
// get the host's own items for the param.
//
//    ParamBindings bindings{access};
//    bindings.bind(cutoffKnob, kCutoffId);
//    bindings.bind(bypassButton, kBypassId);   // becomes an on/off toggle
//    bindings.bind(modeDropdown, kModeId);     // choice i is the value firstValue + i
//    bindings.bind(pad, kXId, kYId);

#include <vector>

#include <visage/ui.h>

#include "jclap/ui/button.h"
#include "jclap/ui/dropdown.h"
#include "jclap/ui/params.h"
#include "jclap/ui/value_control.h"
#include "jclap/ui/xy_pad.h"

namespace jclap::ui {

class ParamBindings : public visage::EventTimer {
public:
   explicit ParamBindings(ParamAccess &access, int pollMs = 30);

   // Knob or Slider. Its range stays as set; this wires the callbacks and the current value.
   void bind(ValueControl &control, clap_id id);
   // Makes the button a toggle: on is 1, off is 0, and any value >= 0.5 shows as on.
   void bind(Button &button, clap_id id);
   void bind(Dropdown &dropdown, clap_id id);
   void bind(XYPad &pad, clap_id xId, clap_id yId);

   // Pulls the current values into every bound widget. The timer calls it.
   void sync();
   void timerCallback() override { sync(); }

   // Appends the host's menu for `id` to `menu`, if the host has one.
   void addHostMenu(visage::PopupMenu &menu, clap_id id);

private:
   struct Bound {
      ValueControl *control = nullptr;
      Button *button = nullptr;
      Dropdown *dropdown = nullptr;
      XYPad *pad = nullptr;
      clap_id id = 0, id2 = 0;
   };

   void wireAxis(XYPad::Axis &axis, clap_id id);

   ParamAccess &access_;
   std::vector<Bound> bound_;
};

} // namespace jclap::ui
