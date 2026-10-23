<div align="center">

# Basic Synth

An eight-voice MPE synth: one oscillator, filter, envelope and VCA per voice, built from jclap::dsp.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![CLAP: instrument](https://img.shields.io/badge/CLAP-instrument-blue)](https://github.com/free-audio/clap) [![MPE](https://img.shields.io/badge/MPE-yes-blue)](#mpe) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](#panel)

</div>

The simplest complete synth: it exists to show [jclap::dsp](../../libs/dsp) in a real plugin, and to
start a new instrument from.

![Panel](docs/panel.png)

## The voice

```
oscillator ──> filter ──> VCA ──> out
                 ^         ^
   envelope ─────┴─────────┘
   pressure, timbre ──> cutoff
```

- **Oscillator**: sine, triangle, saw, square or pulse, with pulse width, band-limited.
- **Filter**: low, high or band pass at 12, 24, 36 or 48 dB/oct, with resonance. The envelope moves
  the cutoff by up to ±5 octaves.
- **Envelope**: one ADSR, shaping both the VCA and the cutoff.
- **Voices**: eight. A ninth note takes the quietest releasing voice, or else the oldest.

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
