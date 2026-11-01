#include "jclap/ui/preset_bar.h"

#include <algorithm>
#include <cmath>

#include <visage/widgets.h>

namespace jclap::ui {

namespace {
constexpr float kArrow = 26.0f, kSave = 52.0f, kStar = 24.0f;
constexpr int kSaveId = 1 << 20, kDeleteId = kSaveId + 1, kFavoriteId = kSaveId + 2, kUserBase = 1 << 16,
              kFavoritesBase = 1 << 18;

// The save name box. A click anywhere but SAVE drops the name, so the box never sits open without
// the keyboard; SAVE itself saves, and so does Enter.
class NameEditor : public visage::TextEditor {
public:
   std::function<void()> onClickAway;
   void focusChanged(bool is_focused, bool was_clicked) override {
      visage::TextEditor::focusChanged(is_focused, was_clicked);
      if (!is_focused && was_clicked && onClickAway)
         onClickAway();
   }
};
} // namespace

void drawStar(visage::Canvas &canvas, float cx, float cy, float radius, bool filled) {
   constexpr float kPi = 3.14159265f;
   visage::Path star;
   for (int i = 0; i < 10; ++i) {
      const float angle = i * kPi / 5.0f, r = i % 2 ? radius * 0.45f : radius;
      const visage::Point p{cx + r * std::sin(angle), cy - r * std::cos(angle)};
      if (i == 0)
         star.moveTo(p);
      else
         star.lineTo(p);
   }
   star.close();
   if (filled)
      canvas.fill(star);
   else
      canvas.fill(star.stroke(std::max(1.0f, radius * 0.16f)));
}

void drawDie(visage::Canvas &canvas, float cx, float cy, float size) {
   const float x = cx - size / 2, y = cy - size / 2;
   canvas.roundedRectangleBorder(x, y, size, size, size * 0.24f, std::max(1.0f, size * 0.11f));
   const float pip = size * 0.2f, near = size * 0.3f, far = size * 0.7f;
   const float spots[5][2] = {{near, near}, {far, near}, {0.5f * size, 0.5f * size}, {near, far}, {far, far}};
   for (const auto &spot : spots)
      canvas.circle(x + spot[0] - pip / 2, y + spot[1] - pip / 2, pip);
}

void applyPreset(const Preset &preset, ParamAccess &params, const std::vector<double> &defaults) {
   std::vector<std::pair<clap_id, double>> values;
   for (clap_id id = 0; id < defaults.size(); ++id)
      values.push_back({id, defaults[id]});
   for (const auto &[id, v] : preset.values) {
      auto it = std::find_if(values.begin(), values.end(), [id = id](const auto &p) { return p.first == id; });
      if (it != values.end())
         it->second = v;
      else
         values.push_back({id, v});
   }
   for (const auto &[id, v] : values) {
      if (params.paramValue(id) == v)
         continue;
      params.beginEdit(id);
      params.setParam(id, v);
      params.endEdit(id);
   }
}

PresetBar::PresetBar(const std::vector<Preset> &factory, PresetFormat format, ParamAccess &params)
   : factory_(factory), format_(std::move(format)), params_(params),
     favorites_(PresetFavorites::fileFor(format_.name)) {
   rescan();
}

PresetBar::~PresetBar() = default;

void PresetBar::setTheme(const Theme &theme) {
   theme_ = &theme;
   redraw();
}

void PresetBar::setAccent(unsigned color) {
   accent_ = color;
   redraw();
}

std::string PresetBar::name() const { return currentName ? currentName() : ownName_; }

void PresetBar::rename(const std::string &name) {
   if (setCurrentName)
      setCurrentName(name);
   else
      ownName_ = name;
}

int PresetBar::total() const { return static_cast<int>(factory_.size() + user_.size()); }

const Preset &PresetBar::at(int i) const {
   return i < static_cast<int>(factory_.size()) ? factory_[i] : user_[i - factory_.size()];
}

// The preset loaded by name: the one last loaded here if it still has that name, otherwise the first
// in menu order.
int PresetBar::indexOf(const std::string &name) const {
   if (currentIndex_ >= 0 && currentIndex_ < total() && at(currentIndex_).name == name)
      return currentIndex_;
   for (int i = 0; i < total(); ++i)
      if (at(i).name == name)
         return i;
   return -1;
}

void PresetBar::rescan() {
   favorites_.reload();
   const std::string current = currentIndex_ >= 0 && currentIndex_ < total() ? at(currentIndex_).name : "";
   const bool wasUser = currentIndex_ >= static_cast<int>(factory_.size());
   user_ = userPresets(format_);
   currentIndex_ = -1;
   if (!current.empty() && wasUser)
      for (int i = 0; i < static_cast<int>(user_.size()); ++i)
         if (user_[i].name == current)
            currentIndex_ = static_cast<int>(factory_.size()) + i;
}

void PresetBar::loadIndex(int i) {
   if (i < 0 || i >= total())
      return;
   // Copied: a user preset could go away if the load rescans.
   const Preset p = at(i);
   if (onLoad)
      onLoad(p);
   else
      applyPreset(p, params_, defaults);
   rename(p.name);
   currentIndex_ = i;
   redraw();
}

void PresetBar::step(int dir) {
   const int n = total();
   if (n == 0)
      return;
   const int current = indexOf(name());
   loadIndex(current < 0 ? 0 : (current + dir % n + n) % n);
}

bool PresetBar::toggleFavorite() {
   const int i = indexOf(name());
   if (i < 0)
      return false;
   favorites_.toggle(at(i).name);
   redraw();
   return true;
}

bool PresetBar::load(const std::string &name) {
   const int i = indexOf(name);
   if (i < 0)
      return false;
   loadIndex(i);
   return true;
}

PresetBar::Zone PresetBar::zoneAt(float x) const {
   const float s = height() / 28.0f, w = width();
   if (x < kArrow * s)
      return Zone::Prev;
   if (x >= w - kSave * s)
      return Zone::Save;
   if (x >= w - (kSave + kArrow) * s)
      return Zone::Next;
   if (x >= w - (kSave + kArrow + kStar) * s)
      return Zone::Star;
   return Zone::Name;
}

void PresetBar::setHover(Zone z) {
   if (z != hover_) {
      hover_ = z;
      redraw();
   }
}

void PresetBar::draw(visage::Canvas &c) {
   const Theme &t = *theme_;
   const unsigned accent = accent_ ? accent_ : t.accent;
   const float s = height() / 28.0f, w = width(), h = height();
   c.setColor(t.knobDark);
   c.roundedRectangle(0, 0, w, h, 6 * s);
   c.setColor(t.panelBorder);
   c.roundedRectangleBorder(0, 0, w, h, 6 * s, 1.0f * s);
   const float a = kArrow * s, save = kSave * s;
   c.setColor(t.track);
   c.fill(a, 4 * s, 1 * s, h - 8 * s);
   c.fill(w - save - a, 4 * s, 1 * s, h - 8 * s);
   c.fill(w - save, 4 * s, 1 * s, h - 8 * s);
   const auto arrow = [&](float cx, float dir, bool hot) {
      c.setColor(hot ? t.text : t.dimText);
      c.triangle(cx + dir * 4 * s, h / 2, cx - dir * 3 * s, h / 2 - 5 * s, cx - dir * 3 * s, h / 2 + 5 * s);
   };
   arrow(a / 2, -1.0f, hover_ == Zone::Prev);
   arrow(w - save - a / 2, 1.0f, hover_ == Zone::Next);
   c.setColor(hover_ == Zone::Save ? t.text : accent);
   c.text("SAVE", font(11 * s), visage::Font::kCenter, w - save, 0, save, h);
   if (editor_ && editor_->isVisible())
      return;
   const std::string current = name();
   const int i = indexOf(current);
   const float star = kStar * s;
   c.setColor(hover_ == Zone::Name ? 0xffffffffu : t.text);
   c.text(current.empty() ? std::string("Untitled") : current, font(13 * s), visage::Font::kCenter, a, 0,
          w - 2 * a - save - star, h);
   if (i >= 0) {
      const bool starred = favorites_.contains(at(i).name);
      c.setColor(starred ? accent : hover_ == Zone::Star ? t.text : t.dimText);
      drawStar(c, w - save - a - star / 2, h / 2, 6.5f * s, starred);
   }
   if (i >= 0) {
      c.setColor(t.dimText);
      c.text(at(i).category, font(9 * s), visage::Font::kLeft, a + 8 * s, 0, 80 * s, h);
   }
}

void PresetBar::mouseMove(const visage::MouseEvent &e) { setHover(zoneAt(e.position.x)); }
void PresetBar::mouseExit(const visage::MouseEvent &) { setHover(Zone::None); }

void PresetBar::mouseDown(const visage::MouseEvent &e) {
   switch (zoneAt(e.position.x)) {
   case Zone::Prev: step(-1); break;
   case Zone::Next: step(1); break;
   case Zone::Save: editor_ && editor_->isVisible() ? finishSave(true) : startSave(); break;
   case Zone::Name: showMenu(); break;
   case Zone::Star: toggleFavorite(); break;
   case Zone::None: break;
   }
}

bool PresetBar::mouseWheel(const visage::MouseEvent &e) {
   // Only a vertical scroll steps: a sideways trackpad swipe, or the zero-delta events some
   // trackpads send, would otherwise step every time.
   if (e.wheel_delta_y == 0.0f || std::fabs(e.wheel_delta_x) > std::fabs(e.wheel_delta_y))
      return false;
   step(e.wheel_delta_y > 0 ? -1 : 1);
   return true;
}

void PresetBar::showMenu() {
   rescan();
   visage::PopupMenu menu;
   const std::string current = name();
   // Favorites first, in menu order, for the presets that still exist.
   visage::PopupMenu favorites("Favorites");
   for (int i = 0; i < total(); ++i)
      if (favorites_.contains(at(i).name))
         favorites.addOption(kFavoritesBase + i, at(i).name).select(at(i).name == current);
   if (favorites.options().empty())
      favorites.addOption(-2, "(click the star to add one)").enable(false);
   menu.addSubMenu(std::move(favorites));
   menu.addBreak();
   std::vector<std::string> categories;
   for (const Preset &p : factory_)
      if (std::find(categories.begin(), categories.end(), p.category) == categories.end())
         categories.push_back(p.category);
   for (const std::string &cat : categories) {
      visage::PopupMenu sub(cat);
      for (int i = 0; i < static_cast<int>(factory_.size()); ++i)
         if (factory_[i].category == cat)
            sub.addOption(i, factory_[i].name).select(factory_[i].name == current);
      menu.addSubMenu(std::move(sub));
   }
   visage::PopupMenu user("User");
   for (int i = 0; i < static_cast<int>(user_.size()); ++i)
      user.addOption(kUserBase + i, user_[i].name).select(user_[i].name == current);
   if (user_.empty())
      user.addOption(-2, "(none saved yet)").enable(false);
   menu.addSubMenu(std::move(user));
   menu.addBreak();
   menu.addOption(kSaveId, "Save as...");
   if (indexOf(current) >= 0)
      menu.addOption(kFavoriteId, favorites_.contains(current) ? "Remove from Favorites" : "Add to Favorites");
   if (indexOf(current) >= static_cast<int>(factory_.size()))
      menu.addOption(kDeleteId, "Delete \"" + current + "\"...");
   menu.onSelection() += [this](int id) {
      if (id == kSaveId)
         startSave();
      else if (id == kDeleteId)
         confirmDelete();
      else if (id == kFavoriteId)
         toggleFavorite();
      else if (id >= kFavoritesBase && id - kFavoritesBase < total())
         loadIndex(id - kFavoritesBase);
      else if (id >= kUserBase && id - kUserBase < static_cast<int>(user_.size()))
         loadIndex(static_cast<int>(factory_.size()) + id - kUserBase);
      else if (id >= 0 && id < static_cast<int>(factory_.size()))
         loadIndex(id);
   };
   menu.show(this, {kArrow * height() / 28.0f, height()});
}

// Deleting a preset can't be undone, so the menu asks once more before the file goes.
void PresetBar::confirmDelete() {
   const std::string current = name();
   visage::PopupMenu menu;
   menu.addOption(kDeleteId, "Delete \"" + current + "\" for good");
   menu.addOption(-2, "Cancel");
   menu.onSelection() += [this, current](int id) {
      if (id != kDeleteId)
         return;
      std::error_code ec;
      std::filesystem::remove(userPresetFolder(format_) / (current + format_.extension), ec);
      favorites_.remove(current);
      rescan();
      redraw();
   };
   menu.show(this, {kArrow * height() / 28.0f, height()});
}

void PresetBar::startSave() {
   if (!editor_) {
      auto editor = std::make_unique<NameEditor>();
      editor->setJustification(visage::Font::kCenter);
      editor->onEnterKey() += [this] { finishSave(true); };
      editor->onEscapeKey() += [this] { finishSave(false); };
      editor->onClickAway = [this] {
         if (hover_ != Zone::Save)
            finishSave(false);
      };
      editor_ = std::move(editor);
      addChild(editor_.get(), false);
   }
   const float s = height() / 28.0f;
   editor_->setBounds(kArrow * s, 3 * s, width() - (2 * kArrow + kSave) * s, height() - 6 * s);
   editor_->setFont(font(13 * s));
   const std::string current = name();
   editor_->setText(current.empty() ? std::string("My Preset") : current);
   editor_->setVisible(true);
   editor_->requestKeyboardFocus();
   editor_->selectAll();
   redraw();
}

void PresetBar::finishSave(bool apply) {
   if (!editor_ || !editor_->isVisible())
      return;
   const std::string wanted = uniqueName(presetFileName(editor_->text().toUtf8()));
   editor_->setVisible(false);
   if (apply && writePresetFile(userPresetFolder(format_) / (wanted + format_.extension), format_, params_)) {
      rename(wanted);
      rescan();
      for (int i = 0; i < static_cast<int>(user_.size()); ++i)
         if (user_[i].name == wanted)
            currentIndex_ = static_cast<int>(factory_.size()) + i;
   }
   redraw();
}

// A save name that won't hide a factory preset or silently replace another user preset: saving again
// over the user preset you're on keeps its name, anything else that's taken gets a number.
std::string PresetBar::uniqueName(const std::string &wanted) {
   rescan();
   const bool onUserPreset =
      currentIndex_ >= static_cast<int>(factory_.size()) && currentIndex_ < total() && at(currentIndex_).name == wanted;
   if (onUserPreset)
      return wanted;
   std::string candidate = wanted;
   for (int k = 2; indexOf(candidate) >= 0; ++k)
      candidate = wanted + " " + std::to_string(k);
   return candidate;
}

} // namespace jclap::ui
