#pragma once

// JSynth's sound: up to 40 voices across the two parts, each a creature in the flock.
//
// Timing. A global 32-sample control grid runs across process() calls, so the output is the same at
// any host block size. Notes start and stop on their exact sample; param and expression changes only
// set targets that the next control tick reads. On each tick the engine reads the params, steps the
// flock (at a fixed 500 Hz, counted in whole samples), runs each voice's envelopes 2 and 3, LFOs and
// routes, and sets per-sample linear ramps for everything the audio path reads. Env 1 (the amp) runs
// every sample.
//
// Voices run sample by sample, all together, so one voice can hear another's last sample: Neighbour
// FM (a neighbour's Osc 1 into this voice's Osc 2) and Bite (a hunter's Osc 1 ring-modulating its
// prey) are one-sample couplings. At 2x or 4x Quality the voices run oversampled; each part's stereo
// bus is decimated by IIR half-bands.
//
// Everything is allocated in prepare(); process() allocates nothing and takes no locks.

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp.h"
#include "flock.h"
#include "params.h"

namespace jsynth {

// A note as the engine sees it: who sent it, so expressions and NOTE_END can find it again.
struct NoteRef {
   int32_t noteId = -1;
   int16_t port = 0, channel = -1, key = -1;
   // From the host (it wants a NOTE_END), not the panel.
   bool host = false;
};

class Engine {
public:
   static constexpr int kTick = 32;
   static constexpr int kMaxOversample = 4;
   static constexpr float kFlockRate = 500.0f;

   // Reads a param's current value (with host modulation): set by the plugin.
   struct ParamSource {
      const std::atomic<double> *values = nullptr;
      const std::atomic<double> *mods = nullptr;
   };

   void prepare(double sampleRate, ParamSource source) {
      sampleRate_ = sampleRate;
      params_ = source;
      // Each part's param offset to its index in kParams(), and to its route destination.
      for (int part = 0; part < kPartCount; ++part)
         for (clap_id o = 0; o < kPartEnd; ++o)
            partIndex_[part][o] = paramIndex(partId(part, o));
      for (clap_id o = 0; o < kPartEnd; ++o) {
         destOf_[o] = -1;
         const int i = partIndex_[0][o];
         min_[o] = i < 0 ? 0.0f : static_cast<float>(kParams()[i].def.min);
         span_[o] = i < 0 ? 1.0f : static_cast<float>(std::max(1e-9, kParams()[i].def.max - kParams()[i].def.min));
         isRatio_[o] = i >= 0 && kParams()[i].unit == Unit::Ratio;
      }
      const auto &dests = destinations();
      destCount_ = static_cast<int>(dests.size());
      for (int d = 0; d < destCount_; ++d) {
         destOf_[dests[d]] = d;
         destOffset_[d] = dests[d];
      }
      firstAmountDest_ = destinationIndex(routeParam(0, kRouteAmount));
      // The comb's longest delay: its lowest tuning (30 Hz) at 4x.
      const int combMax = static_cast<int>(sampleRate * kMaxOversample / 30.0) + 8;
      for (Voice &v : voices_)
         for (auto &f : v.comb_)
            for (dsp::Comb &c : f)
               c.prepare(combMax);
      first_ = true;
      readParams();
      reset(1);
   }

   // Silence, and the flock and every random sequence back to their start from `seed`.
   void reset(uint32_t seed) {
      seed_ = seed;
      flock_.reset(seed * 7919u + 1u);
      for (int i = 0; i < kVoiceSlots; ++i) {
         Voice &v = voices_[i];
         v.active = false;
         v.clear();
         v.noise.rng.seed(seed * 2654435761u + static_cast<uint32_t>(i) * 40503u + 17u);
         v.random.seed(seed * 2246822519u + static_cast<uint32_t>(i) * 3266489917u + 5u);
         for (int l = 0; l < kLfoCount; ++l)
            v.lfo[l].seed(seed * 668265263u + static_cast<uint32_t>(i * kLfoCount + l) * 374761393u + 3u);
         v.drift.seed(seed * 1274126177u + static_cast<uint32_t>(i) * 2654435761u + 11u);
         v.drift.restart(0);
         for (int l = 0; l < kLfoCount; ++l)
            v.lfo[l].restart(0);
      }
      for (int p = 0; p < kPartCount; ++p) {
         for (int l = 0; l < kLfoCount; ++l) {
            partLfo_[p][l].seed(seed * 3266489917u + static_cast<uint32_t>(p * kLfoCount + l) * 668265263u + 7u);
            partLfo_[p][l].restart(0);
         }
         for (int c = 0; c < 2; ++c)
            for (auto &h : halfBands_[p][c])
               h.reset();
         dc_[p][0] = dc_[p][1] = dcIn_[p][0] = dcIn_[p][1] = 0;
         stackSize_[p] = 0;
      }
      for (float &c : chanBend_)
         c = 0;
      for (float &c : chanPressure_)
         c = 0;
      for (float &c : chanTimbre_)
         c = 0.5f;
      managerBend_ = 0;
      lastPitch_[0] = lastPitch_[1] = -1.0f;
      width_.snap(static_cast<float>(global(kOutputWidth)));
      master_.snap(static_cast<float>(std::pow(10.0, global(kMasterLevel) / 20.0)));
      routesBuilt_ = false;
      modWheel_ = 0;
      sustain_ = false;
      tickPos_ = 0;
      flockAcc_ = 0;
      age_ = 0;
      first_ = true;
      frames_ = 0;
      for (auto &a : oscOut_)
         for (float &x : a)
            x = 0;
   }

   // ---- notes (audio thread) ----

   // A note on: hatches a creature in each part the play mode sends it to.
   void noteOn(const NoteRef &ref, float velocity) {
      if (ref.key < 0 || ref.key > 127)
         return;
      // MPE: a note on a member channel starts from that channel's last bend, pressure and timbre.
      const int ch = ref.channel;
      float bendNorm = 0, pressure = 0, timbre = 0.5f;
      if (ch >= 1 && ch < 16) {
         bendNorm = chanBend_[ch];
         pressure = chanPressure_[ch];
         timbre = chanTimbre_[ch];
      }
      else if (ch == 0) {
         pressure = chanPressure_[0];
         timbre = chanTimbre_[0];
      }
      const int mode = playMode();
      for (int part = 0; part < kPartCount; ++part) {
         if (!plays(part, mode, ref, velocity))
            continue;
         const int voiceMode = static_cast<int>(plain(part, kVoiceMode) + 0.5);
         if (voiceMode != 0) {
            monoOn(part, ref, velocity, bendNorm, pressure, timbre, voiceMode == 2);
            continue;
         }
         start(part, ref, velocity, bendNorm, pressure, timbre, -1);
      }
   }

   void noteOff(const NoteRef &ref, float velocity) {
      for (int part = 0; part < kPartCount; ++part)
         if (static_cast<int>(plain(part, kVoiceMode) + 0.5) != 0)
            monoOff(part, ref, velocity);
      for (Voice &v : voices_)
         if (v.active && v.gate && matches(v.ref, ref)) {
            v.lift = velocity;
            if (sustain_)
               v.sustained = true;
            else
               gateOff(v);
         }
   }

   // A CLAP note expression; -1 fields match any.
   void expression(const NoteRef &ref, int id, double value) {
      for (Voice &v : voices_) {
         if (!v.active || !matches(v.ref, ref))
            continue;
         if (id == CLAP_NOTE_EXPRESSION_TUNING)
            v.bendSemis = static_cast<float>(std::clamp(value, -128.0, 128.0));
         else if (id == CLAP_NOTE_EXPRESSION_PRESSURE)
            v.pressure = static_cast<float>(std::clamp(value, 0.0, 1.0));
         else if (id == CLAP_NOTE_EXPRESSION_BRIGHTNESS)
            v.timbre = static_cast<float>(std::clamp(value, 0.0, 1.0));
      }
   }

   // Ends every voice playing `ref` at once.
   void choke(const NoteRef &ref) {
      for (int i = 0; i < kVoiceSlots; ++i)
         if (voices_[i].active && matches(voices_[i].ref, ref))
            end(i);
   }

   // A MIDI 1 message. Channel 1 is the MPE manager (and a plain keyboard's channel): its bend moves
   // every note, by the Manager Bend Range. Channels 2 to 16 are member channels, one note each.
   void midi(const uint8_t data[3], int port) {
      const int status = data[0] & 0xF0, ch = data[0] & 0x0F;
      NoteRef ref;
      ref.port = static_cast<int16_t>(port);
      ref.channel = static_cast<int16_t>(ch);
      ref.key = static_cast<int16_t>(data[1] & 0x7F);
      ref.host = true;
      switch (status) {
      case 0x90:
         if (data[2] > 0) {
            noteOn(ref, data[2] / 127.0f);
            break;
         }
         [[fallthrough]];
      case 0x80:
         noteOff(ref, status == 0x80 ? data[2] / 127.0f : 0.0f);
         break;
      case 0xE0: {
         const float norm = std::clamp(((data[2] << 7 | data[1]) - 8192) / 8192.0f, -1.0f, 1.0f);
         if (ch == 0)
            managerBend_ = norm;
         else {
            chanBend_[ch] = norm;
            for (Voice &v : voices_)
               if (v.active && v.ref.channel == ch && v.ref.noteId == -1)
                  v.bendNorm = norm;
         }
         break;
      }
      case 0xD0:
         chanPressure_[ch] = data[1] / 127.0f;
         for (Voice &v : voices_)
            if (v.active && v.ref.channel == ch && v.ref.noteId == -1)
               v.pressure = chanPressure_[ch];
         break;
      case 0xB0:
         if (data[1] == 74) {
            chanTimbre_[ch] = data[2] / 127.0f;
            for (Voice &v : voices_)
               if (v.active && v.ref.channel == ch && v.ref.noteId == -1)
                  v.timbre = chanTimbre_[ch];
         }
         else if (data[1] == 1 && ch == 0)
            modWheel_ = data[2] / 127.0f;
         else if (data[1] == 64) {
            const bool down = data[2] >= 64;
            if (sustain_ && !down)
               for (Voice &v : voices_)
                  if (v.active && v.sustained) {
                     v.sustained = false;
                     gateOff(v);
                  }
            sustain_ = down;
         }
         else if (data[1] == 120 || data[1] == 123)
            for (Voice &v : voices_)
               if (v.active && v.gate)
                  gateOff(v);
         break;
      default:
         break;
      }
   }

   // Tempo for synced LFOs, and whether the transport just started (which reseeds the flock and
   // randomness when nothing is sounding, so a bounce from the top matches playback).
   void transport(double bpm, bool playing) {
      if (bpm > 1.0)
         bpm_ = bpm;
      if (playing && !playing_) {
         bool silent = true;
         for (const Voice &v : voices_)
            silent = silent && !v.active;
         if (silent)
            reset(seed_);
      }
      playing_ = playing;
   }

   // ---- render ----

   // Renders `frames` samples at sample offset 0..frames. The plugin splits a host block at note
   // events and calls this between them; the control grid carries over regardless.
   // `out` is [port][channel], three stereo ports: main, part A's own, part B's own.
   void render(float *const *const *out, int offset, int frames) {
      for (int n = 0; n < frames; ++n) {
         blockSample_ = offset + n;
         if (tickPos_ == 0)
            tick();
         renderSample(out, offset + n);
         if (++tickPos_ == kTick)
            tickPos_ = 0;
      }
   }

   // Note ends since the last call, in order, with the sample they happened at.
   struct Ended {
      NoteRef ref;
      int sample;
   };
   int endedCount() const { return endedCount_; }
   const Ended &ended(int i) const { return ended_[i]; }
   void clearEnded() { endedCount_ = 0; }

   int activeVoices() const {
      int n = 0;
      for (const Voice &v : voices_)
         n += v.active;
      return n;
   }

   const Flock &flock() const { return flock_; }
   // The newest held voice's sources for the panel's chips: envelopes and LFOs of `part`.
   float envLevel(int part, int e) const {
      const Voice *v = newest(part);
      return v ? v->envOut[e] : 0.0f;
   }
   float lfoLevel(int part, int l) const {
      const Voice *v = newest(part);
      return v ? v->lfoOut[l] : partLfoOut_[part][l];
   }
   // Where every sounding voice's envelopes are, in the curve's four stages: a delay sits at the
   // start of the attack and a hold at the peak.
   void showEnvelopes(FlockFrame &f) const {
      f.playheadCount = 0;
      for (const Voice &v : voices_) {
         if (!v.active || v.fading)
            continue;
         FlockFrame::Playhead &p = f.playheads[f.playheadCount++];
         p.part = static_cast<int8_t>(v.part);
         for (int e = 0; e < kEnvCount; ++e) {
            const dsp::Envelope &env = v.env[e];
            int stage = -1;
            switch (env.stage()) {
            case dsp::Envelope::kDelay:
            case dsp::Envelope::kAttack: stage = 0; break;
            case dsp::Envelope::kHold:
            case dsp::Envelope::kDecay: stage = 1; break;
            case dsp::Envelope::kSustain: stage = 2; break;
            case dsp::Envelope::kRelease: stage = 3; break;
            default: break;
            }
            p.stage[e] = static_cast<int8_t>(stage);
            p.level[e] = env.level();
            p.from[e] = env.releaseFrom();
         }
      }
   }

   // The sample being rendered within the current host block, for NOTE_END times.
   void setBlockSample(int s) { blockSample_ = s; }

private:
   // ---- the voice ----

   struct Ramp {
      float v = 0, inc = 0;
      void to(float target, float steps) { inc = (target - v) / steps; }
      void snap(float target) { v = target, inc = 0; }
      float next() { return v += inc; }
   };

   struct Voice {
      bool active = false;
      bool gate = false, sustained = false, fading = false;
      bool fresh = true;
      int part = 0;
      NoteRef ref;
      uint64_t age = 0;
      float velocity = 0, lift = 0;
      // Touch: CLAP tuning in semitones, MIDI bend as -1..1 of the bend range, pressure, timbre.
      float bendSemis = 0, bendNorm = 0, pressure = 0, timbre = 0.5f;
      float touch[kTouchCount] = {};
      float randomValue = 0;
      float pitch = 60, targetPitch = 60;
      dsp::Envelope env[kEnvCount];
      dsp::Envelope::Settings envSettings[kEnvCount];
      float envKey[kEnvCount][5] = {};
      float envOut[kEnvCount] = {}, envScale[kEnvCount] = {1, 1, 1};
      dsp::Lfo lfo[kLfoCount];
      float lfoOut[kLfoCount] = {};
      dsp::Lfo drift;
      dsp::Random random;
      dsp::Noise noise;
      float tune = 0; // Tune Pull, in semitones
      // Oscillators.
      float phase[kOscCount] = {}, out[kOscCount] = {}, last[kOscCount] = {};
      int wave[kOscCount] = {};
      bool sync[kOscCount] = {};
      // Whether each oscillator is heard or modulates something, so a silent one can be skipped.
      bool heard[kOscCount] = {true, true, true};
      bool wrapped = false;
      // Each oscillator's level times its pan, per side.
      Ramp freq[kOscCount], shape[kOscCount], oscL[kOscCount], oscR[kOscCount];
      Ramp fm[kOscCount * kOscCount];
      bool expFm = false;
      Ramp noiseLevel, noiseColor, drive;
      int driveType = 0;
      // Filters: per filter, type and slope, and per channel the stages.
      int ftype[kFilterCount] = {}, fstages[kFilterCount] = {1, 1};
      bool open[kFilterCount] = {};
      Ramp g[kFilterCount], fdrive[kFilterCount], comb[kFilterCount], res[kFilterCount];
      float ks[kFilterCount][4] = {{1.414f, 1.414f, 1.414f, 1.414f}, {1.414f, 1.414f, 1.414f, 1.414f}};
      dsp::Svf svf[kFilterCount][2][4];
      dsp::Ladder ladder[kFilterCount][2];
      dsp::Comb comb_[kFilterCount][2];
      int routing = 0;
      Ramp balance;
      bool stereo = false;
      // Amp: level, velocity and pan as one gain per side, times Env 1 and the steal fade.
      Ramp ampL, ampR;
      float fade = 1.0f, fadeStep = 0;
      // Who it hears: neighbours' Osc 1 into Osc 2, and a hunter's Osc 1 ringing it.
      int neighbour[kNeighbours] = {-1, -1, -1};
      Ramp neighbourWeight[kNeighbours];
      Ramp neighbourFm;
      int hunter = -1;
      Ramp bite;
      float flockSources[8] = {};

      void clear() {
         gate = sustained = fading = false;
         fresh = true;
         for (int e = 0; e < kEnvCount; ++e) {
            env[e].reset();
            envOut[e] = 0;
         }
         for (int o = 0; o < kOscCount; ++o)
            phase[o] = out[o] = last[o] = 0;
         for (auto &f : svf)
            for (auto &c : f)
               for (dsp::Svf &s : c)
                  s.reset();
         for (auto &f : ladder)
            for (dsp::Ladder &l : f)
               l.reset();
         for (auto &f : comb_)
            for (dsp::Comb &c : f)
               c.reset();
         noise.reset();
         fade = 1;
         fadeStep = 0;
         tune = 0;
         for (int n = 0; n < kNeighbours; ++n) {
            neighbour[n] = -1;
            neighbourWeight[n].snap(0);
         }
         hunter = -1;
         bite.snap(0);
      }
   };
   // ---- params ----

   double raw(int index) const {
      if (index < 0)
         return 0.0;
      const jclap::core::ParamDef &d = kParams()[index].def;
      return std::clamp(params_.values[index].load(std::memory_order_relaxed) +
                            params_.mods[index].load(std::memory_order_relaxed),
                        d.min, d.max);
   }
   double global(clap_id id) const { return raw(paramIndex(id)); }
   // A part param's value, smoothed at control rate (a choice is never smoothed).
   float plain(int part, clap_id offset) const { return cur_[part][offset]; }
   int choice(int part, clap_id offset) const { return static_cast<int>(cur_[part][offset] + 0.5f); }
   int playMode() const { return static_cast<int>(global(kPlayMode) + 0.5); }

   // A destination's value normalised to 0..1, and back: Ratio in log space, Fixed already is.
   float norm(clap_id offset, float v) const {
      if (isRatio_[offset])
         return std::log(v / 0.25f) * (1.0f / std::log(64.0f));
      return (v - min_[offset]) / span_[offset];
   }
   float denorm(clap_id offset, float n) const {
      n = std::clamp(n, 0.0f, 1.0f);
      if (isRatio_[offset])
         return 0.25f * std::exp(n * std::log(64.0f));
      return min_[offset] + n * span_[offset];
   }

   // Reads every part param, smoothing the continuous ones over about 10 ms.
   void readParams() {
      const float tickSeconds = kTick / static_cast<float>(sampleRate_);
      const float a = first_ ? 1.0f : 1.0f - std::exp(-tickSeconds / 0.010f);
      for (int part = 0; part < kPartCount; ++part)
         for (clap_id o = 0; o < kPartEnd; ++o) {
            const int i = partIndex_[part][o];
            if (i < 0)
               continue;
            const float v = static_cast<float>(raw(i));
            if (kParams()[i].def.flags & CLAP_PARAM_IS_STEPPED)
               cur_[part][o] = v;
            else
               cur_[part][o] += (v - cur_[part][o]) * a;
         }
      first_ = false;
      factor_ = 1 << std::clamp(static_cast<int>(global(kQuality) + 0.5), 0, 2);
   }

   // The used routes, rebuilt when any route param changes.
   struct Route {
      int source, dest;
      float amount;
      int index; // which route, for its amount's own modulation
   };
   void compileRoutes() {
      for (int part = 0; part < kPartCount; ++part) {
         bool changed = false;
         for (int r = 0; r < kRouteCount; ++r)
            for (int f = 0; f < 3; ++f) {
               const float v = static_cast<float>(raw(partIndex_[part][routeParam(r, static_cast<RouteParam>(f))]));
               if (v != routeCache_[part][r][f]) {
                  routeCache_[part][r][f] = v;
                  changed = true;
               }
            }
         if (!changed && routesBuilt_)
            continue;
         int n = 0;
         for (int r = 0; r < kRouteCount; ++r) {
            const int src = static_cast<int>(routeCache_[part][r][0] + 0.5f);
            const int dst = static_cast<int>(routeCache_[part][r][1] + 0.5f);
            if (src <= kSourceOff || src >= kSourceCount || dst < 0 || dst >= destCount_)
               continue;
            routes_[part][n++] = {src, dst, routeCache_[part][r][2], r};
         }
         routeCount_[part] = n;
      }
      routesBuilt_ = true;
   }

   // A source's value for voice `v` (unipolar 0..1, or -1..1 for the bipolar ones).
   float source(const Voice &v, int s) const {
      if (s >= kSourceEnv1 && s < kSourceEnv1 + kEnvCount)
         return v.envOut[s - kSourceEnv1];
      if (s >= kSourceWingbeat && s < kSourceWingbeat + kLfoCount)
         return v.lfoOut[s - kSourceWingbeat];
      if (s >= kSourceStrike && s < kSourceStrike + kTouchCount)
         return v.touch[s - kSourceStrike];
      if (s == kSourceKey)
         return std::clamp((v.ref.key - 60) / 48.0f, -1.0f, 1.0f);
      if (s >= kSourceSpeed && s <= kSourceSkyY)
         return v.flockSources[s - kSourceSpeed];
      if (s == kSourceRandom)
         return v.randomValue;
      if (s == kSourceModWheel)
         return modWheel_;
      if (s >= kSourceMacro1 && s < kSourceMacro1 + kMacroCount)
         return static_cast<float>(global(kMacro1 + (s - kSourceMacro1)));
      return 0.0f;
   }

   // Every route's offset on voice `v`, by destination, in normalised units.
   void evaluateRoutes(const Voice &v, float *offset) const {
      for (int d = 0; d < destCount_; ++d)
         offset[d] = 0;
      const int part = v.part;
      const int n = routeCount_[part];
      // Pass 1: routes onto other routes' amounts, at their own unmodulated amounts.
      for (int i = 0; i < n; ++i) {
         const Route &r = routes_[part][i];
         if (r.dest >= firstAmountDest_)
            offset[r.dest] += source(v, r.source) * r.amount * 0.5f; // amount's range is 2 wide
      }
      // Pass 2: everything else, each at its amount moved by pass 1.
      for (int i = 0; i < n; ++i) {
         const Route &r = routes_[part][i];
         if (r.dest >= firstAmountDest_)
            continue;
         const float amount = std::clamp(r.amount + 2.0f * offset[firstAmountDest_ + r.index], -1.0f, 1.0f);
         offset[r.dest] += source(v, r.source) * amount;
      }
   }

   // A part param on voice `v`, with its routes.
   float mod(const Voice &v, const float *offset, clap_id o) const {
      const int d = destOf_[o];
      const float base = plain(v.part, o);
      if (d < 0 || offset[d] == 0.0f)
         return base;
      return denorm(o, norm(o, base) + offset[d]);
   }

   // ---- the control tick ----

   void tick() {
      readParams();
      compileRoutes();
      const float tickSeconds = kTick / static_cast<float>(sampleRate_);
      // The touch curves and smoothing, the same for every voice.
      for (int t = 0; t < kTouchCount; ++t) {
         touchCurve_[t] = static_cast<float>(global(kTouchCurve1 + t));
         const float smooth = static_cast<float>(timeMs(global(kTouchSmooth1 + t))) / 1000.0f;
         touchCoef_[t] = smooth <= 0.0f ? 1.0f : 1.0f - std::exp(-tickSeconds / smooth);
      }
      const int steps = kTick * factor_;

      // The part-wide LFOs (Global trigger), and the wind that pushes each species.
      for (int part = 0; part < kPartCount; ++part)
         for (int l = 0; l < kLfoCount; ++l) {
            const float hz = lfoRate(part, l, nullptr, nullptr);
            float v = partLfo_[part][l].process(tickSeconds, hz, choice(part, lfoParam(l, kLfoShape)), 0.0f);
            if (choice(part, lfoParam(l, kLfoPolarity)) == 1)
               v = 0.5f + 0.5f * v;
            partLfoOut_[part][l] = v;
         }
      for (int part = 0; part < kPartCount; ++part)
         flock_.gust[part] = partLfoOut_[part][1];
      flock_.size = static_cast<float>(global(kSkySize));
      flock_.walls = static_cast<int>(global(kSkyWalls) + 0.5);

      // The resting creatures fly by their species' own params.
      for (int i = kVoiceSlots; i < kCreatureSlots; ++i) {
         Creature &c = flock_.creatures[i];
         c.params.read([&](clap_id o) { return plain(c.part, o); });
      }

      float offset[256];
      for (int i = 0; i < kVoiceSlots; ++i) {
         Voice &v = voices_[i];
         if (!v.active)
            continue;
         controlVoice(i, v, offset, tickSeconds, steps);
      }

      // The flock, at its fixed rate counted in whole samples.
      flockAcc_ += static_cast<int64_t>(kTick) * static_cast<int64_t>(kFlockRate);
      const int64_t sr = static_cast<int64_t>(std::llround(sampleRate_));
      while (flockAcc_ >= sr) {
         flockAcc_ -= sr;
         flock_.step();
      }

      // Main bus settings.
      width_.to(static_cast<float>(global(kOutputWidth)), static_cast<float>(kTick));
      master_.to(static_cast<float>(std::pow(10.0, global(kMasterLevel) / 20.0)), static_cast<float>(kTick));
      ++frames_;
   }

   float lfoRate(int part, int l, const Voice *v, const float *offset) const {
      if (choice(part, lfoParam(l, kLfoSync)) == 1) {
         static const float beats[] = {0.125f, 1.0f / 6, 0.25f, 1.0f / 3, 0.375f, 0.5f, 2.0f / 3, 0.75f,
                                       1.0f,   4.0f / 3, 1.5f, 2.0f, 4.0f, 8.0f, 16.0f};
         const int d = std::clamp(choice(part, lfoParam(l, kLfoDivision)), 0, 14);
         return static_cast<float>(bpm_ / 60.0) / beats[d];
      }
      const float r = v ? mod(*v, offset, lfoParam(l, kLfoRate)) : plain(part, lfoParam(l, kLfoRate));
      return static_cast<float>(lfoHz(r));
   }

   static float curve(float x, float c) {
      // c > 0 bows a touch up (more response early), c < 0 down.
      const float e = std::pow(4.0f, -c);
      return x >= 0.0f ? std::pow(x, e) : -std::pow(-x, e);
   }

   void controlVoice(int slot, Voice &v, float *offset, float tickSeconds, int steps) {
      const int part = v.part;
      const float fs = static_cast<float>(sampleRate_) * static_cast<float>(factor_);
      const float st = static_cast<float>(steps);

      // Touch, through each dimension's curve and smoothing.
      const float bendRange = plain(part, kBendRange);
      const float bend = v.bendSemis + v.bendNorm * bendRange +
                         managerBend_ * static_cast<float>(global(kManagerBendRange));
      const float raws[kTouchCount] = {v.velocity, v.pressure, std::clamp(bend / std::max(1.0f, bendRange), -1.0f, 1.0f),
                                       v.timbre, v.lift};
      for (int t = 0; t < kTouchCount; ++t) {
         const float target = touchCurve_[t] == 0.0f ? raws[t] : curve(raws[t], touchCurve_[t]);
         v.touch[t] += (target - v.touch[t]) * (v.fresh ? 1.0f : touchCoef_[t]);
      }

      // The creature: its sources in, its lure and press out.
      Creature &c = flock_.creatures[slot];
      if (c.alive && c.part == part) {
         const CreatureSources &s = c.sources;
         const float src[8] = {s.speed, s.crowd, s.nearest, s.heading, s.caught, s.wake, s.x, s.y};
         const float a = v.fresh ? 1.0f : 1.0f - std::exp(-tickSeconds / 0.006f);
         for (int k = 0; k < 8; ++k)
            v.flockSources[k] += (src[k] - v.flockSources[k]) * a;
         lureFor(static_cast<float>(v.ref.key), bend, v.touch[3], c.lureX, c.lureY);
         c.press = v.touch[1];
      }

      // Routes, from the sources as they were at the last tick; then envelopes 2 and 3 at control
      // rate (Env 1 runs every sample) and the LFOs.
      evaluateRoutes(v, offset);
      for (int e = 0; e < kEnvCount; ++e)
         setEnvelope(v, e, offset);
      for (int e = 1; e < kEnvCount; ++e)
         v.envOut[e] = v.env[e].process(tickSeconds) * v.envScale[e];
      for (int l = 0; l < kLfoCount; ++l) {
         float out;
         if (choice(part, lfoParam(l, kLfoTrigger)) == 2)
            out = partLfoOut_[part][l];
         else {
            const float delay = static_cast<float>(timeMs(mod(v, offset, lfoParam(l, kLfoDelay)))) / 1000.0f;
            out = v.lfo[l].process(tickSeconds, lfoRate(part, l, &v, offset), choice(part, lfoParam(l, kLfoShape)), delay);
            if (choice(part, lfoParam(l, kLfoPolarity)) == 1)
               out = 0.5f + 0.5f * out;
         }
         v.lfoOut[l] = out;
      }
      // The creature's own flight follows its routes too: Press can make it faster, say.
      if (c.alive && c.part == part) {
         c.params.read([&](clap_id o) { return mod(v, offset, o); });
         c.wing = v.lfoOut[0];
      }

      // Pitch: the note, transpose, fine, glide, bend, drift and Tune Pull.
      const float glide = static_cast<float>(timeMs(mod(v, offset, kGlideTime))) / 1000.0f;
      const float rounding = static_cast<float>(global(kGlideRounding));
      float target = v.targetPitch;
      if (glide <= 0.0f || v.fresh)
         v.pitch = target;
      else
         v.pitch += (target - v.pitch) * (1.0f - std::exp(-tickSeconds / (0.25f * glide)));
      float bendNow = bend;
      if (rounding > 0.0f) {
         // Glide Rounding: a resting finger settles on the nearest semitone.
         const float nearest = std::round(bendNow);
         bendNow += (nearest - bendNow) * rounding;
      }
      const float driftAmt = mod(v, offset, kDrift);
      const float drift = v.drift.process(tickSeconds, 0.3f, dsp::Lfo::kSmooth, 0.0f) * driftAmt * 0.15f;
      tunePull(slot, v, offset, tickSeconds);
      const float notePitch = v.pitch + plain(part, kPartTranspose) + mod(v, offset, kPartFine) / 100.0f + drift + v.tune;

      // Oscillators.
      for (int o = 0; o < kOscCount; ++o) {
         const int pm = choice(part, oscParam(o, kOscPitchMode));
         const float oscBend = plain(part, oscParam(o, kOscBend));
         const bool keyTrack = choice(part, oscParam(o, kOscKeyTrack)) == 1;
         float hz;
         const float base = keyTrack ? notePitch + bendNow * oscBend : 60.0f + bendNow * oscBend;
         if (pm == 0)
            hz = 440.0f * std::pow(2.0f, (base + mod(v, offset, oscParam(o, kOscCoarse)) +
                                          mod(v, offset, oscParam(o, kOscFine)) / 100.0f - 69.0f) / 12.0f);
         else if (pm == 1)
            hz = 440.0f * std::pow(2.0f, (base + mod(v, offset, oscParam(o, kOscFine)) / 100.0f - 69.0f) / 12.0f) *
                 mod(v, offset, oscParam(o, kOscRatio));
         else
            hz = static_cast<float>(fixedHz(mod(v, offset, oscParam(o, kOscFixed)))) *
                 std::pow(2.0f, mod(v, offset, oscParam(o, kOscFine)) / 1200.0f);
         hz = std::clamp(hz, 0.0f, 0.45f * fs);
         v.wave[o] = choice(part, oscParam(o, kOscWave));
         v.sync[o] = o > 0 && choice(part, oscParam(o, kOscSync)) == 1;
         ramp(v.freq[o], hz / fs, st, v.fresh);
         ramp(v.shape[o], mod(v, offset, oscParam(o, kOscShape)), st, v.fresh);
         const float lvl = mod(v, offset, mixParam(kMixLevel, o));
         const float pan = mod(v, offset, mixParam(kMixPan, o));
         ramp(v.oscL[o], lvl * std::cos((pan + 1.0f) * 0.25f * dsp::kPi) * 1.41421356f, st, v.fresh);
         ramp(v.oscR[o], lvl * std::sin((pan + 1.0f) * 0.25f * dsp::kPi) * 1.41421356f, st, v.fresh);
      }
      v.stereo = false;
      for (int o = 0; o < kOscCount; ++o)
         v.stereo = v.stereo || std::fabs(plain(part, mixParam(kMixPan, o))) > 1e-3f ||
                    (destOf_[mixParam(kMixPan, o)] >= 0 && offset[destOf_[mixParam(kMixPan, o)]] != 0.0f);
      for (int from = 0; from < kOscCount; ++from)
         for (int to = 0; to < kOscCount; ++to) {
            const float c2 = mod(v, offset, fmCell(from, to));
            ramp(v.fm[from * kOscCount + to], c2 * c2, st, v.fresh);
         }
      v.expFm = choice(part, kFmBase + kFmType) == 1;
      // Heard: it has level, or feeds another that is heard (or the neighbours, for Osc 1). Worked
      // out from the targets and where the ramps are now, so nothing is cut while it fades.
      const bool neighboursListen = true;
      for (int o = 0; o < kOscCount; ++o)
         v.heard[o] = v.oscL[o].v + v.oscR[o].v > 1e-6f || v.oscL[o].inc != 0.0f || v.oscR[o].inc != 0.0f ||
                      (o == 0 && neighboursListen);
      for (int pass = 0; pass < 2; ++pass)
         for (int from = 0; from < kOscCount; ++from)
            for (int to = 0; to < kOscCount; ++to)
               if (from != to && v.heard[to] && (v.fm[from * kOscCount + to].v > 0.0f || v.fm[from * kOscCount + to].inc != 0.0f))
                  v.heard[from] = true;
      ramp(v.noiseLevel, mod(v, offset, mixParam(kNoiseLevel)), st, v.fresh);
      ramp(v.noiseColor, mod(v, offset, mixParam(kNoiseColor)), st, v.fresh);
      v.driveType = choice(part, tailParam(kDriveType));
      ramp(v.drive, mod(v, offset, tailParam(kDriveAmount)), st, v.fresh);

      // Filters.
      v.routing = choice(part, tailParam(kFilterRouting));
      ramp(v.balance, mod(v, offset, tailParam(kFilterBalance)), st, v.fresh);
      for (int f = 0; f < kFilterCount; ++f) {
         const int type = choice(part, filterParam(f, kFilterType));
         const int stages = type <= 2 ? choice(part, filterParam(f, kFilterSlope)) + 1 : 1;
         if (type != v.ftype[f] || stages != v.fstages[f]) {
            for (auto &ch : v.svf[f])
               for (int s = v.fstages[f]; s < 4; ++s)
                  ch[s].reset();
            if (type != v.ftype[f]) {
               for (auto &l : v.ladder[f])
                  l.reset();
               for (auto &cmb : v.comb_[f])
                  cmb.reset();
            }
         }
         v.ftype[f] = type;
         v.fstages[f] = stages;
         const float keyTrack = mod(v, offset, filterParam(f, kFilterKeyTrack));
         float hz = static_cast<float>(cutoffHz(mod(v, offset, filterParam(f, kFilterCutoff)))) *
                    std::pow(2.0f, (notePitch - 60.0f) / 12.0f * keyTrack);
         const float resonance = mod(v, offset, filterParam(f, kFilterResonance));
         if (type == 6 || type == 7) {
            // The comb: tuned to the cutoff, 30 Hz up.
            hz = std::clamp(hz, 30.0f, 0.45f * fs);
            ramp(v.comb[f], fs / hz, st, v.fresh);
            ramp(v.res[f], (type == 6 ? 1.0f : -1.0f) * (0.3f + 0.68f * resonance), st, v.fresh);
         }
         else {
            hz = std::clamp(hz, 10.0f, 0.45f * fs);
            ramp(v.g[f], std::tan(dsp::kPi * hz / fs), st, v.fresh);
            // Each SVF stage's damping: a Butterworth cascade, with the resonance (Q up to 30 times)
            // on the last stage. Set per tick; the cutoff, which moves fastest, ramps per sample.
            static const float qs[4][4] = {{0.7071f}, {0.5412f, 1.3066f}, {0.5176f, 0.7071f, 1.9319f},
                                           {0.5098f, 0.6013f, 0.9000f, 2.5629f}};
            const float boost = std::pow(30.0f, resonance);
            if (type == 3)
               v.ks[f][0] = 1.0f / (0.7071f * boost);
            else if (type <= 2)
               for (int s = 0; s < stages; ++s)
                  v.ks[f][s] = 1.0f / (qs[stages - 1][s] * (s == stages - 1 ? boost : 1.0f));
            ramp(v.res[f], resonance, st, v.fresh);
         }
         const float fdrive = mod(v, offset, filterParam(f, kFilterDrive));
         ramp(v.fdrive[f], 1.0f + 4.0f * fdrive, st, v.fresh);
         // A low pass or ladder opened all the way, undriven and barely resonant, passes the sound
         // through untouched, so it costs nothing. Its state starts clean when it closes again.
         const bool open = (type == 0 || type == 5) && hz >= 18000.0f && fdrive < 1e-3f && resonance < 0.25f;
         if (v.open[f] && !open) {
            for (auto &ch : v.svf[f])
               for (dsp::Svf &sv : ch)
                  sv.reset();
            for (auto &l : v.ladder[f])
               l.reset();
         }
         v.open[f] = open;
      }

      // Amp: level, velocity, the Expression crossfade, and the creature's place in the stereo field.
      const float levelDb = mod(v, offset, kPartLevel);
      const float velAmt = mod(v, offset, kVelocityToAmp);
      float gain = std::pow(10.0f, levelDb / 20.0f) * (1.0f - velAmt + velAmt * v.touch[0]) * 0.35f;
      if (playMode() == 5) {
         const float x = std::clamp(global(kCrossfadeSource) < 0.5 ? v.touch[3] : v.touch[1], 0.0f, 1.0f);
         gain *= part == 0 ? std::cos(x * 0.5f * dsp::kPi) : std::sin(x * 0.5f * dsp::kPi);
      }
      // The gain goes into the pan gains, so the sample loop ramps two values, not three.
      const float xPan = mod(v, offset, kSkyPan);
      const float pan = std::clamp(mod(v, offset, kPartPan) + xPan * (2.0f * v.flockSources[6] - 1.0f), -1.0f, 1.0f);
      ramp(v.ampL, gain * std::cos((pan + 1.0f) * 0.25f * dsp::kPi), st, v.fresh);
      ramp(v.ampR, gain * std::sin((pan + 1.0f) * 0.25f * dsp::kPi), st, v.fresh);

      // Who it hears. A neighbour keeps its slot while it stays near, and a new one only takes a
      // slot whose weight has faded out, so the modulator never jumps.
      const Creature &me = flock_.creatures[slot];
      const float nfm = mod(v, offset, flockParam(kNeighbourFm));
      ramp(v.neighbourFm, nfm * nfm, st, v.fresh);
      float want[kNeighbours] = {};
      int wantSlot[kNeighbours] = {-1, -1, -1};
      if (me.alive && me.part == part)
         for (int n = 0; n < kNeighbours; ++n)
            if (me.neighbour[n] >= 0 && me.neighbour[n] < kVoiceSlots && voices_[me.neighbour[n]].active) {
               wantSlot[n] = me.neighbour[n];
               want[n] = me.neighbourWeight[n];
            }
      const float fadeSteps = std::max(st, 0.010f * fs); // about 10 ms
      for (int n = 0; n < kNeighbours; ++n) {
         int found = -1;
         for (int w = 0; w < kNeighbours; ++w)
            if (wantSlot[w] >= 0 && wantSlot[w] == v.neighbour[n])
               found = w;
         const float target = found >= 0 ? want[found] : 0.0f;
         if (found >= 0)
            wantSlot[found] = -1;
         // Moves at most a full weight per 10 ms.
         v.neighbourWeight[n].inc = std::clamp(target - v.neighbourWeight[n].v, -st / fadeSteps, st / fadeSteps) / st;
      }
      for (int w = 0; w < kNeighbours; ++w) {
         if (wantSlot[w] < 0)
            continue;
         for (int n = 0; n < kNeighbours; ++n)
            if (v.neighbourWeight[n].v < 1e-3f && v.neighbourWeight[n].inc <= 0.0f) {
               v.neighbour[n] = wantSlot[w];
               v.neighbourWeight[n].inc = std::min(want[w], st / fadeSteps) / st;
               break;
            }
      }
      for (int n = 0; n < kNeighbours; ++n)
         if (v.neighbourWeight[n].v < 1e-3f && v.neighbourWeight[n].inc <= 0.0f)
            v.neighbour[n] = -1, v.neighbourWeight[n].snap(0);

      // Bite: the hunter on it, while Caught.
      const int hunter = me.alive && me.hunter >= 0 && me.hunter < kVoiceSlots && voices_[me.hunter].active ? me.hunter : -1;
      const float biteAmt = hunter >= 0 ? plain(voices_[hunter].part, flockParam(kBite)) * v.flockSources[4] : 0.0f;
      if (hunter != v.hunter && v.bite.v > 1e-3f)
         ramp(v.bite, 0.0f, st, false); // let the old one go first
      else {
         if (hunter >= 0)
            v.hunter = hunter;
         ramp(v.bite, v.hunter >= 0 ? biteAmt : 0.0f, st, false);
         if (v.bite.v < 1e-3f && hunter < 0)
            v.hunter = -1;
      }

      v.fresh = false;
   }

   void setEnvelope(Voice &v, int e, const float *offset) {
      dsp::Envelope::Settings &s = v.envSettings[e];
      // The times cost a pow() each, so they are only worked out again when their knob or routes move.
      static const EnvParam times[5] = {kEnvDelay, kEnvAttack, kEnvHold, kEnvDecay, kEnvRelease};
      float *seconds[5] = {&s.delay, &s.attack, &s.hold, &s.decay, &s.release};
      for (int i = 0; i < 5; ++i) {
         const clap_id o = envParam(e, times[i]);
         const float value = offset ? mod(v, offset, o) : plain(v.part, o);
         if (value != v.envKey[e][i] || v.fresh) {
            v.envKey[e][i] = value;
            *seconds[i] = static_cast<float>(timeMs(value)) / 1000.0f;
         }
      }
      s.sustain = offset ? mod(v, offset, envParam(e, kEnvSustain)) : plain(v.part, envParam(e, kEnvSustain));
      s.attackCurve = plain(v.part, envParam(e, kEnvAttackCurve));
      s.decayCurve = plain(v.part, envParam(e, kEnvDecayCurve));
      s.releaseCurve = plain(v.part, envParam(e, kEnvReleaseCurve));
      s.loop = choice(v.part, envParam(e, kEnvLoop));
      v.env[e].set(s);
      // Env Velocity scales the whole envelope by the strike.
      const float vel = offset ? mod(v, offset, envParam(e, kEnvVelocity)) : plain(v.part, envParam(e, kEnvVelocity));
      v.envScale[e] = 1.0f - vel + vel * v.touch[0];
   }

   // Tune Pull: creatures flying the same way as this one drift toward a just interval from them.
   void tunePull(int slot, Voice &v, const float *offset, float tickSeconds) {
      const float amount = mod(v, offset, flockParam(kTunePull));
      const Creature &me = flock_.creatures[slot];
      float target = 0, weight = 0;
      if (amount > 0.0f && me.alive) {
         static const float just[] = {0.0f, 315.64f, 386.31f, 498.04f, 701.96f, 884.36f, 1200.0f};
         const float mv = std::sqrt(me.vx * me.vx + me.vy * me.vy) + 1e-6f;
         for (int n = 0; n < kNeighbours; ++n) {
            const int j = me.neighbour[n];
            if (j < 0 || j >= kVoiceSlots || !voices_[j].active)
               continue;
            const Creature &o = flock_.creatures[j];
            const float ov = std::sqrt(o.vx * o.vx + o.vy * o.vy) + 1e-6f;
            if ((me.vx * o.vx + me.vy * o.vy) / (mv * ov) < 0.866f) // within 30 degrees
               continue;
            // Their unpulled pitch, so two creatures don't chase each other.
            const float cents = (v.pitch - voices_[j].pitch) * 100.0f;
            const float within = cents - 1200.0f * std::floor(cents / 1200.0f);
            float best = 0, bestErr = 1e9f;
            for (float j2 : just)
               if (std::fabs(j2 - within) < bestErr)
                  bestErr = std::fabs(j2 - within), best = j2;
            target += (best - within) * me.neighbourWeight[n];
            weight += me.neighbourWeight[n];
         }
         if (weight > 0.0f)
            target = std::clamp(target / weight, -30.0f, 30.0f) * amount / 100.0f;
      }
      v.tune += (target - v.tune) * (1.0f - std::exp(-tickSeconds / 0.15f));
   }

   static void ramp(Ramp &r, float target, float steps, bool snap) {
      if (snap)
         r.snap(target);
      else
         r.to(target, steps);
   }

   // ---- one sample ----

   void renderSample(float *const *const *out, int n) {
      float bus[kPartCount][2][kMaxOversample] = {};
      const float dtBase = 1.0f / static_cast<float>(sampleRate_);
      for (int i = 0; i < kVoiceSlots; ++i) {
         Voice &v = voices_[i];
         if (!v.active)
            continue;
         // Env 1, the amp, every sample.
         v.envOut[0] = v.env[0].process(dtBase) * v.envScale[0];
         if (v.fading) {
            v.fade -= v.fadeStep;
            if (v.fade <= 0.0f) {
               end(i);
               continue;
            }
         }
         if (!v.env[0].active()) {
            end(i);
            continue;
         }
      }
      for (int s = 0; s < factor_; ++s) {
         for (int i = 0; i < kVoiceSlots; ++i) {
            Voice &v = voices_[i];
            if (!v.active)
               continue;
            float l, r;
            voiceSample(i, v, l, r);
            bus[v.part][0][s] += l;
            bus[v.part][1][s] += r;
         }
         // Everyone has run this sample: what they played is what the others hear next.
         for (int i = 0; i < kVoiceSlots; ++i)
            if (voices_[i].active)
               oscOut_[0][i] = oscOut_[1][i];
      }

      float mainL = 0, mainR = 0;
      for (int part = 0; part < kPartCount; ++part) {
         float lr[2];
         for (int c = 0; c < 2; ++c) {
            float *x = bus[part][c];
            if (factor_ == 4) {
               const float a = halfBands_[part][c][0].process(x[0], x[1]);
               const float b = halfBands_[part][c][0].process(x[2], x[3]);
               lr[c] = halfBands_[part][c][1].process(a, b);
            }
            else if (factor_ == 2)
               lr[c] = halfBands_[part][c][1].process(x[0], x[1]);
            else
               lr[c] = x[0];
            // A gentle DC block (drive and the asymmetric waves leave some).
            const float y = lr[c] - dcIn_[part][c] + 0.9995f * dc_[part][c];
            dcIn_[part][c] = lr[c];
            dc_[part][c] = y;
            lr[c] = y;
         }
         if (choice(part, kPartOutput) == 1 && out[part + 1]) {
            out[part + 1][0][n] = lr[0];
            out[part + 1][1][n] = lr[1];
         }
         else {
            mainL += lr[0];
            mainR += lr[1];
         }
      }
      // Width (mid/side), master, and a soft safety limit.
      const float w = width_.next(), m = master_.next();
      const float mid = 0.5f * (mainL + mainR), side = 0.5f * (mainL - mainR) * w;
      out[0][0][n] = limit((mid + side) * m);
      out[0][1][n] = limit((mid - side) * m);
   }

   static float limit(float x) {
      const float a = std::fabs(x);
      if (a <= 0.8f)
         return x;
      return std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), x);
   }

   static float frac(float x) { return dsp::frac(x); }

   void voiceSample(int slot, Voice &v, float &outL, float &outR) {
      // The oscillators, 3 then 2 then 1: a later one hears an earlier one's sample now, and the rest
      // (and itself, the feedback) a sample ago.
      float f[kOscCount], sh[kOscCount], c[kOscCount * kOscCount];
      for (int o = 0; o < kOscCount; ++o) {
         f[o] = v.freq[o].next();
         sh[o] = v.shape[o].next();
      }
      bool anyFm = false;
      for (int i = 0; i < kOscCount * kOscCount; ++i) {
         c[i] = v.fm[i].next();
         anyFm = anyFm || c[i] != 0.0f;
      }
      // Neighbour FM: their Osc 1 a sample ago, weighted.
      float neighbour = 0;
      const float nfm = v.neighbourFm.next();
      for (int nb = 0; nb < kNeighbours; ++nb) {
         const float w = v.neighbourWeight[nb].next();
         if (v.neighbour[nb] >= 0)
            neighbour += w * oscOut_[0][v.neighbour[nb]];
      }
      neighbour *= nfm;
      float now[kOscCount] = {v.out[0], v.out[1], v.out[2]};
      bool wrapped = false;
      for (int o = kOscCount - 1; o >= 0; --o) {
         if (!v.heard[o]) {
            // Silent and modulating nothing: only its phase moves (Osc 1's wrap still syncs).
            v.phase[o] += f[o];
            if (v.phase[o] >= 1.0f) {
               v.phase[o] -= 1.0f;
               wrapped = wrapped || o == 0;
            }
            now[o] = 0;
            continue;
         }
         float m = 0;
         for (int from = 0; anyFm && from < kOscCount; ++from) {
            const float amt = c[from * kOscCount + o];
            if (amt == 0.0f)
               continue;
            // Feedback: the average of its last two samples, which keeps it from going to noise.
            const float src = from == o ? 0.5f * (v.out[o] + v.last[o]) : now[from];
            m += amt * src * (from == o ? 0.5f : 2.0f);
         }
         if (o == 1)
            m += neighbour * 2.0f;
         float inc = f[o];
         float ph;
         if (v.expFm) {
            // Exponential FM: the modulator moves the pitch, up to 4 octaves either way per unit.
            inc *= std::exp2(std::clamp(2.0f * m, -4.0f, 4.0f));
            inc = std::min(inc, 0.45f);
            ph = v.phase[o];
         }
         else
            ph = frac(v.phase[o] + m);
         const float noiseWave = v.wave[o] == dsp::kWhite ? v.noise.white() : v.wave[o] == dsp::kPink ? v.noise.pink() : 0.0f;
         const float y = v.wave[o] >= dsp::kWhite ? noiseWave : dsp::wave(v.wave[o], ph, inc, sh[o]);
         v.last[o] = v.out[o];
         now[o] = y;
         v.phase[o] += inc;
         if (v.phase[o] >= 1.0f) {
            v.phase[o] = dsp::frac(v.phase[o]);
            if (o == 0)
               wrapped = true;
         }
         // Sync: Osc 2 and 3 restart on Osc 1's wrap a sample ago.
         if (o > 0 && v.sync[o] && v.wrapped)
            v.phase[o] = 0;
      }
      v.wrapped = wrapped;
      for (int o = 0; o < kOscCount; ++o)
         v.out[o] = now[o];
      oscOut_[1][slot] = now[0];

      // The mix: each oscillator panned, then noise.
      float pl[kOscCount], pr[kOscCount];
      for (int o = 0; o < kOscCount; ++o) {
         pl[o] = v.oscL[o].next();
         pr[o] = v.oscR[o].next();
      }
      const float nl = v.noiseLevel.next(), nc = v.noiseColor.next();
      float noise = 0;
      if (nl > 1e-4f) {
         const float wn = v.noise.white(), pn = v.noise.pink() * 2.5f;
         noise = (pn + (wn - pn) * (0.5f + 0.5f * nc)) * nl;
      }
      // Split sends Osc 1 to Filter 1 and the rest to Filter 2.
      const bool split = v.routing == 2;
      float aL = 0, aR = 0, bL = 0, bR = 0;
      for (int o = 0; o < kOscCount; ++o) {
         const float s = now[o];
         if (split && o > 0)
            bL += s * pl[o], bR += s * pr[o];
         else
            aL += s * pl[o], aR += s * pr[o];
      }
      if (split)
         bL += noise, bR += noise;
      else
         aL += noise, aR += noise;

      // Drive.
      const float d = v.drive.next();
      const float dg = 1.0f + 15.0f * d * d;
      const auto drive = [&](float x) {
         if (d < 1e-4f)
            return x;
         x *= dg;
         const float y = v.driveType == 0 ? dsp::fastTanh(x) : v.driveType == 1 ? dsp::hardClip(x) : dsp::tube(x);
         return y / std::sqrt(dg);
      };

      float g[kFilterCount], fd[kFilterCount], cd[kFilterCount], rs[kFilterCount];
      for (int fi = 0; fi < kFilterCount; ++fi) {
         g[fi] = v.g[fi].next();
         fd[fi] = v.fdrive[fi].next();
         cd[fi] = v.comb[fi].next();
         rs[fi] = v.res[fi].next();
      }
      const float bal = v.balance.next();
      const int channels = v.stereo ? 2 : 1;
      float y[2] = {0, 0};
      for (int ch = 0; ch < channels; ++ch) {
         float a = drive(ch == 0 ? (channels == 1 ? 0.5f * (aL + aR) : aL) : aR);
         float b = drive(ch == 0 ? (channels == 1 ? 0.5f * (bL + bR) : bL) : bR);
         float o;
         if (v.routing == 0)
            o = filter(v, 1, ch, filter(v, 0, ch, a, g[0], fd[0], cd[0], rs[0]), g[1], fd[1], cd[1], rs[1]);
         else {
            const float f1 = filter(v, 0, ch, a, g[0], fd[0], cd[0], rs[0]);
            const float f2 = filter(v, 1, ch, split ? b : a, g[1], fd[1], cd[1], rs[1]);
            const float w2 = 0.5f + 0.5f * bal;
            o = split ? f1 * std::min(1.0f, 2.0f - 2.0f * w2) + f2 * std::min(1.0f, 2.0f * w2) : f1 * (1.0f - w2) + f2 * w2;
         }
         y[ch] = o;
      }
      if (channels == 1)
         y[1] = y[0];

      // Bite: a hunter's Osc 1 rings it.
      const float bite = v.bite.next();
      if (bite > 1e-4f && v.hunter >= 0) {
         const float ring = oscOut_[0][v.hunter];
         for (float &s : y)
            s = s * (1.0f - bite) + s * ring * 1.5f * bite;
      }

      const float amp = v.envOut[0] * v.fade;
      const float L = v.ampL.next(), R = v.ampR.next();
      outL = y[0] * amp * L;
      outR = y[1] * amp * R;
      if (!std::isfinite(outL) || !std::isfinite(outR)) {
         // Something blew up (feedback, a ladder at full resonance): this voice starts clean.
         v.clear();
         v.env[0].reset();
         outL = outR = 0;
      }
   }

   float filter(Voice &v, int f, int ch, float x, float g, float drive, float combDelay, float res) {
      if (v.open[f])
         return x;
      if (drive > 1.001f)
         x = dsp::fastTanh(x * drive) / std::sqrt(drive);
      switch (v.ftype[f]) {
      case 0: // low pass
      case 1: // high pass
      case 2: { // band pass
         const int n = v.fstages[f];
         for (int s = 0; s < n; ++s) {
            const float ks = v.ks[f][s];
            float lp, bp, hp;
            v.svf[f][ch][s].process(x, g, ks, lp, bp, hp);
            x = v.ftype[f] == 0 ? lp : v.ftype[f] == 1 ? hp : bp * ks;
         }
         return x;
      }
      case 3: { // notch
         float lp, bp, hp;
         v.svf[f][ch][0].process(x, g, v.ks[f][0], lp, bp, hp);
         return lp + hp;
      }
      case 4: { // peak: the band boosted by the resonance
         float lp, bp, hp;
         v.svf[f][ch][0].process(x, g, 1.0f, lp, bp, hp);
         return x + bp * 4.0f * res;
      }
      case 5: // ladder
         return v.ladder[f][ch].process(x, g, res * 1.02f);
      default: // comb + / -
         return v.comb_[f][ch].process(x, combDelay, res);
      }
   }

   // ---- voices ----

   // Whether voice note `a` is the one `b` names (-1 fields match any). The panel's notes and the
   // host's never match each other.
   bool matches(const NoteRef &a, const NoteRef &b) const {
      if (a.host != b.host)
         return false;
      if (b.noteId != -1 && a.noteId != -1)
         return a.noteId == b.noteId;
      return (b.key == -1 || a.key == b.key) && (b.channel == -1 || a.channel == b.channel) &&
             (b.port == -1 || a.port == b.port);
   }

   bool plays(int part, int mode, const NoteRef &ref, float velocity) const {
      const int lo = static_cast<int>(plain(part, kKeyLow) + 0.5f), hi = static_cast<int>(plain(part, kKeyHigh) + 0.5f);
      if (ref.key < lo || ref.key > hi)
         return false;
      switch (mode) {
      case 0: // Single
         return part == 0;
      case 2: // Split: B below the split key, A from it up
         return (ref.key < static_cast<int>(global(kSplitKey) + 0.5)) == (part == 1);
      case 3: // MPE Zones: member channels 2-8 play A, 9-16 play B; channel 1 plays both
         return ref.channel <= 0 || (ref.channel < 8) == (part == 0);
      case 4: // Velocity: B for hard hits
         return (velocity * 127.0f >= static_cast<float>(global(kVelocitySplit))) == (part == 1);
      default: // Layer and Expression play both
         return true;
      }
   }

   const Voice *newest(int part) const {
      const Voice *best = nullptr;
      for (const Voice &v : voices_)
         if (v.active && v.part == part && (!best || (v.gate && !best->gate) || (v.gate == best->gate && v.age > best->age)))
            best = &v;
      return best;
   }

   // Starts a voice for `ref` in `part`, stealing if the part is at its polyphony. `reuse` is a voice
   // to restart in place (mono), or -1.
   int start(int part, const NoteRef &ref, float velocity, float bendNorm, float pressure, float timbre, int reuse) {
      int slot = reuse;
      if (slot < 0) {
         const int poly = std::clamp(static_cast<int>(plain(part, kPolyphony) + 0.5f), 1, kMaxPolyphony);
         int sounding = 0;
         for (const Voice &v : voices_)
            sounding += v.active && !v.fading && v.part == part;
         if (sounding >= poly) {
            // Steal: the oldest released voice in this part, else the oldest held.
            int victim = -1;
            for (int i = 0; i < kVoiceSlots; ++i) {
               const Voice &v = voices_[i];
               if (!v.active || v.fading || v.part != part)
                  continue;
               if (victim < 0 || (v.gate < voices_[victim].gate) ||
                   (v.gate == voices_[victim].gate && v.age < voices_[victim].age))
                  victim = i;
            }
            if (victim >= 0) {
               voices_[victim].fading = true;
               voices_[victim].fadeStep = 1.0f / (0.003f * static_cast<float>(sampleRate_));
            }
         }
         for (int i = 0; i < kVoiceSlots && slot < 0; ++i)
            if (!voices_[i].active)
               slot = i;
         if (slot < 0) {
            // Every slot busy (fades included): cut the oldest at once.
            for (int i = 0; i < kVoiceSlots; ++i)
               if (slot < 0 || voices_[i].age < voices_[slot].age)
                  slot = i;
            end(slot);
         }
      }
      Voice &v = voices_[slot];
      const bool restart = reuse >= 0;
      if (!restart)
         v.clear();
      v.active = true;
      v.gate = true;
      v.sustained = false;
      v.part = part;
      v.ref = ref;
      v.age = ++age_;
      v.velocity = velocity;
      v.lift = 0;
      v.bendSemis = 0;
      v.bendNorm = bendNorm;
      v.pressure = pressure;
      v.timbre = timbre;
      v.randomValue = v.random.bipolar();
      v.targetPitch = static_cast<float>(ref.key);
      // Glide Mode Legato only glides from a note still held.
      const bool legatoGlide = choice(part, kGlideMode) == 1;
      if (!restart) {
         float from = static_cast<float>(ref.key);
         if (!legatoGlide || anyHeld(part, slot))
            from = lastPitch_[part] >= 0.0f ? lastPitch_[part] : from;
         v.pitch = from;
      }
      lastPitch_[part] = v.targetPitch;
      if (!restart) {
         for (int o = 0; o < kOscCount; ++o)
            if (choice(part, oscParam(o, kOscRetrigger)) == 1)
               v.phase[o] = plain(part, oscParam(o, kOscPhase)) / 360.0f;
      }
      for (int e = 0; e < kEnvCount; ++e) {
         setEnvelope(v, e, nullptr);
         v.env[e].gateOn();
      }
      for (int l = 0; l < kLfoCount; ++l) {
         const int trig = choice(part, lfoParam(l, kLfoTrigger));
         const float ph = plain(part, lfoParam(l, kLfoPhase)) / 360.0f;
         if (trig == 0)
            v.lfo[l].restart(ph);
         else if (trig == 1)
            v.lfo[l].restart(partLfo_[part][l].phase() + ph);
      }
      if (!restart) {
         float lx, ly;
         lureFor(static_cast<float>(ref.key), 0.0f, timbre, lx, ly);
         flock_.hatch(slot, part, lx, ly);
         flock_.creatures[slot].params.read([&](clap_id o) { return plain(part, o); });
      }
      else
         flock_.creatures[slot].held = true;
      // Its settings now, rather than at the next tick: it plays from this sample.
      if (!routesBuilt_)
         compileRoutes();
      float offset[256];
      v.fresh = true;
      controlVoice(slot, v, offset, kTick / static_cast<float>(sampleRate_), kTick * factor_);
      return slot;
   }

   bool anyHeld(int part, int except) const {
      for (int i = 0; i < kVoiceSlots; ++i)
         if (i != except && voices_[i].active && voices_[i].gate && voices_[i].part == part)
            return true;
      return false;
   }

   void gateOff(Voice &v) {
      v.gate = false;
      for (int e = 0; e < kEnvCount; ++e)
         v.env[e].gateOff(e == 0);
      const int slot = static_cast<int>(&v - voices_);
      flock_.release(slot);
   }

   // A voice is done: free its slot, and tell the host if that was its note's last voice.
   void end(int slot) {
      Voice &v = voices_[slot];
      if (!v.active)
         return;
      v.active = false;
      flock_.release(slot);
      if (!v.ref.host)
         return;
      for (int i = 0; i < kVoiceSlots; ++i)
         if (voices_[i].active && voices_[i].ref.host && voices_[i].ref.noteId == v.ref.noteId &&
             voices_[i].ref.key == v.ref.key && voices_[i].ref.channel == v.ref.channel && voices_[i].ref.port == v.ref.port)
            return;
      if (endedCount_ < kMaxEnded)
         ended_[endedCount_++] = {v.ref, blockSample_};
   }

   // Mono and Legato: one voice per part, and a stack of held notes to fall back to.
   void monoOn(int part, const NoteRef &ref, float velocity, float bendNorm, float pressure, float timbre, bool legato) {
      if (stackSize_[part] < kStack)
         stack_[part][stackSize_[part]++] = {ref, velocity};
      int slot = -1;
      for (int i = 0; i < kVoiceSlots; ++i)
         if (voices_[i].active && !voices_[i].fading && voices_[i].part == part)
            slot = i;
      if (slot >= 0 && voices_[slot].gate && legato) {
         // Legato: the held voice moves to the new note without restarting.
         Voice &v = voices_[slot];
         NoteRef old = v.ref;
         v.ref = ref;
         v.targetPitch = static_cast<float>(ref.key);
         lastPitch_[part] = v.targetPitch;
         v.velocity = velocity;
         v.bendNorm = bendNorm;
         v.pressure = pressure;
         v.timbre = timbre;
         noteHandedOver(old);
         return;
      }
      if (slot >= 0) {
         NoteRef old = voices_[slot].ref;
         start(part, ref, velocity, bendNorm, pressure, timbre, slot);
         noteHandedOver(old);
         return;
      }
      start(part, ref, velocity, bendNorm, pressure, timbre, -1);
   }

   void monoOff(int part, const NoteRef &ref, float velocity) {
      int n = 0;
      for (int i = 0; i < stackSize_[part]; ++i)
         if (!matches(stack_[part][i].ref, ref))
            stack_[part][n++] = stack_[part][i];
      stackSize_[part] = n;
      for (int i = 0; i < kVoiceSlots; ++i) {
         Voice &v = voices_[i];
         if (!v.active || v.part != part || !matches(v.ref, ref))
            continue;
         if (n > 0 && !sustain_) {
            // Back to the last note still held.
            const Held &h = stack_[part][n - 1];
            const NoteRef old = v.ref;
            v.ref = h.ref;
            v.targetPitch = static_cast<float>(h.ref.key);
            lastPitch_[part] = v.targetPitch;
            noteHandedOver(old);
            v.lift = velocity;
         }
      }
   }

   // A mono voice moved on from `old`: if no voice plays it now, its note has ended.
   void noteHandedOver(const NoteRef &old) {
      if (!old.host)
         return;
      for (const Voice &v : voices_)
         if (v.active && v.ref.host && v.ref.noteId == old.noteId && v.ref.key == old.key && v.ref.channel == old.channel)
            return;
      if (endedCount_ < kMaxEnded)
         ended_[endedCount_++] = {old, blockSample_};
   }

   // ---- state ----

   double sampleRate_ = 48000.0;
   double bpm_ = 120.0;
   bool playing_ = false;
   uint32_t seed_ = 1;
   ParamSource params_;
   int partIndex_[kPartCount][kPartEnd];
   int destOf_[kPartEnd];
   float min_[kPartEnd], span_[kPartEnd];
   bool isRatio_[kPartEnd];
   clap_id destOffset_[256];
   int destCount_ = 0, firstAmountDest_ = 0;
   float cur_[kPartCount][kPartEnd] = {};
   bool first_ = true;
   float routeCache_[kPartCount][kRouteCount][3] = {};
   bool routesBuilt_ = false;
   Route routes_[kPartCount][kRouteCount];
   int routeCount_[kPartCount] = {};

   Voice voices_[kVoiceSlots];
   Flock flock_;
   dsp::Lfo partLfo_[kPartCount][kLfoCount];
   float partLfoOut_[kPartCount][kLfoCount] = {};
   // Each voice's Osc 1: [0] the last sample (what the others hear), [1] this one.
   float oscOut_[2][kVoiceSlots] = {};
   dsp::HalfBand halfBands_[kPartCount][2][2];
   float dc_[kPartCount][2] = {}, dcIn_[kPartCount][2] = {};
   Ramp width_, master_;

   float touchCurve_[kTouchCount] = {}, touchCoef_[kTouchCount] = {};
   float chanBend_[16] = {}, chanPressure_[16] = {}, chanTimbre_[16] = {};
   float managerBend_ = 0, modWheel_ = 0;
   bool sustain_ = false;
   float lastPitch_[kPartCount] = {-1.0f, -1.0f};

   struct Held {
      NoteRef ref;
      float velocity;
   };
   static constexpr int kStack = 16;
   Held stack_[kPartCount][kStack];
   int stackSize_[kPartCount] = {};

   static constexpr int kMaxEnded = 128;
   Ended ended_[kMaxEnded];
   int endedCount_ = 0;
   int blockSample_ = 0;

   int factor_ = 2;
   int tickPos_ = 0;
   int64_t flockAcc_ = 0;
   uint64_t age_ = 0;
   uint32_t frames_ = 0;
};

} // namespace jsynth
