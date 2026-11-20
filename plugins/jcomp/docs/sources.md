# JComp research: comping rhythms, voicings and harmony by genre

Compiled 2026-11-20 as the source material for JComp's style library (plugins/jcomp/src/styles.h), written so the content could be encoded as data. "This session" below means the research pass that made it.

## How to read this document

**Honesty markers.** Every claim is tagged with one of these:

- **[n]**: checked this session against source n in the bibliography. Where I read only a search
  snippet or a secondary page that quotes the primary source, the bibliography entry says so.
- **(unverified)**: from general knowledge of the idiom (recordings, method books I know of) that I
  did **not** confirm against a readable source this session. Treat it as a well-informed default,
  and check it against the named recording before it ships as a "signature" preset.
- **(derived)**: my own reduction of a source. For example, a 16-step grid read off a published
  table, or a voicing rule restated as MIDI numbers.

**Rhythm grid notation** (as specified in the brief):

| char | meaning |
|---|---|
| `X` | accented chord |
| `x` | chord |
| `o` | soft chord |
| `g` | ghost or muted chop (very short, low velocity, damped) |
| `-` | tie or hold from the previous attack |
| `.` | rest |

- 16 chars = one 4/4 bar of sixteenths. Positions are 1-based: pos 1 = beat 1, pos 3 = "and" of 1
  (8th), pos 5 = beat 2, pos 13 = beat 4, pos 16 = the last sixteenth (the "a" of 4).
- 32 chars = two bars (clave-based, two-bar patterns). Bars are separated by `|` for readability; the
  `|` is not a step.
- 12 chars = one 4/4 bar in a 12/8 or triplet-swing grid (4 beats x 3 triplet partials). Pos 1, 4,
  7 and 10 are the beats; pos 3, 6, 9 and 12 are the swung "and"s.
- 12 chars in **Jazz Waltz** = one 3/4 bar of eighth-note triplets (3 beats x 3 partials). It is
  marked where it is used.

**Register.** MIDI note numbers use the convention C4 = 60 (middle C), so C3 = 48 and C5 = 72.

**Lengths.** Note lengths are given as a fraction of a step or in musical terms:

| term | duration |
|---|---|
| staccato chop | 25-40% of a 16th |
| short | about 1/2 of the gap to the next attack |
| legato | held to the next attack, minus about 5 ms |
| held | ties through `-` steps |

---

## Part A. Cross-genre principles

### A1. Low interval limits (LIL): avoid mud in the left hand and lower voices

Arrangers use a **low interval limit** table: the lowest pitch at which each interval still sounds
clearly, rather than as mud. The chart below was read off the published chart image at [3]
(Robin Hoffmann, "Low Interval Limits", 2019). It is the same table taught in Berklee-tradition
arranging texts: Pease & Pullig's *Modern Jazz Voicings* [4] gives the minor and major second
limits in its five-part-cluster chapter, "Check that your voicings don't go below the low interval
limits for major and minor seconds".

The values below are the **lowest acceptable pair** (lower note to upper note). Any voicing whose
interval sits *lower* than this should be revoiced.

| Interval | Lowest pair | MIDI (lower, upper) | Note |
|---|---|---|---|
| minor 2nd | E3 to F3 | 52, 53 | [3] |
| major 2nd | Eb3 to F3 | 51, 53 | [3] |
| minor 3rd | C3 to Eb3 | 48, 51 | [3] |
| major 3rd | Bb2 to D3 | 46, 50 | [3] |
| perfect 4th | Bb2 to Eb3 | 46, 51 | [3] (some charts give A2 to D3; unverified) |
| augmented 4th | Bb2 to E3 | 46, 52 | [3] |
| diminished 5th | Bb2 to Fb3 | 46, 52 | [3] |
| perfect 5th | Bb1 to F2 | 34, 41 | [3] (read from ledger lines) |
| minor 6th | G2 to Eb3 | 43, 51 | [3] (derived reading of image) |
| major 6th | F2 to D3 | 41, 50 | [3] |
| diminished 7th | F#2 to Eb3 | 42, 51 | [3] |
| minor 7th | F2 to Eb3 | 41, 51 | [3] |
| major 7th | F2 to E3 | 41, 52 | [3] |
| minor 9th | E2 to F3 | 40, 53 | [3] |
| major 9th | Eb2 to F3 | 39, 53 | [3] |
| minor 10th | C2 to Eb3 | 36, 51 | [3] |
| major 10th | Bb1 to D3 | 34, 50 | [3] |
| unison, octave | unlimited | | [3] |

**Implementation rule (derived).** For every adjacent pair of voices (lower `l`, upper `u`) with
interval `i = u - l`:

- If `i` is in the table and `l < LIL_low[i]`, the voicing fails. Move the lower voice up an octave,
  or drop the offending note.
- Below about D3 (MIDI 50), allow only octaves, fifths and tenths in the comp part.

The second rule matches the arranger's rule of thumb in [41]: "Below the middle line of the bass
stave, make sure all intervals are consonant and ideally a 10th, 5th or octave". Note that a sharp
or flat letter at the same staff position counts as the same row: F#2 to Eb3 is dim 7th.

Octave-number caveat: some web tables print these an octave lower (for example "E2-F2" for the minor
2nd). That is a C3 = middle C labelling difference, not a different rule [3 vs search snippets of
Sweetwater InSync]. The pitches above are read directly from the staff.

### A2. Voice-leading principles for comping

The developer's voice-leader should implement these principles:

1. **Keep common tones.** If a pitch class appears in both chords, keep it in the same voice and
   octave. This is the core of Levine's left-hand voicing method: *The Jazz Piano Book* builds
   ii-V-I comping from three-note voicings (ch. 3, p. 17) and four-note left-hand voicings (ch. 7,
   p. 41), arranged so that one chord leads to the next with minimal motion [2] (chapter titles and
   pages verified [2]; content summary unverified).
2. **Move the other voices by the smallest interval.** Prefer a step (1 or 2 semitones). In a ii-V-I
   the 7th of each chord falls a half or whole step to the 3rd of the next. The cost function
   (derived) is: minimise the sum over voices of |pitch motion| (in semitones), subject to LIL and
   range limits. This is the "efficient voice leading" criterion formalised in Tymoczko, *A Geometry
   of Music* [45] (unverified this session).
3. **Alternate A and B forms.** For a cycle of fifths, alternate rootless "A" (3-5-7-9) and "B"
   (7-9-3-5) shapes. This keeps the hand in about one octave (Levine ch. 7 "Left-Hand Voicings" [2];
   A/B labels unverified this session).
4. **Limit the top voice.** Keep top-voice motion at or under a 3rd per chord change by default. A
   leap of a 4th or more is allowed only at a section boundary, or after a reset of the comp
   "register centre" (unverified, practice rule). Keep the top voice inside the genre's
   register band (see each genre).
5. **No voice crossing, and avoid parallel octaves and fifths between the outer voices of the comp.**
   This matters less in pop and rock, where parallel triads are idiomatic (planed triads, power
   chords). Make it a per-genre flag (unverified).
6. **Respect the low interval limits** (A1).
7. **Don't double the bass player.** When a bass part exists, the comp may omit the root. In jazz,
   rootless voicings are the norm with a bassist (Levine ch. 7 [2]; a search snippet from a teaching
   source says the same).
8. **Re-centre on section changes.** On the downbeat of a new section (verse to chorus), allow a
   register jump to the section's preferred register. A chorus typically sits higher and wider than
   a verse (unverified, practice rule).

Further reading, not read this session: Huron, *Voice Leading: The Science Behind a Musical Art*
(MIT Press, 2016) [43]. It derives the classical rules from perceptual principles: pitch proximity,
avoiding parallel perfect consonances (they fuse), and register (low-register mud). This supports
A1 and A2 (unverified).

### A3. Naming a chord from held notes (chord recognition)

**Standard approach: template matching over pitch classes** (Pardo & Birmingham, "Algorithms for
Chordal Analysis", *Computer Music Journal* 26(2), 2002 [29]; described in later literature as a
"template matching system" and an extension of Temperley's work [29]).

Recipe (derived from [29] and standard MIR practice):

1. **Reduce the held MIDI notes to a pitch-class set**, a 12-bit vector. Keep the lowest note
   separately as `bass`.
2. **For each of the 12 candidate roots and each chord template**, score the match. Templates are
   interval sets relative to the root:
   - maj {0,4,7}, min {0,3,7}, dim {0,3,6}, aug {0,4,8}, sus4 {0,5,7}, sus2 {0,2,7}
   - 7 {0,4,7,10}, maj7 {0,4,7,11}, m7 {0,3,7,10}, m7b5 {0,3,6,10}, dim7 {0,3,6,9}
   - 6 {0,4,7,9}, m6 {0,3,7,9}, add9 {0,2,4,7}, 9 {0,2,4,7,10}, m9 {0,2,3,7,10},
     maj9 {0,2,4,7,11}, 7#9 {0,3,4,7,10}, 13 {0,4,9,10}
3. **Score each candidate** as `score = (#notes matching template) - (#template notes missing) -
   (#notes not in template)`. This is the Pardo-Birmingham scoring idea [29]; exact weights are
   unverified.
4. **Break ties** by:
   - preferring the root that equals the bass note;
   - then the template with fewer members (simpler chord);
   - then a fixed preference order (maj > min > 7 > ...). Pardo & Birmingham use tie-break rules of
     this kind (unverified detail).
5. **Allow a missing 5th.** Comping voicings routinely omit it, so give the 5th a zero penalty when
   missing. A rootless voicing has no root, so allow root-absent matches only when the user's
   setting says "assume rootless" or a bass note is supplied (derived).
6. **If the bass is not the root, output a slash chord** (for example C/E). If the set is a triad
   over a foreign bass, output an upper-structure or slash name (for example D/C = C maj7(9,#11)
   family) (derived).

Temperley's *The Cognition of Basic Musical Structures* (MIT Press, 2001) [30] gives a
preference-rule model for root finding. Its rules: the root is the pitch class with the most
"compatible" intervals above it, plus a bias toward roots near the previous root on the line of
fifths. That bias is a useful tie-breaker when a set is ambiguous (for example {C,E,G,A} = C6 or
Am7) (unverified this session).

---

## Part B. Genres

Each genre section gives: rhythms (with lengths and sources), voicing practice (shapes, register,
root, extensions), harmonic vocabulary (progressions by section, substitutions), feel and
ornaments, and the player archetype.

### B1. Funk

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| F1 | `x.x.x.x.x.xx...x` | Clav riff, single line or dyads, after "Superstition" (Stevie Wonder, *Talking Book*, 1972) |
| F2 | `X.gxg.x.gXg.x.gx` | Funk "chicken-scratch" keyboard/guitar comp: continuous 16th motor, mostly damped, a few chords allowed to ring (unverified as a specific recording) |
| F3 | `X.....x...x.X...` | "On the one" stab comp (horn-like hits). Sparse, leaves room for an F2 part on another player (unverified) |
| F4 | `..x..x.x..x..x.x` | Offbeat clav counter-part, complementing F1 (layered clav) (unverified) |

- **F1** is derived from Hein's description [5]:
  - The first half of the bar is a "steady stream of eighth notes": root, octave, b7, root.
  - In the second half, the b7 "deliberately avoids beat four, coming a sixteenth note early" (pos
    12), and the resolution lands "the sixteenth note right before bar two" (pos 16).
  - Pitches are Eb minor pentatonic.
  - Bar 2 is the same, except the b7 falls *on* beat 4.
  - The multitrack shows about 8 clav tracks: the main riff plus counter-riffs [6].
  - Length: short (about 50% of a 16th), percussive.
- **F2** applies Stewart's description of the funk rhythm guitar [13]. The player "mutes the strings
  ... yielding a percussive, scraping sound known as 'chicken scratch'. Occasional chords are allowed
  to ring out adding to the dense syncopated texture."
  - Chord length: 30-60% of a 16th. `g` = 15-25% of a 16th, velocity 25-40.

**Voicings**

- Clav and Rhodes use 2-3 note grips in the mid register, about C3-C5 (MIDI 48-72).
- Dominant 7 and 9 in close position. Typical is 3-7-9 or the root-less "9 grip" (3, b7, 9, 5).
- The root is often omitted when bass is present.
- Minor 7/9 vamps (Dorian) are common.
- Extensions: 9, 13, #9 (7#9 "Hendrix" chord) (unverified).
- James Brown's harmony leans on the dominant 7th, "the bluesier sound of dominant seventh chords";
  Fela's leans on m7/Dorian [13].

**Harmony**

| Progression | Section | Source |
|---|---|---|
| one-chord vamp: I7 or i7 (Dorian) | verse | static harmony "an essential part" of Brown's style [13] |
| I7 to IV7 (e.g. E9 to A9) | verse/chorus | unverified |
| modulation up a step or to IV for the bridge | bridge | "take it to the bridge" with a "modulation to a new key during a bridge section" [13] |
| i7 to IV7 (Dorian vamp, e.g. Em7 to A7) | groove | unverified |

- Substitutions: 7#9 on the tonic; chromatic approach chord a half-step above or below on pos 16
  into the next bar (unverified).

**Feel**

- Straight 16ths, little swing (0-55%).
- On top of the beat; the drummer's "one" is emphasised.
- Tempo 95-115 BPM (unverified). "Superstition" is about 100 BPM (unverified).
- Dynamics: high accent contrast between `X` and `g`.
- Ornaments: chromatic slide-in chord a half step below, on about 1 in 4 bars, at the end of a bar
  (unverified).

**Archetype:** the "clav riff architect" (Stevie Wonder on "Superstition", layered clav [5][6]). Also
the "scratch guitarist" who treats chords as drums: Brown, "I was hearing everything, even the
guitars, like they were drums" (quoted in [13]).

### B2. Soul (Southern/Stax and Northern soul keyboard)

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| S1 | `....X.......X...` | Backbeat chords on 2 and 4, short-medium length. The keyboard and guitar "backbeat" role in 60s soul |
| S2 | `x-x-x-x-x-x-x-x-` | Eighth-note pumping triads (piano), lightly accented on 2 and 4 (unverified) |
| S3 | `x--x--x-x--x--x-` | Gospel-derived "3-3-2" organ/piano push (unverified) |
| S4 (12-grid, 12/8 ballad soul) | `x.xx.xx.xx.x` | Triplet piano pulse, with the chord on every triplet partial except the weakest (unverified) |

- **S1** source: the Motown guitarist's backbeat is documented. Earl Van Dyke: "Joe [Messina] never
  blew a backbeat on one session in the 14 years he was at Motown" [23]. Applying this to soul
  keyboards is (unverified).
- **S4** reflects the slow 6/8 or 12/8 soul ballad (for example "When a Man Loves a Woman", 1966,
  Atlantic) (unverified).

**Voicings**

- Triads in close position, right hand C4-C5 (60-72). The left hand plays the root, or a root and
  octave, in C2-C3.
- Organ uses 3-4 note close triads with an added 6 or b7.
- Extensions are sparse: 7, 6, sus4 to 3.

**Harmony**

| Progression | Section |
|---|---|
| I-vi-IV-V | verse |
| I-IV (vamp) | groove |
| ii-V-I | bridge |
| I-I7-IV-iv (minor plagal) | ending |

- All of these are (unverified). Rock-corpus data shows IV is the most common chord before and after
  I in Anglo-American popular music, and IV-I and I-IV are the commonest transitions [1].

**Feel**

- Laid-back, straight or slightly swung 8ths (unverified).
- Tempo 70-120 BPM.
- Gospel grace notes (a minor 3rd sliding to the major 3rd) on about 1 chord in 4.

**Archetype:** the "Memphis house-band organist/pianist" (Booker T. Jones type) (unverified).

### B3. Motown (Detroit, 1960s)

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| M1 | `x...X...x...X...` | Quarter-note piano chords leaning on the backbeat, with Benny Benjamin's quarter-note snare [52] (corrected in the review, Part E) |
| M2 | `....X.......X...` | Guitar/piano backbeat on 2 and 4, the defining Motown backbeat role [23] |
| M3 | `x.x.x.x.x.x.x.x.` | Eighth-note piano pump, left hand on roots ("aggressive ... bass notes" [23]) |
| M4 | `x..x..x.x..x..x.` | Hunter-era bluesy boogie-influenced figure (unverified) |

- **M1** is what you hear on "Ain't Too Proud to Beg" (1966) and "For Once in My Life" (1968), which
  [23] credits to Van Dyke. Exact placement is unverified.
- **M3** reflects the stylistic contrast in [23]: predecessor Joe Hunter played "in a bluesy, almost
  boogie-woogie style", while Van Dyke was "more aggressive, especially with the bass notes on his
  left hand".

**Voicings**

- The piano doubles the bass root in the left hand (octaves C2-C3).
- Right hand: close triads, often in the upper-middle register (C4-G5).
- Bright and percussive. Van Dyke's "gorilla piano" touch contrasts with Johnny Griffith's gentle
  touch [23].
- Instruments: Steinway grand, Hammond B-3, Wurlitzer, Rhodes; Griffith played Wurlitzer on "I Heard
  It Through the Grapevine" [23].
- Multiple keyboards doubling one part is idiomatic: Griffith and Van Dyke traded piano, organ and
  Wurlitzer, and lines were often doubled [23].

**Harmony**

| Progression | Section |
|---|---|
| I-vi-ii-V (doo-wop/50s cycle) | verse |
| I-IV-I-V | verse |
| IV-V-I | chorus |
| truck-driver modulation up a semitone or whole tone for the last chorus | outro chorus |

- All (unverified). Jazz-trained players (Van Dyke's influences per [23] were Tommy Flanagan, Hank
  Jones and Barry Harris) add 6ths and 9ths to the tonic.

**Feel**

- Straight 8ths, driving, on top. Tempo 110-135 BPM.
- Tambourine on 2 and 4 pairs with M2 (unverified).

**Archetype:** the "studio bandleader pianist": hard attack, left-hand octaves, doubling the backbeat
with guitar (Van Dyke type [23]).

### B4. Disco

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| D1 | `..x...x...x...x.` | Offbeat 8th chords (the "and"s), short. Lifts against a four-on-the-floor kick (unverified) |
| D2 | `gxgxgXgxgxgxgXgx` | Continuous 16ths with muted/ringing alternation, after Chic's "chucking" (accent placement unverified) |
| D3 | `x..x..x...x..x..` | 3-3-2 syncopated Rhodes/piano figure over 2 beats, repeated (unverified) |

- **D2** source: Nile Rodgers' "chucking" mixes off-beat strumming with muting; Rodgers says he
  learned it in New York and based his picking on George Van Eps-style three-string inversions [40].

**Voicings**

- Three-note inversions (guitar-style grips) on the top strings, or keys C4-C5.
- m7, m9, maj7, 9sus. Root mostly omitted (the octave bass covers it).
- Extensions: 9, 11 (m11), sus.
- Rodgers' "three string inversions" are documented in [40]; the extension list is (unverified).

**Harmony**

| Progression | Section |
|---|---|
| i7-iv7 (Dorian two-chord vamp, e.g. Am7-D9) | verse/groove |
| ii7-V7sus | chorus |
| IVmaj7-iii7-ii7-I | bridge |

- All (unverified).

**Feel**

- Straight 16ths, tight on top. Tempo 110-130 BPM. Dynamics constant.
- Ornament: a 16th pickup chord into the bar (pos 16) about once every 4 bars (unverified).

**Archetype:** the "disco rhythm guitarist-on-keys": 16th-note motor, inversions on top (Rodgers
type [40]).

### B5. Neo-Soul

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| N1 | `x--.--x---.x--..` | Long, behind-the-beat Rhodes chords with an anticipation into beat 3 (unverified) |
| N2 | `x.....x-.x....x.` | Sparse syncopation, the last 8th anticipating the next bar (unverified) |
| N3 | `x--o--x-o-x--o--` | Comp with soft re-attacks ("breathing" chords), Rhodes/Wurli (unverified) |

- **Timing is the defining trait.** Danielsen's analysis of D'Angelo's "Left and Right" (*Voodoo*,
  2000) shows that different instruments imply different pulse locations at once. The pulse is a
  "beat bin" with temporal extent, not a point [15]. The *Voodoo* feel is linked to J Dilla's
  unquantized micro-displacements [15]. This is a coverage claim from the search snippet.
- Implementation (derived from [15]):
  - Per-instrument timing offsets within a bin of about +10 to +40 ms late for keys.
  - Per-hit jitter of 5-15 ms.
  - Hats and keys must not be aligned.

**Voicings**

- Rootless or quartal-ish clusters, m9, m11, maj9, 13sus, 9sus. Right hand about F3-C5 (53-72).
- Left hand plays a 7th or 10th shell, or nothing (bass covers).
- Seconds inside the voicing must respect the LIL (A1): keep m2 and M2 clusters above Eb3.
- All (unverified).

**Harmony**

| Progression | Section |
|---|---|
| imaj9-IV9 or i9-IV13 | verse vamp |
| IVmaj7-iii7-vi7 / ii7-V7sus | chorus |
| chromatic planing of m9 shapes up or down a semitone | turnarounds |

- All (unverified).

**Feel**

- Swing 55-62%, laid back, 70-95 BPM. Soft dynamics.
- Ornaments: grace-note slides into the top voice on about 1 in 3 chords; rolled chords.

**Archetype:** the "behind-the-beat Rhodes player" (D'Angelo/Soulquarians type [15]).

### B6. 90s R&B

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| R1 | `x--------.x-----` | Whole and half-note pads/stabs over a programmed beat, with an anticipation (unverified) |
| R2 | `x..x..x.....x...` | 3-3-2 synth/e-piano figure (unverified) |
| R3 | `x.x...x.x...x.x.` | New-jack-swing stab rhythm (swung 16ths) (unverified) |

**Voicings**

- maj9, m9, 11, 13sus, add9 in close/cluster position, C4-C5.
- Gospel-derived passing chords.

**Harmony**

| Progression | Section |
|---|---|
| IVmaj7-iii7-vi7 | verse |
| ii7-V7sus-Imaj9 | chorus |
| bVImaj7-bVII-I | pre-chorus |

- All (unverified).

**Feel**

- Swung 16ths (new jack swing, about 60%) or straight 8ths for ballads. Tempo 65-100 BPM (unverified).

**Archetype:** the "producer-keyboardist" (Jimmy Jam & Terry Lewis, Babyface type) (unverified).

**Sources were thin here.** No academic or method source was located this session.

### B7. Gospel

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| G1 (12-grid, 12/8) | `X.xx.xX.xx.x` | Slow 12/8 "shout ballad" chords on triplets (unverified) |
| G2 | `x.xX.xx.x.xX.xx.` | Uptempo offbeat comp (unverified) |
| G4 (12-grid) | `b.xb.Xb.xb.X` | The shout (praise break): left hand on the beats, chords on the swung upbeats, 150-190 BPM [53] (added in the review, Part E) |
| G3 | `x---x-x-----x-x-` | Ballad comp with passing-chord pickups on the 8ths before beats 3 and 1 (unverified) |

- **G2** is a 16th grid: chords on the "and"s and 16ths, with accents on 2 and 4. Tempo 120-140 BPM.

**Voicings**

- The left hand plays octave roots, often moving bass lines.
- The right hand plays triads and 4-note chords with added 9 and 11, C4-C6.
- Upper-structure triads over the bass (for example D/C).

**Harmony and substitutions**

- 2-5-1s everywhere, including the minor 2-5-1 (taught as a staple in [31][32]; search-snippet level).
- Passing chords and inversions [31].
- Minor plagal (IV-iv-I).
- Walk-ups: I-I/3-IV-#IVdim7-I/5 (unverified).
- "Backdoor" bVII9-I (unverified).
- Tritone subs (unverified).

| Progression | Section |
|---|---|
| I-vi-ii-V | verse |
| IV-#IVo7-I/5-VI7-ii-V-I | chorus walk-up |
| ii-V-I vamps (shout) | vamp |
| I-I7/3-IV-iv-I | ending |

- All (unverified).

**Feel**

- 12/8 for ballads, straight 16ths for uptempo, strong swing. Tempo 60-75 BPM (12/8) or 120-140 BPM
  (shout).
- Ornaments: fills on almost every chord change, chromatic runs into the downbeat.

**Archetype:** the "church musician" (unverified).

**Sources.**

- Method sources exist: Kurt Cowling, *Gospel Piano* (Hal Leonard Keyboard Style Series) [31], and
  Mark Harrison, *Gospel Keyboard Styles* (Hal Leonard) [32]. I could confirm only the titles and
  authors, not the content.
- An academic lead: the University of Chicago dissertation "Sermons in Song: Richard Smallwood, the
  Vamp, and the Gospel Imagination" (described in a news piece; author not confirmed) [44].
- **Sources were thin here.**

### B8. Blues (shuffle)

Grid: 12-grid (triplet swing).

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| B1 | `x.xx.xx.xx.x` | Shuffle comp: long-short triplet pulse on every beat (piano right hand chords) (unverified) |
| B2 | `...x.....x..` | Backbeat chords on 2 and 4, short (unverified) |
| B3 | `x..x.xx..x.x` | Chicago piano "lift" with a pushed "and" of 2 and 4 (unverified) |

**Voicings**

- Dominant 7th triads with the 6th; the right hand plays 3-b7 or 3-6-b7, about G3-C5.
- The left hand plays a boogie/shuffle bass, or nothing if bass is present.

**Harmony** (12-bar blues)

| Form | Progression |
|---|---|
| standard 12-bar | I7 I7 I7 I7 / IV7 IV7 I7 I7 / V7 IV7 I7 V7 |
| quick-change variant | I7 IV7 I7 I7 / IV7 IV7 I7 I7 / V7 IV7 I7 V7 |

- Turnaround: I-I7/3-IV-#ivo7-I/5-V7.
- Grace notes: b3 to 3 crushed (acciaccatura), on about every second chord.
- All (unverified).

**Feel**

- Swing ratio about 2:1 (triplet). Tempo 80-130 BPM. Laid back.

**Archetype:** the "Chicago blues pianist" (Otis Spann type) (unverified).

### B9. Swing Jazz (piano comping)

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| J1 | `x.....x.........` | Charleston: beat 1 + the "and" of 2 |
| J2 | `..x.......x.....` | Reverse Charleston: "and" of 1 + beat 3 |
| J3 | `......x.......x.` | Red Garland offbeat: chord on the swung "and" of 2 and "and" of 4 (straight-8 grid) |
| J4 | `x...x...x...x...` | Freddie Green four-to-the-bar (guitar, or piano imitating it) |

- **J1 and J2** come from the jazz-library comping guide (search snippet), which also cites Red
  Garland as a light-touch Charleston user [21]. Lengths: beat 1 is a quarter; the "and" of 2 is
  short or tied over.
- **J3**: [21] places Garland's chord at "the swung 8th note behind beats 2 and 4" and says he
  "considers himself to be an 8th note ahead of the next beat". In other words, a regular
  anticipation of the next chord on the "and" of 4, which ties into the next bar.
  - On a 12-grid it is `.....x.....x` (pos 6 = "and" of 2, pos 12 = "and" of 4).
  - Length: short, about 40%.
  - Recording: "C-Jam Blues" [21].
- **J4** reflects Butterman's 2009 thesis on Freddie Green [19] (Count Basie Orchestra):
  - Green's clarity and pulse came from mostly **one- and two-note voicings in the middle register**,
    not three-note grips.
  - Downstrokes, four to the bar.
  - Berens: "rhythm guitar is about time, not about voicings" [20].
  - Length: quarter, slightly detached; beats 2 and 4 a little stronger.

**Voicings**

- Rootless left-hand voicings A (3-5-7-9) and B (7-9-3-5). Shells (1-3-7 or 1-7-3).
- Three-note voicings (Levine ch. 3 p. 17; ch. 5 "Adding Notes to Three-Note Voicings" p. 27; ch. 7
  "Left-Hand Voicings" p. 41) [2].
- Altered dominants (ch. 8 p. 49), So What chords (ch. 12 p. 97), fourth chords (ch. 13 p. 105) and
  upper structures (ch. 14 p. 109) [2].
- Range: Levine and the Berklee tradition keep left-hand voicings between about D3 and G4/C5
  (unverified exact limits). A common rule is "top note under about C5, bottom note over about
  C3/D3", consistent with LIL A1.

**Harmony and substitutions**

| Progression | Section |
|---|---|
| ii-V-I | A sections |
| I-vi-ii-V turnaround ("rhythm changes" A) | A sections |
| iii-VI7-ii-V | bridge/turnaround |
| III7-VI7-II7-V7 (rhythm-changes bridge) | bridge |

- Tritone substitution (Levine ch. 6 "Tritone Substitution", p. 37 [2]); passing diminished.
- All (unverified) except the chapter references.

**Feel**

- Swing ratio about 60-66%, relaxed. Tempo 120-240 BPM. Comp sparse.
- Ornament: chromatic approach chord (a half step above, on the "and" before) on about 1 change in 4.

**Archetypes:**

- The "anticipating trio pianist" (Garland type [21]).
- The "four-to-the-bar rhythm guitarist" (Green type [19][20]).

### B10. Bossa Nova

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| BN1 | `x...x...b.x.....` | Batida: one 2/4 bar read as one 4/4 bar in eighths (corrected in the review, Part E) |
| BN2 | `x.....x.....x...|....x.....x.....` | "Bossa clave" 3-3-4-3-3 in eighths over two bars (corrected in the review, Part E) |
| BN3 | `x.....x.b...x...|b...x...b.x.....` | BN2 with the thumb on 1 and 3 (the review's replacement for an unverified two-bar grid, Part E) |

- **BN1** is written with fingers and thumb as separate parts.
  - Combined attacks use the 16-grid per 2/4 bar written twice: the thumb plays on each beat (pos 1,
    5, 9, 13), and the fingers play pos 1, 3 and 6 (and 9, 11, 14).
  - This follows the Wikipedia description, which cites the Brazilian literature (Garcia 1999 et al.)
    [11]: "the thumb plays the bass notes on 1 and 2, while the fingers pluck the chords in unison on
    the two eighth notes of beat one, followed by the second sixteenth note of beat two". The
    pattern is written in 2/4.
  - Thumb "stylized a surdo", fingers "phrased like a tamborim" [11].
  - Encode as two layers: bass layer `x...x...x...x...` (thumb, legato) and chord layer
    `x.x..x..x.x..x..` (fingers, short-medium).
- **BN2**: the pattern is idiomatic, but its attribution to a specific João Gilberto recording is
  (unverified).
- **BN3**: [11] says "Two-measure patterns usually contain a syncopation into the second measure".
  The exact grid is (unverified).
- Lengths: finger chords short-medium (50-70%). Thumb bass legato.
- Walter Garcia's book (*Bim Bom: a contradição sem conflitos de João Gilberto*, Paz e Terra, 1999)
  [12] analyses the batida as regular bass versus irregular chords. **I did not read the
  transcription.**

**Voicings**

- Guitar 4-note grips without doubling, middle register about G3-D5 (55-74).
- Bass on the thumb, the root or 5th in E2-B2.
- Extensions: maj7, 6/9 (for example C6/9 [11]), m7, 9, 13, b9, #11, dim, half-diminished.
- On piano, the left hand plays the bass and the right hand plays 4-note rootless grips (unverified).

**Harmony**

| Progression | Section |
|---|---|
| Imaj7-II7(#11) or ii-V-Imaj7 | A sections (e.g. "Garota de Ipanema" uses Fmaj7-G7-Gm7-Gb7; unverified) |
| ii-V chains | bridge |
| chromatic descending ii-Vs, tritone subs (bII7) | bridge |

- [11] notes "unconventional chords ... complex progressions and 'ambiguous' harmonies" and that
  similar harmonic structures were used by samba guitarists from the 1920s.

**Feel**

- No swing: "typically played without swing, contrasting with jazz" [11]. Soft, intimate.
- Tempo 120-145 BPM (in 4/4, i.e. 60-72 in 2/2) (unverified).
- Very few ornaments.

**Archetype:** the "voice-and-guitar batida player" (João Gilberto type [11][12]).

### B11. Jazz Waltz

Grid: 9 chars per 3/4 bar (3 beats x 3 triplet partials; pos 1, 4 and 7 are beats 1-3, pos 6 and 9
are the swung "and"s of 2 and 3).

**Rhythms (9-char grid = one 3/4 bar)**

| ID | Pattern | Description |
|---|---|---|
| W1 | `x..x.....` | Beats 1 and 2 |
| W2 | `...x..x..` | Beats 2 and 3, the typical "oom-pah-pah" omitting 1 because the bass takes it |
| W3 | `x....x...` | Beat 1 + swung "and" of 2: a 2-against-3 hemiola feel over 2 bars |

- W1, W2 and W3 are all (unverified).

**Voicings**

- Rootless A/B left-hand voicings, as in swing (Levine ch. 7 [2]). Also quartal (ch. 13 [2]).

**Harmony**

- ii-V-I (e.g. "Someday My Prince Will Come": I-III7-IV-VI7-ii-V; unverified).
- "Waltz for Debby" uses an A-B-A-C form with many ii-V-Is and modulations (lesson-site snippet).

**Feel**

- Swing ratio about 60% on 8ths. Tempo 140-200 BPM (dotted half 47-67).
- Hemiola (3 quarters over 2 bars of 3/4) once every 4-8 bars.

**Archetype:** the "impressionist trio pianist" (Bill Evans type) (unverified).

**Sources were thin here** (no transcription read).

### B12. Lo-fi hip-hop keys

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| L1 | `x---------x-----` | Two chords per bar, held, the second slightly late (unverified) |
| L2 | `x-----.x--------` | One chord per bar with a pushed re-attack (unverified) |
| L3 | `x..x..x...x.....` | Sampled-loop chop rhythm (3-3-4) (unverified) |

**Voicings**

- Rhodes/felt-piano maj7, m9, maj9, 13, m11, about C3-C5.
- Close rootless, often with an added 9 on top.

**Harmony**

| Progression | Section |
|---|---|
| IVmaj7-iii7-vi7 / ii7-V7-Imaj7 | loop |
| imaj7-iv9 | verse |
| i-iv-bVII-bIIImaj7 | hook |

- All (unverified).

**Feel**

- Dilla-style late, unquantized timing, as in Neo-Soul [15]. Swing 55-60%.
- Tempo 70-90 BPM. Soft (velocity 40-70). Detune/wow is a sound-design matter.

**Archetype:** the "bedroom producer-pianist" (Nujabes/J Dilla lineage) (unverified).

**Sources.** Academic coverage is cultural, not musical: Winston & Saywood, "Beats to Relax/Study
To", *IASPM Journal* 9(2), 2019, pp. 40-54 [17], on nostalgia and paradox. **There are no
comping-level sources: thin.**

### B13. Pop

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| P1 | `x-x-x-x-x-x-x-x-` | Driving 8th-note piano (verse build) (unverified) |
| P2 | `x--x--x-x--x--x-` | 3-3-2 dotted-8th piano/synth figure (unverified) |
| P3 | `x---------------` | Whole-note pad (intro/breakdown) |
| P4 | `x.....x.....x...` | Dotted-quarter push (pre-chorus) (unverified) |

**Voicings**

- Triads and add9/sus2/sus4 in close position, C4-C5, with the root in the left hand C2-C3.
- Inversions are chosen for minimal motion (A2).

**Harmony (corpus-grounded)**

de Clercq & Temperley's corpus [1] (Rolling Stone 500, 5 decades x 20 songs, 99 songs analysed)
gives data that can weight the progression generator directly.

- **Root frequencies** (proportion of all harmonies, Table 2) [1]:

  | Root | Proportion |
  |---|---|
  | I | .328 |
  | IV | .226 |
  | V | .163 |
  | bVII | .081 |
  | VI | .072 |
  | bVI | .040 |
  | II | .036 |
  | bIII | .026 |
  | III | .019 |
  | bII | .005 |
  | VII | .004 |
  | #IV | .003 |

  I, IV, V, bVII and VI account for 87% of all harmonies.
- **Most frequent transitions** (Table 3, counts) [1]:

  | Transition | Count |
  |---|---|
  | IV to I | 1162 |
  | I to IV | 1052 |
  | V to I | 788 |
  | I to V | 710 |
  | IV to V | 514 |
  | I to bVII | 470 |
  | V to IV | 392 |
  | bVII to I | 386 |
  | I to VI | 302 |
  | VI to IV | 260 |
  | bVI to I | 208 |
  | bVII to IV | 188 |
  | V to VI | 191 |
  | I to II | 132 |
  | VI to V | 124 |
  | bVII to bVI | 114 |
  | I to bVI | 104 |

- **Trigrams ending on I** (Table 7) [1]:

  | Trigram | Count |
  |---|---|
  | IV-V-I | 352 |
  | V-IV-I | 292 |
  | bVII-IV-I | 146 |
  | VI-IV-I | 126 |
  | bVII-bVI-I | 103 |
  | bIII-bVI-I | 66 |
  | II-V-I | 63 |
  | bVI-bVII-I | 60 |
  | V-VI-I | 42 |
  | IV-bVII-I | 39 |

- **Root motion** is symmetric (ascending P4 2266 vs descending P4 2220) [1]. Use symmetric weights,
  not common-practice ones.
- By decade, the 1950s are >95% I-IV-V [1]. A "Retro 50s" mode should restrict to I-IV-V (plus
  I-vi-IV-V).

Section suggestions (unverified, but consistent with [1]):

| Section | Progression |
|---|---|
| verse | I-V-vi-IV or vi-IV-I-V |
| pre-chorus | IV-V or ii-IV-V |
| chorus | I-V-vi-IV / IV-I-V-vi |
| bridge | vi-IV-I-V or bVI-bVII-I |

- The "I-V-vi-IV" loop's popularity is widely documented; its exact frequency is not in [1].

**Feel**

- Straight 8ths/16ths, tempo 90-130 BPM. Dynamics build from verse to chorus.
- Fill: walk-up chord on the 8th before a section downbeat.

**Archetype:** the "singer-songwriter piano" (unverified).

### B14. Rock

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| RK1 | `x-x-x-x-x-x-x-x-` | Straight 8th "pumping" chords (piano/organ), the keyboard analogue of 8th-note rhythm guitar (unverified) |
| RK2 | `X---X---X---X---` | Quarter-note pounding chords (unverified) |
| RK3 | `x..x..x.x..x..x.` | Syncopated 3-3-2 rock riff (unverified) |

**Voicings**

- Triads, power chords (root-5-octave), and the organ playing 5ths plus octaves. Mid register.
- Root-5 dyads are fine down to Bb1 per LIL (A1); 3rds stay above Bb2.

**Harmony**

| Progression | Section |
|---|---|
| I-bVII-IV-I | verse, Mixolydian |
| I-IV-V | verse |
| bVI-bVII-I | chorus lift |
| i-bVI-bIII-bVII | minor rock |

- bVII is far more common in rock than in common practice [1]; bVII-IV-I is the third most common
  trigram ending on I [1].

**Feel**

- Straight, on top. Tempo 100-160 BPM. Loud.

**Archetype:** the "rock pianist/organist" (Nicky Hopkins, Jon Lord types) (unverified).

### B15. Piano Ballad

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| PB1 | `x.x.x.x.x.x.x.x.` | Broken-chord 8ths (arpeggiated: root-5-octave-10 in the left hand, with chord on top) (unverified) |
| PB2 | `x-------x-------` | Half-note chords (unverified) |
| PB3 (12-grid) | `x.xx.xx.xx.x` | 12/8 ballad triplets (unverified) |

- For **PB1**, implement the arpeggio by voice order, not as chord hits.

**Voicings**

- The left hand plays an open root-5-10 spread, in C2-C4 (avoid 3rds below Bb2; see A1).
- The right hand plays triads with add9/sus, C4-C6.

**Harmony**

| Progression | Section |
|---|---|
| I-V/7-vi-IV (descending bass line) | verse |
| I-iii-IV-V | verse |
| IV-V-iii-vi-ii-V-I | chorus |
| bVI-bVII-I | climax |
| truck-driver modulation | last chorus |

- All (unverified). The VI-IV-I and IV-V-I trigrams are frequent [1].

**Feel**

- Straight or 12/8. Tempo 60-80 BPM. Rubato at intro and outro.
- Dynamics: p to f over the song.
- Ornaments: grace notes and a rolled final chord.

**Archetype:** the "power-ballad pianist" (Elton John / Billy Joel type) (unverified).

### B16. Country

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| C1 | `x...x...x...x...` | "Boom-chick": left-hand root on 1 and 3 alternating with the 5th, right-hand chord on 2 and 4 (two layers) (unverified) |
| C2 | `x.xx..x.x.xx..x.` | Train-beat/two-step fill pattern (unverified) |
| C3 | `x---x-x-x---x-x-` | Nashville-sound ballad with slip-note fills |

- **C1** as two layers: left hand `x.......x.......`, right hand `....x.......x...`.
- **C3** (slip-note fills, unverified grid): the slip note is "a whole-tone slur" (Cramer, quoted by
  the Country Music Hall of Fame [18]). Cramer took it from a Don Robertson demo, and it was
  showcased on "Last Date" (1960) [18]. It is played louder than the target, unlike a grace note
  (Pianist Magazine, via search snippet).
  - Implement: on about 1 in 3 melody/top-voice changes, insert a 30-60 ms note a whole step (2
    semitones) below the target top or inner voice, then slide to the target.

**Voicings**

- Triads (major), 6ths; diatonic, the root on the left hand.
- Right hand C4-C6, with 3rds and 6ths doubled in the melody voice.

**Harmony**

| Progression | Section |
|---|---|
| I-IV-I-V | verse |
| I-I7-IV-iv-I | chorus/ending |
| I-V7/V-V7-I (secondary dominant, "II7") | chorus |

- All (unverified).

**Feel**

- Straight or light shuffle (about 55%). Tempo 80-140 BPM.

**Archetype:** the "Nashville session pianist" (Floyd Cramer type [18]). Method: Mark Harrison,
*Country Piano* (Hal Leonard Keyboard Style Series) [33] (title/author verified only).

### B17. House (90s piano house)

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| H1 | `x..x..x...x..x..` | 3-3-2 + 3-3-2 syncopated piano riff ("Ride On Time" style) (unverified grid) |
| H2 | `..X...X...X...X.` | Offbeat stabs (unverified) |
| H3 | `x..x..x.x..x..x.|x..x..x...x..x..` | Two-bar M1-piano riff with the last stab anticipating bar 2 (unverified) |

- **H1**: the M1 piano is tied to Black Box "Ride On Time" (1989) [36] and Robin S "Show Me Love"
  (1993) [37]. The grid is (unverified).
- Lengths: staccato stabs (40-60% of a 16th), with a pedal "hold" on accents.

**Voicings**

- Root-position triads and 7ths in octaves, with a strong top-note melody. Bright, C4-C6.
- Root doubled; often parallel (planed) chord shapes.
- The Korg M1 (1988) piano preset is the canonical sound [36][37].

**Harmony**

| Progression | Section |
|---|---|
| i-bVII-bVI-bVII | chorus riff |
| I-iii-IV / IV-V-vi | lift |
| two-chord vamps (i-iv) | breakdowns |

- All (unverified).

**Feel**

- Straight 16ths with slight swing (52-56%). Tempo 120-128 BPM. On top.

**Archetype:** the "Italo/piano-house riff programmer" (unverified).

### B18. Deep House (minor 7th/9th stabs)

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| DH1 | `..x...x...x..x..` | Offbeat chord stabs with a 16th push (unverified) |
| DH2 | `x-------x-------` | Long held organ/pad chords |
| DH3 | `...x..x...x..x..` | Syncopated stab (16th-shifted), as in Kerri Chandler / Detroit-Chicago lineage (unverified) |

- **DH2**: per a Wikipedia-mirror description, Larry Heard's "Can You Feel It" (Mr. Fingers, 1986)
  uses "an organ played like strings, with long chords held rather than stabbed" (search snippet).
  So the seminal deep house track is *not* a stab pattern.
- Lengths: stabs 50-100% of a 16th, release tails from the synth envelope.

**Voicings**

- m7, m9, m11, maj9, 9sus, add9. Close clusters, "elegantly clustered in a nod to his jazz training"
  [24], about F3-C5.
- Root optional.
- Hooktheory's user transcription of "Can You Feel It" gives Am(add9)-F7-G(add9) (i(add9)-VI7-
  VII(add9)) at about 116 BPM [25]. This is secondary, user-submitted.
- Gear: Juno-60 and TR-909 [24].

**Harmony**

| Progression | Section |
|---|---|
| i9-iv9 (Dorian/Aeolian) | vamp |
| i(add9)-bVI7-bVII(add9) | [25] |
| m9 planed up or down a whole tone | verse |

- The planing is (unverified).

**Feel**

- Swing 52-58%. Tempo 118-124 BPM. Soft-medium.

**Archetype:** the "jazz-trained Chicago deep-house keyboardist" (Larry Heard type [24]).

### B19. Reggae (skank and organ bubble)

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| RG1 | `....x.......x...` | Skank: short chord on beats 2 and 4 (in half-time feel: the offbeat) |
| RG2 | `..x...x...x...x.` | Skank in a "double-time" count: chord on every 8th offbeat |
| RG3 | `..b.x.b...b.x.b.` | Organ bubble: left-hand low notes on the upbeats, right-hand chop on 2 and 4 [51] (corrected in the review, Part E) |
| RG4 | `....x..x....x..x` | Skank with a 16th "double chop" (unverified) |

- **RG1 and RG2**: the right-hand chop doubles the guitar on 2 and 4 (Berklee keyboard guide,
  search-snippet level [35]). The one-drop kick/snare lands on 3, leaving beat 1 open (several
  teaching sources).
  - Length: staccato, 25-35%.
  - Pick RG1 or RG2 depending on whether the user counts the tempo at 70 or 140.
- **RG3**, the organ bubble, as two layers:
  - Left hand (low): `.x.x....x.x.....`. Low notes about an octave below the chop, a 16th either
    side of it.
  - Right hand (chop): `....x.......x...`.
  - The description of the bubble as 16ths around the offbeat chop, with the framing notes about an
    octave below, comes from a Stretta Music article (search snippet only; the page returned 403).
  - The bubble ("shuffle") is attributed to late-60s Studio One, with Jackie Mittoo central [42].
  - Exact grid (unverified).

**Voicings**

- Skank: close triads in inversion, upper register C4-G5. Guitar skank on the top 3 strings.
- Bubble: low single notes or 5ths, C3-C4.
- Extensions: triads mostly; m7 for minor tunes.

**Harmony**

| Progression | Section |
|---|---|
| I-IV / i-iv two-chord vamps | verse |
| I-V-vi-IV | chorus |
| i-bVII | Aeolian |

- All (unverified).

**Feel**

- Straight or shuffled 8ths; the bubble decides it [35]. Tempo 65-80 BPM. Laid back.

**Archetype:** the "Studio One organist" (Jackie Mittoo type [42]).

**Sources were thin** on exact transcriptions.

### B20. Ska

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| SK1 | `..x...x...x...x.` | Offbeat 8th chop on every "and" |
| SK2 | `..xx..xx..xx..xx` | Offbeat chop doubled a sixteenth later (derived; the old grid hit beats 2-4 too, Part E) |
| SK3 (12-grid shuffle) | `..x..x..x..x` | Offbeat chop on the swung "and"s (unverified) |

- **SK1**: Britannica says the R&B boogie-woogie piano vamp "was simulated by a guitar chop on the
  offbeat and onomatopoeically became known as ska" [22]. Piano and horns also played the offbeat
  (secondary snippet). Length: short, 30-40%.

**Voicings**

- Close triads/7ths, upper middle C4-C5. Guitar on the top strings. The root is not needed.

**Harmony**

| Progression | Section |
|---|---|
| I-IV-V | verse |
| I-vi-IV-V | verse |
| ii-V-I | ska-jazz |

- All (unverified).

**Feel**

- Fast, 110-150 BPM (often counted in half time). On top. Early ska is shuffled.

**Archetype:** "Kingston session guitarist/pianist" (Ernest Ranglin, ska chop originator per
Britannica, "generally regarded as plausible" [22]).

### B21. Salsa (piano montuno/guajeo)

Grid: 32 chars, two bars. The clave direction must be a parameter.

**Clave reference** (son clave, standard):

- 3-2: `x.....x.....x...|....x...x.......`. Bar 1 = beat 1, "and" of 2 (pos 7), beat 4 (pos 13);
  bar 2 = beats 2 and 3 (pos 5, 9). Standard (unverified this session, but universal).
- 2-3: swap the bars.

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| SL1 | `x...x.x...x...x.|..x...x...x...x.` | 2-3 onbeat/offbeat montuno, in eighths (corrected in the review from [7]'s MIDI, Part E) |
| SL2 | `..x...x...x...x.|x...x.x...x...x.` | 3-2 offbeat/onbeat, in eighths (corrected in the review from [7]'s MIDI, Part E) |
| SL3 | `x.xx.x.x.xx..x.x|...` | Chachachá "ponchando" (left for the developer to transcribe) |

- **SL1 (2-3).** Per Peñalosa's "offbeat/onbeat motif" (via Moore [7][9][10]), in 2-3 "the figure
  begins on the downbeat". The two-side (bar 1) mixes onbeats, and the three-side (bar 2) is mostly
  offbeats.
  - Struck-pulse grid (derived from the motif description; unverified as a specific recording).
  - "Most salsa music is in two-three clave, and most salsa piano guajeos are based on the two-three
    onbeat/offbeat motif" [7].
- **SL2 (3-2).** "The three-side is expressed entirely with offbeats, and the figure begins with an
  offbeat pickup before beat 1" [7]. The grid is (derived).
- Lengths: legato-ish 8ths with octave doubling (right hand octaves, the 3rd or 5th filled in).
  Anticipations tie across beat 1 of the next chord.
- **SL3 (chachachá "ponchando").** Right-hand ponchando lineage: "Tanga" (Bauzá 1943; Machito 1949,
  René Hernández), Cachao "Rareza de Melitón" (1942), Tito Puente "Oye Como Va" (1963) [7] (Moore
  2009 pp. 16-17; Peñalosa 2010 p. 256). Transcribe from "Oye Como Va" (1963). The exact grid is
  (unverified).
- The older "clave motif" (decorated three-side, cinquillo) is Peñalosa 2010 p. 136 [7][9].
- Mauleón (1993, p. 117) notes the pianist's role is more rhythmic than harmonic. For NY mozambique,
  p. 214 says no set piano part exists, though a 2-3 guajeo is given [7][8].

**Voicings**

- Right hand: octaves with the chord tone in between (1-3-8 or 1-5-8 / 3-5-3), C4-C6.
- Left hand: octave doubling of the right-hand line, or anticipated bass. The root is played.
- Timba variants: the left hand adds a note between right-hand notes (Melón Lewis, 1995 [7]).

**Harmony**

| Progression | Section |
|---|---|
| I-IV-V-IV (son montuno) | montuno section |
| ii-V-I-(IV) | montuno |
| i-iv-V7 | minor tunes |

- Changes typically land on the anticipated "and" of 4 (unverified).
- Montunos are 2-bar cycles aligned with clave [7].

**Feel**

- Straight 8ths, very on top. Tempo 160-210 BPM (counted as 2). Strong, even dynamics.

**Archetypes:**

- The "salsa montuno pianist" (Eddie Palmieri / Papo Lucca type) (unverified).
- The "timba tumbao pianist" (Melón Lewis type [7]).

Method sources: Levine *The Jazz Piano Book* ch. 20 "Salsa and Latin Jazz", p. 207 [2]; Mauleón
*Salsa Guidebook* [8]; Kevin Moore *Beyond Salsa Piano* [10].

### B22. Afrobeat (Fela-era guitar and keys interlock)

**Rhythms** (from Stewart's Example 6, "Alagbon Close" (Fela, 1974) [13])

| ID | Part | Pattern |
|---|---|---|
| AF1 | tenor guitar | `x.x.xx..xx.x.xx.` |
| AF2 | rhythm guitar | `.x.xx.x.x.x.x.x.` |
| AF3 | bass (reference) | `x.....x..x.x..x.` |

- **Caveat on AF1-AF3.** These grids are my reading (derived) of the column positions in the text
  layout of Stewart's Example 6 ("Alagbon Close") [13]. The table is printed on 1 + 2 + 3 + 4 +
  with 16th columns in between. Verify against the article's printed example before encoding as
  canonical.
- **AF1, tenor guitar.** Single-note ostinato, short (staccato 40%), mid register. The tenor guitar
  is an electric guitar playing "single-note melodic/rhythmic ostinatos ... that interlock with the
  rhythm guitar" [13].
- **AF2, rhythm guitar.** Chordal; less fully muted than James Brown's ("less often scratched")
  [13]. Length 40-60%.
- **AF3, bass.** Included as the reference against which the parts lock.

**Keys**

- Fela composed at the keyboard and transposed lines to tenor guitar [13], quoting Tony Allen via
  Ewens 1991.
- A keys part can therefore take an AF1-like single-line ostinato, or an AF2-like chord chop
  (derived).
- Fela also played organ (unverified detail of the part).

**Voicings**

- m7 / Dorian chord grips (m7, m9, m11), 3-note, about G3-D5.
- Single-note ostinato for the "tenor" voice, G3-G4.

**Harmony**

- A static one-chord or two-chord vamp: Fela "preferred the cool minor-seventh or Dorian sound of
  modal jazz", and "tonal shifts were rare" [13].
- Sections are defined by arrangement (groove, then horns, then chant, then call-and-response), not
  by harmony [13].
- Progression: i7 (vamp), optionally i7-IV7 (Dorian).

**Feel**

- Straight 16ths, tight. Tempo 100-130 BPM (unverified).
- Parts play "without variation" while the drums vary (Veal quote via snippet [14]).
- Very long forms.

**Archetype:** the "interlocking tenor guitarist" and the "Dorian chord chopper" (Africa 70 rhythm
section type [13][14]).

### B23. Highlife (guitar)

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| HL1 | `x.xxx.x.x.xxx.x.` | Two-finger palm-wine interlock: thumb on 8ths (bass), finger on 16ths between (two layers, see below) |
| HL2 | (12-grid) | Bell-pattern-aligned guitar figure: highlife borrows the 12/8 bell (Peñalosa 2010 p. 247, via [7]) |
| HL3 | `x..x..x.x..x..x.` | Dance-band highlife chord comp, 3-3-2 (unverified) |

- **HL1** source: a Koo Nimo article (*African Music*) describes "one finger keep[ing] a steady
  lower part while another articulates an upper line, often on sixteenth notes between the lower
  articulations". Koo Nimo's early pattern "caterpillar walking" is a form of dagomba [27] (search
  snippet).
  - Thumb layer: `x.x.x.x.x.x.x.x.`.
  - Finger layer: `..x..x.x..x..x.x` (combined grid unverified).
- **HL2 grid (12-grid, unverified):** `x.x.xx.x.x.x`.

**Named patterns**

- "Mainline", "dagomba" and "fireman" palm-wine patterns, some attributed to foreign seamen [28][46].
- "Yaa Amponsah" (Sam / Kwame Asare, recorded 1928 for Zonophone) is a "compositional form, or
  'Rhythm'" with countless variations (Braddock, 2020 dissertation, Tufts) [26].

**Voicings**

- Open-position guitar triads (major), the thumb on the root/5th alternating. Bright, G3-E5.

**Harmony**

- I-V7 emphasis: dagomba-type patterns "emphasize two chords, the tonic major (I), and the dominant
  seventh (V7)" [27] (snippet).
- Also I-IV-V cycles (unverified).

| Progression | Section |
|---|---|
| I-V7 | verse |
| I-IV-I-V7 | chorus |

**Feel**

- Lilting, straight-ish 16ths with a 12/8 bell undercurrent. Tempo 100-130 BPM (unverified).

**Archetype:** the "palm-wine fingerstyle guitarist" (Kwaa Mensah / Koo Nimo type [26][27][28]).

**Sources were moderate.** Braddock [26] is the best primary-adjacent source; I did not read its
transcriptions.

### B24. Latin Jazz / Cha-cha (my choice for the 24th genre)

I recommend **Latin Jazz / Cha-cha** as the 24th genre. It reuses B21's montuno infrastructure and
is documented by Levine ch. 20 [2] and Moore [7][10]. The chachachá "ponchando" lineage ("Oye Como
Va", Puente 1963) is documented with page numbers in [7].

The alternative is **Organ Trio / Soul Jazz**, but I found no sources for it this session.

**Rhythms**

| ID | Pattern | Description |
|---|---|---|
| LJ1 | `x...x...x...x...` | Chachachá piano: quarter notes with a ponchando right hand (unverified grid) |
| LJ2 | (= SL1/SL2) | Two-bar guajeo aligned to clave |

**Voicings**

- Octave-doubled right-hand lines plus left-hand shells; rootless jazz voicings for Latin jazz [2].

**Harmony**

- i-IV7 vamp ("Oye Como Va" is Am7-D9; unverified). ii-V-I with tritone subs.

**Feel**

- Straight 8ths. Chachachá about 110-125 BPM (unverified).

---

### B25. Ambient (added after the first release, at Jeff's request)

**Rhythms** (derived)

| ID | Pattern | Description |
|---|---|---|
| AM1 | `x---------------\|----------------` | one chord held two bars |
| AM2 | `x---------------` | one chord a bar, held |
| AM3 | `o-------x-------` | slow half-note swell, the second stronger |
| AM4 | `x---------o-----` | re-struck late in the bar, like a loop coming round again |

- Eno's ambient music must be "as ignorable as it is interesting"; unlike conventional background
  music it keeps "doubt and uncertainty" [47]. Hence few attacks and long holds.
- *Music for Airports* "1/2" layers piano tape loops (Robert Wyatt) with "2/1"'s voice loops; the loops are of unequal length and drift
  in and out of alignment, so the music keeps changing without a set structure [48][49]. The late
  re-strike (AM4) and the two-bar hold (AM1) imitate that unevenness on a fixed grid (derived).
- Harold Budd's "soft pedal" playing on *Ambient 2: The Plateaux of Mirror* (1980): slow and
  sustained [50].

**Voicings**

- Open and wide: quartal stacks and spread voicings over about two octaves, rolled slightly
  (derived from [48]: the chord in "1/2" is an arpeggiated F, C, G).
- Thirds left out or blurred: sus and add9 chords. "1/2"'s chord is read as F(add9) with no third,
  "ambiguous", fitting "equally well into Ab major and F minor" [49].

**Harmony** (derived; unverified as any specific recording's changes)

- Major set: Csus, Cadd9, Fmaj7, Am9, Dm9, Gsus, G7sus, with a borrowed Abmaj7 in the bridge.
- Minor set, sitting between C minor and its relative Eb major as [49] describes: Cm9, Abmaj7,
  Ebmaj7, Bbsus, Fm9.

**Feel:** no swing, 64-76 BPM, no runs, chords held to the next.

**Archetype:** the "ambient drifter": long chords, barely moving.

## Part C. Section-by-section guidance (all genres)

These defaults are (unverified, practice-derived) unless noted.

| Section | Density | Register | Rhythm choice | Harmony |
|---|---|---|---|---|
| Intro | low (pads, P3/DH2, or a quarter of the groove) | mid | genre pattern with half the attacks | tonic vamp or the chorus progression |
| Verse | medium | low-mid | main pattern | verse progression |
| Pre-chorus | rising | rising | add pushes (pos 15/16 anticipations) | IV-V, ii-IV-V, bVI-bVII |
| Chorus | high | mid-high, wider voicings | full pattern + accents | chorus progression (I-V-vi-IV etc. [1]) |
| Bridge | contrast (different pattern) | shift | sparser or different | bridge progression; modulation in funk/Motown [13] |
| Break | very low/none, or one stab on 1 | | `X...............` | |
| Outro | groove, then fade, or a held final chord | | | plagal (IV-I) or minor plagal (iv-I) ending; IV-I is the commonest pre-tonic motion [1] |

---

## Part D. Gaps and caveats

- **Strong sources** (read directly this session):
  - Rock/pop harmony statistics [1].
  - Low interval limits [3].
  - Afrobeat interlock (Stewart's table [13]).
  - Salsa guajeo theory (Wikipedia digest of Peñalosa, Moore and Mauleón with page numbers [7]).
  - Levine's chapter structure [2].
  - Superstition riff description [5].
  - Freddie Green [19].
- **Thin sources:**
  - 90s R&B, Lo-fi, Gospel, Jazz Waltz, Disco keys, House piano, Country, Motown (exact keyboard
    rhythms), Reggae (exact bubble grid), Highlife (transcriptions).
  - Most of their rhythm grids are marked (unverified) and should be checked by ear against the
    named recordings before they are presented as "signature" patterns.
- **Not read** (behind paywalls or unavailable this session): Mauleón *Salsa Guidebook*; Garcia *Bim
  Bom*; Veal *Fela*; Braddock 2020 dissertation; Danielsen 2010 chapter; Hal Leonard method books;
  Pease & Pullig full LIL table (only the m2/M2 mention was located in the OCR).

## Part E. The independent review (2026-11-20)

A second pass checked every genre, style and player against sources read directly. The findings
doc ranks them; these are the corrections to this document and to `styles.h`.

- **Salsa and Latin Jazz guajeos (SL1, SL2) were in the wrong subdivision.** JComp's clave grid puts
  the clave on 1, the and of 2, 4 | 2, 3, so a two-bar clave cycle is two 4/4 bars of eighths. The
  old grids used sixteenth positions. Wikipedia's Guajeo article ships Moore's 3-2 offbeat/onbeat
  guajeo as MIDI [7]: read note by note it is three-side `.x.x.x.x`, two-side `x.xx.x.x` (eighths),
  the two-side's and of 4 the pickup. 2-3 swaps the bars ("the attack-point sequence of the two
  measures is reversed" [7]). The clave-motif example (3-2: cinquillo `x.xx.xx.`, then `x.x.x...`)
  was read the same way. A 2-3 clave-strokes rhythm was added for the 2-3 styles.
- **Bossa (BN1-BN3) ran twice as fast.** [11] writes the batida in 2/4 in sixteenths. Brazilian 2/4
  is read as 4/4 in eighths at the tempos quoted in BPM (120-145), so each 2/4 bar is a 4/4 bar here,
  each sixteenth two steps. JDrummer's review made the same correction.
- **Reverse Charleston.** The table above was right (the and of 1, beat 3); `styles.h` had the and of
  3. Confirmed by [21] and Piano With Jonny [54].
- **Freddie Green.** Four To The Bar now voices low shells (root, 3rd, 7th, E2 to E4) rather than the
  Rhythm Guitar's mid-register grips: strings 6-4-3, often one note sounding [55].
- **Organ bubble (RG3).** Jenson puts the left hand's single low notes on the upbeats and the right
  hand's chop on 2 and 4 [51]. The sixteenth-either-side grid from the Stretta snippet is dropped.
- **Gospel shout (G4).** Shout music is "very fast" with "hand claps on the upbeat of each beat";
  genre guides give 150-190 BPM, swung [53]. The Shout style moves to 160 BPM on the triplet grid.
- **Motown (M1).** The quarter-note piano now leans on 2 and 4, with the quarter-note snare [52].
- **Ambient (B25).** "1/2" is piano loops (Wyatt) with the voice loops of "2/1" [48]; the earlier
  text called it eight piano loops.
- **Players.** Jazz Comper voices close (Levine's A/B rootless voicings sit inside an octave [2]).
  Skank Guitar strums both ways, so offbeat chops are upstrokes. Bossa is played by Jazz Comper and
  Highlife by Strummer: the Rhythm Guitar's scratch ghosts don't belong in either.
- **Still unconfirmed** after this pass: 90s R&B (many slow jams are 12/8; not settled), House's M1
  riffs, Country's slip note (it needs a whole-tone grace note the engine doesn't play yet), the
  chachachá ponchando grid, and the palm-wine interlock.


## Part F. The second review (2026-11-20)

A second cold pass, made without reading Part E first, generated every style (all seven sections,
all four takes) and played it through the engine, then checked what came out against sources.

- **Eighth-note idioms got sixteenths.** A player busier than 0.7 ghosted the sixteenth after a hit,
  and the "ghosts", "shift", "busier" and "answer" mutations, WEIRD's strays and DENSITY's fill all
  used sixteenths, whatever the style. So the Montuno player put sixteenth chops back into the
  guajeos Part E had just moved to eighths [7], and the Rhythm Guitar put them into Freddie Green's
  four to the bar [55]. A rhythm with no sixteenth in it now only gains eighths.
- **Crossed clave.** Latin Jazz Guajeo picked its 2-3 or its 3-2 guajeo afresh for each section (both
  at the same energy), so the montuno changed clave direction at random section boundaries. Playing
  against the clave's direction is *cruzado* [56]; a direction change needs an odd-bar phrase, and
  JComp's sections are all even. The style is now 2-3, as montunos usually are [56]. The "answer"
  mutation, which copied a take's first bar over its second, now leaves two-bar rhythms alone.
- **Montunos in the verse.** The clave-strokes rhythms sat at energy 0.35 and the guajeos at 0.8, so
  three verses in four of Montuno 2-3 played clave strokes, not a montuno. They are now 0.2 and 0.6.
  Pocket Stabs likewise played the offbeat clav counter-part three sections in four; it now plays its
  stabs on the one.
- **SPARSE broke two-hit rhythms.** It dropped the and of 2 from the Charleston (leaving whole notes
  on the one) and points of the bossa 3-3-4-3-3 cycle. It now only thins bars of four hits or more.
- **Jazz Comper wasn't rootless.** Close voicing took the root before the ninth, and the swing
  progressions were mostly sevenths and triads, so the comper played C-E-G-C and D-F-A-C. Levine's A
  and B forms are 3-5-7-9 and 7-9-3-5, with the 13th for a dominant's fifth [2][57]. A new Rootless
  shape voices them, and the swing I chord is written C6.
- **Montuno octaves.** The Montuno player ("figures in octaves") voiced close four-note chords.
  Montunos are commonly played in octaves with chord tones between [58]: a new Octaves shape.
- **Fourths didn't change.** The quartal shape accepted any stack of chord tones and tensions, so in
  a diatonic progression one stack (A-D-G-C-E) fit every chord and voice leading kept it: Ambient's
  Cadd9, Fmaj9, Am9 and Fmaj7 all sounded the same. A stack of fourths must now hold the root.
- **Two hands doubled the bass on every chop.** SPREAD put the left hand's low note in every chord,
  so a backbeat, an organ bubble's chop, boom-chick's "chick" and the shout's upbeat chords all had a
  bass note under them, on top of the take's own left hand. On keys the low note now sounds on beats
  1 and 3 and on a change of chord.
- **The left hand.** A bass note was a sixteenth long and the next chord cut it. The left hand now
  holds to its next note and the hands don't cut each other. A new part, the fifth, gives boom-chick
  its alternating bass [59] and the bossa thumb root then fifth on the 2/4 beats [60]; the bossa styles
  voice the guitar's thumb under a three-note chord, so the thumb on 1 sounds (Jazz Comper's close
  voicing had left it out). The ballad's "broken chords" (written as root, fifth, octave, tenth but
  played as bass, top note, chord, top note) are now root-fifth eighths under a held chord.
- **Smaller things.** Strum both read the triplet grid as sixteenths (Shuffle Ska's offbeat chops
  went up, down, down, up); an offbeat style at very low energy was turned into a chord on the one;
  pushes on the triplet grid sat on the middle triplet; DENSITY could re-strike under a held chord;
  chords were spelled with sharps in flat contexts (G#, A#, D# for Ab, Bb, Eb in C minor); and passing
  runs used the key's scale over chromatic chords (an A natural into Abmaj7 in the C Dorian sets, C
  major runs over the Db of the last chorus's key change).
- **Seen and left.** The Afrobeat tenor line repeats the chord's top note rather than a melodic riff;
  palm-wine highlife is strummed by Strummer rather than picked; Rodgers' "chucking" grid (accents only
  on the e and a) is unconfirmed. Comparing with Part E afterwards: the two passes overlap only on
  Freddie Green and the montunos, where this pass found the engine undoing Part E's grid fixes.

---

## Bibliography

1. de Clercq, Trevor, and David Temperley. "A corpus analysis of rock harmony." *Popular Music* 30/1
   (Cambridge University Press, 2011): 47-70. Read in full (Tables 2, 3, 4, 5, 7, 8) at
   https://rockcorpus.midside.com/2011_paper/declercq_temperley_2011.pdf ; project site
   https://rockcorpus.midside.com/
2. Levine, Mark. *The Jazz Piano Book.* Petaluma: Sher Music, 1989. Table of contents read from the
   publisher sample https://www.ejazzlines.com/wp-files/2/shjpb.pdf (ch. 3 Three-Note Voicings p. 17;
   ch. 6 Tritone Substitution p. 37; ch. 7 Left-Hand Voicings p. 41; ch. 8 Altering Notes in
   Left-Hand Voicings p. 49; ch. 12 So What Chords p. 97; ch. 13 Fourth Chords p. 105; ch. 14 Upper
   Structures p. 109; ch. 19 Block Chords p. 179; ch. 20 Salsa and Latin Jazz p. 207; ch. 21 'Comping
   p. 223). Chapter contents were not read.
3. Hoffmann, Robin. "Low Interval Limits." *Daily Film Scoring Bits*, Dec. 2019. Chart image read:
   https://www.robin-hoffmann.com/dfsb/low-interval-limits (image
   https://i0.wp.com/www.robin-hoffmann.com/wp-content/uploads/2019/12/Low_Interval_Limits.jpg). No
   source cited on the page.
4. Pease, Ted, and Ken Pullig. *Modern Jazz Voicings: Arranging for Small and Medium Ensembles.*
   Boston: Berklee Press, 2001. "Low Interval Limits" note in the five-part cluster section (OCR
   text, p. ~94). Read via
   https://opencourses.ionio.gr/modules/document/file.php/DMS171/Pease-Pullig---2001---Modern-Jazz-Voicings-bea38_ocr.pdf
5. Hein, Ethan. "Musical Simples: Superstition." 2015.
   https://www.ethanhein.com/wp/2015/musical-simples-superstition/ (on Stevie Wonder, "Superstition",
   *Talking Book*, Tamla, 1972).
6. Synthtopia. "Stevie Wonder's Clavinet: Superstition, the multitrack masters" (Funkscribe analysis),
   2010.
   https://www.synthtopia.com/content/2010/09/28/stevie-wonders-clavinet-superstition-the-multitrack-masters/
   (search snippet only).
7. Wikipedia. "Guajeo." https://en.wikipedia.org/wiki/Guajeo (read). It cites Moore, Kevin, *Beyond
   Salsa Piano* (2009-2011; pp. 15-17, 32-33, 40-41); Peñalosa 2010 (pp. 136, 247, 256); Mauleón 1993
   (pp. 117, 155, 214).
8. Mauleón, Rebeca. *Salsa Guidebook for Piano and Ensemble.* Petaluma: Sher Music, 1993.
   https://www.shermusic.com/0961470194.php (not read; cited via [7]).
9. Peñalosa, David. *The Clave Matrix: Afro-Cuban Rhythm, Its Principles and African Origins.*
   Redway, CA: Bembe Books, 2009/2010 (not read; cited via [7]).
10. Moore, Kevin. *Beyond Salsa Piano* (series). Santa Cruz: Timba.com/Moore, 2009-2011. Sample
    https://www.latinpulsemusic.com/files/album_pages/beyondsalsapiano/UnderstandingClave_BSP_KevinMoore_Sample.pdf
    (not read; cited via [7]).
11. Wikipedia. "Bossa nova." https://en.wikipedia.org/wiki/Bossa_nova (read). It cites Garcia 1999
    p. 21, Lopes & Simas 2015 pp. 46-48, Castro 2000, McCann 2007, et al.
12. Garcia, Walter. *Bim Bom: a contradição sem conflitos de João Gilberto.* São Paulo: Paz e Terra,
    1999 (not read; described in search results).
13. Stewart, Alexander. "Make It Funky: Fela Kuti, James Brown and the Invention of Afrobeat."
    *American Studies* 52:4 (2013): 99-118. Read in full (Examples 1-6):
    https://journals.ku.edu/amsj/article/download/4463/4304
14. Veal, Michael E. *Fela: The Life and Times of an African Musical Icon.* Philadelphia: Temple
    University Press, 2000 (not read). Veal's quotes on tenor guitar and "African counterpoint" are
    via search snippets.
15. Danielsen, Anne, ed. *Musical Rhythm in the Age of Digital Reproduction.* Farnham: Ashgate, 2010;
    esp. Danielsen, "Here, There and Everywhere: Three accounts of pulse in D'Angelo's 'Left and
    Right'". Also Danielsen et al., "Moving to the Beat" (2015),
    https://www.duo.uio.no/bitstream/10852/44108/2/Danielsen%2bet%2bal%2b2015_Moving%2bto%2bthe%2bBeat.pdf
    (search snippets; chapters not read).
16. (reserved)
17. Winston, Emma, and Laurence Saywood. "Beats to Relax/Study To: Contradiction and Paradox in Lofi
    Hip Hop." *IASPM Journal* 9(2) (2019): 40-54.
    https://iaspmjournal.net/index.php/IASPM_Journal/article/view/949
18. Country Music Hall of Fame and Museum. "Floyd Cramer" (artist biography).
    https://countrymusichalloffame.org/artist/floyd-cramer/ (via search snippet). Also Wikipedia
    "Floyd Cramer".
19. Butterman, Matt. Master's thesis on Freddie Green's rhythm guitar style. William Paterson
    University, 2009.
    https://pettersenwww.freddiegreen.org/technique/butterman_thesis.pdf (search snippet: findings on
    one- and two-note voicings).
20. Berens, Tim. "Basics of Freddie Green Comping." freddiegreen.org.
    https://www.freddiegreen.org/technique/berens.html (search snippet).
21. Jazz Library. "Red Garland" (comping article), Dec. 7, 2020.
    https://jazz-library.com/articles/red-garland/ (read; cites no sources). Also the Jazz Library
    comping guide https://jazz-library.com/articles/comping/ (Charleston definitions, search snippet).
22. Encyclopaedia Britannica. "Ska." https://www.britannica.com/art/ska (search snippet).
23. Wikipedia. "The Funk Brothers" https://en.wikipedia.org/wiki/The_Funk_Brothers ; "Johnny Griffith
    (musician)"; "Earl Van Dyke" (search snippets: Hunter vs Van Dyke style; Messina backbeat quote;
    instrument list).
24. Roland Corporation. "Sound Behind the Song: 'Can You Feel It' by Mr. Fingers."
    https://articles.roland.com/?p=17260 (read).
25. Hooktheory TheoryTab. "Mr. Fingers - Can You Feel It."
    https://www.hooktheory.com/theorytab/view/mr-fingers/can-you-feel-it (read; user-submitted
    transcription).
26. Braddock, Nathaniel. "Yaa Amponsah and the Rhythm at the Heart of West African Guitar." PhD
    dissertation, Tufts University, 2020. https://dl.tufts.edu/concern/pdfs/m613nc160 (search snippet).
27. "Koo Nimo: a contemporary Ghanaian musician." *African Music* (journal of the International
    Library of African Music, Rhodes University).
    https://journal.ru.ac.za/index.php/africanmusic/article/view/2003/1078 (search snippet; author and
    year not confirmed).
28. Music in Africa. "A Short History of the Palmwine Guitar." https://musicinafrica.net/node/13568 ;
    and "Highlife in West Africa" https://www.musicinafrica.net/node/15054 (search snippets).
29. Pardo, Bryan, and William P. Birmingham. "Algorithms for Chordal Analysis." *Computer Music
    Journal* 26(2) (MIT Press, 2002): 27-49. https://muse.jhu.edu/article/7922 (bibliographic details
    and the "template matching" characterisation via search; full text not read).
30. Temperley, David. *The Cognition of Basic Musical Structures.* Cambridge, MA: MIT Press, 2001
    (not read this session).
31. Cowling, Kurt. *Gospel Piano.* Hal Leonard Keyboard Style Series. Milwaukee: Hal Leonard.
    https://www.halleonard.com/product/311327/gospel-piano (product page; content not read).
32. Harrison, Mark. *Gospel Keyboard Styles.* Milwaukee: Hal Leonard.
    https://www.halleonard.com/product/290537/gospel-keyboard-styles (product page).
33. *Country Piano: The Complete Guide.* Hal Leonard Keyboard Style Series (author listed by a
    retailer as Mark Harrison).
    https://www.halleonard.com/product/311052/country-piano-the-complete-guide-with-online-audio
34. *Salsa Piano: The Complete Guide.* Hal Leonard Keyboard Style Series.
    https://www.halleonard.com/product/311049/salsa-piano-the-complete-guide-with-online-audio
35. Berklee College of Music. Keyboard reggae guide (bubble/chop). https://www.berklee.edu/node/2299691
    (search snippet).
36. MusicRadar. "4 '90s house clichés that we still can't get enough of."
    https://www.musicradar.com/how-to/4-90s-house-cliches-that-we-still-cant-get-enough-of (search
    snippet).
37. RouteNote. "The Korg M1: a legendary synth that defined an era."
    https://create.routenote.com/blog/the-korg-m1-a-legendary-synth-that-defined-an-era/ (search
    snippet; secondary).
38. (see 1)
39. (reserved)
40. Rodgers, Nile, interviews: The Quietus, "'Less Is More In My World': Nile Rodgers Speaks"
    https://thequietus.com ; Fender, "Why Nile Rodgers almost rage quit"
    https://fender.com/articles/misc/why-nile-rogers-almost-rage-quit (search snippets: "chucking",
    Van Eps three-string inversions).
41. Rogers, Evan. "Voicings (Part 2)." Big band arranging blog.
    https://evanrogersmusic.com/blog-contents/big-band-arranging/voicings-part2 (read).
42. Dubmatix. "The History of the Organ Bubble in Reggae." *Bass Culture* (Substack), May 12, 2025.
    https://bassculture.substack.com/p/the-history-of-the-organ-bubble-in (intro read; paywalled).
43. Huron, David. *Voice Leading: The Science Behind a Musical Art.* Cambridge, MA: MIT Press, 2016
    (not read this session).
44. University of Chicago Arts & Humanities news. On the dissertation "Sermons in Song: Richard
    Smallwood, the Vamp, and the Gospel Imagination." https://ahd.uchicago.edu/news/tuning (search
    snippet).
45. Tymoczko, Dmitri. *A Geometry of Music: Harmony and Counterpoint in the Extended Common
    Practice.* New York: Oxford University Press, 2011 (not read this session).
46. Scribd copy of an academic article on early West African highlife (palm-wine patterns
    "mainline", "dagomba", "fireman"). https://www.scribd.com/document/465261286/931273 (search
    snippet; author not identified).
47. Eno, Brian. Liner notes, *Ambient 1: Music for Airports* (Polydor/EG, 1978), dated September
    1978; reprinted as "Ambient Music Defined", Synthtopia, 2008.
    https://www.synthtopia.com/content/2008/10/28/ambient-music-defined/ (search snippet: "as
    ignorable as it is interesting").
48. Reverb Machine. "How Brian Eno Created Ambient 1: Music for Airports."
    https://reverbmachine.com/blog/deconstructing-brian-eno-music-for-airports/ ; and Open Culture,
    "Deconstructing Brian Eno's Music for Airports" (2019)
    https://www.openculture.com/2019/07/deconstructing-brian-enos-music-for-airports.html (search
    snippets: eight piano loops of different lengths; they disagree on whether one is an arpeggio or a
    glissando chord).
49. MusicRadar. "A music professor breaks down Brian Eno's Ambient 1: Music for Airports."
    https://www.musicradar.com/artists/the-album-was-played-in-laguardia-airport-for-a-brief-period-in-1980-but-travellers-said-it-induced-unease-and-sounded-like-funeral-music-a-music-professor-breaks-down-brian-enos-ambient-1-music-for-airports
    (search snippet: F(add9) with no third, fitting Ab major or F minor; uncorroborated elsewhere).
50. Wikipedia. "Harold Budd" and "Ambient 2: The Plateaux of Mirror."
    https://en.wikipedia.org/wiki/Harold_Budd (search snippet: the "soft pedal" style, slow and
    sustained).
51. Jenson, Matt. "The Woodshed: Bubblin'." *Berklee Today*, Spring 2015.
    https://www.berklee.edu/berklee-today/spring-2015/The-Woodshed-Bubblin (read: the bubble's
    upbeats in the left hand, the chop on 2 and 4; the notated examples are images).
52. Soundbrenner. "Motown groove" (rhythm library). https://www.soundbrenner.com/blogs/articles/motown-groove
    (search snippet: Benjamin's quarter notes on the snare).
53. Wikipedia. "Shout (Black gospel music)." https://en.wikipedia.org/wiki/Shout_(Black_gospel_music)
    (search snippet: "very fast tempo, chromatic basslines and piano/organ chords ... hand claps on the
    upbeat"); PianoGroove, "Holy Shout Music Tutorial"; genre guides giving 150-190 BPM.
54. Piano With Jonny. "Jazz piano comping guide." https://pianowithjonny.com/piano-lessons/jazz-piano-comping-guide-beginner-to-pro/
    (search snippet: the reverse Charleston on the and of 1 and beat 3).
55. freddiegreen.org: Pettersen, "Notable quotes supporting the one note chord"
    https://www.freddiegreen.org/technique/mp_quotes.html ; Berens [20] (search snippets: voicings on
    strings 6-4-3, often only one note clearly sounding).
56. Piano With Jonny, "Piano montunos: the complete guide" (2-3 montunos; *cruzado*),
    https://pianowithjonny.com/piano-lessons/piano-montunos-the-complete-guide/ ; Soundbrenner, "Clave"
    (direction doesn't change within a tune), https://www.soundbrenner.com/blogs/articles/clave
57. Rootless voicing handout after Levine, *The Jazz Theory Book* (A: 3-5-7-9, B: 7-9-3-5, the 13th for
    the 5th on a dominant), https://www.cs.hmc.edu/courses/2002/fall/mus84/rootless.pdf
58. MusicRadar, "Afro-Cuban piano tips: how to master the montuno" (octaves in the right hand),
    https://www.musicradar.com/how-to/afro-cuban-piano-tips-how-to-master-the-montuno ; Keyboard,
    "Afro-Cuban Piano: Master the Montuno", https://www.keyboardmag.com/lessons/afro-cuban-piano-master-the-montuno
59. Wikipedia, "Alternate bass," https://en.wikipedia.org/wiki/Alternate_bass
60. jazzguitar.be, "Blue Bossa chords and rhythm pattern" (the thumb on root and fifth, beats 1 and 3),
    https://www.jazzguitar.be/blog/blue-bossa-chords/
