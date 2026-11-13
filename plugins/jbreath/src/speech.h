#pragma once
// A singing voice that says words: a Klatt-style cascade-parallel formant synthesizer, driven a
// syllable at a time. The voiced sound and the aspiration go through the cascade (the vocal tract's
// resonances in a row, with a nasal pole and zero); frication and bursts go through a parallel bank
// shaped for each place in the mouth. Each syllable runs as a list of segments (closure, burst,
// aspiration, frication, murmur, the vowel held while the note is, the coda), each with its own
// targets for the formants and the sources, which the voice moves towards at that segment's pace.
// Written from Klatt (1980) "Software for a cascade/parallel formant synthesizer", Klatt & Klatt
// (1990) for the glottal source, Peterson & Barney (1952) and Hillenbrand (1995) for the vowels.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace jbreath::speech {

constexpr double kPi = 3.14159265358979323846;

enum Kind { kVowel, kGlide, kLiquid, kNasal, kFric, kStop, kAffricate, kAspirate };
enum Place { kNoPlace, kLabial, kLabioDental, kDental, kAlveolar, kPostAlveolar, kPalatal, kVelar, kGlottal };
// The frication's spectrum: the shape of the hiss or burst, set by where in the mouth it's made.
enum Spectrum { kFlat, kDentalHiss, kSibilant, kHush, kLabialBurst, kAlveolarBurst, kVelarBurst, kSpectrumCount };

struct Phone {
   const char *name;
   Kind kind;
   Place place;
   bool voiced;
   float f1, f2, f3, b1, b2, b3;
   float g1, g2, g3; // a diphthong's end (0: a pure vowel)
   float ms;         // its inherent length: a vowel's, a consonant's main sound
   float level;      // frication or burst level, dB against the vowel
   Spectrum spectrum;
};

// ARPAbet, as in the CMU dictionary. Formants and bandwidths for an adult (Klatt 1980, Table III,
// and Hillenbrand), the consonants' targets being where they leave the mouth.
inline const Phone kPhones[] = {
   // vowels
   {"IY", kVowel, kNoPlace, true, 300, 2250, 2950, 50, 120, 200, 0, 0, 0, 160, 0, kFlat},
   {"IH", kVowel, kNoPlace, true, 420, 1950, 2600, 55, 100, 160, 0, 0, 0, 110, 0, kFlat},
   {"EY", kVowel, kNoPlace, true, 480, 1950, 2600, 60, 100, 180, 330, 2200, 2800, 180, 0, kFlat},
   {"EH", kVowel, kNoPlace, true, 580, 1800, 2550, 60, 100, 180, 0, 0, 0, 130, 0, kFlat},
   {"AE", kVowel, kNoPlace, true, 680, 1700, 2450, 70, 120, 200, 0, 0, 0, 190, 0, kFlat},
   {"AA", kVowel, kNoPlace, true, 760, 1200, 2600, 90, 80, 160, 0, 0, 0, 190, 0, kFlat},
   {"AO", kVowel, kNoPlace, true, 620, 950, 2550, 80, 80, 140, 0, 0, 0, 180, 0, kFlat},
   {"OW", kVowel, kNoPlace, true, 530, 1000, 2450, 70, 80, 140, 400, 800, 2350, 170, 0, kFlat},
   {"UH", kVowel, kNoPlace, true, 460, 1150, 2400, 70, 90, 140, 0, 0, 0, 110, 0, kFlat},
   {"UW", kVowel, kNoPlace, true, 340, 1050, 2300, 60, 90, 140, 300, 850, 2250, 160, 0, kFlat},
   {"AH", kVowel, kNoPlace, true, 650, 1250, 2550, 80, 80, 140, 0, 0, 0, 120, 0, kFlat},
   {"AX", kVowel, kNoPlace, true, 520, 1450, 2500, 80, 100, 160, 0, 0, 0, 70, 0, kFlat},
   {"ER", kVowel, kNoPlace, true, 480, 1300, 1650, 80, 80, 110, 0, 0, 0, 170, 0, kFlat},
   {"AY", kVowel, kNoPlace, true, 730, 1250, 2550, 90, 80, 160, 380, 2050, 2650, 210, 0, kFlat},
   {"AW", kVowel, kNoPlace, true, 700, 1250, 2550, 90, 80, 160, 420, 900, 2350, 220, 0, kFlat},
   {"OY", kVowel, kNoPlace, true, 570, 900, 2450, 80, 80, 140, 380, 1950, 2600, 220, 0, kFlat},
   // glides and liquids
   {"W", kGlide, kLabial, true, 290, 650, 2200, 60, 90, 120, 0, 0, 0, 60, 0, kFlat},
   {"Y", kGlide, kPalatal, true, 270, 2200, 3050, 50, 160, 300, 0, 0, 0, 55, 0, kFlat},
   {"R", kLiquid, kPostAlveolar, true, 330, 1100, 1450, 70, 100, 120, 0, 0, 0, 65, 0, kFlat},
   {"L", kLiquid, kAlveolar, true, 360, 1050, 2800, 60, 120, 250, 0, 0, 0, 65, 0, kFlat},
   // nasals: the murmur's resonances (the mouth shut, the sound through the nose)
   {"M", kNasal, kLabial, true, 450, 1150, 2300, 150, 100, 200, 0, 0, 0, 70, 0, kFlat},
   {"N", kNasal, kAlveolar, true, 450, 1700, 2600, 150, 120, 200, 0, 0, 0, 60, 0, kFlat},
   {"NG", kNasal, kVelar, true, 450, 2100, 2900, 150, 120, 200, 0, 0, 0, 70, 0, kFlat},
   // fricatives
   {"F", kFric, kLabioDental, false, 350, 1100, 2100, 200, 120, 150, 0, 0, 0, 100, -18, kFlat},
   {"V", kFric, kLabioDental, true, 250, 1100, 2100, 80, 100, 130, 0, 0, 0, 65, -24, kFlat},
   {"TH", kFric, kDental, false, 350, 1400, 2600, 200, 100, 200, 0, 0, 0, 100, -24, kDentalHiss},
   {"DH", kFric, kDental, true, 270, 1400, 2600, 70, 90, 170, 0, 0, 0, 50, -25, kDentalHiss},
   {"S", kFric, kAlveolar, false, 350, 1600, 2600, 200, 100, 200, 0, 0, 0, 110, -11, kSibilant},
   {"Z", kFric, kAlveolar, true, 260, 1600, 2600, 70, 90, 180, 0, 0, 0, 80, -13, kSibilant},
   {"SH", kFric, kPostAlveolar, false, 350, 1900, 2500, 200, 120, 250, 0, 0, 0, 120, -11, kHush},
   {"ZH", kFric, kPostAlveolar, true, 270, 1900, 2500, 70, 100, 200, 0, 0, 0, 80, -12, kHush},
   {"HH", kAspirate, kGlottal, false, 0, 0, 0, 300, 120, 200, 0, 0, 0, 80, -13, kFlat},
   // stops: the closure's length, then the burst
   {"P", kStop, kLabial, false, 400, 1000, 2150, 300, 150, 220, 0, 0, 0, 70, -17, kLabialBurst},
   {"B", kStop, kLabial, true, 250, 1000, 2150, 60, 110, 130, 0, 0, 0, 60, -19, kLabialBurst},
   {"T", kStop, kAlveolar, false, 400, 1700, 2650, 300, 120, 250, 0, 0, 0, 60, -10, kAlveolarBurst},
   {"D", kStop, kAlveolar, true, 250, 1700, 2650, 60, 100, 170, 0, 0, 0, 50, -13, kAlveolarBurst},
   {"K", kStop, kVelar, false, 350, 1900, 2400, 250, 160, 330, 0, 0, 0, 70, -9, kVelarBurst},
   {"G", kStop, kVelar, true, 250, 1900, 2400, 60, 150, 280, 0, 0, 0, 60, -13, kVelarBurst},
   // affricates: a stop's closure, released into its fricative
   {"CH", kAffricate, kPostAlveolar, false, 350, 1900, 2500, 200, 120, 250, 0, 0, 0, 70, -9, kHush},
   {"JH", kAffricate, kPostAlveolar, true, 270, 1900, 2500, 70, 100, 200, 0, 0, 0, 55, -11, kHush},
};
constexpr int kPhoneCount = sizeof(kPhones) / sizeof(kPhones[0]);

inline int findPhone(std::string_view name) {
   for (int i = 0; i < kPhoneCount; ++i)
      if (name == kPhones[i].name)
         return i;
   return -1;
}

// Where the formants start as a consonant opens onto a vowel (or end as it closes off one): its
// locus, pulled towards the vowel (Sussman's locus equations: F2 onset = k F2 vowel + c by place).
inline void locus(const Phone &c, const double vowel[3], double out[3]) {
   // The first formant starts low under a closure and rises as the mouth opens: after a stop from
   // near where a shut mouth leaves it (250 to 350 Hz, whatever the vowel: the rise that says
   // "stop"), after a fricative from about half way, its narrow channel not quite shut.
   const bool closes = c.kind == kStop || c.kind == kAffricate;
   out[0] = std::min(vowel[0], closes ? 200.0 + 0.2 * vowel[0] : std::max(250.0, 0.5 * vowel[0] + 100.0));
   switch (c.place) {
   case kLabial:
   case kLabioDental:
      // (Shut lips lower every resonance: F2 and F3 both rise out of a b, p or m.)
      out[1] = 0.7 * vowel[1] + 150.0;
      out[2] = 0.82 * vowel[2];
      break;
   case kDental:
      out[1] = 0.5 * vowel[1] + 750.0;
      out[2] = 0.5 * vowel[2] + 1300.0;
      break;
   case kAlveolar:
      out[1] = 0.43 * vowel[1] + 1225.0; // (Sussman et al. 1991, /d/)
      out[2] = 0.4 * vowel[2] + 1650.0;
      break;
   case kPostAlveolar:
   case kPalatal:
      out[1] = 0.35 * vowel[1] + 1400.0;
      out[2] = 0.5 * vowel[2] + 1250.0;
      break;
   case kVelar: // the velar pinch: F2 and F3 close together, in front of a front vowel or a back one
      out[1] = vowel[1] > 1500.0 ? 0.9 * vowel[1] + 250.0 : 0.7 * vowel[1] + 650.0;
      out[2] = out[1] + 350.0;
      break;
   default:
      out[0] = vowel[0];
      out[1] = vowel[1];
      out[2] = vowel[2];
   }
   // A nasal opens all at once: its first formant is already near the vowel's (the abrupt jump
   // that tells it from an l, whose formants glide).
   if (c.kind == kNasal)
      out[0] = 0.85 * vowel[0];
}

// One syllable: up to four consonants before the vowel and after it.
struct Syllable {
   int onset[4] = {-1, -1, -1, -1};
   int vowel = -1;
   int coda[4] = {-1, -1, -1, -1};
   int stress = 1; // 0 unstressed, 1 primary, 2 secondary
   int onsetCount() const {
      int n = 0;
      while (n < 4 && onset[n] >= 0)
         ++n;
      return n;
   }
   int codaCount() const {
      int n = 0;
      while (n < 4 && coda[n] >= 0)
         ++n;
      return n;
   }
};

// Splits a run of ARPAbet phones (stress digits on the vowels) into syllables: each vowel takes
// the consonants before it that English lets start a syllable, the rest close the one before.
inline bool legalOnset(const int *c, int n) {
   if (n <= 1)
      return n == 0 || kPhones[c[0]].name != std::string_view("NG");
   auto is = [&](int i, const char *name) { return std::string_view(kPhones[c[i]].name) == name; };
   const Kind k0 = kPhones[c[0]].kind;
   if (n == 2) {
      const Kind k1 = kPhones[c[1]].kind;
      if (is(0, "S") && (k1 == kStop && !kPhones[c[1]].voiced))
         return true;
      if (is(0, "S") && (is(1, "M") || is(1, "N") || is(1, "L") || is(1, "W")))
         return true;
      if ((k0 == kStop || is(0, "F") || is(0, "TH") || is(0, "SH")) && (k1 == kLiquid || k1 == kGlide))
         return !(is(0, "T") && is(1, "L")) && !(is(0, "D") && is(1, "L"));
      return false;
   }
   if (n == 3)
      return is(0, "S") && kPhones[c[1]].kind == kStop && !kPhones[c[1]].voiced &&
             (kPhones[c[2]].kind == kLiquid || kPhones[c[2]].kind == kGlide);
   return false;
}

inline std::vector<Syllable> syllabify(std::string_view text) {
   std::vector<int> phones;
   std::vector<int> stress;
   size_t i = 0;
   while (i < text.size()) {
      while (i < text.size() && !std::isalpha(static_cast<unsigned char>(text[i])))
         ++i;
      size_t j = i;
      while (j < text.size() && std::isalpha(static_cast<unsigned char>(text[j])))
         ++j;
      if (j == i)
         break;
      std::string name(text.substr(i, j - i));
      for (char &ch : name)
         ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      int s = 1;
      if (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])))
         s = text[j++] - '0';
      // An unstressed AH is the reduced schwa, as the CMU dictionary writes it.
      if (name == "AH" && s == 0)
         name = "AX";
      const int p = findPhone(name);
      if (p >= 0) {
         phones.push_back(p);
         stress.push_back(s);
      }
      i = j;
   }
   std::vector<Syllable> out;
   std::vector<int> pending;
   for (size_t k = 0; k < phones.size(); ++k) {
      const int p = phones[k];
      if (kPhones[p].kind != kVowel) {
         pending.push_back(p);
         continue;
      }
      // Give the new syllable the longest legal tail of the pending consonants.
      int take = 0;
      for (int t = std::min<int>(3, static_cast<int>(pending.size())); t >= 0; --t)
         if (legalOnset(pending.data() + pending.size() - t, t)) {
            take = t;
            break;
         }
      if (!out.empty()) {
         Syllable &prev = out.back();
         const int give = static_cast<int>(pending.size()) - take;
         for (int g = 0; g < give && prev.codaCount() < 4; ++g)
            prev.coda[prev.codaCount()] = pending[g];
      }
      Syllable s;
      for (int t = 0; t < take; ++t)
         s.onset[t] = pending[pending.size() - take + t];
      s.vowel = p;
      s.stress = stress[k];
      out.push_back(s);
      pending.clear();
   }
   if (!out.empty())
      for (int p : pending)
         if (out.back().codaCount() < 4)
            out.back().coda[out.back().codaCount()] = p;
   return out;
}

// Turns a vowel round the middle of the vowel space (F1 about 500 Hz, F2 about 1500, in octaves):
// half of the way a quarter turn (the open vowels high at the front, the close ones low at the
// back), all of it a half turn, every vowel where its opposite is.
inline void flipVowel(double &f1, double &f2, double amount) {
   const double a = kPi * amount, u = std::log2(f1 / 500.0), v = std::log2(f2 / 1500.0);
   f1 = 500.0 * std::exp2(u * std::cos(a) - v * std::sin(a));
   f2 = std::max(1500.0 * std::exp2(u * std::sin(a) + v * std::cos(a)), 1.25 * f1);
}

// What a voice sings: syllables in order, and where each one's word is in the text they came from
// (for the panel). See lyrics.h.
struct Book {
   static constexpr int kMax = 256;
   Syllable syl[kMax];
   // The syllable's word, in the book's text: from its first letter to one past its last.
   int16_t begin[kMax] = {}, end[kMax] = {};
   int count = 0;
   void clear() { count = 0; }
   bool add(const Syllable &s, int from, int to) {
      if (count >= kMax)
         return false;
      syl[count] = s;
      begin[count] = static_cast<int16_t>(std::clamp(from, 0, 32767));
      end[count] = static_cast<int16_t>(std::clamp(to, 0, 32767));
      ++count;
      return true;
   }
};

// The voice: plays one syllable at a time, its vowel held until it's released.
class Voice {
 public:
   void setRate(double fs) {
      fs_ = fs;
      // (The smoothing and filter coefficients that only the rate sets, and those that only the
      // tension sets, worked out here once rather than every sample.)
      pK_ = 1.0 - std::exp(-(1.0 / fs_) / 0.04);
      barK_ = 1.0 - std::exp(-2.0 * kPi * 250.0 / fs_);
      shelfK_ = std::exp(-2.0 * kPi * 2000.0 / fs_);
      airK_ = std::exp(-2.0 * kPi * 5000.0 / fs_);
      {
         const double cdt = 16.0 * (1.0 / fs_);
         scoopK_ = std::exp(-cdt / 0.06);
         fallK_ = std::exp(-cdt / 0.03);
         shiverK_ = 1.0 - std::exp(-cdt * 2.0 * kPi * 16.0);
         rattleK_ = 1.0 - std::exp(-cdt * 2.0 * kPi * 35.0);
      }
      ampMsAt_ = formantMsAt_ = centsAt_ = -1e300;
      setTension(tension_, true);
      for (int b = 0; b < kBands; ++b)
         band_[b].clear();
      reset();
   }
   void reset() {
      std::memset(cascade_, 0, sizeof(cascade_));
      nasalZero_ = {};
      nasalPole_ = {};
      notchZero_ = {};
      notchPole_ = {};
      nasalZeroHz_ = 270;
      notchBw_ = 600;
      notchHz_ = 1000;
      for (auto &b : band_)
         b.clear();
      segs_.clear();
      seg_ = 0;
      segLeft_ = 0;
      av_ = avs_ = ah_ = af_ = 0;
      sounding_ = false;
      phase_ = 0;
      const double neutral[kF] = {500, 1500, 2500, 3300, 3850, 4900, 5900, 7000};
      const double wide[kF] = {100, 150, 200, 250, 300, 450, 600, 800};
      for (int n = 0; n < kF; ++n) {
         f_[n] = fv_[n] = neutral[n];
         ft_[n] = neutral[n];
      }
      for (int n = 0; n < kF; ++n)
         b_[n] = bt_[n] = wide[n];
      lastVowel_[0] = 520;
      lastVowel_[1] = 1450;
      lastVowel_[2] = 2500;
      diffPrev_ = tiltY_ = barY_ = barY2_ = shelfLp_ = airLp_ = 0;
      glotPower_ = extPower_ = 0.01;
      held_ = false;
      wander_ = scoop_ = fall_ = rattle_ = 0.0;
      period_ = 1.0;
      falling_ = false;
      for (int n = 0; n < 3; ++n)
         shiver_[n] = shiverA_[n] = 0.0;
   }

   // Starts a syllable: the last one's coda (if it's still to say), then this one's onset, then its
   // vowel, held until release().
   void start(const Syllable &s, double strength = 1.0) {
      Plan &plan = scratch_;
      plan.clear();
      // (Whatever of the last syllable is still to be said is said first, quicker, not dropped: a
      // quick run of notes still says every consonant, and a tongued line its closing ones.)
      if (held_) {
         carry(plan);
         planCoda(plan, current_, true);
      } else if (falling_) {
         carry(plan);
      }
      // (A living voice scoops up into each syllable's pitch, further from silence.)
      scoop_ = -(40.0 + 160.0 * rough_) * (sounding_ ? 0.3 : 1.0);
      falling_ = false;
      current_ = s;
      held_ = true;
      planOnset(plan, s, strength);
      replace(plan);
   }
   // Lets the held vowel go: its diphthong's glide, the coda, and the voice falls silent.
   void release() {
      if (!held_)
         return;
      Plan &plan = scratch_;
      plan.clear();
      carry(plan);
      planCoda(plan, current_, false);
      held_ = false;
      falling_ = true;
      replace(plan);
   }
   // How alive it sounds: `rough` (0..1) the folds' unsteadiness (the pitch wandering and scooping,
   // each pulse a little off in time and strength, the breath's turbulence); `growl` (0..1) a
   // creature's rasp (every other pulse weaker and later, a subharmonic under the note, a rattle,
   // now and then a creak); `wet` (0..1) a throat with something moving in it (its resonances
   // shivering, a gurgle).
   void setOrganic(double rough, double growl, double wet) {
      rough_ = std::clamp(rough, 0.0, 1.0);
      growl_ = std::clamp(growl, 0.0, 1.0);
      wet_ = std::clamp(wet, 0.0, 1.0);
   }
   // How long a diphthong holds its first vowel before moving on (ms).
   void setDiphthongHold(double ms) { diphthongHold_ = ms; }
   // The throat's shape over the phones' own: every resonance moved by `size` (a smaller throat
   // above 1), the bandwidths by `sharp` (narrower under 1), and the first three each by its `warp`
   // (an accent, or a drift). `ride` (0..1) lifts the first resonance over a high note's pitch and
   // widens the others, as a singer does up high.
   void setShape(double size, double sharp, const double warp[3], double ride) {
      size_ = size;
      sharp_ = sharp;
      for (int n = 0; n < 3; ++n)
         warp_[n] = warp[n];
      ride_ = ride;
   }
   // Strange throats: `spread` (-1..1) squeezes every resonance towards the first (under 0) or
   // pushes them apart (over it); `flip` (0..1) turns the vowels inside out, the first two
   // resonances mirrored about the middle of their ranges (an ee's where an ah's are, a front vowel
   // at the back).
   void setStrange(double spread, double flip) {
      spread_ = spread;
      flip_ = flip;
   }
   // How it's voiced: `nasal` (0..1) sings through the nose, the velum lowered on every vowel (a
   // low hum and a notch near 1 kHz); `twang` (0..1) narrows the throat over the folds, a bright,
   // brassy ring around 3 kHz.
   void setVoicing(double nasal, double twang) {
      nasal_ = std::clamp(nasal, 0.0, 1.0);
      twang_ = std::clamp(twang, 0.0, 1.0);
   }
   // The folds' tension (-1..1): lax, open longer and dark (a soft, hooting voice), or pressed,
   // shut most of each period and buzzing.
   void setTension(double t, bool force = false) {
      t = std::clamp(t, -1.0, 1.0);
      if (t == tension_ && !force)
         return;
      tension_ = t;
      // The open quotient's scale and the glottal pulse's tilt, which only the tension moves.
      oqScale_ = std::exp2(-0.9 * tension_);
      tilt_ = std::exp(-2.0 * kPi * std::min(20000.0 * std::exp2(4.5 * std::min(tension_, 0.0)), 0.45 * fs_) / fs_);
   }
   // How long the vowels take to glide in, over their own (1: as speech has it).
   void setGlide(double scale) { glide_ = scale; }
   bool active() const { return sounding_ || seg_ < segs_.size(); }

   // One sample at the pitch hz, with breathiness 0..1 and effort 0..1 (how hard it's sung).
   //
   // `ext` is another source (a bowed string, say) that takes the folds' place by `extMix` (0..1),
   // held to their level.
   double tick(double hz, double breath = 0.15, double effort = 0.6, double ext = 0.0, double extMix = 0.0) {
      advance();
      const double dt = 1.0 / fs_;
      // Sources move to their targets quickly (a burst at once), formants at the segment's pace.
      const Segment *s = seg_ < segs_.size() ? &segs_[seg_] : nullptr;
      // (Each smoothing coefficient is worked out again only when its time changes.)
      if (const double ampMs = s ? s->ampMs : 6.0; ampMs != ampMsAt_) {
         ampMsAt_ = ampMs;
         aK_ = 1.0 - std::exp(-dt / (ampMs * 0.001));
      }
      const double aK = aK_;
      av_ += aK * ((s ? s->av : 0.0) - av_);
      avs_ += aK * ((s ? s->avs : 0.0) - avs_);
      ah_ += aK * ((s ? s->ah : 0.0) - ah_);
      af_ += (s && s->ampMs < 2.0 ? 1.0 : aK) * ((s ? s->af : 0.0) - af_);
      // (A jump is made in a few ms: a resonance moved at once, still ringing, clicks.)
      const double fMs = !s ? 20.0 : s->setF ? kSnapMs : s->formantMs * (s->vowelPart ? glide_ : 1.0);
      if (fMs != formantMsAt_) {
         formantMsAt_ = fMs;
         fK_ = 1.0 - std::exp(-dt / (fMs * 0.001 * 0.3));
      }
      const double fK = fK_;
      for (int n = 0; n < kF; ++n) {
         fv_[n] += fK * (ft_[n] - fv_[n]);
         f_[n] += fK * (fv_[n] - f_[n]);
         b_[n] += fK * (bt_[n] - b_[n]);
      }
      sounding_ = av_ + avs_ + ah_ + af_ > 1e-4;
      if (++control_ >= 16) {
         control_ = 0;
         // The living voice's slow movements: a flutter in pitch (a few tens of ms of
         // memory, so it never leaves the note), the scoop settling, a fall as it's let go; the throat's shiver (a band of
         // noise at 8 to 25 Hz for each resonance) and the growl's rattle.
         const double cdt = 16.0 * dt;
         wander_ += -wander_ * cdt / 0.06 + std::sqrt(cdt) * 4.0 * noise();
         scoop_ *= scoopK_;
         if (falling_)
            fall_ = std::max(fall_ - cdt * 400.0 * rough_, -250.0);
         else
            fall_ *= fallK_;
         const double shK = shiverK_;
         for (int n = 0; n < 3; ++n) {
            shiverA_[n] += shK * (noise() * 1.7 - shiverA_[n]);
            shiver_[n] += shK * (shiverA_[n] - shiver_[n]);
         }
         const double raK = rattleK_;
         rattle_ += raK * (noise() - rattle_);
         double base[kF];
         for (int n = 0; n < kF; ++n)
            base[n] = f_[n];
         if (flip_ > 0.0)
            flipVowel(base[0], base[1], flip_);
         if (spread_ != 0.0) {
            const double k = std::exp2(spread_);
            for (int n = 1; n < kF; ++n)
               base[n] = base[0] * std::pow(base[n] / base[0], k);
         }
         double below = 0.0;
         for (int n = 0; n < kF; ++n) {
            double f = base[n] * size_ * (n < 3 ? warp_[n] * (1.0 + wet_ * (n == 0 ? 0.1 : 0.16) * shiver_[n]) : 1.0);
            // (A high note's first resonance rides over the second on purpose: the guard below
            // looks at where it was.)
            const double own = f;
            if (n == 0)
               f = std::max(f, f + ride_ * (1.15 * hz - f));
            double bw = b_[n] * sharp_ * (n > 0 ? 1.0 + 0.8 * ride_ : 1.0) * (n >= 2 ? 1.0 - 0.6 * twang_ : 1.0);
            if (n < 3)
               bw *= 1.0 + wet_ * 0.6 * std::fabs(shiver_[n]);
            // (Two resonances pushed onto each other would ring together many times louder than
            // either: closer than 10% apart, they're widened.)
            if (below > 0.0 && n < 3 && own < 1.10 * below)
               bw = std::max(bw, 1.5 * (1.10 * below - std::max(own, 0.0)) + 0.1 * own);
            below = own;
            cascadeK_[n].set(std::clamp(f, 50.0, 0.45 * fs_), std::max(bw, 8.0), fs_);
         }
         nasalZeroK_.set(nasalZeroHz_, 100.0, fs_);
         nasalPoleK_.set(270.0, 100.0, fs_);
         // (A nasal vowel's notch sits near 1 kHz, between its first resonances.)
         notchZeroK_.set(notchHz_, notchBw_, fs_);
         notchPoleK_.set(notchHz_, 600.0, fs_);
      }

      // The glottis: a KLGLOTT88 flow pulse, open for the open quotient of each period, its tilt
      // set by effort (a harder sung note closes faster and so is brighter).
      //
      // Alive (setOrganic), each period comes a little early or late and a little stronger or
      // weaker, the pitch wanders, scoops in and falls away; growling, every other period is
      // weaker and longer than the one before (a subharmonic, an octave under), and now and then
      // one hardly opens (a creak).
      const double cents = rough_ * 20.0 * wander_ + scoop_ + fall_;
      // (The cents only move every 16 samples.)
      if (cents != centsAt_) {
         centsAt_ = cents;
         centsRatio_ = std::exp2(cents / 1200.0);
      }
      const double hzNow = hz * centsRatio_;
      phase_ += hzNow * dt * period_;
      if (phase_ >= 1.0) {
         phase_ -= 1.0;
         odd_ = !odd_;
         const double split = 0.1 * growl_;
         period_ = (1.0 + (odd_ ? -split : split)) * (1.0 + (0.003 + 0.012 * rough_) * noise());
         shimmer_ = (1.0 + (0.02 + 0.08 * rough_) * noise()) * (odd_ ? 1.0 - 0.55 * growl_ : 1.0);
         if (growl_ > 0.0 && 0.5 + 0.5 * noise() < 0.12 * growl_)
            shimmer_ *= 0.15;
      }
      const double oq = std::clamp((0.5 - 0.15 * effort) * oqScale_, 0.12, 0.9);
      double flow = 0.0;
      const double ph = phase_;
      if (ph < oq) {
         const double x = ph / oq;
         flow = 6.75 * x * x * (1.0 - x) * shimmer_ * (1.0 + 0.6 * growl_ * rattle_);
      }
      const double tilt = tilt_;
      tiltY_ = (1.0 - tilt) * flow + tilt * tiltY_;
      double glot = tiltY_;
      if (extMix > 0.0) {
         // (Each measured over the last 40 ms, while voiced, so the other source comes in as loud.)
         const double pK = pK_;
         if (av_ > 0.5) {
            glotPower_ += pK * (glot * glot - glotPower_);
            extPower_ += pK * (ext * ext - extPower_);
         }
         glot += std::min(extMix, 1.0) * (ext * std::sqrt(glotPower_ / (extPower_ + 1e-12)) - glot);
      }
      // The voice bar (voicing under a closure or a voiced fricative): the pulse's fundamental.
      const double barK = barK_;
      barY_ += barK * (flow - barY_);
      barY2_ += barK * (barY_ - barY2_);
      const double nz = noise();
      const double openMod = ph < oq ? 1.0 : 0.4;
      // (The breath's turbulence comes in gusts, as a living throat's does.)
      const double asp = nz * (av_ > 0.05 ? openMod : 1.0) * (1.0 + rough_ * 0.8 * shiver_[2]);
      double in = av_ * glot + avs_ * barY2_ * 2.0 + (ah_ + (breath + 0.1 * rough_ + 0.15 * growl_) * 0.25 * av_) * asp * 0.35;
      // The nose: a pole at 270 Hz and a zero, which sit together (and cancel) until a nasal moves
      // the zero up to where the mouth, shut off behind the nose, swallows its sound.
      // The nose: a pole at 270 Hz and a zero that sit together (and cancel) until a nasal lifts the
      // zero to 550 Hz, leaving the murmur's low hum; and a notch where the shut-off mouth swallows
      // its sound, deep in a nasal and gone (its zero's bandwidth that of its pole) otherwise.
      nasalZeroHz_ += fK * ((nasalOn_ ? 550.0 : 270.0 + 230.0 * nasal_) - nasalZeroHz_);
      notchBw_ += fK * ((nasalOn_ ? 80.0 : 600.0 - 480.0 * nasal_) - notchBw_);
      notchHz_ += fK * ((nasalOn_ ? nasalHz_ : 1000.0) - notchHz_);
      in = nasalZero_.tick(nasalPole_.tick(in, nasalPoleK_), nasalZeroK_);
      in = notchPole_.tick(notchZero_.tick(in, notchZeroK_), notchPoleK_);
      double y = in;
      for (int n = 0; n < kF; ++n)
         y = cascade_[n].tick(y, cascadeK_[n]);
      // The lips radiate: a first difference.
      // (Held as loud whatever the pitch: a pulse's slope grows with how often it comes.)
      double radiated = (y - diffPrev_) * radiateGain() * std::min(130.0 / std::max(hz, 20.0), 4.0);
      diffPrev_ = y;
      // A lift over 2 kHz: a real voice's top, which a smooth pulse through the cascade lacks.
      const double shK = shelfK_;
      shelfLp_ = shK * shelfLp_ + (1.0 - shK) * radiated;
      radiated += kPresence * (1.0 + 5.0 * twang_) * (radiated - shelfLp_);

      // Frication: noise through the bank for this place, voiced fricatives buzzing with the pulse.
      double fr = 0.0;
      if (af_ > 1e-5) {
         const double n2 = noise();
         const double mod = fricVoiced_ ? (ph < oq ? 1.0 : 0.35) : 1.0;
         fr = af_ * mod * fricate(n2);
      }
      // Air at the very top, which the tract's eight resonances leave out: the breath's own hiss
      // and the folds' edge, highpassed over 5 kHz.
      const double air = nz * (av_ * (0.02 + 0.05 * breath) + ah_ * 0.15);
      const double hpK = airK_;
      airLp_ = hpK * airLp_ + (1.0 - hpK) * air;
      return radiated + fr + (air - airLp_);
   }

   double vowelLevel() const { return av_; }

 private:
   struct Segment {
      double ms = 0;    // its length (-1: held until released)
      double f[3] = {0, 0, 0}, b[3] = {0, 0, 0};
      bool setF = false;  // move the formants to f at once (in kSnapMs) at its start
      bool moveF = true;  // move the formants to f
      double formantMs = 30, ampMs = 6;
      double av = 0, avs = 0, ah = 0, af = 0;
      Spectrum spectrum = kFlat;
      bool fricVoiced = false;
      double nasalHz = 0; // the nasal zero (0: none)
      double widen = 1;   // the first formant's bandwidth widened (aspiration, an open glottis)
      bool second = false; // a diphthong's second vowel
      bool vowelPart = false; // the vowel itself (its glide in set by setGlide)
      double shift = 1;    // the frication's bands moved (a velar burst's, to where its F2 is)
   };

   struct Coeffs {
      double a = 1, b = 0, c = 0;
      // (Set every 16 samples, mostly to where they already were: worked out only when they move.)
      double hzAt = -1.0, bwAt = -1.0, fsAt = -1.0;
      void set(double hz, double bw, double fs) {
         if (hz == hzAt && bw == bwAt && fs == fsAt)
            return;
         hzAt = hz;
         bwAt = bw;
         fsAt = fs;
         const double r = std::exp(-kPi * bw / fs);
         c = -r * r;
         b = 2.0 * r * std::cos(2.0 * kPi * hz / fs);
         a = 1.0 - b - c;
      }
   };
   struct Res {
      double y1 = 0, y2 = 0;
      double tick(double x, const Coeffs &k) {
         const double y = k.a * x + k.b * y1 + k.c * y2;
         y2 = y1;
         y1 = y;
         return y;
      }
   };
   struct AntiRes {
      double x1 = 0, x2 = 0;
      double tick(double x, const Coeffs &k) {
         // The inverse of a resonator: a zero pair with unity gain at DC.
         const double y = (x - k.b * x1 - k.c * x2) / k.a;
         x2 = x1;
         x1 = x;
         return y;
      }
   };
   // A band of the parallel bank: a two-pole bandpass, unity at its peak.
   struct Band {
      double b1 = 0, b2 = 0, g = 0, y1 = 0, y2 = 0;
      void clear() { y1 = y2 = 0; }
      void set(double hz, double bw, double fs) {
         const double r = std::exp(-kPi * bw / fs);
         b1 = 2.0 * r * std::cos(2.0 * kPi * hz / fs);
         b2 = -r * r;
         g = (1.0 - r * r) * 0.5;
      }
      double tick(double x) {
         const double y = g * x + b1 * y1 + b2 * y2;
         y2 = y1;
         y1 = y;
         return y;
      }
   };
   static constexpr int kBands = 6;
   // A syllable's segments, held in place (a voice plans them on the audio thread).
   struct Plan {
      static constexpr int kMax = 64;
      Segment at[kMax];
      int n = 0;
      void push_back(const Segment &s) {
         if (n < kMax)
            at[n++] = s;
      }
      bool empty() const { return n == 0; }
      int size() const { return n; }
      Segment &back() { return at[n - 1]; }
      const Segment &operator[](int i) const { return at[i]; }
      void clear() { n = 0; }
   };

   // What's still to be said of the plan under way, before a new one replaces it: the consonants
   // not yet finished (a little quicker, to catch up), and the vowel if it hasn't yet sounded long
   // enough to be heard (55 ms stressed, 35 unstressed).
   void carry(Plan &plan) {
      if (seg_ < 0 || seg_ >= segs_.size())
         return;
      const double msPer = 1000.0 / fs_;
      int carried = 0;
      for (int k = seg_; k < segs_.size() && carried < 16; ++k) {
         const Segment &g = segs_[k];
         if (g.vowelPart) {
            const double had = k == seg_ ? segAt_ * msPer : 0.0;
            const double need = (current_.stress == 1 ? 55.0 : 35.0) - had;
            if (need > 3.0) {
               Segment v = g;
               v.ms = need;
               plan.push_back(v);
            }
            return;
         }
         Segment rest = g;
         rest.ms = 0.8 * (k == seg_ ? segLeft_ * msPer : g.ms);
         if (rest.ms >= 1.0) {
            plan.push_back(rest);
            ++carried;
         }
      }
   }

   void replace(Plan &plan) {
      // The new plan starts now (what was unfinished of the old one, carried into it, first).
      segs_ = plan;
      seg_ = -1;
      segLeft_ = 0;
      advance();
   }
   void advance() {
      while (segLeft_ <= 0) {
         if (seg_ + 1 >= segs_.size()) {
            if (seg_ < segs_.size())
               seg_ = segs_.size();
            return;
         }
         ++seg_;
         const Segment &s = segs_[seg_];
         segLeft_ = s.ms < 0 ? 1e12 : std::max(1.0, s.ms * 0.001 * fs_);
         segAt_ = 0.0;
         if (s.moveF || s.setF)
            for (int n = 0; n < 3; ++n) {
               ft_[n] = s.f[n];
               bt_[n] = s.b[n] * (n == 0 ? s.widen : 1.0);
               // (From silence it can jump: nothing is ringing to click.)
               if (s.setF && !sounding_) {
                  f_[n] = fv_[n] = s.f[n];
                  b_[n] = bt_[n];
               }
            }
         nasalOn_ = s.nasalHz > 0;
         if (nasalOn_)
            nasalHz_ = s.nasalHz;
         fricVoiced_ = s.fricVoiced;
         setSpectrum(s.spectrum, s.shift);
      }
      if (segLeft_ < 1e11)
         segLeft_ -= 1.0;
      segAt_ += 1.0;
   }

   static double db(double d) { return std::pow(10.0, d / 20.0); }
   // Where the shut-off mouth, branching off the nose, swallows the murmur's sound.
   static double nasalZero(const Phone &c) {
      // (Fujimura 1962: an m's about 1 kHz, an n's 1.5 to 2 kHz, an ng's over 3.)
      return c.place == kLabial ? 1000.0 : c.place == kAlveolar ? 1750.0 : 3200.0;
   }

   void vowelTargets(const Phone &v, double out[3], double bw[3], bool end = false) const {
      out[0] = end && v.g1 > 0 ? v.g1 : v.f1;
      out[1] = end && v.g1 > 0 ? v.g2 : v.f2;
      out[2] = end && v.g1 > 0 ? v.g3 : v.f3;
      bw[0] = v.b1;
      bw[1] = v.b2;
      bw[2] = v.b3;
   }

   // The onset: each consonant in turn, then the vowel, held.
   void planOnset(Plan &plan, const Syllable &s, double strength) {
      const Phone &v = kPhones[s.vowel];
      double vf[3], vb[3];
      vowelTargets(v, vf, vb);
      const int n = s.onsetCount();
      double transition = sounding_ ? 45.0 : 25.0; // the vowel's glide in from whatever came before
      const bool fromSilence = !sounding_ && plan.empty();
      for (int i = 0; i < n; ++i) {
         const Phone &c = kPhones[s.onset[i]];
         // What it opens onto: the next consonant if that's a liquid or glide, else the vowel.
         double next[3] = {vf[0], vf[1], vf[2]};
         if (i + 1 < n) {
            const Phone &nx = kPhones[s.onset[i + 1]];
            if (nx.kind == kLiquid || nx.kind == kGlide) {
               next[0] = nx.f1;
               next[1] = nx.f2;
               next[2] = nx.f3;
            }
         }
         double loc[3];
         locus(c, next, loc);
         double cb[3] = {c.b1, c.b2, c.b3};
         const double scale = strength;
         switch (c.kind) {
         case kStop:
         case kAffricate: {
            Segment closure;
            closure.ms = c.ms * (i > 0 ? 0.6 : 1.0);
            std::copy(loc, loc + 3, closure.f);
            std::copy(cb, cb + 3, closure.b);
            closure.setF = true;
            closure.avs = c.voiced ? 0.25 : 0.0;
            closure.ampMs = 4;
            plan.push_back(closure);
            Segment burst;
            burst.ms = c.kind == kAffricate ? c.ms : (c.place == kVelar ? 1.8 : 1.0) * (c.voiced ? 6 : 10);
            std::copy(loc, loc + 3, burst.f);
            std::copy(cb, cb + 3, burst.b);
            burst.moveF = false;
            burst.af = db(c.level) * scale * (c.kind == kAffricate ? 1.0 : 1.4);
            burst.spectrum = c.spectrum;
            if (c.spectrum == kVelarBurst)
               burst.shift = loc[1] / 2000.0;
            burst.avs = c.voiced ? 0.25 : 0.0;
            burst.ampMs = 0.5;
            burst.fricVoiced = c.voiced;
            plan.push_back(burst);
            // (After an s, as in "spin", "stay" and "sky", a p, t or k isn't aspirated: it sounds
            // nearly as a b, d or g would.)
            const bool afterS = i > 0 && kPhones[s.onset[i - 1]].kind == kFric && !kPhones[s.onset[i - 1]].voiced;
            if (!c.voiced) {
               // Aspiration: the breath through the opening tract, the formants already moving. Its
               // length (the voice onset time) is longest at the back of the mouth: p 50 ms, t 60, k 70.
               Segment asp;
               const double vot = c.kind == kAffricate ? 25 : c.place == kLabial ? 50 : c.place == kVelar ? 70 : 60;
               asp.ms = afterS ? 10 : vot * (i + 1 < n ? 0.8 : 1.0);
               std::copy(next, next + 3, asp.f);
               std::copy(vb, vb + 3, asp.b);
               asp.widen = 4.0;
               asp.formantMs = 40;
               asp.ah = db(afterS ? -24 : -12) * scale;
               asp.ampMs = 3;
               plan.push_back(asp);
               transition = 30;
            } else {
               transition = 45;
            }
            break;
         }
         case kFric: {
            Segment f;
            f.ms = c.ms * (i > 0 ? 0.7 : 1.0);
            std::copy(loc, loc + 3, f.f);
            std::copy(cb, cb + 3, f.b);
            f.setF = true;
            f.af = db(c.level) * scale;
            f.spectrum = c.spectrum;
            f.fricVoiced = c.voiced;
            f.av = c.voiced ? 0.3 : 0.0;
            f.avs = c.voiced ? 0.3 : 0.0;
            // (A fricative's hiss swells in over 20 ms or so, where a stop's or an affricate's
            // starts at once: the difference between "shop" and "chop", "fee" and "pea".)
            f.ampMs = 18;
            plan.push_back(f);
            transition = 40;
            break;
         }
         case kAspirate: {
            Segment h;
            h.ms = c.ms;
            std::copy(vf, vf + 3, h.f);
            std::copy(vb, vb + 3, h.b);
            h.setF = true;
            h.widen = 3.0;
            h.ah = db(c.level) * scale;
            h.ampMs = 10;
            plan.push_back(h);
            transition = 20;
            break;
         }
         case kNasal: {
            Segment m;
            m.ms = c.ms;
            m.f[0] = c.f1;
            m.f[1] = c.f2;
            m.f[2] = c.f3;
            std::copy(cb, cb + 3, m.b);
            m.setF = !sounding_ && i == 0 && plan.empty();
            m.formantMs = 15;
            m.av = db(kMurmur);
            m.nasalHz = nasalZero(c);
            m.ampMs = 10;
            plan.push_back(m);
            // The release: the mouth opens at once on the locus.
            Segment r;
            r.ms = 1;
            std::copy(loc, loc + 3, r.f);
            std::copy(vb, vb + 3, r.b);
            r.setF = true;
            r.av = 1.0;
            r.ampMs = 6;
            plan.push_back(r);
            transition = 20;
            break;
         }
         case kGlide:
         case kLiquid: {
            Segment g;
            g.ms = c.ms;
            g.f[0] = c.f1;
            g.f[1] = c.f2;
            g.f[2] = c.f3;
            std::copy(cb, cb + 3, g.b);
            g.setF = !sounding_ && i == 0 && plan.empty();
            g.formantMs = 20;
            g.av = db(-3);
            g.ampMs = 12;
            plan.push_back(g);
            // (An l lets go of the tongue's tip at once, a w or y glides: the difference between
            // "lay" and "way".)
            transition = c.kind == kGlide ? 75 : c.place == kAlveolar ? 25 : 60;
            break;
         }
         default:
            break;
         }
      }
      if (n == 0 && !plan.empty() && plan.back().nasalHz > 0 && plan.back().av < 0.5) {
         // Straight on from a nasal ("an alien"): the mouth opens on the vowel at once.
         double loc[3];
         Phone nasal = kPhones[findPhone("N")];
         locus(nasal, vf, loc);
         Segment r;
         r.ms = 1;
         std::copy(loc, loc + 3, r.f);
         std::copy(vb, vb + 3, r.b);
         r.setF = true;
         r.av = 1.0;
         plan.push_back(r);
         transition = 20;
      }
      Segment vowel;
      vowel.ms = -1;
      std::copy(vf, vf + 3, vowel.f);
      std::copy(vb, vb + 3, vowel.b);
      vowel.setF = n == 0 && fromSilence;
      vowel.formantMs = transition;
      vowel.av = s.stress == 0 ? db(-2) : 1.0;
      vowel.ampMs = n == 0 && fromSilence ? 25 : 8;
      vowel.vowelPart = true;
      if (v.g1 > 0) {
         // A diphthong moves on to its second vowel by itself after a while, as it does in speech;
         // a singer holds the first one longer (setDiphthongHold).
         vowel.ms = diphthongHold_;
         plan.push_back(vowel);
         Segment second = vowel;
         second.ms = -1;
         second.f[0] = v.g1;
         second.f[1] = v.g2;
         second.f[2] = v.g3;
         second.setF = false;
         second.formantMs = 90;
         second.second = true;
         plan.push_back(second);
      } else {
         plan.push_back(vowel);
      }
      std::copy(vf, vf + 3, lastVowel_);
   }

   // The coda: a diphthong's glide, then each closing consonant, then quiet (or, going straight
   // on to another syllable, the voice left sounding for its onset).
   void planCoda(Plan &plan, const Syllable &s, bool goingOn) {
      const Phone &v = kPhones[s.vowel];
      double vf[3], vb[3];
      vowelTargets(v, vf, vb, true);
      const bool glided = seg_ >= 0 && seg_ < segs_.size() && segs_[seg_].second;
      if (v.g1 > 0) {
         Segment glide;
         glide.ms = glided ? 20 : goingOn ? 60 : 90;
         std::copy(vf, vf + 3, glide.f);
         std::copy(vb, vb + 3, glide.b);
         glide.formantMs = 60;
         glide.av = 1.0;
         plan.push_back(glide);
      }
      const int n = s.codaCount();
      for (int i = 0; i < n; ++i) {
         const Phone &c = kPhones[s.coda[i]];
         double loc[3];
         locus(c, vf, loc);
         double cb[3] = {c.b1, c.b2, c.b3};
         if (i == 0 && c.kind != kGlide && c.kind != kLiquid && c.kind != kAspirate) {
            // The vowel bends towards where the consonant closes.
            Segment t;
            const bool voiceless = !c.voiced && (c.kind == kStop || c.kind == kAffricate || c.kind == kFric);
            // (Going straight on to the next syllable, it's said briskly: that one's note has started.)
            t.ms = (voiceless ? 35 : 50) * (goingOn ? 0.7 : 1.0);
            std::copy(loc, loc + 3, t.f);
            std::copy(vb, vb + 3, t.b);
            t.formantMs = voiceless ? 25 : 35;
            t.av = 1.0;
            if (c.kind == kNasal)
               t.nasalHz = nasalZero(c);
            plan.push_back(t);
         }
         switch (c.kind) {
         case kStop:
         case kAffricate: {
            Segment closure;
            closure.ms = c.ms * (goingOn ? 0.75 : 1.0);
            std::copy(loc, loc + 3, closure.f);
            std::copy(cb, cb + 3, closure.b);
            closure.avs = c.voiced ? 0.4 : 0.0;
            closure.ampMs = c.voiced ? 15 : 3;
            plan.push_back(closure);
            Segment burst;
            burst.ms = c.kind == kAffricate ? c.ms : 12;
            std::copy(loc, loc + 3, burst.f);
            std::copy(cb, cb + 3, burst.b);
            burst.moveF = false;
            burst.af = db(c.level - 2);
            burst.spectrum = c.spectrum;
            if (c.spectrum == kVelarBurst)
               burst.shift = loc[1] / 2000.0;
            burst.ampMs = 0.5;
            plan.push_back(burst);
            // (Its release is a short breath, or none when another consonant follows straight on,
            // as in "box": much longer, it's heard as a vowel after it, "tank" as "tannic".)
            const bool more = i + 1 < n;
            Segment asp;
            asp.ms = more ? 5 : c.voiced ? 15 : goingOn ? 15 : 30;
            asp.moveF = false;
            asp.widen = 3.0;
            asp.ah = c.voiced || more ? 0.0 : db(goingOn ? -22 : -18);
            asp.ampMs = 6;
            plan.push_back(asp);
            break;
         }
         case kFric: {
            Segment f;
            f.ms = c.ms * 1.2;
            std::copy(loc, loc + 3, f.f);
            std::copy(cb, cb + 3, f.b);
            f.af = db(c.level);
            f.spectrum = c.spectrum;
            f.fricVoiced = c.voiced;
            f.av = c.voiced ? 0.25 : 0.0;
            f.avs = c.voiced ? 0.25 : 0.0;
            f.ampMs = 10;
            plan.push_back(f);
            break;
         }
         case kNasal: {
            Segment m;
            m.ms = goingOn ? c.ms : c.ms * 2.0;
            m.f[0] = c.f1;
            m.f[1] = c.f2;
            m.f[2] = c.f3;
            std::copy(cb, cb + 3, m.b);
            m.formantMs = 8;
            m.av = db(kMurmur);
            m.nasalHz = nasalZero(c);
            m.ampMs = 6;
            plan.push_back(m);
            break;
         }
         case kGlide:
         case kLiquid: {
            Segment g;
            g.ms = c.ms * 1.5;
            g.f[0] = c.f1;
            g.f[1] = c.f2;
            g.f[2] = c.f3;
            std::copy(cb, cb + 3, g.b);
            g.formantMs = 45;
            g.av = db(-3);
            g.ampMs = 15;
            plan.push_back(g);
            break;
         }
         default:
            break;
         }
      }
      if (!goingOn) {
         Segment quiet;
         quiet.ms = 1;
         quiet.moveF = false;
         quiet.ampMs = n == 0 ? 35 : 20;
         plan.push_back(quiet);
      }
   }

   void setSpectrum(Spectrum s, double shift = 1.0) {
      if (s == spectrum_ && shift == shift_ && bandsSet_)
         return;
      spectrum_ = s;
      shift_ = shift;
      bandsSet_ = true;
      // The bands: a centre, a bandwidth and a level (dB) each, and the flat bypass.
      struct Shape {
         double hz[kBands], bw[kBands], gain[kBands], bypass;
      };
      static const Shape kShapes[kSpectrumCount] = {
         // flat (f, v, h): the lips' hiss, nearly flat, a little lift up top
         {{1500, 2500, 3500, 5000, 7000, 9000}, {1500, 1500, 1500, 2000, 3000, 3000}, {-12, -10, -8, -6, -6, -8}, 0.3},
         // dental (th): flat, a soft peak high up
         {{1500, 2600, 3500, 5000, 7000, 9000}, {1500, 1500, 1500, 1500, 2500, 3000}, {-14, -12, -10, -6, -4, -6}, 0.3},
         // sibilant (s, z): a strong band over 5 kHz
         {{2500, 3500, 4500, 6000, 7500, 9500}, {400, 500, 800, 1200, 1500, 2500}, {-40, -30, -20, -6, 0, -6}, 0.02},
         // hush (sh, zh, ch, j): a wide hump from 2 to 5 kHz
         {{1900, 2500, 3200, 4200, 5500, 7500}, {350, 500, 700, 1000, 1500, 2500}, {-14, 0, -2, -4, -10, -18}, 0.02},
         // labial burst (p, b): diffuse and falling, its energy low (Stevens & Blumstein)
         {{600, 1200, 2000, 3000, 4500, 7000}, {600, 800, 1200, 1500, 2000, 3000}, {0, -3, -9, -15, -22, -30}, 0.03},
         // alveolar burst (t, d): high, over 3.5 kHz
         {{1700, 2700, 3700, 4500, 6000, 8000}, {500, 600, 800, 1200, 1500, 2500}, {-24, -12, -4, 0, -2, -8}, 0.05},
         // velar burst (k, g): compact, in the middle
         {{1500, 2000, 2600, 3300, 4500, 7000}, {300, 400, 500, 700, 1200, 2500}, {-4, 0, -2, -8, -14, -20}, 0.05},
      };
      const Shape &sh = kShapes[s];
      for (int b = 0; b < kBands; ++b) {
         const double hz = std::min(sh.hz[b] * shift, 0.45 * fs_);
         band_[b].set(hz, sh.bw[b], fs_);
         bandGain_[b] = db(sh.gain[b]) * (sh.hz[b] < 0.45 * fs_ ? 1.0 : 0.0);
      }
      bypass_ = sh.bypass;
      // Its power for white noise, from the bank's impulse response, so every spectrum hisses at
      // the level its consonant asks for.
      Band probe[kBands];
      std::copy(band_, band_ + kBands, probe);
      for (auto &p : probe)
         p.clear();
      double power = 0.0;
      const int len = static_cast<int>(0.02 * fs_);
      for (int i = 0; i < len; ++i) {
         const double x = i == 0 ? 1.0 : 0.0;
         double y = bypass_ * x;
         for (int b = 0; b < kBands; ++b)
            y += (b & 1 ? -1.0 : 1.0) * bandGain_[b] * probe[b].tick(x);
         power += y * y;
      }
      fricNorm_ = 1.0 / std::sqrt(std::max(power, 1e-12));
   }
   double fricate(double x) {
      double y = bypass_ * x;
      for (int b = 0; b < kBands; ++b)
         y += (b & 1 ? -1.0 : 1.0) * bandGain_[b] * band_[b].tick(x);
      return y * fricNorm_ * kFricScale;
   }

   double radiateGain() const { return fs_ / (2.0 * kPi * 400.0); }

   double noise() {
      seed_ ^= seed_ << 13;
      seed_ ^= seed_ >> 17;
      seed_ ^= seed_ << 5;
      return (static_cast<double>(seed_) / 4294967296.0) * 2.0 - 1.0;
   }

   static constexpr double kSnapMs = 8.0;
   static constexpr double kFricScale = 0.7, kPresence = 1.5, kMurmur = -5.0;
   double fs_ = 48000;
   Plan segs_, scratch_;
   int seg_ = 0;
   double segLeft_ = 0, segAt_ = 0;
   Syllable current_;
   bool held_ = false, sounding_ = false;
   double av_ = 0, avs_ = 0, ah_ = 0, af_ = 0;
   // Eight formants: the five that move, and three more above that keep the top of the voice open
   // (at a high sample rate, a tract of only five falls away above them).
   static constexpr int kF = 8;
   double f_[kF], fv_[kF], ft_[kF], b_[kF], bt_[kF];
   Coeffs cascadeK_[kF], nasalZeroK_, nasalPoleK_, notchZeroK_, notchPoleK_;
   Res cascade_[kF];
   AntiRes nasalZero_, notchZero_;
   Res nasalPole_, notchPole_;
   bool nasalOn_ = false, fricVoiced_ = false, bandsSet_ = false;
   double nasalHz_ = 1500, nasalZeroHz_ = 270, notchBw_ = 600, notchHz_ = 1000;
   Band band_[kBands];
   double bandGain_[kBands] = {}, bypass_ = 0, fricNorm_ = 1;
   Spectrum spectrum_ = kFlat;
   double shift_ = 1.0;
   double phase_ = 0, jitter_ = 1, shimmer_ = 1;
   double diphthongHold_ = 110, shelfLp_ = 0;
   double airLp_ = 0, diffPrev_ = 0, tiltY_ = 0, barY_ = 0, barY2_ = 0;
   double lastVowel_[3] = {520, 1450, 2500};
   double spread_ = 0.0, flip_ = 0.0, size_ = 1.0, sharp_ = 1.0, warp_[3] = {1.0, 1.0, 1.0}, ride_ = 0.0, glide_ = 1.0;
   double glotPower_ = 0.01, extPower_ = 0.01;
   int control_ = 0;
   double nasal_ = 0.0, twang_ = 0.0, tension_ = 0.0;
   // What only the rate or the tension sets, and the last smoothing times and cents, with what
   // they gave (see setRate, setTension and tick).
   double scoopK_ = 0.0, fallK_ = 0.0, shiverK_ = 0.0, rattleK_ = 0.0;
   double pK_ = 0.0, barK_ = 0.0, shelfK_ = 0.0, airK_ = 0.0, oqScale_ = 1.0, tilt_ = 0.0;
   double ampMsAt_ = -1e300, aK_ = 0.0, formantMsAt_ = -1e300, fK_ = 0.0, centsAt_ = -1e300, centsRatio_ = 1.0;
   double rough_ = 0.0, growl_ = 0.0, wet_ = 0.0, wander_ = 0.0, scoop_ = 0.0, fall_ = 0.0, period_ = 1.0, rattle_ = 0.0;
   double shiver_[3] = {}, shiverA_[3] = {};
   bool odd_ = false, falling_ = false;
   uint32_t seed_ = 0x9e3779b9u;
};

} // namespace jbreath::speech
