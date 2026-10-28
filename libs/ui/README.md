<div align="center">

# jclap::ui

Shared GUI widgets, layout and CLAP glue for JClap plugins.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![C++: 17](https://img.shields.io/badge/C%2B%2B-17-blue)](../../CMakeLists.txt) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage) [![CLAP: gui | context-menu](https://img.shields.io/badge/CLAP-gui%20%7C%20context--menu-blue)](https://github.com/free-audio/clap) [![screenshots: tested](https://img.shields.io/badge/screenshots-tested-brightgreen)](docs/gallery.png)

</div>

Drawn with [Visage](https://github.com/VitalAudio/visage), pinned to a commit in `CMakeLists.txt`.
Link it from a plugin's `CMakeLists.txt`:

```cmake
target_link_libraries(<name> PRIVATE jclap::ui)
```

![Widgets](docs/gallery.png)

## Widgets

| Header | What it is |
|---|---|
| `jclap/ui/knob.h` | Rotary knob: label, arc, value and an optional caption. Shows host modulation as an outer arc; `setMarker` ticks a value on the track, such as where a normal range ends; `setReach` shows a faint arc for how far something else (a mod matrix slot) can push it |
| `jclap/ui/slider.h` | Horizontal or vertical fader with the same value model as the knob |
| `jclap/ui/button.h` | Push button, or an on/off toggle with `setToggle(true)`. `onWheel` makes steppers easy |
| `jclap/ui/dropdown.h` | Pick one of a list of options (menu on click, wheel to step), for enum params |
| `jclap/ui/xy_pad.h` | Two values at once, each with its own range, modulation and param. `setGrid(Grid::Polar)` makes it a circle of angle and radius |
| `jclap/ui/meter.h` | Level meter with a falling peak marker, fed from a timer |
| `jclap/ui/number_box.h` | A value in a box: drag, scroll or type it. `setStepButtons(true)` adds - and + to make a stepper |
| `jclap/ui/segmented.h` | A row of buttons with one on, for enum params with a few options |
| `jclap/ui/led.h` | Activity light that flashes on MIDI (or anything) and fades. Feed it a counter the audio thread bumps, or set `fadeMs = 0` for a steady light with variable brightness (an LFO, a gate) |
| `jclap/ui/envelope.h` | ADSR editor: drag the corners. Binds to four params. `setPlayheads` shows a glowing dot per playing voice |
| `jclap/ui/keyboard.h` | Piano keyboard that plays notes when clicked and lights held notes. Held notes past either end show as an arrow; `shiftOctaves` moves the range |
| `jclap/ui/note_grid.h` | LinnStrument-style note grid (rows a fourth, fifth or octave apart) that shows per-note MPE: bend, pressure and timbre. Slide on it to bend. Off-grid notes show as an arrow; `shiftOctaves` moves it |
| `jclap/ui/note_roll.h` | Small piano roll that shows what a sequencer or note generator is about to play: notes as bars with their names, a beat grid and a playhead. `setLanes` fixes the rows (a sequencer's pitches), `setCompact` shows only the pitches in use |
| `jclap/ui/preset_bar.h` | Preset browser for a header: arrows and the wheel step through presets, the name opens a menu by category, SAVE writes the current settings to the user's folder. `jclap/ui/presets.h` holds the factory-bank helpers and the preset file format, without Visage |
| `jclap/ui/update_notice.h` | A small notice for a header when a newer JClap release is out. Click it to open the release page, or its cross to hide it until the next one. Hidden otherwise, and never in screenshots or tests. `jclap/ui/updates.h` is the check behind it, without Visage |
| `jclap/ui/octave_shift.h` | Octave - and + buttons around a readout, to move a keyboard or grid up and down |
| `jclap/ui/scope.h` | Oscilloscope or waveform view, fed from a `jclap::core::SampleFifo` |
| `jclap/ui/tabs.h` | Tab bar that shows one page of widgets at a time |
| `jclap/ui/tooltip.h` | Hover help for any widget |
| `jclap/ui/section.h` | `Section` (titled panel box), `Label` and `Divider`. They ignore the mouse |
| `jclap/ui/theme.h` | Colours shared by every widget, and the `font()` and `noteName()` helpers |

Knob and Slider share `ValueControl`: a `ValueRange` (min, max, default, wheel step, bipolar, and
optional `stops`, the only values a drag, the wheel or a stepper lands on, such as loop lengths of
1, 2, 4, 8, 16 or 32 bars), the
host's value and modulated value, and `onBeginEdit` / `onChange` / `onEndEdit` callbacks. Drag
(shift for fine), scroll for one step, double-click to reset. Right-click opens a menu with "Reset
to default", "Enter value..." (type a number) and the host's own items for the param.
`setShowValuePopup(true)` floats the value above the control while dragging (the XY pad always
does).

Every widget points at `defaultTheme()`; pass another `Theme` to `setTheme()` to restyle one, or
`setAccent()` for just the accent colour.

## Layout

`jclap/ui/design_frame.h`: derive the panel from `DesignFrame(width, height)` and add widgets at
design-grid coordinates. They are re-laid out whenever the window is resized, so the panel scales
as one picture. `row()`, `column()` and `grid()` split a `Rect` into cells:

```cpp
class Panel : public jclap::ui::DesignFrame {
public:
   explicit Panel(ParamAccess &params) : DesignFrame(600, 300), bindings_(params) {
      add<Section>({10, 10, 580, 280}, "FILTER");
      const auto cells = row({20, 50, 560, 100}, 4);
      bindings_.bind(add<Knob>(cells[0], ValueRange{20, 20000, 1000}), kCutoff);
      // ...
   }
   ParamBindings bindings_;
};
```

> [!TIP]
> Right-clicking the panel background offers UI sizes from 75% to 200%, as long as the panel is the
> window's root, and "Check for Updates", which turns the update check on or off.

## Params

`jclap/ui/params.h` and `jclap/ui/param_bindings.h` connect widgets to CLAP params in one call.
The plugin implements `ParamAccess` (read a value, begin/set/end an edit, by param id), usually
storing values in atomics and forwarding gestures to a `jclap::core::EditQueue`. Then
`ParamBindings::bind(widget, id)` wires a Knob, Slider, NumberBox, toggle Button, Dropdown, SegmentedSwitch, XY pad (two ids) or EnvelopeEditor (four ids):
edits go to the host as gestures, a 30 ms timer pulls automation, modulation and state loads back
in, and the right-click menu gets the host's items through CLAP's context-menu extension (return a
`HostMenu` from `ParamAccess::hostMenu()`).

## Window and CLAP glue

`jclap/ui/window.h` wraps a Visage window around one root frame and handles host sizing and zoom.
It doesn't include Visage, so the plugin's main file doesn't have to either. 

> [!NOTE]
> On macOS and Windows the window follows the OS's per-monitor scale and refuses the host's
> `set_scale`. On Linux it takes the host's `set_scale`, and without one the desktop's scale
> (`Xft.dpi`, which desktop scaling sets), not the monitor's physical DPI.

`jclap/ui/clap_gui.h` implements the CLAP `gui`, `posix-fd-support` and `timer-support` extensions
for that window. On Linux the window's events come through the host's fd polling, or a 16 ms host
timer when the host has no fd support.

Visage is patched (`libs/ui/patches/visage.patch`, applied when CMake fetches it). Each change is
marked `jclap:`. A press while another button is held goes to the widget being dragged. On Linux
the window no longer follows the host's parent window size, writes no size hints on the host's
window, and stops drawing while hidden. On Windows the host's window isn't subclassed.
With a `jclap::core::Plugin` subclass:

```cpp
// member:      jclap::ui::ClapGui gui_;   (declare it last)
// init():      gui_.init(host()); gui_.createWindow = [this] { return createEditor(*this); };
// extension(): return jclap::ui::ClapGui::extensionFor<MyPlugin, &MyPlugin::gui_>(id);
```

> [!IMPORTANT]
> Declare `gui_` as the plugin's last member, so the window closes before anything it reads is
> destroyed.

`createEditor` returns `std::make_unique<jclap::ui::Window>(std::move(panel), WindowOptions{w, h})`.
`plugins/_template` is a small complete example; `plugins/micromonsta2/src/editor.cpp` a big one.

## Update notices

When a plugin's window opens, `Window` asks GitHub for the newest release
(`api.github.com/repos/jeffmhopkins/Jclap/releases/latest`) on a background thread, at most once a
day across every JClap plugin, and compares it with the project version this build was made from.
Any `UpdateNotice` in the panel then shows the newer release. The check uses WinHTTP on Windows and
the system's libcurl elsewhere, loaded at run time, so there is nothing more to link or install.
Offline, rate-limited, or without libcurl, nothing shows and nothing fails.

What it learns is kept in `updates.txt` in the user's JClap folder (`%APPDATA%\JClap`,
`~/Library/Application Support/JClap` or `~/.config/JClap`), with the release the user dismissed
and whether checking is on. Unticking "Check for Updates" in the background menu stops all requests.

## Gallery

`docs/gallery.png` is rendered by `jclap-ui-gallery` (configure with `-DJCLAP_BUILD_TOOLS=ON`).

> [!NOTE]
> With tools on, `ctest` checks the render against `docs/gallery.png`, and CI does it on Linux. `jclap::ui::renderToPng()` does the same offscreen render for
any frame.
