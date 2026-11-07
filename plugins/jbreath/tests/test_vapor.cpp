// The smoke's fluid solver alone: it fills with coloured smoke where loops breathe and stays
// finite however hard they blow, the same jets give the same picture every time, and the threaded
// runner hands back pictures while the panel keeps submitting.

#include <jclap/core/test_host.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "vapor.h"

using namespace jbreath;

namespace {

// A loop's worth of jets at (x, y), blowing up and out, in an amber note colour.
std::vector<VaporEmitter> loopJets(float x, float y, float strength) {
   std::vector<VaporEmitter> out;
   for (int k = 0; k < 12; ++k) {
      const float a = 6.2831853f * k / 12.0f;
      VaporEmitter e;
      e.x = x + 0.05f * std::cos(a);
      e.y = y + 0.08f * std::sin(a);
      e.r = 0.012f;
      e.vx = 0.15f * std::cos(a) * strength;
      e.vy = 0.15f * std::sin(a) * strength + 0.12f;
      e.grip = 8;
      e.dye = 0.4f * strength;
      e.col[0] = 0.55f;
      e.col[1] = 0.33f;
      e.col[2] = 0.12f;
      out.push_back(e);
   }
   return out;
}

struct Picture {
   int w = 0, h = 0;
   std::vector<float> rgb[3];
};

Picture run(int steps, float strength) {
   Vapor v;
   v.resize(720, 494, 72, 120);
   std::vector<VaporLight> lights(1);
   lights[0].x = 0.4f;
   lights[0].y = 0.4f;
   lights[0].r = 0.15f;
   lights[0].k = 0.6f;
   for (int i = 0; i < steps; ++i)
      v.step(1.0f / 30.0f, i / 30.0f, loopJets(0.4f, 0.4f, strength), VaporParams{});
   v.render(lights, 1.0f, 1.0f, false);
   Picture p;
   p.w = v.dyeWidth();
   p.h = v.dyeHeight();
   for (int c = 0; c < 3; ++c)
      p.rgb[c] = v.channel(c);
   return p;
}

// How warm (red over blue) the picture is round a point, uv.
float warmth(const Picture &p, float x, float y) {
   double r = 0, b = 0;
   for (int j = static_cast<int>((y - 0.1f) * p.h); j < static_cast<int>((y + 0.1f) * p.h); ++j)
      for (int i = static_cast<int>((x - 0.1f) * p.w); i < static_cast<int>((x + 0.1f) * p.w); ++i) {
         r += p.rgb[0][static_cast<size_t>(j) * p.w + i];
         b += p.rgb[2][static_cast<size_t>(j) * p.w + i];
      }
   return static_cast<float>(r / std::max(1e-9, b));
}

} // namespace

int main() {
   // Smoke builds up warm round the loop (more red than blue), over the cool navy ground elsewhere.
   {
      const Picture p = run(120, 1.0f);
      bool finite = true;
      for (const auto &c : p.rgb)
         for (float x : c)
            finite = finite && std::isfinite(x) && x >= 0.0f && x <= 1.0f;
      JCLAP_CHECK(finite);
      const float near = warmth(p, 0.4f, 0.5f), far = warmth(p, 0.85f, 0.2f);
      std::printf("warmth by the loop %.2f, far from it %.2f\n", near, far);
      JCLAP_CHECK(near > 1.0f);
      JCLAP_CHECK(near > 1.25f * far);
   }
   // Blowing far too hard for a long time: still finite and in range.
   {
      const Picture p = run(600, 6.0f);
      bool finite = true;
      for (const auto &c : p.rgb)
         for (float x : c)
            finite = finite && std::isfinite(x) && x >= 0.0f && x <= 1.0f;
      JCLAP_CHECK(finite);
   }
   // The same jets, the same picture.
   {
      const Picture a = run(60, 1.0f), b = run(60, 1.0f);
      JCLAP_CHECK(a.rgb[0] == b.rgb[0] && a.rgb[1] == b.rgb[1] && a.rgb[2] == b.rgb[2]);
   }
   // The threaded runner: pictures come back while jobs keep arriving faster than it solves, and it
   // shuts down cleanly with work still queued.
   {
      VaporRunner runner(true);
      int got = 0;
      for (int i = 0; i < 200; ++i) {
         VaporRunner::Job job;
         job.w = 1616;
         job.h = 868;
         job.velShort = 96;
         job.dyeShort = 168;
         job.dt = 1.0f / 60.0f;
         job.time = i / 60.0f;
         job.emitters = loopJets(0.3f + 0.002f * i, 0.4f, 1.0f);
         runner.submit(std::move(job));
         runner.latest([&](int w, int h, const std::vector<float> &r, const std::vector<float> &, const std::vector<float> &) {
            if (w > 0 && h > 0 && r.size() == static_cast<size_t>(w) * h)
               ++got;
         });
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      std::printf("runner handed back %d pictures over 200 frames\n", got);
      JCLAP_CHECK(got > 100);
   }
   return jclap::core::testResult();
}
