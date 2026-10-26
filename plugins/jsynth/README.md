<div align="center">

# JSynth

A bi-timbral MPE synth where every note is a creature and the creatures flock. Two species, each a three-oscillator FM patch with two filters, three envelopes and two LFOs, and creatures that fly near each other modulate each other. Each one trails ink.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![C++: 17](https://img.shields.io/badge/C%2B%2B-17-blue)](../../CMakeLists.txt) [![CLAP: 1.2.2 instrument](https://img.shields.io/badge/CLAP%201.2.2-instrument-blue)](https://github.com/free-audio/clap) [![MPE: yes](https://img.shields.io/badge/MPE-yes-blue)](#the-design) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage)

</div>

**[Read the user manual (PDF)](docs/JSynth-User-Manual.pdf)**: installing, a quick start, every control with its
range and default, the flock, MAP and routes, the presets, sound-design recipes and troubleshooting.

[ARCHITECTURE.md](ARCHITECTURE.md) walks through all of it from the code: the signal flow, timing,
the voice, the flock and its sources, the couplings between creatures, every parameter with its range,
the presets, and threads and state.

| Part A | Part B |
|---|---|
| ![Part A](docs/panel.png) | ![Part B](docs/panel-b.png) |
| **MAP mode, with SPEED armed** | **The route list** |
| ![MAP mode](docs/panel-map.png) | ![Routes](docs/panel-routes.png) |
| **The REDUCED view** | **The SKY view** |
| ![REDUCED](docs/panel-reduced.png) | ![SKY](docs/panel-hidden.png) |

**Three views**, switched at the top right. **FULL** has every card around the sky. **REDUCED** puts
big, drawn panels down the left beside a sky the same size: the oscillators with their waves drawn
(and an FM tab that draws what comes out), the filters' response, which you drag to set cutoff and
resonance, and the envelopes (and an LFOS tab). Under the sky a panel has VOICE (the stereo field,
the levels of both parts and the voices in use), FLOCK and MOTION tabs. Only the touch curves, glide
rounding and the route list stay on FULL. **SKY** hides everything but the flocks.

## The design

- **Every note is a creature.** Your finger is its lure: the note sets where the lure sits left to
  right, Glide moves it sideways, Slide lifts it, and Press makes the creature fly faster and
  brighter. Let go and it flies off as it fades. LURE X and LURE Y on the FLOCK card choose what
  places the lure, and RINGS under the sky shows or hides each held note's lure ring.
- **Two species, A and B**, each a whole patch with its own way of flying: Shoal, Scatter, Hunt or
  Flee. The play mode decides which species a note hatches (Single, Layer, Split, MPE Zones,
  Velocity or Expression). Each plays up to 16 creatures.
- **A voice:** three oscillators (analog waves, the TX81Z's eight FM waves, noise, or Random: a
  wave drawn fresh for each note; pitched by
  note, ratio or fixed Hz), a 3×3 FM grid whose diagonal is each oscillator's feedback, noise, drive,
  and two filters, serial or parallel, with each oscillator and the noise picking Filter 1, Filter 2
  or both.
- **Between creatures:** the nearest neighbour's Osc 1 feeds this creature's Osc 2 (Neighbour FM), so
  a tight flock rings and a loose one is clean. Creatures flying the same way drift toward pure
  intervals (Tune Pull). A hunter that catches its prey ring-modulates it (Bite).
- **Ink:** each creature sheds ink in the sky that blooms (Spread) and fades (Tail).
- **Stereo:** X Pan sets how far each creature's place in the sky, left to right, pans its voice,
  so a flock can spread across the stereo field. Output Width sets the whole output's width.
- **Four LFOs.** Two move the flock: the wingbeat, one per creature, and the wind, one gust for the
  whole sky (Push sets how hard). They only move the flock. LFO 1 and LFO 2 are plain LFOs, the two
  to map.

## Modulation: MAP

There is no matrix page. Touch a source chip, which turns **MAP** on with it armed, then drag any knob: the drag sets how much that
source moves it, shown as a ring in the source's colour (up to four per knob). Double-click a knob to
flip its route, right-click to remove it. The sources are the touch chips in the header (Velocity,
Press, Glide, Slide, and Release, the release velocity), the envelope and LFO chips on their cards,
and the flock's own under the sky: Speed, Nearest, Heading, and Turn (which way and how hard the
creature is turning, + right, - left). Each chip's LED glows with its source as it moves. Random (a
value per note) has a chip under the sky in REDUCED; Key, Mod Wheel and the four macros are in the
route list. The FM grid and the flock params are knobs, so they map like any other.

Underneath, each part has a pool of 64 routes (source, destination, amount) as host params, so route
depths can be automated. **ROUTES** lists the ones in use, sixteen to a page, to edit by name: **+ ADD**
starts one and **X** removes it.

## Presets

As in JVerb: the arrows step through the presets, the name opens them by category, and **SAVE** saves
the current sound as a user preset (`.jspreset`, plain text, in the user's application data folder
under `JSynth/Presets`). SAVE opens a name box: type a name and press Enter or click SAVE again to
save it, or press Escape or click anywhere else to back out. To remove a user preset, load it, open the name
menu and pick **Delete**; it asks once more before the file goes. A factory preset loads every param it doesn't set at its default, so nothing
from the last patch carries over.

The 49 factory presets run in six categories from the fastest envelopes to the slowest: **Hits**,
**Plucks**, **Keys**, **Bass and Leads**, **Pads** and **Swells**. Each category starts with a plain
patch with no routes at all and works up to ones the flock plays: Neighbour FM, Bite, and routes
from the flock's sources. Nothing from the flock reaches the sound unless a preset (or you) routes it.

`jsynth-record out.wav|out.mp4 [preset] [cm7-swell|four-bar]` (built with the tools) plays a phrase
through a preset and writes the audio, or a video of the panel with it: a held Cm7 whose pressure
swells up and back over 15 seconds, or four strummed chords (Cm9, Abmaj7, Fm9, G7b9) with the
pressure swelling through each bar.

## Params

`src/params.h` writes a part's params once, as offsets, and `partId(part, offset)` gives the host's
id. Each module is a block of ids with room to grow (oscillators 16 apart, routes 4 apart), so ids can
stay put once released. The state saves each value with its id, so a state from a build with fewer
params loads, and unknown ids are skipped.
