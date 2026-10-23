#pragma once

// An ADSR envelope you edit by dragging its corners: the peak sets attack, the knee sets decay (x)
// and sustain (y), the end sets release. Each stage is a ValueAxis with its own range, so it binds
// to four params. Double-click a corner to reset its stage(s); stage values float above while
// dragging.

#include <functional>
#include <vector>

#include <visage/ui.h>

#include "jclap/ui/theme.h"
#include "jclap/ui/value_control.h"

namespace jclap::ui {

class EnvelopeEditor : public visage::Frame {
public:
   enum Stage { kAttack, kDecay, kSustain, kRelease, kStageCount };

   EnvelopeEditor(ValueRange attack = {}, ValueRange decay = {}, ValueRange sustain = {}, ValueRange release = {});
   ~EnvelopeEditor() override;

   ValueAxis &stage(Stage s) { return stages_[s]; }

   // Host updates, ignored while the user drags.
   void setValues(double attack, double decay, double sustain, double release);
   void setValue(Stage s, double value, double modulated);

   // Times (attack, decay, release) are drawn on a curve so short ones stay visible: position is
   // fraction^timeCurve of the stage's width. 1 is linear; the default 0.5 suits millisecond ranges.
   void setTimeCurve(float curve);

   // Where a playing envelope is now: its stage, its level (0..1), and in release the level it was
   // let go at. Each shows as a glowing dot riding the curve.
   struct Playhead {
      Stage stage = kAttack;
      float level = 0.0f, releaseFrom = 1.0f;
      bool operator==(const Playhead &o) const {
         return stage == o.stage && level == o.level && releaseFrom == o.releaseFrom;
      }
   };
   // One per sounding voice; feed them from a timer, and an empty list when nothing plays.
   void setPlayheads(std::vector<Playhead> playheads);

   void setAccent(unsigned color);
   void setTheme(const Theme &theme);

   // Adds items to the right-click menu, after "Reset to default".
   std::function<void(visage::PopupMenu &)> onContextMenu;

   void draw(visage::Canvas &canvas) override;
   void mouseDown(const visage::MouseEvent &e) override;
   void mouseDrag(const visage::MouseEvent &e) override;
   void mouseUp(const visage::MouseEvent &e) override;

private:
   enum Handle { kNone = -1, kPeak, kKnee, kEnd };

   // The curve's corners for the given stage values, in local coordinates.
   struct Points {
      visage::Point start, peak, knee, hold, end;
   };
   Points points(bool modulated) const;
   visage::Bounds area() const;
   Handle handleAt(visage::Point p) const;
   // The stages a handle moves.
   void stagesOf(Handle h, Stage &a, Stage &b) const;
   void moveTo(visage::Point p);
   void resetHandle(Handle h);
   std::string popupText() const;

   const Theme *theme_ = &defaultTheme();
   ValueAxis stages_[kStageCount];
   unsigned accent_ = 0;
   float timeCurve_ = 0.5f;
   Handle dragging_ = kNone;
   std::vector<Playhead> playheads_;
   ValuePopup popup_;
};

} // namespace jclap::ui
