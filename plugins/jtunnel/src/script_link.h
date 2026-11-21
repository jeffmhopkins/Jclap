// A sender's link to the JTunnel extension for Bitwig Studio (bitwig/JTunnel.control.js), which
// reads the clip launcher, the transport, the mixer and the selected device and hands them to every
// JTunnel sender on the same computer.
//
// They talk OSC over UDP on loopback, the only way out a Bitwig controller script has. The script
// listens on port 47250. A sender takes the first free port of 47251 to 47266 and says hello from
// it every second (/jtunnel/hello ,is port tunnel); the script then sends it everything it has and
// every change after (/jtunnel/state ,ss key json), and /jtunnel/alive each second. A sender that
// stops saying hello is forgotten after a few seconds.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "net.h"
#include "state.h"

namespace jtunnel {

class ScriptLink {
public:
   static constexpr uint16_t kScriptPort = 47250;
   static constexpr uint16_t kClientPortFirst = 47251;
   static constexpr int kClientPortCount = 16;
   using Clock = std::chrono::steady_clock;

   // Takes a client port. False if all of them are taken.
   bool open();
   void close();
   bool isOpen() const { return socket_.isOpen(); }
   uint16_t port() const { return socket_.localPort(); }

   // Waits up to timeoutMs for messages from the script and puts what they carry in the store, and
   // says hello when it's due. The script's own keys are dropped from the store a while after it
   // goes quiet.
   void poll(StateStore &store, const std::string &tunnelName, int timeoutMs);
   // The script has been heard from lately.
   bool connected() const;
   // Messages taken from the script.
   uint32_t received() const { return received_; }

   // OSC, as little of it as this needs (exposed for the tests).
   static std::string message(const std::string &address, const std::string &tags, const int32_t *ints,
                              const std::string *strings);
   // Splits an OSC message of ints and strings; false for anything else.
   static bool parse(const uint8_t *data, size_t size, std::string &address, std::string &tags,
                     std::vector<int32_t> &ints, std::vector<std::string> &strings);

private:
   void forgetScriptKeys(StateStore &store);

   net::UdpSocket socket_;
   Clock::time_point lastHello_{}, lastHeard_{};
   bool heard_ = false;
   uint32_t received_ = 0;
   std::vector<std::string> keys_; // what the script has put in the store
};

} // namespace jtunnel
