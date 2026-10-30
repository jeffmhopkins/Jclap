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


# nrpn: byte, for the settings in src/settings.h other than the routing.
SETTINGS = {27: 35, 31: 39, 35: 43, 125: 150, 126: 151, 61: 71, 63: 74, 64: 75, 66: 78, 67: 79, 69: 82, 100: 123}


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
        settings = ", ".join("{%d, %d}" % (nrpn, p[byte]) for nrpn, byte in sorted(SETTINGS.items()))
        escaped = name.replace("\\", "\\\\").replace('"', '\\"')
        lines.append('{%d, "%s", "%s", {%s}, {%s}, {%s}},' % (n + 1, escaped, category, values, mods, settings))
        count += 1
    with open(out_path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%d programs written to %s (checked against %d calibration dumps)" % (count, out_path, checked))


if __name__ == "__main__":
    main()
