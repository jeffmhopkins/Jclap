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


def tmm_holes(profile, holes, nopen, w, cup, vent=None, dl=0.002):
    """A bore with tone holes, worked backward from the bell. holes: (x, b, t) for each hole (its
    place, radius and chimney height), from the bell up; the first nopen are open, the rest shut
    (each a little volume). vent: an open register hole, (x, b, t). Returns Zin, the flow radiated
    (the bell's and every open hole's together) over the flow in, and Zc at the input."""
    xs = np.array([p[0] for p in profile])
    rs = np.array([p[1] for p in profile])
    f = w / (2 * np.pi)
    k0 = w / C
    rb = rs[-1]
    ka = k0 * rb
    z = 0.25 * ka ** 2 + 0.61j * ka
    p = z / (1 + z) * RHO * C / (np.pi * rb * rb)
    u = np.ones_like(p)
    rad = u.copy()
    pts = [(h[0], h[1], h[2], i < nopen) for i, h in enumerate(holes)]
    if vent:
        pts.append((vent[0], vent[1], vent[2], True))
    pts.sort(key=lambda h: -h[0])
    x = xs[-1]
    for hx, hb, ht, ho in pts + [(0.0, None, None, None)]:
        n = max(1, int(round((x - hx) / dl)))
        e = np.linspace(hx, x, n + 1)
        for ri in np.interp(0.5 * (e[1:] + e[:-1]), xs, rs)[::-1]:
            Zc = RHO * C / (np.pi * ri * ri)
            alpha = 3.0e-5 * np.sqrt(np.maximum(f, 1e-3)) / ri
            k = k0 + alpha - 1j * alpha
            c, s = np.cos(k * (x - hx) / n), np.sin(k * (x - hx) / n)
            p, u = c * p + 1j * Zc * s * u, 1j * s / Zc * p + c * u
        x = hx
        if hb is not None:
            Zh = RHO * C / (np.pi * hb * hb)
            if ho:  # an open hole: its air's mass, and its radiation
                uh = p / (1j * Zh * k0 * (ht + 0.75 * hb) + Zh * 0.25 * (k0 * hb) ** 2)
                rad = rad + uh
            else:  # a shut one: its chimney's volume under the pad, a compliance
                uh = p * 1j * k0 * ht / Zh
            u = u + uh
    if cup > 0:
        u = u + 1j * w * cup / (RHO * C * C) * p
    return p / u, rad / u, RHO * C / (np.pi * rs[0] ** 2)


def first_peak(f, Z):
    m = np.abs(Z)
    i = int(np.argmax((m[1:-1] > m[:-2]) & (m[1:-1] > m[2:]))) + 1
    a, b, c = np.log(m[i - 1:i + 2])
    return f[i] + 0.5 * (a - c) / (a - 2 * b + c) * (f[1] - f[0])


def sax_profile(L, r0=0.006, rc=0.040, bell_r=0.065, bell_len=0.12, throat=0.0045, throat_len=0.03):
    """An alto saxophone's air column: the mouthpiece's throat (a 4.5 mm bore, 3 cm), then from the
    neck's tip a cone (the neck and body, 6 mm to 40 mm over the bore's length) opening into the
    bell over its last 12 cm. With the mouthpiece's chamber (SAX_CUP) the throat stands in for the
    cone's missing apex: these keep the resonances within about 25 cents of harmonic from low Bb to
    the octave key's C#6 (a chamber alone, matched to the apex at the lowest notes, leaves the top
    fingerings' second resonance a semitone or more sharp)."""
    x1 = L - bell_len
    r1 = r0 + (rc - r0) * (x1 - throat_len) / (L - throat_len)
    pts = [(0.0, throat), (throat_len, throat), (throat_len + 1e-6, r0), (x1, r1)]
    for x in np.linspace(x1, L, 20)[1:]:
        pts.append((x, r1 + (bell_r - r1) * ((x - x1) / bell_len) ** 2.2))
    return pts


SAX_LOW, SAX_CUP, SAX_HOLES, SAX_TAPS = 138.59, 5.5e-6, 19, 256


def sax_geometry():
    """The alto's length and its tone holes, each placed (by bisection) to sound its semitone when it
    and every hole under it are open; a hole's radius is 0.8 of the bore's there (at least 3.5 mm),
    its chimney 3 mm and 15% of its radius."""
    def tune(lo, hi, tgt, z_of):
        fq = np.linspace(tgt * 0.7, tgt * 1.4, 700)
        for _ in range(24):
            x = 0.5 * (lo + hi)
            if first_peak(fq, z_of(x, 2 * np.pi * fq)) > tgt:
                lo = x
            else:
                hi = x
        return x
    L = tune(0.8, 1.4, SAX_LOW, lambda L, w: tmm_holes(sax_profile(L), [], 0, w, SAX_CUP)[0])
    prof = sax_profile(L)
    holes = []

    def hole(x):
        b = max(0.0035, 0.8 * np.interp(x, [p[0] for p in prof], [p[1] for p in prof]))
        return (x, b, 0.003 + 0.15 * b)
    for n in range(1, SAX_HOLES + 1):
        top = holes[-1][0] - 0.01 if holes else L - 0.02
        x = tune(0.08, top, SAX_LOW * 2 ** (n / 12),
                 lambda x, w: tmm_holes(prof, holes + [hole(x)], n, w, SAX_CUP)[0])
        holes.append(hole(x))
    return prof, holes


def sax_vent(n):
    """The octave key's vent for fingering n: the body's (near the second resonance's pressure node
    for the lower notes of the second register) or, from written A5 up, the neck's."""
    return (0.24, 0.0015, 0.005) if n < 12 else (0.11, 0.0015, 0.005)


def oboe_profile(L, reed_r=0.0017, reed_len=0.025, st_r0=0.0012, st_r1=0.0022, st_len=0.047, body_r1=0.0085,
                 bell_r=0.019, bell_len=0.065):
    """An oboe's air column: the reed's channel between its blades (as a 1.7 mm tube, 25 mm), the
    staple (a narrow cone, 1.2 to 2.2 mm over 47 mm), then the body's cone (2.2 to 8.5 mm) opening
    into the bell over its last 6.5 cm. With the reed's own volume (OBOE_CUP) these stand in for
    the cone's missing apex: the second resonance within about 25 cents of the first's octave from
    low Bb to D5."""
    x0 = reed_len
    x1 = x0 + st_len
    xb = L - bell_len
    pts = [(0.0, reed_r), (x0, reed_r), (x0 + 1e-6, st_r0), (x1, st_r1)]
    for x in np.linspace(x1, xb, 30)[1:]:
        pts.append((x, st_r1 + (body_r1 - st_r1) * (x - x1) / (xb - x1)))
    for x in np.linspace(xb, L, 15)[1:]:
        pts.append((x, body_r1 + (bell_r - body_r1) * ((x - xb) / bell_len) ** 2.2))
    return pts


OBOE_LOW, OBOE_CUP, OBOE_HOLES = 233.08, 0.15e-6, 16


def reed_geometry(profile, low, cup, nholes, hole, top0=0.08):
    """A reed instrument's length and its tone holes, each placed (by bisection) to sound its semitone
    when it and every hole under it are open. hole(prof, x): the hole at x, (x, radius, chimney)."""
    def tune(lo, hi, tgt, z_of):
        fq = np.linspace(tgt * 0.7, tgt * 1.4, 700)
        for _ in range(24):
            x = 0.5 * (lo + hi)
            if first_peak(fq, z_of(x, 2 * np.pi * fq)) > tgt:
                lo = x
            else:
                hi = x
        return x
    L = tune(0.4 * 343.0 / (2 * low), 1.6 * 343.0 / (2 * low), low, lambda L, w: tmm_holes(profile(L), [], 0, w, cup)[0])
    prof = profile(L)
    holes = []
    for n in range(1, nholes + 1):
        top = holes[-1][0] - 0.008 if holes else L - 0.02
        x = tune(top0, top, low * 2 ** (n / 12), lambda x, w: tmm_holes(prof, holes + [hole(prof, x)], n, w, cup)[0])
        holes.append(hole(prof, x))
    return prof, holes


def oboe_geometry():
    """The oboe's tone holes: small (0.55 of the bore's radius, at least 1.4 mm) through its thick
    wooden wall (5 mm chimneys)."""
    def hole(prof, x):
        r = np.interp(x, [p[0] for p in prof], [p[1] for p in prof])
        return (x, max(0.0014, 0.55 * r), 0.005)
    return reed_geometry(oboe_profile, OBOE_LOW, OBOE_CUP, OBOE_HOLES, hole, top0=0.12)


def oboe_vent(prof, holes, n):
    """The octave key's vent for fingering n (the oboe has three): of the places from 9 to 21 cm
    below the reed's tip, the one that puts the second resonance nearest the octave, favouring
    those that also lift it over the first."""
    f = np.linspace(100, 4000, 8000)
    w = 2 * np.pi * f
    tgt = 2 * OBOE_LOW * 2 ** (n / 12)
    best, score = None, 1e9
    for vx in np.arange(0.09, 0.215, 0.01):
        v = (vx, 0.0007, 0.004)
        a = 20 * np.log10(np.abs(tmm_holes(prof, holes, n, w, OBOE_CUP, v)[0]))
        pk = [i for i in range(1, len(f) - 1) if a[i] > a[i - 1] and a[i] > a[i + 1]]
        j = min(range(len(pk)), key=lambda j: abs(np.log(f[pk[j]] / tgt)))
        lower = max([a[pk[i]] for i in range(j)] or [a[pk[j]] - 20])
        sc = abs(1200 * np.log2(f[pk[j]] / tgt)) - 3.0 * min(a[pk[j]] - lower, 6.0)
        if sc < score:
            best, score = v, sc
    return best


def bake_holes(prof, holes, n, vent, fmax=2400.0, floor_db=-6.0, cup=SAX_CUP):
    f = np.linspace(20, 16000, 32000)
    w = 2 * np.pi * f
    Z, R, Zc = tmm_holes(prof, holes, n, w, cup, vent)
    poles = peaks(f, Z, fmax, floor_db, Zc)
    lo = 1 / (1 + (f / 3000) ** 4)
    s, cz, r0, Zf = fit(f, Z / Zc, poles, [np.ones_like(f)], lo, 1e-8)
    H = 1j * w * R / 1e4
    tau = (holes[n - 1][0] if n > 0 else prof[-1][0]) / C
    start = int(tau * FS_REF) - N_TAPS // 3
    taps = [np.exp(-1j * w * (start + k) / FS_REF) for k in range(SAX_TAPS)]
    _, cr, fir, Hf = fit(f, H, poles, taps, 1 / np.abs(H), 1e-6)
    zerr = np.abs(20 * np.log10(np.abs(Zf) / np.abs(Z / Zc)))[f < 3000].mean()
    rerr = np.abs(20 * np.log10(np.abs(Hf) / np.abs(H)))[f < 12000].mean()
    return dict(s=s, cz=cz, cr=cr, r0=float(r0[0]), fir=fir, start=start / FS_REF, zc=Zc,
                peaks=[p[0] for p in poles], zerr=zerr, rerr=rerr)


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
        out.write(f"}}, {len(b['fir'])}}},\n")
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
    # Alto sax: each fingering from low Bb (written; concert Db3) up, the holes from the bell opening
    # one by one (0 to 19, to the palm keys' F), then with the octave key from written D5 (4 to 19).
    prof, holes = sax_geometry()
    fings = []
    for oct_, n in [(0, n) for n in range(SAX_HOLES + 1)] + [(1, n) for n in range(4, SAX_HOLES + 1)]:
        fings.append(bake_holes(prof, holes, n, sax_vent(n) if oct_ else None))
        b = fings[-1]
        print(f"sax {'octave ' if oct_ else ''}fingering {n}: {len(b['s'])} modes from {b['peaks'][0]:.1f} Hz, "
              f"Z {b['zerr']:.2f} dB, rad {b['rerr']:.2f} dB", file=sys.stderr)
    emit("Sax", fings, "Alto saxophone: fingerings 0 to 19 (written low Bb up a semitone each), then 4 to 19 with the octave key.", out)
    # Oboe: each fingering from low Bb up (0 to 16, to D5), then with an octave key from D5 (4 to 16).
    prof, holes = oboe_geometry()
    fings = []
    for oct_, n in [(0, n) for n in range(OBOE_HOLES + 1)] + [(1, n) for n in range(4, OBOE_HOLES + 1)]:
        fings.append(bake_holes(prof, holes, n, oboe_vent(prof, holes, n) if oct_ else None, fmax=4000.0, cup=OBOE_CUP))
        b = fings[-1]
        print(f"oboe {'octave ' if oct_ else ''}fingering {n}: {len(b['s'])} modes from {b['peaks'][0]:.1f} Hz, "
              f"Z {b['zerr']:.2f} dB, rad {b['rerr']:.2f} dB", file=sys.stderr)
    emit("Oboe", fings, "Oboe: fingerings 0 to 16 (low Bb up a semitone each), then 4 to 16 with an octave key.", out)
    out.write("} // namespace jbreath\n")


if __name__ == '__main__':
    main()
