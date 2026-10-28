// One JTunnel instance's connection to the network: a UDP socket and two threads. The send thread
// sleeps until the audio thread posts packets, then unicasts each to every subscribed listener.
// The receive thread takes in audio (into the ring), subscriptions and goodbyes, and twice a second
// keeps the instance's own subscription and its mDNS announcement up to date.
#pragma once

#include <jclap/core/spsc_queue.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net.h"
#include "protocol.h"
#include "receive_ring.h"

namespace jtunnel {

struct OutPacket {
   uint16_t size = 0;
   uint8_t data[kMaxPacketBytes];
};

enum class Connection : int {
   None = 0,      // receiving from nothing
   Looking = 1,   // the tunnel isn't on the network (yet)
   Waiting = 2,   // asked for it, nothing back yet
   Receiving = 3, // audio is arriving
};

// Senders listen here first (or the next free port), so a typed address rarely needs a port.
constexpr uint16_t kDefaultPort = 47000;

// What the plugin and its link share. The audio thread only touches `outgoing`, `outgoingReady`,
// `ring` and the atomics.
struct LinkState {
   jclap::core::SpscQueue<OutPacket, 128> outgoing;
   net::Semaphore outgoingReady;
   ReceiveRing ring;
   std::atomic<bool> receiving{false}; // the mode: receive (true) or send

   // Set from the main thread, read by the link.
   std::mutex mutex;
   std::string sendName;
   std::string listenTarget; // a tunnel name, or an address someone typed

   // Written by the link.
   std::atomic<int> listeners{0};
   std::atomic<uint16_t> port{0};
   std::atomic<int> connection{0};
   std::atomic<uint32_t> source{0}; // the IP audio comes from
   std::atomic<uint16_t> sourcePort{0};
};

class Link {
public:
   Link() = default;
   ~Link() { stop(); }
   Link(const Link &) = delete;
   Link &operator=(const Link &) = delete;

   // Main thread. Opens the socket and starts the threads; false if no socket could be opened.
   bool start(LinkState &state, uint64_t owner);
   void stop();
   bool running() const { return running_; }

private:
   using Clock = std::chrono::steady_clock;
   struct Subscriber {
      net::Address address;
      Clock::time_point lastSeen;
   };

   void receiveLoop();
   void sendLoop();
   void handle(const uint8_t *data, int size, const net::Address &from, Clock::time_point now);
   void housekeeping(Clock::time_point now);
   void dropTarget();

   LinkState *state_ = nullptr;
   uint64_t owner_ = 0;
   net::UdpSocket socket_;
   std::thread receiveThread_, sendThread_;
   std::atomic<bool> running_{false};
   bool discoveryHeld_ = false;

   std::mutex subscribersMutex_;
   std::vector<Subscriber> subscribers_;

   // Receive thread only.
   bool receiving_ = false;
   std::string sendName_, listenTarget_;
   std::string resolvedFor_;
   net::Address target_;
   Clock::time_point lastAudio_{}, lastSubscribe_{}, lastResolve_{};
   std::atomic<bool> published_{false};
};

} // namespace jtunnel
