# Funk song v10 (100 BPM, E, 1:49)

## Bass and keys in verse 1
- The intro's bassline (JArp *On the One*) now carries on through verse 1 as one unbroken run,
  instead of switching to *Funk Bass* when the keys come in.
- In verse 1 and the outro the e-piano locks to the bass: a chord stab on every bass hit, as long and
  as hard as the bass note (its ghost notes become ghost chops), on that bar's chord. Chorus, verse 2
  and the break keep their own written parts.

## The vocal line, rewritten the way funk vocals go
- **The one is the band's.** The voice comes in just after it, on the "and", and pushes its strong
  word a sixteenth early, onto the "a" of 2.
- **Call and response.** Each line is cut in two short fragments: four syllables in the first bar,
  five in the third. Bars 2 and 4 are left to the band.
- **Narrow and speech-like.** E minor pentatonic (E G A B D) with the b7 for colour, repeated notes,
  a range of a sixth (D3 to D4). Short notes, with only the last word of a fragment held.

| Section | Line |
|---|---|
| Chorus (0:29, 1:17) | I like to *funk*, / and *funk* likes *me too* · So come on *down*, / and get on *the one* |
| Verse 2 (0:48) | Keys are *talking* / and the drums *reply* · Shake your *shoulders*, / let the pocket *ride* |
| Break (1:07, unison) | Break it down *now*, / break it down *for me* |
| Outro (1:36) | Yeah, funk likes *me* / and I like it *too* |

## v11: MPE pressure on the voice
JBreath is played as from an MPE controller: each sung note on its own channel (2) with channel
pressure every 10 ms (2,080 pressure messages). BREATH FROM is on Auto, so velocity only tongues the
note and the pressure blows it:
- a 30 ms rise into each note, the held (strong) words pushed to 0.9, the short ones to 0.72;
- a short note eases off over its last 40%;
- a held note dips to three quarters, swells past its start into its end (the funk "push"), then
  falls away to release, with a touch of random wobble so no two notes breathe alike.
`source/lead.txt` has every note and pressure message.

## v12
- **No hole in verse 1:** the intro and verse 1 bass is now one run of JArp *Funk Bass*. v10 and v11
  used *On the One* there, which leaves 0.9 s of space after every downbeat. (The break still uses
  On the One on purpose: it's the breakdown.)
- **The keys move under the bass hits:** in verse 1 and the outro each stab picks its shape by
  where it lands: the 9th on the beat, the 13th and the IV-over-the-root (A/E, C/G, D/A) rocking on
  the off-beats, the #9 when the bass pops its octave, and a slide from a half step below before a beat.
- **MPE pitch bend on the voice** (channel 2, JBreath's 48-semitone MPE range): strong words scoop
  up from 60 cents under, held notes grow a 5.5 Hz vibrato up to 25 cents after their first third,
  and each fragment's held last word falls off at the end.
- **A bigger throat:** Alien Shift (SIZE) -0.2, every formant a little lower, a bigger voice.
