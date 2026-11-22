# Funk song v3

v2 (same drums, kit sounds and Finger Bass), plus JKit's own mix settings:

- **Reverb:** JKit's Room reverb, size 0.5, decay 1.5 s, return -2 dB. Pad sends: snare, clap and
  snare 2 at 0.45, rim 0.35, toms 0.3, FX 0.3, crash and perc 0.25, open hat 0.2, closed and pedal
  hats and shaker 0.15, kicks 0.08.
- **Panning** (drummer's view): hats -0.4 (left), snare and rim -0.1, high tom -0.15, mid tom +0.15,
  floor tom +0.45, crash +0.35, perc +0.35, shaker +0.3, kicks centre.

`kit.txt` holds it all (field 1 is Pan, 16 is Reverb Send); `funk.jkit` is the JKit state.
The kit sounds are in ../v2/kit/.

v3b: the closed and pedal hats (the short airy ticks, which can read as a shaker) and the shaker
are sent wetter: closed and pedal hats 0.32, shaker 0.35, open hat 0.3.

v3c: every pad reviewed by rendering it solo with and without the reverb. The Room reverb barely
answered the airy hats (closed hat -35 dB wet against dry), so it's now JKit's Plate (brighter), with
sends raised: hats and shaker 0.5, open hat 0.4, rim 0.4, crash and perc 0.35. Wet against dry now:
snare and clap -16 dB, shaker -18, rim -19, open hat -20, crash -21, closed hat -24, kick -31 (kept
nearly dry on purpose). The bass (JSynth) has no reverb.
