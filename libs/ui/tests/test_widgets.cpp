// Widget input checks: calls the mouse handlers directly and records what reaches the params.

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <jclap/core/test_host.h>
#include <jclap/ui/button.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/note_grid.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/xy_pad.h>

namespace ui = jclap::ui;

namespace {

visage::MouseEvent press(float x, float y, visage::MouseButton button = visage::kMouseButtonLeft) {
   visage::MouseEvent e;
   e.position = {x, y};
   e.button_id = button;
   e.button_state = button;
   e.is_down = true;
   e.repeat_click_count = 1;
   return e;
}

visage::MouseEvent wheel(float x, float y, float delta) {
   visage::MouseEvent e = press(x, y, visage::kMouseButtonNone);
   e.is_down = false;
   e.wheel_delta_y = delta;
   return e;
}

// Logs every gesture and value, and checks gestures pair up.
struct Params : ui::ParamAccess {
   std::vector<std::string> log;
   std::vector<double> values;
   int open = 0;
   bool nested = false;

   double paramValue(clap_id) const override { return values.empty() ? 0.0 : values.back(); }
   void beginEdit(clap_id) override {
      log.push_back("begin");
      nested = nested || open > 0;
      ++open;
   }
   void setParam(clap_id, double v) override {
      log.push_back("set");
      values.push_back(v);
   }
   void endEdit(clap_id) override {
      log.push_back("end");
      --open;
   }
   int count(const char *what) const {
      int n = 0;
      for (const std::string &s : log)
         n += s == what;
      return n;
   }
};

void steppedDragSnaps() {
   Params p;
   ui::ParamBindings bindings(p);
   ui::NumberBox box(ui::ValueRange{-24, 24, 0, 1.0, false, true});
   box.setBounds(0, 0, 132, 28);
   bindings.bind(box, 1);
   box.mouseDown(press(60, 14));
   for (int dy = 1; dy <= 40; ++dy)
      box.mouseDrag(press(60, 14.0f - dy * 0.7f));
   box.mouseUp(press(60, 0));
   JCLAP_CHECK(!p.values.empty());
   for (double v : p.values)
      JCLAP_CHECK_EQ(v, std::round(v));
   JCLAP_CHECK_EQ(p.open, 0);

   // Unstepped ranges still drag smoothly.
   Params q;
   ui::ParamBindings smooth(q);
   ui::Knob knob(ui::ValueRange{0, 100, 0, 1.0});
   knob.setBounds(0, 0, 100, 100);
   smooth.bind(knob, 2);
   knob.mouseDown(press(50, 50));
   knob.mouseDrag(press(50, 49));
   knob.mouseUp(press(50, 49));
   JCLAP_CHECK(!q.values.empty() && q.values.back() != std::round(q.values.back()));
}

void wheelEdgeCases() {
   // min == max: nothing is sent (it used to be NaN).
   Params p;
   ui::ParamBindings bindings(p);
   ui::Knob flat(ui::ValueRange{1, 1, 1});
   flat.setBounds(0, 0, 100, 100);
   bindings.bind(flat, 1);
   JCLAP_CHECK(!flat.mouseWheel(wheel(50, 50, 1)));
   JCLAP_CHECK(p.log.empty());
   JCLAP_CHECK_EQ(ui::ValueRange{}.clamp(std::nan("")), 0.0);

   // A wheel step mid-drag doesn't open a second gesture.
   Params q;
   ui::ParamBindings drag(q);
   ui::Knob knob(ui::ValueRange{0, 1, 0.5});
   knob.setBounds(0, 0, 100, 100);
   drag.bind(knob, 2);
   knob.mouseDown(press(50, 50));
   knob.mouseDrag(press(50, 40));
   knob.mouseWheel(wheel(50, 40, 1));
   knob.mouseDrag(press(50, 30));
   knob.mouseUp(press(50, 30));
   JCLAP_CHECK(!q.nested);
   JCLAP_CHECK_EQ(q.count("begin"), 1);
   JCLAP_CHECK_EQ(q.count("end"), 1);
}

void destroyedMidDragEndsGesture() {
   Params p;
   ui::ParamBindings bindings(p);
   auto knob = std::make_unique<ui::Knob>(ui::ValueRange{0, 1, 0.5});
   knob->setBounds(0, 0, 100, 100);
   bindings.bind(*knob, 1);
   knob->mouseDown(press(50, 50));
   knob->mouseDrag(press(50, 40));
   knob.reset();
   JCLAP_CHECK_EQ(p.open, 0);
   JCLAP_CHECK_EQ(p.count("end"), 1);

   // The XY pad opens one gesture per axis.
   auto pad = std::make_unique<ui::XYPad>(ui::ValueRange{}, ui::ValueRange{});
   pad->setBounds(0, 0, 100, 100);
   bindings.bind(*pad, 2, 3);
   pad->mouseDown(press(30, 30));
   pad.reset();
   JCLAP_CHECK_EQ(p.open, 0);

   // Bindings destroyed first (a panel member declared before its widgets): they end the gesture,
   // and the widget's own end afterwards is a no-op.
   Params q;
   ui::Knob outlives(ui::ValueRange{0, 1, 0.5});
   outlives.setBounds(0, 0, 100, 100);
   {
      ui::ParamBindings shortLived(q);
      shortLived.bind(outlives, 4);
      outlives.mouseDown(press(50, 50));
   }
   JCLAP_CHECK_EQ(q.open, 0);
   outlives.mouseUp(press(50, 50));
   JCLAP_CHECK_EQ(q.open, 0);
   JCLAP_CHECK_EQ(q.count("end"), 1);
}

void rightClickDoesntToggle() {
   Params p;
   ui::ParamBindings bindings(p);
   ui::Button button("MUTE");
   button.setBounds(0, 0, 56, 30);
   bindings.bind(button, 1);
   button.mouseDown(press(10, 10, visage::kMouseButtonRight));
   JCLAP_CHECK(!button.isOn());
   JCLAP_CHECK(p.log.empty());
   button.mouseDown(press(10, 10));
   JCLAP_CHECK(button.isOn());
}

void noteGridSecondPress() {
   ui::NoteGrid grid(16, 8, 36);
   grid.setBounds(0, 0, 160, 80);
   std::vector<int> held;
   grid.onNoteOn = [&](int note, float) { held.push_back(note); };
   grid.onNoteOff = [&](int note) {
      for (auto it = held.begin(); it != held.end(); ++it) {
         if (*it == note) {
            held.erase(it);
            break;
         }
      }
   };
   grid.mouseDown(press(5, 75));
   grid.mouseDown(press(25, 75, visage::kMouseButtonRight));
   grid.mouseUp(press(25, 75, visage::kMouseButtonRight));
   grid.mouseUp(press(25, 75));
   JCLAP_CHECK(held.empty());
}

} // namespace

int main() {
   steppedDragSnaps();
   wheelEdgeCases();
   destroyedMidDragEndsGesture();
   rightClickDoesntToggle();
   noteGridSecondPress();
   return jclap::core::testResult();
}
