// JVerb: an algorithmic reverb modulated by a live gravity simulation. Two to six bodies orbit each
// other (orbit.h); each pans its share of the reverb's sixteen delay lines (engine.h), so the tail
// swirls around the stereo field as the orbit turns. Each body can also be routed to one more
// destination, and transients in the input can kick the bodies.
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

#include <clap/ext/tail.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
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
// 2: the bodies' targets changed (version 1's Delay Mod, Size and Echo became Off).
constexpr uint32_t kStateVersion = 2;

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
         heard_[i].store(std::nan(""));
      }
   }

   bool init() override {
      hostTail_ = hostExtension<clap_host_tail_t>(CLAP_EXT_TAIL);
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
      // A first guess for the tail until the first control tick works it out.
      tailSamples_.store(static_cast<uint32_t>((1.6 * value(kDecay) + 0.5) * sampleRate), std::memory_order_relaxed);
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
      if (!std::strcmp(id, CLAP_EXT_TAIL))
         return &tailExtension;
      return jclap::ui::ClapGui::extensionFor<JVerb, &JVerb::gui_>(id);
   }

   // An audio effect: no note ports.
   uint32_t notePortsCount(bool) const override { return 0; }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id].load() : 0.0; }
   // What the audio is using: host modulation, plus the bodies' for the params they can move.
   double paramModulated(clap_id id) const override {
      if (id >= kParamCount)
         return 0.0;
      const double h = heard_[id].load(std::memory_order_relaxed);
      return std::isnan(h) ? value(id) : h;
   }
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
          !jclap::core::readAll(stream, header, sizeof(header)) || (header[0] != kStateVersion && header[0] != 1) || header[1] > kParamCount)
         return false;
      double v[kParamCount];
      if (!jclap::core::readAll(stream, v, sizeof(double) * header[1]))
         return false;
      for (uint32_t i = 0; i < header[1]; ++i)
         if (!std::isfinite(v[i]))
            return false;
      if (header[0] == 1)
         for (int b = 0; b < kMaxBodies; ++b) {
            const clap_id id = bodyParam(b, kBody1Target);
            if (id < header[1])
               v[id] = targetFromV1(v[id]);
         }
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
      drainCommands();
      readTransport(proc->transport, proc->frames_count);

      // Param events apply at their own sample (the control tick after it), so a render is the
      // same at any block size.
      const clap_input_events_t *events = proc->in_events;
      const uint32_t eventCount = events->size(events);
      uint32_t nextEvent = 0;
      const auto applyEvents = [&](uint32_t upTo) {
         for (; nextEvent < eventCount; ++nextEvent) {
            const clap_event_header_t *h = events->get(events, nextEvent);
            if (h->time > upTo)
               break;
            handleEvent(h);
         }
      };

      if (proc->audio_inputs_count < 1 || proc->audio_outputs_count < 1) {
         applyEvents(UINT32_MAX);
         return CLAP_PROCESS_CONTINUE;
      }
      const clap_audio_buffer_t &in = proc->audio_inputs[0];
      const clap_audio_buffer_t &out = proc->audio_outputs[0];
      if (!in.data32 || !out.data32 || in.channel_count < 1 || out.channel_count < 1) {
         applyEvents(UINT32_MAX);
         jclap::core::passAudioThrough(proc);
         return CLAP_PROCESS_CONTINUE;
      }
      const float *inL = in.data32[0], *inR = in.data32[in.channel_count > 1 ? 1 : 0];
      float *outL = out.data32[0], *outR = out.data32[out.channel_count > 1 ? 1 : 0];
      float peak[2] = {0.0f, 0.0f};
      bool resetThisBlock = false;
      for (uint32_t i = 0; i < proc->frames_count; ++i) {
         applyEvents(i);
         if (tick_ == 0)
            controlTick();
         tick_ = (tick_ + 1) % kControlTick;
         // A NaN or infinite input sample is heard as silence, before the analyzer and the tank see
         // it: either would hold on to it for good.
         const float l = std::isfinite(inL[i]) ? inL[i] : 0.0f, r = std::isfinite(inR[i]) ? inR[i] : 0.0f;
         analyzer_.process(l, r);
         float yl, yr, late;
         engine_.process(l, r, yl, yr, late);
         if (!std::isfinite(yl) || !std::isfinite(yr)) {
            // Never pass on a NaN or infinity; start the tank again instead, at most once a block.
            // With the input cleaned above, only a blown-up tank gets here.
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
      applyEvents(UINT32_MAX);
      for (int ch = 0; ch < 2; ++ch) {
         float seen = peaks_[ch].load(std::memory_order_relaxed);
         while (peak[ch] > seen && !peaks_[ch].compare_exchange_weak(seen, peak[ch])) {
         }
      }
      return CLAP_PROCESS_CONTINUE;
   }

   // ---- tail ----

   static uint32_t tailGet(const clap_plugin_t *plugin) {
      return static_cast<JVerb *>(plugin->plugin_data)->tailSamples_.load(std::memory_order_relaxed);
   }
   static constexpr clap_plugin_tail_t tailExtension = {tailGet};

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
      for (uint32_t i = 0; i < n; ++i)
         handleEvent(in->get(in, i));
   }

   void handleEvent(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return;
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

   void drainCommands() {
      OrbitCommand cmd;
      while (commands_.pop(cmd))
         apply(cmd);
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
         pendingAge_ = 0;
         break;
      case OrbitCommand::Kind::Remove:
         removePending_ = b;
         pendingAge_ = 0;
         break;
      case OrbitCommand::Kind::Reset:
         restartSim();
         break;
      }
   }

   void readTransport(const clap_event_transport_t *t, uint32_t frames) {
      if (!t)
         return;
      if (t->flags & CLAP_TRANSPORT_HAS_TEMPO)
         tempo_ = std::max(1.0, t->tempo);
      // A bar is as many quarter notes as the time signature says: 3 in 3/4, 3.5 in 7/8.
      if ((t->flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) && t->tsig_num > 0 && t->tsig_denom > 0)
         barBeats_ = std::clamp(t->tsig_num * 4.0 / t->tsig_denom, 0.25, 64.0);
      const bool playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
      const bool hasBeats = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
      const double beats = hasBeats ? static_cast<double>(t->song_pos_beats) / CLAP_BEATTIME_FACTOR : 0.0;
      if (static_cast<Clock>(choice(kClock)) != Clock::Free && playing) {
         // Starting, or a jump in the song (a loop coming round, a seek), puts the orbits where they
         // are at that point of the cycle, so they line up with the bars every time.
         const bool jumped = hasBeats && playing_ && std::fabs(beats - expectedBeats_) > 0.05;
         if (!playing_ || jumped) {
            restartSim();
            if (hasBeats && static_cast<Clock>(choice(kClock)) == Clock::Sync)
               advanceSim(beats);
         }
      }
      playing_ = playing;
      expectedBeats_ = beats + frames * tempo_ / (60.0 * sampleRate_);
   }

   // The sync clock's cycle in quarter notes, and the sim's speed to go round it once.
   double cycleBeats() const { return choice(kSyncBars) * barBeats_; }
   double syncRate() const { return kLap / (cycleBeats() * 60.0 / tempo_) / std::sqrt(value(kGravity)); }

   // Runs the orbits on from their start to where they'd be `beats` into the song, in at most a
   // couple of hundred steps (a fraction of a lap each).
   void advanceSim(double beats) {
      const double into = std::fmod(std::max(0.0, beats), cycleBeats()) / cycleBeats();
      if (into <= 0.0 || value(kRun) < 0.5)
         return;
      double mass[kMaxBodies];
      masses(mass);
      const double total = into * kLap / std::sqrt(value(kGravity));
      const int steps = std::max(1, static_cast<int>(std::ceil(into * 200.0)));
      for (int i = 0; i < steps; ++i)
         sim_.step(total / steps, mass, value(kGravity));
      if (!sim_.finite())
         restartSim();
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
      for (double &sig : signal_)
         sig = 0.0;
      ++resets_;
      spawnPending_ = false;
      removePending_ = -1;
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

      // Bodies coming and going, one per tick. The panel sends where a new body goes (or which one
      // to remove) before it changes Bodies, so reading Bodies first and then the panel's commands
      // always finds the command that goes with the change. One that no change follows is dropped
      // after a quarter of a second.
      const int want = std::clamp(choice(kBodies), 2, kMaxBodies);
      drainCommands();
      if ((spawnPending_ || removePending_ >= 0) && ++pendingAge_ > static_cast<int>(0.25 * sampleRate_ / kControlTick)) {
         spawnPending_ = false;
         removePending_ = -1;
      }
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
         signal_[b] = 0.0;
      }
      else if (want < sim_.count()) {
         // The panel removes the body that was clicked; the param edits moving the later bodies'
         // settings down a place come with it, so their state moves down too.
         const int gone = removePending_ >= 0 && removePending_ < sim_.count() ? removePending_ : sim_.count() - 1;
         removePending_ = -1;
         for (int b = gone; b + 1 < sim_.count(); ++b) {
            signal_[b] = signal_[b + 1];
            lastVelocity_[b] = lastVelocity_[b + 1];
         }
         sim_.remove(gone);
         lastVelocity_[sim_.count()] = value(bodyParam(sim_.count(), kBody1Velocity));
         signal_[sim_.count()] = 0.0;
      }

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
         rate = syncRate();
      if (run)
         sim_.step(rate * kControlTick / sampleRate_, mass, gravity);
      // Should the orbits ever blow up (they shouldn't: every input is finite), start them again
      // rather than send NaN positions on to the tank and the panel.
      if (!sim_.finite())
         restartSim();

      // What each body sends: its left/right position, its tone and phase for its share of the tail
      // (as deep as the Orbit Pan, Tone and Swirl knobs say), and a modulation signal from its speed
      // relative to the others and how close its nearest neighbour is (smoothed over about 60 ms).
      // The signal moves only the param the body's card routes it to, and that param's knob shows
      // the movement, so nothing changes the sound that the panel doesn't show.
      const int n = sim_.count();
      double meanSpeed = 0.0;
      for (int b = 0; b < n; ++b)
         meanSpeed += sim_.speed(b);
      meanSpeed = std::max(0.05, meanSpeed / n);
      const double smooth = 1.0 - std::exp(-kControlTick / (0.06 * sampleRate_));
      double offsets[static_cast<int>(Target::Count)] = {};
      Control c;
      c.bodies = n;
      c.panDepth = value(kOrbitPan);
      c.swirlDepth = value(kOrbitSwirl);
      const double orbitTone = value(kOrbitTone);
      const double sweeps = 2.0 + std::round(6.0 * std::max(0.0, c.swirlDepth - 1.0));
      for (int b = 0; b < n; ++b) {
         const OrbitSim::Body &body = sim_.body(b);
         c.pan[b] = std::clamp(body.x / 1.3, -1.0, 1.0);
         // Bright near the centre of mass, dark out on the swing (rest distance is about 0.9).
         c.tone[b] = orbitTone * std::clamp(1.2 * (1.0 - sim_.radius(b) / 0.9), -1.0, 1.0);
         // Twice round the phaser's sweep per lap, so it swirls as fast as the body goes round; past
         // full Swirl, up to eight times (a whole number, so the sweep stays smooth as the angle wraps).
         c.phase[b] = std::sin(sweeps * std::atan2(body.y, body.x));
         const double closeness = 1.0 / (1.0 + sim_.nearest(b) / 0.25);
         const double raw = std::clamp(std::tanh(1.5 * (sim_.speed(b) / meanSpeed - 1.0)) + closeness, -1.0, 1.0);
         signal_[b] += smooth * (raw - signal_[b]);
         const int target = choice(bodyParam(b, kBody1Target));
         if (target > 0 && target < static_cast<int>(Target::Count))
            offsets[target] += value(bodyParam(b, kBody1Amount)) * signal_[b];
      }
      const auto offset = [&](Target t) { return offsets[static_cast<int>(t)]; };

      c.decay = std::clamp(value(kDecay) * std::exp2(offset(Target::Decay)), kParams[kDecay].min, kParams[kDecay].max);
      c.size = value(kSize);
      c.motion = value(kMotion);
      c.attack = value(kAttack);
      c.diffusion = std::clamp(value(kDiffusion) + 0.5 * offset(Target::Diffusion), 0.0, 1.0);
      c.earlyLate = std::clamp(value(kEarlyLate) + 0.5 * offset(Target::EarlyLate), 0.0, 1.0);
      c.predelayMs = value(kPredelay);
      c.dampHigh = std::clamp(value(kDampHigh) * std::exp2(1.5 * offset(Target::Damping)), kParams[kDampHigh].min,
                              kParams[kDampHigh].max);
      c.bassMult = value(kBassMult);
      c.bassXover = value(kBassXover);
      c.lowCut = value(kLowCut);
      c.highCut = value(kHighCut);
      c.feedback = std::clamp(value(kFeedback) + 0.4 * offset(Target::Feedback), 0.0, 1.1);
      c.shimmer = std::clamp(value(kShimmer) + 0.5 * offset(Target::Shimmer), 0.0, 1.0);
      c.shimmerPitch = std::round(value(kShimmerPitch));
      c.tilt = std::clamp(value(kTilt) + 0.8 * offset(Target::Tilt), -1.0, 1.0);
      c.smear = value(kSmear);
      c.safetyHard = choice(kSafety) == 1;
      c.freeze = value(kFreeze) >= 0.5;
      c.driveOn = value(kDriveOn) >= 0.5;
      c.driveWhere = static_cast<DriveWhere>(std::clamp(choice(kDriveWhere), 0, 2));
      c.driveType = static_cast<DriveType>(std::clamp(choice(kDriveType), 0, 4));
      c.drive = std::clamp(value(kDrive) + 18.0 * offset(Target::Drive), 0.0, 36.0);
      c.driveTone = value(kDriveTone);
      c.driveBias = value(kDriveBias);
      c.driveMix = value(kDriveMix);
      c.driveTrim = value(kDriveTrim);
      c.echoOn = value(kEchoOn) >= 0.5;
      c.echoWhere = static_cast<EchoWhere>(std::clamp(choice(kEchoWhere), 0, 2));
      c.echoSeconds = echoSeconds();
      c.echoFeedback = value(kEchoFeedback);
      c.echoTone = value(kEchoTone);
      c.echoPingPong = value(kEchoPingPong) >= 0.5;
      c.echoMix = value(kEchoMix);
      c.width = std::clamp(value(kWidth) + 0.8 * offset(Target::Width), 0.0, 2.0);
      c.duck = value(kDuck);
      c.mix = value(kMix);
      c.levelDb = value(kLevel);
      engine_.setControl(c);
      updateTail(c);

      // The values the bodies are moving, for those params' knobs.
      const std::pair<clap_id, double> heard[] = {{kDecay, c.decay},       {kDampHigh, c.dampHigh}, {kDiffusion, c.diffusion},
                                                  {kEarlyLate, c.earlyLate}, {kDrive, c.drive},       {kShimmer, c.shimmer},
                                                  {kFeedback, c.feedback}, {kTilt, c.tilt},         {kWidth, c.width}};
      for (const auto &[id, v] : heard)
         heard_[id].store(v, std::memory_order_relaxed);

      if (++publish_ >= kPublishEvery) {
         publish_ = 0;
         publishFrame(mass, c);
      }
   }

   // How long the output can keep sounding after the input stops: what the host's tail extension
   // reports. Infinite while frozen, with the loop at or past where it sustains, or with an echo in
   // the loop; otherwise until the tank and the echo are 96 dB down.
   void updateTail(const Control &c) {
      uint32_t samples = UINT32_MAX;
      if (!c.freeze && c.feedback < 0.95 && !(c.echoOn && c.echoWhere == EchoWhere::Loop)) {
         double seconds = 1.6 * c.decay * (1.0 + c.feedback) + c.predelayMs * 0.001 + 0.1;
         if (c.echoOn)
            seconds += c.echoSeconds * (1.0 + std::log(0.0000158) / std::log(std::clamp(c.echoFeedback, 0.01, 0.99)));
         samples = static_cast<uint32_t>(std::min(seconds * sampleRate_, static_cast<double>(INT32_MAX - 1)));
      }
      const uint32_t old = tailSamples_.load(std::memory_order_relaxed);
      // Tell the host when it changes by more than a tenth, or to or from infinite.
      const bool infinite = samples >= INT32_MAX, wasInfinite = old >= INT32_MAX;
      if (infinite != wasInfinite || (!infinite && std::fabs(static_cast<double>(samples) - old) > 0.1 * old)) {
         tailSamples_.store(samples, std::memory_order_relaxed);
         if (hostTail_)
            hostTail_->changed(host());
      }
   }

   // The echo's time: its milliseconds, or its note value at the host's tempo, halved until it fits.
   // Bar lengths follow the time signature.
   double echoSeconds() const {
      const int sync = std::clamp(choice(kEchoSync), 0, kEchoSyncCount - 1);
      if (sync == 0)
         return value(kEchoTime) * 0.001;
      const double beats = kEchoSyncBeats[sync] >= 4.0 ? kEchoSyncBeats[sync] / 4.0 * barBeats_ : kEchoSyncBeats[sync];
      double seconds = beats * 60.0 / tempo_;
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
         f.signal[b] = static_cast<float>(signal_[b]);
         f.voicePan[b] = static_cast<float>(engine_.bodyPan(b));
         f.voiceTone[b] = static_cast<float>(engine_.bodyTone(b));
         f.voiceSwirl[b] = static_cast<float>(engine_.bodySwirl(b));
         f.target[b] = static_cast<uint8_t>(std::clamp(choice(bodyParam(b, kBody1Target)), 0, static_cast<int>(Target::Count) - 1));
         f.amount[b] = static_cast<float>(value(bodyParam(b, kBody1Amount)));
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
   double sampleRate_ = 48000.0, tempo_ = 120.0, barBeats_ = 4.0, expectedBeats_ = 0.0;
   bool playing_ = false;
   int tick_ = 0, publish_ = 0;
   Engine engine_;
   Analyzer analyzer_;
   OrbitSim sim_;
   double signal_[kMaxBodies] = {};
   // The value each body-movable param is heard at, for its knob; NaN until the first control tick
   // and for every param the bodies can't move.
   std::atomic<double> heard_[kParamCount];
   double lastVelocity_[kMaxBodies] = {1, 1, 1, 1, 1, 1};
   uint32_t seenHits_[3] = {};
   uint32_t resets_ = 0;
   static constexpr uint32_t kRngSeed = 0x9e3779b9u;
   uint32_t rng_ = kRngSeed;
   float lateLevel_ = 0.0f;
   bool spawnPending_ = false;
   double spawnX_ = 0.0, spawnY_ = 0.0;
   // The body the panel asked to remove (-1: the last), and how many ticks a request has waited.
   int removePending_ = -1, pendingAge_ = 0;
   const clap_host_tail_t *hostTail_ = nullptr;
   std::atomic<uint32_t> tailSamples_{0};

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
   "0.8.0",
   "Reverb modulated by a live three-body gravity simulation, with a loopable drive",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(JVerb)
