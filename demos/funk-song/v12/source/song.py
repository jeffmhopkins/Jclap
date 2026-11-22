"""A short funk song from JClap's own plugins, rendered offline.

JDrummer (the real plugin, hosted) rolls a Funk "Tight Pocket" song with the Pocket Funk drummer and
plays an arrangement by automating its Section param. JKit (the real plugin, hosted) plays those
notes on a funk kit it generated itself with Stable Audio Open Small. JArp's Funk bass presets play
root-and-b7 chords into JSynth's Finger Bass. Then a simple mix to MP3.

    python3 song.py <tools dir> <work dir> <kit state> <out.mp3>
"""
import subprocess, sys, os
import numpy as np

TOOLS, W, KIT, OUT = sys.argv[1:5]
os.makedirs(W, exist_ok=True)
BPM, SR = 100, 48000
BAR = 4 * 60 / BPM
DRUM_SEED = 14
BASS_PATCH = 'Finger Bass'  # JSynth's bass guitar
EP_PATCH = 'Tine Piano'     # JSynth's electric piano

# bar, JDrummer section (0 intro, 1 verse, 3 chorus, 5 break, 6 outro), then the end.
ARR = [(0, 0), (4, 1), (12, 3), (20, 1), (28, 5), (32, 3), (40, 6), (44, -1)]
END = ARR[-1][0]

# Chords held into JArp, root and b7 (JArp's funk presets want those), in E.
E, G, A, B = [52, 62], [55, 65], [57, 67], [59, 69]
VERSE = [(0, E), (6, G), (7, A)]
# The chorus starts on the one: E, A, E, then the turn on B (I IV I V).
CHORUS = [(0, E), (2, A), (4, E), (6, B)]
# (start bar, bars, JArp preset, chords relative to the start)
BASS = [
    # The intro's line carries on through verse 1 when the keys come in: one run of the preset.
    # (Funk Bass, not On the One, whose space after every downbeat left a hole in each bar.)
    (0, 12, 'Funk Bass', [(0, E)] + [(4 + b, k) for b, k in VERSE]),
    (12, 8, 'Slap Pops', CHORUS),
    (20, 8, 'Funk Bass', VERSE),
    (28, 4, 'On the One', [(0, E)]),
    (32, 8, 'Slap Pops', CHORUS),
    (40, 4, 'Funk Bass', [(0, E)]),
]

def run(*a, **k):
    return subprocess.run([str(x) for x in a], check=True, capture_output=True, text=True, **k).stdout

# ---- drums: JDrummer -> notes -> JKit ----
open(f'{W}/arrangement.txt', 'w').write(''.join(f'{b} {s}\n' for b, s in ARR))
drums = run(os.environ.get('JDRUMMER', f'{TOOLS}/jdrummer-funk'), 'Tight Pocket', BPM, DRUM_SEED, f'{W}/arrangement.txt')
# The last downbeat: a kick and a crash to end on.
t_end = END * BAR
drums += f'{t_end:.6f} 153 36 110\n{t_end:.6f} 153 49 110\n{t_end + 0.2:.6f} 137 36 0\n{t_end + 0.2:.6f} 137 49 0\n'
open(f'{W}/drums.txt', 'w').write(drums)
total = t_end + 3.5
# JKit's own reverb: Plate (type 2), size 0.5, decay 1.5 s, return -2 dB; the sends are on the pads.
run(f'{TOOLS}/jkit-funk', 'render', KIT, f'{W}/drums.txt', f'{W}/drums.wav', f'{total:.3f}', '4=2', '5=0.5', '6=1.5', '7=-2')

# ---- bass: JArp -> notes -> JSynth ----
notes = []
for start, bars, preset, chords in BASS:
    args = [f'{b}:{",".join(map(str, k))}' for b, k in chords]
    out = run(f'{TOOLS}/jarp-bass', preset, BPM, f'{start * BAR:.6f}', *args, bars)
    # Drop the tool's end marker, and any note-off past the segment's end (the next one takes over).
    for line in out.splitlines():
        t, st, d1, d2 = line.split()
        if int(d1) == 0 and int(st) == 128:
            continue
        notes.append(line)
notes.append(f'{t_end:.6f} 144 40 110')   # E, on the last downbeat
notes.append(f'{t_end + 1.6:.6f} 128 40 0')
open(f'{W}/bass.txt', 'w').write('\n'.join(notes) + '\n')
run(f'{TOOLS}/jsynth-notes', f'{W}/bass.txt', f'{W}/bass.wav', BASS_PATCH, f'{total:.3f}')

# ---- e-piano: a funk comp written out, into JSynth Tine Piano ----
# JArp's comping presets were too even for funk, so the keys are scored here: syncopated sixteenth
# stabs with ghosted chops, 9th and 13th voicings swapping, and slides in from a half step below.
# Patterns are (step, length in sixteenths, voicing, feel): feel 'a' accent, 'n' normal, 'g' ghost.
V = {
    'E9': [56, 62, 66, 71], 'E13': [56, 62, 66, 73], 'D#9': [55, 61, 65, 70],
    'G9': [59, 65, 69, 74], 'G13': [59, 65, 69, 76], 'F#9': [58, 64, 68, 73],
    'A9': [61, 67, 71, 76], 'A13': [61, 67, 71, 78], 'G#9': [60, 66, 70, 75],
    'B7#9': [63, 69, 74], 'B7': [63, 69, 71], 'A#7#9': [62, 68, 73],
}
VEL = {'a': 108, 'n': 88, 'g': 52}
def pat_a(c, c13, low): return [(0, 1, c, 'a'), (2, 1, c, 'g'), (3, 2, c, 'a'), (6, 1, c13, 'n'), (7, 1, c, 'g'),
                                 (10, 2, c, 'a'), (14, 1, low, 'n'), (15, 1, c, 'a')]
def pat_b(c, c13): return [(1, 1, c, 'n'), (3, 1, c13, 'a'), (4, 2, c, 'n'), (8, 1, c, 'g'), (9, 1, c, 'n'),
                           (11, 2, c13, 'a'), (14, 1, c, 'n')]
def pat_c(c, c13): return [(2, 1, c, 'a'), (5, 1, c, 'g'), (6, 1, c13, 'a'), (7, 1, c, 'g'), (10, 1, c, 'a'),
                           (12, 1, c, 'g'), (14, 1, c13, 'a'), (15, 1, c, 'n')]
def pat_d(c, c13, low): return [(0, 2, c, 'a'), (3, 1, c, 'n'), (6, 1, c13, 'a'), (9, 2, c, 'a'), (12, 1, c, 'g'),
                                (13, 1, low, 'n'), (14, 2, c, 'a')]
EA, EB = pat_a('E9', 'E13', 'D#9'), pat_b('E9', 'E13')
VERSE_KEYS = [EA, EB, EA, pat_b('E13', 'E9'), EA, EB, pat_a('G9', 'G13', 'F#9'), pat_d('A9', 'A13', 'G#9')]
CHORUS_KEYS = [pat_c('E9', 'E13'), pat_d('E9', 'E13', 'D#9'), pat_c('A9', 'A13'), pat_d('A9', 'A13', 'G#9'),
               pat_c('E13', 'E9'), pat_d('E9', 'E13', 'D#9'), pat_c('B7#9', 'B7'), pat_d('B7#9', 'B7', 'A#7#9')]
BREAK_KEYS = [[(0, 4, 'E9', 'a')], [(14, 1, 'D#9', 'n'), (15, 1, 'E9', 'a')], [(0, 4, 'E13', 'a')],
              [(10, 1, 'E9', 'g'), (12, 1, 'E9', 'n'), (14, 1, 'D#9', 'n'), (15, 1, 'E9', 'a')]]
OUTRO_KEYS = [EA, EB, EA, pat_b('E13', 'E9')]
# Every section its own part. Verse 1 lays back (a stab on the "and" of one, a held chord, a chop);
# verse 2 is the busy chopping comp; chorus 1 stabs the off-beats; chorus 2 goes up an octave with
# sixteenth chops and a two-bar answer; the outro thins back to verse 1's feel.
for name in list(V):
    V[name + '^'] = [k + 12 for k in V[name]]
def pat_e(c, c13, low): return [(2, 1, c, 'a'), (4, 4, c13, 'n'), (11, 1, c, 'g'), (12, 2, c, 'a'), (15, 1, low, 'g')]
def pat_e2(c, c13): return [(0, 1, c, 'n'), (6, 1, c, 'a'), (8, 6, c13, 'n')]
def pat_f(c, c13): return [(0, 1, c, 'a'), (1, 1, c, 'g'), (2, 1, c, 'g'), (3, 1, c13, 'a'), (6, 1, c, 'a'),
                           (8, 1, c, 'g'), (9, 1, c13, 'a'), (11, 1, c, 'g'), (12, 1, c, 'a'), (14, 1, c, 'g'),
                           (15, 1, c13, 'a')]
def pat_g(c, c13, low): return [(0, 3, c13, 'a'), (4, 1, c, 'g'), (5, 1, c, 'n'), (7, 2, c, 'a'), (10, 1, c, 'g'),
                                (11, 1, low, 'n'), (12, 4, c, 'a')]
VERSE1_KEYS = [pat_e('E9', 'E13', 'D#9'), pat_e2('E9', 'E13'), pat_e('E9', 'E13', 'D#9'), pat_e2('E13', 'E9'),
               pat_e('E9', 'E13', 'D#9'), pat_e2('E9', 'E13'), pat_e('G9', 'G13', 'F#9'), pat_d('A9', 'A13', 'G#9')]
CHORUS2_KEYS = [pat_f('E9^', 'E13'), pat_g('E13', 'E9^', 'D#9'), pat_f('A9^', 'A13'), pat_g('A13', 'A9^', 'G#9'),
                pat_f('E9^', 'E13'), pat_g('E13', 'E9^', 'D#9'), pat_f('B7#9^', 'B7#9'), pat_g('B7#9', 'B7#9^', 'A#7#9')]
OUTRO_KEYS = [pat_e('E9', 'E13', 'D#9'), pat_e2('E9', 'E13'), pat_e('E13', 'E9', 'D#9'), pat_e2('E9', 'E13')]
KEYS = [(12, CHORUS_KEYS), (20, VERSE_KEYS), (28, BREAK_KEYS), (32, CHORUS2_KEYS)]
# In verse 1 and the outro the keys lock to the bass: a stab on every bass hit, as long as the bass
# note, as hard as it (the bass's ghost notes become ghost chops), the chord moving under the hits.
def follow_bass(first, last, chord_of):
    ons = {}
    hits = []
    for line in sorted(open(f'{W}/bass.txt').read().split('\n')[:-1], key=lambda l: float(l.split()[0])):
        t, st, d1, d2 = line.split(); t, st, d1, d2 = float(t), int(st), int(d1), int(d2)
        if st == 144 and d2 > 0:
            ons[d1] = (t, d2)
        elif d1 in ons:
            t0, v = ons.pop(d1)
            if first * BAR - 1e-6 <= t0 < last * BAR - 1e-6:
                hits.append((t0, max(t - t0, 0.06), v))
    out = []
    for n, (t0, ln, v) in enumerate(hits):
        bar = int(t0 / BAR + 1e-6)
        chord = chord_of(bar - first)
        step = int(round((t0 - bar * BAR) / EP_STEP)) % 16
        name = moving(chord, n, step, bass_key.get(round(t0, 4), 40))
        vel = 50 if v < 60 else min(115, v + 14)
        for j, k in enumerate(V[name]):
            out.append(f'{t0 + (0.004 * j if vel > 50 else 0):.6f} 144 {k} {vel - 4 * j}')
            out.append(f'{t0 + min(ln, 0.45):.6f} 128 {k} 0')
    return out
# The chords move under the hits rather than sitting on one shape: on the beat the 9th, then the 13th
# and the IV-over-the-root shape (A/E: the classic funk rocking), the #9 when the bass pops its octave,
# and a half step below sliding up on the last sixteenths before a beat.
V.update({'A/E': [57, 61, 64, 69], 'E7#9': [56, 62, 67, 71], 'C/G': [60, 64, 67, 72], 'G7#9': [59, 65, 70, 74],
          'D/A': [62, 66, 69, 74], 'A7#9': [61, 67, 72, 76]})
SHAPES = {'E9': ['E9', 'E13', 'A/E', 'E13', 'E7#9', 'D#9'], 'G9': ['G9', 'G13', 'C/G', 'G13', 'G7#9', 'F#9'],
          'A9': ['A9', 'A13', 'D/A', 'A13', 'A7#9', 'G#9']}
def moving(chord, n, step, bass):
    nine, thirteen, four, thirteen2, sharp9, below = SHAPES[chord]
    if step in (3, 7, 11, 15) and step % 4 == 3 and n % 3 == 2:
        return below
    if bass >= 50:
        return sharp9
    if step % 4 == 0:
        return nine
    return [thirteen, four, thirteen2, four][n % 4]
bass_key = {}
for line in open(f'{W}/bass.txt').read().split('\n')[:-1]:
    t, st, d1, d2 = line.split()
    if st == '144':
        bass_key[round(float(t), 4)] = int(d1)
EP_STEP, EP_SWING = BAR / 16, 0.52
ep = []
for start, bars in KEYS:
    for i, notes in enumerate(bars):
        for step, length, chord, feel in notes:
            t0 = (start + i) * BAR + step * EP_STEP + ((EP_SWING - 0.5) * 2 * EP_STEP if step % 2 else 0)
            for j, k in enumerate(V[chord]):
                roll = j * 0.004 if feel != 'g' else 0   # a hand's tiny roll up the chord
                ep.append(f'{t0 + roll:.6f} 144 {k} {VEL[feel] - 4 * j}')
                ep.append(f'{t0 + length * EP_STEP * (0.55 if feel == "g" else 0.8):.6f} 128 {k} 0')
ep += follow_bass(4, 12, lambda b: 'G9' if b == 6 else 'A9' if b == 7 else 'E9')
ep += follow_bass(40, 44, lambda b: 'E9')
for k in V['E9']:                     # the last chord, left to ring
    ep.append(f'{t_end:.6f} 144 {k} 96'); ep.append(f'{t_end + 2.5:.6f} 128 {k} 0')
open(f'{W}/epiano.txt', 'w').write('\n'.join(ep) + '\n')
run(f'{TOOLS}/jsynth-notes', f'{W}/epiano.txt', f'{W}/epiano.wav', EP_PATCH, f'{total:.3f}')

# ---- lead: JBreath's Alien, singing "I like to funk, and funk likes me too" on repeat ----
# The line is nine syllables, a syllable a note, so
# every phrase below is one line of nine notes over two bars, and each section has its own melody
# for it: the verse's, the lift into the chorus, the chorus hook (on A, on E, and its turn on B), the
# break's chant and the outro's fall. Talk is down so a held note keeps its syllable.
# Phrases are (step, length, key) in sixteenths across two bars; E dorian.
LEAD_PRESET = 'Alien Speech'
LYRICS = 'I like to funk, and funk likes me too'
LEAD_PARAMS = ['Alien Words=1', 'lyrics=' + LYRICS, 'Breath Source=0', 'Alien Shift=-0.2', 'Alien Talk=0.1', 'Alien Pace=0', 'Unison=1',
               'Unison Detune=10', 'Room=0.45', 'Room Size=0.7', 'Room Decay=2.6', 'Delay=1', 'Delay Mix=0.14',
               'tempo=%d' % BPM,
               # Unison only in the middle: the break (bars 29-32) doubles the voice, then it's single again.
               '@%.3f:Unison=2' % (28 * BAR - 0.05), '@%.3f:Unison=1' % (32 * BAR - 0.05)]
#                I            like         to           funk         and           funk          likes         me            too
VERSE_L  = [(2, 2, 67), (4, 2, 69), (6, 2, 67), (8, 6, 71), (14, 2, 69), (16, 6, 71), (22, 2, 69), (24, 4, 67), (28, 4, 64)]
LIFT_L   = [(0, 2, 74), (2, 2, 74), (4, 2, 71), (6, 4, 74), (10, 2, 71), (12, 4, 74), (16, 4, 73), (20, 4, 76), (24, 8, 76)]
HOOK_A   = [(0, 2, 76), (2, 4, 76), (6, 2, 73), (8, 8, 69), (16, 2, 67), (18, 4, 69), (22, 2, 73), (24, 4, 76), (28, 4, 76)]
HOOK_E   = [(0, 2, 74), (2, 4, 74), (6, 2, 71), (8, 6, 67), (14, 2, 69), (16, 6, 71), (22, 2, 69), (24, 4, 67), (28, 4, 64)]
TURN_B   = [(0, 2, 75), (2, 4, 75), (6, 2, 71), (8, 8, 69), (16, 2, 66), (18, 4, 69), (22, 2, 71), (24, 4, 75), (28, 4, 76)]
CHANT_L  = [(0, 2, 76), (2, 2, 76), (4, 2, 74), (6, 6, 71), (14, 2, 71), (16, 4, 76), (20, 2, 74), (22, 2, 71), (24, 8, 71)]
OUTRO_L  = [(0, 4, 71), (4, 4, 69), (8, 4, 67), (12, 4, 64), (16, 2, 67), (18, 4, 69), (22, 2, 67), (24, 4, 67), (28, 8, 64)]
CHORUS_L = [HOOK_A, HOOK_E, HOOK_A, TURN_B]
# (first bar, phrases of two bars each)
# How a funk vocal line goes, and so how these are written:
# - The one belongs to the kick and bass. The voice comes in just after it, on the "and" or the "a",
#   and pushes its strong word a sixteenth early (onto the "a" of 2, ahead of beat 3).
# - Short punchy fragments, then space for the band to answer: call and response. Each nine-syllable
#   line is cut in two, four syllables in the first bar and five in the third, with bars 2 and 4
#   left to the band.
# - Narrow and speech-like: repeated notes, the E minor pentatonic (E G A B D), the b7 for colour,
#   a range of about a sixth. Short notes, with only the last word of a fragment held.
# Fragments are (step, length, key) in sixteenths of one bar, written an octave up.
FRAG_A = [2, 3, 5, 7]          # "I like to FUNK": in on the "and" of 1, the strong word pushed
FRAG_A_LEN = [1, 1, 2, 4]
FRAG_B = [2, 4, 5, 7, 10]      # "and FUNK likes ME TOO"
FRAG_B_LEN = [1, 1, 2, 2, 5]
def line(keys_a, keys_b):
    return ([(st, ln, k) for st, ln, k in zip(FRAG_A, FRAG_A_LEN, keys_a)],
            [(st, ln, k) for st, ln, k in zip(FRAG_B, FRAG_B_LEN, keys_b)])
HOOK = 'I like to funk, and funk likes me too'
CHORUS = [(0, line([71, 71, 69, 74], [69, 71, 69, 67, 64]), HOOK),                              # E | A
          (4, line([71, 71, 69, 67], [66, 69, 69, 70, 71]), 'So come on down and get on the one')]  # E | B7
LINES = ([(12 + b, m, w) for b, m, w in CHORUS]
         + [(20, line([64, 64, 67, 64], [67, 69, 67, 64, 62]), 'Keys are talking and the drums reply'),
            (24, line([64, 64, 67, 69], [67, 67, 69, 71, 74]), 'Shake your shoulders, let the pocket ride'),
            (28, line([74, 74, 71, 69], [74, 74, 71, 69, 64]), 'Break it down now, break it down for me')]
         + [(32 + b, m, w) for b, m, w in CHORUS]
         + [(40, line([71, 69, 67, 64], [67, 67, 69, 67, 64]), 'Yeah, funk likes me and I like it too')])
LEAD = [(b, [a, [], z, []]) for b, (a, z), w in LINES]
LYRICS = '\n'.join(w for b, m, w in LINES)
LEAD_PARAMS[1] = 'lyrics=' + LYRICS
# Sung an octave below where it's written: E3 to E4, a man's range.
LEAD = [(b, [[(st, ln, k - 12) for st, ln, k in ph] for ph in phrases]) for b, phrases in LEAD]
STEP = BAR / 16
SWING = 0.52            # as the drums: every other sixteenth a little late
def at(bar, step):
    late = (SWING - 0.5) * 2 * STEP if step % 2 else 0
    return bar * BAR + step * STEP + late
# Played as an MPE singer: each note on channel 2 with its own breath from channel pressure, sent
# every 10 ms. JBreath's BREATH FROM is on Auto, so the velocity only tongues the note and the
# pressure blows it: a quick rise into the note, the strong words pushed harder, a held note dipping
# and then swelling into its end (the funk "push"), and the breath falling away as it's let go.
CH = 1
rng_p = np.random.default_rng(7)
def breath(t0, dur, strong):
    pk = 0.9 if strong else 0.72
    out, t = [], 0.0
    while t <= dur:
        u = t / dur
        if t < 0.03:
            p = 0.35 + (pk - 0.35) * t / 0.03
        elif dur < 0.25:
            p = pk - (pk - 0.5) * max(0.0, (u - 0.6) / 0.4)
        else:
            dip = 0.75 * pk
            if u < 0.35:
                p = pk + (dip - pk) * (u - 0.03 / dur) / (0.35 - 0.03 / dur)
            elif u < 0.85:
                p = dip + (min(1.0, pk * 1.08) - dip) * (u - 0.35) / 0.5
            else:
                p = min(1.0, pk * 1.08) - (min(1.0, pk * 1.08) - 0.45) * (u - 0.85) / 0.15
        p = min(1.0, max(0.0, p + rng_p.normal(0, 0.015)))
        out.append(f'{t0 + 0.001 + t:.6f} {0xD0 | CH} {int(round(p * 127))} 0')
        t += 0.01
    return out
# And the MPE pitch bend (channel 2 is an MPE note channel, so its bend range is JBreath's 48
# semitones): every strong word scoops up into its pitch from 60 cents under, a held note grows a
# vibrato (5.5 Hz, up to 25 cents) after its first third, and the held last word of a fragment falls
# off its pitch at the end, as a singer drops a phrase's last word.
def bend_msg(t, semis):
    v = int(round(8192 + semis / 48.0 * 8192)); v = min(16383, max(0, v))
    return f'{t:.6f} {0xE0 | CH} {v & 127} {v >> 7}'
def bends(t0, dur, strong, last):
    out, t = [], 0.0
    while t <= dur:
        b = -0.6 * np.exp(-t / 0.03) if strong else 0.0
        if dur > 0.3 and t > dur / 3:
            depth = 0.25 * min(1.0, (t - dur / 3) / (dur / 3))
            b += depth * np.sin(2 * np.pi * 5.5 * (t - dur / 3))
        if last and t > dur - 0.12:
            b -= 1.6 * ((t - (dur - 0.12)) / 0.12) ** 2
        out.append(bend_msg(t0 + 0.001 + t, b))
        t += 0.01
    return out
lead = []
for start, phrases in LEAD:
    for i, notes in enumerate(phrases):
        for step, length, key in notes:
            t0 = at(start + i, step)
            strong = length >= 2
            vel = 112 if strong else 92
            dur = length * STEP * (0.75 if length == 1 else 0.9)
            lead.append(f'{t0 - 0.002:.6f} {0xE0 | CH} 0 64')        # bend back to centre first
            lead.append(f'{t0:.6f} {0x90 | CH} {key} {vel}')
            lead += breath(t0, dur, strong)
            lead += bends(t0, dur, strong, length >= 4 and (step, length, key) == notes[-1])
            lead.append(f'{t0 + dur:.6f} {0x80 | CH} {key} 0')
            lead.append(f'{t0 + dur + 0.002:.6f} {0xD0 | CH} 0 0')
open(f'{W}/lead.txt', 'w').write('\n'.join(lead) + '\n')
run(f'{TOOLS}/jbreath-lead', LEAD_PRESET, f'{W}/lead.txt', f'{W}/lead.wav', f'{total:.3f}', *LEAD_PARAMS)

# ---- mix ----
def wav(path):
    d = open(path, 'rb').read()
    i = d.find(b'data')
    x = np.frombuffer(d[i + 8:], dtype='<f4').reshape(-1, 2).astype(np.float64)
    return x

dr, bs, kp, ld = wav(f'{W}/drums.wav'), wav(f'{W}/bass.wav'), wav(f'{W}/epiano.wav'), wav(f'{W}/lead.wav')
n = min(len(dr), len(bs), len(kp), len(ld))
dr, bs, kp, ld = dr[:n], bs[:n], kp[:n], ld[:n]
def rms(x): return np.sqrt(np.mean(x ** 2) + 1e-12)
print('drums rms %.4f peak %.3f, bass rms %.4f peak %.3f' % (rms(dr), abs(dr).max(), rms(bs), abs(bs).max()))
# Bass a touch under the drums' level, centred.
bs = bs * (0.85 * rms(dr) / rms(bs))
# E-piano under the bass, a little right (the hats are left), with a short plate-like tail of
# decaying noise so it sits in the same space as the kit.
kp = kp * (0.55 * rms(dr) / rms(kp))
rng = np.random.default_rng(1)
tl = np.arange(int(1.4 * SR)) / SR
ir = rng.standard_normal((len(tl), 2)) * np.exp(-tl / 0.35)[:, None]
ir /= np.sqrt((ir ** 2).sum(0))
from numpy.fft import rfft, irfft
L = 1 << int(np.ceil(np.log2(n + len(ir))))
wet = np.stack([irfft(rfft(kp[:, c], L) * rfft(ir[:, c], L), L)[:n] for c in (0, 1)], 1)
kp = kp + wet * 0.35
kp = kp * np.array([0.85, 1.0])
# The lead on top, centred: measured over the bars it sings.
sung = np.abs(ld).max(1) > 1e-3
ld = ld * (0.75 * rms(dr) / np.sqrt(np.mean(ld[sung] ** 2)))
mix = dr + bs + kp + ld
# Gentle bus glue: a soft clip after bringing the peak to about -1 dB.
mix = mix / abs(mix).max() * 1.3
mix = np.tanh(mix) / np.tanh(1.3) * 0.89
# Fade the tail. (ffmpeg then lifts the top a little, +1.5 dB above 6 kHz, and limits.)
fade = int(2.5 * SR)
mix[-fade:] *= np.linspace(1, 0, fade)[:, None] ** 2
pcm = (np.clip(mix, -1, 1) * 32767).astype('<i2')
import wave
with wave.open(f'{W}/mix.wav', 'wb') as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', f'{W}/mix.wav', '-af', 'treble=g=1.5:f=6000,alimiter=limit=0.95', '-b:a', '256k', OUT], check=True)
print('wrote', OUT, '%.1f s' % (n / SR))
