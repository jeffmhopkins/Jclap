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
#include <complex>
#include <cstdint>
#include <vector>

namespace jbreath {

constexpr double kPi = 3.14159265358979323846;

enum ModelId : int { kFlute, kClarinet, kSax, kOboe, kTrumpet, kModelCount };

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
   // The bore's extra loss (0.04 as tuned), and its holes' cutoff over 1.3 kHz.
   float loss = 0.04f, cutoff = 1.0f;

   // A loop's reflection with the Loss knob's share taken out (or, under its default, put back).
   float keep(float base) const { return std::min(0.995f, base * (1.0f - 0.4f * (loss - 0.04f))); }
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
   double cutoffDelay(double hz) const override {
      return onePoleDelay(hz, hz * 3.2 * character.cutoff, fs_) - onePoleDelay(hz, hz * 3.2, fs_);
   }
   void notePitch(double hz) override {
      hz_ = hz;
      lp_.setHz(hz * 3.2 * character.cutoff, fs_);
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
      return 2.0 * (onePoleDelay(hz, cut * character.cutoff, fs_) - onePoleDelay(hz, cut, fs_));
   }
   // The bore's loss sits near 1.3 kHz, which only lets the low and middle register speak; it moves
   // up with notes above that, so they play too (as a real clarinet's tone holes leave more bore).
   void notePitch(double hz) override {
      hz_ = hz;
      // High notes need a firmer push to speak (a player bites and blows harder up there).
      lift_ = 1.0 + 0.2 * std::clamp(std::log2(hz / 440.0) / 0.8, 0.0, 1.0);
      const double cut = std::max(1300.0, 2.2 * hz) * character.cutoff;
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
   Delay bore_;
   OnePoleLP lp_, lp2_, noiseLP_;
   DCBlock dc_;
   double hz_ = 294.0, lift_ = 1.0;
   float tx_ = 0.0f;
};

// Reed on a cone: the sax and oboe. A truncated cone's input admittance is an open pipe's in
// parallel with a short one as long as the missing apex (Ayers; Dalmont and Kergomard's "cylindrical
// saddle"): -j cot(kL) - j cot(kx0), whose zeros fall on every harmonic of L + x0. So the bore is two
// tubes meeting at the reed, both open at their far ends: the body, whose bell and open holes reflect
// the lows and let the highs out, and the missing apex (the mouthpiece's or the reed's own volume),
// a round trip of tau. Blown hard, the reed shuts for about that round trip once a period (the
// two-step Helmholtz motion, as on a bowed string), a pulse of fixed length: so the spectrum has
// minima at fixed frequencies (multiples of 1/tau) with formants between them, the same on every
// note, and the share of the period the reed is shut grows up the range, which takes the even
// harmonics out of the middle register. Blown softly the reed never shuts and the tone is nearly
// pure; the harder it's blown, the more it beats and the brighter it gets.
//
// The reed is quasi-static (its own resonance, 2 to 3 kHz on a real reed, sits above every note
// played): its opening closes in proportion to the pressure drop across it, onto a lay that it
// meets over a short curl of its tip, and the flow through its slit is Bernoulli's, u = zeta h
// sgn(dp) sqrt|dp|, solved in closed form with the pressure it makes. Pressures are in units of the
// one that would shut the reed.
//
// The losses on each trip are a little less at the fundamental than at the harmonics above it (the
// wall losses grow with frequency, and the radiation from the holes more so); that keeps a cone in
// its low register, where with flat losses the octave can take over.
class ConicalReed : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      body_.init(static_cast<int>(fs / 20.0) + 8);
      apex_.init(static_cast<int>(fs / 20.0) + 8); // up to the cylinder (Cone 0) at the lowest note
      reset();
   }
   void reset() override {
      body_.clear();
      apex_.clear();
      lattice_ = {};
      apexLattice_ = {};
      lift_ = {};
      radiate_ = {};
      wall_ = {};
      apexWall_ = {};
      noiseLP_ = {};
      noiseLP_.setHz(noiseHz_, fs_);
      dc_ = {};
      dc_.setFs(fs_);
      y_ = dp_ = 0.0;
      cutHz_ = liftHz_ = wallHz_ = radHz0_ = -1.0;
      notePitch(hz_);
   }
   double nominal(double hz) const override { return fs_ / hz; }
   double cutoffDelay(double hz) const override {
      // (The apex's share of the holes' filter moves the pitch as well, in proportion.)
      const double cut = cutFor(hz);
      return (1.0 + apexMix_) * (latticeDelay(hz, holeCut(cut, 2.6 * hz)) - latticeDelay(hz, cut));
   }
   // Below the instrument's lowest note it plays as a bigger member of its family (a baritone or bass
   // sax, a cor anglais or a bassoon): the whole bore scaled up, so the holes' cutoff and the apex
   // follow the note down.
   double family(double hz) const { return std::clamp(hz / lowHz_, familyMin_, 1.0); }
   void notePitch(double hz) override {
      hz_ = hz;
      const double cut = holeCut(cutFor(hz), 2.6 * hz);
      if (cut != cutHz_) {
         cutHz_ = cut;
         lattice_.lowpass(cut, latticeQ_, fs_);
         apexLattice_.lowpass(cut, latticeQ_, fs_);
         // The lowest octave comes out of the bell, which lets more of the highs out than the open
         // holes do above it; a bigger family member sends more of its lows out of the bell.
         const double up = std::clamp(std::log2(hz / lowHz_), 0.0, 1.0), bell = radBell_ > 0 ? radBell_ : radHigh_;
         radMix_ = static_cast<float>((bell + (radHigh_ - bell) * up) * std::sqrt(family(hz)));
      }
      const double rad = holeCut(radHz_ * family(hz), 2.6 * hz);
      if (rad != radHz0_) {
         radHz0_ = rad;
         radiate_.lowpass(rad, radQ_, fs_);
      }
      // (A bigger family member has a bigger reed and a wider bore, which loses less on each trip.)
      const double fam = family(hz), low = (1.0 - fam) / (1.0 - familyMin_);
      reedScale_ = 1.0 / std::pow(fam, reedLow_);
      tauS_ = tau_ * fs_ / std::pow(fam, familyApex_);
      // The Loss knob: a rougher, lossier bore soaks up its highs on every trip (wall losses grow
      // with frequency) far more than its fundamental, so most of it darkens the tone through the
      // wall filters and only a tenth of it takes from the whole reflection (all of it would stop the
      // note: a reed sustains only while the bore gives back enough at the fundamental).
      const double lossK = character.keep(0.5f) / 0.5f;
      keepNow_ = static_cast<float>(std::min(0.995, (keep_ + (std::max(keep_, keepLow_) - keep_) * low) *
                                                        (1.0 - 0.1 * (1.0 - lossK))));
      if (hz != liftHz_) {
         liftHz_ = hz;
         lift_.peak(hz, liftQ_, liftDb_, fs_);
      }
      const double wallHz = wallK_ * hz * std::pow(std::min(lossK, 1.2), 5.0);
      if (wallHz != wallHz_) {
         wallHz_ = wallHz;
         wall_.setHz(wallHz, fs_);
         apexWall_.setHz(wallHz, fs_);
         // (and the darker walls' extra delay comes out of the bore, so the note stays in tune).
         wallDelay_ = 2.0 * (onePoleDelay(hz, wallHz, fs_) - onePoleDelay(hz, wallK_ * hz, fs_));
      }
   }
   float tick(float breath, float emb, double len, float tn) override {
      const float noise = noiseLP_.tick(rng.white());
      const float growl = std::max(0.0f, breath - 0.95f) * growl_ * character.edge * character.growl;
      // The lip's pressure on the reed (embouchure) narrows its opening and lowers the pressure that
      // shuts it; a stiffer reed takes more; a more open lay (aperture) passes more air.
      // (Within what a player's breath still makes up for: a reed so stiff or so pinched that the
      // note's own breath can't start it would only fall silent.)
      const double stiff = (0.92 + 0.16 * character.stiffness) * (1.1 - 0.2 * emb);
      // Mouth pressure: the breath, its turbulence and the tongue's burst as it leaves the reed. A
      // reed blown past its closing pressure would stay shut, so it eases in under it.
      double g = gamma_ / stiff * breath * (1 + (breathNoise_ * character.noise + growl) * noise) +
                 tn * tongueNoise_ * noise;
      g = g < 0.8 ? g : 0.8 + 0.15 * std::tanh((g - 0.8) / 0.15);
      // The apex's round trip; the body is the rest of the loop. The Cone knob runs from a cylinder
      // (0: the reed half way round the loop, so only the odd harmonics fit, as in a clarinet)
      // through the instrument's own truncated cone (0.5) to a sharp, nearly complete one (1: a
      // short missing apex, every harmonic and the pulse's minima pushed up).
      const double c = character.cone, tau = tauS_;
      const double la0 = c >= 0.5 ? tau * std::pow(2.0, -2.0 * (c - 0.5)) : tau + (0.5 * len - tau) * (1.0 - 2.0 * c);
      const double la = std::clamp(la0, 2.0, 0.8 * len), lb = std::max(2.0, len - la - wallDelay_);
      // The waves arriving back at the reed, each inverted by its open end: the body's through its
      // bell and holes (which keep the lows and let the highs out), the apex's with the same losses
      // in proportion.
      const float fromBody = wall_.tick(body_.read(lb));
      const float heldBody = lattice_.tick(fromBody);
      const double inBody = -keepNow_ * lift_.tick(heldBody);
      const float fromApex = apexWall_.tick(apex_.read(la));
      const double inApex = -keepNow_ * (apexMix_ * apexLattice_.tick(fromApex) + (1 - apexMix_) * fromApex);
      const double P = inBody + inApex;
      // The reed closes with the drop across it (a sample behind), meeting the lay over a curl of its
      // tip: shorter, so a harder, buzzier closure, as the Edge (bite) goes up or the reed is
      // stiffer (a soft reed rolls shut gently and sounds darker).
      y_ += (dp_ - y_) * reedCoef_;
      const double knee = 0.3 * std::exp(-1.2 * character.edge) * std::max(0.1, 2.2 - 2.4 * character.stiffness), gap = 1.0 - y_;
      const double open = gap >= knee ? gap : gap <= -knee ? 0.0 : (gap + knee) * (gap + knee) / (4 * knee);
      const double h = open * zeta_ * (0.5 + character.aperture) * (1.2 - 0.4 * emb) * reedScale_ / std::sqrt(stiff);
      // u = h sgn(D) sqrt|D - u/2|: D the drop before this sample's flow; the two tubes in parallel
      // take half the flow's pressure each.
      const double D0 = g - P;
      const double s = 0.5 * (-0.5 * h + std::sqrt(0.25 * h * h + 4.0 * std::fabs(D0)));
      const double u = (D0 >= 0 ? 1.0 : -1.0) * h * s;
      const double p = P + 0.5 * u;
      dp_ = g - p;
      // A tongue on the reed stops it, so the tube rings down quickly.
      const double stopped = 0.9 + 0.1 * std::clamp(breath * 8.0f, 0.0f, 1.0f);
      body_.push(static_cast<float>((p - inBody) * stopped));
      apex_.push(static_cast<float>((p - inApex) * stopped));
      // Heard: the wave reaching the bell and open holes, the part they let through brought up (they
      // radiate the highs better than the lows).
      return dc_.tick(fromBody - radMix_ * radiate_.tick(fromBody)) * outGain_;
   }

protected:
   // The holes' cutoff, moved up for the top notes so they still find a resonance there.
   // (Its Hole Cutoff scale as Character::cut on the Holes branch: max(base k, min(base, floor)).)
   double holeCut(double base, double floor) const { return std::max(base * character.cutoff, std::min(base, floor)); }
   double cutFor(double hz) const { return std::max(cut_ * family(hz), 2.0 * hz); }
   // The holes' phase delay at hz, in samples.
   double latticeDelay(double hz, double cut) const {
      Biquad b;
      b.lowpass(cut, latticeQ_, fs_);
      const double w = 2.0 * kPi * hz / fs_;
      const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
      const std::complex<double> H = (b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2);
      return -std::arg(H) / w;
   }

   // The apex's round trip (s), the holes' cutoff (Hz) and resonance, the walls' loss (a lowpass at
   // wallK times the note), how much less the fundamental loses than the harmonics above it (dB, Q);
   // the instrument's lowest note and how far below it the bigger family members go (the apex
   // growing as family^-familyApex); the reed's flow and the breath that shuts it; the radiation's
   // corner and resonance (the formant); the reflection per trip (keepLow for the biggest family
   // member), the apex's share of the holes' filter, how much of the highs the open holes (radHigh)
   // and, in the lowest octave, the bell (radBell) send out; the breath's noise, the tongue's and
   // the forte growl; and how much bigger the family's reeds are (family^-reedLow).
   double tau_ = 1.0e-3, cut_ = 1200.0, latticeQ_ = 0.8, wallK_ = 30.0, liftDb_ = 2.0, liftQ_ = 0.7;
   double lowHz_ = 138.6, familyMin_ = 0.25, familyApex_ = 1.0, zeta_ = 0.6, gamma_ = 0.6, radHz_ = 1200.0, radQ_ = 1.5;
   float keep_ = 0.92f, keepLow_ = 0.95f, apexMix_ = 0.5f, radHigh_ = 0.5f, radBell_ = 0.8f, outGain_ = 1.0f;
   float noiseHz_ = 3000, breathNoise_ = 0.05f, tongueNoise_ = 0.3f, growl_ = 0.6f;
   double reedCoef_ = 1.0, reedLow_ = 1.0;

private:
   Delay body_, apex_;
   Biquad lattice_, apexLattice_, lift_, radiate_;
   OnePoleLP wall_, apexWall_, noiseLP_;
   DCBlock dc_;
   double hz_ = 294.0, cutHz_ = -1.0, liftHz_ = -1.0, wallHz_ = -1.0, radHz0_ = -1.0, wallDelay_ = 0.0;
   double y_ = 0.0, dp_ = 0.0;
   float radMix_ = 0.5f;
   double reedScale_ = 1.0, tauS_ = 48.0;
   float keepNow_ = 0.92f;
};

// Alto saxophone: a single reed on a wide mouthpiece whose volume stands in for a long missing cone
// (a 1 ms round trip: the pulse's minima near 1, 2 and 3 kHz), a wide bore that loses little on each
// trip, holes that cut off near 1.2 kHz, and a bell that lets more of the highs out of the lowest
// octave than the holes do above it. Below Db3 it plays as a baritone or bass sax.
class Sax final : public ConicalReed {};

// Oboe (double reed): two cane blades beating at the top of a narrow cone. The reed and its staple
// stand in for a short missing apex (0.5 ms: the pulse's minima near 2 and 4 kHz), the narrow bore
// loses more on each trip, its holes cut off near 1.2 kHz and radiate through a sharp formant there
// (the oboe's nasal 1 to 1.5 kHz peak), and its stiffer reed takes more breath to start. Below Ab3 it plays as
// a cor anglais or a bassoon.
class Oboe final : public ConicalReed {
public:
   Oboe() {
      lowHz_ = 207.7;
      familyApex_ = 1.0;
      tau_ = 0.5e-3;
      cut_ = 1200.0;
      wallK_ = 20.0;
      liftDb_ = 2.5;
      liftQ_ = 1.0;
      keep_ = 0.88f;
      keepLow_ = 0.93f;
      zeta_ = 0.6;
      gamma_ = 0.55;
      radHz_ = 1200.0;
      radQ_ = 2.5;
      radHigh_ = 0.97f;
      radBell_ = 0.0f;
      noiseHz_ = 3500;
      breathNoise_ = 0.04f;
      tongueNoise_ = 0.25f;
      growl_ = 0.3f;
   }
};

// Trumpet: the lips are a mass on a spring, pushed open by the mouth pressure against the pressure
// in the mouthpiece (an outward-striking valve), tuned near the note; air through the slit goes as
// the square root of the pressure drop. The bore is a one-period loop whose resonances fall on
// every harmonic, as a flared horn's do, with a reflection of -1 at DC (a horn can't hold a steady
// pressure in the mouthpiece). At forte the wave steepens on its way down the tube: the brassy edge.
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
      bell_ = {};
      x_ = v_ = dcz_ = 0.0;
      shockD_ = kShock0 * fs_ / 96000.0;
      acMean_ = acEnv_ = 0.0;
      acLP_ = {};
      acLP2_ = {};
      acCoef_ = 1.0 - std::exp(-2.0 * kPi * 25.0 / fs_);
      radHP_ = {};
      radHP_.highpass(1200, 0.7, fs_);
      radLP_ = {};
      radLP_.lowpass(9000, 0.7, fs_);
      notePitch(lipHz_);
   }
   double nominal(double hz) const override { return fs_ / hz; }
   double cutoffDelay(double hz) const override {
      return onePoleDelay(hz, hz * 8.0 * character.cutoff, fs_) - onePoleDelay(hz, hz * 8.0, fs_);
   }
   void notePitch(double hz) override {
      lipHz_ = hz;
      acLP_.setHz(hz * 1.2, fs_);
      acLP2_.setHz(hz * 1.2, fs_);
      bell_.setHz(std::min(hz * 8.0 * character.cutoff, 0.45 * fs_), fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      const double dt = 1.0 / fs_, w = 2 * kPi * lipHz_ * (0.9 + 0.08 * (emb - 0.5));
      const double pm = breath * (1 + 0.02 * tn * rng.white());
      const double xin = bore_.read(len);
      dcz_ += (xin - dcz_) * (2 * kPi * 20 / fs_);
      const double pin = character.keep(0.985f) * (bell_.tick(static_cast<float>(xin)) - 2.0 * dcz_);
      // Lips: pushed open by mouth pressure minus mouthpiece pressure (before this sample's flow).
      const double a = -w * w * x_ - (w / (2.0 + 4.0 * character.stiffness)) * v_ + 0.25 * w * w * (pm - 2 * pin) + 1e-4 * rng.white();
      v_ += dt * a;
      x_ += dt * v_;
      const double h = std::max(x_, 0.0) * (0.5 + character.aperture);
      // The flow through the slit, solved in closed form with the pressure it makes:
      // u = h sgn(D) sqrt|D - u|, D = pm - 2 pin.
      const double D = pm - 2 * pin;
      const double sq = 0.5 * (-h + std::sqrt(h * h + 4 * std::fabs(D)));
      const double u = (D >= 0 ? 1 : -1) * h * sq;
      const double pplus = pin + u;
      bore_.push(static_cast<float>(pplus));
      // Louder pressure travels faster down the horn, so the outgoing wave steepens towards a shock:
      // a read delay that follows the pressure. How far it steepens (sigma, the delay's steepest
      // slope: 1 is a shock) grows with the breath, so a trumpet only blares when it's pushed; the
      // pressure's own level is divided out, so every note and setting steepens alike.
      shock_.push(static_cast<float>(pplus));
      const double k = fs_ / 96000.0;
      acMean_ += (pplus - acMean_) * acCoef_;
      // Driven by the wave's fundamental, so the read never runs faster than the wave allows.
      const double ac = acLP2_.tick(acLP_.tick(static_cast<float>(pplus - acMean_)));
      acEnv_ += (std::fabs(ac) - acEnv_) * acCoef_;
      const double push = std::clamp((breath - 0.2) / 0.48, 0.0, 1.5);
      const double sigma = (0.3 + 1.4 * push * push) * character.edge * (character.brass / 0.5);
      const double K = sigma * fs_ / (2.0 * kPi * lipHz_ * (1.5708 * acEnv_ + 1e-4));
      const double want = k * kShock0 - K * ac;
      shockD_ += std::clamp(std::clamp(want, 2.0, 2.0 * kShock0 * k) - shockD_, -0.95, 0.95);
      // The bell passes highs and reflects lows.
      return radLP_.tick(radHP_.tick(shock_.read(shockD_))) * 3.0f;
   }

private:
   Delay bore_, shock_;
   OnePoleLP bell_, acLP_, acLP2_;
   Biquad radHP_, radLP_;
   static constexpr double kShock0 = 480.0;
   double acMean_ = 0, acEnv_ = 0, acCoef_ = 0.001;
   double x_ = 0, v_ = 0, lipHz_ = 440, dcz_ = 0, shockD_ = 120;
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
      {"Sax", 0.6f, 0.5f, 0.85f},
      {"Oboe", 0.5f, 0.6f, 0.9f},
      {"Trumpet", 0.48f, 0.2f, 0.65f},
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
   static const Point trumpet[] = {{0.24f, -12}, {0.30f, -9}, {0.34f, -8}, {0.42f, -5}, {0.49f, -3},
                                   {0.58f, -1}, {0.68f, 1}};
   const Point *p = nullptr;
   int n = 0;
   switch (model) {
   case kFlute: p = flute; n = 9; break;
   case kClarinet: p = clarinet; n = 5; break;
   case kTrumpet: p = trumpet; n = 7; break;
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
