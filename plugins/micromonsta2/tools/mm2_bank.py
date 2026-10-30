"""Decodes a MicroMonsta 2 program bank (UTILITY U02 SysEx dump) into src/factory_bank.inc.

    python tools/mm2_bank.py data/bank.syx src/factory_bank.inc

The program format isn't documented. It was worked out from data/cal-*.syx: single-program dumps
(UTILITY U01) taken after tools/mm2_capture.py set every sound CC to a known pattern. Each run checks
the decoding against those dumps when they sit beside the bank.

Each program is one 176-byte message: F0 00 21 22 4D 02 03 09, an 8-character name, a category byte,
3 zero bytes, the parameters, F7. A parameter is stored one of five ways, and a CC value v becomes:

    raw      the byte is v
    tune     40 + floor(v * 36 / 127)        osc coarse tune, 40-76 (+/-18 semitones around 58)
    fine     39 + floor(v * 50 / 127)        osc fine tune, 39-89 (centre 64)
    wide     floor(v * 255 / 127)            two bytes, high bit first (filter cutoff)
    amount   29 + floor(v * 198 / 127)       two bytes, high bit first; minus 128 is the -99..+99
                                             shown on the synth (mod amounts, filter env, EQ)

Several CC values can store the same thing (tune has 37 steps for 128 values); the inverse used here
is the middle one, and sending it back reproduces the program exactly. Mod matrix slot k (0-9) is 4 bytes at 83 + 4k: source,
destination, amount (two bytes); sources and destinations are indexes into the lists in src/patches.h.
The other settings no CC reaches (SETTINGS, the byte of each NRPN in src/settings.h) are stored as is.
The byte map of those and of the routing is the Monstaguru editor's, which INIT confirms (slot 1 is
LFO1xWheel to pitch, as on the synth).
"""

import math
import os
import sys

SIZE = 176
HEADER = bytes([0xF0, 0x00, 0x21, 0x22, 0x4D, 0x02, 0x03, 0x09])
CATEGORIES = ["Bass", "Lead", "Pad", "Keys", "Organ", "Strings", "Brass", "Percussion", "Drone", "Noise",
              "SFX", "Arp", "Misc", "User 1", "User 2", "User 3"]

# cc: (offset, kind). For two-byte kinds the offset is the high byte's.
LAYOUT = {
    5: (30, "raw"), 33: (27, "raw"), 34: (28, "raw"), 35: (29, "raw"), 36: (33, "raw"), 37: (34, "raw"),
    39: (36, "raw"), 40: (37, "tune"), 41: (38, "fine"),
    42: (40, "raw"), 43: (41, "tune"), 44: (42, "fine"),
    45: (44, "raw"), 46: (45, "tune"), 47: (46, "fine"),
    48: (47, "raw"), 49: (48, "raw"), 50: (49, "raw"), 51: (50, "raw"),
    74: (51, "wide"), 71: (53, "raw"), 88: (54, "amount"), 89: (56, "raw"), 90: (57, "raw"), 91: (58, "raw"),
    73: (59, "raw"), 75: (60, "raw"), 79: (61, "raw"), 72: (62, "raw"),
    80: (63, "raw"), 81: (64, "raw"), 82: (65, "raw"), 83: (66, "raw"),
    84: (67, "raw"), 85: (68, "raw"), 86: (69, "raw"), 87: (70, "raw"),
    76: (73, "raw"), 77: (77, "raw"), 78: (81, "raw"),
    62: (124, "raw"), 25: (130, "amount"),
    26: (133, "raw"), 27: (134, "raw"), 28: (135, "raw"), 29: (136, "raw"), 30: (137, "raw"), 31: (138, "raw"),
}
for slot in range(10):
    LAYOUT[52 + slot] = (85 + 4 * slot, "amount")

# kind: (stored value for a CC value, number of bytes)
ENCODE = {
    "raw": (lambda v: v, 1),
    "tune": (lambda v: 40 + v * 36 // 127, 1),
    "fine": (lambda v: 39 + v * 50 // 127, 1),
    "wide": (lambda v: v * 255 // 127, 2),
    "amount": (lambda v: 29 + v * 198 // 127, 2),
}


def stored(program, cc):
    offset, kind = LAYOUT[cc]
    if ENCODE[kind][1] == 2:
        return program[offset] * 128 + program[offset + 1]
    return program[offset]


def cc_value(program, cc):
    """The middle of the CC values the synth stores as this program's value (or the nearest one)."""
    encode = ENCODE[LAYOUT[cc][1]][0]
    s = stored(program, cc)
    closest = min(abs(encode(v) - s) for v in range(128))
    same = [v for v in range(128) if abs(encode(v) - s) == closest]
    return same[len(same) // 2]


# nrpn: (byte, 14-bit, sync choices), for the settings in src/settings.h other than the routing. A
# sync setting is 0 (free) unless the stored speed is above 127: then it's that minus 127.
SETTINGS = {
    9: (16, False, 0), 10: (17, False, 0), 11: (18, False, 0), 12: (19, False, 0), 13: (20, False, 0),
    14: (21, False, 0), 15: (22, False, 0), 16: (23, False, 0), 17: (24, False, 0), 18: (25, True, 0),
    23: (31, False, 0), 24: (32, False, 0), 27: (35, False, 0), 31: (39, False, 0), 35: (43, False, 0),
    61: (71, False, 0), 62: (72, True, 30), 63: (74, False, 0), 64: (75, False, 0), 65: (76, True, 30),
    66: (78, False, 0), 67: (79, False, 0), 68: (80, True, 30), 69: (82, False, 0), 100: (123, False, 0),
    102: (125, False, 0), 103: (126, False, 0), 104: (127, False, 0), 105: (128, False, 0),
    108: (132, True, 6), 114: (139, False, 0), 115: (140, False, 0), 116: (141, False, 0),
    117: (142, False, 0), 118: (143, False, 0), 119: (144, False, 0), 120: (145, False, 0),
    121: (146, False, 0), 122: (147, False, 0), 124: (149, False, 0), 125: (150, False, 0),
    126: (151, False, 0), 127: (152, False, 0), 128: (153, False, 0), 130: (155, False, 0),
    131: (156, False, 0), 132: (157, False, 0), 133: (158, False, 0),
}
for step in range(16):
    SETTINGS[134 + step] = (159 + step, False, 0)


def setting(program, nrpn):
    byte, wide, sync = SETTINGS[nrpn]
    v = program[byte] * 128 + program[byte + 1] if wide else program[byte]
    if sync:
        return v - 127 if 127 < v <= 127 + sync else 0
    return v


def routes(program):
    return [(program[83 + 4 * k], program[84 + 4 * k]) for k in range(10)]  # (source, destination)


def split(data):
    if len(data) % SIZE:
        sys.exit("not a whole number of %d-byte programs" % SIZE)
    programs = [data[i:i + SIZE] for i in range(0, len(data), SIZE)]
    for n, p in enumerate(programs):
        if p[:8] != HEADER or p[-1] != 0xF7:
            sys.exit("program %d isn't a MicroMonsta 2 program" % (n + 1))
    return programs


def check_calibration(folder):
    """Every CC decodes to a value the synth stores the same as the one the capture script sent."""
    patterns = {"zero": lambda i: 0, "max": lambda i: 127, "mid": lambda i: 64, "quarter": lambda i: 32,
                "three-quarter": lambda i: 96, "ramp-up": lambda i: 5 + 2 * i, "ramp-down": lambda i: 122 - 2 * i}
    order = [39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 74, 71, 88, 89, 90, 73, 75, 79, 72, 80, 81, 82, 83,
             84, 85, 86, 87, 36, 37, 76, 77, 78, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 26, 27, 28, 29, 30, 31,
             25, 5, 33, 34, 91, 35]
    assert sorted(order) == sorted(LAYOUT)
    checked = 0
    for name, value in patterns.items():
        path = os.path.join(folder, "cal-%s.syx" % name)
        if not os.path.exists(path):
            continue
        program = split(open(path, "rb").read())[0]
        for i, cc in enumerate(order):
            got = cc_value(program, cc)
            encode = ENCODE[LAYOUT[cc][1]][0]
            if encode(got) != encode(value(i)) or encode(value(i)) != stored(program, cc):
                sys.exit("%s: CC %d decodes as %d, sent %d" % (path, cc, got, value(i)))
        checked += 1
    return checked


def main():
    bank_path, out_path = sys.argv[1], sys.argv[2]
    checked = check_calibration(os.path.dirname(bank_path))
    programs = split(open(bank_path, "rb").read())
    lines = ["// Generated by tools/mm2_bank.py from data/bank.syx; don't edit.",
             "// {program, name, category, {{cc, value}...}, {{source, destination} x 10}, {{nrpn, value}...}}"]
    count = 0
    for n, p in enumerate(programs[:300]):
        name = p[8:16].decode("ascii", "replace").rstrip()
        if name == "INIT":
            continue
        category = CATEGORIES[p[16]] if p[16] < len(CATEGORIES) else "Misc"
        values = ", ".join("{%d, %d}" % (cc, cc_value(p, cc)) for cc in sorted(LAYOUT))
        mods = ", ".join("{%d, %d}" % r for r in routes(p))
        settings = ", ".join("{%d, %d}" % (nrpn, setting(p, nrpn)) for nrpn in sorted(SETTINGS))
        escaped = name.replace("\\", "\\\\").replace('"', '\\"')
        lines.append('{%d, "%s", "%s", {%s}, {%s}, {%s}},' % (n + 1, escaped, category, values, mods, settings))
        count += 1
    with open(out_path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%d programs written to %s (checked against %d calibration dumps)" % (count, out_path, checked))


if __name__ == "__main__":
    main()
