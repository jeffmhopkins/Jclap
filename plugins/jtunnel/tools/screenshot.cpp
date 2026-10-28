// Renders the panel to a PNG without a host: jtunnel-screenshot out.png [width height] [send]
// It shows a receiver mid-stream, or with "send", a sender with two listeners.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "editor.h"
#include "params.h"

namespace {

class DemoHost : public jtunnel::EditorHost {
public:
   explicit DemoHost(bool send) : send_(send) {}
   double paramValue(clap_id id) const override {
      if (id == jtunnel::kParamMode)
         return send_ ? jtunnel::kModeSend : jtunnel::kModeReceive;
      return id < jtunnel::kParamCount ? jtunnel::kParams[id].def : 0.0;
   }
   void beginEdit(clap_id) override {}
   void setParam(clap_id, double) override {}
   void endEdit(clap_id) override {}
   std::string sendName() const override { return "Studio Mac 1"; }
   void setSendName(const std::string &) override {}
   std::string listenTarget() const override { return "Drums"; }
   void setListenTarget(const std::string &) override {}
   std::vector<std::string> tunnelNames() const override { return {"Drums", "Keys", "Vocal Mic"}; }
   jtunnel::TunnelStatus status() override {
      jtunnel::TunnelStatus s;
      s.active = true;
      s.linkUp = true;
      s.localAddress = "192.168.1.20:47000";
      if (send_) {
         s.listeners = 2;
         s.inputPeak = 0.6f;
         s.outputPeak = 0.6f;
         return s;
      }
      s.connection = 3;
      s.source = "192.168.1.31:47000";
      s.state = 2;
      s.fillMs = 23.0f;
      s.targetMs = 20.0f;
      s.driftPpm = 37.0f;
      s.lost = 2;
      s.concealed = 5;
      s.sampleRate = 48000;
      s.codec = 0;
      s.inputPeak = 0.0f;
      s.outputPeak = 0.5f;
      return s;
   }

private:
   bool send_;
};

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s out.png [width height]\n", argv[0]);
      return 1;
   }
   const int width = argc > 3 ? std::atoi(argv[2]) : 720;
   const int height = argc > 3 ? std::atoi(argv[3]) : 440;
   DemoHost host(argc > 4 && std::string(argv[4]) == "send");
   return jtunnel::renderScreenshot(host, width, height, argv[1]) ? 0 : 1;
}
