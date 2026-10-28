// JTunnel: sends a track's audio and MIDI to other JTunnel instances on the network, or plays what
// one of them sends. One instance is a sender (it publishes a named tunnel) or a receiver (it
// listens to one by name, or at a typed address).
//
// The audio thread never touches the network. A sender codes frames and queues packets for its
// link's send thread; a receiver plays frames its link's receive thread has stored in a ring. See
// link.h, sender.h and receiver.h.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "discovery.h"
#include "editor.h"
#include "link.h"
#include "net.h"
#include "params.h"
#include "receiver.h"
#include "sender.h"

namespace {

using namespace jtunnel;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

// State layout: 4-byte magic, uint32 version, uint32 param count, one double per param, then the
// send name and the listen target, each a length byte and that many bytes.
constexpr char kStateMagic[4] = {'J', 'T', 'U', 'N'};
constexpr uint32_t kStateVersion = 1;
constexpr size_t kMaxTarget = 63;

constexpr int kMaxMidi = 512;

// A default name nobody else on the network is likely to have: this computer's, numbered for each
// instance in the process.
std::string defaultSendName() {
   static std::atomic<int> count{0};
   char wanted[64];
   std::snprintf(wanted, sizeof(wanted), "%.24s %d", net::hostName().c_str(), ++count);
   char clean[kMaxNameLength + 1];
   sanitizeName(wanted, clean, sizeof(clean));
   return clean[0] ? clean : "JTunnel";
}

// What the panel can type into the listen box: a tunnel name, or an address (adds . and :).
std::string cleanTarget(const std::string &text) {
   std::string out;
   for (const char ch : text) {
      const unsigned char c = static_cast<unsigned char>(ch);
      if ((std::isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.' || c == ':') && out.size() < kMaxTarget)
         out += ch;
   }
   while (!out.empty() && out.back() == ' ')
      out.pop_back();
   while (!out.empty() && out.front() == ' ')
      out.erase(out.begin());
   return out;
}

class QueueSink final : public PacketSink {
public:
   explicit QueueSink(LinkState &state) : state_(state) {}
   void send(const uint8_t *data, size_t size) override {
      OutPacket packet;
      packet.size = static_cast<uint16_t>(size);
      std::memcpy(packet.data, data, size);
      // A full queue means the send thread is stuck; dropping is better than waiting.
      sent_ = state_.outgoing.push(packet) || sent_;
   }
   bool sent() const { return sent_; }

private:
   LinkState &state_;
   bool sent_ = false;
};

class JTunnel final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit JTunnel(const clap_host_t *host) : Plugin(&descriptor, host) {}

   bool init() override {
      for (uint32_t i = 0; i < kParamCount; ++i)
         params_[i].store(kParams[i].def);
      {
         std::lock_guard<std::mutex> lock(link_.mutex);
         link_.sendName = defaultSendName();
      }
      owner_ = Discovery::newOwnerId();
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(host());
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   bool activate(double sampleRate, uint32_t, uint32_t maxFrames) override {
      sampleRate_ = sampleRate;
      if (!sender_.setup(sampleRate, static_cast<uint32_t>(owner_ * 2654435761u)) || !receiver_.setup(sampleRate))
         return false;
      thruL_.assign(maxFrames, 0.0f);
      thruR_.assign(maxFrames, 0.0f);
      silence_.assign(maxFrames, 0.0f);
      scratch_.assign(maxFrames * 2, 0.0f);
      sending_ = false;
      appliedMode_ = -1;
      link_.receiving = mode() == kModeReceive;
      // No socket isn't fatal: the plugin still passes audio through, and the panel says why.
      link_.ring.clear();
      impair_ = ImpairSettings::fromEnvironment();
      linkUp_ = network_.start(link_, owner_, impair_);
      active_ = true;
      return true;
   }

   void deactivate() override {
      network_.stop();
      linkUp_ = false;
      active_ = false;
   }

   void reset() override {
      sender_.restart();
      receiver_.restart();
   }

   const void *extension(const char *id) override {
      if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
         return &audioPortsExtension;
      if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
         return &notePortsExtension;
      if (!std::strcmp(id, CLAP_EXT_PARAMS))
         return &paramsExtension;
      if (!std::strcmp(id, CLAP_EXT_STATE))
         return &stateExtension;
      if (!std::strcmp(id, CLAP_EXT_RENDER))
         return &renderExtension;
      return jclap::ui::ClapGui::extensionFor<JTunnel, &JTunnel::gui_>(id);
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

   std::string sendName() const override {
      std::lock_guard<std::mutex> lock(link_.mutex);
      return link_.sendName;
   }
   void setSendName(const std::string &name) override {
      char clean[kMaxNameLength + 1];
      sanitizeName(name.c_str(), clean, sizeof(clean));
      std::lock_guard<std::mutex> lock(link_.mutex);
      link_.sendName = clean;
      markDirty();
   }
   std::string listenTarget() const override {
      std::lock_guard<std::mutex> lock(link_.mutex);
      return link_.listenTarget;
   }
   void setListenTarget(const std::string &target) override {
      std::lock_guard<std::mutex> lock(link_.mutex);
      link_.listenTarget = cleanTarget(target);
      markDirty();
   }
   std::vector<TunnelEntry> tunnels() const override {
      std::vector<TunnelEntry> found;
      const std::string own = mode() == kModeSend ? sendName() : std::string();
      for (const FoundTunnel &t : Discovery::instance().tunnels())
         if (t.name != own && std::none_of(found.begin(), found.end(), [&](const TunnelEntry &e) { return e.name == t.name; }))
            found.push_back({t.name, t.address.toString(), t.local});
      return found;
   }
   TunnelStatus status() override {
      TunnelStatus s;
      s.active = active_;
      s.linkUp = linkUp_;
      s.discoveryOnNetwork = Discovery::instance().networkAvailable();
      const auto now = std::chrono::steady_clock::now();
      if (now - localIpCheckedAt_ > std::chrono::seconds(3)) {
         localIp_ = net::localIp();
         localIpCheckedAt_ = now;
      }
      const uint16_t port = link_.port.load();
      if (port)
         s.localAddress = net::Address{localIp_ ? localIp_ : 0x7F000001u, port}.toString();
      s.hostRate = sampleRate_;
      if (active_ && impair_.enabled())
         s.impairment = impair_.describe();
      s.listeners = link_.listeners.load();
      if (mode() == kModeSend)
         Discovery::instance().nameTaken(owner_, s.nameTakenBy);
      s.connection = link_.connection.load();
      const net::Address source{link_.source.load(), link_.sourcePort.load()};
      if (source.valid())
         s.source = source.toString();
      const ReceiverStats &r = receiver_.stats();
      s.state = r.state.load();
      s.fillMs = r.fillMs.load();
      s.targetMs = r.targetMs.load();
      s.driftPpm = r.driftPpm.load();
      s.lost = r.lost.load();
      s.concealed = r.concealed.load();
      s.sampleRate = r.sampleRate.load();
      s.codec = r.codec.load();
      s.midiEvents = midiEvents_.load(std::memory_order_relaxed);
      s.lastMidi = lastMidi_.load(std::memory_order_relaxed);
      s.inputPeak = inPeak_.exchange(0.0f);
      s.outputPeak = outPeak_.exchange(0.0f);
      return s;
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
      std::vector<uint8_t> bytes(kStateMagic, kStateMagic + 4);
      const auto put32 = [&](uint32_t v) {
         for (int i = 0; i < 4; ++i)
            bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
      };
      put32(kStateVersion);
      put32(kParamCount);
      for (uint32_t i = 0; i < kParamCount; ++i) {
         const double v = params_[i].load();
         uint8_t raw[8];
         std::memcpy(raw, &v, 8);
         bytes.insert(bytes.end(), raw, raw + 8);
      }
      const auto putText = [&](const std::string &text, size_t max) {
         const size_t n = std::min(text.size(), max);
         bytes.push_back(static_cast<uint8_t>(n));
         bytes.insert(bytes.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(n));
      };
      {
         std::lock_guard<std::mutex> lock(link_.mutex);
         putText(link_.sendName, kMaxNameLength);
         putText(link_.listenTarget, kMaxTarget);
      }
      return jclap::core::writeAll(stream, bytes.data(), static_cast<int64_t>(bytes.size()));
   }

   bool stateLoad(const clap_istream_t *stream) override {
      uint8_t head[12];
      if (!jclap::core::readAll(stream, head, sizeof(head)) || std::memcmp(head, kStateMagic, 4))
         return false;
      const auto get32 = [](const uint8_t *p) {
         return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
                static_cast<uint32_t>(p[3]) << 24;
      };
      // Params are only ever added at the end: a project saved before one existed gets its default.
      const uint32_t saved = get32(head + 8);
      if (get32(head + 4) != kStateVersion || saved < kParamCarry || saved > kParamCount)
         return false;
      double values[kParamCount];
      for (uint32_t i = 0; i < kParamCount; ++i)
         values[i] = kParams[i].def;
      if (!jclap::core::readAll(stream, values, saved * sizeof(double)))
         return false;
      for (double v : values)
         if (!std::isfinite(v))
            return false;
      std::string texts[2];
      for (std::string &text : texts) {
         uint8_t n = 0;
         if (!jclap::core::readAll(stream, &n, 1))
            return false;
         text.resize(n);
         if (n && !jclap::core::readAll(stream, &text[0], n))
            return false;
      }

      for (uint32_t i = 0; i < kParamCount; ++i)
         params_[i].store(std::clamp(values[i], kParams[i].min, kParams[i].max));
      char clean[kMaxNameLength + 1];
      sanitizeName(texts[0].c_str(), clean, sizeof(clean));
      {
         std::lock_guard<std::mutex> lock(link_.mutex);
         if (clean[0])
            link_.sendName = clean;
         link_.listenTarget = cleanTarget(texts[1]);
      }
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      const clap_input_events_t *in = proc->in_events;
      const clap_output_events_t *out = proc->out_events;
      edits_.drain(out);
      const uint32_t frames = proc->frames_count;

      // Params apply for the whole block (nothing here needs them sample-accurate). Notes and MIDI
      // are both sent on and, as a note effect would, passed through.
      midiInCount_ = 0;
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;
         if (h->type == CLAP_EVENT_PARAM_VALUE)
            applyParam(reinterpret_cast<const clap_event_param_value_t *>(h));
         else
            collectNote(h);
      }

      // Audio buffers, standing in silence for a missing input and scratch for a missing output.
      const float *inL = silence_.data(), *inR = silence_.data();
      float *outL = scratch_.data(), *outR = scratch_.data() + frames;
      if (proc->audio_inputs_count > 0 && proc->audio_inputs[0].data32 && proc->audio_inputs[0].channel_count > 0) {
         const clap_audio_buffer_t &b = proc->audio_inputs[0];
         inL = b.data32[0];
         inR = b.data32[b.channel_count > 1 ? 1 : 0];
      }
      const bool hasOut =
         proc->audio_outputs_count > 0 && proc->audio_outputs[0].data32 && proc->audio_outputs[0].channel_count > 0;
      if (hasOut) {
         const clap_audio_buffer_t &b = proc->audio_outputs[0];
         outL = b.data32[0];
         outR = b.data32[b.channel_count > 1 ? 1 : 0];
      }
      if (frames > thruL_.size()) {
         // Hosts must not exceed max_frames from activate(); stay safe anyway.
         jclap::core::passAudioThrough(proc);
         return CLAP_PROCESS_CONTINUE;
      }
      inPeak_.store(std::max(inPeak_.load(std::memory_order_relaxed), peak(inL, inR, frames)), std::memory_order_relaxed);

      const int m = mode();
      if (m != appliedMode_) {
         appliedMode_ = m;
         link_.receiving = m == kModeReceive;
         sender_.restart();
         receiver_.restart();
         sending_ = false;
      }
      const bool thru = params_[kParamThru].load() >= 0.5;
      const int carry = intParam(kParamCarry);

      if (m == kModeSend) {
         if (link_.listeners.load(std::memory_order_relaxed) > 0) {
            // Someone new may be listening: start a fresh stream for them.
            if (!sending_)
               sender_.restart();
            sending_ = true;
            const Codec codec = !carriesAudio(carry)          ? Codec::None
                                : intParam(kParamCodec) == kCodecPcm ? Codec::Pcm24
                                                                     : Codec::Opus;
            sender_.configure(codec, intParam(kParamBitrate), params_[kParamRedundancy].load() >= 0.5);
            QueueSink sink(link_);
            sender_.process(inL, inR, frames, midiIn_, carriesMidi(carry) ? midiInCount_ : 0, sink);
            if (sink.sent())
               link_.outgoingReady.post();
         }
         else
            sending_ = false;
         if (midiInCount_ > 0)
            noteMidi(midiIn_[midiInCount_ - 1].data, midiInCount_);
         for (uint32_t i = 0; i < frames; ++i) {
            const float l = inL[i], r = inR[i];
            outL[i] = thru ? l : 0.0f;
            outR[i] = thru ? r : 0.0f;
         }
         pushMerged(in, out, 0, frames);
      }
      else {
         if (thru) {
            std::copy(inL, inL + frames, thruL_.begin());
            std::copy(inR, inR + frames, thruR_.begin());
         }
         receiver_.setBuffer(params_[kParamBuffer].load(), params_[kParamAutoBuffer].load() >= 0.5);
         int received = receiver_.process(
            link_.ring, outL, outR, frames, midiOut_, kMaxMidi,
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
               .count());
         if (received > 0)
            noteMidi(midiOut_[received - 1].data, received);
         if (!carriesMidi(carry))
            received = 0;
         const double levelDb = params_[kParamLevel].load();
         const float gain = levelDb <= kParams[kParamLevel].min || !carriesAudio(carry)
                               ? 0.0f
                               : static_cast<float>(std::pow(10.0, levelDb / 20.0));
         for (uint32_t i = 0; i < frames; ++i) {
            outL[i] = outL[i] * gain + (thru ? thruL_[i] : 0.0f);
            outR[i] = outR[i] * gain + (thru ? thruR_[i] : 0.0f);
         }
         pushMerged(in, out, received, frames);
      }
      outPeak_.store(std::max(outPeak_.load(std::memory_order_relaxed), peak(outL, outR, frames)),
                     std::memory_order_relaxed);
      if (hasOut)
         proc->audio_outputs[0].constant_mask = 0;
      return CLAP_PROCESS_CONTINUE;
   }

private:
   static const clap_plugin_render_t renderExtension;

   int mode() const { return intParam(kParamMode); }
   int intParam(ParamId id) const { return static_cast<int>(params_[id].load() + 0.5); }

   void markDirty() {
      if (auto *state = hostExtension<clap_host_state_t>(CLAP_EXT_STATE))
         state->mark_dirty(host());
   }

   static float peak(const float *l, const float *r, uint32_t frames) {
      float p = 0.0f;
      for (uint32_t i = 0; i < frames; ++i)
         p = std::max(p, std::max(std::abs(l[i]), std::abs(r[i])));
      return p;
   }

   // For the panel's MIDI light: how many events went through, and the last of them.
   void noteMidi(const uint8_t *last, int count) {
      midiEvents_.fetch_add(static_cast<uint32_t>(count), std::memory_order_relaxed);
      lastMidi_.store(static_cast<uint32_t>(last[0]) | static_cast<uint32_t>(last[1]) << 8 |
                         static_cast<uint32_t>(last[2]) << 16,
                      std::memory_order_relaxed);
   }

   void applyParam(const clap_event_param_value_t *ev) {
      if (ev->param_id < kParamCount && std::isfinite(ev->value))
         params_[ev->param_id].store(std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max));
   }

   // Notes go on the wire as MIDI (pushMerged passes the originals through).
   void collectNote(const clap_event_header_t *h) {
      if (!jclap::core::isNoteEvent(h))
         return;
      TimedMidi m{h->time, {0, 0, 0}};
      bool wire = true;
      switch (h->type) {
      case CLAP_EVENT_NOTE_ON:
      case CLAP_EVENT_NOTE_OFF:
      case CLAP_EVENT_NOTE_CHOKE: {
         const auto *note = reinterpret_cast<const clap_event_note_t *>(h);
         if (note->key < 0 || note->key > 127) {
            wire = false;
            break;
         }
         const int channel = note->channel >= 0 && note->channel < 16 ? note->channel : 0;
         const bool on = h->type == CLAP_EVENT_NOTE_ON;
         const int velocity = static_cast<int>(std::lround(std::clamp(note->velocity, 0.0, 1.0) * 127.0));
         m.data[0] = static_cast<uint8_t>((on ? 0x90 : 0x80) | channel);
         m.data[1] = static_cast<uint8_t>(note->key);
         m.data[2] = static_cast<uint8_t>(on ? std::max(1, velocity) : velocity);
         break;
      }
      case CLAP_EVENT_MIDI: {
         const auto *midi = reinterpret_cast<const clap_event_midi_t *>(h);
         std::memcpy(m.data, midi->data, 3);
         // Only channel messages make sense on the far side.
         wire = midi->data[0] >= 0x80 && midi->data[0] < 0xF0;
         break;
      }
      default:
         wire = false;
      }
      if (wire && midiInCount_ < kMaxMidi)
         midiIn_[midiInCount_++] = m;
   }

   // The input's notes and MIDI, merged in time order with what was received.
   void pushMerged(const clap_input_events_t *in, const clap_output_events_t *out, int received, uint32_t frames) {
      const uint32_t n = in->size(in);
      int r = 0;
      for (uint32_t i = 0; i <= n; ++i) {
         const clap_event_header_t *h = i < n ? in->get(in, i) : nullptr;
         if (h && (h->space_id != CLAP_CORE_EVENT_SPACE_ID || !jclap::core::isNoteEvent(h)))
            continue;
         const uint32_t until = h ? h->time : frames;
         for (; r < received && midiOut_[r].time <= until; ++r)
            jclap::core::pushMidi(out, midiOut_[r].time, midiOut_[r].data[0], midiOut_[r].data[1], midiOut_[r].data[2]);
         if (h)
            out->try_push(out, h);
      }
      for (; r < received; ++r)
         jclap::core::pushMidi(out, midiOut_[r].time, midiOut_[r].data[0], midiOut_[r].data[1], midiOut_[r].data[2]);
   }

   std::atomic<double> params_[kParamCount];
   double sampleRate_ = 48000.0;
   uint64_t owner_ = 0;
   std::atomic<bool> active_{false}, linkUp_{false};

   mutable LinkState link_;
   Link network_;
   Sender sender_;
   Receiver receiver_;
   int appliedMode_ = -1;
   bool sending_ = false;

   std::vector<float> thruL_, thruR_, silence_, scratch_;
   TimedMidi midiIn_[kMaxMidi];
   int midiInCount_ = 0;
   OutMidi midiOut_[kMaxMidi];
   std::atomic<float> inPeak_{0.0f}, outPeak_{0.0f};
   std::atomic<uint32_t> midiEvents_{0}, lastMidi_{0};

   ImpairSettings impair_;
   uint32_t localIp_ = 0;
   std::chrono::steady_clock::time_point localIpCheckedAt_{};

   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;
   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_render_t JTunnel::renderExtension = {
   // A stream runs in real time; an offline bounce would send it faster than anyone can play it.
   [](const clap_plugin_t *) { return true; },
   [](const clap_plugin_t *, clap_plugin_render_mode mode) { return mode == CLAP_RENDER_REALTIME; },
};

const clap_plugin_descriptor_t JTunnel::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.jtunnel",
   "JTunnel",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   "0.10.6",
   "Sends audio and MIDI to other JTunnel instances on the network, or plays what they send",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(JTunnel)
