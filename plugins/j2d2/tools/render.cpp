// Renders J2D2's factory presets offline to a WAV, one after another, each played the same way: a
// chord held (D, A, E, F), a press that swells and falls on the lowest note, and two new notes over
// it. For hearing the bank and checking levels without a host.
//
//    j2d2-render out.wav [seconds-per-preset]

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine.h"
#include "presets.h"

using namespace j2d2;

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.wav [seconds-per-preset]\n", argv[0]);
      return 1;
   }
   const double sr = 48000.0, seg = argc > 2 ? std::atof(argv[2]) : 9.5;
   const auto &bank = factoryPresets();
   const int per = static_cast<int>(seg * sr), total = per * static_cast<int>(bank.size());
   std::vector<float> L(total, 0.0f), R(total, 0.0f);
   Engine engine;
   engine.prepare(sr);
   for (size_t p = 0; p < bank.size(); ++p) {
      Settings s;
      for (int i = 0; i < kParamCount; ++i)
         s.v[i] = kParams[i].defaultValue;
      for (const auto &[id, v] : bank[p].values)
         s.v[id] = v;
      engine.reset();
      engine.seed(1000 + 37 * p);
      const bool droid = s[kMorph] < 0.3;
      const int chord[4] = {50, 57, 64, 65};
      const int base = static_cast<int>(p) * per;
      double peak = 0.0, sum = 0.0;
      for (int i = 0; i < per; i += 64) {
         const double t = static_cast<double>(i) / sr;
         // The performance.
         if (droid) {
            // Single notes, a phrase each, at different velocities and keys.
            static const double at[5] = {0.1, 1.9, 3.8, 5.6, 7.3};
            static const int keys[5] = {60, 67, 64, 72, 62};
            for (int k = 0; k < 5; ++k) {
               if (t <= at[k] && t + 64 / sr > at[k])
                  engine.noteOn(keys[k], 0, -1, 0.5 + 0.1 * k, s);
               if (t <= at[k] + 1.4 && t + 64 / sr > at[k] + 1.4)
                  engine.noteOff(keys[k], 0, -1);
            }
         } else {
            if (i == 0)
               for (int k = 0; k < 4; ++k)
                  engine.noteOn(chord[k] - 12 * (k == 0), 0, -1, 0.75, s);
            const double press = std::pow(std::max(0.0, std::sin(kPi * std::clamp((t - 2.0) / (seg - 4.0), 0.0, 1.0))), 1.5);
            engine.pressure(38, 0, -1, press);
            if (t <= 4.0 && t + 64 / sr > 4.0)
               engine.noteOn(74, 0, -1, 0.9, s);
            if (t <= 4.8 && t + 64 / sr > 4.8)
               engine.noteOff(74, 0, -1);
            if (t <= seg - 2.2 && t + 64 / sr > seg - 2.2)
               engine.releaseAll();
         }
         const int n = std::min(64, per - i);
         engine.process(&L[base + i], &R[base + i], n, s);
      }
      for (int i = 0; i < per; ++i) {
         peak = std::max(peak, static_cast<double>(std::fabs(L[base + i])));
         sum += L[base + i] * L[base + i];
      }
      std::printf("%-18s rms %6.1f dB  peak %5.2f\n", bank[p].name.c_str(), 10 * std::log10(sum / per + 1e-12), peak);
   }
   // 16-bit stereo WAV.
   FILE *f = std::fopen(argv[1], "wb");
   if (!f)
      return 1;
   const uint32_t dataBytes = total * 4, rate = static_cast<uint32_t>(sr), byteRate = rate * 4;
   const uint16_t fmt[2] = {1, 2}, align[2] = {4, 16};
   const uint32_t riff = 36 + dataBytes, fmtLen = 16;
   std::fwrite("RIFF", 1, 4, f);
   std::fwrite(&riff, 4, 1, f);
   std::fwrite("WAVEfmt ", 1, 8, f);
   std::fwrite(&fmtLen, 4, 1, f);
   std::fwrite(fmt, 2, 2, f);
   std::fwrite(&rate, 4, 1, f);
   std::fwrite(&byteRate, 4, 1, f);
   std::fwrite(align, 2, 2, f);
   std::fwrite("data", 1, 4, f);
   std::fwrite(&dataBytes, 4, 1, f);
   for (int i = 0; i < total; ++i) {
      const int16_t s2[2] = {static_cast<int16_t>(std::lround(std::clamp(L[i], -1.0f, 1.0f) * 32767)),
                             static_cast<int16_t>(std::lround(std::clamp(R[i], -1.0f, 1.0f) * 32767))};
      std::fwrite(s2, 2, 2, f);
   }
   std::fclose(f);
   return 0;
}
