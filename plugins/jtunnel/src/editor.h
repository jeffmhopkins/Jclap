#pragma once

// The plugin's panel. Kept behind this small interface so plugin.cpp never includes Visage.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <jclap/ui/params.h>
#include <jclap/ui/window.h>

namespace jtunnel {

// A snapshot of what the plugin and its network link are doing, for the panel.
struct TunnelStatus {
   bool active = false;          // the host has the plugin running
   bool linkUp = false;          // it has a socket
   bool discoveryOnNetwork = true; // mDNS works (else only this process is searched)
   std::string localAddress;     // where others can reach this instance, "192.168.1.5:47000"
   double hostRate = 0;          // the host's sample rate (what PCM sends at)
   std::string impairment;       // what JTUNNEL_IMPAIR is doing to sent packets, if anything
   // Sending.
   int listeners = 0;
   // Receiving.
   int connection = 0;           // Connection: 0 none, 1 looking, 2 waiting, 3 receiving
   std::string source;           // the sender's address
   int state = 0;                // ReceiveState: 0 idle, 1 buffering, 2 playing
   float fillMs = 0, targetMs = 0, driftPpm = 0; // driftPpm is NaN while still being measured
   uint32_t lost = 0, concealed = 0, sampleRate = 0;
   int codec = 0;
   // Peak levels since the last call, linear.
   float inputPeak = 0, outputPeak = 0;
};

// A tunnel found on the network.
struct TunnelEntry {
   std::string name;
   std::string address; // "192.168.1.31:47000"
   bool local = false;  // in this process (another track in this DAW)
};

// What the panel needs from the plugin, on the main thread.
class EditorHost : public jclap::ui::ParamAccess {
public:
   virtual std::string sendName() const = 0;
   virtual void setSendName(const std::string &name) = 0;
   // A tunnel name or a typed address.
   virtual std::string listenTarget() const = 0;
   virtual void setListenTarget(const std::string &target) = 0;
   // Tunnels on the network now, one per name.
   virtual std::vector<TunnelEntry> tunnels() const = 0;
   std::vector<std::string> tunnelNames() const {
      std::vector<std::string> names;
      for (const TunnelEntry &t : tunnels())
         names.push_back(t.name);
      return names;
   }
   virtual TunnelStatus status() = 0;
};

std::unique_ptr<jclap::ui::Window> createEditor(EditorHost &host);

// Draws the panel offscreen at the given pixel size and saves it as a PNG.
bool renderScreenshot(EditorHost &host, int width, int height, const char *pngPath);

} // namespace jtunnel
