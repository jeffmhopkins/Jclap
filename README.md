<div align="center">

# JClap

Small, single-purpose CLAP utility plugins, plus the shared toolkit they are built with.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![Release](https://img.shields.io/github/v/release/jeffmhopkins/Jclap)](https://github.com/jeffmhopkins/Jclap/releases/latest) [![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](CMakeLists.txt) [![CLAP 1.2](https://img.shields.io/badge/CLAP-1.2.2-blue)](https://github.com/free-audio/clap) [![Platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Linux-blue)](.github/workflows/build.yml) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage)

</div>

Each plugin does one job. Settings are plain host parameters, so they can be automated and modulated,
and plugins that need a panel get one from the shared GUI library.

## Plugins

| Plugin | Type | What it does |
|---|---|---|
| [Polar XY](plugins/orb-polar) | Note effect | Turns two XY CCs into angle/radius CCs for Omnisphere's Orb |
| [MicroMonsta 2 CC](plugins/micromonsta2) | Note effect | Knob panel that sends every MicroMonsta 2 parameter as a MIDI CC |

## Download

Grab the zip for your OS from the [latest release](https://github.com/jeffmhopkins/Jclap/releases/latest).
It contains every plugin. Copy the `.clap` files (bundles on macOS) to your CLAP folder:

- macOS: `~/Library/Audio/Plug-Ins/CLAP`
- Windows: `C:\Program Files\Common Files\CLAP`
- Linux: `~/.clap`

> [!WARNING]
> **macOS: browser downloads are quarantined.** The plugins are only ad-hoc signed (not notarized), so
> files downloaded through a browser are quarantined and Bitwig silently skips them. Files downloaded
> with `curl` aren't flagged, so the easiest install is from Terminal:

```sh
curl -sL https://github.com/jeffmhopkins/Jclap/releases/latest/download/jclap-macOS.zip -o /tmp/jclap.zip && mkdir -p ~/Library/Audio/Plug-Ins/CLAP && ditto -x -k /tmp/jclap.zip ~/Library/Audio/Plug-Ins/CLAP
```

If you downloaded with a browser instead, clear the quarantine flag once after copying:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/*.clap
```

> [!TIP]
> Then restart Bitwig, or rescan plug-ins under Settings → Locations.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

> [!NOTE]
> On Linux, install `libx11-dev libxrandr-dev libgl1-mesa-dev` first (the GUI library uses them).

All plugins land in `build/clap/`. CI builds Linux, macOS (universal) and Windows on every push, runs
the tests, and checks every plugin with [clap-validator](https://github.com/free-audio/clap-validator).

## Adding a plugin

> [!TIP]
> **Start from the template.** It is a working gain/pan plugin with a panel, and one command copies
> and renames it:

```sh
cmake -DNAME=my-plugin -DFILE_NAME=MyPlugin "-DDISPLAY_NAME=My Plugin" -P cmake/NewPlugin.cmake
```

It creates `plugins/my-plugin/` (plugin id `com.jeffmhopkins.jclap.my-plugin`), adds it to the
top-level `CMakeLists.txt`, and prints what to change next. See
[plugins/_template](plugins/_template) for the details, then add a row to the table above.

Plugins build on two shared libraries:

- [jclap::core](libs/core): the CLAP boilerplate. A `Plugin` base class, `JCLAP_ENTRY` for the
  factory, param/state/port/event helpers, the GUI edit queue and `TestHost` for host-style tests.
- [jclap::ui](libs/ui): the GUI. Knobs, sliders, buttons, dropdowns, an XY pad, meters and panels
  on [Visage](https://github.com/VitalAudio/visage), a design-grid layout, one-line param binding
  and the CLAP window glue.

> [!NOTE]
> **Screenshot tests.** Configure with `-DJCLAP_BUILD_TOOLS=ON` and `ctest` also renders the panels and
> the widget gallery and compares them with the PNGs in each `docs/` folder. CI does this on Linux.
> After an intended visual change, re-render the reference (the failing test prints how).

## Releasing

> [!IMPORTANT]
> In GitHub, open Actions → build → Run workflow and enter a version such as `v0.2.0`. The run builds
> every platform, tags the commit and publishes a release with one zip per OS. Pushing a `v*` tag does
> the same. Bump the version in `CMakeLists.txt` and each plugin descriptor first.
