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
run(f'{TOOLS}/jkit-funk', 'render', KIT, f'{W}/drums.txt', f'{W}/drums.wav', f'{total:.3f}')

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

# ---- mix ----
def wav(path):
    d = open(path, 'rb').read()
    i = d.find(b'data')
    x = np.frombuffer(d[i + 8:], dtype='<f4').reshape(-1, 2).astype(np.float64)
    return x

dr, bs = wav(f'{W}/drums.wav'), wav(f'{W}/bass.wav')
n = min(len(dr), len(bs))
dr, bs = dr[:n], bs[:n]
def rms(x): return np.sqrt(np.mean(x ** 2) + 1e-12)
print('drums rms %.4f peak %.3f, bass rms %.4f peak %.3f' % (rms(dr), abs(dr).max(), rms(bs), abs(bs).max()))
# Bass a touch under the drums' level, centred.
bs = bs * (0.85 * rms(dr) / rms(bs))
mix = dr + bs
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
