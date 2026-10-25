<div align="center">

# JSynth

A bi-timbral MPE synth where every note is a creature and the creatures flock. Two species, each a three-oscillator FM patch with two filters, three envelopes and two LFOs, and creatures that fly near each other modulate each other. Each one trails ink.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![C++: 17](https://img.shields.io/badge/C%2B%2B-17-blue)](../../CMakeLists.txt) [![CLAP: 1.2.2 instrument](https://img.shields.io/badge/CLAP%201.2.2-instrument-blue)](https://github.com/free-audio/clap) [![MPE: yes](https://img.shields.io/badge/MPE-yes-blue)](#the-design) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage) [![status: panel mock](https://img.shields.io/badge/status-panel%20mock-orange)](#status) [![shipped: no](https://img.shields.io/badge/shipped-no-lightgrey)](#status)

</div>

## Status

> [!IMPORTANT]
> **This is the panel mock.** Every param exists, can be automated, and saves and loads, and the panel
> flies the flocks from the held notes, but the plugin makes **no sound** yet. It builds into
> `build/preview/`, which CI checks but never ships.

| Part A | Part B |
|---|---|
| ![Part A, the FLOCK and MOTION tabs](docs/panel.png) | ![Part B, the FM and LFO tabs](docs/panel-b.png) |
| **MAP mode, with SPEED armed** | **The route list** |
| ![MAP mode](docs/panel-map.png) | ![Routes](docs/panel-routes.png) |

## The design

- **Every note is a creature.** Your finger is its lure: the note sets where the lure sits left to
  right, Glide moves it sideways, Slide lifts it, and Press makes the creature fly faster and
  brighter. Let go and it flies off as it fades.
- **Two species, A and B**, each a whole patch with its own way of flying: Shoal, Scatter, Hunt or
  Flee. The play mode decides which species a note hatches (Single, Layer, Split, MPE Zones,
  Velocity or Expression). Each plays up to 16 creatures.
- **A voice:** three oscillators (analog waves, the TX81Z's eight FM waves, or noise; pitched by
  note, ratio or fixed Hz), a 3×3 FM grid whose diagonal is each oscillator's feedback, noise, drive,
  and two filters (serial, parallel or split).
- **Between creatures:** the nearest neighbour's Osc 1 feeds this creature's Osc 2 (Neighbour FM), so
  a tight flock rings and a loose one is clean. Creatures flying the same way drift toward pure
  intervals (Tune Pull). A hunter that catches its prey ring-modulates it (Bite).
- **Ink:** each creature sheds ink that blooms (Spread) and fades (Tail). Flying through another
  creature's ink stirs it, and that is the Wake source.
- **Four LFOs.** Two move the flock: the wingbeat, one per creature, and the wind, one gust for the
  whole sky (Push sets how hard). LFO 1 and LFO 2 are plain LFOs that only modulate. All four are
  sources to map.

## Modulation: MAP

There is no matrix page. Press **MAP**, touch a source, then drag any knob: the drag sets how much that
source moves it, shown as a ring in the source's colour (up to four per knob). Double-click a knob to
flip its route, right-click to remove it. The sources are the touch chips in the header, the envelope
and LFO chips on the right card, and the flock's own under the sky: Speed, Crowd, Nearest, Heading,
Caught and Wake. Each chip's LED glows with its source as it moves. The FM grid and the flock params
are knobs, so they map like any other.

Underneath, each part has a pool of 64 routes (source, destination, amount) as host params, so route
depths can be automated. **ROUTES** lists the ones in use, sixteen to a page, to edit by name: **+ ADD**
starts one and **X** removes it.

## Presets

As in JVerb: the arrows step through the presets, the name opens them by category, and **SAVE** saves
the current sound as a user preset (`.jspreset`, plain text, in the user's application data folder
under `JSynth/Presets`). A factory preset loads every param it doesn't set at its default, so nothing
from the last patch carries over.

## Params

`src/params.h` writes a part's params once, as offsets, and `partId(part, offset)` gives the host's
id. Each module is a block of ids with room to grow (oscillators 16 apart, routes 4 apart), so ids can
stay put once released. The state saves each value with its id, so a state from a build with fewer
params loads, and unknown ids are skipped.
