// JVerb: an algorithmic reverb modulated by a live gravity simulation. Two to six bodies orbit each
// other (orbit.h); each owns a pair of the reverb's delay lines (engine.h), so its distance from the
// centre of mass is heard as the size of its part of the space and its motion as Doppler. Each body
// can also be routed to one more destination, and transients in the input can kick the bodies.
//
// Threads: the audio thread owns the sim and the engine. Once every 32 samples (a control tick) it
// reads the params, steps the sim and updates the engine; every few ticks it publishes an
// OrbitFrame for the panel through a TripleBuffer. The panel's drags come back through an
// SpscQueue. Neither side ever locks or waits, so drawing can't glitch the audio or the other way
// round.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/spsc_queue.h>
#include <jclap/core/triple_buffer.h>
#include <jclap/dsp/denormals.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "editor.h"
#include "engine.h"
#include "orbit.h"
#include "params.h"
#include "shared.h"

namespace {

using namespace jverb;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_REVERB,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr};

// State layout: 4-byte magic, uint32 version, uint32 param count, then one double per param in id
// order. A state from a version with fewer params loads them and leaves the rest at their defaults.
constexpr char kStateMagic[4] = {'J', 'V', 'R', 'B'};
constexpr uint32_t kStateVersion = 1;

// Sim time units per second with the clock free: the figure-eight comes round about every 8 s.
constexpr double kFreeRate = 0.8;
// One lap of the figure-eight, in sim units, for the synced clock.
constexpr double kLap = 6.3259;
// Publish a frame for the panel every this many control ticks (about 375 a second at 48 kHz).
constexpr int kPublishEvery = 4;

// Splits the dry input into three bands and follows each, flagging transients: what the panel's
// dust and ripples follow, and what React kicks the bodies with.
class Analyzer {
public:
   void setSampleRate(double sr) {
      lowSplit_.setCutoff(250.0, sr);
      highSplit_.setCutoff(2500.0, sr);
      const auto coef = [sr](double seconds) { return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sr))); };
      fastAttack_ = coef(0.001);
      fastRelease_ = coef(0.04);
      slowAttack_ = coef(0.04);
      slowRelease_ = coef(0.5);
      holdoff_ = static_cast<int>(0.09 * sr);
      reset();
   }
   void reset() {
      lowSplit_.y = highSplit_.y = 0.0f;
      for (Band &b : bands_)
         b = Band{};
   }
   void process(float l, float r) {
      const float x = 0.5f * (l + r);
      const float low = lowSplit_.lowpass(x);
      const float notHigh = highSplit_.lowpass(x);
      const float split[3] = {low, notHigh - low, x - notHigh};
      for (int i = 0; i < 3; ++i) {
         Band &b = bands_[i];
         const float a = std::fabs(split[i]);
         b.fast += (a > b.fast ? fastAttack_ : fastRelease_) * (a - b.fast);
         b.slow += (a > b.slow ? slowAttack_ : slowRelease_) * (a - b.slow);
         if (b.wait > 0)
            --b.wait;
         else if (b.fast > 2.2f * b.slow + 0.004f) {
            ++b.hits;
            b.strength = std::min(1.0f, 6.0f * b.fast);
            b.wait = holdoff_;
         }
      }
   }
   float level(int band) const { return std::min(1.5f, 5.0f * bands_[band].fast); }
   uint32_t hits(int band) const { return bands_[band].hits; }
   float strength(int band) const { return bands_[band].strength; }

private:
   struct Band {
      float fast = 0.0f, slow = 0.0f, strength = 0.0f;
      uint32_t hits = 0;
      int wait = 0;
   };
   detail::OnePole lowSplit_, highSplit_;
   Band bands_[3];
   float fastAttack_ = 0.1f, fastRelease_ = 0.01f, slowAttack_ = 0.01f, slowRelease_ = 0.001f;
   int holdoff_ = 4000;
};

class JVerb final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit JVerb(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (size_t i = 0; i < kParamCount; ++i) {
         values_[i].store(kParams[i].defaultValue);
         mods_[i].store(0.0);
      }
   }

   bool init() override {
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(host());
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   bool activate(double sampleRate, uint32_t, uint32_t) override {
      sampleRate_ = sampleRate;
      engine_.setSampleRate(sampleRate);
      analyzer_.setSampleRate(sampleRate);
      restartSim();
      tick_ = 0;
      return true;
   }

   void reset() override {
      engine_.reset();
      analyzer_.reset();
      for (int band = 0; band < 3; ++band)
         seenHits_[band] = analyzer_.hits(band);
   }

   const void *extension(const char *id) override {
      if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
         return &audioPortsExtension;
      if (!std::strcmp(id, CLAP_EXT_PARAMS))
         return &paramsExtension;
      if (!std::strcmp(id, CLAP_EXT_STATE))
         return &stateExtension;
      return jclap::ui::ClapGui::extensionFor<JVerb, &JVerb::gui_>(id);
   }

   // An audio effect: no note ports.
   uint32_t notePortsCount(bool) const override { return 0; }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id].load() : 0.0; }
   double paramModulated(clap_id id) const override { return id < kParamCount ? value(id) : 0.0; }
   void beginEdit(clap_id id) override { edits_.begin(id); }
   void setParam(clap_id id, double v) override {
      if (id >= kParamCount || !std::isfinite(v))
         return;
      v = std::clamp(v, kParams[id].min, kParams[id].max);
      values_[id].store(v);
      edits_.value(id, v);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   void setParams(const double *values) override {
      bool changed = false;
      for (clap_id id = 0; id < kParamCount; ++id) {
         if (!std::isfinite(values[id]))
            continue;
         const double v = std::clamp(values[id], kParams[id].min, kParams[id].max);
         if (values_[id].load() == v)
            continue;
         values_[id].store(v);
         pending_[id].store(true, std::memory_order_release);
         changed = true;
      }
      if (changed)
         if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
            hostParams->request_flush(host());
   }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }
   bool readFrame(OrbitFrame &out) override {
      if (!frames_.update())
         return false;
      out = frames_.front();
      return true;
   }
   void send(const OrbitCommand &command) override { commands_.push(command); }
   std::string presetName() const override { return presetName_; }
   void setPresetName(const std::string &name) override { presetName_ = name; }
   void outputPeaks(float &left, float &right) override {
      left = peaks_[0].exchange(0.0f);
      right = peaks_[1].exchange(0.0f);
   }

   // ---- params ----

   uint32_t paramsCount() const override { return kParamCount; }

   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index >= kParamCount)
         return false;
      jclap::core::fillParamInfo(kParams[index], info);
      return true;
   }

   bool paramsValue(clap_id id, double *v) const override {
      if (id >= kParamCount)
         return false;
      *v = values_[id].load();
      return true;
   }

   bool paramsValueToText(clap_id id, double v, char *out, uint32_t size) const override {
      if (id >= kParamCount)
         return false;
      formatParam(id, v, out, size);
      return true;
   }

   bool paramsTextToValue(clap_id id, const char *text, double *v) const override {
      return id < kParamCount && parseParam(id, text, *v);
   }

   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      handleEvents(in);
      drainEdits(out);
   }

   // ---- state ----

   bool stateSave(const clap_ostream_t *stream) override {
      const uint32_t header[2] = {kStateVersion, kParamCount};
      if (!jclap::core::writeAll(stream, kStateMagic, 4) || !jclap::core::writeAll(stream, header, sizeof(header)))
         return false;
      for (size_t i = 0; i < kParamCount; ++i) {
         const double v = values_[i].load();
         if (!jclap::core::writeAll(stream, &v, sizeof(v)))
            return false;
      }
      const uint32_t nameLength = static_cast<uint32_t>(std::min<size_t>(presetName_.size(), 255));
      return jclap::core::writeAll(stream, &nameLength, sizeof(nameLength)) &&
             jclap::core::writeAll(stream, presetName_.data(), nameLength);
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t header[2] = {};
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, header, sizeof(header)) || header[0] != kStateVersion || header[1] > kParamCount)
         return false;
      double v[kParamCount];
      if (!jclap::core::readAll(stream, v, sizeof(double) * header[1]))
         return false;
      for (uint32_t i = 0; i < header[1]; ++i)
         if (!std::isfinite(v[i]))
            return false;
      // The preset's name follows the values; older saves may not have one.
      uint32_t nameLength = 0;
      char name[256];
      if (jclap::core::readAll(stream, &nameLength, sizeof(nameLength)) && nameLength < sizeof(name) &&
          jclap::core::readAll(stream, name, nameLength))
         presetName_.assign(name, nameLength);
      else
         presetName_.clear();
      for (size_t i = 0; i < kParamCount; ++i)
         values_[i].store(i < header[1] ? std::clamp(v[i], kParams[i].min, kParams[i].max) : kParams[i].defaultValue);
      // The orbits restart from the loaded masses and velocities.
      commands_.push({OrbitCommand::Kind::Reset, 0, 0.0f, 0.0f});
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      jclap::dsp::ScopedNoDenormals noDenormals;
      drainEdits(proc->out_events);
      handleEvents(proc->in_events);
      OrbitCommand cmd;
      while (commands_.pop(cmd))
         apply(cmd);
      readTransport(proc->transport);

      if (proc->audio_inputs_count < 1 || proc->audio_outputs_count < 1)
         return CLAP_PROCESS_CONTINUE;
      const clap_audio_buffer_t &in = proc->audio_inputs[0];
      const clap_audio_buffer_t &out = proc->audio_outputs[0];
      if (!in.data32 || !out.data32 || in.channel_count < 1 || out.channel_count < 1) {
         jclap::core::passAudioThrough(proc);
         return CLAP_PROCESS_CONTINUE;
      }
      const float *inL = in.data32[0], *inR = in.data32[in.channel_count > 1 ? 1 : 0];
      float *outL = out.data32[0], *outR = out.data32[out.channel_count > 1 ? 1 : 0];
      float peak[2] = {0.0f, 0.0f};
      bool resetThisBlock = false;
      for (uint32_t i = 0; i < proc->frames_count; ++i) {
         if (tick_ == 0)
            controlTick();
         tick_ = (tick_ + 1) % kControlTick;
         const float l = inL[i], r = inR[i];
         analyzer_.process(l, r);
         float yl, yr, late;
         engine_.process(l, r, yl, yr, late);
         if (!std::isfinite(yl) || !std::isfinite(yr)) {
            // Never pass on a NaN or infinity; start the tank again instead, at most once a block so
            // a source that keeps producing them can't make every sample pay for a reset.
            if (!resetThisBlock) {
               engine_.reset();
               resetThisBlock = true;
            }
            yl = yr = 0.0f;
         }
         lateLevel_ = std::max(lateLevel_ * 0.9995f, late);
         outL[i] = yl;
         outR[i] = yr;
         peak[0] = std::max(peak[0], std::fabs(yl));
         peak[1] = std::max(peak[1], std::fabs(yr));
      }
      for (int ch = 0; ch < 2; ++ch) {
         float seen = peaks_[ch].load(std::memory_order_relaxed);
         while (peak[ch] > seen && !peaks_[ch].compare_exchange_weak(seen, peak[ch])) {
         }
      }
      return CLAP_PROCESS_CONTINUE;
   }

private:
   // Tells the host about the panel's edits: knob gestures in order, then each param a preset load
   // changed, as one edit carrying its latest value.
   void drainEdits(const clap_output_events_t *out) {
      edits_.drain(out);
      for (clap_id id = 0; id < kParamCount; ++id)
         if (pending_[id].load(std::memory_order_relaxed) && pending_[id].exchange(false, std::memory_order_acquire)) {
            jclap::core::pushParamGesture(out, id, true);
            jclap::core::pushParamValue(out, id, values_[id].load());
            jclap::core::pushParamGesture(out, id, false);
         }
   }

   void handleEvents(const clap_input_events_t *in) {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;
         if (h->type == CLAP_EVENT_PARAM_VALUE) {
            auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
            if (ev->param_id < kParamCount && std::isfinite(ev->value))
               values_[ev->param_id].store(std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max));
         }
         else if (h->type == CLAP_EVENT_PARAM_MOD) {
            auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
            if (ev->param_id < kParamCount && std::isfinite(ev->amount))
               mods_[ev->param_id].store(ev->amount);
         }
      }
   }

   void apply(const OrbitCommand &c) {
      const int b = c.body;
      switch (c.kind) {
      case OrbitCommand::Kind::Place:
         sim_.place(b, c.x, c.y);
         break;
      case OrbitCommand::Kind::Velocity:
         sim_.setVelocity(b, c.x, c.y);
         break;
      case OrbitCommand::Kind::Spawn:
         spawnPending_ = true;
         spawnX_ = c.x;
         spawnY_ = c.y;
         break;
      case OrbitCommand::Kind::Reset:
         restartSim();
         break;
      }
   }

   void readTransport(const clap_event_transport_t *t) {
      if (!t)
         return;
      if (t->flags & CLAP_TRANSPORT_HAS_TEMPO)
         tempo_ = std::max(1.0, t->tempo);
      const bool playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
      if (playing && !playing_ && static_cast<Clock>(choice(kClock)) != Clock::Free)
         restartSim();
      playing_ = playing;
   }

   void masses(double *m) const {
      for (int b = 0; b < kMaxBodies; ++b)
         m[b] = value(bodyParam(b, kBody1Mass));
   }

   void restartSim() {
      double m[kMaxBodies], v[kMaxBodies];
      masses(m);
      for (int b = 0; b < kMaxBodies; ++b)
         v[b] = lastVelocity_[b] = value(bodyParam(b, kBody1Velocity));
      sim_.reset(static_cast<int>(value(kBodies) + 0.5), m, v, value(kGravity));
      for (int b = 0; b < kMaxBodies; ++b)
         radius_[b] = b < sim_.count() ? sim_.radius(b) : 1.0;
      ++resets_;
      spawnPending_ = false;
      // The same kicks in the same order after every restart, so two bounces match.
      rng_ = kRngSeed;
      for (int band = 0; band < 3; ++band)
         seenHits_[band] = analyzer_.hits(band);
   }

   int choice(clap_id id) const { return static_cast<int>(value(id) + 0.5); }

   void controlTick() {
      double mass[kMaxBodies];
      masses(mass);
      const double gravity = value(kGravity);

      // Bodies coming and going, one per tick.
      const int want = std::clamp(choice(kBodies), 2, kMaxBodies);
      if (want > sim_.count()) {
         double x, y;
         if (spawnPending_) {
            x = spawnX_, y = spawnY_;
            spawnPending_ = false;
         }
         else
            sim_.spawnPoint(x, y);
         const int b = sim_.count();
         sim_.add(x, y, mass, gravity);
         lastVelocity_[b] = value(bodyParam(b, kBody1Velocity));
         radius_[b] = sim_.radius(b);
      }
      else if (want < sim_.count())
         sim_.remove();

      // A velocity param that moved scales that body's speed by as much.
      for (int b = 0; b < sim_.count(); ++b) {
         const double v = value(bodyParam(b, kBody1Velocity));
         if (v != lastVelocity_[b]) {
            sim_.scaleVelocity(b, v / lastVelocity_[b]);
            lastVelocity_[b] = v;
         }
      }

      // Transients kick the bodies that follow their band, in a direction from a fixed sequence so
      // a render comes out the same every time.
      const double react = value(kReact);
      for (int band = 0; band < 3; ++band) {
         const uint32_t hits = analyzer_.hits(band);
         if (hits != seenHits_[band]) {
            seenHits_[band] = hits;
            if (react > 0.0)
               for (int b = band; b < sim_.count(); b += 3) {
                  const double angle = random() * 2.0 * jclap::dsp::kPi;
                  const double dv = 0.35 * react * analyzer_.strength(band) * std::sqrt(gravity);
                  sim_.kick(b, dv * std::cos(angle), dv * std::sin(angle));
               }
         }
      }

      // Step the orbits.
      const bool run = value(kRun) >= 0.5;
      double rate = kFreeRate;
      if (static_cast<Clock>(choice(kClock)) == Clock::Sync)
         rate = kLap / (choice(kSyncBars) * 4.0 * 60.0 / tempo_) / std::sqrt(gravity);
      if (run)
         sim_.step(rate * kControlTick / sampleRate_, mass, gravity);
      // Should the orbits ever blow up (they shouldn't: every input is finite), start them again
      // rather than send NaN positions on to the tank and the panel.
      if (!sim_.finite())
         restartSim();

      // What each body sends: distance (smoothed over about 60 ms), pan, and a modulation signal
      // from its speed relative to the others and how close its nearest neighbour is.
      const int n = sim_.count();
      double meanSpeed = 0.0;
      for (int b = 0; b < n; ++b)
         meanSpeed += sim_.speed(b);
      meanSpeed = std::max(0.05, meanSpeed / n);
      const double smooth = 1.0 - std::exp(-kControlTick / (0.06 * sampleRate_));
      double offsets[static_cast<int>(Target::Count)] = {};
      Control c;
      c.bodies = n;
      for (int b = 0; b < kMaxBodies; ++b) {
         if (b >= n) {
            c.radius[b] = radius_[b];
            continue;
         }
         radius_[b] += smooth * (sim_.radius(b) - radius_[b]);
         c.radius[b] = radius_[b];
         c.pan[b] = std::clamp(sim_.body(b).x / 1.3, -1.0, 1.0);
         const double closeness = 1.0 / (1.0 + sim_.nearest(b) / 0.25);
         const double signal = std::clamp(std::tanh(1.5 * (sim_.speed(b) / meanSpeed - 1.0)) + closeness, -1.0, 1.0);
         const int target = choice(bodyParam(b, kBody1Target));
         const double amount = value(bodyParam(b, kBody1Amount)) * signal;
         if (target == static_cast<int>(Target::DelayMod))
            c.delayMod[b] = amount;
         else if (target > 0 && target < static_cast<int>(Target::Count))
            offsets[target] += amount;
      }

      c.decay = value(kDecay);
      c.size = value(kSize) * std::exp2(0.5 * offsets[static_cast<int>(Target::Size)]);
      c.motion = value(kMotion);
      c.attack = value(kAttack);
      c.diffusion = value(kDiffusion);
      c.earlyLate = value(kEarlyLate);
      c.predelayMs = value(kPredelay);
      c.dampHigh = value(kDampHigh);
      c.bassMult = value(kBassMult);
      c.bassXover = value(kBassXover);
      c.lowCut = value(kLowCut);
      c.highCut = value(kHighCut);
      c.feedback = std::clamp(value(kFeedback) + 0.4 * offsets[static_cast<int>(Target::Feedback)], 0.0, 1.1);
      c.shimmer = std::clamp(value(kShimmer) + 0.5 * offsets[static_cast<int>(Target::Shimmer)], 0.0, 1.0);
      c.shimmerPitch = std::round(value(kShimmerPitch));
      c.tilt = std::clamp(value(kTilt) + 0.8 * offsets[static_cast<int>(Target::Tilt)], -1.0, 1.0);
      c.smear = value(kSmear);
      c.safetyHard = choice(kSafety) == 1;
      c.freeze = value(kFreeze) >= 0.5;
      c.driveOn = value(kDriveOn) >= 0.5;
      c.driveWhere = static_cast<DriveWhere>(std::clamp(choice(kDriveWhere), 0, 2));
      c.driveType = static_cast<DriveType>(std::clamp(choice(kDriveType), 0, 4));
      c.drive = std::clamp(value(kDrive) + 18.0 * offsets[static_cast<int>(Target::Drive)], 0.0, 36.0);
      c.driveTone = value(kDriveTone);
      c.driveBias = value(kDriveBias);
      c.driveMix = value(kDriveMix);
      c.driveTrim = value(kDriveTrim);
      c.echoOn = value(kEchoOn) >= 0.5;
      c.echoWhere = static_cast<EchoWhere>(std::clamp(choice(kEchoWhere), 0, 2));
      c.echoSeconds = echoSeconds() * std::exp2(0.5 * offsets[static_cast<int>(Target::Echo)]);
      c.echoFeedback = value(kEchoFeedback);
      c.echoTone = value(kEchoTone);
      c.echoPingPong = value(kEchoPingPong) >= 0.5;
      c.echoMix = value(kEchoMix);
      c.width = std::clamp(value(kWidth) + 0.8 * offsets[static_cast<int>(Target::Width)], 0.0, 2.0);
      c.duck = value(kDuck);
      c.mix = value(kMix);
      c.levelDb = value(kLevel);
      engine_.setControl(c);

      if (++publish_ >= kPublishEvery) {
         publish_ = 0;
         publishFrame(mass, c);
      }
   }

   // The echo's time: its milliseconds, or its note value at the host's tempo, halved until it fits.
   double echoSeconds() const {
      const int sync = std::clamp(choice(kEchoSync), 0, kEchoSyncCount - 1);
      if (sync == 0)
         return value(kEchoTime) * 0.001;
      double seconds = kEchoSyncBeats[sync] * 60.0 / tempo_;
      while (seconds > kEchoMaxSeconds - 0.02)
         seconds *= 0.5;
      return seconds;
   }

   void publishFrame(const double *mass, const Control &c) {
      OrbitFrame &f = frames_.back();
      f.resets = resets_;
      f.count = sim_.count();
      for (int b = 0; b < kMaxBodies; ++b) {
         const OrbitSim::Body &body = sim_.body(b);
         f.x[b] = static_cast<float>(body.x);
         f.y[b] = static_cast<float>(body.y);
         f.vx[b] = static_cast<float>(body.vx);
         f.vy[b] = static_cast<float>(body.vy);
         f.mass[b] = static_cast<float>(mass[b]);
         f.delayMs[b] = static_cast<float>(engine_.delayMs(b));
      }
      for (int band = 0; band < 3; ++band) {
         f.band[band] = analyzer_.level(band);
         f.hits[band] = analyzer_.hits(band);
         f.hitStrength[band] = analyzer_.strength(band);
      }
      f.late = lateLevel_;
      f.decay = static_cast<float>(c.decay);
      f.running = value(kRun) >= 0.5;
      f.frozen = c.freeze;
      frames_.publish();
   }

   // 0..1 from a fixed sequence (xorshift), so renders are repeatable.
   double random() {
      rng_ ^= rng_ << 13;
      rng_ ^= rng_ >> 17;
      rng_ ^= rng_ << 5;
      return (rng_ & 0xffffff) / static_cast<double>(0x1000000);
   }

   // A param as the audio hears it: its value plus host modulation, clamped to its range.
   double value(clap_id id) const {
      return std::clamp(values_[id].load() + mods_[id].load(), kParams[id].min, kParams[id].max);
   }

   std::atomic<double> values_[kParamCount];
   // Host modulation, written by the audio thread.
   std::atomic<double> mods_[kParamCount];
   jclap::core::EditQueue edits_;
   // Params a preset load changed that the host hasn't been told about yet.
   std::atomic<bool> pending_[kParamCount] = {};
   jclap::ui::HostMenu hostMenu_;
   // Main thread.
   std::string presetName_ = "Init";

   // Audio thread.
   double sampleRate_ = 48000.0, tempo_ = 120.0;
   bool playing_ = false;
   int tick_ = 0, publish_ = 0;
   Engine engine_;
   Analyzer analyzer_;
   OrbitSim sim_;
   double radius_[kMaxBodies] = {1, 1, 1, 1, 1, 1};
   double lastVelocity_[kMaxBodies] = {1, 1, 1, 1, 1, 1};
   uint32_t seenHits_[3] = {};
   uint32_t resets_ = 0;
   static constexpr uint32_t kRngSeed = 0x9e3779b9u;
   uint32_t rng_ = kRngSeed;
   float lateLevel_ = 0.0f;
   bool spawnPending_ = false;
   double spawnX_ = 0.0, spawnY_ = 0.0;

   // Shared with the panel.
   jclap::core::TripleBuffer<OrbitFrame> frames_;
   jclap::core::SpscQueue<OrbitCommand, 256> commands_;
   std::atomic<float> peaks_[2] = {{0.0f}, {0.0f}};

   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t JVerb::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.jverb",
   "JVerb",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   "0.3.0",
   "Reverb modulated by a live three-body gravity simulation, with a loopable drive",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(JVerb)
