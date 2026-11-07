#pragma once

// The vapour behind the breath loops: a small stable-fluids simulation (velocity, pressure and curl
// on a coarse grid, dye on a finer one) and the colour grading that turns its dye into smoke.
//
// Dye has four channels: each note's own coloured breath (r, g, b) and the shared cold vapour (a)
// it cools into, so a plume leaves its loop in the note's colour and drifts off as blue mist.
// Every sounding loop is a ring of jets, each easing the air towards its own speed and adding dye;
// the air rises gently, warm dye is buoyant, a slow curl-noise drift wanders through it, vorticity
// confinement keeps the curls tight, and a pressure solve keeps it from piling up.
//
// It runs on the CPU, so it looks the same on every machine. render() grades the dye into RGB at
// the dye grid's resolution (relief from the density gradient, the loops' light scattered by the
// vapour, bloom, a filmic shoulder and a vignette); the panel draws that as three additive heat maps
// the GPU smooths up to full size.
//
// Coordinates are those of a texture: u right and v up, 0..1 over the view.

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace jbreath {

struct VaporEmitter {
   float x = 0, y = 0, r = 0.01f;  // centre (uv) and radius (fraction of the view's height)
   float vx = 0, vy = 0, swirl = 0; // the speed it eases the air to, in view heights a second
   float grip = 1;                  // how quickly (1/s)
   float dye = 0;                   // how much dye a second
   float col[4] = {0, 0, 0, 0};     // the dye's mix: note colour (rgb) and cold vapour (a)
};

struct VaporLight {
   float x = 0, y = 0, r = 0.1f, k = 0; // centre (uv), radius (fraction of height), strength
   float col[3] = {1, 1, 1};
};

struct VaporParams {
   float rise = 0.03f, drift = 0.016f, buoy = 0.14f, drag = 0.45f, curl = 11.0f;
   int iters = 18;
   float velDiss = 0.04f;
   // cooling (note colour into cold), cold fade, cold-from-cold fade, colour fade, a second
   float rates[4] = {0.28f, 0.45f, 0.3f, 0.05f};
};

class Vapor {
public:
   // The view's size in design units, and how many velocity and dye cells its short side gets.
   void resize(float w, float h, int velShort, int dyeShort) {
      aspect_ = w / std::max(1.0f, h);
      const int sh = velShort, sw = std::max(8, static_cast<int>(std::lround(velShort * aspect_)));
      const int dh = dyeShort, dw = std::max(8, static_cast<int>(std::lround(dyeShort * aspect_)));
      if (sw == sw_ && sh == sh_ && dw == dw_ && dh == dh_)
         return;
      sw_ = sw;
      sh_ = sh;
      dw_ = dw;
      dh_ = dh;
      const size_t nv = static_cast<size_t>(sw) * sh, nd = static_cast<size_t>(dw) * dh;
      for (auto *f : {&u_, &v_, &u2_, &v2_, &p_, &p2_, &div_, &curl_, &psi_})
         f->assign(nv, 0.0f);
      dye_.assign(nd * 4, 0.0f);
      dye2_.assign(nd * 4, 0.0f);
      dens_.assign(nd, 0.0f);
      bw_ = std::max(8, dw / 4);
      bh_ = std::max(8, dh / 4);
      bloom_.assign(static_cast<size_t>(bw_) * bh_ * 4, 0.0f);
      bloom2_.assign(static_cast<size_t>(bw_) * bh_ * 4, 0.0f);
      bloomRgb_.assign(static_cast<size_t>(bw_) * bh_ * 3, 0.0f);
      for (int c = 0; c < 3; ++c) {
         rgb_[c].assign(nd, 0.0f);
         scatter_[c].assign(nd, 0.0f);
      }
      psiAge_ = 1e9f;
   }

   int dyeWidth() const { return dw_; }
   int dyeHeight() const { return dh_; }
   int cells() const { return sw_ * sh_ + dw_ * dh_; }
   // The graded colour of dye cell (i, j), j counted up from the bottom, after render().
   const std::vector<float> &channel(int c) const { return rgb_[c]; }

   void clear() {
      for (auto *f : {&u_, &v_, &p_})
         std::fill(f->begin(), f->end(), 0.0f);
      std::fill(dye_.begin(), dye_.end(), 0.0f);
   }

   void step(float dt, float time, const std::vector<VaporEmitter> &emitters, const VaporParams &P) {
      if (sw_ == 0)
         return;
      emitVelocity(dt, emitters);
      emitDye(dt, emitters);
      force(dt, time, P);
      vorticity(dt, P.curl);
      project(P.iters);
      advectVelocity(dt, P.velDiss);
      advectDye(dt, P.rates);
   }

   // Grades the dye into colour: deep navy ground, the vapour lit and shaded, the loops' light
   // scattered through it, bloom, a soft shoulder and a vignette. `glow` scales the light and bloom.
   void render(const std::vector<VaporLight> &lights, float tint, float glow, bool ink) {
      if (sw_ == 0)
         return;
      const size_t nd = static_cast<size_t>(dw_) * dh_;
      for (size_t k = 0; k < nd; ++k) {
         const float *d = &dye_[k * 4];
         dens_[k] = std::max(0.0f, d[0]) + std::max(0.0f, d[1]) + std::max(0.0f, d[2]) + std::max(0.0f, d[3]) * 0.8f;
      }
      // The loops' light, scattered by the vapour round them (and a whisper of air glow): only the
      // cells near each loop.
      for (auto &sc : scatter_)
         std::fill(sc.begin(), sc.end(), 0.0f);
      if (!ink)
         for (const VaporLight &L : lights) {
            if (L.k <= 0)
               continue;
            const float reach = L.r * 6.4f; // where exp(-r2 * 0.18) has fallen below 0.1%
            const int i0 = std::max(0, static_cast<int>((L.x - reach / aspect_) * dw_)), i1 = std::min(dw_ - 1, static_cast<int>((L.x + reach / aspect_) * dw_) + 1);
            const int j0 = std::max(0, static_cast<int>((L.y - reach) * dh_)), j1 = std::min(dh_ - 1, static_cast<int>((L.y + reach) * dh_) + 1);
            const float inv = 1.0f / (L.r * L.r), k = L.k * glow;
            for (int j = j0; j <= j1; ++j) {
               const float py = (j + 0.5f) / dh_ - L.y;
               for (int i = i0; i <= i1; ++i) {
                  const float px = ((i + 0.5f) / dw_ - L.x) * aspect_, r2 = (px * px + py * py) * inv;
                  const size_t q = idx(i, j, dw_);
                  const float a = 1.0f - std::exp(-dens_[q] * 1.3f);
                  const float g = k * (std::exp(-r2) * (a * 0.85f + 0.05f) + std::exp(-r2 * 0.18f) * a * 0.22f);
                  for (int c = 0; c < 3; ++c)
                     scatter_[c][q] += L.col[c] * g;
               }
            }
         }
      makeBloom();
      const float bk = ink ? 0.0f : std::min(1.5f, glow);
      const float ty = 1.0f / dh_;
      for (int j = 0; j < dh_; ++j) {
         const float vy = (j + 0.5f) * ty;
         // the ground: deep navy, a faint warm horizon at the floor, a cooler zenith
         const float s = smoothstep(0.0f, 0.9f, vy), hz = std::exp(-vy * 5.0f) * tint;
         const float ground[3] = {0.040f - 0.018f * s + 0.11f * hz, 0.052f - 0.020f * s + 0.05f * hz, 0.098f - 0.028f * s + 0.012f * hz};
         const float gy = vy - 0.5f;
         const int jd = std::max(0, j - 2), ju = std::min(dh_ - 1, j + 2);
         const float by = std::clamp(vy * bh_ - 0.5f, 0.0f, bh_ - 1.001f);
         const int bj = static_cast<int>(by);
         const float bt = by - bj;
         for (int i = 0; i < dw_; ++i) {
            const size_t k = idx(i, j, dw_);
            const float *dd = &dye_[k * 4];
            const float d[4] = {std::max(0.0f, dd[0]), std::max(0.0f, dd[1]), std::max(0.0f, dd[2]), std::max(0.0f, dd[3])};
            // relief from the density gradient, lit from up and to the left
            const float dl = dens_[idx(std::max(0, i - 2), j, dw_)], dr = dens_[idx(std::min(dw_ - 1, i + 2), j, dw_)];
            const float db = dens_[idx(i, jd, dw_)], dt = dens_[idx(i, ju, dw_)];
            const float nx = dl - dr, ny = db - dt, nl = 1.0f / std::sqrt(nx * nx + ny * ny + 0.3025f);
            const float relief = 0.9f + 0.28f * std::clamp((nx * -0.3812f + ny * 0.7079f + 0.55f * 0.7406f) * nl, 0.0f, 1.0f);
            const float a = 1.0f - std::exp(-dens_[k] * 1.3f);
            float sh[3];
            shade(d, sh);
            // the bloom, graded at a quarter size, blended up
            const float vx = (i + 0.5f) / dw_;
            const float bx = std::clamp(vx * bw_ - 0.5f, 0.0f, bw_ - 1.001f);
            const int bi = static_cast<int>(bx);
            const float bs = bx - bi;
            const float *b00 = &bloomRgb_[idx(bi, bj, bw_) * 3], *b10 = b00 + 3, *b01 = b00 + bw_ * 3, *b11 = b01 + 3;
            const float gx = (vx - 0.5f) * aspect_ * 0.75f, vig = 1.0f - 0.38f * (gx * gx + gy * gy);
            for (int c = 0; c < 3; ++c) {
               const float bl = (b00[c] + (b10[c] - b00[c]) * bs) * (1 - bt) + (b01[c] + (b11[c] - b01[c]) * bs) * bt;
               const float x = ground[c] * (1.0f - 0.55f * a) + sh[c] * relief + scatter_[c][k] + (bl * 0.38f + bl * bl * 0.25f) * bk;
               rgb_[c][k] = std::min(1.0f, tone(x) * vig);
            }
         }
      }
   }

private:
   static size_t idx(int i, int j, int w) { return static_cast<size_t>(j) * w + i; }
   // The soft filmic shoulder and the slight lift of the mids, from a table.
   static float tone(float x) {
      static const std::vector<float> kTable = [] {
         std::vector<float> t(1025);
         for (int i = 0; i <= 1024; ++i) {
            const float v = i / 128.0f;
            t[static_cast<size_t>(i)] = std::pow(v / (1.0f + v * 0.28f), 0.94f);
         }
         return t;
      }();
      const float p = std::clamp(x, 0.0f, 7.999f) * 128.0f;
      const int i = static_cast<int>(p);
      return kTable[static_cast<size_t>(i)] + (kTable[static_cast<size_t>(i) + 1] - kTable[static_cast<size_t>(i)]) * (p - i);
   }
   static float smoothstep(float e0, float e1, float x) {
      const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
      return t * t * (3 - 2 * t);
   }

   // Bilinear, as a texture with clamp-to-edge samples it: (x, y) in uv.
   static float sample(const std::vector<float> &f, int w, int h, float x, float y) {
      x = std::clamp(x * w - 0.5f, 0.0f, w - 1.001f);
      y = std::clamp(y * h - 0.5f, 0.0f, h - 1.001f);
      const int i = static_cast<int>(x), j = static_cast<int>(y);
      const float s = x - i, t = y - j;
      const float *r0 = &f[idx(i, j, w)], *r1 = r0 + w;
      return (r0[0] + (r0[1] - r0[0]) * s) * (1 - t) + (r1[0] + (r1[1] - r1[0]) * s) * t;
   }

   // Bilinear over four interleaved channels (the dye), clamp-to-edge.
   static void sample4(const std::vector<float> &f, int w, int h, float x, float y, float out[4]) {
      x = std::clamp(x * w - 0.5f, 0.0f, w - 1.001f);
      y = std::clamp(y * h - 0.5f, 0.0f, h - 1.001f);
      const int i = static_cast<int>(x), j = static_cast<int>(y);
      const float s = x - i, t = y - j;
      const float w00 = (1 - s) * (1 - t), w10 = s * (1 - t), w01 = (1 - s) * t, w11 = s * t;
      const float *a = &f[idx(i, j, w) * 4], *b = a + 4, *c = a + static_cast<size_t>(w) * 4, *d = c + 4;
      for (int q = 0; q < 4; ++q)
         out[q] = a[q] * w00 + b[q] * w10 + c[q] * w01 + d[q] * w11;
   }

   // Each emitter eases the air within its radius towards its jet's speed (plus any swirl).
   void emitVelocity(float dt, const std::vector<VaporEmitter> &emitters) {
      for (const VaporEmitter &e : emitters) {
         if (e.dye <= 0 && e.grip <= 0)
            continue;
         const float tvx = e.vx * sh_, tvy = e.vy * sh_, sw = e.swirl * sh_, mix = 1.0f - std::exp(-dt * e.grip);
         box(e, sw_, sh_, [&](size_t k, float g, float px, float py) {
            const float m = std::clamp(g * mix, 0.0f, 1.0f);
            const float tu = tvx + sw * -py / e.r, tv = tvy + sw * px / e.r;
            u_[k] += (tu - u_[k]) * m;
            v_[k] += (tv - v_[k]) * m;
         });
      }
   }

   void emitDye(float dt, const std::vector<VaporEmitter> &emitters) {
      for (const VaporEmitter &e : emitters) {
         if (e.dye <= 0)
            continue;
         const float w = e.dye * dt;
         box(e, dw_, dh_, [&](size_t k, float g, float, float) {
            for (int c = 0; c < 4; ++c)
               dye_[k * 4 + c] += e.col[c] * g * w;
         });
      }
   }

   // Calls fn(cell, gaussian weight, dx, dy) for the cells of a w x h grid within 3 radii of e.
   template <typename Fn> void box(const VaporEmitter &e, int w, int h, Fn fn) const {
      const float rx = 3.0f * e.r / aspect_, ry = 3.0f * e.r, r2 = e.r * e.r;
      const int i0 = std::max(0, static_cast<int>((e.x - rx) * w)), i1 = std::min(w - 1, static_cast<int>((e.x + rx) * w) + 1);
      const int j0 = std::max(0, static_cast<int>((e.y - ry) * h)), j1 = std::min(h - 1, static_cast<int>((e.y + ry) * h) + 1);
      for (int j = j0; j <= j1; ++j) {
         const float py = (j + 0.5f) / h - e.y;
         for (int i = i0; i <= i1; ++i) {
            const float px = ((i + 0.5f) / w - e.x) * aspect_;
            const float g = std::exp(-(px * px + py * py) / r2);
            if (g > 1e-4f)
               fn(idx(i, j, w), g, px, py);
         }
      }
   }

   // The stream function of the room's slow drift.
   static float psi(float x, float y, float t) {
      return std::sin(x * 2.3f + t * 0.13f + std::sin(y * 1.7f - t * 0.05f) * 1.4f) * std::cos(y * 2.9f - t * 0.09f) +
             0.55f * std::sin(x * 4.1f - y * 3.3f + t * 0.19f) + 0.28f * std::cos(x * 7.3f + y * 5.9f - t * 0.27f);
   }

   // Ambient drift (the curl of psi), a gentle rising current, buoyancy of the dye, and drag.
   void force(float dt, float time, const VaporParams &P) {
      // psi drifts slowly, so it's worked out afresh only every 0.1 s.
      psiAge_ += dt;
      if (psiAge_ >= 0.1f) {
         psiAge_ = 0;
         for (int j = 0; j < sh_; ++j)
            for (int i = 0; i < sw_; ++i)
               psi_[idx(i, j, sw_)] = psi((i + 0.5f) / sw_ * aspect_, (j + 0.5f) / sh_, time);
      }
      const float rise = P.rise * sh_, drift = P.drift * sh_, buoy = P.buoy * sh_, damp = 1.0f / (1.0f + P.drag * dt);
      const float inv2e = static_cast<float>(sh_) * 0.5f; // central difference over two cells, in q units
      for (int j = 0; j < sh_; ++j) {
         const float y = (j + 0.5f) / sh_;
         const float lift = rise * (0.55f + 0.45f * smoothstep(0.0f, 0.6f, y));
         const int jd = std::max(0, j - 1), ju = std::min(sh_ - 1, j + 1);
         for (int i = 0; i < sw_; ++i) {
            const int il = std::max(0, i - 1), ir = std::min(sw_ - 1, i + 1);
            const float dpx = (psi_[idx(ir, j, sw_)] - psi_[idx(il, j, sw_)]) * inv2e;
            const float dpy = (psi_[idx(i, ju, sw_)] - psi_[idx(i, jd, sw_)]) * inv2e;
            const float x = (i + 0.5f) / sw_;
            float dd[4];
            sample4(dye_, dw_, dh_, x, y, dd);
            const float dr = dd[0], dg = dd[1], db = dd[2];
            const size_t k = idx(i, j, sw_);
            u_[k] = (u_[k] + dt * dpy * drift) * damp;
            v_[k] = (v_[k] + dt * (-dpx * drift + lift + buoy * (dr + 0.9f * db + 0.12f * dg))) * damp;
         }
      }
   }

   // Vorticity confinement: push along the gradient of |curl|, so small eddies don't smear away.
   void vorticity(float dt, float strength) {
      const auto U = [&](int i, int j) { return u_[idx(std::clamp(i, 0, sw_ - 1), std::clamp(j, 0, sh_ - 1), sw_)]; };
      const auto V = [&](int i, int j) { return v_[idx(std::clamp(i, 0, sw_ - 1), std::clamp(j, 0, sh_ - 1), sw_)]; };
      for (int j = 0; j < sh_; ++j)
         for (int i = 0; i < sw_; ++i)
            curl_[idx(i, j, sw_)] = 0.5f * (V(i + 1, j) - V(i - 1, j) - U(i, j + 1) + U(i, j - 1));
      const auto C = [&](int i, int j) { return curl_[idx(std::clamp(i, 0, sw_ - 1), std::clamp(j, 0, sh_ - 1), sw_)]; };
      for (int j = 0; j < sh_; ++j)
         for (int i = 0; i < sw_; ++i) {
            float fx = 0.5f * (std::fabs(C(i, j + 1)) - std::fabs(C(i, j - 1)));
            float fy = 0.5f * (std::fabs(C(i + 1, j)) - std::fabs(C(i - 1, j)));
            const float l = std::sqrt(fx * fx + fy * fy) + 1e-4f, c = C(i, j);
            fx = fx / l * strength * c;
            fy = -fy / l * strength * c;
            const size_t k = idx(i, j, sw_);
            u_[k] = std::clamp(u_[k] + fx * dt, -400.0f, 400.0f);
            v_[k] = std::clamp(v_[k] + fy * dt, -400.0f, 400.0f);
         }
   }

   // Takes out the divergence: walls at the sides and floor, open at the top.
   void project(int iters) {
      for (int j = 0; j < sh_; ++j)
         for (int i = 0; i < sw_; ++i) {
            const size_t k = idx(i, j, sw_);
            const float cu = u_[k], cv = v_[k];
            const float L = i > 0 ? u_[k - 1] : -cu, R = i < sw_ - 1 ? u_[k + 1] : -cu;
            const float B = j > 0 ? v_[k - sw_] : -cv, T = j < sh_ - 1 ? v_[k + sw_] : cv;
            div_[k] = 0.5f * (R - L + T - B);
            p_[k] *= 0.8f;
         }
      for (int it = 0; it < iters; ++it) {
         for (int j = 0; j < sh_; ++j)
            for (int i = 0; i < sw_; ++i) {
               const size_t k = idx(i, j, sw_);
               const float c = p_[k];
               const float L = i > 0 ? p_[k - 1] : c, R = i < sw_ - 1 ? p_[k + 1] : c;
               const float B = j > 0 ? p_[k - sw_] : c, T = j < sh_ - 1 ? p_[k + sw_] : 0.0f;
               p2_[k] = 0.25f * (L + R + B + T - div_[k]);
            }
         p_.swap(p2_);
      }
      for (int j = 0; j < sh_; ++j)
         for (int i = 0; i < sw_; ++i) {
            const size_t k = idx(i, j, sw_);
            const float c = p_[k];
            const float L = i > 0 ? p_[k - 1] : c, R = i < sw_ - 1 ? p_[k + 1] : c;
            const float B = j > 0 ? p_[k - sw_] : c, T = j < sh_ - 1 ? p_[k + sw_] : 0.0f;
            u_[k] -= 0.5f * (R - L);
            v_[k] -= 0.5f * (T - B);
         }
   }

   void advectVelocity(float dt, float diss) {
      const float k1 = 1.0f / (1.0f + diss * dt);
      for (int j = 0; j < sh_; ++j)
         for (int i = 0; i < sw_; ++i) {
            const size_t k = idx(i, j, sw_);
            const float x = (i + 0.5f - dt * u_[k]) / sw_, y = (j + 0.5f - dt * v_[k]) / sh_;
            u2_[k] = sample(u_, sw_, sh_, x, y) * k1;
            v2_[k] = sample(v_, sw_, sh_, x, y) * k1;
         }
      u_.swap(u2_);
      v_.swap(v2_);
   }

   // Carries the dye, cooling note colour into cold vapour as it goes, then fading it all, and
   // letting it out at the sides, top and floor.
   void advectDye(float dt, const float rates[4]) {
      const float kc = std::exp(-rates[0] * dt), fadeCold = std::exp(-rates[2] * dt), fadeColour = std::exp(-rates[3] * dt);
      const float edgeMix = std::min(1.0f, dt * 8.0f);
      for (int j = 0; j < dh_; ++j) {
         const float y = (j + 0.5f) / dh_;
         const float ey = smoothstep(1.0f, 0.9f, y) * smoothstep(0.0f, 0.01f, y);
         for (int i = 0; i < dw_; ++i) {
            const float x = (i + 0.5f) / dw_;
            const float vu = sample(u_, sw_, sh_, x, y), vv = sample(v_, sw_, sh_, x, y);
            const float cx = x - dt * vu / sw_, cy = y - dt * vv / sh_;
            float d[4];
            sample4(dye_, dw_, dh_, cx, cy, d);
            for (float &q : d)
               q = std::max(0.0f, q);
            const float edge = smoothstep(0.0f, 0.035f, x) * smoothstep(1.0f, 0.965f, x) * ey;
            const float m = 1.0f + (edge - 1.0f) * edgeMix;
            const float cold = std::min(3.0f, (d[3] + 0.8f * (d[0] + d[1] + d[2]) * (1.0f - kc)) * fadeCold);
            const size_t k = idx(i, j, dw_);
            float *o = &dye2_[k * 4];
            for (int c = 0; c < 3; ++c)
               o[c] = d[c] * kc * fadeColour * m;
            o[3] = cold * m;
         }
      }
      dye_.swap(dye2_);
   }

   // Each note breathes its own hue (rgb); as it ages it cools through dusk into the shared cold
   // blue vapour (a), so the field stays one palette while every plume keeps its note's colour.
   static void shade(const float d[4], float out[3]) {
      const float W = d[0] + d[1] + d[2], c = d[3] * 0.85f, D = W + c + 1e-4f;
      const float lum = 1.0f - std::exp(-D * 1.45f);
      float hw[3] = {d[0] / (W + 1e-4f), d[1] / (W + 1e-4f), d[2] / (W + 1e-4f)};
      const float mx = std::max(std::max(hw[0], hw[1]), std::max(hw[2], 1e-3f));
      for (float &x : hw)
         x /= mx;
      static constexpr float kCream[3] = {1.0f, 0.93f, 0.84f};
      const float hotMix = smoothstep(0.6f, 2.6f, W) * 0.5f, coolMix = smoothstep(0.3f, 2.2f, c);
      static constexpr float kCoolA[3] = {0.24f, 0.38f, 0.68f}, kCoolB[3] = {0.58f, 0.72f, 0.95f}, kDusk[3] = {0.46f, 0.40f, 0.66f};
      const float f = W / D, k = lum * (0.58f + 0.42f * f);
      for (int i = 0; i < 3; ++i) {
         const float hot = hw[i] + (kCream[i] - hw[i]) * hotMix;
         const float cool = kCoolA[i] + (kCoolB[i] - kCoolA[i]) * coolMix;
         const float dusk = kDusk[i] + (hw[i] * 0.75f - kDusk[i]) * 0.3f;
         const float col = f < 0.35f ? cool + (dusk - cool) * (f / 0.35f) : dusk + (hot - dusk) * ((f - 0.35f) / 0.65f);
         out[i] = col * k;
      }
   }

   // A quarter-size copy of the dye (each cell the mean of its 4 x 4 block), blurred twice (5 taps
   // each way, the second wider), then graded.
   void makeBloom() {
      std::fill(bloom_.begin(), bloom_.end(), 0.0f);
      for (int j = 0; j < bh_ * 4 && j < dh_; ++j)
         for (int i = 0; i < bw_ * 4 && i < dw_; ++i) {
            const float *d = &dye_[idx(i, j, dw_) * 4];
            float *o = &bloom_[idx(i / 4, j / 4, bw_) * 4];
            for (int c = 0; c < 4; ++c)
               o[c] += std::max(0.0f, d[c]) * (1.0f / 16.0f);
         }
      for (int pass = 0; pass < 2; ++pass) {
         blur(1 + pass, 0);
         blur(0, 1 + pass);
      }
      for (size_t k = 0; k < static_cast<size_t>(bw_) * bh_; ++k)
         shade(&bloom_[k * 4], &bloomRgb_[k * 3]);
   }
   void blur(int dx, int dy) {
      static constexpr float kW0 = 0.2270270f, kW1 = 0.3162162f, kW2 = 0.0702703f, kO1 = 1.3846154f, kO2 = 3.2307692f;
      for (int j = 0; j < bh_; ++j)
         for (int i = 0; i < bw_; ++i) {
            const float x = (i + 0.5f) / bw_, y = (j + 0.5f) / bh_, ox = static_cast<float>(dx) / bw_, oy = static_cast<float>(dy) / bh_;
            float a[4], b[4], c[4], d[4];
            sample4(bloom_, bw_, bh_, x + ox * kO1, y + oy * kO1, a);
            sample4(bloom_, bw_, bh_, x - ox * kO1, y - oy * kO1, b);
            sample4(bloom_, bw_, bh_, x + ox * kO2, y + oy * kO2, c);
            sample4(bloom_, bw_, bh_, x - ox * kO2, y - oy * kO2, d);
            const size_t k = idx(i, j, bw_) * 4;
            for (int q = 0; q < 4; ++q)
               bloom2_[k + q] = bloom_[k + q] * kW0 + (a[q] + b[q]) * kW1 + (c[q] + d[q]) * kW2;
         }
      bloom_.swap(bloom2_);
   }

   int sw_ = 0, sh_ = 0, dw_ = 0, dh_ = 0, bw_ = 0, bh_ = 0;
   float aspect_ = 1, psiAge_ = 1e9f;
   std::vector<float> u_, v_, u2_, v2_, p_, p2_, div_, curl_, psi_;
   // dye, its next step and the bloom are four channels interleaved: note colour (rgb) and cold
   std::vector<float> dye_, dye2_, dens_, bloom_, bloom2_, bloomRgb_, rgb_[3], scatter_[3];
};

// Runs a Vapor off the UI thread: the panel hands it each frame's jets and lights, and takes the
// latest graded picture, a frame behind. If the solver falls behind (a slow machine), frames it
// missed fold into the next step, so the smoke slows rather than the panel. Synchronous when
// asked (screenshots and tests, which need the same picture every time).
class VaporRunner {
public:
   struct Job {
      float w = 0, h = 0;
      int velShort = 0, dyeShort = 0;
      float dt = 0, time = 0, tint = 1, glow = 1;
      bool ink = false;
      VaporParams params;
      std::vector<VaporEmitter> emitters;
      std::vector<VaporLight> lights;
   };

   explicit VaporRunner(bool async) : async_(async) {
      if (async_)
         thread_ = std::thread([this] { run(); });
   }
   ~VaporRunner() {
      if (async_) {
         {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
         }
         wake_.notify_one();
         thread_.join();
      }
   }
   VaporRunner(const VaporRunner &) = delete;
   VaporRunner &operator=(const VaporRunner &) = delete;

   void submit(Job job) {
      if (!async_) {
         work(job);
         return;
      }
      {
         std::lock_guard<std::mutex> lock(mutex_);
         // Time a step the solver hasn't taken yet carries over into this one.
         if (pending_)
            job.dt = std::min(job.dt + job_.dt, 1.0f / 12.0f);
         job_ = std::move(job);
         pending_ = true;
      }
      wake_.notify_one();
   }

   // Copies the latest picture into fn(width, height, red, green, blue), rows from the bottom.
   template <typename Fn> void latest(Fn fn) {
      std::lock_guard<std::mutex> lock(outMutex_);
      if (outW_ > 0)
         fn(outW_, outH_, out_[0], out_[1], out_[2]);
   }
   int cells() {
      std::lock_guard<std::mutex> lock(outMutex_);
      return cells_;
   }

private:
   void run() {
      for (;;) {
         Job job;
         {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return pending_ || quit_; });
            if (quit_)
               return;
            job = std::move(job_);
            pending_ = false;
         }
         work(job);
      }
   }
   void work(const Job &job) {
      vapor_.resize(job.w, job.h, job.velShort, job.dyeShort);
      vapor_.step(std::min(job.dt, 1.0f / 24.0f), job.time, job.emitters, job.params);
      vapor_.render(job.lights, job.tint, job.glow, job.ink);
      std::lock_guard<std::mutex> lock(outMutex_);
      outW_ = vapor_.dyeWidth();
      outH_ = vapor_.dyeHeight();
      cells_ = vapor_.cells();
      for (int c = 0; c < 3; ++c)
         out_[c] = vapor_.channel(c);
   }

   const bool async_;
   Vapor vapor_;
   std::thread thread_;
   std::mutex mutex_, outMutex_;
   std::condition_variable wake_;
   Job job_;
   bool pending_ = false, quit_ = false;
   std::vector<float> out_[3];
   int outW_ = 0, outH_ = 0, cells_ = 0;
};

} // namespace jbreath
