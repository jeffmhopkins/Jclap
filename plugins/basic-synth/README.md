<div align="center">

# Basic Synth

An eight-voice MPE synth built from jclap::dsp: oscillator, filter and VCA, two envelopes and an LFO per voice.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![CLAP: instrument](https://img.shields.io/badge/CLAP-instrument-blue)](https://github.com/free-audio/clap) [![MPE](https://img.shields.io/badge/MPE-yes-blue)](#mpe) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](#panel)

</div>

The simplest complete synth: it exists to show [jclap::dsp](../../libs/dsp) in a real plugin, and to
start a new instrument from.

![Panel](docs/panel.png)

## The voice

```
oscillator ──> filter ──> VCA ──> out
                 ^         ^
   filter env ───┤         ├─── amp env
   LFO ──────────┤         ├─── LFO (tremolo)
   pressure, timbre ───────┘ (cutoff)
```

- **Oscillator**: sine, triangle, saw, square or pulse, with pulse width, band-limited.
- **Filter**: low, high or band pass at 12, 24, 36 or 48 dB/oct, with resonance.
- **Filter envelope**: an ADSR that moves the cutoff by its amount, up to ±5 octaves.
- **Amp envelope**: an ADSR that shapes the VCA by its amount. At 0 the VCA just follows the key:
  on while held, off on release.
- **LFO**: sine, triangle, saw, square or random, 0.05 to 20 Hz, restarted with each note. It moves the
  cutoff (±4 octaves) and the VCA (tremolo). The light next to it shows the newest note's LFO.
- **Voices**: eight. A ninth note takes the quietest releasing voice, or else the oldest.

## Automation and modulation

Every parameter is a host parameter: Bitwig can automate it, and its modulators can move it. Every
one except MPE Bend Range can also be modulated **per voice**, so a polyphonic modulator in Bitwig
(a per-note LFO, velocity or random, for instance) moves each note on its own. The knobs and
envelope drawings show the host's modulation as a light ring or line over the value.

## MPE

Every note bends, presses and slides on its own.

| Expression | CLAP note expression | MPE MIDI | Does |
|---|---|---|---|
| Bend | Tuning | Pitch bend on the note's channel (±48 st, set by MPE Bend Range) | Pitch |
| Pressure | Pressure | Channel pressure | Opens the cutoff, by up to the Pressure knob's octaves |
| Timbre | Brightness | CC 74 | Moves the cutoff either way from its centre, by up to the Timbre knob's octaves |
| Volume | Volume | | Level |

Plain MIDI works too: notes on channel 1 play, and its pitch bend moves every note by ±2 semitones (as
the MPE manager channel does). The synth reports each finished voice to the host (`NOTE_END`), so
Bitwig's per-note modulators know when a note is done.

> [!TIP]
> In Bitwig, MPE controllers and per-note expressions from the piano roll arrive as CLAP note
> expressions, with no MPE setup needed.

## Panel

The **GRID** tab is a LinnStrument-style note grid: rows a fourth apart, and every held note shows its
bend (sideways), pressure (size) and timbre (height). Play it with the mouse too: press, slide sideways
to bend, up and down for timbre. **KEYS** is a piano keyboard.

![Note grid](docs/panel-grid.png)
