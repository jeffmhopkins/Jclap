// Finds tunnels on the network with mDNS / DNS-SD (the same multicast group Bonjour uses, which
// Wi-Fi access points pass even when they filter other multicast), as service type
// _jtunnel._udp.local. One per process, shared by every JTunnel instance in it, with one thread.
//
// Senders publish "<tunnel name>._jtunnel._udp.local." with their port. Instances in the same
// process also see each other directly, without the network, so two tracks in one DAW work even
// where multicast is blocked. Receivers take a tunnel's address from where its announcement came
// from.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net.h"

namespace jtunnel {

struct FoundTunnel {
   std::string name;
   net::Address address;
   bool local = false; // in this process
};

class Discovery {
public:
   // The shared instance, started by the first user and stopped when the last releases it.
   static Discovery &acquire();
   static void release();
   // The shared instance without changing who holds it, for code that already acquired it.
   static Discovery &instance();

   // Owner ids are per JTunnel instance; one owner publishes at most one tunnel.
   void publish(uint64_t owner, const std::string &name, uint16_t port);
   void unpublish(uint64_t owner);

   // Every tunnel seen recently, local ones first, sorted by name within each.
   std::vector<FoundTunnel> tunnels() const;
   // The address for a tunnel name (case-insensitive), preferring one in this process.
   bool resolve(const std::string &name, net::Address &out) const;
   // False when the mDNS socket couldn't be opened: only tunnels in this process can be found.
   bool networkAvailable() const;

   // An id for a new owner.
   static uint64_t newOwnerId();

   struct Impl;

private:
   Discovery() = default;
};

} // namespace jtunnel
