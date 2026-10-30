// JClap Polar XY: a CLAP note effect that turns two XY CCs into Omnisphere Orb angle/radius CCs.
// Settings are plain parameters; the panel in editor.cpp shows them with a live view of the Orb.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "editor.h"
#include "motion.h"
#include "params.h"
#include "polar.h"

namespace {

using namespace polar_xy;

struct ChannelState {
   // The last X/Y seen. Nothing is sent until both have arrived: the other axis isn't known.
   int x = 64, y = 64;
   bool hasX = false, hasY = false;
   // The same X/Y between the CC steps (0..127), which is what converts: from a 14-bit controller's
   // LSBs, or the panel's XY pad. fromPad: the pad has it, until the controller moves again; the mouse
   // arrives a block at a time, so the output glides between its moves.
   double fx = 64.0, fy = 64.0;
   bool fromPad = false;
   // The angle held at dead center, and what was last sent on the output CCs (-1: send again), in
   // the output's resolution.
   int angle = -1;
   int sentAngle = -1, sentRadius = -1;
   // Where the output is on its way to the X/Y, with Smoothing and Inertia.
   jclap::Follower motion;
};

// What the conversion depends on, to notice when a setting changes.
struct Settings {
   int outAngle = -1, outRadius = -1, direction = 0, radiusMode = 0;
   double angleOffset = 0.0;
   bool around = false;
   bool fine = false; // 14-bit output
   bool operator==(const Settings &o) const {
      return outAngle == o.outAngle && outRadius == o.outRadius && direction == o.direction &&
             radiusMode == o.radiusMode && angleOffset == o.angleOffset && around == o.around && fine == o.fine;
   }
};

// State layout: 4-byte magic, uint32 version, one double per parameter, then (from version 3) uint32
// name length and the preset name. Version 1 had the params before Smoothing, versions 2 and 3 those
// before Output Resolution; the ones they lack load as their defaults.
constexpr char kStateMagic[4] = {'J', 'C', 'O', 'P'};
constexpr uint32_t kStateVersion = 4;

struct StateHeader {
   char magic[4];
   uint32_t version;
};

uint32_t savedParamCount(uint32_t version) {
   switch (version) {
   case 1:
      return kParamSmoothing;
   case 2:
   case 3:
      return kParamResolution;
   case 4:
      return kParamCount;
   default:
      return 0;
   }
}

// Smoothing and inertia move the output in steps this many samples apart.
constexpr uint32_t kMotionStep = 32;

// The least Smoothing a move of the panel's XY pad gets (seconds). The mouse reports a few dozen
// times a second, and each move is picked up at the start of a block, so jumping straight to each
// one steps the output unevenly; this glides between them.
constexpr double kPadGlide = 0.04;

// The XY pad's position packed for the audio thread: sequence << 32 | x << 16 | y, each axis 0..127
// in 1/512 steps, so whole CC values (the centre above all) come through exact.
constexpr double kPadScale = 512.0;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

class PolarXY final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit PolarXY(const clap_host_t *host) : Plugin(&descriptor, host) {}

   bool init() override {
      for (uint32_t i = 0; i < kParamCount; ++i)
         params_[i].store(kParams[i].def);
      resetChannels();
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(*this);
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   // A reset is not a controller move: keep the X/Y last seen, and only send them again with the next.
   // Anything mid-glide lands where it was headed.
   bool activate(double sampleRate, uint32_t, uint32_t) override {
      sampleRate_ = sampleRate;
      forgetSent();
      return true;
   }

   void reset() override { forgetSent(); }

   const void *extension(const char *id) override {
      if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
         return &audioPortsExtension;
      if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
         return &notePortsExtension;
      if (!std::strcmp(id, CLAP_EXT_PARAMS))
         return &paramsExtension;
      if (!std::strcmp(id, CLAP_EXT_STATE))
         return &stateExtension;
      return jclap::ui::ClapGui::extensionFor<PolarXY, &PolarXY::gui_>(id);
   }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override { return id < kParamCount ? params_[id].load() : 0.0; }
   void beginEdit(clap_id id) override { edits_.begin(id); }
   void setParam(clap_id id, double value) override {
      if (id >= kParamCount || !std::isfinite(value))
         return;
      value = std::clamp(value, kParams[id].min, kParams[id].max);
      params_[id].store(value);
      edits_.value(id, value);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }
   void lastInput(int &x, int &y, int &channel) const override {
      const uint32_t v = lastIn_.load();
      channel = static_cast<int>(v >> 16) & 0x0F;
      x = static_cast<int>(v >> 8) & 0x7F;
      y = static_cast<int>(v) & 0x7F;
   }
   void dragInput(double x, double y) override {
      if (!std::isfinite(x) || !std::isfinite(y))
         return;
      // A new number each move, so the audio thread takes every one, even if released before it runs.
      const uint64_t seq = ((dragIn_.load() >> 32) + 1) & 0xFFFFFFFF;
      const auto axis = [](double v) {
         return static_cast<uint64_t>(std::lround(std::clamp(v, 0.0, 127.0) * kPadScale));
      };
      dragIn_.store((seq ? seq : 1) << 32 | axis(x) << 16 | axis(y));
      dragging_.store(true);
      host()->request_process(host());
   }
   void releaseInput() override { dragging_.store(false); }
   void sendOutput(int which) override {
      if (which < 0 || which > 1)
         return;
      learn_.fetch_or(1u << which);
      host()->request_process(host());
   }
   std::string presetName() const override { return presetName_; }
   void setPresetName(const std::string &name) override { presetName_ = name; }
   bool lastOutput(int &angle, int &radius) const override {
      const int32_t v = lastOut_.load();
      if (v < 0)
         return false;
      angle = (v >> 8) & 0x7F;
      radius = v & 0x7F;
      return true;
   }

   bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const override {
      if (!Plugin::notePortsInfo(index, isInput, info))
         return false;
      std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "XY In" : "Orb Out");
      return true;
   }

   // ---- params ----

   uint32_t paramsCount() const override { return kParamCount; }

   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index >= kParamCount)
         return false;
      const ParamInfo &p = kParams[index];
      jclap::core::fillParamInfo(
         {index, p.name, "", p.min, p.max, p.def,
          static_cast<clap_param_info_flags>(CLAP_PARAM_IS_AUTOMATABLE | (p.stepped ? CLAP_PARAM_IS_STEPPED : 0))},
         info);
      return true;
   }

   bool paramsValue(clap_id id, double *value) const override {
      if (id >= kParamCount)
         return false;
      *value = params_[id].load();
      return true;
   }

   bool paramsValueToText(clap_id id, double value, char *out, uint32_t size) const override {
      if (id >= kParamCount)
         return false;
      formatParam(id, value, out, size);
      return true;
   }

   bool paramsTextToValue(clap_id id, const char *text, double *value) const override {
      return id < kParamCount && parseParam(id, text, *value);
   }

   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
            applyParam(reinterpret_cast<const clap_event_param_value_t *>(h));
      }
      edits_.drain(out);
   }

   // ---- state ----

   bool stateSave(const clap_ostream_t *stream) override {
      StateHeader header{};
      std::memcpy(header.magic, kStateMagic, sizeof(kStateMagic));
      header.version = kStateVersion;
      double values[kParamCount];
      for (uint32_t i = 0; i < kParamCount; ++i)
         values[i] = params_[i].load();
      const uint32_t nameLength = static_cast<uint32_t>(std::min<size_t>(presetName_.size(), 255));
      return jclap::core::writeAll(stream, &header, sizeof(header)) &&
             jclap::core::writeAll(stream, values, sizeof(values)) &&
             jclap::core::writeAll(stream, &nameLength, sizeof(nameLength)) &&
             jclap::core::writeAll(stream, presetName_.data(), nameLength);
   }

   bool stateLoad(const clap_istream_t *stream) override {
      StateHeader header{};
      if (!jclap::core::readAll(stream, &header, sizeof(header)) ||
          std::memcmp(header.magic, kStateMagic, sizeof(kStateMagic)))
         return false;
      const uint32_t count = savedParamCount(header.version);
      if (count == 0)
         return false;
      double values[kParamCount];
      for (uint32_t i = 0; i < kParamCount; ++i)
         values[i] = kParams[i].def;
      if (!jclap::core::readAll(stream, values, count * sizeof(double)))
         return false;
      for (uint32_t i = 0; i < count; ++i)
         if (!std::isfinite(values[i]))
            return false;
      // Older state has no name: it was whatever the settings were, not a preset.
      uint32_t nameLength = 0;
      char name[256];
      if (header.version >= 3 && jclap::core::readAll(stream, &nameLength, sizeof(nameLength)) &&
          nameLength < sizeof(name) && jclap::core::readAll(stream, name, nameLength))
         presetName_.assign(name, nameLength);
      else
         presetName_.clear();

      for (uint32_t i = 0; i < kParamCount; ++i)
         params_[i].store(std::clamp(values[i], kParams[i].min, kParams[i].max));

      // Tell the host the values changed so it refreshes what it shows.
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      const clap_input_events_t *in = proc->in_events;
      const clap_output_events_t *out = proc->out_events;
      edits_.drain(out);
      // Panel edits, flushes and state loads land between blocks.
      applySettings(out, 0);
      applyDrag(out);
      applyLearn(out);
      const uint32_t n = in->size(in);
      frames_ = std::max<uint32_t>(proc->frames_count, 1);
      clock_ = 0;
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         // X and Y moving at the same moment are one move: a channel converts once its events at that
         // time are all in, so no point made of the new X and the old Y goes out between them.
         if (h->time != pendingTime_)
            convertPending(out);
         glide(out, h->time);
         if (h->space_id == CLAP_CORE_EVENT_SPACE_ID) {
            if (h->type == CLAP_EVENT_PARAM_VALUE) {
               convertPending(out);
               applyParam(reinterpret_cast<const clap_event_param_value_t *>(h));
               applySettings(out, h->time);
               continue;
            }
            if (h->type == CLAP_EVENT_PARAM_MOD || h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
                h->type == CLAP_EVENT_PARAM_GESTURE_END)
               continue;
            if (h->type == CLAP_EVENT_MIDI && handleMidi(reinterpret_cast<const clap_event_midi_t *>(h), out))
               continue;
         }
         // Notes and other MIDI go straight through; anything else (transport...) must not be echoed.
         if (jclap::core::isNoteEvent(h))
            out->try_push(out, h);
      }
      convertPending(out);
      glide(out, frames_);
      // Audio is copied through untouched; the port only exists so hosts that expect audio IO accept us.
      jclap::core::passAudioThrough(proc);
      return CLAP_PROCESS_CONTINUE;
   }

private:
   void resetChannels() {
      for (auto &c : channels_)
         c = ChannelState{};
      pendingMask_ = 0;
   }

   void convertPending(const clap_output_events_t *out) {
      for (int channel = 0; pendingMask_ != 0; ++channel)
         if (pendingMask_ & (1u << channel)) {
            pendingMask_ &= ~(1u << channel);
            convert(out, pendingTime_, channel);
         }
   }

   // Two inputs or two outputs on one CC can't be told apart, so the plugin converts nothing (and
   // passes the input CCs through) until they differ. The panel says so.
   bool conflicting() const {
      return intParam(kParamInX) == intParam(kParamInY) || intParam(kParamOutAngle) == intParam(kParamOutRadius);
   }

   void forgetSent() {
      for (auto &c : channels_) {
         c.sentAngle = c.sentRadius = -1;
         c.motion.land();
      }
   }

   jclap::MotionSettings motion() const {
      jclap::MotionSettings m;
      m.smoothing = params_[kParamSmoothing].load() / 1000.0;
      m.inertia = params_[kParamInertia].load() / 100.0;
      m.around = intParam(kParamMotionPath) != 0;
      return m;
   }

   // A channel's motion: the settings, with at least kPadGlide of Smoothing while the pad has it.
   static jclap::MotionSettings motionFor(const ChannelState &st, jclap::MotionSettings m) {
      if (st.fromPad)
         m.smoothing = std::max(m.smoothing, kPadGlide);
      return m;
   }

   // Moves every gliding channel on to `to` (a sample time in this block), in steps of kMotionStep
   // samples, sending what changes as it goes. A channel with Smoothing and Inertia both off lands.
   void glide(const clap_output_events_t *out, uint32_t to) {
      to = std::max(to, clock_);
      const jclap::MotionSettings m = motion();
      for (int channel = 0; channel < 16; ++channel) {
         ChannelState &st = channels_[channel];
         if (st.motion.moving() && !motionFor(st, m).active()) {
            st.motion.land();
            send(out, std::min(to, frames_ - 1), channel);
         }
      }
      const double dt = kMotionStep / sampleRate_;
      // Steps must land inside the block; one due right at its end runs at the start of the next.
      while (clock_ + stepLeft_ <= to && clock_ + stepLeft_ < frames_) {
         clock_ += stepLeft_;
         stepLeft_ = kMotionStep;
         for (int channel = 0; channel < 16; ++channel) {
            ChannelState &st = channels_[channel];
            if (st.motion.moving()) {
               st.motion.step(dt, motionFor(st, m));
               send(out, clock_, channel);
            }
         }
      }
      stepLeft_ -= std::min(stepLeft_, to - clock_);
      clock_ = to;
   }

   int intParam(ParamId id) const { return static_cast<int>(params_[id].load() + 0.5); }

   void applyParam(const clap_event_param_value_t *ev) {
      if (ev->param_id < kParamCount && std::isfinite(ev->value))
         params_[ev->param_id].store(std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max));
   }

   Settings settings() const {
      Settings s;
      s.outAngle = intParam(kParamOutAngle);
      s.outRadius = intParam(kParamOutRadius);
      s.direction = intParam(kParamDirection);
      s.radiusMode = intParam(kParamRadiusMode);
      s.angleOffset = params_[kParamAngleOffset].load();
      s.around = intParam(kParamMotionPath) != 0;
      s.fine = fineOutput(s.outAngle, s.outRadius, intParam(kParamResolution) != 0);
      return s;
   }

   // A changed setting changes what the controller's position means, so every channel that has a
   // position sends it again at once; on new output CCs, whatever it was sent on the old ones.
   void applySettings(const clap_output_events_t *out, uint32_t time) {
      // A new input CC means that axis isn't known until it arrives on it, rather than pairing the
      // other axis with the old CC's last value.
      const int inX = intParam(kParamInX), inY = intParam(kParamInY);
      if (inX != inX_ || inY != inY_) {
         for (ChannelState &st : channels_) {
            st.hasX = st.hasX && inX == inX_;
            st.hasY = st.hasY && inY == inY_;
         }
         inX_ = inX;
         inY_ = inY;
      }
      const Settings now = settings();
      if (now == applied_)
         return;
      const bool moved = now.outAngle != applied_.outAngle || now.outRadius != applied_.outRadius;
      // The held angle carries over to the new resolution; what was sent is sent again in it.
      const bool rescaled = now.fine != applied_.fine;
      applied_ = now;
      if (rescaled)
         for (ChannelState &st : channels_) {
            if (st.angle >= 0)
               st.angle = now.fine ? static_cast<int>(std::lround(st.angle * double(jclap::kSteps14) / jclap::kSteps7))
                                   : static_cast<int>(std::lround(st.angle * double(jclap::kSteps7) / jclap::kSteps14)) %
                                        jclap::kSteps7;
            st.sentAngle = st.sentRadius = -1;
         }
      for (ChannelState &st : channels_)
         st.motion.setAround(now.around);
      for (int channel = 0; channel < 16; ++channel) {
         ChannelState &st = channels_[channel];
         if (moved)
            st.sentAngle = st.sentRadius = -1;
         convert(out, time, channel);
      }
   }

   // Points the channel's output at its X/Y (at once, or gliding there with Smoothing or Inertia)
   // and sends where it is.
   void convert(const clap_output_events_t *out, uint32_t time, int channel) {
      ChannelState &st = channels_[channel];
      if (!st.hasX || !st.hasY || conflicting())
         return;
      const double x = jclap::ccToBipolar(st.fx), y = jclap::ccToBipolar(st.fy);
      if (motionFor(st, motion()).active())
         st.motion.follow(x, y);
      else
         st.motion.place(x, y);
      send(out, time, channel);
   }

   // Sends a channel's output position as angle and radius, each only if it changed.
   void send(const clap_output_events_t *out, uint32_t time, int channel) {
      ChannelState &st = channels_[channel];
      if (!st.motion.placed() || conflicting())
         return;
      jclap::PolarSettings s;
      s.angleOffsetDeg = applied_.angleOffset;
      s.clockwise = applied_.direction != 0;
      s.radiusMode = static_cast<jclap::RadiusMode>(applied_.radiusMode);
      double x, y;
      st.motion.position(x, y);
      const jclap::PolarCC p = jclap::toPolar(x, y, s, applied_.fine ? jclap::kSteps14 : jclap::kSteps7);

      // At dead center the angle is undefined, so it holds its last value.
      if (p.hasAngle)
         st.angle = p.angle;
      if (st.angle >= 0 && st.angle != st.sentAngle) {
         pushValue(out, time, channel, applied_.outAngle, st.angle);
         st.sentAngle = st.angle;
      }
      if (p.radius != st.sentRadius) {
         pushValue(out, time, channel, applied_.outRadius, p.radius);
         st.sentRadius = p.radius;
      }
      if (channel == panelChannel_ && st.sentAngle >= 0)
         lastOut_.store(panelOutput(st));
   }

   // What a channel last sent, for the panel: angle << 8 | radius at 7 bits, or -1 for nothing yet.
   int32_t panelOutput(const ChannelState &st) const {
      if (st.sentAngle < 0)
         return -1;
      const int shift = applied_.fine ? 7 : 0;
      return (st.sentAngle >> shift) << 8 | (st.sentRadius >> shift);
   }

   // An output value: one CC, or at 14 bits the MSB on `cc` then the LSB on cc + 32.
   void pushValue(const clap_output_events_t *out, uint32_t time, int channel, int cc, int value) {
      if (!applied_.fine) {
         pushCC(out, time, channel, cc, value);
         return;
      }
      pushCC(out, time, channel, cc, value >> 7);
      pushCC(out, time, channel, cc + 32, value & 0x7F);
   }

   void pushCC(const clap_output_events_t *out, uint32_t time, int channel, int cc, int value) {
      jclap::core::pushMidi(out, time, static_cast<uint8_t>(0xB0 | channel), static_cast<uint8_t>(cc),
                            static_cast<uint8_t>(value));
   }

   // A move of the panel's XY pad: the shown channel goes there as if the controller had sent it.
   void applyDrag(const clap_output_events_t *out) {
      const uint64_t v = dragIn_.load();
      if (v == dragSeen_)
         return;
      dragSeen_ = v;
      const int channel = panelChannel_;
      ChannelState &st = channels_[channel];
      st.fx = static_cast<double>((v >> 16) & 0xFFFF) / kPadScale;
      st.fy = static_cast<double>(v & 0xFFFF) / kPadScale;
      st.fromPad = true;
      st.x = static_cast<int>(std::lround(st.fx));
      st.y = static_cast<int>(std::lround(st.fy));
      st.hasX = st.hasY = true;
      lastIn_.store(static_cast<uint32_t>(channel) << 16 | static_cast<uint32_t>(st.x) << 8 |
                    static_cast<uint32_t>(st.y));
      if (conflicting())
         return;
      // Pass Input CCs sends on the X/Y as if it came from the controller.
      if (intParam(kParamPassThrough) != 0) {
         pushCC(out, 0, channel, intParam(kParamInX), st.x);
         pushCC(out, 0, channel, intParam(kParamInY), st.y);
      }
      convert(out, 0, channel);
   }

   // The panel's SEND buttons: one output CC alone, for a MIDI learn. It goes at the value last sent
   // on it (0 before any), and at 14 bits as the MSB only, the CC to learn.
   void applyLearn(const clap_output_events_t *out) {
      const uint32_t which = learn_.exchange(0);
      if (which == 0)
         return;
      const ChannelState &st = channels_[panelChannel_];
      const int shift = applied_.fine ? 7 : 0;
      if (which & 1u)
         pushCC(out, 0, panelChannel_, intParam(kParamOutAngle), std::max(st.sentAngle, 0) >> shift);
      if (which & 2u)
         pushCC(out, 0, panelChannel_, intParam(kParamOutRadius), std::max(st.sentRadius, 0) >> shift);
   }

   // Returns true if the event was an XY input CC and should not be forwarded.
   bool handleMidi(const clap_event_midi_t *ev, const clap_output_events_t *out) {
      if ((ev->data[0] & 0xF0) != 0xB0)
         return false;
      const int channel = ev->data[0] & 0x0F;
      const int cc = ev->data[1];
      const int value = ev->data[2] & 0x7F;
      ChannelState &st = channels_[channel];

      if (conflicting())
         return false;
      const int inX = intParam(kParamInX), inY = intParam(kParamInY);
      // A 14-bit controller sends each axis's LSB on its CC + 32 (MIDI's pairing for CCs 0-31), after
      // the MSB; a 7-bit one just never does.
      const bool lsbX = inX < 32 && cc == inX + 32 && cc != inY;
      const bool lsbY = inY < 32 && cc == inY + 32 && cc != inX;
      if (cc != inX && cc != inY && !lsbX && !lsbY)
         return false;
      const bool passThrough = intParam(kParamPassThrough) != 0;
      if (passThrough)
         out->try_push(out, &ev->header);
      // While the pad is held the mouse has that channel (and the panel stays on it); the controller
      // takes over again with its next move after release.
      const bool dragging = dragging_.load();
      if (dragging && channel == panelChannel_)
         return true;
      if (st.fromPad) {
         st.fx = st.x;
         st.fy = st.y;
         st.fromPad = false;
      }
      // An MSB on its own is the whole value, so a 7-bit controller lands on the CC step exactly.
      if (cc == inX) {
         st.x = value;
         st.fx = value;
         st.hasX = true;
      } else if (cc == inY) {
         st.y = value;
         st.fy = value;
         st.hasY = true;
      } else if (lsbX) {
         st.fx = std::min(st.x + value / 128.0, 127.0);
      } else {
         st.fy = std::min(st.y + value / 128.0, 127.0);
      }
      // The panel follows the channel that moved last, as one tear-free value.
      if (channel != panelChannel_ && !dragging) {
         panelChannel_ = channel;
         lastOut_.store(panelOutput(st));
      }
      if (channel == panelChannel_)
         lastIn_.store(static_cast<uint32_t>(channel) << 16 | static_cast<uint32_t>(st.x) << 8 |
                       static_cast<uint32_t>(st.y));

      pendingMask_ |= 1u << channel;
      pendingTime_ = ev->header.time;
      return true;
   }

   std::atomic<double> params_[kParamCount];
   ChannelState channels_[16];
   // The settings the last conversion used, and the input CCs (audio thread).
   Settings applied_;
   int inX_ = -1, inY_ = -1;
   // Channels with input waiting to convert, all at pendingTime_ (audio thread).
   uint32_t pendingMask_ = 0, pendingTime_ = 0;
   // Gliding: the block's length, how far into it the motion has got, and the samples to its next
   // step (audio thread).
   double sampleRate_ = 48000.0;
   uint32_t frames_ = 1, clock_ = 0, stepLeft_ = kMotionStep;
   // For the panel: the channel that moved last, its X/Y (channel << 16 | x << 8 | y) and what it
   // last sent (angle << 8 | radius, or -1 for nothing yet).
   int panelChannel_ = 0;
   std::atomic<uint32_t> lastIn_{64u << 8 | 64u};
   std::atomic<int32_t> lastOut_{-1};
   // The XY pad's last move (see kPadScale; 0 for none yet), whether it is held, and the last move
   // the audio thread took.
   std::atomic<uint64_t> dragIn_{0};
   std::atomic<bool> dragging_{false};
   uint64_t dragSeen_ = 0;
   // SEND presses not yet sent: bit 0 angle, bit 1 radius.
   std::atomic<uint32_t> learn_{0};
   // The preset last loaded or saved (main thread).
   std::string presetName_ = "Init";
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;
   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t PolarXY::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.polar-xy",
   "Polar XY",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   JCLAP_PLUGIN_VERSION,
   "Converts XY CCs to Omnisphere Orb angle and radius CCs",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(PolarXY)
