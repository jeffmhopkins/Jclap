# jclap::ui

Shared GUI pieces for JClap plugins, drawn with [Visage](https://github.com/VitalAudio/visage) (pinned
to a commit in `CMakeLists.txt`). Link it from a plugin's `CMakeLists.txt`:

```cmake
target_link_libraries(<name> PRIVATE jclap::ui)
```

![Widgets](docs/gallery.png)

## Widgets

| Header | What it is |
|---|---|
| `jclap/ui/knob.h` | Rotary knob: label, arc, value and an optional caption. Shows host modulation as an outer arc |
| `jclap/ui/slider.h` | Horizontal or vertical fader with the same value model as the knob |
| `jclap/ui/button.h` | Push button, or an on/off toggle with `setToggle(true)`. `onWheel` makes steppers easy |
| `jclap/ui/section.h` | `Section` (titled panel box), `Label` and `Divider`. They ignore the mouse |
| `jclap/ui/theme.h` | Colours shared by every widget, and the `font()` helper |

Knob and Slider share `ValueControl`: give it a `ValueRange` (min, max, default, wheel step, bipolar)
and hook up `onBeginEdit` / `onChange` / `onEndEdit`, which map straight onto CLAP gesture begin,
param value and gesture end. Push host changes back with `setValue(value, modulated)`, usually from a
timer; it is ignored while the user is dragging. Drag (shift for fine), scroll for one step, and
double-click to reset.

Every widget points at `defaultTheme()`; pass another `Theme` to `setTheme()` to restyle one, or
`setAccent()` for just the accent colour.

## Window and CLAP glue

`jclap/ui/window.h` wraps a Visage window around one root frame and handles host sizing. It doesn't
include Visage, so the plugin's main file doesn't have to either.

`jclap/ui/clap_gui.h` implements the CLAP `gui` and `posix-fd-support` extensions for that window:

```cpp
struct MyPlugin { clap_plugin_t plugin; jclap::ui::ClapGui gui; /* ... */ };
jclap::ui::ClapGui *guiOf(const clap_plugin_t *p) { return &self(p)->gui; }

// init():          p->gui.init(host); p->gui.createWindow = [p] { return createEditor(*p); };
// destroy():       p->gui.destroy();
// get_extension(): if (const void *ext = jclap::ui::ClapGui::extension<guiOf>(id)) return ext;
```

`createEditor` builds your root frame and returns
`std::make_unique<jclap::ui::Window>(std::move(root), jclap::ui::WindowOptions{width, height})`.
See `plugins/micromonsta2/src/editor.cpp` for a full panel.

## Gallery

To regenerate `docs/gallery.png`, configure with `-DJCLAP_BUILD_TOOLS=ON` and run
`build/libs/ui/jclap-ui-gallery libs/ui/docs/gallery.png`. `jclap::ui::renderToPng()` does the same
offscreen render for any frame.
