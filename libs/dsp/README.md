<div align="center">

# jclap::dsp

Header-only audio building blocks for synths and effects: oscillator, filter, envelope, LFO and VCA.

[![CI](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml/badge.svg)](https://github.com/jeffmhopkins/Jclap/actions/workflows/build.yml) [![C++: 17](https://img.shields.io/badge/C%2B%2B-17-blue)](../../CMakeLists.txt) [![header only](https://img.shields.io/badge/header-only-blue)](include/jclap/dsp) [![tests: measured](https://img.shields.io/badge/tests-measured-blue)](tests)

</div>

Link it with `target_link_libraries(<name> PRIVATE jclap::dsp)`. It needs only the standard library:
no CLAP, no GUI, no allocation, so everything is safe on the audio thread.

| Header | What it is |
|---|---|
| `jclap/dsp/oscillator.h` | `Oscillator`: sine, triangle, saw, square and pulse with variable width. Saw, square and pulse are band-limited with PolyBLEP and the triangle with PolyBLAMP. Output is -1..1 with no DC, narrow pulses included |
| `jclap/dsp/filter.h` | `Filter`: resonant low, high or band pass, 12, 24, 36 or 48 dB/oct. Cascaded zero-delay-feedback state variable stages, tuned as a Butterworth cascade, stable with the cutoff moving every sample |
| `jclap/dsp/lfo.h` | `Lfo`: sine, triangle, saw, square or sample-and-hold random, -1..1, for modulation |
| `jclap/dsp/envelope.h` | `Envelope`: ADSR with analog-style curves and an optional loop (attack and decay repeat while held). Retriggers from the current level, so no clicks |
| `jclap/dsp/vca.h` | `Vca`: a smoothed gain times a control signal, with linear or exponential (60 dB) response |
| `jclap/dsp/delay_line.h` | `DelayLine`: a circular buffer with whole-sample taps and Hermite-interpolated fractional reads, for delays whose time glides. It allocates once, in `setMaxDelay()` (call it from activate) |
| `jclap/dsp/smoother.h` | `Smoother`: a one-pole glide for param changes |
| `jclap/dsp/denormals.h` | `ScopedNoDenormals`: flush-to-zero for the scope of `process()`, so decaying tails stay fast and never output subnormals |
| `jclap/dsp/math.h` | `noteToHz`, `dbToGain`, `gainToDb`, and `softClip` to saturate overs at the end of a chain |

A voice is a few lines:

```cpp
using namespace jclap::dsp;

Oscillator osc;
Filter filter;
Envelope env;
Vca vca;
// In activate(): setSampleRate(sampleRate) on each. Then, per note:
osc.setWave(Oscillator::Wave::Pulse);
osc.setPulseWidth(0.3);
osc.setFrequency(noteToHz(note));
filter.setSlope(Filter::Slope::Db24);
env.gateOn();
// Per sample:
const float level = env.process();
filter.setCutoff(800.0 * std::exp2(3.0 * level));
out[i] = vca.process(filter.process(osc.process()), level);
```

> [!TIP]
> [Basic Synth](../../plugins/basic-synth) puts these together into an eight-voice MPE synth.

`tests/test_dsp.cpp` measures what comes out: each wave's pitch and DC, filter gains at the cutoff
and an octave or two away for every mode and slope, envelope timing, and VCA smoothing.
