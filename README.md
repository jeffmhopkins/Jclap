# JClap: Orb Polar

A tiny CLAP note-effect plugin with no GUI. It listens for two MIDI CCs carrying an X/Y position
(Cartesian) and sends out two CCs carrying the same position as an angle and a radius (polar), ready to
MIDI Learn onto Omnisphere's Orb. Built for Bitwig, but any CLAP host with note effects works.

## Using it in Bitwig

1. Put **Orb Polar** on the Omnisphere track, before Omnisphere.
2. Send your X/Y controller to CC 16 (X) and CC 17 (Y). The plugin swallows those two CCs and emits
   CC 18 (angle) and CC 19 (radius) on the same MIDI channel. Everything else passes through untouched.
3. In Omnisphere, right-click the Orb, choose **MIDI CC Learn**, and move the controller so it learns
   the angle (CC 18). Right-click again, choose **Switch to Radius**, MIDI CC Learn again and it learns
   the radius (CC 19). Tip: while learning, wiggle only along one axis near the edge so the CC you want
   is the one that changes most.

## Mapping

- X and Y are 0..127 with 64 as center. Y grows upward.
- **Radius**: 0 at center, 127 at the edge. A square XY pad can reach about 1.41 in the corners; by
  default this is clamped to the unit circle (`Radius Mode = Clamp to circle`). `Fill square` instead
  uses `max(|x|, |y|)` so the whole square edge reaches full radius.
- **Angle**: 0..127 covers one full turn. 0 is +X (pointing right), increasing counter-clockwise. The
  wrap from 127 back to 0 is a neighbor step on the circle, not a jump.
- At dead center the angle is undefined, so the plugin holds the last angle and only sends radius 0.
- Output CCs are only sent when their value changes.

Spectrasonics doesn't document where the Orb's angle 0 sits or which way it turns, so these are
exposed as settings. If the Orb dot lands rotated or mirrored from your controller, adjust
`Angle Offset` (degrees) and `Direction`.

## Settings (host parameters, no GUI)

| Parameter | Default | |
|---|---|---|
| X In CC / Y In CC | 16 / 17 | Input CC numbers |
| Angle Out CC / Radius Out CC | 18 / 19 | Output CC numbers |
| Angle Offset | 0 deg | Rotates the angle |
| Direction | Counter-clockwise | Flip to clockwise to mirror |
| Radius Mode | Clamp to circle | Or Fill square |
| Pass Input CCs | Off | Also forward the raw X/Y CCs |

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

Copy `build/Orb Polar.clap` (a bundle on macOS) to your CLAP folder: `~/Library/Audio/Plug-Ins/CLAP`
on macOS, `C:\Program Files\Common Files\CLAP` on Windows, `~/.clap` on Linux. CI builds all three; tagged
versions (`v*`) are published as zips on the Releases page.
