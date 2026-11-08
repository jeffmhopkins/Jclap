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
// pipe is open at both ends. Turbulence at the edge is the breath noise, riding on the jet.
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
      envF_ = envS_ = rawPrev_ = 0.0f;
      tau_ = 0.0;
      nhp_ = {};
      nhp_.setHz(500, fs_);
      olp_.clear();
      olp2_.clear();
      notePitch(hz_);
   }
   double nominal(double hz) const override { return fs_ / hz; }
   double cutoffDelay(double hz) const override {
      return onePoleDelay(hz, 2.4 * hz * character.cutoff, fs_) - onePoleDelay(hz, 2.4 * hz, fs_);
   }
   void notePitch(double hz) override {
      hz_ = hz;
      // The pipe's losses: its open holes let the upper harmonics out of the loop.
      lp_.setHz(2.4 * hz * character.cutoff, fs_);
      // How quickly the jet answers the sound: a wider jet (Aperture) follows higher harmonics.
      jlp_.setHz(hz * (1.4 + 1.2 * character.aperture), fs_);
      // High notes are played with a short, fast jet over a small opening: it swings less across
      // the edge for the same breath, so the third octave stays nearly pure even at forte.
      reg_ = static_cast<float>(std::pow(std::min(1.0, 300.0 / hz), 1.2));
      // Radiation from the embouchure hole and the open holes: a monopole, so the sound outside is
      // the jet's flow differentiated (6 dB an octave), from about 100 Hz up.
      rz_ = static_cast<float>(std::exp(-2.0 * kPi * 100.0 / fs_));
      // From about 2.5 kHz up the tone holes' lattice and the head joint stop supporting the sound.
      olp_.lowpass(2500.0, 0.7071, fs_);
      olp2_.lowpass(6000.0, 0.7071, fs_);
      // Edge turbulence: a broad band from about 500 Hz to 3 kHz.
      nhp_.setHz(500.0, fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      const float U = std::sqrt(std::max(breath, 0.0f));
      const float raw = bore_.read(len);
      // The Loss knob: more of each trip soaked up by the walls: darker, and it needs more breath.
      const float lossK = std::clamp(1.0f - 0.1f * (character.loss - 0.04f), 0.9f, 1.02f);
      const float p = std::min(0.995f, 0.97f * lossK) * lp_.tick(raw);
      jet_.push(p);
      // The jet's length over its speed: Embouchure rolls the head joint (a longer or shorter jet).
      const double tauT = std::clamp((0.33 + 0.12 * (emb - 0.5)) * len / std::max(U, 0.08f), 2.0, jet_.size() - 8.0);
      // The jet's transit time can't jump faster than the air in it moves: a quick release would
      // otherwise sweep the read point and chirp.
      tau_ = tau_ <= 0.0 ? tauT : tau_ + std::clamp(tauT - tau_, -2.0, 0.5);
      const double tau = tau_;
      const float turb = nlp_.tick(rng.white());
      // The jet's own turbulence goes with the jet: none once the breath stops.
      const float y = jlp_.tick(jet_.read(tau)) + (0.012f * character.noise * std::min(1.0f, 1.5f * U) + 0.12f * tn) * turb;
      // The jet's flow grows with pressure, not just its speed; Stiffness also firms the jet's gain.
      const float q = 1.2f * U * U * std::tanh(8.0f * (0.5f + character.stiffness) * y + 0.3f);
      bore_.push(dc_.tick(p - 0.5f * q));
      // What we hear is the jet's flow into and out of the embouchure hole: the jet swung by the
      // sound (y), split by the edge. Blown softly it swings only a little across the edge and its
      // flow follows the sound, nearly pure; blown harder its swing is many times its width and the
      // flow saturates both ways, all harmonics up. Where it's aimed (the player's embouchure) sets
      // the evens: on the edge, a symmetric square (odd harmonics); off it, a pulse whose 2nd and 4th
      // reach the fundamental's level at forte, as a low flute's do. The swing is measured against
      // the jet's own level, so the colour follows the breath, not the note's loudness.
      envF_ += 0.002f * (96000.0f / static_cast<float>(fs_)) * (std::fabs(y) - envF_);
      const float a = 1.5708f * envF_ + 1e-4f, n = std::clamp(y / a, -1.5f, 1.5f);
      // While the note is still growing, the swing is small against what it will be (the level above
      // lags it): a slower follower holds the drive back until the tone settles, so the onset comes
      // in darker than the held note, as a real flute's does.
      envS_ += 0.0005f * (96000.0f / static_cast<float>(fs_)) * (std::fabs(y) - envS_);
      // (Within 2 dB counts as settled, so a vibrato's swell doesn't darken a held note.)
      const float settle = std::min(1.0f, 1.25f * (envS_ + 1e-6f) / (envF_ + 1e-6f));
      const float grow = settle * settle;
      const float push = std::clamp((breath - 0.2f) / 0.7f, 0.0f, 1.4f);
      // Bite is how hard the jet is driven across the edge. A wider jet (Aperture) or a looser lip
      // (Stiffness down) swings less of its width, and a lossier pipe (Loss) has less sound at the
      // embouchure to swing it: purer and darker. (At the defaults every factor is 1.)
      const float D = std::max(1e-3f, 8.0f * character.edge * (1.6f - 1.2f * character.aperture) *
                                          (0.6f + 0.8f * character.stiffness) /
                                          (1.0f + 4.0f * std::max(0.0f, character.loss - 0.04f)) * push * push * push *
                                          std::sqrt(push) * reg_);
      const float off = 1.2f * (0.4f + 1.2f * emb), s0 = std::tanh(off), d0 = 1.0f - s0 * s0;
      // (Saturated, it still carries more flow at the fundamental, so forte keeps growing a little.)
      const float Dg = std::max(1e-3f, D * grow);
      const float J = a * (std::tanh(Dg * n + off) - s0) / (Dg * d0) * std::sqrt(std::max(1.0f, 0.8f * D));
      const float rad = J - rz_ * rawPrev_;
      rawPrev_ = J;
      // The edge's turbulence, band-limited and swelling as the jet sweeps across the edge: more of
      // it from a wide jet or a loose lip.
      const float hiss = turb - nhp_.tick(turb);
      const float airy = (0.5f + character.aperture) * (1.6f - 1.2f * character.stiffness);
      return dco_.tick(olp2_.tick(olp_.tick(3.0f * rad)) + 0.04f * character.noise * airy * U * hiss * (0.3f + std::fabs(n)));
   }

private:
   Delay bore_, jet_;
   OnePoleLP lp_, jlp_, nlp_, nhp_;
   Biquad olp_, olp2_;
   DCBlock dc_, dco_;
   double hz_ = 440.0, tau_ = 0.0;
   float rawPrev_ = 0.0f, envF_ = 0.0f, envS_ = 0.0f, reg_ = 1.0f, rz_ = 0.0f;
};

// Clarinet: a single reed is a pressure-controlled valve on a cylinder closed at the reed. The reed
// is the Kergomard / Wilson-Beavers valve, u = zeta (1 - gamma + p) sgn(gamma - p) sqrt|gamma - p|,
// shut when the pressure drop closes it, solved each sample together with the pressure it makes.
// The bell and tone holes reflect lows and let highs out (a 2-pole lowpass in the loop).
//
// What we hear is radiated by the open tone holes. Below the lattice's cutoff they radiate the
// standing wave, whose pressure at the reed has almost no even harmonics (they fall on the bore's
// impedance minima): the sound outside is that pressure tilted up 6 dB an octave (Benade's spectrum
// transformation). Above the cutoff the wave runs past the holes and out, and every harmonic leaves.
// So the low register is odd harmonics with a deep 2nd, and the clarion, whose 2nd harmonic lies
// near the cutoff, has its even harmonics back.
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
      hissLP_ = {};
      hissLP_.setHz(400, fs_);
      outLP_ = {};
      outLP_.setHz(3000.0, fs_);
      outLP2_ = {};
      outLP2_.setHz(3000.0, fs_);
      thru_.clear();
      tx_ = 0.0f;
   }
   double nominal(double hz) const override { return fs_ / (2.0 * hz); }
   double cutoffDelay(double hz) const override {
      const double cut = std::max(1300.0, 2.2 * hz);
      return 2.0 * (onePoleDelay(hz, holeCut(hz), fs_) - onePoleDelay(hz, cut, fs_));
   }
   // The Hole Cutoff knob moves the lattice's cutoff, but never below twice the note: a fingering
   // whose holes cut off under its own fundamental can't hold the note at all.
   double holeCut(double hz) const {
      return std::max(std::max(1300.0, 2.2 * hz) * character.cutoff, 2.0 * hz);
   }
   // The bore's loss sits near 1.3 kHz, which only lets the low and middle register speak; it moves
   // up with notes above that, so they play too (as a real clarinet's tone holes leave more bore).
   void notePitch(double hz) override {
      hz_ = hz;
      // High notes need a firmer push to speak (a player bites and blows harder up there).
      lift_ = 1.0 + 0.2 * std::clamp(std::log2(hz / 440.0) / 0.8, 0.0, 1.0);
      const double cut = holeCut(hz);
      lp_.setHz(cut, fs_);
      lp2_.setHz(cut, fs_);
      thru_.highpass(cut, 0.7071, fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      const float white = rng.white();
      const float noise = noiseLP_.tick(white);
      const float growl = std::max(0.0f, breath - 0.9f) * 0.1f * character.edge * character.growl;
      // Bite: the lip pressing the reed nearer the lay, so less breath closes it (a higher gamma for
      // the same breath) through a narrower slit (a lower zeta): buzzier and brighter. Eased off, the
      // lip cushions the reed, which closes softly onto the lay: round and dark. A softer reed bends
      // with less breath (bright, buzzy); a harder one opens less under the same flow (a lower
      // zeta: darker, and it needs more breath).
      const float e = character.edge - 1.0f, st = character.stiffness - 0.5f;
      const double bite = 1.0 + 0.2 * std::max(0.0f, e);
      const double soft = 1.0 + 0.24 * std::max(0.0f, -st);
      const double g = 0.7 * lift_ * breath * bite * soft * (1 + (0.012f * character.noise + growl) * noise) +
                       tn * 0.04f * noise;
      const double z = 0.35 * (0.8 + 0.4 * emb) * (0.6 + 0.8 * character.aperture) *
                       (1.0 - 0.12 * std::max(0.0f, e) + 0.1 * std::min(0.0f, e)) * (1.0 - 0.4 * std::max(0.0f, st));
      // A stiffer reed closes over a shorter curl of its tip; a cushioning lip lengthens it.
      const double knee = 0.04 + 0.16 * (1.0 - character.stiffness) + 0.25 * std::max(0.0f, -e);
      const double inc = bore_.read(len);
      // The Loss knob: more of each trip soaked up by the walls, so the tone is darker and needs more
      // breath (the lightest touch may no longer speak near the knob's end).
      const float lossK = std::clamp(1.0f - 0.1f * (character.loss - 0.04f), 0.9f, 1.02f);
      const double back = -std::min(0.995f, 0.96f * lossK) * lp2_.tick(lp_.tick(static_cast<float>(inc)));
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
      const double u = 0.5 * (lo + hi);
      // Breath noise: the air rushing through the reed's slit is turbulent, and its hiss (a band
      // above a few hundred Hz) rides on the flow into the bore, coloured by it on the way out.
      const float hiss = (white - hissLP_.tick(white)) * 0.6f;
      const double turb = hiss * character.noise * (0.5f + 0.5f * character.noise) * std::sqrt(std::max(0.0, u)) * 0.1;
      bore_.push(static_cast<float>(back + u + turb));
      // Radiated: the reed's pressure (the standing wave the holes radiate below the cutoff), less
      // the wave that runs past the holes above it, and differentiated (the holes are monopoles).
      const float t = outLP2_.tick(outLP_.tick(static_cast<float>(2.0 * back + u) - thru_.tick(static_cast<float>(inc))));
      const float o = (t - tx_) * static_cast<float>(fs_ / 96000.0);
      tx_ = t;
      return dc_.tick(10.0f * o);
   }

private:
   Delay bore_;
   OnePoleLP lp_, lp2_, noiseLP_, hissLP_, outLP_, outLP2_;
   Biquad thru_;
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
      return onePoleDelay(hz, cut_ * character.cutoff, fs_) - onePoleDelay(hz, cut_, fs_);
   }
   // A split bore lets the second mode compete with the first, and on its own the loop jumps to the
   // octave on most notes. A gentle lift at the fundamental (a cone's wider first resonance, from
   // its missing apex) keeps it in the low register; on release the loop still dies away.
   void notePitch(double hz) override {
      if (character.cutoff != cutK_) {
         cutK_ = character.cutoff;
         lp_.setHz(std::min(cut_ * cutK_, 0.45f * static_cast<float>(fs_)), fs_);
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
      // Where the reed sits along the split bore: the cone's truncation, moved by the Cone knob.
      const double pos = posBase_ + posEmb_ * (emb - 0.5) + 0.16 * (character.cone - 0.5);
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
      {"Sax", 0.65f, 0.2f, 0.6875f},
      {"Oboe", 0.42f, 0.48f, 0.795f},
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
