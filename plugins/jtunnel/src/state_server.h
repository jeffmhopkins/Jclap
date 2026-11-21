// Serves an instance's side-channel state (state.h) to programs on this computer: a streaming app,
// a browser source, a script. It listens on 127.0.0.1 only, on the first free port of 47300 to
// 47315, so nothing on the network can reach it.
//
//   GET /            a viewer page that draws the clip grid, the transport and the device
//   GET /state       every key's message, as one JSON object
//   GET /state/KEY   one key's message (404 if there's none)
//   GET /ws          a WebSocket: {"type":"snapshot","state":{...}} on connecting (and again
//                    whenever the state starts over), then {"type":"update","key":K,"value":{...}}
//                    for every change
//
// HTTP answers carry Access-Control-Allow-Origin: *, so a page from anywhere can fetch them; a
// request whose Host isn't this computer (a DNS rebinding trick) is refused.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "net.h"
#include "state.h"

namespace jtunnel {

class StateServer {
public:
   static constexpr uint16_t kFirstPort = 47300;
   static constexpr int kPortCount = 16;

   StateServer() = default;
   ~StateServer() { stop(); }
   StateServer(const StateServer &) = delete;
   StateServer &operator=(const StateServer &) = delete;

   // Listens and starts the server's thread. False if no port was free.
   bool start(StateStore &store);
   void stop();
   uint16_t port() const { return port_.load(); }
   // WebSocket clients connected now.
   int clients() const { return clients_.load(); }

   // The Sec-WebSocket-Accept answer to a Sec-WebSocket-Key (exposed for the tests).
   static std::string acceptKey(const std::string &key);
   // A WebSocket text frame, as a server sends it (unmasked).
   static std::string textFrame(const std::string &payload);

private:
   void run();

   StateStore *store_ = nullptr;
   std::thread thread_;
   std::atomic<bool> running_{false};
   std::atomic<uint16_t> port_{0};
   std::atomic<int> clients_{0};
   net::TcpSocket listener_;
};

} // namespace jtunnel
