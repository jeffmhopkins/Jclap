# Funk song (100 BPM, E, 1:49)

All four parts come from JClap's own plugins, rendered offline:

- **JDrummer** (the real plugin, hosted): genre Funk, style *Tight Pocket*, drummer *Pocket Funk*,
  energy 0.6, weird 0.12, ghosts 0.35, vary 0.3, seed 14. The arrangement is the Section param
  automated on a playing transport, switching at the next bar, so JDrummer's own fills lead in.
- **JKit** (the real plugin, hosted): a WHOLE KIT (drum kit) made with Stable Audio Open Small, style
  "classic 1970s funk drum kit, crisp dry studio recording", two takes a pad. Its pads and prompts
  are in `kit/`; `kit/funk.jkit` is the plugin state.
- **JArp** Funk bass presets, holding root and b7, into **JSynth** *Rubber Bass*.

| Time | Bars | Drums (section) | Bass (JArp preset) | Chords |
|---|---|---|---|---|
| 0:00 | 1-4 | Intro | On the One | E7 |
| 0:10 | 5-12 | Verse | Funk Bass | E7 x6, G7, A7 |
| 0:29 | 13-20 | Chorus | Slap Pops | A7 A7 E7 E7 A7 A7 B7 B7 |
| 0:48 | 21-28 | Verse | Funk Bass | as before |
| 1:07 | 29-32 | Break | On the One | E7 |
| 1:17 | 33-40 | Chorus | Slap Pops | as before |
| 1:36 | 41-44 | Outro | Funk Bass | E7 |
| 1:46 | | final kick, crash and low E | | |

Mix: bass at 0.85 of the drums' level, soft-clipped, +4 dB above 5 kHz and a limiter.

Source: `source/song.py` runs it all; the C++ tools build inside the repo (add `demo-tools/` with
`source/CMakeLists.txt` as a subdirectory of the root CMakeLists).
