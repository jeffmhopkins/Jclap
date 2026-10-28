<div align="center">

# Polar XY

Turns an XY controller's two CCs into angle and radius CCs for Omnisphere's Orb.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![CLAP: note effect](https://img.shields.io/badge/CLAP-note%20effect-blue)](https://github.com/free-audio/clap) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](#panel) [![host: Bitwig](https://img.shields.io/badge/host-Bitwig-blue)](#using-it-in-bitwig)

</div>

A small CLAP note effect. It listens for two MIDI CCs carrying an X/Y position
(Cartesian) and sends out two CCs carrying the same position as an angle and a radius (polar), ready to
MIDI Learn onto Omnisphere's Orb. Built for Bitwig, but any CLAP host with note effects works, and
there is a VST3 build for hosts without CLAP.

![Panel](docs/panel.png)

## Using it in Bitwig

1. Put **Polar XY** on the Omnisphere track, before Omnisphere.
2. Send your X/Y controller to CC 16 (X) and CC 17 (Y). The plugin swallows those two CCs and emits
   CC 18 (angle) and CC 19 (radius) on the same MIDI channel. Everything else passes through untouched.
3. In Omnisphere, right-click the Orb, choose **MIDI CC Learn**, and move the controller so it learns
   the angle (CC 18). Right-click again, choose **Switch to Radius**, MIDI CC Learn again and it learns
   the radius (CC 19).

> [!TIP]
> While learning, wiggle only along one axis near the edge so the CC you want is the one that changes
> most.

## VST3

`PolarXY.vst3` is the same plugin, wrapped by clap-wrapper. It's in `jclap-PolarXY-<version>-VST3-<OS>.zip`
on the release page; see the [main README](../../README.md#download) for where it goes.

- It shows up as an instrument (VST3 has no MIDI-effect category), with a MIDI input and output.
- The X/Y CCs reach it the usual VST3 way, as the host's MIDI CC mapping, and the angle and radius
  go out as MIDI CC events, so the host has to pass a plugin's MIDI output on to the next device.
  Ableton Live, for one, doesn't take MIDI out of VST3s; Bitwig, Cubase and Reaper do.

## Mapping

- X and Y are 0..127 with 64 as center. Y grows upward.
- **Radius**: 0 at center, 127 at the edge. A square XY pad can reach about 1.41 in the corners; by
  default this is clamped to the unit circle (`Radius Mode = Clamp to circle`). `Fill square` instead
  uses `max(|x|, |y|)` so the whole square edge reaches full radius.
- **Angle**: one full turn in steps of 1/127, so 0..126. 0 is +X (pointing right), increasing
  counter-clockwise. 127 would be a full turn, the same angle as 0, so the wrap goes from 126 to 0.
- Around dead center (within about two CC steps) the angle is undefined, so the plugin holds the last
  angle and sends radius 0. Controller jitter at rest can't swing the Orb across quadrants. Outside
  that the radius starts at 1.
- X and Y arriving at the same moment are one move: only the final point goes out.
- Each MIDI channel has its own position. Output CCs are only sent when their value changes.
- If X and Y In (or Angle and Radius Out) are set to the same CC, nothing is converted and the input
  CCs pass through. The panel shows a warning.

## Motion

By default the Orb jumps to wherever the controller is. **Smoothing** and **Inertia** make it travel
there instead, sending a stream of in-between CCs (one step every 32 samples, only when a value
changes) until it arrives. Each channel moves on its own.

- **Smoothing** is roughly how long the Orb takes to glide to the controller's position.
- **Inertia** gives it weight: the heavier it is, the slower it gets going and the further it swings
  past the target before settling.
- **Motion Path** (PATH on the panel) picks what moves. *Straight* glides across the pad in X/Y, so going from one side to the
  other passes through the centre. *Around* moves the angle and radius on their own, so the Orb travels
  round the circle the short way. The angle keeps counting past a full turn, so stirring the controller
  with some Inertia sets the Orb whirling, and it carries on past where you stop.

The first position on a channel is jumped to, as there's nowhere to glide from. A reset lands anything
still moving.

> [!IMPORTANT]
> Spectrasonics doesn't document where the Orb's angle 0 sits or which way it turns, so these are
> exposed as settings. If the Orb dot lands rotated or mirrored from your controller, adjust
> `Angle Offset` (degrees) and `Direction`.

## Panel

**XY IN** shows the last X/Y position received, with its channel, and **POLAR OUT** the angle and
radius the plugin sent for that channel (a dash until both axes have arrived),
with angle 0 marked by the tick on the right. Comparing the two shows what `Angle Offset` and
`Direction` do. Below them are all the settings, which are also plain host parameters.

The preset bar in the header steps through presets with its arrows, lists them from the name, stars
favourites, and SAVE stores the current settings as your own preset. The factory bank is a single
**Init** with everything at its default.

> [!TIP]
> To regenerate `docs/panel.png`, configure with `-DJCLAP_BUILD_TOOLS=ON` and run
> `build/plugins/polar-xy/polar-xy-screenshot plugins/polar-xy/docs/panel.png`. With tools on,
> `ctest` checks the panel still renders like that image.

## Settings

| Parameter | Default | |
|---|---|---|
| X In CC / Y In CC | 16 / 17 | Input CC numbers |
| Angle Out CC / Radius Out CC | 18 / 19 | Output CC numbers |
| Angle Offset | 0 deg | Rotates the angle |
| Direction | Counter-clockwise | Flip to clockwise to mirror |
| Radius Mode | Clamp to circle | Or Fill square |
| Pass Input CCs | Off | Also forward the raw X/Y CCs |
| Smoothing | 0 ms | Glide time, up to 2 s |
| Inertia | 0% | Weight: lag and overshoot |
| Motion Path | Straight | Or Around, to move round the circle |
