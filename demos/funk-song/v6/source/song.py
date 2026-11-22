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
CHORUS = [(0, A), (2, E), (4, A), (6, B)]
# (start bar, bars, JArp preset, chords relative to the start)
BASS = [
    (0, 4, 'On the One', [(0, E)]),
    (4, 8, 'Funk Bass', VERSE),
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
CHORUS_KEYS = [pat_c('A9', 'A13'), pat_d('A9', 'A13', 'G#9'), pat_c('E9', 'E13'), pat_d('E9', 'E13', 'D#9'),
               pat_c('A13', 'A9'), pat_d('A9', 'A13', 'G#9'), pat_c('B7#9', 'B7'), pat_d('B7#9', 'B7', 'A#7#9')]
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
CHORUS2_KEYS = [pat_f('A9^', 'A13'), pat_g('A13', 'A9^', 'G#9'), pat_f('E9^', 'E13'), pat_g('E13', 'E9^', 'D#9'),
                pat_f('A9^', 'A13'), pat_g('A13', 'A9^', 'G#9'), pat_f('B7#9^', 'B7#9'), pat_g('B7#9', 'B7#9^', 'A#7#9')]
OUTRO_KEYS = [pat_e('E9', 'E13', 'D#9'), pat_e2('E9', 'E13'), pat_e('E13', 'E9', 'D#9'), pat_e2('E9', 'E13')]
KEYS = [(4, VERSE1_KEYS), (12, CHORUS_KEYS), (20, VERSE_KEYS), (28, BREAK_KEYS), (32, CHORUS2_KEYS), (40, OUTRO_KEYS)]
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
for k in V['E9']:                     # the last chord, left to ring
    ep.append(f'{t_end:.6f} 144 {k} 96'); ep.append(f'{t_end + 2.5:.6f} 128 {k} 0')
open(f'{W}/epiano.txt', 'w').write('\n'.join(ep) + '\n')
run(f'{TOOLS}/jsynth-notes', f'{W}/epiano.txt', f'{W}/epiano.wav', EP_PATCH, f'{total:.3f}')

# ---- lead: JBreath's Alien, singing its own words, a syllable a note ----
# It comes in once the keys have had four bars. One melody per section, each repeated so it sticks:
# the verse's two-bar phrase, the chorus's four-bar hook, the break's chant. Long notes, and Talk
# turned down so a held note keeps its syllable (the preset's 0.6 chatters through held notes).
# Bars of (step, length, key) in sixteenths; E dorian.
LEAD_PRESET = 'Alien Speech'
LEAD_PARAMS = ['Alien Talk=0.1', 'Alien Pace=0', 'Unison=2', 'Unison Detune=10', 'Room=0.45', 'Room Size=0.7',
               'Room Decay=2.6', 'Delay=1', 'Delay Mix=0.14', 'tempo=%d' % BPM]
V1 = [(2, 4, 71), (6, 2, 69), (8, 6, 67), (14, 2, 69)]
V2 = [(0, 8, 71), (10, 2, 67), (12, 4, 64)]
V2b = [(0, 6, 74), (8, 8, 71)]
LIFT_G = [(0, 4, 74), (4, 4, 71), (8, 8, 67)]
LIFT_A = [(0, 4, 69), (4, 4, 73), (8, 8, 76)]
H1 = [(0, 6, 76), (6, 2, 73), (8, 8, 69)]
H2 = [(0, 2, 67), (2, 2, 69), (4, 4, 73), (8, 8, 76)]
H3 = [(0, 6, 74), (6, 2, 71), (8, 4, 67), (12, 4, 69)]
H4 = [(0, 12, 71)]
TURN1 = [(0, 8, 75), (8, 8, 71)]
TURN2 = [(0, 4, 69), (4, 4, 66), (8, 8, 71)]
CHORUS_LEAD = [H1, H2, H3, H4, H1, H2, TURN1, TURN2]
CH1 = [(0, 4, 76), (4, 4, 74), (8, 8, 71)]
LEAD = [(8, [V1, V2, LIFT_G, LIFT_A]),
        (12, CHORUS_LEAD),
        (20, [V1, V2, V1, V2b, V1, V2, LIFT_G, LIFT_A]),
        (28, [CH1, [], CH1, [(8, 4, 71), (12, 4, 74)]]),
        (32, CHORUS_LEAD),
        (40, [V1, V2, [(0, 4, 71), (4, 4, 67), (8, 8, 64)], []])]
STEP = BAR / 16
SWING = 0.52            # as the drums: every other sixteenth a little late
def at(bar, step):
    late = (SWING - 0.5) * 2 * STEP if step % 2 else 0
    return bar * BAR + step * STEP + late
lead = []
for start, bars in LEAD:
    for i, notes in enumerate(bars):
        for step, length, key in notes:
            t0 = at(start + i, step)
            vel = 112 if step % 4 == 0 else 98
            lead.append(f'{t0:.6f} 144 {key} {vel}')
            lead.append(f'{t0 + length * STEP * 0.9:.6f} 128 {key} 0')
lead.append(f'{t_end:.6f} 144 64 110'); lead.append(f'{t_end + 2.4:.6f} 128 64 0')
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
