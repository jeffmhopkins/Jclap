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
};

// The player's settings a model reads, beyond breath and embouchure. Each is a multiplier around 1.
struct Character {
   float noise = 1.0f; // breath noise and turbulence
   float edge = 1.0f;  // the forte bite: the flute's 2nd harmonic, the reeds' growl, brass steepening
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
   void notePitch(double hz) override {
      hz_ = hz;
      lp_.setHz(hz * 3.2, fs_);
      jlp_.setHz(hz * 2.0, fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      const float U = std::sqrt(std::max(breath, 0.0f));
      const float raw = bore_.read(len);
      const float p = 0.97f * lp_.tick(raw);
      jet_.push(p);
      const double tau = std::clamp((0.33 + 0.1 * (emb - 0.5)) * len / std::max(U, 0.08f), 2.0, jet_.size() - 8.0);
      const float turb = nlp_.tick(rng.white());
      const float y = jlp_.tick(jet_.read(tau)) + (0.012f * character.noise + 0.12f * tn) * turb;
      // The jet's flow grows with pressure, not just its speed.
      const float q = 1.2f * U * U * std::tanh(8.0f * y + 0.3f);
      bore_.push(dc_.tick(p - 0.5f * q));
      // What leaves the embouchure hole and the open end: the tube wave, tilted up a little
      // (radiation), plus edge noise riding on the jet.
      const float rad = raw - 0.85f * rawPrev_;
      rawPrev_ = raw;
      // A flute's strong 2nd harmonic grows as it's blown harder: the jet's asymmetry, as a
      // level-normalised square term on the tube wave.
      envF_ += 0.002f * (96000.0f / static_cast<float>(fs_)) * (std::fabs(raw) - envF_);
      const float even = 0.3f * character.edge * (0.4f + 0.8f * breath) * raw * (raw / (envF_ + 1e-4f));
      return dco_.tick(0.2f * (0.5f * raw + rad + even) +
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
   // The bore's loss sits near 1.3 kHz, which only lets the low and middle register speak; it moves
   // up with notes above that, so they play too (as a real clarinet's tone holes leave more bore).
   void notePitch(double hz) override {
      hz_ = hz;
      // High notes need a firmer push to speak (a player bites and blows harder up there).
      lift_ = 1.0 + 0.2 * std::clamp(std::log2(hz / 440.0) / 0.8, 0.0, 1.0);
      const double cut = std::max(1300.0, 2.2 * hz);
      lp_.setHz(cut, fs_);
      lp2_.setHz(cut, fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      const float noise = noiseLP_.tick(rng.white());
      const float growl = std::max(0.0f, breath - 0.9f) * 0.1f * character.edge;
      const double g = 0.7 * lift_ * breath * (1 + (0.012f * character.noise + growl) * noise) + tn * 0.04f * noise;
      const double z = 0.35 * (0.8 + 0.4 * emb);
      const double inc = bore_.read(len);
      const double back = -0.96 * lp2_.tick(lp_.tick(static_cast<float>(inc)));
      const auto F = [g, z](double p) {
         // The reed closes softly (its tip curls onto the lay), not at a hard corner, which would
         // make a beating reed's tone rough and noisy.
         const double W = g - p, gap = 1 - W;
         if (gap <= -0.12)
            return 0.0;
         const double open = gap >= 0.12 ? gap : (gap + 0.12) * (gap + 0.12) / 0.48;
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
      lp_.setHz(cut_, fs_);
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
   // A split bore lets the second mode compete with the first, and on its own the loop jumps to the
   // octave on most notes. A gentle lift at the fundamental (a cone's wider first resonance, from
   // its missing apex) keeps it in the low register; on release the loop still dies away.
   void notePitch(double hz) override {
      if (hz == regHz_)
         return;
      regHz_ = hz_ = hz;
      register_.peak(hz, 0.7, 1.5, fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      const float noise = noiseLP_.tick(rng.white());
      const float growl = std::max(0.0f, breath - 0.95f) * growl_ * character.edge;
      const float p = breath * (1 + (breathNoise_ * character.noise + growl) * noise) + tn * tongueNoise_ * noise;
      const double pos = 0.2 + posEmb_ * (emb - 0.5);
      // The lift puts the loop's gain a little over one at the fundamental; the reed's losses hold it
      // there in normal playing, and the bore's walls (a clip far above any played level) at extremes.
      const float temp = std::clamp(-0.95f * register_.tick(lp_.tick(d0_.read(std::max(2.0, len * (1 - pos))))), -6.0f, 6.0f);
      out0_ = temp - d1_.read(std::max(2.0, len * pos));
      const float pd = p - out0_;
      const float r = std::clamp(0.7f + (stiff_ + stiffEmb_ * emb) * pd, -1.0f, 1.0f);
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
   float posEmb_ = 0.08f, stiff_ = 0.22f, stiffEmb_ = 0.14f;

private:
   Delay d0_, d1_;
   OnePoleLP lp_, noiseLP_;
   Biquad register_;
   DCBlock dc_;
   double hz_ = 294.0, regHz_ = 0.0;
   float out0_ = 0.0f;
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
      f1_.peak(1100, 1.6, 7.0, fs_);
      f2_ = {};
      f2_.peak(2900, 2.5, 5.0, fs_);
      f3_ = {};
      f3_.highshelf(400, -6.0, fs_);
   }
   float colour(float x) override { return f3_.tick(f2_.tick(f1_.tick(x))); }

private:
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
      shock_.init(static_cast<int>(256 * fs / 96000.0) + 8);
      reset();
   }
   void reset() override {
      bore_.clear();
      shock_.clear();
      bell_ = {};
      x_ = v_ = dcz_ = 0.0;
      shockD_ = 120.0 * fs_ / 96000.0;
      radHP_ = {};
      radHP_.highpass(1200, 0.7, fs_);
      radLP_ = {};
      radLP_.lowpass(9000, 0.7, fs_);
      notePitch(lipHz_);
   }
   double nominal(double hz) const override { return fs_ / hz; }
   void notePitch(double hz) override {
      lipHz_ = hz;
      bell_.setHz(hz * 8.0, fs_);
   }
   float tick(float breath, float emb, double len, float tn) override {
      const double dt = 1.0 / fs_, w = 2 * kPi * lipHz_ * (0.9 + 0.08 * (emb - 0.5));
      const double pm = breath * (1 + 0.02 * tn * rng.white());
      const double xin = bore_.read(len);
      dcz_ += (xin - dcz_) * (2 * kPi * 20 / fs_);
      const double pin = 0.985 * (bell_.tick(static_cast<float>(xin)) - 2.0 * dcz_);
      // Lips: pushed open by mouth pressure minus mouthpiece pressure (before this sample's flow).
      const double a = -w * w * x_ - (w / 4.0) * v_ + 0.25 * w * w * (pm - 2 * pin) + 1e-4 * rng.white();
      v_ += dt * a;
      x_ += dt * v_;
      const double h = std::max(x_, 0.0);
      // The flow through the slit, solved in closed form with the pressure it makes:
      // u = h sgn(D) sqrt|D - u|, D = pm - 2 pin.
      const double D = pm - 2 * pin;
      const double sq = 0.5 * (-h + std::sqrt(h * h + 4 * std::fabs(D)));
      const double u = (D >= 0 ? 1 : -1) * h * sq;
      const double pplus = pin + u;
      bore_.push(static_cast<float>(pplus));
      // Louder pressure travels faster down the horn, so the outgoing wave steepens towards a shock:
      // a read delay that follows the pressure, changing by less than a sample per sample.
      shock_.push(static_cast<float>(pplus));
      const double k = fs_ / 96000.0;
      const double want = k * (120.0 - 2.0 * character.edge * 40.0 * std::clamp(pplus, -1.5, 1.5));
      shockD_ += std::clamp(want - shockD_, -0.6, 0.6);
      // The bell passes highs and reflects lows.
      return radLP_.tick(radHP_.tick(shock_.read(shockD_))) * 3.0f;
   }

private:
   Delay bore_, shock_;
   OnePoleLP bell_;
   Biquad radHP_, radLP_;
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
      {"Trumpet", 0.6f, 0.2f, 0.65f},
   };
   return info[std::clamp(id, 0, kModelCount - 1)];
}

} // namespace jbreath
