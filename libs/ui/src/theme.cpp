#include "jclap/ui/theme.h"

#include "embedded/fonts.h"

namespace jclap::ui {

const Theme &defaultTheme() {
   static const Theme theme;
   return theme;
}

visage::Font font(float size) { return visage::Font(size, visage::fonts::Lato_Regular_ttf); }

} // namespace jclap::ui
