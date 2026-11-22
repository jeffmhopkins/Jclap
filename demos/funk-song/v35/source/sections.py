"""Per-section spectrum of an MP3: log-log slope, 2-8 kHz slope, octave bands relative to 1 kHz."""
import numpy as np, subprocess, sys
B = 2.4
SECTIONS = [('intro', 0, 4), ('verse 1', 4, 12), ('chorus 1', 12, 20), ('verse 2', 20, 24), ('break', 24, 32),
            ('chorus 2', 32, 40), ('pre-tag', 40, 46), ('tag', 46, 49)]
raw = subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', sys.argv[1], '-ac', '1', '-ar', '44100', '-f', 'f32le', '-'],
                     capture_output=True, check=True).stdout
x = np.frombuffer(raw, dtype='<f4'); sr = 44100; N = 8192; w = np.hanning(N)
fr = np.fft.rfftfreq(N, 1 / sr)
cs = [125, 1000, 2000, 4000, 8000, 16000]
for name, a, b in SECTIONS:
    seg = x[int(a * B * sr):int(b * B * sr)]
    P = sum(np.abs(np.fft.rfft(seg[i:i + N] * w)) ** 2 for i in range(0, len(seg) - N, N // 2))
    m = (fr >= 20) & (fr < 20000)
    ll = np.polyfit(np.log10(fr[m]), np.log10(np.sqrt(P[m])), 1)[0]
    mm = (fr >= 2000) & (fr < 8000); s28 = np.polyfit(np.log2(fr[mm]), 10 * np.log10(P[mm]), 1)[0]
    bands = [10 * np.log10(P[(fr >= c / 2 ** .5) & (fr < c * 2 ** .5)].sum()) for c in cs]
    print('%-9s slope %.2f  2-8k %+.1f dB/oct  ' % (name, ll, s28) + ' '.join('%dk:%+.1f' % (c // 1000, v - bands[1]) if c >= 1000 else '%d:%+.1f' % (c, v - bands[1]) for c, v in zip(cs, bands)))
