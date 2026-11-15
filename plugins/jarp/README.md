<div align="center">

# JArp

An MPE arpeggiator that plays the notes you hold through a grid pattern.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![CLAP: note effect](https://img.shields.io/badge/CLAP-note%20effect-blue)](https://github.com/free-audio/clap) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage) [![License: MIT](https://img.shields.io/badge/license-MIT-blue)](../../LICENSE)

</div>

A CLAP note effect, made for an MPE controller such as the LinnStrument. Hold a chord and the grid
plays it: each row is one of the notes you hold, each column a step, and a lit cell plays that row's
note on that step for as long as the cell is drawn. Every note goes out on the finger's own channel and
key, so the synth after it still hears that finger's pressure, slide and bend. Put it before any MPE
instrument.

Version 0.4.0.

![Panel](docs/panel.png)

## The grid

Eight rows by up to 128 steps, sixteen in view at a time. Row 1 is at the bottom.

- **Click an empty cell** for a one-step note, and drag right while you hold the button to make it
  longer.
- **Drag a note's right edge** to set how long it sounds, in quarter steps, from a quarter of a step to
  the whole pattern. A note stops at the next note in its row.
- **Click a note** to remove it.
- **Drag the purple bar** (or its tab) to set the pattern's length, from 1 to 128 steps. Drag it past
  the right edge and the grid scrolls on with it. The LENGTH knob is the same setting. Each pattern
  has its own length.

There is no gate setting: a note's length on the grid is its gate. A note on row 1 drawn across the
whole pattern holds a pedal under the rest.

### Long patterns

A pattern longer than 16 steps scrolls. A bar under the step numbers shows the whole pattern: the
lighter part is the 16 steps in view, the ticks are steps where notes start, and the orange line is
the playhead. Beside it, the steps in view (`17-32 of 48`) and **FOLLOW**.

![A long pattern](docs/long.png)

- With **FOLLOW** lit, the grid turns the page as the playhead moves on, 16 steps at a time.
- **Drag along the bar** (or the step numbers above it), or scroll the mouse wheel over the grid, to
  look elsewhere. That turns FOLLOW off; click it to follow again.
- The lanes under the grid scroll with it.

Notes and lane settings past the pattern's length are kept, dimmed, so shortening a pattern and
lengthening it again loses nothing.

The column with a frame round it is the step playing. A note playing lights up, and so does its row's
note name, with a dot beside it.

## Patterns and chains

JArp holds eight patterns, **1** to **8**, in the strip beside the PATTERN title. Click a number to
play it and edit it on the grid. A lit number has notes in it; the one playing has an orange bar under
it. Right-click a number to **copy** it, **paste** into it, or **clear** it, or to **load from
preset**: pick a preset, or one pattern of a preset that has several, and only that pattern loads into
this slot, leaving the other slots and the settings as they are.

The switch beside the numbers says how playing moves along them:

- **OFF**: the pattern selected plays.
- **CHORD**: each new chord moves to the next pattern. Let go of every note, play again, and the next
  pattern starts. The first chord plays the one selected.
- **LOOP**: while you hold, each time a pattern ends the next one starts, so 1, 2 and 3 play as one
  long phrase.

Empty patterns are skipped, and after the last pattern with notes it goes round to the first. To chain
1, 2 and 3, fill those three and leave the rest empty; clear one to drop it from the chain.

With RESTART on **SONG** the chain follows the song too. In LOOP the patterns are laid end to end from
the start of the song (beginning with the one selected), so where the song is decides which pattern
plays and which step: come in during the third bar of a 1 2 3 chain and you hear the part of the
chain that falls there. In CHORD the next pattern picks up at the step the song has reached in it.

Both are host params, so you can automate or modulate them: **Pattern** (1 to 8, the same numbers
as the strip) picks the pattern, and **Advance** (Off, Chord, Loop) turns chaining on and off. A new Pattern value takes over from the
next step.

## Rows

**ROWS BY** decides which held note each row plays.

- **PITCH**: row 1 is the lowest note you hold, row 2 the next up, and so on. Hold a 7th chord and rows
  1 to 4 are its four notes. **EMPTY ROWS** says what rows above the notes you hold do: **REST** stays
  silent, **WRAP** starts again from the lowest note (with four notes held, row 5 plays row 1's note),
  and **OCT** carries on up an octave (with a triad held, rows 4 to 6 are the triad an octave up). OCT
  makes one shape fit any chord size: rows 1, 4 and 7 are always the root in three octaves.
- **DEGREE**: rows count up a scale from the lowest note you hold: row 1 is that note, row 3 the 3rd
  above it, row 5 the 5th, and so on. Pick the scale's key and MAJOR or MINOR beside it. A degree you
  aren't holding plays the held note nearest to it, so JArp never plays a note you didn't hold. Where
  two held notes are equally near, the one in the scale wins, and then the lower. So rows 1 and 5
  alternating play the root and fifth of any chord.

Each row's label shows the note it plays and its MIDI channel. In DEGREE mode a row whose degree
isn't held shows the note it wanted, as `wants F3`. Three hairlines along the bottom of each label follow
that finger live: pressure (blue), slide, CC74 (purple), and bend from the middle (orange).

## The lanes

Under the grid, one cell per step, for the notes that start on that step. Drag a cell up or down, or
scroll on it, to change it; double-click to set it back.

- **OCTAVE**: -3 to +3 octaves. A note shows its octave in its corner.
- **RATCHET**: splits the note into 1 to 8 equal hits across its length. The note on the grid shows
  its hits. Click **P** to make the step's ratchet follow pressure: pressing harder adds hits, up to
  MAX HITS. The note shows the extra hits it could take as blue outlines, filled as the finger presses.
- **VELOCITY**: an offset from -64 to +64, added to the note's velocity.

## Settings

**TIMING**

- **RATE**: how long a step is, from 1/32 to 1/4, with triplets.
- **SWING**: moves every second step later, from 50% (straight) to 75%.
- **LENGTH**: steps before this pattern loops, 1 to 128. Each pattern has its own.

**PLAYBACK**

- **LATCH**: on, the notes keep playing after you let go, until you play a new chord.
- **RESTART**: where the pattern starts.
  - **SONG** locks the pattern to the song position, so it is sequenced in time rather than started
    by your hands: play a chord half way through the pattern and it starts half way through. Each
    step falls on the same beat every time round.
  - **NOTE** starts it on the first note you play.
  - **BAR** starts it at the start of the bar the first note falls in.

With the host stopped, JArp runs on its own clock at the host's tempo, so you can play it without
pressing play.

**PRESSURE**, from each finger:

- **VELOCITY**: how far each note's velocity follows its finger's pressure, rather than how hard the
  key was struck. At 0 it's the strike velocity alone. The Velocity lane's offset is added after.
- **CURVE**: how pressure maps. SOFT reaches high values with a light touch, LINEAR is as played, HARD
  needs a firm press. It shapes both Velocity and Ratchet.
- **RATCHET**: how far pressure adds hits on steps with P lit, from the step's own count up to MAX HITS.
- **MAX HITS**: the most hits a pressed P step splits into, 1 to 8.

The pressure is read when the note starts.

## MPE

Each arp note plays on the channel and key of the finger it comes from (plus the step's octave), so
an MPE synth plays it with that finger's expression:

- Channel pressure, CC74 and pitch bend pass straight through on their channel.
- Poly aftertouch and CLAP note expressions (pressure, brightness, tuning) are sent on to every arp
  note playing from that finger.
- Letting go of a finger ends its notes at once (unless LATCH is on).

The notes you hold don't pass through: only the arp's notes do.

## Dice

Click the die in the preset bar for the dice card.

![Dice](docs/dice.png)

- **RANDOMIZE** builds a new pattern from a plain base (one note per step on row 1, octave 0, one hit,
  velocity offset 0), rolling each ticked item within its range.
- **MUTATE** nudges the pattern you have, each ticked item by up to its range. Click again to keep
  going.
- **RANDOM PRESET**: pick a category (or any), then **PRESET** loads a random factory preset from it,
  settings and all its patterns, or **INTO SLOT** puts one random pattern from those presets into the
  selected slot and leaves everything else as it is.
- **UNDO** and **REDO** step back and forward through what the dice replaced, a random preset
  included.

Untick an item to keep it as it is. The ranges:

| Item | Range | |
|---|---|---|
| PLACEMENT | 25% | The share of steps left empty (RANDOMIZE) or flipped between a note and a rest (MUTATE) |
| NOTES | rows 1-3 | Which rows notes land on |
| LENGTH | ¼ to 2 | Note lengths, in steps |
| OCTAVE | ±1 oct | Octave lane values |
| RATCHET | up to 3 | Ratchet lane counts |
| VELOCITY | ±20 | Velocity lane offsets |
| PRESSURE | 20% | The share of steps with P lit (unticked to start with) |

The dice roll the pattern on the grid, only up to its length. The dice only ever change the pattern, never
the held notes, so JArp still plays only notes you hold.

## Presets

The preset bar steps through the factory bank and then your own. A preset holds the settings and all
eight patterns, and loading one from the bar replaces them all. To load one pattern into one slot
instead, right-click the slot's number and use **Load from preset**:

There are 77 factory presets in twelve categories, each built on how the style is really played.
Most use PITCH rows with OCT, so a triad and a 7th chord both play them in shape. Presets marked
(chain) use several patterns: CHORD moves on with each new chord, LOOP plays them one after another.

The **Grooves** are bass lines. They play the bottom three notes you hold, an octave down, so hold a
chord in the middle of the keyboard and route JArp into a bass patch.

| Category | Presets |
|---|---|
| Basics | Init, Up, Down, Up Down, Converge, Pinky Top, Root Fifth and Broken Thirds (DEGREE rows) |
| Berlin School | Octave Line, Pedal Sequence, Sequencer Bass, Phase Drift (a seven-step accent drifting across the line), Night Drive (four patterns, LOOP), Long Drive (64 steps) |
| Grooves (bass) | Funk Bass, Slap Pops, House Bass, Rolling House, Disco Octaves, Acid Line, Root Fifth Octave, Tresillo (3+3+2), Dub Bass, Synth Pump, Boogie Shuffle, Gallop (triplets), Two Step (chain, CHORD) |
| Piano | Alberti Bass, Waltz Spread (3/4), Nocturne, Stride, Pop Eighths, Rolled Ballad, Piano Verse (three patterns, CHORD) |
| Guitar Fingerpicking | Travis Picking, Classical Arpeggio (p i m a), Banjo Roll, Ballad Picking, Fingerstyle Song (four patterns, LOOP) |
| Strum and Harp | Down Strum, Up Strum, Strum Pattern (down down up, up down up), Harp Glissando, Harp Cascade, Strum to Harp (chain, CHORD) |
| Pads and Pedals | Pedal Climb, Slow Top, Swell Layers, Drone and Drift (64 steps), Pedal Story (four patterns, LOOP) |
| Ratchet and Rhythm | Trap Hats, Stutter, Machine Gun, Pressure Ratchets, Stabs, Stutter Build (four patterns, LOOP) |
| MPE Expressive | Press Roll, Press to Bloom, Breath Pulse, Pressure Waves, Touch Chain (three patterns, CHORD): press harder for louder notes and more hits |
| Long Sequences | Four Bar Groove (64 steps), Slow Evolution (32), Odyssey (128), Long Chain (two 32-step patterns, LOOP) |
| Chains | Chord Chain and Verse Chorus (CHORD), Phrase Loop, Build Up, Call and Response and Eight Bars (LOOP, Eight Bars using all eight slots) |
| Odd Meters | Five Four, Seven Eight, Nine Eight, Three Over Four, Five Over Four, Odd Chain (7/8 then 5/8, LOOP) |

**SAVE** names the current settings and writes them as a small text file, with the patterns in a
`.jarppatterns` file beside it:

- macOS: `~/Library/Application Support/JArp/Presets`
- Windows: `%APPDATA%\JArp\Presets`
- Linux: `~/.config/JArp/Presets`

Click the **star** beside the name to make the current preset a favorite.

## Building

Built with the rest of the repo; see the [top-level README](../../README.md). The engine is plain C++
in `src/engine.h`, tested on its own in `tests/test_engine.cpp`; the dice are in `src/randomize.h`.
