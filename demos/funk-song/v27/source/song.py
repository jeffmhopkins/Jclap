"""A short funk song from JClap's own plugins, rendered offline.

JDrummer (the real plugin, hosted) rolls a Funk "Tight Pocket" song with the Pocket Funk drummer and
plays an arrangement by automating its Section param. JKit (the real plugin, hosted) plays those
notes on a funk kit it generated itself with Stable Audio Open Small. JArp's Funk bass presets play
root-and-b7 chords into JSynth's Finger Bass. Then a simple mix to MP3.

    python3 song.py <tools dir> <work dir> <kit state> <out.mp3>
"""
import random, subprocess, sys, os
import numpy as np

TOOLS, W, KIT, OUT = sys.argv[1:5]
os.makedirs(W, exist_ok=True)
BPM, SR = 100, 48000
BAR = 4 * 60 / BPM
DRUM_SEED = 7   # seed 14 dropped the backbeat on 2 in the first bar of every four in the verses
BASS_PATCH = 'Finger Bass'  # JSynth's bass guitar
EP_PATCH = 'Tine Piano'     # JSynth's electric piano

# bar, JDrummer section (0 intro, 1 verse, 3 chorus, 5 break, 6 outro), then the end.
# Verse 2 is four bars so the break comes sooner, and the break is eight bars (sung twice).
ARR = [(0, 0), (4, 1), (12, 3), (20, 1), (24, 5), (32, 3), (40, 0), (46, -1)]   # the last stretch before the tag goes back to the light intro groove
# The ending: the groove stops at bar 47 for a two-bar tag the whole band plays together (stop-time
# hits, a turnaround, a fill), and lands on a last E9 with the crash at bar 49.
TAG = ARR[-1][0]
END = TAG + 2

# Chords held into JArp, root and b7 (JArp's funk presets want those), in E.
E, G, A, B = [52, 62], [55, 65], [57, 67], [59, 69]
VERSE = [(0, E), (6, G), (7, A)]
# The chorus starts on the one: E, A, E, then the turn on B (I IV I V).
CHORUS = [(0, E), (2, A), (4, E), (6, B)]
VERSE4 = [(0, E), (2, G), (3, A)]
# (start bar, bars, JArp preset, chords relative to the start)
BASS = [
    (0, 4, 'On the One', [(0, E)]),
    (4, 8, 'Funk Bass', VERSE),
    (12, 8, 'Slap Pops', CHORUS),
    (20, 4, 'Funk Bass', VERSE4),
    (24, 8, 'On the One', [(0, E)]),
    (32, 8, 'Slap Pops', CHORUS),
    (40, 6, 'Funk Bass', [(0, E)]),
]

def run(*a, **k):
    return subprocess.run([str(x) for x in a], check=True, capture_output=True, text=True, **k).stdout

# ---- drums: JDrummer -> notes -> JKit ----
open(f'{W}/arrangement.txt', 'w').write(''.join(f'{b} {s}\n' for b, s in ARR))
drums = run(os.environ.get('JDRUMMER', f'{TOOLS}/jdrummer-funk'), 'Tight Pocket', BPM, DRUM_SEED, f'{W}/arrangement.txt')
# The last downbeat: a kick and a crash to end on.
t_end = END * BAR
# The tag: (step from the tag's start, notes, velocity); kick 36, snare 38, hat 42, crash 49, toms 45 43 41.
TAG_HITS = [(0, 'E9', 40), (3, 'E9', 52), (6, 'E9', 40), (10, 'G9', 43), (12, 'A9', 45), (16, 'B7#9', 47)]
TAG_DRUMS = [(0, 36, 118), (0, 49, 112), (3, 36, 105), (3, 42, 90), (6, 38, 115), (6, 42, 95), (10, 36, 110),
             (10, 42, 90), (12, 36, 112), (12, 38, 118), (12, 42, 95), (16, 36, 118), (16, 49, 108),
             (24, 38, 70), (25, 38, 78), (26, 45, 96), (27, 45, 100), (28, 43, 104), (29, 43, 108),
             (30, 41, 112), (31, 38, 120)]
def tag_t(step):
    return TAG * BAR + step * BAR / 16 + ((0.52 - 0.5) * 2 * BAR / 16 if step % 2 else 0)
drums = ''.join(l + '\n' for l in drums.splitlines() if l.strip() and (float(l.split()[0]) < TAG * BAR - 0.02 or l.split()[1] == '137'))
# Hi-hats per section, so each part has its own: JDrummer plays the same sixteenths through the
# verses and choruses, so they're reshaped here. Intro and break as played. Verse 1: closed sixteenths.
# Chorus: the hat opens on the "and" of 2 and 4 (the chorus 2 on every "and"), the sixteenth after
# each open hat dropped so it rings. Verse 2: eighths up front, the in-between sixteenths only
# whispered, a pedal chick on 2 and 4. The bars before the tag are pared right down: a soft pedal
# hat on the beats, kick on 1 and the "and" of 3, snare on 2 and 4, nothing else.
def reshape(drums):
    out = []
    step = BAR / 16
    def add(t, k, v, ln=0.1):
        out.append(f'{t:.6f} 153 {k} {v}'); out.append(f'{t + ln:.6f} 137 {k} 0')
    for l in drums.splitlines():
        if not l.strip():
            continue
        t, st_, k, v = l.split(); tt, k, v = float(t), int(k), int(v)
        b = int((tt + 0.05) // BAR); n = int(round((tt - b * BAR) / step)) % 16
        hat = k in (42, 44, 46)
        if st_ == '137':
            if not (hat and 12 <= b < 24 or 32 <= b < 46):
                out.append(l)
            continue
        if 40 <= b < 46:
            if hat or (k == 36 and n not in (0, 10)) or (k == 38 and n not in (4, 12)) or k not in (36, 38, 42, 44, 46):
                if hat and n in (0, 4, 8, 12):
                    add(tt, 44, 62 if n % 8 else 72)
                continue
            out.append(l); continue
        if hat and (12 <= b < 20 or 32 <= b < 40):
            opens = (6, 14) if b < 20 else (2, 6, 10, 14)
            if n in opens:
                add(tt, 46, max(v, 92), 0.3)
            elif (n - 1) % 16 not in opens:
                add(tt, 42, min(127, v + 8) if n % 2 == 0 else v)
            continue
        if hat and 20 <= b < 24:
            if n % 2:
                add(tt, 42, min(v, 38))
            else:
                add(tt, 42, v)
                if n in (4, 12):
                    add(tt + 0.002, 44, 70)
            continue
        out.append(l)
    return ''.join(x + '\n' for x in out)
drums = reshape(drums)
for st_, k, v in TAG_DRUMS:
    drums += f'{tag_t(st_):.6f} 153 {k} {v}\n{tag_t(st_) + 0.15:.6f} 137 {k} 0\n'
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
TAG_LEN = {0: 0.13, 3: 0.11, 6: 0.13, 10: 0.14, 12: 0.32, 16: 0.85}   # seconds each tag hit is held
notes = [l for l in notes if float(l.split()[0]) < TAG * BAR - 1e-6]
for st_, ch, bk in TAG_HITS:
    notes.append(f'{tag_t(st_):.6f} 144 {bk} 115'); notes.append(f'{tag_t(st_) + TAG_LEN[st_]:.6f} 128 {bk} 0')
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
KEYS = [(12, CHORUS_KEYS), (20, VERSE_KEYS[:2] + VERSE_KEYS[6:]), (24, BREAK_KEYS + BREAK_KEYS), (32, CHORUS2_KEYS)]
# The bass's hits in a stretch of bars: (time, length, velocity).
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
    return hits
# Verse 1 and the outro: the keys pick out the bass's important notes rather than every hit, and
# let the chords ring between them: the downbeat (held), then the bass's loudest note above its root
# (an octave pop or the b7), and in every other bar one more. The shape moves with them: the 9th on
# the one, the 13th on a pop, the IV over the root on the b7.
V.update({'A/E': [57, 61, 64, 69], 'C/G': [60, 64, 67, 72], 'D/A': [62, 66, 69, 74]})
FOURTH = {'E9': 'A/E', 'G9': 'C/G', 'A9': 'D/A'}
# Verse 1 sits on E for six bars. So the same rhythm doesn't sound the same each time, each bar has
# its own shapes, and the shapes move more often as it goes: one move in the first bars, then two,
# then three by the fourth time round. (No half-step slides: they sounded like glissandos.)
V.update({'E9hi': [62, 66, 68, 71], 'E7#9': [56, 62, 67], 'E13hi': [62, 68, 73, 78], 'E6/9': [56, 61, 66, 71],
          'E11': [57, 62, 66, 71], 'D/E': [62, 66, 69, 74], 'B/E': [59, 63, 66, 71], 'G/E': [59, 62, 67, 71],
          'D#/E': [61, 65, 68, 73], 'C#/E': [61, 65, 68, 73]})
E_BARS = [  # (downbeat, the shapes for the moves after it, how many moves, slide in)
    ('E9', ['E13', 'A/E'], 1, False),
    ('E6/9', ['A/E', 'E9'], 2, False),
    ('E9hi', ['D/E', 'E13hi'], 2, False),
    ('E7#9', ['G/E', 'A/E', 'E9'], 3, False),
    ('E13hi', ['D/E', 'E9hi', 'A/E'], 3, False),
    ('E11', ['E9', 'D/E', 'E7#9'], 3, False),
]
DORIAN = {4, 6, 7, 9, 11, 1, 2}   # E dorian's pitch classes
THIRTEENTH = {'E9': 'E13', 'G9': 'G13', 'A9': 'A13'}
def bass_highlights(first, last, chord_of, vary=False):
    # bass hits with their keys, to find the pops (the octave) and the b7
    keys = {}
    for line in open(f'{W}/bass.txt').read().split('\n')[:-1]:
        t, st, d1, d2 = line.split()
        if st == '144' and int(d2) > 0:
            keys[round(float(t), 4)] = int(d1)
    picked = []
    by_bar = {}
    for t0, ln, v in follow_bass(first, last, chord_of):
        by_bar.setdefault(int(t0 / BAR + 1e-6), []).append((t0, v, keys.get(round(t0, 4), 40)))
    for bar, hits in sorted(by_bar.items()):
        chord = chord_of(bar - first)
        odd = (bar - first) % 2
        plan = E_BARS[bar - first] if vary and chord == 'E9' and bar - first < len(E_BARS) else None
        for t0, v, k in hits:
            step = int(round((t0 - bar * BAR) / EP_STEP)) % 16
            if step == 0:
                picked.append((t0, plan[0] if plan else chord, 108))
        # Above the root: the pops and the b7, the loudest one or two of them in the bar.
        cands = [h for h in hits if h[2] > 40 and int(round((h[0] - bar * BAR) / EP_STEP)) % 16 > 3]
        if plan and len(cands) < plan[2]:   # not enough pops: take the bass's other off-beat hits too
            cands += [h for h in hits if h not in cands and int(round((h[0] - bar * BAR) / EP_STEP)) % 16 > 3]
        tops = sorted(cands, key=lambda h: -h[1])[:plan[2] if plan else 1 + odd]
        for i, (t0, v, k) in enumerate(sorted(tops)):
            if plan:
                shape = plan[1][i % len(plan[1])]
                if plan[3] and i == len(tops) - 1 and t0 - EP_STEP > bar * BAR:
                    slide = [n - 1 for n in V[shape]]          # from a half step below
                    V['_s%s' % shape] = slide
                    picked.append((t0 - EP_STEP / 2, '_s%s' % shape, 80))
                picked.append((t0, shape, 96))
            else:
                picked.append((t0, FOURTH[chord] if k % 12 == 2 else THIRTEENTH[chord], 96))
    picked.sort()
    out = []
    # Played, not quantized: each chord lands a little early or late (mostly laid back), the hand
    # rolls it by a different amount each time (now and then from the top down), every note has its
    # own weight with the top voice a touch louder, and the chords are let go at different lengths.
    rnd = random.Random(first * 1000 + 7)
    gate, last_run = random.Random(first + 99), [-9]
    prev = None
    for i, (t0, chord, vel) in enumerate(picked):
        nxt = picked[i + 1][0] if i + 1 < len(picked) else t0 + BAR / 2
        notes_ = V[chord]
        n = len(notes_)
        room = max(0.06, nxt - t0 - 0.04)
        if not vary:
            end = t0 + min(room, 0.9)
            for j, k in enumerate(notes_):
                out.append(f'{t0 + 0.005 * j:.6f} 144 {k} {vel - 5 * j}')
                out.append(f'{end:.6f} 128 {k} 0')
            prev = chord
            continue
        push = max(-0.012, min(0.022, rnd.gauss(0.006, 0.009)))
        vel = vel + rnd.randint(-10, 6)
        # Articulation: how long a chord is held matters as much as when it's hit. Downbeats mostly
        # ring (sometimes the hand lifts the bottom and leaves the top singing); the pops are mostly
        # short chops, now and then a lazier half-length one, rarely let ring.
        a = rnd.random()
        if vel >= 98:
            kind = 'long' if a < 0.55 else 'medium' if a < 0.85 else 'stab'
        else:
            kind = 'stab' if a < 0.5 else 'medium' if a < 0.8 else 'long'
        # A passing line into the next chord now and then: two to four single sixteenths walking
        # (in E dorian, the last a half step under) up or down into the next chord's top note.
        run = []
        if i + 1 < len(picked):
            gap = int(round((nxt - t0) / EP_STEP))
            if gap >= 5 and rnd.random() < 0.5:
                m = rnd.choice([2, 3, 3, 4] if gap >= 7 else [2, 3])
                target = V[picked[i + 1][1]][-1]
                up = rnd.random() < 0.65
                seq = [target - 1 if up else target + (2 if (target + 2) % 12 in DORIAN else 1)]
                while len(seq) < m:
                    k = seq[-1] - 1 if up else seq[-1] + 1
                    while k % 12 not in DORIAN:
                        k += -1 if up else 1
                    seq.append(k)
                seq.reverse()
                for q, k in enumerate(seq):
                    st = round(nxt / EP_STEP) - (m - q)
                    tq = st * EP_STEP + ((EP_SWING - 0.5) * 2 * EP_STEP if st % 2 else 0) + rnd.uniform(-0.004, 0.01)
                    run.append((tq, k, 74 + 6 * q + rnd.randint(-6, 6)))
                # Only now and then: at most one line every two bars, and not every chance it gets.
                bar_ = int(t0 / BAR + 1e-6)
                if bar_ - last_run[0] >= 2 and gate.random() < 0.6:
                    last_run[0] = bar_
                    room = min(room, run[0][0] - t0 - push - 0.03)
                else:
                    run = []
        room = max(room, 0.06)
        length = {'long': min(room, 0.9), 'medium': min(room, rnd.uniform(0.24, 0.4)),
                  'stab': min(room, rnd.uniform(0.07, 0.12))}[kind]
        lift = kind == 'long' and rnd.random() < 0.3
        if kind == 'stab':
            vel += 6        # chops are dug in
        t = t0 + push
        for j, k in enumerate(notes_):   # all the notes of a chord together, as one hand
            v = vel - 5 * j + (6 if j == n - 1 else 0) + rnd.randint(-5, 5)
            on = t + rnd.uniform(0, 0.002)
            off = t + length * (rnd.uniform(0.4, 0.6) if lift and j < n - 1 else 1.0) + rnd.uniform(-0.01, 0.01)
            out.append(f'{on:.6f} 144 {k} {max(30, min(127, v))}')
            out.append(f'{max(on + 0.05, off):.6f} 128 {k} 0')
        for tq, k, v in run:
            out.append(f'{tq:.6f} 144 {k} {max(30, min(127, v))}')
            out.append(f'{tq + EP_STEP * rnd.uniform(0.6, 0.85):.6f} 128 {k} 0')
        prev = chord
    return out
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
ep += bass_highlights(4, 12, lambda b: 'G9' if b == 6 else 'A9' if b == 7 else 'E9', vary=True)
ep += bass_highlights(40, 46, lambda b: 'E9', vary=True)
_tr = random.Random(46)
for st_, ch, bk in TAG_HITS:                 # the tag, with the band
    t0 = tag_t(st_) + _tr.uniform(-0.004, 0.012)
    for j, k in enumerate(V[ch]):
        ep.append(f'{t0 + 0.006 * j:.6f} 144 {k} {112 - 4 * j + _tr.randint(-4, 4)}')
        ep.append(f'{t0 + TAG_LEN[st_] * _tr.uniform(0.85, 1.0):.6f} 128 {k} 0')
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
LEAD_PARAMS = ['Alien Words=1', 'lyrics=' + LYRICS, 'Breath Source=0', 'Alien Talk=0.1', 'Alien Pace=0', 'Unison=1',
               'Unison Detune=10', 'Room=0.45', 'Room Size=0.7', 'Room Decay=2.6', 'Delay=1', 'Delay Mix=0.14',
               'tempo=%d' % BPM,
               # Unison only in the middle: the break (bars 25-32) doubles the voice, then it's single again.
               '@%.3f:Unison=2' % (24 * BAR - 0.05), '@%.3f:Unison=1' % (32 * BAR - 0.05)]
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
            (24, line([74, 74, 71, 69], [74, 74, 71, 69, 64]), 'Break it down now, break it down for me'),
            (28, line([76, 76, 74, 71], [79, 76, 74, 71, 69]), 'Break it down now, break it down for me')]   # second time higher
         + [(32 + b, m, w) for b, m, w in CHORUS]
         + [(40, line([71, 69, 67, 64], [67, 67, 69, 67, 64]), 'Yeah, funk likes me and I like it too'),
            (44, line([71, 71, 74, 71], [76, 74, 71, 69, 71]), 'Yeah, funk likes me and I like it too')])   # again, higher, over the tag
LEAD = [(b, [a, [], z, []]) for b, (a, z), w in LINES]
LYRICS = '\n'.join(w for b, m, w in LINES)
LEAD_PARAMS[1] = 'lyrics=' + LYRICS
# Sung half an octave above the low octave: every note moved up three steps of the E minor pentatonic
# (E->B, G->D, A->E, B->G, D->A), which keeps the line in key; a straight 6-semitone shift would not.
PENT = [0, 3, 5, 7, 10]
def up3(k):
    o, pc = divmod(k - 4, 12)
    if pc in PENT:
        i = PENT.index(pc) + 3
        return 4 + 12 * (o + i // 5) + PENT[i % 5]
    return up3(k - 1) + 1 if pc - 1 in PENT else up3(k + 1) - 1   # passing notes keep their half step
LEAD = [(b, [[(st, ln, up3(k) - 12) for st, ln, k in ph] for ph in phrases]) for b, phrases in LEAD]
# "and get on the one" over B7: moved up it would land on F natural, so sing it on the chord instead.
for b, phrases in LEAD:
    if b in (16, 36):
        phrases[2] = [(st, ln, k) for (st, ln, _), k in zip(phrases[2], [64, 66, 66, 69, 66])]
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
# More echo on the voice through the break, from the dropout exactly. Automating JBreath's delay
# lagged by seconds, so the voice is rendered again with the wetter echo and that take is used for
# the break, crossfaded in at bar 25 and back out as the chorus comes in.
run(f'{TOOLS}/jbreath-lead', LEAD_PRESET, f'{W}/lead.txt', f'{W}/lead_wet.wav', f'{total:.3f}', *LEAD_PARAMS,
    'Delay Mix=0.36', 'Delay Feedback=0.55')
WET_FROM, WET_TO = 24 * BAR, 32 * BAR

# ---- mix ----
def wav(path):
    d = open(path, 'rb').read()
    i = d.find(b'data')
    x = np.frombuffer(d[i + 8:], dtype='<f4').reshape(-1, 2).astype(np.float64)
    return x

dr, bs, kp, ld = wav(f'{W}/drums.wav'), wav(f'{W}/bass.wav'), wav(f'{W}/epiano.wav'), wav(f'{W}/lead.wav')
_wet = wav(f'{W}/lead_wet.wav')
_m = min(len(ld), len(_wet)); ld = ld[:_m].copy()
_g = np.zeros(_m)
_a, _b = int((WET_FROM - 0.05) * 44100), int(WET_FROM * 44100)
_c, _d = int((WET_TO - 0.2) * 44100), int((WET_TO + 0.2) * 44100)
_g[_a:_b] = np.linspace(0, 1, _b - _a); _g[_b:_c] = 1; _g[_c:_d] = np.linspace(1, 0, _d - _c)
ld = ld * (1 - _g[:, None]) + _wet[:_m] * _g[:, None]
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
