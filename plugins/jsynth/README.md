<div align="center">

# JSynth

A two-part MPE synth where every note is a creature, and the creatures flock. How they fly shapes how
they sound: a tight flock rings, a loose one is clean, a hunter that catches its prey bites it, and
each one trails ink across the sky.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![C++: 17](https://img.shields.io/badge/C%2B%2B-17-blue)](../../CMakeLists.txt) [![CLAP: 1.2.10 instrument](https://img.shields.io/badge/CLAP%201.2.10-instrument-blue)](https://github.com/free-audio/clap) [![MPE: yes](https://img.shields.io/badge/MPE-yes-blue)](#play-it-with-your-hands) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage)

![JSynth](docs/readme/panel.jpg)

**[User manual (PDF)](docs/JSynth-User-Manual.pdf)** · **[Architecture](ARCHITECTURE.md)** · **[Install](../../README.md)**

</div>

## At a glance

- **Two parts, A and B**, each a whole patch: three oscillators, a 3×3 FM grid, noise, drive, two
  filters, three envelopes, two LFOs and four mod buses. Play one, layer both, split the keyboard, or
  pick the part by MPE zone, velocity or pressure.
- **Every note is a creature.** It hatches where your note puts its lure, flies with the others of
  its species, and fades off when you let go. Up to 16 per part.
- **The flock is a modulation source.** Speed, Nearest, Heading and Turn route anywhere, and three
  couplings run between creatures: Neighbour FM, Tune Pull and Bite.
- **Shape envelopes** after XAOC's Zadar: 60 contours in ten banks, from 1 ms to 30 minutes, chained
  into each other.
- **MAP** instead of a matrix page: arm a source, drag a knob.
- **166 factory presets** in ten categories, with favorites.

## Sound

![The FM tab](docs/readme/fm.jpg)

Each oscillator plays an analog wave (sine, triangle, saw, square, pulse), one of the TX81Z's eight FM
waves, white or pink noise, or **Random**, a wave drawn fresh for every note. It's pitched by note,
by ratio or at a fixed Hz. The **FM** tab above is the 3×3 grid, row into column, with each
oscillator's feedback on the diagonal, and draws what actually comes out at C4. Beside it sit the
couplings **between creatures** and the part's noise and drift.

![The filter, in MAP mode](docs/readme/map-filter.jpg)

Two filters, serial or parallel with a balance between them: low pass, high pass, band pass, notch,
peak, ladder and two combs, 12 to 48 dB. Each oscillator and the noise picks Filter 1, Filter 2 or
both. The response is drawn and draggable (handle 1 and 2 set cutoff and resonance), and it follows
the newest note's modulated cutoff as it plays. A drive stage (soft, hard or tube) sits after them.
This shot is in MAP mode with the Wah bus of *Finger Wah* armed: the rings on Cutoff and Res are how
far it moves them.

## Envelopes, LFOs and buses

The card at the bottom left has three tabs.

![Shape envelopes](docs/readme/shape.jpg)

**ENVELOPES.** Each of the three is an ADSR (a DAHDSR with curves and loops) or a **Shape**: one
contour from a library of 60 in ten banks (Rise, Fall, Swell, Pulse, Waves, Steps, Drift, Strike,
ADSR and Acoustic), stretched over 1 ms to 30 minutes or a tempo length, then warped, bent, scaled,
reversed, repeated and held at a sustain point. **Env Chain** starts one Shape envelope from another
at a phase of its run, in a line or a loop, so drones keep moving. Above, *Two Tides*: Bloom, Arch
and Wander, five, thirteen and twenty-nine seconds long.

![LFOs](docs/readme/lfos.jpg)

**LFOS.** Two plain LFOs per part (sine, triangle, saw, square, S&H, smooth), free or tempo-synced,
retriggered per note, free-running or one for the whole part. *Twin Pulse* multiplies these two so
its drone only swells where both are up.

![Mod buses](docs/readme/buses.jpg)

**BUSES.** Four per part. A bus mixes the routes into it by **Sum, Multiply, Min or Max**, bends the
result (linear, exponential, log, S-curve, absolute), sets its polarity (as is, unipolar,
bipolar, inverted), can wait and fade in
after the note, lags it, and runs per note or once for the whole part. Name it and it routes on under
that name, as *Flight Steps* does with **Wing Speed** and **Flock Heading**.

## The flock

![The FLOCK tab](docs/readme/flock.jpg)

The tabs under the sky set how each species flies. **Behaviour** is Shoal, Scatter, Hunt or Flee;
the knobs are the boids' cohesion, alignment and separation, plus speed, wander, how hard the lure
pulls, how near counts as a neighbour, and the size of the sky. **Lure X** and **Lure Y** choose what places a note's
lure (pitch, velocity, press, slide, release, random or the centre), and **Walls** are soft, wrap or
open. **Tail** and **Spread** set how long the ink lasts and how far it blooms.

![The MOTION tab](docs/readme/motion.jpg)

**MOTION** holds the two LFOs that move only the flock: the **wingbeat**, one per creature, and the
**wind**, one gust for the whole sky, with **Push** for how hard each shoves. **VOICE** has the
stereo field (X Pan spreads creatures across it by where they fly), polyphony and mono/legato, glide,
bend range, levels and live meters. **TOUCH** has a curve and smoothing for each touch dimension and
glide rounding for MPE slides.

Between creatures, the nearest neighbour's Osc 1 feeds this creature's Osc 2 (**Neighbour FM**);
creatures flying the same way drift toward pure intervals (**Tune Pull**); and a hunter that catches
its prey ring-modulates it (**Bite**).

![The SKY view](docs/readme/sky.jpg)

**SKY**, top right, hides everything but the flocks. Part A's creatures are orange and B's teal;
grey rings are lures, a pink ring is a creature that's been caught, and thin lines join neighbours
that hear each other.

## Modulation: MAP and routes

There is no matrix page. Click a source chip and **MAP** turns on with it armed; drag any knob and
the drag sets how far that source moves it, drawn as a ring in the source's colour (up to four per
knob). Double-click a knob to flip its route, right-click to remove it. Sources are the touch chips
in the header (Velocity, Press, Glide, Slide, Release), the envelope, LFO and bus chips on their
cards, and the flock's own under the sky (Speed, Nearest, Heading, Turn, Random). Key, Mod Wheel and
four macros are in the route list. While a part plays, each moved knob shows a white line out to
where the newest note has it now, MAP or not.

![The route list](docs/readme/routes.jpg)

Each part has 64 routes (source, destination, amount), all host params, so depths can be
automated. **ROUTES** lists the ones in use, sixteen to a page, to edit by name: **+ ADD** starts
one and **X** removes it.

## Play it with your hands

JSynth is MPE first. Your finger is the creature's lure: the note sets where it sits left to right,
**Glide** (pitch bend) moves it sideways, **Slide** lifts it, and **Press** makes the creature fly
faster and brighter. Release velocity is a source too. The play mode decides which part a note
hatches in: **Single, Layer, Split, MPE Zones, Velocity** or **Expression**. Most presets also answer
the mod wheel, so they play from any keyboard.

## Presets

The arrows step through presets, the name opens them by category, and **SAVE** stores the current
sound as a user preset (`.jspreset`, plain text, under `JSynth/Presets` in your application data
folder). To delete a user preset, load it and pick **Delete** from the name menu. Click the **star**
to make any preset a favorite; **Favorites** sits at the top of the menu and is shared by every
instance.

The 166 factory presets are level-matched and come in ten categories:

| Category | What's in it |
|---|---|
| **Essentials** | Plain patches to start from: basses, leads, keys, bells, plucks, brass, strings, pads, splits |
| **Flock** | The creatures playing the sound: Neighbour FM, Bite, Tune Pull and the flock's sources |
| **Pressure** | Press shaped by the buses: curved, multiplied, delayed and lagged |
| **Expressive** | Played with Press, which bows, blows, bends, gates or blooms the sound |
| **Drones** | Long drifting Shape envelopes under a held chord |
| **Textures** | Shapes as the amp, so each held note plays a pattern |
| **Motion** | Patches that keep moving on their own |
| **Interplay** | Envelopes routed onto each other's Time, Warp and Bend |
| **Landscapes** | Both parts at once on Shape envelopes that move each other |
| **Monsta** | Variations on MicroMonsta 2 programs |

## For developers

- [ARCHITECTURE.md](ARCHITECTURE.md) walks through the code: signal flow, timing, the voice, the
  flock, the couplings, every parameter with its range, the presets, threads and state.
- `src/params.h` writes a part's params once, as offsets; `partId(part, offset)` gives the host's id.
  Each module is a block of ids with room to grow, so ids stay put once released, and unknown ids in
  a saved state are skipped.
- With `-DJCLAP_BUILD_TOOLS=ON`:
  - `jsynth-screenshot out.png [part [routes [map-source [width height [view [voice-tab [env-tab
    [osc-tab [preset]]]]]]]]]` renders the panel offscreen. [`docs/readme/make.sh`](docs/readme/make.sh)
    makes this page's pictures with it.
  - `jsynth-record out.wav|out.mp4 [preset] [cm7-swell|four-bar|drone|landscape]` plays a phrase
    through a preset and writes the audio, or a video of the panel with it.
