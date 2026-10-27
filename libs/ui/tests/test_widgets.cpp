// Widget input checks: calls the mouse handlers directly and records what reaches the params.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <jclap/core/test_host.h>
#include <jclap/ui/button.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/envelope.h>
#include <jclap/ui/theme.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>
#include <jclap/ui/updates.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/note_grid.h>
#include <jclap/ui/note_roll.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/presets.h>
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


visage::MouseEvent withShift(visage::MouseEvent e) {
   e.modifiers = visage::kModifierShift;
   return e;
}

void shiftMidDragCarriesOn() {
   // A coarse move, then Shift: the coarse move stays and only what follows is fine.
   ui::Knob knob(ui::ValueRange{0, 1, 0});
   knob.setBounds(0, 0, 100, 100);
   knob.mouseDown(press(50, 100));
   knob.mouseDrag(press(50, 40));
   const double coarse = knob.value();
   JCLAP_CHECK(coarse > 0.1);
   knob.mouseDrag(withShift(press(50, 40)));
   JCLAP_CHECK_EQ(knob.value(), coarse);
   knob.mouseDrag(withShift(press(50, 30)));
   const double fine = knob.value();
   JCLAP_CHECK(fine > coarse && fine - coarse < (coarse / 6.0) * 0.2);
   // Letting go of Shift doesn't multiply the fine part back up either.
   knob.mouseDrag(press(50, 30));
   JCLAP_CHECK_EQ(knob.value(), fine);
   knob.mouseUp(press(50, 30));
}

void envelopeDragsFromWhereItWasGrabbed() {
   // A 200 x 100 editor: a 6 point pad, so each time stage is 0.3 of 188 points wide.
   ui::EnvelopeEditor env(ui::ValueRange{0, 1, 0.25}, ui::ValueRange{0, 1, 0.25}, ui::ValueRange{0, 1, 0.5},
                          ui::ValueRange{0, 1, 0.25});
   env.setBounds(0, 0, 200, 100);
   const float stage = 188.0f * 0.3f, peak = 6.0f + 0.5f * stage; // the default time curve is a square root
   // Grabbing the corner a little off doesn't move it.
   env.mouseDown(press(peak + 5, 10));
   env.mouseDrag(press(peak + 5, 10));
   JCLAP_CHECK(std::fabs(env.stage(ui::EnvelopeEditor::kAttack).value - 0.25) < 1e-6);
   env.mouseDrag(press(peak + 15, 10));
   const double coarse = env.stage(ui::EnvelopeEditor::kAttack).value;
   JCLAP_CHECK(coarse > 0.3);
   // Shift: only what follows moves, and a fifth as far.
   env.mouseDrag(withShift(press(peak + 15, 10)));
   JCLAP_CHECK_EQ(env.stage(ui::EnvelopeEditor::kAttack).value, coarse);
   env.mouseDrag(withShift(press(peak + 25, 10)));
   const double fine = env.stage(ui::EnvelopeEditor::kAttack).value;
   JCLAP_CHECK(fine > coarse && fine - coarse < (coarse - 0.25) * 0.3);
   env.mouseUp(press(peak + 25, 10));

   // A mapping of its own that shows only half the range: a time past it sits at the end, and stays
   // put while the corner is held there.
   env.setTimeMapping([](double v) { return static_cast<float>(v * 2.0); }, [](float p) { return p * 0.5; });
   env.setValues(0.25, 0.25, 0.5, 0.9);
   // Attack and decay half way, the sustain plateau, then release at the end.
   const float end = 6.0f + 2.0f * stage + 188.0f * (1.0f - 3.0f * 0.3f);
   env.mouseDown(press(end, 94));
   env.mouseDrag(press(end + 3, 90));
   JCLAP_CHECK_EQ(env.stage(ui::EnvelopeEditor::kRelease).value, 0.9);
   env.mouseDrag(press(end - 0.5f * stage, 90));
   JCLAP_CHECK(std::fabs(env.stage(ui::EnvelopeEditor::kRelease).value - 0.25) < 0.01);
   env.mouseUp(press(end - 0.5f * stage, 90));
}

void secondButtonMidDrag() {
   // A right-press during a drag belongs to the drag; the first release ends its gesture.
   Params p;
   ui::ParamBindings bindings(p);
   ui::Knob knob(ui::ValueRange{0, 1, 0.5});
   knob.setBounds(0, 0, 100, 100);
   bindings.bind(knob, 1);
   knob.mouseDown(press(50, 50));
   knob.mouseDrag(press(50, 30));
   knob.mouseDown(press(150, 30, visage::kMouseButtonRight));
   knob.mouseUp(press(150, 30, visage::kMouseButtonRight));
   JCLAP_CHECK_EQ(p.open, 0);
   JCLAP_CHECK_EQ(p.count("begin"), 1);
   // Host updates get through again.
   knob.setValue(0.1);
   JCLAP_CHECK_EQ(knob.value(), 0.1);

   ui::XYPad pad(ui::ValueRange{}, ui::ValueRange{});
   pad.setBounds(0, 0, 100, 100);
   bindings.bind(pad, 2, 3);
   pad.mouseDown(press(30, 30));
   pad.mouseDown(press(130, 30, visage::kMouseButtonRight));
   pad.mouseUp(press(130, 30, visage::kMouseButtonRight));
   JCLAP_CHECK_EQ(p.open, 0);
}

void polarDotClickKeepsValue() {
   ui::XYPad pad(ui::ValueRange{}, ui::ValueRange{});
   pad.setGrid(ui::XYPad::Grid::Polar);
   pad.setBounds(0, 0, 100, 110); // a 100 x 100 pad over a 10 point label row
   pad.setValues(0.25, 0.25, 0.5, 0.5);
   // Where drawPolar puts the dot: a quarter turn up, half way out of the radius it runs on.
   const float size = 100.0f, radius = size * 0.5f - 1.0f - size * 0.07f * 0.5f;
   pad.mouseDown(press(50, 50 - 0.5f * radius));
   JCLAP_CHECK(std::fabs(pad.y().value - 0.5) < 0.01);
   JCLAP_CHECK(std::fabs(pad.x().value - 0.25) < 0.01);
   pad.mouseUp(press(50, 50 - 0.5f * radius));
}

void tooltipsOutlivedByFrames() {
   ui::Knob knob;
   knob.setBounds(0, 0, 100, 100);
   {
      ui::Tooltips tips;
      tips.add(knob, "Cutoff");
   }
   // The frame's callbacks are still registered; they must do nothing now.
   knob.onMouseEnter().callback(press(10, 10));
   knob.onMouseExit().callback(press(10, 10));
   knob.onMouseDown().callback(press(10, 10));
}

void dropdownRightClickDoesntPick() {
   Params p;
   ui::ParamBindings bindings(p);
   ui::Dropdown dropdown({"Saw", "Square"});
   dropdown.setBounds(0, 0, 100, 24);
   bindings.bind(dropdown, 1);
   dropdown.mouseDown(press(10, 10, visage::kMouseButtonRight));
   dropdown.mouseDown(press(10, 10, visage::kMouseButtonMiddle));
   JCLAP_CHECK(p.log.empty());
}

void measuresAtAScale() {
   // stringWidth needs a DPI scale (Debug Visage asserts without one); this is what popups use.
   const float w1 = ui::font(12.0f, 1.0f).stringWidth(U"D 400 ms", 8);
   const float w2 = ui::font(12.0f, 2.0f).stringWidth(U"D 400 ms", 8);
   JCLAP_CHECK(w1 > 10.0f && std::fabs(w1 - w2) < 2.0f);
   JCLAP_CHECK(ui::font(12.0f, 0.0f).stringWidth(U"x", 1) > 0.0f);
}

void stopsSnap() {
   // A loop length of 1, 2, 4, 8, 16, 32 or 55: drags, the wheel and the steppers land only on those.
   ui::ValueRange range{1, 55, 8, 1.0, false, true};
   range.stops = {1, 2, 4, 8, 16, 32, 55};
   JCLAP_CHECK_EQ(range.stepFrom(8, 1), 16.0);
   JCLAP_CHECK_EQ(range.stepFrom(8, -1), 4.0);
   JCLAP_CHECK_EQ(range.stepFrom(10, -1), 8.0);
   JCLAP_CHECK_EQ(range.stepFrom(55, 1), 55.0);
   JCLAP_CHECK_EQ(range.nearestStop(11), 8.0);
   JCLAP_CHECK_EQ(range.stopPosition(12), 3.5);

   Params p;
   ui::ParamBindings bindings(p);
   ui::Knob knob(range);
   knob.setBounds(0, 0, 100, 100);
   bindings.bind(knob, 1);
   knob.setValue(8);
   knob.mouseDown(press(50, 50));
   for (int dy = 1; dy <= 60; ++dy)
      knob.mouseDrag(press(50, 50.0f - dy * 2.0f));
   knob.mouseUp(press(50, 0));
   JCLAP_CHECK(!p.values.empty());
   for (double v : p.values)
      JCLAP_CHECK(std::find(range.stops.begin(), range.stops.end(), v) != range.stops.end());
   JCLAP_CHECK_EQ(p.values.back(), 55.0);

   Params q;
   ui::ParamBindings wheelBindings(q);
   ui::Knob wheeled(range);
   wheeled.setBounds(0, 0, 100, 100);
   wheelBindings.bind(wheeled, 1);
   wheeled.setValue(8);
   wheeled.mouseWheel(wheel(50, 50, 1.0f));
   JCLAP_CHECK(!q.values.empty() && q.values.back() == 16.0);

   Params r;
   ui::ParamBindings boxBindings(r);
   ui::NumberBox box(range);
   box.setStepButtons(true);
   box.setBounds(0, 0, 132, 28);
   boxBindings.bind(box, 1);
   box.setValue(8);
   box.mouseDown(press(128, 14));
   box.mouseUp(press(128, 14));
   JCLAP_CHECK(!r.values.empty() && r.values.back() == 16.0);
}

void noteRollLanes() {
   ui::NoteRoll roll;
   roll.setNotes({{0, 2, 60, 1.0f}, {2, 2, 67, 0.5f}});
   // Every semitone between the notes, two either side, highest first.
   const std::vector<int> range = roll.lanes();
   JCLAP_CHECK_EQ(range.size(), 12u);
   JCLAP_CHECK_EQ(range.front(), 69);
   JCLAP_CHECK_EQ(range.back(), 58);
   roll.setCompact(true);
   JCLAP_CHECK(roll.lanes() == (std::vector<int>{67, 60}));
   roll.setLanes({61, 72, 65, 68, 65});
   JCLAP_CHECK(roll.lanes() == (std::vector<int>{72, 68, 65, 61}));
   roll.setLanes({});
   JCLAP_CHECK(roll.lanes() == (std::vector<int>{67, 60}));
}

void presetFilesRoundTrip() {
   struct Values : ui::ParamAccess {
      double v[3] = {0.25, -3.0, 7.0};
      double paramValue(clap_id id) const override { return id < 3 ? v[id] : 0.0; }
      void beginEdit(clap_id) override {}
      void setParam(clap_id id, double x) override { v[id] = x; }
      void endEdit(clap_id) override {}
   } values;
   const ui::PresetFormat format{"JclapTest", 2, ".jtpreset", 3};
   const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jclap-preset-test";
   std::filesystem::create_directories(dir);
   const std::filesystem::path file = dir / (ui::presetFileName("Warm / Wide?") + format.extension);
   JCLAP_CHECK(file.filename().string() == "Warm  Wide.jtpreset");
   JCLAP_CHECK(ui::writePresetFile(file, format, values));
   ui::Preset back;
   int version = 0;
   JCLAP_CHECK(ui::readPresetFile(file, format, back, &version));
   JCLAP_CHECK_EQ(version, 2);
   JCLAP_CHECK_EQ(back.values.size(), 3u);
   for (const auto &[id, v] : back.values)
      JCLAP_CHECK_EQ(v, values.v[id]);

   // Another plugin's file, or a newer version than this one reads, is refused.
   const ui::PresetFormat other{"Other", 2, ".jtpreset", 3};
   JCLAP_CHECK(!ui::readPresetFile(file, other, back));
   const ui::PresetFormat older{"JclapTest", 1, ".jtpreset", 3};
   JCLAP_CHECK(!ui::readPresetFile(file, older, back));
   std::filesystem::remove_all(dir);

   // A factory preset filled out: what it lists, and the defaults for the rest.
   std::vector<ui::Preset> bank = {{"Init", "A", {}}, {"Loud", "A", {{1, 6.0}}}};
   ui::completePresets(bank, {0.5, 0.0, 1.0});
   JCLAP_CHECK_EQ(bank[0].values.size(), 3u);
   JCLAP_CHECK_EQ(bank[1].values.size(), 3u);
   for (const auto &[id, v] : bank[1].values)
      JCLAP_CHECK_EQ(v, id == 1 ? 6.0 : id == 0 ? 0.5 : 1.0);
}

void updateChecks() {
   namespace up = ui::updates;
   JCLAP_CHECK(up::isNewer("0.10.3", "0.10.2"));
   JCLAP_CHECK(up::isNewer("v1.0.0", "0.99.9"));
   JCLAP_CHECK(up::isNewer("0.11.0", "v0.10.12"));
   JCLAP_CHECK(!up::isNewer("0.10.2", "0.10.2"));
   JCLAP_CHECK(!up::isNewer("0.9.9", "0.10.0"));
   JCLAP_CHECK(!up::isNewer("dev", "0.10.2"));
   JCLAP_CHECK(!up::isNewer("0.10.3-beta", "0.10.2"));
   JCLAP_CHECK(!up::isNewer("", "0.10.2"));

   // Trimmed from a real reply: the release's tag, not the author's or an asset's fields.
   const std::string reply = R"({"url":"https://api.github.com/x","html_url":"https://github.com/jeffmhopkins/Jclap/releases/tag/v0.10.3",
      "author":{"login":"github-actions[bot]","site_admin":false},"tag_name" : "v0.10.3","draft":false,
      "prerelease": false,"assets":[{"name":"jclap-macOS.zip"}]})";
   JCLAP_CHECK(up::parseLatestRelease(reply) == "0.10.3");
   std::string pre = reply;
   pre.replace(pre.find("\"prerelease\": false"), 19, "\"prerelease\": true");
   JCLAP_CHECK(up::parseLatestRelease(pre).empty());
   JCLAP_CHECK(up::parseLatestRelease(R"({"tag_name":"dev","prerelease":false})").empty());
   JCLAP_CHECK(up::parseLatestRelease(R"({"message":"API rate limit exceeded"})").empty());
   JCLAP_CHECK(up::parseLatestRelease("").empty());

   // Due once a day, or when the clock went back; never when turned off.
   up::State state;
   state.checkedAt = 1000000;
   JCLAP_CHECK(!up::checkDue(state, 1000000 + 3600));
   JCLAP_CHECK(up::checkDue(state, 1000000 + 86400));
   JCLAP_CHECK(up::checkDue(state, 999000));
   state.enabled = false;
   JCLAP_CHECK(!up::checkDue(state, 1000000 + 86400));

   const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jclap-update-test";
   std::filesystem::remove_all(dir);
   const std::filesystem::path file = dir / "updates.txt";
   JCLAP_CHECK(up::readState(file).enabled); // no file: defaults

   // A fresh check that found a newer release: nothing is fetched, and the notice shows only while a
   // window holds the checker.
   const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
   const std::string newer = "99.0.0";
   JCLAP_CHECK(up::writeState(file, {true, static_cast<long long>(now), newer, ""}));
   const up::State back = up::readState(file);
   JCLAP_CHECK(back.enabled && back.checkedAt == now && back.latest == newer && back.dismissed.empty());
   up::setStateFileForTesting(file);
   JCLAP_CHECK(!up::available());
   {
      const auto handle = up::startChecking();
      const auto release = up::available();
      JCLAP_CHECK(release && release->version == newer);
      JCLAP_CHECK(release && release->url == "https://github.com/jeffmhopkins/Jclap/releases/tag/v99.0.0");

      ui::UpdateNotice notice;
      notice.setBounds(0, 0, 200, 26);
      notice.alignRight = true;
      JCLAP_CHECK(!notice.isVisible());
      notice.timerCallback();
      JCLAP_CHECK(notice.isVisible() && notice.release().version == newer);
      // A click beside the pill does nothing; the cross dismisses it, for good.
      notice.mouseDown(press(2, 13));
      JCLAP_CHECK(notice.isVisible());
      notice.mouseDown(press(190, 13));
      JCLAP_CHECK(!notice.isVisible());
      JCLAP_CHECK(!up::available());
      JCLAP_CHECK(up::readState(file).dismissed == newer);

      // Turned off: no notice, even for a release not dismissed.
      JCLAP_CHECK(up::writeState(file, {true, static_cast<long long>(now), "99.0.1", newer}));
      up::setStateFileForTesting(file);
      JCLAP_CHECK(up::available());
      up::setEnabled(false);
      JCLAP_CHECK(!up::available());
      JCLAP_CHECK(!up::readState(file).enabled);
   }
   JCLAP_CHECK(!up::available());
   up::setStateFileForTesting({});
   std::filesystem::remove_all(dir);
}
} // namespace

int main() {
   steppedDragSnaps();
   wheelEdgeCases();
   destroyedMidDragEndsGesture();
   rightClickDoesntToggle();
   noteGridSecondPress();
   shiftMidDragCarriesOn();
   envelopeDragsFromWhereItWasGrabbed();
   secondButtonMidDrag();
   polarDotClickKeepsValue();
   tooltipsOutlivedByFrames();
   dropdownRightClickDoesntPick();
   measuresAtAScale();
   stopsSnap();
   noteRollLanes();
   presetFilesRoundTrip();
   updateChecks();
   return jclap::core::testResult();
}
