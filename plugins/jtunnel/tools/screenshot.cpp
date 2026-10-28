// Renders the panel to a PNG without a host:
// jtunnel-screenshot out.png [width height] [send|receive] [idle|taken|midi]
// It shows a receiver mid-stream on Wi-Fi, with one stall that auto buffer answered, or with
// "send", a sender with two receivers. "idle" shows them before anything connects instead: a
// receiver searching without mDNS, a sender set to PCM with nobody listening. "taken" shows a
// sender whose name another sender already has, and "midi" a tunnel set to carry MIDI only. The panel polls status() a few seconds' worth of times
// before the render, so each call here is one 30 ms tick.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "editor.h"
#include "params.h"

namespace {

class DemoHost : public jtunnel::EditorHost {
public:
   DemoHost(bool send, bool idle, bool taken, bool midi) : send_(send), idle_(idle), taken_(taken), midi_(midi) {}
   double paramValue(clap_id id) const override {
      if (id == jtunnel::kParamMode)
         return send_ ? jtunnel::kModeSend : jtunnel::kModeReceive;
      if (id == jtunnel::kParamCodec && idle_)
         return jtunnel::kCodecPcm;
      if (id == jtunnel::kParamCarry && midi_)
         return jtunnel::kCarryMidi;
      if (id == jtunnel::kParamBuffer && midi_)
         return 3.0;
      return id < jtunnel::kParamCount ? jtunnel::kParams[id].def : 0.0;
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   std::string sendName() const override { return "Studio Mac 1"; }
   void setSendName(const std::string &) override {}
   std::string listenTarget() const override { return "Drums"; }
   void setListenTarget(const std::string &) override {}
   std::vector<jtunnel::TunnelEntry> tunnels() const override {
      if (idle_)
         return {};
      return {{"Drums", "192.168.1.31:47000", false},
              {"Keys", "192.168.1.31:47001", false},
              {"Vocal Mic", "192.168.1.44:47000", false},
              {"Bass DI", "127.0.0.1:47002", true}};
   }
   jtunnel::TunnelStatus status() override {
      const int t = tick_++;
      // A beat's worth of peaks every half second, decaying, the downbeat loudest.
      const int bar = t / 16;
      const float accents[] = {1.0f, 0.3f, 0.6f, 0.22f, 0.85f, 0.3f, 0.5f, 0.4f};
      const float accent = accents[bar % 8];
      const float beat = accent * static_cast<float>(std::exp(-(t % 16) * 0.22));
      const float wobble = static_cast<float>(std::sin(t * 0.31) * 0.5 + std::sin(t * 0.083) * 0.5);
      jtunnel::TunnelStatus s;
      // A chord on each downbeat, and its release.
      static const uint8_t kChord[] = {48, 55, 64, 67};
      if (!idle_) {
         s.midiEvents = static_cast<uint32_t>(bar * 8 + std::min(t % 16, 4) + (t % 16 >= 12 ? 4 : 0));
         s.lastMidi = 0x90u | static_cast<uint32_t>(kChord[std::min(t % 16, 3)]) << 8 | (t % 16 >= 12 ? 0u : 96u << 16);
      }
      s.active = true;
      s.linkUp = true;
      s.hostRate = 48000;
      s.localAddress = send_ ? "192.168.1.31:47000" : "192.168.1.20:47000";
      if (idle_) {
         s.discoveryOnNetwork = send_;
         s.connection = send_ ? 0 : 1;
         s.inputPeak = send_ ? 0.03f + 0.8f * beat : 0.0f;
         s.outputPeak = s.inputPeak;
         return s;
      }
      if (send_ && taken_) {
         s.nameTakenBy = "192.168.1.44:47000";
         s.inputPeak = 0.03f + 0.8f * beat;
         s.outputPeak = s.inputPeak;
         return s;
      }
      if (send_) {
         s.listeners = 2;
         s.inputPeak = 0.03f + 0.8f * beat + 0.02f * wobble;
         s.outputPeak = s.inputPeak;
         return s;
      }
      s.connection = 3;
      s.source = "192.168.1.31:47000";
      s.state = 2;
      // A Wi-Fi stall about half way through: the lead drains, the late frames are concealed, and
      // auto buffer raises the target by the stall's length, which the lead then grows back to.
      const int stall = 140;
      s.targetMs = t < stall ? 20.0f : 26.0f;
      float lead = s.targetMs + 1.6f * wobble;
      if (t >= stall - 4 && t < stall + 4)
         lead = std::max(1.0f, 20.0f - (t - stall + 4) * 5.0f);
      else if (t >= stall + 4 && t < stall + 40)
         lead = 4.0f + (s.targetMs - 4.0f) * (1.0f - std::exp(-(t - stall - 4) * 0.12f)) + 1.6f * wobble;
      s.fillMs = lead;
      s.driftPpm = 37.0f;
      s.lost = t > 60 ? 1 : 0;
      s.lost += t > 205 ? 1 : 0;
      s.concealed = t >= stall ? 5 : 0;
      s.sampleRate = 48000;
      s.codec = midi_ ? 2 : 0;
      s.inputPeak = 0.0f;
      s.outputPeak = midi_ ? 0.0f : 0.25f + 0.45f * beat + 0.04f * wobble;
      if (midi_) {
         s.targetMs = 3.0f;
         s.fillMs = 3.0f + 0.3f * wobble;
         s.concealed = 0;
         s.lost = 0;
      }
      return s;
   }

private:
   bool send_, idle_, taken_, midi_;
   int tick_ = 0;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height] [send|receive] [idle|taken|midi]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 720;
   const int height = argc > 3 ? std::atoi(argv[3]) : 556;
   const std::string variant = argc > 5 ? argv[5] : "";
   DemoHost host(argc > 4 && std::string(argv[4]) == "send", variant == "idle", variant == "taken", variant == "midi");
   return jtunnel::renderScreenshot(host, width, height, argv[1]) ? 0 : 1;
}
