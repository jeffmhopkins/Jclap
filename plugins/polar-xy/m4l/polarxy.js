// Polar XY for Max for Live: the CLAP plugin's conversion and motion (src/polar.h, src/motion.h and the
// MIDI handling in src/plugin.cpp), ported to the JavaScript Max's [js] object runs. Kept to ES5, which
// is what [js] speaks. The engine at the top has no Max dependencies, so node runs the tests on it
// (test_polarxy.js); the Max glue is at the bottom.
//
// In the device: [midiin] -> [js polarxy.js] -> [midiout]. Bytes come in one at a time; anything that
// isn't an X/Y input CC goes straight back out. A [metro] sends `tick` every few milliseconds, which
// converts the CCs that arrived since (X and Y together, as one move) and moves any glide on.

if (typeof jsarguments !== 'undefined') {
   inlets = 1;
   outlets = 2; // 0: MIDI bytes to [midiout]; 1: `set <text>` for the status line
   autowatch = 0;
}

var PI = Math.PI;

function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- polar.h ----

// Maps a 0..127 CC value (or a position between the steps) to -1..+1 with 64 as dead center.
function ccToBipolar(v) {
   v = clamp(v, 0, 127);
   return v >= 64 ? (v - 64) / 63 : (v - 64) / 64;
}

// Inside this radius the angle holds and the radius is 0, so jitter at rest can't swing the Orb.
var DEAD_ZONE = 0.03;
var STEPS7 = 127, STEPS14 = 16383;

// This script's version, the plugin's (CMakeLists.txt; a test keeps them the same). The status line
// ends with it, so it shows which copy of the script Live loaded.
var VERSION = '0.19.3';

function wrapDegrees(deg) {
   deg = deg % 360;
   if (deg < 0) deg += 360;
   return deg;
}

// The angle of x/y in degrees, 0..360, with the direction and offset applied.
function polarDegrees(x, y, s) {
   var deg = Math.atan2(y, x) * 180 / PI;
   if (s.clockwise) deg = -deg;
   return wrapDegrees(deg + s.angleOffset);
}

// x/y on -1..+1, Y up. s: {angleOffset (deg), clockwise, fillSquare}. steps: 127, or 16383 for 14-bit.
function toPolar(x, y, s, steps) {
   steps = steps || STEPS7;
   var out = { angle: 0, radius: 0, hasAngle: false };
   var r = s.fillSquare ? Math.max(Math.abs(x), Math.abs(y)) : Math.sqrt(x * x + y * y);
   r = clamp(r, 0, 1);
   if (r < DEAD_ZONE) return out;
   out.radius = Math.round((r - DEAD_ZONE) / (1 - DEAD_ZONE) * steps);
   var deg = polarDegrees(x, y, s);
   // A turn is 0..steps-1: what would round to a full turn is the same angle as 0.
   out.angle = Math.round(deg / 360 * steps) % steps;
   out.hasAngle = true;
   return out;
}

// ---- motion.h ----

var MODE_PULL = 0, MODE_COAST = 1, MODE_PAD = 2;

function motionActive(m) { return m.smoothing > 0 || m.inertia > 0 || m.mode === MODE_PAD; }
function inertiaPeriod(inertia) { return 1.5 * inertia * (1 + 2.5 * inertia); }
function coastTime(inertia) { return 0.4 * inertiaPeriod(inertia); }
var COAST_HOLD = 0.1, COAST_BOUNCE = 0.5, PAD_BOUNCE = 0.8, PAD_JUMP = 0.25;

function wrapPi(a) { return a - 2 * PI * Math.round(a / (2 * PI)); }

// Follows a target on the -1..+1 pad: see Follower in src/motion.h, which this mirrors line for line.
function Follower() {
   this.around = false; this.placed = false; this.moving = false;
   this.tx = 0; this.ty = 0; this.rx = 0; this.ry = 0;
   this.still = 0; this.fresh = false; this.kick = [0, 0];
   this.px = 0; this.py = 0; this.padMove = [0, 0]; this.padVel = [0, 0];
   this.padCatch = false; this.padLift = false;
   this.target = [0, 0]; this.real = [0, 0]; this.drift = [0, 0];
   this.smooth = [0, 0]; this.pos = [0, 0]; this.vel = [0, 0];
}

Follower.prototype.place = function (x, y) {
   this.tx = this.rx = x;
   this.ty = this.ry = y;
   this.toSpace(x, y, this.target);
   for (var i = 0; i < 2; ++i) {
      this.real[i] = this.smooth[i] = this.pos[i] = this.target[i];
      this.vel[i] = this.drift[i] = 0;
   }
   this.still = COAST_HOLD;
   this.placed = true;
   this.moving = false;
};

Follower.prototype.follow = function (x, y) {
   if (!this.placed) {
      this.place(x, y);
      this.px = x;
      this.py = y;
      return;
   }
   if (x === this.rx && y === this.ry) return;
   this.padFollow(x, y);
   this.tx = this.rx = x;
   this.ty = this.ry = y;
   var t = [0, 0];
   this.toSpace(x, y, t);
   var a = this.still < COAST_HOLD ? 1 - Math.exp(-this.still / 0.05) : 0;
   this.fresh = this.still >= COAST_HOLD;
   for (var i = 0; i < 2; ++i) {
      this.kick[i] = this.fresh ? (t[i] - this.real[i]) / COAST_HOLD : 0;
      this.drift[i] = this.still < COAST_HOLD ? this.drift[i] : 0;
      if (this.still > 0) this.drift[i] += a * ((t[i] - this.real[i]) / this.still - this.drift[i]);
      this.real[i] = this.target[i] = t[i];
   }
   this.still = 0;
   this.moving = true;
};

Follower.prototype.land = function () {
   if (this.moving && this.coasting()) this.restTarget();
   if (this.moving) this.place(this.tx, this.ty);
};

Follower.prototype.setAround = function (around) {
   if (around === this.around) return;
   var p = this.position();
   var tx = this.rx, ty = this.ry;
   var wasMoving = this.moving;
   this.around = around;
   if (!this.placed) return;
   this.place(p[0], p[1]);
   if (wasMoving) this.follow(tx, ty);
};

Follower.prototype.position = function () {
   if (!this.moving) return [this.tx, this.ty];
   return this.toPad(this.pos);
};

Follower.prototype.step = function (dt, m) {
   if (!this.moving) return false;
   if (!motionActive(m)) {
      this.land();
      return false;
   }
   if (m.mode === MODE_PAD) return this.padStep(dt, m);
   if (m.mode === MODE_COAST && m.inertia > 0) return this.coastStep(dt, m);
   if (m.inertia > 0) this.coast(dt, m);
   else this.drift[0] = this.drift[1] = 0;

   var i;
   var a = m.smoothing > 0 ? 1 - Math.exp(-dt * 3 / m.smoothing) : 1;
   for (i = 0; i < 2; ++i) this.smooth[i] += a * (this.target[i] - this.smooth[i]);

   if (m.inertia > 0) {
      var w = 2 * PI / inertiaPeriod(m.inertia);
      var zeta = 0.85 - 0.7 * m.inertia;
      var n = Math.max(1, Math.ceil(w * dt / 0.05));
      var h = dt / n;
      for (var k = 0; k < n; ++k)
         for (i = 0; i < 2; ++i) {
            this.vel[i] += h * (w * w * (this.smooth[i] - this.pos[i]) - 2 * zeta * w * this.vel[i]);
            this.pos[i] += h * this.vel[i];
         }
   } else {
      for (i = 0; i < 2; ++i) {
         this.pos[i] = this.smooth[i];
         this.vel[i] = 0;
      }
   }

   var eps = 1e-4;
   for (i = 0; i < 2; ++i)
      if (Math.abs(this.target[i] - this.pos[i]) > eps || Math.abs(this.target[i] - this.smooth[i]) > eps ||
          Math.abs(this.vel[i]) > eps * 10 || Math.abs(this.drift[i]) > 0)
         return true;
   this.land();
   return false;
};

Follower.prototype.padFollow = function (x, y) {
   var d = [x - this.px, y - this.py];
   this.px = x;
   this.py = y;
   var jump = Math.sqrt(d[0] * d[0] + d[1] * d[1]) > PAD_JUMP;
   var i;
   if (this.still >= COAST_HOLD) {
      this.padCatch = true;
      this.padLift = false;
      this.padVel[0] = this.padVel[1] = 0;
      if (!jump)
         for (i = 0; i < 2; ++i) this.padMove[i] += d[i];
   } else if (jump) {
      this.padLift = true;
   } else {
      this.padLift = false;
      var a = 1 - Math.exp(-this.still / 0.05);
      for (i = 0; i < 2; ++i) {
         this.padMove[i] += d[i];
         if (this.still > 0) this.padVel[i] += a * (d[i] / this.still - this.padVel[i]);
      }
   }
};

Follower.prototype.padStep = function (dt, m) {
   this.still += dt;
   var scale = [this.around ? PI : 1, this.around ? 0.5 : 1];
   if (this.padCatch) {
      this.vel[0] = this.vel[1] = 0;
      this.padCatch = false;
   }
   var driven = this.still < COAST_HOLD && !this.padLift;
   var tau = coastTime(m.inertia);
   var i;
   for (i = 0; i < 2; ++i) {
      this.pos[i] += this.padMove[i] * scale[i];
      this.padMove[i] = 0;
      if (driven) {
         this.vel[i] = this.padVel[i] * scale[i];
      } else if (tau > 0) {
         var drag = Math.exp(-dt / tau);
         this.pos[i] += this.vel[i] * tau * (1 - drag);
         this.vel[i] *= drag;
      } else {
         this.vel[i] = 0;
      }
   }
   if (this.around && this.pos[1] < 0) {
      this.pos[0] += PI;
      this.pos[1] = -this.pos[1];
      this.vel[1] = -this.vel[1];
   }
   for (i = this.around ? 1 : 0; i < 2; ++i)
      if (Math.abs(this.pos[i]) > 1) {
         this.pos[i] = clamp(this.pos[i], -1, 1);
         if (this.pos[i] * this.vel[i] > 0) this.vel[i] *= -PAD_BOUNCE;
      }
   if (driven || Math.abs(this.vel[0]) * tau > 1e-3 || Math.abs(this.vel[1]) * tau > 1e-3) return true;
   var p = this.toPad(this.pos);
   this.place(clamp(p[0], -1, 1), clamp(p[1], -1, 1));
   return false;
};

Follower.prototype.coastStep = function (dt, m) {
   this.still += dt;
   var driven = this.still < COAST_HOLD;
   var tau = coastTime(m.inertia);
   var take = 1 - Math.exp(-dt * 2 * PI / inertiaPeriod(m.inertia));
   var drag = Math.exp(-dt / tau);
   var i;
   for (i = 0; i < 2; ++i) {
      var speed = this.fresh ? this.kick[i] : this.drift[i];
      this.vel[i] = driven ? this.vel[i] + take * (speed - this.vel[i]) : this.vel[i] * drag;
      this.pos[i] += this.vel[i] * dt;
   }
   if (this.around && this.pos[1] < 0) {
      this.pos[0] += PI;
      this.pos[1] = -this.pos[1];
      this.vel[1] = -this.vel[1];
   }
   for (i = this.around ? 1 : 0; i < 2; ++i)
      if (Math.abs(this.pos[i]) > 1 && this.pos[i] * this.vel[i] > 0) this.vel[i] *= -COAST_BOUNCE;
   if (driven || Math.abs(this.vel[0]) * tau > 1e-3 || Math.abs(this.vel[1]) * tau > 1e-3) return true;
   var p = this.toPad(this.pos);
   this.place(clamp(p[0], -1, 1), clamp(p[1], -1, 1));
   return false;
};

Follower.prototype.coasting = function () { return this.still >= COAST_HOLD; };

Follower.prototype.coast = function (dt, m) {
   var before = this.still;
   this.still += dt;
   if (!this.coasting() || (this.drift[0] === 0 && this.drift[1] === 0)) return;
   var ran = before < COAST_HOLD ? this.still : dt;
   var tau = coastTime(m.inertia);
   var drag = Math.exp(-ran / tau);
   var i;
   for (i = 0; i < 2; ++i) {
      this.target[i] += this.drift[i] * tau * (1 - drag);
      this.drift[i] *= drag;
   }
   for (i = this.around ? 1 : 0; i < 2; ++i)
      if (Math.abs(this.target[i]) > 1 && this.target[i] * this.drift[i] > 0) this.drift[i] *= -COAST_BOUNCE;
   if (Math.abs(this.drift[0]) * tau < 1e-3 && Math.abs(this.drift[1]) * tau < 1e-3)
      this.drift[0] = this.drift[1] = 0;
   var p = this.toPad(this.target);
   this.tx = p[0];
   this.ty = p[1];
};

Follower.prototype.restTarget = function () {
   if (this.target[0] === this.real[0] && this.target[1] === this.real[1]) return;
   var p = this.toPad(this.target);
   this.tx = this.rx = clamp(p[0], -1, 1);
   this.ty = this.ry = clamp(p[1], -1, 1);
};

Follower.prototype.toPad = function (p) {
   if (this.around) return [p[1] * Math.cos(p[0]), p[1] * Math.sin(p[0])];
   return [p[0], p[1]];
};

Follower.prototype.toSpace = function (x, y, out) {
   if (!this.around) {
      out[0] = x;
      out[1] = y;
      return;
   }
   var r = Math.sqrt(x * x + y * y);
   var ref = this.placed ? this.target[0] : 0;
   out[0] = r < DEAD_ZONE ? ref : ref + wrapPi(Math.atan2(y, x) - ref);
   out[1] = r;
};

// A Follower whose Around motion fades to Straight near the centre: see CenterFollower in
// src/motion.h.
function CenterFollower() {
   this.main = new Follower();
   this.straight = new Follower();
   this.around = false;
   this.zone = 0;
}
CenterFollower.prototype.blending = function () { return this.around && this.zone > 0; };
Object.defineProperty(CenterFollower.prototype, 'placed', { get: function () { return this.main.placed; } });
Object.defineProperty(CenterFollower.prototype, 'moving', {
   get: function () { return this.main.moving || (this.blending() && this.straight.moving); }
});
CenterFollower.prototype.place = function (x, y) { this.main.place(x, y); this.straight.place(x, y); };
CenterFollower.prototype.follow = function (x, y) { this.main.follow(x, y); this.straight.follow(x, y); };
CenterFollower.prototype.land = function () { this.main.land(); this.straight.land(); };
CenterFollower.prototype.setAround = function (around) { this.around = around; this.main.setAround(around); };
CenterFollower.prototype.position = function () {
   var p = this.main.position();
   if (!this.blending()) return p;
   var s = this.straight.position();
   var w = Math.min(1, Math.sqrt(s[0] * s[0] + s[1] * s[1]) / this.zone);
   return [s[0] + w * (p[0] - s[0]), s[1] + w * (p[1] - s[1])];
};
CenterFollower.prototype.step = function (dt, m) {
   var a = this.main.step(dt, m);
   var b = this.straight.step(dt, m);
   return a || (this.blending() && b);
};

// ---- touch.h ----

var TOUCH_PAIR = 0.003, TOUCH_JUMP = 0.25, TOUCH_LIFT = 0.05, TOUCH_AGREE = 0.06;

// Throws away the press and release of each touch on a resistive pad: see TouchFilter in src/touch.h,
// which this mirrors line for line.
function TouchFilter() { this.reset(); }

var TOUCH_MOVE = 0, TOUCH_DOWN = 1, TOUCH_UP = 2; // what pop gives

TouchFilter.prototype.reset = function () {
   this.window = 0;
   this.open = false; this.openX = 0; this.openY = 0; this.openT = 0;
   this.hasLast = false; this.lastX = 0; this.lastY = 0; this.lastT = 0;
   this.settling = false; this.start = 0; this.readings = 0;
   this.candX = 0; this.candY = 0; this.prevX = 0; this.prevY = 0;
   this.gated = false; this.touched = false; this.downDue = false; this.downOut = false;
   this.pressed = false; // settling a press the touch CC started, so one reading will do
   this.queue = [];
   // Tuning (kept by reset): the press (below 0, the window), the jump limit and the agreement.
   this.hasPrev = false; this.prevRX = 0; this.prevRY = 0; this.prevT = 0;
   if (this.pressTime === undefined) {
      this.pressTime = -1; this.jump = TOUCH_JUMP; this.agree = TOUCH_AGREE;
   }
};

TouchFilter.prototype.setPress = function (press) { this.pressTime = press; };
TouchFilter.prototype.setJump = function (jump) { this.jump = jump; };
TouchFilter.prototype.setAgree = function (agree) { this.agree = Math.max(agree, 0); };
TouchFilter.prototype.press = function () { return this.pressTime < 0 ? this.window : this.pressTime; };

// The touch CC: a finger down or up at `now`. From the first one on, the touch decides the edges.
TouchFilter.prototype.touch = function (down, now, window) {
   this.window = window;
   this.gated = true;
   if (down === this.touched) return;
   this.touched = down;
   this.hasPrev = false;
   this.open = false;
   if (down) {
      this.settling = true;
      this.start = now;
      this.readings = 0;
      this.downDue = true;
      this.pressed = true;
      return;
   }
   var downHeld = false;
   for (var i = 0; i < this.queue.length; ++i) downHeld = downHeld || this.queue[i].event === TOUCH_DOWN;
   this.queue = [];
   this.settling = false;
   this.pressed = false;
   this.downDue = false;
   if (this.downOut && !downHeld) this.hold(0, 0, now, TOUCH_UP);
   this.downOut = false;
};

TouchFilter.prototype.push = function (x, y, now, window) {
   this.window = window;
   if (this.open && now - this.openT < TOUCH_PAIR) {
      this.openX = x;
      this.openY = y;
      return;
   }
   if (this.open) this.finalize();
   this.open = true;
   this.openX = x;
   this.openY = y;
   this.openT = now;
};

// The next event due by `now` as [x, y, event], oldest first, or null.
TouchFilter.prototype.pop = function (now, window) {
   this.window = window;
   if (this.open && now - this.openT >= TOUCH_PAIR) this.finalize();
   if (this.settling && now >= this.start + this.press()) this.settle();
   if (!this.queue.length || this.queue[0].due > now) return null;
   var r = this.queue.shift();
   return [r.x, r.y, r.event];
};

TouchFilter.prototype.busy = function () { return this.open || this.settling || this.queue.length > 0; };

TouchFilter.prototype.finalize = function () {
   this.open = false;
   var x = this.openX, y = this.openY, t = this.openT;
   if (this.gated && !this.touched) return;
   if (this.settling && t >= this.start + this.press()) this.settle();
   var step = this.hasLast ? Math.sqrt((x - this.lastX) * (x - this.lastX) + (y - this.lastY) * (y - this.lastY)) : 0;
   // From the last reading or from where the finger was headed, whichever is nearer.
   var off = step;
   if (this.hasPrev && t - this.lastT < TOUCH_LIFT && this.lastT > this.prevT) {
      var k = Math.min((t - this.lastT) / (this.lastT - this.prevT), 2);
      var hx = x - (this.lastX + (this.lastX - this.prevRX) * k), hy = y - (this.lastY + (this.lastY - this.prevRY) * k);
      off = Math.min(step, Math.sqrt(hx * hx + hy * hy));
   }
   // The pause only means a new touch without the touch CC (see src/touch.h).
   var paused = !this.gated && t - this.lastT >= TOUCH_LIFT && step > this.agree;
   var onset = !this.hasLast || off > Math.max(this.jump, this.agree) || paused;
   if (onset && this.window > 0) {
      // Still the touch CC's press if nothing has arrived in it yet; a jump later in the touch
      // (often the finger starting to lift) needs two readings that agree, as without it.
      this.pressed = this.pressed && this.settling && this.readings === 0;
      this.queue = [];
      this.settling = true;
      this.start = t;
      this.readings = 0;
   }
   if (this.settling) {
      this.prevX = this.candX;
      this.prevY = this.candY;
      this.candX = x;
      this.candY = y;
      ++this.readings;
   } else {
      this.hold(x, y, t + this.window);
   }
   this.hasPrev = this.hasLast && !onset;
   this.prevRX = this.lastX; this.prevRY = this.lastY; this.prevT = this.lastT;
   this.hasLast = true;
   this.lastX = x;
   this.lastY = y;
   this.lastT = t;
};

TouchFilter.prototype.settle = function () {
   if (this.pressed && this.readings === 0) return; // the touch CC's press waits for its first reading
   this.settling = false;
   var dx = this.candX - this.prevX, dy = this.candY - this.prevY;
   var rested = this.readings >= 2 && Math.sqrt(dx * dx + dy * dy) <= this.agree;
   if (rested || (this.pressed && this.readings >= 1)) this.hold(this.candX, this.candY, this.start + this.press());
   this.pressed = false;
};

TouchFilter.prototype.hold = function (x, y, due, event) {
   event = event || TOUCH_MOVE;
   if (event === TOUCH_MOVE && this.downDue) {
      this.downDue = false;
      this.downOut = true;
      this.hold(x, y, due, TOUCH_DOWN);
   }
   if (this.queue.length === 64) this.queue.shift();
   this.queue.push({ x: x, y: y, due: due, event: event });
};

// ---- the plugin's MIDI handling (src/plugin.cpp) ----

// Name: [min, max, default]. The same ranges and defaults as the CLAP's params (src/params.h).
var PARAMS = {
   inx: [0, 127, 16],
   iny: [0, 127, 17],
   outangle: [0, 127, 18],
   outradius: [0, 127, 19],
   angleoffset: [0, 360, 0],
   direction: [0, 1, 0],    // 0 counter-clockwise, 1 clockwise
   radiusmode: [0, 1, 0],   // 0 clamp to circle, 1 fill square
   passthrough: [0, 2, 0],  // forward the X/Y and touch CCs: 0 off, 1 raw, 2 filtered
   smoothing: [0, 2000, 0], // ms
   inertia: [0, 100, 0],    // %
   path: [0, 1, 0],         // 0 straight, 1 around
   resolution: [0, 1, 0],   // 0 7-bit, 1 14-bit
   inertiamode: [0, 2, 0],  // 0 pull, 1 coast, 2 pad
   touchfilter: [0, 100, 0], // ms, 0 off
   touchin: [-1, 127, -1],  // the pad's touch CC, -1 none
   touchout: [-1, 127, -1], // the touch sent on, -1 none
   polarout: [0, 1, 1],     // 0: no angle or radius goes out
   centersmoothing: [0, 50, 0], // %: the Center zone, where the angle is steadied and Around goes straight
   touchjump: [8, 64, 16],  // CC steps one reading may jump before a second must agree
   touchpress: [-1, 100, -1], // ms a press is held back, -1 the same as touchfilter
   touchagree: [1, 16, 4]   // CC steps two readings may differ by and still agree
};

function Channel() {
   this.x = 64; this.y = 64; this.fx = 64; this.fy = 64;
   this.hasX = false; this.hasY = false;
   this.angle = -1; this.sentAngle = -1; this.sentRadius = -1; this.sentTouch = -1;
   this.motion = new CenterFollower();
   this.touch = new TouchFilter();
   this.centerX = 0; this.centerY = 0; this.centerSet = false; // Center's anchor
}

// emit(status, data1, data2) sends one MIDI message out.
function Engine(emit) {
   this.emit = emit;
   this.p = {};
   for (var name in PARAMS) this.p[name] = PARAMS[name][2];
   this.channels = [];
   for (var c = 0; c < 16; ++c) this.channels.push(new Channel());
   this.pending = [];
   this.time = 0; // seconds of ticks, Touch Filter's clock
   this.lastChannel = 0; // the channel that moved last: the SEND buttons and the status line use it
   this.inX = this.int('inx');
   this.inY = this.int('iny');
   this.applied = this.settings();
   this.learnText = ''; this.learnTime = 0; // the last SEND, for the status line
   this.learning = null; // a SEND's wiggle under way
}

Engine.prototype.int = function (name) { return Math.floor(this.p[name] + 0.5); };

// Sets a param by name. Unknown names and non-numbers are ignored.
Engine.prototype.setParam = function (name, value) {
   var range = PARAMS[name];
   if (!range || typeof value !== 'number' || !isFinite(value)) return;
   this.convertPending();
   var was = this.p[name];
   this.p[name] = clamp(value, range[0], range[1]);
   this.applySettings();
   // Typing an Out CC (even the one already there) sends that output's value once on it. Not while
   // the device loads, when every control sends its value once.
   var which = { outangle: 'angle', outradius: 'radius', touchout: 'touch' }[name];
   // A new angle or radius CC already sent the position there, if there is one.
   var st = this.channels[this.lastChannel];
   var resent = which !== 'touch' && this.p[name] !== was && (which === 'angle' ? st.sentAngle : st.sentRadius) >= 0;
   if (which && this.time >= LOAD_SETTLE && !resent) this.sendOnce(which);
};

// One output's value, once, on its CC: the value last sent (64 before any; touch released before
// any). Angle and radius at 14 bits go as their MSB and LSB.
Engine.prototype.sendOnce = function (which) {
   var c = this.lastChannel, st = this.channels[c], cc, value;
   if (which === 'touch') {
      cc = this.touchOut();
      if (cc < 0) return;
      value = st.sentTouch === 127 ? 127 : 0;
      this.emit(0xB0 | c, cc, value);
   } else {
      if (!this.applied.polar || this.conflicting()) return;
      cc = this.int(which === 'angle' ? 'outangle' : 'outradius');
      var sent = which === 'angle' ? st.sentAngle : st.sentRadius;
      if (sent >= 0) this.pushValue(c, cc, sent);
      else this.emit(0xB0 | c, cc, LEARN_VALUE);
      value = sent >= 0 ? sent >> (this.applied.fine ? 7 : 0) : LEARN_VALUE;
   }
   this.learned(which + ': sent CC ' + cc + ' = ' + value + ' on ch ' + (c + 1));
};

// Seconds after the device starts ticking before a typed Out CC counts as typed.
var LOAD_SETTLE = 1;

Engine.prototype.conflicting = function () {
   return this.int('inx') === this.int('iny') || this.int('outangle') === this.int('outradius');
};

// 14-bit needs both output CCs below 32, for their LSBs on CC + 32.
Engine.prototype.fineWanted = function () { return this.int('resolution') !== 0; };
Engine.prototype.fineBlocked = function () {
   return this.fineWanted() && (this.int('outangle') >= 32 || this.int('outradius') >= 32);
};

// Touch In CC, or -1 for none (or when it would take one of the X/Y CCs).
Engine.prototype.touchIn = function () {
   var cc = Math.round(this.p.touchin), inX = this.int('inx'), inY = this.int('iny');
   if (cc < 0 || cc === inX || cc === inY || (inX < 32 && cc === inX + 32) || (inY < 32 && cc === inY + 32)) return -1;
   return cc;
};

// Touch Out CC, or -1 for none (or when it would take one of the angle/radius CCs).
Engine.prototype.touchOut = function () {
   var cc = Math.round(this.p.touchout), s = this.applied;
   if (cc < 0 || cc === s.outAngle || cc === s.outRadius || (s.fine && (cc === s.outAngle + 32 || cc === s.outRadius + 32)))
      return -1;
   return cc;
};

// Pass Input CCs on Filtered: a reading Touch Filter let through, sent on as the X/Y CCs it came in
// on (with each axis's LSB too where a 14-bit controller sent one).
Engine.prototype.passReading = function (channel, x, y) {
   var ccs = [this.int('inx'), this.int('iny')], v = [x, y];
   for (var i = 0; i < 2; ++i) {
      var cc = clamp(v[i] >= 0 ? 64 + v[i] * 63 : 64 + v[i] * 64, 0, 127);
      var msb = Math.floor(cc + 1e-9), lsb = clamp(Math.round((cc - msb) * 128), 0, 127);
      this.emit(0xB0 | channel, ccs[i], msb);
      if (ccs[i] < 32 && lsb > 0) this.emit(0xB0 | channel, ccs[i] + 32, lsb);
   }
};

Engine.prototype.sendTouch = function (channel, down) {
   var st = this.channels[channel], cc = this.touchOut(), value = down ? 127 : 0;
   if (cc < 0 || this.conflicting() || value === st.sentTouch) return;
   this.emit(0xB0 | channel, cc, value);
   st.sentTouch = value;
};

// 1 to 3 are the old choices (10, 20, 40 ms), as saved before Touch Filter was in ms.
Engine.prototype.touchWindow = function () {
   var ms = this.int('touchfilter');
   return (ms >= 1 && ms <= 3 ? 10 << (ms - 1) : Math.max(ms, 0)) / 1000;
};

// Touch Filter's tuning (CC steps are 2/127 of the -1..+1 pad).
Engine.prototype.tune = function (f) {
   var press = Math.round(this.p.touchpress);
   f.setPress(press < 0 ? -1 : press / 1000);
   f.setJump(this.int('touchjump') * 2 / 127);
   f.setAgree(this.int('touchagree') * 2 / 127);
};

Engine.prototype.motionSettings = function () {
   return {
      smoothing: this.p.smoothing / 1000,
      inertia: this.p.inertia / 100,
      mode: clamp(this.int('inertiamode'), 0, 2)
   };
};

Engine.prototype.settings = function () {
   var outAngle = this.int('outangle'), outRadius = this.int('outradius');
   return {
      outAngle: outAngle,
      outRadius: outRadius,
      clockwise: this.int('direction') !== 0,
      fillSquare: this.int('radiusmode') !== 0,
      angleOffset: this.p.angleoffset,
      around: this.int('path') !== 0,
      fine: this.fineWanted() && outAngle < 32 && outRadius < 32,
      polar: this.int('polarout') !== 0
   };
};

function sameSettings(a, b) {
   return a.outAngle === b.outAngle && a.outRadius === b.outRadius && a.clockwise === b.clockwise &&
          a.fillSquare === b.fillSquare && a.angleOffset === b.angleOffset && a.around === b.around &&
          a.fine === b.fine && a.polar === b.polar;
}

// A changed setting changes what each channel's position means, so each sends it again.
Engine.prototype.applySettings = function () {
   var inX = this.int('inx'), inY = this.int('iny');
   var c, st;
   if (inX !== this.inX || inY !== this.inY) {
      for (c = 0; c < 16; ++c) {
         st = this.channels[c];
         st.hasX = st.hasX && inX === this.inX;
         st.hasY = st.hasY && inY === this.inY;
      }
      this.inX = inX;
      this.inY = inY;
   }
   var now = this.settings();
   var was = this.applied;
   if (sameSettings(now, was)) return;
   var moved = now.outAngle !== was.outAngle || now.outRadius !== was.outRadius || (now.polar && !was.polar);
   var rescaled = now.fine !== was.fine;
   this.applied = now;
   for (c = 0; c < 16; ++c) {
      st = this.channels[c];
      if (rescaled) {
         if (st.angle >= 0)
            st.angle = now.fine ? Math.round(st.angle * STEPS14 / STEPS7)
                                : Math.round(st.angle * STEPS7 / STEPS14) % STEPS7;
         st.sentAngle = st.sentRadius = -1;
      }
      st.motion.setAround(now.around);
      if (moved) st.sentAngle = st.sentRadius = -1;
      this.convert(c);
   }
};

Engine.prototype.convertPending = function () {
   var list = this.pending;
   this.pending = [];
   for (var i = 0; i < list.length; ++i) this.convert(list[i], true);
};

// A controller's new reading (`fresh`) goes through Touch Filter if it's on; anything else (a
// setting changed) only sends again.
Engine.prototype.convert = function (channel, fresh) {
   var st = this.channels[channel];
   if (!st.hasX || !st.hasY || this.conflicting()) return;
   var x = ccToBipolar(st.fx), y = ccToBipolar(st.fy);
   var filtered = this.touchWindow() > 0;
   if (filtered && fresh) {
      this.tune(st.touch);
      st.touch.push(x, y, this.time, this.touchWindow());
      return;
   }
   if (!filtered) this.moveTo(st, x, y);
   this.send(channel);
};

Engine.prototype.moveTo = function (st, x, y) {
   st.motion.zone = this.p.centersmoothing / 100;
   if (motionActive(this.motionSettings())) st.motion.follow(x, y);
   else st.motion.place(x, y);
};

// Sends on what Touch Filter has let through by now.
Engine.prototype.releaseTouches = function () {
   var window = this.touchWindow();
   for (var c = 0; c < 16; ++c) {
      var st = this.channels[c], any = false, r;
      while (st.touch.busy() && (r = st.touch.pop(this.time, window))) {
         var pass = this.int('passthrough') === 2;
         if (r[2] === TOUCH_MOVE) {
            this.moveTo(st, r[0], r[1]);
            if (pass) this.passReading(c, r[0], r[1]);
            any = true;
            continue;
         }
         if (pass && this.touchIn() >= 0) this.emit(0xB0 | c, this.touchIn(), r[2] === TOUCH_DOWN ? 127 : 0);
         if (any) this.send(c);
         any = false;
         this.sendTouch(c, r[2] === TOUCH_DOWN);
      }
      if (any) this.send(c);
   }
};

Engine.prototype.send = function (channel) {
   var st = this.channels[channel];
   if (!st.motion.placed || this.conflicting()) return;
   var s = this.applied;
   st.motion.zone = this.p.centersmoothing / 100;
   var pos = st.motion.position();
   var steps = s.fine ? STEPS14 : STEPS7;
   var p = toPolar(pos[0], pos[1], s, steps);
   if (p.hasAngle) st.angle = this.centered(st, pos[0], pos[1], s, p, steps);
   if (!s.polar) return; // Polar Out off: nothing goes out, nothing counts as sent
   if (st.angle >= 0 && st.angle !== st.sentAngle) {
      this.pushValue(channel, s.outAngle, st.angle);
      st.sentAngle = st.angle;
   }
   if (p.radius !== st.sentRadius) {
      this.pushValue(channel, s.outRadius, p.radius);
      st.sentRadius = p.radius;
   }
};

// Center: near the centre the angle is read from an anchor the position drags along on a string
// (CENTER_STEADY steps long in the inner half of the zone, none at its edge), so a wobble shorter than
// the string doesn't turn it and a real move turns it at once. See centered in src/plugin.cpp.
var CENTER_STEADY = 4;
Engine.prototype.centered = function (st, x, y, s, p, steps) {
   var zone = this.p.centersmoothing / 100;
   var near = zone > 0 ? Math.max(0, 1 - p.radius / steps / zone) : 0;
   var string = CENTER_STEADY * 2 / 127 * Math.min(1, 2 * near);
   var dx = x - st.centerX, dy = y - st.centerY, d = Math.sqrt(dx * dx + dy * dy);
   if (!st.centerSet || d > string) {
      var keep = st.centerSet && d > 0 ? string / d : 0;
      st.centerX = x - dx * keep;
      st.centerY = y - dy * keep;
      st.centerSet = true;
   }
   if (Math.sqrt(st.centerX * st.centerX + st.centerY * st.centerY) < 1e-9) return st.angle >= 0 ? st.angle : p.angle;
   return Math.round(polarDegrees(st.centerX, st.centerY, s) / 360 * steps) % steps;
};

Engine.prototype.pushValue = function (channel, cc, value) {
   if (!this.applied.fine) {
      this.emit(0xB0 | channel, cc, value);
      return;
   }
   this.emit(0xB0 | channel, cc, value >> 7);
   this.emit(0xB0 | channel, cc + 32, value & 0x7F);
};

// A CC from the track. Returns true if it was an X/Y input (taken, not passed on).
Engine.prototype.handleCC = function (channel, cc, value) {
   if (this.conflicting()) return false;
   var inX = this.int('inx'), inY = this.int('iny');
   var lsbX = inX < 32 && cc === inX + 32 && cc !== inY;
   var lsbY = inY < 32 && cc === inY + 32 && cc !== inX;
   var touch = cc === this.touchIn();
   if (cc !== inX && cc !== inY && !lsbX && !lsbY && !touch) return false;
   var pass = this.int('passthrough');
   if (pass === 1 || (pass === 2 && this.touchWindow() <= 0)) this.emit(0xB0 | channel, cc, value);
   var st = this.channels[channel];
   if (touch) {
      this.convertPending(); // readings before the edge are on their side of it
      this.lastChannel = channel;
      if (this.touchWindow() > 0) {
         this.tune(st.touch);
         st.touch.touch((value & 0x7F) >= 64, this.time, this.touchWindow());
      }
      else this.sendTouch(channel, (value & 0x7F) >= 64);
      return true;
   }
   value &= 0x7F;
   if (cc === inX) {
      st.x = st.fx = value;
      st.hasX = true;
   } else if (cc === inY) {
      st.y = st.fy = value;
      st.hasY = true;
   } else if (lsbX) {
      st.fx = Math.min(st.x + value / 128, 127);
   } else {
      st.fy = Math.min(st.y + value / 128, 127);
   }
   this.lastChannel = channel;
   if (this.pending.indexOf(channel) < 0) this.pending.push(channel);
   return true;
};

// Converts what arrived since the last tick, then moves every glide dt seconds on.
Engine.prototype.tick = function (dt) {
   this.convertPending();
   this.time += dt;
   if (this.learning && this.time >= this.learning.next - 1e-9) this.learnStep();
   this.releaseTouches();
   var m = this.motionSettings();
   for (var c = 0; c < 16; ++c) {
      var st = this.channels[c];
      if (!st.motion.moving) continue;
      if (!motionActive(m)) st.motion.land();
      else st.motion.step(dt, m);
      this.send(c);
   }
};

// A SEND button: that output CC alone, for a MIDI learn. A learn listens for a control being moved,
// and Omnisphere's doesn't take a single CC, so the CC goes out as a short wiggle like a knob turned
// a little and back: LEARN_WIGGLE steps from where it is (64 before anything was sent), one every
// LEARN_STEP seconds, then the value last sent on it again. At 14 bits the wiggle is MSBs only, the CC
// to learn, and the last step is the whole value. Touch goes out pressed and released twice, then as
// it is.
var LEARN_VALUE = 64, LEARN_WIGGLE = [2, 4, 6, 8, 10, 8, 6, 4, 2], LEARN_STEP = 0.02;
Engine.prototype.learn = function (which) {
   var c = this.lastChannel, st = this.channels[c];
   var shift = this.applied.fine ? 7 : 0;
   var cc, steps = [], i;
   if (which === 'angle' || which === 'radius') {
      if (!this.applied.polar) return this.learned('SEND ' + which + ': Polar Out is off, nothing sent');
      if (this.conflicting()) return this.learned('SEND ' + which + ': Angle and Radius Out are the same CC, nothing sent');
      cc = this.int(which === 'angle' ? 'outangle' : 'outradius');
      var sent = which === 'angle' ? st.sentAngle : st.sentRadius;
      var from = sent >= 0 ? sent >> shift : LEARN_VALUE, dir = from > 63 ? -1 : 1;
      for (i = 0; i < LEARN_WIGGLE.length; ++i) steps.push(from + dir * LEARN_WIGGLE[i]);
   } else if (which === 'touch') {
      cc = this.touchOut();
      if (cc < 0) return this.learned('SEND touch: set a Touch Out CC first, nothing sent');
      steps = [127, 0, 127, 0];
   } else return;
   this.learning = { which: which, channel: c, cc: cc, steps: steps, next: 0 };
   this.learnStep();
   this.learned('SEND ' + which + ': wiggling CC ' + cc + ' on ch ' + (c + 1));
};

// Sends the wiggle's next step, then the value last sent on the CC to end it.
Engine.prototype.learnStep = function () {
   var l = this.learning, st = this.channels[l.channel];
   if (l.steps.length) {
      this.emit(0xB0 | l.channel, l.cc, l.steps.shift());
      l.next = this.time + LEARN_STEP;
      return;
   }
   this.learning = null;
   if (l.which === 'touch') {
      if (st.sentTouch === 127) this.emit(0xB0 | l.channel, l.cc, 127);
      return;
   }
   var sent = l.which === 'angle' ? st.sentAngle : st.sentRadius;
   if (sent >= 0 && this.applied.polar) this.pushValue(l.channel, l.cc, sent);
};

// What the last SEND did, shown on the status line for a few seconds, so a press is seen to land.
var LEARN_SHOWN = 4;
Engine.prototype.learned = function (text) {
   this.learnText = text;
   this.learnTime = this.time;
};

// Lands every glide (after a reset, say) and forgets what was sent, so the next move sends both.
Engine.prototype.reset = function () {
   for (var c = 0; c < 16; ++c) {
      var st = this.channels[c];
      st.motion.land();
      st.touch.reset();
      st.sentAngle = st.sentRadius = st.sentTouch = -1;
   }
};

// One line for the device: the last channel's input and output, or what stops it converting.
Engine.prototype.status = function () {
   if (this.learnText && this.time - this.learnTime < LEARN_SHOWN) return this.learnText;
   if (this.int('inx') === this.int('iny')) return 'X and Y In are the same CC: passing through';
   if (this.int('outangle') === this.int('outradius')) return 'Angle and Radius Out are the same CC: passing through';
   var c = this.lastChannel, st = this.channels[c];
   var text = 'ch ' + (c + 1) + '  in ' + (st.hasX ? st.x : '-') + ',' + (st.hasY ? st.y : '-');
   var shift = this.applied.fine ? 7 : 0;
   text += '  out ' + (!this.applied.polar ? 'off' : st.sentAngle >= 0 ? (st.sentAngle >> shift) + ' / ' + (st.sentRadius >> shift) : '-');
   if (this.fineBlocked()) text += '  (14-bit needs out CCs < 32)';
   return text + '  v' + VERSION;
};

// Raw MIDI bytes in, whole messages out: X/Y CCs to the engine, everything else (notes, bends,
// SysEx, clock) passed straight through.
function MidiParser(engine, out) {
   this.engine = engine;
   this.out = out; // out(byte)
   this.status = 0;
   this.data = [];
   this.sysex = false;
}

function dataLength(status) {
   var hi = status & 0xF0;
   if (hi === 0xC0 || hi === 0xD0) return 1;
   if (hi < 0xF0) return 2;
   if (status === 0xF1 || status === 0xF3) return 1;
   if (status === 0xF2) return 2;
   return 0;
}

MidiParser.prototype.byte = function (b) {
   b &= 0xFF;
   if (b >= 0xF8) { // real time, even inside a message
      this.out(b);
      return;
   }
   if (b === 0xF0) {
      this.sysex = true;
      this.status = 0;
      this.out(b);
      return;
   }
   if (this.sysex) {
      if (b < 0x80 || b === 0xF7) {
         this.out(b);
         if (b === 0xF7) this.sysex = false;
         return;
      }
      this.sysex = false; // a status byte cuts the SysEx short; handle it as itself
   }
   if (b >= 0x80) {
      this.status = b;
      this.data = [];
      if (dataLength(b) === 0) {
         this.out(b);
         this.status = 0;
      }
      return;
   }
   if (!this.status) return; // a stray data byte
   this.data.push(b);
   if (this.data.length < dataLength(this.status)) return;
   var s = this.status, d = this.data;
   this.data = [];
   if (s >= 0xF0) this.status = 0; // no running status for system common
   if ((s & 0xF0) === 0xB0 && this.engine.handleCC(s & 0x0F, d[0], d[1])) return;
   this.out(s);
   for (var i = 0; i < d.length; ++i) this.out(d[i]);
};

if (typeof module !== 'undefined' && module.exports) {
   module.exports = {
      ccToBipolar: ccToBipolar, toPolar: toPolar, Follower: Follower, TouchFilter: TouchFilter, Engine: Engine,
      MidiParser: MidiParser, PARAMS: PARAMS, VERSION: VERSION, STEPS7: STEPS7, STEPS14: STEPS14
   };
}

// ---- Max glue ----
// MIDI and ticks are marked immediate so they run in Max's scheduler thread with the MIDI, not
// deferred to the UI thread, so notes passing through aren't held up.

var engine = new Engine(function (status, d1, d2) {
   outlet(0, status);
   outlet(0, d1);
   outlet(0, d2);
});
var parser = new MidiParser(engine, function (b) { outlet(0, b); });
var lastTick = 0, lastStatus = '', statusAge = 0;

function msg_int(b) { parser.byte(b); }
msg_int.immediate = 1;

function list() {
   for (var i = 0; i < arguments.length; ++i) parser.byte(arguments[i]);
}
list.immediate = 1;

function tick() {
   var now = new Date().getTime();
   var dt = lastTick ? clamp((now - lastTick) / 1000, 0.001, 0.05) : 0.005;
   lastTick = now;
   engine.tick(dt);
   statusAge += dt;
   if (statusAge >= 0.1) {
      statusAge = 0;
      var text = engine.status();
      if (text !== lastStatus) {
         lastStatus = text;
         outlet(1, 'set', text);
      }
   }
}
tick.immediate = 1;

// `param <name> <value>`, from the device's dials and menus.
function param(name, value) { engine.setParam(name, value); }

// `learn angle` / `learn radius`, from the SEND buttons.
function learn(which) { engine.learn(which); }

function reset() { engine.reset(); }
