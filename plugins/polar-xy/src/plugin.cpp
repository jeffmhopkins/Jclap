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

#include "editor.h"
#include "params.h"
#include "polar.h"

namespace {

using namespace polar_xy;

struct ChannelState {
   // The last X/Y seen. Nothing is sent until both have arrived: the other axis isn't known.
   int x = 64, y = 64;
   bool hasX = false, hasY = false;
   // The angle held at dead center, and what was last sent on the output CCs (-1: send again).
   int angle = -1;
   int sentAngle = -1, sentRadius = -1;
};

// What the conversion depends on, to notice when a setting changes.
struct Settings {
   int outAngle = -1, outRadius = -1, direction = 0, radiusMode = 0;
   double angleOffset = 0.0;
   bool operator==(const Settings &o) const {
      return outAngle == o.outAngle && outRadius == o.outRadius && direction == o.direction &&
             radiusMode == o.radiusMode && angleOffset == o.angleOffset;
   }
};

// State layout: 4-byte magic, uint32 version, then one double per parameter.
constexpr char kStateMagic[4] = {'J', 'C', 'O', 'P'};
constexpr uint32_t kStateVersion = 1;

struct StateBlob {
   char magic[4];
   uint32_t version;
   double values[kParamCount];
};

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
      gui_.init(host());
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   // A reset is not a controller move: keep the X/Y last seen, and only send them again with the next.
   bool activate(double, uint32_t, uint32_t) override {
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
      StateBlob blob{};
      std::memcpy(blob.magic, kStateMagic, sizeof(kStateMagic));
      blob.version = kStateVersion;
      for (uint32_t i = 0; i < kParamCount; ++i)
         blob.values[i] = params_[i].load();
      return jclap::core::writeAll(stream, &blob, sizeof(blob));
   }

   bool stateLoad(const clap_istream_t *stream) override {
      StateBlob blob{};
      if (!jclap::core::readAll(stream, &blob, sizeof(blob)))
         return false;
      if (std::memcmp(blob.magic, kStateMagic, sizeof(kStateMagic)) || blob.version != kStateVersion)
         return false;
      for (uint32_t i = 0; i < kParamCount; ++i)
         if (!std::isfinite(blob.values[i]))
            return false;

      for (uint32_t i = 0; i < kParamCount; ++i)
         params_[i].store(std::clamp(blob.values[i], kParams[i].min, kParams[i].max));

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
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         // X and Y moving at the same moment are one move: a channel converts once its events at that
         // time are all in, so no point made of the new X and the old Y goes out between them.
         if (h->time != pendingTime_)
            convertPending(out);
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
      for (auto &c : channels_)
         c.sentAngle = c.sentRadius = -1;
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
      applied_ = now;
      for (int channel = 0; channel < 16; ++channel) {
         ChannelState &st = channels_[channel];
         if (moved)
            st.sentAngle = st.sentRadius = -1;
         convert(out, time, channel);
      }
   }

   // Sends a channel's position as angle and radius, each only if it changed.
   void convert(const clap_output_events_t *out, uint32_t time, int channel) {
      ChannelState &st = channels_[channel];
      if (!st.hasX || !st.hasY || conflicting())
         return;
      jclap::PolarSettings s;
      s.angleOffsetDeg = applied_.angleOffset;
      s.clockwise = applied_.direction != 0;
      s.radiusMode = static_cast<jclap::RadiusMode>(applied_.radiusMode);
      const jclap::PolarCC p = jclap::toPolar(st.x, st.y, s);

      // At dead center the angle is undefined, so it holds its last value.
      if (p.hasAngle)
         st.angle = p.angle;
      if (st.angle >= 0 && st.angle != st.sentAngle) {
         pushCC(out, time, channel, applied_.outAngle, st.angle);
         st.sentAngle = st.angle;
      }
      if (p.radius != st.sentRadius) {
         pushCC(out, time, channel, applied_.outRadius, p.radius);
         st.sentRadius = p.radius;
      }
      if (channel == panelChannel_ && st.sentAngle >= 0)
         lastOut_.store(st.sentAngle << 8 | st.sentRadius);
   }

   void pushCC(const clap_output_events_t *out, uint32_t time, int channel, int cc, int value) {
      jclap::core::pushMidi(out, time, static_cast<uint8_t>(0xB0 | channel), static_cast<uint8_t>(cc),
                            static_cast<uint8_t>(value));
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
      if (cc == intParam(kParamInX)) {
         st.x = value;
         st.hasX = true;
      } else if (cc == intParam(kParamInY)) {
         st.y = value;
         st.hasY = true;
      } else {
         return false;
      }
      // The panel follows the channel that moved last, as one tear-free value.
      if (channel != panelChannel_) {
         panelChannel_ = channel;
         lastOut_.store(st.sentAngle >= 0 ? st.sentAngle << 8 | st.sentRadius : -1);
      }
      lastIn_.store(static_cast<uint32_t>(channel) << 16 | static_cast<uint32_t>(st.x) << 8 |
                    static_cast<uint32_t>(st.y));

      const bool passThrough = intParam(kParamPassThrough) != 0;
      if (passThrough)
         out->try_push(out, &ev->header);

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
   // For the panel: the channel that moved last, its X/Y (channel << 16 | x << 8 | y) and what it
   // last sent (angle << 8 | radius, or -1 for nothing yet).
   int panelChannel_ = 0;
   std::atomic<uint32_t> lastIn_{64u << 8 | 64u};
   std::atomic<int32_t> lastOut_{-1};
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
   "0.10.1",
   "Converts XY CCs to Omnisphere Orb angle and radius CCs",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(PolarXY)
