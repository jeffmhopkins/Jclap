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
#include <atomic>
#include <cmath>
#include <complex>
#include <string_view>
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
   // A bandpass whose peak is 0 dB, at hz with bandwidth hz / q.
   void bandpass(double hz, double q, double fs) {
      const double w = 2 * kPi * hz / fs, al = std::sin(w) / (2 * q), a0 = 1 + al;
      b0 = al / a0;
      b1 = 0.0;
      b2 = -al / a0;
      a1 = -2 * std::cos(w) / a0;
      a2 = (1 - al) / a0;
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
   // The Alien's throat: where on its ring of shapes a note starts (0..1), how fast it travels and
   // how far it wanders (0..1), its shift in octaves (-1..1) and how sharp its resonances are (0..1).
   float vowel = 0.0f, morph = 0.3f, drift = 0.3f, shift = 0.0f, sharp = 0.5f, bow = 0.15f, talk = 0.5f, paceSec = 0.0f, linger = 0.3f, glideMs = 30.0f;
   // (The Alien: how many semitones the note is bent that its throat follows.)
   float bendSemis = 0.0f;
   // (The Alien's consonants: how strong, and which of its eight families it says, a bit each.)
   float consonants = 1.0f;
   uint32_t sounds = 0xffu;
   // (Its vowels: which of A E I O U it says, a bit each, and how a note picks its first: 0 at
   // random, 1 in turn, 2 by pitch, 3 always the same.)
   uint32_t vowels = 0x1fu;
   int order = 0;

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

// What a player's breath and lips do to a held note that no bore does: the pitch never holds dead
// still (a real sax or trumpet wanders by 1 to 3 cents, slowly, measured on the University of Iowa
// recordings; a tone with none sounds synthetic, Dubnov et al. 1996, Barthet et al. 2005), and a
// note starts a little under its pitch and rises into it (a trumpet by 40 to 130 cents at its
// onset, settling in about 50 ms; a sax by 10 to 20 at forte). Both as cents, per sample.
struct Player {
   // Slow wander: three bands of filtered noise (0.15, 0.5 and 1.5 Hz), 1.0, 0.9 and 0.5 cents
   // each. (Faster than a couple of Hz it would blur the upper harmonics, which a player's doesn't.)
   void prepare(double fs) {
      static constexpr double hz[3] = {0.15, 0.5, 1.5}, cents[3] = {1.0, 0.9, 0.5};
      for (int i = 0; i < 3; ++i) {
         a[i] = static_cast<float>(std::exp(-2.0 * 3.14159265358979 * hz[i] / fs));
         // A one-pole's output of uniform noise (variance 1/3) has variance (1 - a) / (1 + a) / 3.
         k[i] = static_cast<float>(cents[i] / std::sqrt((1.0 - a[i]) / (1.0 + a[i]) / 3.0));
      }
      hold = 0.008 * fs;
      fs_ = fs;
   }
   void reset() {
      z[0] = z[1] = z[2] = 0.0f;
      age = since = 0.0;
      tongue = 0.0f;
   }
   // A new tongued attack (the tongue's burst rising from nothing, a fifth of a second or more
   // after the last) starts the scoop again; a flutter tongue's quick strokes don't.
   void tongued(float tn) {
      if (tn > 0.15f && tongue < 0.02f && since > 0.2 * fs_)
         age = since = 0.0;
      since += 1.0;
      tongue = tn;
   }
   // The wander in cents, and (0..1) how much of the onset's scoop is left.
   float wander(Noise &rng) {
      float c = 0.0f;
      for (int i = 0; i < 3; ++i) {
         z[i] = (1.0f - a[i]) * rng.white() + a[i] * z[i];
         c += k[i] * z[i];
      }
      return c;
   }
   // (rate: how fast its clock runs, so a note that speaks late still starts under its pitch.)
   double scoop(double tauSec, double rate = 1.0) {
      const double t = age - hold;
      age += rate;
      return t <= 0.0 ? 1.0 : std::exp(-t / (tauSec * fs_));
   }
   float a[3] = {}, k[3] = {}, z[3] = {}, tongue = 0.0f;
   double age = 0.0, since = 0.0, hold = 700.0, fs_ = 96000.0;
};

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
   // The Alien's throat's place on its ring of shapes (0..8), for the panel; -1 for the others.
   virtual float throat() const { return -1.0f; }
   // A new note, or a slurred one: the Alien starts a syllable.
   virtual void articulate(uint32_t word, double hardness) {
      (void)word;
      (void)hardness;
   }

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
   // The holes', tube's and taper's cutoff, its delay taken out (the jet's delay follows the bore,
   // so it shows only part of the filter's), and the loss's pitch.
   double cutoffDelay(double hz) const override {
      return onePoleDelay(hz, loopCut(hz), fs_) - onePoleDelay(hz, 2.4 * hz, fs_);
   }
   void notePitch(double hz) override {
      hz_ = hz;
      // The pipe's losses: its open holes let the upper harmonics out of the loop.
      lp_.setHz(loopCut(hz), fs_);
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
      // (A soft jet is smooth, laminar: its turbulence grows faster than its speed.)
      const float laminar = std::pow(std::min(1.0f, breath / 0.6f), 1.5f);
      const float y = jlp_.tick(jet_.read(tau)) + (0.012f * character.noise * std::min(1.0f, 1.5f * U) * laminar + 0.12f * tn) * turb;
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
      // (It grows faster than the tone with the breath: a soft note is nearly pure, a hard-blown one
      // rushes, as on a real flute.)
      // (and high notes, blown through a smaller, faster jet, are breathier than low ones).
      const float rush = U * std::min(3.0f, std::pow(breath / 0.67f, 1.5f)) * std::clamp(static_cast<float>(hz_) / 400.0f, 1.0f, 4.0f);
      return dco_.tick(olp2_.tick(olp_.tick(3.0f * rad)) + 0.04f * character.noise * airy * rush * hiss * (0.3f + std::fabs(n)));
   }

private:
   double loopCut(double hz) const { return character.cut(hz * 2.4, 1.5 * hz); }
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
      hissTop_ = {};
      hissTop_.setHz(kHissTop, fs_);
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
      // (Turbulence in a slit that narrow is strongest around a couple of kHz and falls away above:
      // the bell's radiation lifts the top of whatever comes out, so it goes in falling.)
      const float hiss = hissTop_.tick(white - hissLP_.tick(white)) * kHissGain;
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
   // How much of the filters' delay shows in the pitch, and the loss's pitch in cents (measured).
   static constexpr double kCutPitch = 1.0, kLossA = -13.0, kLossB = 1.8;
   Delay bore_;
   OnePoleLP lp_, lp2_, noiseLP_, hissLP_, hissTop_, outLP_, outLP2_;
   static constexpr float kHissTop = 2000.0f, kHissGain = 0.9f;
   Biquad thru_;
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
      player_.prepare(fs);
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
      player_.reset();
      tone_ = {};
      toneAt_ = 0;
      toneDyn_ = 0.0f;
      formant_.clear();
      lowCut_.clear();
      buzzLo_.clear();
      buzzHi_.clear();
      buzzMid_.clear();
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
      // The apex's round trip grows with the family member, and on the lowest notes it is never
      // shorter than apexTurns of the period, as if the family scaling began there. (A resonance
      // of the two tubes takes the reed in proportion to sin^2(pi f tau). With a short apex under a
      // long body the fundamental hardly couples while the third resonance couples five times as
      // well, so a forte start first settles into the twelfth's two-step motion, the reed shutting
      // three times a period, and the fundamental then grows through it at its bore's slow rate: a
      // quick start, then a 6 dB swell over a quarter of a second.)
      tauS_ = std::max(tau_ / std::pow(fam, familyApex_), apexTurns_ / hz) * fs_;
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
         // The low register (Sax only): a real sax's lowest notes have a weak fundamental (the bell
         // radiates little of it) under a strong band of harmonics near 650 Hz, its formant, and a
         // plateau of harmonics on to 3 kHz: the honk (UNSW tenor, low Bb to G: H1 12 to 15 dB under
         // the 500 to 700 Hz harmonics at every dynamic; Iowa alto from Db3: H1 to H5 level). It
         // fades in from 300 Hz down, all of it by 150 Hz; the middle register keeps its round
         // fundamental.
         lowReg_ = static_cast<float>(toneDepth_ > 0.0 ? std::clamp((300.0 - hz) / 150.0, 0.0, 1.0) : 0.0);
         formant_.peak(650.0, 1.1, 9.0 * lowReg_, fs_);
         buzzLo_.highshelf(std::min(2000.0, 0.4 * fs_), toneDepth_ > 0.0 ? buzzDb_ : 0.0, fs_);
         buzzHi_.highshelf(std::min(5000.0, 0.4 * fs_), toneDepth_ > 0.0 ? buzzDb_ + 6.0 * lowReg_ : 0.0, fs_);
         lowCut_.highpass(std::min(190.0, 0.4 * fs_), 0.6, fs_);
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
      // How hard it's blown, 0 where the reed starts to 1 at full breath (past it at more): what the
      // player's dynamics move.
      const float dyn = std::clamp((breath - dynLo_) / (dynHi_ - dynLo_), 0.0f, 1.3f);
      if (scoopCents_ > 0.0) {
         // The player's slow wander, and the start of the note from under its pitch (more at forte).
         player_.tongued(tn);
         const double cents = player_.wander(rng) - scoopCents_ * (0.4 + 0.6 * dyn) * player_.scoop(0.025);
         len *= std::exp2(-cents / 1200.0);
      }
      const float noise = noiseLP_.tick(rng.white());
      // (With the Growl knob at nothing the reed's own rasp is slight: a held forte note is a clean
      // tone, with the rasp coming in only as the knob goes up.)
      const float growl = std::max(0.0f, breath - 0.95f) * growl_ * character.edge * (character.growl - 0.6f);
      // The lip's pressure on the reed (embouchure) narrows its opening and lowers the pressure that
      // shuts it; a stiffer reed takes more; a more open lay (aperture) passes more air.
      // (Within what a player's breath still makes up for: a reed so stiff or so pinched that the
      // note's own breath can't start it would only fall silent.)
      const double stiff = (0.92 + 0.16 * character.stiffness) * (1.1 - 0.2 * emb);
      // Mouth pressure: the breath, its turbulence and the tongue's burst as it leaves the reed. A
      // reed blown past its closing pressure would stay shut, so it eases in under it.
      // (Its turbulence is a smaller share of a hard-blown note: the reed slams shut each cycle and
      // the tone grows faster than the rush through the slit.)
      const float share = breath <= 0.88f ? 1.0f : std::pow(0.88f / breath, 3.3f);
      double g = gamma_ / stiff * breath * (1 + (breathNoise_ * share * character.noise + growl) * noise) +
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
      // (The lift is the reed's own pull on its fundamental, so it fades with the breath: a bore with
      // nothing blowing it only rings down.)
      const double liftMix = std::clamp(breath * 20.0f, 0.0f, 1.0f);
      const double lifted = lift_.tick(heldBody);
      const double inBody = -keepNow_ * (heldBody + liftMix * (lifted - heldBody));
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
      const float out = dc_.tick(fromBody - radMix_ * radiate_.tick(fromBody)) * outGain_;
      if (toneDepth_ <= 0.0)
         return out;
      // A reed blown softly hardly beats: its tone is nearly a sine with a little of the 2nd
      // harmonic, and it fills in up to forte (Iowa alto: at pp H2 -16 to -31 dB and H3 under -27;
      // at mf a steep fall to H3 and then a plateau, H8 to H20 at -40 to -60; at ff H2 -6 and H3 to
      // H8 -7 to -28). The bore here makes almost the same spectrum at every breath, so the
      // radiated tone is shaped by the dynamic: everything over a lowpass near the note's lowest
      // harmonics is shelved down, by toneDepth dB at the softest and none from forte up, and the
      // level swings by rangeDb more over the dynamics, as a real one's does (about 15 dB, pp to ff).
      if ((toneAt_++ & 15) == 0) {
         // (It follows the breath over 60 ms, as the reed's swing does, so a tongue's
         // quick burst on a soft note is an accent, not a forte.)
         toneDyn_ += (dyn - toneDyn_) * static_cast<float>(1.0 - std::exp(-16.0 / (0.06 * fs_)));
         const float dyn = toneDyn_;
         const double fc = std::min(hz_ * std::exp2(toneLow_ + 1.6 * dyn), 0.45 * fs_);
         tone_.lowpass(fc, 0.7, fs_);
         const float soft = std::max(0.0f, 1.0f - dyn / 0.85f);
         // (and pushed past forte the reed's buzz comes up over what the bore gives: the Iowa alto's
         // H6 to H12 at ff stand at -20 to -31 dB).
         const float push = std::clamp((dyn - 0.85f) / 0.25f, 0.0f, 1.0f);
         // (The low register darkens far less when it's played softly: the UNSW tenor's low Bb,
         // very soft, still has H1 to H4 level and H9 to H20 at -25 to -45 dB.)
         const double depth = toneDepth_ * (1.0 - 0.7 * lowReg_);
         toneShelf_ = static_cast<float>(std::pow(10.0, (8.0 * push * (1.0 - lowReg_) - depth * std::pow(soft, 1.3f)) / 20.0));
         // (The low register's own swing is wider already: its harmonics grow faster than its weak
         // fundamental, so it takes less of the extra.)
         toneGain_ = static_cast<float>(std::pow(10.0, rangeDb_ * (1.0 - 0.75 * lowReg_) * (dyn - 0.6) / 20.0));
         buzzMid_.peak(std::min(2800.0, 0.4 * fs_), 0.9, 8.0 * (1.0 - std::min(1.0f, dyn * dyn)), fs_);
      }
      const float low = tone_.tick(out);
      float y = low + toneShelf_ * (out - low);
      if (lowReg_ > 0.0f) {
         const float cut = lowCut_.tick(y);
         y = formant_.tick(y + lowReg_ * (cut - y));
      }
      // The buzz: a real sax's spectrum falls only about 6 dB an octave over 2 kHz, at every
      // dynamic (Iowa alto, C4 to G4: the third-octaves from 2 to 8 kHz at -20 to -44 dB of the
      // whole at mf, still -43 to -55 at pp), where the bore's and bell's losses here took 12 dB
      // an octave or more: 10 to 30 dB too little, a muffled tone. Two shelves give it back (the
      // upper one more on the low register, whose highs the UNSW tenor holds out to 8 kHz).
      // (and the sax's upper formant near 2.8 kHz stands out the more the softer it's played: at
      // forte the reed's own buzz fills that octave anyway.)
      y = buzzMid_.tick(buzzHi_.tick(buzzLo_.tick(y)));
      return y * toneGain_;
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
   // the forte growl; how much bigger the family's reeds are (family^-reedLow); and the shortest
   // apex round trip on the lowest notes, as a share of the period (apexTurns; 0 for none).
   double tau_ = 1.0e-3, cut_ = 1200.0, latticeQ_ = 0.8, wallK_ = 30.0, liftDb_ = 2.0, liftQ_ = 0.7;
   double lowHz_ = 138.6, familyMin_ = 0.25, familyApex_ = 1.0, zeta_ = 0.6, gamma_ = 0.6, radHz_ = 1200.0, radQ_ = 1.5;
   float keep_ = 0.92f, keepLow_ = 0.95f, apexMix_ = 0.5f, radHigh_ = 0.5f, radBell_ = 0.8f, outGain_ = 1.0f;
   float noiseHz_ = 3000, breathNoise_ = 0.05f, tongueNoise_ = 0.3f, growl_ = 0.6f;
   double reedCoef_ = 1.0, reedLow_ = 1.0, apexTurns_ = 0.0;
   // The player (Sax only; 0 leaves the model as it was): the breath where the reed starts and
   // full breath, for the dynamic (dynLo, dynHi); the tone's lowpass at the softest (octaves over
   // the note) and how far the highs over it are shelved down there (toneDepth dB: 0 for none); how many dB
   // louder full breath is than soft over what the bore gives (rangeDb); and how far under its
   // pitch a note starts (scoopCents; 0 for no scoop and no wander).
   float dynLo_ = 0.5f, dynHi_ = 1.1f;
   double toneLow_ = 0.0, toneDepth_ = 0.0, rangeDb_ = 0.0, scoopCents_ = 0.0, buzzDb_ = 8.0;

private:
   Delay body_, apex_;
   Biquad lattice_, apexLattice_, lift_, radiate_, tone_, formant_, lowCut_, buzzLo_, buzzHi_, buzzMid_;
   Player player_;
   unsigned toneAt_ = 0;
   float toneGain_ = 1.0f, toneShelf_ = 1.0f, toneDyn_ = 0.0f, lowReg_ = 0.0f;
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
// octave than the holes do above it. Below Db3 it plays as a baritone or bass sax. From C#4 down its
// apex keeps 0.28 of the period, so the bottom notes start on their fundamental.
class Sax final : public ConicalReed {
public:
   Sax() {
      apexTurns_ = 0.28;
      toneLow_ = 0.4;
      toneDepth_ = 30.0;
      rangeDb_ = 4.0;
      scoopCents_ = 45.0;
   }
};

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
      player_.prepare(fs);
      reset();
   }
   void reset() override {
      bore_.clear();
      shock_.clear();
      bell1_ = {};
      bell2_ = {};
      x_ = v_ = u_ = dcz_ = closed_ = 0.0;
      tongue_ = 0.0f;
      player_.reset();
      tone_ = {};
      toneAt_ = 0;
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
      breathLP_ = {};
      breathLP_.setHz(1500.0, fs_);
      breathLP2_ = {};
      breathLP2_.setHz(1500.0, fs_);
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
      trim_ = std::exp2((trim(character.aperture, kTrimAp0, kTrimAp1) + trim(lipStiffness(), kTrimSt0, kTrimSt1) +
                         150.0 * (character.loss - 0.04f)) /
                        1200.0);
   }
   float tick(float breath, float emb, double len, float tn) override {
      // Over the middle the embouchure tightens the lips by less: pinched any tighter, they'd sit so
      // far over a high note that it stops speaking (a real player's lips crack up a partial instead).
      if (emb > 0.5f)
         emb = 0.5f + 0.4f * (emb - 0.5f);
      // How hard it's blown, 0 where the lips start to 1 at full breath.
      const double dyn = std::clamp((breath - 0.15) / 0.4, 0.0, 1.3);
      // The player: a slow wander, and every tongued note starting under its pitch and rising into
      // it as the lips tighten onto the horn's resonance (Iowa trumpet: 40 to 130 cents flat at the
      // onset, settling in 50 to 80 ms; more and slower on soft and low notes, quick at forte, and
      // little from C5 up). Its clock runs slowly until the lips close on every cycle, so a soft
      // note that speaks late still rises into its pitch. The bore's length is what bends: the lips
      // follow it (bent too, they'd settle a forte into a darker regime and stay there).
      player_.tongued(tn);
      const double dynTop = std::min(dyn, 1.0);
      const double depth = 330.0 * std::clamp(1.2 - (lipHz_ - 260.0) / 300.0, 0.1, 1.2) * (1.3 - 0.75 * dynTop);
      const double cents = player_.wander(rng) - depth * player_.scoop(0.015 + 0.03 * (1.0 - dynTop),
                                                                      0.25 + 0.75 * std::min(1.0, closed_ / 0.3));
      const double bend = std::exp2(cents / 1200.0);
      // The lips' own frequency: under the note, tighter with the embouchure.
      const double dt = 1.0 / fs_, w = 2 * kPi * lipHz_ * (0.9 + 0.12 * (emb - 0.5));
      // (The breath's turbulence in the mouthpiece cup is a rush under a couple of kHz: the lips and
      // the bell would make a hiss of anything above it, which a trumpet hardly has. Loose, slow lips
      // under a low note pass it on as a roar, so they take less of it.)
      const double pm = breath * (1 + 0.02 * tn * rng.white()) * (1 + 0.05 * std::min(1.0, lipHz_ / 233.0) * character.noise * breathLP2_.tick(breathLP_.tick(rng.white())));
      // While the lips don't yet close on each cycle (the first few ms of a note), they pull the
      // pitch sharp: a real trumpet's long bore holds it, this one-period loop is lengthened instead.
      const double pull = 0.06 * (0.33 + breath) * std::max(0.0, 1.0 - closed_ / 0.3);
      if (emb != emb_) {
         emb_ = emb;
         embTrim_ = std::exp2(trim(emb, kTrimEm0, kTrimEm1) / 1200.0);
      }
      const double xin = bore_.read((len / bend * trim_ * embTrim_ - coneDelay_) * (1.0 + pull));
      dcz_ += (xin - dcz_) * (2 * kPi * 20 / fs_);
      const double pin = character.keep(0.95f) * (bell2_.tick(bell1_.tick(static_cast<float>(xin))) - 2.0 * dcz_);
      // Lips: pushed open by mouth pressure minus mouthpiece pressure (with the last sample's flow).
      const double a = -w * w * x_ - (w / (4.0 * std::exp2(lipStiffness() - 0.5))) * v_ + 0.7 * w * w * (pm - 2 * pin - u_) +
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
      const float out = radLP_.tick(radHP_.tick(shock_.read(shockD_))) * 3.0f;
      // Brass is mellow when it's played softly and brightens as it's pushed (Risset 1965: the
      // spectrum's bandwidth grows with the level). Here the lips' pulse is nearly as rich at pp as
      // at mf (Iowa trumpet at pp: H8 and over 15 to 25 dB further down than at mf), so the tone is
      // shaped by the dynamic: a lowpass from 3 harmonics up at the softest to wide open at forte.
      if ((toneAt_++ & 15) == 0)
         tone_.lowpass(std::min(lipHz_ * std::exp2(1.6 + 3.4 * dyn), 0.45 * fs_), 0.7, fs_);
      return tone_.tick(out);
   }

private:
   // The bell's cutoff: a Bb trumpet's 1.4 kHz, but never under the note's 2nd harmonic, so the
   // top of the range still has a resonance to play on.
   // The Cone knob flares the bore: under the middle a narrower, more cylindrical trumpet (a
   // higher cutoff), over it a wider, more conical one like a flugelhorn's (a lower cutoff).
   // The Loss knob's wall losses grow with frequency, so a lossier tube is also a darker one.
   // However they darken it, the bell never cuts off under the note's 2nd harmonic (or, if that's
   // under where it's tuned, where it's tuned): a top note with no resonance over it dies.
   double bellCut(double hz, float cutoff, float cone, float loss) const {
      const double base = std::max(1400.0, 2.0 * hz);
      return std::max(base * cutoff * std::exp2(-1.4 * (cone - 0.5) - 1.2 * (loss - 0.04f)), std::min(base, 2.0 * hz));
   }
   double bellDelay(double hz, float cutoff, float cone, float loss) const {
      return 2.0 * onePoleDelay(hz, std::min(bellCut(hz, cutoff, cone, loss), 0.45 * fs_), fs_);
   }
   // Stiffness under the middle softens the lips by half as much: lips any slacker are damped so
   // heavily that the top of the range stops speaking.
   float lipStiffness() const {
      const float st = character.stiffness;
      return st < 0.5f ? 0.5f - 0.5f * (0.5f - st) : st;
   }
   // The lips' settings move the pitch (they pull it towards their own frequency, more or less):
   // a player lips it back, here the bore is trimmed by as much (in cents of length, measured).
   static double trim(double v, double at0, double at1) {
      const double d = v - 0.5;
      return (at1 - at0) * d + 2.0 * (at0 + at1) * d * d;
   }

   Delay bore_, shock_;
   OnePoleLP bell1_, bell2_, acLP_, acLP2_, breathLP_, breathLP2_;
   Biquad radHP_, radLP_, tone_;
   Player player_;
   unsigned toneAt_ = 0;
   static constexpr double kShock0 = 480.0;
   double acMean_ = 0, acEnv_ = 0, acCoef_ = 0.001, envFall_ = 0.9997, closedCoef_ = 0.003;
   double x_ = 0, v_ = 0, u_ = 0, lipHz_ = 440, dcz_ = 0, shockD_ = 120, closed_ = 0, coneDelay_ = 0, trim_ = 1;
   static constexpr double kTrimAp0 = -134, kTrimAp1 = 91, kTrimSt0 = 16, kTrimSt1 = -45, kTrimEm0 = -150, kTrimEm1 = 115;
   double emb_ = 0.5, embTrim_ = 1;
   float tongue_ = 0.0f;
};

// The Alien's throat: five resonances, at a point on a ring of eight shapes (a, ghost, o, abyss, u,
// i, insect, e, and round to a), moved by Shift (-1..1 octaves) and sharpened by Sharp (0..1). The
// vowels are a tenor's formants; the others no throat has. They sound in a row, each 1 at DC (a
// Klatt cascade, as a vocal tract's do), so how loud each is comes from where they all stand.
// Shared with the panel, which draws it.
constexpr int kAlienFormants = 5, kAlienShapes = 8;
struct AlienThroat {
   double hz[kAlienFormants], bw[kAlienFormants];
   double norm = 1.0;
   // The cascade's response at f, before norm.
   double cascade(double f) const {
      double g = 1.0;
      for (int k = 0; k < kAlienFormants; ++k) {
         const double r = f / hz[k], d = bw[k] / hz[k];
         g /= std::sqrt((1.0 - r * r) * (1.0 - r * r) + d * d * r * r);
      }
      return g;
   }
};
// Its level over a voice's spectrum (falling 6 dB an octave), held at 1 so the level holds as the
// throat moves.
inline void alienNorm(AlienThroat &out) {
   double e = 0.0, w = 0.0;
   for (double fr = 110.0; fr < 8000.0; fr *= 1.12) {
      const double g = out.cascade(fr), wt = 1.0 / fr;
      e += wt * g * g;
      w += wt;
   }
   out.norm = std::sqrt(w / std::max(e, 1e-12));
}
inline void alienThroat(double pos, double shift, double sharp, AlienThroat &out) {
   struct Shape {
      float hz[kAlienFormants], bw[kAlienFormants];
   };
   static const Shape ring[kAlienShapes] = {
      {{730, 1090, 2440, 3400, 4000}, {70, 80, 110, 130, 150}},   // a
      {{230, 1450, 1620, 5200, 7100}, {60, 18, 18, 40, 60}},      // ghost
      {{500, 820, 2500, 3350, 4000}, {55, 70, 100, 130, 150}},    // o
      {{140, 330, 520, 2100, 2350}, {30, 40, 50, 60, 60}},        // abyss
      {{300, 870, 2240, 3300, 4000}, {45, 60, 100, 130, 150}},    // u
      {{270, 2290, 3010, 3500, 4100}, {45, 90, 110, 130, 150}},   // i
      {{1200, 2300, 4100, 6200, 8800}, {120, 50, 60, 80, 120}},   // insect
      {{480, 1900, 2550, 3400, 4000}, {55, 85, 110, 130, 150}},   // e
   };
   const int i = static_cast<int>(std::floor(pos));
   const double f0 = pos - i, f = f0 * f0 * (3.0 - 2.0 * f0);
   const Shape &a = ring[((i % kAlienShapes) + kAlienShapes) % kAlienShapes];
   const Shape &b = ring[(((i + 1) % kAlienShapes) + kAlienShapes) % kAlienShapes];
   const double up = std::exp2(std::clamp(shift, -1.0, 1.0));
   const double narrow = std::exp2(4.0 * (0.5 - std::clamp(sharp, 0.0, 1.0)));
   for (int k = 0; k < kAlienFormants; ++k) {
      out.hz[k] = std::exp(std::log(a.hz[k]) + f * (std::log(b.hz[k]) - std::log(a.hz[k]))) * up;
      out.bw[k] = std::max(4.0, std::exp(std::log(a.bw[k]) + f * (std::log(b.bw[k]) - std::log(a.bw[k]))) * narrow);
   }
   alienNorm(out);
}

// The Alien's consonants, after the Klatt synthesizer's (Klatt 1980) and measured against recorded
// speech: where in the mouth each is made (its place, which sets where the resonances start, by the
// vowel that follows), and its timeline: the mouth shut (a stop's closure), the consonant's own
// sound, then the breath let through the throat unvoiced (aspiration), then the vowel. Each sound
// is a level against the vowel's: the throat open (oral, a liquid's or a glide's own shape), a
// murmur (the voice with the mouth shut, all under 400 Hz: a nasal's hum, a voiced stop's buzz),
// and a hiss at the teeth or lips (a band of noise, buzzed by the voice in a voiced one), with a
// short pop at a stop's release.
enum AlienPlace { kLips, kTeeth, kRidge, kPalate, kVelum, kGlottis };
struct AlienConsonant {
   const char *name;
   int place;
   float oral, murmur, closeMurmur;
   bool nasal, voicedHiss;
   float hiss, hissHz, hissQ, pop, closeMs, soundMs, aspMs;
   float f1, f2, f3; // a liquid's or a glide's own resonances (0: from its place)
};
// Soft (sonorants), middling (voiced), hard (voiceless): the order Talk's hardness picks from.
inline constexpr AlienConsonant kAlienConsonants[] = {
   // soft: nasals, liquids and glides
   {"m", kLips, 0.0f, 0.19f, 0.0f, true, false, 0, 0, 0, 0, 0, 70, 0, 0, 0, 0},
   {"n", kRidge, 0.0f, 0.13f, 0.0f, true, false, 0, 0, 0, 0, 0, 65, 0, 0, 0, 0},
   {"ng", kVelum, 0.0f, 0.22f, 0.0f, true, false, 0, 0, 0, 0, 0, 70, 0, 0, 0, 0},
   {"l", kRidge, 0.3f, 0.3f, 0.0f, false, false, 0, 0, 0, 0, 0, 55, 0, 360, 1050, 2700},
   {"r", kPalate, 0.4f, 0.12f, 0.0f, false, false, 0, 0, 0, 0, 0, 60, 0, 350, 1100, 1600},
   {"w", kLips, 0.25f, 0.15f, 0.0f, false, false, 0, 0, 0, 0, 0, 50, 0, 300, 650, 2200},
   {"y", kPalate, 0.45f, 0.1f, 0.0f, false, false, 0, 0, 0, 0, 0, 50, 0, 270, 2100, 2900},
   // middling: voiced fricatives, stops and an affricate, and h
   {"v", kLips, 0.0f, 0.11f, 0.0f, false, true, 0.05f, 2500, 0.4f, 0, 0, 60, 0, 0, 0, 0},
   {"dh", kTeeth, 0.0f, 0.09f, 0.0f, false, true, 0.04f, 3000, 0.4f, 0, 0, 55, 0, 0, 0, 0},
   {"z", kRidge, 0.0f, 0.16f, 0.0f, false, true, 0.2f, 6500, 2.0f, 0, 0, 80, 0, 0, 0, 0},
   {"zh", kPalate, 0.0f, 0.16f, 0.0f, false, true, 0.25f, 3000, 1.3f, 0, 0, 80, 0, 0, 0, 0},
   {"b", kLips, 0.0f, 0.0f, 0.055f, false, false, 0, 900, 0.8f, 0.15f, 45, 8, 0, 0, 0, 0},
   {"d", kRidge, 0.0f, 0.0f, 0.055f, false, false, 0, 4000, 1.2f, 0.3f, 40, 8, 0, 0, 0, 0},
   {"g", kVelum, 0.0f, 0.0f, 0.055f, false, false, 0, 2000, 1.6f, 0.3f, 45, 10, 0, 0, 0, 0},
   {"j", kPalate, 0.0f, 0.12f, 0.06f, false, true, 0.25f, 3000, 1.3f, 0.2f, 35, 45, 0, 0, 0, 0},
   {"h", kGlottis, 0.0f, 0.0f, 0.0f, false, false, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0},
   // hard: voiceless fricatives, stops and an affricate
   {"f", kLips, 0.0f, 0.0f, 0.0f, false, false, 0.08f, 2500, 0.4f, 0, 0, 90, 0, 0, 0, 0},
   {"th", kTeeth, 0.0f, 0.0f, 0.0f, false, false, 0.055f, 3000, 0.4f, 0, 0, 85, 0, 0, 0, 0},
   {"s", kRidge, 0.0f, 0.0f, 0.0f, false, false, 0.5f, 7000, 2.0f, 0, 0, 105, 0, 0, 0, 0},
   {"sh", kPalate, 0.0f, 0.0f, 0.0f, false, false, 0.6f, 3000, 1.3f, 0, 0, 105, 0, 0, 0, 0},
   {"p", kLips, 0.0f, 0.0f, 0.0f, false, false, 0, 900, 0.8f, 0.2f, 50, 8, 35, 0, 0, 0},
   {"t", kRidge, 0.0f, 0.0f, 0.0f, false, false, 0, 4500, 1.2f, 0.4f, 45, 8, 35, 0, 0, 0},
   {"k", kVelum, 0.0f, 0.0f, 0.0f, false, false, 0, 2000, 1.6f, 0.35f, 50, 10, 40, 0, 0, 0},
   {"ch", kPalate, 0.0f, 0.0f, 0.0f, false, false, 0.45f, 3000, 1.3f, 0.25f, 40, 55, 0, 0, 0, 0},
};
inline constexpr int kAlienConsonantCount = static_cast<int>(sizeof(kAlienConsonants) / sizeof(kAlienConsonants[0]));
inline constexpr int kAlienSoft = 7, kAlienMiddling = 16; // (the soft ones end, the middling ones end)
// Its eight families, each of which can be turned off: m n ng, l r, w y, v dh z zh, b d g j, h,
// f th s sh, p t k ch.
inline constexpr int alienFamily(int consonant) {
   constexpr int kStarts[8] = {0, 3, 5, 7, 11, 15, 16, 20};
   int f = 0;
   while (f < 7 && consonant >= kStarts[f + 1])
      ++f;
   return f;
}
// Where a consonant made at `place` starts the first three resonances, given the vowel's (Sussman's
// locus equations for F2: the lips pull it down, the ridge towards 1.7 kHz, the velum close to a
// front vowel's and pinched against F3): every consonant sounds a little different before each vowel,
// as it does in speech.
inline void alienLocus(int place, const double vowel[3], double out[3]) {
   const double f2 = vowel[1];
   out[0] = 260.0;
   switch (place) {
   case kLips:
      out[1] = 0.7 * f2 + 200.0;
      out[2] = 0.9 * vowel[2];
      break;
   case kTeeth:
      out[1] = 0.5 * f2 + 800.0;
      out[2] = 2600.0;
      break;
   case kRidge:
      out[1] = 0.4 * f2 + 1200.0;
      out[2] = 2700.0;
      break;
   case kPalate:
      out[1] = 0.3 * f2 + 1500.0;
      out[2] = 2400.0;
      break;
   case kVelum:
      out[1] = f2 > 1600.0 ? 0.95 * f2 + 150.0 : 0.6 * f2 + 700.0;
      out[2] = out[1] + 300.0;
      break;
   default: // the glottis: the vowel's own
      out[0] = vowel[0];
      out[1] = vowel[1];
      out[2] = vowel[2];
   }
}

// Alien: a bowed string sounding through a throat that keeps changing shape.
//
// - The string: two waveguides either side of the bow (towards the bridge and towards the nut),
//   each end reflecting with its sign turned over, the bridge's through a lowpass (the string's
//   losses, which the Hole Cutoff sets). The bow drags the string with stick-slip friction: while
//   the string moves with it, they stick and the bow carries it; once the string's velocity falls
//   too far behind, it slips. The friction's curve is the classic bow table (McIntyre, Schumacher &
//   Woodhouse; Smith): its steepness is the bow's force. The breath is the bow's speed, the Bite its
//   force (light: a flautando whisper; heavy: crunch and scratch), the Embouchure where it crosses
//   the string (near the bridge: bright and glassy; towards the middle: round). Lifting the bow (the
//   breath going) lets the string ring on, muted under the player's hand.
// - The Cone knob is how stiff the string is: a stiff string's waves travel faster the higher they
//   are, so its partials stretch apart, from a gut string (0) through a piano wire to a bar (1).
// - The throat: the bridge's motion, through five resonances at once, as a voice's formants. They
//   don't follow the note (like a mouth, they stay where they are while the note moves), and they
//   travel through a ring of eight shapes: the vowels a, o, u, i and e, and three no throat has (a
//   ghost's whistle, an abyss, an insect's whine). VOICE is where on the ring each note starts,
//   MORPH how fast it travels from there while the note is held, DRIFT how far it wanders on its own
//   (each resonance a little apart from the others), SHIFT moves them all up or down an octave (a
//   smaller or larger throat), and SHARP narrows them, until they whistle.
class Alien final : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      neck_.init(static_cast<int>(std::max(fs / 20.0, 2.0 * kMinLoop)) + 8);
      bridge_.init(static_cast<int>(std::max(fs / 20.0, 2.0 * kMinLoop)) + 8);
      rate_ = fs;
      control_ = std::max(1, static_cast<int>(fs / 3000.0));
      reset();
   }
   void reset() override {
      neck_.clear();
      bridge_.clear();
      loop_ = {};
      still_ = {};
      for (auto &a : stiff_)
         a = {};
      x1_ = 0.0f;
      low_.z = edge_.z = mellow_.z = mellow2_.z = 0.0f;
      sawPower_ = throatPower_ = 0.0f;
      match_ = 1.0f;
      // (Each copy of a unison its own place in its period: voices that start together still don't
      // start in step, so they swell and fade against each other less.)
      phase_ = 0.5 + 0.5 * rng.white();
      g1_ = 0.0;
      sylT_ = 1e9;
      mouthSet_ = false;
      oralT_ = oralS_ = srcT_ = srcS_ = 1.0f;
      murmurT_ = murmurS_ = hissT_ = hissS_ = aspT_ = aspS_ = 0.0f;
      pop_ = 0.0;
      fric_.clear();
      murmur1_.y1 = murmur1_.y2 = murmur2_.y1 = murmur2_.y2 = 0.0;
      breathS_ = 0.0;
      for (auto &r : res_)
         r.y1 = r.y2 = 0.0;
      dc_ = {};
      dc_.setFs(fs_);
      noiseLP_ = {};
      noiseLP_.setHz(3000, fs_);
      travel_ = 0.0;
      wander_ = 0.0;
      wanderV_ = 0.0;
      for (auto &j : jitter_)
         j = 0.0;
      count_ = 0;
      lifted_ = 1.0;
      notePitch(hz_);
      shape();
   }
   double nominal(double hz) const override { return fs_ / hz; }
   void notePitch(double hz) override {
      hz_ = hz;
      // A bowed string only catches into its stick-slip motion with a couple of hundred steps to
      // each period (fewer, and the slip's corner is lost between them): a high note's string is
      // run that many times faster than the voice.
      const int steps = std::clamp(static_cast<int>(std::ceil(kMinLoop * hz / fs_)), 1, kMaxSteps);
      if (steps != steps_) {
         steps_ = steps;
         neck_.clear();
         bridge_.clear();
      }
      rate_ = fs_ * steps_;
      loop_.setHz(loopCut(hz), rate_);
      still_.setFs(rate_);
      // The stiffness: two first-order allpasses. Their delay at the note, and the loop's lowpass's,
      // come out of the string (in its own steps), so it plays in tune at any stiffness and cutoff.
      const double c = std::clamp(static_cast<double>(character.cone), 0.0, 1.0);
      stiffA_ = -0.7 * std::pow(c, 1.5);
      filterDelay_ = 2.0 * allpassDelay(hz, stiffA_) + onePoleDelay(hz, loopCut(hz), rate_);
      tilt_ = static_cast<float>(fs_ / (2.0 * kPi * 400.0));
      edge_.setHz(3500.0, fs_);
      powerA_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.04 * fs_)));
      easeA_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.002 * fs_)));
      popA_ = std::exp(-1.0 / (0.004 * fs_));
      // (The murmur: the voice with the mouth shut, a nasal's or a voiced stop's hum, all low.)
      murmur1_.set(260.0, 120.0, fs_);
      murmur2_.set(300.0, 300.0, fs_);
      low_.setHz(hz, fs_);
      // High notes as a singer sings them: from about E4 up (an octave, then two), the folds close
      // more gently, the first resonance rides above the note, the upper ones widen, and the top is
      // rolled off, so a few lone harmonics don't land on sharp peaks and whistle.
      high_ = std::clamp(std::log2(hz / 330.0), 0.0, 2.0);
      const double roll = high_ > 0.0 ? 9000.0 / (1.0 + 1.2 * high_) : 0.45 * fs_;
      mellow_.setHz(roll, fs_);
      mellow2_.setHz(roll, fs_);
      // (The tilt makes a high note's string far louder than its voice: brought back level.)
      sawLevel_ = static_cast<float>(1.0 / std::sqrt(1.0 + (hz / 300.0) * (hz / 300.0)));
   }
   // Where on its ring of shapes the throat is now, 0..8.
   float throat() const override { return pos_; }
   void articulate(uint32_t word, double hardness) override {
      word_ = word;
      said_ = 0;
      syllable(1.0, hardness, true);
   }
   float tick(float breath, float emb, double len, float tn) override {
      breath_ = breath;
      if (--count_ <= 0) {
         count_ = control_;
         shape();
      }
      const float scrape = noiseLP_.tick(rng.white());
      // The bow: its speed from the breath, roughened by the rosin (Noise) and the tongue's bite.
      // Where it crosses the string: nearer the bridge it needs a slower, heavier bow (the bow's
      // speed is scaled so a note keeps its one slip a period anywhere along it).
      const double beta = betaK_ + 0.11 * std::clamp(static_cast<double>(emb), 0.0, 1.0);
      // (Aperture is how wide the bow's hair lies on the string: more of it, more sound.)
      const double vb = vbK_ * beta / 0.125 * (0.7 + 0.6 * character.aperture) * breath * (1.0 + 0.04 * character.noise * scrape) + 0.03 * tn * scrape;
      const double force = std::clamp(f0_ + 0.35 * (character.edge - 1.0) + f1_ * (breath - 0.8), 0.04, 0.95);
      // (Stiffness is the hair's tension and the rosin: a tauter, stickier bow grips and lets go
      // more sharply, brighter and more biting.)
      const double slope = (5.0 - 4.0 * force) * (0.8 + 0.4 * character.stiffness);
      // Lifted as the breath goes, so the string rings out; the player's hand mutes it.
      const double down = std::clamp(breath / 0.12, 0.0, 1.0);
      lifted_ += (down - lifted_) * 0.002;
      // (A hair longer: the string on its own sounds 3 cents sharp of its loop, and it must sing in
      // tune with the voice.)
      const double L = std::max(4.0, 1.0017 * len * steps_ - filterDelay_);
      const double lb = std::max(1.0, beta * L), ln = std::max(1.0, L - lb);
      const double keep = character.keep(0.995f) * (0.86 + 0.14 * lifted_);
      double sum = 0.0;
      const float bowMix = std::clamp(character.bow, 0.0f, 1.0f);
      for (int step = 0; step < steps_ && bowMix > 0.0f; ++step) {
         // (Its ends are fixed, so it can't drift along with the bow: no velocity at DC.)
         double fromBridge = still_.tick(loop_.tick(bridge_.read(lb)));
         for (auto &a : stiff_)
            fromBridge = a.tick(fromBridge, stiffA_);
         const double bridgeRefl = -keep * fromBridge;
         const double nutRefl = -neck_.read(ln);
         const double dv = vb - (bridgeRefl + nutRefl);
         double friction = std::fabs((dv + 0.001) * slope) + 0.75;
         friction = std::clamp(1.0 / (friction * friction * friction * friction), 0.01, 0.98);
         const double nv = dv * friction * down;
         neck_.push(static_cast<float>(bridgeRefl + nv));
         bridge_.push(static_cast<float>(nutRefl + nv));
         sum += bridgeRefl;
      }
      const float x = static_cast<float>(sum / steps_);
      // (Its velocity, tilted up 6 dB an octave from 400 Hz towards the force on the bridge: the
      // bowed string's bright sawtooth buzz.)
      const float saw = (x + tilt_ * (x - x1_)) * sawLevel_;
      x1_ = x;
      // The voice: vocal folds opening and snapping shut once a period (a Rosenberg pulse, radiated:
      // its slope), harder blowing closing them faster (brighter, as a voice pushed), and the
      // breath hissing through them while they're open (Noise). A consonant closes the mouth
      // over it for an instant at each syllable.
      phase_ += 1.0 / std::max(len, 2.0);
      phase_ -= std::floor(phase_);
      const double push = std::clamp(static_cast<double>(breath), 0.0, 1.4);
      const double open = std::clamp(0.72 - 0.18 * push - 0.08 * (character.stiffness - 0.5) + 0.1 * high_, 0.35, 0.9);
      const double rise = 0.62 * open, fall = open - rise;
      const double g = phase_ < rise    ? 0.5 * (1.0 - std::cos(kPi * phase_ / rise))
                       : phase_ < open ? std::cos(0.5 * kPi * (phase_ - rise) / fall)
                                       : 0.0;
      const double glot = (g - g1_) * len * 0.12;
      g1_ = g;
      const float hiss = scrape * static_cast<float>(g) * (0.05f + 0.25f * character.noise);
      // The consonant's parts (each eased over a couple of ms, so nothing clicks): how much of the
      // throat is open, how much voice goes into it, the murmur, the hiss and the breath.
      oralS_ += (oralT_ - oralS_) * easeA_;
      srcS_ += (srcT_ - srcS_) * easeA_;
      murmurS_ += (murmurT_ - murmurS_) * easeA_;
      hissS_ += (hissT_ - hissS_) * easeA_;
      aspS_ += (aspT_ - aspS_) * easeA_;
      const float level = static_cast<float>(down * (0.35 + 0.65 * std::min(push, 1.0)));
      const float air = rng.white();
      const float src = (1.0f - bowMix) * (level * srcS_ * (static_cast<float>(glot) + hiss) + level * aspS_ * kAsp * air) +
                        bowMix * srcS_ * saw;
      // The throat: five resonances in a row, as a vocal tract's are (deep valleys between sharp
      // peaks), with the fundamental under them. It's held to the source's level (both measured
      // over the last 40 ms, and only while it's voiced, so a consonant's breath doesn't set it),
      // so it stays as loud whatever its shape and the note.
      double v = src;
      for (auto &r : res_)
         v = r.tick(v);
      const float throat = norm_ * static_cast<float>(v + low_.tick(src));
      if (srcS_ > 0.9f && aspS_ < 0.05f) {
         sawPower_ += (src * src - sawPower_) * powerA_;
         throatPower_ += (throat * throat - throatPower_) * powerA_;
      }
      // The consonant's own sounds, each held to the vowel's level (the voice's, measured all the
      // while): the murmur, the voice under 400 Hz; the hiss, a band of noise (a voiced one's buzzed
      // by the voice), and a stop's pop through the same band, dying away in a few ms.
      const float gl = static_cast<float>(glot);
      glotPower_ += (gl * gl - glotPower_) * powerA_;
      const float vowelLevel = level * std::sqrt(glotPower_);
      const float hum = static_cast<float>(murmur2_.tick(murmur1_.tick(gl)));
      humPower_ += (hum * hum - humPower_) * powerA_;
      float noise = static_cast<float>(fric_.tick(rng.white())) * fricNorm_;
      if (voicedHiss_)
         noise *= 0.35f + 0.65f * static_cast<float>(g);
      pop_ *= popA_;
      const float sounds = murmurS_ * hum * std::sqrt(glotPower_ / (humPower_ + 1e-12f)) * level +
                           (hissS_ + static_cast<float>(pop_)) * noise * vowelLevel;
      // The bow's rosin edge, above the resonances, gets out round the throat.
      const float buzz = saw - edge_.tick(saw);
      float y = oralS_ * match_ * throat + sounds + kBuzz * bowMix * buzz;
      y = mellow2_.tick(mellow_.tick(y));
      return dc_.tick(kOut * y);
   }

private:
   static constexpr double kLoopCut = 14.0, kMinLoop = 240.0;
   static constexpr int kMaxSteps = 12;
   static constexpr float kOut = 9.0f, kBuzz = 0.5f, kAsp = 0.25f;
   struct Allpass {
      double x1 = 0, y1 = 0;
      double tick(double x, double a) {
         const double y = a * x + x1 - a * y1;
         x1 = x;
         y1 = y;
         return y;
      }
   };
   // A first-order allpass's phase delay at hz, in the string's steps.
   double allpassDelay(double hz, double a) const {
      const double w = 2.0 * kPi * hz / rate_;
      const std::complex<double> z1 = std::polar(1.0, -w);
      return -std::arg((a + z1) / (1.0 + a * z1)) / w;
   }
   // (The Hole Cutoff moves it half as far as on a bore: much brighter and the string would rather
   // slip twice a period, up the octave.)
   double loopCut(double hz) const {
      return std::max(kLoopCut * hz * std::sqrt(std::clamp(static_cast<double>(character.cutoff), 0.25, 3.1)), 3.0 * hz);
   }

   // Every control_ samples: where on the ring the throat is, and its resonances there.
   void shape() {
      if (character.cone != coneK_) {
         coneK_ = character.cone;
         notePitch(hz_);
      }
      const double dt = control_ / fs_;
      const double m = std::clamp(static_cast<double>(character.morph), 0.0, 1.0);
      travel_ += 3.0 * m * m * dt;
      // The drift: a slow random walk (about a second's memory), and each resonance its own.
      const double d = std::clamp(static_cast<double>(character.drift), 0.0, 1.0);
      const double leak = std::exp(-dt / 0.8);
      wanderV_ = wanderV_ * leak + std::sqrt(dt) * 1.2 * rng.white();
      wander_ = wander_ * std::exp(-dt / 3.0) + wanderV_ * dt * 2.0;
      for (auto &j : jitter_)
         j = j * leak + std::sqrt(dt) * 0.25 * rng.white();
      // Talk: each syllable starts with the mouth shut (a consonant, the resonances low, wide and
      // muffled) and opens onto its own vowel, a jump round the ring from the last; the breath's
      // pressure moves the mouth (harder, further round), and the more it talks, the more it
      // chatters on its own while a note is held, syllable after syllable.
      const double talk = std::clamp(static_cast<double>(character.talk), 0.0, 1.0);
      breathS_ += (breath_ - breathS_) * (1.0 - std::exp(-dt / 0.03));
      const bool voiced = breath_ > 0.06;
      // A held note goes on to its next syllable after a while, a lighter one (a glide more than a
      // consonant): at the Pace's note value at the host's tempo, or free, every 1.6 s or so at
      // half Talk and 0.8 s at full, each a little longer or shorter than the last, as speech is.
      held_ += dt;
      if (voiced && talk > 0.2 && held_ > nextAt_)
         syllable(0.6, 0.6 * std::min(breathS_, 1.0));
      // The syllable: its consonant (the mouth shut, then its own sound, then the breath through
      // the throat), then its vowel, gliding out of where the consonant left the mouth in the
      // Glide's time. (Lighter, a held note's next syllable or less Consonant: closer to the vowel.)
      sylT_ += dt * 1000.0;
      const AlienConsonant &k = kAlienConsonants[std::max(cons_, 0)];
      const bool sounding = talk > 0.05 && cons_ >= 0;
      const double closeEnd = sounding ? k.closeMs : 0.0, soundEnd = sounding ? closeEnd + k.soundMs : 0.0,
                   aspEnd = sounding ? soundEnd + k.aspMs : -1.0;
      const bool inCons = sounding && sylT_ < aspEnd;
      const float depth = static_cast<float>(consDepth_);
      const auto lighter = [depth](float full, float vowel) { return vowel + depth * (full - vowel); };
      if (!inCons) {
         oralT_ = srcT_ = 1.0f;
         murmurT_ = hissT_ = aspT_ = 0.0f;
      } else if (sylT_ < closeEnd) {
         oralT_ = lighter(0.0f, 1.0f);
         srcT_ = 1.0f;
         murmurT_ = depth * k.closeMurmur;
         hissT_ = aspT_ = 0.0f;
      } else if (sylT_ < soundEnd) {
         if (!popped_ && k.pop > 0.0f) {
            pop_ = depth * k.pop; // (a stop's release: a pop at its place)
            popped_ = true;
         }
         oralT_ = lighter(k.oral, 1.0f);
         srcT_ = 1.0f;
         murmurT_ = depth * k.murmur;
         hissT_ = depth * k.hiss;
         aspT_ = 0.0f;
      } else {
         oralT_ = 1.0f;
         srcT_ = lighter(0.0f, 1.0f);
         murmurT_ = hissT_ = 0.0f;
         aspT_ = depth;
      }
      if (inCons)
         glide_ = 0.0;
      else
         glide_ = std::min(1.0, glide_ + dt / (0.001 * std::clamp(static_cast<double>(character.glideMs), 5.0, 400.0)));
      const double f = glide_ * glide_ * (3.0 - 2.0 * glide_);
      const double base = character.vowel * kAlienShapes + travel_ + 0.6 * d * wander_ + talk * 0.5 * (breathS_ - 0.7);
      AlienThroat ta, tb, t;
      alienThroat(base + from_, character.shift, character.sharp, ta);
      alienThroat(base + to_, character.shift, character.sharp, tb);
      // Where the consonant leaves the mouth: its place's locus for the vowel that follows (or a
      // liquid's or glide's own shape), the nasals' upper resonances damped by the nose.
      if (sounding) {
         const double up = std::exp2(std::clamp(static_cast<double>(character.shift), -1.0, 1.0));
         double loc[3];
         if (k.f1 > 0.0f) {
            loc[0] = k.f1 * up;
            loc[1] = k.f2 * up;
            loc[2] = k.f3 * up;
         } else
            alienLocus(k.place, tb.hz, loc);
         ta = tb;
         for (int n = 0; n < 3; ++n)
            ta.hz[n] = std::exp(std::log(tb.hz[n]) + consDepth_ * (std::log(loc[n]) - std::log(tb.hz[n])));
         for (int n = 0; n < kAlienFormants; ++n)
            ta.bw[n] = tb.bw[n] * (k.nasal && n > 0 ? 1.0 + 2.0 * consDepth_ : 1.0);
      }
      for (int n = 0; n < kAlienFormants; ++n) {
         t.hz[n] = std::exp(std::log(ta.hz[n]) + f * (std::log(tb.hz[n]) - std::log(ta.hz[n])));
         t.bw[n] = ta.bw[n] + f * (tb.bw[n] - ta.bw[n]);
      }
      // (The mouth can't jump: every move eased over 6 ms, a consonant's too, so none clicks.)
      const double ease = 1.0 - std::exp(-dt / 0.006);
      if (!mouthSet_)
         mouth_ = t;
      mouthSet_ = true;
      for (int n = 0; n < kAlienFormants; ++n) {
         mouth_.hz[n] = std::exp(std::log(mouth_.hz[n]) + ease * (std::log(t.hz[n]) - std::log(mouth_.hz[n])));
         mouth_.bw[n] += ease * (t.bw[n] - mouth_.bw[n]);
      }
      t = mouth_;
      // (Bent, the throat follows the bend, as far as Bend sets.)
      if (character.bendSemis != 0.0f) {
         const double follow = std::exp2(std::clamp(static_cast<double>(character.bendSemis), -48.0, 48.0) / 12.0);
         for (int n = 0; n < kAlienFormants; ++n)
            t.hz[n] *= follow;
      }
      // (A high note's first resonance above it, and the upper ones wider: see notePitch.)
      const double ride = std::min(high_, 1.0);
      t.hz[0] = std::max(t.hz[0], t.hz[0] + ride * (1.15 * hz_ - t.hz[0]));
      for (int n = 1; n < kAlienFormants; ++n)
         t.bw[n] *= 1.0 + 0.8 * high_;
      alienNorm(t);
      // (A higher note has only a few harmonics, and how loud the throat makes it depends on where
      // they fall against the resonances: from about A3 up it's held level on its own harmonics,
      // at once, rather than by the slower level match alone, so a moving mouth doesn't surge.)
      const double onHarmonics = std::clamp(std::log2(hz_ / 220.0), 0.0, 1.0);
      if (onHarmonics > 0.0) {
         double e = 0.0, w = 0.0;
         for (int k = 1; k <= 24 && k * hz_ < 9000.0; ++k) {
            const double g = t.cascade(k * hz_), wt = 1.0 / (k * k);
            e += wt * g * g;
            w += wt;
         }
         const double own = std::sqrt(w / std::max(e, 1e-12));
         t.norm = std::exp(std::log(t.norm) + onHarmonics * (std::log(own) - std::log(t.norm)));
      }
      const double at = base + (f < 0.5 ? from_ : to_);
      pos_ = static_cast<float>(at - kAlienShapes * std::floor(at / kAlienShapes));
      for (int n = 0; n < kAlienFormants; ++n) {
         const double fc = std::clamp(t.hz[n] * std::exp2(0.12 * d * jitter_[n]), 40.0, 0.42 * fs_);
         res_[n].set(fc, std::max(t.bw[n], 4.0), fs_);
      }
      norm_ = static_cast<float>(t.norm);
      match_ = std::clamp(std::sqrt((sawPower_ + 1e-12f) / (throatPower_ + 1e-12f)), 0.05f, 20.0f);
   }

   // A syllable: a consonant (how fully: 1 a note's own, less a held note's next) and a new vowel. Each
   // note's word comes from the voice (one count across every voice, so they run on from note to
   // note as one sentence), and a held note's syllables follow from it; each a good way round the
   // ring from the one before (about three shapes, give or take one), so no two in a row say the
   // same thing. (All from the count, so a unison's copies say it together.)
   void syllable(double depth, double hardness, bool initial = false) {
      held_ = 0.0;
      sylT_ = 0.0;
      popped_ = false;
      consDepth_ = std::clamp(depth * std::min(1.0, 2.0 * character.talk) * character.consonants, 0.0, 1.0);
      const uint32_t k = word_ * 7919u + said_++;
      uint32_t h = k * 2654435761u;
      h ^= h >> 15;
      h *= 2246822519u;
      h ^= h >> 13;
      const double r0 = (h & 0xffff) / 65536.0, r1 = (h >> 16) / 65536.0;
      // Its vowel, among those that are on (A E I O U, on the ring at 0, 7, 5, 2 and 4): by the
      // Order, at random (never the last one again), in turn, by the note's pitch (low notes the dark
      // vowels, high the bright), or every note on the same one (a held note's next syllables
      // still move on, at random).
      static const double kVowels[5] = {0.0, 7.0, 5.0, 2.0, 4.0};
      static const int kDarkToBright[5] = {4, 3, 0, 1, 2}; // u o a e i
      int on[5], ons = 0;
      for (int i = 0; i < 5; ++i)
         if (character.vowels & (1u << i))
            on[ons++] = i;
      if (ons == 0)
         on[ons++] = 0;
      int next = on[0];
      const int order = initial ? character.order : 0;
      if (order == 1) { // in turn
         int at = 0;
         while (at < ons && on[at] <= vowelIndex_)
            ++at;
         next = on[at % ons];
      } else if (order == 2) { // by pitch
         int dark[5], darks = 0;
         for (int i : kDarkToBright)
            if (character.vowels & (1u << i))
               dark[darks++] = i;
         if (darks > 0) {
            const double where = std::clamp((std::log2(hz_ / 65.4) / 5.0), 0.0, 0.999); // C2 .. C7
            next = dark[static_cast<int>(where * darks)];
         }
      } else if (order == 3) { // the same
         next = on[0];
      } else { // at random, never the last one again
         int others[5], n = 0;
         for (int i = 0; i < ons; ++i)
            if (on[i] != vowelIndex_ || ons == 1)
               others[n++] = on[i];
         next = others[static_cast<int>(r0 * n) % n];
      }
      vowelIndex_ = next;
      from_ = to_;
      to_ = character.talk > 0.05f ? kVowels[next] : 0.0;
      glide_ = 0.0;
      // Its consonant: softer touches (velocity, pressure) the soft ones (m, n, l, r, w, y), harder
      // the voiced (v, z, b, d, g...), hardest the hiss and pop of s, sh, f, t, k, p. Which one in
      // its class is the syllable's own, as English would put them: never a ng or zh to start a
      // word, a y before an i or a w before a u.
      const double hard = std::clamp(hardness + 0.3 * (r1 - 0.5), 0.0, 0.999);
      const int lo = hard < 0.38 ? 0 : hard < 0.7 ? kAlienSoft : kAlienMiddling;
      const int hi = hard < 0.38 ? kAlienSoft : hard < 0.7 ? kAlienMiddling : kAlienConsonantCount;
      uint32_t h2 = h * 2654435761u;
      h2 ^= h2 >> 16;
      const auto fits = [&](int c) {
         if (!(character.sounds & (1u << alienFamily(c))))
            return false;
         const std::string_view name = kAlienConsonants[c].name;
         if (initial && (name == "ng" || name == "zh"))
            return false;
         return !((name == "y" && next == 2) || (name == "w" && next == 4));
      };
      int pick[kAlienConsonantCount], picks = 0;
      for (int c = lo; c < hi; ++c)
         if (fits(c))
            pick[picks++] = c;
      if (picks == 0)
         for (int c = 0; c < kAlienConsonantCount; ++c)
            if (fits(c))
               pick[picks++] = c;
      cons_ = picks > 0 && consDepth_ > 0.01 ? pick[h2 % static_cast<uint32_t>(picks)] : -1;
      if (cons_ >= 0) {
         const AlienConsonant &c = kAlienConsonants[cons_];
         voicedHiss_ = c.voicedHiss;
         if (c.hissHz > 0.0f) {
            // (Held to the same level whatever its band: a bandpass passes white noise's power in
            // proportion to its width.)
            const double hz = std::min(static_cast<double>(c.hissHz), 0.42 * fs_);
            fric_.bandpass(hz, c.hissQ, fs_);
            fricNorm_ = static_cast<float>(1.0 / (0.577 * std::sqrt(kPi * hz / c.hissQ / fs_)));
         }
      }
      const double talk = std::clamp(static_cast<double>(character.talk), 0.2, 1.0);
      nextAt_ = character.paceSec > 0.0f ? character.paceSec : (2.4 - 1.6 * talk) * (0.75 + 0.5 * r1);
      // Linger: a chance this vowel is held on for two or three times as long, more the harder it's
      // blown.
      const double linger = std::clamp(character.linger * (0.4 + 0.6 * std::min(breathS_, 1.3)), 0.0, 1.0);
      if (r0 * 0.618 + r1 * 0.382 < linger)
         nextAt_ *= r1 > 0.5 ? 3.0 : 2.0;
   }
   uint32_t word_ = 0, said_ = 0;
   int vowelIndex_ = 0, cons_ = 0;
   double sylT_ = 1e9, consDepth_ = 0.0;
   float oralT_ = 1.0f, srcT_ = 1.0f, murmurT_ = 0.0f, hissT_ = 0.0f, aspT_ = 0.0f;
   float oralS_ = 1.0f, srcS_ = 1.0f, murmurS_ = 0.0f, hissS_ = 0.0f, aspS_ = 0.0f, easeA_ = 0.01f;
   float glotPower_ = 0.01f, humPower_ = 0.01f, fricNorm_ = 1.0f;
   double pop_ = 0.0, popA_ = 0.99;
   bool popped_ = false, voicedHiss_ = false;
   Biquad fric_;
   AlienThroat mouth_;
   bool mouthSet_ = false;
   double from_ = 0.0, to_ = 0.0, glide_ = 1.0;

   Delay neck_, bridge_;
   OnePoleLP loop_, noiseLP_;
   double phase_ = 0.0, g1_ = 0.0, breathS_ = 0.0, held_ = 0.0, nextAt_ = 1.0;
   float breath_ = 0.0f;
   Allpass stiff_[2];
   float sawLevel_ = 1.0f, tilt_ = 0.0f, x1_ = 0.0f, norm_ = 1.0f, match_ = 1.0f, sawPower_ = 0.0f, throatPower_ = 0.0f, powerA_ = 0.001f;
   OnePoleLP low_, edge_, mellow_, mellow2_;
   double high_ = 0.0;
   // A Klatt resonator: 1 at DC, a peak at hz, bw wide.
   struct Resonator {
      double a = 1, b = 0, c = 0, y1 = 0, y2 = 0;
      void set(double hz, double bw, double fs) {
         const double r = std::exp(-kPi * bw / fs);
         c = -r * r;
         b = 2.0 * r * std::cos(2.0 * kPi * hz / fs);
         a = 1.0 - b - c;
      }
      double tick(double x) {
         const double y = a * x + b * y1 + c * y2;
         y2 = y1;
         y1 = y;
         return y;
      }
   };
   Resonator res_[kAlienFormants];
   Resonator murmur1_, murmur2_;
   DCBlock dc_, still_;
   double rate_ = 96000.0, filterDelay_ = 0.0, vbK_ = 0.1, f0_ = 0.4, f1_ = 0.2, betaK_ = 0.07;
   int steps_ = 1;
   double hz_ = 220.0, stiffA_ = 0.0, travel_ = 0.0, wander_ = 0.0, wanderV_ = 0.0, lifted_ = 1.0;
   double jitter_[kAlienFormants] = {};
   float coneK_ = 0.5f, pos_ = 0.0f;
   int control_ = 32, count_ = 0;
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
   // (The Alien's bowed string holds its pitch whatever the bow's speed.)
   const Point *p = nullptr;
   int n = 0;
   switch (model) {
   case kFlute: p = flute; n = 9; break;
   case kClarinet: p = clarinet; n = 5; break;
   case kTrumpet: p = trumpet; n = 12; break;
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
