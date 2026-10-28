#pragma once

// Static decorations: a titled panel box, a text label and a divider line. None of them take mouse
// events, so add them before the controls they sit behind.

#include <string>

#include <visage/ui.h>

#include "jclap/ui/design_frame.h"
#include "jclap/ui/theme.h"

namespace jclap::ui {

// A rounded panel with an optional title in its top-left corner. Sizes scale with `scale`, the
// panel's design-to-pixel ratio.
class Section : public visage::Frame, public Scalable {
public:
   explicit Section(std::string title = {});

   void setTitle(std::string title);
   void setScale(float scale) override;
   void setTheme(const Theme &theme);

   void draw(visage::Canvas &canvas) override;

private:
   const Theme *theme_ = &defaultTheme();
   std::string title_;
   float scale_ = 1.0f;
};

// One line of text sized to fit its height: the font is `fontScale` times the label's height.
class Label : public visage::Frame {
public:
   explicit Label(std::string text = {}, unsigned color = 0,
                  visage::Font::Justification justification = visage::Font::kCenter, float fontScale = 0.75f);

   void setText(std::string text);
   void setColor(unsigned color);
   void setFontScale(float scale);
   void setTheme(const Theme &theme);

   void draw(visage::Canvas &canvas) override;

private:
   const Theme *theme_ = &defaultTheme();
   std::string text_;
   unsigned color_;
   visage::Font::Justification justification_;
   float fontScale_;
};

// A filled line in the panel border colour; give it bounds one unit wide or tall.
class Divider : public visage::Frame {
public:
   Divider();
   void draw(visage::Canvas &canvas) override;
   void setTheme(const Theme &theme);

private:
   const Theme *theme_ = &defaultTheme();
};

} // namespace jclap::ui
