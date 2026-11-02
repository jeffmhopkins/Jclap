// Tests for the Max for Live port (polarxy.js), run with node: `node test_polarxy.js`. They mirror the
// CLAP's tests (tests/test_polar.cpp and tests/test_plugin.cpp) so the two stay in step.
'use strict';

var path = require('path');
var px = require(path.join(__dirname, 'polarxy.js'));

var failures = 0;
function check(ok, what) {
   if (!ok) {
      ++failures;
      console.log('FAIL: ' + what);
   }
}

var def = { angleOffset: 0, clockwise: false, fillSquare: false };
function polar(x, y, s) { return px.toPolar(px.ccToBipolar(x), px.ccToBipolar(y), s || def); }

// ---- the conversion (tests/test_polar.cpp) ----
check(polar(64, 64).radius === 0 && !polar(64, 64).hasAngle, 'centre: no radius, no angle');
check(polar(127, 64).radius === 127 && polar(127, 64).angle === 0, 'right edge');
check(polar(64, 127).angle === 32, 'top is 32');
check(polar(0, 64).angle === 64, 'left is 64');
check(polar(64, 0).angle === 95, 'bottom is 95');
check(polar(127, 127).radius === 127, 'corners clamp to the circle');
check(polar(96, 96).radius === 90, 'diagonal radius');
var fill = { angleOffset: 0, clockwise: false, fillSquare: true };
check(polar(96, 96, fill).radius === 63, 'fill square radius');
check(polar(127, 127, fill).angle === 16, '45 degrees');
check(polar(127, 63).angle === 0, 'just below +X wraps to 0');
check(polar(127, 66).angle === 1, 'just above +X is 1');
var maxAngle = 0;
for (var x = 0; x < 128; ++x)
   for (var y = 0; y < 128; ++y) maxAngle = Math.max(maxAngle, polar(x, y).angle);
check(maxAngle === 126, 'angles stop at 126');
check(!polar(65, 63).hasAngle && polar(63, 63).radius === 0, 'dead zone');
check(polar(66, 65).hasAngle && polar(66, 65).radius === 1, 'just outside the dead zone');
check(polar(64, 127, { angleOffset: 0, clockwise: true, fillSquare: false }).angle === 95, 'clockwise');
var off = { angleOffset: 90, clockwise: false, fillSquare: false };
check(polar(127, 64, off).angle === 32 && polar(64, 0, off).angle === 0, 'offset rotates and wraps');
check(polar(200, 64).radius === 127, 'out of range clamps');

// ---- the device ----

// An engine and parser with what they send collected as [status, d1, d2] messages.
function device() {
   var d = { msgs: [], bytes: [] };
   d.engine = new px.Engine(function (s, a, b) { d.msgs.push([s, a, b]); });
   d.parser = new px.MidiParser(d.engine, function (b) { d.bytes.push(b); });
   d.send = function () { for (var i = 0; i < arguments.length; ++i) d.parser.byte(arguments[i]); };
   d.take = function () { var m = d.msgs; d.msgs = []; return m; };
   return d;
}

// The angle and radius sent on channel 1 over `ticks` ticks of 5 ms.
function run(d, ticks) {
   var p = { angles: [], radii: [] };
   for (var t = 0; t < ticks; ++t) {
      d.engine.tick(0.005);
      d.take().forEach(function (m) {
         if (m[1] === 18) p.angles.push(m[2]);
         if (m[1] === 19) p.radii.push(m[2]);
      });
   }
   return p;
}
function last(a) { return a[a.length - 1]; }
function min(a) { return Math.min.apply(null, a); }
function max(a) { return Math.max.apply(null, a); }

// Everything that isn't an X/Y CC passes straight through, byte for byte.
var d = device();
d.send(0x90, 60, 100, 0x80, 60, 0, 0xE0, 0, 64, 0xB0, 7, 100, 0xC0, 5, 0xF8, 0xF0, 1, 2, 3, 0xF7);
check(JSON.stringify(d.bytes) === JSON.stringify([0x90, 60, 100, 0x80, 60, 0, 0xE0, 0, 64, 0xB0, 7, 100, 0xC0, 5,
                                                  0xF8, 0xF0, 1, 2, 3, 0xF7]), 'other MIDI passes through');
d.bytes = [];
d.send(0x90, 60, 100, 62, 100); // running status
check(JSON.stringify(d.bytes) === JSON.stringify([0x90, 60, 100, 0x90, 62, 100]), 'running status');

// X/Y CCs are taken; the next tick sends angle and radius once both are in.
d = device();
d.send(0xB0, 16, 127);
check(d.bytes.length === 0, 'X is taken');
d.engine.tick(0.005);
check(d.take().length === 0, 'nothing until Y arrives');
d.send(0xB0, 17, 64);
d.engine.tick(0.005);
var m = d.take();
check(m.length === 2 && m[0][1] === 18 && m[0][2] === 0 && m[1][1] === 19 && m[1][2] === 127, 'angle and radius out');
d.send(0xB0, 16, 127);
d.engine.tick(0.005);
check(d.take().length === 0, 'unchanged values are not sent again');
d.send(0xB1, 16, 64, 0xB1, 17, 127);
d.engine.tick(0.005);
m = d.take();
check(m.length === 2 && m[0][0] === 0xB1 && m[0][2] === 32, 'each channel has its own position');

// Pass Input CCs forwards the X/Y too.
d = device();
d.engine.setParam('passthrough', 1);
d.send(0xB0, 16, 100);
m = d.take();
check(m.length === 1 && m[0][1] === 16 && m[0][2] === 100, 'pass input CCs');

// The same CC for X and Y converts nothing and passes everything.
d = device();
d.engine.setParam('iny', 16);
d.send(0xB0, 16, 100);
check(d.bytes.length === 3, 'conflicting CCs pass through');
check(/same CC/.test(d.engine.status()), 'status says why');

// A new output CC sends the position again there.
d = device();
d.send(0xB0, 16, 127, 0xB0, 17, 64);
d.engine.tick(0.005);
d.take();
d.engine.setParam('outangle', 20);
m = d.take();
check(m.length === 2 && m[0][1] === 20 && m[1][1] === 19, 'moved output CCs resend');

// SEND buttons: one CC alone at its last value.
d.engine.learn('radius');
m = d.take();
check(m.length === 1 && m[0][1] === 19 && m[0][2] === 127, 'send radius');

// 14-bit: MSB then LSB on CC + 32.
d = device();
d.engine.setParam('resolution', 1);
d.send(0xB0, 16, 96, 0xB0, 17, 64);
d.engine.tick(0.005);
m = d.take();
var want = px.toPolar(px.ccToBipolar(96), 0, def, px.STEPS14);
check(m.length === 4 && m[0][1] === 18 && m[1][1] === 50 && m[2][1] === 19 && m[2][2] === (want.radius >> 7) &&
      m[3][1] === 51 && m[3][2] === (want.radius & 0x7F), '14-bit pairs');
// 14-bit input: an LSB on CC + 32 places the axis between steps, and is taken.
d.send(0xB0, 48, 64);
check(d.bytes.length === 0, 'input LSB taken');
d.engine.tick(0.005);
m = d.take();
var fine = px.toPolar(px.ccToBipolar(96.5), 0, def, px.STEPS14);
check(m.length === 2 && ((m[0][2] << 7) | m[1][2]) === fine.radius, 'input LSB moves the radius');
// On CCs of 32 and up it stays 7-bit.
d.engine.setParam('outradius', 40);
check(/14-bit needs/.test(d.engine.status()), 'status says 14-bit is blocked');

// ---- motion (motionTests in tests/test_plugin.cpp) ----
d = device();
d.engine.setParam('smoothing', 100);
d.send(0xB0, 16, 127, 0xB0, 17, 64);
var p = run(d, 1);
check(p.angles.length === 1 && p.angles[0] === 0 && p.radii[0] === 127, 'first position is jumped to');
d.send(0xB0, 16, 0);
p = run(d, 60);
// Ticks are coarser than the CLAP's 32-sample steps, so the dip only comes near the centre.
check(p.radii.length > 20 && min(p.radii) < 10 && last(p.radii) === 127, 'smoothing glides through the centre');
check(last(p.angles) === 64, 'and ends on the target');
check(run(d, 5).radii.length === 0, 'settled: nothing more sent');

d.engine.setParam('path', 1);
d.send(0xB0, 16, 127);
p = run(d, 40);
check(p.radii.length === 0 || min(p.radii) >= 125, 'around keeps the radius');
check(p.angles.length > 15 && last(p.angles) === 0, 'around sweeps the angle');

d.engine.setParam('smoothing', 0);
d.engine.setParam('inertia', 60);
d.send(0xB0, 16, 64, 0xB0, 17, 127);
p = run(d, 1600);
check(max(p.angles) > 36 && last(p.angles) === 32, 'inertia swings past and settles');

var whirl = [];
[[0, 64], [64, 0], [127, 64], [64, 127], [0, 64], [64, 0]].forEach(function (xy) {
   d.send(0xB0, 16, xy[0], 0xB0, 17, xy[1]);
   whirl = whirl.concat(run(d, 28).angles);
});
var backwards = 0;
for (var i = 1; i < whirl.length; ++i) backwards += ((whirl[i] - whirl[i - 1] + 127) % 127) > 63 ? 1 : 0;
check(whirl.length > 30 && backwards <= 2, 'a whirl keeps turning the same way');

d.engine.setParam('inertia', 100);
d.send(0xB0, 16, 127, 0xB0, 17, 64);
run(d, 2);
d.engine.setParam('inertia', 0);
p = run(d, 1);
check(p.angles.length > 0 && last(p.angles) === 0, 'turning motion off lands at once');
check(run(d, 2).angles.length === 0, 'and stays landed');

// Coast: once the controller stops, the output carries on and stops somewhere else.
d = device();
d.engine.setParam('inertia', 50);
d.engine.setParam('inertiamode', 1);
d.send(0xB0, 16, 64, 0xB0, 17, 64);
run(d, 1);
for (var s = 70; s <= 100; s += 6) {
   d.send(0xB0, 16, s);
   run(d, 4);
}
p = run(d, 1000);
check(p.radii.length > 0 && !d.engine.channels[0].motion.moving, 'coast settles');

// Pad: a touch doesn't move it, a drag moves it by as much.
d = device();
d.engine.setParam('inertiamode', 2);
d.send(0xB0, 16, 64, 0xB0, 17, 64);
run(d, 30);
var start = d.engine.channels[0].motion.position();
d.send(0xB0, 16, 20, 0xB0, 17, 20); // a finger landing far away
run(d, 40);
var landed = d.engine.channels[0].motion.position();
check(Math.abs(landed[0] - start[0]) < 1e-9 && Math.abs(landed[1] - start[1]) < 1e-9, 'pad: a touch does not move it');

// Touch Filter: a resistive pad's skittery press and release never reach the output. A reading
// each 5 ms tick: two skittery ones as the finger presses, 100 ms held at (90, 90), two as it lifts.
function touch(filter) {
   var dev = device();
   dev.engine.setParam('touchfilter', filter);
   var readings = [[10, 12, 1], [100, 40, 1], [90, 90, 20], [60, 30, 1], [5, 2, 1]];
   var out = { angles: [], radii: [] };
   readings.forEach(function (r) {
      for (var k = 0; k < r[2]; ++k) {
         dev.send(0xB0, 16, r[0], 0xB0, 17, r[1]);
         var q = run(dev, 1);
         out.angles = out.angles.concat(q.angles);
         out.radii = out.radii.concat(q.radii);
      }
   });
   var q = run(dev, 40);
   out.angles = out.angles.concat(q.angles);
   out.radii = out.radii.concat(q.radii);
   return out;
}
var heldTouch = polar(90, 90);
check(touch(0).angles.length > 2, 'touch filter off: the skitter goes out');
[1, 2, 3].forEach(function (f) {
   var on = touch(f);
   check(on.angles.length === 1 && on.angles[0] === heldTouch.angle && on.radii.length === 1 &&
         on.radii[0] === heldTouch.radius, 'touch filter ' + f + ': only where the finger was held');
});

// The filter on its own: X and Y apart in time are one reading (see tests/test_touch.cpp).
var tf = new px.TouchFilter();
var tfIn = [[0, 0, 0], [0, 0, 0.002], [0, 0, 0.004], [0.2, 0, 0.05], [0.2, 0.2, 0.051], [0.2, 0.2, 0.056]];
var popped = [], r, next = 0;
for (var now = 0; now < 0.2; now += 0.0005) {
   while (next < tfIn.length && tfIn[next][2] <= now) {
      tf.push(tfIn[next][0], tfIn[next][1], tfIn[next][2], 0.01);
      ++next;
   }
   while ((r = tf.pop(now, 0.01))) popped.push(r);
}
check(popped.length === 2 && popped[1][0] === 0.2 && popped[1][1] === 0.2 && popped[0][0] === 0,
      'touch filter pairs X and Y');

// With the touch CC, a lone lift-off reading more than a window before the touch goes up still
// needs a second that agrees, so it never goes out (see tests/test_touch.cpp).
var tg = new px.TouchFilter(), moves = [];
function tgPoll(from, to) {
   for (var now = from; now <= to; now += 0.0005)
      while ((r = tg.pop(now, 0.01))) if (r[2] === 0) moves.push(r[0]);
}
tg.touch(true, 0, 0.01);
for (var i = 0; i < 10; ++i) {
   tg.push(0.2, 0.2, 0.002 + 0.01 * i, 0.01);
   tgPoll(0.002 + 0.01 * i, 0.002 + 0.01 * (i + 1) - 0.0005);
}
tg.push(0.9, 0.9, 0.102, 0.01);
tgPoll(0.102, 0.117);
tg.touch(false, 0.1175, 0.01);
tgPoll(0.1175, 0.2);
check(moves.length > 0 && moves.every(function (x) { return x === 0.2; }),
      'touch filter with touch CC: a lone lift-off reading never goes out');

// The real thing: a resistive pad played by hand (tests/data/resistive-pad-capture.txt), whose last
// two lifts skitter to a wrong spot just before its touch CC goes up.
function replay(settings) {
   var dev = device(), fs = require('fs');
   for (var k in settings) dev.engine.setParam(k, settings[k]);
   var lines = fs.readFileSync(path.join(__dirname, '..', 'tests', 'data', 'resistive-pad-capture.txt'), 'utf8')
      .split('\n').filter(function (l) { return l && l[0] !== '#'; })
      .map(function (l) { return l.split(' ').map(Number); });
   var out = { visited: [], touches: [], passed: [], passedTouch: [] }, angle = -1, radius = -1, next = 0;
   var px12 = -1, py13 = -1;
   for (var now = 0; now < 16; now += 0.004) {
      while (next < lines.length && lines[next][0] <= now) {
         dev.send(0xB0, lines[next][1], lines[next][2]);
         ++next;
      }
      dev.engine.tick(0.004);
      dev.take().forEach(function (m) {
         if (m[1] === 18) angle = m[2];
         if (m[1] === 19) radius = m[2];
         if (m[1] === 93) out.touches.push(m[2]);
         if (m[1] === 12) px12 = m[2];
         if (m[1] === 13) py13 = m[2];
         if (m[1] === 92) out.passedTouch.push(m[2]);
      });
      out.passed.push(px12 + ',' + py13);
      out.visited.push(angle + '/' + radius);
   }
   return out;
}
var skitter = [[64, 127], [60, 95]].map(function (xy) { var q = polar(xy[0], xy[1]); return q.angle + '/' + q.radius; });
function visits(o, spot) { return o.visited.indexOf(spot) >= 0; }
var capture = { inx: 12, iny: 13, touchout: 93 };
var rawRun = replay(capture);
check(visits(rawRun, skitter[0]) && visits(rawRun, skitter[1]), 'capture: unfiltered, the lifts skitter');
capture.touchfilter = 2;
var filtered = replay(capture);
check(!visits(filtered, skitter[0]) && !visits(filtered, skitter[1]), 'capture: Touch Filter drops the skitter');
capture.touchin = 92;
var gated = replay(capture);
check(!visits(gated, skitter[0]) && !visits(gated, skitter[1]), 'capture: with the touch CC too');
check(gated.touches.length >= 8 && gated.touches.every(function (v, i) { return v === (i % 2 ? 0 : 127); }) &&
      gated.touches[gated.touches.length - 1] === 0, 'capture: the touch goes out, down and up in turn');

// Pass Input CCs: Raw sends the skitter on as it came; Filtered sends only what Touch Filter let through.
capture.passthrough = 1;
var passRaw = replay(capture);
check(passRaw.passed.indexOf('64,127') >= 0 && passRaw.passed.indexOf('60,95') >= 0, 'capture: raw pass-through skitters');
capture.passthrough = 2;
var passFiltered = replay(capture);
check(passFiltered.passed.indexOf('64,127') < 0 && passFiltered.passed.indexOf('60,95') < 0 &&
      passFiltered.passed[passFiltered.passed.length - 1] !== '-1,-1', 'capture: filtered pass-through drops it');
check(passFiltered.passedTouch.length === gated.touches.length &&
      passFiltered.passedTouch.every(function (v, i) { return v === gated.touches[i]; }),
      'capture: filtered pass-through sends the touch in step');

// Polar Out off: no angle or radius goes out; back on, both go out again.
d = device();
d.send(0xB0, 16, 127, 0xB0, 17, 64);
run(d, 1);
d.engine.setParam('polarout', 0);
d.send(0xB0, 16, 0);
p = run(d, 5);
check(p.angles.length === 0 && p.radii.length === 0, 'polar out off: nothing goes out');
d.engine.setParam('polarout', 1);
p = run(d, 1);
check(p.angles.length === 1 && p.angles[0] === 64 && p.radii.length === 1, 'polar out back on: both go out');

if (failures) {
   console.log(failures + ' failure(s)');
   process.exit(1);
}
console.log('all Max for Live tests passed');
