"""Counts holes in a JDrummer funk render: bars (not fills, not the break) missing the backbeat on 2
or 4, and missing sixteenth hats (not counting the step before an open hat or a fill bar)."""
import sys
bar = 2.4; st = bar / 16
FILL = {3, 11, 19, 23, 31, 39}   # bars before a section change
def holes(path):
    hits = {}
    for l in open(path):
        t, s, k, v = l.split(); t = float(t)
        if s == '153' and int(v) > 0:
            b = int((t + 0.05) // bar); n = int(round((t - b * bar) / st))
            if n == 16: b += 1; n = 0
            hits.setdefault(b, {}).setdefault(int(k), set()).add(n)
    back = hats = 0
    for b in range(44):
        if b in FILL or 24 <= b < 32 or b < 4: continue
        h = hits.get(b, {})
        sn = h.get(38, set()) | h.get(39, set()) | h.get(37, set())
        back += (4 not in sn) + (12 not in sn)
        hat = h.get(42, set()) | h.get(46, set()) | h.get(44, set())
        hats += sum(1 for n in range(16) if n not in hat and n + 1 not in h.get(46, set()))
    return back, hats
if __name__ == '__main__':
    print(*holes(sys.argv[1]))
