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
//    bindings.bind(envelope, kAttack, kDecay, kSustain, kRelease);

#include <vector>

#include <visage/ui.h>

#include "jclap/ui/button.h"
#include "jclap/ui/dropdown.h"
#include "jclap/ui/envelope.h"
#include "jclap/ui/params.h"
#include "jclap/ui/segmented.h"
#include "jclap/ui/value_control.h"
#include "jclap/ui/xy_pad.h"

namespace jclap::ui {

class ParamBindings : public visage::EventTimer {
public:
   explicit ParamBindings(ParamAccess &access, int pollMs = 30);

   // Knob, Slider or NumberBox. Its range stays as set; this wires the callbacks and the current value.
   void bind(ValueControl &control, clap_id id);
   // Makes the button a toggle: on is 1, off is 0, and any value >= 0.5 shows as on.
   void bind(Button &button, clap_id id);
   void bind(Dropdown &dropdown, clap_id id);
   void bind(SegmentedSwitch &segments, clap_id id);
   void bind(XYPad &pad, clap_id xId, clap_id yId);
   void bind(EnvelopeEditor &envelope, clap_id attack, clap_id decay, clap_id sustain, clap_id release);

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
      SegmentedSwitch *segments = nullptr;
      EnvelopeEditor *envelope = nullptr;
      clap_id id = 0, id2 = 0, id3 = 0, id4 = 0;
   };

   void wireAxis(ValueAxis &axis, clap_id id);
   void sync(const Bound &b);

   ParamAccess &access_;
   std::vector<Bound> bound_;
};

} // namespace jclap::ui
