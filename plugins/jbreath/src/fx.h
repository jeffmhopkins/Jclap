#pragma once

// The DELAY tab's echo: a plain stereo delay after the body and before the reverb. It lives in
// jclap::dsp (jclap/dsp/stereo_delay.h), shared with JSynth.

#include <jclap/dsp/stereo_delay.h>

namespace jbreath {

using jclap::dsp::DelaySettings;
using jclap::dsp::delaySyncBeats;
using jclap::dsp::delayTimeMs;
using jclap::dsp::kDelaySyncCount;
using jclap::dsp::kDelaySyncNames;
using jclap::dsp::StereoDelay;

} // namespace jbreath
