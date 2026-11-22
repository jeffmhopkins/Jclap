"""Measures JKit candidate sounds and ranks them per pad: what a tight funk kit wants."""
import glob, sys, numpy as np
D = sys.argv[1]
def load(f):
    d = open(f, 'rb').read(); i = d.find(b'data')
    return np.frombuffer(d[i + 8:], dtype='<f4').reshape(-1, 2).mean(1)
def feats(x):
    S = abs(np.fft.rfft(x)) ** 2; fr = np.fft.rfftfreq(len(x), 1 / 44100); t = S.sum() + 1e-12
    b = lambda a, z: S[(fr >= a) & (fr < z)].sum() / t
    env = np.sqrt(np.convolve(x ** 2, np.ones(441) / 441, 'same')); pk = env.max()
    above = np.where(env > pk * 0.01)[0]          # -40 dB
    dec = (above[-1] - env.argmax()) / 44100 if len(above) else 0
    return dict(low=b(0, 150), mid=b(150, 1000), hi=b(1000, 5000), air=b(5000, 22050), dec=dec)
score = {
    1: lambda f: f['low'] * 2 + f['hi'] * 3 - max(0, f['dec'] - 0.3) * 4,
    3: lambda f: f['hi'] * 3 + f['air'] * 3 - max(0, f['dec'] - 0.3) * 3,
    7: lambda f: f['air'] * 4 + f['hi'] - max(0, f['dec'] - 0.15) * 4,
    9: lambda f: f['air'] * 4 + f['hi'] - max(0, f['dec'] - 0.15) * 4,
    11: lambda f: f['air'] * 3 + f['hi'],
    14: lambda f: f['air'] * 3 + f['hi'],
    2: lambda f: f['hi'] * 3 + f['air'] - max(0, f['dec'] - 0.2) * 3,
}
pads = sorted({int(f.split('/')[-1].split('-')[0]) for f in glob.glob(f'{D}/*.wav')})
for p in pads:
    rows = []
    for f in sorted(glob.glob(f'{D}/{p}-*.wav')):
        ft = feats(load(f)); rows.append((score.get(p, lambda f: f['hi'] + f['air'] - f['dec'])(ft), f, ft))
    rows.sort(key=lambda r: -r[0])
    print(f'pad {p}')
    for s, f, ft in rows:
        print('  %6.2f %-28s low %.2f mid %.2f hi %.2f air %.2f dec %.2fs' % (s, f.split('/')[-1], ft['low'], ft['mid'], ft['hi'], ft['air'], ft['dec']))
