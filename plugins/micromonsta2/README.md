<div align="center">

# MicroMonsta 2 CC

A knob panel that sends every Audiothingies MicroMonsta 2 sound parameter as a MIDI CC.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![CLAP: note effect](https://img.shields.io/badge/CLAP-note%20effect-blue)](https://github.com/free-audio/clap) [![parameters: 61 CCs](https://img.shields.io/badge/parameters-61%20CCs-blue)](docs/cc-map.csv) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage)

</div>

A CLAP note effect for the Audiothingies MicroMonsta 2. Every sound parameter the
synth accepts over MIDI CC is a host parameter, so it can be automated or modulated, and each change goes
out to the synth as a CC. Notes and other MIDI pass straight through.

![Panel](docs/panel.png)

## Using it in Bitwig

1. Put **MicroMonsta 2 CC** on the track that plays the MicroMonsta 2, before the hardware instrument
   (or HW Instrument) device that sends MIDI to the synth.
2. Set the panel's **MIDI CH** to the synth's MIDI channel (SYSTEM page S02).
3. Turn knobs, automate them, or drop Bitwig modulators on them. Modulation is added on top of the
   knob's setting: the knob stays where you put it, a thin white arc shows how far the modulator moves
   it, and the number under the knob is the value actually sent.

> [!TIP]
> In bi-timbral mode Program B listens on the MIDI channel + 1, so use a second instance for it.

## Programs

The bar at the top lists the programs stored on the synth, by slot and category ("P013 OctaBass").
Picking one:

- sends Bank Select (CC 0 = 0, CC 32 = 0-2) and Program Change (0-99) on the panel's MIDI channel, so
  the synth loads that program (P001-P100 are bank 0, P101-P200 bank 1, P201-P300 bank 2);
- moves every knob to that program's value without sending it, since the synth already has it. Turning
  a knob afterwards sends it as usual. Mod wheel, mod knob and volume aren't part of a program and stay
  where they are;
- labels the mod matrix knobs with the program's routing in plain English: what the slot modulates on
  top ("CUTOFF", "OSC2 SHAPE", "RELEASES" for all three envelopes), the source under the value
  ("LFO 2", "LFO1×Wheel" for LFO 1 scaled by the mod wheel). Slots with no source or destination say
  OFF. Hovering a mod knob says it in a sentence: "Mod slot 1: LFO 2 modulates the pitch of all
  oscillators. The knob sets how much: none at the centre, reversed below it (CC 52)."
- renames the mod matrix parameters in the host to match, so automation lanes read "Mod 1: LFO 2 →
  Pitch" instead of "Mod Matrix 1".

CCs can only change a slot's amount, so the routing stays until another program is picked. The names
follow the manual's description of each source and destination (sections 6.8.4 and 6.8.5).

The project remembers the program and sends its Program Change again when it opens. **SAVE** stores the
knobs as your own preset (under "User"), which sends its CCs when loaded and doesn't change the program.

> [!NOTE]
> Audiothingies hasn't published the factory bank or the SysEx program format. The bank here is the 209
> named programs from a full dump of a real unit (`data/bank.syx`, UTILITY U02), which is the factory
> bank plus a few programs its owner saved. `tools/mm2_bank.py` decodes it into `src/factory_bank.inc`
> and checks the decoding against `data/cal-*.syx`: single-program dumps taken after
> `tools/mm2_capture.py` set every CC to a known pattern. Tune, fine tune, cutoff and the ±99 amounts are
> stored in their own ranges, so the panel shows the CC value that gives the stored value.
>
> To use your own synth's bank: run `python tools/mm2_capture.py data` with the synth on USB (it walks
> you through the U02 and U01 dumps), then `python tools/mm2_bank.py data/bank.syx src/factory_bank.inc`
> and rebuild.

## How it sends

> [!NOTE]
> A CC is only sent once its knob has been set (by you, automation or modulation). Inserting the
> plugin never overwrites the synth's current sound with the plugin's defaults.

- Saved projects remember which knobs were set and send them again when the project opens.
  **SEND ALL** pushes every knob's value to the synth now. A knob never set sends its default, which
  is an init patch rather than all zeros: Volume 100, Osc1 Level, Cutoff and the env sustains at 127,
  the LFO speeds and centered knobs at 64, and the rest at 0. Resetting a knob to its default sends
  the same value.
- Modulation sends each CC at most every 5 ms, so a fast modulator can't flood 5-pin MIDI. The
  latest value always goes out.
- A CC from your controller that passes through on the synth's channel counts as sent: the plugin
  doesn't push its knob back over it until you move that knob.
- Values are plain 0 to 127. The manual doesn't say how each CC maps to ms, Hz or semitones, so the
  panel shows the raw value. Tune, fine tune, filter env amount, EQ and the mod matrix knobs are drawn
  as centered (64 = middle).

> [!WARNING]
> The manual lists no MIDI out jack and doesn't say the synth sends CCs, so the panel doesn't follow
> the synth's own knobs.

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

Built from the shared [jclap::ui](../../libs/ui) widgets, which draw with Visage. `src/editor.cpp`
only holds the layout.
Drag a knob up or down (shift for fine), use the mouse wheel for single steps, double-click to reset.
Right-click a knob to reset it, type a value, or reach the host's menu for that parameter;
right-click the background to pick a UI size. The window can be resized and keeps its shape.

> [!TIP]
> To regenerate `docs/panel.png`, configure with `-DJCLAP_BUILD_TOOLS=ON` and run
> `build/plugins/micromonsta2/micromonsta2-screenshot plugins/micromonsta2/docs/panel.png`. With tools
> on, `ctest` checks the panel still renders like that image.
