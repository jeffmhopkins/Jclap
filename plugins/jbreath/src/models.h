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

#include "bore_table.h"
#include "speech.h"

namespace jbreath {

constexpr double kPi = 3.14159265358979323846;

enum ModelId : int { kFlute, kClarinet, kSax, kOboe, kTrumpet, kAlien, kModelCount };

// The tuning table's rows: one for each model, then one for each bigger sax horn a tuned-down sax
// plays (tenor, baritone, bass, contrabass: see Sax::notePitch), whose tuning differs from the
// alto's playing the same pitch.
inline constexpr int kSaxHorns = 4, kTuneRows = kModelCount + kSaxHorns;
inline constexpr double kSaxHornSize[kSaxHorns] = {0.7492, 0.5, 0.3746, 0.25};
inline constexpr float kSaxHornLevel = 0.7f;
inline constexpr float kSaxHornTune[kSaxHorns] = {-5.0f, -12.0f, -17.0f, -24.0f};
// The sax horn nearest a tuning of `tune` semitones (-1 for the alto: tuned down less than 4.5).
inline int saxHorn(double tune) {
   if (tune > -4.5)
      return -1;
   const double want = std::exp2(tune / 12.0);
   int best = 0;
   for (int j = 1; j < kSaxHorns; ++j)
      if (std::fabs(std::log(kSaxHornSize[j] / want)) < std::fabs(std::log(kSaxHornSize[best] / want)))
         best = j;
   return best;
}
// The level a row's gains are baked at, against the models' own rows (tools/tune.cpp).
inline double rowLevel(int row) { return row >= kModelCount ? kSaxHornLevel : 1.0; }
inline int tuningRow(int model, double tune) {
   const int horn = model == kSax ? saxHorn(tune) : -1;
   return horn < 0 ? model : kModelCount + horn;
}

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
      // (The four Lagrange weights, their shared products taken once.)
      const double a = t - 1, b = t - 2, c = t - 3, ab = a * b, tc = t * c;
      const double c0 = -ab * c * (1.0 / 6.0);
      const double c1 = 0.5 * tc * b;
      const double c2 = -0.5 * tc * a;
      const double c3 = t * ab * (1.0 / 6.0);
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
   // Whether the filter passes everything unchanged (a peak or shelf at 0 dB): once its state has
   // rung out it is skipped, giving exactly what running it would (y = x, its state staying 0).
   bool flat() const { return b0 == 1.0 && b1 == a1 && b2 == a2; }
   float tick(float x) {
      if (pass_ && z1 == 0.0 && z2 == 0.0)
         return x;
      return static_cast<float>(tickd(x));
   }
   // The same in double: a chain of them rounds to float once, at its end.
   double tickd(double x) {
      if (pass_ && z1 == 0.0 && z2 == 0.0)
         return x;
      const double y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
   }
   void peak(double hz, double q, double db, double fs) {
      const double A = std::pow(10, db / 40), w = 2 * kPi * hz / fs, al = std::sin(w) / (2 * q), a0 = 1 + al / A;
      b0 = (1 + al * A) / a0;
      b1 = -2 * std::cos(w) / a0;
      b2 = (1 - al * A) / a0;
      a1 = b1;
      a2 = (1 - al / A) / a0;
      pass_ = flat();
   }
   // A bandpass whose peak is 0 dB, at hz with bandwidth hz / q.
   void bandpass(double hz, double q, double fs) {
      const double w = 2 * kPi * hz / fs, al = std::sin(w) / (2 * q), a0 = 1 + al;
      b0 = al / a0;
      b1 = 0.0;
      b2 = -al / a0;
      a1 = -2 * std::cos(w) / a0;
      a2 = (1 - al) / a0;
      pass_ = flat();
   }
   void highpass(double hz, double q, double fs) {
      const double w = 2 * kPi * hz / fs, al = std::sin(w) / (2 * q), cs = std::cos(w), a0 = 1 + al;
      b0 = (1 + cs) / 2 / a0;
      b1 = -(1 + cs) / a0;
      b2 = b0;
      a1 = -2 * cs / a0;
      a2 = (1 - al) / a0;
      pass_ = flat();
   }
   void lowpass(double hz, double q, double fs) {
      hz = std::min(hz, 0.45 * fs);
      const double w = 2 * kPi * hz / fs, al = std::sin(w) / (2 * q), cs = std::cos(w), a0 = 1 + al;
      b0 = (1 - cs) / 2 / a0;
      b1 = (1 - cs) / a0;
      b2 = b0;
      a1 = -2 * cs / a0;
      a2 = (1 - al) / a0;
      pass_ = flat();
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
      pass_ = flat();
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
      pass_ = flat();
   }

private:
   bool pass_ = true;
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
   // The Alien's throat: its accent (0..1), how fast its vowels wobble (morph) and how far they
   // wander (0..1), its size in octaves (-1..1) and how sharp its resonances are (0..1); the bow; how
   // often a held note says another syllable (talk, or paceSec), a diphthong's hold (linger) and
   // the vowels' glide in. How breathy, how its resonances spread (-1..1), how far its vowels are
   // turned inside out and its throat follows the note (0..1), and whether it sings typed words.
   float vowel = 0.0f, morph = 0.0f, drift = 0.2f, shift = 0.0f, sharp = 0.5f, bow = 0.0f, talk = 0.5f, paceSec = 0.0f, linger = 0.2f, glideMs = 30.0f;
   float breathy = 0.25f, spread = 0.0f, flip = 0.0f, follow = 0.0f;
   // How alive the Alien sounds: rough, rasping (its Growl) and wet (0..1 each).
   float rough = 0.45f, rasp = 0.0f, wet = 0.2f;
   bool typed = false;
   // (The Alien: how many semitones the note is bent that its throat follows.)
   float bendSemis = 0.0f;
   // (The Alien's consonants: how strong, and which of its eight families it says, a bit each.)
   float consonants = 1.0f;
   // (The tongue's kick: the Alien's attack and consonants, harder with more.)
   float kick = 0.25f;
   // (The Tune knob, in semitones: a sax tuned down a fourth or more is a bigger horn of its family.)
   float tuneSemis = 0.0f;
   uint32_t sounds = 0xffu;
   // (Its vowels: which of A E I O U it says, a bit each. Those and the consonants' families make
   // its own words, which the plugin builds: see lyrics.h.)
   uint32_t vowels = 0x1fu;
   int order = 1;

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
      // (Gone, past 40 of its time constants: no exp for the rest of the note.)
      return t <= 0.0 ? 1.0 : t > 40.0 * tauSec * fs_ ? 0.0 : std::exp(-t / (tauSec * fs_));
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
   // A new note, or a slurred one: the Alien sings the syllable at `word` in its book.
   virtual void articulate(uint32_t word, double hardness) {
      (void)word;
      (void)hardness;
   }
   // The Alien's words, and the syllable it's singing (-1: none, or another instrument).
   virtual void setBook(const speech::Book * /*book*/) {}
   virtual int saying() const { return -1; }
   // How far through its book it has got: the place of the syllable it last sang (a held note goes on
   // past the one it started on, by Talk and Pace).
   virtual uint32_t word() const { return 0; }
   // The note let go: the Alien closes its syllable.
   virtual void letGo() {}
   // Still sounding after the breath has gone (the Alien's closing consonants).
   virtual bool ringing() const { return false; }

   Noise rng;
   Character character;
   // How many semitones of the note's pitch are the player's bend (set before notePitch). A sax,
   // oboe or trumpet player bends a note with the lips on the fingering it was played with, so the
   // models that finger their notes choose it (the trumpet's valves, the sax's register, a bigger
   // horn) for the pitch without the bend, up to kLipBendMax either way; past that, with it.
   double lipBend = 0.0;
   static constexpr double kLipBendMax = 3.0;
   // How many semitones the note last given to notePitch is off the pitch of the fingering it's
   // played on (bent there by the lips): the voice tunes it as that fingering's note.
   virtual double tuningOffset() const { return 0.0; }
   // The tuning table's row the note last given to notePitch is played from, `row` being the one
   // its model and tuning pick: another when the model has kept a horn the row doesn't play there.
   virtual int tuneRow(int row) const { return row; }
   double fingerHz(double hz) const { return hz * std::exp2(-std::clamp(lipBend, -kLipBendMax, kLipBendMax) / 12.0); }

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
      const float lam = std::clamp(breath / 0.6f, 0.0f, 1.0f), laminar = lam * std::sqrt(lam);
      const float y = jlp_.tick(jet_.read(tau)) + (0.012f * character.noise * std::min(1.0f, 1.5f * U) * laminar + 0.12f * tn) * turb;
      // The jet's flow grows with pressure, not just its speed; Stiffness also firms the jet's gain.
      const float q = 1.2f * U * U * tanhJet(8.0f * (0.5f + character.stiffness) * y + 0.3f);
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
      // (The bias depends only on the embouchure, which moves at the control rate.)
      if (emb != offEmb_) {
         offEmb_ = emb;
         off_ = 1.2f * (0.4f + 1.2f * emb);
         s0_ = tanhJet(off_);
         d0_ = 1.0f - s0_ * s0_;
      }
      const float off = off_, s0 = s0_, d0 = d0_;
      // (Saturated, it still carries more flow at the fundamental, so forte keeps growing a little.)
      const float Dg = std::max(1e-3f, D * grow);
      const float J = a * (tanhJet(Dg * n + off) - s0) / (Dg * d0) * std::sqrt(std::max(1.0f, 0.8f * D));
      const float rad = J - rz_ * rawPrev_;
      rawPrev_ = J;
      // The edge's turbulence, band-limited and swelling as the jet sweeps across the edge: more of
      // it from a wide jet or a loose lip.
      const float hiss = turb - nhp_.tick(turb);
      const float airy = (0.5f + character.aperture) * (1.6f - 1.2f * character.stiffness);
      // (It grows faster than the tone with the breath: a soft note is nearly pure, a hard-blown one
      // rushes, as on a real flute.)
      // (and high notes, blown through a smaller, faster jet, are breathier than low ones).
      const float rush = U * std::min(3.0f, std::max(0.0f, breath / 0.67f) * std::sqrt(std::max(0.0f, breath / 0.67f))) * std::clamp(static_cast<float>(hz_) / 400.0f, 1.0f, 4.0f);
      return dco_.tick(olp2_.tick(olp_.tick(3.0f * rad)) + 0.04f * character.noise * airy * rush * hiss * (0.3f + std::fabs(n)));
   }

private:
   double loopCut(double hz) const { return character.cut(hz * 2.4, 1.5 * hz); }
   // tanh, by its [7/6] Pade approximant where the jet mostly works (within 1.4e-7 of it, a couple
   // of float steps), and the library's beyond: called twice a sample, it was an eighth of the CPU.
   static float tanhJet(float x) {
      if (std::fabs(x) >= 2.5f)
         return std::tanh(x);
      const float x2 = x * x;
      return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
   }
   float offEmb_ = -1e9f, off_ = 0.0f, s0_ = 0.0f, d0_ = 1.0f;
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
      pitchCut_ = -1.0f;
      delayHz_ = -1.0;
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
      s_ = 0.0;
   }
   double nominal(double hz) const override { return fs_ / (2.0 * hz); }
   // (Both asked for every control tick, but the note and knobs they follow seldom move: the last
   // answer is kept until they do.)
   double cutoffDelay(double hz) const override {
      if (hz == delayHz_ && character.cutoff == delayCut_ && character.loss == delayLoss_)
         return delay_;
      const double cut = std::max(1300.0, 2.2 * hz);
      delayHz_ = hz;
      delayCut_ = character.cutoff;
      delayLoss_ = character.loss;
      delay_ = kCutPitch * 2.0 * (onePoleDelay(hz, character.cut(cut, 2.1 * hz), fs_) - onePoleDelay(hz, cut, fs_)) +
               character.lossDelay(nominal(hz), kLossA, kLossB);
      return delay_;
   }
   // The bore's loss sits near 1.3 kHz, which only lets the low and middle register speak; it moves
   // up with notes above that, so they play too (as a real clarinet's tone holes leave more bore).
   void notePitch(double hz) override {
      if (hz == hz_ && character.cutoff == pitchCut_)
         return;
      pitchCut_ = character.cutoff;
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
      // u = F(2 back + u): the flow and the pressure it makes. With W = D - u the drop across the
      // reed (D its drop before this sample's flow) and s = sgn(W) sqrt|W|, where it's smooth:
      // h(s) = W + z open(1 - W) s - D = 0, which rises through its root wherever the reed is. A few
      // Newton steps from the last sample's answer settle it (to far under float's resolution);
      // where they don't, as the reed snaps shut or open, by bisection, as before.
      const double D = g - 2.0 * back;
      const auto h = [z, knee, D](double s, double &dh) {
         const double a = std::fabs(s), W = s * a, gap = 1.0 - W;
         double open, dOpen;
         if (gap >= knee) {
            open = gap;
            dOpen = 1.0;
         } else if (gap > -knee) {
            open = (gap + knee) * (gap + knee) / (4.0 * knee);
            dOpen = (gap + knee) / (2.0 * knee);
         } else {
            open = dOpen = 0.0;
         }
         dh = 2.0 * a + z * (open - 2.0 * s * a * dOpen);
         return W + z * open * s - D;
      };
      double s = s_;
      bool solved = false;
      for (int it = 0; it < 4 && !solved; ++it) {
         double dh;
         const double v = h(s, dh);
         if (!(dh > 1e-9))
            break;
         const double step = v / dh;
         s -= step;
         solved = std::fabs(step) < 1e-9;
      }
      double u;
      if (solved) {
         s_ = s;
         u = D - s * std::fabs(s);
      } else {
         double lo = -3, hi = 3;
         for (int it = 0; it < 24; ++it) {
            const double uu = 0.5 * (lo + hi);
            if (uu - F(2 * back + uu) > 0)
               hi = uu;
            else
               lo = uu;
         }
         u = 0.5 * (lo + hi);
         const double W = D - u;
         s_ = W >= 0.0 ? std::sqrt(W) : -std::sqrt(-W);
      }
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
   float pitchCut_ = -1.0f;
   mutable double delayHz_ = -1.0, delay_ = 0.0;
   mutable float delayCut_ = -1.0f, delayLoss_ = -1.0f;
   // The last sample's reed solution, sgn(W) sqrt|W|, where the next one's Newton steps start.
   double s_ = 0.0;
   float tx_ = 0.0f;
};

// Alto saxophone: an alto's own air column, fingering by fingering (bore.h, baked from its
// geometry by tools/bore.py: the mouthpiece's chamber and throat, the neck and the conical body
// with its nineteen tone holes and the bell, and the octave key's vents), with a cane reed beating
// on it.
//
// The reed is a mass on a spring under the player's lip, pushed shut by the mouth's pressure over
// the mouthpiece's (an inward-striking valve: Wilson & Beavers 1974, Chaigne & Kergomard 2016),
// with its own resonance well above the notes (about 2 kHz, damped by the lip). The air through
// its slit goes as the square root of the drop across it; blown hard enough it beats against the
// lay, shutting for part of every cycle, and closes softly over a little travel as cane rolls
// onto the lay. Each note is the fingering a player would use: the first register from low Bb
// with the holes opening from the bell one by one, the second with the octave key's vent, which
// pulls the first resonance away so the reed takes the second. The resonances above the open
// holes' cutoff fade, as a real alto's do, and what's heard is the flow out of the bell and every
// open hole, radiated.
//
// Below the alto's low Bb (concert Db3) it plays as a bigger member of its family (a baritone or
// bass sax): the whole instrument scaled up, its reed's resonance lower in proportion.
class Sax final : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      player_.prepare(fs);
      reset();
   }
   void reset() override {
      bore_.reset();
      x_ = v_ = dpPrev_ = pmS_ = pmB_ = 0.0;
      tongue_ = 0.0f;
      player_.reset();
      at_ = 0;
      ticked_ = false;
      pmCoef_ = 1.0 - std::exp(-1.0 / (0.004 * fs_));
      breathLP_ = {};
      breathLP_.setHz(2000.0, fs_);
      dc_ = {};
      dc_.setFs(fs_);
      eqHp_.clear();
      eqPk1_.clear();
      eqPk2_.clear();
      eqHs_.clear();
      eqLp1_.clear();
      eqLp2_.clear();
      tone_.clear();
      lift_.clear();
      reg_.clear();
      fam_.clear();
      famDb_ = 1e9;
      regDb_ = 1e9;
      onT_ = 1.0;
      tnPrev_ = 0.0f;
      shut_ = false;
      lift0_ = 0.0;
      toneDb_ = 1e9;
      liftDb_ = 1e9;
      eqAt_ = -1.0;
      fing_ = -1;
      notePitch(hz_);
   }
   double nominal(double hz) const override { return fs_ / hz; }
   double cutoffDelay(double) const override { return 0.0; }
   double tuningOffset() const override { return fingerOff_; }
   // (Slid past the lips' reach on the note's horn, it's that horn's row.)
   int tuneRow(int row) const override {
      if (std::fabs(lipBend) <= kLipBendMax)
         return row;
      if (member_ == 1.0)
         return kSax;
      for (int j = 0; j < kSaxHorns; ++j)
         if (member_ == kSaxHornSize[j])
            return kModelCount + j;
      return row;
   }
   void notePitch(double hz) override {
      hz_ = hz;
      // Under the alto's low Bb the horn is a bigger one of the family, the alto's bore scaled up:
      // the tenor (a fourth down), the baritone (an octave), the bass (an octave and a fourth) or
      // the contrabass (two octaves), each fingered as the alto is for its note, so every member's
      // low notes come from its own tone holes.
      // (The pitch here wanders with the player's drift and vibrato, a few cents either way: the
      // switch to a bigger horn is a quarter tone under each one's bottom note, so a note played
      // on it, the alto's own low C#3 above all, keeps to one horn instead of flipping between two.)
      // (The horn and the fingering are chosen for the note without the player's bend, which the
      // lips make on the fingering the note was played with, the whole bore bent with it up to
      // kLipBendMax. Re-fingered across the octave key, the reed fell to the low register for a
      // tenth of a second, an octave down; fingered along chromatically, a bend stepped, its tone
      // and level jumping at each semitone.)
      // (The horn and the register, though, are the note's own for the whole of a bend, however
      // far: slid on past kLipBendMax, the sax fingers along the note's register on its horn, and
      // past the register's last fingering stretches it. Changing horn or taking the octave key on
      // or off mid-slide, the reed fell to another resonance, an octave or a fifth away, and
      // broke up while it found the note again.)
      double hf;
      int f = choose(hz * std::exp2(-lipBend / 12.0), fingerHz(hz), member_, hf);
      const double ha = hz / member_;
      // (Once the note is sounding, the fingering held stays until the note is three quarters of a
      // semitone off it, so a note between two doesn't flip back and forth.)
      if (ticked_ && f != fing_ && member_ == fingMember_ && (fing_ >= 20) == (f >= 20) &&
          std::fabs(12.0 * std::log2(hf / kLow) - noteOf(fing_)) < 0.75)
         f = fing_;
      fingMember_ = member_;
      const double want = kLow * std::exp2(noteOf(f) / 12.0);
      // (The voice tunes and levels the note as the table has this fingering's own note: across
      // a bend, or a vibrato, the fingering's correction and level hold, and change with it. Tuned
      // by the pitch, a note bent towards the next fingering's took on its tuning and level while
      // still on its own, up to a semitone sharp and 15 dB loud where the two differ most.)
      const double sp = 12.0 * std::log2(hz);
      // (Only the lips' bend: a note past the top fingering, its bore stretched over it, has its
      // own place in the table, which a whole offset to the fingering's note left silent.)
      fingerOff_ = 12.0 * std::log2(hz / fingerHz(hz));
      if (std::fabs(lipBend) > kLipBendMax) {
         // (Slid past the lips' reach, it's tuned from the horn's own row (tuneRow), at the
         // fingered pitch; slid under the horn's lowest fingering, as that fingering's own note,
         // stretched.)
         const double own = 12.0 * std::log2(kLow * member_) + noteOf(f);
         if (sp - fingerOff_ < own - 0.75)
            fingerOff_ = sp - own;
      } else if (!same(sp - fingerOff_, f)) {
         // (Bent onto another horn's notes, the table has none for this fingering on this horn:
         // the nearest pitch it plays on this one, towards the note without the bend.)
         const double fp = 12.0 * std::log2(fingerHz(hz));
         double lo = 0.0, hi = 1.0;
         if (same(fp, f))
            for (int it = 0; it < 14; ++it) {
               const double mid = 0.5 * (lo + hi);
               (same(sp + mid * (fp - sp), f) ? hi : lo) = mid;
            }
         fingerOff_ = hi * (sp - fp);
      }
      const BoreFingering &b = kSaxBore[f];
      int best = 0;
      for (int i = 1; i < b.modes; ++i)
         if (std::fabs(std::log(b.peakHz[i] / want)) < std::fabs(std::log(b.peakHz[best] / want)))
            best = i;
      fing_ = f;
      peakHz_ = b.peakHz[best];
      // (With the octave key, the vent's leak and the player's voicing keep the reed off the first
      // resonance, which the vent has pulled away from the second's octave.)
      vent_ = best > 0 ? 0.75 * peakHz_ : 0.0;
      // The reed stands less open up the range, as a player's lip firms for the higher notes (more
      // steeply above C#4): wide open on the low notes, which keeps them on their fundamental.
      open_ = kOpen * std::pow(ha / 277.0, ha < 277.0 ? -0.5 : -1.0);
      // (Once the note is playing, the bore follows the length it's played at, which the tuning
      // table has corrected, from the next sample: set here from the uncorrected pitch, it
      // flipped between two sizes every 16 samples, a buzz of sidebands and, each time the
      // bell's wave was resampled for the other size, many times the CPU.)
      if (ticked_)
         at_ = 0;
      else
         setBore(hz);
   }
   float tick(float breath, float emb, double len, float tn) override {
      player_.tongued(tn);
      // (No scoop up to the note: a real alto's tongued notes start on their pitch, Philharmonia.)
      // (The wander runs every sample; the pitch it moves is only read by setBore(), every 16.)
      const float wander = player_.wander(rng);
      // A tongued note from silence: the pressure built up behind the tongue is let go with the
      // reed, an accent that fades over kAccentSec (a real alto's tongued notes reach full level in
      // 10 to 20 ms, a few dB over the note held, Philharmonia).
      onT_ += 1.0 / fs_;
      // (The tongue's burst rises smoothly with the air, so a note's tonguing starts where it
      // passes a small threshold.)
      const bool tongued = tn > 0.02f && tnPrev_ <= 0.02f;
      if (tongued)
         onT_ = 0.0;
      const bool fromRest = tongued && pmS_ < 0.05;
      tnPrev_ = tn;
      if (fromRest) {
         shut_ = true;
         xShut_ = x_;
      }
      // (Only from mezzo forte up: a softer note's accent would come out a burst of a louder,
      // brighter note. And less on a softer reed, Stiffness under half way, which blown past it
      // would start on an upper resonance.)
      pmB_ += (breath - pmB_) * pmCoef_;
      // (Its exp only while the accent can sound: the note blown hard enough, the reed firm enough.)
      if (const double acc = kAccent * std::clamp((breath - 0.7) / 0.25, 0.0, 1.0) * std::clamp(2.0 * character.stiffness, 0.0, 1.0); acc > 0.0 && onT_ < 25.0 * kAccentSec)
         breath = static_cast<float>(breath * (1.0 + acc * std::exp(-onT_ / kAccentSec)));
      pmS_ += (breath - pmS_) * pmCoef_;
      ticked_ = true;
      if ((at_++ & 15) == 0)
         setBore(fs_ / len * std::exp2(wander / 1200.0));
      // The mouth's pressure, over the pressure that shuts the reed: the breath, its turbulence,
      // the tongue's burst and (with the Growl knob up, blown hard) the throat's rasp.
      const float noise = breathLP_.tick(rng.white());
      const float growl = std::max(0.0f, breath - 0.95f) * 0.6f * character.edge * (character.growl - 0.6f);
      const double stiff = (0.8 + 0.4 * character.stiffness) * (1.1 - 0.2 * emb);
      // A player blows softly by closing the lip on the reed as well as easing the breath: the reed
      // stands less open, so less pressure shuts it, and the two fall together (the mouth's
      // pressure over the pressure that shuts the reed, gamma, stays where the reed sounds).
      const double lip = std::clamp(kLip0 + (1.0 - kLip0) * (pmS_ - 0.5) / 0.5, kLip0, 1.2);
      const double g0 = std::min(1.0, pmS_ / 0.5) * kGamma + kGamma1 * std::max(0.0, pmS_ - 0.5);
      const double gamma = g0 / stiff * (1.0 + (0.01 * character.noise + growl) * noise) + 0.2 * tn * noise;
      const double pM = kPM * stiff * lip, pm = gamma * pM;
      // The reed: x is its tip's travel over the opening at rest (-1 against the lay), pushed shut
      // by the drop across it a sample behind, its resonance damped by the lip (and lower on a
      // bigger family member's reed, though by less than its bore: lower, a baritone's E3 blown hard
      // locked a semitone flat onto a resonance near 1.8 kHz).
      // (The bigger horn's factors change only with the note: worked out again when they do.)
      if (body_ != bodyAt_ || lowNote_ != lowAt_) {
         bodyAt_ = body_;
         lowAt_ = lowNote_;
         const double bd = std::min(1.0, body_);
         bodyQ_ = std::sqrt(std::sqrt(bd));
         bodyH_ = 1.0 / std::pow(bd, 1.5 + 0.5 * lowNote_);
      }
      if (character.edge != edgeAt_) {
         edgeAt_ = character.edge;
         knee_ = 0.08 * std::exp(-1.2 * (character.edge - 1.0));
      }
      const double wr = 2.0 * kPi * kReedHz * std::sqrt(stiff) * (0.9 + 0.2 * emb) * bodyQ_;
      double acc = -wr * wr * (x_ + dpPrev_ / pM) - kReedDamp * wr * v_;
      // (Against the lay the cane meets the stiff facing.)
      if (x_ < -1.0)
         acc -= 30.0 * wr * wr * (x_ + 1.0) + 4.0 * wr * v_;
      v_ += acc / fs_;
      // (The tongue leaving the reed sets it moving.)
      if (tn > tongue_)
         v_ += kKick * (tn - tongue_) * wr;
      tongue_ = tn;
      x_ += v_ / fs_;
      // (The tongue holds the reed shut against the lay while the mouth's pressure builds behind
      // it, and lets go once it's nearly there: the note starts on the wave that release makes,
      // not growing slowly out of a reed standing half open.)
      // (A soft note's tongue comes away more gently: let go at once, its reed would click against
      // a quiet tone.)
      if (shut_ && pmS_ < 0.97 * breath) {
         // (closing over 2 ms: shut at once, the flow already through it would stop with a click)
         x_ = std::max(-1.0, std::min(x_, xShut_ - 1.0 / (0.002 * fs_)));
         xShut_ = x_;
         v_ = 0.0;
      } else if (shut_) {
         shut_ = false;
         lift0_ = 1.0;
         liftSec_ = 0.003 + 0.007 * std::clamp((0.7 - breath) / 0.25, 0.0, 1.0);
      }
      if (lift0_ > 0.0) {
         lift0_ = std::max(0.0, lift0_ - 1.0 / (liftSec_ * fs_));
         const double ceiling = -1.0 + 1.5 * (1.0 - lift0_);
         if (x_ > ceiling) {
            x_ = ceiling;
            v_ = std::min(v_, 0.0);
         }
      }
      // The slit: closing softly over the curl of the reed's tip (shorter with more bite). A bigger
      // horn's reed and lay open wider again than its scale on its low notes (with less, a baritone's
      // and tenor's low notes buzzed between two pitches for most of a second; over them, as wide
      // locked the baritone's E3 a semitone flat).
      const double knee = knee_, gap = 1.0 + x_;
      const double open = 0.5 * (gap + std::sqrt(gap * gap + knee * knee)) - 0.5 * knee;
      const double H = open_ * lip * (0.5 + character.aperture) * (1.2 - 0.4 * emb) * bodyH_;
      const double a = std::max(0.0, open) * H * kWidth * std::sqrt(2.0 / kRho);
      // u = a sgn(D) sqrt|D|, with D the drop with this sample's flow's own pressure in it.
      const double ph = bore_.history(), zi = bore_.instant(), D0 = pm - ph;
      const double um = 0.5 * (-a * a * zi + std::sqrt(a * a * a * a * zi * zi + 4.0 * a * a * std::fabs(D0)));
      const double u = D0 >= 0 ? um : -um;
      bore_.push(u);
      dpPrev_ = pm - (ph + zi * u);
      const float y = dc_.tick(static_cast<float>(bore_.radiated() * kOut));
      return static_cast<float>(fam_.tickd(reg_.tickd(lift_.tickd(tone_.tickd(eqLp2_.tickd(eqLp1_.tickd(eqHs_.tickd(eqPk2_.tickd(eqPk1_.tickd(eqHp_.tickd(y))))))))))) * softGain_ * regGain_ * famGain_;
   }

private:
   // A fingering's note, in semitones over the alto's low Bb (with the octave key, the octave up).
   static double noteOf(int f) { return f < 20 ? f : f - 20 + 4 + 12; }
   // The horn (member) and fingering for a note played at home (its pitch without the bend) and
   // fingered at fhz (the same with no bend, or a bend within the lips'), and the fingered pitch on
   // that horn (ha). The horn and register are home's; the fingering, the nearest fhz in that
   // register.
   int choose(double home, double fhz, double &member, double &ha) const {
      member = 1.0;
      for (double s : {0.7492, 0.5, 0.3746, 0.25})
         if (home < kQuarterDown * kLow * member)
            member = s;
      // Tuned down (the Tenor and Baritone presets), the horn is the member nearest the tuning
      // across its whole range, up to an octave over its top fingering: a real baritone's A3 is a
      // baritone's, far brighter than an alto's (Good-sounds).
      // (Tuned down to the contrabass, it's the bass sax over the contrabass's own lowest notes:
      // the contrabass's reed found no note through most of its range, a fifth or an octave under
      // the one asked for, so those notes jumped an octave.)
      if (const int horn = std::min(saxHorn(character.tuneSemis), kSaxHorns - 2); horn >= 0) {
         const double near = kSaxHornSize[horn];
         if (near < member && home < 4.0 * kLow * std::exp2(kTopN / 12.0) * near)
            member = near;
      }
      ha = fhz / member;
      // The fingering: written low Bb (0) to C#5 (15) in the first register, then the octave key
      // over the fingerings from D4 (4) to the palm keys' F (19). The note is the resonance of the
      // fingering nearest it (its first, or with the octave key its second).
      const bool upper = 12.0 * std::log2(fhz / member / kLow) >= 15.5;
      const double m = 12.0 * std::log2(ha / kLow) - (upper ? 12.0 : 0.0);
      const int n = std::clamp(static_cast<int>(std::lround(m)), upper ? 4 : 0, upper ? kTopN : 15);
      return upper ? 20 + n - 4 : n;
   }
   // Whether the tuning table's note at `semis` (12 log2 of its Hz) is played on the horn now
   // sounding with fingering f, within half a semitone of that fingering's own note.
   bool same(double semis, int f) const {
      double member, ha;
      const double hz = std::exp2(semis / 12.0);
      return choose(hz, hz, member, ha) == f && member == member_ &&
             std::fabs(12.0 * std::log2(ha / kLow) - noteOf(f)) <= 0.5;
   }
   void setBore(double hz) {
      scale_ = hz / peakHz_;
      // (What the bore's size sets in the reed and the tone follows it over kBodySec: at each
      // fingering a bend crosses, the size jumps by the bend's share of a semitone, and the reed's
      // opening and resonance jumping with it, the flow through it did too, a click each time.)
      body_ = ticked_ ? body_ + (scale_ - body_) * (1.0 - std::exp(-16.0 / (kBodySec * fs_))) : scale_;
      // What's heard, as a real alto's in front of it (fitted to the Iowa recordings' harmonics):
      // a broad formant near 290 Hz, a peak near 2.3 kHz and its bright top (falling away past 6 kHz), scaled down with a bigger family member.
      const double fam = std::min(1.0, body_);
      // (The peak near 2.3 kHz, the bright top and its fall come from the mouthpiece and reed, which
      // grow far less with a bigger horn than its bore does: scaled with it, they'd pile into a
      // narrow band near 1.2 kHz on a baritone, a ringing spike over its low notes.)
      const double up = std::pow(fam, 0.3);
      // (And on a bigger horn that peak and the shelf are lower, and its top falls away sooner: a
      // real baritone's spectrum is flat from 1 to 3.5 kHz and 30 dB down past it, Good-sounds. Left
      // as the alto's they rang as a spike near 1.9 kHz.)
      const double big = (1.0 - fam) / 0.5;
      if (std::fabs(fam - eqAt_) > 1e-3) {
         eqAt_ = fam;
         eqHp_.highpass(131.0 * fam, 3.0, fs_);
         eqPk1_.peak(285.8 * fam, 0.60, 24.7, fs_);
         eqPk2_.peak(2342.0 * up, 1.83, 15.7 - kBigPeakDb * big, fs_);
         eqHs_.highshelf(std::min(3764.0 * up, 0.4 * fs_), 40.0 - kBigShelfDb * big, fs_);
         // The shelf would hold to Nyquist; a real horn's top falls away past 5 kHz.
         const double upLp = std::pow(fam, 1.3);
         eqLp1_.lowpass(std::min(kTopHz * upLp, 0.4 * fs_), 0.54, fs_);
         eqLp2_.lowpass(std::min(kTopHz * upLp, 0.4 * fs_), 1.31, fs_);
      }
      // Blown softly a real alto's reed hardly beats, and its tone is nearly the fundamental alone
      // (Iowa, pp: H2 19 dB under it, H3 28); the reed here beats from the start, so its harmonics
      // are shelved down as the breath eases under mezzo forte (and the fundamental's share of the
      // shelf given back, most of it). With the octave key the second register's highs come through.
      const double soft = std::clamp((0.77 - pmB_) / 0.2, 0.0, 1.0);
      const double toneDb = -20.2 * soft + (vent_ > 0.0 ? 4.0 * (1.0 - soft) : 0.0);
      const double toneHz = std::min((vent_ > 0.0 && soft < 0.5 ? 1.75 : 1.2) * hz, 0.4 * fs_);
      if (std::fabs(toneDb - toneDb_) > 0.05 || std::fabs(toneHz / toneHz_ - 1.0) > 0.01) {
         toneDb_ = toneDb;
         toneHz_ = toneHz;
         tone_.highshelf(toneHz, toneDb, fs_);
      }
      softGain_ = static_cast<float>(std::pow(10.0, 7.3 * soft / 20.0));
      // (The voicing below takes the low notes' upper harmonics down; they're given back here, and
      // more on the bigger horns, whose low notes are richer still: a tenor's low Bb has its 3rd to
      // 7th harmonics 5 to 9 dB over its fundamental.)
      const double liftDb = 20.0 * std::clamp((300.0 - hz / member_) / 60.0, 0.0, 1.0) + 10.0 * (1.0 - member_);
      const double liftHz = std::min(3.5 * hz, 0.4 * fs_);
      if (std::fabs(liftDb - liftDb_) > 0.05 || std::fabs(liftHz / liftHz_ - 1.0) > 0.01) {
         liftDb_ = liftDb;
         liftHz_ = liftHz;
         lift_.highshelf(liftHz, liftDb, fs_);
      }
      // Each register's brightness and level, as a real alto's (fitted note by note to the
      // Philharmonia alto, D3 to F6, forte and piano): a shelf over the 2nd harmonic up and a gain,
      // every 4 semitones from the low Bb (on a bigger horn, its own). Without them the notes just
      // over the octave key stood out bright against the darker registers either side.
      {
         // (hz can be unset before the first note.)
         const double hn = hz > 1.0 && std::isfinite(hz) ? hz : kLow * member_;
         const double m = std::clamp(12.0 * std::log2(hn / member_ / kLow) / 4.0, 0.0, kRegPoints - 1.0);
         const int i = std::min(static_cast<int>(m), kRegPoints - 2);
         // (A bigger horn's low notes already take liftMember's highs: its shelf eases with its size.)
         const double f = m - i, rdb = (kRegDb[i] + (kRegDb[i + 1] - kRegDb[i]) * f) * member_ * member_ * member_;
         regGain_ = static_cast<float>(std::pow(10.0, (kRegGainDb[i] + (kRegGainDb[i + 1] - kRegGainDb[i]) * f +
                                                      soft * (kRegSoftDb[i] + (kRegSoftDb[i + 1] - kRegSoftDb[i]) * f)) / 20.0));
         const double rHz = std::min(1.5 * hz, 0.4 * fs_);
         if (std::fabs(rdb - regDb_) > 0.05 || std::fabs(rHz / regHz_ - 1.0) > 0.01) {
            regDb_ = rdb;
            regHz_ = rHz;
            reg_.highshelf(rHz, rdb, fs_);
         }
      }
      // A bigger horn's 3rd harmonic up, brighter again (a real baritone's low notes have their 3rd
      // to 6th harmonics 10 to 25 dB over the fundamental, Good-sounds): kFamilyDb on the
      // baritone, half of it on the tenor.
      {
         const double db = kFamilyDb * (1.0 - member_) / 0.5, fHz = std::min(3.0 * hz, 0.4 * fs_);
         if (std::fabs(db - famDb_) > 0.05 || std::fabs(fHz / famHz_ - 1.0) > 0.01) {
            famDb_ = db;
            famHz_ = fHz;
            fam_.highshelf(fHz, db, fs_);
            // (and the level it adds taken back, half of it: the boosted harmonics carry the note)
            famGain_ = static_cast<float>(std::pow(10.0, -0.5 * db / 20.0));
         }
      }
      // The Loss knob widens the resonances; Hole Cutoff and Cone move the holes' cutoff.
      const double loss = std::clamp(1.0 + 1.5 * (character.loss - 0.04), 0.75, 1.35);
      const double cut = kHoleCut * character.cutoff * std::exp2(-(character.cone - 0.5));
      // Under D4 (on a bigger horn, its D4) the walls' loss is eased to kLowLoss of it below B3,
      // and the resonances over the note's 1.5th damped kVoicing times as much, as a player voices
      // a low note: with neither, the long bore's fundamental gives way to its upper resonances
      // and the tone hollows out or cracks up the octave. (On a bigger horn the octave vent's leak is
      // kBigVent times as strong: as the alto's, its G4 and top A fell back to the low register.)
      const double low = std::clamp((300.0 - hz / member_) / 60.0, 0.0, 1.0);
      lowNote_ = low;
      bore_.set(kSaxBore[fing_], scale_, fs_, loss * (1.0 + (kLowLoss - 1.0) * low), kHoleCut, cut, vent_ * scale_, kVentDamp * (member_ < 0.6 ? kBiggerVent : member_ < 1.0 ? kBigVent : 1.0),
                low > 0.0 ? 1.5 * peakHz_ * scale_ : 0.0, 1.0 + (kVoicing - 1.0) * low);
   }

   static constexpr double kLow = 138.59, kRho = 1.2, kPM = 6000.0, kWidth = 0.015, kOut = 500.0, kHoleCut = 1200.0,
                           kGamma = 0.45, kGamma1 = 0.1, kLip0 = 0.3, kReedHz = 2000.0, kReedDamp = 1.0,
                           kOpen = 7e-4, kKick = 0.2, kVentDamp = 8.0,
                           kBigVent = 2.5, kBiggerVent = 10.0, kBigPeakDb = 12.0, kBigShelfDb = 5.0,
                           kQuarterDown = 0.97153;
   static constexpr int kTopN = 15;
   static constexpr double kTopHz = 6500.0;  // the output's 4-pole lowpass
   static constexpr double kBodySec = 0.015;
   static constexpr double kLowLoss = 0.6;  // the walls' loss on the low notes, of the full
   static constexpr double kVoicing = 10.0;  // and their upper resonances' damping (at 5 the low notes
                                             // blown hard buzzed between two pitches)
   static constexpr double kAccent = 1.4, kAccentSec = 0.03;  // the tongued attack's accent on the breath
   static constexpr double kFamilyDb = 12.0;
   static constexpr int kRegPoints = 11;
   static constexpr double kRegDb[kRegPoints] = {8.0, 9.0, 10.0, 10.0, 8.3, -11.2, 15.0, 10.3, 4.2, 2.6, -15.0};
   // (and played softly, as much again: the real alto's piano is as even up its range as its forte)
   static constexpr double kRegSoftDb[kRegPoints] = {1.4, 5.7, 5.7, 1.4, 0.0, -2.9, 1.4, -1.4, -7.1, -8.6, -8.6};
   static constexpr double kRegGainDb[kRegPoints] = {4.0, -0.5, -1.5, 1.5, 3.6, 13.2, -1.6, -1.2, 4.0, 4.4, 0.0};
   ModalBore bore_;
   OnePoleLP breathLP_;
   DCBlock dc_;
   Player player_;
   Biquad eqHp_, eqPk1_, eqPk2_, eqHs_, eqLp1_, eqLp2_, tone_, lift_, reg_, fam_;
   unsigned at_ = 0;
   bool ticked_ = false;
   int fing_ = -1;
   double fingMember_ = 1.0, fingerOff_ = 0.0;
   double bodyAt_ = -1.0, lowAt_ = -1.0, bodyQ_ = 1.0, bodyH_ = 1.0, edgeAt_ = -1e9, knee_ = 0.08;
   double lowNote_ = 0.0, peakHz_ = 277.0, hz_ = 277.0, member_ = 1.0, scale_ = 1.0, body_ = 1.0, vent_ = 0.0, open_ = 7e-4, eqAt_ = -1.0, toneDb_ = 1e9, toneHz_ = 1.0, liftDb_ = 1e9, liftHz_ = 1.0;
   double regDb_ = 1e9, regHz_ = 1.0, famDb_ = 1e9, famHz_ = 1.0, onT_ = 1.0, lift0_ = 0.0, xShut_ = 0.0, liftSec_ = 0.003;
   double x_ = 0, v_ = 0, dpPrev_ = 0, pmS_ = 0, pmB_ = 0, pmCoef_ = 0.01;
   float tongue_ = 0.0f, tnPrev_ = 0.0f, softGain_ = 1.0f, regGain_ = 1.0f, famGain_ = 1.0f;
   bool shut_ = false;
};

// Oboe: a double reed on a conical bore, as two digital waveguides: Benade's "cylindrical
// saxophone" (and Ollivier, Kergomard & Dalmont's idealized reed woodwinds), the recipe behind
// Scavone's and STK's conical bores, with a quasi-static double reed as in Guillemain 2004.
//
// A cone's resonances are a pipe's of the whole cone's length, its missing tip out to the apex
// included: every harmonic of the note, the octave the oboe overblows to. Seen from the reed, that
// is a pipe of the bore's own length in parallel with a short one as long as the missing tip, both
// open at their far ends: their resonances fall exactly on the note's harmonics. So the bore is two
// delay lines meeting at the reed: the long one a round trip to the open holes and back, inverted
// and lowpassed by the holes and walls; the short one the tip's, inverted and lowpassed above a few
// kHz (a real cone's tip is a mass of air, with no resonance of its own). The tip shunts the low
// resonances more than the high, so the first stands lower than the next few, as on a real oboe.
// (The tip is a fixed length on the low notes and no more than a share of the note's half
// wavelength above them, as a real oboe's octave key and cross fingerings keep its upper notes'
// resonances in line; with the tip too short the low notes flip to their octave.)
//
// The reed is quasi-static: the air through it goes as the square root of the drop across it, and
// it closes as that drop grows, softly over the last of its travel (the blades' tips meeting). Its
// own resonance sits far above the notes, so it follows the drop sample by sample; the flow and
// the pressure it makes are solved together each sample, by a few Newton steps on the drop's square
// root from the last sample's answer. No stiff spring to step and no table of the bore's resonances
// to sum: an oboe voice costs about a quarter of what the modal one did.
//
// What's heard is the flow out of the open holes and bell, radiated (differentiated), through the
// response fitted to the Philharmonia oboe, Bb3 to F6 piano, forte and fortissimo: a real oboe's
// fundamental lies under its 2nd to 5th harmonics, with a broad bump near 900 Hz and a formant
// near 1.8 kHz. Under the low Bb the horn is a bigger one of the family (cor anglais, bass oboe),
// its tip longer and its response lower.
class Oboe final : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      bore_.init(static_cast<int>(fs / 25.0) + 16);
      tip_.init(static_cast<int>(fs / 25.0) + 16);
      player_.prepare(fs);
      reset();
   }
   void reset() override {
      bore_.clear();
      tip_.clear();
      tipLp_ = {};
      lp1_ = {};
      lp2_ = {};
      s_ = pmS_ = pmB_ = flowPrev_ = 0.0;
      player_.reset();
      at_ = 0;
      ticked_ = false;
      pmCoef_ = 1.0 - std::exp(-1.0 / (0.004 * fs_));
      breathLP_ = {};
      breathLP_.setHz(2000.0, fs_);
      dc_ = {};
      dc_.setFs(fs_);
      eqHp_.clear();
      eqPk1_.clear();
      eqPk2_.clear();
      eqHs_.clear();
      eqLp1_.clear();
      eqLp2_.clear();
      tone_.clear();
      onT_ = 1.0;
      tnPrev_ = 0.0f;
      shut_ = false;
      gate_ = 1.0;
      toneDb_ = 1e9;
      eqAt_ = -1.0;
      notePitch(hz_);
   }
   double nominal(double hz) const override { return fs_ / hz; }
   double cutoffDelay(double) const override { return 0.0; }
   void notePitch(double hz) override {
      hz_ = hz;
      // Under the oboe's low Bb the horn is a bigger one of the family, the oboe's bore scaled up:
      // the cor anglais (a fifth down), the bass oboe (an octave), two lower still and the
      // contrabassoon (three octaves). (Without the contrabassoon the notes under the bassoon's low
      // Bb, played on its bore stretched, jumped up an octave blown hard.)
      // (The switch is a quarter tone under each one's bottom note, so the player's drift and
      // vibrato don't flip a note between two horns.)
      // (Chosen for the note without the player's bend, so a bend doesn't swap the horn under it.)
      const double fh = fingerHz(hz);
      member_ = 1.0;
      for (double s : {0.6674, 0.5, 0.3337, 0.25, 0.125})
         if (fh < kQuarterDown * kLow * member_)
            member_ = s;
      // (Once the note is playing, the bore follows the length it's played at, which the tuning
      // table has corrected, from the next sample: set here from the uncorrected pitch, it
      // flipped between two sizes every 16 samples, a buzz of sidebands and, each time the
      // bell's wave was resampled for the other size, many times the CPU.)
      if (ticked_)
         at_ = 0;
      else
         setBore(hz);
   }
   float tick(float breath, float emb, double len, float tn) override {
      player_.tongued(tn);
      // (No scoop up to the note: a real oboe's tongued notes start on their pitch, Philharmonia.)
      const double hz = fs_ / len * std::exp2(player_.wander(rng) / 1200.0);
      // A low note tongued from rest (Bb3 to E4): the pressure built up behind the tongue is let go
      // with the reed, an accent of kAccentLow that fades over kAccentSec, as a player's low notes
      // start (a real oboe's notes all reach full level in about a tenth of a second).
      onT_ += 1.0 / fs_;
      // (The tongue's burst rises smoothly with the air, so a note's tonguing starts where it
      // passes a small threshold; only from rest: a note tongued over one still sounding starts at
      // once.)
      const bool tongued = tn > 0.02f && tnPrev_ <= 0.02f;
      const bool fromRest = tongued && pmS_ < 0.05;
      if (fromRest) {
         onT_ = 0.0;
         shut_ = true;
      }
      tnPrev_ = tn;
      pmB_ += (breath - pmB_) * pmCoef_;
      // (Its log and exp only while the accent can sound: a low note, blown hard, on a firm reed.)
      if (const double acc = kAccentLow * std::clamp((breath - kAccentFrom) / 0.25, 0.0, 1.0) * std::clamp(2.0 * character.stiffness, 0.0, 1.0);
          acc > 0.0 && member_ >= 1.0 && hz_ < 330.0) {
         const double lowF = std::clamp(std::log2(330.0 / hz_) / 0.5, 0.0, 1.0);
         breath = static_cast<float>(breath * (1.0 + acc * lowF * std::exp(-onT_ / kAccentSec)));
      }
      pmS_ += (breath - pmS_) * pmCoef_;
      ticked_ = true;
      if ((at_++ & 15) == 0)
         setBore(hz);
      // The mouth's pressure, over the pressure that shuts the reed (gamma): the breath, its
      // turbulence, the tongue's burst and (with the Growl knob up, blown hard) the throat's rasp.
      // A stiffer reed needs more of it; a firmer embouchure presses the reed nearer shut.
      const float noise = breathLP_.tick(rng.white());
      const float growl = std::max(0.0f, breath - 0.95f) * 0.6f * character.edge * (character.growl - 0.6f);
      const double stiff = (0.9 + 0.2 * character.stiffness) * (1.1 - 0.2 * emb);
      // (Rising quickly to near where the reed starts to sound, then more slowly on to forte, so the
      // breath's range is the reed's playing range: a real reed speaks at a third of full breath.)
      const double air = pmS_ < kSpeakAt ? kGammaSpeak * pmS_ / kSpeakAt : kGammaSpeak + (kGamma - kGammaSpeak) * (pmS_ - kSpeakAt) / (1.0 - kSpeakAt);
      const double gamma = air / stiff * (1.0 + (0.01 * character.noise + growl) * noise) + 0.05 * tn * noise;
      // The reed's slit at rest (zeta): the Aperture knob, the embouchure (a looser lip lets it
      // stand wider) and, blown softly, the lip closing on it with the breath (a player's piano).
      const double lip = std::clamp(kLip0 + (1.0 - kLip0) * (pmS_ - kSpeakAt) / (1.0 - kSpeakAt), kLip0, 1.2);
      // (So the mouthpiece's pressure, and the sound, scale with the pressure that shuts the reed,
      // which the lip sets: blown softly, the lip closes the reed, and the note is quieter.)
      level_ = std::clamp(kLevel0 + (1.0 - kLevel0) * (pmS_ - kSpeakAt) / (1.0 - kSpeakAt), kLevel0, 1.2);
      // (The tongue holds the reed shut against the lay while the mouth's pressure builds behind
      // it, and lets go once it's nearly there, over a few ms: the note starts on the wave that
      // release makes. A soft note's tongue comes away more gently.)
      if (shut_ && pmS_ >= 0.97 * breath) {
         shut_ = false;
         liftSec_ = 0.008 + 0.007 * std::clamp((0.7 - breath) / 0.25, 0.0, 1.0);
      }
      // (Closing over 2 ms: shut at once, the flow still through it from a note's tail would stop
      // with a click.)
      if (shut_)
         gate_ = std::max(0.0, gate_ - 1.0 / (0.002 * fs_));
      else if (gate_ < 1.0)
         gate_ = std::min(1.0, gate_ + 1.0 / (liftSec_ * fs_));
      const double zeta = kZeta * lip * (0.5 + character.aperture) * (1.2 - 0.4 * emb) * gate_ * gate_;
      // The tips' curl, over which the slit closes softly (shorter with more bite).
      if (character.edge != edgeAt_) {
         edgeAt_ = character.edge;
         kn_ = 0.05 * std::exp(-1.2 * (character.edge - 1.0));
      }
      const double kn = kn_;
      // The bore: the waves back from the holes and from the missing tip (each inverted and
      // lowpassed). The long pipe's round trip is the whole cone's, less the tip's and the filters'
      // delay (by kLpPitch of it: so the Hole Cutoff knob leaves the pitch near where it was).
      const double N = std::max(2.0, fs_ / hz - tipSamples_ - lpDelay_);
      const double in = bore_.read(N);
      const double back = -refl_ * lp2_.tick(lp1_.tick(static_cast<float>(in)));
      const double tipBack = -kTipRefl * tipLp_.tick(tip_.read(std::max(1.0, tipSamples_)));
      // At the reed, the two meet with the flow (each of impedance 1): p = c u + B.
      const double c = 0.5, B = back + tipBack, D = gamma - B, cz = c * zeta;
      // The reed's flow, u = zeta open(W) sgn(W) sqrt|W|, with W = gamma - p the drop across it
      // (over the drop that shuts it) and open(W) its slit, closing softly at W = 1. Solved in
      // s = sgn(W) sqrt|W|, where it's smooth: h(s) = W + c u - D = 0.
      const auto slit = [kn](double W, double &dOpen) {
         const double gap = 1.0 - W;
         if (gap >= kn) {
            dOpen = -1.0;
            return gap;
         }
         if (gap > -kn) {
            dOpen = -(gap + kn) / (2.0 * kn);
            return (gap + kn) * (gap + kn) / (4.0 * kn);
         }
         dOpen = 0.0;
         return 0.0;
      };
      const auto h = [&](double s, double &dh) {
         const double a = std::fabs(s), W = s * a;
         double dOpen;
         const double open = slit(W, dOpen);
         dh = 2.0 * a + cz * (open + 2.0 * s * a * dOpen);
         return W + cz * open * s - D;
      };
      double s = s_;
      bool solved = false;
      for (int it = 0; it < 4 && !solved; ++it) {
         double dh;
         const double v = h(s, dh);
         if (dh <= 1e-9)
            break;
         const double step = v / dh;
         s -= step;
         solved = std::fabs(step) < 1e-7;
      }
      // (Where Newton's steps don't settle, as the reed snaps shut or open, by bisection: h rises
      // through its root whatever the reed does there.)
      if (!solved) {
         double lo = -2.0, hi = 2.0, dh;
         for (int it = 0; it < 30; ++it) {
            const double mid = 0.5 * (lo + hi);
            (h(mid, dh) > 0.0 ? hi : lo) = mid;
         }
         s = 0.5 * (lo + hi);
      }
      s_ = s;
      double dOpen;
      const double u = zeta * slit(s * std::fabs(s), dOpen) * s, p = c * u + B;
      bore_.push(static_cast<float>(p - back));
      tip_.push(static_cast<float>(p - tipBack));
      // Radiated: the flow out of the holes and bell (the wave arriving and the one leaving).
      const double flow = in - back / refl_;
      const double o = (flow - flowPrev_) * (fs_ / 96000.0);
      flowPrev_ = flow;
      const float y = dc_.tick(static_cast<float>(o * outGain_ * level_));
      return static_cast<float>(tone_.tickd(eqLp2_.tickd(eqLp1_.tickd(eqHs_.tickd(eqPk2_.tickd(eqPk1_.tickd(eqHp_.tickd(y)))))))) * softGain_;
   }

private:
   void setBore(double hz) {
      // The missing tip: kTip on the oboe, longer on a bigger family member, and no more than
      // kTipMax of the note's half wavelength. The Cone knob scales it: a longer tip, more of a
      // cone, speaks more easily; a shorter one is nearer a cylinder (at the knob's bottom the low
      // notes blown hard can flip to their octave).
      const double taper = std::exp2(kConeDepth * (character.cone - 0.5));
      const double tip = taper * std::min(kTip / member_, kTipMax * kC / (2.0 * hz));
      tipSamples_ = 2.0 * tip / kC * fs_;
      // (The radiated level climbs steeply with the note; most of that comes back out here, so the
      // tuning table's levelling gains stay near 1.)
      outGain_ = kOut * std::pow(440.0 / hz, kOutTilt);
      tipLp_.setHz(kTipCut, fs_);
      // The holes' and walls' loss: two one-pole lowpasses at kCut times the note (at least kCutMin,
      // which damps a low note's upper resonances, as a player voices it: without that it flips
      // to its octave), which Hole Cutoff moves; the Loss knob takes more of each trip.
      const double cut = character.cut(std::max(kCutMin * member_, kCut * hz), 1.5 * hz);
      lp1_.setHz(cut, fs_);
      lp2_.setHz(cut, fs_);
      const double a = lp1_.a, w = 2.0 * kPi * hz / fs_;
      const double ta = tipLp_.a;
      lpDelay_ = kLpPitch * 2.0 * a * (std::cos(w) - a) / (1.0 - 2.0 * a * std::cos(w) + a * a) +
                 ta * (std::cos(w) - ta) / (1.0 - 2.0 * ta * std::cos(w) + ta * ta);
      refl_ = character.keep(static_cast<float>(kRefl));
      // What's heard, as a real oboe's in front of it (fitted to the Philharmonia oboe's harmonics,
      // Bb3 to F6): the fundamental highpassed under the harmonics, a broad bump near 900 Hz, the
      // formant near 1.8 kHz, a shelf over 1.4 kHz and the top falling away past 4 kHz (scaled down
      // with a bigger family member, the formant and shelf by less: they come from the reed).
      const double fam = member_;
      const double up = std::pow(fam, 0.3);
      if (std::fabs(fam - eqAt_) > 1e-3) {
         eqAt_ = fam;
         eqHp_.highpass(kHpHz * fam, kHpQ, fs_);
         eqPk1_.peak(kPk1Hz * fam, kPk1Q, kPk1Db, fs_);
         eqPk2_.peak(kPk2Hz * up, kPk2Q, kPk2Db, fs_);
         eqHs_.highshelf(std::min(kHsHz * up, 0.4 * fs_), kHsDb, fs_);
         const double upLp = std::pow(fam, 1.3);
         eqLp1_.lowpass(std::min(kTopHz * upLp, 0.4 * fs_), 0.54, fs_);
         eqLp2_.lowpass(std::min(kTopHz * upLp, 0.4 * fs_), 1.31, fs_);
      }
      // Blown softly the harmonics are shelved down as the breath eases under mezzo forte, as a
      // real oboe's piano is darker.
      const double soft = std::clamp((kSoftAt - pmB_) / 0.2, 0.0, 1.0);
      const double toneDb = kSoftDb * soft;
      const double toneHz = std::min(kSoftHz * hz, 0.4 * fs_);
      if (std::fabs(toneDb - toneDb_) > 0.05 || std::fabs(toneHz / toneHz_ - 1.0) > 0.01) {
         toneDb_ = toneDb;
         toneHz_ = toneHz;
         tone_.highshelf(toneHz, toneDb, fs_);
      }
      softGain_ = static_cast<float>(std::pow(10.0, kSoftGain * soft / 20.0));
   }

   static constexpr double kLow = 233.08, kC = 343.0, kQuarterDown = 0.97153;
   static constexpr double kTip = 0.24, kTipMax = 0.36, kConeDepth = 0.5, kTipRefl = 0.995, kTipCut = 3000.0;
   static constexpr double kCut = 2.0, kCutMin = 400.0, kRefl = 0.985, kLpPitch = 1.3;
   static constexpr double kGamma = 0.8, kSpeakAt = 0.25, kGammaSpeak = 0.49, kZeta = 0.8, kLip0 = 0.6;
   static constexpr double kOut = 1.0, kOutTilt = 1.6, kLevel0 = 0.12;
   static constexpr double kHpHz = 204, kHpQ = 0.86, kPk1Hz = 924, kPk1Q = 0.30, kPk1Db = 11.0, kPk2Hz = 1827,
                           kPk2Q = 1.07, kPk2Db = 16.9, kHsHz = 1404, kHsDb = 3.2, kTopHz = 4059;
   static constexpr double kSoftAt = 0.7, kSoftDb = -7.2, kSoftHz = 3.00, kSoftGain = 4.0;
   static constexpr double kAccentSec = 0.1, kAccentFrom = 0.15, kAccentLow = 2.0;
   Delay bore_, tip_;
   OnePoleLP lp1_, lp2_, tipLp_, breathLP_;
   DCBlock dc_;
   Player player_;
   Biquad eqHp_, eqPk1_, eqPk2_, eqHs_, eqLp1_, eqLp2_, tone_;
   unsigned at_ = 0;
   bool ticked_ = false;
   double hz_ = 277.0, member_ = 1.0, eqAt_ = -1.0, toneDb_ = 1e9, toneHz_ = 1.0;
   double tipSamples_ = 0.0, outGain_ = 1.0, level_ = 1.0, lpDelay_ = 0.0, refl_ = kRefl;
   double s_ = 0.0, flowPrev_ = 0.0, gate_ = 1.0, liftSec_ = 0.01;
   double edgeAt_ = -1e9, kn_ = 0.05;
   double onT_ = 1.0, pmS_ = 0.0, pmB_ = 0.0, pmCoef_ = 0.01;
   float tnPrev_ = 0.0f, softGain_ = 1.0f;
   bool shut_ = false;
};

// Trumpet: a Bb trumpet's own air column, as its resonances (bore.h, baked from the horn's
// geometry by tools/bore.py: a mouthpiece cup and throat, a leadpipe, the cylinder through the
// valves and a Bessel-horn bell), with the lips buzzing on it.
//
// The lips are a mass on a spring, pushed open by the mouth's pressure over the mouthpiece's (an
// outward-striking valve, the "elementary" brass model: Elliott & Bowsher 1982, Velut et al. 2017),
// tuned under the note they play; the air through them goes as the square root of the drop across
// them, and they close softly (tissue meets tissue over a little travel, not at a corner). They
// buzz on the horn's resonance just over them: the horn's resonances are where its notes are, and
// as on a real trumpet each note is a partial of one of its seven valve combinations, picked as a
// player would (the valves that put a resonance nearest the note), with the lips tightening into
// it. The horn's resonances aren't quite harmonic, as a real one's aren't, and the higher ones
// fade above the bell's cutoff near 1.5 kHz: so a note speaks and slots as a trumpet's does, and
// its tone grows brighter the harder it's blown as the lips close for more of each cycle.
//
// What's heard is the bell's: the horn's flow at its rim, radiated (differentiated, and beamed on
// its axis as the bell's rim nears the wavelength), with the wave that passes straight out of the
// bell above its cutoff, through the response that the geometry alone misses of a real trumpet
// heard in front of it (fitted to the Iowa recordings). At forte the wave steepens on its way
// down the tube: the brassy edge.
class Trumpet final : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      shock_.init(static_cast<int>(1024 * fs / 96000.0) + 8);
      player_.prepare(fs);
      reset();
   }
   void reset() override {
      bore_.reset();
      shock_.clear();
      x_ = v_ = dpPrev_ = closed_ = puff_ = puffAt_ = puffTn_ = kickTn_ = 0.0;
      tongue_ = 0.0f;
      player_.reset();
      at_ = 0;
      ticked_ = false;
      shockD_ = kShock0 * fs_ / 96000.0;
      acMean_ = acEnv_ = 0.0;
      acLP_ = {};
      acLP2_ = {};
      acCoef_ = 1.0 - std::exp(-2.0 * kPi * 25.0 / fs_);
      envFall_ = std::exp(-1.0 / (0.03 * fs_));
      closedCoef_ = 1.0 - std::exp(-1.0 / (0.004 * fs_));
      pmCoef_ = 1.0 - std::exp(-1.0 / (0.008 * fs_));
      pmS_ = 0.0;
      breathLP_ = {};
      breathLP_.setHz(1500.0, fs_);
      breathLP2_ = {};
      breathLP2_.setHz(1500.0, fs_);
      dc_ = {};
      dc_.setFs(fs_);
      dcOut_ = {};
      dcOut_.setFs(fs_);
      // What the horn's shape alone misses of a real trumpet heard in front of it: the Iowa
      // trumpet's harmonics (C4 to B4, pp and mf) over this model's, fitted as one response. Its
      // tone peaks near 1.2 kHz, some 30 dB over 200 Hz, and falls away above 2 kHz.
      eqHp_.clear();
      eqHp_.highpass(1144.0, 1.06, fs_);
      eqPk_.clear();
      eqPk_.peak(1257.0, 1.39, 11.9, fs_);
      eqLp_.clear();
      eqLp_.lowpass(3132.0, 0.1, fs_);
      eqAt_ = 1.0;
      fing_ = -1;
      notePitch(hz_);
   }
   double nominal(double hz) const override { return fs_ / hz; }
   double cutoffDelay(double) const override { return 0.0; }
   double tuningOffset() const override { return 12.0 * std::log2(hz_ / fingerHz(hz_)); }
   void notePitch(double hz) override {
      hz_ = hz;
      acLP_.setHz(hz * 1.2, fs_);
      acLP2_.setHz(hz * 1.2, fs_);
      // The fingering: the valves and partial whose resonance is nearest the note, a semitone of
      // valves weighed as 8 cents (a player takes the open horn over a valve when it's in tune).
      // Under the lowest (all the valves' second partial) or over the top, the horn is scaled to
      // the note: a bigger or a smaller trumpet.
      // (Chosen for the note without the player's bend, which the lips make on the valves the note
      // was played with: re-fingered as a bend crossed each semitone, the lips lost the note and
      // fell to another partial, or stopped, for a tenth of a second or more.)
      const double fh = fingerHz(hz);
      int fv = 0, fp = 1;
      double best = 1e9;
      // (In cents from each resonance, against a table of their pitches made once: a log for each
      // of the hundred resonances, every control block, was most of the trumpet's control cost.)
      static const ResonanceCents cents;
      const double fc = 1200.0 * std::log2(fh);
      const auto costOf = [fc](int v, int p) { return std::fabs(fc - cents.at[v][p]) + 8.0 * v; };
      for (int v = 0; v < 7; ++v) {
         const BoreFingering &b = kTrumpetBore[v];
         for (int p = 1; p < b.modes && b.peakHz[p] < 1300.0f; ++p) {
            const double cost = costOf(v, p);
            if (cost < best) {
               best = cost;
               fv = v;
               fp = p;
            }
         }
      }
      // (Once the note is sounding, the valves held stay until another fingering is a quarter tone
      // nearer the note, so a note between two, or a bend past kLipBendMax, doesn't flip.)
      if (ticked_ && (fv != fing_ || fp != partial_) && costOf(fing_, partial_) < best + 50.0) {
         fv = fing_;
         fp = partial_;
      }
      if (fv != fing_ || fp != partial_) {
         fing_ = fv;
         partial_ = fp;
      }
      peakHz_ = kTrumpetBore[fing_].peakHz[partial_];
      // The lips' own frequency under the note's: slack on the low notes, nearer it as they
      // tighten for the high ones (where the horn's resonances crowd together and slack lips
      // would slip down to the one below).
      // (Through the fifth octave, from about Eb5 to A5, the resonances are close enough that lips
      // tuned any nearer slip to the one above.)
      lipRatio_ = std::clamp(0.84 + 0.06 * std::log2(hz / 450.0), 0.76, 0.92);
      if (hz < 900.0)
         lipRatio_ = std::min(lipRatio_, 0.86);
      // Lips heavier under C3, as a low brass player's are (lighter lips would be blown wide open
      // and stop buzzing).
      lipMu_ = kLipMu * std::max(1.0, std::pow(130.0 / hz, 1.5));
      // (Once the note is playing, the bore follows the length it's played at, which the tuning
      // table has corrected, from the next sample: set here from the uncorrected pitch, it
      // flipped between two sizes every 16 samples, a buzz of sidebands and, each time the
      // bell's wave was resampled for the other size, many times the CPU.)
      if (ticked_)
         at_ = 0;
      else
         setBore(hz);
   }
   float tick(float breath, float emb, double len, float tn) override {
      // How hard it's blown, 0 where the lips start to 1 at full breath.
      const double dyn = std::clamp((breath - 0.15) / 0.4, 0.0, 1.3);
      // The player: a slow wander, and every tongued note starting under its pitch and rising into
      // it as the lips tighten onto the horn's resonance (Iowa trumpet: 40 to 130 cents flat at the
      // onset, settling in 50 to 80 ms; more and slower on soft and low notes, quick at forte, and
      // little from C5 up). Its clock runs slowly until the lips close on every cycle.
      player_.tongued(tn);
      const double dynTop = std::min(dyn, 1.0);
      const double depth = kScoop * std::clamp(1.2 - (hz_ - 260.0) / 300.0, 0.1, 1.2) * (1.3 - 0.75 * dynTop);
      const double cents = player_.wander(rng) - depth * player_.scoop(0.015 + 0.03 * (1.0 - dynTop),
                                                                      0.25 + 0.75 * std::min(1.0, closed_ / 0.3));
      // The note the lips are asked for, and the horn under it (a little longer while it scoops).
      const double hz = fs_ / len * std::exp2(cents / 1200.0);
      ticked_ = true;
      if ((at_++ & 15) == 0)
         setBore(hz);
      // Mouth pressure: the breath (5 kPa at 1 on the trumpet's Bb4, more for the notes above it and
      // less under it, as a player's), its turbulence (a rush under a couple of kHz in the cup) and
      // the tongue's release.
      // (The mouth is a volume of air behind the lips: its pressure follows the breath over a few
      // milliseconds, so a hard tongue's surge doesn't slam the lips harder than the note asks.)
      pmS_ += (breath - pmS_) * pmCoef_;
      const double pm = kP0 * (hz_ / 466.0) * pmS_ * (1 + 0.02 * tn * rng.white()) *
                        (1 + 0.05 * character.noise * breathLP2_.tick(breathLP_.tick(rng.white())));
      // The lips: tuned under the note (tighter with the embouchure), their damping and mass from
      // the Stiffness, pushed open by the drop across them.
      // (Blown harder, a player's lips sit lower under the note: pressed tighter against the drop they'd
      // jump to the resonance above.)
      // (Through the fifth octave, from about Eb5, they ease off further: the resonances there are
      // close, and lips held tight under the drop would buzz on their own, off the horn.)
      const double loose = 0.10 + 0.08 * std::clamp((hz_ - 550.0) / 60.0, 0.0, 1.0);
      const double lip = std::max(0.72, lipRatio_ - loose * std::clamp((1000.0 - hz_) / 250.0, 0.0, 1.0) * (pmS_ - 0.25));
      const double wl = 2 * kPi * hz * (lip + 0.1 * (emb - 0.5));
      // (The lips' Q only moves with the Stiffness: worked out again only when it does.)
      if (character.stiffness != qStiffness_) {
         qStiffness_ = character.stiffness;
         lipQ_ = kLipQ * std::exp2(lipStiffness() - 0.5);
      }
      const double q = lipQ_;
      const double acc = -wl * wl * x_ - (wl / q) * v_ + dpPrev_ / lipMu_ + 1e-6 * rng.white();
      v_ += acc / fs_;
      // The tongue's release plucks the lips open, so the note starts at once, as hard as tongued
      // (up to a middling strike: harder is an accent in the breath, not a harder pluck).
      if (tn > tongue_) {
         const double d = std::min<double>(tn - tongue_, std::max(0.0, kHardest - kickTn_));
         kickTn_ += tn - tongue_;
         v_ += kKick * d * hz;
      } else if (tn <= 0.0f)
         kickTn_ = 0.0;
      // ... and the air it held back bursts into the mouthpiece: a puff a period long, which sets
      // the horn's resonance under the note ringing, so the lips lock onto it at once.
      if (tn > tongue_) {
         puffTn_ += tn - tongue_;
         puffAt_ = 0.0;
      } else if (puffTn_ > 0.0) {
         // (Sized once the tongue is away: as hard as it struck, up to a middling strike; by the
         // breath behind it then, as its square, so a soft note's is small against the note, up to
         // a moderate breath; and larger over Bb4, where the lips need more to lock on.)
         const double pb = std::clamp<double>(breath, 0.05, 0.3) / 0.5, up = std::max(hz_ / 466.0, 1.0);
         // (and four times as much from Eb5 to G#5, where the lips, eased off, take longest to catch).
         const double zone = std::clamp((hz_ - 560.0) / 60.0, 0.0, 1.0) * std::clamp((820.0 - hz_) / 60.0, 0.0, 1.0);
         puff_ += kPuff * std::min(puffTn_, kHardest) * pb * pb * up * up * (1.0 + 3.0 * zone);
         puffTn_ = 0.0;
      }
      tongue_ = tn;
      x_ += v_ / fs_;
      closed_ += ((x_ <= 0.0 ? 1.0 : 0.0) - closed_) * closedCoef_;
      // (They close over a tenth of a millimetre.)
      const double h = 0.5 * (x_ + std::sqrt(x_ * x_ + kSoft * kSoft));
      const double a = h * kLipWidth * (0.5 + character.aperture) * std::sqrt(2.0 / kRho);
      // The flow, solved with the pressure it makes in the mouthpiece:
      // u = a sgn(D) sqrt|D - Zi u|, D the drop before this sample's flow.
      const double ph = bore_.history(), zi = bore_.instant(), D0 = pm - ph;
      const double um = 0.5 * (-a * a * zi + std::sqrt(a * a * a * a * zi * zi + 4.0 * a * a * std::fabs(D0)));
      double u = D0 >= 0 ? um : -um;
      if (puff_ > 0.0) {
         const double sn = std::sin(kPi * puffAt_);
         u += puff_ * sn * sn;
         puffAt_ += hz / fs_;
         if (puffAt_ >= 1.0)
            puff_ = 0.0;
      }
      bore_.push(u);
      dpPrev_ = pm - (ph + zi * u);
      const double y = dc_.tick(static_cast<float>(eqLp_.tickd(eqPk_.tickd(eqHp_.tickd(static_cast<float>(bore_.radiated() * kOut))))));
      // Louder pressure travels faster down the horn, so the outgoing wave steepens towards a shock:
      // a read delay that follows the wave. How far it steepens (sigma, the delay's steepest
      // slope: 1 is a shock) grows with the breath, so a trumpet only blares when it's pushed; the
      // wave's own level is divided out, so every note and setting steepens alike.
      shock_.push(static_cast<float>(y));
      const double k = fs_ / 96000.0;
      acMean_ += (y - acMean_) * acCoef_;
      const double ac = acLP2_.tick(acLP_.tick(static_cast<float>(y - acMean_)));
      acEnv_ = std::max(std::fabs(ac), acEnv_ * envFall_);
      const double push = std::clamp((breath - 0.4) / 0.4, 0.0, 1.5);
      const double sigma = 1.2 * push * push * character.edge * (character.brass / 0.5);
      const double K = sigma * fs_ / (2.0 * kPi * hz_ * (acEnv_ + 1e-6));
      const double want = k * kShock0 - K * ac;
      shockD_ += std::clamp(std::clamp(want, 2.0, 2.0 * kShock0 * k) - shockD_, -0.95, 0.95);
      // (The steepening follows the wave's own level, so it rectifies a little of it: blown hard,
      // the horn's sound came out with a DC offset up to half its level, a thump at each note's end.)
      return dcOut_.tick(shock_.read(shockD_));
   }

private:
   // The horn for the note now: the fingering's resonances scaled so its partial sits on the note.
   // The Loss knob widens every resonance (a rougher, lossier tube), the Hole Cutoff moves the
   // bell's (its highs fade sooner or later), the Cone flares it (a lower cutoff, like a
   // flugelhorn's).
   void setBore(double hz) {
      const double loss = std::clamp(1.0 + 1.5 * (character.loss - 0.04), 0.75, 1.35);
      const double cut = kBellCut * character.cutoff * std::exp2(-(character.cone - 0.5));
      bore_.set(kTrumpetBore[fing_], hz / peakHz_, fs_, loss, kBellCut, cut);
      // What reaches the listener follows: the tone's peak and its fall above move with the bell's
      // cutoff, and a lossier tube is duller.
      const double r = cut / kBellCut / (1.0 + 1.5 * std::max(0.0, character.loss - 0.04));
      if (std::fabs(r - eqAt_) > 1e-3) {
         eqAt_ = r;
         eqPk_.peak(1257.0 * std::sqrt(r), 1.39, 11.9, fs_);
         eqLp_.lowpass(3132.0 * r, 0.1, fs_);
      }
   }
   // Stiffness under the middle softens the lips by half as much.
   float lipStiffness() const {
      const float st = character.stiffness;
      return st < 0.5f ? 0.5f - 0.5f * (0.5f - st) : st;
   }

   // Each valve combination's resonances in cents (1200 log2 of their Hz).
   struct ResonanceCents {
      double at[7][kBoreModes] = {};
      ResonanceCents() {
         for (int v = 0; v < 7; ++v)
            for (int p = 0; p < kTrumpetBore[v].modes; ++p)
               at[v][p] = 1200.0 * std::log2(static_cast<double>(kTrumpetBore[v].peakHz[p]));
      }
   };
   static constexpr double kP0 = 8000.0, kRho = 1.2, kLipQ = 12.0, kLipMu = 3.0, kLipWidth = 0.012,
                           kSoft = 1e-4, kOut = 1000.0, kBellCut = 1500.0, kKick = 5e-3, kPuff = 2e-3, kHardest = 0.4, kScoop = 100.0,
                           kShock0 = 480.0;
   ModalBore bore_;
   Delay shock_;
   OnePoleLP acLP_, acLP2_, breathLP_, breathLP2_;
   DCBlock dc_, dcOut_;
   Biquad eqHp_, eqPk_, eqLp_;
   Player player_;
   unsigned at_ = 0;
   bool ticked_ = false;
   int fing_ = -1, partial_ = 1;
   double peakHz_ = 466.0, hz_ = 466.0, lipRatio_ = 0.84, lipMu_ = 3.0, eqAt_ = 1.0, lipQ_ = kLipQ;
   float qStiffness_ = -1.0f;
   double acMean_ = 0, acEnv_ = 0, acCoef_ = 0.001, envFall_ = 0.9997, closedCoef_ = 0.003;
   double x_ = 0, v_ = 0, dpPrev_ = 0, closed_ = 0, shockD_ = 120, pmS_ = 0, pmCoef_ = 0.001, puff_ = 0, puffAt_ = 0, puffTn_ = 0, kickTn_ = 0;
   float tongue_ = 0.0f;
};

// Alien: a voice that sings words, a syllable a note, with a bowed string it can sing through.
//
// - The voice: a formant synthesizer (speech.h), folds buzzing through a throat of eight
//   resonances, with a nose and a mouth that make every consonant. Each note sings the next
//   syllable of its words (typed, or its own alien ones: see lyrics.h): the last one's closing
//   consonants, this one's opening ones, then its vowel, held as long as the note is and closed off
//   when it's let go. The breath is how loud and how hard it sings.
// - The throat can be another's: SHIFT makes it smaller or larger, SHARP narrows its resonances
//   until they ring, ACCENT bends each syllable's vowels somewhere no human mouth puts them, DRIFT
//   lets them wander. Breathy is the air through the folds.
// - The string: two waveguides either side of a bow (towards the bridge and towards the nut), each
//   end reflecting with its sign turned over, the bridge's through a lowpass (the string's losses,
//   which the Hole Cutoff sets). The bow drags the string with stick-slip friction: while the
//   string moves with it, they stick and the bow carries it; once the string's velocity falls too
//   far behind, it slips (the classic bow table: McIntyre, Schumacher & Woodhouse; Smith). The
//   breath is the bow's speed, the Bite its force, the Embouchure where it crosses the string, and
//   Cone how stiff the string is (its partials stretched apart, from gut to a bar). BOW is how much
//   of the voice's buzz the string takes over: all of it, and the string sings the words.
class Alien final : public Model {
public:
   void prepare(double fs) override {
      fs_ = fs;
      neck_.init(static_cast<int>(std::max(fs / 20.0, 2.0 * kMinLoop)) + 8);
      bridge_.init(static_cast<int>(std::max(fs / 20.0, 2.0 * kMinLoop)) + 8);
      rate_ = fs;
      control_ = std::max(1, static_cast<int>(fs / 3000.0));
      voice_.setRate(fs);
      letGoFall_ = std::exp(-1.0 / (0.25 * fs));
      onsetFall_ = std::exp(-1.0 / (0.12 * fs));
      powerK_ = 1.0 - std::exp(-1.0 / (0.25 * fs));
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
      edge_.z = mellow_.z = mellow2_.z = 0.0f;
      dc_ = {};
      dc_.setFs(fs_);
      voice_.reset();
      breathS_ = 0.0;
      wanderV_ = 0.0;
      for (int n = 0; n < 3; ++n)
         drift_[n] = accent_[n] = accentS_[n] = 0.0;
      count_ = 0;
      lifted_ = 1.0;
      letGo_ = false;
      held_ = 0.0;
      onset_ = 0.0;
      saying_ = -1;
      power_ = kSungPower;
      match_ = 1.0;
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
      // High notes as a singer sings them: from about E4 up (an octave, then two), the first
      // resonance rides above the note, the upper ones widen, and the top is rolled off, so a few
      // lone harmonics don't land on sharp peaks and whistle.
      high_ = std::clamp(std::log2(hz / 330.0), 0.0, 2.0);
      const double roll = high_ > 0.0 ? 9000.0 / (1.0 + 1.2 * high_) : 0.45 * fs_;
      mellow_.setHz(roll, fs_);
      mellow2_.setHz(roll, fs_);
      // (The tilt makes a high note's string far louder than its voice: brought back level.)
      sawLevel_ = static_cast<float>(1.0 / std::sqrt(1.0 + (hz / 300.0) * (hz / 300.0)));
   }
   // The syllable of its book it's singing, or -1.
   int saying() const override { return voice_.active() ? saying_ : -1; }
   uint32_t word() const override { return word_; }
   void setBook(const speech::Book *book) override { book_ = book; }
   // A note: the syllable at `word` in its book, its consonants as hard as the note was struck.
   void articulate(uint32_t word, double hardness) override {
      word_ = word;
      hardness_ = hardness;
      letGo_ = false;
      // (A word's first consonant is said as the note starts, not as the breath swells behind it.)
      // (Kick: from a soft start, its consonants at 0.4 of their strength, through as written at
      // the default, to a hard, punched one at twice.)
      const double kc = std::clamp(static_cast<double>(character.kick), 0.0, 1.0);
      const double kick = kc < 0.25 ? 0.4 + 2.4 * kc : 1.0 + (kc - 0.25) * 4.0 / 3.0;
      onset_ = (0.5 + 0.4 * hardness) * std::sqrt(std::min(kick, 1.0));
      say(word, (0.6 + 0.4 * hardness) * kick);
   }
   void letGo() override {
      if (letGo_)
         return;
      letGo_ = true;
      letGoLevel_ = level_;
      voice_.release();
   }
   bool ringing() const override { return voice_.active(); }
   float tick(float breath, float emb, double len, float tn) override {
      breath_ = breath;
      emb_ = emb;
      if (--count_ <= 0) {
         count_ = control_;
         shape();
      }
      const float bowMix = std::clamp(character.bow, 0.0f, 1.0f);
      float saw = 0.0f;
      const double down = std::clamp(breath / 0.12, 0.0, 1.0);
      if (bowMix > 0.0f) {
         const float scrape = static_cast<float>(rng.white());
         // The bow: its speed from the breath, roughened by the rosin (Noise) and the tongue's
         // bite. Where it crosses the string: nearer the bridge it needs a slower, heavier bow
         // (the bow's speed is scaled so a note keeps its one slip a period anywhere along it).
         const double beta = betaK_ + 0.11 * std::clamp(static_cast<double>(emb), 0.0, 1.0);
         // (Aperture is how wide the bow's hair lies on the string: more of it, more sound.)
         const double vb = vbK_ * beta / 0.125 * (0.7 + 0.6 * character.aperture) * std::max(breath, letGo_ ? 0.0f : 0.0f) *
                              (1.0 + 0.04 * character.noise * scrape) +
                           0.03 * tn * scrape;
         const double force = std::clamp(f0_ + 0.35 * (character.edge - 1.0) + f1_ * (breath - 0.8), 0.04, 0.95);
         // (Stiffness is the hair's tension and the rosin: a tauter, stickier bow grips and lets go
         // more sharply, brighter and more biting.)
         const double slope = (5.0 - 4.0 * force) * (0.8 + 0.4 * character.stiffness);
         // Lifted as the breath goes, so the string rings out; the player's hand mutes it.
         lifted_ += (down - lifted_) * 0.002;
         // (A hair longer: the string on its own sounds 3 cents sharp of its loop.)
         const double L = std::max(4.0, 1.0017 * len * steps_ - filterDelay_);
         const double lb = std::max(1.0, beta * L), ln = std::max(1.0, L - lb);
         const double keep = character.keep(0.995f) * (0.86 + 0.14 * lifted_);
         double sum = 0.0;
         for (int step = 0; step < steps_; ++step) {
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
         saw = (x + tilt_ * (x - x1_)) * sawLevel_;
         x1_ = x;
      }
      // How loud and how hard it sings: the breath (and, let go, the level it was let go at, dying
      // away while it says its closing consonants).
      const double push = std::clamp(static_cast<double>(breath), 0.0, 1.4);
      level_ = down * (0.35 + 0.65 * std::min(push, 1.0));
      onset_ *= onsetFall_;
      level_ = std::max(level_, onset_ * std::min(1.0, 4.0 * down));
      if (letGo_) {
         letGoLevel_ *= letGoFall_;
         level_ = std::max(level_, letGoLevel_);
      }
      // (Stiffness is the folds' tension: lax and soft, or pressed, buzzing and bright.)
      const double effort = std::clamp(0.25 + 0.55 * push + 0.2 * (character.stiffness - 0.5), 0.0, 1.0);
      const double breathy = std::clamp(0.8 * static_cast<double>(character.breathy) + 0.1 * (character.noise - 1.0), 0.0, 1.0);
      const double sung = voice_.tick(fs_ / std::max(len, 2.0), breathy, effort, saw, bowMix) * mouthGain_;
      // The bow's rosin edge, above the resonances, gets out round the throat.
      const float buzz = saw - edge_.tick(saw);
      // Held to one level whatever its throat's shape (measured on its vowels over the last quarter
      // second), so a strange throat plays as loud as a human one.
      if (voice_.vowelLevel() > 0.8) {
         power_ += powerK_ * (sung * sung - power_);
         const double want = std::clamp(std::sqrt(kSungPower / (power_ + 1e-12)), 0.3, 3.0);
         match_ += 20.0 * powerK_ * (want - match_);
      }
      float y = static_cast<float>(level_ * kVoice * match_ * sung) + kBuzz * bowMix * buzz;
      y = mellow2_.tick(mellow_.tick(y));
      return dc_.tick(kOut * y);
   }

private:
   static constexpr double kLoopCut = 14.0, kMinLoop = 240.0, kVoice = 0.12, kSungPower = 1.0;
   static constexpr int kMaxSteps = 12;
   static constexpr float kOut = 9.0f, kBuzz = 0.5f;
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

   // Sings the syllable at `at` in its book (nothing, with no book or an empty one).
   void say(uint32_t at, double strength) {
      held_ = 0.0;
      // (With no words, an "ah".)
      speech::Syllable s;
      if (book_ && book_->count > 0) {
         saying_ = static_cast<int>(at % static_cast<uint32_t>(book_->count));
         s = book_->syl[saying_];
      } else {
         static const int ah = speech::findPhone("AA");
         saying_ = -1;
         s.vowel = ah;
      }
      const double consonants = std::clamp(static_cast<double>(character.consonants), 0.0, 1.0);
      if (consonants < 0.05) {
         // (No consonants: only the vowels.)
         for (int k = 0; k < 4; ++k)
            s.onset[k] = s.coda[k] = -1;
      }
      // Its accent: each of the first three resonances somewhere of its own for this syllable.
      uint32_t h = (at + 1u) * 2654435761u;
      for (double &a : accent_) {
         h ^= h >> 15;
         h *= 2246822519u;
         h ^= h >> 13;
         a = (h & 0xffff) / 32768.0 - 1.0;
      }
      voice_.start(s, strength * (0.3 + 0.7 * consonants));
      // When a held note goes on to its next syllable: at the Pace's note value at the host's tempo,
      // or free (its own words only: typed ones keep to a syllable a note), every 1.6 s or so at
      // half Talk and 0.8 s at full, each a little longer or shorter than the last, as speech is.
      const double talk = std::clamp(static_cast<double>(character.talk), 0.2, 1.0);
      const double r = ((h >> 16) & 0xffff) / 65536.0;
      nextAt_ = character.paceSec > 0.0f ? character.paceSec : (2.4 - 1.6 * talk) * (0.75 + 0.5 * r);
   }

   // Every control_ samples: the throat's shape, and whether a held note says its next syllable.
   void shape() {
      if (character.cone != coneK_) {
         coneK_ = character.cone;
         notePitch(hz_);
      }
      const double dt = control_ / fs_;
      breathS_ += (breath_ - breathS_) * (1.0 - std::exp(-dt / 0.03));
      held_ += dt;
      if ((!character.typed || character.paceSec > 0.0f) && !letGo_ && breath_ > 0.06 && character.talk > 0.2f && held_ > nextAt_)
         say(++word_, 0.4 + 0.3 * std::min(breathS_, 1.0));
      // The drift: a slow random walk (about a second's memory) for each resonance.
      const double d = std::clamp(static_cast<double>(character.drift), 0.0, 1.0);
      const double leak = std::exp(-dt / 0.8);
      for (double &j : drift_)
         j = j * leak + std::sqrt(dt) * 0.5 * rng.white();
      const double a = std::clamp(static_cast<double>(character.vowel), 0.0, 1.0);
      // The wobble: each resonance swung by its own slow wave, faster and wider the more of it.
      const double wob = std::clamp(static_cast<double>(character.morph), 0.0, 1.0);
      static constexpr double kWobbleHz[3] = {1.0, 1.37, 1.83};
      const double ease = 1.0 - std::exp(-dt / 0.05);
      double warp[3];
      for (int n = 0; n < 3; ++n) {
         accentS_[n] += ease * (accent_[n] - accentS_[n]);
         wobble_[n] += dt * kWobbleHz[n] * (0.5 + 7.5 * wob * wob);
         wobble_[n] -= std::floor(wobble_[n]);
         // (An accent moves each resonance up to most of an octave; a drift, a few semitones; a
         // wobble, up to an octave either way.)
         warp[n] = std::exp2(0.8 * a * accentS_[n] * (n == 0 ? 0.6 : 1.0) + 0.25 * d * drift_[n] +
                             wob * std::sin(2.0 * kPi * wobble_[n]) * (n == 0 ? 0.4 : 1.0));
      }
      // The mouth, from the Exciter knobs: Embouchure opens the jaw (the first resonance up an
      // octave, wide open, or down one, nearly shut); Aperture spreads the lips (the upper
      // resonances up) or rounds and pushes them out (down, darker).
      embS_ += (emb_ - embS_) * ease;
      const double jaw = std::clamp(embS_ - 0.5, -0.5, 0.5);
      const double lips = std::clamp(static_cast<double>(character.aperture) - 0.5, -0.5, 0.5);
      warp[0] *= std::exp2(2.0 * jaw + 0.3 * lips);
      warp[1] *= std::exp2(0.5 * jaw + 1.0 * lips);
      warp[2] *= std::exp2(0.8 * lips);
      // Smaller or larger, following the note (as far as Follow sets: from middle C) and the bend
      // (as far as Bend sets).
      double size = std::exp2(std::clamp(static_cast<double>(character.shift), -1.0, 1.0));
      size *= std::pow(hz_ / 261.63, std::clamp(static_cast<double>(character.follow), 0.0, 1.0));
      if (character.bendSemis != 0.0f)
         size *= std::exp2(std::clamp(static_cast<double>(character.bendSemis), -48.0, 48.0) / 12.0);
      // (Loss damps the throat's walls: its resonances widen, up to about six times, muffled.)
      const double sharp = std::exp2(3.0 * (0.5 - std::clamp(static_cast<double>(character.sharp), 0.0, 1.0))) *
                           std::max(0.6, 1.0 + 10.0 * (static_cast<double>(character.loss) - 0.04));
      // (Bite: under the middle it sings more and more through its nose; over it, a brassy twang.)
      const double bite = std::clamp(static_cast<double>(character.edge) - 1.0, -1.0, 1.0);
      voice_.setVoicing(std::max(0.0, -bite), std::max(0.0, bite));

      // (An open jaw or spread lips let out more of the voice, a damped throat less: each held
      // level, so they change its colour and not how loud it is.)
      const double jawOct = 2.0 * jaw, damp = std::log2(std::max(0.6, 1.0 + 10.0 * (static_cast<double>(character.loss) - 0.04)));
      voice_.setTension(2.0 * (std::clamp(static_cast<double>(character.stiffness), 0.0, 1.0) - 0.5));
      const double tense = 2.0 * (std::clamp(static_cast<double>(character.stiffness), 0.0, 1.0) - 0.5);
      mouthGain_ = std::exp2(-(jawOct > 0.0 ? 2.26 : 0.83) * jawOct - 3.0 * std::max(lips, 0.0) + 0.5 * damp + 0.9 * std::max(-tense, 0.0) -
                             0.4 * std::max(tense, 0.0) - 1.0 * std::max(bite, 0.0));
      voice_.setShape(size, sharp, warp, std::min(high_, 1.0));
      voice_.setOrganic(character.rough, character.rasp, character.wet);
      voice_.setStrange(std::clamp(static_cast<double>(character.spread), -1.0, 1.0), std::clamp(static_cast<double>(character.flip), 0.0, 1.0));
      voice_.setGlide(std::clamp(static_cast<double>(character.glideMs) / 30.0, 0.2, 12.0));
      voice_.setDiphthongHold(40.0 + 360.0 * std::clamp(static_cast<double>(character.linger), 0.0, 1.0));
   }

   speech::Voice voice_;
   const speech::Book *book_ = nullptr;
   uint32_t word_ = 0;
   int saying_ = -1;
   double hardness_ = 0.5, level_ = 0.0, letGoLevel_ = 0.0, letGoFall_ = 0.99995, onset_ = 0.0, onsetFall_ = 0.9999;
   bool letGo_ = false;
   double accent_[3] = {}, accentS_[3] = {}, drift_[3] = {}, wobble_[3] = {0.0, 0.31, 0.67};
   double power_ = kSungPower, match_ = 1.0, powerK_ = 1e-4;

   Delay neck_, bridge_;
   OnePoleLP loop_;
   double breathS_ = 0.0, held_ = 0.0, nextAt_ = 1.0, wanderV_ = 0.0;
   float breath_ = 0.0f, emb_ = 0.5f;
   double embS_ = 0.5, mouthGain_ = 1.0;
   Allpass stiff_[2];
   float sawLevel_ = 1.0f, tilt_ = 0.0f, x1_ = 0.0f;
   OnePoleLP edge_, mellow_, mellow2_;
   double high_ = 0.0;
   DCBlock dc_, still_;
   double rate_ = 96000.0, filterDelay_ = 0.0, vbK_ = 0.1, f0_ = 0.4, f1_ = 0.2, betaK_ = 0.07;
   int steps_ = 1;
   double hz_ = 220.0, stiffA_ = 0.0, lifted_ = 1.0;
   float coneK_ = 0.5f;
   int control_ = 32, count_ = 0;
};

// How each instrument is played: its breath scale and offset (mouth pressure from the player's 0..1
// level), and the breath and embouchure its tuning table was measured at.
struct ModelInfo {
   const char *name;
   float breathScale, breathOffset;
   float tuneBreath;
   // The steady tone's level at the tuning breath, against the others': under 1 for a model whose
   // loud end and tongued attacks run well over it (the sax's hit the limiter, which distorted them).
   float level = 1.0f;
};

inline const ModelInfo &modelInfo(int id) {
   static const ModelInfo info[kModelCount] = {
      {"Flute", 0.8f, 0.15f, 0.75f},
      {"Clarinet", 0.42f, 0.5f, 0.815f},
      {"Sax", 0.6f, 0.5f, 0.85f, 0.6f},
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
   // (The Alien's bowed string holds its pitch whatever the bow's speed.)
   const Point *p = nullptr;
   int n = 0;
   switch (model) {
   case kFlute: p = flute; n = 9; break;
   case kClarinet: p = clarinet; n = 5; break;
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
