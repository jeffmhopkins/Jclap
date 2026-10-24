#include "jclap/ui/param_bindings.h"

#include <algorithm>
#include <cmath>

namespace jclap::ui {

ParamBindings::ParamBindings(ParamAccess &access, int pollMs)
   : access_(access), gestures_(std::make_shared<Gestures>(Gestures{&access, {}})) {
   startTimer(pollMs);
}

ParamBindings::~ParamBindings() {
   for (clap_id id : gestures_->open)
      access_.endEdit(id);
   gestures_->open.clear();
   gestures_->access = nullptr;
}

void ParamBindings::Gestures::begin(clap_id id) {
   if (!access)
      return;
   open.push_back(id);
   access->beginEdit(id);
}

void ParamBindings::Gestures::end(clap_id id) {
   const auto it = std::find(open.begin(), open.end(), id);
   if (!access || it == open.end())
      return;
   open.erase(it);
   access->endEdit(id);
}

void ParamBindings::bind(ValueControl &control, clap_id id) {
   control.onBeginEdit = [g = gestures_, id] { g->begin(id); };
   control.onChange = [this, id](double v) { access_.setParam(id, v); };
   control.onEndEdit = [g = gestures_, id] { g->end(id); };
   control.modulatedSource = [this, id] { return access_.paramModulated(id); };
   control.onContextMenu = [this, id](visage::PopupMenu &menu) { addHostMenu(menu, id); };
   control.setValue(access_.paramValue(id), access_.paramModulated(id));
   Bound b;
   b.control = &control;
   b.id = id;
   bound_.push_back(b);
}

void ParamBindings::bind(Button &button, clap_id id) {
   button.setToggle(true);
   button.onToggle = [this, id](bool on) {
      access_.beginEdit(id);
      access_.setParam(id, on ? 1.0 : 0.0);
      access_.endEdit(id);
   };
   button.onContextMenu = [this, id](visage::PopupMenu &menu) { addHostMenu(menu, id); };
   button.setOn(access_.paramValue(id) >= 0.5);
   Bound b;
   b.button = &button;
   b.id = id;
   bound_.push_back(b);
}

void ParamBindings::bind(Dropdown &dropdown, clap_id id) {
   dropdown.onChange = [this, id, &dropdown](int index) {
      access_.beginEdit(id);
      access_.setParam(id, dropdown.firstValue + index);
      access_.endEdit(id);
   };
   dropdown.onContextMenu = [this, id](visage::PopupMenu &menu) { addHostMenu(menu, id); };
   dropdown.setSelected(static_cast<int>(std::lround(access_.paramValue(id) - dropdown.firstValue)));
   Bound b;
   b.dropdown = &dropdown;
   b.id = id;
   bound_.push_back(b);
}

void ParamBindings::bind(SegmentedSwitch &segments, clap_id id) {
   segments.onChange = [this, id, &segments](int index) {
      access_.beginEdit(id);
      access_.setParam(id, segments.firstValue + index);
      access_.endEdit(id);
   };
   segments.onContextMenu = [this, id](visage::PopupMenu &menu) { addHostMenu(menu, id); };
   segments.setSelected(static_cast<int>(std::lround(access_.paramValue(id) - segments.firstValue)));
   Bound b;
   b.segments = &segments;
   b.id = id;
   bound_.push_back(b);
}

void ParamBindings::bind(EnvelopeEditor &envelope, clap_id attack, clap_id decay, clap_id sustain, clap_id release) {
   const clap_id ids[] = {attack, decay, sustain, release};
   for (int i = 0; i < EnvelopeEditor::kStageCount; ++i)
      wireAxis(envelope.stage(static_cast<EnvelopeEditor::Stage>(i)), ids[i]);
   envelope.onContextMenu = [this, ids](visage::PopupMenu &menu) {
      for (clap_id id : ids)
         addHostMenu(menu, id);
   };
   Bound b;
   b.envelope = &envelope;
   b.id = attack;
   b.id2 = decay;
   b.id3 = sustain;
   b.id4 = release;
   bound_.push_back(b);
   sync(b);
}

void ParamBindings::wireAxis(ValueAxis &axis, clap_id id) {
   axis.onBeginEdit = [g = gestures_, id] { g->begin(id); };
   axis.onChange = [this, id](double v) { access_.setParam(id, v); };
   axis.onEndEdit = [g = gestures_, id] { g->end(id); };
   axis.modulatedSource = [this, id] { return access_.paramModulated(id); };
}

void ParamBindings::bind(XYPad &pad, clap_id xId, clap_id yId) {
   wireAxis(pad.x(), xId);
   wireAxis(pad.y(), yId);
   pad.onContextMenu = [this, xId, yId](visage::PopupMenu &menu) {
      addHostMenu(menu, xId);
      addHostMenu(menu, yId);
   };
   pad.setValues(access_.paramValue(xId), access_.paramModulated(xId), access_.paramValue(yId),
                 access_.paramModulated(yId));
   Bound b;
   b.pad = &pad;
   b.id = xId;
   b.id2 = yId;
   bound_.push_back(b);
}

void ParamBindings::sync() {
   for (const Bound &b : bound_)
      sync(b);
}

void ParamBindings::sync(const Bound &b) {
   if (b.control)
      b.control->setValue(access_.paramValue(b.id), access_.paramModulated(b.id));
   else if (b.button)
      b.button->setOn(access_.paramValue(b.id) >= 0.5);
   else if (b.dropdown)
      b.dropdown->setSelected(static_cast<int>(std::lround(access_.paramValue(b.id) - b.dropdown->firstValue)));
   else if (b.segments)
      b.segments->setSelected(static_cast<int>(std::lround(access_.paramValue(b.id) - b.segments->firstValue)));
   else if (b.pad)
      b.pad->setValues(access_.paramValue(b.id), access_.paramModulated(b.id), access_.paramValue(b.id2),
                       access_.paramModulated(b.id2));
   else if (b.envelope) {
      const clap_id ids[] = {b.id, b.id2, b.id3, b.id4};
      for (int i = 0; i < EnvelopeEditor::kStageCount; ++i)
         b.envelope->setValue(static_cast<EnvelopeEditor::Stage>(i), access_.paramValue(ids[i]),
                              access_.paramModulated(ids[i]));
   }
}

void ParamBindings::addHostMenu(visage::PopupMenu &menu, clap_id id) {
   const HostMenu *host = access_.hostMenu();
   if (!host || !host->available())
      return;
   const std::vector<HostMenu::Item> items = host->items(id);
   if (items.empty())
      return;

   // Option ids only need to be unique within this menu, so they count up past the built-in items
   // rather than reuse the host's action ids, which can be any 32-bit value.
   constexpr int kFirstId = 1000;
   int nextId = kFirstId;
   std::vector<visage::PopupMenu> stack(1);
   for (const HostMenu::Item &item : items) {
      visage::PopupMenu &current = stack.back();
      switch (item.kind) {
      case HostMenu::Item::Kind::Entry:
      case HostMenu::Item::Kind::Check: {
         const clap_id action = item.action;
         current.addOption(nextId++, item.label)
            .select(item.checked)
            .enable(item.enabled)
            .onSelection() += [host, id, action](int) { host->perform(id, action); };
         break;
      }
      case HostMenu::Item::Kind::Title:
         current.addOption(-1, item.label).enable(false);
         break;
      case HostMenu::Item::Kind::Separator:
         current.addBreak();
         break;
      case HostMenu::Item::Kind::BeginSubmenu:
         stack.emplace_back(item.label);
         break;
      case HostMenu::Item::Kind::EndSubmenu:
         if (stack.size() > 1) {
            visage::PopupMenu sub = std::move(stack.back());
            stack.pop_back();
            stack.back().addSubMenu(std::move(sub));
         }
         break;
      }
   }
   while (stack.size() > 1) {
      visage::PopupMenu sub = std::move(stack.back());
      stack.pop_back();
      stack.back().addSubMenu(std::move(sub));
   }
   menu.addBreak();
   for (const visage::PopupMenu &option : stack.front().options())
      menu.addSubMenu(option);
}

} // namespace jclap::ui
