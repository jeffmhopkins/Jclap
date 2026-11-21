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
| D3 | `x..x..x...x..x..` | 3-3-4-3-3 syncopated Rhodes/piano figure over 2 beats, repeated (unverified) |

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
| H1 | `x..x..x...x..x..` | 3-3-4-3-3 syncopated piano riff ("Ride On Time" style) (unverified grid) |
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

## Part G. The third review (2026-11-20)

A third cold pass, by five reviewers who didn't read Parts E and F first, generated every style and
dumped what the engine actually played (a new tool, `jcomp-dump`, prints each section's chords, takes
and MIDI bar by bar), then probed the plugin through CLAP. It found 107 problems; these were fixed.

- **Chords that never sounded.** A chord that changed between two hits (the V of a ii-V in a
  Charleston bar, the second chord of any split bar under a held rhythm) was never played, and the
  chord before rang over the change (a boom-chick's last "chick" sounding B7 over the E). Nothing now
  rings over a change; a hit an eighth before a change with nothing after it anticipates it, as the
  and of 2 does in swing comping [2][54]; and a change with no hit at all is struck where it comes.
- **Clave turned round.** Two-bar takes counted their bars from where the section started, so a
  section starting on an odd bar (SWITCH AT Next Bar, a transport start, a host loop) played 3-2 for
  2-3, and the one-bar fill always landed on the three-side. Takes now keep their phase to the host's
  bars, and a figure played as written fills with itself [56].
- **Hands cutting each other.** A note-off from one hand ended the same key struck since by the
  other, so the left hand's step-behind note and the ballad's bass on 3 lasted a few milliseconds.
  Every note's on and off are now paired.
- **DENSITY against the idiom.** Above the middle it filled montunos, the batida and Freddie Green's
  four [55] with straight eighths, and put chords on the downbeats of offbeat styles (one drop, ska,
  offbeat house). Fixed figures now get nothing added, and offbeat styles nothing on the beat.
- **Riffs that didn't move.** The Superstition clav riff was one repeated note (Hein [5] has a
  moving sixteenth line, now written as degrees of the chord, on the Dorian set); the Afrobeat tenor
  line [13][14] and the palm-wine finger and thumb [26][27] likewise. A new LINE part plays one degree
  of the chord per hit. The montuno's octave line now moves through the chord [58].
- **Guitars that weren't guitars.** The Strummer voiced four piano notes with a hole in the middle,
  and Rock had no power chord. A new GRIP shape voices the guitar's bass string, fifth and octave
  with the chord above, and Rock's Quarter Pound and Minor Rock play power chords.
- **Floyd Cramer's slip.** Nashville Slip played scalar sixteenth runs. It now plays the slip note: a
  grace note a whole step under an inner chord tone, slid into it [18].
- **Styles that contradicted their names.** The 12/8 ballad was a shuffle; the Two-Step a rock
  syncopation; Gospel's Sunday Ballad played the shout pattern at 66 BPM, and the Shout used the
  ballad's ii-V walk-up rather than a I-vi-IV-I vamp [53]; Slow Blues never played on the one;
  Motown's "Gorilla Piano" named no sourced practice (now Straight Eighths) and its Boogie Figure
  wasn't swung; Deep House's i9-iv9 is Aeolian, not Dorian [24][25]; reggae's double chop sat on 2 and
  its "a" [35][51]. Disco's IV is a 13th, after Rodgers [40].
- **Harmony.** WEIRD's tritone sub replaced blues and funk tonic I7s and kept slash basses; it now
  only replaces a dominant that resolves down a fifth. Secondary dominants into minor chords get a b9;
  iii chords no longer get a 9th outside the key; a sus becomes 13sus only when it resolves.
- **Voicing.** Low-interval mud and low half-step rubs were only costs, so a voicing could still
  choose them; they are now limits [3][4]. Rootless 7b9 uses the b13 [57]. A slash chord keeps its bass
  at the bottom. On an organ or a pad the spread voicing's low note stays.
- **Runs.** A half-step landing could leap a fourth before its target; runs ran in sixteenths in
  eighth-note styles, could swallow the V chord, and could be flagged on a bass note. Runs now move by
  step, on the take's grid, never over another chord, and only into a chord.
- **The plugin.** A host loop could drop its first downbeat; keyboard chords played on time were
  taken a beat late; LATCH lost quick taps and turned a chord let go key by key into its last key;
  changing CHANNEL left notes stuck; loading a preset reset the routing; the audio thread allocated;
  SWITCH AT Next Beat moved the bar line for good; the pre-roll played; the host setting a recipe
  param didn't remake the song. All fixed, each with a test.
- **Seen and left.** Disco's chucking grid (judgement), the broken-chord left hand's shape, smaller
  naming points, and the reach of the progressions in some genres.

## Part H. The fourth review (2026-11-20)

A fourth cold pass by five reviewers (funk, soul, church and blues; jazz and Latin; pop, dance, world
and ambient; the generator and voicing; the plugin and real time) reproduced every finding with
`jcomp-dump` or a probe program. It found 78 problems (3 critical); these were fixed.

- **Changes that came from the wrong place.** In SONG mode a push or a run at a section's end played
  the section's own first chord rather than the next section's; a run into a chord played on the keys
  landed on the progression's chord and kept its voicing. Both now go to the chord coming. A run no
  longer replaces the only strike of a chord, and one cut short doesn't switch runs off.
- **DENSITY.** Below the middle it did nothing until about 0.25, then stripped every chord from a bar
  that began with a bass note, a change's only strike and a line style's line. It now thins across
  its whole travel and keeps the bar's first chord, the chord of a change and the line; VARY doesn't
  drop that first chord either. Above the middle it no longer puts chords on the one of a skank.
- **Energy.** A section's energy below 0.15 played its loudest chords as a dry stab; it now plays the
  take softly and lets it ring. Dragging a section's energy no longer re-rolls its chords.
- **Held-chord styles.** Pads, ballads and strings got chops, stab fills and ghosts from the
  mutations, WEIRD and the genre's guitar fill; they now hold, and their fill swells into the next bar.
- **Idiom.** The shuffle's left-hand fifth sat on the middle triplet; Chicken Scratch and Clav Riff
  rarely played their figures; a montuno chorus could play the sparse clave figure, and BLEND wrote
  another genre's steps into fixed figures; the Shout restruck the bass on every upbeat; the organ
  bubble's left hand sounded on 2 and 4; swing fills didn't anticipate; reggae's and country's swing
  did nothing. All fixed, with the montuno's octave line moving as a guajeo's does [58].
- **Harmony and voicing.** WEIRD tritone-subbed the tonic; its approach chords ignored PLAIN; a V triad
  was enriched to 6/9; minor and sharp keys were spelled in the wrong key (D#m for Ebm); held notes
  were misnamed; runs used the key's scale over chromatic chords and leapt over the leading tone. 4THS
  gave a diatonic progression one stack and dropped sevenths: stacks may now hold one tritone or third
  (Bb E A D over C7) or sit on a low root. Funk's 7#9 had its #9 below the 3rd; SPREAD added a fifth to
  diminished and #11 chords; GRIP dropped the #9, b9 and #11, and its strum restruck the bass string
  of a boom-chick or palm-wine thumb (the chick is "all the notes in the chord except the bass note").
- **The plugin.** The first chord played on the keys skipped its beat, At Once played the old chord
  on a beat pressed just before it, a host loop played different chords on its first pass, PLAY ONLY
  WHILE HELD kept playing after you let go, passed-through notes could hang when the routing changed,
  the dice changed on every playback start (renders differed), the panel's playhead ran 80 ms ahead,
  and a preset loaded from the menu didn't set its tempo. All fixed, each with a test.
- **Seen and left.** In the "chords on the clave strokes" figure a new chord arrives on its bar's
  second stroke (beat 2), as written; the reviewer's case for moving it rested on judgement alone.

## Part I. The fifth review (2026-11-20)

A fifth cold pass by six reviewers (funk, soul, church and blues; jazz and Latin; pop, dance, world
and ambient; the generator and voicing; the plugin in real time; and a comparison of the rhythm
library with three open-source accompaniment programs [61][62][63]) reproduced every finding with
`jcomp-dump` or a probe program. It found 75 problems (15 high); these were fixed, each with a test
where a test could hold it. The comparison's 15 gaps (endings, drop fills, phrases, tempo-aware
swing, new feels, 3/4 and 6/8) were built in 0.6.0 (below).

- **Passing runs.** A run restruck the top note already sounding, shrank to one note, wiped out the
  hits in its span (accents and the left hand's bass included), broke into fixed figures and, after
  the first section, stopped for good (AT MOST counted the section's bars). A run now walks from the
  note sounding (starting a sixth away at most when there's no room), is 2 to 4 notes, never lies
  over a written chord or another change, never plays in a figure played as written, and AT MOST
  counts the host's bars. A sus chord's run passes through its third.
- **Chords on the keys.** A legato change named both chords together; a change waited for the take's
  next hit (a bar late over a pad); LATCH did nothing in BOTH; letting one key go renamed the chord.
  The keys pressed together now name the chord, it is struck at its moment, BOTH latches to the
  progression's next change, and a release never renames. A tritone reads as a dominant's shell and
  a rootless voicing as its dominant (Levine's left-hand forms [2][57]); a triad over a foreign bass
  is a slash chord (F/G, not Fadd9/G).
- **Voicing.** A colour change (C7 to C9) threw the voicing an octave; SAME SHAPE ignored the knobs;
  GRIP was silent for many slash, diminished and augmented chords and made grips no hand can (it now
  checks a fingering on six strings in standard tuning and prefers the open position); 4THS fell
  back to SPREAD for triads and zigzagged (53% of moves past TOP MOVE, now 15%); SPREAD's left hand
  sat two octaves under the right; DOUBLE IN THE MIDDLE put the doubled note on top in up to two
  thirds of triads; ROOTLESS at five voices clustered the root over the major seventh; three-voice
  chops clustered an m9; DROP 2 lost a slash bass below FLOOR; a 6/9 over a bass didn't parse; sharp
  minor keys spelled the leading tone as the next letter (F for E#).
- **Idiom.** The bossa thumb played the old fifth over a chord arriving on beat 3, as did every
  alternating bass at a mid-bar change [59][60]; a plain dominant into a minor chord had a natural 9th
  and 13th; Freddie Green's four had no accents on 2 and 4 [19]; DENSITY and WEIRD stripped the
  batida, the guajeo and the Afrobeat tenor line; the Clav Riff got chord chops; the chicken scratch
  lost its motor; the Rhythm Guitar strummed every stroke down; Neo-Soul's progressions were tagged
  Dorian; the Rockers skank changed figure between sections; the held-chord styles re-struck mid-bar;
  the one drop gained chops before its skank; a boom-chick's strum kept the thumb's fifth string
  (Carter and Travis leave the bass strings to the thumb); fills came from another instrument (the
  Boom-Chick in Nashville Slip, chords in the tenor line); VARY dropped normal hits in quiet sections;
  the organ bubble's left hand rang for a beat (Berklee's "Bubblin'" [64]); the pumps had no backbeat;
  Cramer's slip came on 60% of strokes and into notes already sounding; the Broken Chords left hand put
  the fifth under the root; Ambient's WEIRD added chromatic dominant 9ths. All fixed.
- **The plugin.** A host loop's first pass voiced, ran and strummed differently from the rest; a
  start a few ms after the bar lost its downbeat; FOLLOW ONE went silent in a one-bar loop; PLAY ONLY
  WHILE HELD ignored the bass coming in; BOTH struck the first chord twice; cuts after a jump clamped
  to the old position; PARAM_MOD reached PLAY and CHANNEL; the rolls threw other dice than the
  engine and drew runs that never played; a fill cleared in EDIT played silence. All fixed.
- **Seen and left.** A held chord pressed exactly on a block boundary can sound the old chord's first
  notes for a millisecond or two before the new one (the strike has already gone out); BLEND has no
  effect on a fixed figure, by design (now said in the README); the NOW PLAYING card turns to the
  next section about 80 ms early, with the clock's lookahead.

---

### The comparison built (0.6.0)

Each of the comparison's fifteen gaps, as built. The step texts are the review's conversions of the
MMA groove lines [61] and Impro-Visor style rules [62]; JJazzLab [63] is cited from its user guide.

- **F1 endings.** SONG now ends: the outro's last bar or two play an ending (MMA's `…End` grooves:
  SwingEnd `x...x...x...x...|X.......x-------`, the cha-cha's `x...x...x...x.x.|X---------------`, the
  R&B ending `x.......x...x...|X-------........`; a held style's whole two bars held; otherwise four, then
  the last chord held), its last chord rung, nothing anticipated. Then JComp rests. The old wrap into
  the intro is SONG's REPEAT.
- **F2 3/4 and 6/8.** A bar of three beats (twelve sixteenths), a song's own meter saved with it:
  Jazz Waltz (MMA's jazzwaltz piano and guitar, Impro-Visor's waltz `X2 X4`, the "2 and 3" comp),
  Country Waltz (oom-pah-pah) and 6/8 Ballad (six eighths leaning on 1 and 4, written in 3/4's
  sixteenths). Their progressions are a chord a bar.
- **F3 fills down.** Each section has a fill down as well as its fill: a held stab with the next
  downbeat clear (MMA's SwingFill `x-------x-------`, SonFill `x.......x.......`, else
  `X-------........`), played into a quieter section; the Yamaha fills JJazzLab describes are chosen by
  where they go.
- **F4 four-bar phrases.** MMA's 8-beat (`P1 P2 P3 P4`), doo-wop and spiritual (`C13 / / C1234`), ballad
  (`C1234 / C234 C1234`) and blues (`C13 / / C1234`) phrases, the fourth bar turning the phrase round.
- **F5 tempo-aware swing.** Above 160 BPM the swing relaxes:
  `0.5 + (swing - 0.5) * clamp((320 - bpm) / 160, 0.25, 1)` (our constants; JJazzLab's jjSwing adjusts
  its swing to the tempo, and Friberg and Sundström measured the long-short ratio falling with tempo).
- **F6 to F11 new feels.** Ballad quarters, the ballad figure and the right hand's arpeggio
  (pianoballad.mma); swing's held comps and the guitar's two-feel (swing.mma, swing.sty); samba
  (samba.mma, samba.sty); rock'n'roll triplets and the 50s pickups (rock-128.mma, 50srock.mma); the
  bolero (rhumba.sty, rhumba.mma); Calypso (calypso.mma).
- **F12 the intro's setup.** The intro's last bar into the verse is a held chord (`X-------........`),
  as MMA's intros end, unless *Intro sets up the verse* is off.
- **F13 band hits.** A chord can be marked accent, hold or shot (JJazzLab's chord interpretations).
- **F14 the pad layer.** The chords held on a second channel (MMA's `…Sus` grooves), off by default.
- **F15 cells.** The mambo's broken chord, the 60s R&B piano and organ, the slow blues' quarters with
  a pickup, and Western swing's four with its fourth-bar pickups.

## Part J. The sixth review (2026-11-20)

A sixth cold pass by five reviewers (the groove styles; jazz, Latin and the three-beat styles; pop,
dance, world and ambient; the generator and voicing; the engine and plugin in real time) reproduced
every finding with `jcomp-dump` or a probe program. It found 71 problems (1 critical, 20 high, 26
medium, 24 low); these were fixed in 0.7.0, each with a test where a test could hold it.

- **Endings.** Eleven genres' outros ended on a IV, a iv or a ii-V (Soul on iv, Latin Jazz on the
  V, House, Deep House, Reggae, Lo-fi and Disco on the IV or its minor, Motown a semitone up), so the
  song's last rung chord wasn't the tonic. A final cadence ends on I (Kostka and Payne, *Tonal
  Harmony*, the authentic and plagal cadences); every outro now comes home in its last bar, the
  Piano Ballad's last chorus in its own key, and WEIRD's approach chord never splits the last bar.
  Endings are tested for every genre and style, seeds 1 to 6, WEIRD 0.2 and 1.
- **Meter.** 6/8 shared the 3/4 grid, so its fills, endings and DENSITY treated eighths 3 and 5 as
  beats and swing delayed the second eighth of each dotted-quarter beat; it has its own grid now
  (twelve sixteenths, two dotted-quarter beats). A 3/4 split bar changed chord on the and of 2; it
  changes on beat 3 (2 + 1, as waltz charts write it), in the engine and the rolls. A style picked
  while playing that changes the meter re-finds the host's bars.
- **Harmony.** A held shell over a bass (C7b9 as Bbdim/C) took the slash bonus; a maj7 under its
  root rubbed in close voicings (the cost of a half step and a minor ninth under the root went up,
  and a maj7 doubles its 9th, not its root); 4THS dropped an add9's 9th. The new 7#5 quality gives
  the jazz waltz "Someday My Prince Will Come"'s I-III7+5-IV-VI7+5-ii-V. Bolero has its own minor
  progressions (the bolero's i-iv-V and its relative-major bridge); Rock gains the I-vi-IV-V and
  fifties set for its 50s styles. Cb is spelled in Gb and its modes (Gb major has Cb in its
  signature). ROOTLESS still adds the 9th to iii (now said in the README).
- **Rhythm.** The engine struck a fixed figure again at every change and a waltz's bar line on the
  one the bass owns; a pushed chord was struck again on the beat it anticipated (it now ties over,
  as a pianist's push does); phrases kept host phase, so a four-bar phrase's turnaround fell on the
  wrong bar after a switch (only clave figures keep host phase now); the pad cut at 16 bars, took a
  push a beat late and stayed on after it was switched off; auto fills ignored a 12-bar section's
  form. ENERGY at the bottom took the offbeat-with-bass left hand's downbeat; mutation and WEIRD could
  drop the chord on the beat a take is built on; BLEND copied another genre's line notes and left
  hands. Samba is at 200 BPM (MMA's double tempo), Mambo is a fixed figure on the clave, Western
  Swing accents 2 and 4 on the guitar, Sixties R&B is the piano player, the Right-Hand Arpeggio
  breaks up and back. DENSITY in swing added a pulse of chords on the beats (Levine: comp with
  space [2]); it adds offbeat stabs only. Freddie Green's four [19] played in two through the verse,
  each stroke 50 ms; it plays four, each most of a beat, and in two only at the lowest energy.
- **Fills and endings.** At low energy a fill could lose its anticipation; "push it" pushed the left
  hand and "shift" carried a push onto a beat; fills dipped 20 velocity under the groove's accents
  and fills down hit at full velocity (both now held to the groove's level); the boom-chick ending
  dropped the alternating bass; the bossa's ending was a bar of block quarters (it carries the batida
  into the last chord). Seen and left: the salsa ending is the cha-cha cell on both clave directions.
- **The plugin.** An ended song kept playing chords from the bass, stayed silent when SONG mode
  turned to LOOP or REPEAT, and couldn't replay the outro; a queued section silenced this one's band
  hits; a one-bar outro never rang its last chord; a seek into the ending played the take to the bar
  line; ACC could be softer than the take's accents; CHORDS edits left band hits on the wrong chords;
  the rolls ignored band hits and the EDIT roll wrote the song from inside its draw; a click on a
  tile in SONG mode moved where the song starts; ENERGY and BLEND remade the chords. Projects saved
  before 0.6.0 had turned from LOOP to SONG (which ends) and gained swing easing; old songs lacked
  fills down and endings; a damaged song could load a chord quality past the table or a pattern off
  its meter. All fixed. Seen and left: the host's time signature and bar start are not read (the
  README asks for 3/4 from bar 1), and the intro's setup takes the fill's place, so it needs *Fill
  into the new section* (now said in the README).

## Part K. The seventh review (2026-11-20)

A seventh cold pass, split as the sixth, reproduced every finding with `jcomp-dump` or a probe
program. It found 55 problems (3 critical, 7 high, 22 medium, 23 low); these were fixed in 0.8.0,
each with a test where a test could hold it.

- **Harmony and voicing.** ROOTLESS gave a 7#5 a natural fifth beside its #5 (the A and B forms put
  the dominant's alteration where its fifth would go [2][57]); it keeps the #5. A new key left the
  chords that ENERGY or BLEND keep in the old key (a locked section's moved); they move now. SPREAD
  found its bass within one octave and a sixth's gap, so many majors had two voicings and the top
  leapt a fourth in Motown; two octaves and a fifth's gap give it room (top jumps over a fourth went
  to about 1%). OCTAVES dropped a m7b5's b5 and every b9, #9 and #11 (a montuno's right hand plays the
  b9 against the bass's root [7]); it keeps them. 4THS stopped at its first strictness level, often
  one stack a chord; it takes the next while there are fewer than three, keeping the seventh. A
  triad's double was rejected or kept by how the stack was built; DOUBLE is now a rule on the result.
  WEIRD's tritone sub of a 7#5 kept the #5 (the old #5 is the sub's 9th, Levine [2] ch. 6) and was
  spelled sharp in the panel's NOW PLAYING and chord card and in the dump (Ab13 into G, not G#13);
  WEIRD gave the slow blues ambient sus chords. Rock's Mixolydian bridge repeated the pre (now
  bVI-bVII-IV).
- **Rhythm.** DENSITY's thinning kept a bar's first hit even when it was a ghost, dropping the bar's
  chord; it keeps the first sounding chord. A pushAll genre pushed its fixed figures; a fixed figure
  tied pushes over (a guajeo strikes its two-side as written). A run cleared a beat or an accented
  chord under it. The offbeat test read the take, not the ending, while the ending played. Freddie
  Green's four went to two at low energy; Green kept four [19] (Four To The Bar plays four
  throughout). The 60s R&B piano's second bar was one upbeat; it plays the piano's and the organ's
  part as one two-bar rhythm. Bossa's swing feel was on sixteenths its grid doesn't have. Western
  Swing's four gained fill-in chops at high energy; a four to the bar takes none. Added hits from
  mutations and WEIRD came in at full level in a quiet take; they sit at or under its own. The
  answer bar deleted line and thumb notes ("without variation", Veal via [14]).
- **Fills and endings.** A skank's or offbeat stab's fill that came out as its groove led nowhere;
  its last two chops are accented with a pickup. A kLongs fill and a fill's fallback took the take's
  first bar, not its last. The offbeat styles ended on block beats; they end on their offbeats, then
  the held chord. The bossa's ending copied the cycle's second bar; it copies the first. A held
  style's intro set-up was a stab; it holds. Ambient's fills pushed every section join; they don't.
  Funk's fills tied over the next section's One; they are pickups. A fill left with one downbeat chord
  no longer carries a push. Salsa's ending now puts its last chord on the and of 4 (a cierre's
  anticipated last hit), held.
- **The engine and plugin.** A jump left a fill playing in the bar it landed on (and the ending,
  outside SONG); SONG running free started from the intro, not the Section parameter; a band hit on a
  chord the take pushed into struck it twice an eighth apart (it goes on the push). Switching in 6/8
  counted quarter beats. Host automation of ENERGY or BLEND threw away chord edits that the panel's
  ENERGY keeps; a host loop's dice changed every pass with SAME DICE off (a bar's dice are the host
  bar's now). A 6/8 song saved by 0.6.0 stayed on the 3/4 grid (it moves to 6/8 on load; songs are
  saved as version 4). The rolls missed the anticipation, the tie over a push, the change struck where
  the take has no hit and a hit's chance, put the playhead on the take's own bars, and drew the
  ending over the outro's first bars with band hits it doesn't play; a probe now finds the same
  onsets in the roll and the engine for all 87 styles. The EDIT roll hid hits under a HOLD. The
  descriptor said 24 genres. Nashville Slip's runs were forced to at least 0.5 though its slip
  replaces them (it now has the player's runs).

## Part L. The eighth review (2026-11-21)

An eighth cold pass, split as the seventh (styles and genres; endings, meters and bossa; fills and
ambient; harmony and voicing; the engine, plugin and rolls), reproduced every finding with
`jcomp-dump` or a probe program. It found 59 problems (13 high, 23 medium, 23 low); these were fixed
in 0.9.0, each with a test where a test could hold it, except two judgement calls noted at the end.

- **Band hits.** ACC replaced the stroke's own length and push with a plain accent; it keeps them.
  HOLD and SHOT silenced a chord leading into the next change (the and of 4 before it); the lead-in
  plays. A band hit pushed into a section's first bar in SONG mode struck over the bar line into the
  next section; it doesn't, and a pushed band hit counts as struck when its beat comes. The rolls draw
  all of this as played.
- **Harmony and voicing.** A piano's alternating bass played the fifth above the root into the chord
  (boom-chick's E3 then B3 under an E3 chord); on keys it goes below when the fifth above would crowd
  the chord. DOUBLE dropped every doubled-top voicing; they are choices at a cost again. A 13th chord's
  three-note voicing dropped its 13th; it keeps 3, 7 and 13 (Levine [2] ch. 3). A three-note 13sus
  was the ii9's notes a fourth over; it is 4, b7 and 9. 4THS lost every 7b9's b9, 7#11's #11 and 7#5's
  #5 at its looser levels; the altered tone always stays, the 13th goes with a b9 or #9, a #11 keeps
  its lydian 13th, and a chord no stack holds is voiced SPREAD. A sus or triad in 4THS took no 9th, and
  voice leading counted only the new voicing's notes, so a thinner voicing always looked cheaper; the
  sus takes its add9 and movement counts both ways. A minor-ninth interval cost as little as a few
  semitones of movement; it costs as much as the maj7-under-root rub (the voicing texts keep it out
  [2]). A diminished triad's first inversion was named a rootless G7 (vii°6, Kostka and Payne), and a
  m7b5's upper inversions an Ab9; a chord held whole is named as itself. A guitar's 9th grip at five
  voices was four strings; the fifth goes on top (C9's x32333). OCTAVES' line leapt a sixth inside a
  chord, its register set by the key; each stroke takes the octave nearest the last. WEIRD's half-step
  approach aimed at a slash chord's root, not its bass; it approaches the bass, and leaves a son
  montuno's I-IV-V-IV alone. The reharm put an approach on the progression's last bar, aimed at a bar
  1 the next section doesn't play. Deep House's progression was rebuilt from "Can You Feel It" [24][25].
- **Styles.** Chic's chucking muted every beat and rang only the e's and a's; it strikes the downbeat
  ("Good Times" and "Le Freak" start on the one: Premier Guitar's "The Emperor of Chuck"; Guitar
  World's Cory Wong lesson). Funk's Dorian Groove played the clav riff and the guitar scratch on an
  e-piano; it plays the stabs and the counter. The chicken scratch never rang ("occasional chords are
  allowed to ring out", Stewart); its accents do. Highlife's Bell Twelve was mutated and thinned like a
  comp; it is a timeline, kFixed. The Quiet Bossa had no thumb for three of its four half bars (the
  thumb is the surdo); it takes the batida and the thumbed cycle. Gospel Push and the Sunday Ballad
  could never run (their offbeat chords sat under every run); a style that walks into its changes lets
  the run take an offbeat chord's place, and Memphis Backbeat no longer marks a run into beat 2, where
  there is no change. Freddie Green's four leaned 20 to 27 velocity steps on 2 and 4; it is even [19].
  A swing comp gained soft chords on the beats at high energy, and the jazz waltz no fill-in at all
  (the "four" test counted its three beats); neither now. BLEND put a chord on an offbeat style's
  empty one, inside a held chord, or a ghost over an accent; it doesn't. DENSITY couldn't thin a
  one-bar fixed figure (every take is two bars); a figure whose bars are the same thins as any take,
  its thumb kept, and a clave figure keeps every stroke.
- **Fills and endings.** A fill was compared with the take's first bar for the Shout's accents and
  accented every hit in other genres (Palm-Wine, Boogie); it is compared with the last bar, the Shout
  only. Mutations raised hits above the take's peak and "push it" pushed funk over its One; they
  don't. Ambient's fill struck a fresh downbeat into a two-bar hold and stuttered the old chord an
  eighth before the change (Eno's loops have no cadential pickups); it holds on, or swells softly in
  the middle. The fill down put a bass on the organ bubble's dropped one [64]; it keeps the take's
  left hand. A fill's last-beat chords overwrote the left hand (the bubble's and of 4, Broken Chords'
  fifth); the left hand plays on. A fixed figure's fill down was two half notes (Freddie Green's four
  in two, the montuno off clave); it is the figure, its last chord rung. The 12/8 Ballad's fill dropped
  its middle triplets. The cha-cha and the bolero ended on the and of 4, which is the montunos' cierre;
  they end on the 1. The swing ending's last chord was softer than its accent, and House ended on four
  block quarters; the last chord is the ending's loudest, and a house riff ends on its own last bar.
  The ska upstroke's fill down and ending landed on 2 and 4; they keep its offbeats.
- **The engine, plugin and rolls.** SPREAD's low note on an organ or a pad dropped out between 1 and
  3, though the README said it stays; it stays. A locked section kept its 3/4 grid under a 6/8 style;
  it is made again on the new grid. ENERGY from the host threw away chords edited to start with N.C.
  A start or jump mid-bar played the partial bar with the last bar's dice and take. The main thread
  could take the "remake the chords" flag without the remake (both are one atomic word now). A
  pushed TOP RINGS chord's inner notes ended before the beat it tied over. The rolls drew a fill or a
  fill down over bar 1 rather than the bar it fills, stopped at four bars of an 8- or 12-bar
  progression, struck SPREAD's low note and GRIP's thumb strings on every chop, played the fifth
  before the root on a change, rang a chopped style's bass, drew runs under a HOLD, didn't shorten the
  chord before a run, and threw the view's dice, not the section's. All now follow the engine.
- **Left as judgement.** The Rhythm Guitar's three-voice 9th grip keeps the voicing voice leading
  picks rather than always the 3-b7-9 inversion, and the roll doesn't draw the run into the next pass's
  first bar from the view's last (it draws the runs inside the view).

## Part M. The ninth review (2026-11-21)

A ninth cold pass, split as before (styles and genres; endings, meters and the reharm; fills and
energy; harmony and voicing; the engine, plugin and rolls), reproduced every finding with `jcomp-dump`
or a probe program. It found 72 problems (11 high, 31 medium, 30 low); these were fixed in 0.10.0,
each with a test where a test could hold it, except the few noted at the end. Two fixes of 0.9.0 were
turned back: DENSITY's thinning of a one-bar fixed figure (a figure that is the style, the bell, the
batida, Freddie Green's four, loses nothing below the middle now, as a clave figure didn't), and the
naming of the three-note rootless dominants (see below).

- **Harmony and voicing.** The stock left-hand forms 3-7-9 and 7-3-13 (Levine [2] ch. 3) were named
  as the tritone sub's 7#5 or 7#9 (E Bb D as Gb7#5), so the bass played a tritone away; the unaltered
  dominant wins. nameChord broke ties by absolute pitch class, so 40 of 252 inversion classes were
  named differently by key (Am7/G in one key, C6/G in another); candidates are counted up from the
  bass, and of two whole readings the one with a third wins (Cm11, not F13sus over Eb). A whole chord
  over its own third took the slash-bass reading (Cadd9/E as Gsus/E, C7#9/E as Cm7/E); it is itself.
  The keys' fifth below (0.9.0) went up to five semitones under FLOOR; it never does. A new chord's
  fresh voicing could carry a minor-ninth rub the best one lacked (Quiet Storm's B13sus); it can't.
  4THS gave no m9 its 9th at five voices and dropped a m7b5's b5 and a minor-major's 7th; the levels
  go on down until a stack holds the 9th, and a b5 or a maj7 over a minor third stays. OCTAVES and SHELL
  put a major seventh a half step under a doubled root (Pease and Pullig rule it out); the seventh
  takes the root's place, and the shell's upper part skips the root. ROOTLESS at three notes dropped a
  #11 or #5. SPREAD's triad at five voices gave four notes (the doubled fifth was one pitch class); the
  stack's lowest note is doubled an octave up. A guitar's grip on a maj7, a 13th or an m11 was often
  one no hand can fret (70 of 348 at five voices); close grips over the root are offered where the
  fifth-and-octave one can't be fretted (26 left at five, none at four). A thinner voicing won by
  moving less; a voice short of what fits costs a little.
- **Styles and the reharm.** The Dorian Groove's counter-rhythm never touched the One, and its loud
  sections got it most (Vincent's *Funk*; the code's own "Funk is on the One"); it starts on the One.
  The son montuno changed chord once a bar, half the guajeo's harmonic rhythm (Moore's I IV V IV in
  one clave, via Wikipedia's *Guajeo*); two chords a bar, the and of 2 anticipating beat 3. Its octave
  line leapt a sixth where the window cut the root off; the strokes after the first may dip a fourth
  under it. Freddie Green's four was three equal notes with random lengths (Pettersen: 1 and 3 a
  little longer, the low string only touched); 1 and 3 are held longer and the low note is lighter, and
  mutations no longer chop it, the bossa's fingers or Held Comp. The gospel shout's chords struck the
  left hand's bass key again; an organ's chord over its own left hand leaves it out. The "left hand"
  mutation put the organ bubble's bass on 1 and 3 (Berklee's *Bubblin'*: all the upbeats); not into an
  offbeat take. The Quiet Bossa was the batida at a slower tempo (kSparse did nothing to a three-chord
  bar); it has a thinner figure of its own, and Charleston Comp loses the no-op too. Chic's chucking
  was one bar on one shape; the second bar answers, stab-oriented. The Bell Twelve strummed five
  strings at one level on every stroke (Wikipedia's *Highlife*: arpeggiated, two-finger); three
  strings and an accent on stroke 1. The clav riff climbed past its window; its octave fits under the
  top. WEIRD's approach chord was the chord itself under a falling slash bass (I to I9 to V/3); not
  where the bass already moves by step. A locked section kept its grid between 4/4 sixteenths and
  12/8; it is made again. The bolero ended on the cha-cha cell (MMA's BoleroEnd broadens); it ends on
  its tresillo, held. Some section joins left a secondary dominant hanging (the swing chorus's VI7
  into the bridge's III7, the salsa bridge's V/vi, the bolero's pre-chorus V7 into iv, the waltz
  chorus's V7 into IV); each now resolves.
- **Fills, endings and energy.** A montuno's fill down cleared its pushes, so it played the old chord
  on the and of 4; it keeps them. Held, legato styles' endings chopped their penultimate bar; the
  strokes before the last ring. A skank at energy under 0.15 rang for a whole bar, chorus energy added
  un-chopped chords to choked styles, and swing fills added soft chords on the beats; a choked style
  stays chopped and a swing fill's fill-in is off the beats (keyed on swung eighths, not the swing
  amount). A fixed figure's fill jumped its last stroke to a raw accent and pushed where its style
  doesn't (the cha-cha, the bell); it comes up from the take's level and pushes only with the style.
  The gospel shout's fill accent never fired; it does. Fills skipped the player, so an organ's fill
  went detached; an organ's fill holds. The fill pushed a riff's last note to the next chord's degree;
  a line isn't pushed. Held-chord mutations added unrung eighths, re-struck ambient's held chords, and
  "push it" rang a skank; mutations keep the style's articulation. Mirror Drift's fill re-struck its
  held chord half way through the bar; a bar that only holds has no fill. A fill made from the take's
  last bar was scaled for energy twice; once. The fill's left hand was the take's first bar, not the
  one it replaces. Several fills were the groove itself (Sixties R&B, Two-Step); the last beat rests
  and its "and" leads in. Blending brought another style's ghost mutes into held styles; onsets only.
- **The engine, plugin and rolls.** A push below the middle of LENGTH was still treated as tied, so
  the beat it led to was silent; only a tied push holds the beat. A tied push ignored LENGTH toward
  legato; it follows it, and a chopped push isn't tied at all. A looping section with a switch queued
  counted every slot as a change and cut long chords at each half bar; the section's end is the
  pass's. VARY never swapped a take with SAME DICE at 1, 2 or 4 (a constant salt); each section throws
  its own dice from its seed. A late switch left the old section's pushed band hit ringing over the new
  bar 1; the switch clears it. ACC on a pushed chord lost its tie and nudge; it keeps both. A start or a
  loop inside a fill bar played the take; the bar's fill, fill down or ending is decided there too.
  RUNS SHORTEN missed the chord struck where a late run was decided; it is cut at the run's first note.
  The host's recipe change could be lost to the order of a flag and its value; the value is stored
  first. Turning SONG on mid-play left a looped section at the next bar line; it waits for the pass's
  end, or (following the host) lands where the song is. The panel's tile showed the fill, section and
  take a lookahead early; it shows the bar heard. Copying another section's chords didn't round its
  bars to the progression; it does. The rolls threw the run dice for the view's bar, never drew the
  turnaround run into the next pass or page, started every page afresh, dropped a run into a band hit,
  ignored VARY's soft-hit drops, and ignored HALF STEP FROM BELOW and LANDS ON; they now follow the
  engine (a take's view is played from bar 1 and a bar past it, and cut out). The README's loop
  promise was overstated (a first pass entered from before the loop can differ a little); it says so.
- **Left as judgement, or for later.** SONG plays each section once (B08); an editable song structure
  is planned as its own change. Afrobeat's Interlock still lets the tenor line take the chord chop's
  place where both strike (both together need a hit that carries two parts). A 4THS C9 at four voices
  is still the 13th's stack (no four-note stack holds root, 3, 7 and 9). A fill's roll is still voiced
  from the fill's own start, not from the take before it.

## Part N. The tenth review (2026-11-21)

A tenth cold pass, split as before (styles and genres; song mode and structure; fills and takes;
harmony and voicing; the engine, plugin and rolls), reproduced every finding with `jcomp-dump` or a
probe program. It found 54 problems (2 critical, 4 high, 22 medium, 26 low); these were fixed in
0.12.0, each with a test where a test could hold it, except the two noted at the end.

- **Song mode and structure.** A host count-in (negative beats) placed the song at its last entry;
  the song waits on its first. A structure ending on a section with no ending of its own never ended;
  the outro's ending plays, and the tile and NOW PLAYING show it. A structure edited while following
  the host kept the old position; it is placed again, and so is a change to a section's bars or to
  SONG ENDS. NEXT and PREV stepped sections, so Pop's C C skipped its second chorus; they step
  entries. The panel's frame kept the last bar's fill and ending lit after the song stopped; only a
  live bar shows them. A position past the structure's end wrapped to the start; it is clamped, and a
  section found again after a wrap with SONG ENDS on is its last entry. RESET didn't clear a waiting
  count-in or a queued entry; it does. A section that comes back played identically each time; it
  plays a little harder (four percent a time, to the fourth) with its own dice, and AABA's last A
  before the bridge closes on the outro's chord (a judgement: the A before a contrasting B usually
  cadences home). New songs were Once through; they are Verse-Chorus. A factory preset dropped the song's
  structure; it keeps it. The start entry is marked on the letters; an empty structure's readout says
  it plays every section once.
- **Harmony and voicing.** The octave line leapt an octave or more between chords in 9 of 12 styles
  that use it; it follows the line before, within a fourth where the window allows. E Bb C was named
  Gb7 rootless (a tritone away); a held root with its 3rd and b7 wins (C7/E). C E A D was Am11/C; a
  bass that has a third above it wins (C6/9). A tritone pair is named by the key (Bb E in C is C7,
  not Gb7). A three-note maj7 dropped its 7th; it replaces the root. A guitar's open C was the barre;
  the open triad shapes (x32010) are offered for a major or minor triad, and a grip that can't be
  fretted at five strings tries four (none unplayable of 348). ROOTLESS at three notes on a minor or sus
  chord lost its extension; the 11th or 13th takes the fifth's place. A slash chord's bass on keys
  went above the chord; it stays under.
- **Styles.** The Bell Twelve played open fourths with no thirds (A01); close triads. Freddie Green's
  four played every stroke the same length; 1 and 3 ring a little longer (sometimes nearly the full
  beat) and 2 and 4 are a touch louder (Berens [20]). The gospel shout's organ held every upbeat
  legato at 160 bpm; at a fast triplet tempo the upbeats are half held and the downbeats ring. Chic's
  kBusy filled the second bar's rests one take in five; a rest the other bar plays is filled only
  seldom. WEIRD put jazz's tritone subs into every genre; only jazz families get them, others approach
  from the V7, and the Caribbean and African families get no reharm.
- **Fills and takes.** Boom-chick-type takes (Two-Step, Boogie Figure, Organ Bubble) and Broken Chords
  had fills identical to the groove; their last chords lead in, the broken chord's over its left
  hand's last note. Fills raised a quiet section's chords to a full accent (up to +75%); they come up
  to the take's peak and a little, never more. Interlock's fill accented and pushed the tenor line;
  only chords lead in. The 6/8 fill's lead-in fell on eighth 5; it is the last eighth. A held fill put
  a downbeat chord into a backbeat take; not over a kept-off beat. A pushed lead-in could be a WEIRD
  chance hit; it is always played. Ambient's fill down re-struck its held drift chord and went silent
  for a beat; a drift has no fill down. "sparser" and "shift" took a boom-chick's bass off 1 and 3
  (thinnedBy's own rule); the bass's strong-beat notes stay. "busier", "shift", "answer" and WEIRD's
  strays cut held chords; nothing is struck inside one. Mutated organ takes stabbed; the organ's rule
  holds them. Four To The Bar's takes were always the same as its first; an even four's 1 and 3 may
  go lighter, and a take that came out the same as its parent is mutated again. BLEND kept the other
  genre's lengths (a held skank, a stabbed Held Comp); this style's lengths stay. DENSITY above the
  middle chopped pads into eighths; a held style or a pad gets only rung chords on the beats.
- **The rolls.** A ringing chord was drawn over the next change, where the engine cuts it; it is cut
  there. SPREAD's held low note wasn't cut by the next low note; it is. A line counted as the chord
  hand for RING's gap; it is a hand of its own, as in the engine. An ending's roll used the ending's
  own offbeat feel, not the section's; the section's.
- **Left as judgement.** C E F# B is still D13 (the same pitch set as Levine's G13, F A B E, an
  earlier choice). A three-voice 13sus is the IV triad over V; the bass marks the change. A line
  style's fill is its groove (Tenor Line, Palm-Wine, Right-Hand Arpeggio), documented in the README.
  The rolls don't draw the slip note's grace notes (documented).

## Part O. The eleventh review (2026-11-21)

An eleventh cold pass, split as before (styles; song mode; takes and fills; harmony and voicing; the
engine, plugin and rolls), reproduced every finding with `jcomp-dump` or a probe program, and the roll
reviewer compared the editor's roll with the engine's first pass for all 87 styles and 7 sections. It
found 71 problems (9 high, 25 medium, 37 low); these were fixed in 0.13.0, with tests where a test
could hold them, except those noted at the end.

- **Song mode.** A second NEXT onto another entry of the section playing cancelled the queue (and a
  later NEXT skipped an entry); it goes there, and a NEXT back onto the entry playing cancels cleanly.
  A structure ending on a section other than the outro rang out its last chord, often the V7; the last
  entry of a song that ends closes on the outro's last chord, and so does AABA's second A, each with
  its own last bar's cadence moved a bar earlier (only the last bar had changed, which lost the
  cadence in 83 of 87 styles). The repeat lift multiplied the take and clipped at 127, so by the third
  chorus the accents were gone; it lifts the softer hits more than the loud ones. Stopped, a cue
  (NEXT, PREV, a note, a tile) was shown but play started from the Section parameter; play starts at
  the cue. HOLD CHORD let go in SONG moved the song on mid-phrase and off the host's bars; it is placed
  again where the host is. PREV from the first entry wrapped to the outro and ended the song; it stays.
  A section played by hand outside the structure took the interrupted entry's lift and dice; its own.
  Placing the song dropped a queued section; it keeps it. RESET kept a stale push target, which
  swallowed the next downbeat.
- **Styles.** Chic's second bar still filled its rests in two of three paths (kBusy, the busy ghost,
  the "busier" and "ghosts" mutations); all four respect them. Afrobeat's Chord Chop and Interlock
  were varied like any take; they are fixed figures. Memphis's organ held its 2-and-4 to the next hit,
  so no backbeat was heard; an offbeat take's organ beats are half held. The batida (and every fixed
  figure) rang one bar and clipped the next; a fixed figure never rings or chops by the player's rule,
  nor does an even four (a chord on every beat) except on an organ. Motown's WEIRD took jazz's bII9
  approaches; it takes the pop branch's V of the next chord.
- **Takes and fills.** A take came out the same as take 0 one time in twenty (a fifth in offbeat
  styles): "accents" was undone by the cap that ran after the no-op check; each mutation is now judged
  after the whole post-pass, retried if it did nothing, and only the ones that did something are
  named. "accents" works from the take's peak, keeps the One, and leaves lines and ghosts alone; "let
  it ring" skips lines and ghosts. "sparser" took notes out of an ostinato left hand; it leaves the left
  hand. "push it" and "ghosts" struck inside held chords, and WEIRD chopped held chords and left its
  strays unchoked in a choked style; none of these now. "answer" compared bars by onsets alone and
  overwrote a second bar's left hand; it compares parts. "left hand" and "run in" are offered only
  where they can do something. DENSITY's fill-ins went under rung chords and had fixed velocities,
  louder than a quiet section's own chords; they skip rung chords and sit under the take's peak. A quiet
  section's fill was a full bar louder than its take; it is shaped at the section's own energy below
  0.15. Riff styles' fills (Clav Riff, Right-Hand Arpeggio) were their groove; they end on a lead. An
  eighth-note take's folded genre fill came out as the groove; the lead-in rule now applies after the
  fold. A 6/8 fill rested the whole second dotted beat; only its last four steps rest. The 3/4 and 6/8
  endings put a block on the oom-pah's bass downbeat; they keep the offbeat. The MUTATE page skipped
  the organ's post-pass; it uses it.
- **Harmony and voicing.** The montuno's octave line sat nearly an octave under its window; it starts
  no more than a third under it and prefers the chord's degree, its 7th and fifth over a low root. A
  guitar's G, D, E, C7 and B7 were barre or close shapes; the open shapes are offered and preferred.
  4THS' added 9ths, 11ths and 13ths could be out of the key; they pass the key's scale. A three-note
  Cmaj7 over E dropped its C; the maj7 swap is only over the root. A tritone pair was named by the
  major scale of the key (B F in A minor was C#7); it follows the key's scale. Cmaj7#11 over its C
  was read as D13 without the C; a bass a fifth under the rest that explains the chord is its root. A
  power chord was major whatever the key; in a key whose scale has the minor third and not the major,
  minor. A minor key's V7 ran F# and C# against the tonic's third; a dominant takes the key's b9 and
  b13 where the key has only those (the harmonic minor's V, Levine [2]). 4THS at five voices played a
  dominant without its 7th (an Eb6/9); every size with the 7th is tried first. SPREAD put a 7#9's #9 a
  half step under the 3rd; its upper part may start on the 3rd, so the #9 goes on top (the Hendrix
  chord). SHELL at four voices doubled the root on top; the root is the bass's. ROOTLESS at three voices
  dropped an m11's 11th and a 13sus's 13th; they take the colour's place. B major's #iv°7 and V/V/3 were
  spelled with F; E#. The Strummer's blurb said six strings, but a grip holds five notes at most; it
  says "across the strings".
- **The engine and the rolls.** A chord played a moment late on the keys with a strumming or rolling
  player let the old chord's unplayed strings sound over the new one; they are dropped. The engine
  judged a later bar's hits with this bar's dice; each with its own bar's. A change struck where the
  take had no chord counted a bass note as its next hit; the chord's own hand. Before the host's bar 1,
  LOOP played through the count-in and a bass played in SONG's count-in brought chords in; both rest.
  NOW PLAYING's chord and keyboard lit a lookahead (about 80 ms) early; they show the step heard. Freddie
  Green's four ignored LENGTH above the middle; it lengthens toward the next stroke. A factory preset
  read a user song file of the same name (a case-insensitive file system, a new factory style); only a
  user preset has a song file. The rolls ended a ring at a hit its chance or VARY leaves out, never cut
  a chord at the next chord of its hand, voiced a fill or an ending from nothing, drew Freddie's four
  at the generic length, had no 50 ms floor, let a chord a run replaced cut the held low note, struck
  no chord for a change over a bass note, dropped a chord tied over a page's downbeat, and drew the
  first chorus's dice and chords for every repeat; all as the engine now. Over the 609 sections at the
  defaults, 6 still differ (swing, which the roll draws straight, a key the line strikes again, and the slip
  note's grace notes).
- **Left as judgement.** Interlock's guitar and tenor share steps (a step holds one part). The organ's
  shout rule uses the style's tempo, since takes are made without the host's. In E Mixolydian C F# is
  G#7 (neither reading is in the scale; the reviewer's D7 has a C natural). G C F Bb is Gm11 without its
  fifth, the reading with a third, as earlier choices (Cm11 over F7sus). The rolls draw a section's
  first take; VARY's borrowed bars are heard, not drawn (documented). Most generated sections end off
  the tonic (tracked as data for the closing rule above).

## Part P. The twelfth review (2026-11-21)

A twelfth cold pass, split as before (styles; song mode; takes, fills and DENSITY; harmony and
voicing; the engine, plugin and rolls), reproduced every finding with `jcomp-dump` or a probe
program; the roll reviewer compared the editor's roll with the engine for every style and section,
over takes 2-4, fills, band hits and SONG entries as well as the first take. It found 61 problems
(12 high, 25 medium, 24 low); these were fixed in 0.14.0, with tests where a test could hold them,
except those noted at the end.

- **Takes and DENSITY.** 0.13.0's "an even four never rings" covered every player; it is a guitar's.
  "let it ring" still rang fixed figures (the montuno, the batida, the bell) and lines, ghosts and
  hits already rung, and swapped a held RING for TOP RINGS on organs, pads and guitars; it skips them,
  and only a piano keeps TOP RINGS. "accents" lightened the One; never. A tag could name a mutation
  twice; each once. "run in" was offered to players with no runs and to held styles; not. A line-only
  take (the Afrobeat tenor) bred its parent four times; its line's dynamics vary. DENSITY's threshold
  was a square root (half the hits gone in its first notch) and then, when made straight, left a third
  of its travel dead; it is a 0.65 power, and a motor of ghosts thins as its other offbeats do. It also
  thinned a fill's push; kept. Its fill-ins re-struck a tied push's beat, did nothing in held styles,
  and filled phrase rests; they leave a tie alone, swell a held chord on 1 or 3, and seldom fill a
  rest. *back* restored only take 1; the whole section.
- **Fills.** A fixed figure's fill accented (and its fill down rang) the tenor's line note; its chord.
  Palm-Wine's fill was the groove less its last note; its genre's own fill, which has the line too, and
  a riff whose lead step holds the thumb puts its chord on the next free step. A build-up's backbeat
  dropped to the fill text's level; never under the groove's chord on the same step. The lead-in's
  rest was refilled by the energy; it stays. A near-silent section's fill had no lead-in; its last
  chord stays. The song's ending struck a raw accent after a quiet outro; it sits at the outro's level.
- **Styles.** Interlock's chops weren't choked; they are, the line excepted. Mambo was a jazz comper's
  rootless block on every stroke; the Montuno player (Moore's guajeo, an octave line moving). Clav
  Riff's only chord, its resolving stab, could be thinned to a top note; an accent stays a chord.
- **Song mode.** A NEXT queued before a stop survived it, and the song skipped ahead after the
  restart; running free it is the start cue, following the host it is dropped. A stale queued entry
  sent a later NEXT to the wrong entry after a stop or a seek; cleared or re-aimed. Stopped, the tiles
  showed the last section played; where play starts. Closing a phrase moved any last bar a bar
  earlier, which made a blues end V-IV-V-I and dropped a pop chorus's V; only a dominant moves. The
  repeat lift raised ghosts toward the soft level; 5% a repeat, every hit in proportion. A factory
  preset turned SONG off; it keeps how the song is played. The panel's HOLD CHORD skipped the SONG
  release; it lets go as the note does. NEXT at a song's end wrapped to the intro, PREV twice at the
  start cancelled its restart, and NEXT with SWITCH AT longer than the entry made it longer; none now.
  The structure lit the interrupted entry's letter for a section played by hand, and a fill played
  under HOLD CHORD; neither now. Bars were counted from beat 0 whatever the host's bar lines; where the
  host's meter is the song's, its bar start sets them.
- **Harmony.** 0.13.0's open-grip table dropped every colour tone (add9, 9, sus) and rewarded any
  subset of an open shape; a grip must hold the chord's tones (a fifth may go) and an open grip must be
  an exact cut of its shape, and the table gained Cadd9, the sus2 and sus4 grips. The Blues scale named
  the I7's 7-3 shell as its tritone sub and read a power chord as minor; it reads as the key's
  Mixolydian, and a power chord on the V stays a power chord. SHELL, GRIP and CLOSE over a slash bass
  left out the root (C/G was Em/G); the root stays. 4THS lost an m9's 9th at every size; the "So What"
  stack (its 9th in the third's place) is a choice where no stack holds both. At five voices 4THS often
  played three notes; a missing voice costs more there, and a secondary dominant in a key takes the
  key's b9 and b13 and a stack with a tritone and a major third (B E# A D over C#7). OCTAVES' line
  leapt a sixth after dipping under the window; a degree out of reach gives way to one in reach. The
  7#9's wider search went above HIGHEST; it widens down only. Db under D F Ab read as Ddim/Db; a bass a
  fifth apart whose chord holds the rest is the root. C# minor's major third was F; E#. Gb blues's IV
  was B7; Cb7.
- **Rolls and the engine.** The roll decided a run at its landing chord, after drawing the hits the
  run replaces; it decides where the run starts, from the chord sounding there (takes 2-4 at RUNS 0.6:
  438 differing notes to 4). It struck a change wherever the chord differed from the last full chord
  drawn; only where the progression changes (107 to 10). Past a section's end it drew the section's
  own first chord; the next entry's, as the engine plays it, and an entry is led on from the one before
  it, its push tied over (fills in SONG 132 to 4; voicings across entries 1,824 to 471). Fills were led
  from take 1; the take playing (223 to 1). OCTAVES' line restarted for each roll; it carries on. A
  left hand's single note under TOP RINGS was drawn half length, band hits had their own lengths and
  dropped the bass the engine keeps, two notes on one key overlapped, and SAME DICE Never counted each
  section's bars; all as the engine now. Runs climbed to 92 whatever the section's level; they climb
  to the take's own loudest. A change struck between a grip's or an organ's take hits kept the bass the
  take's own chords leave to the left hand; it leaves it too.
- **Left as judgement.** OCTAVES and SPREAD start at four notes; VOICES 3 plays them as at four
  (documented). Following the host in AUTO, a cue made while stopped is for PLAY; the host's start
  places the song (documented). A vamp can still be offered "run in" (a take doesn't know its chords).
  Some band-hit HOLD lengths in the roll still differ from the engine's (267 of 49,247 notes). A few
  secondary dominants in a key still voice three fourths at five voices where no five-note stack holds
  their tones. The roll draws LOOP's first pass, not the later ones the engine voice-leads into.

## Part Q. The thirteenth review (2026-11-21)

A thirteenth cold pass, split as before (styles; song mode; takes, fills and DENSITY; harmony and
voicing; the engine and rolls), backed every finding with `jcomp-dump`, a probe program or the roll
comparator, which now plays whole SONG chains as well as single takes. It found 57 problems (4 high,
23 medium, 30 low); these were fixed in 0.15.0, with tests where a test could hold them, except those
noted at the end.

- **Styles.** The "top line" mutation thinned Clav Riff's resolving stab to one note in every take
  2-4; it leaves accents alone and isn't offered to a take with a line, and the dead kTopLine mod is
  gone. Interlock's line and chop shared three steps and a die decided per section which kept them;
  the line is laid first, the same way everywhere. The batida's thumb played its fifth above the root
  (up to C#4); a guitarist's thumb plays it below. The montunos and guajeos played staccato sixteenths
  and their push into the three side was cut at the bar line; they play eighths, and a fixed figure's
  push ties over an empty beat (not over a struck one). Calypso Piano's chorus was never busier than
  its verse; its mask is wider. A quarter pulse on keys rang or not stroke by stroke; once a take. The
  README listed twists (a sparser hand, shuffle, the top note alone) no style uses; it lists those
  that are, and Mambo's line names its guajeo.
- **Song mode and transport.** HOLD CHORD let go at every host loop wrap or jump, and in SONG a jump
  moved the section under it; the held chord is kept apart from the voice leading, and the song stays
  put under HOLD. A pickup bar at the project's start played the song's first half bar and then
  restarted it; the pickup rests as a count-in does (a short bar mid-song still leaves the bar lines
  as they were). A queued NEXT, PREV or tile turned off the phrase-closing chords; it reads the entry
  it will reach. SAME DICE Never following the host counted from where playback started; from the
  host's bar 1. A host stop in AUTO kept a cue on show that the start then dropped, a stopped cue
  showed a stale repeat, a NEXT in an entry's last 80 ms skipped an entry, and LOOP after SONG kept
  the entry's anchor; all fixed.
- **Takes, fills and DENSITY.** The held-style swell under DENSITY never fired (the held-over check
  came first); it plays. DENSITY's thinning still came in steps (each class of hit at once); each
  hit's own roll is spread over the travel. It dropped an arpeggio's bass on the One (the line's
  notes counted as chords); it keeps it. It filled the rest before a fill's lead-in, turned the one
  drop's skank into a ska chop, and struck a change twice with a fill-in; none now, and an unpushed
  fill pickup is kept as a pushed one is. Five held or fixed styles had four identical takes; fixed
  accents may lighten a stroke other than the One, and the new *restrike* strikes a held chord again
  at its half bar. WEIRD gave guitars, organs and pads TOP RINGS; RING without a piano's pedal. The
  palm-wine fill's lead-in no longer hangs on one stroke of the groove.
- **Harmony.** 4THS at five voices gave a V7 three notes (the stack with the b9 clash blocked the
  clean one); the clean four-note stack is built, and each voice more short costs more than the last.
  It also dropped a dominant 9th's third at four voices; the "So What" substitute is for chords
  without a major third. ROOTLESS at five voices put the 5th a half step under the b13. Slash basses
  and passing diminished chords were spelt flat (E7/Ab, Dbdim7); by the chord tone's letter, and a
  diminished chord on a raised degree as the raised lower neighbour. GRIP never chose the open G7
  (320001: its fret span read as unplayable once cut) and played four-voice sevenths as three-note
  shells; both fixed, and the dead sus2 rows are gone. The "V power chord is major" rule fired in
  Dorian and Mixolydian; only in minor and blues. OCTAVES doubled a 7#9's #9 and dropped C11's 11th,
  and its look-ahead ignored HIGHEST. An altered slash dominant lost its 7th at three voices, and
  CLOSE and DROP 2 slash triads at four voices played five notes.
- **The engine and rolls.** SONG's roll was led from one entry back in take 1 (21,322 notes over all
  styles the engine doesn't play); it chains from the song's first entry in the take playing, cached
  (532). Fill rolls over an entry's last bar were drawn over the wrong bar; under SONG the roll counts
  the section's own bar, and fills are led from the chain (2,408 to 90 differing cases). The passing
  run walked over the wrong chord, ANY NOTE searched the wrong voicing, RUNS AT MOST stopped at the
  first run (and now carries on into the next entry), and the run dice ignored the repeat's salt.
  NOW PLAYING drew the progression under a held chord; the chord heard. A SHOT or ACC held SPREAD's
  low note to the next change, ACC lost TOP RINGS, a HOLD band hit ran past its slot, OCTAVES' run
  landed off the line and GRIP's kept its bass string, and a key could be re-struck 20 ms after
  itself; all as the engine now (band-hit MARKS 953 differing notes to 40).
- **Left as judgement.** A major seventh over its third or fifth keeps the root a half step over the
  7th, as written (documented). VOICES 3 still plays SPREAD and OCTAVES as four (documented). A host
  bar of another length mid-song leaves the bar lines as they were. The roll's "one key once" rule
  still misses a few run notes (E15); the SONG roll differs from the engine in 532 of about a million
  notes.

## Part R. The fourteenth review (2026-11-21)

A fourteenth cold pass, split as before (styles; song mode; takes, fills and DENSITY; harmony and
voicing; the engine and rolls), backed every finding with `jcomp-dump`, a probe program, the fuzzer
or the roll comparator. It found 59 problems (1 critical, 6 high, 26 medium, 26 low); these were
fixed in 0.16.0, with tests where a test could hold them, except those noted at the end.

- **Styles.** WEIRD's riff art and the backbeat ring were still decided stroke by stroke; a quarter
  pulse takes one decision, a riff's strokes are chopped or halved, never rung. The README's Calypso
  Piano row lists its busier chorus and quiet one and four. The House M1 riff and the disco Rhodes
  figure (`x..x..x...x..x..`, rows D3 and H1) are 3-3-4-3-3, not 3-3-2; relabelled. Mambo could go a
  whole song without its guajeo; it always plays it at its climax, and the Cha-Cha plays one there
  sometimes, unpushed.
- **Song mode and transport.** With CHORDS FROM BOTH and *Latch* (both defaults) a tap's latch end
  was an absolute tick never cleared, so a tap at beat 30 replaced the first bars of the next
  playback; a start or a jump clears it, and a tap while stopped plays to the first change. A 2/4
  project under a 4/4 style was read as a pickup (the 0.15.0 rule); a short first bar is a pickup
  only when the bar after it is in the song's meter. HOLD CHORD in SONG following the host restarted
  its section at each wrap and, let go, played the held section's chords out of place until the bar
  line; the song stays put and comes in at the bar line. PREV, RESTART or a tile in an entry's last
  80 ms acted on the next entry, and a second NEXT after a cued switch was swallowed; they act on the
  entry heard. A mid-bar start skipped VARY's swap; HOLD pressed while stopped held the last
  playback's chord; both fixed.
- **Takes, fills and DENSITY.** *shift* moved on-beat chords (116 of 304 shift takes) and *sparser*
  dropped backbeat accents and the one drop's skank; neither touches an accent, a bar's first chord
  or a 2-and-4 take. *restrike* re-struck ambient drifts in cascades; once per chord, never in
  Ambient. 649 pairs of identical takes over six seeds; none, with a fallback walk over every fitting
  mutation and a new *voicing* mutation (a stroke or two in the 2nd to 5th ranked voicing). *accents*
  and *chop it* touched the left hand, and *let it ring* and *chop it* broke the one-touch pulse;
  fixed. LENGTH just below the middle turned the push tie off and added strikes; the tie shortens
  with LENGTH instead. DENSITY filled the fill down's rest and its first notch thinned a quarter of a
  sixteenth groove; neither now. MUTATE THIS TAKE on a fill or an ending broke them; it mutates them
  by their role. A line style near silence shrank to a note a bar and lost its bass, and endings
  ignored the line; both fixed, as are the palm-wine fill and ending, the one drop's ghosts, and the
  3/4 and 6/8 grids of restrike, the swell and the push ties.
- **Harmony.** 4THS voiced m9 and m11 without their minor third (the Ambient tonic in every key),
  claiming the So What stack, which in fact keeps it (E A D G B); every stack now has the third and
  seventh, colour after them, and a chord no stack holds is SPREAD. 4THS slash chords lost the root or
  put a note under the bass, and it dropped the 7th of 7b9 and maj9; fixed. GRIP at four strings lost
  the 7#9's, 7b9's, 13's and maj9's colour to the 0.15.0 reach grips, picked 20 grips needing five
  fingers, and cut open G7 and B7 with muted inner strings; grips are built from the naming tones,
  fingers are counted, and cuts take neighbouring strings. Spellings: Dbm7 in G is C#m7, C#7/F is
  C#7/E#, raised spellings follow the scale (Cdim7 in A minor), and held So What stacks are m11.
  OCTAVES' 7#9 rubbed against its doubled third, SHELL dropped add9's 9th and three-voice altered
  tones, ROOTLESS 13 at three voices had no 13th, and the DROP 2+4 slash fallback doubled the bass;
  all fixed. TRY INSTEAD ignored the key and the next chord; each substitute is offered where it fits.
- **The engine and rolls.** The SONG roll chain missed runs into the next entry (about 1,400 of the
  1,660 SONG roll-only notes) and a fill's roll never drew its run into the next section; the take
  and the fill are previewed a bar longer. The engine treated every entry boundary as a change; only
  where the harmony changes, or the song ends. The roll left out the change strike on an entry's
  downbeat, rang SPREAD's low note past a HOLD slot, landed OCTAVES' line an octave off after a run,
  and couldn't start a run on step 0; all fixed. A note placed in the past had no length, and strums
  at fast tempos overran their gap; fixed. The engine voiced an ACC band hit and a run landing on a
  band hit without the stroke's own voicing pick, which the roll used; it follows the roll (MARKS
  106 differing cases to 73, SONG chains 436 to 0, fills 344 to 68).
- **Left as judgement or for later.** An ACC pushed by a top note (E08), a late chord's flam of its
  common tones (E12), a change strike's double trigger of a bass key (E13) and a run into a band hit
  that is itself a push (E07) are left; the pad layer on the comp's own channel shares keys, as the
  README says (E15). C7/G at three voices stays G C E (G Bb E would be an E diminished over G); C F
  Bb stays Fsus/C, the shape of G C F's Csus/G; Db13 at five voices is four notes, 1-b7-3-13.

## Part S. The fifteenth review (2026-11-21)

A fifteenth cold pass, split as before (styles; song mode; takes, fills and DENSITY; harmony and
voicing; the engine and rolls), backed every finding with `jcomp-dump`, a probe program, two fuzzers
or the roll comparator. It found 69 problems (7 high, 29 medium, 33 low, some found twice); these
were fixed in 0.17.0, with tests where a test could hold them, except those noted at the end.

- **Styles.** The 0.16.0 *voicing* mutation was the only variation in the Ambient styles and leapt
  9 to 19 semitones between bars (it took the 2nd to 5th ranked voicing, often an octave copy or one
  with fewer notes); a pick is now a neighbour of the best voicing, same count and register, and
  Ambient takes vary by a soft re-strike late in the bar (*come round*). The Cha-Cha's climax guajeo
  was one repeated rootless block on the Jazz Comper; it plays on the Montuno player, in octaves, and
  the README no longer calls its anticipated and-of-4 "unpushed". The one-decision pulse missed beats
  longer than a step. Calypso Strum's mask couldn't reach its quiet one and four; Boom-Chick and Half
  Notes played one grid in every section; fixed figures (chop, bell, montuno) ended on generic
  quarters and now carry their bar into the ending. A chorus picked the sparsest groove up to 15% of
  the time; never now. A blend with a clave genre keeps one clave. Left-hand styles ended on a
  six-four (the Shout, Organ Bubble) or with the thumb's fifth ringing under the last chord
  (Palm-Wine, Two-Step); the left hand's root sounds under the final chord and the hand stops there.
- **Song mode and transport.** A line take struck block chords on its changes only after some chord
  had sounded, so the music depended on where playback started; a chord sounded is no longer the
  test. AUTO FILLS never filled a section shorter than its setting (Every 8, the default, never
  filled a 4-bar intro); such a section counts the host's bars. A late tile restarted the section
  after a bar of the next entry, and a late PREV or RESTART played the next entry's first bar first;
  they act as if pressed at the bar line (a snapshot is kept there). Latch outlived a cued switch;
  the pad layer's voice leading survived a reset, so a loop's first pass differed; a 3/4 project
  under a 4/4 style rested a bar on every playback; VARY under SAME DICE 1 swapped every bar or never;
  HOLD couldn't be pressed again during its let-go and could add a pass running free; NEXT in an
  odd-length two-bar-clave entry added a bar; AUTO with PLAY on ran free in a pre-roll; a start inside
  a held chord was silent for seconds. All fixed.
- **Takes, fills and DENSITY.** In 20 styles takes differed only by a few softer strokes (1,725 such
  takes over four seeds, 445 now); fixed figures vary by *lean* (their offbeats lighter) and
  voicing, broken chords by *turn*, and mutate prefers the change heard most. *accents* made Freddie
  Green's 2 and 4 softer than 1 and 3 and moved backbeat accents onto offbeats; neither now.
  MUTATE THIS TAKE did nothing on a fill down in 55 styles or an ending in held styles; it finds a
  change, or says there is none. *restrike* stuttered on chords that weren't held; it needs a beat of
  chord either side. Fill mutations filled the rest before the lead-in, the MUTATE litter repeated
  itself and the takes, *sparser* split a quarter pulse, and *shift* could move an accented push; all
  fixed. Below the middle of LENGTH the push tie stayed full from 0.3 to 0.49, fell to a stab and a
  silent bar below, and depended on tempo (it edited note-offs already sent); it is decided when the
  push is struck, scaled smoothly. OCTAVES montuno endings landed on the 3rd or 5th; on the root.
- **Harmony.** 4THS, keeping root, 3rd and 7th and then colour, left no fourth in nearly every 9th
  chord at VOICES 4 (C9 as C E Bb D); a rootless level comes before the fourths are given up (Bb E A
  D, Levine's left-hand voicing), every 4THS voicing has a perfect fourth, triads are D G C E, and a
  7b9 takes its b13 (C Bb E Ab Db) instead of falling to SPREAD. SHELL at three voices dropped a 7b9's
  7th and a 7#11's 3rd; every dominant is root, 3rd and 7th. SPREAD doubled a slash bass on top
  (E7/G#: G# D E G#); GRIP dropped the 13th (C13 x32335 now) and took three strings at VOICES 4; the
  drops voiced triads as CLOSE; slash chords exceeded VOICES; Bb C E G was C/Bb; a V was misspelt
  into its target (Gb9 into B7); runs off augmented chords mixed F and F#; ROOTLESS 7#11 clustered.
  TRY INSTEAD showed only its first four entries, always the unconditional ones, and offered B natural
  on a Mixolydian I; suggestions are ranked by how they lead to the next chord and filtered to the
  key and scale, and RICHER's V7 into minor is 7b9, 7#9 or 7#5.
- **The engine and rolls.** The roll missed a run landing on the next entry's downbeat, a run into a
  pushed band hit, runs under a HOLD or SHOT, and OCTAVES' step after a run; a Half ACC, a top-note
  ACC, RUNS SHORTEN off and a run removing a cutting hit were drawn wrong; all fixed (fills 56
  differing cases to 0, band hits 70 to 0, OCTAVES 42 to 0, LENGTH 0 to 0.3 several hundred to 0).
  A note struck just before a cut was cut to nothing (zero-length notes under RHYTHM FROM BOTH); a
  note struck with the cut keeps its length. The pad on the comp's channel lost keys; it takes them
  back. A push without a voicing pick of its own takes its target's.
- **Left as judgement or for later.** A run near an entry's end still reads its target from the
  leaving take (E02; 56 differing SONG-form cases); two comparator cases are untraced (E14); a late
  held chord leaves sub-millisecond notes, by design (E13). A voicing pick led from a re-voiced
  chord can still drift; a batida chord over the held thumb has one voicing, so its pick isn't
  heard; and an organ's tied repeats make a few different takes play alike. A close triad at four
  voices still doubles its top note. The Afrobeat tenor line varies only its dynamics.

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
61. MMA, Musical MIDI Accompaniment, version 25.05.0, its standard groove library (`lib/stdlib`),
    https://www.mellowood.ca/mma/
62. Impro-Visor, its style files (`styles/*.sty`), https://github.com/Impro-Visor/Impro-Visor
63. JJazzLab user guide (Yamaha styles, chord symbol interpretation, jjSwing),
    https://jjazzlab.gitbook.io/user-guide
64. "Bubblin'", *Berklee Today*, Spring 2015 (the reggae organ's left hand on the upbeats)
