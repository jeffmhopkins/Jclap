#include "jclap/ui/theme.h"

#include "embedded/fonts.h"

namespace jclap::ui {

const Theme &defaultTheme() {
   static const Theme theme;
   return theme;
}

visage::Font font(float size) { return visage::Font(size, visage::fonts::Lato_Regular_ttf); }

visage::Font font(float size, float dpiScale) { return font(size).withDpiScale(dpiScale > 0.0f ? dpiScale : 1.0f); }

std::string noteName(int note) {
   static const char *const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
   const int octave = (note < 0 ? (note - 11) / 12 : note / 12) - 1;
   return kNames[((note % 12) + 12) % 12] + std::to_string(octave);
}

} // namespace jclap::ui
