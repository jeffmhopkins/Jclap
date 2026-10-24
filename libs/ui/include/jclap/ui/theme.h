#pragma once

// Colours and the font shared by every jclap::ui widget. Widgets point at defaultTheme() unless
// given another one with setTheme().

#include <string>

#include <visage/graphics.h>

namespace jclap::ui {

struct Theme {
   unsigned background = 0xff15181d;
   unsigned panel = 0xff1c2027;
   unsigned panelBorder = 0xff2c323b;
   unsigned headerBar = 0xff111317;
   unsigned accent = 0xfff28c28;
   unsigned text = 0xffe6e8eb;
   unsigned dimText = 0xff8d939c;
   unsigned track = 0xff2e333b;
   unsigned modulation = 0xfff4f1e6;
   unsigned button = 0xff20252c;
   unsigned buttonHover = 0xff2a3039;
   unsigned knobLight = 0xff4b515a;
   unsigned knobDark = 0xff1d2026;
   unsigned knobEdge = 0xff101216;
   unsigned shadow = 0x66000000;
};

const Theme &defaultTheme();

// The library's UI font (Lato Regular) at the given size.
visage::Font font(float size);
// The same at a window's DPI scale, for measuring text (stringWidth needs the scale set).
visage::Font font(float size, float dpiScale);

// A MIDI note's name with its octave, middle C (60) as C4: "C4", "F#2".
std::string noteName(int note);

} // namespace jclap::ui
