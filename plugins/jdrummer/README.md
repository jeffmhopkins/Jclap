<div align="center">

# JDrummer

A drum pattern generator and sequencer that plays JKit.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![CLAP: note effect](https://img.shields.io/badge/CLAP-note%20effect-blue)](https://github.com/free-audio/clap) [![GUI: Visage](https://img.shields.io/badge/GUI-Visage-blue)](https://github.com/VitalAudio/visage) [![License: MIT](https://img.shields.io/badge/license-MIT-blue)](../../LICENSE)

</div>

A CLAP note effect that writes whole drum parts and plays them as MIDI for [JKit](../jkit)'s 16 pads.
Pick a genre and a style, a drummer (and a percussionist if you like), set how hard and how strange it
goes, and roll a song: intro, verse, pre-chorus, chorus, bridge, break and outro, each with four
variations, a fill and its own energy. Mutate any section until it sits right, edit any hit, then
switch sections live by click, by note or by automation. It plays with the host's transport, or on
its own PLAY while the host is stopped.

Version 0.1.0.

![Panel](docs/panel.png)

Hover over a control for a one-line tip.

## Wiring it to JKit

Put JDrummer on a MIDI track (or before JKit in the same chain) and send its output to JKit. It sends
notes C2 to D#3 (36 to 51), JKit's pads 1 to 16, on **channel 10** by default (CHANNEL in OUTPUT).
Set JDrummer's KIT to the kit JKit has loaded (Drum Kit, Percussion, FX or Animals) so the lanes are
named and coloured as the sounds you hear.

A percussionist plays a second part on its own channel (**11** by default, PERC in OUTPUT): send that
to a second JKit set to PERCUSSION. Notes JDrummer doesn't use as section notes pass straight through
to JKit, so you can still play pads by hand (untick *Pass other notes to JKit* to stop that).

## GENERATE

![Generate](docs/panel.png)

### GENRE

34 genres in eight families, 230 named styles among them (4 to 12 a genre), and every style is also a
factory preset.

| Family | Genres |
|---|---|
| Four on the Floor | House, Techno, Disco, Trance, Minimal |
| Breaks | Breakbeat, Drum & Bass, Jungle, UK Garage, Electro |
| Hip-Hop | Boom Bap, Trap, Lo-fi, Drill |
| Live Kit | Rock, Funk, Soul & R&B, Metal, Jazz |
| Latin & Afro | Afrobeat, Amapiano, Samba, Bossa Nova, Reggaeton |
| Reggae & Dub | Reggae, Dub |
| Hand Percussion | Afro-Cuban, West African, Middle Eastern, Indian (on JKit's Percussion kit) |
| FX & Odd | Glitch (FX kit), Animal Grooves (Animals kit), Odd Meters (7/8, 5/4, 6/8, 9/8), Halftime |

- **The twelve tiles** are common genres, each with its groove in miniature. Click one to use it (its
  first style). The genre playing always has a lit tile: if it isn't one of the twelve, it takes the
  last tile's place.
- **Shift-click** another tile to blend it in: the song takes hits from both. The small purple slider
  beside ALL 34 GENRES sets how much of the second genre comes in. Shift-click it again to drop it.
- **The style menu** lists the genre's styles. A style is a groove, a tempo and its twists (shuffled,
  busy hats, half-time, rolling percussion, toms, a clap stack, ratcheted hats and so on). Picking one
  sets the free tempo and swing to suit it, and the player and percussionist its genre likes.
- **ALL 34 GENRES** opens the browser: every genre by family with its count of styles, and the styles
  of the genre you pick, each with its tag, tempo and groove. Click a style to use it.

![All genres](docs/panel-browser.png)

### HOW IT PLAYS

- **The drummer** button opens PLAYERS. A player is how a pattern is played: how loose the timing is,
  how busy, how many ghost notes, how often and how big the fills, and how far behind the beat the
  backbeat and hats sit. Six drummers: Session Pro, Pocket Funk, Stadium Rock, Brushes Jazz, Break
  Chopper and Machine (on the grid, no humanize).
- **The percussionist** chip adds a second part on its own channel: Conguero, Djembe Circle, Bateria,
  Hand Drummer, Tabla & Darbuka, or Shaker & Tamb. Click it to pick one; × drops it.
- **ENERGY** (chill to peak time) is how hard the whole song goes: which instruments play, how many
  hits, how loud. Each section's own energy rides on it.
- **WEIRD** (by the book to out there) moves hits off the grid, leaves some to chance, rolls them,
  brings in stray sounds and odd lengths.
- **ROLL SONG** makes the song again from a new seed. Locked sections stay as they are.
  **PREVIOUS ROLL** goes back to the song before, up to 16 times.

![Players](docs/panel-players.png)

### THE SONG

The seven sections, each as wide as its bars, with its groove. For each:

- **Click it** to mutate it in the card below.
- **Drag its energy** (the dot and number under its groove) up or down. Energy runs from 100
  down to 0. Under 20 a section keeps only its anchors (kick and backbeat on the beat, no ghosts, no
  stray hits, no crash); under 10 only the lowest sound on the half notes; at 0 it is silent. So a
  section can be as sparse as you want, down to a single kick or a bar of nothing before the drop.
- **The die** rolls just that section again.
- **The lock** keeps it from ROLL SONG.

### MUTATE

The selected section's groove now, and five children made from it, each tagged with what changed
("more ghost snares", "kick moves off the 1", "lays back further", "half-time feel", "toms come in",
"hats left to chance", "perc answers", "flams on the backbeat" and ten more). Click a child to put it
in the song: the section's other variations and fill are made again from it, and its generation goes
up. **< back to gen N** goes back one. **MUTATION** sets how far the children stray; **NEW LITTER**
makes five more.

## EDIT

![Edit](docs/panel-edit.png)

One section's pattern: pick the **SECTION**, then **VAR** 1 to 4, its **FILL**, or **PERC** (the
percussionist's part). **STEPS** sets the pattern's length (1 to 32) and **RATE** its step (1/16,
1/8, 1/16T, 1/32). **DICE** mutates the pattern once, by MUTATION.

Only the pads the pattern plays get a lane, highest pad at the top. **+ ADD A PAD** adds a lane for
another pad. Each lane shows its pad's name and note, with **M** (mute) and **S** (solo) for
listening.

- **Click a cell** to add a hit; click a hit to take it away.
- **Drag up or down on a hit** for its velocity. Accents have a white top; ghost notes are half
  height.
- **Right-click a hit** for accent, normal or ghost, a ratchet (2 to 4 hits in the step) and a chance
  (100, 75, 50 or 25%). A hit left to chance is dashed with its percent.
- **Click a lane's name** to show its **VELOCITY**, **CHANCE** and **NUDGE** (up to 40 ms early or
  late) for each step below the grid. Drag a value up or down; double-click to reset it.

## PLAY

![Play](docs/panel-play.png)

### SECTIONS

A tile for each section with its note, its groove and its energy. The section playing is lit, and the
one queued to come in has a dashed edge and NEXT. Click a tile to switch to it, a number for that
variation, FILL for a fill.

- **SWITCH AT**: when a switch happens: the next beat, the next bar (default), the next 2 or 4 bars,
  or the end of the section.
- **BY**: what switches sections: a note or the Section parameter (default), only a note, or only the
  parameter. The Section and Variation parameters can be automated.
- **NOTES** opens SECTION NOTES (below).

### NOW PLAYING

The pattern playing, lane by lane, with the playhead.

### LIFE

What makes each pass a little different:

- **VARY**: each bar may take another variation, and hits come and go.
- **GHOSTS**: soft snare hits around the beat.
- **SWING**: every second sixteenth late (50% is straight).
- **SAME DICE EVERY** never, 1, 2, 4 (default) or 8 bars: the chance hits and VARY repeat every so
  many bars, so the randomness makes a pattern of its own.
- **AUTO FILLS** off, or a fill every 4, 8 (default) or 16 bars.
- **PLAY** LOOP stays in a section; SONG plays each section for its bars, then the next.

### TRANSPORT

- **PLAY** runs JDrummer on its own.
- **SYNC**: AUTO (default) follows the host while it plays and runs free on PLAY while it's stopped;
  HOST only follows the host; FREE ignores the host.
- **TEMPO WHEN FREE**: 40 to 300 BPM.

The readout shows whether it's following the host or running free, the tempo, and the bar and beat in
the section.

### OUTPUT

**KIT** (the kit JKit has loaded), **CHANNEL** (default 10) and **PERC** (the percussionist's channel,
default 11), and JKit's 16 pads as JKit lays them out (pad 1 bottom left), each lighting as it plays.

## Section notes

![Section notes](docs/panel-notes.png)

Play a note to switch section, much as you'd cue clips. With the **KEYBOARD MAP** the white keys from
C are the seven sections then NEXT, PREV, RESTART and STOP, and the black keys FILL, VAR 1 to 4, MUTE
HATS, MUTE KICK and HOLD. **OCT** sets the octave (C0 by default, well below JKit's pads) and the
channel box which channel they come on (ANY by default).

**CUSTOM** gives each of the 19 jobs its own note: press **LEARN** and play any note. **RESET ALL**
puts them back as the keyboard map has them.

![Learn](docs/panel-learn.png)

- *Fill into the new section*: a switch plays a fill in the bar before it.
- *Velocity sets energy*: how hard you play a section's note sets how hard it plays.
- *Play only while held*: JDrummer plays while a section's note is held, and stops when you let go.
- *Pass other notes to JKit*: notes that aren't section notes go on to JKit.

MUTE HATS, MUTE KICK and HOLD (stay in this section) last as long as you hold the note.

## Presets

Every style is a factory preset, filed by family and genre in the preset menu, so loading one makes
its song from the style's own seed. A saved preset keeps the song itself (in a `.jdrummersong` file
beside the preset), with every mutation and edit. The die in the preset bar rolls the song again.

## Building

Built with the rest of JClap (see the [root README](../../README.md)). `jdrummer-engine-tests` checks
the generator and engine; `jdrummer-tests` drives the plugin through CLAP; `jdrummer-screenshot`
renders these pictures:

    jdrummer-screenshot out.png [generate|edit|play] [width height] [players] [browser] [notes] [custom] [addpad]
