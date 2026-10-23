# MicroMonsta 2 CC

A CLAP note effect with a knob panel for the Audiothingies MicroMonsta 2. Every sound parameter the
synth accepts over MIDI CC is a host parameter, so it can be automated or modulated, and each change goes
out to the synth as a CC. Notes and other MIDI pass straight through.

![Panel](docs/panel.png)

## Using it in Bitwig

1. Put **MicroMonsta 2 CC** on the track that plays the MicroMonsta 2, before the hardware instrument
   (or HW Instrument) device that sends MIDI to the synth.
2. Set the panel's **MIDI CH** to the synth's MIDI channel (SYSTEM page S02). In bi-timbral mode
   Program B listens on that channel + 1, so use a second instance for it.
3. Turn knobs, automate them, or drop Bitwig modulators on them. Modulation is added on top of the
   knob's setting: the knob stays where you put it, a thin white arc shows how far the modulator moves
   it, and the number under the knob is the value actually sent.

## How it sends

- A CC is only sent once its knob has been set (by you, automation or modulation). Inserting the
  plugin never overwrites the synth's current sound with the plugin's defaults.
- Saved projects remember which knobs were set and send them again when the project opens.
  **SEND ALL** pushes every knob's value to the synth now.
- Values are plain 0 to 127. The manual doesn't say how each CC maps to ms, Hz or semitones, so the
  panel shows the raw value. Tune, fine tune, filter env amount, EQ and the mod matrix knobs are drawn
  as centered (64 = middle).
- The manual lists no MIDI out jack and doesn't say the synth sends CCs, so the panel doesn't follow
  the synth's own knobs.

## CC map

From the *MicroMonsta 2 User manual v2.8*, section 7.1. The full list is in
[docs/cc-map.csv](docs/cc-map.csv).

| Section | Parameters (CC) |
|---|---|
| Oscillators | Osc1 Shape 39, Tune 40, Fine 41 · Osc2 42-44 · Osc3 45-47 |
| Mix | Osc1 48, Osc2 49, Osc3 50, Noise 51 |
| Filter | Cutoff 74, Resonance 71, Env Amount 88, Tracking 89, FM 90 |
| Envelopes | Env1 A/D/S/R 73/75/79/72 · Env2 80-83 · Env3 84-87 |
| LFO | Speed 76, 77, 78 |
| Mod Matrix | Levels 1-10 52-61, Lag 1 62 |
| Effects | Delay Time 26, Feedback 27, Level 28 · Reverb Decay 29, Mod 30, Level 31 · EQ 25 |
| Voice | Glide 5, Detune 33, Osc Detune 34, Drive 91, Pan Spread 35, VCA/Filter Env Velocity 36/37 |
| Header | Mod Wheel 1, Mod Knob 70, Volume 7 |

Bank select (0/32), sustain (64) and All Sound/Notes Off (120/123) are left out.

## Panel

Drawn with [Visage](https://github.com/VitalAudio/visage), pinned to a commit in `CMakeLists.txt`.
Drag a knob up or down (shift for fine), use the mouse wheel for single steps, double-click to reset.
The window can be resized and keeps its shape.

To regenerate `docs/panel.png`, configure with `-DJCLAP_BUILD_TOOLS=ON` and run
`build/plugins/micromonsta2/micromonsta2-screenshot docs/panel.png`.
