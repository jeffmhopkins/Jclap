#!/usr/bin/env python3
"""Writes Polar XY.amxd, the Max for Live MIDI effect, from the layout below.

    python3 make_device.py           writes "Polar XY.amxd" next to this script
    python3 make_device.py --check   fails if the committed device doesn't match this script

The device is a patcher ([midiin] -> [js polarxy.js] -> [midiout], a [metro] ticking the glide) with a
panel of live.* controls, each a Live parameter that can be automated and mapped, sent to the js
as `param <name> <value>`. An .amxd is the patcher's JSON behind a small binary header.
"""

import json
import pathlib
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
DEVICE = HERE / "Polar XY.amxd"
JS = "polarxy.js"
HEIGHT = 169  # every Live device is this tall

boxes, lines = [], []
_next = [0]


def box(maxclass, **attrs):
    _next[0] += 1
    b = {"id": f"obj-{_next[0]}", "maxclass": maxclass}
    b.update(attrs)
    boxes.append({"box": b})
    return b["id"]


def newobj(text, ins, outs, outtype, at):
    return box("newobj", text=text, numinlets=ins, numoutlets=outs, outlettype=outtype,
               patching_rect=[at[0], at[1], max(40, 7 * len(text) + 12), 22])


def wire(src, dst, outlet=0, inlet=0):
    lines.append({"patchline": {"source": [src, outlet], "destination": [dst, inlet]}})


def label(text, rect, bold=False):
    box("comment", text=text, numinlets=1, numoutlets=0, presentation=1, presentation_rect=rect,
        patching_rect=[rect[0] + 700, rect[1] + 400, rect[2], rect[3]],
        fontsize=9.0 if not bold else 10.0, fontface=1 if bold else 0)


def param_attrs(longname, shortname, ptype, lo, hi, initial, **extra):
    v = {
        "parameter_longname": longname,
        "parameter_shortname": shortname,
        "parameter_type": ptype,  # 0 float, 1 int, 2 enum
        "parameter_mmin": float(lo),
        "parameter_mmax": float(hi),
        "parameter_initial_enable": 1,
        "parameter_initial": [initial],
    }
    v.update(extra)
    return {"valueof": v}


# The js and the MIDI path.
midiin = newobj("midiin", 1, 1, ["int"], (20, 20))
js = newobj(f"js {JS}", 1, 2, ["", ""], (20, 260))
midiout = newobj("midiout", 1, 0, [], (20, 320))
thisdevice = newobj("live.thisdevice", 1, 3, ["bang", "int", "int"], (200, 20))
metro = newobj("metro 4", 2, 1, ["bang"], (200, 60))
ticker = box("message", text="tick", numinlets=2, numoutlets=1, outlettype=[""], patching_rect=[200, 100, 34, 22])
wire(midiin, js)
wire(js, midiout)
wire(thisdevice, metro)  # the device is loaded: start ticking
wire(metro, ticker)
wire(ticker, js)

_px = [20]


def control(maxclass, name, rect, attrs, outs=2, outtype=("", "float")):
    """A live.* control in the panel, sending `param <name> <value>` to the js."""
    x = _px[0]
    _px[0] += 90
    cid = box(maxclass, numinlets=1, numoutlets=outs, outlettype=list(outtype), parameter_enable=1,
              presentation=1, presentation_rect=rect, patching_rect=[x, 140, rect[2], rect[3]],
              varname=attrs["valueof"]["parameter_longname"], saved_attribute_attributes=attrs)
    pre = newobj(f"prepend param {name}", 1, 1, [""], (x, 200))
    wire(cid, pre)
    wire(pre, js)
    # A loaded device sends every control's value once, so the js starts from the saved settings.
    wire(thisdevice, cid)
    return cid


def numbox(name, longname, short, initial, rect, lo=0, hi=127, **extra):
    extra.setdefault("parameter_unitstyle", 0)
    return control("live.numbox", name, rect, param_attrs(longname, short, 1, lo, hi, initial, **extra))


def menu(name, longname, short, items, rect, initial=0):
    return control("live.menu", name, rect,
                   param_attrs(longname, short, 2, 0, len(items) - 1, initial, parameter_enum=items),
                   outs=3, outtype=("", "", "float"))


def dial(name, longname, short, hi, rect, lo=0, initial=0, **extra):
    return control("live.dial", name, rect, param_attrs(longname, short, 0, lo, hi, initial, **extra))


# ---- the panel ----
# Rows: headings at 2, labels at 18 / 52 / 86, controls at 32 / 66 / 100, the status line at 146.
label("XY IN", [8, 2, 100, 16], bold=True)
label("X CC", [8, 32, 44, 15]); numbox("inx", "X In CC", "X In", 16, [56, 32, 44, 15])
label("Y CC", [8, 52, 44, 15]); numbox("iny", "Y In CC", "Y In", 17, [56, 52, 44, 15])
label("Touch", [8, 72, 44, 15]); numbox("touchin", "Touch In CC", "Touch In", -1, [56, 72, 44, 15], lo=-1)
label("Pass", [8, 96, 44, 15]); menu("passthrough", "Pass Input CCs", "Pass In", ["Off", "Raw", "Filtered"], [56, 96, 52, 15])

label("POLAR OUT", [116, 2, 60, 16], bold=True)
control("live.text", "polarout", [176, 3, 32, 14],
        param_attrs("Polar Out", "Polar Out", 2, 0, 1, 1, parameter_enum=["off", "on"]))
boxes[-2]["box"].update(text="OFF", texton="ON", mode=1)
label("Angle", [116, 32, 44, 15]); numbox("outangle", "Angle Out CC", "Angle Out", 18, [164, 32, 44, 15])
label("Radius", [116, 52, 44, 15]); numbox("outradius", "Radius Out CC", "Radius Out", 19, [164, 52, 44, 15])
label("Touch", [116, 72, 44, 15]); numbox("touchout", "Touch Out CC", "Touch Out", -1, [164, 72, 44, 15], lo=-1)

# SEND buttons, for a MIDI learn in Omnisphere: that CC alone. Not parameters, so not automatable.
label("SEND (MIDI LEARN)", [116, 90, 100, 15])
for i, (which, text) in enumerate((("angle", "ANGLE"), ("radius", "RADIUS"), ("touch", "TOUCH"))):
    x = 116 + i * 33
    btn = box("live.text", text=text, texton=text, mode=0, fontsize=8.0, numinlets=1, numoutlets=2,
              outlettype=["", ""], parameter_enable=1, presentation=1, presentation_rect=[x, 104, 31, 16],
              patching_rect=[700 + i * 80, 300, 70, 20], varname=f"Send {which.title()}",
              saved_attribute_attributes=param_attrs(f"Send {which.title()}", f"Send {which.title()}", 2, 0, 1, 0,
                                                     parameter_enum=["off", "on"], parameter_invisible=1))
    press = newobj("sel 1", 2, 2, ["bang", ""], (700 + i * 80, 330))
    msg = box("message", text=f"learn {which}", numinlets=2, numoutlets=1, outlettype=[""],
              patching_rect=[700 + i * 80, 360, 80, 22])
    wire(btn, press)
    wire(press, msg)
    wire(msg, js)

label("MAPPING", [224, 2, 150, 16], bold=True)
dial("angleoffset", "Angle Offset", "Offset", 360, [224, 22, 44, 48],
     parameter_unitstyle=9, parameter_units="%.1f deg")
label("Direction", [276, 18, 96, 14]); menu("direction", "Direction", "Direction", ["CCW", "CW"], [276, 32, 96, 15])
label("Radius", [276, 52, 96, 14]); menu("radiusmode", "Radius Mode", "Radius", ["Clamp to circle", "Fill square"], [276, 66, 96, 15])

label("MOTION", [384, 2, 190, 16], bold=True)
dial("smoothing", "Smoothing", "Smooth", 2000, [384, 22, 44, 48], parameter_unitstyle=2, parameter_exponent=3.0)
dial("inertia", "Inertia", "Inertia", 100, [432, 22, 44, 48], parameter_unitstyle=5)
label("Path", [484, 18, 88, 14]); menu("path", "Motion Path", "Path", ["Straight", "Around"], [484, 32, 88, 15])
label("Inertia mode", [484, 52, 88, 14]); menu("inertiamode", "Inertia Mode", "Mode", ["Pull", "Coast", "Pad"], [484, 66, 88, 15])
label("Resolution", [484, 86, 88, 14]); menu("resolution", "Output Resolution", "Res", ["7-bit", "14-bit"], [484, 100, 88, 15])

# Touch Filter and its tuning (the plugin's TOUCH tab). Touch Filter is in ms, 0 off; Press -1 is the
# same as Touch Filter.
label("TOUCH", [584, 2, 110, 16], bold=True)
label("Filter ms", [584, 32, 56, 15]); numbox("touchfilter", "Touch Filter", "Touch", 0, [644, 32, 44, 15], hi=100)
label("Press ms", [584, 52, 56, 15]); numbox("touchpress", "Press Time", "Press", -1, [644, 52, 44, 15], lo=-1, hi=100)
label("Jump", [584, 72, 56, 15]); numbox("touchjump", "Jump Limit", "Jump", 16, [644, 72, 44, 15], lo=8, hi=64)
label("Agree", [584, 92, 56, 15]); numbox("touchagree", "Agree", "Agree", 4, [644, 92, 44, 15], lo=1, hi=16)
label("Check", [584, 112, 56, 15]); menu("touchheading", "Jump Check", "Check", ["Distance", "Heading"], [644, 112, 52, 15])
label("CENTER", [704, 2, 96, 16], bold=True)
dial("centersmoothing", "Center Smoothing", "Center", 50, [704, 22, 44, 48], parameter_unitstyle=5)
dial("centerlag", "Center Lag", "Lag", 1000, [752, 22, 44, 48], lo=50, initial=250, parameter_unitstyle=2)

# The status line: the last channel's X/Y in and angle/radius out, or what stops it converting.
status = box("comment", text="waiting for X/Y CCs", numinlets=1, numoutlets=0, presentation=1,
             presentation_rect=[8, 146, 788, 16], patching_rect=[20, 360, 300, 18], fontsize=9.0)
wire(js, status, outlet=1)

patcher = {
    "patcher": {
        "fileversion": 1,
        "appversion": {"major": 8, "minor": 6, "revision": 0, "architecture": "x64", "modernui": 1},
        "classnamespace": "box",
        "rect": [100.0, 100.0, 1000.0, 600.0],
        "openrect": [0.0, 0.0, 0.0, float(HEIGHT)],
        "bglocked": 0,
        "openinpresentation": 1,
        "default_fontsize": 10.0,
        "default_fontface": 0,
        "default_fontname": "Arial Bold",
        "gridonopen": 1,
        "gridsize": [8.0, 8.0],
        "gridsnaponopen": 1,
        "objectsnaponopen": 0,
        "statusbarvisible": 2,
        "toolbarvisible": 1,
        "boxanimatetime": 200,
        "enablehscroll": 1,
        "enablevscroll": 1,
        "devicewidth": 0.0,
        "description": "Turns an XY controller's two CCs into angle and radius CCs for Omnisphere's Orb.",
        "digest": "",
        "tags": "",
        "style": "",
        "subpatcher_template": "",
        "assistshowspatchername": 0,
        "boxes": boxes,
        "lines": lines,
        "dependency_cache": [{"name": JS, "bootpath": ".", "type": "TEXT", "implicit": 1}],
        "latency": 0,
        "is_mpe": 0,
        "external_mpe_tuning_enabled": 0,
        "minimum_live_version": "",
        "minimum_max_version": "",
        "platform_compatibility": 0,
        "autosave": 0,
    }
}


def amxd():
    # 'ampf', a version, the device type ('mmmm': MIDI effect), an empty 'meta' chunk, then 'ptch'
    # holding the patcher JSON (NUL-terminated).
    body = (json.dumps(patcher, indent="\t") + "\n").encode("utf-8") + b"\x00"
    return (b"ampf" + struct.pack("<I", 4) + b"mmmm" + b"meta" + struct.pack("<I", 4) + b"\x00" * 4 +
            b"ptch" + struct.pack("<I", len(body)) + body)


def read_patcher(data):
    """The patcher JSON inside an .amxd, as a dict."""
    assert data[:4] == b"ampf" and data[8:12] == b"mmmm", "not a MIDI effect .amxd"
    i = data.index(b"ptch")
    n = struct.unpack("<I", data[i + 4:i + 8])[0]
    return json.loads(data[i + 8:i + 8 + n].rstrip(b"\x00"))


if __name__ == "__main__":
    if "--check" in sys.argv:
        committed = read_patcher(DEVICE.read_bytes())
        if committed != read_patcher(amxd()):
            sys.exit(f"{DEVICE.name} is out of date: run python3 {pathlib.Path(__file__).name}")
        # Every line joins boxes that exist, on outlets and inlets they have.
        ids = {b["box"]["id"]: b["box"] for b in committed["patcher"]["boxes"]}
        for l in committed["patcher"]["lines"]:
            (src, o), (dst, i) = l["patchline"]["source"], l["patchline"]["destination"]
            assert src in ids and dst in ids, l
            assert o < ids[src]["numoutlets"] and i < ids[dst]["numinlets"], l
        names = [b["box"]["saved_attribute_attributes"]["valueof"]["parameter_longname"]
                 for b in committed["patcher"]["boxes"] if "saved_attribute_attributes" in b["box"]]
        assert len(names) == len(set(names)), "parameter names must be unique"
        assert (HERE / JS).exists()
        print(f"{DEVICE.name} is up to date ({len(names)} parameters)")
    else:
        DEVICE.write_bytes(amxd())
        print(f"wrote {DEVICE}")
