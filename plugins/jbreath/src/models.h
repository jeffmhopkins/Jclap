#pragma once

// JBreath's five instruments, as digital waveguides: a bore (a delay line that carries the sound
// down the tube and back) driven by a nonlinear exciter (the player's jet, reed or lips).
//
// Every model runs at the voice's internal rate (88.2 kHz or more; see engine.h) and takes, per
// sample: breath (0..about 1.3, mouth pressure), embouchure (0..1) and the bore length in samples.
// Each knows its nominal length for a pitch; the baked tuning table (tuning_table.h) corrects it to
// the length that really plays that pitch. Filters are set in Hz, so the models sound the same at
// any internal rate.
//
// The models were tuned by measurement (pitch, harmonics and articulation) in the JBreath DSP
// prototype; the constants here are that prototype's.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace jbreath {

constexpr double kPi = 3.14159265358979323846;

enum ModelId : int { kFlute, kClarinet, kSax, kOboe, kTrumpet, kAlien, kModelCount };

struct Noise {
   uint32_t s = 0x9e3779b9u;
   float white() {
      s ^= s << 13;
      s ^= s >> 17;
      s ^= s << 5;
      return static_cast<float>(static_cast<int32_t>(s)) * (1.0f / 2147483648.0f);
   }
};

// A delay line with a third-order Lagrange fractional read, so the pitch stays clean while the
// length glides. Its size is a power of two, allocated once in init().
class Delay {
public:
   void init(int minSize) {
      int n = 16;
      while (n < minSize)
         n <<= 1;
      b_.assign(static_cast<size_t>(n), 0.0f);
      mask_ = n - 1;
      w_ = 0;
   }
   void clear() {
      std::fill(b_.begin(), b_.end(), 0.0f);
      w_ = 0;
   }
   int size() const { return mask_ + 1; }
   void push(float x) {
      b_[static_cast<size_t>(w_)] = x;
      w_ = (w_ + 1) & mask_;
   }
   // The value written d samples ago (d >= 1).
   float read(double d) const {
      d = std::clamp(d, 1.0, static_cast<double>(mask_ - 3));
      const int i = static_cast<int>(d);
      const double t = d - i + 1.0;
      const auto at = [this](int k) { return static_cast<double>(b_[static_cast<size_t>((w_ - k) & mask_)]); };
      const double x0 = at(i - 1 < 1 ? 1 : i - 1), x1 = at(i), x2 = at(i + 1), x3 = at(i + 2);
      const double c0 = -(t - 1) * (t - 2) * (t - 3) / 6.0;
      const double c1 = t * (t - 2) * (t - 3) / 2.0;
      const double c2 = -t * (t - 1) * (t - 3) / 2.0;
      const double c3 = t * (t - 1) * (t - 2) / 6.0;
      return static_cast<float>(c0 * x0 + c1 * x1 + c2 * x2 + c3 * x3);
   }

private:
   std::vector<float> b_;
   int mask_ = 0, w_ = 0;
};

struct OnePoleLP {
   float a = 0.0f, z = 0.0f;
   void setHz(double hz, double fs) { a = static_cast<float>(std::exp(-2.0 * kPi * std::min(hz, 0.45 * fs) / fs)); }
   float tick(float x) {
      z = (1.0f - a) * x + a * z;
      return z;
   }
};

struct DCBlock {
   float x1 = 0.0f, y1 = 0.0f, r = 0.9995f;
   void setFs(double fs) { r = static_cast<float>(std::exp(-2.0 * kPi * 7.6 / fs)); }
   float tick(float x) {
      const float y = x - x1 + r * y1;
      x1 = x;
      y1 = y;
      return y;
   }
};

struct Biquad {
   double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
   void clear() { z1 = z2 = 0; }
   float tick(float x) {
      const double y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return static_cast<float>(y);
   }
   void peak(double hz, double q, double db, double fs) {
      const double A = std::pow(10, db / 40), w = 2 * kPi * hz / fs, al = std::sin(w) / (2 * q), a0 = 1 + al / A;
      b0 = (1 + al * A) / a0;
      b1 = -2 * std::cos(w) / a0;
      b2 = (1 - al * A) / a0;
      a1 = b1;
      a2 = (1 - al / A) / a0;
   }
   void highpass(double hz, double q, double fs) {
      const double w = 2 * kPi * hz / fs, al = std::sin(w) / (2 * q), cs = std::cos(w), a0 = 1 + al;
      b0 = (1 + cs) / 2 / a0;
      b1 = -(1 + cs) / a0;
      b2 = b0;
      a1 = -2 * cs / a0;
      a2 = (1 - al) / a0;
   }
   void lowpass(double hz, double q, double fs) {
      hz = std::min(hz, 0.45 * fs);
      const double w = 2 * kPi * hz / fs, al = std::sin(w) / (2 * q), cs = std::cos(w), a0 = 1 + al;
      b0 = (1 - cs) / 2 / a0;
      b1 = (1 - cs) / a0;
      b2 = b0;
      a1 = -2 * cs / a0;
      a2 = (1 - al) / a0;
   }
   void highshelf(double hz, double db, double fs) {
      const double A = std::pow(10, db / 40), w = 2 * kPi * hz / fs, cs = std::cos(w),
                   al = std::sin(w) / 2 * std::sqrt(2.0);
      const double a0 = (A + 1) - (A - 1) * cs + 2 * std::sqrt(A) * al;
      b0 = A * ((A + 1) + (A - 1) * cs + 2 * std::sqrt(A) * al) / a0;
      b1 = -2 * A * ((A - 1) + (A + 1) * cs) / a0;
      b2 = A * ((A + 1) + (A - 1) * cs - 2 * std::sqrt(A) * al) / a0;
      a1 = 2 * ((A - 1) - (A + 1) * cs) / a0;
      a2 = ((A + 1) - (A - 1) * cs - 2 * std::sqrt(A) * al) / a0;
   }
   void lowshelf(double hz, double db, double fs) {
      const double A = std::pow(10, db / 40), w = 2 * kPi * hz / fs, cs = std::cos(w),
                   al = std::sin(w) / 2 * std::sqrt(2.0);
      const double a0 = (A + 1) + (A - 1) * cs + 2 * std::sqrt(A) * al;
      b0 = A * ((A + 1) - (A - 1) * cs + 2 * std::sqrt(A) * al) / a0;
      b1 = 2 * A * ((A - 1) - (A + 1) * cs) / a0;
      b2 = A * ((A + 1) - (A - 1) * cs - 2 * std::sqrt(A) * al) / a0;
      a1 = -2 * ((A - 1) + (A + 1) * cs) / a0;
      a2 = ((A + 1) + (A - 1) * cs - 2 * std::sqrt(A) * al) / a0;
   }
};

// The player's settings a model reads, beyond breath and embouchure. Each is a multiplier around 1.
struct Character {
   float noise = 1.0f; // breath noise and turbulence
   float edge = 1.0f;  // the forte bite: the flute's 2nd harmonic, the reeds' growl, brass steepening
   float growl = 1.0f; // the forte growl in the reed (the Growl knob over its default)
   // 0..1, 0.5 as the models were tuned: the reed's or lips' stiffness (the jet's gain on a flute),
   // how far the valve stands open (the jet's width), the bore's taper (sax and oboe), and how far
   // a trumpet's wave steepens into a blare.
   float stiffness = 0.5f, aperture = 0.5f, cone = 0.5f, brass = 0.5f;
   // The bore's extra loss (0.04 as tuned: the Loss knob plus the tube's inner surface), and the
   // cutoff of its tone holes over 1.3 kHz (the Hole Cutoff knob, the Holes count and the tube).
   float loss = 0.04f, cutoff = 1.0f;

   // The loss over the models' tuning, easing off towards the top so a reed or lips can still
   // sustain a note at full Loss (a very lossy bore is soft and slow, not silent).
   float extraLoss() const { const float d = loss - 0.04f; return d > 0.0f ? d / (1.0f + d / 0.3f) : d; }
   // A loop's reflection with the Loss knob's share taken out (or, under its default, put back).
   float keep(float base) const { return std::min(0.995f, base * (1.0f - 0.4f * extraLoss())); }
   // A loop filter tuned at `base` Hz moved by the holes' cutoff, but never under `floor` (a note
   // whose own pitch is near the cutoff stops speaking; a real instrument's top notes just get
   // thinner). The floor never lifts a filter above where it was tuned.
   double cut(double base, double floor) const { return std::max(base * cutoff, std::min(base, floor)); }
   // How far the loss moves the model's pitch (in cents: a per unit of loss over its tuning, b per
   // unit squared, measured), as a bore-length change at `nominal` samples that takes it back out.
   double lossDelay(double nominal, double a, double b) const {
      const double d = extraLoss();
      return -nominal * (std::pow(2.0, (a * d + b * d * d) / 1200.0) - 1.0);
   }
   // The Cone knob on the trumpet's bore, which isn't split around its mouthpiece: how much a filter
   // tuned at the model's own taper moves, 2^(-depth (cone - 0.5)): more conical is darker.
   double taper(double depth) const { return std::pow(2.0, -depth * (cone - 0.5)); }
};

// A one-pole lowpass's phase delay at hz, in samples.
inline double onePoleDelay(double hz, double cut, double fs) {
   const double a = std::exp(-2.0 * kPi * std::min(cut, 0.45 * fs) / fs), w = 2.0 * kPi * hz / fs;
   return std::atan2(a * std::sin(w), 1.0 - a * std::cos(w)) / w;
}

class Model {
public:
   virtual ~Model() = default;
   // Allocates for the rate, then reset(). Not real-time safe.
   virtual void prepare(double fs) = 0;
   // Silence, ready for a new note.
   virtual void reset() = 0;
   // The bore length, in samples, that would play hz with no correction.
   virtual double nominal(double hz) const = 0;
   // Filters that follow the note; called when the note or its bend moves.
   virtual void notePitch(double /*hz*/) {}
   // How much longer (in samples, at hz) the loop's filters make the trip with the Hole Cutoff
   // where it is than where the model was tuned: the bore is that much shorter to stay in tune.
   virtual double cutoffDelay(double /*hz*/) const { return 0.0; }
   // One sample. tongue is the tongue's noise burst, 1 at a tongued attack and decaying.
   virtual float tick(float breath, float emb, double len, float tongue) = 0;

   Noise rng;
   Character character;

protected:
   double fs_ = 96000.0;
};

// Flute: an air jet crosses the embouchure hole and is deflected in or out of the pipe by the sound
// in it. The jet takes time to reach the edge (longer when it's slow, so harder breath shortens the
// delay and pushes the note up a register, as on a real flute); a tanh splits the jet in or out; the
// pipe is open at both ends. Turbulence at the edge is the breath noise.
class Flute final : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      const int n = static_cast<int>(fs / 20.0) + 8;
      bore_.init(n);
      jet_.init(n * 4);
      reset();
   }
   void reset() override {
      bore_.clear();
      jet_.clear();
      lp_ = {};
      jlp_ = {};
      nlp_ = {};
      nlp_.setHz(2800, fs_);
      dc_ = {};
      dc_.setFs(fs_);
      dco_ = {};
      dco_.setFs(fs_);
      envF_ = rawPrev_ = 0.0f;
      notePitch(hz_);
   }
   double nominal(double hz) const override { return fs_ / hz; }
   // The holes', tube's and taper's cutoff, its delay taken out (the jet's delay follows the bore,
   // so it shows only part of the filter's), and the loss's pitch.
   double cutoffDelay(double hz) const override {
      return (0.8 + 0.06 * std::log2(loopCut(hz) / (hz * 3.2))) *
                (onePoleDelay(hz, loopCut(hz), fs_) - onePoleDelay(hz, hz * 3.2, fs_)) +
             character.lossDelay(nominal(hz), kLossA, kLossB);
   }
   void notePitch(double hz) override {
      hz_ = hz;
      lp_.setHz(loopCut(hz), fs_);
      jlp_.setHz(hz * (1.4 + 1.2 * character.aperture), fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      const float U = std::sqrt(std::max(breath, 0.0f));
      const float raw = bore_.read(len);
      const float p = character.keep(0.97f) * lp_.tick(raw);
      jet_.push(p);
      const double tau = std::clamp((0.33 + 0.1 * (emb - 0.5)) * len / std::max(U, 0.08f), 2.0, jet_.size() - 8.0);
      const float turb = nlp_.tick(rng.white());
      const float y = jlp_.tick(jet_.read(tau)) + (0.012f * character.noise + 0.12f * tn) * turb;
      // The jet's flow grows with pressure, not just its speed.
      const float q = 1.2f * U * U * std::tanh(8.0f * (0.5f + character.stiffness) * y + 0.3f);
      bore_.push(dc_.tick(p - 0.5f * q));
      // What leaves the embouchure hole and the open end: the tube wave, tilted up a little
      // (radiation), plus edge noise riding on the jet.
      const float rad = raw - 0.85f * rawPrev_;
      rawPrev_ = raw;
      // A flute is nearly pure when it's blown softly; harder, the jet's asymmetry brings up a strong
      // 2nd harmonic, and at forte the 3rd: level-normalised Chebyshev terms on the tube wave, so
      // the colour follows the breath and not the note's loudness.
      envF_ += 0.002f * (96000.0f / static_cast<float>(fs_)) * (std::fabs(raw) - envF_);
      const float a = 1.5708f * envF_ + 1e-4f, n = std::clamp(raw / a, -1.3f, 1.3f);
      const float push = std::clamp((breath - 0.22f) / 0.7f, 0.0f, 1.5f);
      const float even = a * character.edge * (0.03f + 0.75f * push * push) * (2.0f * n * n - 1.0f);
      const float odd = a * character.edge * 0.3f * push * push * push * n * (4.0f * n * n - 3.0f);
      return dco_.tick(0.2f * (0.5f * raw + rad + even + odd) +
                       0.025f * character.noise * U * turb * (0.25f + std::fabs(y)));
   }

private:
   double loopCut(double hz) const { return character.cut(hz * 3.2, 1.5 * hz); }
   // The loss's pitch, in cents (measured), which cutoffDelay() takes back out.
   static constexpr double kLossA = 60.5, kLossB = -16.7;
   Delay bore_, jet_;
   OnePoleLP lp_, jlp_, nlp_;
   DCBlock dc_, dco_;
   double hz_ = 440.0;
   float rawPrev_ = 0.0f, envF_ = 0.0f;
};

// Clarinet: a single reed is a pressure-controlled valve on a cylinder closed at the reed. The reed
// is the Kergomard / Wilson-Beavers valve, u = zeta (1 - gamma + p) sgn(gamma - p) sqrt|gamma - p|,
// shut when the pressure drop closes it, solved each sample together with the pressure it makes.
// The bell and tone holes reflect lows and let highs out (a 2-pole lowpass in the loop); what we
// hear is the transmitted, radiated part.
class Clarinet final : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      bore_.init(static_cast<int>(fs / 40.0) + 8);
      reset();
   }
   void reset() override {
      bore_.clear();
      lp_ = {};
      lp2_ = {};
      notePitch(hz_);
      dc_ = {};
      dc_.setFs(fs_);
      noiseLP_ = {};
      noiseLP_.setHz(800, fs_);
      tx_ = 0.0f;
   }
   double nominal(double hz) const override { return fs_ / (2.0 * hz); }
   double cutoffDelay(double hz) const override {
      const double cut = std::max(1300.0, 2.2 * hz);
      return kCutPitch * 2.0 * (onePoleDelay(hz, character.cut(cut, 2.1 * hz), fs_) - onePoleDelay(hz, cut, fs_)) +
             character.lossDelay(nominal(hz), kLossA, kLossB);
   }
   // The bore's loss sits near 1.3 kHz, which only lets the low and middle register speak; it moves
   // up with notes above that, so they play too (as a real clarinet's tone holes leave more bore).
   void notePitch(double hz) override {
      hz_ = hz;
      // High notes need a firmer push to speak (a player bites and blows harder up there).
      lift_ = 1.0 + 0.2 * std::clamp(std::log2(hz / 440.0) / 0.8, 0.0, 1.0);
      const double cut = character.cut(std::max(1300.0, 2.2 * hz), 2.1 * hz);
      lp_.setHz(cut, fs_);
      lp2_.setHz(cut, fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      const float noise = noiseLP_.tick(rng.white());
      const float growl = std::max(0.0f, breath - 0.9f) * 0.1f * character.edge * character.growl;
      const double g = 0.7 * lift_ * breath * (1 + (0.012f * character.noise + growl) * noise) + tn * 0.04f * noise;
      const double z = 0.35 * (0.8 + 0.4 * emb) * (0.6 + 0.8 * character.aperture);
      // A stiffer reed closes over a shorter curl of its tip.
      const double knee = 0.04 + 0.16 * (1.0 - character.stiffness);
      const double inc = bore_.read(len);
      const double back = -character.keep(0.96f) * lp2_.tick(lp_.tick(static_cast<float>(inc)));
      const auto F = [g, z, knee](double p) {
         // The reed closes softly (its tip curls onto the lay), not at a hard corner, which would
         // make a beating reed's tone rough and noisy.
         const double W = g - p, gap = 1 - W;
         if (gap <= -knee)
            return 0.0;
         const double open = gap >= knee ? gap : (gap + knee) * (gap + knee) / (4 * knee);
         return z * open * (W >= 0 ? 1 : -1) * std::sqrt(std::fabs(W));
      };
      // u = F(2 back + u): the flow and the pressure it makes, by bisection (F falls as u rises).
      double lo = -3, hi = 3;
      for (int it = 0; it < 24; ++it) {
         const double u = 0.5 * (lo + hi);
         if (u - F(2 * back + u) > 0)
            hi = u;
         else
            lo = u;
      }
      bore_.push(static_cast<float>(back + 0.5 * (lo + hi)));
      // Radiated: what leaves the open end, (1 + R) p, differentiated (a monopole).
      const float t = static_cast<float>(inc + back);
      const float o = (t - tx_) * static_cast<float>(fs_ / 96000.0);
      tx_ = t;
      return dc_.tick(8.0f * o);
   }

private:
   // How much of the filters' delay shows in the pitch, and the loss's pitch in cents (measured).
   static constexpr double kCutPitch = 1.0, kLossA = -13.0, kLossB = 1.8;
   Delay bore_;
   OnePoleLP lp_, lp2_, noiseLP_;
   DCBlock dc_;
   double hz_ = 294.0, lift_ = 1.0;
   float tx_ = 0.0f;
};

// Reed on a cone: the sax and oboe. The bore is split around the reed so the loop behaves like a
// cone (every harmonic, overblowing at the octave). The reed's opening follows the pressure across
// it, clipped where it slams shut.
class ConicalReed : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      const int n = static_cast<int>(fs / 20.0) + 8;
      d0_.init(n);
      d1_.init(n);
      reset();
   }
   void reset() override {
      d0_.clear();
      d1_.clear();
      lp_ = {};
      cutK_ = -1.0f;
      dc_ = {};
      dc_.setFs(fs_);
      noiseLP_ = {};
      noiseLP_.setHz(noiseHz_, fs_);
      out0_ = 0.0f;
      register_.clear();
      regHz_ = 0.0;
      notePitch(hz_);
      resetColour();
   }
   double nominal(double hz) const override { return fs_ / hz; }
   double cutoffDelay(double hz) const override {
      // The Cone knob moves the reed along the split bore, which sharpens it towards a full cone
      // (measured, in cents): taken back out.
      const double c = 2.0 * (character.cone - 0.5), coneCents = c * (3.33 + 4.3 * c + 2.87 * c * c);
      return cutPitch_ * (onePoleDelay(hz, loopCut(hz), fs_) - onePoleDelay(hz, cut_, fs_)) +
             character.lossDelay(nominal(hz), lossA_, lossB_) -
             nominal(hz) * (std::pow(2.0, coneCents / 1200.0) - 1.0);
   }
   // A split bore lets the second mode compete with the first, and on its own the loop jumps to the
   // octave on most notes. A gentle lift at the fundamental (a cone's wider first resonance, from
   // its missing apex) keeps it in the low register; on release the loop still dies away.
   void notePitch(double hz) override {
      if (character.cutoff != cutK_ || hz != regHz_) {
         cutK_ = character.cutoff;
         lp_.setHz(std::min(loopCut(hz), 0.45 * fs_), fs_);
      }
      if (hz == regHz_)
         return;
      regHz_ = hz_ = hz;
      register_.peak(hz, 0.7, 1.5, fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      breath_ = breath;
      const float noise = noiseLP_.tick(rng.white());
      const float growl = std::max(0.0f, breath - 0.95f) * growl_ * character.edge * character.growl;
      const float p = breath * (1 + (breathNoise_ * character.noise + growl) * noise) + tn * tongueNoise_ * noise;
      // Where the reed sits along the split bore: the cone's truncation, moved by the Cone knob. The
      // short side is the missing apex; harmonics that fit a whole number of times into it are
      // cancelled, so at 1/2 (Cone 0) the evens go and it sounds like a cylinder, a clarinet; near
      // the apex (Cone 1) every harmonic is there, as in a long, sharp cone.
      const double cone = character.cone - 0.5;
      const double pos = posBase_ + posEmb_ * (emb - 0.5) - (cone < 0 ? 0.2 : 0.22) * cone;
      // The lift puts the loop's gain a little over one at the fundamental; the reed's losses hold it
      // there in normal playing, and the bore's walls (a clip far above any played level) at extremes.
      const float temp = std::clamp(-character.keep(0.95f) * register_.tick(lp_.tick(d0_.read(std::max(2.0, len * (1 - pos))))), -6.0f, 6.0f);
      out0_ = temp - d1_.read(std::max(2.0, len * pos));
      const float pd = p - out0_;
      const float r = std::clamp(0.7f - 0.24f * (character.aperture - 0.5f) +
                                    (stiff_ + stiffEmb_ * emb) * (0.6f + 0.8f * character.stiffness) * pd,
                                 -1.0f, 1.0f);
      d1_.push(temp);
      // A tongue on the reed stops it vibrating, so the tube rings down quickly.
      const float stopped = std::clamp(breath * 8.0f, 0.0f, 1.0f);
      d0_.push((p - pd * r - temp) * (0.9f + 0.1f * stopped));
      return colour(dc_.tick(out0_)) * 0.6f;
   }

protected:
   virtual void resetColour() = 0;
   virtual float colour(float x) = 0;
   double loopCut(double hz) const { return character.cut(cut_, 2.6 * hz); }
   // How much of the filter's delay shows in the pitch, and the loss's pitch in cents (measured).
   double cutPitch_ = 0.83, lossA_ = -2.5, lossB_ = -124.0;
   float cut_ = 5200, noiseHz_ = 3000, breathNoise_ = 0.06f, tongueNoise_ = 0.3f, growl_ = 0.6f;
   float breath_ = 0.0f;
   float posBase_ = 0.2f, posEmb_ = 0.08f, stiff_ = 0.22f, stiffEmb_ = 0.14f;

private:
   Delay d0_, d1_;
   OnePoleLP lp_, noiseLP_;
   Biquad register_;
   DCBlock dc_;
   double hz_ = 294.0, regHz_ = 0.0;
   float out0_ = 0.0f, cutK_ = -1.0f;
};

// Saxophone: a mouthpiece-cavity resonance gives the honk; noise in the reed gives a little growl
// when it's pushed hard.
class Sax final : public ConicalReed {
protected:
   void resetColour() override {
      honk_ = {};
      honk_.peak(1250, 1.2, 5.0, fs_);
   }
   float colour(float x) override { return honk_.tick(x); }

private:
   Biquad honk_;
};

// Oboe (double reed): two cane blades beating at the top of a narrow cone. The sax's loop with a
// stiffer, smaller reed and a darker bore, and fixed formants near 1.1 and 2.9 kHz from the bell and
// tone holes, which is what makes it read as an oboe and not a soprano sax.
class Oboe final : public ConicalReed {
public:
   Oboe() {
      cut_ = 3800;
      cutPitch_ = 0.89;
      lossA_ = -32.0;
      lossB_ = -65.0;
      noiseHz_ = 3500;
      breathNoise_ = 0.04f;
      tongueNoise_ = 0.25f;
      growl_ = 0.3f;
      posEmb_ = 0.06f;
      stiffEmb_ = 0.12f;
   }

protected:
   void resetColour() override {
      f1_ = {};
      f1_.peak(1250, 1.0, 6.0, fs_);
      f2_ = {};
      f2_.peak(2900, 1.8, 5.0, fs_);
      f3_ = {};
      f3_.lowshelf(550, -7.0, fs_);
      env_ = 0.0f;
      envCoef_ = static_cast<float>(1.0 - std::exp(-2.0 * kPi * 20.0 / fs_));
      dc_ = {};
      dc_.setFs(fs_);
   }
   // The two blades beat shut every cycle, so what leaves the reed is a train of narrow pulses rich
   // in harmonics, more so the harder it's blown: shaped here on the level-normalised wave, so
   // it brightens with the breath and not with the note's loudness.
   float colour(float x) override {
      env_ += (std::fabs(x) - env_) * envCoef_;
      const float a = 1.5708f * env_ + 1e-6f;
      const float n = std::clamp(x / a, -1.3f, 1.3f), n2 = n * n;
      const float buzz = (0.25f + 0.5f * std::clamp(breath_ - 0.5f, 0.0f, 0.6f)) * character.edge;
      // Chebyshev T2, T3 and T5 of the normalised wave: the 2nd, 3rd and 5th harmonics on a sine.
      const float t2 = 2 * n2 - 1, t3 = n * (4 * n2 - 3), t5 = n * (16 * n2 * n2 - 20 * n2 + 5);
      const float y = x + a * buzz * (0.35f * t2 + 0.5f * t3 + 0.4f * t5);
      return f3_.tick(f2_.tick(f1_.tick(dc_.tick(y))));
   }

private:
   DCBlock dc_;
   float env_ = 0.0f, envCoef_ = 0.001f;
   Biquad f1_, f2_, f3_;
};

// Trumpet: the lips are a mass on a spring, pushed open by the mouth pressure against the pressure
// in the mouthpiece (an outward-striking valve, tuned a little under the note); air through the slit
// goes as the square root of the pressure drop. The bore is a one-period loop whose resonances fall
// on every harmonic, as a flared horn's do, with a reflection of -1 at DC (a horn can't hold a steady
// pressure in the mouthpiece). The bell reflects the lows and lets the highs out above a fixed
// cutoff (about 1.4 kHz, as a Bb trumpet's), so the resonances fade above it; what leaves the bell
// is the wave's highs, with the bell's and mouthpiece's formant just above 1 kHz. Each trip loses
// about 5% (a real trumpet's resonances have a Q of 20 to 40, so a note builds and settles in a few
// tens of ms). At forte the wave steepens on its way down the tube: the brassy edge.
class Trumpet final : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      bore_.init(static_cast<int>(fs / 20.0) + 8);
      shock_.init(static_cast<int>(1024 * fs / 96000.0) + 8);
      reset();
   }
   void reset() override {
      bore_.clear();
      shock_.clear();
      bell1_ = {};
      bell2_ = {};
      x_ = v_ = u_ = dcz_ = closed_ = 0.0;
      tongue_ = 0.0f;
      shockD_ = kShock0 * fs_ / 96000.0;
      acMean_ = acEnv_ = 0.0;
      acLP_ = {};
      acLP2_ = {};
      acCoef_ = 1.0 - std::exp(-2.0 * kPi * 25.0 / fs_);
      envFall_ = std::exp(-1.0 / (0.03 * fs_));
      closedCoef_ = 1.0 - std::exp(-1.0 / (0.004 * fs_));
      radHP_ = {};
      radHP_.highpass(1300, 1.4, fs_);
      radLP_ = {};
      radLP_.lowpass(5000, 0.7, fs_);
      notePitch(lipHz_);
   }
   double nominal(double hz) const override { return fs_ / hz; }
   double cutoffDelay(double hz) const override {
      return bellDelay(hz, character.cutoff, character.cone, character.loss) -
             bellDelay(hz, 1.0f, character.cone, character.loss);
   }
   void notePitch(double hz) override {
      lipHz_ = hz;
      acLP_.setHz(hz * 1.2, fs_);
      acLP2_.setHz(hz * 1.2, fs_);
      const double cut = std::min(bellCut(hz, character.cutoff, character.cone, character.loss), 0.45 * fs_);
      bell1_.setHz(cut, fs_);
      bell2_.setHz(cut, fs_);
      // The Cone's and Loss's bell delays the trip by more or less than the tuned one: the bore
      // makes it up (the Hole Cutoff's share is the voice's, through cutoffDelay()).
      coneDelay_ = character.cone != 0.5f || character.loss != 0.04f
                      ? bellDelay(hz, 1.0f, character.cone, character.loss) - bellDelay(hz, 1.0f, 0.5f, 0.04f)
                      : 0.0;
      trim_ = std::exp2((trim(character.aperture, kTrimAp0, kTrimAp1) + trim(character.stiffness, kTrimSt0, kTrimSt1) +
                         150.0 * (character.loss - 0.04f)) /
                        1200.0);
   }
   float tick(float breath, float emb, double len, float tn) override {
      // The lips' own frequency: under the note, tighter with the embouchure.
      const double dt = 1.0 / fs_, w = 2 * kPi * lipHz_ * (0.9 + 0.12 * (emb - 0.5));
      const double pm = breath * (1 + 0.02 * tn * rng.white()) * (1 + 0.03 * character.noise * rng.white());
      // While the lips don't yet close on each cycle (the first few ms of a note), they pull the
      // pitch sharp: a real trumpet's long bore holds it, this one-period loop is lengthened instead.
      const double pull = 0.06 * (0.33 + breath) * std::max(0.0, 1.0 - closed_ / 0.3);
      if (emb != emb_) {
         emb_ = emb;
         embTrim_ = std::exp2(trim(emb, kTrimEm0, kTrimEm1) / 1200.0);
      }
      const double xin = bore_.read((len * trim_ * embTrim_ - coneDelay_) * (1.0 + pull));
      dcz_ += (xin - dcz_) * (2 * kPi * 20 / fs_);
      const double pin = character.keep(0.95f) * (bell2_.tick(bell1_.tick(static_cast<float>(xin))) - 2.0 * dcz_);
      // Lips: pushed open by mouth pressure minus mouthpiece pressure (with the last sample's flow).
      const double a = -w * w * x_ - (w / (4.0 * std::exp2(character.stiffness - 0.5))) * v_ + 0.7 * w * w * (pm - 2 * pin - u_) +
                       1e-4 * rng.white();
      v_ += dt * a;
      // The tongue's release plucks the lips open, so the note starts at once, as hard as tongued.
      v_ += 1.5 * std::max(0.0f, tn - tongue_) * w;
      tongue_ = tn;
      x_ += dt * v_;
      closed_ += ((x_ <= 0.0 ? 1.0 : 0.0) - closed_) * closedCoef_;
      const double h = std::max(x_, 0.0) * (0.5 + character.aperture);
      // The flow through the slit, solved in closed form with the pressure it makes:
      // u = h sgn(D) sqrt|D - u|, D = pm - 2 pin.
      const double D = pm - 2 * pin;
      const double sq = 0.5 * (-h + std::sqrt(h * h + 4 * std::fabs(D)));
      const double u = (D >= 0 ? 1 : -1) * h * sq;
      const double pplus = pin + u;
      u_ = u;
      bore_.push(static_cast<float>(pplus));
      // Louder pressure travels faster down the horn, so the outgoing wave steepens towards a shock:
      // a read delay that follows the pressure. How far it steepens (sigma, the delay's steepest
      // slope: 1 is a shock) grows with the breath, so a trumpet only blares when it's pushed; the
      // pressure's own level is divided out, so every note and setting steepens alike.
      shock_.push(static_cast<float>(pplus));
      const double k = fs_ / 96000.0;
      acMean_ += (pplus - acMean_) * acCoef_;
      // Driven by the wave's fundamental, so the read never runs faster than the wave allows; its
      // level is a peak that rises at once, so a growing note never steepens past what was asked.
      const double ac = acLP2_.tick(acLP_.tick(static_cast<float>(pplus - acMean_)));
      acEnv_ = std::max(std::fabs(ac), acEnv_ * envFall_);
      const double push = std::clamp((breath - 0.2) / 0.48, 0.0, 1.5);
      // A conical bore spreads the wave out and steepens it less: a flugelhorn hardly blares.
      const double sigma = (0.9 + 1.4 * push * push) * character.edge * (character.brass / 0.5) *
                           (1.5 - character.cone);
      const double K = sigma * fs_ / (2.0 * kPi * lipHz_ * (acEnv_ + 1e-4));
      const double want = k * kShock0 - K * ac;
      shockD_ += std::clamp(std::clamp(want, 2.0, 2.0 * kShock0 * k) - shockD_, -0.95, 0.95);
      // The bell passes highs and reflects lows.
      return radLP_.tick(radHP_.tick(shock_.read(shockD_))) * 3.0f;
   }

private:
   // The bell's cutoff: a Bb trumpet's 1.4 kHz, but never under the note's 2nd harmonic, so the
   // top of the range still has a resonance to play on.
   // The Cone knob flares the bore: under the middle a narrower, more cylindrical trumpet (a
   // higher cutoff), over it a wider, more conical one like a flugelhorn's (a lower cutoff).
   // The Loss knob's wall losses grow with frequency, so a lossier tube is also a darker one.
   double bellCut(double hz, float cutoff, float cone, float loss) const {
      return std::max(1400.0, 2.0 * hz) * cutoff * std::exp2(-1.4 * (cone - 0.5) - 1.2 * (loss - 0.04f));
   }
   double bellDelay(double hz, float cutoff, float cone, float loss) const {
      return 2.0 * onePoleDelay(hz, std::min(bellCut(hz, cutoff, cone, loss), 0.45 * fs_), fs_);
   }
   // The lips' settings move the pitch (they pull it towards their own frequency, more or less):
   // a player lips it back, here the bore is trimmed by as much (in cents of length, measured).
   static double trim(double v, double at0, double at1) {
      const double d = v - 0.5;
      return (at1 - at0) * d + 2.0 * (at0 + at1) * d * d;
   }

   Delay bore_, shock_;
   OnePoleLP bell1_, bell2_, acLP_, acLP2_;
   Biquad radHP_, radLP_;
   static constexpr double kShock0 = 480.0;
   double acMean_ = 0, acEnv_ = 0, acCoef_ = 0.001, envFall_ = 0.9997, closedCoef_ = 0.003;
   double x_ = 0, v_ = 0, u_ = 0, lipHz_ = 440, dcz_ = 0, shockD_ = 120, closed_ = 0, coneDelay_ = 0, trim_ = 1;
   static constexpr double kTrimAp0 = -134, kTrimAp1 = 91, kTrimSt0 = 16, kTrimSt1 = -45, kTrimEm0 = -150, kTrimEm1 = 115;
   double emb_ = 0.5, embTrim_ = 1;
   float tongue_ = 0.0f;
};

// Alien: an instrument that couldn't be built, from physics that could. A mouthpiece with two reeds
// in one small chamber, each on its own bore, and both bores coiled as springs (helices).
//
// - A helix is dispersive: a wave's speed along it depends on its frequency, so its resonances
//   aren't the harmonic series a straight tube has but stretched like a bell's or a stiff bar's.
//   Here that's a chain of allpass sections in the loop (all of the delay at the note, less above),
//   placed at fixed ratios of the note so every register is stretched alike; the Cone knob sets how
//   far. A note's attack and release ring at those stretched modes: a chirp, a spring's "boing".
// - The main bore is a closed cylinder with a single reed (the clarinet's valve) and holds the
//   pitch. The second, the "ghost", is shorter by an irrational ratio (2.76, a free bar's second
//   mode over its first, at the middle embouchure; MPE slide moves it from about 2.3 to 3.3, through
//   the harmonic 3 where it locks), with a stiffer reed that only opens once the breath is strong.
// - The two reeds share the chamber, whose pressure falls as either draws air: each reed is blown by
//   the other's flow as well as the player's breath. Two oscillations that aren't harmonic, driven
//   through one nonlinearity, give every combination tone m f0 + n f1 (a multiphonic) and pull on
//   each other's pitch; near a simple ratio they lock (a mode jump).
//
// So the colour runs with the breath: soft, a hollow, nearly pure tone; middling, the ghost's
// inharmonic partial rises out of it as a bell-like shimmer; hard, the reeds fight and the spectrum
// fills with sidebands, a ring-modulated roar, with the main bore's pitch still underneath.
class Alien final : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      a_.bore.init(static_cast<int>(fs / 40.0) + 8);
      b_.bore.init(static_cast<int>(fs / 100.0) + 8);
      irisCoef_ = 1.0 - std::exp(-2.0 * kPi * 30.0 / fs);
      reset();
   }
   void reset() override {
      a_.clear();
      b_.clear();
      dc_ = {};
      dc_.setFs(fs_);
      noiseLP_ = {};
      noiseLP_.setHz(1200, fs_);
      radB_ = {};
      rad_ = {};
      chamber_ = {};
      tx_ = 0.0f;
      uA_ = uB_ = uMean_ = 0.0;
      iris_ = 0.0;
      coneK_ = -1.0f;
      notePitch(hz_);
   }
   // The main bore's loop is half the period, less the delay its helix and bell add at the note.
   double nominal(double hz) const override {
      return fs_ / (2.0 * hz) - Helix::delayAt(hz, kFc * hz, kBw * hz, fs_) * 2.0 - onePoleDelay(hz, bellA(hz), fs_);
   }
   double cutoffDelay(double hz) const override {
      return onePoleDelay(hz, bellA(hz) * holes(), fs_) - onePoleDelay(hz, bellA(hz), fs_);
   }
   void notePitch(double hz) override {
      a_.keep = character.keep(0.96f);
      b_.keep = character.keep(0.95f);
      const float cone = character.cone, emb = embNow_;
      if (hz == hz_ && cone == coneK_ && emb == embK_ && character.cutoff == cutK_)
         return;
      hz_ = hz;
      coneK_ = cone;
      embK_ = emb;
      cutK_ = character.cutoff;
      // The helix's pitch: Cone 0 is looser, 1 coiled tight. Its delay at the note moves with it, so
      // the bore gives back (or takes) the difference; and the stretched modes pull the note away
      // from the first one's own pitch (the reed plays a compromise between the resonances), by
      // as much again, measured: the bore's length follows that too, so the Cone knob stays in tune.
      const double bw = helixBw(cone);
      a_.set(kFc * hz, bw * hz, bellA(hz) * holes(), fs_);
      lenFix_ = 2.0 * (Helix::delayAt(hz, kFc * hz, kBw * hz, fs_) - Helix::delayAt(hz, kFc * hz, bw * hz, fs_));
      const double c = std::clamp(static_cast<double>(cone), 0.0, 1.0);
      const double above = std::clamp(12.0 * std::log2(hz / 82.4), 0.0, 48.0); // semitones over E2
      lenScale_ = c >= 0.5 ? 1.0 + 0.08 * (c - 0.5) : 1.0 - (0.5 - c) * 2.0 * (0.05 - 0.00056 * above);
      // The ghost bore, a little stiffer still, at its ratio over the note.
      const double rho = kRho * std::pow(2.0, 0.6 * (emb - 0.5));
      const double hb = hz * rho;
      b_.set(kFc * hb, 0.8 * bw * hb, bellB(hb), fs_);
      radB_.setHz(2.0 * bellB(hb), fs_);
      rad_.setHz(std::max(5000.0, 10.0 * hz) * character.cutoff, fs_);
      chamber_.setHz(std::min(6.0 * hz, 3000.0), fs_);
      lenB_ = fs_ / (2.0 * hb) - 2.0 * Helix::delayAt(hb, kFc * hb, 0.8 * bw * hb, fs_) -
              onePoleDelay(hb, bellB(hb), fs_);
      nomA_ = nominal(hz);
      extra_ = fs_ / (2.0 * hz) - nomA_;
      meanCoef_ = 1.0 - std::exp(-2.0 * kPi * std::max(8.0, 0.25 * hz) / fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      if (emb != embNow_ && ++embCount_ >= 32) {
         embCount_ = 0;
         embNow_ = emb;
         notePitch(hz_);
      }
      const float noise = noiseLP_.tick(rng.white());
      // Over its forte the main reed growls, as a clarinet's does.
      const float growl = std::max(0.0f, breath - 1.0f) * 0.12f * character.edge * character.growl;
      // The shared chamber: only the flow's swing moves its pressure (the lungs hold its mean), and
      // the air in it has mass, so it follows the swing through a lowpass.
      const double uSum = uA_ + uB_;
      uMean_ += (uSum - uMean_) * meanCoef_;
      const double kappa = 1.5 * std::clamp(character.edge, 0.0f, 2.0f);
      const double swing = chamber_.tick(static_cast<float>(kappa * (uSum - uMean_)));
      const double pc = breath * (1.0 + (0.012 * character.noise + growl) * noise) - swing;
      const double z = 0.35 * (0.6 + 0.8 * character.aperture);
      const double knee = 0.04 + 0.16 * (1.0 - character.stiffness);
      // The voice stretches the bore for bends, unison and the breath's drift: stretch the whole
      // loop (the helix's delay at the note with it), so a cent asked for is a cent played.
      const double lenA = std::max(2.0, (len * (nomA_ + extra_) / nomA_ - extra_ + lenFix_) * lenScale_);
      uA_ = a_.tick(0.7 * pc + tn * 0.04 * noise, z, knee, lenA);
      // The ghost's stiffer reed needs half as much pressure again to swing; its sound leaves through
      // an iris that the breath pushes open, so it rises out of the tone as the player blows harder.
      uB_ = b_.tick(0.7 * kShareB * pc + tn * 0.02 * noise, 0.85 * z, 0.75 * knee, std::max(2.0, lenB_ * len / nomA_));
      const double open = std::clamp((breath - kIrisOn) / (kIrisFull - kIrisOn), 0.0, 1.0);
      iris_ += (open * open * (3.0 - 2.0 * open) - iris_) * irisCoef_;
      // Radiated: what leaves the open ends, (1 + R) p, differentiated (a monopole), as the clarinet.
      // The ghost's reed beats shut when it's pushed, and its thin horn passes little of the buzz.
      const float t = static_cast<float>(a_.t + kGhostLevel * iris_ * radB_.tick(static_cast<float>(b_.t)));
      const float o = (t - tx_) * static_cast<float>(fs_ / 96000.0);
      tx_ = t;
      // A beating reed's corners would ring up to the top of the band: the coil's walls damp them.
      return dc_.tick(32.0f * rad_.tick(o));
   }

private:
   // A second-order allpass: the helix's frequency-dependent delay.
   struct Helix {
      double a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
      void clear() { x1 = x2 = y1 = y2 = 0; }
      void set(double fc, double bw, double fs) {
         const double r = std::exp(-kPi * bw / fs), th = 2.0 * kPi * std::min(fc, 0.45 * fs) / fs;
         a1 = -2.0 * r * std::cos(th);
         a2 = r * r;
      }
      double tick(double x) {
         const double y = a2 * x + a1 * x1 + x2 - a1 * y1 - a2 * y2;
         x2 = x1;
         x1 = x;
         y2 = y1;
         y1 = y;
         return y;
      }
      // Its phase delay at hz, in samples (below the section's centre, where a note's fundamental is).
      static double delayAt(double hz, double fc, double bw, double fs) {
         Helix h;
         h.set(fc, bw, fs);
         const double w = 2.0 * kPi * hz / fs;
         const double re = 1.0 + h.a1 * std::cos(w) + h.a2 * std::cos(2 * w);
         const double im = -(h.a1 * std::sin(w) + h.a2 * std::sin(2 * w));
         double lag = 2.0 * w + 2.0 * std::atan2(im, re);
         while (lag < 0.0)
            lag += 2.0 * kPi;
         return lag / w;
      }
   };
   // A reed on a closed helix: the clarinet's valve, solved with the pressure it makes.
   struct Bore {
      Delay bore;
      Helix h1, h2;
      OnePoleLP bell;
      double keep = 0.96, t = 0.0, flow = 0.0;
      void clear() {
         flow = 0.0;
         bore.clear();
         h1.clear();
         h2.clear();
         bell.z = 0.0f;
         t = 0.0;
      }
      void set(double fc, double bw, double cut, double fs) {
         h1.set(fc, bw, fs);
         h2.set(fc, bw, fs);
         bell.setHz(cut, fs);
      }
      double tick(double g, double z, double knee, double len) {
         const double inc = bore.read(len);
         const double back = -keep * bell.tick(static_cast<float>(h2.tick(h1.tick(inc))));
         const auto F = [g, z, knee](double p) {
            const double W = g - p, gap = 1 - W;
            if (gap <= -knee)
               return 0.0;
            const double open = gap >= knee ? gap : (gap + knee) * (gap + knee) / (4 * knee);
            return z * open * (W >= 0 ? 1 : -1) * std::sqrt(std::fabs(W));
         };
         // u = F(2 back + u), by bisection (F falls as u rises), from a bracket around the last
         // sample's flow when it holds the answer (it nearly always does), else the whole range.
         const auto G = [&F, back](double u) { return u - F(2 * back + u); };
         double lo = flow - 0.03, hi = flow + 0.03;
         int steps = 12;
         if (!(G(lo) <= 0 && G(hi) > 0)) {
            lo = -3;
            hi = 3;
            steps = 22;
         }
         for (int it = 0; it < steps; ++it) {
            const double u = 0.5 * (lo + hi);
            if (G(u) > 0)
               hi = u;
            else
               lo = u;
         }
         flow = 0.5 * (lo + hi);
         bore.push(static_cast<float>(back + flow));
         t = inc + back;
         return flow;
      }
   };
   // The Hole Cutoff and Holes: mostly how much of the highs the coil lets out (the radiation), only
   // a little the loop's own loss, which would pull the note against the stretched modes.
   double holes() const {
      return std::clamp(std::pow(static_cast<double>(std::max(character.cutoff, 0.05f)), 0.25), 0.85, 1.12);
   }
   // The helix's allpass bandwidth over the note: 2.8 at Cone 0.5 (as tuned), narrower (a tighter
   // coil, its delay more concentrated near the note, the modes above stretched further) as it rises.
   static double helixBw(float cone) { return kBw * std::pow(2.0, 0.8 * (0.5 - std::clamp(cone, 0.0f, 1.0f))); }

   // The bells: their cutoff follows the note, so every register is coloured alike and the upper
   // (stretched) modes never lose less than the first, which would let a slow swell start a low
   // note in a high, inharmonic register.
   static double bellA(double hz) { return kCutA * hz; }
   static double bellB(double hz) { return kCutB * hz; }
   static constexpr double kFc = 2.0, kBw = 2.8, kCutA = 6.0, kCutB = 5.0, kRho = 2.756;
   static constexpr double kShareB = 0.66, kIrisOn = 0.8, kIrisFull = 1.3, kGhostLevel = 0.35;
   Bore a_, b_;
   OnePoleLP noiseLP_, radB_, rad_, chamber_;
   DCBlock dc_;
   double hz_ = 220.0, lenB_ = 50.0, nomA_ = 200.0, lenFix_ = 0.0, lenScale_ = 1.0, extra_ = 0.0;
   double meanCoef_ = 0.01, irisCoef_ = 0.002;
   double uA_ = 0.0, uB_ = 0.0, uMean_ = 0.0, iris_ = 0.0;
   float tx_ = 0.0f, coneK_ = -1.0f, embK_ = -1.0f, cutK_ = -1.0f, embNow_ = 0.5f;
   int embCount_ = 0;
};

// How each instrument is played: its breath scale and offset (mouth pressure from the player's 0..1
// level), and the breath and embouchure its tuning table was measured at.
struct ModelInfo {
   const char *name;
   float breathScale, breathOffset;
   float tuneBreath;
};

inline const ModelInfo &modelInfo(int id) {
   static const ModelInfo info[kModelCount] = {
      {"Flute", 0.8f, 0.15f, 0.75f},
      {"Clarinet", 0.42f, 0.5f, 0.815f},
      {"Sax", 0.65f, 0.2f, 0.6875f},
      {"Oboe", 0.42f, 0.48f, 0.795f},
      {"Trumpet", 0.48f, 0.2f, 0.65f},
      {"Alien", 0.6f, 0.5f, 0.8f},
   };
   return info[std::clamp(id, 0, kModelCount - 1)];
}

// How far (in cents) a model's pitch moves from its tuning as the breath changes, measured on
// every model across its range (it hardly depends on the note). Between the points it's
// interpolated; past them it holds.
inline double breathDrift(int model, double breath) {
   struct Point { float breath, cents; };
   static const Point flute[] = {{0.21f, -80}, {0.25f, -55}, {0.31f, -32}, {0.39f, -20}, {0.51f, -10},
                                 {0.63f, -5}, {0.79f, 1}, {0.95f, 7}, {1.2f, 14}};
   static const Point clarinet[] = {{0.53f, -5}, {0.63f, -5}, {0.75f, -2}, {0.84f, 0}, {0.92f, 1}};
   // (The trumpet's lips pull its pitch towards their own, so its bore moves further than its
   // pitch would: these are the bore's cents that hold it in tune.)
   static const Point trumpet[] = {{0.12f, -167}, {0.16f, -147}, {0.20f, -128}, {0.24f, -112},
                                   {0.30f, -91},  {0.36f, -72},  {0.42f, -53},  {0.50f, -33},
                                   {0.58f, -14},  {0.65f, 0},    {0.75f, 17},   {0.90f, 40}};
   // The alien rises a little as its reed first swings, and the ghost reed pulls it flat once it joins.
   static const Point alien[] = {{0.56f, 10}, {0.6f, 13}, {0.65f, 14}, {0.7f, 12}, {0.76f, 6}, {0.8f, 0},
                                 {0.85f, -6.5f}, {0.9f, -9.5f}, {1.0f, -10}, {1.1f, -9}, {1.2f, -7}, {1.4f, -3}};
   const Point *p = nullptr;
   int n = 0;
   switch (model) {
   case kFlute: p = flute; n = 9; break;
   case kClarinet: p = clarinet; n = 5; break;
   case kTrumpet: p = trumpet; n = 12; break;
   case kAlien: p = alien; n = 12; break;
   default: return 0.0;
   }
   if (breath <= p[0].breath)
      return p[0].cents;
   for (int i = 1; i < n; ++i)
      if (breath < p[i].breath) {
         const double f = (breath - p[i - 1].breath) / (p[i].breath - p[i - 1].breath);
         return p[i - 1].cents + f * (p[i].cents - p[i - 1].cents);
      }
   return p[n - 1].cents;
}

} // namespace jbreath
