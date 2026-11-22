# Funk song v2 (100 BPM, E, 1:49)

Same arrangement as v1 (see ../cue-list.md), with three changes:

- **Drums:** JDrummer built from PR 285 (branch claude/project-thread-k0u0m5 at c3c302d), which
  fixes the Funk grooves: accented quarter hats, an open hat on the "and" of 4, ghosts around the
  backbeat, a tighter Pocket Funk, swing 0.52. Same settings as before, seed 14.
- **Kit:** still JKit with Stable Audio Open Small, but each pad was generated with a tight funk
  prompt in 4 to 8 candidates ("tight muffled kick drum, 1970s funk, dry close miked, short punchy
  thud", "closed hi-hat, tight crisp bright tick..."). The candidates were measured (`source/pick.py`:
  short decay, a bright top on the hats and snare) and the best went on the pads, often two takes a
  pad. `kit/kit.txt` lists which file sits on which pad; `source/cands.sh` has every prompt.
- **Bass:** JSynth *Finger Bass*, its bass guitar patch, in place of Rubber Bass.
