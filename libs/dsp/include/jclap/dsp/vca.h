#pragma once

// A VCA: a gain, smoothed so changes never click, times a control signal such as an envelope.
//
//    Vca vca;
//    vca.setSampleRate(sampleRate);
//    vca.setGainDb(-6.0);
//    float out = vca.process(in, envelope.process());
//
// Linear response multiplies by the control as it is. Exponential response treats the control as
// a position on a 60 dB scale, which sounds more even when a knob or LFO sweeps it.

#include "math.h"
#include "smoother.h"

namespace jclap::dsp {

class Vca {
public:
   enum class Response { Linear, Exponential };

   Vca() {
      gain_.setTime(0.01);
      gain_.reset(1.0);
   }
   void setSampleRate(double sampleRate) { gain_.setSampleRate(sampleRate); }
   // How long a gain change takes to settle. Default 10 ms.
   void setSmoothingTime(double seconds) { gain_.setTime(seconds); }
   void setGain(double gain) { gain_.setTarget(gain); }
   void setGainDb(double db) { gain_.setTarget(dbToGain(db)); }
   void setResponse(Response response) { response_ = response; }
   // Skip the glide, for the first block.
   void reset() { gain_.reset(gain_.target()); }

   // The control is 0..1; 0 is always silent.
   float process(float in, float control = 1.0f) {
      const double gain = gain_.process();
      return static_cast<float>(in * gain * shape(control));
   }

private:
   double shape(double control) const {
      if (control <= 0.0)
         return 0.0;
      return response_ == Response::Linear ? control : dbToGain((control - 1.0) * 60.0);
   }

   Smoother gain_;
   Response response_ = Response::Linear;
};

} // namespace jclap::dsp
