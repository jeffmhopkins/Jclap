<div align="center">

# JBreath

An eight-voice MPE wind instrument built from physical models: flute, clarinet, sax, oboe and trumpet, each a digital waveguide blown by a model of the player, with every voice drawn live as a glowing loop of its own breath.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![C++: 17](https://img.shields.io/badge/C%2B%2B-17-blue)](../../CMakeLists.txt) [![CLAP: 1.2.10 instrument](https://img.shields.io/badge/CLAP%201.2.10-instrument-blue)](https://github.com/free-audio/clap) [![voices: 8](https://img.shields.io/badge/voices-8-blue)](#voices) [![MPE: yes](https://img.shields.io/badge/MPE-yes-blue)](#playing-it) [![synthesis: waveguide](https://img.shields.io/badge/synthesis-waveguide-blue)](#the-instruments) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage) [![Platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Linux-blue)](../../.github/workflows/build.yml)

</div>

![Panel](docs/panel.png)

## The instruments

Each voice is a bore (a delay line carrying the sound down the tube and back, with the losses and
reflections of the real one) driven by a nonlinear exciter, the part the player blows into:

| Instrument | Exciter | Bore | What gives it its sound |
|---|---|---|---|
| **Flute** | an air jet crossing the embouchure hole, split in or out of the pipe by the sound in it | open at both ends: every harmonic | the jet takes longer to reach the edge when it's slow, so blowing harder pushes it up a register; edge noise; a 2nd harmonic that grows with the breath |
| **Clarinet** | a single reed: a pressure-controlled valve (Kergomard / Wilson-Beavers), solved each sample with the pressure it makes | a cylinder closed at the reed: odd harmonics | the reed slamming shut at forte; the bell letting the highs out |
| **Sax** | the same kind of reed, softer | a cone, as a split bore: every harmonic, overblowing at the octave | the mouthpiece's honk; growl when it's pushed |
| **Oboe** | a stiff double reed | a narrow cone, darker | formants near 1.1 and 2.9 kHz from the bell and tone holes: nasal, not a soprano sax |
| **Trumpet** | the lips: a mass on a spring blown open by the mouth pressure (outward striking), with the flow through them in closed form | a flared horn: resonances on every harmonic, none at DC | the wave steepening towards a shock at forte: the brassy blare |

Every note plays in tune: the bore length for each model and note was measured offline by playing
it and correcting the length until the pitch was within a cent, at the rates the voices run at, and
baked into [`src/tuning_table.h`](src/tuning_table.h) along with a gain that levels each note's
steady tone. Notes past an instrument's range play an octave (or more) in. The voices run at 88.2 kHz
or more (2x at 44.1 and 48 kHz) and are decimated once.

## Playing it

The player model turns a note into breath, embouchure and pitch:

- **Breath** comes from MPE pressure, a breath controller (CC 2), velocity, or the BREATH IN
  parameter (modulate it from a CV, an LFO or automation: in Bitwig, route a eurorack CV in to it).
  BREATH FROM picks: **Auto** blows with the velocity until pressure or Breath In arrives and then
  follows it (so it plays from a keyboard and from an MPE controller alike), **Press** only from
  pressure, **Vel** only from velocity, **Input** only from Breath In, and **Mix** from Breath In
  times each note's own pressure (or velocity, until pressure arrives): the CV is the lungs for the
  whole chord, each finger how much of it its note lets through. CURVE shapes how pressure becomes
  breath.
- **The tongue** lets the air go with a small burst of pressure (KICK), the breath rises over ATTACK
  and dies over RELEASE.
- **Embouchure** (lip and jaw) is the EMBOUCHURE knob, moved by MPE slide (CC 74) by SLIDE: the jet's
  angle on the flute, the bite on a reed, the lips' tension on brass.
- **Bend** bends the bore: MPE bend per note (BEND sets the note channels' range), CLAP tuning
  expressions, and the manager channel's bend for every note.
- **Vibrato** grows in after DELAY, in the breath (DEPTH) and the pitch (PITCH), and its rate and
  depth wander a little, as a player's do.
- **NOISE** is the breath noise and turbulence; **EDGE** the forte bite: the flute's 2nd harmonic,
  the reeds' growl, the trumpet's steepening.

### Voices

POLY plays eight voices; a ninth note takes the quietest released one, or the oldest. MONO and
LEGATO play one: letting go of a note goes back to the newest key still held. In MONO the tongue
stops the air between notes; in LEGATO the fingers change and the air doesn't stop (a slur), with
GLIDE setting how long the pitch takes to get there.

### Presets

Fifty factory presets, about ten for each instrument, from clean to grungy, airy, bass and the
odd ones: Concert Flute to Tin Whistle, Piccolo and Bass Flute; Wooden Reed to Klezmer Cry and Bass
Clarinet; Clean Alto to Gutbucket and Bass Honk; Pastoral to Bassoon, Shawm and Duduk Air; Brass
Plume to Flugel Glow, French Horn, Tuba and Blat. Each plays in tune at a usable level (tested).

## The panel

- **EXCITER**: the five instruments, the valve curve of the one playing (how the air through it
  answers the pressure across it, with a dot for every sounding voice), and how it's blown.
- **BORE**: the bore's input impedance, worked out from the same loop the model runs, with the
  partials of the loudest note as it sounds, and how long the real tube would be for that note.
- **PLAYER**: the tongue's breath envelope and the vibrato, drawn from their knobs.
- **BREATH LOOPS**: each voice as a loop in three dimensions (its sound now, a quarter period ago and
  half a period ago), slowly turning. A steady tone draws a clean closed loop; growl, a split note or
  breath noise fray it.
- **VOICE**: the voice mode, where the breath comes from, the press curve with each voice's
  pressure on it, the eight voices, and the output.

## Building

It builds with the rest of JClap (see the [top-level README](../../README.md)). With
`-DJCLAP_BUILD_TOOLS=ON` there are also:

- `jbreath-screenshot out.png [width height [frames]]`: renders the panel (the screenshot test's
  reference is [`docs/panel.png`](docs/panel.png)).
- `jbreath-tune > src/tuning_table.h`: bakes the tuning table again, after a change to a model.

`jbreath-voice-tests` checks every instrument across its range (tuning, level, how fast it speaks,
and that nothing blows up with every control at its extremes); `jbreath-tests` drives the whole
plugin the way a host does, at 44.1 to 192 kHz.

## Coming next

The vapour field behind the loops, JSynth-style modulation (source chips, MAP and routes), and an
AIR view for the room.
