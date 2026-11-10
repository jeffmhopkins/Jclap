#!/usr/bin/env python3
"""Bakes src/bore_table.h: each instrument's bore, from its geometry, as resonant modes.

    python3 tools/bore.py > src/bore_table.h

For every fingering, the bore's input impedance Z (the pressure at the lips or reed over the flow
into it) is computed from the bore's profile by transfer matrices: the tube sliced into 1 mm
cylinders, each with its wall losses (Benade's 3e-5 sqrt(f) / r nepers a metre), the mouthpiece
cup a compliance at the input, and the bell's radiation impedance at the far end (an unflanged
pipe's: Zr / Zc = z / (1 + z), z = (ka)^2 / 4 + 0.61 j ka). From the same matrices comes the bell's
transfer, the flow leaving the bell over the flow coming in.

Z is fitted as a sum of modes (Silva, Guillemain and others: the resonances as pairs of complex
poles, with residues), plus a constant for the highs, where a bore with its bell cut off looks like
a resistance. The poles are the impedance peaks (their frequency and their Q from the half-power
width); the residues are a least-squares fit. The radiated sound, the bell's flow differentiated
(a monopole) and brought up on the bell's axis as it beams the highs, shares those poles (the same
resonances ring in it) and adds the wave that passes straight out of the bell above its cutoff: a
short FIR, delayed by the trip down the bore.

Only numpy is needed.
"""
import sys
import numpy as np

RHO, C = 1.2, 343.0
FS_REF = 96000.0
N_TAPS = 96


def tmm(profile, w, cup=0.0):
    """profile: (x, r) points, metres, linear between. Returns Zin, T = Uout / Uin, Zc at input."""
    xs = np.array([p[0] for p in profile])
    rs = np.array([p[1] for p in profile])
    L = xs[-1]
    n = int(L / 0.001) + 1
    edges = np.linspace(0, L, n + 1)
    r = np.interp(0.5 * (edges[1:] + edges[:-1]), xs, rs)
    dl = L / n
    A = np.ones_like(w, dtype=complex)
    B = np.zeros_like(A)
    Cm = np.zeros_like(A)
    D = np.ones_like(A)
    f = w / (2 * np.pi)
    for ri in r:
        Zc = RHO * C / (np.pi * ri * ri)
        alpha = 3.0e-5 * np.sqrt(np.maximum(f, 1e-3)) / ri
        k = w / C + alpha - 1j * alpha
        c, s = np.cos(k * dl), np.sin(k * dl)
        A, B, Cm, D = A * c + B * 1j * s / Zc, A * 1j * Zc * s + B * c, Cm * c + D * 1j * s / Zc, Cm * 1j * Zc * s + D * c
    ka = w / C * r[-1]
    z = 0.25 * ka ** 2 + 0.61j * ka
    Zr = z / (1 + z) * RHO * C / (np.pi * r[-1] ** 2)
    Zin = (A * Zr + B) / (Cm * Zr + D)
    T = 1 / (Cm * Zr + D)
    if cup > 0:
        Zs = 1 / (1 / Zin + 1j * w * cup / (RHO * C * C))
        T = T * Zs / Zin
        Zin = Zs
    return Zin, T, RHO * C / (np.pi * r[0] ** 2)


def peaks(f, Z, fmax, floor_db, Zc):
    m = np.abs(Z)
    out = []
    for i in range(1, len(f) - 1):
        if f[i] > fmax:
            break
        if m[i] > m[i - 1] and m[i] > m[i + 1] and f[i] > 40 and 20 * np.log10(m[i] / Zc) > floor_db:
            h = m[i] / np.sqrt(2)
            a = b = i
            while a > 0 and m[a] > h:
                a -= 1
            while b < len(f) - 1 and m[b] > h:
                b += 1
            q = f[i] / max(f[b] - f[a], 1e-3)
            if q > 5:
                out.append((f[i], q))
    return out


def fit(f, H, poles, extra, wt, ridge):
    jw = 2j * np.pi * f
    s = np.array([-np.pi * fn / q + 2j * np.pi * fn * np.sqrt(1 - 1 / (4 * q * q)) for fn, q in poles])
    cols = []
    for sn in s:
        cols.append(1 / (jw - sn) + 1 / (jw - np.conj(sn)))
        cols.append(1j / (jw - sn) - 1j / (jw - np.conj(sn)))
    cols += extra
    M = np.array(cols).T
    Mw = M * wt[:, None]
    Mr = np.vstack([Mw.real, Mw.imag])
    hr = np.concatenate([(H * wt).real, (H * wt).imag])
    sc = np.linalg.norm(Mr, axis=0)
    A = np.vstack([Mr / sc, np.sqrt(ridge) * np.eye(len(sc))])
    x = np.linalg.lstsq(A, np.concatenate([hr, np.zeros(len(sc))]), rcond=None)[0] / sc
    res = x[0:2 * len(s):2] + 1j * x[1:2 * len(s):2]
    return s, res, x[2 * len(s):], M @ x


def bake(profile, cup, bell_r, fmax=2400.0, floor_db=-6.0):
    f = np.linspace(20, 16000, 32000)
    w = 2 * np.pi * f
    Z, T, Zc = tmm(profile, w, cup)
    poles = peaks(f, Z, fmax, floor_db, Zc)
    lo = 1 / (1 + (f / 3000) ** 4)
    s, cz, r0, Zf = fit(f, Z / Zc, poles, [np.ones_like(f)], lo, 1e-8)
    ka = w / C * bell_r
    H = 1j * w * T / 1e4 * np.sqrt(1 + ka * ka)
    tau = profile[-1][0] / C
    start = int(tau * FS_REF) - N_TAPS // 3
    taps = [np.exp(-1j * w * (start + k) / FS_REF) for k in range(N_TAPS)]
    _, cr, fir, Hf = fit(f, H, poles, taps, 1 / np.abs(H), 1e-6)
    zerr = np.abs(20 * np.log10(np.abs(Zf) / np.abs(Z / Zc)))[f < 3000].mean()
    rerr = np.abs(20 * np.log10(np.abs(Hf) / np.abs(H)))[f < 12000].mean()
    return dict(s=s, cz=cz, cr=cr, r0=float(r0[0]), fir=fir, start=start / FS_REF, zc=Zc,
                peaks=[p[0] for p in poles], zerr=zerr, rerr=rerr)


def trumpet_profile(L, bell_len=0.55, bell_r=0.062, flare=0.7, cyl=0.00583):
    """A Bb trumpet: mouthpiece throat and backbore, leadpipe, the cylinder through the valves and
    slides, and a Bessel-horn bell r = b / (xe - x)^flare out to its rim."""
    pts = [(0.0, 0.0018), (0.008, 0.0018), (0.075, 0.0042), (0.075, 0.0045), (0.30, 0.0057), (0.30, cyl)]
    x0 = L - bell_len
    pts.append((x0, cyl))
    q = (cyl / bell_r) ** (1 / flare)
    xe = (L - q * x0) / (1 - q)
    for x in np.linspace(x0, L, 60)[1:]:
        pts.append((x, cyl * ((xe - x0) / (xe - x)) ** flare))
    return pts


def ff(v):
    """A float literal for C++ (always with a point or an exponent)."""
    t = f"{v:.6g}"
    return (t if ('.' in t or 'e' in t or 'n' in t) else t + ".0") + "f"


def emit(name, fings, comment, out):
    n = max(len(b['s']) for b in fings)
    out.write(f"// {comment}\n")
    out.write(f"inline constexpr BoreFingering k{name}Bore[{len(fings)}] = {{\n")
    for b in fings:
        out.write(f"   {{{len(b['s'])}, {ff(b['r0'])}, {b['start']:.8g}, {b['zc']:.6g}, // modes from {b['peaks'][0]:.1f} Hz; "
                  f"fit within {b['zerr']:.1f} dB (Z), {b['rerr']:.1f} dB (radiation)\n")
        out.write("    {")
        out.write(", ".join(f"{p:.2f}f" for p in b['peaks']))
        out.write("},\n    {\n")
        for sn, a, r in zip(b['s'], b['cz'], b['cr']):
            out.write("     {" + ", ".join(ff(v) for v in (sn.real, sn.imag, a.real, a.imag, r.real, r.imag)) + "},\n")
        out.write("    },\n    {")
        out.write(", ".join(ff(t) for t in b['fir']))
        out.write("}},\n")
    out.write("};\n\n")


def main():
    out = sys.stdout
    out.write("// Generated by tools/bore.py: python3 tools/bore.py > src/bore_table.h. Don't edit by hand.\n")
    out.write("#pragma once\n\n#include \"bore.h\"\n\nnamespace jbreath {\n\n")
    # Trumpet: the open horn and the six valve combinations (each a semitone more tubing, added
    # to the cylinder through the valves).
    fings = []
    for v in range(7):
        L = 1.40 * 2 ** (v / 12)
        fings.append(bake(trumpet_profile(L), 0.6e-6, 0.062))
        print(f"trumpet valves {v}: {len(fings[-1]['s'])} modes, Z {fings[-1]['zerr']:.2f} dB, rad {fings[-1]['rerr']:.2f} dB",
              file=sys.stderr)
    emit("Trumpet", fings, "Bb trumpet, the open horn then each valve combination a semitone longer (0 to 6).", out)
    out.write("} // namespace jbreath\n")


if __name__ == '__main__':
    main()
