"""Decodes a MatrixBrute preset dump (from mb_dump.py) into src/factory_bank.inc.

    python tools/mb_bank.py data/dump src/factory_bank.inc [--check]

With --check it writes nothing, and fails if the file isn't what it would write (ctest runs this).

The preset format isn't documented. It was worked out from data/calib/calib-*.syx: one scratch slot
read back after tools/mb_calibrate.py set every sound CC to a known pattern and the panel was saved.
Each run checks the decoding against those reads when they sit beside the dump (../calib).

A preset is 50 chunks of 32 bytes (20 16 / 20 17 replies), 1600 bytes in all. That's 7-bit MIDI
data packed 8 bytes to 7: each group of 8 starts with a byte holding the top bits of the next 7,
first byte in bit 6. Unpacked it's 1400 bytes, and every knob is a 16-bit little-endian word at an
even offset, stored one of these ways (v is the CC value, 0-127):

    level    v * 32767 / 127                        unipolar knobs
    center   v * 516 - 32768, signed                centered knobs (fine tune, env amounts...)
    semi24   round((v - 63.5) * 48 / 127) * 32767 / 24, signed: coarse tune, +/-24 semitones
    semi12   round(v * 12 / 127) * 32767 / 12       bend range, 0-12 semitones
    split    |v * 516 - 32768|, with a flag word elsewhere saying which side of centre: the
             audio mod knobs that send VCO 3 or noise one way or the other (calib-4 put 78 and 80
             on opposite sides to tell their flags apart)

The sequencer's rate, swing and gate (CC 116-118) aren't in a preset.
"""

import glob
import os
import sys
import types

CHUNKS = 50
STEP24 = 32767 / 24
STEP12 = 32767 / 12

# cc: (offset, kind[, flag offset]). For "split" the flag word is 1 when the knob is above centre.
LAYOUT = {
    # VCO 1
    9: (1240, "level"), 10: (1236, "level"), 12: (1238, "level"), 13: (1242, "level"),
    66: (1130, "semi24"), 65: (1128, "center"), 67: (1202, "level"), 68: (1234, "level"),
    69: (1232, "level"), 70: (1158, "level"),
    # VCO 2
    14: (1246, "level"), 15: (1254, "level"), 16: (1244, "level"), 17: (1248, "level"),
    72: (1134, "semi24"), 71: (1132, "center"), 73: (1204, "level"), 74: (1252, "level"),
    75: (1250, "level"), 76: (1160, "level"),
    # VCO 3 / LFO 3
    22: (1136, "semi24"),
    # Audio mod
    77: (1206, "level"), 78: (1210, "split", 1262), 79: (1208, "split", 1260), 80: (1212, "split", 1266),
    # Mixer
    18: (1094, "level"), 19: (1096, "level"), 20: (1098, "level"), 21: (1100, "level"), 89: (1102, "level"),
    # Steiner
    23: (1126, "level"), 83: (1198, "level"), 82: (1194, "level"), 81: (1192, "level"), 24: (1200, "center"),
    7: (1230, "level"),
    # Ladder
    25: (1222, "level"), 87: (1224, "level"), 86: (1172, "level"), 85: (1170, "level"), 26: (1178, "center"),
    8: (1220, "level"),
    # Master cutoff
    27: (1082, "center"),
    # Envelopes
    102: (1032, "level"), 103: (1034, "level"), 28: (1036, "level"), 104: (1038, "level"), 94: (1162, "level"),
    105: (1040, "level"), 106: (1042, "level"), 29: (1044, "level"), 107: (1046, "level"), 95: (1164, "level"),
    108: (1056, "level"), 109: (1048, "level"), 110: (1050, "level"), 30: (1052, "level"), 111: (1054, "level"),
    # LFOs
    91: (1062, "level"), 90: (1068, "center"), 93: (1072, "level"), 92: (1078, "level"),
    # Analog effects
    31: (1168, "level"), 112: (1166, "level"), 113: (1214, "level"), 114: (1216, "level"), 115: (1218, "level"),
    # Performance
    5: (1116, "level"), 119: (1322, "semi12"), 11: (1180, "center"), 2: (1182, "center"), 4: (1190, "level"),
    3: (1188, "level"),
}


def messages(blob):
    out, cur = [], None
    for b in blob:
        if b == 0xF0:
            cur = [b]
        elif cur is not None:
            cur.append(b)
            if b == 0xF7:
                out.append(bytes(cur))
                cur = None
    return out


def read_preset(path):
    """(header bytes, name, 1600 packed bytes) from an mb_dump.py file."""
    msgs = messages(open(path, "rb").read())
    header = next(m for m in msgs if m[7:9] == bytes([0x23, 0x52]))
    chunks = [m for m in msgs if m[7] == 0x20 and m[8] in (0x16, 0x17)]
    if len(chunks) != CHUNKS:
        raise ValueError("%s: %d chunks" % (path, len(chunks)))
    name = header[21:-1].split(b"\0")[0].decode("ascii", "replace").strip()
    return header[12:21], name, b"".join(c[9:-1] for c in chunks)


def unpack(data):
    out = bytearray()
    for g in range(0, len(data), 8):
        top = data[g]
        for i in range(7):
            out.append(data[g + 1 + i] | (((top >> (6 - i)) & 1) << 7))
    return bytes(out)


def word(u, offset, signed=False):
    w = u[offset] | (u[offset + 1] << 8)
    return w - 65536 if signed and w >= 32768 else w


def middle(values):
    """The middle CC value of those that store the same thing."""
    return values[len(values) // 2]


SEMI24 = {}
SEMI12 = {}
for v in range(128):
    SEMI24.setdefault(round((v - 63.5) * 48 / 127), []).append(v)
    SEMI12.setdefault(round(v * 12 / 127), []).append(v)


def clamp(v):
    return min(127.0, max(0.0, v))


def decode(u):
    """{cc: value 0-127, to 1/128} for one unpacked preset."""
    values = {}
    for cc, spec in LAYOUT.items():
        offset, kind = spec[0], spec[1]
        if kind == "level":
            v = word(u, offset) * 127 / 32767
        elif kind == "center":
            v = (word(u, offset, True) + 32768) / 516
        elif kind == "split":
            magnitude = word(u, offset)
            above = word(u, spec[2]) != 0
            v = (32768 + (magnitude if above else -magnitude)) / 516
        elif kind == "semi24":
            semis = max(-24, min(24, round(word(u, offset, True) / STEP24)))
            v = middle(SEMI24[semis])
        elif kind == "semi12":
            v = middle(SEMI12[max(0, min(12, round(word(u, offset) / STEP12)))])
        values[cc] = round(clamp(v) * 128) / 128
    return values


def check(calib_dir):
    """Decodes the calibration reads and compares them with the CCs that were sent."""
    # mb_calibrate.py needs mido to send; only its patterns are needed here. No .pyc in the source
    # tree (ctest runs this).
    sys.dont_write_bytecode = True
    sys.modules.setdefault("mido", types.ModuleType("mido"))
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import mb_calibrate
    worst = 0
    for path in sorted(glob.glob(os.path.join(calib_dir, "calib-*.syx"))):
        round_ = int(path[-5])
        sent = mb_calibrate.pattern(round_)
        got = decode(unpack(read_preset(path)[2]))
        for cc, v in sent.items():
            if cc not in got:
                continue
            kind = LAYOUT[cc][1]
            if kind == "semi24":
                expected = middle(SEMI24[round((v - 63.5) * 48 / 127)])
            elif kind == "semi12":
                expected = middle(SEMI12[round(v * 12 / 127)])
            else:
                expected = v
            err = abs(got[cc] - expected)
            # Round 0's VCO 1 mixer level (0) didn't take on the synth; round 4 sent 0 again and it did.
            skip = round_ == 0 and cc == 18
            if err > 0.02 and not skip:
                print("calib %d: CC %d sent %d, decoded %.2f" % (round_, cc, v, got[cc]))
            worst = max(worst, 0 if skip else err)
    return worst


def main():
    args = [a for a in sys.argv[1:] if a != "--check"]
    check_only = len(args) != len(sys.argv) - 1
    if len(args) != 2:
        sys.exit(__doc__)
    dump, out = args
    calib = os.path.join(os.path.dirname(os.path.abspath(dump)), "calib")
    if os.path.isdir(calib):
        worst = check(calib)
        print("Calibration check: worst error %.3f CC steps" % worst)
        if worst > 0.02:
            sys.exit("The decoding doesn't match the calibration reads")

    rows = []
    for path in sorted(glob.glob(os.path.join(dump, "b*-s*.syx"))):
        bank, slot = int(os.path.basename(path)[1:3]), int(os.path.basename(path)[5:7])
        _, name, data = read_preset(path)
        if not name:
            continue
        values = decode(unpack(data))
        cells = ", ".join("{%d, %s}" % (cc, ("%.4f" % v).rstrip("0").rstrip(".")) for cc, v in values.items())
        escaped = name.replace("\\", "\\\\").replace('"', '\\"')
        rows.append('{%d, "%s", {%s}},' % (bank * 16 + slot, escaped, cells))
    text = ("// Generated by tools/mb_bank.py from data/dump; don't edit.\n"
            "// {preset 0-255, name, {{cc, value}...}}\n" + "\n".join(rows) + "\n")
    if check_only:
        # Universal newlines: a Windows checkout may have turned the file's LFs into CRLFs.
        with open(out, encoding="utf-8") as f:
            if f.read() != text:
                sys.exit("%s is out of date: run tools/mb_bank.py" % out)
        print("%s matches the dump (%d named presets)" % (out, len(rows)))
        return
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("Wrote %d named presets to %s" % (len(rows), out))


if __name__ == "__main__":
    main()
