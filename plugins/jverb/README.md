<div align="center">

# JVerb

A reverb whose delay network is moved by a live three-body gravity simulation, with a long echo, a
drive you can loop into the tail, and presets.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![C++: 17](https://img.shields.io/badge/C%2B%2B-17-blue)](../../CMakeLists.txt) [![CLAP: audio effect](https://img.shields.io/badge/CLAP-audio%20effect-blue)](https://github.com/free-audio/clap) [![DSP: jclap::dsp](https://img.shields.io/badge/DSP-jclap%3A%3Adsp-blue)](../../libs/dsp) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage)

</div>

![Panel](docs/panel.png)

## How it sounds the way it does

Every body in the system owns two delay lines in a feedback delay network. As a body swings out from
the centre of mass its lines stretch, and as it falls back in they shrink, so you hear its orbit as
slow, never-repeating Doppler drift. Three bodies (the default) start on a figure-eight that slowly
turns chaotic; add more, up to six, and the network goes from sparse and echoey to dense.

```
in -> low/high cut -> [drive PRE] -> [echo INTO] -> predelay + early reflections
   -> diffusion -> attack (swell, or reverse windows)
   -> tank: 2 lines per body, Householder mix, bass and high damping
   -> outer loop: [drive LOOP] -> shimmer -> smear -> tilt -> feedback -> [echo LOOP] -> safety -> back in
   -> early/late -> [drive POST] -> [echo AFTER] -> duck, width, mix, level -> out
```

- **Space**: Decay (RT60), System Size (scales every line and the early reflections), Gravity (how
  fast and tight the orbits run), Bodies, Motion (how much distance moves the lines), Predelay and
  Damp High. **Shape** has Attack, Diffusion, Early/Late, the bass multiplier and crossover, the input
  cuts and React.
- **Bodies**: each has Mass and Velocity, which change the orbit itself, and a Target with an Amount.
  A body's modulation signal comes from its speed against the others and how close its nearest
  neighbour is, and goes to Delay Mod, Size, Drive, Shimmer, Feedback, Tilt, Width or Echo time.
- **Loop**: Feedback sends the tank's output back to its input; past 100% it grows into the safety
  stage (soft saturation or a hard clip). Shimmer pitch shifts the loop, Smear blurs it, Tilt colours
  it, Freeze holds the tail forever.
- **Drive**: Tape, Tube, Fuzz, Fold or Crush, oversampled 2x, in the PRE, LOOP or POST slot.
- **Echo**: up to four seconds, free in milliseconds or synced to a note value, with feedback, tone and
  ping-pong. INTO puts the repeats into the reverb, LOOP sends echoes of the tail back into the tank
  so it returns in long passes, AFTER echoes the reverb itself. A body routed to Echo bends the time
  like a tape machine.

## The orbit view

Drag a body to move it, drag the arrow on the selected body to set its velocity, scroll on a body to
change its mass. Double-click empty space to add a body there, right-click a body to remove one.

As they move, the bodies shed soft gas that billows out, drifts on a slow swirl and falls back
toward them. Where two bodies' clouds meet they push each other apart, and the seam heats up and
glows whiter. Incoming audio is split into lows, mids and highs. Each band makes the bodies that
follow it glow, shed bigger clouds and spark dust, which falls through the system's gravity, and its
transients send out rings that shove the gas aside. React also kicks the bodies on transients, so
the input changes the orbit itself and through it the sound.

The clock runs the orbits free, synced (one lap of the start orbit fits into Sync Bars, restarting
when the host starts), or free but restarted with the transport (RETRIG).

## Presets

The preset bar in the header steps through the factory bank (Basics, Ambient, Echo, Drive, Orbits)
and your own. SAVE names the current settings and writes them to:

- macOS: `~/Library/Application Support/JVerb/Presets`
- Windows: `%APPDATA%\JVerb\Presets`
- Linux: `$XDG_CONFIG_HOME/JVerb/Presets` (or `~/.config/JVerb/Presets`)

Each is a small text file, `.jvpreset`, listing the params that differ from their defaults. The
preset's name is saved with the project.

## Threads

The audio thread owns the simulation and the reverb. Every 32 samples it steps the orbits and
updates the network, so a render comes out the same at any block size. A few hundred times a second
it publishes a frame (positions, band levels, transients) through a triple buffer; the panel reads
the newest one when it draws, about 60 times a second, and keeps the gas, dust and ripples to
itself. Drags go back through a lock-free queue. Neither side ever waits for the other, and nothing on
the audio thread allocates after activate.

## Panel

1200 x 780, scalable. Renders for the docs come from `jverb-screenshot out.png [bodies]`, which runs
the orbits with a made-up drum and pad input:

![Six bodies](docs/panel-six.png)
