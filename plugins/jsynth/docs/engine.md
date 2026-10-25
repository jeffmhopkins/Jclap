# JSynth engine architecture (proposal, for review)

The panel and params exist (see `../src/params.h`, `../src/editor.cpp`, `../README.md`). This is the
plan for the sound. Nothing here is built yet.

## Threads and who owns what

- **The audio thread owns the flock.** The boids sim that today runs in the panel (`FlockView` in
  `editor.cpp`) moves into a shared header, `flock.h`, with no Visage in it. The engine steps it; the
  panel stops simulating and draws the newest `FlockFrame` the engine publishes through a
  `TripleBuffer` (as JVerb publishes its `OrbitFrame`). The frame holds, per creature: id, part, note
  (or -1 for an idle creature), x, y, vx, vy, energy, press, and its eight flock sources (Speed, Crowd,
  Nearest, Heading, Caught, Wake, X, Y). The ink the panel draws stays panel-side (puffs shed from the
  published positions); the engine keeps only a coarse ink density field for the Wake source.
- When the plugin isn't processing (no host transport, the screenshot tool), the panel runs its own
  copy of the same `flock.h` sim so it still flies. The screenshot tool drives the engine offline
  instead, so its frames are the engine's.
- Panel notes keep going through the existing `SpscQueue<PanelNote>`. Param values reach the audio
  thread as they do now (atomic values plus CLAP events in `process`).

## Timing

- **Control rate:** a fixed sub-block of 32 samples at the base rate. Per sub-block: read params,
  step the flock with a fixed time step (so behaviour doesn't depend on the host's block size;
  the flock steps at a fixed 500 Hz with an accumulator, independent of sample rate), update
  every voice's mod sources, evaluate routes, and set per-voice targets that are ramped linearly
  across the sub-block.
- Events are applied sample-accurately by splitting the host block at event times, then into 32-sample
  sub-blocks.
- **Quality** (1x/2x/4x) oversamples the voice (oscillators, FM, drive, filters) and decimates the
  summed voice bus per part with a polyphase half-band chain. Pan and the output stage run at the
  base rate.

## The voice (per creature)

- 16 voices per part, 32 total. A creature exists while its voice sounds (plus a few idle creatures
  per species, silent, for the visuals and to keep the flock alive).
- **Oscillators:** 3 phase accumulators (double phase). Waves: Sine, Triangle, Saw, Square, Pulse
  (polyBLEP/BLAMP for the analog ones when unmodulated), the TX81Z set (Half Sine, Abs Sine, Quarter,
  Alt Sine, Alt Half, Camel, Alt Camel) as phase-shaping functions of a sine, and White / Pink noise.
  Pitch modes: Note (coarse + fine), Ratio (× the note's Hz), Fixed (Hz). Shape is pulse width for
  Pulse/Square and a phase-distortion amount for the rest. Sync resets to Osc 1's wrap. Retrigger
  resets phase to Phase on note-on.
- **FM grid 3×3**, phase modulation (PHASE) or linear-through-zero frequency modulation on the
  exponential pitch (EXP). Within a sample the oscillators run in the order 3, 2, 1; a cell whose
  source runs later in the order (and every feedback cell on the diagonal) uses the source's
  previous-sample output, averaged over the last two samples for feedback (the DX7 trick) to tame
  the noise.
- **Mixer:** level and pan per oscillator, noise level and colour (tilt).
- **Drive:** Soft (tanh), Hard (clip), Tube (asymmetric), before the filters.
- **Filters:** two per voice. Low/High/Band/Notch/Peak from `jclap::dsp::Filter` (SVF, cascaded for
  12–48 dB slopes), Ladder (4-pole ZDF with tanh feedback), Comb +/- (short delay line tuned to the
  cutoff). Routing: Serial, Parallel (Balance crossfades), Split (Osc 1 to F1, Osc 2+3 and noise to
  F2). Key tracking per filter.
- **Amp:** Env 1 times level times velocity response, then per-creature pan.
- **Envelopes:** `jclap::dsp::Envelope` (DAHDSR with curves and Loop) ×3. **LFOs:** 4 per part;
  per-voice unless Trigger is Global, in which case one per part runs on the part.
- **Glide/portamento**, Mono and Legato, voice stealing (oldest released first, then oldest held,
  with a 3 ms fade on the stolen voice).

## Between creatures

- **Neighbour FM:** the nearest same-species neighbour's Osc 1 output phase-modulates this voice's
  Osc 2. Voices are processed one at a time per sub-block, so the coupling reads the neighbour's
  Osc 1 from the *previous* sub-block (a 32-sample buffer per voice, double-buffered). That is
  0.67 ms of latency in the modulator, which only shifts the modulator's phase.
- **Bite:** while a hunter has caught a creature (Caught > 0), the hunter's output ring-modulates the
  prey's, again from the previous sub-block buffer, crossfaded by Caught times Bite.
- **Tune Pull:** at control rate, for each creature, find the neighbours within Range whose headings
  are within 30°, and pull its pitch (in cents, smoothed) toward the nearest just interval
  (1/1, 6/5, 5/4, 4/3, 3/2, 5/3, 2/1 by octave-reduced ratio) to their pitch, at most ±30 cents.
- **Wake:** the engine's ink field is a 30×22 grid per part of density, deposited along each
  creature's path, decaying with Tail and blurred with Spread at control rate. A creature's Wake is
  the other species' density plus its own species' density minus its own recent deposit, at its
  cell.

## Modulation

- Sources per voice at control rate, normalised: unipolar 0..1 (Env, touch, flock sources, Random,
  Mod Wheel, Macros) or bipolar -1..1 (LFOs per their polarity, Glide/bend).
- **Routes:** 64 per part (source, destination index, amount). The used ones are compiled into a list
  on the audio thread whenever a route param changes. Per voice per sub-block:
  `offset[dest] += source * amount`, in the destination's normalised 0..1 range, then
  `value = denormalise(clamp(norm(param) + offset))`. A route's own amount is a destination, so a
  route can scale another (evaluated in two passes: route amounts first, then everything else).
- Destinations that change the flock (Speed, Cohesion, ...) are applied per creature in the flock
  step, so a creature's own Press can make it faster.
- **Touch:** CLAP note expressions (tuning, pressure, brightness) and MIDI MPE (per-channel bend,
  channel pressure, CC 74) map to Glide, Press, Slide. Strike = note-on velocity, Lift = release
  velocity. Each goes through its Curve and Smooth. MPE zone (lower/upper) and the manager channel's
  bend range are honoured.
- Host param modulation (CLAP `param_mod`) adds to the param's normalised value, as now. Per-note
  modulation is not supported in this first build.

## Output

- Each creature is panned: `pan = clamp(PartPan + XPan * (2x/aspect - 1) + routes)`, equal-power.
- Part level, part output (Main, or its own stereo port: three stereo out ports, main + A + B),
  then the global Output Width (M/S, 0..200%) and Master on the main bus, a soft safety limiter.

## CPU budget (rough)

32 voices × 3 oscillators × 2x oversampling at 48 kHz ≈ 9.2 M oscillator samples/s, plus two filters
per voice. Flock: 32 creatures, O(n²) neighbour search at 500 Hz is about 0.5 M pair checks/s.
Routes: 64 × 32 × 1.5 kHz ≈ 3 M multiply-adds/s.

## Tests

Silence when idle, no NaN/inf across random params and notes, voice count, a note's pitch from an
FFT, state round trip, offline render determinism (same input, same seed, same output regardless of
host block size).

---

# Revisions after the independent DSP review

The review found four blockers and a dozen should-fixes. The engine is built to this revised plan;
where it conflicts with the sections above, this wins.

**Timing and determinism**
- One global 32-sample control grid whose counter carries across `process()` calls. Note on/off
  land on their exact sample; param and expression events only set targets that the next tick
  reads. Output is then identical at any host block size (tested).
- The flock steps at 500 Hz from an integer accumulator (samples × 500 against the sample rate);
  control ticks read its state interpolated between the last two steps.
- All randomness (flock, Random source, Drift, LFO S&H) is our own xorshift, reseeded on activate,
  reset and the transport-start edge, so a bounce matches playback from the same start.

**Voices**
- Voices run sample by sample, all together (voice loop inside the sample loop), so cross-voice
  coupling (Neighbour FM, Bite) is a one-sample delay, not a 32-sample one. Idle voices are skipped.
- New voice-level DSP in `src/dsp.h` instead of the library classes, which lack what's needed:
  table sine (4096 points, linear), TX81Z waves from it, polyBLEP saw/square/pulse from the
  effective phase increment (always on), a 5-mode TPT SVF (LP/HP/BP/Notch/Peak; Notch and Peak are
  one stage, Slope ignored), a 4-pole ZDF ladder with a one-step tanh estimate, a comb with
  preallocated delay, a DAHDSR envelope with curve params and Held/Free loop, an LFO with six
  shapes, delay, polarity, phase and tempo sync.
- FM: PHASE is phase modulation, index 2·cell² cycles (≈12.6 rad at full). EXP is exponential FM
  (pitch moves by up to ±4 octaves per unit, clamped). Order 3, 2, 1: 3→2, 3→1, 2→1 use this
  sample's output, the rest and the diagonal use the last sample's (feedback averaged over two).
  Sync takes Osc 1's wrap from the previous sample.
- Neighbour FM is a smooth kernel-weighted sum over same-species neighbours (weight by distance
  and energy), ramped per tick, so a change of nearest neighbour never clicks. Bite likewise, with
  Caught slewed (20 ms up, 500 ms down).
- Voice pool of 40 for both parts: each part holds at most its Polyphony of sounding notes, and a
  stolen voice fades over 3 ms in its own slot while the new note takes a free one.
- Pan is applied at the oversampled rate; each part's stereo bus is decimated by a polyphase IIR
  allpass half-band chain (no latency to report). Quality is no longer automatable; buffers are
  sized for 4x at 192 kHz in `activate`.

**Modulation**
- Base param values are one-pole smoothed (10 ms) at control rate, then route offsets added, then
  ramped linearly per sample across the tick. Env 1 and the amp run per sample.
- Routes are recompiled when any route param changes (compared against a cached copy each tick),
  so a half-written route is corrected one tick later. Pass 1 evaluates routes that target another
  route's amount using unmodulated amounts; pass 2 uses the result. Ratio and Fixed modulate in
  log space; Coarse modulates continuously. Per-voice routes cannot move global-trigger LFOs or
  the Wind.
- Flock sources are made continuous: Crowd is a kernel-weighted count, Heading becomes "how much
  it flies upward" (0..1, smooth), Caught is slewed.
- The per-note modulation flags are removed from the params until it's built.
- Route Source and Destination are no longer automatable.

**Notes**
- Each voice tracks port, channel, key and note id. `NOTE_END` is sent for host notes when the last
  voice playing that note ends (one note can sound in both parts). `voice-info` reports 32 voices
  with overlapping notes.
- MIDI: channel 1 is the manager (bend with Manager Bend Range, CC 1 Mod Wheel, CC 64 sustain);
  channels 2–16 are member channels with per-note bend (part Bend Range), pressure and CC 74, whose
  last values a new note on that channel starts from. A plain keyboard on channel 1 just works.
  CLAP note expressions are used for CLAP-dialect notes.
- Mono and Legato keep a note stack per part.
- Play modes Single/Layer/Split/Zones/Velocity/Expression and each part's key range decide which
  part(s) a note plays; Expression crossfades the parts' levels by Slide or Press.

**Lifecycle and safety**
- `reset()` clears voices, filters, delays, decimators and the flock. `ScopedNoDenormals` in
  `process`. A voice that produces a non-finite sample is reset. 32-bit buffers; port 0 is main;
  a part sent to its own port skips the main bus's width, master and limiter.

**Tests** add: block-size independence, reseeded render determinism, NOTE_END pairing, and a CPU
benchmark (target under 25% of a core at 32 voices, 2x).
